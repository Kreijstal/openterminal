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

Seventy-one registered runtime classes, backed by [the layout core](../layout/)
and the desktop-bootstrap compatibility objects Terminal reaches while loading
its compiled UI. The authoritative class list is `RUNTIME_CLASSES` in
[`build_xamlcore.py`](../scripts/build_xamlcore.py); it includes:

- the layout and presentation controls (`Grid`, `StackPanel`, `Canvas`,
  `Border`, `TextBlock`, images, shapes, brushes, icons and scrolling);
- content, items, flyout and dialog controls, including stateful
  `ContentDialog`, `MenuFlyoutSubItem`, `TextBox` and `ListView` projections;
- Terminal's Microsoft.UI.Xaml controls (`TabView`, `TabViewItem`,
  `SplitButton`, `CommandBarFlyout`, `ProgressRing`, `XamlControlsResources`
  and `BitmapIconSource`);
- `Application`, `ResourceDictionary`, the metadata-provider bridge, desktop
  XAML hosting, dispatcher and resource-manager bootstrap classes; and
- value/helper surfaces such as `ValueSet`, colors, font weights, duration and
  grid length.

Elements carry the dependency-object, UI-element and framework-element ABI,
including the focus/navigation surface used through `IUIElement5` and the
XamlRoot/size surface used through `IUIElement10`. Collections expose their
vector, iterable, iterator and observable interfaces. The generated contract
currently covers 126 interfaces and 1,246 methods, with 177 harvested IIDs.
Unimplemented operations still return an explicit error instead of silently
inventing state.

`FontFamily` is there because `ITextBlock::put_FontFamily` takes an object, not
a string: there is no way to say "Segoe UI" through this ABI without a class to
say it with. It holds a name and nothing else.

## The boot surface

Two more classes are here for a different reason: they are what the real
`WindowsTerminal.exe` asks for, in that order, before it draws anything. They
are gated by [the boot frontier](../../phase4/), not by the corpus.

| class | interfaces | why it is here |
|---|---|---|
| `Windows.UI.Xaml.DurationHelper` | `IDurationHelperStatics` | a file-scope `Duration` in `Pane.cpp` — the first activation the host performs, during CRT static initialization |
| `Windows.UI.Xaml.Application` | `IApplication`, `IApplication2`, `IApplication3`, `IApplicationFactory` + `IApplicationStatics` on the factory | `runtimeclass App : Windows.UI.Xaml.Application` |

`DurationHelper` is complete. A `Duration` is Automatic, a TimeSpan or
Forever, and every operation is a case analysis over those three; the
semantics come from `dxaml/xcp/dxaml/lib/Duration_Partial.cpp` in the
published core rather than from a guess.

`Application` is the first *composable* class in the DLL, and the mechanism
matters more than the object. Terminal never activates an Application: a WinRT
class derives from another by composing it, so C++/WinRT's `Application` base
calls `IApplicationFactory::CreateInstance`, hands in itself as the
controlling outer, and takes back a non-delegating inner. The two are one COM
identity afterwards. Getting that wrong does not produce a wrong number — it
produces a reference count split in two, or a `QueryInterface` that recurses
between outer and inner until the stack ends.

The rules the implementation keeps, and why each one is not optional:

- every interface the inner hands out forwards all six `IInspectable` methods
  to the outer, so `app.as<IApplication3>()` and the derived object are the
  same identity;
- the outer is never `AddRef`'d — an inner holding a reference on its
  aggregator is a cycle neither can break;
- the inner resolves the composed interfaces itself instead of asking the
  outer, because forwarding a `QueryInterface` back to the aggregator that
  just forwarded it here is how an aggregation loops.

`Application.Current` is real: one process-wide pointer set at construction,
null with `S_OK` when no application was made, and a second application
refused — all three as `FrameworkApplication_Partial.cpp` has them.
`HighContrastAdjustment` round-trips from its `Auto` default. Everything else
on `Application` is `E_NOTIMPL` and stays that way until there is something
behind it: `Resources` has no application resource dictionary,
`DebugSettings` and `Exit` have no process lifetime manager, and the
suspend/resume events have nothing to raise them. `get_RequestedTheme` refuses
until something has put a theme, because before that the honest answer is the
*system's* theme, which this runtime cannot see.

## Where it stands

Against build `10.0.26100.33158`, measured under Wine through the ABI:

| level | cases | matching |
|-------|------:|---------:|
| L0 | 4 | 2 |
| L1 | 72 | **72** |
| L2 | 192 | **192** |
| L3 | 132 | **132** |
| L4 | 72 | needs the font metrics |
| L7 | 69 | 0 |

Byte-identical to what `phase3/layout`'s own `measure_cases` produces without
any of this, which is the result worth having: the ABI wiring adds nothing and
loses nothing. That was re-checked across the original layout surface after
`TextBlock` was added. The current native pass has no missing-type failures in
the pinned L7 corpus; its bare-checkout failures are missing harvested
text/icon font metrics. The old L7 expected snapshot still records the former
type failures and must be refreshed by the Windows oracle before numeric parity
can be claimed.

L4 needs Segoe UI's metrics, which are harvested on the Windows runner and are
not in the repository — see [the fonts directory](../xaml-db/fonts/). The DLL
is told where they are through `OPENXAML_FONT_METRICS`, because unlike the
layout core it has no corpus to find them beside; `build_xamlcore.py --fonts`
sets it. Without them the text cases fail naming the family they could not
find, and nothing else changes.

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

