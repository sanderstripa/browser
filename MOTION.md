# Soulu motion language — Beta 1.0.50

Motion explains structural changes. The existing CEF engine, Settings card home,
settings transaction, backend and native overlay are retained.

## Architecture

Before this change, the tab panel had a generic CSS transition, but navigation
intercepted its buttons for bookmarks and hid the panel. Settings replaced its
card grid with a section synchronously. The native Settings overlay already
used a monotonic timer and Windows Composition backdrop with an upward exit.

`ui/motion.js` now owns bounded animation transactions. Each transaction cancels
its Web Animations and removes its visual layers on completion or interruption.
The latest navigation intent takes effect immediately; a second transition first
settles the previous destination. Resize, page teardown, search, theme edits,
profile changes and close settle temporary layers. There is no new UI framework.

`ui/motion.css` owns web timing/easing tokens read by `ui/motion.js`; native
`cef/soulu/motion.h` supplies equivalent timing and ease-out for the existing
overlay. Keep these small cross-runtime token sets synchronized.

| Class | Duration | Easing |
| --- | --- | --- |
| Micro | 140 ms | deceleration |
| Surface | 180 ms | deceleration |
| Structural Settings | 260 ms | cubic-bezier(.22,1,.36,1) |
| Tab panel enter | 200 ms | cubic-bezier(.22,1,.36,1) |
| Tab panel exit | 180 ms | cubic-bezier(.4,0,.6,1) |
| Tab panel content | 160 ms after 40 ms | deceleration |
| Reduced web motion | 60 ms | opacity only |
| Reduced native overlay | existing 90 ms | existing limited movement |

## Tab panel

The existing tab list is accessible through the tab-panel buttons again.
Favorites and the bookmarks bar retain their separate bookmarks entry points.
`bookmarksSidebarVisible` distinguishes their existing docked layout from the
tab panel. The tab panel overlays the page, using CSS transform and opacity.
The webpage HWND does not move or resize during tab-panel transitions. The
native OSR host remains mapped through exit with a generation-checked bounded
deadline, including close initiated by Escape or Tab Overview. Rapid toggles
retarget the CSS transition and invalidate older host deadlines.

Closing makes the real panel inert immediately and restores focus to its
visible toolbar button when focus was inside it. The panel's top edge follows
the actual compact/classic toolbar and bookmarks-bar height. Bookmarks' existing
276 DIP dock and regression tests remain separate.

## Settings card and section

Only a visible, user-activated category card supplies the forward shared source.
Its entire container and its icon tile are captured, including CSS-pixel bounds,
color and corner radius. After the existing render commits the destination, an
inert, aria-hidden fixed visual container morphs to the real active navigation
item. The icon stays inside that container. An outgoing content snapshot fades
while the navigation rail and section reveal synchronously with short offsets.
The real destination replaces the temporary layer when the transaction settles.

All Settings, Alt+Left and the existing API return use the inverse mapping from
the active navigation item to its card. Focus returns to that card after the
temporary visibility suppression ends, unless the user has focused another
control. The section retains the existing meaningful content focus target.
Native button Enter/Space activation uses the same category handler.

Search and programmatic/deep-link section opens use a section reveal without a
fabricated source. Offscreen cards do not supply a visible source. Bounds stay
inside one CEF document in CSS pixels; no native/DIP coordinate conversion enters
the shared-element calculation. Resize cancels rather than retaining stale bounds.
Reduced motion omits the spatial morph and uses a brief opacity transition.

## Verification and limits

`scripts/test-cef-motion.py` executes against the real Windows executable and
records each category's forward/reverse destination, focus, visual-only layers,
search/deep links, keyboard activation, rapid input, native resize/maximize,
themes, five emulated device scales, reduced motion, thirty sidebar cycles per
layout, stable webpage dimensions, repeated rAF pacing and post-GC retained heap.
The existing backend, overlay, native layout and other regression suites remain
mandatory. Evidence is attached to the Windows workflow.

Device-scale emulation is not physical OS DPI/multi-monitor verification.
Cloud rAF and CPU/GPU diagnostics are not a hardware performance benchmark.
Physical frame-artifact review, representative public-site media, real
authenticated sessions and configured VPN connectivity require their actual
runtime evidence before publication. The final-main packaging and manual
release gates remain enabled.

No new motion is added to Tab Overview, Reader, History, Home, site-info,
ordinary form controls or every popover. Those are possible future consumers of
the tokens, each needing its own scope and validation.
