// The x: namespace directives, separated from the properties they sit beside.
//
// A directive is not a property. It instructs the loader rather than setting
// anything on the object, so it cannot go through the property registry -- the
// registry would report every one of them as "not found in type", which is the
// wrong answer for x:Name and the wrong *reason* for x:Load.
//
// The whole set is taken off an element's attributes in one pass, before the
// remainder is offered to the registry. That is what lets the registry keep
// saying "not a property of this type" and mean it, and it is what makes an
// unimplemented directive fail by its own name instead of by a property error.
//
// What is deliberately not here: silent tolerance. A directive this parser does
// not implement is a named load failure, because a directive that is dropped
// changes what the markup means without changing anything the numbers can show.

#ifndef OPENXAML_XDIRECTIVES_H
#define OPENXAML_XDIRECTIVES_H

#include <map>
#include <string>

namespace openxaml {

// What an element's x: directives tell the loader to do with it.
struct XDirectives {
    // x:Name, retained for namescope registration and storyboard targets.
    std::string name;

    // x:Load="False", or x:DeferLoadStrategy="Lazy" -- read, checked for
    // self-contradiction, and then not acted on, because that is what the
    // runtime does with it.
    //
    // Deferral is the XAML *compiler's*: it generates the code that creates the
    // element on demand, and a load from a string never went through it. The
    // corpus says so directly -- L5-xdirectives-load-false-sibling records a
    // StackPanel with three 18-high children as 54 high, and load-false-only-
    // child records the deferring child of a Border at its full 30 x 18. The
    // element is there, at its ordinary size, in its ordinary slot.
    bool deferred = false;

    // x:Uid, if the element carried one. Empty otherwise. The uid is a key
    // into a localised string table; see resw_strings.h for what supplies it
    // and markup.cpp for when it is applied.
    std::string uid;

    // Whether an x:Uid was written at all, which is not the same as it being
    // non-empty: x:Uid="" is markup that names no resource and is refused.
    bool has_uid = false;

    // Compile-time source type for {x:Bind}. The native loader receives the
    // already-adapted ObservableObject, so this is validation metadata rather
    // than a runtime cast, but it must still be retained and checked.
    std::string data_type;
};

// Removes every x: attribute from `attributes` and reports what they said,
// leaving the ordinary properties behind for the registry.
//
// Throws MarkupError for a directive outside the implemented set, and for an
// implemented one whose value cannot be read.
XDirectives TakeXDirectives(std::map<std::string, std::string>& attributes);

}  // namespace openxaml

#endif  // OPENXAML_XDIRECTIVES_H
