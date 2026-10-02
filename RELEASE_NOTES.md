# Soulu Preview 43

- Independent Startup, New Tab and Home page modes, with legacy Startup migration.
- Offline local Soulu page with profile-scoped search, ordered editable shortcuts and individual visibility controls.
- Ctrl+T and Ctrl+L handled by the native browser; Alt+Home opens the configured Home in the current tab.
- Graceful-close restore of ordered normal tab URLs and selection, excluding incognito.
- Last-tab Startup/blank semantics preserved; pending tabs do not cause duplicate replacement tabs.
- Private Home changes remain in memory and disappear when the last incognito tab closes.
- Weather shows an honest unavailable state until a provider is connected. Selected city is saved without requesting geolocation.
- CEF remains 154.0.32 / Chromium 154.0.8037.58.

See STARTUP_HOME.md for storage, security boundaries, limitations and validation.
