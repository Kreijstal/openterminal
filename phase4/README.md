# Phase 4: integration validation

Validate the complete Terminal UI, ConPTY-backed tabs, input, rendering,
accessibility, resource loading, and multiple console clients. General Wine
fixes should be split into upstreamable patches with regression tests.

The current MinGW-built `WindowsTerminal.exe` crosses the boot frontier under
Wine: a 20-second virtual-display probe reaches WinRT activation, creates the
desktop HWND/XAML island and remains alive until the timeout. The probe reports
no missing import or runtime class, OpenXaml `E_NOTIMPL`, XBF materialization
failure, C++ exception, or access violation. Reproduce that measurement with
`phase4/scripts/boot_frontier.py`; its committed expectation is
`expected/boot-frontier.json`.

This is a boot milestone, not complete UI validation. The shipped XBF object
graphs now materialize, but full control rendering, swap-chain presentation,
input, accessibility, live resource behavior and ConPTY-backed tabs remain the
Phase 4 integration surface.
