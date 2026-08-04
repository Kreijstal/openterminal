// The same corpus, measured through the WinRT ABI instead of by calling the
// layout core directly.
//
// Every object here is created by RoActivateInstance, every property is set
// through an interface method, and every number is read back the same way.
// Nothing in this program touches the layout classes: it shares only the
// markup parser, which decides what a case *says*, not what it measures.
//
// Output is identical in shape to phase3/harness/xaml_probe.exe and to
// phase3/layout's measure_cases, so all three compare with check_layout.py.
// Agreement between this and measure_cases means the ABI surface is wired
// correctly; agreement with the probe means the layout is right.

#include "sdk.h"

#include <roapi.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "json.h"
#include "markup.h"
#include "markup_tree.h"
#include "openxaml_iids.h"

namespace fs = std::filesystem;
namespace wf = ABI::Windows::Foundation;
namespace wux = ABI::Windows::UI::Xaml;
namespace wuxc = ABI::Windows::UI::Xaml::Controls;
namespace wuxcp = ABI::Windows::UI::Xaml::Controls::Primitives;
namespace wuxm = ABI::Windows::UI::Xaml::Media;

using namespace openxaml;

namespace {

class ComError : public std::runtime_error {
public:
    ComError(const std::string& what, HRESULT hr)
        : std::runtime_error(what + " (hresult 0x" + Hex(hr) + ")") {}

private:
    static std::string Hex(HRESULT hr) {
        std::ostringstream out;
        out << std::hex << static_cast<unsigned long>(hr);
        return out.str();
    }
};

void Check(HRESULT hr, const std::string& what) {
    if (FAILED(hr)) throw ComError(what, hr);
}

// A minimal owning pointer. Enough to keep the tree alive and nothing more --
// the point of this program is the ABI calls, not the smart pointer.
template <class T>
class Ref {
public:
    Ref() = default;
    explicit Ref(T* raw) : raw_(raw) {}
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    Ref(Ref&& other) noexcept : raw_(other.raw_) { other.raw_ = nullptr; }
    Ref& operator=(Ref&& other) noexcept {
        if (this != &other) {
            if (raw_) raw_->Release();
            raw_ = other.raw_;
            other.raw_ = nullptr;
        }
        return *this;
    }
    ~Ref() {
        if (raw_) raw_->Release();
    }

