# Sailplay background playback host

`harbour-mconnect --background` starts the shared playback core without a
phone widget tree. It initializes storage, authentication, PlayerNotifier and
MPRIS, and restores playback memory paused. Only MPRIS Play starts playback.

The pinned Sony embedder cannot reliably attach a render surface after starting
an engine without a GL context. The host therefore prepares the EGL/Wayland
render context up front but submits no phone buffer until runApp is requested.
The toplevel remains unmapped during background playback; no foreground window
is opened and then minimized. This is deferred presentation, not a fully
window-system-independent Flutter engine.

The session service `org.sailfishos.Mconnect` activates that entry point.
The desktop declares `X-Sailplay-BackgroundService=org.sailfishos.Mconnect`.
Sailplay activates this service rather than launching the desktop foreground UI.

A runtime-directory flock serializes launches. Subsequent normal launches send
Show to the existing host. The native host asks MPRIS Raise to present the existing
engine, and Dart creates its phone widget tree only then. The UI uses the same
ProviderContainer as the headless host, retaining queue, playback and position.
EnsurePlayback never opens or raises a phone window. No second database/player
instance is created for a repeated background activation.

The current embedder owns the engine through its phone view after attachment.
Explicitly closing that phone view still exits the host, as in the previous
runner. Keeping playback alive after explicitly closing that view needs a
separate lifecycle change; it is not implemented by hiding a window.

Validation results are recorded in sailplay-background-validation.md.

The desktop now declares AudioVideo/Audio/Music/Player categories. Sailplay 42 also recognizes background music adapters directly; a real controller check confirms Mconnect opens the music view instead of a projected map.
