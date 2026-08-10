# Phase 4: integration validation

Validate the complete Terminal UI, ConPTY-backed tabs, input, rendering,
accessibility, resource loading, and multiple console clients. General Wine
fixes should be split into upstreamable patches with regression tests.

## The boot frontier

`scripts/boot_frontier.py` runs the phase-2 build of `WindowsTerminal.exe`
under Wine and Xvfb in a prefix where [the phase-3 XAML DLL](../phase3/xamlcore/)
is registered, and reduces the run to one JSON document: which boot milestones
were reached and the first named thing that stopped the process.

It is a measurement, not a wish. `expected/boot-frontier.json` moves the way
the oracle digest moves — deliberately, after reading what changed — and the
runtime advances it only by implementing what the binary actually asked for
next. The order is the binary's, not ours: what looks like the obvious next
class is repeatedly not what the process reaches first.

    python3 -B phase4/scripts/boot_frontier.py \
        --executable /tmp/openterminal-mingw/native-build/WindowsTerminal.exe \
        --prefix <a prefix built by phase3/scripts/build_xamlcore.py>

The current MinGW-built `WindowsTerminal.exe` crosses the boot frontier under
Wine: the virtual-display probe reaches WinRT activation, creates the
desktop HWND/XAML island and remains alive until the timeout. The probe reports
no missing import or runtime class, OpenXaml `E_NOTIMPL`, XBF materialization
failure, C++ exception, or access violation. Its committed expectation is
`expected/boot-frontier.json`.

For a release-candidate check, use the artifact-coupled launcher rather than a
prefix that may contain an older registration. It refuses a nonempty prefix,
registers the exact DLL argument, verifies that registration, launches from
the Terminal deployment directory with the same directory as the XBF root,
and requires a clean, non-empty committed UI frame before a bounded timeout is
success. It also fetches the pinned Apache-2.0 WinUI-compatible icon font from
`phase3/xamlcore/runtime_fonts.json`, verifies its SHA-256 under `/tmp`, and
passes a private family-alias manifest to DirectWrite. No font binary is
written to the repository:

```bash
python3 -B phase4/scripts/run_terminal_integration.py \
  --xaml-dll /tmp/openterminal-xamlcore/openxaml.dll \
  --executable /tmp/openterminal-mingw/native-build/WindowsTerminal.exe \
  --prefix /tmp/openterminal-exact-integration \
  --timeout 30
```

Omit `--prefix` for an automatically removed temporary prefix. Supply
`--log /tmp/openxaml-terminal.log` when the launch trace must survive that
cleanup. No DLL, executable, XBF or Wine-prefix file is written to the
repository.

This is a boot milestone, not complete UI validation. The shipped XBF object
graphs now materialize, but full control rendering, swap-chain presentation,
input, accessibility, live resource behavior and ConPTY-backed tabs remain the
Phase 4 integration surface.

### Where the frontier has stood

| frontier | what the binary was doing |
|---|---|
| `Windows.UI.Xaml.DurationHelper` | CRT static initialization: a file-scope `Duration` in `TerminalApp/Pane.cpp`, before `wWinMain` |
| `Windows.UI.Xaml.Application` | composing its own `App`, which derives from `Application` |
| `Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider` | `App::Initialize`, registering the metadata providers |
| *(none)* | the full activation surface answers; the process runs until the probe timeout |

### Reading a frontier that did not move

An unchanged `first_missing_class` after a real implementation landed is
information, not a failure: it means the class was registered but something
before it now fails differently. `crash` is the field to read then — a
`winrt::hresult_class_not_registered` is a missing class, anything else is a
method that answered and should not have, or refused and should not have.
