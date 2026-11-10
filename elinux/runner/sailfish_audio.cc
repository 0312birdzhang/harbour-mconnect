#include <gst/gst.h>
#include "sailfish_audio.h"
#include <algorithm>
#include <string>

// Shared C ABI for the native player and the transitional FFI caller.
struct Audio {
  GstElement* player = nullptr;
  GstElement* equalizer = nullptr;
  GstElement* gain = nullptr;
  GstBus* bus = nullptr;
  bool active = false;
  bool ended = false;
  bool buffering = false;
  double rate = 1.0;
  std::string error;
  bool bilibili = false;
};

// Bilibili CDN rejects requests without its web player Referer. Scope these
// headers to URLs returned by that CDN, leaving other services untouched.
static void setup_source(GstElement*, GstElement* source, gpointer data) {
  auto* audio = static_cast<Audio*>(data);
  if (!audio->bilibili) return;
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "extra-headers")) {
    auto* headers = gst_structure_new("headers", "Referer", G_TYPE_STRING,
      "https://www.bilibili.com/", nullptr);
    g_object_set(source, "extra-headers", headers, nullptr);
    gst_structure_free(headers);
  }
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "user-agent"))
    g_object_set(source, "user-agent", "Mozilla/5.0", nullptr);
}

extern "C" {
__attribute__((visibility("default"))) Audio* mc_audio_create_with_sink(const char* factory) {
  gst_init(nullptr, nullptr);
  auto* audio = new Audio;
  audio->player = gst_element_factory_make("playbin", nullptr);
  if (!audio->player) { delete audio; return nullptr; }
  g_signal_connect(audio->player, "source-setup", G_CALLBACK(setup_source), audio);
  // Prevent playbin from constructing a video sink for audio files with art.
  // App gain lives upstream of pulsesink. Never use GstStreamVolume on the
  // sink: it would overwrite Sailfish's system music volume, including zero.
  g_object_set(audio->player, "flags", 2u, nullptr);
  auto* sink = gst_element_factory_make(factory ? factory : "pulsesink", nullptr);
  if (!sink) {
    gst_object_unref(audio->player);
    delete audio;
    return nullptr;
  }
  if (sink) {
    gst_object_ref_sink(sink);
    g_object_set(sink, "sync", TRUE, nullptr);
    if (!factory || std::string(factory) == "pulsesink") {
      auto* properties = gst_structure_new("props", "media.role", G_TYPE_STRING,
        "x-maemo", "application.name", G_TYPE_STRING, "Mconnect", nullptr);
      g_object_set(sink, "stream-properties", properties, nullptr);
      gst_structure_free(properties);
    }
    auto* output = gst_bin_new(nullptr);
    audio->gain = gst_element_factory_make("volume", nullptr);
    if (!output || !audio->gain) {
      if (output) gst_object_unref(output);
      if (audio->gain) gst_object_unref(audio->gain);
      gst_object_unref(sink);
      gst_object_unref(audio->player);
      delete audio;
      return nullptr;
    }
    gst_object_ref_sink(output);
    gst_bin_add_many(GST_BIN(output), audio->gain, sink, nullptr);
    if (!gst_element_link(audio->gain, sink)) {
      gst_object_unref(output);
      gst_object_unref(sink);
      gst_object_unref(audio->player);
      delete audio;
      return nullptr;
    }
    auto* pad = gst_element_get_static_pad(audio->gain, "sink");
    gst_element_add_pad(output, gst_ghost_pad_new("sink", pad));
    gst_object_unref(pad);
    g_object_set(audio->player, "audio-sink", output, nullptr);
    gst_object_unref(output);
    gst_object_unref(sink);
  }
  audio->bus = gst_element_get_bus(audio->player);
  audio->equalizer = gst_element_factory_make("equalizer-10bands", nullptr);
  if (audio->equalizer) {
    gst_object_ref_sink(audio->equalizer);
    g_object_set(audio->player, "audio-filter", audio->equalizer, nullptr);
  }
  return audio;
}

__attribute__((visibility("default"))) Audio* mc_audio_create() {
  return mc_audio_create_with_sink(nullptr);
}

__attribute__((visibility("default"))) void mc_audio_destroy(Audio* audio) {
  if (!audio) return;
  gst_element_set_state(audio->player, GST_STATE_NULL);
  gst_object_unref(audio->bus);
  gst_object_unref(audio->player);
  if (audio->equalizer) gst_object_unref(audio->equalizer);
  delete audio;
}

__attribute__((visibility("default"))) int mc_audio_open(Audio* audio, const char* uri) {
  gst_element_set_state(audio->player, GST_STATE_NULL);
  // Discard errors/EOS from the previous track before changing its URI.
  gst_bus_set_flushing(audio->bus, TRUE);
  gst_bus_set_flushing(audio->bus, FALSE);
  audio->error.clear();
  audio->ended = false;
  audio->active = false;
  audio->buffering = false;
  audio->rate = 1.0;
  auto* parsed = gst_uri_from_string(uri);
  const std::string host = parsed && gst_uri_get_host(parsed) ? gst_uri_get_host(parsed) : "";
  const auto belongs = [&host](const std::string& domain) {
    return host == domain || (host.size() > domain.size() &&
      host.compare(host.size() - domain.size() - 1, domain.size() + 1, "." + domain) == 0);
  };
  audio->bilibili = belongs("bilivideo.com") || belongs("bilivideo.cn") || belongs("bilibili.com");
  if (parsed) gst_uri_unref(parsed);
  g_object_set(audio->player, "uri", uri, nullptr);
  return gst_element_set_state(audio->player, GST_STATE_PAUSED) != GST_STATE_CHANGE_FAILURE;
}

__attribute__((visibility("default"))) int mc_audio_state(Audio* audio, int state) {
  audio->active = state == 2;
  if (state == 0) { audio->ended = false; audio->buffering = false; }
  return gst_element_set_state(audio->player,
      state == 2 ? GST_STATE_PLAYING : state == 1 ? GST_STATE_PAUSED : GST_STATE_NULL)
      != GST_STATE_CHANGE_FAILURE;
}

__attribute__((visibility("default"))) int64_t mc_audio_position(Audio* audio) {
  gint64 value = 0;
  return gst_element_query_position(audio->player, GST_FORMAT_TIME, &value) ? value / 1000 : 0;
}

__attribute__((visibility("default"))) int64_t mc_audio_duration(Audio* audio) {
  gint64 value = 0;
  return gst_element_query_duration(audio->player, GST_FORMAT_TIME, &value) && value > 0
      ? value / 1000 : 0;
}

__attribute__((visibility("default"))) int mc_audio_seek(Audio* audio, int64_t position) {
  const auto success = gst_element_seek(audio->player, audio->rate, GST_FORMAT_TIME,
      static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
      GST_SEEK_TYPE_SET, std::max<int64_t>(0, position) * 1000,
      GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
  if (success) audio->ended = false;
  return success;
}

__attribute__((visibility("default"))) int mc_audio_rate(Audio* audio, double rate) {
  const auto previous = audio->rate;
  audio->rate = rate;
  if (mc_audio_seek(audio, mc_audio_position(audio))) return 1;
  audio->rate = previous;
  return 0;
}

__attribute__((visibility("default"))) void mc_audio_volume(Audio* audio, double volume) {
  g_object_set(audio->gain, "volume", std::clamp(volume, 0.0, 1.0), nullptr);
}

__attribute__((visibility("default"))) int mc_audio_equalizer(Audio* audio, const double* bands) {
  if (!audio->equalizer) return 0;
  for (int index = 0; index < 10; index++) {
    const auto name = "band" + std::to_string(index);
    g_object_set(audio->equalizer, name.c_str(), std::clamp(bands[index / 2], -24.0, 12.0), nullptr);
  }
  return 1;
}

// idle=0, loading=1, paused=2, playing=3, completed=4, error=5.
__attribute__((visibility("default"))) int mc_audio_status(Audio* audio) {
  while (auto* message = gst_bus_pop(audio->bus)) {
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
      GError* error = nullptr;
      gchar* debug = nullptr;
      gst_message_parse_error(message, &error, &debug);
      audio->error = error ? error->message : "GStreamer playback failed";
      if (error) g_error_free(error);
      g_free(debug);
      audio->active = false;
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
      audio->ended = true;
      audio->active = false;
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_BUFFERING) {
      gint percent = 100;
      gst_message_parse_buffering(message, &percent);
      audio->buffering = percent < 100;
    }
    gst_message_unref(message);
  }
  if (!audio->error.empty()) return 5;
  if (audio->ended) return 4;
  GstState state = GST_STATE_NULL;
  const auto change = gst_element_get_state(audio->player, &state, nullptr, 0);
  if (change == GST_STATE_CHANGE_ASYNC || audio->buffering) return 1;
  if (state == GST_STATE_PLAYING) return 3;
  if (state == GST_STATE_PAUSED) return 2;
  return 0;
}

__attribute__((visibility("default"))) const char* mc_audio_error(Audio* audio) {
  return audio->error.c_str();
}
}
