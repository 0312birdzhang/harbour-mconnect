# Native C++ migration audit

Updated on 2026-11-09. The migration replaces the Dart business implementation
while retaining the Silica/QML interface, account isolation, storage formats,
service identities and existing application entry points.

The canonical source is
`D:\Documents\ChatGPT\sfos-music\flutter-sailfishos\apps\mconnect`.
The host-test mirror is
`C:\Users\99188\Documents\Codex\workspaces\mconnect-native-migration\mconnect`.
The initial synchronization checked baseline hashes, copied 98 changed or new
files with zero conflicts and backed up 36 replaced files. Packaging fixes
have also been synchronized into the mirror.

## Implementation and evidence

| Requirement | Current evidence | Status |
| --- | --- | --- |
| Native core dispatch | `main.cpp` constructs `NativeStorage` and `NativeCoreRuntime`. All 53 QML requests are registered and exercised through the real D-Bus service in `native_runtime_test.cc` | Source and host fixtures verified. Release 51 device startup reported 53 methods |
| Four native providers | NetEase, QQ, Kugou and Bilibili use native HTTP, normalization, signing, authentication, account/library operations and error handling | Source and provider fixtures verified. Jailed NetEase search passed on the device in release 51. Live account and playback coverage remains open |
| Compatible user data | Native Hive 2.2.3 frame handling, SQLite schema migration, credentials, playlists, favorites, history, recommendation caches and playback memory | Native storage fixtures and Dart-to-C++-to-Dart reopen test passed. Release 51 device installation preserved application data |
| Native downloads and local music | Download restoration, bounded retries, policy scheduling, resumable transfers, cached-file selection, scanning, TagLib metadata and local artwork | Host fixtures passed |
| Native lyrics and source matching | LRC/QRC/KRC parsing, offsets, cache-first loading, account cancellation and fallback source matching | Host fixtures passed |
| Native playback and MPRIS | `NativePlayer`, GStreamer C ABI, native activation, stable MPRIS track paths and loopback artwork. Restored playback remains paused | Host regressions and jailed silent local-WAV playback, advancing position and MPRIS play/pause passed on the device in release 51 |
| Native production build | `elinux/CMakeLists.txt`, the YAML recipe and preserved spec commands build `mconnect-silica` and `libmconnect_audio.so` | Sailfish SDK compilation, production moc generation, linking and release 51 RPM build passed |
| Native package contents | `tools/verify-rpm.py` checks the two AArch64 ELFs, dependencies, RPATH, compatibility symlinks, launchers, QML and service IDs | Actual RPM payload verification passed. rpmlint reported zero errors and warnings |
| Native installed runtime | Release 51 installation on Sailfish OS 5.2.0.22, package digests, Sailjail confinement, normal D-Bus activation, Core responses, MPRIS and foreground startup | Installed runtime checks passed. Extended live-provider coverage and physical UI acceptance remain open |
| Obsolete source retirement | `lib` retains 245 Dart files as compatibility references and test inputs | Retirement pending package/runtime compatibility verification |

The latest completed host run passed 30 C++ suites. The audio suite compiles
the production GStreamer C ABI against Windows GStreamer 1.28.8 and uses a
synthetic local Ogg file with `fakesink`. It verifies paused position, seek,
rate, latched EOS/error behavior, replay, reopen and shutdown. Native player
integration fixtures retain their deterministic audio substitute.

A Clang AArch64 syntax pass
passed all 35 native source files against cached Sailfish Qt 5.6.3 headers.
The syntax pass uses empty generated moc includes. Production moc generation,
linking and dependency resolution have also passed in the Sailfish SDK build.

The Dart compatibility run passed 119 tests. Its Windows-only D-Bus fixture
adapts Linux ancillary socket operations to local TCP for the isolated test
daemon. Provider assertions retain their business checks. The original storage,
session, expiry and playback-memory tests run alongside those fixtures.
`package_config.json` was restored after execution, and `pubspec.lock` matches
the saved Sailfish lockfile.

## WSL SDK packaging

Full-access execution runs as `bird-pc\99188`. `Ubuntu-22.04` runs under WSL 2
with default Linux user `pc`. The build uses
`D:\code\sfos-skills\tools\sailfish-wsl-build.sh`, SDK root
`/srv/sailfishos/sdks/sfossdk`, environment `/home/pc/.hadk.env` and target
`SailfishOS-latest-aarch64`. Its dedicated staging source is
`/home/pc/code/mconnect-native-migration/mconnect`.

