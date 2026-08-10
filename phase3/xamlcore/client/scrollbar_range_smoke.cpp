// RangeBase/ScrollBar coercion and ValueChanged ABI acceptance.

#include "sdk.h"

#include <roapi.h>

#include <cmath>
#include <cstdio>
#include <cwchar>
#include <functional>
#include <limits>
#include <vector>

#include "openxaml_iids.h"

namespace wux = ABI::Windows::UI::Xaml;
namespace wuxcp = ABI::Windows::UI::Xaml::Controls::Primitives;

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

template <class Interface>
Interface* Activate(const wchar_t* name, const GUID& iid) {
    HSTRING class_name = nullptr;
    if (FAILED(WindowsCreateString(name, static_cast<UINT32>(std::wcslen(name)),
                                   &class_name))) return nullptr;
    IInspectable* instance = nullptr;
    const HRESULT activated = RoActivateInstance(class_name, &instance);
    WindowsDeleteString(class_name);
    if (FAILED(activated) || !instance) return nullptr;
    Interface* result = nullptr;
    const HRESULT queried = instance->QueryInterface(
        iid, reinterpret_cast<void**>(&result));
    instance->Release();
    return SUCCEEDED(queried) ? result : nullptr;
}

class ValueChangedHandler final : public wuxcp::IRangeBaseValueChangedEventHandler {
public:
    using Callback = std::function<HRESULT(
        IInspectable*, wuxcp::IRangeBaseValueChangedEventArgs*)>;

    explicit ValueChangedHandler(Callback callback)
        : callback_(std::move(callback)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown)) {
            *object = static_cast<wuxcp::IRangeBaseValueChangedEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = static_cast<ULONG>(
            InterlockedDecrement(&references_));
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Invoke(
        IInspectable* sender,
        wuxcp::IRangeBaseValueChangedEventArgs* args) override {
        return callback_(sender, args);
    }

private:
    ~ValueChangedHandler() = default;
    LONG references_ = 1;
    Callback callback_;
};

struct Observation {
    char handler = '?';
    double old_value = 0.0;
    double new_value = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    bool original_source_matches = false;
};

Observation Observe(char handler, IInspectable* sender,
                    wuxcp::IRangeBaseValueChangedEventArgs* args,
                    wuxcp::IRangeBase* range) {
    Observation value;
    value.handler = handler;
    args->get_OldValue(&value.old_value);
    args->get_NewValue(&value.new_value);
    range->get_Minimum(&value.minimum);
    range->get_Maximum(&value.maximum);
    wux::IRoutedEventArgs* routed = nullptr;
    if (SUCCEEDED(args->QueryInterface(
            openxaml::iid::Windows_UI_Xaml_IRoutedEventArgs,
            reinterpret_cast<void**>(&routed))) && routed) {
        IInspectable* original = nullptr;
        routed->get_OriginalSource(&original);
        value.original_source_matches = original == sender;
        if (original) original->Release();
        routed->Release();
    }
    return value;
}

bool Same(double left, double right) {
    return std::abs(left - right) < 0.000001;
}

}  // namespace

