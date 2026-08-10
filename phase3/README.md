# Phase 3: Wine bring-up

Run minimal Terminal components under Wine, record focused traces, and convert
missing behavior into isolated Wine tests and patches. Start with activation
and window creation before enabling the full renderer and console stack.

The native Windows workflow also harvests the renderer's stable oracle
boundaries—XAML pixels, effective visual geometry and public DirectWrite glyph
runs—from a small authored corpus. See [render/README.md](render/README.md#native-xaml-render-boundary-harvest).
