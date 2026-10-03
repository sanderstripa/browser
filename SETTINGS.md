# Settings architecture

Baseline: main `1d1ac757276477ddf95918bdefa1c1ecea805e53`, Preview 45.
CEF 154.0.32 / Chromium 154.0.8037.58 are pinned and remain unchanged.

## Canonical storage audit (before implementation)

| User settings / actions | Existing model | Scope | Destination |
|---|---|---|---|
| Language | `language` | Profile | General |
| Theme, matte, panel mode | `theme`, `mattePanel`, `layout` | Profile | Interface |
| Toolbar visibility | `showSidebar`, `showBack`, `showFavorites`, `showNewTab`, `showDownloads`, `vpnToolbarVisible` | Profile | Interface |
| Address, extensions, downloads button | `addressPosition`, `extensionsPosition`, `downloadsMode` | Profile | Interface |
| Startup / new tab / home | independent `startup*`, `newTab*`, `home*` | Profile | Tabs and pages |
| Last tab | `openStartPageAfterLastTab` | Profile | Tabs and pages |
| Home widgets | `homeShowLogo`, `homeShowSearch`, `homeShowShortcuts`, `homeShowBackground`, `homeShortcuts` | Profile | Tabs and pages |
| Weather | `homeShowWeather`, `homeWeatherCity` | Profile | Omitted: provider is explicitly unconfigured |
| Search | `searchEngine`, `addressOpenMode` | Profile | Search |
| Bookmarks bar | `bookmarksBarMode`, `bookmarksBarPosition`, `bookmarksIconsOnly` | Profile | Bookmarks |
| Permissions, exceptions, ad blocking | `SitePolicy`, `soulu-site-rules.json` | Profile; ephemeral in incognito | Websites |
| Reader | `ReaderPreferences`, `soulu-reader.json` | Profile; ephemeral in incognito | Websites |
| Profiles | `profiles.json`, existing create/switch/delete | App registry; profile data isolated | Profiles and data |
| Passwords | `PasswordVault`, `soulu-passwords.json`, DPAPI | Profile | Existing manager in a dialog |
| Import | `DiscoverPasswordSources`, `ImportPasswords` | Explicit source/target profiles | Existing importer in a dialog |
| Downloads | `downloadPath`, `askDownloadLocation` | Profile | Downloads |
| VPN | `vpn` in legacy `settings.json`, native helper profiles | Global | VPN |
| Versions | Runtime `EngineVersion`, current product version in native state | App | Updates |
| Automatic updates | stored `automaticUpdates` has no real updater | Profile legacy value retained | Omitted |
| Windows default browser | Existing registration and Windows Default Apps action | OS user | General |

The profile settings dictionary is `Profiles/<id>/soulu-settings.json`. The
legacy app `settings.json` remains the migration template and global VPN owner.
All existing scopes are preserved, including profile-scoped theme and language.
UI reorganization does not rename or delete storage keys. Existing legacy
Startup and bookmarks migrations remain canonical. Passwords, imports,
permissions, adblock, Reader, Home, onboarding and VPN engines are reused.

## Product structure

General, Interface, Tabs and pages, Search, Bookmarks, Websites, Profiles and
data, Downloads, VPN, Updates. Normal opening starts at the card grid without
a sidebar. Sections show a 60px icon rail with All settings at its top.
Content scrolls independently of the header, rail and Apply/Cancel footer.

The old Appearance, Toolbar, Startup and Passwords top-level views are replaced;
their controls have the canonical destinations in the table above.

## Editing session and persistence

`settings_session.cc` implements a trusted Settings-document bridge. `begin`
loads the existing profile dictionary, SitePolicy snapshot, Reader preferences
and global VPN configuration. The UI keeps loaded, persisted and staged values.
Only the staged dictionary changes while editing. Ordinary controls never write
on input. Explicit profile/password/import/site-data/Windows actions use their
existing backends and confirmations; they are not undone by Cancel.

Theme, layout, matte, language and supported toolbar controls use a native
presentation overlay, not a file write. Cancel clears that overlay and reloads
the actual persisted state. Apply validates values, rebases only changed keys
on the live model and refuses concurrent changes to the same key. Home shortcut
validation is shared with the Home backend. Existing atomic writers commit each
canonical store separately: profile settings, SitePolicy, Reader, then VPN.
There is no cross-file all-or-nothing transaction. VPN helper storage and the
Soulu global JSON are separate existing stores; if helper save succeeds but
the JSON write fails, the error explicitly reports this partial save. If a later group fails, the
reply includes the actual persisted snapshot; already committed groups are
reported honestly and the remaining draft remains editable for retry.

Dirty Apply is enabled only after a real difference. Apply is disabled during
save. Tab close, document navigation, Escape and whole-window close use a
Save / Discard / Keep editing dialog. Failed Save leaves the dialog and draft
open. Profile switching/creation require Apply or Cancel first. Background
Settings documents receive profile-change notifications, clear stale drafts
and close sensitive manager dialogs. Incognito opens ordinary profile Settings;
its ephemeral permission/storage context is not persisted as profile settings.

Matte capability is derived from the existing DWM/Windows advanced-effects and
high-contrast checks. Unavailable effects are explained without overwriting
the saved value. An already enabled value can still be turned off.

No exposed preference needs a browser restart. Startup preferences naturally
change the next browser startup; page defaults affect future page openings.
Existing pages are not navigated as an editing side effect.

## Search and presentation

A shared section schema owns cards, rail icons, groups and control metadata.
Search indexes RU/EN titles, hints, group/section paths, option labels and
synonyms. Results point to the canonical section/control, scroll it into view,
focus and briefly highlight its row. Locale changes rebuild labels and index
without losing bilingual aliases. The home has no sidebar; section navigation
includes an All settings action, titles/tooltips, selected and keyboard-focus
states. Radio segments, labelled inputs, live status and reduced-motion CSS use
native accessible semantics. Small windows stack cards and rows; only content
scrolls while the footer remains reachable.

## Migration and cleanup

No storage schema is replaced. Legacy `startPage*` to `startup*` and legacy
bookmarks modes retain the existing migration code. `bookmarksBarPosition` and
`bookmarksBarMode` are edited through one combined control, including hidden
mode; their stored keys remain unchanged. Unsupported weather, extension
placement and automatic-update values are preserved without fake controls.
New profiles use the existing legacy template, not another profile's edited
settings. Global VPN remains global.

The old embedded `settingsPanel`, renderer views and save handlers were removed.
Bookmarks management retains CRUD/import/drag-and-drop and now links to the
canonical Settings editor instead of owning duplicate preference controls.
Passwords and import remain dialogs over the existing native engines. Site-data
UI lists currently open web origins and invokes the existing origin action;
it does not claim an inventory or complete cache/cookie cleanup.

## Verification

`test-cef-settings.py` launches real Soulu with isolated app-data folders and a
legacy fixture. It covers all ten sections, control search, preview with no
writes, rollback, invalid URLs/VPN keys, multi-store Apply, injected write
failure/retry, native dirty-close actions, restart, profile A/B isolation and
incognito opening. CDP viewport/device-scale probes cover 100%, 125%, 150% and
200% layout conditions and save screenshots; these are not physical OS DPI
switches. The build workflow requires this test before packaging/release and
retains the project's native browser, security, onboarding and integration
checks. Release source must exactly equal current origin/main.
