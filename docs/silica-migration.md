# Silica presentation migration

## Architecture

The release 40 source uses one `mconnect-silica` process for the Sailfish window,
native cover, C++ runtime, provider services, playback and MPRIS. `main.cpp`
constructs `NativeStorage` and `NativeCoreRuntime`. The current request inventory
maps all 51 QML operations to their native implementation.

The root activation service serializes launches. Foreground activation prepares
QML and raises the owner's window. Background activation restores playback
paused and defers QML creation and bridge subscriptions until `Show`.
Minimizing retains playback state. Explicitly closing the foreground view exits
the process and closes its native services.

Host provider, storage, player, MPRIS and real bridge fixtures passed. Sailfish
package linking, installed upgrade and visual/playback acceptance remain pending.
The current implementation and evidence are tracked in
`native-migration-audit.md`.

## Interface v1

The session bus service and interface are `org.sailfishos.Mconnect.Core`.

The object path is `/org/sailfishos/Mconnect/Core`.

`Request(s method, s argumentsJson) -> s resultJson`

`Changed(s topic, s valueJson)`

Operations are listed in `native-core-request-inventory.md`.

State changes publish at a 500 ms sampling interval. Queue, lyrics, accounts and
login retain their separate event notifications. Search uses bounded opaque
result tokens scoped to provider account epochs. The native runtime resolves
song identities and media URLs, and sanitizes errors at the interface boundary.

Artwork is read through the existing loopback-only decoder and private MCA cache.
Lyrics use the native cache-first loader. Explicit UI and MPRIS controls request
audio playback. Startup restores playback memory paused.

The milestone entries below record earlier presentation releases and their
transitional architecture.

## First milestone

- Four-platform song search, pagination and result playback.
- Current song, artist, position, duration, seek, previous/next and play/pause.
- Queue selection, mini player, line-synchronized lyrics and translations.
- Native cover with artwork, contrasting title and play/pause/next actions.
- Separate **Mconnect Silica** launcher; the existing launcher is retained during
  migration.

At this first milestone, native login, discovery, library, download management,
local scanning and settings pages were pending. This is a migration preview, not
feature parity. Existing saved sessions and business behavior are reused.
Word timing remains in the Dart lyric document; the first QML page highlights
whole lines, not individual words.

## Verification

Host tests use isolated D-Bus and fake player/platform objects. They never invoke
the phone's audio controls. `mconnect-silica --validate-qml` checks the installed
QML components without starting the playback host or showing a window. Device
checks must distinguish successful component loading and process lifecycle from
user-confirmed layout and audible playback. Do not take unsolicited screenshots.

## Second milestone (1.5.0-17)

The initial page is now the native music library. Native account management and
QR login are available for the four platforms, including Kugou's lite/android
variant selection. QR generation, polling, account refresh, session persistence
and logout call the existing AuthNotifier. QR images are generated in memory and
passed as data URLs; neither images nor platform QR keys are written to disk.
Only the displayed image reaches QML, not the platform polling key or cookies.

One QR attempt is active at a time, with a three-minute bound. Client session IDs
let a page cancel even an unfinished QR request. Generation guards ignore stale
results; an old page's cancellation cannot stop a newer attempt. Success is
published after account refresh and the existing session-save operation.

Library pages support local saved playlists, platform playlists, local likes and
history, playlist tracks, play-all and individual playback. Local playlist
creation and adding the current song reuse MyPlaylistsRepository. Network
playlist operations use the existing adapters. Local playlist reads do not fetch
platform data, and all playback preserves original Song objects so the existing
download-first file resolver continues to apply.

Track lists are exposed in 100-row pages with bounded result-token storage.
Play-all still submits the entire original list to the shared player. The mini
player appears on library subpages as well as search, and lyrics/player pages
continue to use the shared core.

Additional operations: `accounts`, `loginBegin`, `loginStatus`, `loginCancel`,
`logout`, `libraryPlaylists`, `librarySongs`, `resultPage`, `createPlaylist`,
`addCurrentToPlaylist`. Additional notification topics: `accounts`, `login`.
Static user-facing failures may cross the bridge; arbitrary provider exceptions
remain sanitized. Native mode now connects session-expiry reporting without
requiring a Flutter widget tree.

