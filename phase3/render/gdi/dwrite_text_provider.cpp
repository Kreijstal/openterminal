#include "dwrite_text_provider.h"

#include <dwrite.h>
#include <dwrite_3.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

namespace openxaml::render {
namespace {

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { Reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T* get() const { return value_; }
    T** put() { Reset(); return &value_; }
    T* operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
    void Reset() {
        if (value_) value_->Release();
        value_ = nullptr;
    }
private:
    T* value_ = nullptr;
};

std::string HrMessage(const char* operation, HRESULT hr) {
    char text[160]{};
    std::snprintf(text, sizeof(text), "%s failed with HRESULT 0x%08lx",
                  operation, static_cast<unsigned long>(hr));
    return text;
}

bool Widen(const std::string& utf8, std::wstring& wide, std::string& diagnostic) {
    wide.clear();
    if (utf8.empty()) return true;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           utf8.data(), static_cast<int>(utf8.size()),
                                           nullptr, 0);
    if (!count) {
        diagnostic = "the text is not valid UTF-8";
        return false;
    }
    wide.resize(static_cast<std::size_t>(count));
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                             static_cast<int>(utf8.size()), wide.data(), count)) {
        diagnostic = "the text could not be converted to UTF-16";
        return false;
    }
    return true;
}

bool Narrow(const std::wstring& wide, std::string& utf8, std::string& diagnostic) {
    utf8.clear();
    if (wide.empty()) return true;
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                          wide.data(), static_cast<int>(wide.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (!count) {
        diagnostic = "the resolved font family could not be converted to UTF-8";
        return false;
    }
    utf8.resize(static_cast<std::size_t>(count));
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                             static_cast<int>(wide.size()), utf8.data(), count,
                             nullptr, nullptr)) {
        diagnostic = "the resolved font family could not be converted to UTF-8";
        return false;
    }
    return true;
}

std::vector<std::wstring> FamilyCandidates(const std::string& family,
                                            std::string& diagnostic) {
    std::vector<std::wstring> result;
    std::size_t begin = 0;
    while (begin <= family.size()) {
        const std::size_t comma = family.find(',', begin);
        std::string candidate = family.substr(
            begin, comma == std::string::npos ? std::string::npos : comma - begin);
        const std::size_t first = candidate.find_first_not_of(" \t");
        const std::size_t last = candidate.find_last_not_of(" \t");
        if (first != std::string::npos) {
            candidate = candidate.substr(first, last - first + 1);
            std::wstring wide;
            if (!Widen(candidate, wide, diagnostic)) return {};
            result.push_back(std::move(wide));
        }
        if (comma == std::string::npos) break;
        begin = comma + 1;
    }
    if (result.empty()) diagnostic = "the requested font family list is empty";
    return result;
}

std::vector<UINT32> DecodeUtf16(const std::wstring& text, bool* valid = nullptr) {
    std::vector<UINT32> result;
    bool okay = true;
    for (std::size_t i = 0; i < text.size(); ++i) {
        UINT32 value = static_cast<UINT16>(text[i]);
        if (value >= 0xd800 && value <= 0xdbff) {
            if (i + 1 >= text.size()) { okay = false; break; }
            const UINT32 low = static_cast<UINT16>(text[++i]);
            if (low < 0xdc00 || low > 0xdfff) { okay = false; break; }
            value = 0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00);
        } else if (value >= 0xdc00 && value <= 0xdfff) {
            okay = false;
            break;
        }
        result.push_back(value);
    }
    if (valid) *valid = okay;
    if (!okay) result.clear();
    return result;
}

struct ResolvedFont {
    std::wstring family_name;
    std::string family_utf8;
    IDWriteFontCollection* collection = nullptr;
    ComPtr<IDWriteFont> font;
    ComPtr<IDWriteFontFace> face;
};

class DirectWriteProvider;
std::mutex g_provider_mutex;
std::shared_ptr<DirectWriteProvider> g_provider;
// Files handed to this process by name rather than by manifest. Read once, when
// the provider builds its private collection; see AddPrivateDirectWriteFontFile.
std::vector<std::wstring> g_added_font_paths;

