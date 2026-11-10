#include "../elinux/silica/bridge.h"
#include "../elinux/silica/native_runtime.h"
#include "sailfish_audio_fixture.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "UI_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
bool waitFor(const std::function<bool()>& ready) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!ready() && elapsed.elapsed() < 5000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(1); }
  return ready();
}
QVariantMap song(const QString& id) {
  return NativePlaybackSong({{"id", id}, {"platform", "qq"}, {"name", id}, {"durationMs", 100000}});
}
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  qputenv("XDG_DATA_HOME", (temporary.path() + "/data").toUtf8());
  qputenv("XDG_CACHE_HOME", (temporary.path() + "/cache").toUtf8());
  NativeStorage storage;
  const auto file = temporary.path() + "/audio.mp3";
  NativeAtomicWrite(file, "fixture");
  storage.savePlaybackMemory({{"currentSong", song("first")}, {"playlist", QVariantList{song("first"), song("second")}}, {"currentIndex", 0}, {"durationMs", 100000}});
  QVariantList tasks;
  for (const auto& id : QStringList{"first", "second"}) tasks.append(QVariantMap{{"id", id}, {"song", song(id)}, {"quality", "low"}, {"status", "completed"},
      {"filePath", file}, {"downloadedBytes", 7}, {"totalBytes", 7}, {"isOfflineCache", false}});
  storage.saveDownloadTasks(tasks);
  storage.setCacheSetting("offlineMode", true);
  NativeCoreRuntime* current = nullptr;
  CoreBridge* presentation = nullptr;
  bool responsive = false;
  NativeCoreRuntime runtime(storage, nullptr, [&](const QString&, const QVariantList& arguments) {
    const auto args = QJsonDocument::fromJson(arguments[1].toString().toUtf8()).toVariant().toMap();
    if (args.value("query") == "slow") {
      QTimer::singleShot(10, &application, [&]() { presentation->request("toggle"); });
      QTimer::singleShot(30, &application, [&]() { presentation->request("next"); });
      responsive = waitFor([&]() {
        return current->player().currentSong().value("id") == "second" && current->player().snapshot().value("playing").toBool();
      });
    }
    return QVariantMap{{"backend", "cpp-qq-catalog"}, {"songs", QVariantList{song("first"), song("second")}}};
  }, []() { return false; }, [](const QVariantMap&) { return QString(); });
  current = &runtime;
  CHECK(runtime.start());
  CoreBridge bridge(nullptr, false);
  presentation = &bridge;
  QEventLoop deferred;
  QTimer::singleShot(150, &deferred, &QEventLoop::quit);
  deferred.exec();
  CHECK(!bridge.ready() && bridge.state().isEmpty() && NativeTestAudio().opens == 0);
  bridge.start();
  bridge.start();
  CHECK(waitFor([&]() { return bridge.ready() && bridge.state().value("song").toMap().value("id") == "first" && bridge.queue().size() == 2; }));
  CHECK(NativeTestAudio().opens == 0);
  const auto invoke = [&](const QString& method, const QVariantMap& args = {}) {
    int expected = 0;
    bool finished = false;
    QVariant result;
    QString error;
    const auto connection = QObject::connect(&bridge, &CoreBridge::completed, [&](int id, const QVariant& value, const QString& failure) {
      if (id == expected) { finished = true; result = value; error = failure; }
    });
    expected = bridge.request(method, args);
    CHECK(waitFor([&]() { return finished; }));
    QObject::disconnect(connection);
    CHECK(error.isEmpty());
    return result;
  };
  const auto result = invoke("search", {{"platform", "qq"}, {"query", "slow"}}).toMap();
  CHECK(responsive && result.value("total") == 2);
  CHECK(waitFor([&]() { return bridge.state().value("song").toMap().value("id") == "second" && bridge.state().value("playing").toBool(); }));
  CHECK(invoke("playResult", {{"token", result.value("token")}, {"index", 0}}).toBool());
  CHECK(waitFor([&]() { return bridge.state().value("song").toMap().value("id") == "first" && !bridge.state().value("busy").toBool(); }));
  CHECK(invoke("seek", {{"position", 12345}}).toBool());
  CHECK(waitFor([&]() { return bridge.state().value("position") == 12345; }));
  CHECK(invoke("toggle").toBool());
  CHECK(waitFor([&]() { return !bridge.state().value("playing").toBool(); }));
  runtime.close();
  CHECK(storage.playbackMemory().value("positionMs") == 12345);
  puts("NATIVE_UI_PASS real-bridge same-process-dbus delayed-results responsive-playback state-signals queue restored-offline-file paused-start shutdown");
  return 0;
}
