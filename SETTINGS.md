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