| SDK evidence | Verified value |
| --- | --- |
| Target `/etc/os-release` | Sailfish OS 5.1.0.11, Pispala |
| GCC and libstdc++ | 13.4.0 |
| glibc | 2.41 |
| Qt Core | 5.6.3 |
| SQLite | 3.50.4 |
| TagLib | 2.0.2 |
| GStreamer | 1.26.11 |

The RPM requires `GLIBC_2.34` and `libtag.so.2` among its generated runtime
dependencies. Device dependency resolution remains part of installed upgrade
validation.

The initial build stopped while resolving `pkgconfig(libqrencode)`. QR
encoding now uses the bundled Nayuki generator v1.8.0, with its original MIT
notice. The login regression checks a 512 by 512 PNG and oversized input
rejection. All 30 C++ host suites passed after that change. Spectacle retains
the native CMake build and install commands in the spec's custom sections,
and the changelog in its preserved files block.

Native ELF files install into `/usr/libexec/harbour-mconnect`, with the audio
library in its `lib` subdirectory and `$ORIGIN/lib` in the main executable.
Relative symlinks expose the executable and audio library at the existing
bundle paths under `/usr/share/harbour-mconnect`. Application and QR generator
license notices install into `/usr/share/licenses/harbour-mconnect`.

The latest helper run exported the package to
`D:\Documents\ChatGPT\sfos-music\flutter-sailfishos\apps\mconnect\RPMS\harbour-mconnect-1.5.0-44.aarch64.rpm`.
Its `RPM_OUTPUT` setting uses forward-slash Windows paths. The build command,
timestamps, full log, source synchronization receipts and host results are
saved in the host mirror's sibling `verification` directory. Reproduction
commands are in the native build README.
[Build instructions](../elinux/README.md)

## Verified release 40 RPM

The final package passed `tools/verify-rpm.py` inside the Sailfish SDK at
2026-11-06 16:32 CST. `rpm -K` reported valid digests and the changelog query
returned the release 40 entry.

| Artifact evidence | Result |
| --- | --- |
| Package | harbour-mconnect-1.5.0-40.aarch64.rpm |
| Size | 965682 bytes |
| SHA-256 | `97dfe03a57e40e2c9a86c9deede991e50fedf0906b92279d0ba9d3109da1e4c7` |
| Payload | 55 paths, including two AArch64 ELF files and both MIT notices |
| RPM verifier | PASS |
| rpmlint | 0 errors, 0 warnings, 0 badness |

Full output and machine-readable metadata are in
`verification/sailfish-rpm-verification.log` and
`verification/sailfish-rpm-verification.json` beside the host-test mirror.

## Cover button background in release 41

On 2026-11-06, the cover action area received an opaque `#808080` gray
background. The two system action buttons share this bottom strip, whose
height is `Theme.itemSizeSmall`. Its visibility follows `coverActions.enabled`.

`harbour-mconnect-1.5.0-41.aarch64.rpm` passed the SDK payload verifier and
rpmlint with zero errors and warnings. The package is 965564 bytes and has
SHA-256 `410be479bee8a0a646bb9daff7bd337ff016e854d983c518fe53beb6f7f87e5f`.

The phone at `192.168.3.240` runs Sailfish OS `5.3.0.15`. Its installed release
40 passed `rpm -V` before the change. The SSH configuration for this address
used account `bird`; this connection used the supplied `defaultuser` account
and a separate in-memory binding to the phone's ED25519 host key. The key
fingerprint is `SHA256:96riiZWRnIhHg8xO7iuRowqMlFB4rFZX9UhSliClOXk`.

The phone's existing executable validated all 31 components from a staged
copy of the updated QML. The first standalone attempt omitted the Wayland
platform environment and exited early. After matching `QT_QPA_PLATFORM` and
`EGL_PLATFORM` to the running application, the validation passed.

The RPM upgrade test and installation passed. Release 41 passed `rpm -V`,
and the installed cover file matched the source byte for byte. The application
was restarted through its native launcher with the phone's Wayland and D-Bus
environment. The native Core service responded after restart and MPRIS
reported `Paused`. The normal runtime's redirected output and journal query
provided no startup info marker, so service ownership and the live D-Bus
response supply the startup evidence.

Temporary QML, RPM and runtime-log files were removed after verification.
The user subsequently requested a separate gray background around each of
the two buttons. Build, package and device evidence are in the host-test
mirror's sibling `verification` directory under the
`cover-background-*` and `cover-device-*` filenames.

