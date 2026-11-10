#include "../elinux/silica/native_activation.h"
#include "../elinux/silica/native_mpris.h"
#include "sailfish_audio_fixture.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <cstdlib>
#include <limits>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "MPRIS_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
const QString name = "org.mpris.MediaPlayer2.harbour_mconnect";
const QString path = "/org/mpris/MediaPlayer2";
const QString playerInterface = "org.mpris.MediaPlayer2.Player";

bool waitFor(const std::function<bool()>& ready) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!ready() && elapsed.elapsed() < 5000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(1); }
  return ready();
}

QDBusMessage call(const QString& service, const QString& object, const QString& interface, const QString& method, const QVariantList& args = {}) {
  auto message = QDBusMessage::createMethodCall(service, object, interface, method);
  message.setArguments(args);
  auto client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "native-mpris-test-client");
  QDBusPendingCallWatcher watcher(client.asyncCall(message, 5000));
  QEventLoop loop;
  QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
  if (!watcher.isFinished()) loop.exec();
  return watcher.reply();
}

QVariant property(const QString& interface, const QString& key) {
  const auto reply = call(name, path, "org.freedesktop.DBus.Properties", "Get", {interface, key});
  CHECK(reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() == 1);
  return reply.arguments().first().value<QDBusVariant>().variant();
}

