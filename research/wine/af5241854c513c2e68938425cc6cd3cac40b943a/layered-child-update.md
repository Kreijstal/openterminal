# Layered child-window rendering boundary

This note records the OpenTerminal desktop-island acceptance boundary against
Wine commit `af5241854c513c2e68938425cc6cd3cac40b943a` from
`Kreijstal/wine-kreijstal`. It contains no captured executable or image data.

## Boundary

OpenTerminal presents each immutable, premultiplied island frame to a
`WS_CHILD | WS_EX_LAYERED` window with `UpdateLayeredWindow(ULW_ALPHA)`. The
destination point is in screen coordinates, as required by the Win32 API. The
focused acceptance test is
[`phase3/render/gdi/island_frame_cache_test.cpp`](../../../phase3/render/gdi/island_frame_cache_test.cpp).
It verifies both an opaque source pixel and an alpha-zero pixel through the
real parent/child HWND relationship; the latter must reveal the parent's
existing pixel.

At the pinned Wine revision, `NtUserUpdateLayeredWindow` used the screen-space
destination directly in the parent-relative child window transaction. A child
whose parent client origin was `(x, y)` therefore moved by `(x, y)` twice. The
narrow Wine change maps the API destination from screen space to the child's
parent before calculating the transaction offset. A Wine `user32:win`
regression covers this independently of OpenTerminal.

## Remaining composition gap

Correct coordinates are necessary but do not make a normal X11 child window a
per-pixel-alpha surface. Wine's ordinary child HWNDs share their parent's
surface and clipping model. The X11 layered-window path instead creates a
separate ARGB/XRender surface suitable for top-level layered windows. Promoting
or reparenting a child to a root-level X window breaks the Win32 child
relationship and does not by itself define how alpha-zero pixels recover the
current parent contents across parent repaints, moves, sibling z-order changes,
and clipping.

Consequently the Wine regression keeps the combined opaque/transparent pixel
assertion under `todo_wine` while enforcing the screen-coordinate child
position normally. The correct follow-up belongs in Wine's parent-surface and
XRender composition design. OpenTerminal deliberately retains the real child
HWND and the documented screen-space API contract; an owned popup, shape mask,
hardcoded background, or parent readback is not an acceptable substitute.

## Wine call and data flow

The pinned source makes the missing boundary concrete:

1. `dlls/win32u/window.c:get_default_window_surface()` returns no independent
   surface for an ordinary child; its GDI output belongs to the ancestor paint
   surface.
2. `get_window_surface()` overrides that rule for `UpdateLayeredWindow` and
   asks the display driver for an independent alpha surface.
3. `dlls/winex11.drv/bitblt.c:X11DRV_CreateWindowSurface()` marks the child
   layered and calls `set_window_visual()` with the ARGB visual.
4. A child normally has `x11drv_win_data` but no `whole_window`.
   `dlls/winex11.drv/window.c:set_window_visual()` calls
   `create_whole_window()`, which always creates its X window under
   `root_window`. The child's premultiplied image is then copied to that new
   root-level window by `x11drv_surface_flush()`.
5. `server/window.c:get_top_clipping_window()` knows that the logical child is
   a separate paint surface, but the server reply exposes only one target
   `surface_win`. It does not expose an ordered composition graph.

A focused Xvfb trace confirms this path. The parent first receives an ordinary
X11 window surface. `UpdateLayeredWindow` then requests a second `layered 1`
surface for the logical child. The call succeeds and the opaque pixel is exact,
but the alpha-zero pixel becomes zero rather than the already-painted parent
pixel.

## Smallest correct implementation boundary

The smallest correct implementation is a retained compositor for paint
surfaces belonging to one logical top-level HWND. It cannot be implemented as
another direct `XPutImage` or `AlphaBlend` in the child flush: repeating a
partial-alpha update would blend over the previous result, and a later parent
flush would erase the child. It must keep base and layer pixels separate.

The first missing seam is between the server-owned HWND surface tree and the
display-driver presentation API. The existing
`include/wine/gdi_driver.h:client_surface` abstraction retains geometry for
OpenGL and Vulkan clients, but its `present(surface, hdc)` callback immediately
writes one surface into the top-level drawable. It provides neither ordered
layer enumeration nor an ancestor damage/recomposition callback. Its registry
is also process-local, while Win32 parent and child HWNDs may belong to
different processes.

A complete X11 implementation therefore needs the following data flow:

1. Keep a layered child as an offscreen premultiplied ARGB surface. Do not call
   `set_window_visual()` and do not create a root-level X window for it.
2. Publish a generation-qualified native layer resource for the owning
   top-level HWND. For X11 this can be a retained depth-32 Pixmap/Picture; its
   owning client must keep it alive. The Wine server must provide ordered,
   visible layer descriptors containing HWND, geometry, clip, opacity, damage,
   and lifetime generation, including cross-process children.
3. Give each top-level X11 window separate base and composed-output resources.
   For every damaged region, copy the uncomposited parent/ordinary-child base
   with XRender `PictOpSrc`, then traverse the server-provided layers from back
   to front with `PictOpOver`, and finally replace the corresponding output
   region. This makes repeated partial-alpha presentation stable.
4. Recompose on child publication, parent or ordinary-child surface flush,
   expose, move, resize, visibility, sibling z-order, clipping, and destruction.
   Remove a layer transactionally when its HWND or resource generation dies.
