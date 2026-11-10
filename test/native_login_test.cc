#include "../elinux/silica/native_login.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "LOGIN_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
bool waitFor(const std::function<bool()>& ready) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!ready() && elapsed.elapsed() < 3000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(1); }
  return ready();
}
QVariantMap success(const QString& id = "123") {
  return {{"backend", "cpp-qq-account"}, {"qrStatus", "success"}, {"cookie", "uin=o" + id + "; qm_keyst=fixture"},
      {"user", QVariantMap{{"id", id}, {"platform", "qq"}, {"nickname", "fixture"}}}};
}
bool fails(const std::function<void()>& action) { try { action(); } catch (const std::runtime_error&) { return true; } return false; }
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  NativeStorage storage(temporary.path(), temporary.path() + "/cache");
  NativeLogin* current = nullptr;
  NativeMusicServices* services = nullptr;
  QString mode = "waiting";
  int polls = 0, cancellations = 0;
  NativeMusicServices music(storage, nullptr, [&](const QString&, const QVariantList& arguments) {
    const auto operation = arguments[0].toString();
    if (operation == "qrCancel") { ++cancellations; return QVariantMap{{"backend", "cpp-qq-account"}, {"ok", true}}; }
    if (operation == "qrKey") {
      if (mode == "cancelLoading") current->cancel("loading");
      return QVariantMap{{"backend", "cpp-qq-account"}, {"qrKey", "fixture"}, {"qrUrl", "https://example.test/login"}};
    }
    CHECK(operation == "qrCheck");
    ++polls;
    if (mode == "cancelPolling") current->cancel("polling");
    if (mode == "replaceAccount") services->acceptLogin("qq", "uin=o456; qm_keyst=fixture", success("456").value("user").toMap());
    if (mode == "waiting") return QVariantMap{{"backend", "cpp-qq-account"}, {"qrStatus", "waiting"}};
    return success();
  });
  services = &music;
  NativeLogin login(music, nullptr, 100, 5);
  current = &login;

  CHECK(fails([&]() { login.begin("qq", "x", "android"); }));
  CHECK(fails([&]() { login.begin("qq", ""); }));
  const auto state = login.begin("qq", "expiry");
  CHECK(state.value("status") == "waiting" && state.value("image").toString().startsWith("data:image/png;base64,"));
  const auto qrBytes = QByteArray::fromBase64(state.value("image").toString().section(',', 1).toLatin1());
  CHECK(QImage::fromData(qrBytes).size() == QSize(512, 512));
  login.cancel("another-session");
  CHECK(!login.state().isEmpty());
  CHECK(waitFor([&]() { return login.state().value("status") == "expired"; }));
  CHECK(polls > 0 && !music.loggedIn("qq"));

  mode = "cancelLoading";
  CHECK(login.begin("qq", "loading").value("cancelled").toBool());
  CHECK(login.state().isEmpty());
  mode = "cancelPolling";
  login.begin("qq", "polling");
  CHECK(waitFor([&]() { return login.state().isEmpty(); }));
  CHECK(!music.loggedIn("qq"));

  mode = "replaceAccount";
  login.begin("qq", "replace");
  CHECK(waitFor([&]() { return login.state().isEmpty(); }));
  CHECK(music.accountScope("qq") == "456");

  mode = "success";
  auto cancelSaving = QObject::connect(&login, &NativeLogin::changed, [&]() {
    if (login.state().value("status") == "saving") login.cancel("saving");
  });
  login.begin("qq", "saving");
  CHECK(waitFor([&]() { return login.state().isEmpty(); }));
  CHECK(music.accountScope("qq") == "456");
  QObject::disconnect(cancelSaving);

  login.begin("qq", "complete");
  CHECK(waitFor([&]() { return login.state().value("status") == "success"; }));
  CHECK(music.accountScope("qq") == "123");
  music.logout("netease");
  CHECK(login.state().value("status") == "success");

  auto logoutSaving = QObject::connect(&music, &NativeMusicServices::accountsChanged, [&]() {
    if (login.state().value("status") == "saving" && music.loggedIn("qq")) music.logout("qq");
  });
  login.begin("qq", "logout-during-save");
  CHECK(waitFor([&]() { return login.state().isEmpty(); }));
  CHECK(!music.loggedIn("qq"));
  QObject::disconnect(logoutSaving);

  const auto credential = temporary.path() + "/credentials/" + QString::fromLatin1(QByteArray("user_qq").toBase64(QByteArray::Base64UrlEncoding));
  CHECK(QDir().mkpath(credential));
  login.begin("qq", "persistence-failure");
  CHECK(waitFor([&]() { return login.state().value("status") == "failed"; }));
  CHECK(!music.loggedIn("qq"));
  CHECK(cancellations > 0);
  CHECK(fails([&]() { NativeQrImage({{"qrImage", "invalid"}}); }));
  CHECK(fails([&]() { NativeQrImage({{"qrUrl", QString(4096, QLatin1Char('x'))}}); }));
  puts("NATIVE_LOGIN_PASS memory-qr expiry client-ownership cancel-loading cancel-polling cancel-saving stale-success account-replacement failed-persistence");
  return 0;
}
