#include "../elinux/silica/native_core.h"

#include <QCoreApplication>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QEventLoop>
#include <QJsonDocument>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "CORE_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
QDBusMessage request(const QString& method, const QString& args = "{}", const QString& interface = "org.sailfishos.Mconnect.Core") {
  auto call = QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Core", "/org/sailfishos/Mconnect/Core", interface, "Request");
  call.setArguments({method, args});
  const auto client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "native-core-test-client");
  QDBusPendingCallWatcher watcher(client.asyncCall(call, 5000));
  QEventLoop loop;
  QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
  if (!watcher.isFinished()) loop.exec();
  return watcher.reply();
}
QVariantMap object(const QString& value) { return QJsonDocument::fromJson(value.toUtf8()).toVariant().toMap(); }
bool fails(const std::function<void()>& action) {
  try { action(); } catch (const NativeRequestFailure&) { return true; }
  return false;
}
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  qint64 now = 0;
  QStringList order;
  bool readWhileBusy = false;
  NativeCoreService service([&](const QString& method, const QVariantMap& args) -> QVariant {
    if (method == "snapshot") { readWhileBusy = order.contains("slow-start") && !order.contains("slow-end"); return QVariantMap{{"version", 1}}; }
    if (method == "fail") throw NativeRequestFailure(QStringLiteral("可显示的错误"));
    if (method == "secret") throw std::runtime_error("cookie=DO_NOT_EXPOSE; signed_url=DO_NOT_EXPOSE");
    if (method == "slow") {
      order.append("slow-start");
      QEventLoop loop;
      QTimer::singleShot(100, &loop, &QEventLoop::quit);
      loop.exec();
      order.append("slow-end");
      return true;
    }
    if (method == "write") { order.append("write"); return true; }
    if (method == "importPlaylist") return args;
    throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  }, nullptr, [&]() { return now; });
  CHECK(service.encode(true) == "true" && service.encode(false) == "false" && service.encode(QVariant()) == "null");
  CHECK(service.encode(QVariantList{1, 2}) == "[1,2]");
  CHECK(fails([&]() { service.Request("snapshot", "[]"); }));
  CHECK(fails([&]() { service.Request("snapshot", "{broken}"); }));
  CHECK(fails([&]() { service.Request("snapshot", QString(65537, ' ')); }));

  const QVariantMap large{{"lines", QString(75000, QChar(0x4e2d))}};
  const auto expected = QJsonDocument::fromVariant(large).toJson(QJsonDocument::Compact);
  const auto descriptor = object(service.encode(large));
  const auto token = descriptor.value("_mconnectTransfer").toString();
  CHECK(!token.isEmpty() && descriptor.value("parts").toInt() > 1);
  QByteArray joined;
  for (int index = 0; index < descriptor.value("parts").toInt(); ++index) {
    const auto part = service.transferPart({{"token", token}, {"index", index}}).value("data").toByteArray();
    CHECK(part.size() <= 32768);
    joined += QByteArray::fromBase64(part);
  }
  CHECK(joined == expected);
  CHECK(fails([&]() { service.transferPart({{"token", token}, {"index", 0}}); }));
  const auto expires = object(service.encode(large)).value("_mconnectTransfer");
  now = 60000;
  CHECK(fails([&]() { service.transferPart({{"token", expires}, {"index", 0}}); }));
  const auto evicted = object(service.encode(large)).value("_mconnectTransfer");
  for (int index = 0; index < 4; ++index) service.encode(large);
  CHECK(fails([&]() { service.transferPart({{"token", evicted}, {"index", 0}}); }));
  CHECK(fails([&]() { service.encode(QVariantMap{{"large", QString(8 * 1024 * 1024, 'x')}}); }));
  QString signal;
  QObject::connect(&service, &NativeCoreService::Changed, [&](const QString&, const QString& json) { signal = json; });
  service.publish("lyrics", large);
  CHECK(signal == "{\"_mconnectRefresh\":true}");
  service.publish("state", QVariantMap{{"version", 1}});
  CHECK(object(signal).value("version").toInt() == 1);

  CHECK(service.start());
  CHECK(request("snapshot").type() == QDBusMessage::ReplyMessage);
  CHECK(request("unknown").errorName() == "org.freedesktop.DBus.Error.InvalidArgs");
  CHECK(request("snapshot", "{}", "org.sailfishos.Mconnect.Unknown").type() == QDBusMessage::ErrorMessage);
  CHECK(request("snapshot", "[]").errorName() == "org.freedesktop.DBus.Error.InvalidArgs");
  CHECK(request("fail").errorMessage() == QStringLiteral("可显示的错误"));
  const auto secret = request("secret");
  CHECK(secret.errorMessage() == QStringLiteral("请求失败，请稍后重试"));
  CHECK(!secret.errorMessage().contains("DO_NOT_EXPOSE"));
  CHECK(request("snapshot", "{\"x\":\"" + QString(65536, 'x') + "\"}").type() == QDBusMessage::ErrorMessage);
  CHECK(request("importPlaylist", "{\"link\":\"" + QString(70000, 'x') + "\"}").type() == QDBusMessage::ReplyMessage);

  QTimer::singleShot(10, &service, [&]() { CHECK(request("snapshot").type() == QDBusMessage::ReplyMessage); });
  QTimer::singleShot(20, &service, [&]() {
    auto call = QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Core", "/org/sailfishos/Mconnect/Core", "org.sailfishos.Mconnect.Core", "Request");
    call.setArguments({"write", "{}"});
    QDBusConnection::connectToBus(QDBusConnection::SessionBus, "native-core-test-client").asyncCall(call);
  });
  CHECK(request("slow").type() == QDBusMessage::ReplyMessage);
  QEventLoop finish;
  QTimer::singleShot(50, &finish, &QEventLoop::quit);
  finish.exec();
  CHECK(readWhileBusy && order == QStringList({"slow-start", "slow-end", "write"}));
  service.close();
  puts("NATIVE_CORE_SERVICE_PASS dbus-signature bounded-json scalar-results redacted-errors transfer-chunks expiry eviction refresh-signals serial-writes responsive-state");
  return 0;
}
