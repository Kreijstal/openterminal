# XAML core — a WinRT DLL

`windows.ui.xaml.dll` as far as layout goes: real runtime classes, activated
through `RoActivateInstance`, reached by the same registry lookup and
`DllGetActivationFactory` call a WinUI application makes.

It exists because Wine's `windows.ui.xaml.dll` implements exactly one runtime
class — `Windows.UI.ColorHelper`, in 38 lines — and answers
`CLASS_E_CLASSNOTAVAILABLE` for everything else. Phase 2's own README names the
gap: the WinMD extraction "supplies compile-time ABI metadata, not the WinUI
runtime or its activation factories".

## What it implements

Six runtime classes, backed by [the layout core](../layout/):

| class | interfaces |
|---|---|
| `Windows.UI.Xaml.Controls.Border` | `IBorder` |
| `Windows.UI.Xaml.Controls.Grid` | `IGrid`, `IGridStatics` on the factory |
| `Windows.UI.Xaml.Controls.StackPanel` | `IStackPanel` |
| `Windows.UI.Xaml.Controls.ColumnDefinition` | `IColumnDefinition` |
| `Windows.UI.Xaml.Controls.RowDefinition` | `IRowDefinition` |
| `Windows.UI.Xaml.Controls.Primitives.LayoutInformation` | `ILayoutInformationStatics` |

Each element also carries `IDependencyObject`, `IUIElement` and
`IFrameworkElement`, and the panels carry `IPanel` — plus `IVector`,
`IIterable` and `IIterator` for the three collections. That is 203 methods
across 11 interfaces, of which roughly 30 do anything. **The rest return
`E_NOTIMPL`**, and that is the intended answer: there are no brushes here, no
transforms, no events, no property system. A caller needing one finds out by
being told so, not by receiving a plausible zero.

## Where it stands

Against build `10.0.26100.33158`, measured under Wine through the ABI:

| level | cases | matching |
|-------|------:|---------:|
| L0 | 4 | 2 |
| L1 | 72 | **72** |
| L2 | 192 | **192** |
| L3 | 132 | **132** |
| L4 | 72 | 0 |
| L7 | 69 | 0 |

Byte-identical to what `phase3/layout`'s own `measure_cases` produces without
any of this, which is the result worth having: the ABI wiring adds nothing and
loses nothing. The levels that are red fail as `the type 'TextBlock' is not
implemented`, not as wrong numbers.

## Running it

    python3 phase3/scripts/build_xamlcore.py

Fetches the pinned SDK, prepares its headers, generates, cross-compiles,
registers into a Wine prefix and measures — all under
`/tmp/openterminal-phase3-xamlcore`. Then:

    python3 phase3/scripts/fetch_measurements.py     # prints a directory
    python3 phase3/scripts/check_layout.py \
        --expected <that directory> \
        --actual /tmp/openterminal-phase3-xamlcore/results \
        --levels L1,L2,L3

Needs `x86_64-w64-mingw32-g++` and `wine`. No Windows.

## How the ABI is bound

Two things have to be exactly right, and neither is written by hand.

**Vtable order** comes from the SDK's own generated headers. Every class here
derives from an interface the SDK declares, so a method lands in the slot the
SDK says it does. `phase3/scripts/generate_abi_stubs.py` reads those headers
and emits one `E_NOTIMPL` base per interface; an implementation overrides what
it supports. A method typed in the wrong order would not fail to build — it
would shift every later slot and send the caller to the wrong function through
a correctly-typed pointer.

**IIDs** come from the pinned IDL, via `phase3/scripts/harvest_xaml_iids.py`.
GCC has no `__declspec(uuid)`, so `__uuidof` — the way the SDK's headers hand
out interface IDs — is unavailable. A mistyped IID also would not fail to
build: it produces a `QueryInterface` that answers `E_NOINTERFACE` for an
interface the object genuinely implements, which is indistinguishable from the
class not being implemented at all. Parameterized interfaces (`IVector<UIElement>`
and friends) have no IDL `uuid`; their IIDs are computed from the type
signature, and are read from the generated headers where the computed value is
written down.

`abi-interfaces.txt` and `iid-interfaces.txt` are the DLL's declared contract
surface. Adding a line claims a caller can `QueryInterface` for it and get an
object back.

## Making the SDK headers compile with GCC

`phase3/scripts/prepare_sdk_headers.py` builds a shadow tree. Four mechanical
rewrites, each because the SDK is MSVC-only source:

- **case.** The SDK is authored case-insensitively, so a header says
  `#include "AsyncInfo.h"` for a file named `asyncinfo.h`.
- **`typedef enum E : int E;`** — MSVC's opaque-enum-typedef extension.
- **`#pragma region` inside an enumerator list** — a folding marker GCC will
  not accept in that position.
- **`MIDL_CONST_ID IID& IID_IFoo = __uuidof(IFoo);`** — 866 of them in the
  controls header alone, each an unresolved symbol at link time under GCC.

Files needing no rewrite are symlinked, so it is obvious which were touched.
Nothing is committed; the shadow is build scratch over a `/tmp` payload.

## How it is checked

`client/measure_cases_winrt.cpp` builds every tree through the ABI —
`RoActivateInstance`, then interface calls for every property, including
`Grid.Row`/`Grid.Column` through `IGridStatics` — and reads every number back
the same way, including children through `IVector` rather than from the markup.
It writes what the oracle probe writes, so all three implementations compare
with the same `check_layout.py`.

It shares the markup parser with the layout core, which decides what a case
*says*, not what it measures. That parser was split out for this
(`layout/src/markup_tree.h`): one parse, two builders, so a disagreement
between them is necessarily in the ABI.

Two negative controls confirm the DLL is load-bearing rather than incidental —
the client links the layout core for the parser, so it is fair to ask whether
the numbers really came through the DLL:

| control | result |
|---|---|
| DLL moved aside | `RoGetActivationFactory` → `0x8007007e` (`MOD_NOT_FOUND`) |
| registration removed | `RoGetActivationFactory` → `0x80040154` (`REGDB_E_CLASSNOTREG`) |

Both stop the run before a single case is measured.

## Deliberate omissions

- **Not named `windows.ui.xaml.dll`.** It registers the real class names but
  ships as `openxaml.dll`, so it sits alongside Wine's builtin rather than
  replacing it. Dropping it in under the real name is a later, separate step.
- **No `XamlReader`.** Terminal ships compiled XBF, and text XAML parsing is
  the client's job here. Nothing in this DLL reads markup.
- **No property system.** `IGridStatics`' `*Property` getters stay `E_NOTIMPL`:
  returning a `DependencyProperty` would imply a system that does not exist.
  `Get`/`Set` are the whole of what the attached properties support.
- **No `IXamlMetadataProvider`, no XBF, no text, no rendering, no input.** This
  is the layer those sit on, not a shortcut past them.
- **The client uses the raw C ABI, not C++/WinRT.** Both sides therefore take
  their vtable layouts from the same SDK headers, so a header that was itself
  wrong would cancel out. The IIDs are independent — they come from the IDL —
  but a C++/WinRT client generated by `cppwinrt.exe` from the WinMD would be a
  genuinely independent check, and phase 2 already builds that compiler.
- **Floats.** `Size` and `Rect` are float-valued across the ABI while the
  layout core is double. Every L1–L3 value survives the round trip because
  layout rounding makes them whole numbers, but the narrowing is real.
