# Native backend migration

The only packaged UI is Silica/QML. The compatibility command
`harbour-mconnect` opens that UI; the previous Flutter desktop ID is a hidden Silica compatibility entry for
MPRIS and existing shortcuts, not a second launcher.
The private `/usr/libexec/harbour-mconnect-core` starts the remaining Dart
services with `MCONNECT_SILICA_CORE=1`. The runtime remains necessary until
these services have native replacements. Existing data and platform IDs stay
unchanged.

## First native module: lyrics

`elinux/silica/native_lyrics.cpp` implements read-only SQLite cache access and
LRC parsing. `CoreBridge` runs it on a worker thread before dispatching a lyric
request to the Dart service. It supports cached envelope records, UTF-8,
multiple timestamps, offsets and same-time original/translation pairing.
The callback rejects results after the track changes. Missing, locked, invalid
or unsupported cache records fall back to the existing provider. Local track
sidecar/embedded lyrics retain their existing precedence. No database schema
or cache-writing behavior changes.

## Second native module: artwork

`native_artwork.cpp` owns original-image HTTP downloads, coalesced requests,
32 MiB/20-image memory storage, 200 MiB bounded disk storage, corruption checks,
and the loopback image server. Its MCA1 envelope is byte-compatible with the
previous Dart cache. Existing MCA files and legacy indexed images are reused;
new writes are atomic `.mca` files under the existing private `.artwork` folder
with `.nomedia`. Disk reads, hashing, writes and cleanup run on worker threads.
Images are limited to 16 MiB; HTML/error bodies are rejected. Local image/MCA
sources are supported. The loopback server binds only IPv4 localhost and uses
an unpredictable process token.

QML metadata methods return original sources only. `CoreBridge` maps them to
native image URLs without starting the Dart artwork cache. MPRIS requests its
artwork URLs through `org.sailfishos.Mconnect.Artwork.MediaUrl` on the Silica
session-bus owner. The Dart artwork class remains in the source tree for the
remaining legacy dependencies/tests; these production Silica/MPRIS paths no
longer invoke it. Provider methods not covered by the native modules below and playback
orchestration still use the Dart core.

## Third native module: platform HTTP and models

`platform_http.cpp` provides asynchronous GET/POST transport, percent-encoded
query construction, an 18-second total request budget, bounded response bodies,
same-origin GET redirects and a single bounded retry for transient GET failures.
POST requests are never retried. Requests carry an explicit account cookie;
Qt cookie-jar loading/saving is disabled to avoid sharing credentials between
accounts. TLS verification stays enabled. HTTP error bodies and credentials
are never logged.

`platform_models.cpp` introduces typed artist/album/song/playlist wire models.
The NetEase parser supports both old and modern response field names and formats
numeric IDs as exact integer strings, retaining platform IDs, cover sources,
album/artist IDs, duration, track number and fee flags.

The Silica D-Bus owner exports `Platforms.SearchNetease` with delayed replies.
NetEase song and playlist searches in the production Dart core delegate to this
service; the Dart bridge only validates the normalized records and inserts them
into the existing result/playback/library flow. Native failures are surfaced,
not silently retried through Dart HTTP. Existing session storage remains the
account authority; response cookies are merged only if that request's account
has not changed in the meantime. Login-expired results use the existing central
session-expiry reporter. No account files or offline IDs are migrated/rekeyed.

QQ/Kugou/Bilibili searches and NetEase endpoints not listed in the native
modules below remain in Dart for subsequent provider migrations. The legacy NetEase search path is retained for
non-Sailfish host tests; the production `MCONNECT_SILICA_CORE=1` process always
selects the native client.

## Fourth native module: NetEase artist and album content

`native_content.cpp` implements artist details, artist top songs, paginated
artist albums, album details and complete album track lists. Artist details
prefer the POST head-info endpoint and fall back to legacy artist GET; empty
top-song responses also fall back to legacy `hotSongs`. The operation shares a
10-second total budget, giving the primary request at most five seconds when
a fallback exists. POST is never retried. Login-expired responses stop fallback.
A successful empty primary response stays distinct from two failed requests.

The shared artist/album models now retain biographies, covers, counts,
artist IDs, release dates, company and tag-derived genre. Unknown fan counts
remain absent. Release timestamps use the original local-calendar semantics.
Album tracks come from top-level `songs`, with stable track-number sorting only
when every track supplies a number. Lists of up to 2,000 tracks retain all IDs;
QML paging and offline matching still use the existing result-store path.

The Dart gateway only validates normalized records and reconstructs the
existing model classes. Search and content calls share the same cookie update
and stale-account expiry guard. The production process has no fallback to Dart
HTTP for these five operations. The endpoint override in the C++ constructor is
for isolated local-server tests; D-Bus callers cannot supply a URL.

## Fifth native module: NetEase catalog, streams and lyric retrieval

