// Native Windows.UI.Xaml rendering oracle.
//
// Unlike xaml_probe.cpp, this attaches each authored tree to a real
// DesktopWindowXamlSource.  It records public visual/composition state and the
// exact premultiplied BGRA8 bytes returned by RenderTargetBitmap.  It does not
// intercept D2D, DWrite, D3D or DXGI calls: those are implementation details,
// while this file measures the stable inputs and outputs our renderer must
// reproduce.

// Classic COM interop (`IDesktopWindowXamlSourceNative`) requires IUnknown to
// be declared before C++/WinRT is included.
#include <windows.h>
#include <unknwn.h>
// WinBase's function-like compatibility macro collides with a Timeline method
// in the generated Windows.UI.Xaml projection.
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>

#include <dispatcherqueue.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "json_text.h"

namespace fs = std::filesystem;
using namespace winrt;
using namespace winrt::Windows::UI::Xaml;

namespace {

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::optional<std::string> string_field(const std::string& doc, const std::string& key) {
    const auto token = "\"" + key + "\"";
    auto at = doc.find(token);
    if (at == std::string::npos) return std::nullopt;
    at = doc.find(':', at + token.size());
    if (at == std::string::npos) return std::nullopt;
    const auto open = doc.find('"', at);
    if (open == std::string::npos) return std::nullopt;
    std::string value;
    for (size_t i = open + 1; i < doc.size(); ++i) {
        if (doc[i] == '\\') {
            if (i + 1 >= doc.size()) return std::nullopt;
            value += doc[i];
            value += doc[++i];
        } else if (doc[i] == '"') {
            return openxaml_harness::JsonUnescape(value);
        } else {
            value += doc[i];
        }
    }
    return std::nullopt;
}

std::pair<int32_t, int32_t> render_size(const std::string& doc) {
    auto at = doc.find("\"render_size\"");
    if (at == std::string::npos) throw std::runtime_error("case has no render_size");
    const auto open = doc.find('[', at);
    const auto comma = doc.find(',', open);
    const auto close = doc.find(']', comma);
    if (open == std::string::npos || comma == std::string::npos || close == std::string::npos)
        throw std::runtime_error("render_size is not a two-item array");
    const auto width = std::stoi(doc.substr(open + 1, comma - open - 1));
    const auto height = std::stoi(doc.substr(comma + 1, close - comma - 1));
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        throw std::runtime_error("render_size is outside 1..4096");
    return {width, height};
}

std::string esc(const std::string& text) {
    std::ostringstream out;
    for (const unsigned char ch : text) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(ch) << std::dec;
            } else {
                out << static_cast<char>(ch);
            }
        }
    }
    return out.str();
}

std::string num(double value) {
    if (std::isinf(value)) return "\"Infinity\"";
    if (std::isnan(value)) return "\"NaN\"";
    if (std::abs(value) < 0.00005) value = 0;  // avoid meaningless -0.0000
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << value;
    return out.str();
}

std::string type_name(const winrt::Windows::Foundation::IInspectable& object) {
    return to_string(get_class_name(object));
}

