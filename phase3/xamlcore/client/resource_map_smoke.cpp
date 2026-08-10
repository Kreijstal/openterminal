// ResourceManager/ResourceMap ABI acceptance using Terminal's exact lookup path.

#include "sdk.h"

#include <roapi.h>

#include <cstdio>
#include <cwchar>

#include "openxaml_iids.h"

namespace warc = ABI::Windows::ApplicationModel::Resources::Core;
namespace wfc = ABI::Windows::Foundation::Collections;

using NamedResourceMapView =
    wfc::__FIMapView_2_HSTRING_Windows__CApplicationModel__CResources__CCore__CNamedResource_t;

inline constexpr GUID IID_NamedResourceMapView = {
    0x4825d6c4, 0x835a, 0x5da1,
    {0x9b, 0xdd, 0x12, 0xe9, 0x7e, 0x16, 0xfb, 0x7a}};

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

HSTRING MakeString(const wchar_t* value) {
    HSTRING result = nullptr;
    if (FAILED(WindowsCreateString(value, static_cast<UINT32>(std::wcslen(value)),
                                   &result)))
        return nullptr;
    return result;
}

}  // namespace

int main() {
    const HRESULT initialized = RoInitialize(RO_INIT_SINGLETHREADED);
    Check(SUCCEEDED(initialized) || initialized == S_FALSE,
          "RoInitialize single-threaded");

    HSTRING class_name = MakeString(
        L"Windows.ApplicationModel.Resources.Core.ResourceManager");
    warc::IResourceManagerStatics* statics = nullptr;
    const HRESULT activated = RoGetActivationFactory(
        class_name,
        openxaml::iid::Windows_ApplicationModel_Resources_Core_IResourceManagerStatics,
        reinterpret_cast<void**>(&statics));
    WindowsDeleteString(class_name);
    Check(SUCCEEDED(activated) && statics, "activate ResourceManager statics");

    warc::IResourceManager* manager = nullptr;
    Check(statics && SUCCEEDED(statics->get_Current(&manager)) && manager,
          "ResourceManager.Current");
    warc::IResourceMap* main = nullptr;
    Check(manager && SUCCEEDED(manager->get_MainResourceMap(&main)) && main,
          "ResourceManager.MainResourceMap");

    HSTRING scope = MakeString(L"Microsoft.Terminal.Settings.Model/Resources");
    warc::IResourceMap* subtree = nullptr;
    Check(main && SUCCEEDED(main->GetSubtree(scope, &subtree)) && subtree,
          "ResourceMap.GetSubtree for Settings.Model");
    WindowsDeleteString(scope);

    NamedResourceMapView* view = nullptr;
    Check(subtree && SUCCEEDED(subtree->QueryInterface(
                         IID_NamedResourceMapView,
                         reinterpret_cast<void**>(&view))) && view,
          "ResourceMap exposes its required IMapView interface");

    HSTRING key = MakeString(L"CopyTextCommandKey");
    boolean present = false;
    Check(view && SUCCEEDED(view->HasKey(key, &present)) && present,
          "Settings.Model catalog contains CopyTextCommandKey");

    warc::IResourceCandidate* candidate = nullptr;
    Check(subtree && SUCCEEDED(subtree->GetValue(key, &candidate)) && candidate,
          "ResourceMap.GetValue returns a candidate");
    HSTRING localized = nullptr;
    Check(candidate && SUCCEEDED(candidate->get_ValueAsString(&localized)),
          "ResourceCandidate.ValueAsString succeeds");
    UINT32 length = 0;
    const wchar_t* localized_text =
        localized ? WindowsGetStringRawBuffer(localized, &length) : nullptr;
    Check(localized_text && length == 9 &&
              std::wmemcmp(localized_text, L"Copy text", 9) == 0,
          "CopyTextCommandKey resolves to the harvested localized value");
    WindowsDeleteString(localized);
    if (candidate) candidate->Release();

    HSTRING missing = MakeString(L"DefinitelyMissingResourceKey");
    present = true;
    Check(view && SUCCEEDED(view->HasKey(missing, &present)) && !present,
          "missing catalog key remains absent");
    WindowsDeleteString(missing);
    WindowsDeleteString(key);

    if (view) view->Release();
    if (subtree) subtree->Release();
    if (main) main->Release();
    if (manager) manager->Release();
    if (statics) statics->Release();
    if (SUCCEEDED(initialized)) RoUninitialize();

    if (failures) return 1;
    std::puts("resource map ABI checks passed");
    return 0;
}
