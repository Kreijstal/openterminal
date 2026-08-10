#include "case_runner.h"

#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>

#include "element.h"
#include "cpu_raster_backend.h"
#include "json.h"
#include "markup.h"
#include "resources.h"
#include "resw_strings.h"

namespace openxaml {
namespace render {
namespace {

// Fixed precision, exactly as measure_cases writes it -- the tree this harness
// emits has to be comparable byte-for-byte with the measurement path's, which
// is the whole point of emitting it.
std::string Number(double value) {
    if (std::isinf(value)) return "\"Infinity\"";
    if (std::isnan(value)) return "\"NaN\"";
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << value;
    return out.str();
}

std::string TextNumber(double value) {
    if (std::isinf(value)) return "\"Infinity\"";
    if (std::isnan(value)) return "\"NaN\"";
    std::ostringstream out;
    // The public DirectWrite boundary records six decimal places. Text fields
    // use the same representation so acceptance is equality, not a tolerance.
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

double ReadExtent(const JsonValue& value) {
    if (value.kind == JsonValue::Kind::String) {
        if (value.string == "Infinity") return kInfinity;
        throw JsonError("unexpected available_size value \"" + value.string + "\"");
    }
    if (value.kind != JsonValue::Kind::Number) throw JsonError("available_size is not a number");
    return value.number;
}

// The same walk measure_cases does. Copied rather than shared because the two
// binaries are the two independent statements being compared: if this file
// called into the measurement path's writer, a diff between the two outputs
// could not fail, and the check would be vacuous.
void WalkTree(const Element& element, const std::string& path, std::vector<std::string>& out) {
    const Size desired = element.desired_size();
    const Size actual = element.render_size();
    const Rect slot = element.layout_slot();

    std::ostringstream line;
    line << "  {\"path\": \"" << JsonEscape(path) << "\""
         << ", \"type\": \"" << JsonEscape(element.TypeName()) << "\""
         << ", \"desired\": [" << Number(desired.width) << ", " << Number(desired.height) << "]"
         << ", \"actual\": [" << Number(actual.width) << ", " << Number(actual.height) << "]"
         << ", \"offset\": [" << Number(slot.x) << ", " << Number(slot.y) << "]}";
    out.push_back(line.str());

    int index = 0;
    for (const Element* child : element.RecordedChildren())
        WalkTree(*child, path + "/" + child->TypeName() + "[" + std::to_string(index++) + "]",
                 out);
}

std::string RectJson(const Rect& rect) {
    std::ostringstream out;
    out << "[" << Number(rect.x) << ", " << Number(rect.y) << ", " << Number(rect.width) << ", "
        << Number(rect.height) << "]";
    return out.str();
}

std::string PixelRectJson(const PixelRect& box) {
    std::ostringstream out;
    out << "[" << box.left << ", " << box.top << ", " << box.right << ", " << box.bottom << "]";
    return out.str();
}

std::string ColorJson(Color c) {
    static const char* kHex = "0123456789abcdef";
    std::string out = "\"#";
    for (unsigned char channel : {c.a, c.r, c.g, c.b}) {
        out.push_back(kHex[channel >> 4]);
        out.push_back(kHex[channel & 0xf]);
    }
    out.push_back('"');
    return out;
}

const char* RenderIssueName(RenderIssueCode code) {
    switch (code) {
        case RenderIssueCode::InvalidScene: return "invalid-scene";
        case RenderIssueCode::UnsupportedTransform: return "unsupported-transform";
        case RenderIssueCode::UnsupportedClip: return "unsupported-clip";
        case RenderIssueCode::UnsupportedOpacity: return "unsupported-opacity";
        case RenderIssueCode::UnsupportedBrushAlpha: return "unsupported-brush-alpha";
        case RenderIssueCode::UnsupportedImageBrush: return "unsupported-image-brush";
        case RenderIssueCode::UnsupportedExternalSurface:
            return "unsupported-external-surface";
        case RenderIssueCode::MissingTextRasterizer: return "missing-text-rasterizer";
        case RenderIssueCode::TextRasterizerFailure: return "text-rasterizer-failure";
    }
    return "unknown";
}

class TextBackendAdapter final : public TextRasterizer {
public:
    TextBackendAdapter(TextBackend& backend, std::vector<std::string>& failures)
        : backend_(backend), failures_(failures) {}

    bool DrawText(const TextRasterRequest& request, Surface& surface,
                  std::string& message) override {
        TextOp run;
        run.bounds = request.bounds;
        run.has_clip = request.has_clip;
        run.clip = request.clip;
        run.text = request.text.text;
        run.font_family = request.text.font_family;
        run.font_size = request.text.font_size;
        run.baseline = request.text.baseline;
        run.advances = request.text.advances;
        run.wrap = request.text.wrap;
        run.bold = request.text.bold;
        run.language = request.text.language;

        const std::size_t failure_count = failures_.size();
        // A retained LocalText owns its authored Foreground. The legacy probe
        // marker is already made explicit when Build creates that command for
        // an undeclared Foreground; it must not replace an authored color here.
        backend_.DrawRuns(surface, std::vector<TextOp>{std::move(run)},
                          request.text.color, failures_);
        if (failures_.size() == failure_count) return true;
        message = failures_.back();
        return false;
    }

private:
    TextBackend& backend_;
    std::vector<std::string>& failures_;
};

}  // namespace

CaseResult LayOutCase(const std::string& case_json) {
    CaseResult result;
    std::unique_ptr<Element> root;
    Size surface{};
    try {
        const JsonValue document = ParseJson(case_json);
        result.id = document.At("id").string;
        const JsonValue& environment = document.At("environment");
        if (environment.Has("theme"))
            ThemeResourceLibrary::Default().SetActiveTheme(environment.At("theme").string);
        const JsonValue& extent = environment.At("available_size");
        if (extent.array.size() != 2) throw JsonError("available_size needs two entries");
        const Size available{ReadExtent(extent.array[0]), ReadExtent(extent.array[1])};

        root = LoadMarkup(document.At("markup").string, StringTable{});
        root->Measure(available);
        const Size desired = root->desired_size();
        // The same fallback measure_cases uses: an infinite final rect is not a
        // legal arrange input, so an unbounded axis takes what was asked for.
        surface = Size{std::isinf(available.width) ? desired.width : available.width,
                       std::isinf(available.height) ? desired.height : available.height};
        root->Arrange({0.0, 0.0, surface.width, surface.height});
    } catch (const std::exception& e) {
        result.load_error = e.what();
        return result;
    }

    std::vector<std::string> tree;
    WalkTree(*root, "/" + root->TypeName(), tree);
    std::ostringstream out;
    out << "{\n \"schema_version\": 1,\n \"case_id\": \"" << JsonEscape(result.id) << "\",\n";
    out << " \"tree\": [\n";
    for (size_t i = 0; i < tree.size(); ++i) out << tree[i] << (i + 1 < tree.size() ? ",\n" : "\n");
    out << " ]\n}\n";
    result.tree_json = out.str();

    result.list = Build(*root, surface);
    result.has_surface = true;
    return result;
}

Surface RasterizeDisplayList(const DisplayList& list, TextBackend* backend, Color clear,
                             Color /*legacy_text_ink*/, std::vector<std::string>& text_failures,
                             std::vector<RenderIssue>& render_issues,
                             ExternalSurfaceReader* external_reader) {
    text_failures.clear();
    render_issues.clear();
    if (!list.scene) {
        const PixelRect box =
            SnapRect(Rect{0.0, 0.0, list.surface.width, list.surface.height});
        render_issues.push_back(
            RenderIssue{RenderIssueCode::InvalidScene, NodeId{}, RenderIssue::kNoCommand,
                        "the display list has no retained scene"});
        return Surface(box.right, box.bottom, clear);
    }

    CpuRasterBackend rasterizer;
    std::unique_ptr<TextBackendAdapter> text_adapter;
    if (backend) {
        text_adapter = std::make_unique<TextBackendAdapter>(*backend, text_failures);
    }
    RasterResult rendered =
        rasterizer.Render(*list.scene, clear, text_adapter.get(), external_reader);
    render_issues = std::move(rendered.issues);
    return std::move(rendered.surface);
}

Surface PaintCase(CaseResult& result, TextBackend* backend, Color clear) {
    return RasterizeDisplayList(result.list, backend, clear, ProbeInkColor(),
                                result.text_failures, result.render_issues);
}

std::string SidecarJson(const CaseResult& result, const Surface& surface,
                        const std::string& backend_name, Color clear) {
    std::ostringstream out;
    out << "{\n";
    out << " \"schema_version\": 1,\n";
    out << " \"case_id\": \"" << JsonEscape(result.id) << "\",\n";
    out << " \"backend\": \"" << JsonEscape(backend_name) << "\",\n";
    out << " \"surface\": [" << surface.width() << ", " << surface.height() << "],\n";
    out << " \"backdrop\": " << ColorJson(clear) << ",\n";
    out << " \"probe_ink\": " << ColorJson(ProbeInkColor()) << ",\n";

    out << " \"geometry\": [\n";
    for (size_t i = 0; i < result.list.geometry.size(); ++i) {
        const NodeGeometry& node = result.list.geometry[i];
        out << "  {\"path\": \"" << JsonEscape(node.path) << "\", \"type\": \""
            << JsonEscape(node.type) << "\", \"slot\": " << RectJson(node.slot)
            << ", \"actual\": [" << Number(node.actual.width) << ", "
            << Number(node.actual.height) << "], \"abs\": [" << Number(node.abs_x) << ", "
            << Number(node.abs_y) << "], \"layout_storage\": "
            << (node.has_layout_storage ? "true" : "false")
            << ", \"opacity\": " << Number(node.opacity)
            << ", \"transform_to_root\": {\"origin\": [" << Number(node.abs_x) << ", "
            << Number(node.abs_y) << "], \"unit_x\": ["
            << Number(node.transform_to_root.TransformPoint({1.0, 0.0}).x) << ", "
            << Number(node.transform_to_root.TransformPoint({1.0, 0.0}).y)
            << "], \"unit_y\": ["
            << Number(node.transform_to_root.TransformPoint({0.0, 1.0}).x) << ", "
            << Number(node.transform_to_root.TransformPoint({0.0, 1.0}).y)
            << "]}, \"clip\": ";
        if (node.clip.kind == Clip::Kind::Rect) {
            out << "{\"type\": \"Windows.UI.Xaml.Media.RectangleGeometry\", \"bounds\": "
                << RectJson(node.clip.bounds) << "}";
        } else {
            out << "null";
        }
        out << ", \"z_index\": " << node.z_index
            << ", \"visible\": " << (node.visible ? "true" : "false") << "}"
            << (i + 1 < result.list.geometry.size() ? ",\n" : "\n");
    }
    out << " ],\n";

    out << " \"rects\": [\n";
    for (size_t i = 0; i < result.list.rects.size(); ++i) {
        const RectOp& op = result.list.rects[i];
        out << "  {\"path\": \"" << JsonEscape(op.path) << "\", \"what\": \""
            << JsonEscape(op.what) << "\", \"bounds\": " << RectJson(op.bounds)
            << ", \"pixels\": " << PixelRectJson(SnapRect(op.bounds))
            << ", \"color\": " << ColorJson(op.color) << "}"
            << (i + 1 < result.list.rects.size() ? ",\n" : "\n");
    }
    out << " ],\n";

    out << " \"texts\": [\n";
    for (size_t i = 0; i < result.list.texts.size(); ++i) {
        const TextOp& op = result.list.texts[i];
        out << "  {\"path\": \"" << JsonEscape(op.path) << "\", \"bounds\": "
            // TouchedRect, not SnapRect: this box is read as "the pixels this
            // run's ink may occupy", and a run's ink may occupy any pixel its
            // measured box overlaps. See surface.h.
            << RectJson(op.bounds) << ", \"pixels\": " << PixelRectJson(TouchedRect(op.bounds))
            << ", \"font_family\": \"" << JsonEscape(op.font_family)
            << "\", \"font_size\": " << TextNumber(op.font_size)
            << ", \"baseline\": " << TextNumber(op.baseline) << ", \"text\": \""
            << JsonEscape(op.text) << "\", \"advances\": [";
        for (size_t j = 0; j < op.advances.size(); ++j) {
            if (j) out << ", ";
            out << TextNumber(op.advances[j]);
        }
        out << "]}" << (i + 1 < result.list.texts.size() ? ",\n" : "\n");
    }
    out << " ],\n";

    out << " \"refusals\": [\n";
    for (size_t i = 0; i < result.list.refusals.size(); ++i) {
        const Refusal& refusal = result.list.refusals[i];
        out << "  {\"path\": \"" << JsonEscape(refusal.path) << "\", \"feature\": \""
            << JsonEscape(refusal.feature) << "\", \"reason\": \"" << JsonEscape(refusal.reason)
            << "\"}" << (i + 1 < result.list.refusals.size() ? ",\n" : "\n");
    }
    out << " ],\n";

    out << " \"text_failures\": [\n";
    for (size_t i = 0; i < result.text_failures.size(); ++i) {
        out << "  \"" << JsonEscape(result.text_failures[i]) << "\""
            << (i + 1 < result.text_failures.size() ? ",\n" : "\n");
    }
    out << " ],\n";

    out << " \"render_issues\": [\n";
    for (size_t i = 0; i < result.render_issues.size(); ++i) {
        const RenderIssue& issue = result.render_issues[i];
        out << "  {\"code\": \"" << RenderIssueName(issue.code) << "\", \"node\": "
            << issue.node.value << ", \"command\": ";
        if (issue.command_index == RenderIssue::kNoCommand)
            out << "null";
        else
            out << issue.command_index;
        out << ", \"message\": \"" << JsonEscape(issue.message) << "\"}"
            << (i + 1 < result.render_issues.size() ? ",\n" : "\n");
    }
    out << " ]\n}\n";
    return out.str();
}

}  // namespace render
}  // namespace openxaml