class Signals : public QObject {
  Q_OBJECT
 public:
  int changes = 0, seeks = 0;
  qlonglong position = -1;
 public slots:
  void changed(const QString& interface, const QVariantMap& properties, const QStringList&) {
    CHECK(interface == playerInterface && !properties.contains("Position"));
    ++changes;
  }
  void seeked(qlonglong value) { ++seeks; position = value; }
};
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  NativeStorage storage(temporary.path() + "/data", temporary.path() + "/cache");
  const auto file = temporary.path() + "/audio fixture.mp3";
  const auto artFile = temporary.path() + "/cover.png";
  const auto png = QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aX1sAAAAASUVORK5CYII=");
  NativeAtomicWrite(file, "fixture");
  NativeAtomicWrite(artFile, png);
  const auto song = NativePlaybackSong({{"id", QStringLiteral("id/with-chinese-中文")}, {"platform", "qq"}, {"name", QStringLiteral("测试歌曲")}, {"durationMs", 100000},
      {"artists", QVariantList{QVariantMap{{"id", "artist"}, {"name", QStringLiteral("测试歌手")}}}}, {"coverUrl", QUrl::fromLocalFile(artFile).toString()}});
  auto next = song;
  next["id"] = "next";
  next["name"] = "next";
  storage.savePlaybackMemory({{"currentSong", song}, {"playlist", QVariantList{song, next}}, {"currentIndex", 0}, {"positionMs", 12000}, {"durationMs", 100000}});
  QVariantList tasks;
  for (const auto& item : QVariantList{song, next}) tasks.append(QVariantMap{{"id", item.toMap().value("id")}, {"song", item}, {"quality", "low"}, {"status", "completed"},
      {"filePath", file}, {"downloadedBytes", 7}, {"totalBytes", 7}, {"isOfflineCache", false}});
  storage.saveDownloadTasks(tasks);
  storage.setCacheSetting("offlineMode", true);
  int providers = 0;
  NativeMusicServices music(storage, nullptr, [&](const QString&, const QVariantList&) { ++providers; return QVariantMap(); });
  NativeDownloads downloads(storage, music, nullptr, []() { return false; }, {}, false);
  NativeLibrary library(storage);
  NativePlayer player(storage, music, downloads, library);
  NativeArtwork artwork(nullptr, temporary.path() + "/artwork");
  int raises = 0;
  NativeMpris mpris(player, artwork, {}, [&]() { ++raises; });
  CHECK(mpris.start());
  Signals observed;
  auto client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "native-mpris-test-client");
  CHECK(client.connect(name, path, "org.freedesktop.DBus.Properties", "PropertiesChanged", &observed, SLOT(changed(QString,QVariantMap,QStringList))));
  CHECK(client.connect(name, path, playerInterface, "Seeked", &observed, SLOT(seeked(qlonglong))));
  CHECK(property("org.mpris.MediaPlayer2", "Identity") == "Mconnect");
  CHECK(property("org.mpris.MediaPlayer2", "DesktopEntry") == "harbour-mconnect");
  CHECK(property("org.mpris.MediaPlayer2", "CanRaise").toBool());
  CHECK(property(playerInterface, "PlaybackStatus") == "Paused" && NativeTestAudio().opens == 0);
  CHECK(property(playerInterface, "Position").toLongLong() == 12000000);
  auto metadata = qdbus_cast<QVariantMap>(property(playerInterface, "Metadata"));
  CHECK(metadata.value("xesam:title") == QStringLiteral("测试歌曲"));
  const auto expectedTrack = "/org/mpris/MediaPlayer2/track/t" + QString::fromLatin1(QCryptographicHash::hash(NativeSongKey(song).toUtf8(), QCryptographicHash::Sha1).toHex());
  CHECK(metadata.value("mpris:trackid").value<QDBusObjectPath>().path() == expectedTrack);
  CHECK(metadata.value("mpris:length").toLongLong() == 100000000);
  CHECK(metadata.value("mpris:artUrl").toString().startsWith("http://127.0.0.1:"));
  auto* reply = new QNetworkAccessManager(&application);
  auto* image = reply->get(QNetworkRequest(QUrl(metadata.value("mpris:artUrl").toString())));
  CHECK(waitFor([&]() { return image->isFinished(); }));
  CHECK(image->error() == QNetworkReply::NoError && image->readAll() == png);
  image->deleteLater();
  CHECK(call(name, path, "org.mpris.MediaPlayer2", "Raise").type() == QDBusMessage::ReplyMessage);
  CHECK(raises == 1 && NativeTestAudio().opens == 0);
  NativeActivation activation;
  int shows = 0;
  QObject::connect(&activation, &NativeActivation::showRequested, [&]() { ++shows; });
  CHECK(activation.start());
  CHECK(call("org.sailfishos.Mconnect", "/org/sailfishos/Mconnect", "org.sailfishos.Mconnect", "EnsurePlayback").type() == QDBusMessage::ReplyMessage);
  CHECK(shows == 0 && NativeTestAudio().opens == 0);
  CHECK(call("org.sailfishos.Mconnect", "/org/sailfishos/Mconnect", "org.sailfishos.Mconnect", "Show").type() == QDBusMessage::ReplyMessage && shows == 1);
  CHECK(call(name, path, playerInterface, "Play").type() == QDBusMessage::ReplyMessage);
  CHECK(waitFor([&]() { return player.snapshot().value("playing").toBool() && !player.snapshot().value("busy").toBool(); }));
  CHECK(property(playerInterface, "PlaybackStatus") == "Playing" && providers == 0);
  CHECK(call(name, path, playerInterface, "Seek", {qlonglong(-5000000)}).type() == QDBusMessage::ReplyMessage);
  CHECK(waitFor([&]() { return observed.position == 7000000; }));
  CHECK(call(name, path, playerInterface, "Seek", {std::numeric_limits<qlonglong>::max()}).type() == QDBusMessage::ReplyMessage);
  CHECK(property(playerInterface, "Position").toLongLong() == 100000000);
  CHECK(call(name, path, playerInterface, "Seek", {std::numeric_limits<qlonglong>::min()}).type() == QDBusMessage::ReplyMessage);
  CHECK(property(playerInterface, "Position").toLongLong() == 0);
  CHECK(call(name, path, playerInterface, "SetPosition", {QVariant::fromValue(QDBusObjectPath("/stale_track")), qlonglong(20000000)}).type() == QDBusMessage::ReplyMessage);
  CHECK(property(playerInterface, "Position").toLongLong() == 0);
  CHECK(call(name, path, playerInterface, "SetPosition", {QVariant::fromValue(mpris.trackPath()), qlonglong(20000000)}).type() == QDBusMessage::ReplyMessage);
  CHECK(property(playerInterface, "Position").toLongLong() == 20000000);
  CHECK(call(name, path, playerInterface, "Pause").type() == QDBusMessage::ReplyMessage);
  CHECK(property(playerInterface, "PlaybackStatus") == "Paused");
  CHECK(call(name, path, playerInterface, "PlayPause").type() == QDBusMessage::ReplyMessage);
  CHECK(property(playerInterface, "PlaybackStatus") == "Playing");
  CHECK(call(name, path, playerInterface, "Next").type() == QDBusMessage::ReplyMessage);
  CHECK(waitFor([&]() { return player.currentSong().value("id") == "next" && !player.snapshot().value("busy").toBool(); }));
  CHECK(call(name, path, playerInterface, "Previous").type() == QDBusMessage::ReplyMessage);
  CHECK(waitFor([&]() { return player.currentSong().value("id") == song.value("id") && !player.snapshot().value("busy").toBool(); }));
  CHECK(call(name, path, playerInterface, "Stop").type() == QDBusMessage::ReplyMessage);
  CHECK(property(playerInterface, "PlaybackStatus") == "Paused" && property(playerInterface, "Position").toLongLong() == 0);
  CHECK(call(name, path, playerInterface, "Seek", {QString("wrong")}).type() == QDBusMessage::ErrorMessage);
  CHECK(call(name, path, "org.freedesktop.DBus.Properties", "Set", {playerInterface, "Rate", QVariant::fromValue(QDBusVariant(1.5))}).type() == QDBusMessage::ErrorMessage);
  const auto introspection = call(name, path, "org.freedesktop.DBus.Introspectable", "Introspect");
  CHECK(introspection.type() == QDBusMessage::ReplyMessage && introspection.arguments().first().toString().contains("SetPosition"));
  CHECK(waitFor([&]() { return observed.changes >= 2 && observed.seeks >= 4; }));
  const auto stableChanges = observed.changes;
  QEventLoop stable;
  QTimer::singleShot(700, &stable, &QEventLoop::quit);
  stable.exec();
  CHECK(observed.changes == stableChanges);
  mpris.close();
  activation.close();
  CHECK(!client.interface()->isServiceRegistered(name).value());
  CHECK(!client.interface()->isServiceRegistered("org.sailfishos.Mconnect").value());
  puts("NATIVE_MPRIS_PASS paused-restore metadata track-id unicode local-artwork system-controls relative-seek stale-position typed-signals read-only-properties activation shutdown");
  return 0;
}

#include "native_mpris_test.moc"
