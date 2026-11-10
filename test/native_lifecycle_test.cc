#include "../elinux/silica/native_activation.h"
#include "../elinux/silica/native_artwork.h"
#include "../elinux/silica/native_mpris.h"
#include "../elinux/silica/native_presentation.h"
#include "../elinux/silica/native_runtime.h"
#include "sailplayprovider.h"

#include <QDBusArgument>
#include <QDBusPendingCallWatcher>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "LIFECYCLE_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
bool waitFor(const std::function<bool()>& ready, int timeout = 2000) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!ready() && elapsed.elapsed() < timeout) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
  return ready();
}
void wait(int duration) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (elapsed.elapsed() < duration) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
}
QDBusMessage call(const QString& service, const QString& path, const QString& interface,
                 const QString& member, const QVariantList& arguments = {}) {
  auto message = QDBusMessage::createMethodCall(service, path, interface, member);
  message.setArguments(arguments);
  const auto client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "native-lifecycle-client");
  QDBusPendingCallWatcher watcher(client.asyncCall(message, 3000));
  QEventLoop loop;
  QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
  if (!watcher.isFinished()) loop.exec();
  return watcher.reply();
}
}

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  NativeStorage storage(temporary.path() + "/data", temporary.path() + "/cache");
  storage.setCacheSetting("autoCleanup", false);
  NativePresentation presentation;
  NativeCoreRuntime* live = nullptr;
  int exits = 0;
  bool observedAcceptedWork = false;
  NativeCoreRuntime runtime(storage, nullptr, [&](const QString& method, const QVariantList& arguments) {
    if (method == "QqCatalog" && arguments.first() == "search") {
      CHECK(live && live->hasOngoingWork() && live->service().hasPendingWork());
      observedAcceptedWork = true;
      const int previous = exits;
      QEventLoop awaiting;
      QTimer::singleShot(70, &awaiting, &QEventLoop::quit);
      awaiting.exec();
      CHECK(exits == previous && live->hasOngoingWork());
      return QVariantMap{{"backend", "cpp-qq-catalog"}, {"songs", QVariantList{}}};
    }
    return QVariantMap{{"backend", "cpp-qq-account"}, {"qrKey", "fixture-key"}, {"qrUrl", "https://example.test/qr"},
        {"qrStatus", "waiting"}, {"ok", true}};
  }, []() { return false; }, [](const QVariantMap&) { return QString(); });
  live = &runtime;
  SailplayProvider provider("harbour-mconnect", {}, {"media"});
  CHECK(provider.start());
  NativeActivation activation;
  CHECK(activation.start() && runtime.start());
  const auto publish = [&]() {
    auto media = runtime.player().snapshot();
    media["song"] = media.value("song").toMap();
    CHECK(provider.publish({{"media", media}}));
  };
  QObject::connect(&runtime.player(), &NativePlayer::changed, &provider, publish);
  publish();
  NativeArtwork artwork;
  NativeMpris mpris(runtime.player(), artwork, []() { return QString(); }, [&]() { presentation.Show(); });
  CHECK(mpris.start());
  QFile qml(temporary.path() + "/main.qml");
  CHECK(qml.open(QIODevice::WriteOnly));
  qml.write("import QtQuick 2.0\nItem {}\n");
  qml.close();
  QQuickView view;
  presentation.attach(view);
  presentation.prepare = [&]() {
    if (view.source().isEmpty()) view.setSource(QUrl::fromLocalFile(qml.fileName()));
    return view.status() != QQuickView::Error;
  };
  presentation.keepAlive = [&]() { return provider.sessionCount() > 0 || runtime.hasOngoingWork(); };
  QObject::connect(&presentation, &NativePresentation::idle, [&]() { ++exits; });
  presentation.monitor(25, 5);
  const auto opened = call("org.sailfishos.Mconnect", "/org/sailplay/v1/App", "org.sailplay.App1", "OpenSession", {"car", QStringList{"media"}});
  CHECK(opened.type() == QDBusMessage::ReplyMessage);
  const auto session = qvariant_cast<QDBusObjectPath>(opened.arguments().first()).path();
  const auto instance = provider.instanceId();
  CHECK(provider.sessionCount() == 1);
  presentation.Show();
  view.hide();
  QFile audio(temporary.path() + "/track.mp3");
  CHECK(audio.open(QIODevice::WriteOnly));
  audio.write("isolated-audio-fixture");
  audio.close();
  const auto song = NativePlaybackSong({{"id", audio.fileName()}, {"platform", "local"}, {"name", "fixture track"}, {"durationMs", 100000}});
  runtime.player().playSong(song);
  CHECK(runtime.hasOngoingWork());
  CHECK(waitFor([&]() { return runtime.player().snapshot().value("playing").toBool(); }));
  CHECK(!view.close());
  wait(80);
  CHECK(!view.rootObject() && !presentation.requested && runtime.player().snapshot().value("playing").toBool());
  CHECK(exits == 0 && provider.instanceId() == instance);
  CHECK(call("org.sailfishos.Mconnect", session, "org.sailplay.Session1", "SetActive", {false}).type() == QDBusMessage::ReplyMessage);
  CHECK(provider.activeSessionCount() == 0 && provider.sessionCount() == 1);
  presentation.Show();
  view.hide();
  CHECK(view.rootObject() && provider.instanceId() == instance && runtime.player().snapshot().value("playing").toBool());
  CHECK(!view.close());
  CHECK(call("org.mpris.MediaPlayer2.harbour_mconnect", "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", "Pause").type() == QDBusMessage::ReplyMessage);
  CHECK(waitFor([&]() { return !runtime.hasOngoingWork(); }));
  wait(70);
  CHECK(exits == 0 && !runtime.player().snapshot().value("playing").toBool());
  // A lease retains the paused core without restarting playback.
  CHECK(call("org.sailfishos.Mconnect", session, "org.sailplay.Session1", "Close").type() == QDBusMessage::ReplyMessage);
  CHECK(waitFor([&]() { return exits > 0; }));
  const int beforeAccepted = exits;
  presentation.Show();
  view.hide();
  QTimer::singleShot(5, &view, [&]() { CHECK(!view.close()); });
  const auto searched = call("org.sailfishos.Mconnect.Core", "/org/sailfishos/Mconnect/Core", "org.sailfishos.Mconnect.Core", "Request",
      {"search", "{\"platform\":\"qq\",\"query\":\"fixture\"}"});
  CHECK(observedAcceptedWork && searched.type() == QDBusMessage::ReplyMessage);
  CHECK(exits == beforeAccepted && !presentation.requested && !view.rootObject());
  CHECK(waitFor([&]() { return exits > beforeAccepted; }));
  // Scanning is retained until the worker has joined and imported its result.
  CHECK(runtime.dispatch("localScan", {{"directory", temporary.path()}}).toMap().value("started").toBool());
  CHECK(runtime.hasOngoingWork());
  CHECK(waitFor([&]() { return !runtime.hasOngoingWork(); }));
  runtime.close();
  CHECK(!runtime.hasOngoingWork());
  mpris.close();
  activation.close();
  provider.close();
  {
    NativeStorage workStorage(temporary.path() + "/work-data", temporary.path() + "/work-cache");
    workStorage.setCacheSetting("autoCleanup", false);
    NativeMusicServices music(workStorage, nullptr, [](const QString&, const QVariantList&) {
      return QVariantMap{{"backend", "cpp-qq-account"}, {"qrKey", "fixture-key"}, {"qrUrl", "https://example.test/qr"}, {"qrStatus", "waiting"}, {"ok", true}};
    });
    NativeDownloads downloads(workStorage, music, nullptr, []() { return false; }, {}, false);
    const auto remote = NativePlaybackSong({{"id", "queued"}, {"platform", "qq"}, {"name", "fixture"}});
    downloads.enqueue(remote, 0, true);
    CHECK(downloads.hasOngoingWork());
    downloads.control({{"action", "pauseQueue"}});
    CHECK(!downloads.hasOngoingWork());
    NativeLogin login(music, nullptr, 45, 10);
    CHECK(!login.hasOngoingWork());
    CHECK(login.begin("qq", "lifecycle").value("status") == "waiting");
    CHECK(login.hasOngoingWork());
    CHECK(waitFor([&]() { return !login.hasOngoingWork(); }));
    login.begin("qq", "cancel");
    login.cancel();
    CHECK(login.hasOngoingWork());
    CHECK(waitFor([&]() { return !login.hasOngoingWork(); }));
  }
  puts("NATIVE_LIFECYCLE_PASS shared-core phone-close playing hidden-car paused-lease explicit-pause accepted-work scan downloads login idle cleanup");
  return 0;
}
