// Records which local font DirectWrite's system fallback selects for one
// codepoint. The output is temporary input to harvest_font_metrics.py: that
// script reads the selected font's sfnt metrics and strips the machine path,
// leaving only reviewable, deterministic data in the uploaded artifact.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <dwrite_2.h>

#include <atomic>
#include <cwchar>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Check(HRESULT hr, const char* operation) {
    if (SUCCEEDED(hr)) return;
    std::ostringstream message;
    message << operation << " failed with 0x" << std::hex
            << static_cast<unsigned long>(hr);
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
    T* value_ = nullptr;
};

std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                            static_cast<int>(value.size()),
                                            nullptr, 0, nullptr, nullptr);
    if (length <= 0) throw std::runtime_error("WideCharToMultiByte failed");
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), length, nullptr, nullptr);
    return result;
}

std::string JsonEscape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char unit : value) {
        switch (unit) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (unit < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<unsigned>(unit) << std::dec;
            } else {
                out << static_cast<char>(unit);
            }
        }
    }
    return out.str();
}

class AnalysisSource final : public IDWriteTextAnalysisSource {
public:
    AnalysisSource(std::wstring text, std::wstring locale)
        : text_(std::move(text)), locale_(std::move(locale)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        // __uuidof rather than an IID_ symbol: the DirectWrite headers declare
        // their interfaces with DECLSPEC_UUID and publish no `IID_IDWrite*`
        // constants. mingw-w64's headers happen to define them anyway, which
        // is why this compiled here and never once compiled on the Windows
        // runner -- see the comment above main().
        if (IsEqualIID(iid, __uuidof(IUnknown)) ||
            IsEqualIID(iid, __uuidof(IDWriteTextAnalysisSource))) {
            *object = static_cast<IDWriteTextAnalysisSource*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position, const WCHAR** text,
                                                UINT32* length) override {
        if (!text || !length) return E_POINTER;
        if (position >= text_.size()) {
            *text = nullptr;
            *length = 0;
        } else {
            *text = text_.data() + position;
            *length = static_cast<UINT32>(text_.size() - position);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position, const WCHAR** text,
                                                    UINT32* length) override {
        if (!text || !length) return E_POINTER;
        if (!position || position > text_.size()) {
            *text = nullptr;
            *length = 0;
        } else {
            *text = text_.data();
            *length = position;
        }
        return S_OK;
    }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 position, UINT32* length,
                                            const WCHAR** locale) override {
        if (!length || !locale) return E_POINTER;
        if (position >= text_.size()) return E_INVALIDARG;
        *length = static_cast<UINT32>(text_.size() - position);
        *locale = locale_.c_str();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(
        UINT32 position, UINT32* length, IDWriteNumberSubstitution** substitution) override {
        if (!length || !substitution) return E_POINTER;
        if (position >= text_.size()) return E_INVALIDARG;
        *length = static_cast<UINT32>(text_.size() - position);
        *substitution = nullptr;
        return S_OK;
    }

private:
    std::atomic<ULONG> references_{1};
    std::wstring text_;
    std::wstring locale_;
};

std::wstring FamilyName(IDWriteFont* font, const std::wstring& locale) {
    ComPtr<IDWriteFontFamily> family;
    Check(font->GetFontFamily(family.Put()), "GetFontFamily");
    ComPtr<IDWriteLocalizedStrings> names;
    Check(family->GetFamilyNames(names.Put()), "GetFamilyNames");
    UINT32 index = 0;
    BOOL exists = FALSE;
    Check(names->FindLocaleName(locale.c_str(), &index, &exists), "FindLocaleName");
    if (!exists) index = 0;
    UINT32 length = 0;
    Check(names->GetStringLength(index, &length), "GetStringLength");
    std::vector<WCHAR> buffer(static_cast<std::size_t>(length) + 1);
    Check(names->GetString(index, buffer.data(), length + 1), "GetString");
    return std::wstring(buffer.data(), length);
}

std::wstring FontPath(IDWriteFont* font) {
    ComPtr<IDWriteFontFace> face;
    Check(font->CreateFontFace(face.Put()), "CreateFontFace");
    UINT32 count = 0;
    Check(face->GetFiles(&count, nullptr), "GetFiles(count)");
    if (count != 1) throw std::runtime_error("the mapped face does not have exactly one file");
    ComPtr<IDWriteFontFile> file;
    IDWriteFontFile* raw = nullptr;
    Check(face->GetFiles(&count, &raw), "GetFiles");
    *file.Put() = raw;

    const void* key = nullptr;
    UINT32 key_size = 0;
    Check(file->GetReferenceKey(&key, &key_size), "GetReferenceKey");
    ComPtr<IDWriteFontFileLoader> loader;
    Check(file->GetLoader(loader.Put()), "GetLoader");
    ComPtr<IDWriteLocalFontFileLoader> local;
    Check(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),
                                 reinterpret_cast<void**>(local.Put())),
          "IDWriteLocalFontFileLoader");
    UINT32 length = 0;
    Check(local->GetFilePathLengthFromKey(key, key_size, &length),
          "GetFilePathLengthFromKey");
    std::vector<WCHAR> path(static_cast<std::size_t>(length) + 1);
    Check(local->GetFilePathFromKey(key, key_size, path.data(), length + 1),
          "GetFilePathFromKey");
    return std::wstring(path.data(), length);
}