## Individual cover button backgrounds in release 42

Release 42 gives the play, pause and next-song images their own opaque
`#808080` circles with white symbols. Each image is a 128 by 128 transparent
PNG. `CoverActionList.iconBackground` is false, and the two native actions
continue to dispatch `toggle` and `next`.

`harbour-mconnect-1.5.0-42.aarch64.rpm` passed SDK payload verification with
59 paths and two AArch64 ELF files. rpmlint reported zero errors, warnings
and badness. Device installation, all 31 QML components and `rpm -V` passed.
Temporary device artifacts were removed. Evidence is recorded under the
`cover-individual-*` filenames in the sibling `verification` directory.

The user accepted the individual gray backgrounds, then requested that both
buttons become 1.5 times larger and move slightly toward the left and right
edges.

## Cover button size and spacing in release 43

Lipstick fixes the geometry of native action images. Release 43 paints the
two visible images in the cover scene and uses a transparent image for each
native `CoverAction`. The native actions provide the existing touch handling
and dispatch. The visible row follows `coverActions.enabled`.

The image size is `1.5 * Theme.iconSizeSmall / coverScale`, with an outward
offset of `Theme.paddingSmall / coverScale` for each button. `coverScale`
uses the Small or Large theme width and the cover scene width. The phone's
theme reports `Theme.iconSizeSmall` as 48 and `Theme.paddingSmall` as 10.
After cover scaling, each button is 72 pixels wide and each center moves
outward by 10 pixels. The images are vertically centered in the bottom action
row. The title's bottom margin follows the row height plus
`Theme.paddingMedium` to reserve space for the enlarged controls.

| Artifact evidence | Result |
| --- | --- |
| Package | harbour-mconnect-1.5.0-43.aarch64.rpm |
| Size | 983846 bytes |
| SHA-256 | `31028e0902bc26f00e441e9bdaa7fa827d4731b1e79855cb6e78fac4845adcfa` |
| Payload | 60 paths, including two AArch64 ELF files |
| RPM verifier | PASS |
| rpmlint | 0 errors, 0 warnings, 0 badness |

Isolated Large and Small QML fixtures passed geometry checks and exercised
16 unique resource combinations across four images, normal and pressed
URLs, and both cover sizes. The Small fixture explicitly exercises the
Small-width scaling branch. Centered-anchor checks allow 0.51 logical pixels
for QtQuick's fractional-position rounding.

Release 43 was installed on `192.168.3.240`. The installed executable
validated all 31 QML components, all 35 installed QML and image files matched
the canonical source, and `rpm -V` passed. The running executable's hash
matched the installed executable. Native Core ownership and a live `snapshot`
response passed, with playback observed as `Paused`, a song available and
`busy` false. All created device test files, symlinks, directories and the
temporary RPM were removed.

Build, source, package, QML and device reports use the `cover-buttons-*-43`
filenames in the sibling `verification` directory. The enlarged buttons and
their spacing await the user's visual acceptance in the phone's multitasking
view.

## Explore ComboBoxes in release 44

Explore uses one `SearchPage` with two native ComboBoxes in the same row.
The platform selector offers NetEase, QQ, Kugou and Bilibili. The type
selector offers songs and playlists. Selecting a different platform or type
searches with the current keyword and resets pagination. The search field's
placeholder follows the selected type.

The existing search-state fields retain the selected platform, type, keyword,
results and pagination when returning from another bottom tab. The shell
saves the current search state before replacing its Loader item. Detached
panes skip later cache writes. Superseded search callbacks consume their
eventual replies, protecting the current list and error display.

Device checks identified a transient undefined current-pane reference,
deferred state saving and ComboBox selection bindings overwritten during
initialization. The current-pane binding now falls back to a null item,
state saving precedes the Loader transition, and explicit signal connections
synchronize both selectors with the restored properties. The validator waits
for the destination pane and resolves its controls through
`homePage.currentPane`.

| Artifact evidence | Result |
| --- | --- |
| Package | harbour-mconnect-1.5.0-44.aarch64.rpm |
| Size | 987316 bytes |
| SHA-256 | `834e5b5a30e9689b5425bbb77624a4ec1b3597c0c160542df8449789643e3457` |
| Payload | 60 paths, including two AArch64 ELF files |
| RPM verifier | PASS |
| rpmlint | 0 errors, 0 warnings, 0 badness |

