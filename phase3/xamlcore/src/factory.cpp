// Activation factories and the DLL's one entry point.
//
// Wine's RoGetActivationFactory resolves a class name to a DLL through
// HKLM\Software\Microsoft\WindowsRuntime\ActivatableClassId, loads it, and
// calls DllGetActivationFactory. Everything below that point is ours.

#include "sdk.h"

#include <cstring>

#include "elements.h"
#include "fonts.h"
#include "helpers.h"

namespace openxaml::winrt {
namespace {

// A factory that answers ActivateInstance with a new T, and optionally a
// statics interface as well -- WinRT puts a class's static members on its
// activation factory rather than on a separate object.
template <class T>
class Factory : public ComObject, public IActivationFactory {
public:
    explicit Factory(const wchar_t* name) : name_(name) {}

    const wchar_t* RuntimeClassName() const override { return name_; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        const HRESULT hr = QueryStatics(iid, object);
        if (hr != E_NOINTERFACE) return hr;
        OPENXAML_QI_ARM(::openxaml::iid::IActivationFactory, IActivationFactory)
        OPENXAML_QI_ARM(IID_IUnknown, IActivationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IActivationFactory)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** instance) override {
        if (!instance) return E_POINTER;
        *instance = static_cast<typename T::PrimaryInterface*>(new T());
        return S_OK;
    }

protected:
    // Nothing by default. A class with static members overrides this.
    virtual HRESULT QueryStatics(REFIID, void**) { return E_NOINTERFACE; }

private:
    const wchar_t* name_;
};

// Grid's attached properties. Row, Column, RowSpan and ColumnSpan live on any
// element, not just a Grid's children, so they are stored on the element and
// read by whichever Grid happens to be the parent.
//
// The `*Property` getters stay E_NOTIMPL. There is a property system behind
// this now -- Get and Set go through it -- but its DependencyProperty is a
// plain C++ object and the ABI wants a Windows.UI.Xaml.DependencyProperty,
// which is a runtime class this DLL does not project.
class GridFactory : public Factory<GridObject>, public abi::NotImpl_IGridStatics {
public:
    GridFactory() : Factory(L"Windows.UI.Xaml.Controls.Grid") {}

    HRESULT STDMETHODCALLTYPE GetRow(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                     INT32* value) override {
        return Read(element, value, openxaml::Grid::RowProperty());
    }
    HRESULT STDMETHODCALLTYPE SetRow(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                     INT32 value) override {
        return Write(element, value, openxaml::Grid::RowProperty());
    }
    HRESULT STDMETHODCALLTYPE GetColumn(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                        INT32* value) override {
        return Read(element, value, openxaml::Grid::ColumnProperty());
    }
    HRESULT STDMETHODCALLTYPE SetColumn(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                        INT32 value) override {
        return Write(element, value, openxaml::Grid::ColumnProperty());
    }
    HRESULT STDMETHODCALLTYPE GetRowSpan(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                         INT32* value) override {
        return Read(element, value, openxaml::Grid::RowSpanProperty());
    }
    HRESULT STDMETHODCALLTYPE SetRowSpan(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                         INT32 value) override {
        return Write(element, value, openxaml::Grid::RowSpanProperty());
    }
    HRESULT STDMETHODCALLTYPE GetColumnSpan(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                            INT32* value) override {
        return Read(element, value, openxaml::Grid::ColumnSpanProperty());
    }
    HRESULT STDMETHODCALLTYPE SetColumnSpan(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                            INT32 value) override {
        return Write(element, value, openxaml::Grid::ColumnSpanProperty());
    }

    // IGridStatics arrives through a base of its own, so its copies of the
    // IInspectable methods are still pure here and need defining again.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        return Factory<GridObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

protected:
    HRESULT QueryStatics(REFIID iid, void** object) override {
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_IGridStatics,
                        ABI::Windows::UI::Xaml::Controls::IGridStatics)
        return E_NOINTERFACE;
    }

private:
    using Attached = const openxaml::DependencyProperty&;