std::wstring CodepointText(unsigned long codepoint) {
    if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
        throw std::runtime_error("invalid Unicode codepoint");
    if (codepoint <= 0xffff) return std::wstring(1, static_cast<wchar_t>(codepoint));
    codepoint -= 0x10000;
    std::wstring result;
    result.push_back(static_cast<wchar_t>(0xd800 + (codepoint >> 10)));
    result.push_back(static_cast<wchar_t>(0xdc00 + (codepoint & 0x3ff)));
    return result;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) {
        std::wcerr << L"usage: font_fallback_probe <base-family> <hex-codepoint> <locale>\n";
        return 2;
    }
    try {
        wchar_t* end = nullptr;
        const unsigned long codepoint = std::wcstoul(argv[2], &end, 16);
        if (!end || *end) throw std::runtime_error("codepoint is not hexadecimal");
        const std::wstring text = CodepointText(codepoint);
        const std::wstring locale = argv[3];

        ComPtr<IDWriteFactory2> factory;
        Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                  __uuidof(IDWriteFactory2),
                                  reinterpret_cast<IUnknown**>(factory.Put())),
              "DWriteCreateFactory");
        ComPtr<IDWriteFontFallback> fallback;
        Check(factory->GetSystemFontFallback(fallback.Put()), "GetSystemFontFallback");
        ComPtr<IDWriteFontCollection> collection;
        Check(factory->GetSystemFontCollection(collection.Put()), "GetSystemFontCollection");
        AnalysisSource* source = new AnalysisSource(text, locale);
        ComPtr<IDWriteFont> mapped;
        UINT32 mapped_length = 0;
        FLOAT scale = 0.0f;
        const HRESULT map_hr = fallback->MapCharacters(
            source, 0, static_cast<UINT32>(text.size()), collection.Get(), argv[1],
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, &mapped_length, mapped.Put(), &scale);
        source->Release();
        Check(map_hr, "MapCharacters");
        if (!mapped.Get() || mapped_length != text.size() || scale <= 0.0f)
            throw std::runtime_error("DirectWrite did not map the complete codepoint");

        const std::string base = JsonEscape(Utf8(argv[1]));
        const std::string mapped_family = JsonEscape(Utf8(FamilyName(mapped.Get(), locale)));
        const std::string path = JsonEscape(Utf8(FontPath(mapped.Get())));
        std::cout << "{\n"
                  << " \"schema_version\": 1,\n"
                  << " \"source_family\": \"" << base << "\",\n"
                  << " \"locale\": \"" << JsonEscape(Utf8(locale)) << "\",\n"
                  << " \"mappings\": {\n"
                  << "  \"" << codepoint << "\": {\"family\": \""
                  << mapped_family << "\", \"file\": \"" << path
                  << "\", \"scale\": " << std::setprecision(9) << scale << "}\n"
                  << " }\n"
                  << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
