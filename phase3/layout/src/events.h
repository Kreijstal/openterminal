// Framework event registration, and the two events a layout pass can raise.
//
// A WinRT event is an add_/remove_ pair returning and taking a token, and the
// only thing a caller can check without a UI is that the token it was given
// takes its handler back off again. That much is here, for every event the
// element interfaces declare that Terminal subscribes to.
//
// *Raising* is a different question, and the answer is different per event.
// The rule this file keeps: an event is raised only where the published XAML
// core says when, and where "when" is something this implementation can
// actually reach. Two events qualify.
//
//   SizeChanged   dxaml/xcp/core/core/elements/framework.cpp (188f602b) ends
//                 ArrangeCore with
//
//                     RenderSize = innerInkSize;
//                     if (!IsSameSize(oldRenderSize, innerInkSize))
//                         layoutManager->EnqueueForSizeChanged(this, oldRenderSize);
//
//                 and EnqueueForSizeChanged files the element only if it has
//                 handlers and is not already queued. The queue is drained
//                 after the arrange pass, by CLayoutManager::RaiseSizeChangedEvents
//                 (dxaml/xcp/core/layout/LayoutManager.cpp), which walks it
//                 *backwards* -- the comment there says so: "note that the
//                 order in which we call the event is reversed". Since a
//                 parent's RenderSize is assigned after its ArrangeOverride
//                 has arranged its children, children enqueue first, so the
//                 reversal delivers the event to a parent before its children.
//
//   LayoutUpdated raised in the same drain, after the size-changed queue, once
//                 per pass, to every subscriber -- CLayoutManager::UpdateLayout
//                 raises it with a NULL target, which is what "every handler
//                 in the application" looks like in that source. It is raised
//                 only when there is at least one subscriber, which is what
//                 m_nLayoutUpdatedSubscriberCounter is for.
//
// Everything else is stored and not raised, and says so by name:
//
//   Loaded/Unloaded  the runtime raises these from CEventManager on the tick
//                    after an element enters or leaves the live visual tree
//                    (CCoreServices::RaisePendingLoadedRequests ->
//                    CEventManager::RaiseLoadedEvent). This implementation has
//                    no live tree, no application and no tick: there is no
//                    moment here that is the moment the reference names, so
//                    registering a Loaded handler stores it and nothing ever
//                    calls it.
//   pointer, key, focus, tap  input routing is a later wave. There is no
//                    source of an input event here at all.
//
// A stored-and-never-raised handler is not a lie as long as it is written
// down, and it is what lets Terminal's boot code register what it registers
// without being told E_NOTIMPL.

#ifndef OPENXAML_EVENTS_H
#define OPENXAML_EVENTS_H

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "layout.h"

namespace openxaml {

class Element;

// The events an element carries. The list is what Windows Terminal's
// TerminalApp and TerminalControl subscribe to through the framework element
// interfaces, plus Unloaded, which pairs with Loaded.
enum class FrameworkEvent {
    Loaded,
    Unloaded,
    SizeChanged,
    LayoutUpdated,
    PointerPressed,
    PointerReleased,
    PointerMoved,
    PointerEntered,
    PointerExited,
    KeyDown,
    KeyUp,
    GotFocus,
    LostFocus,
    Tapped,
    DoubleTapped,
    RightTapped,
};

// The name the ABI and the tests use for an event, for error messages and for
// saying which event a refusal is about.
const char* NameOf(FrameworkEvent event);

// Whether this implementation ever raises it. False is the honest answer for
// all but two; see the note at the top of this file.
bool IsRaised(FrameworkEvent event);

// What a SizeChanged handler is given: the two sizes, as
// SizeChangedEventArgs carries them.
struct SizeChangedArgs {
    Size previous;
    Size current;
};

// A registration token. INT64 because that is what an
// EventRegistrationToken holds across the ABI, and because a token has to be
// able to name a registration that has already been removed without being
// mistaken for a live one -- so it counts up and is never reused.
using EventToken = std::int64_t;

// The handlers registered on one element.
//
// Held by value in Element. The handler signature is the same for every event
// because the only argument any of them carries here is the size pair, and
// the events that carry nothing ignore it.
class EventRegistrations {
public:
    using Handler = std::function<void(Element&, FrameworkEvent, const SizeChangedArgs&)>;

    EventRegistrations() = default;
    ~EventRegistrations();

    EventRegistrations(const EventRegistrations&) = delete;
    EventRegistrations& operator=(const EventRegistrations&) = delete;

    // Never returns zero: zero is what an unset EventRegistrationToken holds,
    // so a real token has to be distinguishable from one.
    EventToken Add(FrameworkEvent event, Handler handler);

    // True if the token named a live registration on this event. A token from
    // another event, or one already removed, is false rather than an error --
    // remove_ on a stale token is a caller's business, not a failure.
    bool Remove(FrameworkEvent event, EventToken token);

    size_t Count(FrameworkEvent event) const;
    bool Any(FrameworkEvent event) const { return Count(event) != 0; }

    // Calls every handler registered for the event, in registration order,
    // against a snapshot -- a handler may remove itself or another.
    void Raise(Element& element, FrameworkEvent event, const SizeChangedArgs& args) const;

    // The element these belong to, so that a LayoutUpdated subscription can
    // find its way back. Set once, by Element.
    void Bind(Element* owner) { owner_ = owner; }

private:
    void UpdateLayoutUpdatedSubscription();

    std::map<FrameworkEvent, std::map<EventToken, Handler>> handlers_;
    EventToken next_token_ = 1;
    Element* owner_ = nullptr;
    bool subscribed_to_layout_updated_ = false;
};

// The layout manager's share of a pass.
//
// The real one owns the measure/arrange loop; this one owns only the part that
// decides when an event may be raised, because that is the only part the
// layout core does not already do for itself. Element::Arrange opens a pass on
// its outermost call and drains it on the way out.
class LayoutPass {
public:
    // Opens a pass, or joins the one already open. Non-copyable and scoped:
    // the drain happens when the outermost one goes away, including when an
    // exception leaves through it.
    LayoutPass();
    ~LayoutPass();

    LayoutPass(const LayoutPass&) = delete;
    LayoutPass& operator=(const LayoutPass&) = delete;

    bool outermost() const { return outermost_; }

    // Files an element whose render size moved. Ignored unless the element has
    // SizeChanged handlers, and never files the same element twice in one
    // pass -- both conditions are EnqueueForSizeChanged's.
    static void EnqueueSizeChanged(Element& element, Size previous, Size current);

    // Registers an element as a LayoutUpdated subscriber for as long as it has
    // handlers for it. Called by EventRegistrations, not by layout.
    static void AddLayoutUpdatedSubscriber(Element& element);
    static void RemoveLayoutUpdatedSubscriber(Element& element);

    // Whether a pass is open. The queue is only meaningful inside one.
    static bool InProgress();

private:
    bool outermost_ = false;
};

}  // namespace openxaml

#endif  // OPENXAML_EVENTS_H
