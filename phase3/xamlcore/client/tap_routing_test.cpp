// Focused retained tap/double-tap synthesis and ABI checks.

#include "xaml_focus.h"

#include "element.h"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace wux = ABI::Windows::UI::Xaml;
namespace wuxi = ABI::Windows::UI::Xaml::Input;

namespace {

inline constexpr GUID IID_IDoubleTappedRoutedEventArgs = {
    0xaf404424, 0x26df, 0x44f4,
    {0x87, 0x14, 0x93, 0x59, 0x24, 0x9b, 0x62, 0xd3}};

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
    MockTarget(openxaml::Element& element, char name, std::string& route,
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
        if (capture_on_press && kind ==
                openxaml::winrt::IslandPointerEventKind::Pressed) {
            wuxi::IPointer* pointer = nullptr;
            UINT32 id = 0;
            if (args && SUCCEEDED(args->get_Pointer(&pointer)) && pointer) {
                (void)pointer->get_PointerId(&id);
                pointer->Release();
            }
            captured = id && scope_->CapturePointer(*this, id);
        }
    }
    void InvokeIslandTapEvent(
        openxaml::winrt::IslandTapEventKind kind,
        wuxi::ITappedRoutedEventArgs* args) noexcept override {
        route_.push_back(kind == openxaml::winrt::IslandTapEventKind::Tapped
                             ? static_cast<char>(name_ + ('a' - 'A'))
                             : name_);
        if (kind == openxaml::winrt::IslandTapEventKind::DoubleTapped) {
            wuxi::IDoubleTappedRoutedEventArgs* double_args = nullptr;
            double_qi_ok = double_qi_ok && args && SUCCEEDED(
                args->QueryInterface(
                    IID_IDoubleTappedRoutedEventArgs,
                    reinterpret_cast<void**>(&double_args))) && double_args;
            if (double_args) double_args->Release();
        }
        ABI::Windows::Devices::Input::PointerDeviceType device{};
        ABI::Windows::Foundation::Point position{};
        fields_ok = fields_ok && args &&
            SUCCEEDED(args->get_PointerDeviceType(&device)) &&
            device == ABI::Windows::Devices::Input::PointerDeviceType_Mouse &&
            SUCCEEDED(args->GetPosition(nullptr, &position)) &&
            position.X == expected_x && position.Y == expected_y;
        if (retain_args && !retained_args) {
            args->AddRef();
            retained_args = args;
        }
        if (unregister_during_tap && unregister_target)
            openxaml::winrt::UnregisterXamlFocusTarget(*unregister_target);
        if (handle && args) args->put_Handled(1);
    }

    ~MockTarget() override {
        if (retained_args) retained_args->Release();
    }

    int retains() const noexcept { return retains_; }
    bool capture_on_press = false;
    bool captured = false;
    bool fields_ok = true;
    bool double_qi_ok = true;
    bool handle = false;
    bool retain_args = false;
    bool unregister_during_tap = false;
    MockTarget* unregister_target = nullptr;
    float expected_x = 15.0f;
    float expected_y = 16.0f;
    wuxi::ITappedRoutedEventArgs* retained_args = nullptr;

private:
    openxaml::Element& element_;
    char name_;
    std::string& route_;
    std::shared_ptr<openxaml::winrt::XamlFocusScope> scope_;
    int retains_ = 0;
};

void Click(const std::shared_ptr<openxaml::winrt::IslandInputManager>& manager,
           int x, int y, std::uint64_t down_at, std::uint64_t up_at) {
    (void)manager->ForwardPointerMessage(
        nullptr, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y), down_at);
    (void)manager->ForwardPointerMessage(
        nullptr, WM_LBUTTONUP, 0, MAKELPARAM(x, y), up_at);
}

}  // namespace

