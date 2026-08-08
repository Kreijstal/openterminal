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

### Where it has stood

| frontier | what the binary was doing |
|---|---|
| `Windows.UI.Xaml.DurationHelper` | CRT static initialization: a file-scope `Duration` in `TerminalApp/Pane.cpp`, before `wWinMain` |
| `Windows.UI.Xaml.Application` | composing its own `App`, which derives from `Application` |
| `Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider` | `App::Initialize`, registering the metadata providers |

The third is where the boot path leaves `Windows.UI.Xaml` and enters WinUI 2.
Nothing in this stack implements `Microsoft.UI.Xaml`: phase 2 produces its
WinMD and a C++/WinRT projection of it, which is compile-time metadata and not
a runtime. Advancing past it is the muxc port's work, not the XAML core's.

### What is registered beyond the frontier

The frontier names one class at a time, because the process dies at the first
one. Asking the activation registry directly — `RoGetActivationFactory` for
`IActivationFactory`, which creates nothing and so cannot disturb a prefix —
says what the rest of the near path looks like. Measured in a prefix built by
`phase3/scripts/build_xamlcore.py`, against wine-11.13:

| class | result | who owns it |
|---|---|---|
| `Windows.UI.Xaml.Application` | `S_OK` | phase 3 |
| `Windows.UI.Xaml.DurationHelper` | `S_OK` | phase 3 |
| `Windows.UI.Core.CoreWindow` | `S_OK` | Wine |
| `Windows.System.DispatcherQueueController` | `S_OK` | Wine, `coremessaging.dll` |
| `Windows.System.DispatcherQueue` | `REGDB_E_CLASSNOTREG` | **Wine gap** |
| `Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider` | `REGDB_E_CLASSNOTREG` | the muxc port |
| `Microsoft.UI.Xaml.Controls.XamlControlsResources` | `REGDB_E_CLASSNOTREG` | the muxc port |
| `Windows.UI.Xaml.Hosting.WindowsXamlManager` | `REGDB_E_CLASSNOTREG` | phase 3 |
| `Windows.UI.Xaml.Hosting.DesktopWindowXamlSource` | `REGDB_E_CLASSNOTREG` | phase 3 |
| `Windows.UI.Xaml.Window` | `REGDB_E_CLASSNOTREG` | phase 3 |
| `Windows.UI.Xaml.ResourceDictionary` | `REGDB_E_CLASSNOTREG` | phase 3 |

`Windows.System.DispatcherQueue` is the interesting one. Wine's
`coremessaging.dll` registers `DispatcherQueueController` and not the
`DispatcherQueue` beside it, so `DispatcherQueue.GetForCurrentThread()` —
which `App::Initialize` calls, and needs to answer *null* — throws instead.
It cannot honestly be supplied from outside CoreMessaging: the thread-to-queue
association lives inside the component that creates it, and a class that
always answered null would be right today and wrong the moment a controller
existed. That is a Wine patch, not a XAML one.

So two blockers stand between the current frontier and the next class phase 3
owns: the muxc metadata provider, and that Wine gap.

### Reading a frontier that did not move

An unchanged `first_missing_class` after a real implementation landed is
information, not a failure: it means the class was registered but something
before it now fails differently. `crash` is the field to read then — a
`winrt::hresult_class_not_registered` is a missing class, anything else is a
method that answered and should not have, or refused and should not have.
