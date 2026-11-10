#include "../elinux/silica/native_music.h"
#include "../elinux/silica/native_platforms.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
QString json(const QVariant& value) {
  return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
}
QVariantMap user(const QString& platform, const QString& id = "123") {
  return {{"id", id}, {"platform", platform}, {"nickname", QStringLiteral("测试账号")}, {"vipLevel", 1}};
}
QString cookie(const QString& platform, const QString& id = "123") {
  if (platform == "kugou") return json(QVariantMap{{"token", "fixture"}, {"userid", id}, {"client", "lite"}});
  if (platform == "qq") return "uin=o" + id + "; qm_keyst=fixture";
  if (platform == "bilibili") return "SESSDATA=fixture; DedeUserID=" + id + "; bili_jct=fixture";
  return "MUSIC_U=fixture; __csrf=fixture";
}
template<class Action> bool fails(Action action) {
  try { action(); } catch (const std::runtime_error&) { return true; }
  return false;
}

QVariantMap acceptedCall(NativePlatforms& parser, const QString& method, const QVariantList& arguments) {
  QString reply;
  if (method == "SearchNetease") reply = parser.SearchNetease(arguments[0].toString(), arguments[1].toInt(), arguments[2].toInt(), arguments[3].toInt(), arguments[4].toString());
  else if (method == "NeteaseContent") reply = parser.NeteaseContent(arguments[0].toString(), arguments[1].toString(), arguments[2].toInt(), arguments[3].toInt(), arguments[4].toString());
  else if (method == "NeteaseCatalog") reply = parser.NeteaseCatalog(arguments[0].toString(), arguments[1].toString(), arguments[2].toInt(), arguments[3].toInt(), arguments[4].toString());
  else if (method == "NeteaseAccount") reply = parser.NeteaseAccount(arguments[0].toString(), arguments[1].toString(), arguments[2].toString());
  else if (method == "QqCatalog") reply = parser.QqCatalog(arguments[0].toString(), arguments[1].toString(), arguments[2].toString());
  else if (method == "QqAccount") reply = parser.QqAccount(arguments[0].toString(), arguments[1].toString(), arguments[2].toString());
  else if (method == "KugouCatalog") reply = parser.KugouCatalog(arguments[0].toString(), arguments[1].toString());
  else if (method == "KugouAccount") reply = parser.KugouAccount(arguments[0].toString(), arguments[1].toString(), arguments[2].toString());
  else if (method == "KugouContent") reply = parser.KugouContent(arguments[0].toString(), arguments[1].toString(), arguments[2].toString());
  else if (method == "KugouLibrary") reply = parser.KugouLibrary(arguments[0].toString(), arguments[1].toString(), arguments[2].toString());
  else if (method == "Bilibili") reply = parser.Bilibili(arguments[0].toString(), arguments[1].toString(), arguments[2].toString());
  else CHECK(false);
  auto result = QJsonDocument::fromJson(reply.toUtf8()).toVariant().toMap();
  if (result.value("error") != "dbus_required") fprintf(stderr, "ROUTING_FAILED method=%s operation=%s error=%s\n", qPrintable(method), qPrintable(arguments[0].toString()), qPrintable(result.value("error").toString()));
  CHECK(result.value("error") == "dbus_required");
  result.remove("error");
  result["ok"] = true;
  result["songs"] = QVariantList();
  result["playlists"] = QVariantList();
  return result;
}
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  const auto root = temporary.path();
  {
    NativeStorage storage(root, root + "/cache");
    for (const auto& platform : QStringList{"netease", "qq", "kugou", "bilibili"}) {
      auto legacy = user(platform);
      legacy.remove("nickname");
      storage.saveSession(platform, NativePersistedCookie(platform, cookie(platform)), json(legacy));
    }
    NativePlatforms parser;
    int calls = 0;
    NativeMusicServices music(storage, nullptr, [&](const QString& method, const QVariantList& arguments) {
      ++calls;
      return acceptedCall(parser, method, arguments);
    });
    CHECK(music.platforms().last().toMap().value("name") == QStringLiteral("哔哩哔哩音乐"));
    CHECK(music.accounts().size() == 4);
    for (const auto& account : music.accounts()) CHECK(account.toMap().value("nickname") == "");
    for (const auto& platform : QStringList{"netease", "qq", "kugou", "bilibili"}) {
      CHECK(music.loggedIn(platform) && music.accountScope(platform) == "123");
      music.request(platform, "search", {{"query", "A+B 测试"}, {"page", 1}, {"limit", 30}});
      music.request(platform, "searchPlaylists", {{"query", "test"}, {"page", 1}, {"limit", 30}});
      music.request(platform, "userPlaylists");
      const auto songId = platform == "kugou" ? QString(32, 'a') : platform == "bilibili" ? QString("BVfixture123") : QString("123");
      music.request(platform, "stream", {{"id", songId}, {"quality", 7}});
      music.request(platform, "qualities", {{"id", songId}});
      music.request(platform, "lyrics", {{"id", songId}});
      music.request(platform, "playlist", {{"id", "123"}});
      music.loginRequest(platform, "qrKey", {}, music.session(platform));
      music.loginRequest(platform, "qrCheck", {{"key", "fixture"}}, music.session(platform));
      if (platform == "bilibili") { CHECK(music.vipLevel(platform) == 1); continue; }
      music.request(platform, "profile");
      music.request(platform, "vip");
      music.request(platform, "artist", {{"id", "123"}});
      music.request(platform, "artistSongs", {{"id", "123"}, {"limit", 100}});
      music.request(platform, "artistAlbums", {{"id", "123"}, {"page", 1}, {"limit", 30}});
      music.request(platform, "album", {{"id", "123"}});
      music.request(platform, "albumSongs", {{"id", "123"}});
      music.request(platform, "toplists");
      music.request(platform, "ranked", {{"id", "123"}, {"offset", 0}, {"limit", 1000}, {"period", ""}});
      music.request(platform, "create", {{"name", "fixture"}});
      music.request(platform, "like", {{"id", songId}, {platform == "netease" ? "enabled" : "like", true}});
      if (platform == "qq") {
        music.request(platform, "newSongs", {{"id", "1"}, {"limit", 100}});
        music.request(platform, "liked");
        music.request(platform, "add", {{"id", "123"}, {"songId", songId}});
        music.request(platform, "collect", {{"id", "123"}, {"collect", true}});
        music.request(platform, "dailyId");
      } else if (platform == "netease") {
        music.request(platform, "daily");
        music.request(platform, "newSongs", {{"id", "0"}, {"limit", 100}});
        music.request(platform, "addTrack", {{"id", "123"}, {"songId", songId}});
        music.request(platform, "removeTrack", {{"id", "123"}, {"songId", songId}});
        music.request(platform, "rename", {{"id", "123"}, {"name", "fixture"}});
        music.request(platform, "delete", {{"id", "123"}});
        music.request(platform, "collect", {{"id", "123"}, {"enabled", true}});
      } else {
        music.request(platform, "homepage");
        music.request(platform, "recommend");
        music.request(platform, "liked");
        music.request(platform, "collect", {{"id", "123"}, {"collect", true}});
        music.request(platform, "lyricsByInfo", {{"query", "fixture"}, {"duration", 123000}});
      }
    }
    CHECK(calls > 70);
    CHECK(music.session("bilibili").contains("SESSDATA=fixture"));
    CHECK(QJsonDocument::fromJson(storage.credential("cookie_bilibili").toString().toUtf8()).isObject());
    CHECK(fails([&]() { music.acceptLogin("qq", cookie("qq", "456"), user("qq", "789")); }));
    CHECK(fails([&]() { music.acceptLogin("kugou", cookie("kugou", "456"), user("kugou", "789")); }));
    CHECK(fails([&]() { music.acceptLogin("bilibili", cookie("bilibili", "456"), user("bilibili", "789")); }));
    CHECK(music.accountScope("qq") == "123");
    music.acceptLogin("qq", cookie("qq", "456"), user("qq", "456"));
    CHECK(music.epoch("qq") == 1 && music.accountScope("qq") == "456");
    music.logout("qq");
    CHECK(!music.loggedIn("qq") && music.session("qq").isEmpty());
    music.logout("kugou");
    CHECK(!music.loggedIn("kugou") && ParseNativeKugouSession(music.session("kugou")).value("client") == "lite");
    CHECK(!storage.credential("cookie_kugou").isValid());
  }
  {
    NativeStorage storage(root + "/stale", root + "/cache2");
    NativeMusicServices* current = nullptr;
    NativeMusicServices music(storage, nullptr, [&](const QString&, const QVariantList&) {
      current->acceptLogin("qq", cookie("qq", "456"), user("qq", "456"));
      return QVariantMap{{"backend", "cpp-qq-catalog"}, {"error", "login_required"}};
    });
    current = &music;
    music.acceptLogin("qq", cookie("qq"), user("qq"));
    CHECK(fails([&]() { music.request("qq", "toplists"); }));
    CHECK(music.accountScope("qq") == "456" && music.loggedIn("qq"));
    CHECK(fails([&]() { music.daily("qq"); }));
    CHECK(music.accountScope("qq") == "456");
  }
  CHECK(NativeSessionUser("kugou", NativeSessionCookie("kugou", "fixture", user("kugou")), user("kugou")).value("id") == "123");
  CHECK(NativeSessionUser("qq", "qqmusic_uin=123; qm_keyst=fixture", user("qq")).value("id") == "123");
  CHECK(fails([&]() { NativeIntegerArgument(1.5, 0, 10); }));
  CHECK(fails([&]() { NativeIntegerArgument("1", 0, 10); }));
  CHECK(NativeQualityName(7) == "master");
  puts("NATIVE_MUSIC_PASS provider-routing legacy-sessions account-identity journal-persistence stale-responses recommendation-ownership");
  return 0;
}
