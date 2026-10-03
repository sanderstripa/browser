# Settings overlay hosting

Settings keep their existing card home, section rail, search, controls, native
backends and editing transaction. They are no longer a Tab. The trusted
`settings.html?host=overlay` document uses a dedicated `kSettings` CEF browser,
reserved session identity and normal active-profile request context. Opening
from incognito leaves the private tab selected and edits the ordinary profile.

All ordinary entry points converge on `OpenSettingsOverlay`. Navigation,
NewTab and popup routing intercept the canonical Settings route before it can
replace a webpage or enter `tabs_`. Repeated opening focuses the existing
instance and preserves its draft. Profile changes reset the same editor and
close sensitive subviews. Staging rejects snapshots from another profile.

## Native composition

`SettingsOverlay` owns a captionless tool HWND whose Windows owner is the
current Soulu HWND. It is not a separate workspace: no taskbar entry, independent
placement, drag, saved desktop coordinates or independent maximize behavior.
Bounds are always the owner's client rectangle, mapped into screen coordinates
only at the Win32 boundary. The dedicated DWM host is necessary for
`CreateHostBackdropBrush` to sample the live native CEF children and layered
OSR toolbar behind it; applying the browser's matte brush to its main HWND
would instead sample the desktop behind Soulu.

The existing Windows.UI.Composition backdrop implementation is reused with an
independent state for this host. Its GPU-composed opacity increases the visible
blur contribution. No screenshots, bitmap readbacks, CPU blur or background
browser recreation are part of the production path. The browser remains mapped
and media is not paused. Screenshot capture exists only in verification.

The sharp Settings CEF child is placed above the backdrop. Its maximum width
is 1020 DIP, maximum height 800 DIP, capped by current client bounds. X is
half the remaining width, final Y is zero. Lower corners are clipped; the top
has no separate panel radii or gap. The external host follows the owner's
normal/maximized silhouette.

## Transitions, input and editing

A common 16 ms Win32 timer drives both native panel offset and Composition
backdrop opacity from one monotonic clock. Normal duration is 260 ms, with
cubic-bezier(.22,1,.36,1). The opening panel starts outside the clipping bounds;
opening waits for the initialized frontend's rendering frames. Closing moves
it upward while fading the blur contribution. CEF and the native host are
released only after that transition. Windows client-area animation preference
selects a 90 ms transition with substantially reduced motion.

Background child HWND input is disabled immediately, with previous enabled
states restored after close. Background CEF shortcuts are consumed while the
host exists. Backdrop clicks only refocus Settings. Keyboard focus moves into
the Settings browser; Tab/Shift+Tab wrap within the current workspace or modal
subview. Ctrl+F focuses Settings search; Ctrl+W/Escape use existing dirty-close
logic. Closing restores the saved live HWND focus target or the current browser.

Apply preserves its existing save/partial-failure/retry model and stays open.
The Cancel button rolls back staged preview, then requests animated close.
Save/Discard/Keep editing confirmations retain the backdrop until a successful
save or discard. Backend preview rollback occurs while blur and input blocking
are still active. Password/import/site exceptions remain existing dialogs
inside the Settings document. Site-data actions now address a validated target
tab directly, without switching selected tabs just to return to Settings.

## Verification

The Windows build workflow retains native backend, History, Settings migration,
profile/security, Home, bookmarks, Reader, navigation, storage, content-policy,
layout and installer checks. `test-cef-settings-overlay.py` adds composed-window
blur measurements, real Win32 focus and mouse checks, live DOM/scroll/history
and video preservation, geometry checks, device scales through 200%, and thirty
open/close cycles with target lifecycle and host CPU/time evidence. Browser
screenshots from CDP alone cannot establish cross-HWND blur, so that assertion
uses actual composed screen pixels.

Physical Windows DPI switching, multiple physical monitors, visual artifact
review and public-site behavior must be reported according to actual available
runtime evidence; emulation is never described as a physical OS DPI change.
CEF and Chromium remain the pinned distribution and are verified by the existing
engine-version check before packaging. Release publication requires final source
HEAD to equal origin/main.
