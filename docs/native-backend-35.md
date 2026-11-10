# Native backend migration - release 1.5.0-35

## Scope

Twelve QQ native operations: song search, public playlist search, stream URL,
static quality options, artist details/top songs/albums, album details/songs,
regional new songs, chart catalogue and ranked windows. Production dispatch is
`Platforms.QqCatalog`; these methods never retry through Dart HTTP.

Preserved: mid/string identity, guarded old/new song wrappers, artist/album IDs,
Unicode metadata, album language and local-calendar dates; chart groups and
periods; score-vs-rank distinction, movement/new-entry metadata; eight playback
filename mappings, cookie-derived uin and per-process playback GUID. Missing
purl remains unavailable, not a speculative membership diagnosis. Quality
options remain static and do not promise account playback rights.

Fallbacks are bounded and native. Rejected desktop song search may use mobile
search. Artist albums use filtered mobile album search when the album module
fails, with correct requested paging. Current legacy charts read at most fifty
tracks per page and can fall back to modern detail. Explicit periods use modern
only: failure cannot silently show current-period legacy data. Repeated pages
and incomplete fetches fail explicitly. Public reads preserve the caller's QQ
session and do not store visitor Set-Cookie values as a login indicator.

QQ QR/OAuth, LRC/QRC retrieval and decryption, private playlists and personalized
daily lookup, account writes and session storage remain Dart. The common daily
chart fallback and simple ranking wrapper now use the native ranked method.
Kugou and playback/download/library business orchestration also remain Dart.

## Encoding finding

Live public playlist search declared application/x-javascript;charset=utf-8 but
truncated UTF-8 inside `data.list[0].introduction` and `[1].introduction`.
Explicit UTF-8 query parameters did not change it. This is response encoding,
not evidence of a missing font. Native parsing now normalizes invalid UTF-8 so
unused introductions cannot reject otherwise valid playlist IDs/titles. Lost
source bytes are not recoverable; syntax and module-error checks remain strict.
A byte-truncation fixture and live playlist search both passed after the fix.

## Verification

- 78 tests passed: native QQ bridge, existing QQ catalogue/platform/API/guards,
  native Bilibili and NetEase content/search bridge regressions.
- 43 more tests passed: QML discovery/media/core/library, recommendation cache,
  session-expiry reporting and retained QQ QRC path. Total 121 tests.
- Targeted static analysis: no issues.
- ARM64 isolated HTTP/D-Bus fixture passed all twelve operations, including
  double-encoded JSON, malformed UTF-8, wrappers/IDs, album fallback filtering
  and paging, metadata/date/language, six regions, 300 tracks in six fifty-item
  pages, score/rank movement, current-to-modern fallback, explicit history,
  repeated-page detection, typed errors, filenames/uin/GUID and no POST retry.
- Live native service: 30 charts, 100 legacy-ranked tracks, artist metadata,
  three albums, thirteen album tracks and three song search results.
- Live extra tests after the encoding fix: three public playlists, three
  current-period tracks, five artist top songs and all six regional new-song
  requests. Anonymous stream probe correctly returned unavailable (no purl).
- Complete RPM verified: 80 files, four aarch64 ELFs, QQ/encoding-fix markers,
  previous native module markers, runtime/font dependencies and launcher rules.
- Installed on 192.168.3.234, aarch64 SailfishOS 5.2.0.22; rpm -V clean. All 31
  installed Silica component checks passed.
- Production QQ Core requests passed: 30 songs, 29 public playlists, 30 charts,
  300 current-period ranked tracks, six regions; 30 artist songs, album pages
  30/30, thirteen album tracks, date and stable song metadata.
- One initial installed song search failed; the identical request then passed,
  and it passed again after close/reopen. No root cause for that single failure
  was established; it is not claimed fixed by the encoding change.
- Installed regressions: NetEase search pages 30/30 plus 30 playlists; Bilibili
  26 search videos and one part, retaining BVID:CID and metadata.
- Paused close stopped UI and core, then desktop reopen restored one pair with
  30 queued tracks paused. Complete QQ catalog probe passed again. Final
  observed PIDs: UI 62940, core 64195.

No audio was played, no screenshot was taken, and no account was modified.
Authenticated stream availability, past-period live data and visual/touch/audio
acceptance remain unverified. The isolated history fixture passed; real current
period data was checked, not a manually selected historical chart.

## Artifact

`rpms/harbour-mconnect-1.5.0-35.aarch64.rpm`

SHA256: `c8bd7d4982a68f4f53094e5e26f4ee91aa498bc242cdfbfe2715827b4b6ea9f8`
