// Platform-neutral retained hit-test/routing checks using mock projections.

#include "xaml_focus.h"

#include "canvas.h"
#include "element.h"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace wux = ABI::Windows::UI::Xaml;
namespace wuxi = ABI::Windows::UI::Xaml::Input;

namespace {

int failures = 0;
void Check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

class TestElement final : public openxaml::Element {
public:
    explicit TestElement(std::string name) : name_(std::move(name)) {}
    std::string TypeName() const override { return name_; }
    const std::vector<std::string>& PropertyOwners() const override {
        static const std::vector<std::string> owners{"Element"};
        return owners;
    }
    bool IsLayoutElement() const override { return true; }

    TestElement* Add(std::unique_ptr<TestElement> child, openxaml::Rect slot) {
        TestElement* result = child.get();
        Adopt(*child);
        children_.push_back({std::move(child), slot});
        return result;
    }
    std::vector<openxaml::Element*> Children() const override {
        std::vector<openxaml::Element*> result;
        for (const auto& child : children_) result.push_back(child.element.get());
        return result;
    }
    void Detach(TestElement& child) { DetachVisualChild(child); }

protected:
    openxaml::Size MeasureOverride(openxaml::Size available) override {
        for (auto& child : children_)
            child.element->Measure({child.slot.width, child.slot.height});
        return available;
    }
    openxaml::Size ArrangeOverride(openxaml::Size final_size) override {
        for (auto& child : children_) child.element->Arrange(child.slot);
        return final_size;
    }

private:
    struct Child {
        std::unique_ptr<TestElement> element;
        openxaml::Rect slot;
    };
    std::string name_;
    std::vector<Child> children_;
};

class MockTarget final : public openxaml::winrt::XamlFocusTarget {
public:
    MockTarget(openxaml::Element& element, char name,
               std::string& route,
               std::shared_ptr<openxaml::winrt::XamlFocusScope> scope)
        : element_(element), name_(name), route_(route), scope_(std::move(scope)) {}

    openxaml::Element* FocusLayoutElement() noexcept override { return &element_; }
    bool HasFocusThreadAccess() const noexcept override { return true; }
    void RetainFocusTarget() noexcept override { ++retains_; }
    void ReleaseFocusTarget() noexcept override { --retains_; }
    HRESULT CopyFocusInspectable(IInspectable** value) noexcept override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return E_NOTIMPL;
    }
    HRESULT CopyOwnXamlRoot(wux::IXamlRoot** value) noexcept override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    void SetIslandFocusState(wux::FocusState) noexcept override {}
    void InvokeIslandFocusEvent(bool) noexcept override {}
    void InvokeIslandKeyEvent(bool, bool, wuxi::IKeyRoutedEventArgs*) noexcept override {}
    void InvokeIslandCharacterEvent(
        wuxi::ICharacterReceivedRoutedEventArgs*) noexcept override {}
    void InvokeIslandPointerEvent(
        openxaml::winrt::IslandPointerEventKind kind,
        wuxi::IPointerRoutedEventArgs* args) noexcept override {
        route_.push_back(name_);
        if (!args) return;
        wuxi::IPointer* pointer = nullptr;
        ABI::Windows::UI::Input::IPointerPoint* point = nullptr;
        args->get_Pointer(&pointer);
        args->GetCurrentPoint(nullptr, &point);
        UINT32 id = 0;
        ABI::Windows::Foundation::Point position{};
        UINT64 timestamp = 0;
        if (pointer) pointer->get_PointerId(&id);
        if (point) {
            point->get_Position(&position);
            point->get_Timestamp(&timestamp);
        }
        fields_ok = fields_ok && id == 1 && timestamp == expected_timestamp &&
                    position.X == expected_x && position.Y == expected_y;
        if (capture_on_press && kind ==
                openxaml::winrt::IslandPointerEventKind::Pressed) {
            captured = scope_->CapturePointer(*this, id);
        }
        if (release_on_up && kind ==
                openxaml::winrt::IslandPointerEventKind::Released) {
            released = scope_->ReleasePointer(*this, id);
        }
        if (handle) args->put_Handled(1);
        if (point) point->Release();
        if (pointer) pointer->Release();
    }

    bool capture_on_press = false;
    bool release_on_up = false;
    bool handle = false;
    bool captured = false;
    bool released = false;
    bool fields_ok = true;
    float expected_x = 0.0f;
    float expected_y = 0.0f;
    UINT64 expected_timestamp = 0;

private:
    openxaml::Element& element_;
    char name_;
    std::string& route_;
    std::shared_ptr<openxaml::winrt::XamlFocusScope> scope_;
    int retains_ = 0;
};

}  // namespace