Nine production-JavaScript regression cases passed with an asynchronous mock
Core. They cover platform and type changes, page-specific song tokens,
superseded successes and errors, empty queries, state restoration and
deferred destruction. An isolated phone QML probe passed all 31 components,
including selection restoration with both dropdowns synchronized.

The final RPM was installed on `192.168.3.240`. Its installed executable
passed all 31 QML component checks, the four-destination navigation checks
with pane counts 1/4/1/3, and restoration of the platform, type and keyword.
All 35 installed QML and image files matched the source, and `rpm -V` passed.
After application refresh, PID 17192 owned the native Core service and its
executable hash matched the installed executable. A live `snapshot` response
reported a song available and `busy` false, with MPRIS playback `Paused`.

Temporary probe files, directories and uploaded RPMs were removed after
their digests were checked. A deployment-helper timeout was traced to an
SSH stdin stream released before password entry. Retaining the input stream
resolved the timeout. Build, source, package, state, probe and device evidence
uses the `explore-combobox-*-44` filenames in the sibling `verification`
directory. Earlier attempts retain their separate records. The final visual
layout awaits the user's acceptance on the phone.

## Header cutout in release 45

The shared header obtains its containing native Page through a hidden
PageHeader. In portrait orientation, it adds `Screen.topCutout.height` above
the title, description and buttons. The phone reports a 66-pixel cutout.
Native landscape pages retain Silica's landscape cutout avoidance.

Release 45 was built, installed and verified on `192.168.3.240`. The RPM is
989836 bytes with SHA-256
`6841e4ff60b0fd5016ff81a347840944600cf042e91e35b532d84ad56d2fa4b8`.
Its 60 payload paths include two AArch64 ELF files. rpmlint reported zero
errors, warnings and badness. All 35 installed QML and PNG files matched the
source, and `rpm -V harbour-mconnect` passed.

All 31 QML component checks passed. A no-window Qt 5.6 render-control fixture
verified the header title, description and button geometry in four
orientations, with and without a description. Navigation and search-state
restoration passed. The refreshed native process owned Core and matched the
installed executable. Its snapshot had a song available and `busy` false,
with MPRIS playback paused. Evidence uses the `notch-*-45` filenames in the
sibling verification directory.

The user confirmed the notch layout on the phone before requesting higher
navigation buttons and screen rotation support.

## Navigation spacing and rotation in release 46

The footer adds `Theme.paddingLarge` below its four navigation buttons. On
the phone this is 40 pixels. The capsule background follows the navigation
row's position and height. Both the hidden and visible mini-player cases
retain the same bottom spacing.

The application window follows `defaultAllowedOrientations`, matching the
reference media player, and its default page orientations are
`Orientation.All`. Native Page orientation transitions resize the shell and
its embedded content. Landscape pages use Silica's landscape cutout
avoidance.

The player shows artwork or lyrics on the left and playback controls on the
right in landscape. Its controls use the pane width for button spacing and
action widths. A clipped SilicaFlickable supports controls exceeding the
available height. Portrait retains room for the media area when long error
text expands the controls.

| Artifact evidence | Result |
| --- | --- |
| Package | harbour-mconnect-1.5.0-46.aarch64.rpm |
| Size | 994857 bytes |
| SHA-256 | `eb4a579f0ec2f4212af86cc03f9088bbfe88f09f142122112fd5e919a8d85449` |
| Payload | 60 paths, including two AArch64 ELF files |
| RPM verifier | PASS |
| rpmlint | 0 errors, 0 warnings, 0 badness |

The exact RPM executable, library and QML passed an isolated no-window
probe on the phone before installation. The installed package repeated all
31 component checks. Native application and page-stack rotation tests
covered four orientations and a return to portrait, with 20 main-tab
geometry cases, 42 footer geometry cases and 15 player geometry cases.
Player cases include artwork, lyrics and overflowing error text. The hidden
validator uses an all-orientations window mask to exercise portrait
inversion. Foreground windows follow the system orientation mask.

The shell measured 1032 by 2272 in portrait and 2206 by 1032 in landscape.
The landscape player had a 1103-by-777 media area and a 1103-by-817 control
pane. Search ComboBox widths, mini-player spacing, navigation, search-state
restoration and unique player/queue entries passed. An initial overflow
fixture fit within the portrait viewport. The final fixture uses 2000
characters to exercise scrolling in every tested orientation.

