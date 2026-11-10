#include "../elinux/runner/sailfish_audio.h"

#include <gst/gst.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <cstdio>
#include <cstdlib>
#include <functional>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "AUDIO_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
bool waitFor(const std::function<bool()>& ready) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!ready() && elapsed.elapsed() < 5000) QThread::msleep(5);
  return ready();
}

void createAudio(const QString& path) {
  GError* error = nullptr;
  auto* pipeline = gst_parse_launch("audiotestsrc num-buffers=20 samplesperbuffer=4410 ! audioconvert ! vorbisenc ! oggmux ! filesink name=output", &error);
  CHECK(pipeline && !error);
  auto* output = gst_bin_get_by_name(GST_BIN(pipeline), "output");
  CHECK(output);
  const auto filename = path.toUtf8();
  g_object_set(output, "location", filename.constData(), nullptr);
  gst_object_unref(output);
  auto* bus = gst_element_get_bus(pipeline);
  CHECK(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
  auto* message = gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND,
      static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
  CHECK(message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS);
  gst_message_unref(message);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(bus);
  gst_object_unref(pipeline);
}
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  gst_init(nullptr, nullptr);
  const auto path = temporary.path() + "/synthetic audio.ogg";
  createAudio(path);
  auto* audio = mc_audio_create_with_sink("fakesink");
  CHECK(audio);
  const auto uri = QUrl::fromLocalFile(path).toEncoded();
  CHECK(mc_audio_open(audio, uri.constData()));
  CHECK(waitFor([&]() { return mc_audio_status(audio) == 2; }));
  CHECK(mc_audio_duration(audio) > 1000000);
  CHECK(mc_audio_state(audio, 2));
  CHECK(waitFor([&]() { return mc_audio_position(audio) > 50000 && mc_audio_status(audio) == 3; }));
  CHECK(mc_audio_state(audio, 1));
  CHECK(waitFor([&]() { return mc_audio_status(audio) == 2; }));
  const auto pausedPosition = mc_audio_position(audio);
  QThread::msleep(100);
  CHECK(std::abs(mc_audio_position(audio) - pausedPosition) < 10000);
  CHECK(mc_audio_seek(audio, 100000));
  CHECK(mc_audio_rate(audio, 1.25));
  mc_audio_volume(audio, 0.2);
  CHECK(mc_audio_state(audio, 2));
  CHECK(waitFor([&]() { return mc_audio_status(audio) == 4; }));
  CHECK(mc_audio_state(audio, 1));
  CHECK(mc_audio_status(audio) == 4);
  CHECK(mc_audio_state(audio, 2));
  CHECK(mc_audio_status(audio) == 4);
  CHECK(mc_audio_seek(audio, 0));
  CHECK(waitFor([&]() { return mc_audio_status(audio) == 3; }));
  CHECK(waitFor([&]() { return mc_audio_status(audio) == 4; }));
  CHECK(mc_audio_state(audio, 0));
  CHECK(mc_audio_status(audio) == 0);
  const auto missing = QUrl::fromLocalFile(temporary.path() + "/missing.ogg").toEncoded();
  mc_audio_open(audio, missing.constData());
  CHECK(waitFor([&]() { return mc_audio_status(audio) == 5; }));
  mc_audio_state(audio, 0);
  CHECK(mc_audio_status(audio) == 5);
  mc_audio_state(audio, 1);
  CHECK(mc_audio_status(audio) == 5);
  CHECK(mc_audio_open(audio, uri.constData()));
  CHECK(waitFor([&]() { return mc_audio_status(audio) == 2; }));
  CHECK(mc_audio_error(audio)[0] == '\0');
  mc_audio_destroy(audio);
  puts("NATIVE_AUDIO_PASS real-gstreamer synthetic-file fakesink paused-position seek rate latched-eos replay latched-error reopen shutdown");
  return 0;
}
