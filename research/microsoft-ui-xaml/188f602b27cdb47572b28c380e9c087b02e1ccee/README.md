# The system `generic.xaml`, and the default-style layer under everything

- Upstream repository: <https://github.com/microsoft/microsoft-ui-xaml> (MIT)
- Upstream commit: `188f602b27cdb47572b28c380e9c087b02e1ccee`
- Read by: [`phase3/scripts/extract_default_styles.py`](../../../phase3/scripts/extract_default_styles.py)

Nothing from this repository is committed here. The extractor's output is
several megabytes of somebody else's content and is materialized at build time,
on the same rule as the case corpus and the WinUI 2 theme extract: the script
and the pinned commit are the source. What is written down below is *facts
about* that source.

This is the commit wave 1 already named as the algorithm reference of record --
it publishes the XAML core under `dxaml/xcp/`, which is the code lineage the
oracle runs. What this track found in it is a second thing: the system
`generic.xaml` itself.

## What is in there

    dxaml/xcp/dxaml/themes/generic.xaml     23,820 lines, 2.0 MB

One file, and its own closing comment says what it is:

    <!-- End Windows.UI.Xaml.Controls.dll resources - DO NOT MANUALLY EDIT ABOVE THIS LINE! -->

Unlike WinUI 2's theme dictionary there is no merge to reimplement. The build
only *splits* it -- `themes/autogen/SplitGenericXaml.cs` cuts it into
`Styles.xaml` and `ThemeResources.xaml` and `GenAllXbf.csproj` compiles both to
XBF -- so reading the file is reading what ships.

| what | count |
|---|---:|
| theme dictionaries (`Default`, `HighContrast`, `Light`) | 3 × 1,868 keys |
| theme-independent keys | 170 |
| implicit styles (no `x:Key`, keyed by `TargetType`) | 66 |
| keyed styles | 62 |
| setters across the implicit styles | 539 |
| — carrying a literal | 267 |
| — carrying a `{StaticResource}`/`{ThemeResource}` | 208 |
| — carrying an object with no textual form | 64 |

## What it closes

`../4aa80ad6d272241a6a603f85507063e9fb6bcf92/README.md` counted 28 keys that
Terminal names, never defines, and that WinUI 2.8.4 does not have either, and
said they could only be read off a running system. **Sixteen of them are here**,
in open MIT source:

`AppBarItemBackgroundThemeBrush`, `ContentControlThemeFontFamily`,
`ControlContentThemeFontSize`, `FlyoutThemeMinWidth`, `SymbolThemeFontFamily`,
`SystemAltMediumLowColor`, `SystemBaseHighColor`, `SystemBaseMediumColor`,
`SystemControlBackgroundAltHighBrush`, `SystemControlBackgroundBaseLowBrush`,
`SystemControlForegroundBaseHighBrush`, `SystemControlForegroundBaseMediumBrush`,
`SystemControlForegroundBaseMediumLowBrush`, `SystemErrorTextColor`,
`SystemListLowColor`, `SystemListMediumColor`.

The twelve that remain are the ones that genuinely cannot be in any source
tree, plus two that are simply not in this file:

- **the personalisation palette (10).** `SystemAccentColor`,
  `SystemAccentColorDark2`, `SystemAccentColorLight1`,
  `SystemAccentColorLight2`, `SystemColorButtonFaceColor`,
  `SystemColorButtonTextColor`, `SystemColorHighlightColor`,
  `SystemColorHighlightTextColor`, `SystemColorWindowBrush`,
  `SystemColorWindowTextColor`. The accent colour is a setting and the
  `SystemColor*` family is the OS's high-contrast palette; both are facts about
  a signed-in user, which is why no tree has them.
- **`SharedShadow` and `ColorPickerColorButtonStyle` (2).** Neither is in this
  `generic.xaml` nor in WinUI 2.8.4. They stay named rather than guessed.

## The caveat, stated once

This is the **WinUI 3 lineage** of the system `generic.xaml`, not a byte-for-byte
copy of the `Windows.UI.Xaml` that build `10.0.26100.33158` loads. It is the
same authorship and the same file in the same place in the same tree, which is
the best open source there is for it -- and it is not a measurement.

The corpus decides. Running the reconstruction against the recorded oracle found
two places where it and the recording disagree, and in both the recording won
and the reconstruction stayed out:

- **`Button.Padding`.** `L7-terminal-0e66f8e18d` records a bare `Button`
  desiring `[20, 32]`; applying `ButtonPadding` gives `[42, 32]`. A Control's
  `Padding` is consumed by the `ContentPresenter` its template puts inside it,
  and the recorded trees have no template.
- **`XamlAutoFontFamily`.** `ContentControlThemeFontFamily` is literally that
  string, and it is a sentinel for the system UI font rather than a family.
  Handing it to the font library fails every element that inherits it.

Both are held by name in `phase3/layout/src/markup.cpp`, each carrying the case
that refuses it, and both are now authored oracle questions
(`L5-defaults-autofontfamily`, `L5-defaults-builtin-reachability`).

## The layer this file occupies

Bottom. `Application.Resources` is a stack, and a lookup walks it from the top:

| layer | what | source |
|---|---|---|
| `XamlControlsResources` | WinUI 2's merged theme resources | `4aa80ad6`, via `extract_winui_theme_resources.py` |
| `GlobalThemeResources` | this file's theme dictionaries | `188f602b`, via `extract_default_styles.py` |

56 layout-visible keys are defined by both and disagree -- `ButtonPadding` is
`8,4,8,5` here and `11,5,11,6` in WinUI 2, `AppBarThemeMinHeight` is 56 here and
64 there -- so which layer answers is not a detail. `L5-defaults-layer-order`
asks the oracle to settle it; `dev/dll/XamlControlsResources.cpp` says the
merged dictionary is on top, and that is what is implemented.