Release 46 was installed on `192.168.3.240`. All 35 installed QML and PNG
files matched the source and `rpm -V harbour-mconnect` passed. The refreshed
process, PID 20202, owned Core and matched the installed executable. Its
snapshot had a song available and `busy` false, with MPRIS playback paused.
A privileged read of its available journal entries found no QML binding or
JavaScript errors.

Temporary probe files, directories and uploaded RPMs were removed after
content checks. Source and mirror verification covers 138 files. Build,
package, probe, installation and runtime evidence uses the `layout-*-46`
filenames in the sibling verification directory. The first attempt retains
its separate package and reports. Physical rotation and on-screen spacing
await the user's visual confirmation.

## Landscape search, player menus and download navigation in release 47

The user reported overlapping search controls and empty-state text in
landscape, an inaccessible player pull-down menu, and overlapping pages
after starting a download. These interactions extend the orientation and
geometry coverage recorded for release 46.

Release 47 places the platform and type selectors, search field and action
across one landscape row. The empty-state item belongs to the list's content
item and uses the header height and remaining viewport height for placement.
On this device, the landscape search controls end at pixel 150. With the
mini player visible, the 544-pixel viewport places the prompt at pixels
259 through 435.

The player's full-width flickable owns the menu above the page header.
Artwork, lyrics and controls occupy its page body. Successful enqueue
responses return through `main.qml`'s `openDownloads()`, which returns to the
stable shell and selects the All filter. The download form waits for active
page status and a free page stack. It guards repeated submissions and
consumes callbacks after destruction.

| Check | Result |
| --- | --- |
| Package | harbour-mconnect-1.5.0-47.aarch64.rpm |
| Size | 1009224 bytes |
| RPM SHA-256 | be38a7a14c48f2f98168f53b575c4d7836fc57a20040911b9eebba85e0cc1dca |
| RPM payload | 60 paths and two AArch64 ELFs |
| rpmlint | Zero errors, warnings and badness |
| QML and PNG resources | All 35 installed resources matched the source |
| QML components | 31 passed |
| Native shell rotation | 20 cases across four tabs and four orientations, including portrait return |
| Search placement | 40 cases with both search types, empty queries and results, and mini player visibility |
| Player menu input | 10 window pointer drags from the page header across cover, lyrics and orientations |
| Download navigation | Success in both landscape directions and portrait, error retry, duplicate guards, transition deferral and late callbacks |
| Successful enqueue destination | Main shell, stack depth 1, Downloads All filter |
| Download storage | Its digest matched across RPM installation |
| Installed verification | `rpm -V harbour-mconnect` passed |
| Running executable SHA-256 | bfc1365c0eb314b48236360ac2102a95e7a262060790392041816437a36eca19 |

Release 47 was installed and started on `192.168.3.240`, running Sailfish OS
5.3.0.15. PID 31548 owned Core and matched the installed executable. Its
snapshot had a song available, `busy` false and MPRIS playback paused.
Startup stderr contained seven graphics-loader diagnostics. The privileged
journal query returned zero available lines. The recorded diagnostics and
component validation contained zero QML binding or JavaScript errors.

The interaction validator uses an in-memory Core fixture for download
success and error responses. Physical touch gestures and on-screen
appearance await user confirmation. Temporary device probe resources,
uploaded RPMs and the captured runtime log were removed after content
checks. The `layout-*-47` files in the sibling verification directory record
the package, source, installation and runtime evidence. Earlier attempts
retain their separate packages and reports.

## About page and version 1.6.0 in release 48

The music library places an About entry directly below Settings. Its native
page displays the installed application icon, Mconnect name, version 1.6.0,
developer BirdZhang, MIT license and the Mconnect-Music_connect and BiliMusic
reference projects. Reference items open their documented GitHub URLs.
Portrait uses a vertical layout. Landscape places application identity and
project details in two columns inside the page's scrollable content.

CMake reads the version from the RPM YAML template and passes it to the
native executable. `QCoreApplication::setApplicationVersion()` supplies
`Qt.application.version` for the About label. The YAML, spec and RPM
verifier use 1.6.0-48. The retained Flutter metadata uses 1.6.0+48.

