# Soulu first-run setup

The six-step local flow runs in one native content tab beneath the existing browser shell. It uses existing profile settings, SitePolicy, PasswordVault/importer and VPN helper services. No Settings 2.0, alternative configuration store, cloud importer or new engine is introduced.

## Profile eligibility and persistence

`CreateProfile` decides eligibility before creating its CEF cache. Profiles restored from profiles.json, existing profile directories, and legacy personal settings/bookmarks are treated as existing installations. Their missing first-run marker migrates conservatively to `skipped`, preserving settings and session restoration. A genuinely new profile receives `not_started` and step 1. The default settings template explicitly excludes another profile's onboarding state.

State resides in `Profiles/<id>/soulu-settings.json`, under `onboarding`: status (`not_started`, `completed`, `skipped`), step, draft source/category/privacy choices and native outcomes. The existing atomic WriteJson writer is used. Completed/skipped flows never launch automatically again. A pending flow resumes its stored step. Incognito never receives a marker or privileged onboarding bridge. The bridge accepts only the exact packaged document in the active normal tab of the active profile; it cannot invoke arbitrary shell/settings APIs.

Import intent is saved before work starts. Its native report is saved to the originating profile even if the user switches profiles during the background operation. A saved report suppresses repeat import. An interrupted operation requires review in the ordinary password manager and does not run automatically again. Source deduplication and DPAPI encryption remain unchanged. Keys are not persisted in onboarding drafts.

## Import capabilities

The visible sources are `DiscoverPasswordSources` intersected with the installed executable catalog from `DiscoverImportBrowsers`. Chrome and Edge executable paths come from Windows App Paths or supported installation directories; Firefox uses the same installation discovery and its registered profiles.ini. Only readable existing supported password stores appear. Multiple browser profiles are separate single-source radio choices. There is no misleading import-all control.

All current providers support passwords only. Bookmarks and history are explicitly disabled. Chrome/Edge DPAPI and supported v10/v11 AES-GCM records use the existing local importer; v20 App-Bound records remain protected. Firefox uses installed NSS and never bypasses a primary password. The UI displays actual imported/skipped/protected/failed counts and retains partial/error results before continuing. No detected source produces a usable clean-start/skip state without fake rows.

Icons are extracted from each detected executable with ExtractIconEx/DrawIconEx and supplied as local bitmap data. No downloaded browser logos or machine-specific static icon paths are bundled. If executable icon extraction fails, the name remains usable without a fabricated brand icon.

## Privacy

Advertising updates SitePolicy's existing global `blocking.enabled`. Sensitive permissions update existing defaults for camera, microphone, geolocation and notifications: on means Ask; off means Block, never blanket Allow. Existing per-site exceptions are preserved. Ordinary site/settings controls see exactly these rules after restart. Skip leaves rules unchanged.

## VPN and Windows defaults

The ordinary Settings parser is extracted to `ui/vpn-key.js` and shared by onboarding. It retains the existing Settings parser for VLESS/Xray and Sudoku formats. The shipped native helper was verified to reject Sudoku with 'Ссылка должна начинаться с vless://.'; onboarding therefore advertises and accepts Xray/VLESS only. Format checking is clearly distinguished from backend validation. The existing native helper validates and saves with `save_profile`; no connect action is sent. Its real profile ID and the existing global VPN settings are reused. VPN remains global in the current architecture; onboarding status remains profile-specific. No Windows proxy mutation is added.

The default action registers Soulu's per-user HTTP/HTTPS capability and ProgID, opens `ms-settings:defaultapps?registeredAppUser=Soulu`, and explains that Windows requires the user's choice. It never writes UserChoice or claims that Soulu is now default. Required HTTP/HTTPS command-line handling supports a new launch and CEF's already-running relaunch; pending external navigation waits until first-run ends. The uninstaller removes the registration. Windows reference: https://learn.microsoft.com/en-us/windows/win32/shell/default-programs

## Visuals and accessibility

Production `ui/branding/soulu-128.png` and `soulu-512.png` are composited unchanged. No concept logo is imported. `ui/onboarding-landscape.png` is one shared locally packaged generated landscape. Neutral SVG bookmark/clock/key elements, a glass shield and tunnel provide step illustrations without third-party brands. Real browser icons occur only in the source list. The existing Windows shell and window controls remain in place.

The shared background was made with the built-in imagegen tool using this prompt: "Production UI hero background only, wide 2.6:1, airy soft blue and white glass-like landscape, smooth icy blue hills, pale luminous sky, reflective blue lake and delicate white flowing light trails, quiet composition with blank space for separately composited production assets. No text, logos, icons, petals, UI, browser logos or stock imagery."

Native radio/checkbox semantics, labelled password input, switch roles, visible focus, Enter activation, Tab/Shift+Tab and non-dismissive Escape are supported. Selections persist on Back. Every viewport uses a bounded card with internal content scrolling; hero proportions reduce on small heights. Dark system theme cannot recolor the fixed reference palette.

## Verification

`scripts/test-cef-default-links.py` verifies deferred first-run links, normal relaunch handling, rejected file URLs and return to the normal profile from incognito.

`scripts/test-cef-onboarding.py` exercises the actual CEF process, isolated profile files, local password fixtures, partial results, resume, privacy, VPN persistence, completion/skip, existing-session migration, multiple profiles, incognito and small/DPI viewports. `test-cef-data-security.py` retains the underlying encrypted importer/vault tests. Ordinary regression suites explicitly opt into dismissing first-run with a fixture helper, while first-run tests leave that flag unset. Layout tests seed an existing installation rather than asserting old first-launch behavior.

The Windows workflow gates release publication on onboarding and existing mandatory native suites, and checks that the release build SHA equals current origin/main. CEF remains 154.0.32 / Chromium 154.0.8037.58. Authenticated third-party accounts, remote VPN connectivity and the user's Windows default selection require their real credentials/interaction; automated fixture success does not assert those personal states.
