# Native backend migration - release 1.5.0-33

## Scope

Thirteen new native NetEase operations: QR key, QR status, SMS request,
phone-code login, profile, VIP status, like/unlike, add/remove track, playlist
create/rename/delete and collect/uncollect. The production `NeteasePlatform`
uses `Platforms.NeteaseAccount`; no production NetEase HTTP request goes
through Dart Dio. The old HTTP branches remain for non-Sailfish tests.

C++ validates exact argument schemas, builds the existing form requests,
normalizes QR and provider rejection states, merges response/header cookies,
and requires a real profile for successful QR/phone authentication. Missing
profiles, malformed responses and rejected writes are not treated as success.
Requests have a shared 18-second budget and a 1 MiB response limit. POST is
never retried. Logs contain operation names only, not phone numbers, codes,
QR keys, cookies or nicknames.

The Dart session store and existing keys remain authoritative. Account epoch
and request-cookie checks prevent late account results from resurrecting a
logged-out or switched user. The newest QR key is bound to its epoch, so old
keys are not polled. Restore-session reads also discard results after logout.
The session persistence, polling timer, composition/model adapters, other
providers and playback/download/library orchestration still remain in Dart.
This is not complete removal of the Dart/Flutter core.

## Verification

- 33 gateway and workflow tests passed: native account/catalog/content/search,
  QML login controller and playlist editing. New account tests cover QR states,
  rejected writes, incomplete login, logout/account-switch races, retired keys,
  expiry reporting and strict result validation. Legacy HTTP was blocked.
- 73 additional tests passed: auth provider, session storage/expiry/reporting,
  platform playlists, retained NetEase API and artist/album content.
- Targeted static analysis: no issues.
- ARM64 local-server + isolated-D-Bus test passed all 13 operations, including
  header/body cookies with embedded equals, Unicode form names, CSRF, rejected
  writes, malformed input, missing profiles and no POST retry.
- Live isolated test on 192.168.3.234 passed QR-key and anonymous-profile access.
  No SMS, real phone login or real account mutation was sent.
- RPM verification passed: 80 files, four aarch64 ELF files, native account
  marker, existing dependencies, fonts and single visible Silica launcher.
- Installed on 192.168.3.234, SailfishOS 5.2.0.22, aarch64; `rpm -V` clean.
  All 31 installed QML component checks passed.
- Production Core login workflow generated a PNG QR payload and reached waiting
  state, then cancelled with unchanged account state. The image was checked in
  memory only; no screenshot was taken and no QR key/image was printed or saved.
  Native journal markers confirmed profile, qrKey and qrCheck calls.
- Installed regressions passed: charts 65; ranked and library playlist 200;
  five new-song regions; search pages 30/30 and playlists 30; artist songs 50,
  album pages 30/17, album songs 13; original IDs/metadata preserved.
- Paused close stopped UI and core. Desktop reopen restored one UI/core pair
  and 30 queued songs paused; native login generation/cancellation passed again.
  An immediate probe preceded D-Bus registration; a subsequent probe passed on
  the same process pair. Final observed PIDs: UI 36823, core 37825.

Actual scanned login, SMS/phone login, logged-in profile/VIP and account writes
still require user-directed device acceptance. No audio was played. Visual,
touch and audio acceptance is not implied by these checks.

## Artifact

`rpms/harbour-mconnect-1.5.0-33.aarch64.rpm`

SHA256: `227881c6eef6757875125c365520a307319c16751f51b8f4174230206082eea7`