| Check | Result |
| --- | --- |
| Package | harbour-mconnect-1.6.0-48.aarch64.rpm |
| Size | 1015842 bytes |
| RPM SHA-256 | c9d03068f5554dd4a67d497a551f7aac1c3d56aadbfe47447203cdfda57d8fba |
| RPM payload | 61 paths and two AArch64 ELFs |
| rpmlint | Zero errors, warnings and badness |
| QML components | 32 passed before and after installation |
| Installed QML and PNG resources | All 36 matched the source |
| Installed application icons | All four sizes matched the source |
| About contents | Loaded icon, name, version, developer and two distinct project links |
| About layout | Ten cases across four orientations, portrait return and mini player visibility |
| About navigation | Entry below Settings, offline opening, stack depth 2 and back to Library |
| Existing UI interactions | Search, player pull-down menu, download navigation and native rotation passed |
| Download storage | Its digest matched across RPM installation |
| Installed verification | `rpm -V harbour-mconnect` passed |
| Running executable SHA-256 | c8a0a558f22418a41081ee58eae93f43857ab509a046942552ba61447b833f65 |

The exact RPM passed an isolated hidden-window probe on `192.168.3.240`.
The installed package repeated the component and interaction checks. The
reference validator walks the rendered column's child items and checks
project identities, URLs and positive dimensions. The final stream reader
collects both output channels through EOF. Earlier attempts retain their
separate reports and packages.

The refreshed process, PID 40292, owned Core and matched the installed
executable. Its snapshot had a song available, playback paused and `busy`
false. Startup stderr contained seven graphics-loader diagnostics and zero
QML errors. The privileged journal query returned zero available entries.

Source and mirror verification covers 140 files. The `about-*-48` reports
record package, source, probe, installation and runtime evidence in the
sibling verification directory. Temporary probe resources, uploaded RPMs
and the captured device runtime log were removed after content checks.
Physical touch interaction and on-screen appearance await user confirmation.

## Requested header and About disclaimer in release 49

`AppHeader.qml` matches the supplied template. Titles use
`Theme.horizontalPageMargin`, the settings button follows the native top
cutout inset, and page return uses the native page stack. The About page
adds the supplied four-paragraph disclaimer as plain, wrapping text beneath
the application and project information. Its text spans the content width
in both portrait and landscape.

| Check | Result |
| --- | --- |
| Package | harbour-mconnect-1.6.0-49.aarch64.rpm |
| Size | 1017625 bytes |
| RPM SHA-256 | e5c8419c80ee4279a2e1bd79e08829484c7716c1e62379ac74df270427aff466 |
| RPM payload | 61 paths and two AArch64 ELFs |
| rpmlint | Zero errors, warnings and badness |
| QML components | 32 passed before and after installation |
| Installed QML and PNG resources | All 36 matched the source |
| Installed application icons | All four sizes matched the source |
| Disclaimer text | All four supplied paragraphs matched exactly |
| Disclaimer layout | Ten cases across four orientations and mini player visibility |
| Disclaimer scrolling | Final line remained inside the scroll viewport in all ten cases |
| About navigation | Entry below Settings, offline opening and native return to Library |
| Existing UI interactions | Search, player menu, download navigation and rotation passed |
| Download storage | Its digest matched across RPM installation |
| Installed verification | `rpm -V harbour-mconnect` passed |
| Running executable SHA-256 | ef65e583b263e36e8deda04c3f490d4c72b0dc1db0865eb342a849664a9d6131 |

Before installation, the device's modified header already matched the
requested template. Its contents were saved in the verification directory.
The final running process, PID 45011, owned Core and matched the installed
binary. Startup stderr contained seven graphics-loader diagnostics and zero
QML errors. Temporary probe files, the uploaded RPM and the runtime log were
removed after content checks. Physical appearance and touch interaction use
the user's on-device confirmation.

The disclaimer reproduces the user's wording. Its references to system
secure storage and music storage differ from the current permission-protected
credential files and the application's local downloads and caches.
The RPM changelog uses the SDK's UTC date, 2026-11-06. The first package's
future-date lint failure is retained as separate attempt evidence.
The `about-*-49` reports record the exact source, package, probe, installation
and runtime identities. The build input manifest is retained separately from
the final 140-file source, mirror and staging manifest.

## Sailjail configuration in release 50

Both desktop entries declare organization `org.sailfishos`, application
`Mconnect`, bundle directory `harbour-mconnect`, and the `Internet`, `Audio`,
`Music`, `Documents`, `Downloads` and `RemovableMedia` permissions. The system
launcher applies Sailjail to the matching native ELF entry points. The visible
`harbour-mconnect-silica.desktop` declares the background command in `ExecDBus`.
The D-Bus activation service and core launcher explicitly invoke Sailjail with
that profile, using its permission grant for background activation. The hidden
compatibility entry declares its foreground command. Sailjail records grants
per desktop and generates activation files from `ExecDBus`, so the single
background declaration avoids competing files for `org.sailfishos.Mconnect`.

