# DirectComposition surface-handle boundary

This note records the OpenTerminal composition-surface boundary against Wine
commit `af5241854c513c2e68938425cc6cd3cac40b943a`. The corresponding Windows
Terminal source snapshot is pinned at
`e74649d5f18b1c123556461b30f763407aea558f`.

Windows Terminal's Atlas renderer creates a `Composition` handle with
`DCompositionCreateSurfaceHandle(COMPOSITIONSURFACE_ALL_ACCESS, nullptr, ...)`
and passes that handle to
`IDXGIFactoryMedia::CreateSwapChainForCompositionSurfaceHandle`. Microsoft UI
Xaml's SwapChainPanel test helper uses the same pair of APIs, with
`GENERIC_ALL` access.

The Wine foundation implements the object-manager and DirectComposition import
halves of that contract. `dcomp.dll` creates a server-owned object whose native
type name is `Composition`; the handle has the Composition generic-access
mapping and normal duplication, inheritance, and close semantics. A version 2
desktop composition device can validate a read-capable Composition handle and
retain it in an identity-only content wrapper. Closing the caller's handle does
not invalidate that wrapper. Both server requests are appended to
`server/protocol.def`, so all pre-existing request numbers stay stable.

Wine's focused `dlls/dcomp/tests/dcomp.c` regression verifies the object type,
granted access, duplicate lifetime, inheritable creation, zero-access creation,
device interface identity, import access/type validation, and retained import
lifetime. OpenTerminal's
[`external_surface_binding_test.cpp`](../../../phase3/xamlcore/client/external_surface_binding_test.cpp)
independently verifies that the Xaml-side binding duplicates a supplied handle
transactionally and never borrows its lifetime.

This is not yet a visible SwapChainPanel implementation. The imported object
intentionally exposes no drawing surface and `Commit` reports `E_NOTIMPL`:
there is no GPU resource associated with the server object. The next required
Wine boundary is `IDXGIFactoryMedia` plus
`CreateSwapChainForCompositionSurfaceHandle`, followed by a retained
DirectComposition graph publication path that presents the swap chain with
Xaml sibling ordering, transform, clip, opacity, and lifetime semantics. Treating the
handle as an HWND, taking a one-frame copy, or flattening it into the CPU Xaml
surface would violate that boundary.

The non-rendering retained-graph foundation now also creates desktop HWND
targets and version 2 visuals. Before commit it validates single-parent/root
ownership and cycles, preserves sibling order, retains imported-handle content,
and records offsets, matrix transform, rectangle clip, interpolation, border,
composite, opacity, and back-face modes. Graph references are internal and do
not alter the externally reported COM reference counts. `Commit` deliberately
continues to fail until the DXGI broker and compositor publication can publish
one complete transaction; local mutation alone is not presented as a commit.
