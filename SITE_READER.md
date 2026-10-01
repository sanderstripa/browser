# Current site and Reader Mode — Preview 42

The existing address-bar page action opens a compact OSR-shell popover in both
layouts. The existing native popover flag expands the shell surface without
moving or resizing the content viewport. Outside click and Escape close it;
Tab cycles its visible controls. Tab/document changes close stale UI. Every
site action carries tab ID, exact URL and native document generation. The
browser process rejects stale operations, including asynchronous extraction.

Permissions use `SitePolicy` and its existing domain overrides. Ad blocking
uses that model's separate `blocking` dictionary. `ResetSite` atomically removes
only the current domain's permission and blocking overrides. Existing settings
reset semantics remain intact. Zoom reads/writes CEF Get/SetZoomLevel; find uses
the existing CEF Find method and one shared find UI, including native Ctrl+F.
The header reports the HTTP/HTTPS scheme; it does not provide a certificate
inspection service or a security score.

## Extraction and security

Mozilla Readability **0.6.0** is bundled with its Apache 2.0 license; source:
https://github.com/mozilla/readability/tree/0.6.0 . No runtime download, AI call
or cloud extraction occurs. A one-shot CEF DevTools observer identifies the
main frame, creates an isolated world, and runs Readability on a document clone.
The source page cannot replace that world's parser or DOM prototypes. The
original live DOM is never changed. Parsing is bounded to 50,000 elements;
articles below 500 characters are rejected. A failed parse, navigation, close,
loading state or ten-second deadline produces refusal, never an empty reader.
Availability is checked when the site menu opens and extraction is repeated
on entry. Dynamic content can be retried by reopening the menu.

Results are untrusted data, capped to one million HTML characters. The trusted
shell parses them in an inert template, then builds **new** allowlisted elements
and text nodes. It never inserts source nodes into its live DOM. Scripts, styles,
frames, objects, forms, controls, SVG/MathML and media widgets are discarded;
all source attributes are dropped except explicitly reconstructed HTTP(S)
links/images and bounded image alt text. Event handlers, source CSS, IDs,
srcset, arbitrary protocols, credentials in URLs and source JS cannot survive.
The existing shell CSP remains in effect. Links are handled through the native
Soulu tab routing, including foreground, background and incognito actions.
Images load from their original HTTP(S) URLs with no referrer. Extraction itself
is local; loading original article images still makes ordinary network requests.

Reader is an opaque shell view above the original content browser. It keeps
the original URL and history and never creates a top-level window. Exit restores
that same browser without reloading. Back/forward and source navigation invalidate
the reader at a document boundary. The original page remains alive underneath;
its own scripts can continue to run there, but no source script is copied to or
executed in the privileged reader. This is a reading view, not script suspension.

## Preferences and data boundaries

Themes: light, sepia, dark. Fonts: Arial sans, Georgia serif, system. Text size:
14–32 px in two-pixel controls. Three widths and three line heights; image toggle.
There is no duplicate layout zoom control. Preferences are validated by native
code and stored atomically in `Profiles/<profile>/soulu-reader.json`. Incognito
preferences stay in memory and are discarded when the last private tab closes.

Site-data deletion requires a native confirmation naming the exact origin and
supported subset. `Storage.clearDataForOrigin` operates on that tab's browser
context and clears **localStorage, IndexedDB, Cache Storage and service workers**.
Cookies, sessionStorage, HTTP cache and other storage types are retained. This
intentional subset avoids broad cookie deletion that could affect sibling sites.
The UI only reports success after Chromium acknowledges the operation; timeout
or rejection is an error. Passwords, bookmarks, history, other origins and other
profile contexts are untouched. Reload after clearing if a live page recreates
its storage. Permission/blocking reset never deletes site data.

## Verification

`scripts/test-cef-site-reader.py` runs against the real Windows CEF binary,
exercising article and non-article fixtures, metadata absence, content structure,
malicious markup, entry/exit, preferences, both layouts, native Ctrl+F, popover
closing, internal background links, stale-document refusal, native confirmation,
other-origin isolation, restart and incognito preference disposal. The native
data tests verify resetting both policy models without changing another domain.
The release workflow gates publication on this and existing vault, storage,
profile isolation, policy, navigation, link-menu, bookmarks, layout and installer
checks, then requires build SHA == current main SHA. CEF remains pinned to
154.0.32 / Chromium 154.0.8037.58.

Human review remains necessary for visual quality/transparency on actual hardware,
real Google/Ozon authenticated sessions, camera/microphone hardware and articles
behind authentication/paywalls. Readability can decline unusually structured
articles. Element picking/hiding is deferred; AI-summary and other roadmap blocks
are not part of this change.
