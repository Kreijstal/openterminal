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

## A live shell in TerminalCore

The boot frontier says the host process runs. It says nothing about whether a
shell is behind it. `scripts/conpty_live.py` answers that separately, and the
thing it measures is one PE the phase-2 build links,
`openterminal_conpty_terminal_core_gate.exe`
([`phase2/tests/conpty_terminal_core_gate.cpp`](../phase2/tests/conpty_terminal_core_gate.cpp)),
which is the only binary here that links both halves: the pseudoconsole from
`src/winconpty` and the terminal from `src/cascadia/TerminalCore`.

It creates a pseudoconsole, spawns `cmd.exe` into it, types
`set OTGATE=1138` and `echo CONPTY_GATE_%OTGATE%`, feeds every byte the
pseudoconsole emits through `til::u8u16` into `Terminal::Write` under the
terminal's own write lock, and then reads the answer back out of
`Terminal::GetTextBuffer` under `Terminal::LockForReading`. Neither typed line
contains `CONPTY_GATE_1138`; only the shell's own expansion of that variable
can put it on a row, so the console's input echo cannot satisfy the gate.

```bash
cmake --build /tmp/openterminal-mingw/native-build \
  --target openterminal_conpty_terminal_core_gate
python3 -B phase4/scripts/conpty_live.py \
  --executable /tmp/openterminal-mingw/native-build/openterminal_conpty_terminal_core_gate.exe \
  --prefix /tmp/openterminal-conpty/prefix
```

The gate is [`tests/test_conpty_live.py`](tests/test_conpty_live.py). Its
second case is a permanent negative control: the identical session with
`--ingestion none` reads the same bytes off the pseudoconsole and never writes
them to the terminal, and it must keep failing to find the marker. No display
is needed — the console host runs headless — so nothing here starts an X
server. The Wine prefix must be nested (`/tmp/openterminal-conpty/prefix`, not
`/tmp/prefix`): Wine refuses a prefix whose immediate parent it does not own.

### Two Wine defects the live session had to route around

Both are in Wine, not in Terminal, and both are reachable from any ConPTY
client — they are the reason `pty_path` in the report reads `pack` rather than
`create`.

| where | what happens |
|---|---|
| `dlls/ntdll/unix/file.c`, `nt_to_unix_file_name()` | `if (name_len && name[0] == '\\') return STATUS_INVALID_PARAMETER;` rejects a relative name beginning with a backslash before the wineserver sees it. `winconpty` opens the console reference as `"\Reference"` under the server handle, exactly as shipped, so `ConptyCreatePseudoConsole` fails with `HRESULT_FROM_NT(STATUS_INVALID_PARAMETER)` = `0xd000000d`. The server *does* implement the name (`console_server_lookup_name` in `server/console.c`); opening it as `"Reference"` succeeds. |
| `programs/conhost/conhost.c`, `main_loop()` | the console host arms its `--signal` read with an asynchronous `NtReadFile` before entering its loop. `winconpty` passes an anonymous pipe, which is synchronous, so that call blocks: the host stays alive, never serves the console and never writes a byte to the pseudoconsole. Wine's own `CreatePseudoConsole` avoids this by using an overlapped *named* pipe. |

The gate's `pack` path uses `ConptyPackPseudoConsole` — the same library's
handoff entry point — over a server handle, a `"Reference"` client handle and
an overlapped named signal pipe, and spawns the console host with
`winconpty`'s own `--headless --width --height --signal --server` command
line. `--pty-path create` demands the unmodified path and is expected to fail
until Wine is fixed; `--pty-path auto` prefers it and records in
`conpty_create_hresult` exactly how it refused.

A third difference is in the harness rather than in Wine: the shell is spawned
with `STARTF_USESTDHANDLES` and null standard handles. Without that, Wine
passes the harness's own (non-console) standard handles straight through to
the shell, whose output then never enters the pseudoconsole at all.
