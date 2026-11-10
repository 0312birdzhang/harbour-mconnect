# Sailplay native background playback

Updated on 2026-11-10 for the native release 53 source.
`harbour-mconnect --background` enters `mconnect-silica`, which initializes
native storage, account services, the playback runtime and MPRIS. Playback
memory restores paused. MPRIS Play and explicit UI controls use the same native
player and its cached/local/provider source resolution.

The background entry creates the Sailfish application and view objects. It
defers QML creation and UI bridge subscriptions until `Show`. Qt's platform
integration is initialized during background startup. The view remains
unpresented until a foreground activation.

The session service `org.sailfishos.Mconnect` activates
`/usr/bin/harbour-mconnect-silica --background`. The desktop retains
`X-Sailplay-BackgroundService=org.sailfishos.Mconnect` and the
AudioVideo/Audio/Music/Player categories.

The root D-Bus name serializes native launches. A subsequent foreground launch
calls `Show` on the owner. A subsequent background launch calls
`EnsurePlayback`. That method reuses the initialized runtime and its paused
restoration. `Show` prepares the QML view once, starts bridge subscriptions and
raises the window. Reopening recreates the released QML tree. Queue, position and accounts belong to the existing runtime
throughout activation.

MPRIS retains `org.mpris.MediaPlayer2.harbour_mconnect`, the existing
`harbour-mconnect` desktop identity and UTF-8 SHA-1 track paths. Raise invokes
the same foreground presentation path. Artwork uses the native private cache
and loopback decoder.

Closing the foreground view hides the phone window, releases its QML tree and
suspends UI polling. The native runtime remains alive while a Sailplay session,
playback, download queue, login operation, local scan, lyric load or accepted
core/bridge request needs it. Hidden car sessions retain their lease. Pausing
playback keeps a leased core paused.

`org.sailplay.App1` is exported before the root application name is acquired.
Caller-owned `org.sailplay.Session1` objects provide revisioned media snapshots,
activity and explicit close. Owner loss releases the caller's sessions. Media
commands use the existing MPRIS interface.

The Sailjail profile allows the directed SDK signal members to temporary
`org.sailplay.Client.*` names acquired by the Qt and Python clients. Sessions
and signals retain the caller's unique bus owner. The clients hold their
temporary name through backend replacement and release it when they close.
The scoped members include the standard `PropertiesChanged` notification for
Session1 `Active` alongside state, request completion and navigation guidance.

When the phone presentation, sessions and work have all ended, a ten-second
idle grace permits exit. The timer inspects current work and leases again at
its deadline. Foreground activation cancels pending presentation release and
idle exit. Shutdown releases MPRIS, closes native work, unregisters the Silica
objects/name and finally releases the root activation name.

The lifecycle integration and SDK source overlay are published with Sailplay.
The guarded integration script checks the native release 51 source files and
preserves unrelated migration work. Installed verification is recorded in
Sailplay's `todo.md`. The earlier `sailplay-background-validation.md` records
historical packaged-host evidence.