LRESULT CALLBACK host_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CLOSE) {
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

class IslandHost {
public:
    IslandHost() {
        WNDCLASSW klass{};
        klass.lpfnWndProc = host_window_proc;
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = L"OpenTerminalXamlRenderOracle";
        if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("RegisterClassW failed");

        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                klass.lpszClassName, L"XAML render oracle",
                                WS_OVERLAPPEDWINDOW, -30000, -30000, 320, 240,
                                nullptr, nullptr, klass.hInstance, nullptr);
        if (!hwnd_) throw std::runtime_error("CreateWindowExW failed");

        source_ = Hosting::DesktopWindowXamlSource{};
        const auto native = source_.as<IDesktopWindowXamlSourceNative>();
        check_hresult(native->AttachToWindow(hwnd_));
        check_hresult(native->get_WindowHandle(&island_));
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }

    ~IslandHost() noexcept {
        try {
            if (source_) source_.Close();
        } catch (...) {
            // Cleanup cannot safely replace the capture error already in flight.
        }
        if (hwnd_) DestroyWindow(hwnd_);
    }

    void set_content(const UIElement& element, int32_t width, int32_t height) {
        RECT outer{0, 0, width, height};
        if (!AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW, FALSE,
                                WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE))
            throw std::runtime_error("AdjustWindowRectEx failed");
        if (!SetWindowPos(hwnd_, nullptr, -30000, -30000,
                          outer.right - outer.left, outer.bottom - outer.top,
                          SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW) ||
            !SetWindowPos(island_, nullptr, 0, 0, width, height,
                          SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW))
            throw std::runtime_error("SetWindowPos failed");
        RECT client{};
        if (!GetClientRect(hwnd_, &client) || client.right != width || client.bottom != height)
            throw std::runtime_error("host client area does not match render_size");
        source_.Content(element);
        element.Measure({static_cast<float>(width), static_cast<float>(height)});
        element.Arrange({0, 0, static_cast<float>(width), static_cast<float>(height)});
        if (auto framework = element.try_as<FrameworkElement>()) framework.UpdateLayout();
        UpdateWindow(hwnd_);
        pump_messages();
    }

    void clear() {
        source_.Content(nullptr);
        pump_messages();
    }

    UINT dpi() const { return GetDpiForWindow(hwnd_); }

private:
    static void pump_messages() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) throw std::runtime_error("unexpected WM_QUIT");
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    HWND hwnd_{};
    HWND island_{};
    Hosting::DesktopWindowXamlSource source_{nullptr};
};

const char* dpi_awareness_name() {
    switch (GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext())) {
    case DPI_AWARENESS_UNAWARE: return "unaware";
    case DPI_AWARENESS_SYSTEM_AWARE: return "system-aware";
    case DPI_AWARENESS_PER_MONITOR_AWARE: return "per-monitor-aware";
    default: return "invalid";
    }
}

void pump_once() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) throw std::runtime_error("unexpected WM_QUIT");
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
}

template <typename Async>
void wait_for(const Async& operation) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (operation.Status() == winrt::Windows::Foundation::AsyncStatus::Started) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("XAML asynchronous capture timed out");
        pump_once();
    }
    if (operation.Status() == winrt::Windows::Foundation::AsyncStatus::Error)
        throw_hresult(operation.ErrorCode());
    if (operation.Status() == winrt::Windows::Foundation::AsyncStatus::Canceled)
        throw_hresult(E_ABORT);
}

