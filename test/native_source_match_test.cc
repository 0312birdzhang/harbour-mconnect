#include "../elinux/silica/native_source_match.h"
#include "../elinux/silica/native_lyric_loader.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "SOURCE_MATCH_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
QVariantMap song(const QString& platform = "qq") {
  return NativePlaybackSong({{"id", "123"}, {"platform", platform}, {"name", "Fixture (Live)"}, {"durationMs", 200000},
      {"artists", QVariantList{QVariantMap{{"id", "7"}, {"name", "Artist"}}}}});
}
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  NativeStorage storage(temporary.path(), temporary.path() + "/cache");
  NativeMusicServices* current = nullptr;
  int searches = 0, streams = 0;
  bool cancel = false, valid = true;
  NativeMusicServices music(storage, nullptr, [&](const QString& method, const QVariantList& arguments) {
    if (method == "SearchNetease") {
      ++searches;
      if (cancel) valid = false;
      auto candidate = song("netease");
      candidate["name"] = "Fixture";
      return QVariantMap{{"backend", "cpp-netease-search"}, {"songs", QVariantList{candidate}}};
    }
    CHECK(method == "NeteaseCatalog" && arguments[0] == "stream");
    ++streams;
    if (current && searches > 3) current->logout("netease");
    if (arguments[2].toInt() > 0) return QVariantMap{{"backend", "cpp-netease-catalog"}, {"error", "vip_required"}};
    return QVariantMap{{"backend", "cpp-netease-catalog"}, {"url", "https://fixture.test/audio"}};
  });
  music.acceptLogin("netease", "MUSIC_U=fixture; __csrf=fixture", {{"id", "456"}, {"platform", "netease"}, {"nickname", "fixture"}});
  NativeSourceMatch matching(storage, music);
  const auto original = song();
  auto candidate = song("netease");
  candidate["name"] = "Fixture";
  CHECK(NativeSourceMatchScore(original, candidate) == 1);
  candidate["durationMs"] = 204000;
  CHECK(NativeSourceMatchScore(original, candidate) == 0);
  candidate["durationMs"] = 202000;
  CHECK(NativeSourceMatchScore(original, candidate) > 0.8);
  CHECK(NativeMatchText("Song feat. Other") == "song");
  const auto first = matching.resolve(original, 2, [&]() { return valid; });
  CHECK(first.value("platform") == "netease" && first.value("fromCache") == false);
  CHECK(searches == 1 && streams == 3);
  CHECK(matching.resolve(original, 7, [&]() { return valid; }).value("fromCache").toBool());
  CHECK(searches == 1 && streams == 3);
  matching.invalidate(original, "netease");
  CHECK(!matching.resolve(original, 0, [&]() { return valid; }).isEmpty());
  CHECK(searches == 2 && streams == 4);
  storage.database().execute("UPDATE source_match_caches SET expires_at=?", {QDateTime::currentMSecsSinceEpoch() + 30000});
  CHECK(!matching.resolve(original, 0, [&]() { return valid; }).value("fromCache").toBool());
  CHECK(searches == 3 && streams == 5);
  music.logout("netease");
  CHECK(storage.database().query("SELECT * FROM source_match_caches").isEmpty());
  CHECK(matching.resolve(original, 0, [&]() { return valid; }).isEmpty());
  music.acceptLogin("netease", "MUSIC_U=fixture; __csrf=fixture", {{"id", "456"}, {"platform", "netease"}, {"nickname", "fixture"}});
  cancel = true;
  CHECK(matching.resolve(original, 0, [&]() { return valid; }).isEmpty());
  CHECK(storage.database().query("SELECT * FROM source_match_caches").isEmpty());
  cancel = false;
  valid = true;
  current = &music;
  CHECK(matching.resolve(original, 0, [&]() { return valid; }).isEmpty());
  CHECK(storage.database().query("SELECT * FROM source_match_caches").isEmpty());
  storage.setCacheSetting("offlineMode", true);
  CHECK(matching.resolve(original, 0, []() { return true; }).isEmpty());
  CHECK(!NativeStreamUrl("https://user:secret@fixture.test/audio"));
  puts("NATIVE_SOURCE_MATCH_PASS weighted-score duration-veto logged-targets downshift cache-ttl safety-margin invalidation cancellation account-epoch offline");
  return 0;
}
