# Profiles, local credentials and site policy

## Existing data and migration

The existing tab owner (`BrowserWindow`) is retained. Bookmark records already
have `profileId`; they stay in the existing atomic `bookmarks.json` file and are
not silently moved. Ordinary profile IDs are validated lowercase ASCII tokens;
new IDs use Windows system randomness instead of profile-count numbering.
Windows device names, traversal, and profile-directory reparse points are refused.

One canonical `Profiles/<id>` path and one `CefRequestContext` are created per
registered ordinary profile. `main.cc` and the window use the same canonical
data root. Session-cookie persistence stays enabled for ordinary profiles.
Settings initially inherit the previous global settings as a migration template,
then live in each profile's `soulu-settings.json`. VPN configuration retains its
existing owner and behavior. The old global settings file is preserved.

Deleting a profile requires a native confirmation and keeps at least one profile.
Only that profile's tabs, bookmark records and registry entry are removed.
Physical profile-directory deletion is deferred until **after CefShutdown**,
when Chromium has released its databases. Pending deletions are checked against
the live profile registry; recursive deletion refuses reparse points. Failed
cleanup remains pending instead of removing another directory.

## Incognito

An independent context with an empty cache path holds web storage in memory.
It is shared only by the current incognito tabs. The last private browser close
releases the context, its in-memory rules, bookmarks and download-history rows.
Renderer credential-submit observation is disabled for private browsers,
including private popups. Password-vault access/import from the private view is
refused. Explicit downloads still save the file the user asks to download.
Incognito is not a guarantee that Windows paging/crash dumps contain no memory.

## Password vault

`soulu-passwords.json` contains metadata (origin, username, stable record ID,
profile owner, creation/update time) and a Base64-encoded **DPAPI ciphertext**.
DPAPI uses the current Windows user; per-profile/per-record entropy binds the
ciphertext to its owner and ID. Writes replace an atomic temporary file containing
only ciphertext and metadata. Corrupt vaults refuse mutation.

List responses omit encrypted secrets. Reveal/copy require explicit UI actions;
the settings list masks passwords, hides revealed values after 15 seconds or
loss of focus, and native clipboard copying clears after 30 seconds only if the
clipboard sequence is still unchanged. `origin + username` is the deduplication
key. Import never overwrites an existing record. Explicit manual saves and
confirmed form-save offers can update it.

The renderer reads only a submitted main-frame HTTP/HTTPS form with one password
field, a same-origin form action, and no `new-password` annotation. It does not
poll page fields or inspect cross-origin frames. The native save prompt is
explicit and describes the submission as a **probable** login, not a verified
authentication result. Unchanged saved passwords do not prompt again.
SPA login buttons that never submit a form, cross-origin form posts, multi-step
logins without a username field, signup forms, and password generation/autofill
are not claimed as fully supported. No secrets are logged or sent to a service.

## Import

Browser installation discovery reads Windows App Paths and standard installation
locations, including a registered custom Firefox location. It is separate from
the password-store catalog: an installed browser can have no local saved logins.
The catalog discovers Chrome/Edge Default and Profile directories under the
current user's Local AppData and Firefox profiles registered in that user's
`profiles.ini`. UI selection uses catalog IDs, not arbitrary source paths.
Both source and destination profiles are selectable.

The importer denies concurrent writes while copying encrypted source files to
a unique temporary directory. The original SQLite database is never opened,
checkpointed or changed; WAL data is copied alongside it. A locked source asks
the user to close that browser and retry. Temporary snapshots contain encrypted
source data, never plaintext credentials, and are removed when import ends.

Supported Chromium records: ordinary user-context DPAPI blobs and AES-GCM
`v10`/`v11` with the Local State AES key unwrapped by ordinary DPAPI. `v20`
App-Bound Encryption and unknown versioned schemes are reported as protected or
unsupported. Failed authentication tags are errors, never accepted plaintext.
No elevation, process injection, memory scraping, remote debugging of source
browsers, or protection bypass is used.

