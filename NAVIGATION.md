# Bookmarks and Tab Overview (Preview 37)

The existing `BrowserWindow` owns bookmarks and tabs. The shell UI adds a horizontal bookmarks bar and an internal grid overview; it does not create a second tab manager or switch content to windowless rendering. CEF/Chromium pins remain unchanged.

Bookmarks extend the existing list with `type`, `parentId`, `order`, optional `hideTitle`, and timestamps. Each record retains `id`, `title`, `url`, `favicon` and `profileId`. They are saved separately from website storage to `%LOCALAPPDATA%/Soulu/User Data/bookmarks.json`, using a temporary file and atomic Windows replacement. Incognito bookmarks remain session-only. Profile replacement validates IDs and folder ancestry before saving. Save failures leave the previous in-memory list intact.

The existing toolbar sidebar button opens the bookmarks popover. Search matches titles and URLs, folders navigate into their children, and management supports editing, deletion, ordering and moving into folders. Context actions open in the current, foreground or background tab. The bookmarks bar uses favicon/title items, folders and an overflow entry. Its options are available in the popover: never, always, new/home tab, auto-hide on leaving the toolbar; above/below the classic tabs row; icons and titles or icons only. Individual labels may be hidden. Ctrl+Shift+B toggles auto visibility while the shell has keyboard focus. The previous sidebar remains available through its existing bridge and a menu action.

Import uses the browser's standard exported file: Chrome, Edge and other Chromium `Bookmarks` JSON or Netscape HTML; Firefox HTML export. It preserves folder hierarchy, skips duplicate URLs and merges equally named folders under the same parent. The user confirms the number of new items before persistence. No passwords or cookies are imported. Direct reading of installed browsers' live databases is not implemented.

The separate grid button opens Tab Overview inside the existing shell. It preserves tab order, searches title/URL, closes a tab without leaving the overview, switches on preview click and offers a new-tab tile. Existing tab close behavior is retained. No new top-level HWND is created.

Thumbnails use CEF `Page.captureScreenshot` at tab-switch/new-tab boundaries and on overview entry. They are asynchronous JPEGs, approximately 480px wide, cached in the tab record with a 24MiB aggregate limit and a 2MiB per-image limit. Old-URL results are discarded. Images are not written to disk, there is no polling loop, and unvisited background tabs have a labelled placeholder. Capture failure retains the previous image or placeholder.

Validation: JavaScript syntax and isolated Chromium UI interaction checks run locally. `scripts/test-cef-bookmarks-overview.py` is a mandatory Windows pipeline check for persistence/restart, invalid ancestry rejection, repeated import, compact/classic geometry, overview close/switch, JPEG capture and unchanged top-level window count. Existing storage, navigation, link-menu and layout checks remain in the pipeline.

Manual review is required for fidelity to the supplied reference screenshots, actual site previews and DPI scaling, drag/drop, long bookmark collections, theme/transparency appearance, maximize/restore and Google/Ozon sign-in with a real account. Passing automated checks does not establish visual acceptance.