void append_tree(const UIElement& element, const UIElement& root, const std::string& path,
                 int sibling_index, int& tree_order, std::ostringstream& out, bool& first) {
    if (!first) out << ",\n";
    first = false;

    const auto desired = element.DesiredSize();
    double actual_width = 0, actual_height = 0;
    winrt::Windows::Foundation::Rect slot{};
    if (auto framework = element.try_as<FrameworkElement>()) {
        actual_width = framework.ActualWidth();
        actual_height = framework.ActualHeight();
        slot = Controls::Primitives::LayoutInformation::GetLayoutSlot(framework);
    }

    const auto transform = element.TransformToVisual(root);
    const auto origin = transform.TransformPoint({0, 0});
    const auto unit_x = transform.TransformPoint({1, 0});
    const auto unit_y = transform.TransformPoint({0, 1});

    const auto visual = Hosting::ElementCompositionPreview::GetElementVisual(element);
    const auto visual_offset = visual.Offset();
    const auto visual_size = visual.Size();
    const auto visual_scale = visual.Scale();
    const auto visual_center = visual.CenterPoint();
    const auto visual_anchor = visual.AnchorPoint();
    const auto visual_clip = visual.Clip();

    const auto xaml_clip = element.Clip();
    out << "  {\"path\": \"" << esc(path) << "\""
        << ", \"type\": \"" << esc(type_name(element)) << "\""
        << ", \"tree_order\": " << tree_order++
        << ", \"sibling_index\": " << sibling_index
        << ", \"z_index\": " << Controls::Canvas::GetZIndex(element)
        << ", \"desired\": [" << num(desired.Width) << ", " << num(desired.Height) << "]"
        << ", \"actual\": [" << num(actual_width) << ", " << num(actual_height) << "]"
        << ", \"layout_slot\": [" << num(slot.X) << ", " << num(slot.Y) << ", "
                                    << num(slot.Width) << ", " << num(slot.Height) << "]"
        << ", \"transform_to_root\": {\"origin\": [" << num(origin.X) << ", " << num(origin.Y)
        << "], \"unit_x\": [" << num(unit_x.X) << ", " << num(unit_x.Y)
        << "], \"unit_y\": [" << num(unit_y.X) << ", " << num(unit_y.Y) << "]}"
        << ", \"opacity\": " << num(element.Opacity())
        << ", \"visibility\": " << static_cast<int>(element.Visibility())
        << ", \"clip\": ";
    if (xaml_clip) {
        const auto bounds = xaml_clip.Bounds();
        out << "{\"type\": \"" << esc(type_name(xaml_clip)) << "\", \"bounds\": ["
            << num(bounds.X) << ", " << num(bounds.Y) << ", " << num(bounds.Width) << ", "
            << num(bounds.Height) << "]}";
    } else {
        out << "null";
    }
    out << ", \"composition\": {\"offset\": [" << num(visual_offset.x) << ", "
        << num(visual_offset.y) << ", " << num(visual_offset.z) << "]"
        << ", \"size\": [" << num(visual_size.x) << ", " << num(visual_size.y) << "]"
        << ", \"anchor\": [" << num(visual_anchor.x) << ", " << num(visual_anchor.y) << "]"
        << ", \"center\": [" << num(visual_center.x) << ", " << num(visual_center.y) << ", "
        << num(visual_center.z) << "]"
        << ", \"scale\": [" << num(visual_scale.x) << ", " << num(visual_scale.y) << ", "
        << num(visual_scale.z) << "]"
        << ", \"rotation_degrees\": " << num(visual.RotationAngleInDegrees())
        << ", \"opacity\": " << num(visual.Opacity())
        << ", \"is_visible\": " << (visual.IsVisible() ? "true" : "false")
        << ", \"clip_type\": ";
    if (visual_clip) out << "\"" << esc(type_name(visual_clip)) << "\"";
    else out << "null";
    out << "}}";

    int child_index = 0;
    if (auto panel = element.try_as<Controls::Panel>()) {
        for (const auto& child : panel.Children()) {
            append_tree(child, root,
                        path + "/" + type_name(child) + "[" + std::to_string(child_index) + "]",
                        child_index, tree_order, out, first);
            ++child_index;
        }
    } else if (auto border = element.try_as<Controls::Border>()) {
        if (auto child = border.Child())
            append_tree(child, root, path + "/" + type_name(child) + "[0]", 0,
                        tree_order, out, first);
    } else if (auto content = element.try_as<Controls::ContentControl>()) {
        if (auto child = content.Content().try_as<UIElement>())
            append_tree(child, root, path + "/" + type_name(child) + "[0]", 0,
                        tree_order, out, first);
    } else if (auto presenter = element.try_as<Controls::ContentPresenter>()) {
        if (auto child = presenter.Content().try_as<UIElement>())
            append_tree(child, root, path + "/" + type_name(child) + "[0]", 0,
                        tree_order, out, first);
    }
}