`native_catalog.cpp` owns ten operations: playlist tracks, playlist metadata,
user playlists, daily recommendations, toplists, ranked windows, regional new
songs, complete lyric tracks, playback URL resolution and available qualities.
The production gateway calls `Platforms.NeteaseCatalog`; it reconstructs the
existing model classes and shares the stale-account cookie/expiry guard.
There is no production Dart HTTP fallback for these operations.

Playlist detail hydrates missing IDs in batches of 200, restores canonical
`trackIds` order even when the server returns batches out of order, and omits
removed tracks without shifting later chart positions. Ranked requests hydrate
only their requested window. Playlist metadata/import does not hydrate tracks.
A 25-second operation budget fits inside the existing core request deadline;
responses and playlists are bounded at 8 MiB and 10,000 IDs. Exceeding these
limits returns an explicit failure rather than a truncated successful playlist.

Regional new songs retain all five supported area IDs and client-side limits.
Only the all-regions request may use the personalized-new-song fallback;
login expiry never triggers fallback. Lyrics retain original, translation,
YRC, YRC-specific translation and romanization independently. Empty lyric
tracks stay optional. This moves provider retrieval and selection, not the
remaining YRC parsing/cache writes, into C++.

Stream resolution rejects trial-only URLs and preserves VIP/unavailable errors.
Quality mapping uses the existing eight `AudioLevel` values. Tests resolve URLs
without sending audio to the player. Library IDs, existing cache/offline paths,
session persistence, recommendation cache and playback orchestration remain
unchanged. Liked songs, simple ranking, daily-result wrappers and lyric-string
wrappers use these native operations transitively; their small Dart wrappers
remain. This does not migrate login, playlist mutations, or the other providers.

## Sixth native module: NetEase account and write APIs

`native_account.cpp` owns thirteen operations: QR key/check, SMS request,
phone-code login, profile, VIP status, like/unlike, track add/remove, playlist
create/rename/delete and subscribe/unsubscribe. Every request is a bounded
asynchronous POST; mutations and SMS are never automatically retried.
Arguments are validated against exact operation-specific schemas. IDs remain
strings; form encoding preserves Unicode, plus, ampersand and hash characters.
Responses are normalized and rejected operations never masquerade as success.

QR/phone login success requires a valid user profile. When the login response
omits it, C++ fetches the profile within the same 18-second operation budget,
using the newly merged header/body cookies. A missing profile is an error,
not a successful account with no identity. QR waiting/scanned/expired/failure
states are distinct from login expiry. Cookie values preserve embedded equals;
Set-Cookie attributes never become account cookies. Diagnostic markers contain
only operation names, not phone numbers, CAPTCHA values, keys or cookies.

The Dart adapter retains the current session store and composition wrappers.
An account epoch plus request-cookie check prevents a late login/profile result
from restoring a logged-out or switched account. The active QR key is bound to
its epoch; retired keys are never polled. Restore-session reads also reject
results after logout. Existing storage keys, file formats and offline identities
remain compatible. No production NetEase HTTP operation uses Dart Dio now;
legacy branches remain for non-Sailfish tests. Session storage, polling timer,
queue/download orchestration and other providers still require migration.

All SMS and playlist mutation verification uses a local server on an isolated
D-Bus session. Real-server tests are restricted to QR-key creation and anonymous
profile access. Actual account login and mutations need user-directed acceptance.

## Seventh native module: Bilibili

`native_bilibili.cpp` owns search, WBI key retrieval/signing, DASH audio
selection/qualities, video parts, paginated favorite folders/resources,
subtitle-to-LRC retrieval, trusted share-link metadata, QR generation/polling,
profile retrieval and favorite/like writes. These are exposed as thirteen
`Platforms.Bilibili` operations. The production adapter never falls back to
Dart HTTP. Unsupported provider capabilities remain unsupported.

WBI public keys are cached in the native process for twelve hours. Anonymous
nav -101 is accepted only for retrieving signing keys. Parameters retain the
existing filtering, lexical ordering, percent encoding and MD5 signature.
Search bootstraps anonymous fingerprint cookies and uses the existing unsigned
fallback only when the signed response explicitly returns a verification
voucher. Repeated verification demands surface to the user.

BVID and optional `:cid` identities stay unchanged, as do public media headers
used by the existing audio/download code. Only DASH audio is returned; no video
fallback is introduced. Quality selection and the legacy bitrate representation
are retained. Favorite pagination filters unavailable entries, detects repeated
pages and fails explicitly at the time/size/page limits. Entirely unavailable
pages may still lead to valid later pages. Limits: eighteen seconds, eight MiB,
10,000 normalized rows, 200 folder pages or 500 resource pages.

Subtitle retrieval prefers Chinese, omits all cookies and ignores CDN cookies.
Production subtitle URLs must be HTTPS on bilibili.com/hdslb.com subdomains;
only the C++ test constructor permits the matching loopback fixture origin.
JSON subtitles become bounded timestamped LRC, with missing subtitles distinct
from failures. All provider JSON parsing happens in C++; Dart only validates
normalized models and retains the compatible JSON-map session store.

