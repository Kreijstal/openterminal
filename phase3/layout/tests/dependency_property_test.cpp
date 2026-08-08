// What the property system and the event system do, that no measurement can
// see.
//
// A recorded tree is a tree of numbers. It cannot say which slot a value came
// from, that clearing a local value revealed a style's rather than a default,
// that a callback ran before another one, or that a token took a handler back
// off. Those are the whole content of a dependency-property system reached
// through an ABI, and they are checked here.
//
// The two events this implementation raises are checked here too, against the
// firing rule events.h takes from the published XAML core: a SizeChanged is
// queued during arrange and raised after the pass, parents before children,
// and a LayoutUpdated is raised once per pass after them.

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "border.h"
#include "canvas.h"
#include "element.h"
#include "events.h"
#include "grid.h"
#include "property.h"
#include "stack_panel.h"
#include "style.h"

using namespace openxaml;

namespace {

// Not assert(): a condition here may carry the side effect the next line
// depends on, and NDEBUG would erase it along with the check.
void Check(bool condition, const char* what, int line) {
    if (condition) return;
    std::cerr << "dependency_property_test.cpp:" << line << ": CHECK failed: " << what << "\n";
    std::exit(1);
}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

// --- registration and identity ------------------------------------------------

void RegistrationIsIdentity() {
    const DependencyProperty* first =
        RegisterProperty("DPTest.Owner", "Scalar", PropertyMetadata(7.0));
    CHECK(first != nullptr);

    // The same owner and name a second time is a mistake in the caller, not a
    // second property. The registry says so rather than handing back a twin
    // that would silently shadow the first everywhere.
    bool refused = false;
    try {
        RegisterProperty("DPTest.Owner", "Scalar", PropertyMetadata(9.0));
    } catch (const PropertyError&) {
        refused = true;
    }
    CHECK(refused);

    // Two properties with one name on different owners are different
    // properties. The name alone never identifies one -- which is the reason
    // a property is referred to by address at all.
    const DependencyProperty* other =
        RegisterProperty("DPTest.Other", "Scalar", PropertyMetadata(11.0));
    CHECK(other != first);
    CHECK(other->index() != first->index());
    CHECK(first->name() == other->name());

    // Resolution is by owner chain, and it stops at the first owner that has
    // the name -- which is what makes FontSize a Control's and not a Grid's.
    CHECK(FindProperty({"DPTest.Owner"}, "Scalar") == first);
    CHECK(FindProperty({"DPTest.Other"}, "Scalar") == other);
    CHECK(FindProperty({"DPTest.Other", "DPTest.Owner"}, "Scalar") == other);
    CHECK(FindProperty({"DPTest.Nobody"}, "Scalar") == nullptr);

    // The way back from a store index, which anything that walks a store
    // rather than a property list needs.
    CHECK(PropertyByIndex(first->index()) == first);

    // The metadata is the registration's, read back whole.
    CHECK(std::get<double>(first->default_value()) == 7.0);
    CHECK(!first->inherits());
    CHECK(!first->affects_measure());
    CHECK(!first->is_attached());
}

void AttachedRegistrationIsQualified() {
    const DependencyProperty* attached =
        RegisterAttachedProperty("DPTest.Panel", "Slot", PropertyMetadata(3));
    CHECK(attached->is_attached());

    // Filed under the qualified name, which is the only name it is ever
    // written by. That is what lets it resolve on an element of any type.
    CHECK(attached->name() == "DPTest.Panel.Slot");
    CHECK(FindProperty({"Border", "FrameworkElement"}, "DPTest.Panel.Slot") == attached);
    CHECK(FindProperty({"DPTest.Panel"}, "Slot") == nullptr);

    // A caller that names the owner separately -- which is what the ABI's
    // RegisterAttached and a *Property static both do -- finds it either way.
    CHECK(FindPropertyOnOwner("DPTest.Panel", "Slot") == attached);
    CHECK(FindPropertyOnOwner("DPTest.Panel", "DPTest.Panel.Slot") == attached);

    // An attached property is registered under its bare name. Passing the
    // qualified one would file it as `Owner.Owner.Name`, so it is refused.
    bool refused = false;
    try {
        RegisterAttachedProperty("DPTest.Panel", "DPTest.Panel.Slot", PropertyMetadata(0));
    } catch (const PropertyError&) {
        refused = true;
    }
    CHECK(refused);

    // The ones the layout core itself registers are attached, and reachable
    // by identity from anything -- which is what the ABI's Grid statics hand
    // out.
    CHECK(Grid::RowProperty().is_attached());
    CHECK(Grid::ColumnProperty().is_attached());
    CHECK(Canvas::LeftProperty().is_attached());
    Border loose;
    loose.SetValue(Grid::RowProperty(), 4);
    CHECK(loose.GetInt(Grid::RowProperty()) == 4);
}

// --- precedence ---------------------------------------------------------------

class PrecedenceElement final : public Panel {
public:
    std::string TypeName() const override { return "DPTest.Element"; }
    const std::vector<std::string>& PropertyOwners() const override {
        static const std::vector<std::string> owners = {"DPTest.Element", "Panel",
                                                        "FrameworkElement", "UIElement"};
        return owners;
    }

protected:
    Size MeasureOverride(Size) override { return Size{}; }
    Size ArrangeOverride(Size final_size) override { return final_size; }
};

void PrecedenceAndReadLocalValue() {
    const DependencyProperty& width = Element::WidthProperty();
    PrecedenceElement element;

    // Nothing set: the default, and no local value at all. The distinction is
    // the whole reason ReadLocalValue exists -- "the default" and "nothing
    // here" are the same number and different answers.
    CHECK(std::isnan(element.GetDouble(width)));
    CHECK(element.ReadLocalValue(width) == nullptr);
    CHECK(!element.HasLocalValue(width));

    // A style value is below a local one and above an ancestor's.
    element.SetStyleValue(width, 30.0);
    CHECK(element.GetDouble(width) == 30.0);
    CHECK(element.ReadLocalValue(width) == nullptr);
    CHECK(element.HasStyleValue(width));

    element.SetValue(width, 50.0);
    CHECK(element.GetDouble(width) == 50.0);
    CHECK(element.ReadLocalValue(width) != nullptr);
    CHECK(std::get<double>(*element.ReadLocalValue(width)) == 50.0);

    // An animation is above both, and does not disturb either.
    element.SetAnimatedValue(width, 70.0);
    CHECK(element.GetDouble(width) == 70.0);
    CHECK(std::get<double>(*element.ReadLocalValue(width)) == 50.0);
    element.ClearAnimatedValue(width);
    CHECK(element.GetDouble(width) == 50.0);

    // Clearing the local value reveals the style's, not the default. This is
    // the case a field cannot express and the one a wrong implementation gets
    // wrong: it is not "set it back to Auto".
    element.ClearValue(width);
    CHECK(element.ReadLocalValue(width) == nullptr);
    CHECK(element.GetDouble(width) == 30.0);

    // And clearing the style reveals the default.
    element.ClearStyleValues();
    CHECK(std::isnan(element.GetDouble(width)));
    CHECK(!element.HasStyleValue(width));
}

void InheritedValueIsBelowAStyle() {
    const DependencyProperty& font_size = FontSizeProperty();
    auto parent = std::make_unique<PrecedenceElement>();
    auto* child_pointer = new PrecedenceElement();
    parent->AddChild(std::unique_ptr<Element>(child_pointer));
    parent->SetValue(font_size, 22.0);

    // Inherited: the child has nothing of its own, so it reads the ancestor's
    // effective value.
    CHECK(child_pointer->GetDouble(font_size) == 22.0);
    CHECK(child_pointer->ReadLocalValue(font_size) == nullptr);

    // A style on the child beats what it inherits. The consequence worth
    // stating: a style setter is *above* inheritance, so this is 14 and not
    // 22, which is the opposite of what "inherited unless overridden locally"
    // would give.
    child_pointer->SetStyleValue(font_size, 14.0);
    CHECK(child_pointer->GetDouble(font_size) == 14.0);
    child_pointer->ClearStyleValues();
    CHECK(child_pointer->GetDouble(font_size) == 22.0);
}

// --- notification order -------------------------------------------------------

void CallbackOrderIsTheRuntimes() {
    std::vector<std::string> order;

    // A property whose metadata carries a changed callback -- what
    // PropertyMetadata's second constructor argument is, and what a property
    // registered through the ABI would carry.
    const DependencyProperty* watched = RegisterProperty(
        "DPTest.Order", "Watched",
        PropertyMetadata(0.0, false, false,
                         [&order](DependencyObject&, const DependencyProperty&,
                                  const PropertyValue& before, const PropertyValue& after) {
                             CHECK(std::get<double>(before) != std::get<double>(after));
                             order.push_back("metadata");
                         }));

    PrecedenceElement element;
    element.AddPropertyChangedHandler(
        [&order](DependencyObject&, const DependencyProperty&, const PropertyValue&) {
            order.push_back("object");
        });
    element.RegisterPropertyChangedCallback(
        *watched, [&order](DependencyObject&, const DependencyProperty&, const PropertyValue&) {
            order.push_back("property");
        });

    element.SetValue(*watched, 5.0);

    // The runtime's order: the metadata's callback, then the binding-facing
    // observers, then the per-property ones a caller registered. See the note
    // on ValueMoved in property.cpp for where each of the three sits in
    // dxaml's NotifyPropertyChanged.
    CHECK(order.size() == 3);
    CHECK(order[0] == "metadata");
    CHECK(order[1] == "object");
    CHECK(order[2] == "property");

    // A write that does not move the effective value raises nothing at all.
    order.clear();
    element.SetValue(*watched, 5.0);
    CHECK(order.empty());
}

void PerPropertyCallbacksAreNarrowAndRemovable() {
    const DependencyProperty& width = Element::WidthProperty();
    const DependencyProperty& height = Element::HeightProperty();
    PrecedenceElement element;

    int width_changes = 0;
    int height_changes = 0;
    const auto width_token = element.RegisterPropertyChangedCallback(
        width, [&](DependencyObject&, const DependencyProperty&, const PropertyValue&) {
            ++width_changes;
        });
    element.RegisterPropertyChangedCallback(
        height, [&](DependencyObject&, const DependencyProperty&, const PropertyValue&) {
            ++height_changes;
        });

    // Narrow: a callback registered for one property hears about that one.
    element.SetValue(width, 10.0);
    CHECK(width_changes == 1);
    CHECK(height_changes == 0);
    element.SetValue(height, 20.0);
    CHECK(width_changes == 1);
    CHECK(height_changes == 1);

    // Tokens are per registration, not per property, so unregistering one
    // leaves the other alone.
    element.UnregisterPropertyChangedCallback(width, width_token);
    element.SetValue(width, 30.0);
    element.SetValue(height, 40.0);
    CHECK(width_changes == 1);
    CHECK(height_changes == 2);

    // A token that names nothing is not an error, and does not disturb what
    // is still registered.
    element.UnregisterPropertyChangedCallback(width, width_token);
    element.UnregisterPropertyChangedCallback(width, 999999);
    element.SetValue(height, 50.0);
    CHECK(height_changes == 3);

    // A callback that removes another during a raise: the removed one must
    // not run, which is why the raise walks a snapshot and looks each token up
    // again rather than calling out of the copy.
    PrecedenceElement second;
    std::vector<std::string> seen;
    DependencyObject::PropertyChangedToken doomed = 0;
    second.RegisterPropertyChangedCallback(
        width, [&](DependencyObject& object, const DependencyProperty& property,
                   const PropertyValue&) {
            seen.push_back("first");
            object.UnregisterPropertyChangedCallback(property, doomed);
        });
    doomed = second.RegisterPropertyChangedCallback(
        width, [&](DependencyObject&, const DependencyProperty&, const PropertyValue&) {
            seen.push_back("doomed");
        });
    second.SetValue(width, 12.0);
    CHECK(seen.size() == 1);
    CHECK(seen[0] == "first");
}

// --- event tokens -------------------------------------------------------------

void EventTokensAddAndRemove() {
    Border element;
    int first_calls = 0;
    int second_calls = 0;

    const EventToken first = element.events().Add(
        FrameworkEvent::Loaded,
        [&](Element&, FrameworkEvent, const SizeChangedArgs&) { ++first_calls; });
    const EventToken second = element.events().Add(
        FrameworkEvent::Loaded,
        [&](Element&, FrameworkEvent, const SizeChangedArgs&) { ++second_calls; });

    // Real tokens: never zero, never equal, and both registrations are live.
    CHECK(first != 0);
    CHECK(second != 0);
    CHECK(first != second);
    CHECK(element.events().Count(FrameworkEvent::Loaded) == 2);

    // A token names one registration. Removing it leaves the other.
    CHECK(element.events().Remove(FrameworkEvent::Loaded, first));
    CHECK(element.events().Count(FrameworkEvent::Loaded) == 1);

    // The same token twice is false the second time rather than an error, and
    // so is a token belonging to another event.
    CHECK(!element.events().Remove(FrameworkEvent::Loaded, first));
    CHECK(!element.events().Remove(FrameworkEvent::SizeChanged, second));
    CHECK(element.events().Count(FrameworkEvent::Loaded) == 1);

    // Tokens are not reused: a registration made after a removal is a new
    // token, so a stale one can never be mistaken for a live one.
    const EventToken third = element.events().Add(
        FrameworkEvent::Loaded, [&](Element&, FrameworkEvent, const SizeChangedArgs&) {});
    CHECK(third != first);
    CHECK(third != second);

    // Registrations for different events are separate lists.
    CHECK(element.events().Count(FrameworkEvent::SizeChanged) == 0);
    CHECK(!element.events().Any(FrameworkEvent::SizeChanged));

    // An empty handler registers nothing and says so with a token of zero,
    // which is what an unset EventRegistrationToken holds.
    CHECK(element.events().Add(FrameworkEvent::Loaded, {}) == 0);
}

void UnraisedEventsStayUnraised() {
    // The events this implementation raises, and the ones it stores and never
    // calls, are a list rather than an accident -- events.h says why of each.
    CHECK(IsRaised(FrameworkEvent::SizeChanged));
    CHECK(IsRaised(FrameworkEvent::LayoutUpdated));
    CHECK(!IsRaised(FrameworkEvent::Loaded));
    CHECK(!IsRaised(FrameworkEvent::Unloaded));
    CHECK(!IsRaised(FrameworkEvent::PointerPressed));
    CHECK(!IsRaised(FrameworkEvent::KeyDown));
    CHECK(!IsRaised(FrameworkEvent::GotFocus));
    CHECK(!IsRaised(FrameworkEvent::Tapped));
    CHECK(std::string(NameOf(FrameworkEvent::Loaded)) == "Loaded");
    CHECK(std::string(NameOf(FrameworkEvent::LayoutUpdated)) == "LayoutUpdated");

    // A whole layout pass on a tree with a Loaded handler on it raises
    // nothing: there is no live visual tree here and so no moment that is the
    // moment the reference names.
    auto panel = std::make_unique<StackPanel>();
    auto* child = new Border();
    child->set_width(20);
    child->set_height(10);
    panel->AddChild(std::unique_ptr<Element>(child));
    int loaded_calls = 0;
    child->events().Add(FrameworkEvent::Loaded, [&](Element&, FrameworkEvent,
                                                    const SizeChangedArgs&) { ++loaded_calls; });
    panel->Measure({100, 100});
    panel->Arrange({0, 0, 100, 100});
    CHECK(loaded_calls == 0);
}

// --- the two events a layout pass raises --------------------------------------

void SizeChangedIsRaisedAfterThePassParentFirst() {
    auto panel = std::make_unique<StackPanel>();
    auto* child = new Border();
    child->set_width(20);
    child->set_height(10);
    panel->AddChild(std::unique_ptr<Element>(child));

    std::vector<std::string> order;
    std::vector<SizeChangedArgs> panel_args;
    panel->events().Add(FrameworkEvent::SizeChanged,
                        [&](Element&, FrameworkEvent, const SizeChangedArgs& args) {
                            order.push_back("panel");
                            panel_args.push_back(args);
                        });
    child->events().Add(FrameworkEvent::SizeChanged,
                        [&](Element&, FrameworkEvent, const SizeChangedArgs&) {
                            order.push_back("child");
                            // Raised after the pass, not during it: by the
                            // time a handler runs, every render size in the
                            // tree is already the new one.
                            CHECK(panel_args.size() == 1);
                        });

    panel->Measure({100, 100});
    panel->Arrange({0, 0, 100, 100});

    // Both moved from nothing to something, so both are queued. The queue is
    // filled child-first -- a parent's render size is assigned after its
    // ArrangeOverride has arranged its children -- and raised backwards, so
    // the parent hears first.
    CHECK(order.size() == 2);
    CHECK(order[0] == "panel");
    CHECK(order[1] == "child");

    CHECK(panel_args.size() == 1);
    CHECK(panel_args[0].previous.width == 0.0);
    CHECK(panel_args[0].previous.height == 0.0);
    CHECK(panel_args[0].current.width == 100.0);
    CHECK(panel_args[0].current.height == 100.0);

    // A second pass that changes nothing raises nothing: the comparison is
    // against the size the element already had.
    order.clear();
    panel->Measure({100, 100});
    panel->Arrange({0, 0, 100, 100});
    CHECK(order.empty());

    // A pass that does change it raises again, with the size it had before.
    panel_args.clear();
    panel->Measure({80, 60});
    panel->Arrange({0, 0, 80, 60});
    CHECK(panel_args.size() == 1);
    CHECK(panel_args[0].previous.width == 100.0);
    CHECK(panel_args[0].current.width == 80.0);
}

void SizeChangedNeedsAHandlerToBeQueued() {
    // The reference does not queue an element that has no SizeChanged
    // handlers -- EnqueueForSizeChanged checks GetWantsSizeChanged first. So
    // registering a handler after a size has already moved does not deliver
    // the move that happened before it.
    auto panel = std::make_unique<StackPanel>();
    panel->Measure({50, 50});
    panel->Arrange({0, 0, 50, 50});

    int calls = 0;
    panel->events().Add(FrameworkEvent::SizeChanged,
                        [&](Element&, FrameworkEvent, const SizeChangedArgs&) { ++calls; });
    panel->Measure({50, 50});
    panel->Arrange({0, 0, 50, 50});
    CHECK(calls == 0);
}

void LayoutUpdatedIsOncePerPassAfterSizeChanged() {
    auto panel = std::make_unique<StackPanel>();
    auto* child = new Border();
    child->set_width(20);
    child->set_height(10);
    panel->AddChild(std::unique_ptr<Element>(child));

    std::vector<std::string> order;
    panel->events().Add(FrameworkEvent::SizeChanged,
                        [&](Element&, FrameworkEvent, const SizeChangedArgs&) {
                            order.push_back("size");
                        });
    const EventToken layout_token =
        panel->events().Add(FrameworkEvent::LayoutUpdated,
                            [&](Element&, FrameworkEvent, const SizeChangedArgs&) {
                                order.push_back("layout");
                            });
    child->events().Add(FrameworkEvent::LayoutUpdated,
                        [&](Element&, FrameworkEvent, const SizeChangedArgs&) {
                            order.push_back("layout-child");
                        });

    panel->Measure({100, 100});
    panel->Arrange({0, 0, 100, 100});

    // The size-changed queue is drained first, then every LayoutUpdated
    // subscriber is raised once. The order *between* subscribers is the
    // managed side's in the reference and is not pinned by it, so it is not
    // checked here -- only that each is called exactly once, after the sizes.
    CHECK(order.size() == 3);
    CHECK(order[0] == "size");
    CHECK(std::count(order.begin(), order.end(), std::string("layout")) == 1);
    CHECK(std::count(order.begin(), order.end(), std::string("layout-child")) == 1);

    // Raised on a pass that changed nothing too: LayoutUpdated is about the
    // pass, not about a size.
    order.clear();
    panel->Measure({100, 100});
    panel->Arrange({0, 0, 100, 100});
    CHECK(order.size() == 2);

    // Unsubscribing takes the element out of the subscriber list.
    CHECK(panel->events().Remove(FrameworkEvent::LayoutUpdated, layout_token));
    order.clear();
    panel->Measure({100, 100});
    panel->Arrange({0, 0, 100, 100});
    CHECK(order.size() == 1);
    CHECK(order[0] == "layout-child");

    // And a destroyed element leaves it, so the next pass does not walk into
    // a handler that is no longer there.
    panel.reset();
    StackPanel other;
    int other_calls = 0;
    other.events().Add(FrameworkEvent::LayoutUpdated,
                       [&](Element&, FrameworkEvent, const SizeChangedArgs&) { ++other_calls; });
    other.Measure({10, 10});
    other.Arrange({0, 0, 10, 10});
    CHECK(other_calls == 1);
}

// --- the store the ABI reaches ------------------------------------------------

void TypedAccessorsAndTheStoreAgree() {
    // put_Width and SetValue(WidthProperty, ...) have to be two spellings of
    // one write, or the ABI would have a second store behind it. This is the
    // check that they are.
    Border element;
    element.SetValue(Element::WidthProperty(), 42.0);
    CHECK(element.width() == 42.0);
    element.set_width(17.0);
    CHECK(element.GetDouble(Element::WidthProperty()) == 17.0);
    CHECK(std::get<double>(*element.ReadLocalValue(Element::WidthProperty())) == 17.0);

    element.SetValue(Element::MarginProperty(), Thickness{1, 2, 3, 4});
    CHECK(element.margin().left == 1.0);
    CHECK(element.margin().bottom == 4.0);

    // A null value is a value the store can hold. Nothing in the layout core
    // produces one; a property registered through the ABI with
    // PropertyMetadata(nullptr) defaults to one, and every typed read of it
    // refuses by name rather than answering zero.
    const DependencyProperty* nullable =
        RegisterProperty("DPTest.Null", "Anything", PropertyMetadata(PropertyValue(std::monostate{})));
    CHECK(std::holds_alternative<std::monostate>(element.GetValue(*nullable)));
    bool refused = false;
    try {
        element.GetDouble(*nullable);
    } catch (const PropertyError&) {
        refused = true;
    }
    CHECK(refused);
}

}  // namespace

int main() {
    RegistrationIsIdentity();
    AttachedRegistrationIsQualified();
    PrecedenceAndReadLocalValue();
    InheritedValueIsBelowAStyle();
    CallbackOrderIsTheRuntimes();
    PerPropertyCallbacksAreNarrowAndRemovable();
    EventTokensAddAndRemove();
    UnraisedEventsStayUnraised();
    SizeChangedIsRaisedAfterThePassParentFirst();
    SizeChangedNeedsAHandlerToBeQueued();
    LayoutUpdatedIsOncePerPassAfterSizeChanged();
    TypedAccessorsAndTheStoreAgree();
    std::cout << "dependency property and event tests passed\n";
    return 0;
}