class DirectWriteProvider final : public RuntimeTextProvider,
                                  public std::enable_shared_from_this<DirectWriteProvider> {
public:
    bool Initialize(std::string& diagnostic) {
        if (!LoadPrivateFontAliases(diagnostic)) return false;
        private_font_paths_.insert(private_font_paths_.end(),
                                   g_added_font_paths.begin(),
                                   g_added_font_paths.end());
        HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                          __uuidof(IDWriteFactory),
                                          reinterpret_cast<IUnknown**>(factory_.put()));
        if (FAILED(hr)) {
            diagnostic = HrMessage("DWriteCreateFactory", hr);
            return false;
        }
        // Private fonts are installed before creating this collection. Asking
        // for an update is required on Wine and is also the documented way to
        // observe fonts added after a process began on Windows.
        hr = factory_->GetSystemFontCollection(collection_.put(), TRUE);
        if (FAILED(hr)) {
            diagnostic = HrMessage("GetSystemFontCollection", hr);
            return false;
        }
        if (!private_font_paths_.empty() &&
            !BuildPrivateFontCollection(diagnostic)) {
            return false;
        }
        return true;
    }

    bool ResolveFontMetrics(const std::string& family, FontMetrics& metrics,
                            std::string& resolved_family,
                            std::string& diagnostic) override {
        ResolvedFont resolved;
        if (!Resolve(family, false, resolved, diagnostic)) return false;
        DWRITE_FONT_METRICS native{};
        resolved.face->GetMetrics(&native);
        if (!native.designUnitsPerEm) {
            diagnostic = "DirectWrite returned zero design units per em for the resolved font";
            return false;
        }
        metrics = {};
        metrics.provenance = FontProvenance::RuntimeDirectWrite;
        metrics.units_per_em = native.designUnitsPerEm;
        metrics.ascender = native.ascent;
        metrics.descender = -static_cast<double>(native.descent);
        metrics.line_gap = native.lineGap;
        resolved_family = resolved.family_utf8;
        return true;
    }

    bool Layout(const RuntimeTextRequest& request, RuntimeTextResult& result,
                std::string& diagnostic) override {
        result = {};
        if (!std::isfinite(request.font_size) || request.font_size <= 0.0) {
            diagnostic = "DirectWrite text requires a finite positive FontSize";
            return false;
        }
        std::wstring text;
        if (!Widen(request.text, text, diagnostic)) return false;
        ResolvedFont line_font;
        if (!Resolve(request.family, request.bold, line_font, diagnostic)) return false;
        ResolvedFont glyph_font;
        if (!Resolve(request.family, request.bold, glyph_font, diagnostic, &text)) return false;

        std::wstring locale;
        if (!Widen(request.language, locale, diagnostic) || locale.empty()) {
            if (diagnostic.empty()) diagnostic = "DirectWrite text requires an explicit language";
            return false;
        }

        ComPtr<IDWriteTextFormat> format;
        HRESULT hr = factory_->CreateTextFormat(
            glyph_font.family_name.c_str(), glyph_font.collection,
            request.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            static_cast<FLOAT>(request.font_size), locale.c_str(), format.put());
        if (FAILED(hr)) {
            diagnostic = HrMessage("CreateTextFormat", hr);
            return false;
        }
        DWRITE_FONT_METRICS font_metrics{};
        line_font.face->GetMetrics(&font_metrics);
        if (!font_metrics.designUnitsPerEm) {
            diagnostic = "DirectWrite returned zero design units per em for line metrics";
            return false;
        }
        const FLOAT baseline = static_cast<FLOAT>(
            static_cast<double>(font_metrics.ascent) * request.font_size /
            font_metrics.designUnitsPerEm);
        const FLOAT line_spacing = static_cast<FLOAT>(
            static_cast<double>(font_metrics.ascent + font_metrics.descent +
                                font_metrics.lineGap) * request.font_size /
            font_metrics.designUnitsPerEm);
        hr = format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                                    line_spacing, baseline);
        if (FAILED(hr)) {
            diagnostic = HrMessage("IDWriteTextFormat::SetLineSpacing", hr);
            return false;
        }
        format->SetWordWrapping(request.wrap ? DWRITE_WORD_WRAPPING_WRAP
                                             : DWRITE_WORD_WRAPPING_NO_WRAP);

        const FLOAT width = std::isfinite(request.width)
            ? static_cast<FLOAT>(std::max(0.0, request.width))
            : std::numeric_limits<FLOAT>::max();
        ComPtr<IDWriteTextLayout> layout;
        hr = factory_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                                        format.get(), width,
                                        std::numeric_limits<FLOAT>::max(), layout.put());
        if (FAILED(hr)) {
            diagnostic = HrMessage("CreateTextLayout", hr);
            return false;
        }
        DWRITE_TEXT_METRICS measured{};
        hr = layout->GetMetrics(&measured);
        if (FAILED(hr)) {
            diagnostic = HrMessage("IDWriteTextLayout::GetMetrics", hr);
            return false;
        }
        result.size = {measured.width, measured.height};
        result.resolved_family = glyph_font.family_utf8;
        result.baseline = baseline;

        if (!ClusterAdvances(*layout.get(), text, result.advances, diagnostic))
            return false;
        return true;
    }

    bool Draw(Surface& surface, const TextOp& run, Color ink,
              std::string& diagnostic);

