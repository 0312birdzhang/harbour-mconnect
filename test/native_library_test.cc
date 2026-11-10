#include "../elinux/silica/native_library.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "LIBRARY_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
QVariantMap song(const QString& id, const QString& platform = "qq") {
  return NativePlaybackSong({{"id", id}, {"platform", platform}, {"name", QStringLiteral("歌曲") + id},
      {"artists", QVariantList{QVariantMap{{"id", "7"}, {"name", QStringLiteral("歌手 A")}}}}, {"durationMs", 123400},
      {"album", QVariantMap{{"id", "9"}, {"name", QStringLiteral("专辑")}}}});
}
bool fails(const std::function<void()>& action) {
  try { action(); } catch (const std::runtime_error&) { return true; }
  return false;
}
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  NativeStorage storage(temporary.path(), temporary.path() + "/cache");
  NativeLibrary library(storage);
  const auto first = song("1"), second = song("2");
  CHECK(library.likes().isEmpty() && library.history().isEmpty());
  CHECK(library.toggleLike(first) && library.isLiked(first));
  CHECK(library.likes().first().toMap().value("name") == first.value("name"));
  CHECK(library.toggleLike(second));
  CHECK(!library.toggleLike(first) && !library.isLiked(first));
  CHECK(library.likes().size() == 1 && library.likes().first().toMap().value("id") == "2");
  library.recordListen(first, 1000, 100000);
  library.recordListen(first, 2000, 100001);
  CHECK(library.history().size() == 1);
  CHECK(storage.database().query("SELECT duration_listened FROM listening_history").first().toMap().value("duration_listened").toInt() == 2000);
  library.recordListen(first, 100, 111000);
  library.recordListen(second, 100, 112000);
  CHECK(library.history().size() == 3 && library.history().first().toMap().value("id") == "2");
  storage.database().execute("CREATE TRIGGER reject_like BEFORE INSERT ON user_likes BEGIN SELECT RAISE(ABORT,'fixture'); END");
  CHECK(fails([&]() { library.toggleLike(first); }));
  CHECK(!library.isLiked(first));
  storage.database().execute("DROP TRIGGER reject_like");

  int fetches = 0;
  bool providerFails = false;
  NativeMusicServices music(storage, nullptr, [&](const QString&, const QVariantList&) {
    ++fetches;
    return providerFails ? QVariantMap{{"backend", "cpp-netease-catalog"}, {"error", "network_error"}} :
        QVariantMap{{"backend", "cpp-netease-catalog"}, {"songs", QVariantList{song("10", "netease")}}};
  });
  NativeResults results(music);
  const auto token = results.store({first, second}).value("token").toString();
  CHECK(results.page(token, 0).value("songs").toList().size() == 2);
  CHECK(results.page(token, 2).value("songs").toList().isEmpty());
  CHECK(fails([&]() { results.page(token, 3); }));
  CHECK(results.song({{"token", token}, {"index", 1}}, {}).value("id") == "2");
  CHECK(fails([&]() { results.song({{"current", true}}, {}); }));
  const auto privateToken = results.store({first}, "qq", "qq").value("token").toString();
  music.acceptLogin("qq", "uin=o123; qm_keyst=fixture", {{"id", "123"}, {"nickname", "fixture"}, {"platform", "qq"}});
  CHECK(fails([&]() { results.songs(privateToken); }));
  CHECK(results.songs(token).size() == 2);
  const auto retained = results.store({first}, "qq", "qq").value("token").toString();
  for (int index = 0; index < 110; ++index) results.store({second});
  CHECK(fails([&]() { results.songs(token); }));
  CHECK(results.songs(retained).size() == 1);
  music.logout("qq");
  CHECK(fails([&]() { results.songs(retained); }));

  NativeRecommendations recommendations(storage, music);
  CHECK(recommendations.get("netease", false).value("songs").toList().size() == 1 && fetches == 1);
  CHECK(recommendations.get("netease", false).value("note") == QStringLiteral("本地缓存") && fetches == 1);
  providerFails = true;
  CHECK(recommendations.get("netease", true).value("note") == QStringLiteral("刷新失败，使用本地缓存") && fetches == 2);
  NativeRecommendations restored(storage, music);
  CHECK(restored.get("netease", false).value("songs").toList().size() == 1 && fetches == 2);
  music.acceptLogin("netease", "MUSIC_U=fixture", {{"id", "123"}, {"nickname", "fixture"}, {"platform", "netease"}});
  CHECK(fails([&]() { restored.get("netease", false); }));
  CHECK(fetches == 3);
  puts("NATIVE_LIBRARY_PASS sqlite-likes history-dedupe transaction-rollback result-pages token-eviction account-ownership recommendation-cache offline-fallback");
  return 0;
}