int main() {
    const HRESULT initialized = RoInitialize(RO_INIT_SINGLETHREADED);
    Check(SUCCEEDED(initialized) || initialized == S_FALSE,
          "RoInitialize single-threaded");

    auto* range = Activate<wuxcp::IRangeBase>(
        L"Windows.UI.Xaml.Controls.Primitives.ScrollBar",
        openxaml::iid::Windows_UI_Xaml_Controls_Primitives_IRangeBase);
    Check(range != nullptr, "activate ScrollBar as IRangeBase");
    if (!range) {
        if (SUCCEEDED(initialized)) RoUninitialize();
        return 1;
    }

    double minimum = -1.0;
    double maximum = -1.0;
    double value = -1.0;
    double small = -1.0;
    double large = -1.0;
    range->get_Minimum(&minimum);
    range->get_Maximum(&maximum);
    range->get_Value(&value);
    range->get_SmallChange(&small);
    range->get_LargeChange(&large);
    Check(Same(minimum, 0.0) && Same(maximum, 1.0) && Same(value, 0.0) &&
              Same(small, 0.1) && Same(large, 1.0),
          "RangeBase defaults match WinUI");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    Check(range->put_Minimum(nan) == E_INVALIDARG &&
              range->put_Maximum(infinity) == E_INVALIDARG &&
              range->put_Value(-infinity) == E_INVALIDARG &&
              range->put_SmallChange(nan) == E_INVALIDARG &&
              range->put_LargeChange(infinity) == E_INVALIDARG,
          "all RangeBase doubles reject NaN and infinity");

    Check(SUCCEEDED(range->put_Maximum(10.0)), "expand range for event test");
    std::vector<Observation> calls;
    EventRegistrationToken token_a{};
    EventRegistrationToken token_b{};
    EventRegistrationToken token_c{};
    auto* handler_a = new ValueChangedHandler(
        [&](IInspectable* sender, wuxcp::IRangeBaseValueChangedEventArgs* args) {
            calls.push_back(Observe('A', sender, args, range));
            range->remove_ValueChanged(token_a);
            range->remove_ValueChanged(token_b);
            return range->put_Value(6.0);
        });
    auto* handler_b = new ValueChangedHandler(
        [&](IInspectable* sender, wuxcp::IRangeBaseValueChangedEventArgs* args) {
            calls.push_back(Observe('B', sender, args, range));
            return S_OK;
        });
    auto* handler_c = new ValueChangedHandler(
        [&](IInspectable* sender, wuxcp::IRangeBaseValueChangedEventArgs* args) {
            calls.push_back(Observe('C', sender, args, range));
            return S_OK;
        });
    Check(SUCCEEDED(range->add_ValueChanged(handler_a, &token_a)) &&
              SUCCEEDED(range->add_ValueChanged(handler_b, &token_b)) &&
              SUCCEEDED(range->add_ValueChanged(handler_c, &token_c)),
          "register typed ValueChanged handlers");
    handler_a->Release();
    handler_b->Release();
    handler_c->Release();
    Check(SUCCEEDED(range->put_Value(4.0)),
          "ValueChanged permits removal and nested Value mutation");
    Check(calls.size() == 4 && calls[0].handler == 'A' &&
              calls[1].handler == 'C' && calls[2].handler == 'B' &&
              calls[3].handler == 'C',
          "strong outer snapshot survives removal while nested event sees live set");
    Check(calls.size() == 4 && Same(calls[0].old_value, 0.0) &&
              Same(calls[0].new_value, 4.0) &&
              Same(calls[1].old_value, 4.0) &&
              Same(calls[1].new_value, 6.0) &&
              Same(calls[2].old_value, 0.0) &&
              Same(calls[2].new_value, 4.0) &&
              Same(calls[3].old_value, 0.0) &&
              Same(calls[3].new_value, 4.0),
          "nested and outer event arguments remain immutable");
    bool all_sources_match = calls.size() == 4;
    for (const Observation& call : calls)
        all_sources_match = all_sources_match && call.original_source_matches;
    Check(all_sources_match, "routed OriginalSource is the ScrollBar sender");
    range->remove_ValueChanged(token_c);

    Check(SUCCEEDED(range->put_Value(0.0)), "reset Value");
    calls.clear();
    wuxcp::IRangeBaseValueChangedEventArgs* retained_args = nullptr;
    auto* ordering = new ValueChangedHandler(
        [&](IInspectable* sender, wuxcp::IRangeBaseValueChangedEventArgs* args) {
            calls.push_back(Observe('O', sender, args, range));
            if (!retained_args) {
                retained_args = args;
                retained_args->AddRef();
            }
            return S_OK;
        });
    EventRegistrationToken ordering_token{};
    Check(SUCCEEDED(range->add_ValueChanged(ordering, &ordering_token)),
          "register coercion observer");
    ordering->Release();

    Check(SUCCEEDED(range->put_Value(20.0)), "out-of-range Value clamps");
    Check(SUCCEEDED(range->put_Maximum(30.0)),
          "expanding Maximum restores uncoerced Value");
    Check(SUCCEEDED(range->put_Maximum(15.0)),
          "contracting Maximum coerces Value");
    Check(SUCCEEDED(range->put_Minimum(18.0)),
          "Minimum above Maximum raises Maximum and coerces Value");
    range->get_Minimum(&minimum);
    range->get_Maximum(&maximum);
    range->get_Value(&value);
    Check(Same(minimum, 18.0) && Same(maximum, 18.0) && Same(value, 18.0),
          "range invariant survives contracting setters");
    Check(calls.size() == 4 &&
              Same(calls[0].old_value, 0.0) && Same(calls[0].new_value, 10.0) &&
              Same(calls[0].maximum, 10.0) &&
              Same(calls[1].old_value, 10.0) && Same(calls[1].new_value, 20.0) &&
              Same(calls[1].maximum, 30.0) &&
              Same(calls[2].old_value, 20.0) && Same(calls[2].new_value, 15.0) &&
              Same(calls[2].maximum, 30.0) &&
              Same(calls[3].old_value, 15.0) && Same(calls[3].new_value, 18.0) &&
              Same(calls[3].minimum, 0.0) && Same(calls[3].maximum, 18.0),
          "Minimum/Maximum coercion event order is observable and exact");

    Check(SUCCEEDED(range->put_Minimum(5.0)) &&
              SUCCEEDED(range->put_Maximum(30.0)),
          "expanding range can restore uncoerced Value again");
    range->get_Value(&value);
    Check(Same(value, 20.0), "uncoerced Value is retained across contractions");
    Check(SUCCEEDED(range->put_SmallChange(-2.0)) &&
              SUCCEEDED(range->put_LargeChange(-3.0)),
          "finite change values are accepted without range coercion");

    if (retained_args) {
        double old_retained = -1.0;
        double new_retained = -1.0;
        retained_args->get_OldValue(&old_retained);
        retained_args->get_NewValue(&new_retained);
        Check(Same(old_retained, 0.0) && Same(new_retained, 10.0),
              "handler-retained event args remain valid and immutable");
        retained_args->Release();
    } else {
        Check(false, "handler retained event args");
    }
    range->remove_ValueChanged(ordering_token);
    range->Release();
    if (SUCCEEDED(initialized)) RoUninitialize();

    if (failures) return 1;
    std::puts("ScrollBar RangeBase coercion and event checks passed");
    return 0;
}