int main() {
    using namespace openxaml::winrt;
    auto root = std::make_unique<TestElement>("root");
    TestElement* child = root->Add(std::make_unique<TestElement>("child"),
                                   {10.0, 10.0, 50.0, 50.0});
    root->Measure({100.0, 100.0});
    root->Arrange({0.0, 0.0, 100.0, 100.0});

    auto manager = std::make_shared<IslandInputManager>();
    auto scope = std::make_shared<XamlFocusScope>(manager);
    Check(scope->AttachRoot(root.get()), "attach tap retained root");
    manager->Attach(scope);

    std::string route;
    MockTarget root_target(*root, 'R', route, scope);
    MockTarget child_target(*child, 'C', route, scope);
    RegisterXamlFocusTarget(root_target);
    RegisterXamlFocusTarget(child_target);

    Click(manager, 15, 16, 1000, 2000);
    Check(route == "cr", "single tap bubbles child to root");
    Check(child_target.fields_ok && root_target.fields_ok,
          "tapped args expose mouse device and root position");

    route.clear();
    Click(manager, 15, 16, 3000, 4000);
    Check(route == "crCR",
          "second nearby tap raises Tapped then DoubleTapped routes");
    Check(child_target.double_qi_ok && root_target.double_qi_ok,
          "double-tap args expose the distinct official ABI interface");

    route.clear();
    (void)manager->ForwardPointerMessage(
        nullptr, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(15, 16), 10000);
    (void)manager->ForwardPointerMessage(
        nullptr, WM_LBUTTONUP, 0, MAKELPARAM(80, 80), 11000);
    Check(route.empty(), "release over a different original target suppresses tap");

    route.clear();
    (void)manager->ForwardPointerMessage(
        nullptr, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(15, 16), 20000);
    (void)manager->ForwardPointerMessage(
        nullptr, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(80, 80), 21000);
    (void)manager->ForwardPointerMessage(
        nullptr, WM_LBUTTONUP, 0, MAKELPARAM(15, 16), 22000);
    Check(route.empty(), "movement outside system drag bounds suppresses tap");

    route.clear();
    Click(manager, 15, 16, 30000, 10030000);
    Check(route.empty(), "release after system gesture interval suppresses tap");

    route.clear();
    (void)manager->ForwardPointerMessage(
        nullptr, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(15, 16), 11000000);
    manager->OnHostPointerCanceled();
    (void)manager->ForwardPointerMessage(
        nullptr, WM_LBUTTONUP, 0, MAKELPARAM(15, 16), 11001000);
    Check(route.empty(), "pointer cancellation suppresses tap");

    route.clear();
    child_target.capture_on_press = true;
    (void)manager->ForwardPointerMessage(
        nullptr, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(15, 16), 12000000);
    Check(child_target.captured, "capture-loss test owns pointer capture");
    manager->OnHostPointerCaptureLost();
    (void)manager->ForwardPointerMessage(
        nullptr, WM_LBUTTONUP, 0, MAKELPARAM(15, 16), 12001000);
    Check(route.empty(), "capture loss before release suppresses tap");
    child_target.capture_on_press = false;

    route.clear();
    child_target.retain_args = true;
    child_target.unregister_during_tap = true;
    child_target.unregister_target = &root_target;
    Click(manager, 15, 16, 13000000, 13001000);
    Check(route == "cr",
          "strong route snapshot survives registry mutation during bubbling");
    Check(child_target.retained_args != nullptr,
          "handler can retain concrete tapped args");
    UnregisterXamlFocusTarget(child_target);
    // The root was unregistered by the mutation case above.
    manager->Detach(scope);
    scope->DetachRoot(root.get());
    ABI::Windows::Foundation::Point retained_position{};
    Check(child_target.retained_args && SUCCEEDED(
              child_target.retained_args->GetPosition(nullptr,
                                                       &retained_position)) &&
              retained_position.X == 15.0f && retained_position.Y == 16.0f,
          "retained tapped args remain safe after island detach");
    if (child_target.retained_args) {
        child_target.retained_args->Release();
        child_target.retained_args = nullptr;
    }
    Check(child_target.retains() == 0 && root_target.retains() == 0,
          "gesture and routed-args ownership balances after detach");

    if (failures) return 1;
    std::puts("tap retained routing checks passed");
    return 0;
}