The SDK's Sailjail 1.1.28 requires an ELF application. The package installs
the executable as `/usr/bin/harbour-mconnect-silica`, with relative links for
`/usr/bin/harbour-mconnect`, `/usr/libexec/harbour-mconnect/mconnect-silica`
and the existing bundle entry points. Its audio library is resolved through
`$ORIGIN/../libexec/harbour-mconnect/lib`. The native process applies
`umask 077` before Qt initialization and sets its Qt organization and
application names to the desktop profile values.

The application profile grants ownership of `org.sailfishos.Mconnect.Core`
and `org.sailfishos.Mconnect.Silica`, and creates and whitelists
`~/.local/share/harbour-mconnect` and `~/.cache/harbour-mconnect`. The Silica
profile includes the shared profile. Both install as RPM configuration files.
The root activation name follows from the organization and application names.
The standard `Audio` permission grants MPRIS ownership, and `Internet` includes
the ConnMan permissions.

| Check | Release 50 result |
| --- | --- |
| Native aarch64 SDK build | Passed |
| RPM payload and dependencies | Passed, 63 payload entries, two ELF files and two Sailjail profiles |
| rpmlint | Zero errors and warnings |
| Existing native C++ fixtures | All 30 suites passed after matching 93 current fixture inputs to the host-test mirror |
| SDK Sailjail command templates | Both foreground templates, the primary background template and the activation command passed, with unexpected background arguments rejected by the foreground templates |
| SDK native entry points | All four tested aliases started Core, answered a snapshot request and exported MPRIS `Stopped` for fresh empty data |

The SDK entry-point checks used the minimal Qt platform, an isolated local TCP
D-Bus session and separate temporary XDG directories. Their processes, fixture
bus and temporary files were cleaned up. GNU cpio 2.13 requires the verifier to
create `usr/bin` before extracting the initial launcher symlink.

The final RPM is `harbour-mconnect-1.6.0-50.aarch64.rpm`, 1018764 bytes, with
SHA-256 `454f94c087320a1bcdbd5d9c1b71ef2a03ec6593442b699aefa3b6c500c9756e`.
Device sandbox startup, permission prompts, existing-data access and playback
await verification. SSH authentication to the documented device at
`192.168.3.240` was unavailable, while its ED25519 host key matched the recorded
fingerprint.

## Download folder picker and device deployment in release 51

The Settings page imports `Sailfish.Pickers 1.0` and opens the system
`FolderPickerPage` from Library → Settings → Download directory. Selection
calls the native `setDownloadDirectory` request. Cancelling leaves the saved
directory intact. The RPM declares `sailfish-components-pickers-qt5` as a
runtime dependency.

`NativeDownloads::directory()` supplies the effective root to Settings and
download destinations. `setDirectory()` accepts an existing absolute folder,
canonicalizes it and verifies write, flush and removal of a `QTemporaryFile`
before saving `download_settings/custom_root_path`. A failed check preserves
the saved value. Absolute paths retain trailing spaces, and legacy values
with leading padding receive compatible trimming. The Core exposes 53
business requests, including the two directory methods.

The application profile also permits `com.nokia.profiled` reads through
`get_profile`, `get_profiles` and `get_value`, and subscribes to
`profile_changed`. These rules cover Silica's active-profile and feedback
settings. Normal startup after deployment passed with zero profile access
errors and zero QML errors in the application journal.

The device at `192.168.3.234` runs Sailfish OS `5.2.0.22` and Sailjail `1.2.0`.
Deployment used the user's authorized SSH account and skipped host-key
checking as requested. The final artifact is
`harbour-mconnect-1.6.0-51.aarch64.rpm`, 1027339 bytes, with SHA-256
`20d1e96838d22dde855c2342c0332e45cb1dc75357c1ae2d3387733f61d8ecc8`.
The installed and running native ELF has SHA-256
`f1d85f821f5d825f0b140f83cded69b8f09a5ca42f9ad3dfa65da57adc4fb6f9`.

