#include "events.h"

#include <algorithm>

#include "element.h"

namespace openxaml {
namespace {

// The pass state. One layout pass at a time, as the reference has one layout
// manager per visual tree and this implementation has one tree per run.
struct PassState {
    int depth = 0;
    // Elements whose render size moved, in the order ArrangeCore reached them,
    // with the size each had before. Drained backwards -- see events.h.
    std::vector<std::pair<Element*, Size>> size_changed;
    // Every element that currently has a LayoutUpdated handler, which is this
    // implementation's m_nLayoutUpdatedSubscriberCounter. Kept outside the
    // per-pass fields because a subscription outlives a pass.
    std::vector<Element*> layout_updated;
    // Set while the queue is being drained, so that a handler which arranges
    // something does not re-enter the drain.
    bool draining = false;
};

PassState& State() {
    static PassState state;
    return state;
}

// dxaml's IsSameSize, which compares both axes with the layout tolerance
// rather than exactly -- a size that arithmetic moved by less than the
// tolerance is the size it already was, and did not change.
bool SameSize(Size a, Size b) {
    return AreClose(a.width, b.width) && AreClose(a.height, b.height);
}

}  // namespace

const char* NameOf(FrameworkEvent event) {
    switch (event) {
        case FrameworkEvent::Loaded: return "Loaded";
        case FrameworkEvent::Unloaded: return "Unloaded";
        case FrameworkEvent::SizeChanged: return "SizeChanged";
        case FrameworkEvent::LayoutUpdated: return "LayoutUpdated";
        case FrameworkEvent::PointerPressed: return "PointerPressed";
        case FrameworkEvent::PointerReleased: return "PointerReleased";
        case FrameworkEvent::PointerMoved: return "PointerMoved";
        case FrameworkEvent::PointerEntered: return "PointerEntered";
        case FrameworkEvent::PointerExited: return "PointerExited";
        case FrameworkEvent::KeyDown: return "KeyDown";
        case FrameworkEvent::KeyUp: return "KeyUp";
        case FrameworkEvent::GotFocus: return "GotFocus";
        case FrameworkEvent::LostFocus: return "LostFocus";
        case FrameworkEvent::Tapped: return "Tapped";
        case FrameworkEvent::DoubleTapped: return "DoubleTapped";
        case FrameworkEvent::RightTapped: return "RightTapped";
    }
    return "?";
}

bool IsRaised(FrameworkEvent event) {
    return event == FrameworkEvent::SizeChanged || event == FrameworkEvent::LayoutUpdated;
}

// --- EventRegistrations -------------------------------------------------------

EventRegistrations::~EventRegistrations() {
    // A subscriber list that outlives its element is a list of dangling
    // pointers, so leaving is not optional.
    if (subscribed_to_layout_updated_ && owner_)
        LayoutPass::RemoveLayoutUpdatedSubscriber(*owner_);
}

EventToken EventRegistrations::Add(FrameworkEvent event, Handler handler) {
    if (!handler) return 0;
    const EventToken token = next_token_++;
    handlers_[event].emplace(token, std::move(handler));
    if (event == FrameworkEvent::LayoutUpdated) UpdateLayoutUpdatedSubscription();
    return token;
}

bool EventRegistrations::Remove(FrameworkEvent event, EventToken token) {
    const auto found = handlers_.find(event);
    if (found == handlers_.end()) return false;
    const bool removed = found->second.erase(token) != 0;
    if (found->second.empty()) handlers_.erase(found);
    if (event == FrameworkEvent::LayoutUpdated) UpdateLayoutUpdatedSubscription();
    return removed;
}

size_t EventRegistrations::Count(FrameworkEvent event) const {
    const auto found = handlers_.find(event);
    return found == handlers_.end() ? 0 : found->second.size();
}

void EventRegistrations::Raise(Element& element, FrameworkEvent event,
                               const SizeChangedArgs& args) const {
    const auto found = handlers_.find(event);
    if (found == handlers_.end()) return;
    std::vector<std::pair<EventToken, Handler>> snapshot(found->second.begin(),
                                                         found->second.end());
    for (const auto& [token, handler] : snapshot) {
        (void)token;
        // Re-checked rather than called from the snapshot: a handler is
        // allowed to remove another, and one that was removed must not run.
        const auto live = handlers_.find(event);
        if (live == handlers_.end() || live->second.count(token) == 0) continue;
        handler(element, event, args);
    }
}

void EventRegistrations::UpdateLayoutUpdatedSubscription() {
    if (!owner_) return;
    const bool wanted = Any(FrameworkEvent::LayoutUpdated);
    if (wanted == subscribed_to_layout_updated_) return;
    subscribed_to_layout_updated_ = wanted;
    if (wanted) {
        LayoutPass::AddLayoutUpdatedSubscriber(*owner_);
    } else {
        LayoutPass::RemoveLayoutUpdatedSubscriber(*owner_);
    }
}

// --- LayoutPass ---------------------------------------------------------------

LayoutPass::LayoutPass() {
    PassState& state = State();
    outermost_ = state.depth == 0;
    ++state.depth;
}

LayoutPass::~LayoutPass() {
    PassState& state = State();
    --state.depth;
    if (!outermost_ || state.draining) return;

    // The pass is over. Raise what it accumulated, in the reference's order:
    // the size-changed queue backwards, then LayoutUpdated once per
    // subscriber. Guarded against re-entry, so that a handler which measures
    // or arranges something does not start a second drain inside this one --
    // the reference loops its layout pass instead, which this implementation
    // has no equivalent of and does not pretend to.
    state.draining = true;

    std::vector<std::pair<Element*, Size>> queued;
    queued.swap(state.size_changed);
    for (auto entry = queued.rbegin(); entry != queued.rend(); ++entry) {
        Element& element = *entry->first;
        const SizeChangedArgs args{entry->second, element.render_size()};
        element.events().Raise(element, FrameworkEvent::SizeChanged, args);
    }

    // A copy, because a handler may register or drop a subscription.
    const std::vector<Element*> subscribers = state.layout_updated;
    for (Element* element : subscribers) {
        if (std::find(state.layout_updated.begin(), state.layout_updated.end(), element) ==
            state.layout_updated.end()) {
            continue;
        }
        element->events().Raise(*element, FrameworkEvent::LayoutUpdated, SizeChangedArgs{});
    }

    state.draining = false;
}

void LayoutPass::EnqueueSizeChanged(Element& element, Size previous, Size current) {
    PassState& state = State();
    if (state.depth == 0) return;
    if (!element.events().Any(FrameworkEvent::SizeChanged)) return;
    if (SameSize(previous, current)) return;
    for (const auto& entry : state.size_changed) {
        // Already queued this pass. The reference's GetSizeChanged flag; an
        // element arranged twice in one pass reports the size it started the
        // pass with, not the one it had in between.
        if (entry.first == &element) return;
    }
    state.size_changed.emplace_back(&element, previous);
}

void LayoutPass::AddLayoutUpdatedSubscriber(Element& element) {
    PassState& state = State();
    if (std::find(state.layout_updated.begin(), state.layout_updated.end(), &element) ==
        state.layout_updated.end()) {
        state.layout_updated.push_back(&element);
    }
}

void LayoutPass::RemoveLayoutUpdatedSubscriber(Element& element) {
    PassState& state = State();
    state.layout_updated.erase(
        std::remove(state.layout_updated.begin(), state.layout_updated.end(), &element),
        state.layout_updated.end());
}

bool LayoutPass::InProgress() { return State().depth != 0; }

}  // namespace openxaml
