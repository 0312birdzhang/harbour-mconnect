#include "native_platforms.h"
#include "platform_models.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>

namespace {
constexpr int loginLifetime = 5 * 60 * 1000;
constexpr int requestBudget = 18000;

QString serialize(const QVariantMap& value) {
  return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
}

QVariantMap failure(const QString& error) {
  return {{"backend", "cpp-kugou-account"}, {"error", error}};
}

QString field(const QVariant& source, const QStringList& keys, int depth = 0) {
  if (source.type() != QVariant::Map || depth > 6) return {};
  const auto data = source.toMap();
  for (const auto& key : keys) {
    const auto value = data.value(key);
    if (value.isNull() || value.type() == QVariant::Bool) continue;
    const auto text = value.type() == QVariant::String ? value.toString().trimmed() :
        ParseNeteaseArtist({{"id", value}}).id;
    if (!text.isEmpty() && text != "null") return text;
  }
  for (const auto& key : QStringList{"user_info", "userinfo", "userInfo", "profile", "data", "cookie"}) {
    const auto text = field(data.value(key), keys, depth + 1);
    if (!text.isEmpty()) return text;
  }
  return {};
}

QVariantMap mergedSession(QVariantMap session, const QVariantMap& data) {
  const QList<QPair<QString, QStringList>> aliases{
    {"token", {"token", "usertoken", "t"}}, {"userid", {"userid", "user_id", "uid", "id"}},
    {"vip_token", {"vip_token", "vipToken", "viptoken"}}, {"vip_type", {"vip_type", "vipType", "viptype", "vip"}},
    {"dfid", {"dfid", "DFID"}}, {"mid", {"mid", "KUGOU_API_MID", "kg_mid"}},
    {"uuid", {"uuid", "KUGOU_API_GUID", "guid"}}
  };
  for (const auto& entry : aliases) {
    const auto value = field(data, entry.second);
    if (!value.isEmpty()) session[entry.first] = value;
  }
  return ParseNativeKugouSession(serialize(session));
}

QVariantMap user(const QVariantMap& data, const QString& fallbackId) {
  auto id = field(data, {"user_id", "userid", "uid", "id"});
  if (id.isEmpty()) id = fallbackId;
  if (!QRegularExpression("^[0-9]{1,18}$").match(id).hasMatch()) return {};
  auto name = field(data, {"nick_name", "nickname", "username", "user_name"});
  if (name.isEmpty()) name = QString::fromUtf8("酷狗用户");
  if (name.size() > 8192) return {};
  return {{"id", id}, {"nickname", name}, {"platform", "kugou"}, {"vipLevel", 0}};
}

bool image(const QByteArray& data) {
  return data.startsWith(QByteArray::fromHex("89504e470d0a1a0a")) || data.startsWith("GIF87a") ||
      data.startsWith("GIF89a") || data.startsWith(QByteArray::fromHex("ffd8ff")) ||
      (data.startsWith("RIFF") && data.mid(8, 4) == "WEBP");
}

class KugouAccountJob : public std::enable_shared_from_this<KugouAccountJob> {
 public:
  NativePlatformHttp* http = nullptr;
  QUrl endpoint;
  QString operation, key, targetUser;
  QVariantMap session, qrData;
  std::shared_ptr<NativeKugouAuthState> state;
  QElapsedTimer elapsed;
  using Reply = std::function<void(QVariantMap)>;
  Reply done;

  bool current() const { return !state || state->key == key; }

  void finish(QVariantMap result) {
    if (!current()) result = failure("cancelled");
    else {
      if (state) {
        state->busy = false;
        state->session = session;
      }
      if (!result.contains("error")) {
        result["ok"] = true;
        result["session"] = serialize(session);
        qInfo("MCONNECT_NATIVE_KUGOU_ACCOUNT_OK operation=%s", qPrintable(operation));
      }
      result["backend"] = "cpp-kugou-account";
      const auto status = result.value("qrStatus").toString();
      if (state && operation == "qrCheck" && QStringList{"success", "expired", "failed"}.contains(status)) {
        state->terminalResult = result;
      }
    }
    if (serialize(result).toUtf8().size() > 2 * 1024 * 1024) result = failure("response_too_large");
    done(result);
  }

