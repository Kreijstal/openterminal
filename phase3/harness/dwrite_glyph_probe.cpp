// Record DirectWrite's public shaped-glyph boundary for one text run.
//
// This is intentionally not an API interceptor. IDWriteTextLayout calls the
// documented IDWriteTextRenderer boundary with the glyph indices, advances,
// offsets and raster settings a renderer needs.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <dwrite.h>

#include <atomic>
#include <cmath>
#include <cwchar>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void Check(HRESULT result, const char* operation) {
    if (SUCCEEDED(result)) return;
    std::ostringstream message;
    message << operation << " failed with 0x" << std::hex
            << static_cast<unsigned long>(result);
    throw std::runtime_error(message.str());
}

template <typename T>
class ComPtr {
public:
    ~ComPtr() { if (value_) value_->Release(); }
    T* Get() const { return value_; }
    T** Put() {
        if (value_) value_->Release();
        value_ = nullptr;
        return &value_;
    }
    T* operator->() const { return value_; }
private:
    T* value_{};
};

std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("WideCharToMultiByte failed");
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::string Escape(const std::string& text) {
    std::ostringstream out;
    for (const unsigned char value : text) {
        if (value == '\\' || value == '"') out << '\\' << static_cast<char>(value);
        else if (value == '\n') out << "\\n";
        else if (value == '\r') out << "\\r";
        else if (value == '\t') out << "\\t";
        else if (value < 0x20) {
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                << static_cast<unsigned>(value) << std::dec;
        } else out << static_cast<char>(value);
    }
    return out.str();
}

