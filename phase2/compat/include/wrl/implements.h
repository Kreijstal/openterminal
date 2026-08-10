#pragma once

// A small, classic-COM subset of WRL's implements.h. Windows Terminal's UI
// Automation providers need RuntimeClass, RuntimeClassFlags, and
// MakeAndInitialize; WinRT activation, aggregation, and FTM support are outside
// this compatibility header's scope.

#include <atomic>
#include <new>
#include <utility>
#include <wrl/client.h>

namespace Microsoft::WRL
{
    enum RuntimeClassType : unsigned int
    {
        WinRt = 0x1,
        ClassicCom = 0x2,
        WinRtClassicComMix = WinRt | ClassicCom,
        InhibitFtmBase = 0x4,
    };

    template<unsigned int flags>
    struct RuntimeClassFlags
    {
        static constexpr unsigned int value = flags;
    };

    namespace Details
    {
        template<typename first, typename... rest>
        struct FirstInterface
        {
            using type = first;
        };
    }

    template<typename flags, typename... interfaces>
    class RuntimeClass : public interfaces...
    {
        static_assert(sizeof...(interfaces) != 0, "RuntimeClass requires a COM interface");

    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) noexcept override
        {
            if (object == nullptr)
            {
                return E_POINTER;
            }
            *object = nullptr;

            using first_interface = typename Details::FirstInterface<interfaces...>::type;
            if (IsEqualIID(iid, IID_IUnknown))
            {
                *object = static_cast<IUnknown*>(static_cast<first_interface*>(this));
            }
            else
            {
                const bool matched = ((IsEqualIID(iid, __uuidof(interfaces)) ?
                                           (*object = static_cast<interfaces*>(this), true) :
                                           false) ||
                                      ...);
                if (!matched)
                {
                    return E_NOINTERFACE;
                }
            }

            AddRef();
            return S_OK;
        }

        ULONG STDMETHODCALLTYPE AddRef() noexcept override
        {
            return ++_references;
        }

        ULONG STDMETHODCALLTYPE Release() noexcept override
        {
            const auto references = --_references;
            if (references == 0)
            {
                delete this;
            }
            return references;
        }

    protected:
        RuntimeClass() noexcept = default;
        virtual ~RuntimeClass() = default;

    private:
        std::atomic<ULONG> _references{ 1 };
    };

    template<typename instance, typename interface_type, typename... arguments>
    HRESULT MakeAndInitialize(interface_type** result, arguments&&... values) noexcept
    {
        if (result == nullptr)
        {
            return E_POINTER;
        }
        *result = nullptr;

        auto object = new (std::nothrow) instance{};
        if (object == nullptr)
        {
            return E_OUTOFMEMORY;
        }

        HRESULT status = E_FAIL;
        try
        {
            status = object->RuntimeClassInitialize(std::forward<arguments>(values)...);
        }
        catch (const std::bad_alloc&)
        {
            status = E_OUTOFMEMORY;
        }
        catch (...)
        {
            status = E_FAIL;
        }

        if (FAILED(status))
        {
            object->Release();
            return status;
        }

        *result = static_cast<interface_type*>(object);
        return S_OK;
    }

    template<typename instance, typename interface_type, typename... arguments>
    HRESULT MakeAndInitialize(
        Details::ComPtrRef<ComPtr<interface_type>> result,
        arguments&&... values) noexcept
    {
        return MakeAndInitialize<instance>(
            result.ReleaseAndGetAddressOf(),
            std::forward<arguments>(values)...);
    }

    template<typename instance, typename... arguments>
    ComPtr<instance> Make(arguments&&... values) noexcept
    {
        ComPtr<instance> result;
        result.Attach(new (std::nothrow) instance(std::forward<arguments>(values)...));
        return result;
    }

    // Terminal registers this factory explicitly with CoRegisterClassObject.
    // This focused implementation therefore only needs ordinary non-aggregated
    // activation and does not depend on WRL's SDK-private module machinery.
    template<typename instance>
    class SimpleClassFactory final :
        public RuntimeClass<RuntimeClassFlags<ClassicCom>, IClassFactory>
    {
    public:
        HRESULT STDMETHODCALLTYPE CreateInstance(
            IUnknown* outer,
            REFIID interfaceId,
            void** object) noexcept override
        {
            if (object == nullptr)
            {
                return E_POINTER;
            }
            *object = nullptr;
            if (outer != nullptr)
            {
                return CLASS_E_NOAGGREGATION;
            }

            const auto value = Make<instance>();
            if (!value)
            {
                return E_OUTOFMEMORY;
            }
            return value->QueryInterface(interfaceId, object);
        }

        HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override
        {
            return S_OK;
        }
    };
}

// CoCreatableClass populates WRL's DLL activation table. The open static build
// omits that DLL-only table; CTerminalHandoff uses SimpleClassFactory directly.
#ifndef CoCreatableClass
#define CoCreatableClass(instance)
#endif
