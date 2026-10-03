# Soulu typography

Baseline: `169a65b7d49fbc3479fb2ba506f1511bceb123aa`, Beta 1.0.50.
Target: Beta 1.0.51. Engine remains CEF 154.0.33+ga03e714 and Chromium
154.0.8037.94. Motion timing, easing and native composition are unchanged.

## One semantic model

`typography.json` is authoritative. Run `python scripts/generate-typography.py`
to produce `ui/typography.css`, `installer/typography_metrics.h` and
`cef/soulu/typography_metrics.h`.
CI checks that generated files are current.

| Role | Size (CSS px / DIP) | Line box | Real weight |
| --- | ---: | ---: | ---: |
| display | 32 | 40 | 600 |
| heading1 | 28 | 36 | 600 |
| heading2 | 22 | 28 | 600 |
| heading3 | 18 | 24 | 600 |
| bodyLarge | 16 | 24 | 400 |
| body | 14 | 20 | 400 |
| control | 14 | 20 | 500 |
| compact | 13 | 18 | 400 |
| compactControl | 13 | 18 | 500 |
| caption | 12 | 16 | 400 |

Page titles use heading1, section/dialog titles heading2, cards and subsections
heading3. Descriptions use body; introductory descriptions use bodyLarge.
Buttons use control; tab labels/omnibox/bookmarks and compact menu rows use
compact or compactControl. Timestamps, helper labels and secondary metadata use
caption. Letter spacing is neutral. Compact active/inactive tab labels retain
the same weight to avoid changing text width on activation.

Graphic glyphs have three shared sizes (8/20/32), separate from text hierarchy:
favicon initials, close/arrow/search symbols and empty-state artwork. Existing
SVG/PNG application icons are unchanged.

## Resources and licensing

Official upstream release:
https://github.com/simpals/onest/releases/tag/2.001
Source commit: `8739b1910618a15335e4cc48842052d0ee739ade`.
Unmodified upstream static TTFs Regular (400), Medium (500), SemiBold (600) live
in `ui/fonts/`. The complete upstream SIL Open Font License 1.1 and attribution
are distributed beside them as `OFL.txt`, inside both the ZIP and installer
payload. `ui/fonts/manifest.json` records hashes and provenance.

CEF pages explicitly link the generated stylesheet and preload the three local
TTFs. Explicit `font-src 'self'` authorizes only bundled local fonts; other CSP
directives remain unchanged, with no CDN. `typography.js` waits for actual faces, including RU
glyphs, before revealing the first frame. A missing face is a package failure,
not an accepted silent fallback. Fonts are cached by CEF, not re-registered on
every paint. This works offline and does not require Windows font installation.

Browser chrome, tabs, omnibox, suggestions, side tabs, bookmarks, Tab Overview,
downloads, menus, tooltips, site info, permissions, adblock, Reader controls,
Settings and its dialogs, profiles/password/import/update/VPN settings,
Home/New Tab/Startup, History/Clear Data and onboarding are owned HTML surfaces.
They share the same resource loader and semantic model. Unknown favicon initials
and VPN country codes are DOM labels so they use the bundled face rather than a
standalone SVG with an unavailable font.

Native confirmations, permission/password/profile prompts and JavaScript
alert/confirm/prompt dialogs use the shared Win32 dialog helper. It registers
the same bundled faces privately once, caches HFONT handles by role and DPI,
selects the actual Onest Medium/SemiBold families, and paints headings/body
in semantic line boxes using the font's hhea ascent/descent. Native button and
edit controls receive those private fonts before display. Existing native consent
dialogs retain default rejection; JavaScript confirm/prompt retain OK/Cancel and
Enter behavior. CEF reset and owner shutdown cancel open dialogs explicitly,
and the modal loop permits CEF tasks while retaining its client lifetime.
Toolbar/page/link and CEF content context menus retain Win32 menu tracking and
command routing, while the shared owner-draw layer selects compactControl Onest
and canonical line boxes. Enabled/checked states and nested menu items are retained.
Own-page title tooltips use a shared caption DOM bubble; native CEF tooltips
are suppressed only for those internal URLs. External page tooltips are untouched.
The custom installer paints through GDI+. It embeds the same
three canonical TTFs as RCDATA before showing its window, keeps one private
collection/family for each face and caches the ten semantic Font objects.
Medium and SemiBold use their real regular-style static families, avoiding GDI+
synthetic bold. The existing 720-to-524 reference transform is compensated so
native size/line boxes equal the web DIP model. Caches are released before GDI+
shutdown. Nothing is installed into the system font registry.

## Audit and explicit exceptions

`typography-audit-before.json` records every changed CSS selector, its previous
declarations and semantic replacement. Before migration the shell/Home/Settings
used Segoe UI Variable Text / Segoe UI, History/onboarding/Reader controls used
Segoe UI, VPN used Inter/Segoe UI plus an overriding Montserrat font file.
Native installer text used independently constructed Segoe UI fonts. Local
sizes ranged from 7 to 58 px and weights included 300/450/550/650/700/800.
These UI paths are removed; the unused Montserrat binary is removed.

Remaining text-family exceptions:

* `ui/typography.css` and its generator: Segoe UI Emoji and Segoe UI Symbol are
  centralized fallback families for characters absent from Onest, not primary
  UI families. SVG icon resources and glyph sizing remain graphics.
* `ui/site-reader.css`: Georgia/Times New Roman, Arial/Helvetica, system-ui and
  serif/sans-serif remain only in `.reader-article`, preserving the user's
  functional reading-font/size/line-spacing choices. Article title, deck and
  metadata belong to that content mode. Reader toolbar/settings/menu use Onest.
* `scripts/`: historical test fixture CSS describes external article content;
  typography audit tests contain font names as rejected search patterns.

Only the emergency missing-font error uses an OS message box. Window nonclient
captions, file pickers, OS security prompts, the silent NSIS payload and system uninstaller
surfaces are Windows-owned. Soulu does not hook their font configuration.
No stylesheet/font loader is injected into external sites; only bundled HTML
links it. Navigation/auth/storage/CEF preferences remain unchanged.

## Verification

`scripts/check-typography.py` checks generated files, font weights/cmaps,
licensing, all HTML loaders, residual CSS declarations, JS text assets and
native resource embedding. `scripts/test-cef-typography.py` checks actual custom
font rasterization through CEF's platform-font API and captures the RU/EN,
light/dark/system, 100/125/150/200% device-scale matrix and all Settings sections.
The existing native/motion/overlay/home/history/site/installer suites remain
mandatory. CDP device-scale emulation is not physical monitor DPI testing.
Screenshots and measured font selection do not alone prove every possible
string avoids clipping, or that authenticated sessions work without credentials.
Release publication retains the existing exact-SHA manual regression evidence
gate; never claim unperformed manual checks or bypass that gate.
