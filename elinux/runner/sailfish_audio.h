#pragma once

#include <cstdint>

struct Audio;
extern "C" {
Audio* mc_audio_create();
Audio* mc_audio_create_with_sink(const char* factory);
void mc_audio_destroy(Audio* audio);
int mc_audio_open(Audio* audio, const char* uri);
int mc_audio_state(Audio* audio, int state);
int64_t mc_audio_position(Audio* audio);
int64_t mc_audio_duration(Audio* audio);
int mc_audio_seek(Audio* audio, int64_t position);
int mc_audio_rate(Audio* audio, double rate);
void mc_audio_volume(Audio* audio, double volume);
int mc_audio_equalizer(Audio* audio, const double* bands);
int mc_audio_status(Audio* audio);
const char* mc_audio_error(Audio* audio);
}