private:
    bool LoadPrivateFontAliases(std::string& diagnostic) {
        const DWORD needed = GetEnvironmentVariableW(
            L"OPENXAML_FONT_ALIAS_MANIFEST", nullptr, 0);
        if (!needed) return true;
        std::wstring manifest(needed, L'\0');
        const DWORD copied = GetEnvironmentVariableW(
            L"OPENXAML_FONT_ALIAS_MANIFEST", manifest.data(), needed);
        if (!copied || copied >= needed) {
            diagnostic = "OPENXAML_FONT_ALIAS_MANIFEST could not be read";
            return false;
        }
        manifest.resize(copied);

        std::ifstream input(std::filesystem::path(manifest), std::ios::binary);
        if (!input) {
            diagnostic = "the OpenXaml font-alias manifest could not be opened";
            return false;
        }
        std::string line;
        if (!std::getline(input, line) || line != "openxaml-font-aliases-v1") {
            diagnostic = "the OpenXaml font-alias manifest has no v1 header";
            return false;
        }
        std::size_t line_number = 1;
        while (std::getline(input, line)) {
            ++line_number;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.front() == '#') continue;
            const std::size_t first = line.find('\t');
            const std::size_t second = first == std::string::npos
                ? std::string::npos : line.find('\t', first + 1);
            if (first == std::string::npos) {
                diagnostic = "invalid font-alias record on line " +
                    std::to_string(line_number);
                return false;
            }
            const std::string kind = line.substr(0, first);
            if (kind == "font" && second == std::string::npos) {
                std::wstring path;
                if (!Widen(line.substr(first + 1), path, diagnostic)) return false;
                if (path.empty()) {
                    diagnostic = "the private OpenXaml font on line " +
                        std::to_string(line_number) + " has an empty path";
                    return false;
                }
                private_font_paths_.push_back(std::move(path));
                continue;
            }
            if (kind == "alias" && second != std::string::npos &&
                line.find('\t', second + 1) == std::string::npos) {
                std::wstring logical;
                std::wstring resolved;
                if (!Widen(line.substr(first + 1, second - first - 1), logical,
                           diagnostic) ||
                    !Widen(line.substr(second + 1), resolved, diagnostic)) {
                    return false;
                }
                if (logical.empty() || resolved.empty() ||
                    !font_aliases_.emplace(std::move(logical),
                                           std::move(resolved)).second) {
                    diagnostic = "invalid or duplicate font alias on line " +
                        std::to_string(line_number);
                    return false;
                }
                continue;
            }
            diagnostic = "invalid font-alias record on line " +
                std::to_string(line_number);
            return false;
        }
        if (!input.eof()) {
            diagnostic = "the OpenXaml font-alias manifest could not be read completely";
            return false;
        }
        // An alias is optional: a manifest may exist only to hand this process
        // a face under the name written in it. A manifest naming no font at all
        // is still a mistake, because then it declares nothing.
        if (private_font_paths_.empty()) {
            diagnostic = "the OpenXaml font-alias manifest must name at least one font";
            return false;
        }
        return true;
    }

    bool BuildPrivateFontCollection(std::string& diagnostic) {
        ComPtr<IDWriteFactory5> factory5;
        HRESULT hr = factory_->QueryInterface(
            __uuidof(IDWriteFactory5), reinterpret_cast<void**>(factory5.put()));
        if (FAILED(hr)) {
            diagnostic = HrMessage("QueryInterface(IDWriteFactory5)", hr);
            return false;
        }
        ComPtr<IDWriteFontSetBuilder1> builder;
        hr = factory5->CreateFontSetBuilder(builder.put());
        if (FAILED(hr)) {
            diagnostic = HrMessage("CreateFontSetBuilder", hr);
            return false;
        }
        for (const std::wstring& path : private_font_paths_) {
            ComPtr<IDWriteFontFile> file;
            hr = factory_->CreateFontFileReference(path.c_str(), nullptr, file.put());
            if (FAILED(hr)) {
                diagnostic = HrMessage("CreateFontFileReference", hr);
                return false;
            }
            hr = builder->AddFontFile(file.get());
            if (FAILED(hr)) {
                diagnostic = HrMessage("IDWriteFontSetBuilder1::AddFontFile", hr);
                return false;
            }
        }
        ComPtr<IDWriteFontSet> font_set;
        hr = builder->CreateFontSet(font_set.put());
        if (FAILED(hr)) {
            diagnostic = HrMessage("IDWriteFontSetBuilder::CreateFontSet", hr);
            return false;
        }
        hr = factory5->CreateFontCollectionFromFontSet(
            font_set.get(), private_collection_.put());
        if (FAILED(hr)) {
            diagnostic = HrMessage("CreateFontCollectionFromFontSet", hr);
            return false;
        }
        return true;
    }

    bool Resolve(const std::string& family, bool bold, ResolvedFont& result,
                 std::string& diagnostic, const std::wstring* text = nullptr) {
        const std::vector<std::wstring> candidates = FamilyCandidates(family, diagnostic);
        if (candidates.empty()) return false;
        bool found_installed_family = false;
        for (const std::wstring& candidate : candidates) {
            struct Lookup {
                const std::wstring* name;
                IDWriteFontCollection* collection;
            };
            std::vector<Lookup> lookups{{&candidate, collection_.get()}};
            // A face this process was handed answers to the family name written
            // in the file. Reaching it only through an alias would make a font
            // loaded under its own name unresolvable, which is what the ink
            // samples hit: a real Cascadia Mono file was loaded and
            // "Cascadia Mono" still did not resolve.
            if (private_collection_)
                lookups.push_back({&candidate, private_collection_.get()});
            const auto alias = font_aliases_.find(candidate);
            if (private_collection_ && alias != font_aliases_.end())
                lookups.push_back({&alias->second, private_collection_.get()});
            for (const Lookup& lookup : lookups) {
                const std::wstring& name = *lookup.name;
                UINT32 index = 0;
                WINBOOL exists = FALSE;
                HRESULT hr = lookup.collection->FindFamilyName(
                    name.c_str(), &index, &exists);
                if (FAILED(hr)) {
                    diagnostic = HrMessage("FindFamilyName", hr);
                    return false;
                }
                if (!exists) continue;
                found_installed_family = true;
                ComPtr<IDWriteFontFamily> found_family;
                hr = lookup.collection->GetFontFamily(index, found_family.put());
                if (FAILED(hr)) {
                    diagnostic = HrMessage("GetFontFamily", hr);
                    return false;
                }
                hr = found_family->GetFirstMatchingFont(
                    bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                    result.font.put());
                if (FAILED(hr)) {
                    diagnostic = HrMessage("GetFirstMatchingFont", hr);
                    return false;
                }
                hr = result.font->CreateFontFace(result.face.put());
                if (FAILED(hr)) {
                    diagnostic = HrMessage("CreateFontFace", hr);
                    return false;
                }
                result.family_name = name;
                result.collection = lookup.collection;
                if (!Narrow(name, result.family_utf8, diagnostic)) return false;
                if (text) {
                    bool missing_glyph = false;
                    if (!Covers(result, *text, diagnostic, missing_glyph)) {
                        if (!missing_glyph) return false;
                        diagnostic.clear();
                        result.face.Reset();
                    result.font.Reset();
                    result.family_name.clear();
                    result.family_utf8.clear();
                    result.collection = nullptr;
                        continue;
                    }
                }
                return true;
            }
        }
        if (text && found_installed_family) {
            diagnostic = "no explicitly requested DirectWrite family in \"" + family +
                         "\" covers the complete text; system fallback is ambiguous "
                         "and is not guessed";
            return false;
        }
        diagnostic = "DirectWrite could not resolve any requested family in \"" +
                     family + "\"";
        return false;
    }

    bool Covers(const ResolvedFont& font, const std::wstring& text,
                std::string& diagnostic, bool& missing_glyph) {
        missing_glyph = false;
        bool valid = false;
        const std::vector<UINT32> codepoints = DecodeUtf16(text, &valid);
        if (!valid) {
            diagnostic = "the UTF-16 text contains an unpaired surrogate";
            return false;
        }
        if (codepoints.empty()) return true;
        std::vector<UINT16> glyphs(codepoints.size());
        const HRESULT hr = font.face->GetGlyphIndices(
            codepoints.data(), static_cast<UINT32>(codepoints.size()), glyphs.data());
        if (FAILED(hr)) {
            diagnostic = HrMessage("GetGlyphIndices", hr);
            return false;
        }
        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            if (glyphs[i] == 0) {
                missing_glyph = true;
                char message[192]{};
                std::snprintf(message, sizeof(message),
                              "the resolved DirectWrite face lacks U+%04lX; system fallback "
                              "is ambiguous and is not guessed",
                              static_cast<unsigned long>(codepoints[i]));
                diagnostic = message;
                return false;
            }
        }
        return true;
    }

    static bool ClusterAdvances(IDWriteTextLayout& layout, const std::wstring& text,
                                std::vector<double>& advances,
                                std::string& diagnostic) {
        UINT32 count = 0;
        HRESULT hr = layout.GetClusterMetrics(nullptr, 0, &count);
        if (hr != E_NOT_SUFFICIENT_BUFFER && FAILED(hr)) {
            diagnostic = HrMessage("GetClusterMetrics(size)", hr);
            return false;
        }
        std::vector<DWRITE_CLUSTER_METRICS> clusters(count);
        hr = layout.GetClusterMetrics(clusters.data(), count, &count);
        if (FAILED(hr)) {
            diagnostic = HrMessage("GetClusterMetrics", hr);
            return false;
        }
        std::size_t offset = 0;
        for (const DWRITE_CLUSTER_METRICS& cluster : clusters) {
            if (offset + cluster.length > text.size()) {
                diagnostic = "DirectWrite returned a cluster outside the source text";
                return false;
            }
            bool valid = false;
            const std::vector<UINT32> scalars = DecodeUtf16(
                text.substr(offset, cluster.length), &valid);
            if (!valid || scalars.size() != 1) {
                diagnostic = "DirectWrite shaped multiple codepoints into one cluster; "
                             "per-codepoint retained advances would be ambiguous";
                return false;
            }
            advances.push_back(cluster.width);
            offset += cluster.length;
        }
        if (offset != text.size()) {
            diagnostic = "DirectWrite cluster metrics did not cover the complete source text";
            return false;
        }
        return true;
    }

    ComPtr<IDWriteFactory> factory_;
    ComPtr<IDWriteFontCollection> collection_;
    ComPtr<IDWriteFontCollection1> private_collection_;
    std::vector<std::wstring> private_font_paths_;
    std::map<std::wstring, std::wstring> font_aliases_;

    friend class GlyphRenderer;
};