The build also compiles and runs `wave34_smoke.exe` after registration. It
activates the Wave 3/4 controls and bootstrap helpers through Wine,
independently queries their WinRT interfaces, constructs `Application`, checks
the metadata-provider bridge and duration helpers, exercises `ContentDialog`'s
completed async operation, round-trips text and automation state, queries the
newer UI-element interfaces, and activates both Windows and Microsoft
`BitmapIconSource`/menu/tab surfaces.

The portable XBF 2.1 reader turns node streams into an object graph and
`Application.LoadComponent` materializes that graph through the WinRT ABI,
including local Terminal types, component connections and resource
dictionaries. The native XBF test parses Terminal's shipped `App.xbf` and
`TermControl.xbf` as 2,830 nodes before the real executable is probed.

It also covers the boot surface, which the corpus cannot reach: the
`DurationHelper` cases including the 200ms span `Pane.cpp` builds and the
negative span it rejects, and — through a stand-in derived class that hands
itself in as the controlling outer — that the composed `Application` is a
single identity in both directions, that `CreateInstance` takes exactly one
reference on the aggregate and gives it back, that `Application.Current` is
null before, the application during, and null again after it dies, and that a
second application is refused. None of that is observable from an activation
call, which is why the smoke grew an outer object rather than another
`Activate<>` line.

## Deliberate omissions

- **Not named `windows.ui.xaml.dll`.** It registers the real class names but
  ships as `openxaml.dll`, so it sits alongside Wine's builtin rather than
  replacing it. Dropping it in under the real name is a later, separate step.
- **No `XamlReader`.** Text XAML parsing stays the client's job; nothing in
  this DLL reads markup as text. Compiled XBF it does read: `src/xbf.cpp`
  (the portable reader `Application.LoadComponent` materializes through) is
  what the shipped application actually hands the runtime. The layout core
  carries its own pair — `LoadMarkup` in `phase3/layout/src/markup.h` for
  text and `LoadXbf` in `phase3/layout/src/xbf_markup.h`, held to the text
  path by the genxbf equivalence gate — and unifying the two XBF readers is
  an open item, not an accident to preserve.
- **No font fallback, no shaping, no kerning.** `TextBlock` sums per-character
  advances out of the harvested metrics. That is what the corpus measures and
  no more: a script needing glyph substitution, or a pair the font kerns, is
  not handled and would measure wrong rather than fail. A character with no
  advance in the metrics does fail, by name.
- **No ABI `DependencyProperty` projection.** The native property system now
  has local, style, inherited and animation sources plus effective-change
  notifications. `IGridStatics`' `*Property` getters still stay `E_NOTIMPL`
  until that native identity is projected as a WinRT DependencyProperty.
- **Rendering is only a host bootstrap.** The desktop island paints its base
  background, but controls do not yet produce a complete visual tree and a
  `SwapChainPanel` retains the supplied chain without presenting Terminal's
  rendered frames.
- **Input and focus are compatibility surfaces.** Event subscriptions and the
  focus/navigation properties Terminal queries are stateful, but complete
  pointer, keyboard, routed-event and accessibility dispatch is Phase 4 work.
- **Some live relationships are placeholders.** Framework-element parent links,
  a real `XamlRoot`/`UIContext`, CoreDispatcher projection, scheduled XAML
  `DispatcherTimer` ticks and observable-vector change notifications are not
  complete. Popup and dialog state exists without full visual presentation;
  `ContentDialog.ShowAsync` currently completes deterministically with `None`.
- **XBF coverage follows Terminal's current streams.** The 2.1 reader and ABI
  materializer load the two real Terminal XBFs, but uncommon opcodes, deferred
  templates and richer custom runtime data still need tests before this can be
  called a general Windows.UI.Xaml implementation.
- **No `Microsoft.UI.Xaml` DLL, and so no activatable
  `XamlControlsResources`.** Terminal's binary asks for 57
  `Microsoft.UI.Xaml.*` names (`strings -el WindowsTerminal.exe`), and what
  those names *mean* is now reconstructed -- the theme resources, the default
  styles, and `XamlControlsResources`' merge semantics all live in the layout
  core and are gated by `default_styles_tests`. What is not here is a second
  DLL registering them as runtime classes.

  That is a deliberate order, not an oversight. `XamlControlsResources`
  decides which dictionary an application resolves against, and that decision
  is measurable through the corpus; activating it is measurable only through a
  boot, which is track H's gate and not this one's. Registering a class whose
  resolution had not been checked would be the wrong way round. The pieces the
  step needs are all in place: the muxc WinMD projections at
  `/tmp/openterminal-mingw/cppwinrt-winui`, `generate_abi_stubs.py`, and this
  script's own build/register/measure pipeline, which is where the second DLL
  should be grown rather than beside it.
- **The client uses the raw C ABI, not C++/WinRT.** Both sides therefore take
  their vtable layouts from the same SDK headers, so a header that was itself
  wrong would cancel out. The IIDs are independent — they come from the IDL —
  but a C++/WinRT client generated by `cppwinrt.exe` from the WinMD would be a
  genuinely independent check, and phase 2 already builds that compiler.
- **Floats.** `Size` and `Rect` are float-valued across the ABI while the
  layout core is double. Every L1–L3 value survives the round trip because
  layout rounding makes them whole numbers, but the narrowing is real.
