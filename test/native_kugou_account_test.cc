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
  if (!(condition)) { fprintf(stderr, "native Kugou account failed at %d\n", __LINE__); return 1; } \
} while (false)

namespace {
QDBusMessage request(const QString& operation, const QVariantMap& arguments, const QString& session) {
  auto message = QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Silica",
      "/org/sailfishos/Mconnect/Platforms", "org.sailfishos.Mconnect.Platforms", "KugouAccount");
  message << operation << QString::fromUtf8(QJsonDocument::fromVariant(arguments).toJson(QJsonDocument::Compact)) << session;
  return message;
}

QVariantMap receive(QDBusPendingCall pending) {
  QDBusPendingCallWatcher watcher(pending);
  QEventLoop loop;
  QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
  if (!watcher.isFinished()) loop.exec();
  QDBusPendingReply<QString> reply = watcher;
  if (reply.isError()) return {{"error", "dbus_error"}};
  return QJsonDocument::fromJson(reply.value().toUtf8()).toVariant().toMap();
}

QVariantMap call(QDBusConnection& bus, const QString& operation, const QVariantMap& args = {}, const QString& session = {}) {
  return receive(bus.asyncCall(request(operation, args, session), 20000));
}

void respond(QTcpSocket* socket, const QByteArray& body, int status = 200) {
  socket->write("HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Length: " +
      QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
  socket->disconnectFromHost();
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  auto bus = QDBusConnection::sessionBus();
  auto client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "kugou-account-client");
  CHECK(bus.registerService("org.sailfishos.Mconnect.Silica"));
  QTcpServer fixture;
  CHECK(fixture.listen(QHostAddress::LocalHost));
  bool valid = true, profileFailure = false, profileMismatch = false, missingQrUser = false, missingToken = false;
  bool waf = false, holdCheck = false, holdKey = false;
  int qrStatus = 1, profileHttpStatus = 200, sequence = 0;
  QMap<QString, int> counts;
  std::function<void()> release;
  QString mostRecentProviderKey;
  QObject::connect(&fixture, &QTcpServer::newConnection, &app, [&]() {
    while (fixture.hasPendingConnections()) {
      auto* socket = fixture.nextPendingConnection();
      auto bytes = std::make_shared<QByteArray>();
      auto started = std::make_shared<bool>(false);
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      QObject::connect(socket, &QTcpSocket::readyRead, &app, [&, socket, bytes, started]() {
        if (*started) return;
        bytes->append(socket->readAll());
        if (!bytes->contains("\r\n\r\n")) return;
        *started = true;
        const auto first = bytes->left(bytes->indexOf("\r\n")).split(' ');
        const QUrl url("http://localhost" + QString::fromLatin1(first[1]));
        const auto path = url.path();
        const QUrlQuery query(url);
        ++counts[path];
        valid = valid && first[0] == "GET" && !bytes->toLower().contains("cookie:") && !bytes->toLower().contains("authorization:");
        QVariantMap root{{"status", 1}, {"error_code", 0}};
        int httpStatus = 200;
        if (path == "/v2/qrcode" || path == "/v2/get_userinfo_qrcode") {
          QMap<QString, QString> parameters;
          for (const auto& item : query.queryItems(QUrl::FullyDecoded)) parameters[item.first] = item.second;
          const auto signature = parameters.take("signature");
          const QByteArray secret = "NVPh5oo715z5DIWAeQlhMDsWXXQV4hwt";
          QByteArray material = secret;
          for (auto item = parameters.cbegin(); item != parameters.cend(); ++item) material += (item.key() + '=' + item.value()).toUtf8();
          valid = valid && signature == QString::fromLatin1(QCryptographicHash::hash(material + secret, QCryptographicHash::Md5).toHex());
          valid = valid && !parameters.contains("token") && !parameters.contains("userid") && parameters.value("srcappid") == "2919";
          valid = valid && parameters.value("mid") == "fixture-mid" && parameters.value("dfid") == "fixture-dfid";
          if (path == "/v2/qrcode") {
            mostRecentProviderKey = QString("provider-key-%1").arg(++sequence);
            valid = valid && parameters.value("appid") == "1001";
            valid = valid && parameters.value("clientver") == "11440";
            valid = valid && parameters.value("qrcode_txt") == "https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=3116&";
            root["data"] = QVariantMap{{"qrcode", mostRecentProviderKey}, {"qrcode_img", "data:image/png;base64,iVBORw0KGgo="}};
          } else {
            valid = valid && parameters.value("appid") == "3116" && parameters.value("clientver") == "11440";
            valid = valid && parameters.value("qrcode") == mostRecentProviderKey;
            QVariantMap data{{"status", qrStatus}};
            if (qrStatus == 4) {
              data["user_info"] = QVariantMap{{"vip_token", "fixture-vip"}, {"vip_type", 6},
                {"mid", "updated-mid"}, {"uuid", "updated-uuid"}, {"dfid", "updated-dfid"}};
              if (!missingToken) data["token"] = "new-token";
              if (!missingQrUser) data["userid"] = "1234567890123";
            }
            root["data"] = data;
          }
        } else if (path == "/uc/userinfo") {
          valid = valid && query.queryItemValue("token") == "new-token";
          valid = valid && query.queryItemValue("appid") == "3116" && query.queryItemValue("clientver") == "11440";
          valid = valid && query.queryItemValue("mid") == "updated-mid" && query.queryItemValue("uuid") == "updated-uuid";
          root["status"] = profileFailure ? 0 : 1;
          root["error_code"] = profileFailure ? 10004 : 0;
          root["data"] = QVariantMap{{"user_id", profileMismatch ? "987654321" : "1234567890123"}, {"nick_name", QString::fromUtf8("中文用户")}};
          httpStatus = profileHttpStatus;
        } else if (path == "/api/v2/user/vip") {
          valid = valid && query.queryItemValue("token") == "legacy-token" && query.queryItemValue("format") == "json";
          root["data"] = QVariantMap{{"vip_type", 1}};
        } else valid = false;
        auto body = QJsonDocument::fromVariant(root).toJson(QJsonDocument::Compact);
        if (waf) body = "Access Deny ! No Actions !";
        if ((path == "/v2/qrcode" && holdKey) || (path == "/v2/get_userinfo_qrcode" && holdCheck)) {
          holdKey = false; holdCheck = false;
          release = [socket, body, httpStatus]() { respond(socket, body, httpStatus); };
        } else respond(socket, body, httpStatus);
      });
    }
  });
  const QUrl base(QString("http://127.0.0.1:%1").arg(fixture.serverPort()));
  NativePlatforms service(nullptr, QUrl("https://music.163.com"), QUrl("https://api.bilibili.com"),
      QUrl("https://passport.bilibili.com"), QUrl("https://u.y.qq.com"), QUrl("https://c.y.qq.com"), base);
  CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms", &service, QDBusConnection::ExportAllSlots));
  const QString session = "{\"token\":\"old-token\",\"userid\":\"42\",\"vip_token\":\"old-vip\",\"vip_type\":\"6\",\"client\":\"lite\",\"mid\":\"fixture-mid\",\"dfid\":\"fixture-dfid\",\"uuid\":\"fixture-uuid\"}";
  auto result = call(client, "qrKey", {}, session);
  QString key = result.value("qrKey").toString();
  CHECK(!key.isEmpty() && key != mostRecentProviderKey && result.value("qrImage") == "iVBORw0KGgo=");
  CHECK(QUrlQuery(QUrl(result.value("qrUrl").toString())).queryItemValue("appid") == "3116");
  CHECK(!ParseNativeKugouSession(result.value("session").toString()).contains("token"));
  CHECK(call(client, "qrCheck", {{"key", key}}, session).value("qrStatus") == "waiting");
  qrStatus = 2;
  CHECK(call(client, "qrCheck", {{"key", key}}, session).value("qrStatus") == "scanned");
  qrStatus = 4;
  result = call(client, "qrCheck", {{"key", key}}, session);
  CHECK(result.value("qrStatus") == "success" && result.value("user").toMap().value("id") == "1234567890123");
  CHECK(result.value("user").toMap().value("nickname") == QString::fromUtf8("中文用户"));
  const auto restored = ParseNativeKugouSession(result.value("session").toString());
  CHECK(restored.value("token") == "new-token" && restored.value("vip_token") == "fixture-vip" && restored.value("vip_type") == "6");
  CHECK(restored.value("mid") == "updated-mid" && restored.value("uuid") == "updated-uuid");
  const auto terminalRequests = counts;
  CHECK(call(client, "qrCheck", {{"key", key}}, session) == result && counts == terminalRequests);
  CHECK(call(client, "qrCheck", {{"key", key}}, result.value("session").toString()) == result && counts == terminalRequests);
  CHECK(call(client, "qrCheck", {{"key", key}}, "replacement-token|99").value("error") == "cancelled");
  CHECK(call(client, "qrCancel", {{"key", key}}, session).value("ok") == true);
  CHECK(call(client, "qrCheck", {{"key", key}}, session).value("error") == "cancelled");
  profileFailure = true;
  key = call(client, "qrKey", {}, session).value("qrKey").toString();
  result = call(client, "qrCheck", {{"key", key}}, session);
  CHECK(result.value("qrStatus") == "success" && result.value("user").toMap().value("nickname") == QString::fromUtf8("酷狗用户"));
  profileFailure = false; missingQrUser = true;
  key = call(client, "qrKey", {}, session).value("qrKey").toString();
  CHECK(call(client, "qrCheck", {{"key", key}}, session).value("user").toMap().value("id") == "1234567890123");
  missingQrUser = false; profileMismatch = true;
  key = call(client, "qrKey", {}, session).value("qrKey").toString();
  CHECK(call(client, "qrCheck", {{"key", key}}, session).value("error") == "identity_mismatch");
  profileMismatch = false;
  for (const int status : {401, 403}) {
    profileHttpStatus = status;
    key = call(client, "qrKey", {}, session).value("qrKey").toString();
    CHECK(call(client, "qrCheck", {{"key", key}}, session).value("error") == "login_required");
  }
  profileHttpStatus = 200; missingToken = true;
  key = call(client, "qrKey", {}, session).value("qrKey").toString();
  CHECK(call(client, "qrCheck", {{"key", key}}, session).value("qrStatus") == "failed");
  missingToken = false; qrStatus = 0;
  key = call(client, "qrKey", {}, session).value("qrKey").toString();
  CHECK(call(client, "qrCheck", {{"key", key}}, session).value("qrStatus") == "expired");
  qrStatus = 1;
  key = call(client, "qrKey", {}, session).value("qrKey").toString();
  holdCheck = true; release = {};
  auto held = client.asyncCall(request("qrCheck", {{"key", key}}, session), 20000);
  QElapsedTimer waiting; waiting.start();
  while (!release && waiting.elapsed() < 2000) app.processEvents();
  CHECK(bool(release));
  CHECK(call(client, "qrCheck", {{"key", key}}, session).value("error") == "busy");
  call(client, "qrCancel", {{"key", key}}, session);
  release(); release = {};
  CHECK(receive(held).value("error") == "cancelled");
  holdKey = true;
  held = client.asyncCall(request("qrKey", {}, session), 20000);
  waiting.restart();
  while (!release && waiting.elapsed() < 2000) app.processEvents();
  CHECK(bool(release));
  const auto replacement = call(client, "qrKey", {}, session).value("qrKey").toString();
  release(); release = {};
  CHECK(receive(held).value("error") == "cancelled");
  CHECK(call(client, "qrCheck", {{"key", replacement}}, session).value("qrStatus") == "waiting");
  CHECK(call(client, "vip", {}, session).value("vipLevel") == 2);
  CHECK(call(client, "vip", {}, "{\"token\":\"fixture\",\"userid\":\"42\",\"vip_token\":\"fixture\",\"vip_type\":\"1\"}").value("vipLevel") == 1);
  CHECK(call(client, "vip", {}, "legacy-token|42").value("vipLevel") == 1);
  waf = true;
  CHECK(call(client, "qrKey", {}, session).value("error") == "invalid_response");
  CHECK(call(client, "profile").value("error") == "login_required");
  CHECK(call(client, "qrCheck", {{"key", "../bad"}}, session).value("error") == "invalid_arguments");
  CHECK(valid);
  puts("NATIVE_KUGOU_ACCOUNT_FIXTURE_PASS web signing QR identity ownership terminal replay cancellation supersession profile fallback expiry VIP legacy sessions");
  return 0;
}