Firefox uses copied `logins.json`/`key4.db` with the installed Firefox's NSS.
NSS libraries load from the installed Firefox directory with restricted DLL
dependency lookup. Source `pkcs11.txt` is removed from the snapshot so arbitrary
external PKCS#11 modules are not loaded. A missing/incompatible NSS installation,
an inaccessible key database, a primary-password requirement, or decryption
failure produces protected/unsupported rows. There is no primary-password UI or
attempt to bypass it. Real Firefox-version/profile compatibility must be checked
on the user's installation; synthetic Chromium tests do not establish it.

Reports distinguish imported, existing/skipped, failed, and protected rows.
Import runs on the CEF background file thread. Credential mutations and profile
deletion wait for import; app shutdown waits for the import task to finish.

## Accounts

The standalone Settings Accounts placeholder is replaced by **Sites and
permissions**; **Profiles** is named **Profiles and data**. No cloud Soulu account,
sync backend, discovered-account inventory or telemetry is invented. Existing
quick links to Google account web pages are ordinary website navigation in the
active profile, not an account backend or discovery feature.

## Permissions and exceptions

Each profile owns `soulu-site-rules.json`. Permission defaults and exact-host
overrides are separate dictionaries. Domains use Chromium URL parsing, lower
case, and no scheme/path/port duplication; subdomains are deliberately distinct.
Password origins remain scheme/host/port origins, not these permission keys.

Media and permission prompts use native CEF handlers. Ordinary downloads have
their own `OnBeforeDownload` gate and visible blocked-download feedback.
Programmatic popups and actual popup windows use `OnBeforePopup`; normal
user-initiated link navigation retains the existing tab-opening paths.
CEF content settings mirror the effective camera, microphone, geolocation,
notifications, automatic-download and sound policy at navigation and rule changes.
Mute uses browser audio muting; Block also sets Chromium's sound content setting.
Chromium 154's approximate/precise geolocation dictionary is also synchronized
through `SetWebsiteSetting`, following its
[geolocation schema](https://github.com/chromium/chromium/blob/154.0.8037.58/components/content_settings/core/browser/geolocation_setting_delegate.cc).
Changes apply to subsequent requests. Reload an already open page to stop a
previously running capture or refresh cached script-visible permission state.
Unrelated permission types retain CEF's existing handling.

Settings includes defaults, domain search/edit, inherit-one-permission, reset one
domain, and reset all permission exceptions with native confirmation.
Resetting permission exceptions does not silently reset content-blocker rules.

## Content blocking

Content blocking has a separate request-filter engine and its own global/host
override dictionary. Its state is **not** a CEF permission. The initial default
is off; users can enable it globally or per site, and the page menu offers a
quick toggle followed by a reload.

The supported filter subset is a small, authored exact-host/suffix list:
doubleclick.net, googlesyndication.com, googleadservices.com, adnxs.com,
adsrvr.org, advertising.com, criteo.com, criteo.net. Only third-party script,
image, stylesheet and font requests match. Main/subframe navigation, XHR,
downloads, WebSockets and requests without a browser/frame (including workers)
are exempt. First-party requests are exempt. There are no external filter-list
downloads, ABP/uBlock parser, cosmetic rules, DNS/system-proxy/hosts changes,
paywall bypass or anti-adblock bypass. This conservative first version does not
promise to remove every advertisement.

## Validation and remaining manual checks

The pipeline includes native DPAPI/vault/SQLite/AES-GCM/protected-record tests,
source-file integrity, password restart and plaintext scanning; real CEF
two-profile storage/incognito disposal, trusted form-submit save/decline prompts,
confirmed profile deletion and exception reset; Chromium permission-state queries;
actual request-filter toggles on controlled sites; and the existing bookmarks,
internal-tab/link-menu, cookie persistence, classic/layout/resize and installer
checks. Test artifacts identify the commit and exact CEF build.

User verification is still needed for Google/Ozon authenticated sessions with
the user's real accounts, hardware camera/microphone/OS notification behavior,
real Chrome/Edge encryption mix, installed Firefox/NSS versions, SPA login-save
coverage, and public-site auth/download behavior with blocking enabled. CI and
synthetic credentials cannot establish these claims.

References: [Windows DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata),
[SQLite 3.53.4](https://sqlite.org/download.html). The CEF distribution remains
154.0.32 / Chromium 154.0.8037.58; no CEF upgrade or rollback is part of this work.
