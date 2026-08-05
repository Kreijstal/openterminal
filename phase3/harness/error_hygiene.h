// Deciding whether an error message belongs to the case it was recorded on.
//
// Run 31017111065 recorded 19 failures, and 11 of them named a resource key
// that does not appear anywhere in the failing case's own markup. A harvested
// Terminal subtree "failed" on `BoxWidth`, a key only the authored L5-resources
// cases use; `L5-xprimitives-int32-width`, whose markup is
// `<Border.Width><x:Int32>60</x:Int32></Border.Width>` and mentions no resource
// at all, "failed" on `ScrollBarVerticalThumbMinHeight`. The proof that this is
// contamination rather than a surprising runtime is the size variants: `-s0`,
// `-s1` and `-s2` of one harvested case are the *same markup string* measured
// at three available sizes, and three of those triples came back with three
// different messages each -- while the `[Line: 1 Position: N]` suffix, which
// the parser fills in from the document it is actually reading, was identical
// across the triple and correct for that markup.
//
// The mechanism is WinRT restricted error info: it is per-thread global state,
// a XamlReader::Load can leave a description behind (including one that then
// went on to succeed), and winrt::hresult_error::message() serves whatever is
// on the thread when the code matches. So the description came from an earlier
// case and only the position was fresh. xaml_probe.cpp now clears that state
// before every load, which is the fix; this header is the check that the fix
// held, and the thing that stops a stale description being written into the
// database again if it did not.
//
// The test is deliberately one-sided. A "Cannot find a Resource with the
// Name/Key K" message can only be about a K the *markup asked for*, so a K that
// does not occur in the markup at all is proof the message is not this case's.
// The converse proves nothing, so a key that does occur is always kept, and a
// message with no key in it is always kept -- including the assignment failures
// that same run misfiled, which have no key to check and which this cannot
// catch. Substring, not token matching: a key inside an attribute value, which
// is where nearly every one of them lives, has to count as present.
//
// Header-only and free of every dependency, like json_text.h beside it, so
// phase3/layout's test suite can include it and ctest can run it on Linux --
// the probe itself builds only on Windows and is checked nowhere else.

#ifndef OPENXAML_HARNESS_ERROR_HYGIENE_H
#define OPENXAML_HARNESS_ERROR_HYGIENE_H

#include <cstdint>
#include <string>

namespace openxaml_harness {

// The marker XAML's parser writes in front of the key it could not resolve.
inline const char* ResourceKeyMarker() { return "Name/Key"; }

// The resource key a message names, or an empty string if it names none.
// The key runs to the next space, which is where the `[Line: 1 Position: N]`
// suffix begins.
inline std::string ResourceKeyNamedBy(const std::string& message) {
    const std::string marker = ResourceKeyMarker();
    auto at = message.find(marker);
    if (at == std::string::npos) return {};
    at += marker.size();
    while (at < message.size() && (message[at] == ' ' || message[at] == '\t')) ++at;
    std::string key;
    for (size_t i = at; i < message.size(); ++i) {
        char c = message[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '[') break;
        key += c;
    }
    return key;
}

// True only when the message names a key and the markup does not contain it,
// which is proof the message was left on the thread by some other case.
inline bool NamesAnAbsentResourceKey(const std::string& markup, const std::string& message) {
    auto key = ResourceKeyNamedBy(message);
    if (key.empty()) return false;
    return markup.find(key) == std::string::npos;
}

inline std::string HexHresult(uint32_t code) {
    static const char* digits = "0123456789abcdef";
    std::string out = "0x";
    for (int shift = 28; shift >= 0; shift -= 4)
        out += digits[(code >> shift) & 0xF];
    return out;
}

// What to record for a rejection. The message is kept as it arrived unless it
// is provably about another case, in which case the HRESULT names the failure
// and the suspect text is kept behind a label rather than thrown away: it is
// still evidence, it is just evidence about something else.
inline std::string HygienicError(const std::string& markup,
                                 const std::string& message,
                                 uint32_t code) {
    if (!NamesAnAbsentResourceKey(markup, message)) return message;
    return "the runtime refused the markup with hresult " + HexHresult(code)
           + "; the message on the thread named the resource key \""
           + ResourceKeyNamedBy(message)
           + "\", which this case's markup never mentions, so it is stale "
             "restricted error info from an earlier case and not a description "
             "of this failure. It was: " + message;
}

}  // namespace openxaml_harness

#endif  // OPENXAML_HARNESS_ERROR_HYGIENE_H