    T* get() const { return raw_; }
    T* operator->() const { return raw_; }
    T** put() { return &raw_; }
    explicit operator bool() const { return raw_ != nullptr; }

private:
    T* raw_ = nullptr;
};

template <class T>
Ref<T> QueryFor(::IUnknown* source, REFIID iid, const std::string& what) {
    Ref<T> result;
    Check(source->QueryInterface(iid, reinterpret_cast<void**>(result.put())),
          "QueryInterface for " + what);
    return result;
}

class HString {
public:
    explicit HString(const wchar_t* text) {
        Check(WindowsCreateString(text, static_cast<UINT32>(wcslen(text)), &handle_),
              "WindowsCreateString");
    }
    // The corpus text is ASCII, so this widens rather than decoding UTF-8. A
    // case with a non-ASCII character would need real conversion, and would
    // fail loudly at the font metrics rather than measure something wrong.
    explicit HString(const std::string& text) {
        std::wstring wide;
        for (unsigned char unit : text) {
            if (unit > 0x7F)
                throw std::runtime_error("the corpus text is not ASCII: cannot widen it here");
            wide += static_cast<wchar_t>(unit);
        }
        Check(WindowsCreateString(wide.c_str(), static_cast<UINT32>(wide.size()), &handle_),
              "WindowsCreateString");
    }
    ~HString() { WindowsDeleteString(handle_); }
    HString(const HString&) = delete;
    HString& operator=(const HString&) = delete;
    HSTRING get() const { return handle_; }

private:
    HSTRING handle_ = nullptr;
};

std::string Narrow(HSTRING text) {
    UINT32 length = 0;
    const wchar_t* buffer = WindowsGetStringRawBuffer(text, &length);
    std::string out;
    out.reserve(length);
    // Runtime class names are ASCII, so this is a straight narrowing rather
    // than a real conversion.
    for (UINT32 i = 0; i < length; ++i) out += static_cast<char>(buffer[i]);
    return out;
}

Ref<IInspectable> Activate(const wchar_t* class_name) {
    const HString name(class_name);
    Ref<IInspectable> instance;
    Check(RoActivateInstance(name.get(), instance.put()),
          "RoActivateInstance(" + Narrow(name.get()) + ")");
    return instance;
}

// The statics live on the activation factory, which is where WinRT puts them.
template <class T>
Ref<T> GetStatics(const wchar_t* class_name, REFIID iid) {
    const HString name(class_name);
    Ref<T> statics;
    Check(RoGetActivationFactory(name.get(), iid, reinterpret_cast<void**>(statics.put())),
          "RoGetActivationFactory(" + Narrow(name.get()) + ")");
    return statics;
}

// --- building a tree through the ABI ------------------------------------------

struct Statics {
    Ref<wuxc::IGridStatics> grid;
    Ref<wuxcp::ILayoutInformationStatics> layout_information;
};

wux::Thickness ToAbi(const Thickness& value) {
    return {value.left, value.top, value.right, value.bottom};
}

wux::GridLength ToAbi(const GridLength& value) {
    wux::GridLength length{};
    length.Value = value.value;
    switch (value.type) {
        case GridUnitType::Auto: length.GridUnitType = wux::GridUnitType_Auto; break;
        case GridUnitType::Pixel: length.GridUnitType = wux::GridUnitType_Pixel; break;
        case GridUnitType::Star: length.GridUnitType = wux::GridUnitType_Star; break;
    }
    return length;
}

wux::HorizontalAlignment ToAbi(HorizontalAlignment value) {
    switch (value) {
        case HorizontalAlignment::Left: return wux::HorizontalAlignment_Left;
        case HorizontalAlignment::Center: return wux::HorizontalAlignment_Center;
        case HorizontalAlignment::Right: return wux::HorizontalAlignment_Right;
        default: return wux::HorizontalAlignment_Stretch;
    }
}

wux::VerticalAlignment ToAbi(VerticalAlignment value) {
    switch (value) {
        case VerticalAlignment::Top: return wux::VerticalAlignment_Top;
        case VerticalAlignment::Center: return wux::VerticalAlignment_Center;
        case VerticalAlignment::Bottom: return wux::VerticalAlignment_Bottom;
        default: return wux::VerticalAlignment_Stretch;
    }
}

const wchar_t* ClassNameFor(const std::string& type) {
    if (type == "Border") return L"Windows.UI.Xaml.Controls.Border";
    if (type == "Grid") return L"Windows.UI.Xaml.Controls.Grid";
    if (type == "StackPanel") return L"Windows.UI.Xaml.Controls.StackPanel";
    if (type == "TextBlock") return L"Windows.UI.Xaml.Controls.TextBlock";
    throw MarkupError("the type '" + type + "' is not implemented");
}

void AddDefinitions(const std::vector<MarkupDefinition>& definitions, bool is_column,
                    wuxc::IGrid* grid) {
    if (definitions.empty()) return;
    if (is_column) {
        Ref<__FIVector_1_Windows__CUI__CXaml__CControls__CColumnDefinition> collection;
        Check(grid->get_ColumnDefinitions(collection.put()), "get_ColumnDefinitions");
        for (const MarkupDefinition& source : definitions) {
            Ref<IInspectable> instance = Activate(L"Windows.UI.Xaml.Controls.ColumnDefinition");
            auto definition = QueryFor<wuxc::IColumnDefinition>(
                instance.get(), openxaml::iid::Windows_UI_Xaml_Controls_IColumnDefinition,
                "IColumnDefinition");
            Check(definition->put_Width(ToAbi(source.size)), "put_Width");
            Check(definition->put_MinWidth(source.min_size), "put_MinWidth");
            Check(definition->put_MaxWidth(source.max_size), "put_MaxWidth");
            Check(collection->Append(definition.get()), "ColumnDefinitions.Append");
        }
        return;
    }
    Ref<__FIVector_1_Windows__CUI__CXaml__CControls__CRowDefinition> collection;
    Check(grid->get_RowDefinitions(collection.put()), "get_RowDefinitions");
    for (const MarkupDefinition& source : definitions) {
        Ref<IInspectable> instance = Activate(L"Windows.UI.Xaml.Controls.RowDefinition");
        auto definition = QueryFor<wuxc::IRowDefinition>(
            instance.get(), openxaml::iid::Windows_UI_Xaml_Controls_IRowDefinition,
            "IRowDefinition");
        Check(definition->put_Height(ToAbi(source.size)), "put_Height");
        Check(definition->put_MinHeight(source.min_size), "put_MinHeight");
        Check(definition->put_MaxHeight(source.max_size), "put_MaxHeight");
        Check(collection->Append(definition.get()), "RowDefinitions.Append");
    }
}

Ref<wux::IUIElement> Build(const MarkupNode& node, const Statics& statics) {
    Ref<IInspectable> instance = Activate(ClassNameFor(node.type));
    auto element = QueryFor<wux::IUIElement>(
        instance.get(), openxaml::iid::Windows_UI_Xaml_IUIElement, "IUIElement");
    auto framework = QueryFor<wux::IFrameworkElement>(
        instance.get(), openxaml::iid::Windows_UI_Xaml_IFrameworkElement, "IFrameworkElement");

    Check(framework->put_Width(node.width), "put_Width");
    Check(framework->put_Height(node.height), "put_Height");
    Check(framework->put_MinWidth(node.min_width), "put_MinWidth");
    Check(framework->put_MaxWidth(node.max_width), "put_MaxWidth");
    Check(framework->put_MinHeight(node.min_height), "put_MinHeight");
    Check(framework->put_MaxHeight(node.max_height), "put_MaxHeight");
    Check(framework->put_Margin(ToAbi(node.margin)), "put_Margin");
    Check(framework->put_HorizontalAlignment(ToAbi(node.horizontal_alignment)),
          "put_HorizontalAlignment");
    Check(framework->put_VerticalAlignment(ToAbi(node.vertical_alignment)),
          "put_VerticalAlignment");

    // Attached properties go through the Grid statics, exactly as a caller
    // that had only the public surface would have to do it.
    Check(statics.grid->SetRow(framework.get(), node.grid_row), "Grid.SetRow");
    Check(statics.grid->SetColumn(framework.get(), node.grid_column), "Grid.SetColumn");
    Check(statics.grid->SetRowSpan(framework.get(), node.grid_row_span), "Grid.SetRowSpan");
    Check(statics.grid->SetColumnSpan(framework.get(), node.grid_column_span),
          "Grid.SetColumnSpan");

    if (node.type == "Border") {
        auto border = QueryFor<wuxc::IBorder>(
            instance.get(), openxaml::iid::Windows_UI_Xaml_Controls_IBorder, "IBorder");
        Check(border->put_BorderThickness(ToAbi(node.border_thickness)), "put_BorderThickness");
        Check(border->put_Padding(ToAbi(node.padding)), "put_Padding");
        if (!node.children.empty()) {
            Ref<wux::IUIElement> child = Build(node.children.front(), statics);
            Check(border->put_Child(child.get()), "put_Child");
        }
        return element;
    }

    if (node.type == "TextBlock") {
        auto block = QueryFor<wuxc::ITextBlock>(
            instance.get(), openxaml::iid::Windows_UI_Xaml_Controls_ITextBlock, "ITextBlock");

        // FontFamily is an object, not a string, so it is constructed through
        // its own activation factory -- which is the only way a caller with
        // just the public surface could do it either.
        auto font_factory = GetStatics<wuxm::IFontFamilyFactory>(
            L"Windows.UI.Xaml.Media.FontFamily",
            openxaml::iid::Windows_UI_Xaml_Media_IFontFamilyFactory);
        const HString family_name(node.font_family);
        Ref<wuxm::IFontFamily> family;
        Ref<IInspectable> inner;
        Check(font_factory->CreateInstanceWithName(family_name.get(), nullptr, inner.put(),
                                                   family.put()),
              "FontFamily.CreateInstanceWithName");

        const HString text(node.text);
        Check(block->put_Text(text.get()), "put_Text");
        Check(block->put_FontFamily(family.get()), "put_FontFamily");
        Check(block->put_FontSize(node.font_size), "put_FontSize");
        Check(block->put_TextWrapping(node.text_wrapping == TextWrapping::Wrap
                                          ? wux::TextWrapping_Wrap
                                          : wux::TextWrapping_NoWrap),
              "put_TextWrapping");
        return element;
    }

    if (node.type == "Grid") {
        auto grid = QueryFor<wuxc::IGrid>(
            instance.get(), openxaml::iid::Windows_UI_Xaml_Controls_IGrid, "IGrid");
        AddDefinitions(node.column_definitions, true, grid.get());
        AddDefinitions(node.row_definitions, false, grid.get());
    } else if (node.type == "StackPanel") {
        auto stack = QueryFor<wuxc::IStackPanel>(
            instance.get(), openxaml::iid::Windows_UI_Xaml_Controls_IStackPanel, "IStackPanel");
        Check(stack->put_Orientation(node.orientation == Orientation::Horizontal
                                         ? wuxc::Orientation_Horizontal
                                         : wuxc::Orientation_Vertical),
              "put_Orientation");
    }

    auto panel = QueryFor<wuxc::IPanel>(
        instance.get(), openxaml::iid::Windows_UI_Xaml_Controls_IPanel, "IPanel");
    Ref<__FIVector_1_Windows__CUI__CXaml__CUIElement> children;
    Check(panel->get_Children(children.put()), "get_Children");
    for (const MarkupNode& child_node : node.children) {
        Ref<wux::IUIElement> child = Build(child_node, statics);
        Check(children->Append(child.get()), "Children.Append");
    }
    return element;
}

// --- reading the result back through the ABI ----------------------------------

std::string Number(double value) {
    if (std::isinf(value)) return "\"Infinity\"";
    if (std::isnan(value)) return "\"NaN\"";
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << value;
    return out.str();
}

std::string TypeNameOf(IInspectable* object) {
    HSTRING name = nullptr;
    Check(object->GetRuntimeClassName(&name), "GetRuntimeClassName");
    const std::string result = Narrow(name);
    WindowsDeleteString(name);
    return result;
}

void Walk(wux::IUIElement* element, const std::string& path, const Statics& statics,
          std::vector<std::string>& out) {
    wf::Size desired{};
    Check(element->get_DesiredSize(&desired), "get_DesiredSize");

    auto framework = QueryFor<wux::IFrameworkElement>(
        element, openxaml::iid::Windows_UI_Xaml_IFrameworkElement, "IFrameworkElement");
    DOUBLE actual_width = 0;
    DOUBLE actual_height = 0;
    Check(framework->get_ActualWidth(&actual_width), "get_ActualWidth");
    Check(framework->get_ActualHeight(&actual_height), "get_ActualHeight");

    wf::Rect slot{};
    Check(statics.layout_information->GetLayoutSlot(framework.get(), &slot), "GetLayoutSlot");

    std::ostringstream line;
    line << "  {\"path\": \"" << JsonEscape(path) << "\""
         << ", \"type\": \"" << JsonEscape(TypeNameOf(element)) << "\""
         << ", \"desired\": [" << Number(desired.Width) << ", " << Number(desired.Height) << "]"
         << ", \"actual\": [" << Number(actual_width) << ", " << Number(actual_height) << "]"
         << ", \"offset\": [" << Number(slot.X) << ", " << Number(slot.Y) << "]}";
    out.push_back(line.str());

    // Children come back through the ABI too, rather than from the markup, so
    // that a collection which accepted a child but did not store it shows up.
    std::vector<Ref<wux::IUIElement>> children;
    Ref<wuxc::IBorder> border;
    if (SUCCEEDED(element->QueryInterface(openxaml::iid::Windows_UI_Xaml_Controls_IBorder,
                                          reinterpret_cast<void**>(border.put())))) {
        Ref<wux::IUIElement> child;
        Check(border->get_Child(child.put()), "get_Child");
        if (child) children.push_back(std::move(child));
    } else {
        // Anything that is neither a Border nor a Panel is a leaf -- a
        // TextBlock holds text, not elements. Asked for rather than assumed,
        // so an element that should be a Panel and is not still enumerates as
        // empty here and fails where it is built, which is where IPanel is a
        // hard requirement.
        Ref<wuxc::IPanel> panel;
        if (SUCCEEDED(element->QueryInterface(openxaml::iid::Windows_UI_Xaml_Controls_IPanel,
                                              reinterpret_cast<void**>(panel.put())))) {
            Ref<__FIVector_1_Windows__CUI__CXaml__CUIElement> collection;
            Check(panel->get_Children(collection.put()), "get_Children");
            unsigned count = 0;
            Check(collection->get_Size(&count), "Children.Size");
            for (unsigned i = 0; i < count; ++i) {
                Ref<wux::IUIElement> child;
                Check(collection->GetAt(i, child.put()), "Children.GetAt");
                children.push_back(std::move(child));
            }
        }
    }

    int index = 0;
    for (const Ref<wux::IUIElement>& child : children) {
        Walk(child.get(), path + "/" + TypeNameOf(child.get()) + "[" + std::to_string(index++) + "]",
             statics, out);
    }
}

std::string Slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

double ReadExtent(const JsonValue& value) {
    if (value.kind == JsonValue::Kind::String) {
        if (value.string == "Infinity") return kInfinity;
        throw JsonError("unexpected available_size value \"" + value.string + "\"");
    }
    if (value.kind != JsonValue::Kind::Number) throw JsonError("available_size is not a number");
    return value.number;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: measure_cases_winrt <cases-dir> <out-dir>\n";
        return 2;
    }
    const fs::path cases = argv[1];
    const fs::path out_dir = argv[2];

