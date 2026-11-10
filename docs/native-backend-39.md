# Native migration - release 1.5.0-39

## Scope

Ten native Kugou catalog operations: song search, artists, popular artist songs,
artist albums, album details, album tracks, chart catalogue, ranked windows,
official recommendation attempt and homepage recommendations.

Native parsing retains hash case, author lists and IDs, album IDs, Unicode
titles, duration units, cover templates, local calendar dates, chart totals and
rank movement. Album tracks paginate in 100-song pages up to the explicit
10,000 bound; repeats/incomplete responses fail. Ranked windows correctly skip
non-aligned offsets across pages. Kugou remains current-chart-only; period
metadata does not claim historical chart retrieval.

The original `status:1, error:""` success shape is explicitly normalized;
non-JSON WAF pages and provider rejections remain errors. The existing official
recommendation-to-homepage fallback and provenance are preserved, with both
network paths now native. Public operations send no account credentials.

Legacy public HTTP is allowed only for explicit fixed GET hosts/paths. Cookie,
Authorization, userinfo, other hosts/paths and POST are rejected. Homepage uses
verified HTTPS. TLS settings are never disabled.

## Evidence

- 20 native gateway/QQ compatibility/recommendation tests and 73 existing Kugou
  protocol/cache tests passed (93 total); static analysis passed.
- Phone ARM64 fixture passed all ten operations: hash/author/cover parsing,
  201-track albums, offset paging, wrappers, provider/WAF rejection and homepage
  deduplication. The real empty-error success shape is included as a regression.
- Standalone live native tests passed: search 5, charts 27, ranked window 5,
  homepage 30; associated album 11 tracks, artist popular songs 5, artist albums
  5. Official recommendation unavailable, so homepage fallback remains correct.
- Native HTTP isolation/allowlist tests and the full 23-operation QQ native
  fixture passed against the updated common transport on the phone.

Package/installed checks will be recorded after completion. No automatic audio
playback, screenshots or real-account mutations are performed.

## Full objective

This is intermediate progress toward removing all app Dart code/runtime, not
completion. See `native-migration-audit.md`. Remaining work includes Kugou
signed playback/lyrics/login/library/writes, QQ OAuth/import metadata and native
storage, downloads, queue/player, MPRIS, core dispatch and standalone packaging.