5. Preserve normal Wine input routing through the logical HWND tree; the
   offscreen layer is never an input or popup window.

Wine's Wayland driver provides the closest existing ownership model:
`wayland_win_data_create_wayland_surface()` assigns a real child a subsurface
role, `wayland_surface_make_subsurface()` keeps presentation independent, and
the Wayland compositor owns source-over and stacking. X11 has no equivalent
native alpha-child role, so the equivalent ordering and recomposition must be
owned inside Wine rather than inferred from an ARGB child X window.

The focused regression should leave `todo_wine` only until this graph exists.
Removing it after a one-frame opaque/transparent success would be premature.
Additional required cases are two identical partial-alpha updates, parent
repaint, child move/resize/hide/destroy, overlapping normal and layered
siblings, z-order changes, nested children, and a cross-process child.

## Addendum, 2026-08-10: the unmapped-child measurement, and what this note's failure mode actually needs

This section is appended by wave 7 track I. Nothing above it is changed; it
records what a later measurement showed about the boundary described above and
what a Wine change did to it. It contains no captured executable or image data.

### The two failure modes are distinct, and the note above is about only one

A GDI-only probe with no XAML measures both halves of a layered child's first
presentation. It creates a `WS_POPUP` parent, paints it, creates a
`WS_CHILD | WS_EX_LAYERED` child, publishes an 8x4 frame whose left half is
opaque `0xff908070` and whose right half is alpha zero with
`UpdateLayeredWindow(ULW_ALPHA)`, shows the child, and reads the screen without
dispatching a message. Run against Wine `8d664853bd5` before the change below,
on `Xvfb :122`:

| child created | opaque pixel | alpha-zero pixel |
| --- | --- | --- |
| `WS_CHILD` (not visible) | lost, reads `0x000000` | reveals parent, reads `0x332211` |
| `WS_CHILD \| WS_VISIBLE`  | reaches screen, `0x708090` | lost, reads `0x000000` |

So the failure mode this note records -- the alpha-zero pixel resolving to zero
instead of to the parent -- reproduces **only** when the child is created
`WS_VISIBLE`. It is not what the OpenTerminal island hits. The island's
`phase3/xamlcore/src/factory.cpp` creates its child hidden and presents on
commit, and on that path the alpha-zero pixel was already correct while the
**opaque RGB** was the thing being lost across the map.

Both modes disappear if the probe pumps messages before reading. Neither is a
missing pixel; both are a presentation that Wine completes only once an X11
`Expose` event has been dispatched, while Win32 presents a layered window
synchronously.

### The unmapped-child RGB loss, and its cause

An X11 window that is not mapped has no backing store: Wine's
`create_whole_window()` sets `attr.backing_store = NotUseful`, so the
`XPutImage` that `x11drv_surface_flush()` issues for a withdrawn window is
discarded by the X server. An ordinary window recovers from this because
showing it generates a `WM_PAINT`. A layered window cannot: its pixels exist
only in its window surface, `UpdateLayeredWindow` is allowed to publish them
before the window is ever shown, and no repaint would produce them again. A
trace of `NtUserUpdateLayeredWindow` followed by `ShowWindow` shows the surface
being correctly retained and reused across the map --
`get_default_window_surface: trying to reuse previous surface` -- and shows that
nothing re-presents it after `window_set_wm_state` requests `WM_STATE 0 -> 0x1`.
The only path that would have re-presented it is `X11DRV_Expose` ->
`NtUserExposeWindowSurface`, which requires the application to pump messages
first.

`dlls/winewayland.drv/wayland_surface.c` already solves the same class of
problem for its own driver, flushing the window surface once the initial
configure arrives because content published before it could not be flushed.

The Wine change that fixes this is `track/layered-first-frame` in
`Kreijstal/wine-kreijstal`:

* `winex11: Present a layered window surface when its window is mapped.`
  (`dlls/winex11.drv/window.c`, `X11DRV_WindowPosChanged`) -- when a window
  leaves `WithdrawnState` and carries a surface with an alpha mask, sync the
  map to the server and call `NtUserExposeWindowSurface()` so the retained
  surface is re-presented immediately.
* `user32/tests: Test that layered window contents survive being shown.`
  (`dlls/user32/tests/win.c`, `test_layered_child_window_show`) -- covers both
  rows of the table above.

Under the fixed build the unmapped-child row becomes opaque `0x708090` and
alpha-zero `0x332211` with no message pump, and
`phase3/render/gdi/island_frame_cache_test.cpp` stops skipping: its 32
map-survival checks are measurement-keyed on `survives_map()`, so they enforce
automatically. On stock Wine 11.13 the same binary still prints
`32 island frame cache check(s) skipped by name after measurement`.

### What is still open

The `WS_VISIBLE`-created row is unchanged and remains `todo_wine` in the new
Wine test. Its cause is the ordering inside `NtUserUpdateLayeredWindow`: the
child's X window is created and mapped by `apply_window_pos()` *before* the
frame is blended into the surface and the shape mask is applied, so the child
is first drawn as an opaque rectangle over the parent and only then reshaped.
Recovering the parent's pixel under the removed area is a repaint of the parent,
which Wine performs only when the resulting exposure is dispatched. Fixing it
properly is the retained compositor this note already describes, not another
direct blit; the existing analysis above stands unchanged.
