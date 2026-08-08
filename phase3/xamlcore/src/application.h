// Windows.UI.Xaml.Application -- the first composable class in this DLL.
//
// Terminal does not activate an Application. Its App.idl says
//
//     runtimeclass App : Windows.UI.Xaml.Application
//
// and a WinRT class derives from another by *composing* it: C++/WinRT's
// Application base calls IApplicationFactory::CreateInstance, handing in
// itself as the controlling outer object and taking back a non-delegating
// inner one. The two are a single COM identity afterwards -- every interface
// the inner hands out forwards AddRef, Release and QueryInterface to the
// outer, so `app.as<IApplication3>()` and `app.as<TerminalApp::IApp>()` are
// the same object seen twice.
//
// That is the mechanism this file implements. It is ABI plumbing, not XAML
// semantics: getting it wrong does not produce a wrong number, it produces an
// object whose refcount is split in two and whose QueryInterface loops.
//
// The Application's own behaviour is deliberately thin. What is here is what
// is genuinely specified: the process-wide singleton that Application.Current
// reads, and the properties a caller sets and reads back. Everything else --
// Resources, DebugSettings, the suspend/resume events, Exit -- stays
// E_NOTIMPL, because this runtime has no application resource dictionary, no
// process lifetime manager and nothing to raise those events from.

#ifndef OPENXAML_APPLICATION_H
#define OPENXAML_APPLICATION_H

#include "com.h"
#include "openxaml_abi_stubs.h"

namespace openxaml::winrt {

namespace wux = ABI::Windows::UI::Xaml;

// The composed Application.
//
// Lifetime: the object's own reference count belongs to the inner identity
// alone. Every other pointer it hands out delegates to the outer, which is the
// aggregate. The outer is deliberately *not* AddRef'd -- an inner that held a
// reference on its aggregator would be a cycle neither could break.
class ApplicationObject final : public ComObject,
                                public abi::NotImpl_IApplication,
                                public abi::NotImpl_IApplication2,
                                public abi::NotImpl_IApplication3 {
public:
    // Constructed only by ApplicationFactory::CreateInstance, which is the
    // only caller that can supply a controlling outer.
    ApplicationObject() = default;

    ~ApplicationObject() override {
        if (Current() == this) Current() = nullptr;
    }

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Application";
    }

    // The one application the process has, as Application.Current reads it.
    // The published core keeps exactly this: a raw process-wide pointer set
    // when an Application is constructed and cleared when it dies, with a
    // null return meaning "the runtime is up but no Application was made".
    static ApplicationObject*& Current() {
        static ApplicationObject* current = nullptr;
        return current;
    }