    static openxaml::Element* Unwrap(ABI::Windows::UI::Xaml::IFrameworkElement* element) {
        if (!element) return nullptr;
        IOpenXamlNative* native = nullptr;
        if (FAILED(element->QueryInterface(IID_IOpenXamlNative,
                                           reinterpret_cast<void**>(&native)))) {
            return nullptr;
        }
        openxaml::Element* layout = native->LayoutElement();
        native->Release();
        return layout;
    }

    static HRESULT Read(ABI::Windows::UI::Xaml::IFrameworkElement* element, INT32* value,
                        Attached property) {
        if (!value) return E_POINTER;
        openxaml::Element* layout = Unwrap(element);
        if (!layout) return E_INVALIDARG;
        *value = layout->GetInt(property);
        return S_OK;
    }

    static HRESULT Write(ABI::Windows::UI::Xaml::IFrameworkElement* element, INT32 value,
                         Attached property) {
        openxaml::Element* layout = Unwrap(element);
        if (!layout) return E_INVALIDARG;
        layout->SetValue(property, value);
        return S_OK;
    }
};

// LayoutInformation is static-only: it has no constructor, so activating it
// fails and only the statics interface is reachable.
class LayoutInformationFactory : public ComObject,
                                 public IActivationFactory,
                                 public abi::NotImpl_ILayoutInformationStatics {
public:
    const wchar_t* RuntimeClassName() const override {
        return L"Windows.UI.Xaml.Controls.Primitives.LayoutInformation";
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Controls_Primitives_ILayoutInformationStatics,
                        ABI::Windows::UI::Xaml::Controls::Primitives::ILayoutInformationStatics)
        OPENXAML_QI_ARM(::openxaml::iid::IActivationFactory, IActivationFactory)
        OPENXAML_QI_ARM(IID_IUnknown, IActivationFactory)
        OPENXAML_QI_ARM(::openxaml::iid::IInspectable, IActivationFactory)
        *object = nullptr;
        return E_NOINTERFACE;
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable**) override {
        return E_NOTIMPL;
    }

    // The rect the parent arranged this element into -- not where it ended up
    // rendering. Alignment moves an element inside its slot without moving the
    // slot, which is why the corpus can record this without an aligned offset.
    HRESULT STDMETHODCALLTYPE GetLayoutSlot(ABI::Windows::UI::Xaml::IFrameworkElement* element,
                                            ABI::Windows::Foundation::Rect* slot) override {
        if (!slot) return E_POINTER;
        if (!element) return E_INVALIDARG;
        IOpenXamlNative* native = nullptr;
        if (FAILED(element->QueryInterface(IID_IOpenXamlNative,
                                           reinterpret_cast<void**>(&native)))) {
            return E_INVALIDARG;
        }
        const openxaml::Rect rect = native->LayoutElement()->layout_slot();
        native->Release();
        *slot = {static_cast<FLOAT>(rect.x), static_cast<FLOAT>(rect.y),
                 static_cast<FLOAT>(rect.width), static_cast<FLOAT>(rect.height)};
        return S_OK;
    }
};

}  // namespace
// FontFamily is constructed with its name rather than default-constructed and
// filled in, so its factory carries IFontFamilyFactory alongside
// IActivationFactory -- the same shape as Grid's statics.
class FontFamilyFactory final : public Factory<FontFamilyObject>,
                                public abi::NotImpl_IFontFamilyFactory {
public:
    FontFamilyFactory() : Factory<FontFamilyObject>(L"Windows.UI.Xaml.Media.FontFamily") {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        OPENXAML_QI_ARM(::openxaml::iid::Windows_UI_Xaml_Media_IFontFamilyFactory,
                        wuxm::IFontFamilyFactory)
        return Factory<FontFamilyObject>::QueryInterface(iid, object);
    }
    OPENXAML_COM_BOILERPLATE()

    HRESULT STDMETHODCALLTYPE CreateInstanceWithName(HSTRING name, IInspectable* outer,
                                                     IInspectable** inner,
                                                     wuxm::IFontFamily** instance) override {
        if (!instance) return E_POINTER;
        // Aggregation is what `outer` is for, and nothing here supports being
        // aggregated. Saying so beats ignoring the argument.
        if (outer) return E_NOTIMPL;
        if (inner) *inner = nullptr;

        auto* family = new FontFamilyObject();
        family->source = Utf8FromHString(name);
        family->AddRef();
        *instance = family;
        return S_OK;
    }
};

}  // namespace openxaml::winrt

