# WinUI 2.8.4 runtime reimplementation roadmap

The decision of record: reimplement the runtime fully, layer by layer, each
layer gated on oracle measurements recorded from the real `Windows.UI.Xaml`
on Windows CI. No approximations ship: a subsystem either matches the recorded
numbers or fails with a named error. Work proceeds in waves of parallel,
independently-testable tracks.

## Standing rules

- The corpus (`xaml-db`) is the arbiter. Recorded measurements are never
  edited; ambiguity becomes a new authored case for the next oracle run.
- Authored cases without recorded measurements are *pending*, never claimed
  as passing. Self-consistency checks (twin-pair diffs) are labeled as such.
- Algorithm reference, in priority order: `microsoft/microsoft-ui-xaml` at
  `188f602b` publishes the actual XAML core (MIT) under
  `dxaml/xcp/core/core/elements` — the code lineage the oracle runs, found
  during wave 1; dotnet/wpf (MIT, `2ca037562c`) remains the fallback for
  subsystems the dxaml tree lacks. The corpus stays the arbiter either way:
  wave 1 already recorded a case where the oracle contradicts the published
  Canvas source.
- Wrong numbers are worse than a named not-implemented error.
- Plain C++17 in the layout/runtime core; no Windows dependencies on the
  Linux test path.

## Waves

| wave | tracks | exit criterion |
|------|--------|----------------|
| 1 (running) | L0 property engine to 4/4 · L4 font pipeline + CI harvest · L7-blocking element types (ScrollViewer, ContentPresenter, Canvas, …) · L5 ResourceDictionary/StaticResource groundwork | oracle-gated counts rise; new pending corpora authored |
| 2 | oracle rerun over wave-1 pending cases · styles + ControlTemplate/`Setter` precedence · `{ThemeResource}` + theme dictionaries · x:-directive surface (x:Name, x:Uid, x:Load) | L5 measured; first templated control renders a template |
| 3 | `{x:Bind}`/`{Binding}` engine + INotifyPropertyChanged · VisualStateManager + storyboards sampled at t=0/t=end (L6) · default control styles (open generic.xaml reconstruction, oracle-diffed) | L6 measured; bound properties round-trip |
| 4 | framework control set by L7 unlock order (ListView/ItemsControl virtualization, Frame/Page navigation, Flyout/Popup) · muxc: WinUI 2 controls ported off C++/CX from the pinned MIT source | L7 subtree count climbs toward 69/69 |
| 5 | input routing/focus/gestures · automation peers (bridge to existing ConTypes UIA) · text editing (TextBox, IME via ConTSF) | interactive oracle cases (new probe capability) |
| 6 | render/composition backend (visual tree → swapchain; Wine-side) · DesktopWindowXamlSource island hosting · SwapChainPanel for TermControl | pixels on screen under Wine |
| 7 | XBF loader parity, PRI resolution, aggregate metadata provider → TerminalApp/Settings pages load end-to-end | Windows Terminal UI boots |

Waves 5–6 need new oracle *kinds* (input event traces, rendered-output
probes), not just more layout cases — extending the probe is part of those
waves, and the probe extension lands before the implementation it gates.

Implementation status as of the Wave-2 baseline: the native Wave-3 binding,
visual-state/storyboard and template mechanisms and the Wave-4 framework
control state machines are present and focused-tested. The Windows UI DLL
projects the framework controls through their SDK interfaces. The exit
criteria remain open until an L6 oracle is authored/recorded, reconstructed
generic.xaml is diffed, the muxc WinMD contracts are projected, and the L7
subtree count is remeasured; implementation status is not measurement status.

The subsequent Windows.UI.Xaml completion pass removes every named type
failure from the pinned L7 corpus and projects all 26 native markup/runtime
classes through the DLL. It adds ScrollViewer viewport state, FontIcon glyph
measurement/fallback, the remaining shape/control blockers, and focused native
and Wine ABI smoke coverage. This advances the implementation side of Waves 3
and 4; the unchanged exit gates above still require fresh L3-scroll/L4-icon/L6
oracle data, full generic.xaml reconstruction and muxc ABI projection.

## Repo split

The runtime may move to its own repository once it stands alone (roughly
wave 4+, when the control set outgrows "research"). Until then it stays in
phase3, where the corpus, harvest scripts, and CI gating live; extraction
later is cheap and preserves history.
