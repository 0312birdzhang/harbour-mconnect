#include "sailfish_audio_fixture.h"
#include "../elinux/runner/sailfish_audio.h"

struct Audio {};

NativeAudioFixture& NativeTestAudio() {
  static NativeAudioFixture fixture;
  return fixture;
}

extern "C" {
Audio* mc_audio_create() { return new Audio; }
Audio* mc_audio_create_with_sink(const char*) { return mc_audio_create(); }
void mc_audio_destroy(Audio* audio) { ++NativeTestAudio().destroys; delete audio; }
int mc_audio_open(Audio*, const char* uri) {
  auto& fixture = NativeTestAudio();
  ++fixture.opens;
  fixture.uri = QString::fromUtf8(uri);
  fixture.position = 0;
  fixture.rate = 1;
  fixture.state = 1;
  fixture.status = fixture.uri.contains("reject") ? 5 : 2;
  return fixture.status == 2;
}
int mc_audio_state(Audio*, int state) {
  auto& fixture = NativeTestAudio();
  fixture.state = state;
  if (fixture.status != 5 && (fixture.status != 4 || state == 0)) {
    fixture.status = state == 2 ? 3 : state == 1 ? 2 : 0;
  }
  if (state == 2) ++fixture.plays;
  return 1;
}
int64_t mc_audio_position(Audio*) { return NativeTestAudio().position; }
int64_t mc_audio_duration(Audio*) { return NativeTestAudio().duration; }
int mc_audio_seek(Audio*, int64_t position) {
  auto& fixture = NativeTestAudio();
  if (!fixture.seekSucceeds) return 0;
  fixture.position = position;
  if (fixture.status == 4) fixture.status = fixture.state == 2 ? 3 : fixture.state == 1 ? 2 : 0;
  return 1;
}
int mc_audio_rate(Audio*, double rate) { NativeTestAudio().rate = rate; return 1; }
void mc_audio_volume(Audio*, double volume) { NativeTestAudio().volumes.append(volume); }
int mc_audio_equalizer(Audio*, const double* bands) {
  auto& fixture = NativeTestAudio();
  fixture.bands.clear();
  for (int index = 0; index < 5; ++index) fixture.bands.append(bands[index]);
  return 1;
}
int mc_audio_status(Audio*) { return NativeTestAudio().status; }
const char* mc_audio_error(Audio*) { return "fixture playback failure"; }
}