Native discovery, download management, local scanning, settings and advanced
playlist editing remain later milestones. A real QR scan/authorization requires
the user; successful code generation/poll cancellation alone is not a completed
login verification. QML validation disables all core connections and QR requests.

## Third milestone (1.5.0-19)

Native discovery exposes supported recommendation, chart and new-song entries.
Recommendation labels and fallback notes come from the existing platform result.
Chart period IDs are retained; charts expose at most 1000 tracks, then use the
same 100-row UI paging as playlists. Bilibili does not advertise recommendation,
chart or new-song entries; its search and favorites remain available.

Native download management reuses DownloadNotifier, its scheduler and transport.
Song context menus and the player open a quality selector. Membership checks run
before enqueueing. The queue page exposes progress, pause/resume/retry, cancellation
with a remorse popup, global queue pause/resume, and completed-song playback through
the existing downloaded-file resolver. File paths and transport URLs are not sent
to QML. The page polls only while foreground and active. A keyed ListModel updates
records without destroying delegates during progress changes; its insert/update/
move/remove behavior is checked with actual Qt persistent model indices in the
no-window validator. Cancellation remorse belongs to the page, not a refreshed row.

Common settings reuse the cache-settings provider/repository and player notifier:
offline mode, Wi-Fi-only automatic caching, automatic retry/cleanup, cache size,
shuffle and repeat. Loading and viewing a page do not write settings. Controls
wait for persisted settings to load. Manual downloads follow the existing policy
and are explicitly described as outside the Wi-Fi-only automatic-cache switch.

Additional operations: `discoveryPlatforms`, `discoveryCharts`, `discoverySongs`,
`downloads`, `downloadControl`, `downloadEnqueue`, `playDownload`, `cacheSettings`,
`setCacheSetting`, `toggleShuffle`, `cycleRepeat`. Normal player snapshots now
include shuffle and repeat mode. Existing MCA artwork and cached lyric behavior
and the Maliit native-input initialization are preserved.

Local music scanning, artist/album detail pages, advanced playlist editing and
advanced audio/scrobbling/settings pages remain outside this milestone.

## Fourth milestone (1.5.0-20)

Native local music opens the persisted index without starting a directory scan.
An explicit directory dialog starts the existing scanner in its isolate; progress
is polled while the page is active. Tracks can be searched and grouped by artist,
album or folder. Successful scans retain the selected root. Indexed cover files
continue through the private artwork cache. Scanning updates the index and never
deletes original audio files.

Song actions expose local favorites, adding to a local playlist, and supported
platform artist/album details. Artist pages expose top songs and paged albums;
album pages expose their tracks. All playback retains original Song objects and
the shared download-first resolver. Local playlists support rename, copy, delete,
track removal and reordering. Remote lists can be copied locally. Import accepts
Mconnect links or supported platform share links; export produces a Mconnect link
for explicit clipboard copying. Clipboard reads happen only after tapping Paste.
Local mutations are serialized to preserve concurrent edits.

Large D-Bus replies now use bounded 24 KiB byte chunks, with base64 transport and
UTF-8 decoding after reassembly. This avoids partial socket writes in the existing
Dart D-Bus package. Transfers expire after one minute and have bounded size and
resident storage. Large change notifications request a fresh snapshot. The native
validator does not connect to or raise an existing application.

Advanced audio, scrobbling, smart-playlist rules and remote playlist editing
remain outside this milestone. Component loading and isolated tests do not
replace user acceptance of layout, directory selection or real playback.

## Flutter layout alignment (1.5.0-22)

The native UI follows Flutter's Explore, Discovery, Library and Downloads tabs.
Explore is the initial page. A shared footer places the mini player above the
four tabs on ordinary pages, including library subpages. Selecting a tab returns
to that destination's root; search query and paged results survive tab changes.
The footer hides while the keyboard is visible. The full player is separate.