std::vector<uint8_t> capture(const UIElement& element, int32_t width, int32_t height,
                             int32_t& actual_width, int32_t& actual_height) {
    Media::Imaging::RenderTargetBitmap bitmap;
    auto render = bitmap.RenderAsync(element, width, height);
    wait_for(render);
    render.GetResults();
    actual_width = bitmap.PixelWidth();
    actual_height = bitmap.PixelHeight();
    if (actual_width != width || actual_height != height)
        throw std::runtime_error("RenderTargetBitmap returned unexpected dimensions");

    auto get_pixels = bitmap.GetPixelsAsync();
    wait_for(get_pixels);
    const auto buffer = get_pixels.GetResults();
    std::vector<uint8_t> pixels(buffer.Length());
    winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer).ReadBytes(
        array_view<uint8_t>(pixels));
    const auto expected = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    if (pixels.size() != expected)
        throw std::runtime_error("RenderTargetBitmap returned an unexpected byte count");
    return pixels;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::wcerr << L"usage: xaml_render_probe <render-cases-dir> <out-dir>\n";
        return 2;
    }
    const fs::path cases = argv[1];
    const fs::path outdir = argv[2];

    com_ptr<::IUnknown> queue_controller;
    Hosting::WindowsXamlManager manager{nullptr};
    try {
        init_apartment(apartment_type::single_threaded);
        DispatcherQueueOptions options{sizeof(DispatcherQueueOptions),
                                       DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
        check_hresult(CreateDispatcherQueueController(
            options, reinterpret_cast<PDISPATCHERQUEUECONTROLLER*>(queue_controller.put())));
        manager = Hosting::WindowsXamlManager::InitializeForCurrentThread();
    } catch (const hresult_error& error) {
        std::cerr << "cannot start XAML: " << to_string(error.message()) << "\n";
        return 3;
    }

    fs::create_directories(outdir);
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(cases))
        if (entry.path().extension() == L".json") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::cerr << "no render cases found\n";
        return 4;
    }

    int completed = 0;
    try {
        IslandHost host;
        for (const auto& file : files) {
            const auto doc = slurp(file);
            const auto id = string_field(doc, "id").value_or(file.stem().string());
            const auto markup = string_field(doc, "markup");
            if (!markup) throw std::runtime_error(id + ": case has no markup");
            const auto [width, height] = render_size(doc);

            const auto root = Markup::XamlReader::Load(to_hstring(*markup)).as<UIElement>();
            host.set_content(root, width, height);

            int32_t pixel_width = 0, pixel_height = 0;
            const auto pixels = capture(root, width, height, pixel_width, pixel_height);
            const auto pixel_name = id + ".bgra";
            std::ofstream pixel_file(outdir / pixel_name, std::ios::binary);
            pixel_file.write(reinterpret_cast<const char*>(pixels.data()),
                             static_cast<std::streamsize>(pixels.size()));
            if (!pixel_file) throw std::runtime_error(id + ": could not write pixel capture");

            double rasterization_scale = 0;
            if (const auto xaml_root = root.XamlRoot())
                rasterization_scale = xaml_root.RasterizationScale();
            std::ostringstream tree;
            bool first = true;
            int tree_order = 0;
            append_tree(root, root, "/" + type_name(root), 0, tree_order, tree, first);

            std::ostringstream observation;
            observation << "{\n"
                << " \"schema_version\": 1,\n"
                << " \"case_id\": \"" << esc(id) << "\",\n"
                << " \"environment\": {\"dpi_awareness\": \""
                << dpi_awareness_name() << "\", \"window_dpi\": " << host.dpi()
                << ", \"rasterization_scale\": " << num(rasterization_scale) << "},\n"
                << " \"capture\": {\"method\": "
                   "\"Windows.UI.Xaml.Media.Imaging.RenderTargetBitmap\", "
                   "\"width\": " << pixel_width << ", \"height\": " << pixel_height
                << ", \"stride\": " << pixel_width * 4
                << ", \"pixel_format\": \"BGRA8\", "
                   "\"alpha_mode\": \"premultiplied\", "
                   "\"pixels_file\": \"" << esc(pixel_name) << "\"},\n"
                << " \"tree\": [\n" << tree.str() << "\n ]\n}\n";
            std::ofstream observation_file(outdir / (id + ".json"), std::ios::binary);
            observation_file << observation.str();
            if (!observation_file)
                throw std::runtime_error(id + ": could not write observation JSON");
            host.clear();
            ++completed;
            std::cout << id << ": " << pixel_width << "x" << pixel_height
                      << ", " << tree_order << " nodes\n";
        }
    } catch (const hresult_error& error) {
        std::cerr << "render probe failed: " << to_string(error.message())
                  << " (hresult 0x" << std::hex << static_cast<uint32_t>(error.code()) << ")\n";
        return 5;
    } catch (const std::exception& error) {
        std::cerr << "render probe failed: " << error.what() << "\n";
        return 5;
    }

    std::cout << completed << " render cases captured\n";
    return completed == static_cast<int>(files.size()) ? 0 : 6;
}
