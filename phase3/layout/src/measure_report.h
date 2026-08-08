// How a measured tree is written down.
//
// Two tools report measurements: measure_cases, which runs the corpus through
// the text markup path, and xbf_equivalence, which runs the same corpus through
// the compiled-XBF path and diffs the two. They have to format identically or
// the diff would be about formatting, so the formatting lives here rather than
// in either of them.

#ifndef OPENXAML_MEASURE_REPORT_H
#define OPENXAML_MEASURE_REPORT_H

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "element.h"
#include "json.h"
#include "layout.h"

namespace openxaml {

// Fixed precision rather than shortest-round-trip, so two runs compare
// byte-for-byte and a diff is readable.
inline std::string ReportNumber(double value) {
    if (std::isinf(value)) return "\"Infinity\"";
    if (std::isnan(value)) return "\"NaN\"";
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << value;
    return out.str();
}

// JSON cannot spell infinity, so the corpus carries it as a string.
inline double ReadExtent(const JsonValue& value) {
    if (value.kind == JsonValue::Kind::String) {
        if (value.string == "Infinity") return kInfinity;
        throw JsonError("unexpected available_size value \"" + value.string + "\"");
    }
    if (value.kind != JsonValue::Kind::Number) throw JsonError("available_size is not a number");
    return value.number;
}

inline void WalkTree(const Element& element, const std::string& path,
                     std::vector<std::string>& out) {
    const Size desired = element.desired_size();
    const Size actual = element.render_size();
    const Rect slot = element.layout_slot();

    std::ostringstream line;
    line << "  {\"path\": \"" << JsonEscape(path) << "\""
         << ", \"type\": \"" << JsonEscape(element.TypeName()) << "\""
         << ", \"desired\": [" << ReportNumber(desired.width) << ", "
         << ReportNumber(desired.height) << "]"
         << ", \"actual\": [" << ReportNumber(actual.width) << ", " << ReportNumber(actual.height)
         << "]"
         << ", \"offset\": [" << ReportNumber(slot.x) << ", " << ReportNumber(slot.y) << "]}";
    out.push_back(line.str());

    int index = 0;
    for (const Element* child : element.RecordedChildren()) {
        WalkTree(*child, path + "/" + child->TypeName() + "[" + std::to_string(index++) + "]", out);
    }
}

}  // namespace openxaml

#endif  // OPENXAML_MEASURE_REPORT_H