  void fetch(const QUrl& logicalUrl, const QVariantMap& parameters, Reply reply, bool legacy = false) {
    if (!current()) { finish(failure("cancelled")); return; }
    const int remaining = requestBudget - int(elapsed.elapsed());
    if (remaining <= 0) { reply(failure("timeout")); return; }
    const auto base = endpoint.isEmpty() ? logicalUrl : endpoint.resolved(QUrl(logicalUrl.path()));
    QList<QPair<QString, QString>> query;
    for (auto item = parameters.cbegin(); item != parameters.cend(); ++item) query.append({item.key(), item.value().toString()});
    QNetworkRequest request(NativePlatformUrl(base, query));
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Linux; Android 11) AppleWebKit/537.36");
    const auto self = shared_from_this();
    http->request(request, "GET", {}, [self, reply](NativeHttpResult response) {
      if (!self->current()) { self->finish(failure("cancelled")); return; }
      reply(response.error.isEmpty() ? ParseNativeKugouResponse(response.body) : failure(response.error));
    }, remaining, 2 * 1024 * 1024, true, false, true, legacy);
  }

  void qrKey() {
    const auto appid = session.value("client") == "lite" ? "3116" : "1005";
    const auto parameters = NativeKugouParameters(session, {{"appid", 1001}, {"type", 1}, {"plat", 4},
      {"qrcode_txt", QString("https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=%1&").arg(appid)},
      {"srcappid", 2919}}, {}, NativeKugouSigning::Web);
    const auto self = shared_from_this();
    fetch(QUrl("https://login-user.kugou.com/v2/qrcode"), parameters, [self, appid](QVariantMap result) {
      if (result.contains("error")) { self->finish(result); return; }
      if (result.contains("status") && !NativeKugouSuccess(result)) { self->finish(failure("provider_error")); return; }
      const auto data = result.value("data").toMap();
      const auto providerKey = field(data, {"qrcode", "key", "qrcode_id"});
      if (!QRegularExpression("^[A-Za-z0-9_-]{1,256}$").match(providerKey).hasMatch()) {
        self->finish(failure("invalid_response")); return;
      }
      self->state->providerKey = providerKey;
      const auto url = NativePlatformUrl(QUrl("https://h5.kugou.com/apps/loginQRCode/html/index.html"),
          {{"appid", appid}, {"qrcode", providerKey}}).toString();
      QVariantMap reply{{"qrKey", self->key}, {"qrUrl", url}};
      auto encodedImage = field(data, {"qrcode_img"});
      const auto marker = encodedImage.indexOf("base64,");
      if (marker >= 0) encodedImage = encodedImage.mid(marker + 7);
      if (encodedImage.size() <= 1400000) {
        const auto bytes = QByteArray::fromBase64(encodedImage.toLatin1());
        if (bytes.size() <= 1024 * 1024 && image(bytes)) reply["qrImage"] = QString::fromLatin1(bytes.toBase64());
      }
      self->finish(reply);
    });
  }

  void qrCheck() {
    const auto parameters = NativeKugouParameters(session, {{"plat", 4},
      {"appid", session.value("client") == "lite" ? 3116 : 1005}, {"srcappid", 2919},
      {"qrcode", state->providerKey}}, {}, NativeKugouSigning::Web);
    const auto self = shared_from_this();
    fetch(QUrl("https://login-user.kugou.com/v2/get_userinfo_qrcode"), parameters, [self](QVariantMap result) {
      if (result.contains("error")) { self->finish(result); return; }
      if (result.contains("status") && !NativeKugouSuccess(result)) { self->finish(failure("provider_error")); return; }
      const auto data = result.value("data");
      if (data.type() != QVariant::Map) { self->finish(failure("invalid_response")); return; }
      bool valid = false;
      const auto status = data.toMap().value("status").toInt(&valid);
      const auto rawStatus = data.toMap().value("status");
      if (!valid || rawStatus.type() == QVariant::Bool ||
          (rawStatus.type() == QVariant::Double && rawStatus.toDouble() != double(status))) {
        self->finish(failure("invalid_response")); return;
      }
      if (status != 4) {
        self->finish({{"qrStatus", status == 2 ? "scanned" : status == 0 ? "expired" : "waiting"}});
        return;
      }
      self->qrData = data.toMap();
      const auto session = mergedSession(self->session, self->qrData);
      if (session.contains("error")) { self->finish(failure("invalid_response")); return; }
      self->session = session;
      self->targetUser = self->session.value("userid").toString();
      if (self->session.value("token").toString().isEmpty()) { self->finish({{"qrStatus", "failed"}}); return; }
      self->profile();
    });
  }

