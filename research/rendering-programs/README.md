# Pinned rendering learning programs

This snapshot maps the eight small programs in the renderer learning sequence
to authoritative Microsoft source examples.  It does **not** claim that those
repositories implement OpenTerminal's architecture, and it does not vendor or
build their code.  The examples answer narrower questions: how a Windows
rendering resource is created, used, resized, layered or exposed through XAML.

## Exact sources

| key | upstream | commit |
|---|---|---|
| `classic` | <https://github.com/microsoft/Windows-classic-samples> | `d59e5f1dc9c768615e4e1ab1f0f009e6a3ed747c` |
| `universal` | <https://github.com/microsoft/Windows-universal-samples> | `4eb2fcb499c5bc549e918920cfd2b64396a650d9` |
| `composition` | <https://github.com/microsoft/WindowsCompositionSamples> | `92e5ee73e78ff81bfe46b8045f50c948dc55d6e1` |
| `composition-win32` | <https://github.com/microsoft/Windows.UI.Composition-Win32-Samples> | `ee50e2ea137dcef7b82ba504eff7435e5ebf5294` |

All four repositories state the MIT license.  Their selected license files
are included in the materialized bundle.  The committed `manifest.json` is the
selection; `inventory.json` records the hash, size and line count of every
selected file.

## The eight-program sequence

| program | upstream evidence | OpenTerminal-specific work |
|---|---|---|
| `d2d_window` | Direct2D Hello World: HWND target, resource lifetimes, resize and repaint | reduce it to one rectangle and add pixel read-back |
| `d2d_display_list` | Direct2D Hello World plus SimpleDirect2DApplication device-resource separation | consume the existing `DisplayList`; do not copy Microsoft's object structure |
| `d2d_tree` | `CompositionVisual` and BasicLayoutAndTransforms | translate our arranged tree into offsets, transforms, z-order and clips |
| `d2d_alpha` | `CompositionVisual`'s inherited subtree opacity | author two deterministic overlapping rectangles and record exact pixels |
| `dwrite_text` | DirectWrite SimpleHelloWorld and its `IDWriteTextRenderer` example | feed the glyph run positions already produced by OpenTerminal layout |
| `dcomp_window` | the small `Windows.UI.Composition` Win32 HelloComposition host | attach the OpenTerminal display list surface and expose commit boundaries |
| `mini_xaml` | a minimal UWP `UserControl` plus XAML/Composition interop | parse one `Border`, measure, arrange and emit one display operation |
| `nested_xaml` | nested UWP `Grid`/`StackPanel` markup and XAML/Composition interop | preserve parent offsets, clipping and paint order through the whole pipeline |

The last column is intentionally not harvested from Microsoft.  Those are our
small programs and therefore belong in `phase3/render`; the upstream sources
are evidence and API instruction, not code to paste into the runtime.

## Reproduce

Clone the four repositories at the exact commits above under `/tmp`, then run:

```sh
python3 -B phase3/scripts/harvest_rendering_samples.py \
  --repo classic=/tmp/openterminal-Windows-classic-samples \
  --repo universal=/tmp/openterminal-Windows-universal-samples \
  --repo composition=/tmp/openterminal-WindowsCompositionSamples \
  --repo composition-win32=/tmp/openterminal-Windows.UI.Composition-Win32-Samples \
  --output /tmp/openterminal-rendering-samples \
  --expect research/rendering-programs/inventory.json
```

The command refuses a checkout at a different commit, an untracked path, a
binary input, or a non-empty destination.  UTF-8, UTF-8 with a BOM and the
Windows-1252 encoding used by an older DirectWrite readme are identified in the
inventory without rewriting the upstream bytes.  Its output contains only
textual source and deterministic JSON; building remains a separate activity
whose artifacts must stay under `/tmp`.
