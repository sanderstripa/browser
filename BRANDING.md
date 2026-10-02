# Soulu identity and installer

The supplied October 2 icon and three-panel installer concept are the visual references.
This change is confined to identity, installer rendering and their build/verification
chain. Browser pages, navigation, storage and CEF are not redesigned.

## Production assets

`ui/branding/soulu-master.svg` is the newly drawn, editable vector source. Two
overlapping blue petals use smooth fills without a background, stroke, external
image, shadow or filter. Generated raster studies were rejected for edge artifacts;
no generated study or previous icon is part of the master.

Render this SVG at 4096px or above with an SVG renderer, then run
`python scripts/export-branding.py rendered-master.png` (Pillow required).
The exporter crops transparent master margins, fits the silhouette without changing
proportions, and leaves max(1px, round(size * 0.02)) safety margin on each side.
Positive-kernel, premultiplied-alpha resizing avoids color ringing.

PNG exports: 16, 20, 24, 32, 40, 48, 64, 128, 256, 512 and 1024. All six shipped
ICO aliases contain the same nine PNG-backed 32-bit frames through 256px.
`manifest.json` records their common SHA-256. All three legacy PNG aliases now
contain the same new 1024px export. The obsolete opaque JPEG is removed.
`browser-icon.svg` resolves the replaced `browser-icon.png`.

The main exe RC file embeds the canonical ICO. Both NSIS Icon and UninstallIcon,
the setup wrapper and two shortcuts use that same identity. The installer uses
`soulu-512.png` for its visible logo. The browser already requests the correct
large/small Windows icon sizes; the installer now does too.

Shortcuts use `soulu-icon-v44.ico` to avoid the v24 shell-cache entry. Installation
replaces all shipped aliases and the old v24 ICO's content, removes the JPEG, recreates
desktop/Start shortcuts and sends shell change notifications. Windows can retain
a separately pinned old shortcut or an icon in a running Explorer process;
unpin/re-pin or Explorer refresh may be needed on an upgraded machine. The
installer does not delete users' unrelated pinned shortcuts.

## Native installer

`installer/setup.cpp` draws the background, soft ribbons, text, gradient pill
buttons, minimize/close controls and launch checkbox with GDI+. It embeds only
the new transparent logo and actual NSIS payload. Old baked-screen PNGs are
deleted and cannot be pulled into the build.

The logical window is 720 x 524, matching the reference panel's 524 x 381
proportions, with a 17px corner radius. Welcome, installing and finished retain
the concept's wording and placement. The installing screen waits for actual
payload exit status rather than presenting invented percentage progress.
An error/retry state is retained solely for installation failures.

The finish page remains open until Done; launch is checked by default and can
be toggled by mouse/keyboard. Enter activates the focused launch toggle or the
main action; Escape closes. Native owner-drawn buttons retain tab stops and
accessible names. Per-monitor DPI changes reposition all controls. Background
dragging remains active during extraction. Buffered painting avoids black layers,
text baked into images and checkbox erase patches. Cancellation joins payload
preparation before releasing the job and temporary file.

## Validation

`scripts/verify-branding.py` checks every asset alias, nine frames, alpha, large
silhouette bounds and absence of opaque legacy files. On Windows, `--binary`
loads every executable icon group and compares every frame's pixels against the
new production export, supporting PNG and DIB resources.

The main build requires these checks for Soulu.exe, setup, payload and installed
uninstaller. Existing native suites remain mandatory. The installer mouse smoke
test checks drag, three screens, persistent finish, launch enabled/disabled,
direct close, shortcut/Installed Apps targets and actual uninstall. Screenshots
are uploaded as `Soulu-visual-checks`; final artifacts include the verified engine
version and source SHA. Publication requires all mandatory checks and
HEAD == origin/main == build SHA.

Inspect actual screenshots against the concept and verify taskbar/Start/desktop
on the target user's display scaling before calling visual sign-off complete.
External sites, a real authenticated account, live VPN endpoints and existing
pinned shortcut caches still require checks on the user's machine; automated
coverage cannot establish those external states.
