// Logical XAML focus for one hosted visual tree.
//
// The HWND/input half is IslandInputManager. This half owns the logical
// target, FocusState, and routed GotFocus/LostFocus transition. Keeping the
// two halves separate avoids putting COM objects in the window procedure and
// lets a detached tree stop resolving Focus immediately.

#ifndef OPENXAML_XAMLCORE_XAML_FOCUS_H
#define OPENXAML_XAMLCORE_XAML_FOCUS_H

#include "island_input_manager.h"
#include "sdk.h"

#include <memory>
#include <vector>

namespace openxaml {
class Element;
}

namespace openxaml::winrt {

enum class IslandTapEventKind {
    Tapped,
    DoubleTapped,
};

class XamlFocusTarget {
public:
    virtual ~XamlFocusTarget() = default;
    virtual openxaml::Element* FocusLayoutElement() noexcept = 0;
    virtual bool HasFocusThreadAccess() const noexcept = 0;
    virtual void RetainFocusTarget() noexcept = 0;
    virtual void ReleaseFocusTarget() noexcept = 0;
    virtual HRESULT CopyFocusInspectable(IInspectable** value) noexcept = 0;
    virtual HRESULT CopyOwnXamlRoot(
        ABI::Windows::UI::Xaml::IXamlRoot** value) noexcept = 0;
    virtual void SetIslandFocusState(
        ABI::Windows::UI::Xaml::FocusState state) noexcept = 0;
    virtual void InvokeIslandFocusEvent(bool gained) noexcept = 0;
    virtual void InvokeIslandLosingFocus(
        ABI::Windows::UI::Xaml::Input::ILosingFocusEventArgs*) noexcept {}
    virtual void InvokeIslandGettingFocus(
        ABI::Windows::UI::Xaml::Input::IGettingFocusEventArgs*) noexcept {}
    virtual void InvokeIslandKeyEvent(
        bool preview, bool key_down,
        ABI::Windows::UI::Xaml::Input::IKeyRoutedEventArgs* args) noexcept = 0;
    virtual void InvokeIslandCharacterEvent(
        ABI::Windows::UI::Xaml::Input::ICharacterReceivedRoutedEventArgs* args)
        noexcept = 0;
    virtual void InvokeIslandPointerEvent(
        IslandPointerEventKind,
        ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs*) noexcept {}
    virtual void InvokeIslandTapEvent(
        IslandTapEventKind,
        ABI::Windows::UI::Xaml::Input::ITappedRoutedEventArgs*) noexcept {}
};

class XamlFocusScope final
    : public IslandInputSink,
      public std::enable_shared_from_this<XamlFocusScope> {
public:
    explicit XamlFocusScope(std::weak_ptr<IslandInputManager> manager) noexcept;
    ~XamlFocusScope() override;

    XamlFocusScope(const XamlFocusScope&) = delete;
    XamlFocusScope& operator=(const XamlFocusScope&) = delete;

    // CanAttachRoot is a non-mutating transaction preflight. AttachRoot
    // repeats the check and atomically publishes the new root association.
    bool CanAttachRoot(openxaml::Element* root) const noexcept;
    bool AttachRoot(openxaml::Element* root) noexcept;
    void DetachRoot(openxaml::Element* expected_root) noexcept;

    bool RequestFocus(XamlFocusTarget& target,
                      ABI::Windows::UI::Xaml::FocusState state) noexcept;
    bool CapturePointer(XamlFocusTarget& target,
                        std::uint32_t pointer_id) noexcept;
    bool ReleasePointer(XamlFocusTarget& target,
                        std::uint32_t pointer_id) noexcept;
    HRESULT CopyFocusedInspectable(IInspectable** value) noexcept;

    static openxaml::Element* VisualRoot(openxaml::Element* element) noexcept;

    void OnIslandFocusChanged(bool focused) noexcept override;
    bool OnIslandKey(const IslandKeyEvent& event) noexcept override;
    bool OnIslandCharacter(const IslandCharacterEvent& event) noexcept override;
    bool OnIslandPointer(const IslandPointerEvent& event) noexcept override;
    void OnIslandPointerCaptureLost(
        const IslandPointerCapture& capture) noexcept override;
    void OnVisualSubtreeDetached(openxaml::Element* subtree,
                                 openxaml::Element* former_root) noexcept;
    void PrepareVisualSubtreeDetached(openxaml::Element* subtree,
                                      openxaml::Element* former_root) noexcept;

private:
    struct TapCandidate {
        XamlFocusTarget* target = nullptr;
        std::uint32_t pointer_id = 0;
        std::uint64_t pressed_at = 0;
        double x = 0.0;
        double y = 0.0;
        bool moved = false;
    };

    bool TargetBelongsToRoot(const XamlFocusTarget& target) const noexcept;
    void SetDesired(XamlFocusTarget* target,
                    ABI::Windows::UI::Xaml::FocusState state) noexcept;
    void BeginTapCandidate(XamlFocusTarget& target,
                           const IslandPointerEvent& event) noexcept;
    void ClearTapCandidate() noexcept;
    void ClearLastTap() noexcept;
    void UpdateTapMovement(const IslandPointerEvent& event) noexcept;
    void Reconcile() noexcept;

    std::weak_ptr<IslandInputManager> manager_;
    openxaml::Element* root_ = nullptr;
    XamlFocusTarget* desired_ = nullptr;
    XamlFocusTarget* active_ = nullptr;
    ABI::Windows::UI::Xaml::FocusState desired_state_ =
        ABI::Windows::UI::Xaml::FocusState_Unfocused;
    ABI::Windows::UI::Xaml::FocusState active_state_ =
        ABI::Windows::UI::Xaml::FocusState_Unfocused;
    bool host_focused_ = false;
    IslandPointerEvent last_pointer_event_;
    bool has_last_pointer_event_ = false;
    std::uint64_t hover_node_ = 0;
    TapCandidate tap_candidate_;
    XamlFocusTarget* last_tap_target_ = nullptr;
    std::uint64_t last_tap_at_ = 0;
    double last_tap_x_ = 0.0;
    double last_tap_y_ = 0.0;
    bool releasing_tap_candidate_ = false;
    XamlFocusTarget* detached_capture_target_ = nullptr;
    std::vector<XamlFocusTarget*> detached_capture_route_;
    XamlFocusTarget* detached_focus_target_ = nullptr;
    std::vector<XamlFocusTarget*> detached_focus_route_;
    bool reconciling_ = false;
    bool reconcile_pending_ = false;
};

// Called by IControl::Focus. Resolution through the hosted-root registry is
// what makes a detached or foreign-island element return false.
bool RequestXamlFocus(XamlFocusTarget& target,
                      ABI::Windows::UI::Xaml::FocusState state) noexcept;
bool RequestXamlFocus(openxaml::Element* element,
                      ABI::Windows::UI::Xaml::FocusState state) noexcept;
bool CaptureXamlPointer(XamlFocusTarget& target,
                        std::uint32_t pointer_id) noexcept;
bool ReleaseXamlPointer(XamlFocusTarget& target,
                        std::uint32_t pointer_id) noexcept;
HRESULT CopyFocusedXamlElementForRoot(openxaml::Element* root,
                                     IInspectable** value) noexcept;
HRESULT CopyInheritedXamlRoot(
    openxaml::Element* element,
    ABI::Windows::UI::Xaml::IXamlRoot** value) noexcept;
void RegisterXamlFocusTarget(XamlFocusTarget& target) noexcept;
void UnregisterXamlFocusTarget(XamlFocusTarget& target) noexcept;
void NotifyXamlVisualSubtreeDetached(openxaml::Element* subtree,
                                     openxaml::Element* former_root) noexcept;
void PrepareXamlVisualSubtreeDetached(openxaml::Element* subtree,
                                      openxaml::Element* former_root) noexcept;

}  // namespace openxaml::winrt

#endif  // OPENXAML_XAMLCORE_XAML_FOCUS_H
