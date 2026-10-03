# Soulu Preview 49 candidate — Stable CEF update

- Updated the complete Windows x64 CEF distribution to
  `154.0.33+ga03e714+chromium-154.0.8037.94` (Chromium `154.0.8037.94`),
  the newest stable Windows x64 build in the canonical index on 2026-10-03.
- Headers, import library, rebuilt wrapper, binaries, resources and locales use
  one checksum-verified distribution. Every packaged runtime file is verified.
- Added native web-platform, GPU, media and download validation; excluded build
  libraries/debug symbols from the installer and ZIP.
- Existing interface and feature architecture are preserved. No CEF API
  signature changes or new security exceptions were needed.
- Publication requires the full regression evidence for the final main SHA.
  An external Ozon challenge and unverified authenticated/manual scenarios
  must not be described as passing tests. See `CEF_UPGRADE.md`.

# Soulu Preview 48 — Settings overlay

- Settings opens from the top of the current browser window, preserving the existing Settings sections and controls.
- Native live backdrop blur and coordinated slide transitions, including reduced motion.
- Browser tabs, navigation, scroll and media remain alive; background input is blocked until closing completes.
- Apply, Cancel, dirty close confirmation, sensitive subviews and profile isolation retain their existing behavior.
- Native Windows build and overlay regression checks run alongside the existing browser suite.
- CEF remains 154.0.32 / Chromium 154.0.8037.58.

See SETTINGS_OVERLAY.md for composition, lifecycle and verification details.

# Soulu Preview 47 — History and browsing data

- Profile-scoped persistent History with native toolbar menu entries, Ctrl+H and
  Ctrl+Shift+Delete; clean bilingual light/dark/system pages and day grouping.
- Real title/URL/domain search, date filters, pagination, visit opening, individual
  deletion and six time ranges for clearing history.
- Real profile-wide cookies/site data and cache clearing through Chromium, with
  explicit all-time labels and acknowledgement; passwords and bookmarks preserved.
- Settings controls for recording, grouping and default history filter; private
  browsing cannot record, read or clear ordinary history.
- Native integration checks cover actual storage/cache effects, isolation,
  keyboard/menu interaction and responsive layouts at 100–200% device scale.

See HISTORY.md for storage and time-range limitations.

# Soulu Preview 46

- New Settings home with ten section cards and a compact icon rail inside sections, following the supplied visual reference.
- One canonical Settings editor, bilingual control search with direct navigation and highlighting, keyboard focus and responsive light/dark layouts.
- Staged changes, Apply/Cancel, reversible toolbar/theme preview, native close confirmation and explicit persistence errors with retry.
- Existing profile settings, permissions, ad blocking, Reader, passwords, importer, Home, downloads and VPN backends are preserved.
- Independent Startup, New Tab and Home controls; profile isolation and existing-user migrations retain canonical values.
- No simulated updater or unsupported weather/extension controls. VPN key editing saves through the existing helper without auto-connecting.
- CEF remains 154.0.32 / Chromium 154.0.8037.58.

See SETTINGS.md for storage scopes, transaction boundaries and verification.
