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
#include <zlib.h>

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "native Kugou content failed at %d\n", __LINE__); \
    return 1; \
  } \
} while (false)

namespace {
const QString songHash = "0123456789abcdef0123456789abcdef";
const QString losslessHash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const QString highHash = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const QString lyricText = QString::fromUtf8("[0,2000]<0,500,0>中文<500,500,0>歌词\n");

QString md5(const QByteArray& bytes) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
}

QByteArray encryptedKrc(const QByteArray& text) {
  QByteArray compressed(int(compressBound(text.size())), '\0');
  uLongf size = compressed.size();
  if (compress2(reinterpret_cast<Bytef*>(compressed.data()), &size,
      reinterpret_cast<const Bytef*>(text.constData()), text.size(), Z_BEST_COMPRESSION) != Z_OK) return {};
  compressed.resize(int(size));
  const unsigned char key[]{0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47,
                            0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e, 0x69};
  for (int index = 0; index < compressed.size(); ++index) {
    compressed[index] = char(static_cast<unsigned char>(compressed[index]) ^ key[index % 16]);
  }
  return ("krc1" + compressed).toBase64();
}

QVariantMap call(QDBusConnection& bus, const QString& operation, const QVariantMap& arguments,
                 const QString& session = {}) {
  auto message = QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Silica",
      "/org/sailfishos/Mconnect/Platforms", "org.sailfishos.Mconnect.Platforms", "KugouContent");
  message << operation << QString::fromUtf8(QJsonDocument::fromVariant(arguments).toJson(QJsonDocument::Compact)) << session;
  QDBusPendingCallWatcher watcher(bus.asyncCall(message, 20000));
  QEventLoop loop;
  QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
  loop.exec();
  QDBusPendingReply<QString> reply = watcher;
  if (reply.isError()) return {{"error", "dbus_error"}};
  return QJsonDocument::fromJson(reply.value().toUtf8()).toVariant().toMap();
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  auto bus = QDBusConnection::sessionBus();
  auto client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "kugou-content-client");
  CHECK(bus.registerService("org.sailfishos.Mconnect.Silica"));
  QTcpServer fixture;
  CHECK(fixture.listen(QHostAddress::LocalHost));
  bool valid = true, metadataUrl = true, badMetadata = false, badPlaylists = false;
  bool hashCandidate = true, invalidLyrics = false, hugeLyrics = false;
  QString selectedRoute, expiryRoute, transientRoute, downloadedHash;
  int expiryStatus = 401, transientRemaining = 0, keywordCandidateAt = 1;
  QMap<QString, int> counts;
  QList<QPair<QString, qint64>> lyricQueries;
  QVariantMap lastPrivateBody;
  QStringList routeOrder;
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
        int contentLength = 0;
        for (const auto& line : headers.split('\n')) {
          if (line.toLower().startsWith("content-length:")) contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
        }
        if (bytes->size() < separator + 4 + contentLength) return;
        *started = true;
        const auto first = headers.left(headers.indexOf("\r\n")).split(' ');
        const QUrl url("http://localhost" + QString::fromLatin1(first[1]));
        const QUrlQuery query(url);
        const auto path = url.path();
        const auto requestBody = bytes->mid(separator + 4, contentLength);
        const auto lowerHeaders = headers.toLower();
        valid = valid && !lowerHeaders.contains("cookie:") && !lowerHeaders.contains("authorization:");
        ++counts[path];
        int status = 200;
        QVariantMap root{{"status", 1}, {"error", ""}};
        if (path == "/app/i/getSongInfo.php") {
          valid = valid && first[0] == "GET" && query.queryItemValue("hash") == songHash;
          root = {{"status", 0}, {"songName", QString::fromUtf8("中文歌")}, {"singerName", QString::fromUtf8("中文歌手")},
            {"timeLength", 234000}, {"extra", QVariantMap{{"sqhash", losslessHash}, {"highhash", highHash}}},
            {"data", QVariantMap{{"album_id", "1234567890123"}, {"album_audio_id", "4567890123456"}}}};
          if (metadataUrl) root["url"] = "https://audio.test/ordinary.mp3";
        } else if (path == "/v5/url" || path == "/v6/priv_url" || path == "/v1/search/special") {
          QMap<QString, QString> parameters;
          for (const auto& item : query.queryItems(QUrl::FullyDecoded)) parameters[item.first] = item.second;
          const auto signature = parameters.take("signature");
          const bool lite = parameters.value("appid") == "3116";
          const QByteArray secret = lite ? "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA" : "OIlwieks28dk2k092lksi2UIkp";
          QByteArray material = secret;
          for (auto item = parameters.cbegin(); item != parameters.cend(); ++item) material += (item.key() + '=' + item.value()).toUtf8();
          valid = valid && signature == md5(material + requestBody + secret);
          valid = valid && parameters.value("clienttime").toLongLong() > 0;
          if (parameters.value("token") == "fixture-token") {
            valid = valid && parameters.value("userid") == "1234567890123" && parameters.value("mid") == "fixture-mid";
            valid = valid && parameters.value("dfid") == "fixture-dfid" && parameters.value("uuid") == "fixture-uuid";
          } else if (parameters.value("token") == "legacy-token") {
            valid = valid && parameters.value("userid") == "12345" && parameters.value("mid") == "mid";
          }
          if (path == "/v1/search/special") {
            valid = valid && first[0] == "GET" && lowerHeaders.contains("x-router: complexsearch.kugou.com");
            valid = valid && parameters.value("keyword") == QString::fromUtf8("中文 A+B & #");
            valid = valid && parameters.value("page") == "2" && parameters.value("pagesize") == "5";
            QVariantMap playlist{{"global_collection_id", "collection-123"}, {"listid", 456},
              {"specialname", QString::fromUtf8("中文歌单")}, {"songcount", "405"}, {"imgurl", "https://image.test/{size}/cover"},
              {"nickname", QString::fromUtf8("中文用户")}};
            root["data"] = QVariantMap{{"lists", badPlaylists ? QVariant("malformed") : QVariant(QVariantList{playlist})}};
          } else {
            const auto route = path == "/v6/priv_url" ? QString("private") : lite ? QString("lite") : QString("android");
            routeOrder.append(route);
            ++counts[route];
            const auto hash = path == "/v6/priv_url" ? QJsonDocument::fromJson(requestBody).toVariant().toMap()
                .value("resource").toMap().value("hash").toString() : parameters.value("hash");
            downloadedHash = hash;
            const auto user = parameters.value("userid", "0");
            const auto mid = parameters.value("mid", "mid");
            if (route == "private") {
              valid = valid && first[0] == "POST" && !lowerHeaders.contains("x-router:");
              lastPrivateBody = QJsonDocument::fromJson(requestBody).toVariant().toMap();
              valid = valid && lastPrivateBody.value("tracker_param").toMap().value("key") ==
                  md5((hash + "185672dd44712f60bb1736df5a377e823116" + mid + user).toUtf8());
            } else {
              valid = valid && first[0] == "GET" && lowerHeaders.contains("x-router: trackercdn.kugou.com");
              const auto keySecret = lite ? "185672dd44712f60bb1736df5a377e82" : "57ae12eb6890223e355ccfcb74edf70d";
              valid = valid && parameters.value("key") == md5((hash + keySecret + parameters.value("appid") + mid + user).toUtf8());
              valid = valid && parameters.value("album_id") == "1234567890123" && parameters.value("album_audio_id") == "4567890123456";
              valid = valid && parameters.value("pid") == (lite ? "411" : "2");
            }
            if (route == expiryRoute) status = expiryStatus;
            else if (route == transientRoute && transientRemaining > 0) { status = 503; --transientRemaining; }
            else if (route == selectedRoute) {
              root["data"] = QVariantMap{{"audio_info", QVariantMap{{"backup_urls", QVariantList{"invalid", "//audio.test/selected.flac"}}}}};
            }
          }
        } else if (path == "/search") {
          valid = valid && first[0] == "GET";
          bool candidate = hashCandidate;
          if (query.queryItemValue("client") == "mobi") {
            valid = valid && query.queryItemValue("hash") == songHash;
          } else {
            lyricQueries.append({query.queryItemValue("keyword", QUrl::FullyDecoded), query.queryItemValue("duration").toLongLong()});
            candidate = lyricQueries.size() >= keywordCandidateAt;
          }
          root["candidates"] = candidate ? QVariantList{QVariantMap{{"id", "lyric-123"}, {"accesskey", "fixture+A&B"}}} : QVariantList();
        } else if (path == "/download") {
          valid = valid && first[0] == "GET" && query.queryItemValue("accesskey", QUrl::FullyDecoded) == "fixture+A&B";
          const bool lrc = query.queryItemValue("fmt") == "lrc";
          ++counts[lrc ? "lrc" : "krc"];
          root["contenttype"] = lrc ? 1 : 0;
          root["content"] = lrc ? QString::fromLatin1(lyricText.toUtf8().toBase64()) :
              QString::fromLatin1(hugeLyrics ? encryptedKrc(QByteArray(4 * 1024 * 1024 + 1, 'x')) :
                  invalidLyrics ? QByteArray("Y29ycnVwdA==") : encryptedKrc(lyricText.toUtf8()));
        } else valid = false;
        auto body = QJsonDocument::fromVariant(root).toJson(QJsonDocument::Compact);
        if (path == "/app/i/getSongInfo.php" && badMetadata) body = "Access Deny ! No Actions !";
        socket->write("HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Length: " +
            QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
      });
    }
  });
  const QUrl base(QString("http://127.0.0.1:%1").arg(fixture.serverPort()));
  NativePlatforms service(nullptr, QUrl("https://music.163.com"), QUrl("https://api.bilibili.com"),
      QUrl("https://passport.bilibili.com"), QUrl("https://u.y.qq.com"), QUrl("https://c.y.qq.com"), base);
  CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms", &service, QDBusConnection::ExportAllSlots));
  const QString session = QString::fromUtf8(QJsonDocument::fromVariant(QVariantMap{{"token", "fixture-token"},
    {"userid", "1234567890123"}, {"vip_token", "fixture-vip"}, {"vip_type", "6"},
    {"mid", "fixture-mid"}, {"dfid", "fixture-dfid"}, {"uuid", "fixture-uuid"}, {"client", "lite"}}).toJson(QJsonDocument::Compact));
  const QVariantMap streamArgs{{"id", songHash.toUpper()}, {"quality", 3}};
  auto result = call(client, "stream", {{"id", songHash.toUpper()}, {"quality", 0}}, session);
  CHECK(result.value("url") == "https://audio.test/ordinary.mp3" && routeOrder.isEmpty());
  selectedRoute = "private";
  result = call(client, "stream", streamArgs, session);
  CHECK(result.value("url") == "https://audio.test/selected.flac" && routeOrder == QStringList{"private"});
  CHECK(downloadedHash == losslessHash && lastPrivateBody.value("qualities").toList().first() == "flac");
  CHECK(lastPrivateBody.value("tracker_param").toMap().value("viptoken") == "fixture-vip");
  CHECK(lastPrivateBody.value("resource").toMap().value("album_audio_id").toLongLong() == 4567890123456LL);
  routeOrder.clear(); selectedRoute = "android";
  CHECK(call(client, "stream", streamArgs, session).value("url") == "https://audio.test/selected.flac");
  CHECK(routeOrder == (QStringList{"private", "android"}));
  routeOrder.clear(); selectedRoute = "lite";
  CHECK(call(client, "stream", {{"id", songHash}, {"quality", 4}}, session).value("url") == "https://audio.test/selected.flac");
  CHECK(downloadedHash == highHash && routeOrder == (QStringList{"private", "android", "lite"}));
  routeOrder.clear(); selectedRoute.clear();
  CHECK(call(client, "stream", streamArgs, session).value("url") == "https://audio.test/ordinary.mp3");
  CHECK(routeOrder == (QStringList{"private", "android", "lite"}));
  metadataUrl = false;
  CHECK(call(client, "stream", streamArgs, "legacy-token|12345").value("error") == "unavailable");
  expiryRoute = "android";
  for (const int code : {401, 403}) {
    expiryStatus = code; routeOrder.clear();
    CHECK(call(client, "stream", streamArgs, "legacy-token|12345").value("error") == "login_required");
    CHECK(routeOrder == QStringList{"android"});
  }
  expiryRoute = "private"; routeOrder.clear();
  CHECK(call(client, "stream", streamArgs, session).value("error") == "login_required" && routeOrder == QStringList{"private"});
  expiryRoute.clear(); selectedRoute = "android"; transientRoute = "private"; transientRemaining = 1; routeOrder.clear();
  CHECK(call(client, "stream", streamArgs, session).value("url") == "https://audio.test/selected.flac");
  CHECK(routeOrder == (QStringList{"private", "android"}));
  transientRoute = "android"; transientRemaining = 1; routeOrder.clear();
  CHECK(call(client, "stream", streamArgs, "legacy-token|12345").value("url") == "https://audio.test/selected.flac");
  CHECK(routeOrder == (QStringList{"android", "android"}));
  transientRoute.clear(); badMetadata = true;
  CHECK(call(client, "stream", streamArgs).value("error") == "invalid_response");
  badMetadata = false;
  result = call(client, "searchPlaylists", {{"query", QString::fromUtf8("中文 A+B & #")}, {"page", 2}, {"limit", 5}}, session);
  const auto playlist = result.value("playlists").toList().first().toMap();
  CHECK(playlist.value("id") == "collection-123" && playlist.value("editId") == "456");
  CHECK(playlist.value("coverUrl") == "https://image.test/480/cover" && playlist.value("songCount").toInt() == 405);
  CHECK(!playlist.value("editable").toBool() && playlist.value("collected").toBool());
  badPlaylists = true;
  CHECK(call(client, "searchPlaylists", {{"query", QString::fromUtf8("中文 A+B & #")}, {"page", 2}, {"limit", 5}}).value("error") == "invalid_response");
  badPlaylists = false;
  result = call(client, "qualities", {{"id", "historical arbitrary id"}});
  CHECK(result.value("qualities").toList().size() == 5 && result.value("qualities").toList()[2].toMap().value("level").toInt() == 3);
  result = call(client, "lyrics", {{"id", songHash}});
  CHECK(result.value("lyrics").toMap().value("lrc") == lyricText && counts["lrc"] == 0);
  invalidLyrics = true;
  CHECK(call(client, "lyrics", {{"id", songHash}}).value("lyrics").toMap().value("lrc") == lyricText && counts["lrc"] == 1);
  invalidLyrics = false; hugeLyrics = true;
  CHECK(call(client, "lyrics", {{"id", songHash}}).value("lyrics").toMap().value("lrc") == lyricText && counts["lrc"] == 2);
  hugeLyrics = false; hashCandidate = false; keywordCandidateAt = 3; lyricQueries.clear();
  CHECK(call(client, "lyrics", {{"id", songHash}}).value("lyrics").toMap().value("lrc") == lyricText);
  CHECK(lyricQueries.size() == 3 && lyricQueries[0].first == QString::fromUtf8("中文歌手-中文歌"));
  CHECK(lyricQueries[1].first == QString::fromUtf8("中文歌") && lyricQueries[1].second == 234000 && lyricQueries[2].second == 234);
  keywordCandidateAt = 1; lyricQueries.clear();
  CHECK(call(client, "lyricsByInfo", {{"query", QString::fromUtf8("中文 A+B & #")}, {"duration", 123000}}).value("lyrics").toMap().value("lrc") == lyricText);
  CHECK(lyricQueries.size() == 1 && lyricQueries.first().first == QString::fromUtf8("中文 A+B & #") && lyricQueries.first().second == 123000);
  CHECK(call(client, "stream", {{"id", "../bad"}, {"quality", 0}}).value("error") == "invalid_arguments");
  CHECK(call(client, "stream", {{"id", songHash}, {"quality", 8}}).value("error") == "invalid_arguments");
  CHECK(call(client, "qualities", {{"id", "any"}}, "{\"userid\":123.5}").value("error") == "invalid_arguments");
  CHECK(call(client, "qualities", {{"id", "any"}}, "{\"token\":true}").value("error") == "invalid_arguments");
  CHECK(call(client, "qualities", {{"id", "any"}}, "{\"unknown\":\"value\"}").value("error") == "invalid_arguments");
  CHECK(call(client, "qualities", {{"id", "any"}}, "{\"userid\":12345,\"vip_token\":null,\"client\":\"概念版\"}").value("qualities").toList().size() == 5);
  CHECK(valid);
  puts("NATIVE_KUGOU_CONTENT_FIXTURE_PASS signing identities quality VIP routes expiry retry metadata playlists KRC LRC decompression-bound lyrics fallbacks sessions");
  return 0;
}
