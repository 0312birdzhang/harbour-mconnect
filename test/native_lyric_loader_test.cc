#include "../elinux/silica/native_lyric_loader.h"
#include "../elinux/silica/native_lyrics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "LYRIC_LOADER_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
QVariantMap song(const QString& id, const QString& platform = "qq") {
  return NativePlaybackSong({{"id", id}, {"platform", platform}, {"name", "fixture song (Live)"}, {"durationMs", 200000},
      {"artists", QVariantList{QVariantMap{{"id", "1"}, {"name", "fixture artist"}}}}});
}
bool waitFor(const std::function<bool()>& ready) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!ready() && elapsed.elapsed() < 5000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(1); }
  return ready();
}
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  NativeStorage storage(temporary.path(), temporary.path() + "/cache");
  NativeLyricLoader* controller = nullptr;
  QString mode = "yrc";
  int calls = 0, lrclibCalls = 0;
  NativeMusicServices music(storage, nullptr, [&](const QString& method, const QVariantList& arguments) {
    ++calls;
    const auto platform = method.startsWith("Qq") ? QString("qq") : method.startsWith("Kugou") ? QString("kugou") :
        method == "Bilibili" ? QString("bilibili") : QString("netease");
    const auto backend = platform == "qq" ? QString("cpp-qq-catalog") : platform == "netease" ?
        (method == "SearchNetease" ? QString("cpp-netease-search") : QString("cpp-netease-catalog")) :
        platform == "bilibili" ? QString("cpp-bilibili") : method == "KugouCatalog" ? QString("cpp-kugou-catalog") : QString("cpp-kugou-content");
    if (mode == "fail") return QVariantMap{{"backend", backend}, {"error", "network_error"}};
    const auto operation = method == "SearchNetease" ? QString("search") : arguments[0].toString();
    if (mode == "stale" && platform == "qq" && operation == "lyrics") {
      mode = "yrc";
      controller->load(song("replacement"));
      return QVariantMap{{"backend", backend}, {"lyrics", QVariantMap{{"lrc", "[00:01.00]stale"}}}};
    }
    if (mode == "cross" && operation == "search") {
      auto candidate = song("match", platform);
      candidate["name"] = "fixture song";
      return QVariantMap{{"backend", backend}, {"songs", QVariantList{candidate}}};
    }
    if (mode == "cross" && platform != "qq" && operation == "lyrics") {
      return QVariantMap{{"backend", backend}, {"lyrics", QVariantMap{{"lrc", "[00:01.00]cross-source"}}}};
    }
    if (mode == "yrc" && operation == "lyrics") return QVariantMap{{"backend", backend}, {"lyrics", QVariantMap{
        {"yrc", "[20370,400](0,200,0)native"}, {"yrcTranslation", "[00:20.370]translation"}}}};
    if (mode == "wrongTime" && operation == "lyrics") return QVariantMap{{"backend", backend}, {"lyrics", QVariantMap{{"lrc", "[05:00.00]wrong-time"}}}};
    return QVariantMap{{"backend", backend}, {"lyrics", QVariantMap()}, {"songs", QVariantList()}};
  });
  NativeLyricLoader loader(storage, music, nullptr, [&](const QVariantMap&) {
    ++lrclibCalls;
    if (mode == "lrclib" || mode == "wrongTime") return QString("[00:01.00]fallback");
    if (mode == "throwFallback") throw NativeRequestFailure("fixture");
    return QString();
  });
  controller = &loader;
  loader.load(song("cache"));
  CHECK(waitFor([&]() { return !loader.snapshot().value("loading").toBool(); }));
  CHECK(!loader.snapshot().value("error").toBool() && loader.document().value("format") == "yrc");
  CHECK(loader.snapshot().value("lines").toList().first().toMap().value("translation") == "translation");
  const auto firstCalls = calls;
  loader.load(song("cache"));
  CHECK(waitFor([&]() { return !loader.snapshot().value("loading").toBool(); }));
  CHECK(calls == firstCalls && loader.snapshot().value("lines").toList().first().toMap().value("translation") == "translation");
  CHECK(lrclibCalls == 0);

  mode = "cross";
  loader.load(song("cross"));
  CHECK(waitFor([&]() { return !loader.snapshot().value("loading").toBool(); }));
  CHECK(loader.document().value("source") == "netease");
  mode = "fail";
  const auto beforeCache = calls;
  loader.load(song("cross"));
  CHECK(waitFor([&]() { return !loader.snapshot().value("loading").toBool(); }));
  CHECK(calls == beforeCache && loader.document().value("source") == "netease");
  loader.load(song("uncached"));
  CHECK(waitFor([&]() { return !loader.snapshot().value("loading").toBool(); }));
  CHECK(loader.snapshot().value("error").toBool());
  mode = "empty";
  loader.load(song("no-lyrics"));
  CHECK(waitFor([&]() { return !loader.snapshot().value("loading").toBool(); }));
  CHECK(!loader.snapshot().value("error").toBool() && loader.snapshot().value("lines").toList().isEmpty());
  mode = "lrclib";
  loader.load(song("local-file", "local"));
  CHECK(waitFor([&]() { return !loader.snapshot().value("loading").toBool(); }));
  CHECK(loader.document().value("source") == "lrclib");
  mode = "fail";
  loader.load(song("local-file", "local"));
  CHECK(waitFor([&]() { return !loader.snapshot().value("loading").toBool(); }));
  CHECK(loader.document().value("source") == "lrclib");
  mode = "wrongTime";
  loader.load(song("incorrect-timeline"));
  CHECK(waitFor([&]() { return !loader.snapshot().value("loading").toBool(); }));
  CHECK(loader.document().value("source") == "lrclib");
  mode = "stale";
  loader.load(song("stale"));
  CHECK(waitFor([&]() { return !loader.snapshot().value("loading").toBool(); }));
  CHECK(loader.snapshot().value("lines").toList().first().toMap().value("text") == "native");
  CHECK(storage.database().query("SELECT * FROM lyrics_cache WHERE song_id='stale'").isEmpty());
  storage.setCacheSetting("offlineMode", true);
  const auto offlineCalls = calls;
  loader.load(song("offline"));
  CHECK(waitFor([&]() { return !loader.snapshot().value("loading").toBool(); }));
  CHECK(calls == offlineCalls && !loader.snapshot().value("error").toBool());

  const QVariantList candidates{QVariantMap{{"duration", 200}, {"instrumental", true}, {"syncedLyrics", "instrumental"}},
      QVariantMap{{"duration", 202}, {"syncedLyrics", "near"}}, QVariantMap{{"duration", 210}, {"syncedLyrics", "far"}}};
  CHECK(NativeClosestLrclibLyrics(candidates, 200000) == "near");
  CHECK(NativeClosestLrclibLyrics(candidates, 100000).isEmpty());
  CHECK(NativeClosestLrclibLyrics(QString::fromUtf8(QJsonDocument::fromVariant(candidates).toJson()), 0) == "near");
  CHECK(NativeShouldAskLrclib({{"lines", QVariantList{QVariantMap{{"time", 231000}}}}}, 200000));
  CHECK(!NativeShouldAskLrclib({{"lines", QVariantList{QVariantMap{{"time", 231000}}}}}, 0));
  loader.close();
  puts("NATIVE_LYRIC_LOADER_PASS translated-cache cross-source-owner no-lyrics-versus-error local-lrclib timeline-fallback stale-load offline selection");
  return 0;
}