    if (!fs::exists(cases)) {
        std::cerr << "no such directory: " << cases.string() << "\n";
        return 4;
    }

    Check(RoInitialize(RO_INIT_SINGLETHREADED), "RoInitialize");

    Statics statics;
    try {
        statics.grid = GetStatics<wuxc::IGridStatics>(
            L"Windows.UI.Xaml.Controls.Grid",
            openxaml::iid::Windows_UI_Xaml_Controls_IGridStatics);
        statics.layout_information = GetStatics<wuxcp::ILayoutInformationStatics>(
            L"Windows.UI.Xaml.Controls.Primitives.LayoutInformation",
            openxaml::iid::Windows_UI_Xaml_Controls_Primitives_ILayoutInformationStatics);
    } catch (const std::exception& e) {
        // Without the statics nothing can be measured, and the reason is
        // almost always registration rather than layout -- so say it once here
        // instead of once per case.
        std::cerr << "cannot reach the XAML statics: " << e.what() << "\n";
        return 3;
    }

    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(cases)) {
        if (entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::cerr << "no cases found under " << cases.string() << "\n";
        return 4;
    }

    fs::create_directories(out_dir);
    int measured = 0;
    int failed = 0;

    for (const fs::path& file : files) {
        std::string id = file.stem().string();
        std::string error;
        std::vector<std::string> tree;

        try {
            const JsonValue document = ParseJson(Slurp(file));
            id = document.At("id").string;
            const JsonValue& extent = document.At("environment").At("available_size");
            if (extent.array.size() != 2) throw JsonError("available_size needs two entries");
            const double available_width = ReadExtent(extent.array[0]);
            const double available_height = ReadExtent(extent.array[1]);

            const MarkupNode node = ParseMarkup(document.At("markup").string);
            Ref<wux::IUIElement> root = Build(node, statics);

            Check(root->Measure({static_cast<FLOAT>(available_width),
                                 static_cast<FLOAT>(available_height)}),
                  "Measure");
            wf::Size desired{};
            Check(root->get_DesiredSize(&desired), "get_DesiredSize");
            // An infinite final rect is not a legal arrange input, so an
            // unbounded axis falls back to what the element asked for -- the
            // same fallback the oracle probe applies.
            Check(root->Arrange({0.0f, 0.0f,
                                 std::isinf(available_width) ? desired.Width
                                                             : static_cast<FLOAT>(available_width),
                                 std::isinf(available_height)
                                     ? desired.Height
                                     : static_cast<FLOAT>(available_height)}),
                  "Arrange");
            Walk(root.get(), "/" + TypeNameOf(root.get()), statics, tree);
        } catch (const std::exception& e) {
            error = e.what();
        }

        std::ostringstream out;
        out << "{\n \"schema_version\": 1,\n \"case_id\": \"" << JsonEscape(id) << "\",\n";
        if (!error.empty()) {
            out << " \"error\": \"" << JsonEscape(error) << "\"\n}\n";
            ++failed;
        } else {
            out << " \"tree\": [\n";
            for (size_t i = 0; i < tree.size(); ++i) {
                out << tree[i] << (i + 1 < tree.size() ? ",\n" : "\n");
            }
            out << " ]\n}\n";
            ++measured;
        }
        std::ofstream(out_dir / (id + ".json"), std::ios::binary) << out.str();
    }

    std::cout << measured << " measured, " << failed << " failed\n";
    return 0;
}
