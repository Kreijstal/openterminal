#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "binding.h"
#include "border.h"
#include "default_styles.h"
#include "visual_state.h"
#include "markup.h"

using namespace openxaml;

namespace {

// Not assert(): a condition here may carry the side effect the next line
// depends on, and NDEBUG would erase it along with the check.
void Check(bool condition, const char* what, int line) {
    if (condition) return;
    std::cerr << "wave3_test.cpp:" << line << ": CHECK failed: " << what << "\n";
    std::exit(1);
}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

class TestControl final : public Control {
public:
    std::string TypeName() const override { return "Test.Control"; }
    const std::vector<std::string>& PropertyOwners() const override {
        static const std::vector<std::string> owners = {
            "TestControl", "Control", kTextPropertyOwner, "FrameworkElement", "UIElement"};
        return owners;
    }
};

void BindingModesAndNotifications() {
    PropertyBag source;
    source.Set("Width", 40.0);
    Border target;

    int changes = 0;
    auto token = target.AddPropertyChangedHandler(
        [&](DependencyObject&, const DependencyProperty& property, const PropertyValue&) {
            if (&property == &Element::WidthProperty()) ++changes;
        });
    Binding one_way_binding;
    one_way_binding.path = "Width";
    one_way_binding.mode = BindingMode::OneWay;
    BindingExpression one_way(target, Element::WidthProperty(), source, one_way_binding);
    CHECK(target.width() == 40.0);
    source.Set("Width", 64.0);
    CHECK(target.width() == 64.0);
    CHECK(changes == 2);

    Binding two_way;
    two_way.path = "Opacity";
    two_way.mode = BindingMode::TwoWay;
    source.Set("Opacity", 0.25);
    BindingExpression expression(target, Element::OpacityProperty(), source, two_way);
    CHECK(target.opacity() == 0.25);
    target.set_opacity(0.75);
    PropertyValue value;
    CHECK(source.TryGet("Opacity", value));
    CHECK(std::get<double>(value) == 0.75);

    Binding missing;
    missing.path = "Missing";
    missing.mode = BindingMode::OneTime;
    missing.fallback_value = 22.0;
    BindingExpression fallback(target, Element::HeightProperty(), source, missing);
    CHECK(target.height() == 22.0);
    target.RemovePropertyChangedHandler(token);
}

void VisualStatesAndStoryboards() {
    Border owner;
    Border part;
    part.set_width(10.0);
    part.set_opacity(0.25);
    NameScope names;
    names.Register("Part", part);

    VisualState normal;
    normal.name = "Normal";

    VisualState wide;
    wide.name = "Wide";
    wide.setters.push_back({"Part", &Element::WidthProperty(), 80.0});
    bool brush_like_state = false;
    VisualStateSetter retained_value;
    retained_value.apply = [&brush_like_state]() { brush_like_state = true; };
    retained_value.clear = [&brush_like_state]() { brush_like_state = false; };
    wide.setters.push_back(std::move(retained_value));
    Timeline fade;
    fade.target_name = "Part";
    fade.target_property = &Element::OpacityProperty();
    fade.from = 0.0;
    fade.to = 1.0;
    fade.duration_seconds = 0.2;
    wide.storyboard.timelines.push_back(fade);

    VisualStateGroup common("CommonStates");
    common.Add(std::move(normal));
    common.Add(std::move(wide));
    VisualStateManager manager(owner, names);
    manager.AddGroup(std::move(common));

    CHECK(manager.GoToState("Wide"));
    CHECK(part.width() == 80.0);
    CHECK(part.opacity() == 1.0);
    CHECK(brush_like_state);
    manager.SampleCurrent("CommonStates", 0.0);
    CHECK(part.opacity() == 0.0);
    CHECK(manager.GoToState("Normal"));
    CHECK(part.width() == 10.0);
    CHECK(part.opacity() == 0.25);
    CHECK(!brush_like_state);
    CHECK(!manager.GoToState("Absent"));
}

void TemplatesAndDefaultStyles() {
    TestControl parent;
    Border part;
    TemplateBindingExpression binding(parent, Element::WidthProperty(), part,
                                      Element::WidthProperty());
    parent.set_width(33.0);
    CHECK(part.width() == 33.0);

    DefaultControlStyle style;
    style.style.target_type = "TestControl";
    style.style.setters.push_back({&Element::MinWidthProperty(), 12.0});
    style.control_template = std::make_shared<ControlTemplate>(
        "TestControl", [](Control&) {
            auto border = std::make_unique<Border>();
            border->set_height(7.0);
            return border;
        });
    auto& styles = DefaultStyleRegistry::Default();
    styles.Register("Wave3TestControl", std::move(style));
    CHECK(styles.Apply(parent, "Wave3TestControl", parent.PropertyOwners()));
    CHECK(parent.min_width() == 12.0);
    CHECK(parent.ApplyTemplate());
    CHECK(parent.TemplateRoot() != nullptr);
    parent.Measure({100.0, 100.0});
    CHECK(parent.desired_size().height == 7.0);
}

void MarkupBindings() {
    PropertyBag source;
    source.Set("BoxWidth", 45.0);
    std::unique_ptr<Element> loaded = LoadMarkup(
        "<Border Width=\"{Binding BoxWidth, Mode=TwoWay}\"/>", source);
    auto* border = dynamic_cast<Border*>(loaded.get());
    CHECK(border != nullptr);
    CHECK(border->width() == 45.0);
    CHECK(border->binding_count() == 1);
    source.Set("BoxWidth", 70.0);
    CHECK(border->width() == 70.0);
    border->set_width(80.0);
    PropertyValue value;
    CHECK(source.TryGet("BoxWidth", value));
    CHECK(std::get<double>(value) == 80.0);

    bool refused = false;
    try {
        (void)LoadMarkup("<Border Width=\"{x:Bind BoxWidth}\"/>");
    } catch (const MarkupError& error) {
        refused = std::string(error.what()).find("no data source") != std::string::npos;
    }
    CHECK(refused);
}

void VisualStateMarkup() {
    const std::string markup =
        "<Grid xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\">"
        "<VisualStateManager.VisualStateGroups>"
        "<VisualStateGroup x:Name=\"CommonStates\">"
        "<VisualState x:Name=\"Wide\">"
        "<VisualState.Setters><Setter Target=\"Part.Width\" Value=\"80\"/>"
        "</VisualState.Setters>"
        "<Storyboard><DoubleAnimation Storyboard.TargetName=\"Part\" "
        "Storyboard.TargetProperty=\"Opacity\" From=\"0\" To=\"1\" Duration=\"0:0:0.2\"/>"
        "</Storyboard></VisualState>"
        "<VisualState x:Name=\"Normal\"/>"
        "</VisualStateGroup></VisualStateManager.VisualStateGroups>"
        "<Border x:Name=\"Part\" Width=\"10\" Opacity=\"0.25\"/>"
        "</Grid>";
    std::unique_ptr<Element> root = LoadMarkup(markup);
    CHECK(root->visual_state_manager() != nullptr);
    auto* part = dynamic_cast<Border*>(root->Children().front());
    CHECK(part != nullptr);
    CHECK(root->visual_state_manager()->GoToState("Wide"));
    CHECK(part->width() == 80.0);
    CHECK(part->opacity() == 1.0);
    root->visual_state_manager()->SampleCurrent("CommonStates", 0.0);
    CHECK(part->opacity() == 0.0);
    CHECK(root->visual_state_manager()->GoToState("Normal"));
    CHECK(part->width() == 10.0);
    CHECK(part->opacity() == 0.25);
}

void ElementStateEntryAppliesPropertiesAndInvalidatesByMetadata() {
    const std::string markup =
        "<Grid xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\">"
        "<VisualStateManager.VisualStateGroups>"
        "<VisualStateGroup x:Name=\"CommonStates\">"
        "<VisualState x:Name=\"Emphasized\">"
        "<VisualState.Setters><Setter Target=\"Part.Width\" Value=\"80\"/>"
        "</VisualState.Setters>"
        "<Storyboard><DoubleAnimation Storyboard.TargetName=\"Part\" "
        "Storyboard.TargetProperty=\"Opacity\" To=\"0.5\"/>"
        "</Storyboard></VisualState>"
        "<VisualState x:Name=\"Resting\"/>"
        "</VisualStateGroup></VisualStateManager.VisualStateGroups>"
        "<Border x:Name=\"Part\" Width=\"10\" Opacity=\"0.25\"/>"
        "</Grid>";
    std::unique_ptr<Element> root = LoadMarkup(markup);
    auto* part = dynamic_cast<Border*>(root->Children().front());
    CHECK(part != nullptr);
    root->Measure({100.0, 100.0});
    root->Arrange({0.0, 0.0, 100.0, 100.0});
    CHECK(!part->needs_measure());

    std::vector<bool> invalidations;
    auto sink = std::make_shared<RenderInvalidationSink>(
        [&](bool layout) { invalidations.push_back(layout); });
    CHECK(root->AttachRenderInvalidationSink(sink));

    CHECK(root->GoToState("Emphasized", false));
    CHECK(part->width() == 80.0);
    CHECK(part->opacity() == 0.5);
    CHECK(part->needs_measure());
    CHECK(invalidations.size() == 2);
    CHECK(invalidations[0]);
    CHECK(!invalidations[1]);

    invalidations.clear();
    CHECK(root->GoToState("Emphasized", true));
    CHECK(invalidations.empty());
    CHECK(!root->GoToState("Missing", false));
    CHECK(invalidations.empty());

    root->Measure({100.0, 100.0});
    root->Arrange({0.0, 0.0, 100.0, 100.0});
    CHECK(!part->needs_measure());
    CHECK(root->GoToState("Resting", false));
    CHECK(part->width() == 10.0);
    CHECK(part->opacity() == 0.25);
    CHECK(part->needs_measure());
    CHECK(invalidations.size() == 2);
    CHECK(invalidations[0]);
    CHECK(!invalidations[1]);

    Border no_states;
    CHECK(!no_states.GoToState("Emphasized", false));
}

}  // namespace

int main() {
    BindingModesAndNotifications();
    VisualStatesAndStoryboards();
    TemplatesAndDefaultStyles();
    MarkupBindings();
    VisualStateMarkup();
    ElementStateEntryAppliesPropertiesAndInvalidatesByMetadata();
}