int main() {
    using namespace openxaml::winrt;
    auto root = std::make_unique<TestElement>("root");
    TestElement* bottom = root->Add(std::make_unique<TestElement>("bottom"),
                                    {10.0, 10.0, 50.0, 50.0});
    TestElement* top = root->Add(std::make_unique<TestElement>("top"),
                                 {10.0, 10.0, 50.0, 50.0});
    openxaml::Canvas::SetZIndex(*bottom, 1);
    openxaml::Canvas::SetZIndex(*top, 2);
    root->Measure({100.0, 100.0});
    root->Arrange({0.0, 0.0, 100.0, 100.0});

    auto manager = std::make_shared<IslandInputManager>();
    auto scope = std::make_shared<XamlFocusScope>(manager);
    Check(scope->AttachRoot(root.get()), "attach retained hit-test root");
    manager->Attach(scope);
    bool host_captured = false;
    manager->SetHostPointerCaptureCallbacks(
        [&] { host_captured = true; return true; },
        [&] { host_captured = false; });

    std::string route;
    MockTarget root_target(*root, 'R', route, scope);
    MockTarget bottom_target(*bottom, 'B', route, scope);
    MockTarget top_target(*top, 'T', route, scope);
    RegisterXamlFocusTarget(root_target);
    RegisterXamlFocusTarget(bottom_target);
    RegisterXamlFocusTarget(top_target);

    top_target.capture_on_press = true;
    top_target.expected_x = 15.0f;
    top_target.expected_y = 16.0f;
    top_target.expected_timestamp = 9000;
    root_target.expected_x = 15.0f;
    root_target.expected_y = 16.0f;
    root_target.expected_timestamp = 9000;
    IslandInputResult pressed = manager->ForwardPointerMessage(
        nullptr, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(15, 16), 9000);
    Check(pressed.recognized && !pressed.handled, "pressed recognized and unhandled");
    Check(route == "TR", "higher z child hit then bubbles to root");
    Check(top_target.fields_ok && root_target.fields_ok,
          "mock bridge receives concrete pointer fields");
    Check(top_target.captured && host_captured && manager->pointer_capture(),
          "mock target captures pointer transactionally");

    route.clear();
    top_target.expected_x = 150.0f;
    top_target.expected_y = 160.0f;
    top_target.expected_timestamp = 9100;
    root_target.expected_x = 150.0f;
    root_target.expected_y = 160.0f;
    root_target.expected_timestamp = 9100;
    manager->ForwardPointerMessage(
        nullptr, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(150, 160), 9100);
    Check(route == "TR", "captured target routes outside retained bounds");

    route.clear();
    top_target.release_on_up = true;
    top_target.expected_timestamp = 9200;
    root_target.expected_timestamp = 9200;
    manager->ForwardPointerMessage(
        nullptr, WM_LBUTTONUP, 0, MAKELPARAM(150, 160), 9200);
    Check(top_target.released && !host_captured && !manager->pointer_capture(),
          "release clears logical and host capture");
    Check(route == "TTRR", "release and capture-lost retain both routes");

    // A local rectangular clip excludes the higher-z child, exposing the
    // lower sibling. Equal-z source order is covered by the same reverse walk.
    top->set_visual_clip(openxaml::VisualClip::Rectangle({0.0, 0.0, 3.0, 3.0}));
    route.clear();
    bottom_target.expected_x = 15.0f;
    bottom_target.expected_y = 16.0f;
    bottom_target.expected_timestamp = 9300;
    root_target.expected_x = 15.0f;
    root_target.expected_y = 16.0f;
    root_target.expected_timestamp = 9300;
    manager->ForwardPointerMessage(
        nullptr, WM_MOUSEMOVE, 0, MAKELPARAM(15, 16), 9300);
    Check(route == "BRBR",
          "rect clip hit raises Entered then Moved on exposed sibling");

    bottom_target.handle = true;
    route.clear();
    IslandInputResult handled = manager->ForwardPointerMessage(
        nullptr, WM_MOUSEMOVE, 0, MAKELPARAM(15, 16), 9300);
    Check(handled.handled && route == "B", "Handled stops pointer bubbling");

    // Collection removal detaches the native child before it releases the
    // projected COM object. The post-mutation seam must still find and clear
    // capture exactly once without relying on the former parent link.
    bottom_target.handle = false;
    bottom_target.capture_on_press = true;
    bottom_target.expected_timestamp = 9400;
    root_target.expected_timestamp = 9400;
    route.clear();
    manager->ForwardPointerMessage(
        nullptr, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(15, 16), 9400);
    Check(manager->pointer_capture() && host_captured,
          "descendant owns capture before removal");
    route.clear();
    PrepareXamlVisualSubtreeDetached(bottom, root.get());
    root->Detach(*bottom);
    NotifyXamlVisualSubtreeDetached(bottom, root.get());
    if (route != "BR") std::fprintf(stderr, "detached route=%s\n", route.c_str());
    Check(route == "BR" && !manager->pointer_capture() && !host_captured,
          "detached captured descendant gets one CaptureLost route");

    // Reattach/replace while capture is active used to call ReleasePointer
    // under g_scope_mutex and deadlock when CaptureLost looked up its route.
    top->set_visual_clip({});
    top_target.capture_on_press = true;
    top_target.expected_x = 15.0f;
    top_target.expected_y = 16.0f;
    top_target.expected_timestamp = 9500;
    root_target.expected_timestamp = 9500;
    route.clear();
    manager->ForwardPointerMessage(
        nullptr, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(15, 16), 9500);
    Check(manager->pointer_capture() && host_captured,
          "replacement precondition owns capture");
    auto replacement_root = std::make_unique<TestElement>("replacement");
    replacement_root->Measure({100.0, 100.0});
    replacement_root->Arrange({0.0, 0.0, 100.0, 100.0});
    MockTarget replacement_target(*replacement_root, 'N', route, scope);
    RegisterXamlFocusTarget(replacement_target);
    route.clear();
    Check(scope->AttachRoot(replacement_root.get()),
          "root replacement completes during capture loss");
    Check(route == "TR" && !manager->pointer_capture() && !host_captured,
          "root replacement raises exactly one CaptureLost route");

    UnregisterXamlFocusTarget(top_target);
    UnregisterXamlFocusTarget(bottom_target);
    UnregisterXamlFocusTarget(root_target);
    UnregisterXamlFocusTarget(replacement_target);
    manager->ClearHostPointerCaptureCallbacks();
    manager->Detach(scope);
    scope->DetachRoot(replacement_root.get());

    if (failures) return 1;
    std::puts("pointer retained routing checks passed");
    return 0;
}
