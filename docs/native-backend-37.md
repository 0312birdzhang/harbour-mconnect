# Native backend migration - release 1.5.0-37

## Scope

Four more QQ operations in `QqCatalog` (nineteen total):

- `profile`: native profile request and normalization of creator, home,
  profile/userinfo and base/info aliases. Preserve the caller's string uin,
  nickname and supported HTTP(S) avatar URL.
- `vip`: native membership request; map vipType 0/1/2+ to free/VIP/SVIP.
- `userPlaylists`: preserve detail disstid and separate dirid editing identity,
  counts, covers and creator. Compute g_tk from the current session's p_skey,
  including restored cookies. Retain the existing first-200 request contract.
- `like`: native add/delete favorite writes. Cookie required; validate module
  response/rejection, no POST retry, no fallback through Dart HTTP.

Production Dart dispatch is wired into QqPlatform.getUserInfo/getVipStatus/
getUserPlaylists/likeSong. The gateway validates normalized user and playlist
data. Epoch and cookie checks discard stale account responses; profile refresh
also checks again before replacing the cached user. Compatible session storage
and QQ OAuth remain in Dart.

## Verification

- 88 Dart regression tests passed for native QQ, legacy QQ, lyrics, account
  expiry reporting and playlist editing. Final gateway-only rerun: seven passed.
- Dart static analysis passed for the changed production files and gateway test.
- ARM64 fixture passed on the phone in an isolated D-Bus session for nineteen
  operations: profile aliases/Chinese identity/avatar, VIP values, disstid/dirid,
  g_tk, add/delete favorites, rejected writes, empty-cookie rejection and POST
  network failures without retry. Existing lyric/pagination tests also passed.
- Final native write checks require an explicit successful module code and
  reject missing codes or business-level failure results.
- No fixture request uses a real account or sends writes to QQ.

## Package and installed checks

- Complete AOT/native RPM build exited 0. Actual extracted RPM passed checks
  for 80 payload files, four aarch64 ELFs, font links, launcher/runtime and
  all newly introduced QQ endpoint/module markers.
- Installed `harbour-mconnect-1.5.0-37.aarch64` on 192.168.3.234;
  `rpm -V harbour-mconnect` returned no differences.
- Desktop invocation, paused core readiness, queue 30 and all 31 QML component
  validations passed. Component validation is not visual/touch acceptance.
- Installed public-network regression passed: QQ search 30, real playlist 223
  tracks through production Core `librarySongs`, native Chinese LRC 1,433
  characters. Immediate journal lines confirmed native QQ operations.
- The first QQ search failed; a later identical request succeeded. This is the
  previously observed release 35/36 issue, not claimed fixed by this release.
- The installed account probe reported no logged-in QQ account. Actual
  authenticated profile/VIP/user-playlist calls and real favorite writes were
  therefore not verified with a live QQ account. Their production gateway and
  native HTTP behavior passed isolated tests; no real account was modified.
- No automatic playback or screenshots.
- Paused close/reopen passed: old UI and core both exited, the desktop entry
  recreated one UI (PID 53796) and one core (PID 54560), queue 30 restored,
  playback stayed paused and the UI loaded its Maliit input plugin.

Artifact: `rpms/harbour-mconnect-1.5.0-37.aarch64.rpm`.
SHA256: `5721c9efa2184ab32036b9357c5630d5b4eb3cccf3aaadbe09e9b1c61c16cdf7`.

## Remaining

QQ QR/OAuth (including the no-uin profile fallback during OAuth), personalized
daily lookup and add/create/collect playlist writes;
Kugou endpoints; shared repositories, downloads, queue/playback/session and
MPRIS ownership. The private Dart core and Flutter runtime are still required.
