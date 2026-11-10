#include "../elinux/silica/native_platforms.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QEventLoop>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <cstdio>

#define CHECK(condition) do { \
  if (!(condition)) { fprintf(stderr, "native Kugou library failed at %d\n", __LINE__); return 1; } \
} while (false)

namespace {
QVariantMap call(QDBusConnection& bus, const QString& operation, const QVariantMap& arguments = {}, const QString& session = {}) {
  auto message = QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Silica",
      "/org/sailfishos/Mconnect/Platforms", "org.sailfishos.Mconnect.Platforms", "KugouLibrary");
  message << operation << QString::fromUtf8(QJsonDocument::fromVariant(arguments).toJson(QJsonDocument::Compact)) << session;
  QDBusPendingCallWatcher watcher(bus.asyncCall(message, 20000));
  QEventLoop loop;
  QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
  if (!watcher.isFinished()) loop.exec();
  QDBusPendingReply<QString> reply = watcher;
  return reply.isError() ? QVariantMap{{"error", "dbus_error"}} : QJsonDocument::fromJson(reply.value().toUtf8()).toVariant().toMap();
}

QVariantMap song(int index) {
  return {{"hash", QString("%1").arg(index, 32, 16, QChar('0'))}, {"filename", QString::fromUtf8("歌手 - 中文歌曲")},
      {"timelen", 234567}, {"album_name", QString::fromUtf8("中文专辑")}, {"album_id", 1234567890123LL},
      {"authors", QVariantList{QVariantMap{{"author_id", 456}, {"author_name", QString::fromUtf8("歌手")}}}},
      {"trans_param", QVariantMap{{"union_cover", "https://image.test/{size}/cover"}}}};
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  auto bus = QDBusConnection::sessionBus();
  auto client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "kugou-library-client");
  CHECK(bus.registerService("org.sailfishos.Mconnect.Silica"));
  QTcpServer fixture;
  CHECK(fixture.listen(QHostAddress::LocalHost));
  bool valid = true, emptyPublic = false, emptyShared = false, repeatPage = false, malformed = false;
  bool reflectCreated = true, stableCreatedId = true, reject = false, transientPost = false, metadataFailure = false;
  int httpStatus = 200, declaredTotal = 250;
  QString createdName, redirect = "https://www.kugou.com/songlist/collection_3_12345_456_0";
  QMap<QString, int> counts;
  QStringList privateIds;
  QList<int> sharedSizes;
  QObject::connect(&fixture, &QTcpServer::newConnection, &app, [&]() {
    while (fixture.hasPendingConnections()) {
      auto* socket = fixture.nextPendingConnection();
      auto bytes = std::make_shared<QByteArray>();
      auto started = std::make_shared<bool>(false);
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      QObject::connect(socket, &QTcpSocket::readyRead, &app, [&, socket, bytes, started]() {
        if (*started) return;
        bytes->append(socket->readAll());
        const int separator = bytes->indexOf("\r\n\r\n");
        if (separator < 0) return;
        const auto headers = bytes->left(separator);
        int length = 0;
        for (const auto& line : headers.split('\n')) {
          if (line.toLower().startsWith("content-length:")) length = line.mid(line.indexOf(':') + 1).trimmed().toInt();
        }
        if (bytes->size() < separator + 4 + length) return;
        *started = true;
        const auto first = headers.left(headers.indexOf("\r\n")).split(' ');
        const QUrl url("http://localhost" + QString::fromLatin1(first[1]));
        const auto path = url.path();
        const QUrlQuery query(url);
        const auto body = bytes->mid(separator + 4, length);
        const auto data = QJsonDocument::fromJson(body).toVariant().toMap();
        const auto lowerHeaders = headers.toLower();
        valid = valid && !lowerHeaders.contains("cookie:") && !lowerHeaders.contains("authorization:");
        ++counts[path];
        QMap<QString, QString> parameters;
        for (const auto& item : query.queryItems(QUrl::FullyDecoded)) parameters[item.first] = item.second;
        if (parameters.contains("signature")) {
          const auto signature = parameters.take("signature");
          const bool h5 = parameters.value("appid") == "1058";
          const bool lite = parameters.value("appid") == "3116";
          const QByteArray secret = h5 ? "NVPh5oo715z5DIWAeQlhMDsWXXQV4hwt" :
              lite ? "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA" : "OIlwieks28dk2k092lksi2UIkp";
          QByteArray material = secret;
          for (auto item = parameters.cbegin(); item != parameters.cend(); ++item) material += (item.key() + '=' + item.value()).toUtf8();
          valid = valid && signature == QString::fromLatin1(QCryptographicHash::hash(material + (h5 ? QByteArray() : body) + secret, QCryptographicHash::Md5).toHex());
          if (h5) {
            valid = valid && parameters.value("dfid") == "-" && parameters.value("mid") == "mid" && parameters.value("uuid") == "uuid";
            valid = valid && parameters.value("clienttime").toLongLong() > 1000000000000LL;
          } else valid = valid && parameters.value("mid") == "fixture-mid" && parameters.value("dfid") == "fixture-dfid";
        }
        if (path == "/fixture-short") {
          valid = valid && first[0] == "GET";
          socket->write("HTTP/1.1 302 Found\r\nLocation: " + redirect.toUtf8() + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
          socket->disconnectFromHost();
          return;
        }
        QVariantMap root{{"status", reject ? 0 : 1}};
        if (reject) root["error_code"] = 10004;
        int status = httpStatus;
        if (path == "/v7/get_all_list") {
          valid = valid && first[0] == "POST" && lowerHeaders.contains("x-router: cloudlist.service.kugou.com");
          valid = valid && parameters.value("appid") == "3116" && parameters.value("clientver") == "11440";
          valid = valid && data.value("userid") == "12345" && data.value("token") == "fixture-token";
          valid = valid && data.value("type").toInt() == 2 && data.value("page").toInt() == 1;
          QVariantMap created{{"global_collection_id", "created-global"}, {"listid", "list-456"},
            {"name", QString::fromUtf8("创建歌单")}, {"songcount", 23}, {"imgurl", "https://image.test/{size}/playlist"}};
          QVariantMap collected{{"global_specialid", "collected-global"}, {"listid", "list-789"}, {"name", QString::fromUtf8("收藏歌单")}};
          QVariantList createdLists{created, created};
          if (!createdName.isEmpty() && reflectCreated) createdLists.append(QVariantMap{{"global_collection_id", "new-global"},
              {"listid", "new-list"}, {"name", createdName}});
          root["data"] = QVariantMap{{"list_create_list", createdLists},
              {"list_collect_list", QVariantMap{{"list", QVariantList{collected}}}}};
        } else if (path == "/api/v5/special/song" || path == "/v2/get_other_list_file" || path == "/v4/get_list_all_file") {
          const bool shared = path == "/v2/get_other_list_file", privateRoute = path == "/v4/get_list_all_file";
          valid = valid && first[0] == (privateRoute ? "POST" : "GET");
          int page = privateRoute ? data.value("page").toInt() : query.queryItemValue("page").toInt();
          const int size = privateRoute ? data.value("pagesize").toInt() : query.queryItemValue("pagesize").toInt();
          if (privateRoute) {
            valid = valid && parameters.value("appid") == "3116" && lowerHeaders.contains("x-router: cloudlist.service.kugou.com");
            valid = valid && data.value("token") == "fixture-token" && data.value("show_album_info").toInt() == 1;
            privateIds.append(data.value("listid").toString());
          }
          if (shared) {
            valid = valid && lowerHeaders.contains("referer: https://activity.kugou.com/");
            valid = valid && parameters.value("uid") == "12345" && parameters.value("token") == "fixture-token";
            sharedSizes.append(size);
          }
          if (repeatPage) page = 1;
          const bool empty = shared ? emptyShared : privateRoute ? false : emptyPublic;
          QVariantList rows;
          if (!empty) for (int index = (page - 1) * size; index < qMin(page * size, 250); ++index) rows.append(song(index));
          QVariantMap payload{{"total", empty ? 0 : declaredTotal}, {"name", QString::fromUtf8("分享歌单")},
              {"imgurl", "https://image.test/{size}/shared"},
              {privateRoute ? "list" : shared ? "songs" : "info", malformed ? QVariant("malformed") : QVariant(rows)}};
          root["data"] = payload;
        } else if (path == "/uc/collection/song/list") {
          valid = valid && first[0] == "GET" && parameters.value("token") == "fixture-token";
          root["data"] = QVariantMap{{"info", QVariantList{song(1), song(2)}}};
        } else if (path == "/api/v5/song/collect" || path == "/api/v5/song/uncollect") {
          valid = valid && first[0] == "GET" && parameters.value("token") == "fixture-token" && parameters.value("mid") == "mid";
        } else if (path == "/cloudlist.service/v5/add_list") {
          valid = valid && first[0] == "POST" && !lowerHeaders.contains("x-router:");
          const bool create = data.value("type").toInt() == 0;
          valid = valid && parameters.value("appid") == (create ? "3116" : "1005");
          valid = valid && data.value("userid") == "12345" && data.value("token") == "fixture-token";
          if (create) {
            createdName = data.value("name").toString();
            valid = valid && data.contains("list_create_userid") && data.value("list_create_userid").isNull();
            valid = valid && data.contains("list_create_listid") && data.value("list_create_listid").isNull();
            valid = valid && parameters.value("last_area") == "gztx";
            root.remove("status");
            root["errcode"] = reject ? 10004 : 0;
            root["data"] = QVariantMap{{"listid", "transient-list"}};
            if (stableCreatedId) root["data"] = QVariantMap{{"global_collection_id", 1234567890123LL}, {"listid", "transient-list"}};
          } else valid = valid && data.value("list_create_listid") == "collected-global" && data.value("name").toString().isEmpty();
          if (transientPost) status = 503;
        } else if (path == "/api/v5/special/detail") {
          root["data"] = QVariantMap{{"specialname", QString::fromUtf8("公共歌单")}, {"songcount", "250"}};
        } else valid = false;
        auto response = QJsonDocument::fromVariant(root).toJson(QJsonDocument::Compact);
        if (metadataFailure && path == "/api/v5/special/detail") response = "Access Deny ! No Actions !";
        socket->write("HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Length: " +
            QByteArray::number(response.size()) + "\r\nConnection: close\r\n\r\n" + response);
        socket->disconnectFromHost();
      });
    }
  });
  const QUrl base(QString("http://127.0.0.1:%1").arg(fixture.serverPort()));
  NativePlatforms service(nullptr, QUrl("https://music.163.com"), QUrl("https://api.bilibili.com"),
      QUrl("https://passport.bilibili.com"), QUrl("https://u.y.qq.com"), QUrl("https://c.y.qq.com"), base);
  CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms", &service, QDBusConnection::ExportAllSlots));
  const QString session = "{\"token\":\"fixture-token\",\"userid\":\"12345\",\"client\":\"lite\",\"mid\":\"fixture-mid\",\"dfid\":\"fixture-dfid\"}";
  auto result = call(client, "userPlaylists", {}, session);
  CHECK(result.value("playlists").toList().size() == 2);
  const auto first = result.value("playlists").toList().first().toMap();
  CHECK(first.value("id") == "created-global" && first.value("editId") == "list-456" && first.value("editable") == true);
  CHECK(first.value("coverUrl") == "https://image.test/480/playlist");
  result = call(client, "playlist", {{"id", "public-123"}}, session);
  CHECK(result.value("songs").toList().size() == 250 && counts["/api/v5/special/song"] == 3);
  const auto firstSong = result.value("songs").toList().first().toMap();
  CHECK(firstSong.value("name") == QString::fromUtf8("中文歌曲") && firstSong.value("albumId") == "1234567890123");
  CHECK(firstSong.value("durationMs").toInt() == 234567 && firstSong.value("coverUrl") == "https://image.test/480/cover");
  emptyPublic = true;
  result = call(client, "playlist", {{"id", "private-456"}}, session);
  CHECK(result.value("songs").toList().size() == 250 && privateIds.first() == "private-456");
  emptyPublic = false;
  result = call(client, "playlist", {{"id", "collection_3_12345_456_0"}}, session);
  CHECK(result.value("songs").toList().size() == 250 && sharedSizes == (QList<int>{100, 100, 100}));
  emptyShared = true; privateIds.clear();
  CHECK(call(client, "playlist", {{"id", "collection_3_12345_456_0"}}, session).value("songs").toList().size() == 250);
  CHECK(privateIds.first() == "456");
  emptyShared = false;
  CHECK(call(client, "liked", {}, session).value("songs").toList().size() == 2);
  const QString hash = "0123456789abcdef0123456789abcdef";
  CHECK(call(client, "like", {{"id", hash}, {"like", true}}, session).value("ok") == true);
  CHECK(call(client, "like", {{"id", hash}, {"like", false}}, session).value("ok") == true);
  result = call(client, "create", {{"name", QString::fromUtf8("新建歌单")}}, session);
  CHECK(result.value("ok") == true && result.value("playlists").toList().first().toMap().value("id") == "new-global");
  CHECK(result.value("playlists").toList().first().toMap().value("editId") == "new-list");
  reflectCreated = false;
  result = call(client, "create", {{"name", "stable fallback"}}, session);
  CHECK(result.value("playlists").toList().first().toMap().value("id") == "1234567890123");
  stableCreatedId = false;
  CHECK(call(client, "create", {{"name", "no stable ID"}}, session).value("playlists").toList().isEmpty());
  CHECK(call(client, "collect", {{"id", "collected-global"}, {"collect", true}}, session).value("ok") == true);
  auto previousCounts = counts;
  CHECK(call(client, "collect", {{"id", "collected-global"}, {"collect", false}}, session).value("ok") == false && counts == previousCounts);
  transientPost = true;
  const int writes = counts["/cloudlist.service/v5/add_list"];
  CHECK(call(client, "create", {{"name", "write retry guard"}}, session).value("error") == "network_error");
  CHECK(counts["/cloudlist.service/v5/add_list"] == writes + 1);
  transientPost = false; sharedSizes.clear();
  result = call(client, "share", {{"url", "https://t1.kugou.com/fixture-short"}}, session);
  CHECK(result.value("playlists").toList().first().toMap().value("id") == "collection_3_12345_456_0");
  CHECK(result.value("playlists").toList().first().toMap().value("name") == QString::fromUtf8("分享歌单"));
  CHECK(!result.value("playlists").toList().first().toMap().value("collected").toBool() && sharedSizes == QList<int>{1});
  metadataFailure = true;
  result = call(client, "share", {{"url", "https://www.kugou.com/songlist/public-123"}}, session);
  CHECK(result.value("playlists").toList().first().toMap().value("name") == QString::fromUtf8("酷狗歌单"));
  metadataFailure = false; redirect = "https://example.invalid/playlist/private";
  CHECK(call(client, "share", {{"url", "https://t1.kugou.com/fixture-short"}}, session).value("error") == "redirect_rejected");
  previousCounts = counts;
  CHECK(call(client, "share", {{"url", "https://kugou.com.example.invalid/songlist/private"}}, session).value("playlists").toList().isEmpty());
  CHECK(counts == previousCounts);
  repeatPage = true;
  CHECK(call(client, "playlist", {{"id", "public-123"}}, session).value("error") == "pagination_stalled");
  repeatPage = false; declaredTotal = 9000;
  CHECK(call(client, "playlist", {{"id", "public-123"}}, session).value("error") == "incomplete_response");
  declaredTotal = 20001;
  CHECK(call(client, "playlist", {{"id", "public-123"}}, session).value("error") == "response_too_large");
  declaredTotal = 250; malformed = true;
  CHECK(call(client, "playlist", {{"id", "public-123"}}, session).value("error") == "invalid_response");
  malformed = false; reject = true;
  CHECK(call(client, "like", {{"id", hash}, {"like", true}}, session).value("error") == "provider_error");
  reject = false; httpStatus = 401;
  CHECK(call(client, "userPlaylists", {}, session).value("error") == "login_required");
  CHECK(call(client, "userPlaylists").value("error") == "login_required");
  CHECK(call(client, "like", {{"id", "../bad"}, {"like", true}}, session).value("error") == "invalid_arguments");
  CHECK(valid);
  puts("NATIVE_KUGOU_LIBRARY_FIXTURE_PASS signing cloud list IDs 250-track paging fallback likes writes no replay stable create H5 shares redirects malformed expiry");
  return 0;
}