| Check | Release 51 result |
| --- | --- |
| Native aarch64 SDK build | Passed |
| Existing native C++ fixtures | All 30 suites passed with the final directory implementation |
| RPM payload and dependencies | Passed, 63 payload entries, two ELF files and two Sailjail profiles |
| rpmlint | Zero errors and warnings |
| Installed package | `harbour-mconnect-1.6.0-51.aarch64`, with a matching final-RPM header and file digests and an empty successful `rpm -V` result |
| Installed QML validation | All 32 components passed, including normal page-stack navigation, four orientations and directory selection callbacks |
| Jailed directory handling | Save and readback passed, `/etc` was rejected, a folder with a trailing space passed, and write probes were removed |
| Jailed local playback | Silent local WAV scanning and playback passed, playback position advanced, and MPRIS reported Playing then Paused |
| Jailed provider request | NetEase search returned 30 results |
| Normal activation | `EnsurePlayback` and `Show` passed, with Core, Silica and MPRIS ready and restored playback paused |
| Normal storage | The native process opened the existing `~/.local/share/harbour-mconnect` files and returned its original effective download root |
| Native confinement | UID 100000, umask 0077, seccomp mode 2, NoNewPrivs 1 and a distinct mount namespace |
| Data across final installation | All 17 existing application data files retained their hashes, sizes and modes |

The installed QML validator used Wayland with the device's 1080 × 2340 screen
dimensions, hidden windows and a fixture Core. The initial minimal-platform
run supplied a 240 × 320 viewport while the bottom controls were 330 pixels
high. The complete Wayland run passed. Sailjail's desktop command matcher
rejects extra validation arguments, so the QML validator ran directly under
the SSH user. Actual confinement was measured through the normal D-Bus
activation and the declared Sailjail background entry.

Normal D-Bus activation uses the system-generated invoker service. The bus
reported its connection PID as 4092, the `xdg-dbus-proxy`. Process ancestry
and the shared process group identified native PID 4134 behind that proxy.
Its executable digest, private mount namespace and open application data
files were checked directly. The process used the normal Wayland,
`customcontext` and Maliit environment.

All 15 original application data files retained identical hashes, sizes and
modes throughout the isolated tests and initial normal startup. Subsequent
normal shutdown saved playback memory, appending a new `last_playback` record
and changing the `player_memory.hive` digest. Every original file remained
present. The final package replacement and normal startup each preserved the
17-file pre-install snapshot. SQLite WAL and shared-memory files were created
by the normal running application.

Final readback returned `/home/defaultuser/Music` as the saved download
directory. The original 225601-byte playback-memory prefix retained its
original digest. All 36 installed QML and cover-image files matched the
canonical source, and all 63 installed payload entries matched the final RPM.

The two marked fixture roots were removed after checking markers, absolute
path boundaries, complete manifests and file hashes. Cleanup removed 82 files
and 23 directories. Both later deployment uploads and their marked roots were
also removed after content checks. Fixture processes released their D-Bus
names before cleanup.

Startup still reports `Unable to open vibrator device`. The inspected
`/dev/input/rumble`, `/dev/timed_output/vibrator` and `/dev/vibrator` nodes are
also absent outside the sandbox. Physical appearance, touch interaction and
vibration behavior await the user's on-device confirmation.

## Preserved identities and migration behavior

- Root activation uses `org.sailfishos.Mconnect` and
  `/org/sailfishos/Mconnect`, with `Show` and `EnsurePlayback`.
- Core requests use `org.sailfishos.Mconnect.Core` and
  `/org/sailfishos/Mconnect/Core`. Provider and UI clients use separate named
  D-Bus connections inside the native process.
- MPRIS uses `org.mpris.MediaPlayer2.harbour_mconnect`, with `DesktopEntry`
  set to `harbour-mconnect`. Track paths append the SHA-1 of UTF-8
  `platform:id` to `/org/mpris/MediaPlayer2/track/t`.
- Existing download-cache IDs migrate before scheduling. Collisions receive
  `_cache_legacy_1` and incrementing suffixes. Historical rows/files and
  unexpectedly changed destinations are retained. New downloads select a
  suffixed path when necessary.
- Explicit queue indices retain the selected occurrence of a repeated song.
  Previous-track handling restarts the current track at 4000 ms, matching
  Dart's whole-second threshold. Replay seeks to zero and can reopen after a
  failed seek. Paused EOS/error events preserve paused intent.

## Remaining validation

1. Complete live-account and provider playback coverage, real download and
   restoration checks, extended background playback and physical folder-picker
   interaction. Visual and audible acceptance use user-directed interaction.
2. Retire obsolete Dart/transitional implementation after the native installed
   runtime passes compatibility verification.
