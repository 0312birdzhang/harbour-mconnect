# Native backend migration - release 1.5.0-34

## Implemented

The production Bilibili adapter delegates all supported HTTP paths to native
C++ through thirteen `Platforms.Bilibili` operations: search, stream, qualities,
QR key/check, profile, favorite folders, playlist/video parts, like, add/remove
favorite, lyrics and share metadata. Fingerprint bootstrap, WBI key caching,
signing, response parsing and subtitle-to-LRC conversion are native. There is
no production fallback to Dart HTTP. Unsupported provider features remain
unsupported. The compatible JSON-map session store and UI polling/composition
remain Dart; this does not remove the Dart/Flutter core.

Stable BVID and optional `:cid` values, media headers and legacy quality mapping
are preserved. Favorites read subsequent pages, filter unavailable media and
reject repeated pages. Subtitle requests omit cookies, retain signed query
parameters and ignore CDN cookies; production URLs are restricted to trusted
HTTPS subtitle domains. Account epochs, auth-token checks and QR-key binding
reject stale account results. Read-only responses do not overwrite newer cookie
snapshots. Writes use CSRF and are never automatically retried.

## Verification

- 36 tests passed for the new native Bilibili bridge, retained Bilibili adapter
  and existing NetEase account/catalog/content/search bridges. Coverage includes
  native-only calls, BVID/CID metadata, folder permissions, stored-session JSON,
  QR states, stale login/expiry protection, rejected writes and wire validation.
- 52 further tests passed for QML login/media/core adaptation/playlist editing/
  library, auth provider, session storage and lyric cache. Total: 88 tests.
- Targeted static analysis: no issues.
- On-device ARM64 isolated HTTP/D-Bus fixture passed all thirteen operations,
  including the published WBI signature vector, anonymous nav keys, voucher
  fallback, key reuse, explicit CID, DASH audio selection, favorite pagination,
  unavailable-only pages, pagination stall, Chinese subtitle preference,
  preserved signed subtitle URL query and no CDN cookies, QR cookie/profile
  handling, CSRF and rejected writes, no POST retry, and trusted share hosts.
- Live isolated service test: four search results, one video part, resolvable
  audio URL and QR key. No audio was played and no account write was sent.
- Complete RPM built and verified: 80 payload files, four aarch64 ELF files,
  native Bilibili marker, existing font/runtime dependencies and launcher rules.
- Installed on 192.168.3.234, aarch64 SailfishOS 5.2.0.22; rpm verification clean.
  All 31 installed QML component checks passed.
- Production Core: Bilibili search returned 26 valid videos; one part retained
  its original BVID:CID and artist metadata. QR generation reached waiting and
  cancellation preserved all account states. QR PNG was checked only in memory;
  no screenshot, QR image/key or signed CDN URL was printed or saved.
- Installed native stream URL successfully served a 1,024-byte audio range with
  existing media headers. Data stayed in memory and was not played or saved.
- NetEase installed regression: search pages 30/30 and playlists 30; 65 charts;
  ranked/library playlist 200 each; all five new-song regions passed.
- Paused close stopped both UI and core. Desktop reopen restored exactly one
  UI/core pair with 30 queued songs paused; Bilibili search/part tests passed
  again. Final observed PIDs: UI 46843, core 47404.
- Journal confirmed production native Bilibili search, playlist, QR key/check
  and stream calls along with the existing native NetEase operations.

Real scanned login, logged-in favorite folder retrieval, likes/favorite writes
and user visual/touch/audio acceptance remain unverified. Authenticated writes
were exercised only against the isolated local fixture. Actual subtitle content
was verified with the signed-URL local fixture; not by manually choosing a real
captioned video. No system volume or audio-route diagnostics were performed.

## Artifact

`rpms/harbour-mconnect-1.5.0-34.aarch64.rpm`

SHA256: `01a2f8ca59600cfde39e8b2dfeb35be4f623b09f7e8e20dd3c1e5f889e17c0fa`