class GlyphRenderer final : public IDWriteTextRenderer {
public:
    GlyphRenderer(IDWriteFactory& factory, Surface& surface, Color ink,
                  PixelRect effective_clip)
        : factory_(factory), surface_(surface), ink_(ink),
          effective_clip_(effective_clip) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        if (IsEqualGUID(iid, __uuidof(IUnknown)) ||
            IsEqualGUID(iid, __uuidof(IDWritePixelSnapping)) ||
            IsEqualGUID(iid, __uuidof(IDWriteTextRenderer))) {
            *value = static_cast<IDWriteTextRenderer*>(this);
            AddRef();
            return S_OK;
        }
        *value = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { return --references_; }
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, WINBOOL* disabled) override {
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
        void*, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE measuring,
        const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION*,
        IUnknown*) override {
        ComPtr<IDWriteGlyphRunAnalysis> analysis;
        HRESULT hr = factory_.CreateGlyphRunAnalysis(
            run, 1.0f, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
            measuring, x, y, analysis.put());
        if (FAILED(hr)) return hr;
        RECT bounds{};
        hr = analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
        if (FAILED(hr) || bounds.right <= bounds.left || bounds.bottom <= bounds.top)
            return hr;
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        std::vector<BYTE> texture(static_cast<std::size_t>(width) * height * 3);
        hr = analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds,
                                          texture.data(),
                                          static_cast<UINT32>(texture.size()));
        if (FAILED(hr)) return hr;
        bool needs_grayscale = false;
        for (int row = 0; row < height; ++row) {
            const int sy = bounds.top + row;
            if (sy < 0 || sy >= surface_.height()) continue;
            for (int column = 0; column < width; ++column) {
                const int sx = bounds.left + column;
                if (sx < 0 || sx >= surface_.width()) continue;
                if (sx < effective_clip_.left || sx >= effective_clip_.right ||
                    sy < effective_clip_.top || sy >= effective_clip_.bottom) continue;
                const std::size_t index =
                    (static_cast<std::size_t>(row) * width + column) * 3;
                if ((texture[index] || texture[index + 1] || texture[index + 2]) &&
                    (surface_.At(sx, sy) >> 24) != 0xffu) {
                    needs_grayscale = true;
                    break;
                }
            }
            if (needs_grayscale) break;
        }
        // ClearType's independent colour coverages require an opaque
        // destination. Text in a transparent DirectComposition stratum uses
        // one grayscale coverage value, which is valid premultiplied
        // source-over and needs no invented backing rectangle. Keep the whole
        // run in one mode across opaque/non-opaque boundaries.
        if (needs_grayscale)
            return BlendGrayscale(run, measuring, x, y);
        for (int row = 0; row < height; ++row) {
            const int sy = bounds.top + row;
            if (sy < 0 || sy >= surface_.height()) continue;
            for (int column = 0; column < width; ++column) {
                const int sx = bounds.left + column;
                if (sx < 0 || sx >= surface_.width()) continue;
                if (sx < effective_clip_.left || sx >= effective_clip_.right ||
                    sy < effective_clip_.top || sy >= effective_clip_.bottom) continue;
                const std::size_t index =
                    (static_cast<std::size_t>(row) * width + column) * 3;
                BlendClearType(sx, sy, texture[index], texture[index + 1],
                               texture[index + 2]);
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT,
                                             const DWRITE_UNDERLINE*, IUnknown*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(
        void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(
        void*, FLOAT, FLOAT, IDWriteInlineObject*, WINBOOL, WINBOOL,
        IUnknown*) override {
        return E_NOTIMPL;
    }

private:
    HRESULT BlendGrayscale(const DWRITE_GLYPH_RUN* run,
                           DWRITE_MEASURING_MODE measuring,
                           FLOAT x, FLOAT y) {
        ComPtr<IDWriteFactory2> factory2;
        HRESULT hr = factory_.QueryInterface(
            __uuidof(IDWriteFactory2),
            reinterpret_cast<void**>(factory2.put()));
        if (FAILED(hr)) return hr;
        ComPtr<IDWriteGlyphRunAnalysis> grayscale_analysis;
        hr = factory2->CreateGlyphRunAnalysis(
            run, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
            measuring, DWRITE_GRID_FIT_MODE_DEFAULT,
            DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, x, y,
            grayscale_analysis.put());
        if (FAILED(hr)) return hr;

        RECT bounds{};
        hr = grayscale_analysis->GetAlphaTextureBounds(
            DWRITE_TEXTURE_ALIASED_1x1, &bounds);
        if (FAILED(hr) || bounds.right <= bounds.left ||
            bounds.bottom <= bounds.top)
            return hr;
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        std::vector<BYTE> texture(static_cast<std::size_t>(width) * height);
        hr = grayscale_analysis->CreateAlphaTexture(
            DWRITE_TEXTURE_ALIASED_1x1, &bounds, texture.data(),
            static_cast<UINT32>(texture.size()));
        if (FAILED(hr)) return hr;
        for (int row = 0; row < height; ++row) {
            const int sy = bounds.top + row;
            if (sy < 0 || sy >= surface_.height()) continue;
            for (int column = 0; column < width; ++column) {
                const int sx = bounds.left + column;
                if (sx < 0 || sx >= surface_.width()) continue;
                if (sx < effective_clip_.left || sx >= effective_clip_.right ||
                    sy < effective_clip_.top || sy >= effective_clip_.bottom)
                    continue;
                const unsigned coverage =
                    texture[static_cast<std::size_t>(row) * width + column];
                if (coverage)
                    surface_.BlendPixel(sx, sy, ink_, coverage / 255.0);
            }
        }
        return S_OK;
    }

    void BlendClearType(int x, int y, unsigned red_coverage,
                        unsigned green_coverage, unsigned blue_coverage) {
        const std::uint32_t destination = surface_.At(x, y);
        auto channel = [&](unsigned source, unsigned destination_channel,
                           unsigned coverage) {
            const unsigned alpha =
                (static_cast<unsigned>(ink_.a) * coverage + 127u) / 255u;
            return (source * alpha + destination_channel * (255u - alpha) + 127u) /
                   255u;
        };
        const unsigned red = channel(ink_.r, (destination >> 16) & 0xffu,
                                     red_coverage);
        const unsigned green = channel(ink_.g, (destination >> 8) & 0xffu,
                                       green_coverage);
        const unsigned blue = channel(ink_.b, destination & 0xffu, blue_coverage);
        surface_.pixels()[static_cast<std::size_t>(y) * surface_.width() + x] =
            0xff000000u | (red << 16) | (green << 8) | blue;
    }

    LONG references_ = 1;
    IDWriteFactory& factory_;
    Surface& surface_;
    Color ink_;
    PixelRect effective_clip_;
};

bool DirectWriteProvider::Draw(Surface& surface, const TextOp& run, Color ink,
                               std::string& diagnostic) {
    if (run.has_clip) {
        const double clip_right = run.clip.x + run.clip.width;
        const double clip_bottom = run.clip.y + run.clip.height;
        const auto is_integer = [](double value) {
            return std::isfinite(value) && value == std::floor(value);
        };
        if (!is_integer(run.clip.x) || !is_integer(run.clip.y) ||
            !is_integer(clip_right) || !is_integer(clip_bottom)) {
            diagnostic = "a fractional DirectWrite text clip requires edge-coverage "
                         "masking and is not implemented";
            return false;
        }
    }
    Rect effective = run.bounds;
    if (run.has_clip) {
        const double left = std::max(effective.x, run.clip.x);
        const double top = std::max(effective.y, run.clip.y);
        const double right = std::min(effective.x + effective.width,
                                      run.clip.x + run.clip.width);
        const double bottom = std::min(effective.y + effective.height,
                                       run.clip.y + run.clip.height);
        effective = {left, top, std::max(0.0, right - left),
                     std::max(0.0, bottom - top)};
    }
    PixelRect bounds = TouchedRect(effective);
    bounds.left = std::max(bounds.left, 0);
    bounds.top = std::max(bounds.top, 0);
    bounds.right = std::min(bounds.right, surface.width());
    bounds.bottom = std::min(bounds.bottom, surface.height());
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) return true;
    if (run.advances.empty() && !run.text.empty()) {
        diagnostic = "the retained text run has no shaped advances";
        return false;
    }
    std::wstring text;
    if (!Widen(run.text, text, diagnostic)) return false;
    ResolvedFont glyph_font;
    if (!Resolve(run.font_family, run.bold, glyph_font, diagnostic, &text)) return false;

    // One line, placed by us.
    //
    // This is the case the corpus is mostly made of and the one the delegated
    // path below cannot serve. The layout core snaps an inline run's advances
    // to 1/300 of a DIP (layout/src/text.cpp:105, rule 7) and DirectWrite's own
    // shaping does not, so Cascadia Mono at size 14 is 8.203333 retained
    // against 8.203125 shaped: a run drawn where DirectWrite would put it is
    // drawn 1/4800 of a DIP per glyph away from where this project measured it,
    // and demanding the two agree -- which is what this used to do -- refuses
    // every snapped run in the corpus forever.
    //
    // So the glyph indices and the outlines are DirectWrite's, and every
    // position is the layout's: one advance per codepoint, straight out of the
    // display list, exactly as the pre-DirectWrite backend fed ExtTextOutW a
    // distance array (see phase3/render/README.md). Nothing here re-measures.
    bool valid_text = false;
    const std::vector<UINT32> codepoints = DecodeUtf16(text, &valid_text);
    if (!valid_text) {
        diagnostic = "the UTF-16 text contains an unpaired surrogate";
        return false;
    }
    bool breaks_line = false;
    for (UINT32 code : codepoints)
        breaks_line |= code == 0x0a || code == 0x0d || code == 0x2028 || code == 0x2029;
    double retained_width = 0.0;
    for (double advance : run.advances)
        retained_width = static_cast<double>(
            static_cast<float>(retained_width + advance));
    // A run the layout broke into lines is not placeable from this list: the
    // display list carries advances, not where the breaks went. Those runs go
    // to the delegated path below, which can only accept them when DirectWrite
    // reproduces every retained advance exactly.
    const bool one_line = !breaks_line &&
        (!run.wrap || retained_width <= run.bounds.width + 0.0001);

    if (one_line && !codepoints.empty()) {
        if (run.advances.size() != codepoints.size()) {
            diagnostic = "the retained run has one advance per codepoint everywhere "
                         "else and does not here; its glyph positions are unknown";
            return false;
        }
        std::vector<UINT16> indices(codepoints.size());
        const HRESULT glyph_hr = glyph_font.face->GetGlyphIndices(
            codepoints.data(), static_cast<UINT32>(codepoints.size()), indices.data());
        if (FAILED(glyph_hr)) {
            diagnostic = HrMessage("GetGlyphIndices", glyph_hr);
            return false;
        }
        std::vector<FLOAT> advances;
        advances.reserve(run.advances.size());
        for (double advance : run.advances) advances.push_back(static_cast<FLOAT>(advance));

        DWRITE_GLYPH_RUN glyph_run{};
        glyph_run.fontFace = glyph_font.face.get();
        glyph_run.fontEmSize = static_cast<FLOAT>(run.font_size);
        glyph_run.glyphCount = static_cast<UINT32>(indices.size());
        glyph_run.glyphIndices = indices.data();
        glyph_run.glyphAdvances = advances.data();

        Surface candidate = surface;
        GlyphRenderer renderer(*factory_.get(), candidate, ink, bounds);
        const HRESULT draw_hr = renderer.DrawGlyphRun(
            nullptr, static_cast<FLOAT>(run.bounds.x),
            static_cast<FLOAT>(run.bounds.y + run.baseline),
            DWRITE_MEASURING_MODE_NATURAL, &glyph_run, nullptr, nullptr);
        if (FAILED(draw_hr)) {
            diagnostic = HrMessage("IDWriteGlyphRunAnalysis", draw_hr);
            return false;
        }
        surface = std::move(candidate);
        return true;
    }

    // Several lines. DirectWrite breaks them, so it also places them, and the
    // only thing that keeps that honest is checking it agrees with every number
    // the layout retained.
    ResolvedFont line_font;
    if (!Resolve(run.font_family, run.bold, line_font, diagnostic)) return false;
    std::wstring locale;
    if (!Widen(run.language, locale, diagnostic) || locale.empty()) {
        if (diagnostic.empty()) diagnostic = "DirectWrite text requires an explicit language";
        return false;
    }
    RuntimeTextResult validation;
    if (!Layout(RuntimeTextRequest{run.font_family, run.text, run.font_size,
                                   run.bounds.width, run.wrap, run.bold, run.language},
                validation, diagnostic)) {
        return false;
    }
    if (validation.advances.size() != run.advances.size()) {
        diagnostic = "the DirectWrite glyph clusters do not match the retained advances";
        return false;
    }
    for (std::size_t i = 0; i < run.advances.size(); ++i) {
        if (std::fabs(validation.advances[i] - run.advances[i]) > 0.0001) {
            // Which cluster and by how much. A bare "they differ" says nothing
            // about whether the face is the wrong one, the size is, or a
            // rounding rule moved by a thousandth, and those need different
            // answers.
            char message[224]{};
            std::snprintf(message, sizeof(message),
                          "the DirectWrite glyph positions differ from the retained "
                          "advances: cluster %u is %.6f and the layout retained %.6f",
                          static_cast<unsigned>(i), validation.advances[i],
                          run.advances[i]);
            diagnostic = message;
            return false;
        }
    }
    if (std::fabs(validation.baseline - run.baseline) > 0.0001) {
        char message[192]{};
        std::snprintf(message, sizeof(message),
                      "the DirectWrite baseline differs from the retained line baseline: "
                      "%.6f against %.6f",
                      validation.baseline, run.baseline);
        diagnostic = message;
        return false;
    }

    ComPtr<IDWriteTextFormat> format;
    HRESULT hr = factory_->CreateTextFormat(
        glyph_font.family_name.c_str(), glyph_font.collection,
        run.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        static_cast<FLOAT>(run.font_size), locale.c_str(), format.put());
    if (FAILED(hr)) { diagnostic = HrMessage("CreateTextFormat", hr); return false; }
    DWRITE_FONT_METRICS line_metrics{};
    line_font.face->GetMetrics(&line_metrics);
    if (!line_metrics.designUnitsPerEm) {
        diagnostic = "DirectWrite returned zero design units per em for line metrics";
        return false;
    }
    const FLOAT line_spacing = static_cast<FLOAT>(
        static_cast<double>(line_metrics.ascent + line_metrics.descent +
                            line_metrics.lineGap) * run.font_size /
        line_metrics.designUnitsPerEm);
    hr = format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, line_spacing,
                                static_cast<FLOAT>(run.baseline));
    if (FAILED(hr)) {
        diagnostic = HrMessage("IDWriteTextFormat::SetLineSpacing", hr);
        return false;
    }
    hr = format->SetWordWrapping(run.wrap ? DWRITE_WORD_WRAPPING_WRAP
                                         : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (FAILED(hr)) {
        diagnostic = HrMessage("IDWriteTextFormat::SetWordWrapping", hr);
        return false;
    }
    ComPtr<IDWriteTextLayout> layout;
    hr = factory_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                                    format.get(),
                                    static_cast<FLOAT>(std::max(0.0, run.bounds.width)),
                                    static_cast<FLOAT>(std::max(0.0, run.bounds.height)),
                                    layout.put());
    if (FAILED(hr)) { diagnostic = HrMessage("CreateTextLayout", hr); return false; }

    Surface candidate = surface;
    GlyphRenderer renderer(*factory_.get(), candidate, ink, bounds);
    hr = layout->Draw(nullptr, &renderer, static_cast<FLOAT>(run.bounds.x),
                      static_cast<FLOAT>(run.bounds.y));
    if (FAILED(hr)) {
        diagnostic = HrMessage("IDWriteTextLayout::Draw", hr);
        return false;
    }
    surface = std::move(candidate);
    return true;
}

}  // namespace

