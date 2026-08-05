# WinUI 2.8.4 theme resources

- Upstream repository: <https://github.com/microsoft/microsoft-ui-xaml> (MIT)
- Upstream commit: `4aa80ad6d272241a6a603f85507063e9fb6bcf92`, tagged 2.8.4
- Read by: [`phase3/scripts/extract_winui_theme_resources.py`](../../../phase3/scripts/extract_winui_theme_resources.py)

Nothing from this repository is committed here. The extractor's output is
several megabytes of somebody else's content and is materialized at build time
under `phase3/xaml-db/theme-resources/`, on the same rule as the case corpus:
the script and the pinned commit are the source. What is written down below is
*facts about* that source — counts, and the names of keys Terminal asks for.

## Why the dictionary has to be reconstructed

WinUI 2's theme dictionary does not exist as a file in the tree. It is merged at
build time out of ~120 per-control XAML fragments by
`tools/CustomTasks/BatchMergeXaml.cs`, and the merge is not a union:

- **order decides.** `dev/dll/Microsoft.UI.Xaml.Common.targets` sorts the pages
  by a `Priority` metadata value and then by target OS version, and a later
  entry replaces an earlier one with the same key. Several fragments define the
  same key; unioning them picks an answer at random.
- **there are two flavours.** Each control ships a `_v1` (pre-Fluent) and a
  default XAML, selected by `ControlsResourcesVersion`. From WinUI 2.6 on the
  default is `Version2`, which is what Terminal gets.
- **each OS version is a separate output, layered.** `RS1` through `21H1`, each
  merged on top of the previous one's result. `21H1` is what a current Windows
  loads.
- **conditional XAML is resolved per API contract.**
  `tools/CustomTasks/StripNamespaces.cs` deletes elements and attributes in an
  `IsApiContractNotPresent(...)` namespace that the target contract satisfies.
  At `21H1` (contract 14) that removes 22 keys the fragments otherwise define.

The extractor reimplements all four rules. That is the bulk of it; parsing the
XAML afterwards is the easy part.

## What comes out, at 21H1 / Version2

| dictionary | keys | with a literal value | brushes | aliases | opaque |
|---|---:|---:|---:|---:|---:|
| theme-independent | 628 | 427 | 0 | 0 | 201 |
| `Default` (= `Dark`) | 2,224 | 340 | 438 | 1,404 | 42 |
| `Light` | 2,224 | 340 | 438 | 1,404 | 42 |
| `HighContrast` | 2,205 | 342 | 577 | 1,281 | 5 |

An *alias* is `<StaticResource x:Key="A" ResourceKey="B"/>`, which is most of
the dictionary — WinUI 2.6 renamed the whole palette and kept the old names
pointing at the new ones. Following the chains and the brushes' `Color`
references, the effective `Light` dictionary of 2,852 keys is:

| what it lands on | keys |
|---|---:|
| a literal value | 802 |
| a brush with a colour we can read | 1,417 |
| a brush whose colour is the OS's (`SystemAccentColor` and friends) | 233 |
| a `Style`, `ControlTemplate`, `Storyboard`, converter | 343 |
| a dangling reference (the key it names is the OS's) | 57 |

`Default` and `Light` are, for anything a layout measurement can see, the same
dictionary: they differ in 106 `Color` values and in exactly one other value,
`InfoBadgeIconHeight` (8 against 9). `HighContrast` differs in 31 thicknesses
and sizes as well, so it is a genuinely different measurement.

`HighContrast` is also much less complete once the chains are followed — 192
brushes with a readable colour against `Light`'s 1,417, and 466 dangling
references against 57. That is not damage: high contrast is *defined* as
deferring to the OS's `SystemColor*` palette, which is the part no source tree
has. The number is what that deferral looks like from here.

## The split that matters

Terminal's markup makes 1,565 resource references naming 395 distinct keys. 208
of those it defines itself. Of the **187 it names and never defines anywhere**:

| where the key comes from | keys | share |
|---|---:|---:|
| WinUI 2.8.4, with a value this project can use | 129 | 69% |
| WinUI 2.8.4, but as a `Style` or a brush on an OS colour | 30 | 16% |
| not in the WinUI 2.8.4 source at all | 28 | 15% |

So the premise this work started from — that Terminal's undefined keys are the
OS's closed `generic.xaml` — is wrong by a wide margin. **85% of them are in the
open WinUI 2 source.** The closed remainder is 28 keys, and they fall into three
recognisable groups:

**The OS palette (18 keys).** `SystemAccentColor`, `SystemAccentColorDark2`,
`SystemAccentColorLight1`, `SystemAccentColorLight2`, `SystemAltMediumLowColor`,
`SystemBaseHighColor`, `SystemBaseMediumColor`, `SystemColorButtonFaceColor`,
`SystemColorButtonTextColor`, `SystemColorHighlightColor`,
`SystemColorHighlightTextColor`, `SystemColorWindowBrush`,
`SystemColorWindowTextColor`, `SystemErrorTextColor`, `SystemListLowColor`,
`SystemListMediumColor`, `SystemControlBackgroundAltHighBrush`,
`SystemControlBackgroundBaseLowBrush`. These are user-and-system dependent by
design — the accent colour is a personalisation setting — which is exactly why
they are not in any source tree. They can only be read from a running system.

**Legacy `SystemControl*` brushes (3 keys).**
`SystemControlForegroundBaseHighBrush`, `SystemControlForegroundBaseMediumBrush`,
`SystemControlForegroundBaseMediumLowBrush`. WinUI 2 redefines most of this
family and not these three.

**Framework resources WinUI 2 leaves to the OS (7 keys).**
`AppBarItemBackgroundThemeBrush`, `ColorPickerColorButtonStyle`,
`ContentControlThemeFontFamily`, `ControlContentThemeFontSize`,
`FlyoutThemeMinWidth`, `SharedShadow`, `SymbolThemeFontFamily`.

`SymbolThemeFontFamily` is worth naming on its own: 47 references, the most of
any key in this list, and it is a font family — a value a measurement can
actually see, unlike the colours above.

### Harvesting the OS half

Not implemented here, and it needs no source at all: the 28 keys are readable at
runtime on the Windows runner, which is where the probe already is. The route,
in order of how little it disturbs what exists:

1. a **new probe mode** — `xaml_probe --dump-application-resources <out.json>` —
   that starts XAML the way it already does, walks
   `Application.Current.Resources` and its `ThemeDictionaries`, and writes
   key → type → value for everything with a textual form. It is a read, so it
   can be a separate step in the workflow that no existing step depends on;
2. the output recorded as **another measurement**, not as repository content:
   it is a fact about that runner's Windows build and personalisation settings,
   it moves when either does, and the oracle digest is the mechanism that
   already exists for exactly that;
3. `SystemAccentColor` and the `SystemColor*` family should be **pinned or
   excluded**, not recorded blind. They depend on the signed-in user's theme,
   so a corpus that resolved them would be reproducible only on that machine.
   The honest options are to set `RequestedTheme` and a fixed accent in the
   probe, or to keep them in the blocked set permanently and say why.

Step 1 answers a question that is open regardless: whether a bare
`XamlReader.Load` under `WindowsXamlManager` reaches `Application.Resources` at
all, and whether WinUI 2's dictionary is in there or only the OS's. The
`L5-theme` cases in the corpus ask exactly that, and their answer decides
whether the 21 harvested cases this dictionary unblocked can be measured.