  void profile() {
    const bool lite = session.value("client") == "lite";
    QVariantMap parameters{{"srcappid", 2919}, {"clientver", lite ? 11440 : 20000},
      {"clienttime", QDateTime::currentMSecsSinceEpoch() / 1000}, {"mid", session.value("mid", "mid")},
      {"uuid", session.value("uuid", "uuid")}, {"dfid", session.value("dfid", "-")},
      {"appid", lite ? 3116 : 1005}, {"platid", 4}};
    for (const auto& name : QStringList{"token", "userid"}) {
      if (session.contains(name)) parameters[name] = session.value(name);
    }
    const auto self = shared_from_this();
    fetch(QUrl("https://wwwapi.kugou.com/uc/userinfo"), parameters, [self](QVariantMap result) {
      if (result.contains("error") || !NativeKugouSuccess(result) || result.value("data").type() != QVariant::Map) {
        self->profileFailed(result.value("error", "provider_error").toString()); return;
      }
      const auto data = result.value("data").toMap();
      const auto profile = user(data, self->targetUser);
      if (profile.isEmpty()) { self->profileFailed("invalid_response"); return; }
      if (!self->targetUser.isEmpty() && self->targetUser != profile.value("id")) {
        self->finish(failure("identity_mismatch")); return;
      }
      const auto session = mergedSession(self->session, data);
      if (session.contains("error")) { self->finish(failure("invalid_response")); return; }
      self->session = session;
      self->session["userid"] = profile.value("id");
      QVariantMap reply{{"user", profile}};
      if (self->operation == "qrCheck") reply["qrStatus"] = "success";
      self->finish(reply);
    });
  }

  void profileFailed(const QString& error) {
    if (operation != "qrCheck" || error == "login_required") { finish(failure(error)); return; }
    const auto profile = user(qrData, targetUser);
    if (profile.isEmpty()) { finish({{"qrStatus", "failed"}}); return; }
    qInfo("MCONNECT_NATIVE_KUGOU_PROFILE_FALLBACK");
    session["userid"] = profile.value("id");
    finish({{"user", profile}, {"qrStatus", "success"}});
  }

  void vip() {
    const auto type = session.value("vip_type").toInt();
    const bool privateSession = !session.value("token").toString().isEmpty() &&
        !session.value("userid").toString().isEmpty() && !session.value("vip_token").toString().isEmpty();
    if (type > 0) { finish({{"vipLevel", type >= 2 ? 2 : 1}}); return; }
    if (privateSession) { finish({{"vipLevel", 2}}); return; }
    QVariantMap parameters{{"format", "json"}};
    if (session.contains("token")) parameters["token"] = session.value("token");
    const auto self = shared_from_this();
    fetch(QUrl("http://mobilecdn.kugou.com/api/v2/user/vip"), parameters, [self](QVariantMap result) {
      if (result.contains("error")) { self->finish(result); return; }
      const auto value = field(result.value("data"), {"vip_type", "vipType", "viptype", "is_vip", "isVip", "vip"}).toInt();
      self->finish({{"vipLevel", value >= 2 ? 2 : value >= 1 ? 1 : 0}});
    }, true);
  }
};
}

