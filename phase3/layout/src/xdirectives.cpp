#include "xdirectives.h"

#include <vector>

#include "markup.h"

namespace openxaml {
namespace {

// x:Load takes a Boolean. XAML spells Booleans with either case, and Terminal
// writes "False" -- but a value that is neither is a mistake rather than a
// falsy default, because "x:Load=\"0\"" silently meaning False would hide a
// typo that keeps an element on screen.
bool ParseLoadValue(const std::string& text) {
    if (text == "True" || text == "true") return true;
    if (text == "False" || text == "false") return false;
    throw MarkupError("x:Load takes True or False, not \"" + text + "\"");
}

// x:DeferLoadStrategy has exactly one legal value. It is the older spelling of
// x:Load="False" and Terminal still uses it once.
bool ParseDeferValue(const std::string& text) {
    if (text == "Lazy") return true;
    throw MarkupError("x:DeferLoadStrategy takes Lazy, not \"" + text + "\"");
}

}  // namespace

XDirectives TakeXDirectives(std::map<std::string, std::string>& attributes) {
    XDirectives directives;
    std::vector<std::string> taken;
    bool deferral_seen = false;

    for (const auto& [name, value] : attributes) {
        if (name.rfind("x:", 0) != 0) continue;
        taken.push_back(name);

        // Names an element for a code-behind to reach. There is no code-behind
        // here and no namescope; the corpus reads its results out of the
        // measured tree by path, so a name has never moved an element.
        if (name == "x:Name") continue;

        if (name == "x:Uid") {
            if (value.empty()) throw MarkupError("x:Uid names no resource");
            directives.uid = value;
            directives.has_uid = true;
            continue;
        }
        // The two spellings of the same instruction. An element carrying both
        // is refused rather than resolved by attribute order: they are not
        // ordered in markup, so "whichever came last" would be a coin toss
        // dressed up as a rule.
        if (name == "x:Load" || name == "x:DeferLoadStrategy") {
            const bool defers =
                name == "x:Load" ? !ParseLoadValue(value) : ParseDeferValue(value);
            if (deferral_seen && defers != directives.deferred) {
                throw MarkupError(
                    "x:Load and x:DeferLoadStrategy on one element disagree about whether "
                    "it is realised");
            }
            directives.deferred = defers;
            deferral_seen = true;
            continue;
        }

        // x:Class, x:Bind, x:DataType, x:Key outside a dictionary, x:FieldModifier.
        // Each one is a real dependency on something a standalone load does not
        // have, and is refused by name so that the inventory of what is missing
        // stays accurate.
        throw MarkupError("the directive '" + name + "' is not implemented");
    }

    for (const std::string& name : taken) attributes.erase(name);
    return directives;
}

}  // namespace openxaml