bool AddPrivateDirectWriteFontFile(const std::string& path, std::string& diagnostic) {
    std::wstring wide;
    if (!Widen(path, wide, diagnostic)) return false;
    if (wide.empty()) {
        diagnostic = "a private DirectWrite font file needs a path";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_provider_mutex);
    if (g_provider) {
        diagnostic = "the DirectWrite provider is already installed; a private font "
                     "file offered now would never reach its collection";
        return false;
    }
    g_added_font_paths.push_back(std::move(wide));
    return true;
}

namespace {

// The provider itself, without handing layout over to it. Drawing needs a
// DirectWrite factory; it does not need TextBlock to start measuring through
// one, and those are two different decisions that used to be one.
std::shared_ptr<DirectWriteProvider> EnsureProvider(std::string& diagnostic) {
    std::lock_guard<std::mutex> lock(g_provider_mutex);
    if (!g_provider) {
        auto provider = std::make_shared<DirectWriteProvider>();
        if (!provider->Initialize(diagnostic)) return nullptr;
        g_provider = std::move(provider);
    }
    return g_provider;
}

}  // namespace

bool InstallDirectWriteRuntimeTextProvider(std::string& diagnostic) {
    const std::shared_ptr<DirectWriteProvider> provider = EnsureProvider(diagnostic);
    if (!provider) return false;
    SetRuntimeTextProvider(provider);
    return true;
}

bool DrawDirectWriteTextRun(Surface& surface, const TextOp& run, Color ink,
                            std::string& diagnostic) {
    // Deliberately EnsureProvider and not Install. Rasterizing one run must not
    // make DirectWrite the authority on what every *later* element measures --
    // which is what the lazy install did: the corpus harness lays out from the
    // harvested metrics, the first case with a text run painted, and from that
    // case onward every remaining case in the directory measured against
    // whatever faces the machine happened to have. Same corpus, different
    // numbers, decided by filename order and nothing saying so. A host that
    // does want the platform to be the authority says so by name: see
    // phase3/xamlcore/src/factory.cpp, which installs it explicitly.
    const std::shared_ptr<DirectWriteProvider> provider = EnsureProvider(diagnostic);
    if (!provider) return false;
    return provider->Draw(surface, run, ink, diagnostic);
}

}  // namespace openxaml::render
