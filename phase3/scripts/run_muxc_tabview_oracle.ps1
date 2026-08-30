param(
    [Parameter(Mandatory = $true)]
    [string]$Probe,

    [Parameter(Mandatory = $true)]
    [string]$OpenXamlDll,

    [string]$OutputDirectory = (Join-Path ([System.IO.Path]::GetTempPath()) 'openterminal-muxc-oracle'),

    [switch]$CapturePixels
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Probe = (Resolve-Path $Probe).Path
$OpenXamlDll = (Resolve-Path $OpenXamlDll).Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path $OutputDirectory).Path

$framework = Get-AppxPackage -Name 'Microsoft.UI.Xaml.2.8' |
    Where-Object Architecture -eq 'X64' |
    Sort-Object Version -Descending |
    Select-Object -First 1
if (-not $framework) {
    throw 'Microsoft.UI.Xaml.2.8 is not installed for the current user'
}
$officialDll = Join-Path $framework.InstallLocation 'Microsoft.UI.Xaml.dll'
if (-not (Test-Path $officialDll)) {
    throw "the framework package has no Microsoft.UI.Xaml.dll: $officialDll"
}

# XAML islands need package identity for WinUI's resource graph. This is a
# loose development package: its only payload is the probe copied into the
# caller-selected temporary output directory. No DLL, XBF, SDK, or package is
# written into the source tree.
$packageRoot = Join-Path $OutputDirectory 'package'
New-Item -ItemType Directory -Force $packageRoot | Out-Null
Copy-Item -Force $Probe (Join-Path $packageRoot 'muxc_tabview_oracle.exe')

Add-Type -AssemblyName System.Drawing
$bitmap = [System.Drawing.Bitmap]::new(150, 150)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.Clear([System.Drawing.Color]::FromArgb(255, 32, 32, 32))
$bitmap.Save((Join-Path $packageRoot 'logo.png'),
             [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()

$manifest = @"
<?xml version="1.0" encoding="utf-8"?>
<Package
  xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
  xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
  xmlns:uap5="http://schemas.microsoft.com/appx/manifest/uap/windows10/5"
  xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
  IgnorableNamespaces="uap uap5 rescap">
  <Identity Name="Kreijstal.OpenTerminal.XamlOracle" Publisher="CN=Kreijstal"
            Version="1.0.0.0" ProcessorArchitecture="x64" />
  <Properties>
    <DisplayName>OpenTerminal XAML Oracle</DisplayName>
    <PublisherDisplayName>Kreijstal</PublisherDisplayName>
    <Logo>logo.png</Logo>
  </Properties>
  <Dependencies>
    <TargetDeviceFamily Name="Windows.Desktop" MinVersion="10.0.19041.0"
                        MaxVersionTested="10.0.26100.0" />
    <PackageDependency Name="$($framework.Name)" Publisher="$($framework.Publisher)"
                       MinVersion="$($framework.Version)" />
  </Dependencies>
  <Applications>
    <Application Id="XamlOracle" Executable="muxc_tabview_oracle.exe"
                 EntryPoint="Windows.FullTrustApplication">
      <uap:VisualElements DisplayName="OpenTerminal XAML Oracle"
          Description="Differential WinUI oracle" BackgroundColor="transparent"
          Square150x150Logo="logo.png" Square44x44Logo="logo.png" />
      <Extensions>
        <uap5:Extension Category="windows.appExecutionAlias"
            Executable="muxc_tabview_oracle.exe"
            EntryPoint="Windows.FullTrustApplication">
          <uap5:AppExecutionAlias>
            <uap5:ExecutionAlias Alias="muxctabvieworacle.exe" />
          </uap5:AppExecutionAlias>
        </uap5:Extension>
      </Extensions>
    </Application>
  </Applications>
  <Capabilities><rescap:Capability Name="runFullTrust" /></Capabilities>
</Package>
"@
$manifestPath = Join-Path $packageRoot 'AppxManifest.xml'
$manifest | Set-Content -Encoding utf8 $manifestPath

$existing = Get-AppxPackage -Name 'Kreijstal.OpenTerminal.XamlOracle'
foreach ($package in $existing) {
    Remove-AppxPackage -Package $package.PackageFullName
}
Add-AppxPackage -Register $manifestPath

function Quote-Argument([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

$officialJson = Join-Path $OutputDirectory 'official.json'
$openXamlJson = Join-Path $OutputDirectory 'openxaml.json'
$officialArgs = @('official', $officialDll, $officialJson)
$openXamlArgs = @('openxaml', $OpenXamlDll, $openXamlJson)
if ($CapturePixels) {
    $officialArgs += (Join-Path $OutputDirectory 'official.bgra')
    $openXamlArgs += (Join-Path $OutputDirectory 'openxaml.bgra')
}

$alias = Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps\muxctabvieworacle.exe'
$official = Start-Process -FilePath $alias -Wait -PassThru -ArgumentList (
    $officialArgs | ForEach-Object { Quote-Argument $_ })
if ($official.ExitCode -ne 0) {
    throw "official WinUI probe exited with $($official.ExitCode)"
}

$actual = Start-Process -FilePath $Probe -Wait -PassThru -ArgumentList (
    $openXamlArgs | ForEach-Object { Quote-Argument $_ })
if ($actual.ExitCode -ne 0) {
    throw "OpenXaml probe exited with $($actual.ExitCode)"
}

Write-Host "official: $officialJson"
Write-Host "OpenXaml: $openXamlJson"
Write-Host "official DLL: $officialDll"
