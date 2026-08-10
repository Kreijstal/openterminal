// MinGW-compatible implementation of Terminal's default-terminal discovery.

#include "pch.h"
#include "DelegationConfig.hpp"

#include <winrt/Windows.ApplicationModel.AppExtensions.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace appmodel = winrt::Windows::ApplicationModel;
namespace appextensions = winrt::Windows::ApplicationModel::AppExtensions;
namespace foundation = winrt::Windows::Foundation;
namespace collections = winrt::Windows::Foundation::Collections;

namespace
{
    constexpr wchar_t ConsoleValue[]{ L"DelegationConsole" };
    constexpr wchar_t TerminalValue[]{ L"DelegationTerminal" };
    constexpr wchar_t ConsoleExtension[]{ L"com.microsoft.windows.console.host" };
    constexpr wchar_t TerminalExtension[]{ L"com.microsoft.windows.terminal.host" };

    template<typename T>
    T waitForOperation(const foundation::IAsyncOperation<T>& operation)
    {
        wil::unique_handle completed{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
        THROW_LAST_ERROR_IF_NULL(completed);

        operation.Completed([event = completed.get()](const auto&, const foundation::AsyncStatus) noexcept {
            SetEvent(event);
        });

        HANDLE event = completed.get();
        DWORD index = 0;
        THROW_IF_FAILED(CoWaitForMultipleHandles(
            COWAIT_DISPATCH_CALLS | COWAIT_DISPATCH_WINDOW_MESSAGES,
            INFINITE,
            1,
            &event,
            &index));
        return operation.GetResults();
    }

    HRESULT lookupCatalog(const wchar_t* extensionName,
                          std::vector<DelegationConfig::DelegationBase>& entries) noexcept
    try
    {
        const auto extensions = waitForOperation(
            appextensions::AppExtensionCatalog::Open(extensionName).FindAllAsync());
        for (const auto& extension : extensions)
        {
            DelegationConfig::PackageInfo metadata;
            const auto package = extension.Package();
            const auto packageId = package.Id();

            metadata.name = package.DisplayName();
            metadata.author = package.PublisherDisplayName();
            metadata.pfn = packageId.FamilyName();

            try
            {
                metadata.logo = package.Logo().AbsoluteUri();
            }
            catch (...)
            {
                // Package logos are optional and do not affect delegation.
            }

            const auto version = packageId.Version();
            metadata.version = DelegationConfig::PkgVersion{
                version.Major, version.Minor, version.Build, version.Revision
            };

            const auto properties = waitForOperation(extension.GetExtensionPropertiesAsync());
            const auto clsidProperty = properties.Lookup(L"Clsid").as<collections::IPropertySet>();
            const auto clsidValue = clsidProperty.Lookup(L"#text").as<foundation::IPropertyValue>();
            RETURN_HR_IF(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED),
                         clsidValue.Type() != foundation::PropertyType::String);

            CLSID clsid{};
            RETURN_IF_FAILED(IIDFromString(clsidValue.GetString().c_str(), &clsid));
            entries.push_back({ clsid, std::move(metadata) });
        }
        return S_OK;
    }
    CATCH_RETURN()

    HRESULT readClsid(HKEY key, const wchar_t* name, CLSID& value) noexcept
    {
        wchar_t text[39]{};
        DWORD size = sizeof(text);
        const auto status = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, text, &size);
        if (status != ERROR_SUCCESS)
        {
            return HRESULT_FROM_WIN32(status);
        }
        text[std::size(text) - 1] = L'\0';
        return IIDFromString(text, &value);
    }
}

HRESULT DelegationConfig::s_GetAvailablePackages(std::vector<DelegationPackage>& packages,
                                                  DelegationPackage& current) noexcept
try
{
    auto apartment = wil::CoInitializeEx(COINIT_APARTMENTTHREADED);
    packages.clear();
    packages.push_back({ DefaultDelegationPair });
    packages.push_back({ ConhostDelegationPair });

    std::vector<DelegationBase> consoles;
    LOG_IF_FAILED(lookupCatalog(ConsoleExtension, consoles));
    std::vector<DelegationBase> terminals;
    LOG_IF_FAILED(lookupCatalog(TerminalExtension, terminals));

    for (const auto& terminal : terminals)
    {
        for (const auto& console : consoles)
        {
            if (terminal.info.IsFromSamePackage(console.info))
            {
                packages.push_back({
                    { DelegationPairKind::Custom, console.clsid, terminal.clsid },
                    terminal.info,
                });
                break;
            }
        }
    }

    const auto configured = s_GetDelegationPair();
    for (const auto& package : packages)
    {
        if (package.pair == configured)
        {
            current = package;
            return S_OK;
        }
    }

    current = packages.front();
    return S_OK;
}
CATCH_RETURN()

HRESULT DelegationConfig::s_SetDefaultByPackage(const DelegationPackage& package) noexcept
{
    RETURN_IF_FAILED(s_SetDefaultConsoleById(package.pair.console));
    RETURN_IF_FAILED(s_SetDefaultTerminalById(package.pair.terminal));
    return S_OK;
}

HRESULT DelegationConfig::s_SetDefaultConsoleById(const IID& iid) noexcept
{
    return s_Set(ConsoleValue, iid);
}

HRESULT DelegationConfig::s_SetDefaultTerminalById(const IID& iid) noexcept
{
    return s_Set(TerminalValue, iid);
}

DelegationConfig::DelegationPair DelegationConfig::s_GetDelegationPair() noexcept
{
    wil::unique_hkey startup;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Console\\%%Startup", 0, KEY_READ, &startup) != ERROR_SUCCESS)
    {
        return DefaultDelegationPair;
    }

    CLSID values[2]{ CLSID_Default, CLSID_Default };
    LOG_IF_FAILED(readClsid(startup.get(), ConsoleValue, values[0]));
    LOG_IF_FAILED(readClsid(startup.get(), TerminalValue, values[1]));

    if (values[0] == CLSID_Default || values[1] == CLSID_Default)
    {
        return DefaultDelegationPair;
    }
    if (values[0] == CLSID_Conhost || values[1] == CLSID_Conhost)
    {
        return ConhostDelegationPair;
    }
    return { DelegationPairKind::Custom, values[0], values[1] };
}

HRESULT DelegationConfig::s_Set(const wchar_t* name, const CLSID clsid) noexcept
try
{
    wil::unique_hkey console;
    RETURN_IF_WIN32_ERROR(RegCreateKeyExW(HKEY_CURRENT_USER,
                                          L"Console\\%%Startup",
                                          0,
                                          nullptr,
                                          0,
                                          KEY_WRITE,
                                          nullptr,
                                          &console,
                                          nullptr));

    wil::unique_cotaskmem_string text;
    RETURN_IF_FAILED(StringFromCLSID(clsid, &text));
    const auto bytes = static_cast<DWORD>((wcslen(text.get()) + 1) * sizeof(wchar_t));
    RETURN_IF_WIN32_ERROR(RegSetValueExW(console.get(),
                                         name,
                                         0,
                                         REG_SZ,
                                         reinterpret_cast<const BYTE*>(text.get()),
                                         bytes));
    return S_OK;
}
CATCH_RETURN()