bool NativeKugouSuccess(const QVariantMap& data) {
  if (data.value("status") == 1 || data.value("status") == true) return true;
  for (const auto& key : QStringList{"code", "errcode", "error_code", "errorCode"}) {
    bool valid = false;
    const auto code = data.value(key).toInt(&valid);
    if (valid && data.value(key).type() != QVariant::Bool && (code == 0 || code == 200)) return true;
  }
  return false;
}

QString NativePlatforms::KugouAccount(const QString& operation, const QString& arguments, const QString& encodedSession) {
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(arguments.toUtf8(), &error);
  const auto session = ParseNativeKugouSession(encodedSession);
  if (!QStringList{"qrKey", "qrCheck", "qrCancel", "profile", "vip"}.contains(operation) ||
      arguments.toUtf8().size() > 16384 || error.error != QJsonParseError::NoError ||
      !document.isObject() || session.contains("error")) return serialize(failure("invalid_arguments"));
  const auto args = document.toVariant().toMap();
  const auto key = args.value("key").toString();
  const bool needsKey = operation == "qrCheck" || operation == "qrCancel";
  if ((needsKey && (args.size() != 1 || args.value("key").type() != QVariant::String ||
      !QRegularExpression("^[A-Za-z0-9_-]{1,64}$").match(key).hasMatch())) ||
      (!needsKey && !args.isEmpty())) return serialize(failure("invalid_arguments"));
  if (!calledFromDBus()) return serialize(failure("dbus_required"));
  if (operation == "qrCancel") {
    if (kugouAuthState_->key == key) {
      kugouAuthState_->key.clear();
      kugouAuthState_->providerKey.clear();
      kugouAuthState_->session.clear();
      kugouAuthState_->requestSession.clear();
      kugouAuthState_->terminalResult.clear();
      kugouAuthState_->busy = false;
    }
    return serialize({{"backend", "cpp-kugou-account"}, {"ok", true}});
  }
  if (operation == "qrKey") {
    kugouAuthState_->key = QUuid::createUuid().toString().remove('{').remove('}');
    kugouAuthState_->providerKey.clear();
    kugouAuthState_->terminalResult.clear();
    kugouAuthState_->requestSession = session;
    kugouAuthState_->session = session;
    for (const auto& name : QStringList{"token", "userid", "vip_token", "vip_type"}) kugouAuthState_->session.remove(name);
    kugouAuthState_->lifetime.start();
  } else if (operation == "qrCheck") {
    if (kugouAuthState_->key != key || (session != kugouAuthState_->requestSession && session != kugouAuthState_->session)) {
      return serialize(failure("cancelled"));
    }
    if (!kugouAuthState_->terminalResult.isEmpty()) return serialize(kugouAuthState_->terminalResult);
    if (!kugouAuthState_->lifetime.isValid() || kugouAuthState_->lifetime.elapsed() >= loginLifetime) {
      return serialize({{"backend", "cpp-kugou-account"}, {"ok", true}, {"qrStatus", "expired"}, {"session", serialize(kugouAuthState_->session)}});
    }
    if (kugouAuthState_->busy) return serialize(failure("busy"));
    if (kugouAuthState_->providerKey.isEmpty()) return serialize(failure("invalid_response"));
  }
  if (operation == "profile" && (session.value("token").toString().isEmpty() || session.value("userid").toString().isEmpty())) {
    return serialize(failure("login_required"));
  }
  setDelayedReply(true);
  const auto bus = connection();
  const auto call = message();
  auto job = std::make_shared<KugouAccountJob>();
  job->http = &http_;
  job->endpoint = kugouEndpoint_;
  job->operation = operation;
  job->session = session;
  job->targetUser = session.value("userid").toString();
  if (operation == "qrKey" || operation == "qrCheck") {
    job->state = kugouAuthState_;
    job->key = kugouAuthState_->key;
    job->session = kugouAuthState_->session;
    kugouAuthState_->busy = true;
  }
  job->done = [bus, call](const QVariantMap& result) { bus.send(call.createReply(QVariantList{serialize(result)})); };
  job->elapsed.start();
  if (operation == "qrKey") job->qrKey();
  else if (operation == "qrCheck") job->qrCheck();
  else if (operation == "profile") job->profile();
  else job->vip();
  return {};
}
