#include "geometry.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

#include "markup.h"

namespace openxaml {
namespace {

// One axis of a cubic segment, at parameter t.
double CubicAt(double p0, double p1, double p2, double p3, double t) {
    const double u = 1.0 - t;
    return u * u * u * p0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3;
}

// The extremes of one axis of a cubic segment. The endpoints are always
// candidates; the interior ones are the roots of the derivative, which is a
// quadratic, so there are at most two of them.
void AddCubicExtremes(double p0, double p1, double p2, double p3, double& low, double& high) {
    low = std::min(p0, p3);
    high = std::max(p0, p3);

    const double a = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
    const double b = 2.0 * (p0 - 2.0 * p1 + p2);
    const double c = -p0 + p1;

    auto consider = [&](double t) {
        if (t <= 0.0 || t >= 1.0) return;
        const double value = CubicAt(p0, p1, p2, p3, t);
        low = std::min(low, value);
        high = std::max(high, value);
    };

    if (std::abs(a) < 1e-12) {
        // Degenerate to the linear derivative 3*(b*t + c) -- one root.
        if (std::abs(b) > 1e-12) consider(-c / b);
        return;
    }
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) return;
    const double root = std::sqrt(discriminant);
    consider((-b + root) / (2.0 * a));
    consider((-b - root) / (2.0 * a));
}

class DataScanner {
public:
    explicit DataScanner(const std::string& text) : text_(text) {}

    void SkipSeparators() {
        while (position_ < text_.size() &&
               (std::isspace(static_cast<unsigned char>(text_[position_])) ||
                text_[position_] == ',')) {
            ++position_;
        }
    }

    bool AtEnd() {
        SkipSeparators();
        return position_ >= text_.size();
    }

    char PeekCommand() {
        SkipSeparators();
        return position_ < text_.size() ? text_[position_] : '\0';
    }

    char TakeCommand() {
        const char command = PeekCommand();
        ++position_;
        return command;
    }

    // True when the next token is a number, which is how a repeated command
    // is recognised: "L 1,2 3,4" is two line segments, not a syntax error.
    bool PeekNumber() {
        SkipSeparators();
        if (position_ >= text_.size()) return false;
        const char c = text_[position_];
        return std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.';
    }

    double TakeNumber() {
        SkipSeparators();
        const char* start = text_.c_str() + position_;
        char* end = nullptr;
        const double value = std::strtod(start, &end);
        if (end == start)
            throw MarkupError("expected a number in path data at offset " +
                              std::to_string(position_));
        position_ += static_cast<size_t>(end - start);
        return value;
    }

private:
    const std::string& text_;
    size_t position_ = 0;
};

}  // namespace

void GeometryBounds::Add(double x, double y) {
    if (empty) {
        empty = false;
        left = right = x;
        top = bottom = y;
        return;
    }
    left = std::min(left, x);
    right = std::max(right, x);
    top = std::min(top, y);
    bottom = std::max(bottom, y);
}