    // --- the aggregation boundary ---------------------------------------------
    //
    // These six are the whole point. Every interface below this line is handed
    // out with an IUnknown that belongs to the aggregate, not to us.

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return outer_->QueryInterface(iid, object);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return outer_->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return outer_->Release(); }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** iids) override {
        return outer_->GetIids(count, iids);
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override {
        return outer_->GetRuntimeClassName(name);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* level) override {
        return outer_->GetTrustLevel(level);
    }

    // The non-delegating identity, and the only pointer whose IUnknown counts
    // this object. It resolves the composed interfaces itself rather than
    // asking the outer to: forwarding a QueryInterface back to the aggregator
    // that just forwarded it here is how an aggregation deadlocks.
    class Inner final : public IInspectable {
    public:
        explicit Inner(ApplicationObject* owner) : owner_(owner) {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
            if (!object) return E_POINTER;
            if (IsEqualGUID(iid, IID_IUnknown) ||
                IsEqualGUID(iid, ::openxaml::iid::IInspectable)) {
                *object = static_cast<IInspectable*>(this);
                AddRef();
                return S_OK;
            }
            return owner_->QueryComposed(iid, object);
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return owner_->Retain(); }
        ULONG STDMETHODCALLTYPE Release() override { return owner_->ReleaseOne(); }
        HRESULT STDMETHODCALLTYPE GetIids(ULONG*, IID**) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override {
            return ::openxaml::winrt::CopyToHString(owner_->RuntimeClassName(), name);
        }
        HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* level) override {
            if (!level) return E_POINTER;
            *level = BaseTrust;
            return S_OK;
        }

    private:
        ApplicationObject* owner_;
    };

    IInspectable* InnerIdentity() { return &inner_; }

    // Called once, by the factory. A null outer means nobody is aggregating
    // us, so we are our own identity and the inner is what everything
    // delegates to.
    void Compose(IInspectable* outer) {
        outer_ = outer ? outer : static_cast<IInspectable*>(&inner_);
    }

    HRESULT QueryComposed(REFIID iid, void** object) {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IApplication, wux::IApplication)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IApplication2, wux::IApplication2)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IApplication3, wux::IApplication3)
        *object = nullptr;
        return E_NOINTERFACE;
    }

    // --- the properties a caller sets and reads back ---------------------------
    //
    // Terminal's App constructor sets HighContrastAdjustment to None. That is
    // a stored property with a documented default, so it round-trips here.

    HRESULT STDMETHODCALLTYPE get_HighContrastAdjustment(
        wux::ApplicationHighContrastAdjustment* value) override {
        if (!value) return E_POINTER;
        *value = high_contrast_adjustment_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_HighContrastAdjustment(
        wux::ApplicationHighContrastAdjustment value) override {
        high_contrast_adjustment_ = value;
        return S_OK;
    }

    // RequestedTheme is storage in one direction only. Setting it is a real
    // instruction and is kept; reading it before anything set it would have to
    // answer what the *system* theme is, which this runtime has no way to
    // know. It says so rather than guessing Light.
    HRESULT STDMETHODCALLTYPE get_RequestedTheme(wux::ApplicationTheme* value) override {
        if (!value) return E_POINTER;
        if (!theme_was_set_) return E_NOTIMPL;
        *value = theme_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_RequestedTheme(wux::ApplicationTheme value) override {
        theme_ = value;
        theme_was_set_ = true;
        return S_OK;
    }

private:
    Inner inner_{this};
    // Never null after Compose; Compose is called before the object escapes
    // the factory.
    IInspectable* outer_ = nullptr;
    // Application.HighContrastAdjustment defaults to Auto -- XAML backplates
    // text in high contrast until an application opts out, which is exactly
    // what Terminal's constructor does.
    wux::ApplicationHighContrastAdjustment high_contrast_adjustment_ =
        wux::ApplicationHighContrastAdjustment_Auto;
    wux::ApplicationTheme theme_ = wux::ApplicationTheme_Light;
    bool theme_was_set_ = false;
};

// The activation factory. Application is composable and *not* activatable --
// the IDL gives it [composable] and no [activatable] -- so ActivateInstance
// fails and CreateInstance is the way in.
class ApplicationFactory final : public ComObject,
                                 public IActivationFactory,
                                 public abi::NotImpl_IApplicationFactory,
                                 public abi::NotImpl_IApplicationStatics {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Application";
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IApplicationFactory,
                        wux::IApplicationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IApplicationStatics,
                        wux::IApplicationStatics)
        OPENXAML_QI_ARM(::openxaml::iid::IActivationFactory, IActivationFactory)
        OPENXAML_QI_ARM(IID_IUnknown, IActivationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IActivationFactory)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable**) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IInspectable* outer, IInspectable** inner,
                                             wux::IApplication** value) override {
        if (!inner || !value) return E_POINTER;
        *inner = nullptr;
        *value = nullptr;

        // One application per process. The published core enforces this in
        // FrameworkApplication::Initialize and fails the second construction;
        // a runtime that quietly allowed two would have two answers for
        // Application.Current and no way to say which was right.
        if (ApplicationObject::Current()) return E_UNEXPECTED;

        auto* application = new ApplicationObject();
        application->Compose(outer);
        ApplicationObject::Current() = application;

        // The inner carries the object's own single reference; the returned
        // default interface carries one on the aggregate.
        *inner = application->InnerIdentity();
        const HRESULT composed = application->QueryComposed(
            ::openxaml::iid::Windows_UI_Xaml_IApplication, reinterpret_cast<void**>(value));
        if (FAILED(composed)) {
            *inner = nullptr;
            // Releasing the inner drops the object's only reference, and its
            // destructor is what clears Application.Current again.
            application->InnerIdentity()->Release();
            return composed;
        }
        return S_OK;
    }

    // Null with S_OK when no Application was made: the published core returns
    // exactly that, and warns its own callers to expect it.
    HRESULT STDMETHODCALLTYPE get_Current(wux::IApplication** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        ApplicationObject* current = ApplicationObject::Current();
        if (!current) return S_OK;
        return current->QueryComposed(::openxaml::iid::Windows_UI_Xaml_IApplication,
                                      reinterpret_cast<void**>(value));
    }
};

}  // namespace openxaml::winrt

#endif  // OPENXAML_APPLICATION_H
