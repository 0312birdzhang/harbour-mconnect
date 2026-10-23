# Background playback verification — 2026-10-22

Package: harbour-mconnect-1.5.0-9.aarch64.
Companion: harbour-sailplay-0.1.1-42.aarch64.
Device: JollaPhone2026, accessed on its home network.

- New Dart startup/MPRIS code passes Flutter analysis.
- Real D-Bus MPRIS regression passes, including Raise without Play.
- Native ARM64 runner interface check passes; full AOT/native/RPM build passes.
- The RPM was extracted and verified for ARM64 ELF payloads, runtime dependency,
  background service and desktop declaration. It does not embed a second engine.
- Final packages installed; rpm -V for both packages reports no differences.
- Cold background startup restores the remembered track with CanPlay=true and
  remains paused. Repeated service/launcher activation reuses the same PID.
- Physical outputs were temporarily muted for a real playback check; playback
  position advanced by 1,812,020 microseconds across a two-second observation.
  The player was paused and original hardware mute states restored afterwards.
- A normal phone launch reuses the original PID (56040 in the final test).
- Lock-screen screenshot confirms system artwork, track metadata and controls.
- With locking unchanged, WAYLAND_DEBUG confirms no buffer attachments during
  background startup. After explicit Show, wl_surface#12 receives consecutive
  wl_buffer attachments, proving phone rendering begins only on demand.
- A real Sailplay controller cold-starts the background adapter successfully:
  BACKGROUND_ENSURE=true; BACKGROUND_CORE_READY; IDLE=true. Left in this paused
  background state after tests. sailplay.service remains active.

A full unlocked-phone visual check was not performed: automatic approval
rejected changing the phone lock state. No lock settings were changed; the
window-system trace was used instead. No live car session was required or
claimed for these phone-side integration checks.

Known lifecycle boundary: explicitly closing an attached phone view exits the
current host. Retaining playback after that explicit close is a separate task.

RPM SHA256: c2346ab180d6df6bc7efe8bc6405711132902de511978ac0184139bb27aa4077

The desktop now declares AudioVideo/Audio/Music/Player categories. Sailplay 42 also recognizes background music adapters directly; a real controller check confirms Mconnect opens the music view instead of a projected map.
