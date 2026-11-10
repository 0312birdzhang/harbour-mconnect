// Run on an isolated session bus with local HTTP fixtures.
#include "../elinux/silica/native_platforms.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QEventLoop>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <cstdio>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "native QQ account failed at %d\n", __LINE__); return 1; } } while (false)

namespace {
QDBusPendingCall dispatch(QDBusConnection& bus, const QString& operation,
                          const QVariantMap& arguments, const QString& cookie) {
  auto message = QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Silica",
      "/org/sailfishos/Mconnect/Platforms", "org.sailfishos.Mconnect.Platforms", "QqAccount");
  message << operation << QString::fromUtf8(QJsonDocument::fromVariant(arguments).toJson(QJsonDocument::Compact)) << cookie;
  return bus.asyncCall(message, 22000);
}

QVariantMap receive(const QDBusPendingCall& pending) {
  QDBusPendingCallWatcher watcher(pending);
  if (!watcher.isFinished()) {
    QEventLoop loop;
    QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(23000, &loop, &QEventLoop::quit);
    loop.exec();
  }
  const QDBusPendingReply<QString> reply = watcher;
  return reply.isError() ? QVariantMap{{"error", "dbus_error"}} :
      QJsonDocument::fromJson(reply.value().toUtf8()).toVariant().toMap();
}

QVariantMap call(QDBusConnection& bus, const QString& operation,
                 const QVariantMap& arguments = {}, const QString& cookie = {}) {
  return receive(dispatch(bus, operation, arguments, cookie));
}

quint32 hash(const QString& value, quint32 seed) {
  for (const auto character : value) seed = seed * 33 + character.unicode();
  return seed & 0x7fffffff;
}

