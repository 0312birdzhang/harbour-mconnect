# Native backend migration - release 1.5.0-38

## Scope

Four more QQ operations (twenty-three total):

- `add`: add a song mid to an edit-directory ID. This provider mutates through
  GET, so neither retry nor redirect is allowed for this request.
- `create`: native form POST with Unicode/plus/ampersand encoding, current uin
  and p_skey-derived g_tk. Validate success and return the edit directory ID.
  The existing adapter then refreshes native user-playlist results to identify
  the public detail ID; never fabricate a public ID from dirid.
- `collect`: native collect/uncollect form POST, preserving optype and detail
  ID. Explicit success required; contradictory business failures are rejected.
- `dailyId`: native HTML request and scoped 今日私享 playlist-ID extraction.
  Do not issue a request for an empty cookie. Recommendation composition,
  account/cache guards and honest chart-fallback labels remain in Dart.

The shared native HTTP transport now accepts an explicit safe-GET policy.
Default behavior stays unchanged; only mutating GET callers opt out of replay
and redirect. TLS verification, same-origin read redirects, cookie isolation,
timeouts and payload bounds remain enabled.

## Verification

- 89 Dart tests passed across the native gateway, QQ platform/catalogue/API
  guards, lyrics, session-expiry reporting and playlist editing.
- Dart static analysis passed for both changed production files and gateway
  tests. Gateway tests cover successful/rejected writes, refreshing public/edit
  identities, missing created-playlist resolution and daily provenance.

- ARM64 QQ fixture passed on the phone in an isolated D-Bus session for all
  twenty-three operations, including Chinese/plus/ampersand form encoding,
  explicit rejection and missing-success responses, missing creation directory
  IDs, mutating GET/POST failures without retry, rejected GET redirect, matching
  daily-card scoping and anonymous lookup with no HTTP request.

- Shared native-platform/HTTP fixture also passed on-device in an isolated
  session: stable IDs/Unicode models, ordinary GET retry, POST no retry,
  same-origin redirects, cross-origin rejection, bounds, timeout and account
  isolation. No external traffic was used for these fixture checks.

## Package and installed verification

- Complete AOT/native RPM build exited 0. Actual RPM extraction checks passed:
  80 payload files, four aarch64 ELFs, font links, launcher/runtime dependencies
  and all four newly added endpoint markers.
- Installed `harbour-mconnect-1.5.0-38.aarch64` on 192.168.3.234;
  `rpm -V harbour-mconnect` reported no differences.
- Desktop invocation, paused readiness (queue 30) and all 31 QML component
  validations passed. These checks do not establish visual/touch acceptance.
- Installed new-operation guards passed: add/create/collect with deliberately
  empty cookies rejected before HTTP; anonymous daily lookup returned null.
- Real public QQ regression passed: search 30 songs, production `librarySongs`
  loaded a 223-song playlist, native LRC returned 1,433 characters. Immediate
  logs confirmed native dailyId/search/lyrics/searchPlaylists/playlist dispatch.
- The first QQ search failed and the repeated request passed. This is the
  existing release 35-37 issue and is not claimed fixed here.
- The phone reported no logged-in QQ account. Authenticated daily lookup and
  actual add/create/collect writes were verified with fixtures only, not with
  a real account. No automated playback, screenshots or real account changes.
- Paused close/reopen passed: both old UI/core processes exited, one new UI
  (PID 59912) and one core (PID 60777) started, queue 30 restored, playback
  remained paused and the UI loaded the Maliit input plugin.

Artifact: `rpms/harbour-mconnect-1.5.0-38.aarch64.rpm`.
SHA256: `07825465099aab4a793e6ca9c96c220dce8b79ccca11a0d0172d0930d5e8a43b`.

## Remaining

QQ QR/OAuth and share-import metadata; Kugou endpoints; shared repositories,
downloads, queue/playback/session and MPRIS ownership. The private Dart host and
Flutter runtime remain necessary. Real account writes are not performed by
automated verification.
