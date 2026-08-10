// Visual ownership and renderer invalidation are platform-neutral contracts.
// These tests use raw test nodes to model the COM projection's retained
// children without requiring WinRT, an HWND, or a graphics backend.

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "element.h"
#include "canvas.h"
#include "content_presenter.h"
#include "text.h"

namespace {

int failures = 0;

void Check(bool condition, const std::string& what, int line) {
    if (condition) return;
    std::cerr << "visual_ownership_test.cpp:" << line << ": " << what << "\n";
    ++failures;
}

#define CHECK(condition) Check((condition), #condition, __LINE__)

using namespace openxaml;

class TestNode final : public Element {
public:
    ~TestNode() override {
        for (Element* child : children_) DetachVisualChild(*child);
    }

    std::string TypeName() const override { return "TestNode"; }
    const std::vector<std::string>& PropertyOwners() const override {
        static const std::vector<std::string> owners{"FrameworkElement", "UIElement"};
        return owners;
    }
    bool IsLayoutElement() const override { return true; }
    std::vector<Element*> Children() const override { return children_; }

    bool Append(Element& child) {
        if (std::find(children_.begin(), children_.end(), &child) != children_.end())
            return false;
        if (!AttachVisualChild(child)) return false;
        children_.push_back(&child);
        NotifyVisualStructureChanged();
        return true;
    }

    bool Remove(Element& child) {
        const auto found = std::find(children_.begin(), children_.end(), &child);
        if (found == children_.end()) return false;
        DetachVisualChild(child);  // Must precede losing the owning reference.
        children_.erase(found);
        NotifyVisualStructureChanged();
        return true;
    }

