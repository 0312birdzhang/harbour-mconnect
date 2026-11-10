#include "../elinux/silica/native_runtime.h"
#include "sailfish_audio_fixture.h"

#include <QCoreApplication>
#include <QDBusPendingCallWatcher>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "RUNTIME_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
QVariantMap song(const QString& platform = "qq") {
  return NativePlaybackSong({{"id", "download"}, {"platform", platform}, {"name", "fixture song"}, {"durationMs", 100000},
      {"artists", QVariantList{QVariantMap{{"id", "7"}, {"name", "fixture artist"}}}}, {"album", QVariantMap{{"id", "8"}, {"name", "fixture album"}}}});
}
bool waitFor(const std::function<bool()>& ready) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!ready() && elapsed.elapsed() < 5000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(1); }
  return ready();
}
QDBusMessage request(const QString& method, const QVariantMap& args = {}) {
  auto call = QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Core", "/org/sailfishos/Mconnect/Core", "org.sailfishos.Mconnect.Core", "Request");
  call.setArguments({method, QString::fromUtf8(QJsonDocument::fromVariant(args).toJson(QJsonDocument::Compact))});
  const auto client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "native-runtime-test-client");
  QDBusPendingCallWatcher watcher(client.asyncCall(call, 5000));
  QEventLoop loop;
  QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
  if (!watcher.isFinished()) loop.exec();
  return watcher.reply();
}
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  NativeStorage storage(temporary.path() + "/data", temporary.path() + "/cache");
  const auto file = temporary.path() + "/completed.mp3";
  NativeAtomicWrite(file, "media-fixture");
  storage.saveDownloadTasks({QVariantMap{{"id", "qq_download_low"}, {"song", song()}, {"quality", "low"}, {"status", "completed"},
      {"filePath", file}, {"downloadedBytes", 13}, {"totalBytes", 13}, {"isOfflineCache", false}}});
  storage.database().execute("INSERT INTO lyrics_cache(song_id,platform,content,format,synced_at) VALUES('download','qq','[00:01.00]fixture lyric','lrc',0)");
  QStringList operations;
  NativeCoreRuntime runtime(storage, nullptr, [&](const QString& method, const QVariantList& arguments) {
    const auto platform = method.startsWith("Qq") ? QString("qq") : method.startsWith("Kugou") ? QString("kugou") : method == "Bilibili" ? QString("bilibili") : QString("netease");
    const auto operation = method == "SearchNetease" ? QString("search") : arguments[0].toString();
    operations.append(platform + ':' + operation);
    const auto backend = method == "SearchNetease" ? QString("cpp-netease-search") : method == "NeteaseContent" ? QString("cpp-netease-content") :
        method == "NeteaseAccount" ? QString("cpp-netease-account") : method == "NeteaseCatalog" ? QString("cpp-netease-catalog") :
        method == "QqAccount" ? QString("cpp-qq-account") : method == "QqCatalog" ? QString("cpp-qq-catalog") :
        method == "Bilibili" ? QString("cpp-bilibili") : QString("cpp-kugou-catalog");
    return QVariantMap{{"backend", backend}, {"songs", QVariantList{song(platform)}},
        {"playlists", QVariantList{QVariantMap{{"id", "123"}, {"name", "fixture playlist"}, {"platform", platform}, {"songCount", 1}}}},
        {"charts", QVariantList{QVariantMap{{"id", "123"}, {"name", "fixture chart"}, {"period", "2026-11"}}}},
        {"artist", QVariantMap{{"id", "7"}, {"name", "fixture artist"}, {"songCount", 1}, {"albumCount", 1}}},
        {"album", QVariantMap{{"id", "8"}, {"name", "fixture album"}, {"songCount", 1}}},
        {"albums", QVariantList{QVariantMap{{"id", "8"}, {"name", "fixture album"}}}},
        {"vipLevel", 2}, {"dailyId", "123"}, {"qrKey", "fixture-key"}, {"qrUrl", "https://example.test/login"}, {"ok", true}};
  }, []() { return false; }, [](const QVariantMap&) { return QString(); });
  runtime.music().acceptLogin("qq", "uin=o123; qm_keyst=fixture", {{"id", "123"}, {"nickname", "fixture"}, {"platform", "qq"}});
  CHECK(runtime.requestNames().size() == 53);
  CHECK(runtime.start());
  QSet<QString> visited;
  const auto invoke = [&](const QString& method, const QVariantMap& args = {}) {
    const auto reply = request(method, args);
    if (reply.type() != QDBusMessage::ReplyMessage) fprintf(stderr, "RUNTIME_REPLY_FAILED method=%s message=%s\n", qPrintable(method), qPrintable(reply.errorMessage()));
    CHECK(reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() == 1);
    visited.insert(method);
    const auto value = QJsonDocument::fromJson('[' + reply.arguments().first().toString().toUtf8() + ']').toVariant().toList();
    CHECK(value.size() == 1);
    return value.first();
  };
  CHECK(invoke("snapshot").toMap().value("version") == 1);
  CHECK(invoke("platforms").toList().size() == 4);
  CHECK(invoke("accounts").toList()[1].toMap().value("loggedIn").toBool());
  CHECK(invoke("discoveryPlatforms").toList().size() == 4);
  CHECK(invoke("queue").toList().isEmpty());
  CHECK(!invoke("lyrics").toMap().value("loading").toBool());
  CHECK(invoke("artwork").toMap().value("source") == "");
  const auto result = invoke("search", {{"platform", "qq"}, {"query", "fixture"}}).toMap();
  const auto token = result.value("token");
  const QVariantMap reference{{"token", token}, {"index", 0}};
  CHECK(result.value("total") == 1);
  CHECK(invoke("searchPlaylists", {{"platform", "qq"}, {"query", "fixture"}}).toMap().value("playlists").toList().size() == 1);
  CHECK(invoke("resultPage", {{"token", token}, {"offset", 0}}).toMap().value("total") == 1);
  CHECK(invoke("songInfo", reference).toMap().value("canArtist").toBool());
  CHECK(invoke("resultArtwork", reference).toMap().value("source") == "");
  CHECK(invoke("toggleLike", reference).toMap().value("liked").toBool());
  CHECK(invoke("artistInfo", {{"platform", "qq"}, {"id", "7"}}).toMap().value("title") == "fixture artist");
  CHECK(invoke("artistSongs", {{"platform", "qq"}, {"id", "7"}}).toMap().value("total") == 1);
  CHECK(invoke("artistAlbums", {{"platform", "qq"}, {"id", "7"}}).toList().size() == 1);
  CHECK(invoke("albumInfo", {{"platform", "qq"}, {"id", "8"}}).toMap().value("title") == "fixture album");
  CHECK(invoke("albumSongs", {{"platform", "qq"}, {"id", "8"}}).toMap().value("total") == 1);
  CHECK(invoke("discoveryCharts", {{"platform", "qq"}}).toList().size() == 1);
  CHECK(invoke("discoverySongs", {{"platform", "qq"}, {"kind", "new"}, {"region", "hongKongTaiwan"}}).toMap().value("total") == 1);
  CHECK(invoke("discoverySongs", {{"platform", "qq"}, {"kind", "chart"}, {"id", "123"}}).toMap().value("total") == 1);
  CHECK(invoke("discoverySongs", {{"platform", "qq"}, {"kind", "daily"}}).toMap().value("total") == 1);
  const auto playlist = invoke("createPlaylist", {{"name", " Test playlist "}}).toMap().value("id");
  auto add = reference;
  add["id"] = playlist;
  CHECK(invoke("addToPlaylist", add).toMap().value("ok").toBool());
  CHECK(invoke("copyResultToPlaylist", {{"token", token}, {"name", "Copied"}}).toMap().value("count") == 1);
  CHECK(invoke("playlistEdit", {{"id", playlist}, {"action", "rename"}, {"name", "Renamed"}}).toMap().value("ok").toBool());
  CHECK(invoke("libraryPlaylists", {{"source", "mine"}}).toList().size() == 2);
  CHECK(invoke("libraryPlaylists", {{"source", "qq"}}).toList().size() == 1);
  CHECK(invoke("librarySongs", {{"source", "mine"}, {"id", playlist}}).toMap().value("total") == 1);
  CHECK(invoke("librarySongs", {{"source", "likes"}}).toMap().value("total") == 1);
  CHECK(invoke("librarySongs", {{"source", "platform"}, {"platform", "qq"}, {"id", "123"}}).toMap().value("total") == 1);
  const auto exported = invoke("exportPlaylist", {{"id", playlist}}).toMap().value("link");
  CHECK(invoke("importPlaylist", {{"link", exported}}).toMap().value("count") == 1);
  CHECK(invoke("importPlaylist", {{"link", "https://y.qq.com/playlist/123.html"}}).toMap().value("count") == 1);
  CHECK(invoke("playResult", reference).toBool());
  CHECK(waitFor([&]() { return runtime.player().snapshot().value("playing").toBool() && !runtime.player().snapshot().value("busy").toBool(); }));
  CHECK(NativeTestAudio().uri.startsWith("file:") && !operations.contains("qq:stream"));
  CHECK(invoke("addCurrentToPlaylist", {{"id", playlist}}).toMap().value("ok").toBool());
  CHECK(invoke("seek", {{"position", 12000}}).toBool() && NativeTestAudio().position == 12000000);
  CHECK(invoke("toggle").toBool() && !runtime.player().snapshot().value("playing").toBool());
  CHECK(invoke("toggle").toBool());
  CHECK(invoke("next").toBool());
  CHECK(invoke("previous").toBool());
  CHECK(invoke("playIndex", {{"index", 0}}).toBool());
  CHECK(invoke("toggleShuffle").toMap().value("ok").toBool());
  CHECK(invoke("cycleRepeat").toMap().value("ok").toBool());
  CHECK(invoke("downloads").toMap().value("tasks").toList().size() == 1);
  CHECK(invoke("downloadControl", {{"action", "pauseQueue"}}).toMap().value("ok").toBool());
  auto download = reference;
  download["quality"] = "low";
  CHECK(invoke("downloadEnqueue", download).toMap().value("ok").toBool());
  CHECK(invoke("playDownload", {{"id", "qq_download_low"}}).toMap().value("ok").toBool());
  CHECK(invoke("cacheSettings").toMap().value("wifiOnly").toBool());
  CHECK(invoke("setCacheSetting", {{"key", "autoRetry"}, {"value", false}}).toMap().value("autoRetry") == false);
  CHECK(invoke("downloadDirectory").toMap().value("path") == QDir(storage.dataDirectory).filePath("downloads"));
  const auto directory = temporary.path() + "/music";
  NativeAtomicWrite(directory + "/track.mp3", {});
  const auto chosenDirectory = QFileInfo(directory).canonicalFilePath();
  CHECK(invoke("setDownloadDirectory", {{"path", directory}}).toMap().value("path") == chosenDirectory);
  CHECK(invoke("downloadDirectory").toMap().value("path") == chosenDirectory);
  CHECK(invoke("localSnapshot").toMap().value("count") == 0);
  CHECK(invoke("localScan", {{"directory", directory}}).toMap().value("started").toBool());
  CHECK(waitFor([&]() { return !runtime.dispatch("localSnapshot", {}).toMap().value("scanning").toBool(); }));
  CHECK(invoke("localGroups", {{"view", "folders"}}).toList().size() == 1);
  CHECK(invoke("localSongs", {{"view", "songs"}}).toMap().value("total") == 1);
  CHECK(invoke("loginBegin", {{"platform", "qq"}, {"session", "test-attempt"}}).toMap().value("status") == "waiting");
  CHECK(invoke("loginStatus").toMap().value("session") == "test-attempt");
  CHECK(invoke("loginCancel", {{"session", "test-attempt"}}).toMap().value("ok").toBool());
  CHECK(invoke("logout", {{"platform", "qq"}}).toMap().value("ok").toBool());
  CHECK(visited.size() == 53);
  CHECK(request("resultPage", {{"token", token}, {"offset", 0}}).type() == QDBusMessage::ErrorMessage);
  CHECK(request("seek", {{"position", "12"}}).errorName() == "org.freedesktop.DBus.Error.InvalidArgs");
  CHECK(request("search", {{"platform", "qq"}, {"query", " "}}).errorName() == "org.freedesktop.DBus.Error.InvalidArgs");
  CHECK(request("setCacheSetting", {{"key", "wifiOnly"}, {"value", "false"}}).errorName() == "org.freedesktop.DBus.Error.InvalidArgs");
  CHECK(request("setDownloadDirectory", {{"path", false}}).errorName() == "org.freedesktop.DBus.Error.InvalidArgs");
  runtime.close();
  puts("NATIVE_RUNTIME_PASS all-53-real-dbus-requests provider-reads local-library playlist-transfer downloads download-directory paused-restoration lyrics storage errors stale-lists");
  return 0;
}
