# Toolbar geometry and client-area surfaces

This change starts at `04527a8130850d6ece8e9e7b1601fa95aca1ecdf` (Beta 1.0.51).
It retains CEF `154.0.33+ga03e714+chromium-154.0.8037.94`, bundled Onest,
the existing motion tokens, tab model and native content insets.

## Inventory and policy

`ui/index.html`, `renderer.js` and `navigation.js` own all current toolbar
controls. There is no separate native toolbar icon renderer. The actual set is:

* Main and Classic Sidebar, Back, New Tab and Tab Overview;
* four conditional VPN slots (left/right in both modes), two conditional
  download controls, download progress and badge states;
* Site Info/menu, bookmark star, Reload/Stop and Close active tab in both modes;
* dynamically created Classic tab close actions;
* Minimize, Maximize/Restore and Close caption controls.

The current toolbar has no standalone Forward, Home, profile, Reader, search
provider or security action buttons. Those features use other existing entry
points. Favicons, internal Settings identity and the VPN extension mark remain
identity assets; they are not redrawn as action glyphs. Inactive compact tabs
remain tab identity targets, preserving their current tab layout.

`ui/toolbar-ui.css` is the shared action geometry source. A regular action uses
a 30×30 DIP target and hover/pressed surface, 16×16 DIP glyph viewport, 50%
radius, zero outer padding and seven DIP between a centered glyph viewport and
the target edge. SVG stroke is one CSS pixel/DIP with non-scaling stroke,
round caps and joins, independent of the source 20/24-unit viewBox. Source
paths keep their original coordinate systems. Disabled opacity is .32 and the
focus outline is inset without changing layout. No per-icon offsets were added.
The original active blue and VPN connected states are retained. Download
progress continues using its existing animated accent arc.

Tab Overview now receives the ordinary action class, not the caption class,
and its viewport is 16×16 DIP instead of the later 20×20 override. Its tab
switching, thumbnail and close behavior are unchanged. The stop rect is shared
across the two address layouts. Classic tab close uses the same SVG as other
tab close actions, replacing a font multiplication glyph. All dynamic/hidden
actions inherit the same class policy when created or made visible.

The caption viewport is also 16×16 DIP. Minimize's horizontal line spans ten
DIP, Maximize's square nine DIP, Restore's combined bounds nine DIP and Close's
cross eight DIP. Stroke remains one DIP; the existing crisp half-pixel source
coordinates are retained. Full hit targets remain 42×48 DIP. A centered
pseudo-element draws the 30-DIP circle and never receives pointer events.
Neutral hover/pressed fill uses 8%/14% of the existing theme text color;
Close blends #ef4964 at 22%/34% with the existing solid surface. Caption
backgrounds stay transparent. There is no scale, glyph displacement,
rectangular destructive fill or traffic-light mapping.

## Divider sources

The bottom line came from repeated toolbar `border-bottom` declarations,
including the final `.browser-toolbar` rule. The optional bookmarks bar also
had a bottom border at the content edge. These declarations are removed,
not recolored. The right line came from `.compact-divider-window` markup/CSS
and `.window-controls`' left border; all are removed. No separator column or
spacer remains. Native content still starts at 48 DIP in Main, 82 in Classic,
plus 28 DIP when the bookmarks bar is visible. `CurrentGeometry` and the
existing layout regression suite verify the content/host boundary without a
one-pixel gap. The meaningful boundary between Classic's two rows is retained.

## Popup failure and repair

`ShellSurface` uses a contracted OSR viewport while no surface is open.
`position()` previously computed `min(anchor.bottom+8, innerHeight-80)` before
requesting full client height. A 48-DIP viewport therefore produced y=-32 and
a 50-DIP height limit. Focus could then scroll the document toward offscreen
content. Independently, bookmark transitions could call `setPopover(false)`
while a Site Info/Find surface remained open, contracting the host again.

Site Info/Find now acquire the native client-area surface before exposing or
focusing content, wait for the asynchronous viewport acknowledgement, and
position below the actual anchor. The bridge tracks ownership for independent
surfaces. `clientHeight` is reported in DIP; it is not confused with physical
HWND pixels. Max height is derived reactively in CSS from the real viewport;
long permissions content scrolls inside the panel. Resize repositions the
anchor. Reader remains backed by the existing native reader-active host flag;
its appearance panel has bounded scrolling, and Find follows the active
layout/bookmarks inset. Existing z-order and origin/security rules are retained.

## Native interaction

CEF OSR does not implement CSS `-webkit-app-region`. The old JS path started
native movement only on `.compact-drag-space` and `.classic-tabs-drag`, and
captured a pointer that Windows immediately took over. Classic's free top-row
space was never a drag target, and the residual tab-row space could become
very narrow.

Mouse-down now resolves the current toolbar surface and excludes buttons
(including disabled), inputs, forms, tabs, links, extension slots and bookmarks.
The native bridge releases capture and hands the gesture to Windows through
WM_NCLBUTTONDOWN/HTCAPTION using actual screen cursor coordinates. A double
click uses WM_NCLBUTTONDBLCLK. There are no cached drag rectangles or parallel
move/end workarounds. The parent WM_NCHITTEST still handles resize edges and
returns HTCLIENT for other areas; toolbar gestures are resolved by the OSR
surface's DOM. Aero Snap and the existing modal-loop CEF processing are kept.
The pre-existing OSR architecture does not provide HTMAXBUTTON or the Windows
11 hover Snap Layout flyout; this block does not add a new caption architecture.
WM_SIZE now emits state only when maximized status changes, updating the
Maximize/Restore glyph without re-rendering state during every resize tick.

## Evidence and practical limits

Native HTML select widgets use CEF's separate PET_POPUP paint stream. The
old shell discarded that stream, so nested permission/Reader controls could
open without visible options. ShellSurface now composites those premultiplied
frames over a saved main frame, clamps them to the client at the current DPI,
maps relocated mouse coordinates back to CEF, and restores the main frame
on dismissal. Resize invalidates stale popup buffers. The Reader suite opens
actual widgets and checks paint delivery, bounds and Escape dismissal in both
layouts; diagnostic counters are restricted to the existing UI test mode.
Unchanged site/Reader preferences no longer replace their controls on unrelated
state notifications, preserving focus and an already open native select.

`test-cef-toolbar.py` runs real CEF, checks computed button/glyph bounds,
caption default/hover/pressed surfaces, native maximize/minimize, Main →
Classic → Main mouse dragging, narrow/medium/large popup bounds, themes and
matte, and 100/125/150/200% CDP raster-scale emulation. Caption pseudo states
provide deterministic visual evidence without changing the production CSS.
`test-cef-site-reader.py` also checks real menu actions, nested permissions,
zoom/adblock rerenders, Find and Reader transitions in both layouts, alongside
existing extraction, permissions, persistence, sanitization and storage tests.
`test-cef-layout.py` retains exact HWND/content boundary and native modal resize
checks. CI builds the actual native application before these suites run.

CDP scales are not evidence of moving a window between physical monitors with
different Windows DPI. Authenticated Google/Ozon sessions, sustained public
YouTube playback and a working VPN require the user's profile/service access;
no result for those is inferred from a previous release or fabricated.
