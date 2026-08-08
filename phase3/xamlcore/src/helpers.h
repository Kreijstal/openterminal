// The Windows.UI.Xaml value-type helper classes.
//
// A WinRT struct cannot carry methods, so every XAML value type that needs
// operations gets a companion runtime class holding them as statics:
// `Duration` gets `DurationHelper`, `GridLength` gets `GridLengthHelper`, and
// so on. Such a class has no constructor at all -- the IDL gives it no
// `[activatable]` attribute -- so the activation factory *is* the class, and
// `ActivateInstance` is expected to fail.
//
// These are the one part of the runtime that is fully specified by arithmetic
// on the struct and needs nothing from the layout core, no visual tree and no
// dispatcher. They are implemented here for real rather than stubbed, because
// a caller that asks what `Forever - 200ms` is deserves the answer the real
// runtime gives, and the published source says exactly what that is.

#ifndef OPENXAML_HELPERS_H
#define OPENXAML_HELPERS_H

#include "com.h"
#include "openxaml_abi_stubs.h"

namespace openxaml::winrt {

namespace wux = ABI::Windows::UI::Xaml;
namespace wf = ABI::Windows::Foundation;

// Windows.UI.Xaml.DurationHelper.
//
// Semantics from the published XAML core (microsoft/microsoft-ui-xaml,
// dxaml/xcp/dxaml/lib/Duration_Partial.cpp, MIT). A Duration is one of three
// things -- Automatic, a TimeSpan, or Forever -- and every operation below is
// a case analysis over that, not an approximation of one.
//
// One deliberate difference: where the published code writes only `Type` for
// Automatic and Forever, leaving the caller's `TimeSpan` field untouched, this
// zeroes it. Reading back a field the callee never wrote is a bug in the
// caller either way; producing a fully-defined out parameter is not a
// behavioural claim, it is refusing to hand out uninitialized memory.
class DurationHelperFactory final : public ComObject,
                                    public IActivationFactory,
                                    public abi::NotImpl_IDurationHelperStatics {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.DurationHelper";
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_IDurationHelperStatics,
                        wux::IDurationHelperStatics)
        OPENXAML_QI_ARM(::openxaml::iid::IActivationFactory, IActivationFactory)
        OPENXAML_QI_ARM(IID_IUnknown, IActivationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IActivationFactory)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    // DurationHelper has no [activatable] attribute in the IDL: there is no
    // instance to make. Saying so is the accurate answer, not a shortcoming.
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable**) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE get_Automatic(wux::Duration* value) override {
        if (!value) return E_POINTER;
        *value = Automatic();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_Forever(wux::Duration* value) override {
        if (!value) return E_POINTER;
        *value = Forever();
        return S_OK;
    }

    // A negative TimeSpan is rejected rather than stored: the published
    // factory returns E_INVALIDARG for it, and a Duration is a length of time.
    HRESULT STDMETHODCALLTYPE FromTimeSpan(wf::TimeSpan span,
                                           wux::Duration* result) override {
        if (!result) return E_POINTER;
        if (span.Duration < 0) return E_INVALIDARG;
        *result = {span, wux::DurationType_TimeSpan};
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetHasTimeSpan(wux::Duration target,
                                             boolean* result) override {
        if (!result) return E_POINTER;
        *result = target.Type == wux::DurationType_TimeSpan;
        return S_OK;
    }

    // Automatic is not equal to Forever, but each is equal to itself; two
    // TimeSpans are equal when their tick counts are.
    HRESULT STDMETHODCALLTYPE Equals(wux::Duration target, wux::Duration value,
                                     boolean* result) override {
        if (!result) return E_POINTER;
        if (target.Type == wux::DurationType_TimeSpan) {
            *result = value.Type == wux::DurationType_TimeSpan &&
                      target.TimeSpan.Duration == value.TimeSpan.Duration;
        } else {
            *result = target.Type == value.Type;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Add(wux::Duration target, wux::Duration duration,
                                  wux::Duration* result) override {
        if (!result) return E_POINTER;
        if (target.Type == wux::DurationType_TimeSpan &&
            duration.Type == wux::DurationType_TimeSpan) {
            *result = {{target.TimeSpan.Duration + duration.TimeSpan.Duration},
                       wux::DurationType_TimeSpan};
        } else if (target.Type != wux::DurationType_Automatic &&
                   duration.Type != wux::DurationType_Automatic) {
            // Neither is Automatic, so at least one is Forever: the sum is.
            *result = Forever();
        } else {
            // Automatic plus anything is Automatic.
            *result = Automatic();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Subtract(wux::Duration target, wux::Duration duration,
                                       wux::Duration* result) override {
        if (!result) return E_POINTER;
        if (target.Type == wux::DurationType_TimeSpan &&
            duration.Type == wux::DurationType_TimeSpan) {
            *result = {{target.TimeSpan.Duration - duration.TimeSpan.Duration},
                       wux::DurationType_TimeSpan};
        } else if (target.Type == wux::DurationType_Forever &&
                   duration.Type == wux::DurationType_TimeSpan) {
            // The only difference that stays infinite.
            *result = Forever();
        } else {
            *result = Automatic();
        }
        return S_OK;
    }

    // Ordering, with Automatic outside it: Automatic sorts before everything,
    // Forever after every finite span, and two Automatics compare equal.
    HRESULT STDMETHODCALLTYPE Compare(wux::Duration first, wux::Duration second,
                                      INT32* result) override {
        if (!result) return E_POINTER;
        if (first.Type == wux::DurationType_Automatic) {
            *result = second.Type == wux::DurationType_Automatic ? 0 : -1;
        } else if (second.Type == wux::DurationType_Automatic) {
            *result = 1;
        } else if (LessThan(first, second)) {
            *result = -1;
        } else if (LessThan(second, first)) {
            *result = 1;
        } else {
            *result = 0;
        }
        return S_OK;
    }

private:
    static wux::Duration Automatic() { return {{0}, wux::DurationType_Automatic}; }
    static wux::Duration Forever() { return {{0}, wux::DurationType_Forever}; }

    // Neither operand is Automatic here -- Compare has already dealt with
    // that -- so this is TimeSpan against TimeSpan, or a finite span against
    // Forever.
    static bool LessThan(wux::Duration target, wux::Duration duration) {
        if (target.Type == wux::DurationType_TimeSpan &&
            duration.Type == wux::DurationType_TimeSpan) {
            return target.TimeSpan.Duration < duration.TimeSpan.Duration;
        }
        return target.Type == wux::DurationType_TimeSpan &&
               duration.Type == wux::DurationType_Forever;
    }
};

}  // namespace openxaml::winrt

#endif  // OPENXAML_HELPERS_H
