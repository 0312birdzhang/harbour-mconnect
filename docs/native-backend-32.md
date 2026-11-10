# Native backend migration - release 1.5.0-32

## Implemented

Ten new C++ operations in `elinux/silica/native_catalog.cpp` are connected to
production `NeteasePlatform` calls through the existing native D-Bus owner:
playlist songs, playlist metadata/share-link lookup, user playlists, daily
recommendations, toplists, ranked windows, regional new songs, lyric bundles,
stream resolution and quality discovery. The original shared Song/Playlist IDs
and account cookie authority are retained. There is no production Dart HTTP
fallback for these operations. Small composition wrappers (liked songs, simple
ranking, daily source label and joined LRC text) remain in Dart and use these
native methods. Login, playlist mutations and VIP status are not migrated here.

Playlist hydration uses batches of 200, canonical ID ordering, and explicit
limits of 10,000 IDs/8 MiB/25 seconds; exceeding limits fails instead of
returning an apparently complete truncated playlist. Removed songs do not
shift later chart ranks. Account-switch protection applies to success cookies
and login-expiry errors. Regional fallback applies only to all-regions new
songs. Native lyric retrieval retains YRC-specific translation and romanization;
YRC parsing and cache writes still belong to the remaining Dart services.

## Verification

- 52 tests covering native catalog/content/search gateways, NetEase content,
  QML media and core adaptation passed. After the final decoder guard change,
  the four new catalog tests were rerun and passed.
- 30 further tests covering NetEase lyric bundles, discovery, library,
  offline cache, recommendation cache and the retained API passed.
- Targeted Dart static analysis: no issues.
- ARM64 native catalog local-HTTP/isolated-D-Bus test passed, including a
  405-ID playlist requiring three hydration batches, out-of-order responses,
  deleted IDs/rank gaps, errors, regional fallback, lyrics, trial rejection,
  quality mapping and cookie capture. Native content and common HTTP/search
  regression tests passed on the replacement phone.
- Live native catalog test on 192.168.3.234: 65 charts, 200-song playlist,
  ranked window beginning at rank 31, metadata, five regions, lyric retrieval,
  stream availability/VIP handling and qualities passed. The real song-detail
  API also accepted the string-ID batch parameter format (two tracks).
- RPM verified: 80 payload files, four aarch64 ELF files, native operation
  markers, font links, launcher/service routes and existing dependencies.
- Installed on 192.168.3.234, aarch64, SailfishOS 5.2.0.22. `rpm -V` clean.
  Installed QML validation passed all 31 components.
- Production core requests passed: charts 65; ranking and library playlist
  200 each; all five new-song regions; stable song metadata. Previous native
  search passed pages 30/30 plus 30 playlists; artist songs 50; album pages
  30/17; album tracks 13. Journal confirmed native catalog/search/content calls.
- Installed native service returned lyric tracks and qualities and correctly
  classified a stream without an available full URL; playlist metadata passed.
- Paused close stopped both UI and core. Desktop reopen created one UI/core
  pair, restored the 30-track queue paused, and production catalog tests passed
  again. Final PIDs at verification: UI 30930, core 31407.

The replacement phone has no logged-in provider accounts. Personalized daily
recommendations and user playlists have isolated gateway/server coverage but
still require a real logged-in device check. No audio was played, no screenshot
was taken, and touch/visual/audio behavior is not claimed as accepted.

## Artifact

`rpms/harbour-mconnect-1.5.0-32.aarch64.rpm`

SHA256: `d0abf9a5235f25a67d8808e040adc8f844d04193e1808958b550ce119c167211`
