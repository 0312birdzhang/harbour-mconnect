# Native backend migration - release 1.5.0-36

## Scope

Three additional production QQ operations in `QqCatalog`:

- `lyrics`: native HTTPS, JSON decoding, strict base64/UTF-8 decoding, separate
  LRC/translation and optional readable QRC. Encrypted QRC remains unsupported.
- `playlist`: modern detail service with 200-song pages and first-page legacy
  fallback. Later-page errors, repeated pages and known incomplete results fail
  explicitly. Existing mid, artist and album identities are retained.
- `liked`: the existing first fifty favorites, preserving the existing contract.
  This is not a claim that complete favorite-library pagination was implemented.

The Silica process owns networking/parsing. The transitional Dart adapter only
decodes normalized results and retains session guards, cache precedence and
existing user-visible failure behavior. Native errors do not retry via Dart HTTP.

## Verification

- 71 Dart tests passed for QQ gateway, platform, catalogue, HTTP guards and QRC.
- Dart static analysis passed for both changed production files and gateway test.
- 21 additional regressions passed for playlist editing, session-expiry
  reporting and the Bilibili native gateway (92 Dart tests in total).
- ARM64 C++ fixture passed on the phone in an isolated D-Bus session: all fifteen
  operations, 405-song paging, repeated page rejection, legacy fallback,
  incomplete legacy response rejection, base64 Chinese LRC, plain translation,
  readable/encrypted QRC and optional-QRC network failure retaining LRC.
- Final ARM64 fixture rerun also passed zero-but-nonempty total handling and
  rejection of a later modern page failure without starting a legacy fallback.
- Fixture traffic is local only; no real account mutations or audio playback.

## Package and installed checks

- Complete AOT/native RPM build exited 0. Actual RPM extraction verification
  passed: 80 payload files, four aarch64 ELFs, launcher/font/runtime checks and
  the three newly added native endpoint/module markers.
- Installed `harbour-mconnect-1.5.0-36.aarch64` on 192.168.3.234;
  `rpm -V harbour-mconnect` returned no differences.
- Desktop invocation, paused core readiness (queue 30) and all 31 QML component
  validations passed. These are component checks, not visual acceptance.
- Installed live probe passed: QQ search 30 songs, native Chinese LRC 1,433
  characters, and a real 223-track playlist through Core `librarySongs` into
  the production Dart-to-C++ path. Immediate logs confirmed native `lyrics`
  and `playlist` calls. The sampled track had no translation/readable QRC;
  those paths were validated with fixtures, not claimed live-verified.
- One QQ search request failed, and a later identical request succeeded. This
  was also observed in release 35; its cause remains unestablished and is not
  claimed fixed. The initial live probe also had a Python dbus-send-output
  decoding error; the probe was corrected without changing app code.
- No automated playback, screenshots or real account writes were performed.
- Paused close/reopen passed: both old UI/core processes exited, desktop
  invocation recreated one UI and one core, queue 30 restored and playback
  remained paused. Final observed UI PID 20601, core PID 21339.

Artifact: `rpms/harbour-mconnect-1.5.0-36.aarch64.rpm`.
SHA256: `c3f269156fc0d1533462a56b73fdd3ecf852573ba60876ffa77269bee293d215`.

## Remaining

QQ OAuth, profile/VIP, user playlist listing, personalized daily lookup and
playlist/favorite writes; Kugou APIs; shared storage/download/queue/playback and
MPRIS ownership. The private Dart runtime is still required. This release does
not claim that all remaining migration has finished.
