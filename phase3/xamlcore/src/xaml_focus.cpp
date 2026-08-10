#include "xaml_focus.h"

#include "com.h"
#include "canvas.h"
#include "element.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

namespace openxaml::winrt {
namespace {

std::mutex g_scope_mutex;
std::map<openxaml::Element*, std::weak_ptr<XamlFocusScope>> g_scopes;
std::map<openxaml::Element*, XamlFocusTarget*> g_targets;

bool ValidRequestedState(ABI::Windows::UI::Xaml::FocusState state) noexcept {
    return state == ABI::Windows::UI::Xaml::FocusState_Pointer ||
           state == ABI::Windows::UI::Xaml::FocusState_Keyboard ||
           state == ABI::Windows::UI::Xaml::FocusState_Programmatic;
}

inline constexpr GUID IID_IKeyRoutedEventArgs = {
    0xd4cd3dfe, 0x4079, 0x42e9,
    {0xa3, 0x9a, 0x30, 0x95, 0xd3, 0xf0, 0x49, 0xc6}};
inline constexpr GUID IID_IKeyRoutedEventArgs2 = {
    0x1b02d57a, 0x9634, 0x4f14,
    {0x91, 0xb2, 0x13, 0x3e, 0x42, 0xfd, 0xb3, 0xcd}};
inline constexpr GUID IID_ICharacterReceivedRoutedEventArgs = {
    0x7849fd82, 0x48e4, 0x444d,
    {0x94, 0x19, 0x93, 0xab, 0x88, 0x92, 0xc1, 0x07}};
inline constexpr GUID IID_IPointerRoutedEventArgs = {
    0xda628f0a, 0x9752, 0x49e2,
    {0xbd, 0xe2, 0x49, 0xec, 0xca, 0xb9, 0x19, 0x4d}};
inline constexpr GUID IID_IPointerRoutedEventArgs2 = {
    0x0821f294, 0x1de6, 0x4711,
    {0xba, 0x7c, 0x8d, 0x4b, 0x8b, 0x09, 0x11, 0xd0}};
inline constexpr GUID IID_IPointer = {
    0x5ee8f39f, 0x747d, 0x4171,
    {0x90, 0xe6, 0xcd, 0x37, 0xa9, 0xdf, 0xfb, 0x11}};
inline constexpr GUID IID_IPointerPoint = {
    0xe995317d, 0x7296, 0x42d9,
    {0x82, 0x33, 0xc5, 0xbe, 0x73, 0xb7, 0x4a, 0x4a}};
inline constexpr GUID IID_IPointerPointProperties = {
    0xc79d8a4b, 0xc163, 0x4ee7,
    {0x80, 0x3f, 0x67, 0xce, 0x79, 0xf9, 0x97, 0x2d}};
inline constexpr GUID IID_IPointerDevice = {
    0x93c9bafc, 0xebcb, 0x467e,
    {0x82, 0xc6, 0x27, 0x6f, 0xea, 0xe3, 0x6b, 0x5a}};
inline constexpr GUID IID_ITappedRoutedEventArgs = {
    0xa099e6be, 0xe624, 0x459a,
    {0xbb, 0x1d, 0xe0, 0x5c, 0x73, 0xe2, 0xcc, 0x66}};
inline constexpr GUID IID_IDoubleTappedRoutedEventArgs = {
    0xaf404424, 0x26df, 0x44f4,
    {0x87, 0x14, 0x93, 0x59, 0x24, 0x9b, 0x62, 0xd3}};
inline constexpr GUID IID_IGettingFocusEventArgs = {
    0xfa05b9ce, 0xc67c, 0x4be8,
    {0x8f, 0xd4, 0xc4, 0x4d, 0x67, 0x87, 0x7e, 0x0d}};
inline constexpr GUID IID_ILosingFocusEventArgs = {
    0xf9f683c7, 0xd789, 0x472b,
    {0xaa, 0x93, 0x6d, 0x41, 0x05, 0xe6, 0xda, 0xbe}};

ABI::Windows::UI::Core::CorePhysicalKeyStatus PhysicalStatus(
    const IslandPhysicalKeyStatus& source) noexcept {
    return {source.repeat_count, source.scan_code,
            static_cast<boolean>(source.extended),
            static_cast<boolean>(source.menu_key_down),
            static_cast<boolean>(source.was_key_down),
            static_cast<boolean>(source.released)};
}

ABI::Windows::UI::Xaml::Input::FocusInputDeviceKind FocusInputKind(
    ABI::Windows::UI::Xaml::FocusState state) noexcept {
    if (state == ABI::Windows::UI::Xaml::FocusState_Keyboard)
        return ABI::Windows::UI::Xaml::Input::FocusInputDeviceKind_Keyboard;
    if (state == ABI::Windows::UI::Xaml::FocusState_Pointer)
        return ABI::Windows::UI::Xaml::Input::FocusInputDeviceKind_Mouse;
    return ABI::Windows::UI::Xaml::Input::FocusInputDeviceKind_None;
}

template <class Interface>
class FocusChangingEventArgsObject final : public ComObject, public Interface {
public:
    FocusChangingEventArgsObject(
        const GUID& iid, const wchar_t* name,
        ABI::Windows::UI::Xaml::IDependencyObject* old_element,
        ABI::Windows::UI::Xaml::IDependencyObject* new_element,
        ABI::Windows::UI::Xaml::FocusState state) noexcept
        : iid_(iid), name_(name), old_(old_element), next_(new_element),
          state_(state) {
        if (old_) old_->AddRef();
        if (next_) next_->AddRef();
    }
    ~FocusChangingEventArgsObject() override {
        if (old_) old_->Release();
        if (next_) next_->Release();
    }
    const wchar_t* RuntimeClassName() const override { return name_; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        if (IsEqualGUID(iid, iid_) || IsEqualGUID(iid, IID_IUnknown) ||
            IsEqualGUID(iid, ::openxaml::iid::IInspectable)) {
            *object = static_cast<Interface*>(this);
            this->AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()
    HRESULT STDMETHODCALLTYPE get_OldFocusedElement(
        ABI::Windows::UI::Xaml::IDependencyObject** value) override {
        return Copy(old_, value);
    }
    HRESULT STDMETHODCALLTYPE get_NewFocusedElement(
        ABI::Windows::UI::Xaml::IDependencyObject** value) override {
        return Copy(next_, value);
    }
    HRESULT STDMETHODCALLTYPE put_NewFocusedElement(
        ABI::Windows::UI::Xaml::IDependencyObject* value) override {
        if (value) value->AddRef();
        if (next_) next_->Release();
        next_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FocusState(
        ABI::Windows::UI::Xaml::FocusState* value) override {
        if (!value) return E_POINTER;
        *value = state_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Direction(
        ABI::Windows::UI::Xaml::Input::FocusNavigationDirection* value) override {
        if (!value) return E_POINTER;
        *value = ABI::Windows::UI::Xaml::Input::FocusNavigationDirection_None;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_ ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_InputDevice(
        ABI::Windows::UI::Xaml::Input::FocusInputDeviceKind* value) override {
        if (!value) return E_POINTER;
        *value = FocusInputKind(state_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Cancel(boolean* value) override {
        if (!value) return E_POINTER;
        *value = cancel_ ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Cancel(boolean value) override {
        cancel_ = value != 0;
        return S_OK;
    }
    bool canceled() const noexcept { return cancel_; }
    ABI::Windows::UI::Xaml::IDependencyObject* new_element() const noexcept {
        return next_;
    }

private:
    static HRESULT Copy(ABI::Windows::UI::Xaml::IDependencyObject* source,
                        ABI::Windows::UI::Xaml::IDependencyObject** value) {
        if (!value) return E_POINTER;
        *value = source;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    const GUID& iid_;
    const wchar_t* name_;
    ABI::Windows::UI::Xaml::IDependencyObject* old_ = nullptr;
    ABI::Windows::UI::Xaml::IDependencyObject* next_ = nullptr;
    ABI::Windows::UI::Xaml::FocusState state_;
    bool handled_ = false;
    bool cancel_ = false;
};

using GettingFocusArgs = FocusChangingEventArgsObject<
    ABI::Windows::UI::Xaml::Input::IGettingFocusEventArgs>;
using LosingFocusArgs = FocusChangingEventArgsObject<
    ABI::Windows::UI::Xaml::Input::ILosingFocusEventArgs>;

class KeyRoutedEventArgsObject final
    : public ComObject,
      public ABI::Windows::UI::Xaml::Input::IKeyRoutedEventArgs,
      public ABI::Windows::UI::Xaml::Input::IKeyRoutedEventArgs2 {
public:
    explicit KeyRoutedEventArgsObject(const IslandKeyEvent& event) noexcept
        : key_(static_cast<ABI::Windows::System::VirtualKey>(event.virtual_key)),
          status_(PhysicalStatus(event.status)) {}

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Input.KeyRoutedEventArgs";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IKeyRoutedEventArgs,
                        ABI::Windows::UI::Xaml::Input::IKeyRoutedEventArgs)
        OPENXAML_QI_ARM(IID_IKeyRoutedEventArgs2,
                        ABI::Windows::UI::Xaml::Input::IKeyRoutedEventArgs2)
        OPENXAML_QI_ARM(IID_IUnknown,
                        ABI::Windows::UI::Xaml::Input::IKeyRoutedEventArgs)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable,
                        ABI::Windows::UI::Xaml::Input::IKeyRoutedEventArgs)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Key(
        ABI::Windows::System::VirtualKey* value) override {
        if (!value) return E_POINTER;
        *value = key_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_KeyStatus(
        ABI::Windows::UI::Core::CorePhysicalKeyStatus* value) override {
        if (!value) return E_POINTER;
        *value = status_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_ ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_OriginalKey(
        ABI::Windows::System::VirtualKey* value) override {
        return get_Key(value);
    }
    bool handled() const noexcept { return handled_; }

private:
    ABI::Windows::System::VirtualKey key_;
    ABI::Windows::UI::Core::CorePhysicalKeyStatus status_{};
    bool handled_ = false;
};

class CharacterRoutedEventArgsObject final
    : public ComObject,
      public ABI::Windows::UI::Xaml::Input::ICharacterReceivedRoutedEventArgs {
public:
    explicit CharacterRoutedEventArgsObject(
        const IslandCharacterEvent& event) noexcept
        : character_(static_cast<WCHAR>(event.character)),
          status_(PhysicalStatus(event.status)) {}

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Input.CharacterReceivedRoutedEventArgs";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(
            IID_ICharacterReceivedRoutedEventArgs,
            ABI::Windows::UI::Xaml::Input::ICharacterReceivedRoutedEventArgs)
        OPENXAML_QI_ARM(
            IID_IUnknown,
            ABI::Windows::UI::Xaml::Input::ICharacterReceivedRoutedEventArgs)
        OPENXAML_QI_ARM(
            ::openxaml::iid::IInspectable,
            ABI::Windows::UI::Xaml::Input::ICharacterReceivedRoutedEventArgs)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Character(WCHAR* value) override {
        if (!value) return E_POINTER;
        *value = character_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_KeyStatus(
        ABI::Windows::UI::Core::CorePhysicalKeyStatus* value) override {
        if (!value) return E_POINTER;
        *value = status_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_ ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value != 0;
        return S_OK;
    }
    bool handled() const noexcept { return handled_; }

private:
    WCHAR character_ = 0;
    ABI::Windows::UI::Core::CorePhysicalKeyStatus status_{};
    bool handled_ = false;
};

ABI::Windows::System::VirtualKeyModifiers PointerModifiers(
    const IslandPointerEvent& event) noexcept {
    int value = 0;
    if (event.control) value |= ABI::Windows::System::VirtualKeyModifiers_Control;
    if (event.menu) value |= ABI::Windows::System::VirtualKeyModifiers_Menu;
    if (event.shift) value |= ABI::Windows::System::VirtualKeyModifiers_Shift;
    if (event.windows) value |= ABI::Windows::System::VirtualKeyModifiers_Windows;
    return static_cast<ABI::Windows::System::VirtualKeyModifiers>(value);
}

class PointerObject final
    : public ComObject,
      public ABI::Windows::UI::Xaml::Input::IPointer {
public:
    explicit PointerObject(const IslandPointerEvent& event) noexcept
        : pointer_id_(event.pointer_id),
          in_contact_(event.left_button || event.right_button ||
                      event.middle_button || event.xbutton1 || event.xbutton2) {}

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Input.Pointer";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IPointer,
                        ABI::Windows::UI::Xaml::Input::IPointer)
        OPENXAML_QI_ARM(IID_IUnknown,
                        ABI::Windows::UI::Xaml::Input::IPointer)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable,
                        ABI::Windows::UI::Xaml::Input::IPointer)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_PointerId(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = pointer_id_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PointerDeviceType(
        ABI::Windows::Devices::Input::PointerDeviceType* value) override {
        if (!value) return E_POINTER;
        *value = ABI::Windows::Devices::Input::PointerDeviceType_Mouse;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsInContact(boolean* value) override {
        if (!value) return E_POINTER;
        *value = in_contact_ ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsInRange(boolean* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }

private:
    std::uint32_t pointer_id_ = 1;
    bool in_contact_ = false;
};

class PointerDeviceObject final
    : public ComObject,
      public ABI::Windows::Devices::Input::IPointerDevice {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.Devices.Input.PointerDevice";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IPointerDevice,
                        ABI::Windows::Devices::Input::IPointerDevice)
        OPENXAML_QI_ARM(IID_IUnknown,
                        ABI::Windows::Devices::Input::IPointerDevice)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable,
                        ABI::Windows::Devices::Input::IPointerDevice)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_PointerDeviceType(
        ABI::Windows::Devices::Input::PointerDeviceType* value) override {
        if (!value) return E_POINTER;
        *value = ABI::Windows::Devices::Input::PointerDeviceType_Mouse;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsIntegrated(boolean* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_MaxContacts(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PhysicalDeviceRect(
        ABI::Windows::Foundation::Rect* value) override {
        return ScreenRect(value);
    }
    HRESULT STDMETHODCALLTYPE get_ScreenRect(
        ABI::Windows::Foundation::Rect* value) override {
        return ScreenRect(value);
    }
    HRESULT STDMETHODCALLTYPE get_SupportedUsages(
        __FIVectorView_1_Windows__CDevices__CInput__CPointerDeviceUsage** value)
        override {
        if (!value) return E_POINTER;
        *value = nullptr;
        // HID usage projection is outside the Win32 mouse message boundary;
        // returning an invented list would be worse than an explicit refusal.
        return E_NOTIMPL;
    }

private:
    static HRESULT ScreenRect(ABI::Windows::Foundation::Rect* value) noexcept {
        if (!value) return E_POINTER;
        *value = {0.0f, 0.0f, static_cast<float>(GetSystemMetrics(SM_CXSCREEN)),
                  static_cast<float>(GetSystemMetrics(SM_CYSCREEN))};
        return S_OK;
    }
};

class PointerPointPropertiesObject final
    : public ComObject,
      public ABI::Windows::UI::Input::IPointerPointProperties {
public:
    PointerPointPropertiesObject(const IslandPointerEvent& event,
                                 ABI::Windows::Foundation::Point position) noexcept
        : event_(event), position_(position) {}

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Input.PointerPointProperties";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IPointerPointProperties,
                        ABI::Windows::UI::Input::IPointerPointProperties)
        OPENXAML_QI_ARM(IID_IUnknown,
                        ABI::Windows::UI::Input::IPointerPointProperties)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable,
                        ABI::Windows::UI::Input::IPointerPointProperties)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Pressure(FLOAT* value) override {
        if (!value) return E_POINTER;
        *value = InContact() ? 0.5f : 0.0f;
        return S_OK;
    }
#define OPENXAML_FALSE_POINTER_PROPERTY(name) \
    HRESULT STDMETHODCALLTYPE get_##name(boolean* value) override { \
        if (!value) return E_POINTER; \
        *value = 0; \
        return S_OK; \
    }
    OPENXAML_FALSE_POINTER_PROPERTY(IsInverted)
    OPENXAML_FALSE_POINTER_PROPERTY(IsEraser)
    HRESULT STDMETHODCALLTYPE get_Orientation(FLOAT* value) override {
        if (!value) return E_POINTER;
        *value = 0.0f;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_XTilt(FLOAT* value) override {
        if (!value) return E_POINTER;
        *value = 0.0f;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_YTilt(FLOAT* value) override {
        if (!value) return E_POINTER;
        *value = 0.0f;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Twist(FLOAT* value) override {
        if (!value) return E_POINTER;
        *value = 0.0f;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ContactRect(
        ABI::Windows::Foundation::Rect* value) override {
        if (!value) return E_POINTER;
        *value = {position_.X, position_.Y, 0.0f, 0.0f};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ContactRectRaw(
        ABI::Windows::Foundation::Rect* value) override {
        return get_ContactRect(value);
    }
    OPENXAML_FALSE_POINTER_PROPERTY(TouchConfidence)
#undef OPENXAML_FALSE_POINTER_PROPERTY
#define OPENXAML_BUTTON_POINTER_PROPERTY(name, field) \
    HRESULT STDMETHODCALLTYPE get_##name(boolean* value) override { \
        if (!value) return E_POINTER; \
        *value = event_.field ? 1 : 0; \
        return S_OK; \
    }
    OPENXAML_BUTTON_POINTER_PROPERTY(IsLeftButtonPressed, left_button)
    OPENXAML_BUTTON_POINTER_PROPERTY(IsRightButtonPressed, right_button)
    OPENXAML_BUTTON_POINTER_PROPERTY(IsMiddleButtonPressed, middle_button)
    HRESULT STDMETHODCALLTYPE get_MouseWheelDelta(INT32* value) override {
        if (!value) return E_POINTER;
        *value = event_.wheel_delta;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsHorizontalMouseWheel(boolean* value) override {
        if (!value) return E_POINTER;
        *value = event_.horizontal_wheel ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsPrimary(boolean* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsInRange(boolean* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsCanceled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsBarrelButtonPressed(boolean* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    OPENXAML_BUTTON_POINTER_PROPERTY(IsXButton1Pressed, xbutton1)
    OPENXAML_BUTTON_POINTER_PROPERTY(IsXButton2Pressed, xbutton2)
#undef OPENXAML_BUTTON_POINTER_PROPERTY
    HRESULT STDMETHODCALLTYPE get_PointerUpdateKind(
        ABI::Windows::UI::Input::PointerUpdateKind* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<ABI::Windows::UI::Input::PointerUpdateKind>(
            event_.update_kind);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HasUsage(UINT32, UINT32, boolean* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetUsageValue(UINT32, UINT32, INT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return E_NOTIMPL;
    }

private:
    bool InContact() const noexcept {
        return event_.left_button || event_.right_button || event_.middle_button ||
               event_.xbutton1 || event_.xbutton2;
    }
    IslandPointerEvent event_;
    ABI::Windows::Foundation::Point position_{};
};

class PointerPointObject final
    : public ComObject,
      public ABI::Windows::UI::Input::IPointerPoint {
public:
    PointerPointObject(const IslandPointerEvent& event,
                       ABI::Windows::Foundation::Point position) noexcept
        : event_(event), position_(position) {}

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Input.PointerPoint";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IPointerPoint,
                        ABI::Windows::UI::Input::IPointerPoint)
        OPENXAML_QI_ARM(IID_IUnknown,
                        ABI::Windows::UI::Input::IPointerPoint)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable,
                        ABI::Windows::UI::Input::IPointerPoint)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_PointerDevice(
        ABI::Windows::Devices::Input::IPointerDevice** value) override {
        if (!value) return E_POINTER;
        *value = new (std::nothrow) PointerDeviceObject();
        return *value ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_Position(
        ABI::Windows::Foundation::Point* value) override {
        if (!value) return E_POINTER;
        *value = position_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RawPosition(
        ABI::Windows::Foundation::Point* value) override {
        if (!value) return E_POINTER;
        *value = {static_cast<float>(event_.x), static_cast<float>(event_.y)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_PointerId(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = event_.pointer_id;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FrameId(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = event_.frame_id;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Timestamp(UINT64* value) override {
        if (!value) return E_POINTER;
        *value = event_.timestamp;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsInContact(boolean* value) override {
        if (!value) return E_POINTER;
        *value = (event_.left_button || event_.right_button ||
                  event_.middle_button || event_.xbutton1 || event_.xbutton2) ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Properties(
        ABI::Windows::UI::Input::IPointerPointProperties** value) override {
        if (!value) return E_POINTER;
        *value = new (std::nothrow) PointerPointPropertiesObject(event_, position_);
        return *value ? S_OK : E_OUTOFMEMORY;
    }

private:
    IslandPointerEvent event_;
    ABI::Windows::Foundation::Point position_{};
};

bool PointInRect(openxaml::Point point, const openxaml::Rect& rect) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y) &&
           std::isfinite(rect.x) && std::isfinite(rect.y) &&
           std::isfinite(rect.width) && std::isfinite(rect.height) &&
           rect.width > 0.0 && rect.height > 0.0 &&
           point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

bool ParentPointToLocal(const openxaml::Element& element,
                        openxaml::Point parent,
                        openxaml::Point& local) noexcept {
    local = {parent.x - element.render_origin().x,
             parent.y - element.render_origin().y};
    const openxaml::VisualTransform& transform = element.visual_transform();
    if (transform.kind == openxaml::VisualTransformKind::Unsupported) return false;
    if (transform.kind == openxaml::VisualTransformKind::Rotate) {
        const openxaml::Size size = element.render_size();
        const openxaml::Point pivot{
            size.width * element.render_transform_origin().x,
            size.height * element.render_transform_origin().y};
        const double radians = -transform.angle_degrees *
                               3.14159265358979323846 / 180.0;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        const double x = local.x - pivot.x;
        const double y = local.y - pivot.y;
        local = {x * cosine - y * sine + pivot.x,
                 x * sine + y * cosine + pivot.y};
    }
    const openxaml::VisualClip& clip = element.visual_clip();
    if (clip.kind == openxaml::VisualClipKind::Unsupported) return false;
    if (clip.kind == openxaml::VisualClipKind::Rectangle &&
        !PointInRect(local, clip.bounds)) return false;
    const openxaml::Size size = element.render_size();
    return PointInRect(local, {0.0, 0.0, size.width, size.height});
}

std::vector<openxaml::Element*> OrderedHitChildren(openxaml::Element& element) {
    std::vector<openxaml::Element*> children = element.Children();
    std::stable_sort(children.begin(), children.end(),
        [](const openxaml::Element* left, const openxaml::Element* right) {
            return openxaml::Canvas::GetZIndex(*left) <
                   openxaml::Canvas::GetZIndex(*right);
        });
    return children;
}

XamlFocusTarget* RegisteredTarget(openxaml::Element* element) noexcept {
    std::lock_guard<std::mutex> guard(g_scope_mutex);
    const auto found = g_targets.find(element);
    if (found == g_targets.end()) return nullptr;
    found->second->RetainFocusTarget();
    return found->second;
}

XamlFocusTarget* HitTest(openxaml::Element& element,
                         openxaml::Point parent_point) {
    if (element.visibility() != openxaml::Visibility::Visible) return nullptr;
    openxaml::Point local;
    if (!ParentPointToLocal(element, parent_point, local)) return nullptr;
    std::vector<openxaml::Element*> children = OrderedHitChildren(element);
    for (auto child = children.rbegin(); child != children.rend(); ++child) {
        if (!*child) continue;
        if (XamlFocusTarget* target = HitTest(**child, local)) return target;
    }
    return RegisteredTarget(&element);
}

XamlFocusTarget* TargetByNode(openxaml::Element* root,
                              std::uint64_t node_id) noexcept {
    std::lock_guard<std::mutex> guard(g_scope_mutex);
    for (const auto& entry : g_targets) {
        if (entry.first->render_node_id() == node_id &&
            XamlFocusScope::VisualRoot(entry.first) == root) {
            entry.second->RetainFocusTarget();
            return entry.second;
        }
    }
    return nullptr;
}

bool IsInSubtree(openxaml::Element* element,
                 openxaml::Element* subtree) noexcept {
    for (openxaml::Element* current = element; current;
         current = current->visual_parent()) {
        if (current == subtree) return true;
    }
    return false;
}

XamlFocusTarget* TargetByNodeInSubtree(
    openxaml::Element* subtree, std::uint64_t node_id) noexcept {
    std::lock_guard<std::mutex> guard(g_scope_mutex);
    for (const auto& entry : g_targets) {
        if (entry.first->render_node_id() == node_id &&
            IsInSubtree(entry.first, subtree)) {
            entry.second->RetainFocusTarget();
            return entry.second;
        }
    }
    return nullptr;
}

bool RootPointToLocal(openxaml::Element* relative,
                      openxaml::Element* root,
                      openxaml::Point root_point,
                      openxaml::Point& local) {
    if (!relative || XamlFocusScope::VisualRoot(relative) != root) return false;
    std::vector<openxaml::Element*> chain;
    for (openxaml::Element* current = relative; current;
         current = current->visual_parent()) {
        chain.push_back(current);
        if (current == root) break;
    }
    if (chain.empty() || chain.back() != root) return false;
    local = root_point;
    for (auto current = chain.rbegin(); current != chain.rend(); ++current) {
        if (!ParentPointToLocal(**current, local, local)) return false;
    }
    return true;
}

class PointerRoutedEventArgsObject final
    : public ComObject,
      public ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs,
      public ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs2 {
public:
    PointerRoutedEventArgsObject(IslandPointerEvent event,
                                 XamlFocusTarget* root_owner) noexcept
        : event_(std::move(event)), root_owner_(root_owner),
          pointer_(new (std::nothrow) PointerObject(event_)) {
        if (root_owner_) root_owner_->RetainFocusTarget();
    }
    ~PointerRoutedEventArgsObject() override {
        if (pointer_) pointer_->Release();
        if (root_owner_) root_owner_->ReleaseFocusTarget();
    }
    bool valid() const noexcept { return pointer_ != nullptr; }
    bool handled() const noexcept { return handled_; }

    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Input.PointerRoutedEventArgs";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_IPointerRoutedEventArgs,
                        ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs)
        OPENXAML_QI_ARM(IID_IPointerRoutedEventArgs2,
                        ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs2)
        OPENXAML_QI_ARM(IID_IUnknown,
                        ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable,
                        ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_Pointer(
        ABI::Windows::UI::Xaml::Input::IPointer** value) override {
        if (!value) return E_POINTER;
        *value = pointer_;
        if (*value) (*value)->AddRef();
        return *value ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_KeyModifiers(
        ABI::Windows::System::VirtualKeyModifiers* value) override {
        if (!value) return E_POINTER;
        *value = PointerModifiers(event_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_ ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value != 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentPoint(
        ABI::Windows::UI::Xaml::IUIElement* relative_to,
        ABI::Windows::UI::Input::IPointerPoint** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        openxaml::Point position{event_.x, event_.y};
        if (relative_to) {
            IOpenXamlNative* native = nullptr;
            const HRESULT queried = relative_to->QueryInterface(
                IID_IOpenXamlNative, reinterpret_cast<void**>(&native));
            if (FAILED(queried) || !native) return E_INVALIDARG;
            openxaml::Element* relative = native->LayoutElement();
            native->Release();
            openxaml::Element* const root = root_owner_
                ? root_owner_->FocusLayoutElement() : nullptr;
            if (!RootPointToLocal(relative, root, position, position))
                return E_INVALIDARG;
        }
        *result = new (std::nothrow) PointerPointObject(
            event_, {static_cast<float>(position.x), static_cast<float>(position.y)});
        return *result ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE GetIntermediatePoints(
        ABI::Windows::UI::Xaml::IUIElement*,
        __FIVector_1_Windows__CUI__CInput__CPointerPoint** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        // A Win32 mouse message contains one sample. Returning a fabricated
        // vector would imply history the host did not receive.
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_IsGenerated(boolean* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }

private:
    IslandPointerEvent event_;
    // Routed-event args are legal for a handler to retain. Keeping the root
    // projection alive prevents a later GetCurrentPoint(relativeTo) from
    // dereferencing an Element destroyed by content replacement.
    XamlFocusTarget* root_owner_ = nullptr;
    ABI::Windows::UI::Xaml::Input::IPointer* pointer_ = nullptr;
    bool handled_ = false;
};

class TappedRoutedEventArgsObject final
    : public ComObject,
      public ABI::Windows::UI::Xaml::Input::ITappedRoutedEventArgs,
      public ABI::Windows::UI::Xaml::Input::IDoubleTappedRoutedEventArgs {
public:
    TappedRoutedEventArgsObject(IslandTapEventKind kind,
                                openxaml::Point root_position,
                                XamlFocusTarget* root_owner) noexcept
        : kind_(kind), root_position_(root_position), root_owner_(root_owner) {
        if (root_owner_) root_owner_->RetainFocusTarget();
    }
    ~TappedRoutedEventArgsObject() override {
        if (root_owner_) root_owner_->ReleaseFocusTarget();
    }

    const wchar_t* RuntimeClassName() const override {
        return kind_ == IslandTapEventKind::DoubleTapped
            ? L"Windows.UI.Xaml.Input.DoubleTappedRoutedEventArgs"
            : L"Windows.UI.Xaml.Input.TappedRoutedEventArgs";
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(IID_ITappedRoutedEventArgs,
                        ABI::Windows::UI::Xaml::Input::ITappedRoutedEventArgs)
        OPENXAML_QI_ARM(
            IID_IDoubleTappedRoutedEventArgs,
            ABI::Windows::UI::Xaml::Input::IDoubleTappedRoutedEventArgs)
        OPENXAML_QI_ARM(IID_IUnknown,
                        ABI::Windows::UI::Xaml::Input::ITappedRoutedEventArgs)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable,
                        ABI::Windows::UI::Xaml::Input::ITappedRoutedEventArgs)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE get_PointerDeviceType(
        ABI::Windows::Devices::Input::PointerDeviceType* value) override {
        if (!value) return E_POINTER;
        *value = ABI::Windows::Devices::Input::PointerDeviceType_Mouse;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Handled(boolean* value) override {
        if (!value) return E_POINTER;
        *value = handled_ ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Handled(boolean value) override {
        handled_ = value != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPosition(
        ABI::Windows::UI::Xaml::IUIElement* relative_to,
        ABI::Windows::Foundation::Point* result) override {
        if (!result) return E_POINTER;
        openxaml::Point position = root_position_;
        if (relative_to) {
            IOpenXamlNative* native = nullptr;
            const HRESULT queried = relative_to->QueryInterface(
                IID_IOpenXamlNative, reinterpret_cast<void**>(&native));
            if (FAILED(queried) || !native) return E_INVALIDARG;
            openxaml::Element* relative = native->LayoutElement();
            native->Release();
            openxaml::Element* const root = root_owner_
                ? root_owner_->FocusLayoutElement() : nullptr;
            if (!RootPointToLocal(relative, root, position, position))
                return E_INVALIDARG;
        }
        result->X = static_cast<float>(position.x);
        result->Y = static_cast<float>(position.y);
        return S_OK;
    }

    bool handled() const noexcept { return handled_; }

private:
    IslandTapEventKind kind_;
    openxaml::Point root_position_;
    XamlFocusTarget* root_owner_ = nullptr;
    bool handled_ = false;
};

struct RetainedRoute {
    RetainedRoute() = default;
    RetainedRoute(const RetainedRoute&) = delete;
    RetainedRoute& operator=(const RetainedRoute&) = delete;
    RetainedRoute(RetainedRoute&& other) noexcept
        : targets(std::move(other.targets)) {
        other.targets.clear();
    }
    ~RetainedRoute() {
        for (XamlFocusTarget* target : targets) target->ReleaseFocusTarget();
    }
    bool empty() const noexcept { return targets.empty(); }
    std::vector<XamlFocusTarget*> targets;
};

struct TargetLease {
    explicit TargetLease(XamlFocusTarget* value = nullptr) noexcept
        : target(value) {}
    ~TargetLease() { if (target) target->ReleaseFocusTarget(); }
    TargetLease(const TargetLease&) = delete;
    TargetLease& operator=(const TargetLease&) = delete;
    XamlFocusTarget* target = nullptr;
};

ABI::Windows::UI::Xaml::IDependencyObject* CopyDependency(
    XamlFocusTarget* target) noexcept {
    if (!target) return nullptr;
    IInspectable* inspectable = nullptr;
    if (FAILED(target->CopyFocusInspectable(&inspectable)) || !inspectable)
        return nullptr;
    ABI::Windows::UI::Xaml::IDependencyObject* dependency = nullptr;
    (void)inspectable->QueryInterface(
        ::openxaml::iid::Windows_UI_Xaml_IDependencyObject,
        reinterpret_cast<void**>(&dependency));
    inspectable->Release();
    return dependency;
}

XamlFocusTarget* TargetForDependency(
    ABI::Windows::UI::Xaml::IDependencyObject* dependency) noexcept {
    if (!dependency) return nullptr;
    IOpenXamlNative* native = nullptr;
    if (FAILED(dependency->QueryInterface(
            IID_IOpenXamlNative, reinterpret_cast<void**>(&native))) ||
        !native) return nullptr;
    openxaml::Element* const element = native->LayoutElement();
    native->Release();
    return RegisteredTarget(element);
}

RetainedRoute SnapshotRoute(XamlFocusTarget* target) {
    RetainedRoute route;
    if (!target) return route;
    std::lock_guard<std::mutex> guard(g_scope_mutex);
    for (openxaml::Element* element = target->FocusLayoutElement(); element;
         element = element->visual_parent()) {
        const auto found = g_targets.find(element);
        if (found == g_targets.end()) continue;
        // Grow the owned slot first. If allocation fails, every previously
        // retained entry is released by RetainedRoute's destructor.
        route.targets.push_back(found->second);
        found->second->RetainFocusTarget();
    }
    return route;
}

bool DispatchPointerToRoute(
    XamlFocusTarget* target, XamlFocusTarget* root_owner,
    const IslandPointerEvent& event) {
    RetainedRoute route = SnapshotRoute(target);
    if (route.empty() || !root_owner) return false;
    auto* args = new (std::nothrow) PointerRoutedEventArgsObject(
        event, root_owner);
    if (!args || !args->valid()) {
        if (args) {
            static_cast<ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs*>(
                args)->Release();
        }
        return false;
    }
    for (XamlFocusTarget* current : route.targets) {
        current->InvokeIslandPointerEvent(event.kind, args);
        if (args->handled()) break;
    }
    const bool handled = args->handled();
    static_cast<ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs*>(args)
        ->Release();
    return handled;
}

bool DispatchTapToRoute(
    XamlFocusTarget* target, XamlFocusTarget* root_owner,
    IslandTapEventKind kind, openxaml::Point position) {
    RetainedRoute route = SnapshotRoute(target);
    if (route.empty() || !root_owner) return false;
    auto* args = new (std::nothrow) TappedRoutedEventArgsObject(
        kind, position, root_owner);
    if (!args) return false;
    for (XamlFocusTarget* current : route.targets) {
        current->InvokeIslandTapEvent(kind, args);
        if (args->handled()) break;
    }
    const bool handled = args->handled();
    static_cast<ABI::Windows::UI::Xaml::Input::ITappedRoutedEventArgs*>(args)
        ->Release();
    return handled;
}

}  // namespace

XamlFocusScope::XamlFocusScope(
    std::weak_ptr<IslandInputManager> manager) noexcept
    : manager_(std::move(manager)) {}

XamlFocusScope::~XamlFocusScope() {
    ClearTapCandidate();
    ClearLastTap();
    DetachRoot(root_);
    for (XamlFocusTarget* target : detached_capture_route_)
        target->ReleaseFocusTarget();
    detached_capture_route_.clear();
    for (XamlFocusTarget* target : detached_focus_route_)
        target->ReleaseFocusTarget();
    detached_focus_route_.clear();
    if (detached_focus_target_) detached_focus_target_->ReleaseFocusTarget();
    detached_focus_target_ = nullptr;
}

openxaml::Element* XamlFocusScope::VisualRoot(
    openxaml::Element* element) noexcept {
    while (element && element->visual_parent()) element = element->visual_parent();
    return element;
}

bool XamlFocusScope::CanAttachRoot(openxaml::Element* root) const noexcept {
    if (root && root->visual_parent()) return false;
    if (!root) return true;
    std::lock_guard<std::mutex> guard(g_scope_mutex);
    const auto found = g_scopes.find(root);
    if (found == g_scopes.end()) return true;
    const std::shared_ptr<XamlFocusScope> owner = found->second.lock();
    return !owner || owner.get() == this;
}

bool XamlFocusScope::AttachRoot(openxaml::Element* root) noexcept {
    if (root && root->visual_parent()) return false;
    openxaml::Element* previous_root = nullptr;
    {
        std::lock_guard<std::mutex> guard(g_scope_mutex);
        if (root) {
            const auto found = g_scopes.find(root);
            if (found != g_scopes.end()) {
                const std::shared_ptr<XamlFocusScope> owner = found->second.lock();
                if (owner && owner.get() != this) return false;
            }
        }
        if (root_ == root) return true;
        previous_root = root_;
    }

    // Release can synchronously raise PointerCaptureLost, whose route lookup
    // takes g_scope_mutex and whose user handler may replace island content.
    // No manager or user callback may run while the registry lock is held.
    if (const std::shared_ptr<IslandInputManager> manager = manager_.lock()) {
        if (const auto capture = manager->pointer_capture()) {
            if (const auto self = weak_from_this().lock()) {
                manager->ReleasePointer(self, capture->pointer_id,
                                        capture->node_id);
            }
        }
    }

    {
        std::lock_guard<std::mutex> guard(g_scope_mutex);
        // CaptureLost may have re-entered AttachRoot. Never overwrite the
        // replacement chosen by the inner content transaction.
        if (root_ != previous_root) return root_ == root;
        if (root) {
            const auto found = g_scopes.find(root);
            if (found != g_scopes.end()) {
                const std::shared_ptr<XamlFocusScope> owner = found->second.lock();
                if (owner && owner.get() != this) return false;
            }
        }
        if (root) {
            try {
                const auto inserted = g_scopes.emplace(
                    root, weak_from_this());
                if (!inserted.second) inserted.first->second = weak_from_this();
            } catch (...) {
                // Preserve the former registry/root association when the new
                // map node cannot be allocated.
                return false;
            }
        }
        if (root_) {
            const auto old = g_scopes.find(root_);
            if (old != g_scopes.end()) {
                const std::shared_ptr<XamlFocusScope> owner = old->second.lock();
                if (!owner || owner.get() == this) g_scopes.erase(old);
            }
        }
        root_ = root;
        hover_node_ = 0;
    }
    ClearTapCandidate();
    ClearLastTap();

    // The former logical target is outside the newly hosted tree. Removing
    // the registry entry first ensures a reentrant Focus from LostFocus cannot
    // resurrect a detached control.
    SetDesired(nullptr, ABI::Windows::UI::Xaml::FocusState_Unfocused);
    Reconcile();
    return true;
}

void XamlFocusScope::DetachRoot(openxaml::Element* expected_root) noexcept {
    if (root_ && root_ == expected_root) {
        if (const std::shared_ptr<IslandInputManager> manager = manager_.lock()) {
            if (const auto capture = manager->pointer_capture()) {
                if (const auto self = weak_from_this().lock()) {
                    manager->ReleasePointer(self, capture->pointer_id,
                                            capture->node_id);
                }
            }
        }
    }
    {
        std::lock_guard<std::mutex> guard(g_scope_mutex);
        if (!root_ || root_ != expected_root) return;
        const auto found = g_scopes.find(root_);
        if (found != g_scopes.end()) {
            const std::shared_ptr<XamlFocusScope> owner = found->second.lock();
            if (!owner || owner.get() == this) g_scopes.erase(found);
        }
        root_ = nullptr;
        hover_node_ = 0;
    }
    ClearTapCandidate();
    ClearLastTap();
    SetDesired(nullptr, ABI::Windows::UI::Xaml::FocusState_Unfocused);
    Reconcile();
}

bool XamlFocusScope::TargetBelongsToRoot(
    const XamlFocusTarget& target) const noexcept {
    return root_ && VisualRoot(
        const_cast<XamlFocusTarget&>(target).FocusLayoutElement()) == root_;
}

void XamlFocusScope::BeginTapCandidate(
    XamlFocusTarget& target, const IslandPointerEvent& event) noexcept {
    ClearTapCandidate();
    target.RetainFocusTarget();
    tap_candidate_.target = &target;
    tap_candidate_.pointer_id = event.pointer_id;
    tap_candidate_.pressed_at = event.timestamp;
    tap_candidate_.x = event.x;
    tap_candidate_.y = event.y;
}

void XamlFocusScope::ClearTapCandidate() noexcept {
    XamlFocusTarget* const previous = tap_candidate_.target;
    tap_candidate_ = {};
    if (previous) previous->ReleaseFocusTarget();
}

void XamlFocusScope::ClearLastTap() noexcept {
    XamlFocusTarget* const previous = last_tap_target_;
    last_tap_target_ = nullptr;
    last_tap_at_ = 0;
    last_tap_x_ = 0.0;
    last_tap_y_ = 0.0;
    if (previous) previous->ReleaseFocusTarget();
}

void XamlFocusScope::UpdateTapMovement(
    const IslandPointerEvent& event) noexcept {
    if (!tap_candidate_.target ||
        event.pointer_id != tap_candidate_.pointer_id) return;
    const int max_dx = std::max(1, GetSystemMetrics(SM_CXDRAG) / 2);
    const int max_dy = std::max(1, GetSystemMetrics(SM_CYDRAG) / 2);
    if (std::abs(event.x - tap_candidate_.x) > max_dx ||
        std::abs(event.y - tap_candidate_.y) > max_dy)
        tap_candidate_.moved = true;
}

void XamlFocusScope::SetDesired(
    XamlFocusTarget* target,
    ABI::Windows::UI::Xaml::FocusState state) noexcept {
    if (desired_ == target) {
        desired_state_ = target ? state
                                : ABI::Windows::UI::Xaml::FocusState_Unfocused;
        reconcile_pending_ = true;
        return;
    }
    if (target) target->RetainFocusTarget();
    XamlFocusTarget* previous = desired_;
    desired_ = target;
    desired_state_ = target ? state
                            : ABI::Windows::UI::Xaml::FocusState_Unfocused;
    reconcile_pending_ = true;
    if (previous) previous->ReleaseFocusTarget();
}

bool XamlFocusScope::RequestFocus(
    XamlFocusTarget& target,
    ABI::Windows::UI::Xaml::FocusState state) noexcept {
    if (!target.HasFocusThreadAccess() || !ValidRequestedState(state) ||
        !TargetBelongsToRoot(target)) return false;
    const std::shared_ptr<IslandInputManager> manager = manager_.lock();
    if (!manager) return false;

    SetDesired(&target, state);
    if (host_focused_) {
        Reconcile();
    } else if (!manager->RequestHostFocus()) {
        if (desired_ == &target) {
            SetDesired(nullptr, ABI::Windows::UI::Xaml::FocusState_Unfocused);
            Reconcile();
        }
        return false;
    }
    return active_ == &target;
}

bool XamlFocusScope::CapturePointer(
    XamlFocusTarget& target, std::uint32_t pointer_id) noexcept {
    if (!target.HasFocusThreadAccess() || !pointer_id ||
        !TargetBelongsToRoot(target)) return false;
    const std::shared_ptr<IslandInputManager> manager = manager_.lock();
    if (!manager) return false;
    const std::shared_ptr<XamlFocusScope> self = weak_from_this().lock();
    return self && manager->CapturePointer(
        self, pointer_id, target.FocusLayoutElement()->render_node_id());
}

bool XamlFocusScope::ReleasePointer(
    XamlFocusTarget& target, std::uint32_t pointer_id) noexcept {
    if (!pointer_id || !TargetBelongsToRoot(target)) return false;
    const std::shared_ptr<IslandInputManager> manager = manager_.lock();
    if (!manager) return false;
    const std::shared_ptr<XamlFocusScope> self = weak_from_this().lock();
    return self && manager->ReleasePointer(
        self, pointer_id, target.FocusLayoutElement()->render_node_id());
}

HRESULT XamlFocusScope::CopyFocusedInspectable(
    IInspectable** value) noexcept {
    if (!value) return E_POINTER;
    *value = nullptr;
    XamlFocusTarget* const target = active_;
    return target ? target->CopyFocusInspectable(value) : S_OK;
}

void XamlFocusScope::OnIslandFocusChanged(bool focused) noexcept {
    if (host_focused_ == focused) return;
    host_focused_ = focused;
    reconcile_pending_ = true;
    Reconcile();
}

bool XamlFocusScope::OnIslandKey(const IslandKeyEvent& event) noexcept {
    try {
    RetainedRoute route = SnapshotRoute(active_);
    if (route.empty()) return false;
    auto* args = new (std::nothrow) KeyRoutedEventArgsObject(event);
    if (!args) {
        return false;
    }

    // Preview events tunnel from root to the focused target. Once handled,
    // ordinary registrations do not receive the event.
    for (auto current = route.targets.rbegin();
         current != route.targets.rend(); ++current) {
        (*current)->InvokeIslandKeyEvent(true, event.key_down, args);
        if (args->handled()) break;
    }
    if (!args->handled()) {
        for (XamlFocusTarget* target : route.targets) {
            target->InvokeIslandKeyEvent(false, event.key_down, args);
            if (args->handled()) break;
        }
    }
    const bool handled = args->handled();
    static_cast<ABI::Windows::UI::Xaml::Input::IKeyRoutedEventArgs*>(args)
        ->Release();
    return handled;
    } catch (...) {
        return false;
    }
}

bool XamlFocusScope::OnIslandCharacter(
    const IslandCharacterEvent& event) noexcept {
    // CharacterReceived's ABI is one UTF-16 code unit. WM_CHAR surrogate
    // units remain representable; a supplementary WM_UNICHAR scalar is not
    // silently truncated into a different character.
    if (event.character > 0xffff) return false;
    try {
    RetainedRoute route = SnapshotRoute(active_);
    if (route.empty()) return false;
    auto* args = new (std::nothrow) CharacterRoutedEventArgsObject(event);
    if (!args) {
        return false;
    }
    for (XamlFocusTarget* target : route.targets) {
        target->InvokeIslandCharacterEvent(args);
        if (args->handled()) break;
    }
    const bool handled = args->handled();
    static_cast<ABI::Windows::UI::Xaml::Input::ICharacterReceivedRoutedEventArgs*>(
        args)->Release();
    return handled;
    } catch (...) {
        return false;
    }
}

bool XamlFocusScope::OnIslandPointer(
    const IslandPointerEvent& event) noexcept {
    try {
        if (!root_) return false;
        has_last_pointer_event_ = true;
        last_pointer_event_ = event;

        TargetLease physical_target(HitTest(*root_, {event.x, event.y}));
        XamlFocusTarget* routed_target = nullptr;
        if (event.captured_node) {
            routed_target = TargetByNode(root_, event.captured_node);
        } else if (physical_target.target) {
            physical_target.target->RetainFocusTarget();
            routed_target = physical_target.target;
        }
        TargetLease target(routed_target);
        TargetLease root_owner(RegisteredTarget(root_));
        if (!root_owner.target) {
            if (event.kind == IslandPointerEventKind::Released ||
                event.kind == IslandPointerEventKind::Canceled)
                ClearTapCandidate();
            return false;
        }

        if (event.kind == IslandPointerEventKind::Pressed) {
            if (event.update_kind ==
                    IslandPointerUpdateKind::LeftButtonPressed &&
                physical_target.target) {
                BeginTapCandidate(*physical_target.target, event);
            } else {
                ClearTapCandidate();
            }
        } else if (event.kind == IslandPointerEventKind::Moved ||
                   event.kind == IslandPointerEventKind::Released) {
            UpdateTapMovement(event);
        } else if (event.kind == IslandPointerEventKind::Canceled) {
            ClearTapCandidate();
        }

        if (!target.target) {
            if (event.kind == IslandPointerEventKind::Moved &&
                !event.captured_node && hover_node_) {
                TargetLease old(TargetByNode(root_, hover_node_));
                hover_node_ = 0;
                if (old.target) {
                    IslandPointerEvent exited = event;
                    exited.kind = IslandPointerEventKind::Exited;
                    return DispatchPointerToRoute(
                        old.target, root_owner.target, exited);
                }
            }
            if (event.kind == IslandPointerEventKind::Released)
                ClearTapCandidate();
            return false;
        }

        // Hover transitions are derived from retained hit-test identity, not
        // from Win32 child-window enter tracking. This makes overlapping/z
        // ordered elements receive balanced Exited then Entered routes.
        if (event.kind == IslandPointerEventKind::Moved &&
            !event.captured_node) {
            const std::uint64_t next_hover =
                target.target->FocusLayoutElement()->render_node_id();
            if (hover_node_ != next_hover) {
                if (hover_node_) {
                    TargetLease old(TargetByNode(root_, hover_node_));
                    if (old.target) {
                        IslandPointerEvent exited = event;
                        exited.kind = IslandPointerEventKind::Exited;
                        (void)DispatchPointerToRoute(
                            old.target, root_owner.target, exited);
                    }
                }
                hover_node_ = next_hover;
                IslandPointerEvent entered = event;
                entered.kind = IslandPointerEventKind::Entered;
                (void)DispatchPointerToRoute(
                    target.target, root_owner.target, entered);
            }
        }

        const std::uint64_t tap_timeout =
            static_cast<std::uint64_t>(GetDoubleClickTime()) * 1000u;
        const bool tap_eligible =
            event.kind == IslandPointerEventKind::Released &&
            event.update_kind == IslandPointerUpdateKind::LeftButtonReleased &&
            tap_candidate_.target && !tap_candidate_.moved &&
            tap_candidate_.pointer_id == event.pointer_id &&
            physical_target.target == tap_candidate_.target &&
            event.timestamp >= tap_candidate_.pressed_at && tap_timeout &&
            event.timestamp - tap_candidate_.pressed_at <= tap_timeout;
        TargetLease completed_tap;
        if (tap_eligible) {
            tap_candidate_.target->RetainFocusTarget();
            completed_tap.target = tap_candidate_.target;
        }

        releasing_tap_candidate_ = tap_eligible;
        bool handled = false;
        try {
            handled = DispatchPointerToRoute(
                target.target, root_owner.target, event);
        } catch (...) {
            releasing_tap_candidate_ = false;
            throw;
        }
        releasing_tap_candidate_ = false;
        if (event.kind == IslandPointerEventKind::Released)
            ClearTapCandidate();

        if (!completed_tap.target ||
            !TargetBelongsToRoot(*completed_tap.target)) return handled;

        const openxaml::Point position{event.x, event.y};
        handled = DispatchTapToRoute(
            completed_tap.target, root_owner.target,
            IslandTapEventKind::Tapped, position) || handled;

        const int max_dx =
            std::max(1, GetSystemMetrics(SM_CXDOUBLECLK) / 2);
        const int max_dy =
            std::max(1, GetSystemMetrics(SM_CYDOUBLECLK) / 2);
        const bool double_tap =
            TargetBelongsToRoot(*completed_tap.target) &&
            last_tap_target_ == completed_tap.target &&
            event.timestamp >= last_tap_at_ &&
            event.timestamp - last_tap_at_ <= tap_timeout &&
            std::abs(event.x - last_tap_x_) <= max_dx &&
            std::abs(event.y - last_tap_y_) <= max_dy;
        if (double_tap) {
            handled = DispatchTapToRoute(
                completed_tap.target, root_owner.target,
                IslandTapEventKind::DoubleTapped, position) || handled;
            ClearLastTap();
        } else {
            ClearLastTap();
            completed_tap.target->RetainFocusTarget();
            last_tap_target_ = completed_tap.target;
            last_tap_at_ = event.timestamp;
            last_tap_x_ = event.x;
            last_tap_y_ = event.y;
        }
        return handled;
    } catch (...) {
        // The sink boundary is noexcept. Allocation failure or a malformed
        // retained tree refuses this input sample instead of terminating the
        // host process.
        return false;
    }
}

void XamlFocusScope::OnIslandPointerCaptureLost(
    const IslandPointerCapture& capture) noexcept {
    try {
    if (!releasing_tap_candidate_) ClearTapCandidate();
    if (!root_) return;
    XamlFocusTarget* captured_target = nullptr;
    if (detached_capture_target_ &&
        detached_capture_target_->FocusLayoutElement()->render_node_id() ==
            capture.node_id) {
        detached_capture_target_->RetainFocusTarget();
        captured_target = detached_capture_target_;
    } else {
        captured_target = TargetByNode(root_, capture.node_id);
    }
    TargetLease target(captured_target);
    TargetLease root_owner(RegisteredTarget(root_));
    if (!target.target || !root_owner.target) return;

    IslandPointerEvent event = has_last_pointer_event_
        ? last_pointer_event_ : IslandPointerEvent{};
    event.kind = IslandPointerEventKind::CaptureLost;
    event.pointer_id = capture.pointer_id;
    event.captured_node = capture.node_id;
    event.update_kind = IslandPointerUpdateKind::Other;
    if (!detached_capture_route_.empty() && detached_capture_target_ &&
        detached_capture_target_->FocusLayoutElement()->render_node_id() ==
            capture.node_id) {
        auto* args = new (std::nothrow) PointerRoutedEventArgsObject(
            event, root_owner.target);
        if (args && args->valid()) {
            for (XamlFocusTarget* current : detached_capture_route_) {
                current->InvokeIslandPointerEvent(
                    IslandPointerEventKind::CaptureLost, args);
                if (args->handled()) break;
            }
        }
        if (args) {
            static_cast<ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs*>(
                args)->Release();
        }
    } else {
        (void)DispatchPointerToRoute(target.target, root_owner.target, event);
    }
    } catch (...) {
        // Capture state was already cleared by IslandInputManager. Refuse a
        // diagnostic event that cannot be allocated without violating the
        // noexcept host boundary.
    }
}

void XamlFocusScope::PrepareVisualSubtreeDetached(
    openxaml::Element* subtree, openxaml::Element* former_root) noexcept {
    for (XamlFocusTarget* target : detached_capture_route_)
        target->ReleaseFocusTarget();
    detached_capture_route_.clear();
    for (XamlFocusTarget* target : detached_focus_route_)
        target->ReleaseFocusTarget();
    detached_focus_route_.clear();
    if (detached_focus_target_) detached_focus_target_->ReleaseFocusTarget();
    detached_focus_target_ = nullptr;
    if (!subtree || root_ != former_root) return;

    if (tap_candidate_.target && IsInSubtree(
            tap_candidate_.target->FocusLayoutElement(), subtree))
        ClearTapCandidate();
    if (last_tap_target_ && IsInSubtree(
            last_tap_target_->FocusLayoutElement(), subtree))
        ClearLastTap();

    if (active_ && IsInSubtree(active_->FocusLayoutElement(), subtree)) {
        try {
            RetainedRoute focus_route = SnapshotRoute(active_);
            detached_focus_route_ = std::move(focus_route.targets);
            focus_route.targets.clear();
            active_->RetainFocusTarget();
            detached_focus_target_ = active_;
        } catch (...) {
            for (XamlFocusTarget* target : detached_focus_route_)
                target->ReleaseFocusTarget();
            detached_focus_route_.clear();
        }
    }
    const std::shared_ptr<IslandInputManager> manager = manager_.lock();
    if (!manager) return;
    const auto capture = manager->pointer_capture();
    if (!capture) return;
    TargetLease captured(TargetByNode(root_, capture->node_id));
    if (!captured.target || !IsInSubtree(
            captured.target->FocusLayoutElement(), subtree)) return;
    try {
        RetainedRoute route = SnapshotRoute(captured.target);
        detached_capture_route_ = std::move(route.targets);
        route.targets.clear();
    } catch (...) {
        for (XamlFocusTarget* target : detached_capture_route_)
            target->ReleaseFocusTarget();
        detached_capture_route_.clear();
    }
}

void XamlFocusScope::OnVisualSubtreeDetached(
    openxaml::Element* subtree, openxaml::Element* former_root) noexcept {
    if (!subtree || root_ != former_root) return;

    // Capture is released first while the detached projection is still held
    // by its collection. This produces exactly one CaptureLost and balances
    // User32 capture before removal can destroy the target.
    if (const std::shared_ptr<IslandInputManager> manager = manager_.lock()) {
        if (const auto capture = manager->pointer_capture()) {
            XamlFocusTarget* captured = TargetByNodeInSubtree(
                subtree, capture->node_id);
            if (captured) {
                captured->RetainFocusTarget();
                XamlFocusTarget* previous_override =
                    detached_capture_target_;
                detached_capture_target_ = captured;
                if (const auto self = weak_from_this().lock()) {
                    manager->ReleasePointer(self, capture->pointer_id,
                                            capture->node_id);
                }
                if (detached_capture_target_ == captured)
                    detached_capture_target_ = previous_override;
                for (XamlFocusTarget* target : detached_capture_route_)
                    target->ReleaseFocusTarget();
                detached_capture_route_.clear();
                captured->ReleaseFocusTarget();
                captured->ReleaseFocusTarget();
            }
        }
    }

    const bool desired_removed = desired_ &&
        IsInSubtree(desired_->FocusLayoutElement(), subtree);
    const bool active_removed = active_ &&
        IsInSubtree(active_->FocusLayoutElement(), subtree);
    if (desired_removed || active_removed) {
        SetDesired(nullptr, ABI::Windows::UI::Xaml::FocusState_Unfocused);
        Reconcile();
    }
}

void XamlFocusScope::Reconcile() noexcept {
    if (reconciling_) {
        reconcile_pending_ = true;
        return;
    }
    reconciling_ = true;
    try {
    do {
        reconcile_pending_ = false;
        XamlFocusTarget* wanted = host_focused_ ? desired_ : nullptr;
        if (active_ == wanted) {
            if (active_) {
                active_->SetIslandFocusState(desired_state_);
                active_state_ = desired_state_;
            }
            continue;
        }

        // Pre-transition events observe the old/new projections before any
        // FocusState or Got/Lost mutation. Losing may redirect New, Getting
        // observes that redirection, and cancellation leaves the old target
        // active with its original state.
        ABI::Windows::UI::Xaml::IDependencyObject* old_dependency =
            CopyDependency(active_);
        ABI::Windows::UI::Xaml::IDependencyObject* new_dependency =
            CopyDependency(wanted);
        bool canceled = false;
        if (active_) {
            auto* losing_args = new (std::nothrow) LosingFocusArgs(
                IID_ILosingFocusEventArgs,
                L"Windows.UI.Xaml.Input.LosingFocusEventArgs",
                old_dependency, new_dependency, desired_state_);
            if (losing_args) {
                RetainedRoute route = SnapshotRoute(active_);
                for (XamlFocusTarget* target : route.targets)
                    target->InvokeIslandLosingFocus(losing_args);
                canceled = losing_args->canceled();
                ABI::Windows::UI::Xaml::IDependencyObject* redirected =
                    losing_args->new_element();
                if (redirected) redirected->AddRef();
                if (new_dependency) new_dependency->Release();
                new_dependency = redirected;
                static_cast<ABI::Windows::UI::Xaml::Input::ILosingFocusEventArgs*>(
                    losing_args)->Release();
            }
        }
        if (!canceled && wanted) {
            auto* getting_args = new (std::nothrow) GettingFocusArgs(
                IID_IGettingFocusEventArgs,
                L"Windows.UI.Xaml.Input.GettingFocusEventArgs",
                old_dependency, new_dependency, desired_state_);
            if (getting_args) {
                RetainedRoute route = SnapshotRoute(wanted);
                for (XamlFocusTarget* target : route.targets)
                    target->InvokeIslandGettingFocus(getting_args);
                canceled = getting_args->canceled();
                ABI::Windows::UI::Xaml::IDependencyObject* redirected =
                    getting_args->new_element();
                if (redirected) redirected->AddRef();
                if (new_dependency) new_dependency->Release();
                new_dependency = redirected;
                static_cast<ABI::Windows::UI::Xaml::Input::IGettingFocusEventArgs*>(
                    getting_args)->Release();
            }
        }
        if (old_dependency) old_dependency->Release();

        TargetLease redirected(TargetForDependency(new_dependency));
        if (new_dependency) new_dependency->Release();
        if (!canceled && redirected.target != wanted) {
            if (redirected.target && !TargetBelongsToRoot(*redirected.target))
                canceled = true;
            else {
                SetDesired(redirected.target, desired_state_);
                wanted = redirected.target;
            }
        }
        if (canceled) {
            SetDesired(active_, active_state_);
            reconcile_pending_ = false;
            continue;
        }

        if (active_) {
            TargetLease losing(active_);
            active_ = nullptr;
            active_state_ = ABI::Windows::UI::Xaml::FocusState_Unfocused;
            losing.target->SetIslandFocusState(
                ABI::Windows::UI::Xaml::FocusState_Unfocused);
            // Snapshot every registered element on the route before invoking
            // user code. Each snapshot entry owns a COM reference, so handler
            // removal, content replacement, and object Release are safe.
            if (detached_focus_target_ == losing.target &&
                !detached_focus_route_.empty()) {
                for (XamlFocusTarget* target : detached_focus_route_)
                    target->InvokeIslandFocusEvent(false);
                for (XamlFocusTarget* target : detached_focus_route_)
                    target->ReleaseFocusTarget();
                detached_focus_route_.clear();
                detached_focus_target_->ReleaseFocusTarget();
                detached_focus_target_ = nullptr;
            } else {
                RetainedRoute route = SnapshotRoute(losing.target);
                for (XamlFocusTarget* target : route.targets)
                    target->InvokeIslandFocusEvent(false);
            }
            reconcile_pending_ = true;
            continue;
        }

        if (wanted) {
            // Retain independently from desired_: a reentrant GotFocus handler
            // may replace or clear the requested target.
            wanted->RetainFocusTarget();
            active_ = wanted;
            wanted->SetIslandFocusState(desired_state_);
            active_state_ = desired_state_;
            RetainedRoute route = SnapshotRoute(wanted);
            for (XamlFocusTarget* target : route.targets)
                target->InvokeIslandFocusEvent(true);
            reconcile_pending_ = true;
        }
    } while (reconcile_pending_);
    } catch (...) {
        // Focus state/ref ownership is updated before each optional routed
        // notification snapshot. Allocation failure may omit that diagnostic
        // event, but must never terminate the HWND/content transaction.
        reconcile_pending_ = false;
    }
    reconciling_ = false;
}

bool RequestXamlFocus(XamlFocusTarget& target,
                      ABI::Windows::UI::Xaml::FocusState state) noexcept {
    if (!target.HasFocusThreadAccess()) return false;
    RegisterXamlFocusTarget(target);
    openxaml::Element* const root = XamlFocusScope::VisualRoot(
        target.FocusLayoutElement());
    if (!root) return false;
    std::shared_ptr<XamlFocusScope> scope;
    {
        std::lock_guard<std::mutex> guard(g_scope_mutex);
        const auto found = g_scopes.find(root);
        if (found == g_scopes.end()) return false;
        scope = found->second.lock();
        if (!scope) {
            g_scopes.erase(found);
            return false;
        }
    }
    return scope->RequestFocus(target, state);
}

bool RequestXamlFocus(openxaml::Element* element,
                      ABI::Windows::UI::Xaml::FocusState state) noexcept {
    if (!element) return false;
    XamlFocusTarget* target = nullptr;
    {
        std::lock_guard<std::mutex> guard(g_scope_mutex);
        const auto found = g_targets.find(element);
        if (found == g_targets.end()) return false;
        target = found->second;
        target->RetainFocusTarget();
    }
    const bool focused = RequestXamlFocus(*target, state);
    target->ReleaseFocusTarget();
    return focused;
}

bool CaptureXamlPointer(XamlFocusTarget& target,
                        std::uint32_t pointer_id) noexcept {
    RegisterXamlFocusTarget(target);
    openxaml::Element* const root = XamlFocusScope::VisualRoot(
        target.FocusLayoutElement());
    if (!root) return false;
    std::shared_ptr<XamlFocusScope> scope;
    {
        std::lock_guard<std::mutex> guard(g_scope_mutex);
        const auto found = g_scopes.find(root);
        if (found == g_scopes.end()) return false;
        scope = found->second.lock();
        if (!scope) {
            g_scopes.erase(found);
            return false;
        }
    }
    return scope->CapturePointer(target, pointer_id);
}

bool ReleaseXamlPointer(XamlFocusTarget& target,
                        std::uint32_t pointer_id) noexcept {
    openxaml::Element* const root = XamlFocusScope::VisualRoot(
        target.FocusLayoutElement());
    if (!root) return false;
    std::shared_ptr<XamlFocusScope> scope;
    {
        std::lock_guard<std::mutex> guard(g_scope_mutex);
        const auto found = g_scopes.find(root);
        if (found == g_scopes.end()) return false;
        scope = found->second.lock();
        if (!scope) {
            g_scopes.erase(found);
            return false;
        }
    }
    return scope->ReleasePointer(target, pointer_id);
}

HRESULT CopyFocusedXamlElementForRoot(openxaml::Element* root,
                                     IInspectable** value) noexcept {
    if (!value) return E_POINTER;
    *value = nullptr;
    root = XamlFocusScope::VisualRoot(root);
    if (!root) return S_OK;
    std::shared_ptr<XamlFocusScope> scope;
    {
        std::lock_guard<std::mutex> guard(g_scope_mutex);
        const auto found = g_scopes.find(root);
        if (found == g_scopes.end()) return S_OK;
        scope = found->second.lock();
        if (!scope) {
            g_scopes.erase(found);
            return S_OK;
        }
    }
    return scope->CopyFocusedInspectable(value);
}

HRESULT CopyInheritedXamlRoot(
    openxaml::Element* element,
    ABI::Windows::UI::Xaml::IXamlRoot** value) noexcept {
    if (!value) return E_POINTER;
    *value = nullptr;
    element = XamlFocusScope::VisualRoot(element);
    if (!element) return S_OK;
    XamlFocusTarget* target = nullptr;
    {
        std::lock_guard<std::mutex> guard(g_scope_mutex);
        const auto found = g_targets.find(element);
        if (found == g_targets.end()) return S_OK;
        target = found->second;
        target->RetainFocusTarget();
    }
    const HRESULT copied = target->CopyOwnXamlRoot(value);
    target->ReleaseFocusTarget();
    return copied;
}

void RegisterXamlFocusTarget(XamlFocusTarget& target) noexcept {
    openxaml::Element* const element = target.FocusLayoutElement();
    if (!element) return;
    std::lock_guard<std::mutex> guard(g_scope_mutex);
    try {
        g_targets[element] = &target;
    } catch (...) {
        // Registration is an optional projection index. Callers subsequently
        // fail focus/hit-test lookup explicitly when allocation is exhausted.
    }
}

void UnregisterXamlFocusTarget(XamlFocusTarget& target) noexcept {
    std::lock_guard<std::mutex> guard(g_scope_mutex);
    for (auto found = g_targets.begin(); found != g_targets.end();) {
        if (found->second == &target) {
            found = g_targets.erase(found);
        } else {
            ++found;
        }
    }
}

void NotifyXamlVisualSubtreeDetached(
    openxaml::Element* subtree, openxaml::Element* former_root) noexcept {
    if (!subtree || !former_root) return;
    std::shared_ptr<XamlFocusScope> scope;
    {
        std::lock_guard<std::mutex> guard(g_scope_mutex);
        const auto found = g_scopes.find(former_root);
        if (found == g_scopes.end()) return;
        scope = found->second.lock();
        if (!scope) {
            g_scopes.erase(found);
            return;
        }
    }
    // Reconciliation raises user events and can mutate the tree, so it must
    // run after the registry lock is released.
    scope->OnVisualSubtreeDetached(subtree, former_root);
}

void PrepareXamlVisualSubtreeDetached(
    openxaml::Element* subtree, openxaml::Element* former_root) noexcept {
    if (!subtree || !former_root) return;
    std::shared_ptr<XamlFocusScope> scope;
    {
        std::lock_guard<std::mutex> guard(g_scope_mutex);
        const auto found = g_scopes.find(former_root);
        if (found == g_scopes.end()) return;
        scope = found->second.lock();
        if (!scope) {
            g_scopes.erase(found);
            return;
        }
    }
    scope->PrepareVisualSubtreeDetached(subtree, former_root);
}

}  // namespace openxaml::winrt