using namespace openxaml::winrt;

namespace {

Factory<BorderObject>& BorderFactory() {
    static Factory<BorderObject> factory(L"Windows.UI.Xaml.Controls.Border");
    return factory;
}
Factory<StackPanelObject>& StackPanelFactory() {
    static Factory<StackPanelObject> factory(L"Windows.UI.Xaml.Controls.StackPanel");
    return factory;
}
Factory<CanvasObject>& CanvasFactory() {
    static Factory<CanvasObject> factory(L"Windows.UI.Xaml.Controls.Canvas"); return factory;
}
Factory<ContentPresenterObject>& ContentPresenterFactory() {
    static Factory<ContentPresenterObject> factory(L"Windows.UI.Xaml.Controls.ContentPresenter"); return factory;
}
Factory<ImageObject>& ImageFactory() {
    static Factory<ImageObject> factory(L"Windows.UI.Xaml.Controls.Image"); return factory;
}
Factory<PathObject>& PathFactory() {
    static Factory<PathObject> factory(L"Windows.UI.Xaml.Shapes.Path"); return factory;
}
Factory<PathIconObject>& PathIconFactory() {
    static Factory<PathIconObject> factory(L"Windows.UI.Xaml.Controls.PathIcon"); return factory;
}
Factory<TextBlockObject>& TextBlockFactory() {
    static Factory<TextBlockObject> factory(L"Windows.UI.Xaml.Controls.TextBlock");
    return factory;
}
Factory<ContentControlObject>& ContentControlFactory() {
    static Factory<ContentControlObject> factory(L"Windows.UI.Xaml.Controls.ContentControl");
    return factory;
}
Factory<PageObject>& PageFactory() {
    static Factory<PageObject> factory(L"Windows.UI.Xaml.Controls.Page");
    return factory;
}
Factory<FrameObject>& FrameFactory() {
    static Factory<FrameObject> factory(L"Windows.UI.Xaml.Controls.Frame");
    return factory;
}
Factory<ItemsControlObject>& ItemsControlFactory() {
    static Factory<ItemsControlObject> factory(L"Windows.UI.Xaml.Controls.ItemsControl");
    return factory;
}
Factory<ListViewObject>& ListViewFactory() {
    static Factory<ListViewObject> factory(L"Windows.UI.Xaml.Controls.ListView");
    return factory;
}
Factory<PopupObject>& PopupFactory() {
    static Factory<PopupObject> factory(L"Windows.UI.Xaml.Controls.Primitives.Popup");
    return factory;
}
Factory<ButtonObject>& ButtonFactory() {
    static Factory<ButtonObject> factory(L"Windows.UI.Xaml.Controls.Button"); return factory;
}
Factory<TextBoxObject>& TextBoxFactory() {
    static Factory<TextBoxObject> factory(L"Windows.UI.Xaml.Controls.TextBox"); return factory;
}
Factory<ToolTipObject>& ToolTipFactory() {
    static Factory<ToolTipObject> factory(L"Windows.UI.Xaml.Controls.ToolTip"); return factory;
}
Factory<ThumbObject>& ThumbFactory() {
    static Factory<ThumbObject> factory(L"Windows.UI.Xaml.Controls.Primitives.Thumb"); return factory;
}
Factory<ScrollViewerObject>& ScrollViewerFactory() {
    static Factory<ScrollViewerObject> factory(L"Windows.UI.Xaml.Controls.ScrollViewer"); return factory;
}
Factory<FontIconObject>& FontIconFactory() {
    static Factory<FontIconObject> factory(L"Windows.UI.Xaml.Controls.FontIcon"); return factory;
}
Factory<RectangleObject>& RectangleFactory() {
    static Factory<RectangleObject> factory(L"Windows.UI.Xaml.Shapes.Rectangle"); return factory;
}

FontFamilyFactory& TheFontFamilyFactory() {
    static FontFamilyFactory factory;
    return factory;
}

Factory<ColumnDefinitionObject>& ColumnDefinitionFactory() {
    static Factory<ColumnDefinitionObject> factory(L"Windows.UI.Xaml.Controls.ColumnDefinition");
    return factory;
}
Factory<RowDefinitionObject>& RowDefinitionFactory() {
    static Factory<RowDefinitionObject> factory(L"Windows.UI.Xaml.Controls.RowDefinition");
    return factory;
}
GridFactory& TheGridFactory() {
    static GridFactory factory;
    return factory;
}
LayoutInformationFactory& TheLayoutInformationFactory() {
    static LayoutInformationFactory factory;
    return factory;
}
DurationHelperFactory& TheDurationHelperFactory() {
    static DurationHelperFactory factory;
    return factory;
}

// Where the harvested font metrics are. A real implementation reads the
// system font directory; this one is told, because the metrics are a build
// artefact of phase 3 rather than something installed on the machine.
//
// Loaded once, on the first activation. A TextBlock with no metrics behind it
// fails when it measures, naming the family it could not find, which is a
// better failure than refusing to load the DLL at all.
void EnsureFontMetrics() {
    static const int loaded = [] {
        char path[4096];
        const DWORD length =
            GetEnvironmentVariableA("OPENXAML_FONT_METRICS", path, sizeof path);
        if (length == 0 || length >= sizeof path) return 0;
        return openxaml::LoadFontDirectory(openxaml::FontLibrary::Default(), path);
    }();
    (void)loaded;
}

IActivationFactory* FactoryFor(const wchar_t* name) {
    if (!name) return nullptr;
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Border") == 0)
        return &BorderFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Grid") == 0)
        return &TheGridFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.StackPanel") == 0)
        return &StackPanelFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Canvas") == 0) return &CanvasFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ContentPresenter") == 0)
        return &ContentPresenterFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Image") == 0) return &ImageFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Shapes.Path") == 0) return &PathFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.PathIcon") == 0) return &PathIconFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.TextBlock") == 0)
        return &TextBlockFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ContentControl") == 0)
        return &ContentControlFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Page") == 0)
        return &PageFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Frame") == 0)
        return &FrameFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ItemsControl") == 0)
        return &ItemsControlFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ListView") == 0)
        return &ListViewFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Primitives.Popup") == 0)
        return &PopupFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Button") == 0) return &ButtonFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.TextBox") == 0) return &TextBoxFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ToolTip") == 0) return &ToolTipFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Primitives.Thumb") == 0) return &ThumbFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ScrollViewer") == 0) return &ScrollViewerFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.FontIcon") == 0) return &FontIconFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Shapes.Rectangle") == 0) return &RectangleFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Media.FontFamily") == 0)
        return &TheFontFamilyFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.ColumnDefinition") == 0)
        return &ColumnDefinitionFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.RowDefinition") == 0)
        return &RowDefinitionFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.Controls.Primitives.LayoutInformation") == 0)
        return &TheLayoutInformationFactory();
    if (wcscmp(name, L"Windows.UI.Xaml.DurationHelper") == 0)
        return &TheDurationHelperFactory();
    return nullptr;
}

}  // namespace

extern "C" __declspec(dllexport) HRESULT WINAPI
DllGetActivationFactory(HSTRING classid, IActivationFactory** factory) {
    if (!factory) return E_POINTER;
    *factory = nullptr;
    EnsureFontMetrics();

    IActivationFactory* found = FactoryFor(WindowsGetStringRawBuffer(classid, nullptr));
    if (!found) {
        // Say so rather than returning a stub. A caller asking for a class
        // this DLL does not implement needs to see that, not an object whose
        // every method fails later for no stated reason.
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    found->AddRef();
    *factory = found;
    return S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow() {
    // The factories are process-lifetime statics.
    return S_FALSE;
}
