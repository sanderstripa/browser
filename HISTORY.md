# History and browsing data

Baseline: fresh `origin/main` at `20571921c079f8ed1dce5d2936d302e85304b408`
(Settings 2.0, Soulu Preview 46). The approved CEF/Chromium pin is unchanged.

## Entry points and presentation

The native top-toolbar context menu contains **История** and **Очистить данные
браузера…**. Ctrl+H opens History; Ctrl+Shift+Delete opens its clearing dialog.
Both shortcuts work when the shell or a content document has focus. The address
`soulu://history` resolves to the local `ui/history.html` internal document.
Settings → Profiles and data also exposes both actions.

The bilingual page shows favicon, title, URL and visit time. Newest visits appear
first, optionally grouped into Today, Yesterday and localized calendar dates.
Filters cover all visits, today, yesterday, the last seven days and older visits.
Search matches literal substrings of title or URL (including domain), with Unicode
case folding. Native SQL queries return 100 rows at a time, with a next-page flag.
The UI holds a time boundary across pages so new visits do not shift pagination.

Enter/Space opens the focused entry; the adjacent action opens a new tab.
Arrow keys move between entries; Delete removes the focused entry. All controls
are native focusable HTML controls. Esc cancels the modal while idle; the modal
remains open during clearing and returns focus to its entry button afterwards.

## Canonical model and recording

The baseline source had no persistent Soulu visit model: omnibox suggestions used
open tabs, bookmarks and search suggestions. No previous Soulu history database
or history importer is replaced or duplicated.

`HistoryStore` owns `Profiles/<id>/soulu-history.sqlite`, using the existing safe
`ProfileRoot()` and bundled SQLite. Both the History page and omnibox suggestions
query this same model. Each successful HTTP(S) main-frame load records one visit;
title/favicon callbacks update its existing ID. Internal pages, subframes, failed
loads and incognito never create records. Late title/favicon updates cannot
recreate deleted visits. Visits survive normal restarts. SQL deletions commit
before the bridge reports success; secure deletion is enabled.

Profile settings `saveHistory`, `historyGroupDays` and `historyDefaultFilter` are
part of the existing staged Settings editor. Disabling recording keeps existing
visits and prevents new records; clearing is a separate conscious action.

The History bridge only accepts the exact local History document and binds every
operation to that document's profile, rather than the currently active tab's
profile. An incognito History document shows a private empty state and is denied
all query/delete/clear requests. No normal history is exposed to private browsing.

## Clearing

| Category | Actual backend | Time scope |
|---|---|---|
| Browsing history | Transactional deletion from `HistoryStore` | Last 15 minutes, hour, 24 hours, 7 days, 4 weeks, all time |
| Cookies and site data | Chromium profile-wide browsing data remover | All time |
| Cached files and images | Chromium profile-wide cache remover | All time |

The native coordinator uses an invisible auxiliary Chromium Settings WebUI in
the History document's existing `CefRequestContext`. It invokes Chromium's
allowlisted `browser.clear_data.cookies` / `browser.clear_data.cache` operation,
then waits for its completion through an observed DevTools promise and for the
auxiliary browser to close. The worker has no Soulu bridge. It does not infer
site origins from history, inspect/delete live profile files, reset the entire
profile or create another persistent browser context.

This integration uses the Chromium WebUI contract shipped with the pinned engine.
Engine upgrades must rerun the native clearing suite; missing/changed WebUI APIs
produce an explicit error instead of a simulated success.

**Deliberate limitation:** the time selector applies to history. Cookies/site data
and cache are explicitly labeled **All time** and require a separate acknowledgement,
even when history's range is shorter. The backend also requires this acknowledgement.
There is no misleading partial-time clearing of web storage, and no invented cache
size. Passwords, bookmarks, permissions, UI settings, Reader settings, VPN configuration,
profile metadata and downloaded files are not selected by this flow.

Soulu's Alloy content browsers do not implement persistent form autofill. The
dialog explains that limitation instead of offering a nonfunctional checkbox;
the separate DPAPI password vault is preserved.

Errors retain the modal and refresh the actual history. A partial result states
which category succeeded. Requests are serialized; closing the application while
clearing defers normal shutdown until the operation completes.

## Verification

`scripts/test-cef-history.py` runs the real CEF executable with disposable profile
roots. It checks recording, Unicode/title/domain/URL search, filtering and sorting,
pagination, SQLite deletion, time-range clearing, restart persistence, private-mode
denial, profile isolation, actual cookies/LocalStorage/IndexedDB/Cache Storage removal,
HTTP cache misses after clearing, and preservation of bookmarks/passwords.

It captures native-renderer evidence for light/dark/system, widths 1100 and 420,
and device scales 100%, 125%, 150%, 175%, 200%, with overflow and modal-bound checks.
The existing native data-security suite also checks HistoryStore isolation,
Unicode search, internal-page exclusion and late-update behavior after deletion.
Both suites are wired into the Windows build before packaging.