Compact shared headers provide plain-text titles, nested-page back buttons and
settings access on root pages. Explore supports four platform tabs and song or
playlist search. Playlist results retain source platform and ID. Discovery has
the three compact shortcuts and a three-column, nine-song recommendation area
in a single scroll, matching Flutter's "playlist recommendations" area (which
also contains songs). Actual recommendation provenance is displayed. Unsupported
Bilibili categories stay disabled. Artwork uses the existing private cache.

Library groups existing likes/history/local/cache/playlist/import/download/
settings/account entries. A source page separates local and platform playlists.
Downloads adds all/active/completed filters over the same keyed model. The player
defaults to cover art, switches to lyrics on tapping the cover, and exposes
shuffle/repeat and queue access directly. The compact lyric header is retained.

This aligns structure and access paths while preserving Silica theme, keyboard,
gestures and page-stack behavior. It does not claim pixel parity or add the
pending statistics, smart-playlist and advanced audio screens. The no-window
validator exercises four destinations, nested reset, footer existence and a
nine-card fixture. Visual acceptance requires user feedback; no screenshots are
taken automatically.

## Cyclic SlideshowView shell (1.5.0-23)

The four destinations now live inside a single native SlideshowView, in this
12-pane cycle: Explore NetEase/QQ/Kugou/Bilibili, Discovery NetEase/QQ/Kugou/
Bilibili, Library, Downloads all/active/completed. PathView wraps in both
directions. Platform/filter selectors jump to their corresponding pane; footer
tabs jump to the first pane in each destination. Nested pages return to the
existing shell rather than creating a new shell on every tab selection.

The shell owns the page Components, so recycled PathView delegates do not own
their QML contexts. Delegates load those Components and expose the current pane.
Only the selected pane is active; inactive neighbors do not poll or create pulley
menus. Platform search state is retained independently. Root bodies use Items
inside the actual shell Page so their geometry fits the Slideshow viewport.

The footer sits outside the clipped SlideshowView with an opaque backdrop and
clipping. Embedded panes hide their own footer. The mini player no longer has a
queue icon. Player and queue routes reuse existing stack entries. Discovery and
Library pulley menus no longer contain account management; Library's ordinary
account entry is retained. No Dart business/audio code changed in this release.

## Independent page groups (1.5.0-24)

Replaces release 23's global cycle after user clarification. The bottom tabs are
the only way to change primary destinations. Explore and Discovery each have a
four-platform SlideshowView; Downloads has an all/active/completed SlideshowView.
Library has no SlideshowView. Wrap stays within the current page, and each page
remembers its selected index when the user changes bottom tabs.

The shell owns one fixed title/index bar and a single shared pulley menu; only
the index body slides. Embedded bodies hide their own headers, selectors and
menus. The shared menu follows the active body's Flickable. A hidden parking
Flickable keeps the target valid while loading/changing pages, because this old
Silica menu cannot tolerate a null Flickable. Components remain in the stable
shell context. Opaque footer and unique player/queue routing are retained.

## Fixed outer menus, recommendation metadata and core recovery (1.5.0-25)

Explore and Discovery bodies no longer contain pulley menus. One menu lives on
the shell's permanent outer Flickable, so its parent/target never changes when
index delegates are recycled. Pull from the outer header to use this page menu;
index bodies retain their independent vertical scrolling and local Slideshow.

Recommendation requests are coordinated by ApplicationWindow, not by disposable
index bodies. Platform metadata is shared, in-flight recommendation requests are
joined, and completed results remain available after swiping away. The Dart cache
stores metadata per platform/account under the private .recommendations directory
using .mcr files and .nomedia. It preserves song/platform/artist/album IDs, cover
references and recommendation provenance. Reads prefer memory/disk; explicit
refresh updates the data, with cached fallback on failure. No audio URL/cookie or
ordinary poster image is stored there; artwork still uses MCA. Recommendation
result tokens are protected from ordinary search-result eviction.

Native startup may reconnect to an orphaned Silica core only after both service
PIDs, UID, executable, native-mode marker and PID start time identify the process.
It continues to reject an unrelated Flutter instance. Closing a recovered UI
stops only that verified core. Newly started cores receive a parent-death SIGTERM
with a checked parent PID, so a crashed UI cannot leave an unmanaged host running.