enum class Mode {
  Normal, ShowCode, BodyCode, Expired, InvalidImage, MissingSignature,
  BadCallback, BadRedirect, RedirectLoop, LoginRejected, LoginMissingCode,
  LoginHttpError, IdentityMismatch, ProfileError, ProfileMalformed, ProfileExpired
};
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  auto bus = QDBusConnection::sessionBus();
  auto client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "qq-account-client");
  CHECK(bus.registerService("org.sailfishos.Mconnect.Silica"));

  QTcpServer fixture;
  CHECK(fixture.listen(QHostAddress::LocalHost));
  Mode mode = Mode::Normal;
  QString pollCode = "66";
  QMap<QString, int> requests;
  bool valid = true;
  int pollDelay = 0;
  const auto qrImage = QByteArray::fromHex("89504e470d0a1a0a") + QByteArray("fixture");
  QObject::connect(&fixture, &QTcpServer::newConnection, &application, [&]() {
    while (fixture.hasPendingConnections()) {
      auto* socket = fixture.nextPendingConnection();
      auto buffer = std::make_shared<QByteArray>();
      auto handled = std::make_shared<bool>(false);
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      QObject::connect(socket, &QTcpSocket::readyRead, &application, [&, socket, buffer, handled]() {
        buffer->append(socket->readAll());
        const int headerEnd = buffer->indexOf("\r\n\r\n");
        if (*handled || headerEnd < 0) return;
        const auto length = QRegularExpression("Content-Length:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption)
            .match(QString::fromLatin1(buffer->left(headerEnd))).captured(1).toInt();
        if (buffer->size() < headerEnd + 4 + length) return;
        *handled = true;
        const auto firstLine = buffer->left(buffer->indexOf("\r\n")).split(' ');
        const QUrl url = QUrl::fromEncoded(firstLine.value(1));
        const QUrlQuery query(url);
        const auto path = url.path();
        requests[path]++;
        valid = valid && !buffer->contains("old-account") && !buffer->contains("Domain=.qq.com; Cookie");
        QByteArray body, headers;
        int status = 200;
        QVariantMap root;

        if (path == "/ptqrshow") {
          valid = valid && query.queryItemValue("appid") == "716027609" && query.queryItemValue("pt_3rd_aid") == "100497308";
          body = mode == Mode::InvalidImage ? QByteArray("<html>captcha</html>") : qrImage;
          if (mode != Mode::MissingSignature) headers = "Set-Cookie: qrsig=test-signature; Path=/, consent=yes; Path=/\r\n";
        } else if (path == "/ptqrlogin") {
          valid = valid && query.queryItemValue("ptqrtoken") == QString::number(hash("test-signature", 0)) &&
              buffer->contains("qrsig=test-signature") && query.queryItemValue("u1", QUrl::FullyDecoded) == "https://graph.qq.com/oauth2.0/login_jump";
          const auto code = mode == Mode::Expired ? QString("65") : pollCode;
          const auto redirect = mode == Mode::BadCallback ? QString("https://example.invalid/check_sig?uin=12345") :
              QString("https://ssl.ptlogin2.graph.qq.com/check_sig?uin=12345&ptsigx=fixture");
          body = QString("ptuiCB('%1','0','%2','0','message','')").arg(code, code == "0" ? redirect : QString()).toUtf8();
        } else if (path == "/check_sig") {
          status = 302;
          if (mode == Mode::BadRedirect) headers = "Location: https://example.invalid/check_sig\r\n";
          else if (mode == Mode::RedirectLoop) headers = "Location: /check_sig?uin=12345\r\n";
          else if (query.queryItemValue("stage") != "2") {
            headers = "Location: /check_sig?stage=2&uin=12345\r\n"
                "Set-Cookie: pt2gguin=o12345; Path=/, p_skey=@Xb8aQtVvd; Path=/; Domain=.qq.com\r\n"
                "Set-Cookie: p_skey=; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Domain=.ptlogin2.graph.qq.com\r\n";
          } else {
            valid = valid && buffer->contains("p_skey=@Xb8aQtVvd");
            headers = "Location: https://graph.qq.com/oauth2.0/login_jump\r\nSet-Cookie: uin=o12345; Path=/\r\n";
          }
        } else if (path == "/oauth2.0/login_jump") {
          body = "<html>continue</html>";
        } else if (path == "/oauth2.0/show") {
          valid = valid && query.queryItemValue("scope") == "get_user_info" && !query.hasQueryItem("surl") &&
              buffer->contains("p_skey=@Xb8aQtVvd");
          if (mode == Mode::ShowCode) {
            status = 302;
            headers = "Location: https://y.qq.com/portal/wx_redirect.html?login_type=1&code=auth-code\r\n";
          } else body = "<form action='authorize'></form>";
        } else if (path == "/oauth2.0/authorize") {
          const QUrlQuery form(QString::fromUtf8(buffer->mid(headerEnd + 4)));
          valid = valid && firstLine.value(0) == "POST" && form.queryItemValue("g_tk") == "1934165869" &&
              form.queryItemValue("redirect_uri", QUrl::FullyDecoded) == "https://y.qq.com/portal/wx_redirect.html?login_type=1" &&
              buffer->contains("Origin: https://graph.qq.com") && buffer->contains("Referer: https://graph.qq.com/oauth2.0/show");
          if (mode == Mode::BodyCode) body = "<script>location.href='https://y.qq.com/portal/wx_redirect.html?code=auth%2Bcode%2Fvalue&amp;state=1';</script>";
          else {
            status = 302;
            headers = "Location: https://y.qq.com/portal/wx_redirect.html?login_type=1&code=auth-code\r\n";
          }
        } else if (path == "/cgi-bin/musicu.fcg") {
          const auto request = QJsonDocument::fromJson(buffer->mid(headerEnd + 4)).toVariant().toMap();
          const auto login = request.value("req").toMap();
          valid = valid && request.value("comm").toMap().value("g_tk").toUInt() == 1934165869 &&
              login.value("module") == "QQConnectLogin.LoginServer" && login.value("method") == "QQLogin" &&
              login.value("param").toMap().value("code") == (mode == Mode::BodyCode ? "auth+code/value" : "auth-code");
          const QVariantMap data{{"uin", mode == Mode::IdentityMismatch ? 99999 : 12345}, {"musicid", 12345678901234LL},
            {"musickey", "music-token"}, {"openid", "open"}, {"access_token", "access"}, {"refresh_key", "refresh"}};
          QVariantMap module{{"code", mode == Mode::LoginRejected ? 7 : 0}, {"data", data}};
          if (mode == Mode::LoginMissingCode) module.remove("code");
          root = {{"code", "0"}, {"req", module}};
          if (mode == Mode::LoginHttpError) status = 503;
        } else if (path == "/rsc/fcgi-bin/fcg_get_profile_homepage.fcg") {
          valid = valid && query.queryItemValue("userid") == "12345" && buffer->contains("uin=o12345") &&
              buffer->contains("qqmusic_uin=12345678901234") && buffer->contains("qm_keyst=music-token");
          root = {{"code", 0}, {"data", QVariantMap{{"home", QVariantMap{{"creator", QVariantMap{
              {"nick", QString::fromUtf8("中文用户")}, {"headurl", "//image.test/user"}}}}}}}};
          if (mode == Mode::ProfileError) status = 503;
          if (mode == Mode::ProfileExpired) status = 401;
          if (mode == Mode::ProfileMalformed) body = "{invalid";
        } else valid = false;

        if (body.isEmpty() && !root.isEmpty()) body = QJsonDocument::fromVariant(root).toJson(QJsonDocument::Compact);
        const auto response = "HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\n" + headers +
            "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        const auto guarded = QPointer<QTcpSocket>(socket);
        QTimer::singleShot(path == "/ptqrlogin" ? pollDelay : 0, socket, [guarded, response]() {
          if (!guarded) return;
          guarded->write(response);
          guarded->disconnectFromHost();
        });
      });
    }
  });

  const QUrl base(QString("http://127.0.0.1:%1").arg(fixture.serverPort()));
  NativePlatforms service(nullptr, QUrl("https://music.163.com"), QUrl("https://api.bilibili.com"),
      QUrl("https://passport.bilibili.com"), base, base, QUrl(), base);
  CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms", &service, QDBusConnection::ExportAllSlots));
  CHECK(QJsonDocument::fromJson(service.QqAccount("qrKey", "{}", "").toUtf8()).toVariant().toMap().value("error") == "dbus_required");

  auto qr = call(client, "qrKey", {}, "uin=o99999; old-account=secret");
  CHECK(qr.value("ok").toBool() && QByteArray::fromBase64(qr.value("qrImage").toByteArray()) == qrImage);
  QString key = qr.value("qrKey").toString(), cookie = qr.value("cookie").toString();
  CHECK(!key.isEmpty() && cookie.contains("qrsig=test-signature") && !cookie.contains("old-account") && !cookie.contains("Path="));
  auto result = call(client, "qrCheck", {{"key", key}}, cookie);
  CHECK(result.value("qrStatus") == "waiting");
  pollCode = "67";
  CHECK(call(client, "qrCheck", {{"key", key}}, cookie).value("qrStatus") == "scanned");
  pollCode = "0";
  result = call(client, "qrCheck", {{"key", key}}, cookie);
  CHECK(result.value("qrStatus") == "success" && result.value("user").toMap().value("id") == "12345" &&
      result.value("user").toMap().value("nickname") == QString::fromUtf8("中文用户"));
  CHECK(result.value("cookie").toString().contains("p_skey=@Xb8aQtVvd") && result.value("cookie").toString().contains("qqmusic_uin=12345678901234"));
  const int exchanges = requests["/cgi-bin/musicu.fcg"];
  CHECK(call(client, "qrCheck", {{"key", key}}, cookie) == result);
  CHECK(call(client, "qrCheck", {{"key", key}}, result.value("cookie").toString()) == result);
  CHECK(requests["/cgi-bin/musicu.fcg"] == exchanges);
  CHECK(call(client, "qrCheck", {{"key", key}}, "qrsig=another").value("error") == "cancelled");

  for (const auto shape : {Mode::ShowCode, Mode::BodyCode, Mode::ProfileError, Mode::ProfileMalformed}) {
    mode = shape;
    qr = call(client, "qrKey");
    const int authorizeCount = requests["/oauth2.0/authorize"];
    result = call(client, "qrCheck", {{"key", qr.value("qrKey")}}, qr.value("cookie").toString());
    CHECK(result.value("qrStatus") == "success");
    if (shape == Mode::ShowCode) CHECK(requests["/oauth2.0/authorize"] == authorizeCount);
    if (shape == Mode::ProfileError || shape == Mode::ProfileMalformed) CHECK(result.value("user").toMap().value("nickname") == QString::fromUtf8("QQ用户"));
  }

  for (const auto rejected : {Mode::BadCallback, Mode::BadRedirect, Mode::RedirectLoop, Mode::LoginRejected,
                             Mode::LoginMissingCode, Mode::LoginHttpError, Mode::IdentityMismatch, Mode::ProfileExpired}) {
    mode = rejected;
    qr = call(client, "qrKey");
    key = qr.value("qrKey").toString();
    cookie = qr.value("cookie").toString();
    result = call(client, "qrCheck", {{"key", key}}, cookie);
    const auto error = result.value("error").toString();
    CHECK(!error.isEmpty() && error != "dbus_error");
    if (rejected == Mode::BadCallback || rejected == Mode::BadRedirect) CHECK(error == "redirect_rejected");
    if (rejected == Mode::RedirectLoop) CHECK(error == "redirect_limit");
    if (rejected == Mode::IdentityMismatch) CHECK(error == "identity_mismatch");
    if (rejected == Mode::ProfileExpired) CHECK(error == "login_required");
    if (rejected == Mode::LoginHttpError) {
      const int count = requests["/cgi-bin/musicu.fcg"];
      CHECK(call(client, "qrCheck", {{"key", key}}, cookie) == result && requests["/cgi-bin/musicu.fcg"] == count);
    }
  }
  for (const auto invalidQr : {Mode::InvalidImage, Mode::MissingSignature}) {
    mode = invalidQr;
    CHECK(call(client, "qrKey").value("error") == "invalid_response");
  }
  mode = Mode::Expired;
  qr = call(client, "qrKey");
  CHECK(call(client, "qrCheck", {{"key", qr.value("qrKey")}}, qr.value("cookie").toString()).value("qrStatus") == "expired");

  mode = Mode::Normal;
  pollCode = "66";
  qr = call(client, "qrKey");
  key = qr.value("qrKey").toString();
  cookie = qr.value("cookie").toString();
  CHECK(call(client, "qrCancel", {{"key", "wrong-key"}}).value("ok").toBool());
  CHECK(call(client, "qrCheck", {{"key", key}}, cookie).value("qrStatus") == "waiting");
  pollDelay = 150;
  auto pending = dispatch(client, "qrCheck", {{"key", key}}, cookie);
  CHECK(call(client, "qrCheck", {{"key", key}}, cookie).value("error") == "busy");
  CHECK(receive(pending).value("qrStatus") == "waiting");
  pending = dispatch(client, "qrCheck", {{"key", key}}, cookie);
  CHECK(call(client, "qrCancel", {{"key", key}}).value("ok").toBool());
  CHECK(receive(pending).value("error") == "cancelled");
  CHECK(call(client, "qrCheck", {{"key", key}}, cookie).value("error") == "cancelled");
  pollDelay = 0;
  qr = call(client, "qrKey");
  const auto replacement = call(client, "qrKey");
  CHECK(qr.value("qrKey") != replacement.value("qrKey"));
  CHECK(call(client, "qrCheck", {{"key", qr.value("qrKey")}}, qr.value("cookie").toString()).value("error") == "cancelled");

  const int before = requests["/ptqrshow"];
  CHECK(call(client, "qrKey", {{"extra", true}}).value("error") == "invalid_arguments");
  CHECK(call(client, "qrCheck", {{"key", 42}}).value("error") == "invalid_arguments");
  CHECK(call(client, "qrCancel", {{"key", "../invalid"}}).value("error") == "invalid_arguments");
  CHECK(call(client, "qrKey", {}, "bad\r\nCookie: injected").value("error") == "invalid_arguments");
  CHECK(requests["/ptqrshow"] == before && valid);
  puts("NATIVE_QQ_ACCOUNT_FIXTURE_PASS QR OAuth cookies callback body fallback cancellation concurrency terminal replay identity validation");
  return 0;
}