QR completion requires both SESSDATA and a verified profile. Account epochs
reject late results after logout/restore, and retired QR keys are not polled.
Read-only results never overwrite a newer cookie snapshot; login expiry is
reported only for the session which made the request. All writes require the
existing CSRF cookie and are never retried. Real account mutations are excluded
from automated device tests; isolated local-server tests exercise them instead.

## Eighth native module: QQ catalog and stream resolution

`native_qq.cpp` owns twenty-three operations: song/playlist search, stream resolution,
static quality options, artist detail/top songs/albums, album detail/songs,
regional new songs, chart catalogue and ranked windows. Production calls use
`Platforms.QqCatalog` without a Dart HTTP fallback. Release 36 also moves
playlist tracks, the existing first fifty favorite songs and lyric/QRC requests
to C++. Release 37 adds profile, VIP status, user-playlist listings and
like/unlike. Release 38 adds song-to-playlist writes, playlist creation,
collection/uncollection and personalized daily-playlist lookup. QQ OAuth,
including its no-uin profile fallback, and share-import metadata parsing remain
in Dart.

The native add endpoint mutates through GET, so its request explicitly disables
both retries and redirects. Other GET reads retain their existing transport
policy. Create/collect use percent-encoded form bodies, preserve current cookie
uin/g_tk and require explicit success. Creation returns an edit directory ID;
the adapter refreshes native user-playlist results to obtain a real detail ID,
and returns null if no public playlist can be identified. Daily lookup scopes
the matching 今日私享 card before extracting its ID, skips HTTP without a
session cookie and retains the existing recommendation provenance/fallback.

Native user-playlist parsing preserves both public detail IDs and separate edit
directory IDs. Listing retains the existing request for up to 200 entries; it
does not claim unbounded playlist-library pagination. The g_tk value is derived
from the current caller-owned p_skey, including restored sessions. Profile data
retains the existing creator/profile/info/home aliases and fallback nickname.
VIP types map to free/VIP/SVIP. Like/unlike requires a cookie and a successful
provider response; rejected writes fail, and POSTs are never retried. The Dart
bridge rejects replies from a switched/logged-out account before accepting
profile or library results. Tests of account writes use only a local fixture.

Playlist tracks use modern 200-song pages, falling back to the legacy detail
endpoint only before any modern page has been accepted. Repeated pages,
known missing tracks, failures on later pages and the 10,000-song bound fail
explicitly instead of returning a partial successful list. LRC and translation
stay separate. The optional QRC request only contributes readable XML, and a
failed QRC request retains an available LRC. The existing local-cache-first
lyrics service and offline playback selection remain unchanged.

Native parsers retain old/new song aliases, guarded songInfo/song/musicData/data
wrappers, exact mid/string IDs, album/artist IDs, Unicode metadata, language and
local-calendar release dates. Responses may be JSON or encoded JSON strings. Live playlist search was found
to declare UTF-8 while truncating introduction fields mid-character; encoding
normalization keeps these unused fields from breaking the complete response,
while JSON syntax/module validation remains strict.
Desktop song search may fall back to the mobile method on a module rejection.
Artist albums prefer the catalogue module, with artist-filtered mobile search
as fallback, keeping the requested page instead of repeating the first page.
Artist avatars use the canonical singer_pmid or existing cover fallback.

Current charts prefer legacy pages of at most fifty tracks, preserving movement
only when cur_count is a rank sequence. Score-based charts use index ranks.
The modern module handles explicit periods and rankType movement. An explicit
period is never silently replaced by a current legacy chart on failure. Repeated
pages and incomplete requests fail rather than reporting partial success.
Group ordering, periods and chart counts retain their provider semantics.

CDN/vkey resolution uses the existing eight filename mappings, cookie-derived
uin and a native process GUID. A missing purl is unavailable, not an invented
VIP diagnosis. Static quality options retain their original meaning and are
not a claim that every listed quality is playable by the account.

Transport carries caller-owned cookies with no automatic cookie jar. Public
read responses do not modify the stored QQ session or turn a visitor tracking
cookie into a personalized-daily login indicator. Epoch/cookie guards reject
stale streams and expiry notifications. All operations share eighteen seconds
and bounded responses (eight MiB); POST is never automatically retried.

## Subsequent modules

1. Extend the native transport/models to the remaining provider endpoints;
   migrate credential persistence only with a compatible authoritative store.
2. Migrate provider search/details/playlist/stream-resolution APIs one platform
   at a time, retaining existing accounts, downloaded IDs and provider behavior.
3. Migrate library/download repositories, recommendation
   caches, with compatible storage and atomic writes.
4. Move playback ownership, queue/session restoration and MPRIS to the native
   service; reuse the existing GStreamer backend.
5. Remove the Dart host, AOT assets and Flutter runtime dependency once all
   production methods are migrated and verified on the device.

Every native method must be wired to the production QML request path, tested
against cache/error cases, and verified in the installed RPM. A module scaffold
alone does not count as migration.