    bool Replace(Element& old_child, Element& new_child) {
        const auto found = std::find(children_.begin(), children_.end(), &old_child);
        if (found == children_.end()) return false;
        if (&old_child == &new_child) return true;
        if (std::find(children_.begin(), children_.end(), &new_child) != children_.end())
            return false;
        // Validate before disturbing the old child, as Vector::SetAt does.
        if (!CanAttachVisualChild(new_child)) return false;
        DetachVisualChild(old_child);
        *found = &new_child;
        CHECK(AttachVisualChild(new_child));
        NotifyVisualStructureChanged();
        return true;
    }

protected:
    Size MeasureOverride(Size) override { return {}; }
    Size ArrangeOverride(Size final_size) override { return final_size; }

private:
    std::vector<Element*> children_;
};

struct Counts {
    int layout = 0;
    int render = 0;
};

std::shared_ptr<RenderInvalidationSink> CountingSink(Counts& counts) {
    return std::make_shared<RenderInvalidationSink>([&counts](bool layout) {
        if (layout)
            ++counts.layout;
        else
            ++counts.render;
    });
}

void RemovedAndReplacedChildrenLoseTheOldSinkBeforeMutation() {
    TestNode removed;
    TestNode replacement;
    TestNode root;
    Counts counts;
    const auto sink = CountingSink(counts);
    CHECK(root.AttachRenderInvalidationSink(sink));

    CHECK(root.Append(removed));
    removed.set_width(10.0);
    CHECK(counts.layout == 2);  // append plus the effective Width change

    CHECK(root.Replace(removed, replacement));
    const int after_replace = counts.layout;
    CHECK(removed.visual_parent() == nullptr);
    CHECK(removed.inheritance_parent() == nullptr);

    removed.set_width(11.0);
    CHECK(counts.layout == after_replace);
    replacement.set_width(12.0);
    CHECK(counts.layout == after_replace + 1);
}

void ReparentingIsExplicitAndCyclesAreRejected() {
    TestNode child;
    TestNode first;
    TestNode second;
    Counts first_counts;
    Counts second_counts;
    const auto first_sink = CountingSink(first_counts);
    const auto second_sink = CountingSink(second_counts);
    CHECK(first.AttachRenderInvalidationSink(first_sink));
    CHECK(second.AttachRenderInvalidationSink(second_sink));

    CHECK(!first.Append(first));
    CHECK(first.Append(child));
    CHECK(!first.Append(child));
    CHECK(!second.Append(child));
    CHECK(!child.Append(first));

    CHECK(first.Remove(child));
    CHECK(second.Append(child));
    const int first_after_reparent = first_counts.layout;
    const int second_after_reparent = second_counts.layout;
    child.set_height(9.0);
    CHECK(first_counts.layout == first_after_reparent);
    CHECK(second_counts.layout == second_after_reparent + 1);
    CHECK(child.visual_parent() == &second);
    CHECK(child.inheritance_parent() == &second);
}

void DetachClearsTheSinkBeforeInheritedValuesMove() {
    TestNode child;
    TestNode root;
    Counts counts;
    const auto sink = CountingSink(counts);
    root.SetValue(FontSizeProperty(), 24.0);
    CHECK(root.AttachRenderInvalidationSink(sink));
    CHECK(root.Append(child));

    counts = {};
    CHECK(root.Remove(child));
    // Only the root's structural change is reported. Detaching FontSize from
    // the inherited parent must not notify through the child's old sink.
    CHECK(counts.layout == 1);
    CHECK(counts.render == 0);
}

void ClosingOrDestroyingTheSinkMakesRetainedElementsInert() {
    TestNode child;
    TestNode root;
    Counts counts;
    auto sink = CountingSink(counts);
    CHECK(root.AttachRenderInvalidationSink(sink));
    CHECK(root.Append(child));

    const int before_close = counts.layout;
    sink->Close();
    child.set_width(20.0);
    CHECK(counts.layout == before_close);

    sink.reset();
    child.set_width(21.0);  // Expired weak sink: no callback and no crash.
    CHECK(counts.layout == before_close);
}

void ArrangeInvalidationsAreNotMisreportedAsRenderOnly() {
    TestNode node;
    Counts counts;
    const auto sink = CountingSink(counts);
    CHECK(node.AttachRenderInvalidationSink(sink));

    node.set_horizontal_alignment(HorizontalAlignment::Left);
    node.set_vertical_alignment(VerticalAlignment::Top);
    Canvas::SetLeft(node, 3.0);
    Canvas::SetTop(node, 4.0);
    CHECK(counts.layout == 4);
    CHECK(counts.render == 0);

    ContentPresenter presenter;
    CHECK(presenter.AttachRenderInvalidationSink(sink));
    presenter.set_horizontal_content_alignment(HorizontalAlignment::Center);
    presenter.set_vertical_content_alignment(VerticalAlignment::Center);
    CHECK(counts.layout == 6);

    node.set_opacity(0.5);
    CHECK(counts.layout == 6);
    CHECK(counts.render == 1);

    node.set_visual_clip(VisualClip::Rectangle({1.0, 2.0, 3.0, 4.0}));
    CHECK(counts.layout == 6);
    CHECK(counts.render == 2);
    // Reapplying identical retained state is inert.
    node.set_visual_clip(VisualClip::Rectangle({1.0, 2.0, 3.0, 4.0}));
    CHECK(counts.render == 2);
    node.set_visual_clip(
        VisualClip::Unsupported("Windows.UI.Xaml.Media.EllipseGeometry"));
    CHECK(counts.layout == 6);
    CHECK(counts.render == 3);

    node.set_visual_transform(VisualTransform::Rotate(17.0));
    CHECK(counts.layout == 6);
    CHECK(counts.render == 4);
    node.set_visual_transform(VisualTransform::Rotate(17.0));
    CHECK(counts.render == 4);
    node.set_render_transform_origin({0.5, 0.25});
    CHECK(counts.layout == 6);
    CHECK(counts.render == 5);
    node.set_render_transform_origin({0.5, 0.25});
    CHECK(counts.render == 5);

    Canvas::SetZIndex(node, 7);
    CHECK(counts.layout == 6);
    CHECK(counts.render == 6);
    Canvas::SetZIndex(node, 7);
    CHECK(counts.render == 6);
}

void ParentDestructionDetachesACallerRetainedChild() {
    TestNode child;
    Counts counts;
    const auto sink = CountingSink(counts);
    {
        auto root = std::make_unique<TestNode>();
        CHECK(root->AttachRenderInvalidationSink(sink));
        CHECK(root->Append(child));
    }

    CHECK(child.visual_parent() == nullptr);
    CHECK(child.inheritance_parent() == nullptr);
    const int after_destruction = counts.layout;
    child.set_width(31.0);
    CHECK(counts.layout == after_destruction);
}

void OneRootCannotBelongToTwoLiveIslandSinks() {
    TestNode root;
    Counts first_counts;
    Counts second_counts;
    const auto first = CountingSink(first_counts);
    const auto second = CountingSink(second_counts);

    CHECK(root.AttachRenderInvalidationSink(first));
    CHECK(root.AttachRenderInvalidationSink(first));
    CHECK(!root.AttachRenderInvalidationSink(second));
    root.set_width(1.0);
    CHECK(first_counts.layout == 1);
    CHECK(second_counts.layout == 0);

    root.DetachRenderInvalidationSink(first);
    CHECK(root.AttachRenderInvalidationSink(second));
    // A stale detach from the first island cannot detach the second.
    root.DetachRenderInvalidationSink(first);
    root.set_width(2.0);
    CHECK(first_counts.layout == 1);
    CHECK(second_counts.layout == 1);

    second->Close();
    CHECK(root.AttachRenderInvalidationSink(first));
    root.set_width(3.0);
    CHECK(first_counts.layout == 2);
}

void AVisualChildCannotAlsoBeClaimedAsAnIslandRoot() {
    TestNode parent;
    TestNode child;
    Counts counts;
    const auto sink = CountingSink(counts);

    CHECK(parent.AttachRenderInvalidationSink(sink));
    CHECK(parent.Append(child));
    CHECK(!child.AttachRenderInvalidationSink(sink));

    CHECK(parent.Remove(child));
    CHECK(child.visual_parent() == nullptr);
    CHECK(child.AttachRenderInvalidationSink(sink));
}

void AClaimedIslandRootCannotBecomeAVisualChild() {
    TestNode claimed_root;
    TestNode parent;
    Counts counts;
    const auto sink = CountingSink(counts);

    CHECK(claimed_root.AttachRenderInvalidationSink(sink));
    CHECK(!parent.Append(claimed_root));
    CHECK(claimed_root.visual_parent() == nullptr);

    claimed_root.DetachRenderInvalidationSink(sink);
    CHECK(parent.Append(claimed_root));
    CHECK(claimed_root.visual_parent() == &parent);
}

}  // namespace

int main() {
    RemovedAndReplacedChildrenLoseTheOldSinkBeforeMutation();
    ReparentingIsExplicitAndCyclesAreRejected();
    DetachClearsTheSinkBeforeInheritedValuesMove();
    ClosingOrDestroyingTheSinkMakesRetainedElementsInert();
    ArrangeInvalidationsAreNotMisreportedAsRenderOnly();
    ParentDestructionDetachesACallerRetainedChild();
    OneRootCannotBelongToTwoLiveIslandSinks();
    AVisualChildCannotAlsoBeClaimedAsAnIslandRoot();
    AClaimedIslandRootCannotBecomeAVisualChild();
    if (failures != 0)
        std::cerr << failures << " visual ownership checks failed\n";
    return failures == 0 ? 0 : 1;
}