GeometryBounds PathGeometryBounds(const std::string& data) {
    GeometryBounds bounds;
    DataScanner scanner(data);

    double x = 0.0, y = 0.0;              // current point
    double start_x = 0.0, start_y = 0.0;  // start of the current figure
    // The reflected control point that S and T need. Only meaningful when the
    // previous command was of the matching kind; otherwise the reflection is
    // the current point, which is what the spec says.
    double last_control_x = 0.0, last_control_y = 0.0;
    char previous = '\0';
    bool have_start = false;

    // An optional leading fill rule. It decides which parts of a
    // self-intersecting figure are inside, and the bounds do not depend on
    // that -- but skipping it silently would also skip a malformed one.
    if (scanner.PeekCommand() == 'F') {
        scanner.TakeCommand();
        const double rule = scanner.TakeNumber();
        if (rule != 0.0 && rule != 1.0)
            throw MarkupError("\"F" + std::to_string(static_cast<int>(rule)) +
                              "\" is not a fill rule");
    }

    while (!scanner.AtEnd()) {
        char command = scanner.PeekCommand();
        if (std::isdigit(static_cast<unsigned char>(command)) || command == '-' ||
            command == '+' || command == '.') {
            // A repeated command: the previous one applies again. A repeated
            // M continues as L, which is the one case where the implied
            // command is not the one that was written.
            if (previous == '\0') throw MarkupError("path data starts with a number");
            command = previous == 'M' ? 'L' : (previous == 'm' ? 'l' : previous);
        } else {
            scanner.TakeCommand();
        }

        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        const double origin_x = relative ? x : 0.0;
        const double origin_y = relative ? y : 0.0;

        switch (std::toupper(static_cast<unsigned char>(command))) {
            case 'M': {
                x = origin_x + scanner.TakeNumber();
                y = origin_y + scanner.TakeNumber();
                start_x = x;
                start_y = y;
                have_start = true;
                bounds.Add(x, y);
                break;
            }
            case 'L': {
                x = origin_x + scanner.TakeNumber();
                y = origin_y + scanner.TakeNumber();
                bounds.Add(x, y);
                break;
            }
            case 'H': {
                x = origin_x + scanner.TakeNumber();
                bounds.Add(x, y);
                break;
            }
            case 'V': {
                y = origin_y + scanner.TakeNumber();
                bounds.Add(x, y);
                break;
            }
            case 'C':
            case 'S': {
                double c1x, c1y;
                if (std::toupper(static_cast<unsigned char>(command)) == 'S') {
                    const bool reflect = previous == 'C' || previous == 'c' || previous == 'S' ||
                                         previous == 's';
                    c1x = reflect ? 2.0 * x - last_control_x : x;
                    c1y = reflect ? 2.0 * y - last_control_y : y;
                } else {
                    c1x = origin_x + scanner.TakeNumber();
                    c1y = origin_y + scanner.TakeNumber();
                }
                const double c2x = origin_x + scanner.TakeNumber();
                const double c2y = origin_y + scanner.TakeNumber();
                const double ex = origin_x + scanner.TakeNumber();
                const double ey = origin_y + scanner.TakeNumber();

                double x_low = 0.0, x_high = 0.0, y_low = 0.0, y_high = 0.0;
                AddCubicExtremes(x, c1x, c2x, ex, x_low, x_high);
                AddCubicExtremes(y, c1y, c2y, ey, y_low, y_high);
                bounds.Add(x_low, y_low);
                bounds.Add(x_high, y_high);

                last_control_x = c2x;
                last_control_y = c2y;
                x = ex;
                y = ey;
                break;
            }
            case 'Q':
            case 'T': {
                double cx, cy;
                if (std::toupper(static_cast<unsigned char>(command)) == 'T') {
                    const bool reflect = previous == 'Q' || previous == 'q' || previous == 'T' ||
                                         previous == 't';
                    cx = reflect ? 2.0 * x - last_control_x : x;
                    cy = reflect ? 2.0 * y - last_control_y : y;
                } else {
                    cx = origin_x + scanner.TakeNumber();
                    cy = origin_y + scanner.TakeNumber();
                }
                const double ex = origin_x + scanner.TakeNumber();
                const double ey = origin_y + scanner.TakeNumber();

                // Raised to a cubic rather than solved separately: the same
                // extreme-finding then covers both, and the elevation is exact.
                double x_low = 0.0, x_high = 0.0, y_low = 0.0, y_high = 0.0;
                AddCubicExtremes(x, x + 2.0 * (cx - x) / 3.0, ex + 2.0 * (cx - ex) / 3.0, ex, x_low,
                                 x_high);
                AddCubicExtremes(y, y + 2.0 * (cy - y) / 3.0, ey + 2.0 * (cy - ey) / 3.0, ey, y_low,
                                 y_high);
                bounds.Add(x_low, y_low);
                bounds.Add(x_high, y_high);

                last_control_x = cx;
                last_control_y = cy;
                x = ex;
                y = ey;
                break;
            }
            case 'Z': {
                if (have_start) {
                    x = start_x;
                    y = start_y;
                    bounds.Add(x, y);
                }
                break;
            }
            case 'A':
                // Elliptical arcs need the endpoint-to-centre conversion and
                // then the extremes of a rotated ellipse arc. No case in the
                // corpus has one, so there is nothing to check an
                // implementation against.
                throw MarkupError("the path command 'A' is not implemented");
            default:
                throw MarkupError(std::string("the path command '") + command +
                                  "' is not implemented");
        }

        previous = command;
        // A repeated segment must not be read as a repeat of the implied
        // command's letter case, so the recorded command keeps its own case.
        if (std::toupper(static_cast<unsigned char>(command)) != 'C' &&
            std::toupper(static_cast<unsigned char>(command)) != 'S' &&
            std::toupper(static_cast<unsigned char>(command)) != 'Q' &&
            std::toupper(static_cast<unsigned char>(command)) != 'T') {
            last_control_x = x;
            last_control_y = y;
        }
    }

    return bounds;
}

}  // namespace openxaml