std::string Number(float value) {
    if (!std::isfinite(value)) throw std::runtime_error("DirectWrite returned a non-finite value");
    if (std::abs(value) < 0.0000005f) value = 0;
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

template <typename T>
void Array(std::ostringstream& out, const T* values, UINT32 count) {
    out << '[';
    for (UINT32 index = 0; index < count; ++index) {
        if (index) out << ", ";
        out << values[index];
    }
    out << ']';
}

class Renderer final : public IDWriteTextRenderer {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        // __uuidof, matching DWriteCreateFactory below: the DirectWrite
        // headers declare their interfaces with DECLSPEC_UUID and publish no
        // `IID_IDWrite*` constants for cl to find.
        if (IsEqualIID(iid, __uuidof(IUnknown)) ||
            IsEqualIID(iid, __uuidof(IDWritePixelSnapping)) ||
            IsEqualIID(iid, __uuidof(IDWriteTextRenderer))) {
            *object = static_cast<IDWriteTextRenderer*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override {
        if (!disabled) return E_POINTER;
        *disabled = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
        if (!transform) return E_POINTER;
        *transform = {1, 0, 0, 1, 0, 0};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* value) override {
        if (!value) return E_POINTER;
        *value = 1.0f;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(
        void*, FLOAT baseline_x, FLOAT baseline_y, DWRITE_MEASURING_MODE measuring_mode,
        const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION* description,
        IUnknown*) override {
        if (!run || !description) return E_POINTER;
        if (run->glyphCount && (!run->fontFace || !run->glyphIndices || !run->glyphAdvances))
            return E_UNEXPECTED;
        DWRITE_FONT_METRICS metrics{};
        run->fontFace->GetMetrics(&metrics);
        if (!first_) runs_ << ",\n";
        first_ = false;
        runs_ << "  {\"text_position\": " << description->textPosition
              << ", \"string_length\": " << description->stringLength
              << ", \"locale\": \""
              << Escape(Utf8(description->localeName ? description->localeName : L"")) << "\""
              << ", \"baseline\": [" << Number(baseline_x) << ", " << Number(baseline_y) << "]"
              << ", \"font_em_size\": " << Number(run->fontEmSize)
              << ", \"font_face\": {\"type\": "
              << static_cast<int>(run->fontFace->GetType())
              << ", \"face_index\": " << run->fontFace->GetIndex()
              << ", \"simulations\": " << static_cast<int>(run->fontFace->GetSimulations())
              << ", \"design_units_per_em\": " << metrics.designUnitsPerEm << "}"
              << ", \"measuring_mode\": " << static_cast<int>(measuring_mode)
              << ", \"bidi_level\": " << run->bidiLevel
              << ", \"is_sideways\": " << (run->isSideways ? "true" : "false")
              << ", \"glyph_indices\": ";
        Array(runs_, run->glyphIndices, run->glyphCount);
        runs_ << ", \"glyph_advances\": [";
        for (UINT32 index = 0; index < run->glyphCount; ++index) {
            if (index) runs_ << ", ";
            runs_ << Number(run->glyphAdvances[index]);
        }
        runs_ << "], \"glyph_offsets\": [";
        for (UINT32 index = 0; index < run->glyphCount; ++index) {
            if (index) runs_ << ", ";
            const auto offset = run->glyphOffsets ? run->glyphOffsets[index]
                                                  : DWRITE_GLYPH_OFFSET{};
            runs_ << '[' << Number(offset.advanceOffset) << ", "
                  << Number(offset.ascenderOffset) << ']';
        }
        runs_ << "]}";
        ++count_;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT,
                                             const DWRITE_UNDERLINE*, IUnknown*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(
        void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(
        void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override {
        return E_NOTIMPL;
    }

    const std::string Runs() const { return runs_.str(); }
    UINT32 Count() const { return count_; }

private:
    std::atomic<ULONG> references_{1};
    std::ostringstream runs_;
    bool first_{true};
    UINT32 count_{};
};

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 5) {
        std::wcerr << L"usage: dwrite_glyph_probe <family> <size> <locale> <text>\n";
        return 2;
    }
    try {
        wchar_t* end = nullptr;
        const float font_size = std::wcstof(argv[2], &end);
        if (!end || *end || !std::isfinite(font_size) || font_size <= 0)
            throw std::runtime_error("font size is invalid");
        const std::wstring text = argv[4];

        ComPtr<IDWriteFactory> factory;
        Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                  reinterpret_cast<IUnknown**>(factory.Put())),
              "DWriteCreateFactory");
        ComPtr<IDWriteTextFormat> format;
        Check(factory->CreateTextFormat(argv[1], nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                        DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL, font_size,
                                        argv[3], format.Put()),
              "CreateTextFormat");
        ComPtr<IDWriteTextLayout> layout;
        Check(factory->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                                        format.Get(), 4096.0f, 4096.0f, layout.Put()),
              "CreateTextLayout");

        auto* renderer = new Renderer();
        const HRESULT draw_result = layout->Draw(nullptr, renderer, 0, 0);
        const std::string runs = renderer->Runs();
        const UINT32 run_count = renderer->Count();
        renderer->Release();
        Check(draw_result, "IDWriteTextLayout::Draw");
        if (!run_count && !text.empty()) throw std::runtime_error("DirectWrite emitted no glyph runs");

        ComPtr<IDWriteRenderingParams> params;
        Check(factory->CreateRenderingParams(params.Put()), "CreateRenderingParams");
        std::cout << "{\n"
                  << " \"schema_version\": 1,\n"
                  << " \"family\": \"" << Escape(Utf8(argv[1])) << "\",\n"
                  << " \"font_size\": " << Number(font_size) << ",\n"
                  << " \"locale\": \"" << Escape(Utf8(argv[3])) << "\",\n"
                  << " \"text\": \"" << Escape(Utf8(text)) << "\",\n"
                  << " \"pixel_snapping\": {\"pixels_per_dip\": 1.000000, "
                     "\"transform\": [1, 0, 0, 1, 0, 0]},\n"
                  << " \"system_rendering_params\": {\"gamma\": " << Number(params->GetGamma())
                  << ", \"enhanced_contrast\": " << Number(params->GetEnhancedContrast())
                  << ", \"cleartype_level\": " << Number(params->GetClearTypeLevel())
                  << ", \"pixel_geometry\": " << static_cast<int>(params->GetPixelGeometry())
                  << ", \"rendering_mode\": " << static_cast<int>(params->GetRenderingMode())
                  << "},\n"
                  << " \"runs\": [\n" << runs << "\n ]\n}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
