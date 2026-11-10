# Native core request contract inventory

Updated on 2026-11-09. `NativeCoreService` receives requests and
`NativeCoreRuntime::dispatch()` implements all 53 business methods. The runtime
fixture exercises this interface through a real local D-Bus daemon. Module
fixtures cover provider, storage, download, login, lyric and player behavior.
Installed Sailfish runtime evidence is recorded in the migration audit.

The service and interface are `org.sailfishos.Mconnect.Core`, with object path
`/org/sailfishos/Mconnect/Core`.

```text
Request(method: string, argsJson: string) -> json string
Changed(topic: string, json: string)
```

Arguments are JSON objects bounded at 64 KiB, or 256 KiB for `importPlaylist`.
Result tokens retain normalized song identities and the provider account epoch.
Account changes invalidate stale provider-owned results. Local/current-song
references retain their supported lifetime.

| Request | Native delegate |
| --- | --- |
| `localSnapshot` | `NativeLocalMusic::snapshot` |
| `localScan` | `NativeLocalMusic::scan` |
| `localGroups` | `NativeLocalMusic::groups` |
| `localSongs` | `NativeLocalMusic::songs`, `NativeResults` |
| `artistInfo` | `NativeMusicServices` |
| `artistSongs` | `NativeMusicServices`, `NativeResults` |
| `artistAlbums` | `NativeMusicServices` |
| `albumInfo` | `NativeMusicServices` |
| `albumSongs` | `NativeMusicServices`, `NativeResults` |
| `songInfo` | Runtime song reference and `NativeLibrary` |
| `resultArtwork` | Runtime song reference |
| `toggleLike` | `NativeLibrary::toggleLike` |
| `addToPlaylist` | Runtime playlist edit and `NativeLocalPlaylists` |
| `playlistEdit` | Runtime playlist edit and `NativeLocalPlaylists` |
| `copyResultToPlaylist` | `NativeResults`, `NativeLocalPlaylists` |
| `importPlaylist` | Runtime link import, native provider resolution and `NativeLocalPlaylists` |
| `exportPlaylist` | `NativeLocalPlaylists::exportLink` |
| `discoveryPlatforms` | `NativeMusicServices::discoveryPlatforms` |
| `discoveryCharts` | `NativeMusicServices` |
| `discoverySongs` | Runtime discovery, `NativeRecommendations`, `NativeResults` |
| `downloads` | `NativeDownloads::snapshot` |
| `downloadDirectory` | `NativeDownloads::directory` |
| `setDownloadDirectory` | `NativeDownloads::setDirectory`, writable-folder probe and persisted download root |
| `downloadControl` | `NativeDownloads::control` |
| `downloadEnqueue` | `NativeDownloads::enqueue` |
| `playDownload` | `NativeDownloads::taskSong`, `NativePlayer` |
| `cacheSettings` | `NativeStorage::cacheSettings` |
| `setCacheSetting` | `NativeStorage::setCacheSetting`, download policy update |
| `toggleShuffle` | `NativePlayer::toggleShuffle` |
| `cycleRepeat` | `NativePlayer::cycleRepeat` |
| `accounts` | `NativeMusicServices::accounts` |
| `loginBegin` | `NativeLogin::begin` |
| `loginStatus` | `NativeLogin::state` |
| `loginCancel` | `NativeLogin::cancel` |
| `logout` | Login cancellation and `NativeMusicServices::logout` |
| `libraryPlaylists` | Runtime library source, native provider or `NativeLocalPlaylists` |
| `librarySongs` | Runtime library source, native provider or `NativeLibrary` |
| `resultPage` | `NativeResults::page` |
| `createPlaylist` | `NativeLocalPlaylists::create` |
| `addCurrentToPlaylist` | Runtime current-song reference and `NativeLocalPlaylists` |
| `snapshot` | `NativePlayer::snapshot` |
| `platforms` | `NativeMusicServices::platforms` |
| `queue` | Native player queue rows |
| `lyrics` | `NativeLyricLoader::snapshot` |
| `artwork` | Native current-song/local artwork source |
| `search` | `NativeMusicServices`, `NativeResults` |
| `searchPlaylists` | `NativeMusicServices` |
| `playResult` | `NativeResults`, `NativePlayer::playPlaylist` |
| `toggle` | `NativePlayer::toggle` |
| `next` | `NativePlayer::next` |
| `previous` | `NativePlayer::previous` |
| `seek` | Validated millisecond seek and `NativePlayer::seek` |
| `playIndex` | Validated queue index and `NativePlayer::playIndex` |

`_readTransfer` remains a transport method. Replies above 48 KiB use opaque
transfer tokens, 24 KiB chunks and a one-minute lifetime. A result is bounded at
8 MiB, with four retained transfers and a 16 MiB aggregate bound. Large signals
carry refresh descriptors. `native_core_service_test.cc` and
`native_core_transfer_test.cc` cover transfer lifetime, limits and bridge reads.

State reads, playback controls and cancellation dispatch while provider-backed
requests await replies. Playlist read/modify/write operations execute in arrival
order. Provider errors receive sanitized replies at the interface boundary.

Published topics include `state`, `queue`, `lyrics`, `accounts` and `login`.
State changes publish on a 500 ms sampling interval. Queue, account and lyric
events retain their separate notification paths. The real bridge fixture
verifies paused restoration, delayed results, responsive playback controls and
state/queue delivery within the native process.
