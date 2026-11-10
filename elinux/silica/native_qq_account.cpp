#include "native_platforms.h"
#include "platform_models.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkCookie>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <QUuid>

namespace {
constexpr int requestBudget = 18000;
constexpr int loginLifetime = 300000;

QString serialize(const QVariantMap& value) {
  return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
}

QVariantMap failure(const QString& error) {
  return {{"backend", "cpp-qq-account"}, {"ok", false}, {"error", error}};
}

bool validCookie(const QString& cookie) {
  if (cookie.size() > 32768) return false;
  for (const auto character : cookie) {
    if (character.unicode() < 32 || character.unicode() > 126) return false;
  }
  return true;
}

QMap<QString, QString> cookiePairs(const QString& cookie) {
  const QSet<QString> attributes{"path", "domain", "expires", "max-age", "secure", "httponly", "samesite"};
  QMap<QString, QString> result;
  for (const auto& field : cookie.split(';')) {
    const int separator = field.indexOf('=');
    if (separator < 1) continue;
    const auto name = field.left(separator).trimmed();
    const auto value = field.mid(separator + 1).trimmed();
    if (!name.isEmpty() && !value.isEmpty() && !attributes.contains(name.toLower())) result[name] = value;
  }
  return result;
}

QString joinCookies(const QMap<QString, QString>& pairs) {
  QStringList fields;
  for (auto item = pairs.cbegin(); item != pairs.cend(); ++item) fields.append(item.key() + '=' + item.value());
  return fields.join("; ");
}

QString mergeCookies(const QString& original, const QList<QPair<QByteArray, QByteArray>>& headers) {
  auto pairs = cookiePairs(original);
  const QRegularExpression folded(",(?=\\s*[A-Za-z_$][A-Za-z0-9_$]*\\s*=)");
  for (const auto& header : headers) {
    if (header.first.toLower() != "set-cookie") continue;
    for (const auto& line : QString::fromLatin1(header.second).split(folded)) {
      for (const auto& cookie : QNetworkCookie::parseCookies(line.toLatin1())) {
        // The existing persisted QQ jar is flat. Domain-scoped deletion lines
        // must not erase a value obtained from another hop in this login.
        if (cookie.name().isEmpty() || cookie.value().isEmpty()) continue;
        pairs[QString::fromLatin1(cookie.name())] = QString::fromLatin1(cookie.value());
      }
    }
  }
  return joinCookies(pairs);
}

QString headerValue(const NativeHttpResult& result, const QByteArray& name) {
  for (const auto& header : result.headers) {
    if (header.first.toLower() == name) return QString::fromLatin1(header.second);
  }
  return {};
}

quint32 cookieHash(const QString& value, quint32 seed) {
  for (const auto character : value) seed = seed * 33 + character.unicode();
  return seed & 0x7fffffff;
}

bool trustedUrl(const QUrl& url) {
  if (!url.isValid() || url.scheme() != "https" || url.port(443) != 443 ||
      !url.userInfo().isEmpty() || url.hasFragment() || url.toEncoded().size() > 16384) return false;
  const auto host = url.host();
  const auto path = url.path();
  if (host == "ssl.ptlogin2.qq.com") return path == "/ptqrshow" || path == "/ptqrlogin" || path == "/check_sig";
  if (host == "ssl.ptlogin2.graph.qq.com" || host == "ptlogin2.graph.qq.com" || host == "ptlogin2.qq.com") return path == "/check_sig";
  if (host == "graph.qq.com") {
    return QStringList{"/oauth2.0/login_jump", "/oauth2.0/show", "/oauth2.0/authorize"}.contains(path);
  }
  if (host == "y.qq.com") return path == "/portal/wx_redirect.html";
  if (host == "u.y.qq.com") return path == "/cgi-bin/musicu.fcg";
  return host == "c.y.qq.com" && path == "/rsc/fcgi-bin/fcg_get_profile_homepage.fcg";
}

QString callbackCode(const QUrl& url) {
  if (!trustedUrl(url) || url.host() != "y.qq.com" || url.path() != "/portal/wx_redirect.html") return {};
  const auto code = QUrlQuery(url).queryItemValue("code", QUrl::FullyDecoded);
  return QRegularExpression("^[A-Za-z0-9._~+/=-]{1,512}$").match(code).hasMatch() ? code : QString();
}

QString numericId(const QVariant& value) {
  const auto text = value.type() == QVariant::String ? value.toString() : ParseNeteaseArtist({{"id", value}}).id;
  const auto match = QRegularExpression("^o?([0-9]{1,18})$").match(text);
  return match.hasMatch() && match.captured(1) != "0" ? match.captured(1) : QString();
}

bool imageBody(const QByteArray& body) {
  return body.startsWith(QByteArray::fromHex("89504e470d0a1a0a")) || body.startsWith("GIF87a") ||
      body.startsWith("GIF89a") || body.startsWith(QByteArray::fromHex("ffd8ff")) ||
      (body.startsWith("RIFF") && body.mid(8, 4) == "WEBP");
}

bool successfulCode(const QVariant& code) {
  if (!code.isValid() || code.type() == QVariant::Bool) return false;
  bool valid = false;
  const auto number = code.toLongLong(&valid);
  return valid && number == 0 && (code.type() != QVariant::Double || code.toDouble() == 0);
}

class QqAccountJob : public std::enable_shared_from_this<QqAccountJob> {
 public:
  NativePlatformHttp* http = nullptr;
  std::shared_ptr<NativeQqAuthState> state;
  QUrl authEndpoint, modernEndpoint, legacyEndpoint;
  QString operation, key, cookie, initialCookie, targetUin;
  QUrl initialRedirect;
  QElapsedTimer elapsed;
  int redirectCount = 0;
  bool consumed = false;
  std::function<void(QVariantMap)> done;

  bool current() const { return state->key == key; }

  void finish(QVariantMap result) {
    if (!current()) result = failure("cancelled");
    else {
      state->busy = false;
      if (!result.contains("error")) {
        state->cookie = cookie;
        result["cookie"] = cookie;
        result["ok"] = true;
        result["backend"] = "cpp-qq-account";
        qInfo("MCONNECT_NATIVE_QQ_ACCOUNT_OK operation=%s", qPrintable(operation));
      }
      const auto status = result.value("qrStatus").toString();
      if (operation == "qrCheck" && (consumed || status == "expired" || status == "failed")) {
        state->terminalCookie = initialCookie;
        state->terminalResult = result;
      }
    }
    done(result);
  }

  void fail(const QString& error) { finish(failure(error)); }

  using Reply = std::function<void(NativeHttpResult)>;
  using ReplyError = std::function<void(const QString&)>;

  void fetch(const QUrl& logicalUrl, const QByteArray& method, const QByteArray& body, Reply reply,
             const QByteArray& referer = "https://xui.ptlogin2.qq.com/", ReplyError onError = {}) {
    if (!current()) { fail("cancelled"); return; }
    if (!trustedUrl(logicalUrl)) { fail("redirect_rejected"); return; }
    const int remaining = requestBudget - int(elapsed.elapsed());
    if (remaining <= 0) {
      if (onError) onError("timeout");
      else fail("timeout");
      return;
    }
    QUrl endpoint = authEndpoint;
    if (logicalUrl.host() == "u.y.qq.com") endpoint = modernEndpoint;
    else if (logicalUrl.host() == "c.y.qq.com") endpoint = legacyEndpoint;
    const auto url = endpoint.isEmpty() ? logicalUrl : endpoint.resolved(QUrl(logicalUrl.path() + '?' + logicalUrl.query(QUrl::FullyEncoded)));
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    request.setRawHeader("Referer", referer);
    request.setRawHeader("Cookie", cookie.toLatin1());
    if (method == "POST") {
      const bool music = logicalUrl.host() == "u.y.qq.com";
      request.setHeader(QNetworkRequest::ContentTypeHeader, music ? "application/json" : "application/x-www-form-urlencoded");
      request.setRawHeader("Origin", music ? "https://y.qq.com" : "https://graph.qq.com");
    }
    const auto self = shared_from_this();
    http->request(request, method, body, [self, reply, onError](NativeHttpResult response) {
      if (!self->current()) { self->fail("cancelled"); return; }
      if (!response.error.isEmpty()) {
        if (onError) onError(response.error);
        else self->fail(response.error);
        return;
      }
      self->cookie = mergeCookies(self->cookie, response.headers);
      if (!validCookie(self->cookie)) { self->fail("response_too_large"); return; }
      reply(response);
    }, remaining, 1024 * 1024, false, false, false);
  }

  void qrKey() {
    const auto now = QString::number(QDateTime::currentMSecsSinceEpoch() / 1000);
    const auto url = NativePlatformUrl(QUrl("https://ssl.ptlogin2.qq.com/ptqrshow"),
      {{"appid", "716027609"}, {"e", "2"}, {"l", "M"}, {"s", "3"}, {"d", "72"}, {"v", "4"},
       {"t", now}, {"daid", "383"}, {"pt_3rd_aid", "100497308"}});
    const auto self = shared_from_this();
    fetch(url, "GET", {}, [self](NativeHttpResult response) {
      if (!imageBody(response.body) || cookiePairs(self->cookie).value("qrsig").isEmpty()) {
        self->fail("invalid_response"); return;
      }
      self->finish({{"qrKey", self->key}, {"qrImage", QString::fromLatin1(response.body.toBase64())}});
    });
  }

  void qrCheck() {
    const auto qrsig = cookiePairs(cookie).value("qrsig");
    if (qrsig.isEmpty()) { fail("invalid_arguments"); return; }
    const auto url = NativePlatformUrl(QUrl("https://ssl.ptlogin2.qq.com/ptqrlogin"),
      {{"u1", "https://graph.qq.com/oauth2.0/login_jump"}, {"ptqrtoken", QString::number(cookieHash(qrsig, 0))},
       {"ptredirect", "1"}, {"h", "1"}, {"t", "1"}, {"g", "1"}, {"from_ui", "1"}, {"ptlang", "2052"},
       {"action", "0-0-" + QString::number(QDateTime::currentMSecsSinceEpoch() / 1000)}, {"js_ver", "24060411"},
       {"js_type", "1"}, {"login_sig", ""}, {"pt_uistyle", "40"}, {"aid", "716027609"},
       {"daid", "383"}, {"pt_3rd_aid", "100497308"}});
    const auto self = shared_from_this();
    fetch(url, "GET", {}, [self](NativeHttpResult response) {
      const auto text = QString::fromUtf8(response.body);
      const QRegularExpression callback("ptui_?cb\\s*\\(\\s*'([0-9]+)'\\s*,\\s*'[^']*'\\s*,\\s*'([^']*)'",
                                       QRegularExpression::CaseInsensitiveOption);
      const auto match = callback.match(text);
      if (!match.hasMatch()) { self->fail("invalid_response"); return; }
      const auto code = match.captured(1);
      if (code != "0") {
        const QString status = code == "66" ? "waiting" : code == "67" ? "scanned" : code == "65" ? "expired" : "failed";
        self->finish({{"qrStatus", status}});
        return;
      }
      self->initialRedirect = QUrl(match.captured(2), QUrl::StrictMode);
      if (!trustedUrl(self->initialRedirect) || self->initialRedirect.path() != "/check_sig") {
        self->fail("redirect_rejected"); return;
      }
      self->targetUin = numericId(QUrlQuery(self->initialRedirect).queryItemValue("uin", QUrl::FullyDecoded));
      self->consumed = true;
      self->follow(self->initialRedirect);
    });
  }

  void follow(const QUrl& url) {
    if (++redirectCount > 10) { fail("redirect_limit"); return; }
    const auto self = shared_from_this();
    fetch(url, "GET", {}, [self, url](NativeHttpResult response) {
      const auto location = headerValue(response, "location");
      if (location.isEmpty()) { self->show(); return; }
      const auto next = url.resolved(QUrl(location, QUrl::StrictMode));
      if (!trustedUrl(next)) { self->fail("redirect_rejected"); return; }
      const auto code = callbackCode(next);
      if (!code.isEmpty()) { self->exchange(code); return; }
      self->follow(next);
    });
  }

  void show() {
    QList<QPair<QString, QString>> fields{{"which", "Login"}, {"display", "pc"}, {"response_type", "code"},
      {"client_id", "100497308"}, {"redirect_uri", "https://y.qq.com/portal/wx_redirect.html?login_type=1"},
      {"state", QString::number(QDateTime::currentMSecsSinceEpoch() / 1000)}, {"scope", "get_user_info"}};
    const auto surl = QUrlQuery(initialRedirect).queryItemValue("surl", QUrl::FullyDecoded);
    if (!surl.isEmpty()) fields.append({"surl", surl});
    const auto url = NativePlatformUrl(QUrl("https://graph.qq.com/oauth2.0/show"), fields);
    const auto self = shared_from_this();
    fetch(url, "GET", {}, [self, url](NativeHttpResult response) {
      const auto location = headerValue(response, "location");
      if (!location.isEmpty()) {
        const auto next = url.resolved(QUrl(location, QUrl::StrictMode));
        if (!trustedUrl(next)) { self->fail("redirect_rejected"); return; }
        const auto code = callbackCode(next);
        if (!code.isEmpty()) { self->exchange(code); return; }
      }
      self->authorize();
    });
  }

  void authorize() {
    const auto gtk = cookieHash(cookiePairs(cookie).value("p_skey"), 5381);
    const auto encoded = NativePlatformUrl(QUrl("https://graph.qq.com/oauth2.0/authorize"),
      {{"response_type", "code"}, {"client_id", "100497308"},
       {"redirect_uri", "https://y.qq.com/portal/wx_redirect.html?login_type=1"}, {"scope", "get_user_info"},
       {"state", QString::number(QDateTime::currentMSecsSinceEpoch() / 1000)}, {"g_tk", QString::number(gtk)},
       {"from_ptlogin", "1"}, {"src", "1"}, {"update_auth", "1"}, {"openapi", "1010"},
       {"q_login_code", ""}, {"q_state", ""}, {"from", "login"}}).query(QUrl::FullyEncoded).toLatin1();
    const QUrl url("https://graph.qq.com/oauth2.0/authorize");
    const auto self = shared_from_this();
    fetch(url, "POST", encoded, [self, url](NativeHttpResult response) {
      QString code;
      const auto location = headerValue(response, "location");
      if (!location.isEmpty()) {
        const auto next = url.resolved(QUrl(location, QUrl::StrictMode));
        if (!trustedUrl(next)) { self->fail("redirect_rejected"); return; }
        code = callbackCode(next);
      } else {
        auto body = QString::fromUtf8(response.body).trimmed();
        const QRegularExpression parameter("(?:^|[?&\\s\"'])code=([A-Za-z0-9._~+/%=-]{1,1536})(?=$|[&\\s\"'<>])");
        const auto match = parameter.match(body.replace("&amp;", "&"));
        if (match.hasMatch()) {
          code = QUrl::fromPercentEncoding(match.captured(1).toLatin1());
          if (!QRegularExpression("^[A-Za-z0-9._~+/=-]{1,512}$").match(code).hasMatch()) code.clear();
        }
      }
      if (code.isEmpty()) { self->fail("verification_required"); return; }
      self->exchange(code);
    }, "https://graph.qq.com/oauth2.0/show");
  }

  void exchange(const QString& code) {
    const auto gtk = cookieHash(cookiePairs(cookie).value("p_skey"), 5381);
    const QVariantMap body{{"comm", QVariantMap{{"g_tk", gtk}, {"platform", "yqq"}, {"ct", 24}, {"cv", 0}}},
      {"req", QVariantMap{{"module", "QQConnectLogin.LoginServer"}, {"method", "QQLogin"},
                          {"param", QVariantMap{{"code", code}}}}}};
    const auto self = shared_from_this();
    fetch(QUrl("https://u.y.qq.com/cgi-bin/musicu.fcg"), "POST", QJsonDocument::fromVariant(body).toJson(QJsonDocument::Compact),
      [self](NativeHttpResult response) {
        const auto root = ParseNativeQqResponse(response.body);
        if (root.contains("error")) { self->fail(root.value("error").toString()); return; }
        const auto module = root.value("req").toMap();
        if ((root.contains("code") && !successfulCode(root.value("code"))) || !successfulCode(module.value("code"))) {
          self->fail("provider_error"); return;
        }
        const auto data = module.value("data").toMap();
        auto cookies = cookiePairs(self->cookie);
        const auto first = [&data](const QStringList& fields) {
          for (const auto& field : fields) {
            const auto value = data.value(field);
            if (value.type() == QVariant::Bool || value.isNull()) continue;
            const auto text = value.type() == QVariant::String ? value.toString() : ParseNeteaseArtist({{"id", value}}).id;
            if (!text.isEmpty() && text != "0" && text != "null") return text;
          }
          return QString();
        };
        auto uin = numericId(first({"uin", "loginUin"}));
        const auto musicUin = numericId(first({"musicid", "music_id", "musicuin", "qqmusic_uin", "uin"}));
        const auto musicKey = first({"musickey", "music_key", "qqmusic_key", "qm_keyst"});
        if (!uin.isEmpty() && !self->targetUin.isEmpty() && uin != self->targetUin) {
          self->fail("identity_mismatch"); return;
        }
        if (uin.isEmpty()) uin = self->targetUin;
        if (uin.isEmpty()) uin = numericId(cookies.value("uin"));
        if (uin.isEmpty()) uin = numericId(cookies.value("loginUin"));
        if (uin.isEmpty()) uin = musicUin;
        for (const auto& field : QStringList{"uin", "loginUin"}) {
          const auto headerUin = numericId(cookies.value(field));
          if (!headerUin.isEmpty() && !uin.isEmpty() && headerUin != uin) {
            self->fail("identity_mismatch"); return;
          }
        }
        if (!uin.isEmpty()) { cookies["uin"] = 'o' + uin; cookies["loginUin"] = uin; }
        if (!musicUin.isEmpty()) cookies["qqmusic_uin"] = musicUin;
        if (!musicKey.isEmpty()) {
          cookies["qqmusic_key"] = musicKey;
          cookies["qm_keyst"] = musicKey;
          cookies["musickey"] = musicKey;
        }
        for (const auto& field : QStringList{"openid", "access_token", "refresh_key"}) {
          const auto value = first(field == "openid" ? QStringList{"openid", "openId"} :
                                   field == "access_token" ? QStringList{"access_token", "accessToken"} :
                                                            QStringList{"refresh_key", "refreshKey"});
          if (!value.isEmpty()) cookies[field] = value;
        }
        self->cookie = joinCookies(cookies);
        if (!validCookie(self->cookie)) { self->fail("response_too_large"); return; }
        self->targetUin = uin;
        self->profile();
      }, "https://y.qq.com/");
  }

  void profile() {
    const auto url = NativePlatformUrl(QUrl("https://c.y.qq.com/rsc/fcgi-bin/fcg_get_profile_homepage.fcg"),
      {{"cid", "205360838"}, {"userid", targetUin}, {"reqfrom", "1"}});
    const auto self = shared_from_this();
    fetch(url, "GET", {}, [self](NativeHttpResult response) {
      const auto root = ParseNativeQqResponse(response.body);
      if (root.contains("error")) { self->profileFailed(root.value("error").toString()); return; }
      if (root.isEmpty() || (root.contains("code") && !successfulCode(root.value("code")))) {
        self->profileFailed("provider_error"); return;
      }
      const auto user = NormalizeQqProfile(root, self->targetUin);
      if (user.isEmpty()) { self->profileFailed("invalid_response"); return; }
      self->finish({{"qrStatus", "success"}, {"user", user}});
    }, "https://y.qq.com/portal/profile.html", [self](const QString& error) { self->profileFailed(error); });
  }

  void profileFailed(const QString& error) {
    if (error == "login_required" || targetUin.isEmpty()) { fail(error); return; }
    qInfo("MCONNECT_NATIVE_QQ_PROFILE_FALLBACK");
    finish({{"qrStatus", "success"}, {"user", NormalizeQqProfile({}, targetUin)}});
  }
};
}

QString NativePlatforms::QqAccount(const QString& operation, const QString& arguments, const QString& cookie) {
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(arguments.toUtf8(), &error);
  if (!QStringList{"qrKey", "qrCheck", "qrCancel"}.contains(operation) || !validCookie(cookie) ||
      arguments.toUtf8().size() > 16384 || error.error != QJsonParseError::NoError || !document.isObject()) {
    return serialize(failure("invalid_arguments"));
  }
  const auto args = document.toVariant().toMap();
  const auto suppliedKey = args.value("key").toString();
  if ((operation == "qrKey" && !args.isEmpty()) ||
      (operation != "qrKey" && (args.size() != 1 || args.value("key").type() != QVariant::String ||
       !QRegularExpression("^[A-Za-z0-9_-]{1,64}$").match(suppliedKey).hasMatch()))) {
    return serialize(failure("invalid_arguments"));
  }
  if (!calledFromDBus()) return serialize(failure("dbus_required"));
  if (operation == "qrCancel") {
    if (qqAuthState_->key == suppliedKey) {
      qqAuthState_->key.clear();
      qqAuthState_->cookie.clear();
      qqAuthState_->terminalCookie.clear();
      qqAuthState_->terminalResult.clear();
      qqAuthState_->busy = false;
    }
    return serialize({{"backend", "cpp-qq-account"}, {"ok", true}});
  }
  if (operation == "qrKey") {
    qqAuthState_->key = QUuid::createUuid().toString().remove('{').remove('}');
    qqAuthState_->cookie.clear();
    qqAuthState_->terminalCookie.clear();
    qqAuthState_->terminalResult.clear();
    qqAuthState_->lifetime.start();
  } else {
    if (qqAuthState_->key != suppliedKey) return serialize(failure("cancelled"));
    if (!qqAuthState_->terminalResult.isEmpty()) {
      if (cookie != qqAuthState_->terminalCookie && cookie != qqAuthState_->cookie) return serialize(failure("cancelled"));
      return serialize(qqAuthState_->terminalResult);
    }
    if (cookie != qqAuthState_->cookie) return serialize(failure("cancelled"));
    if (!qqAuthState_->lifetime.isValid() || qqAuthState_->lifetime.elapsed() >= loginLifetime) {
      return serialize({{"backend", "cpp-qq-account"}, {"ok", true}, {"qrStatus", "expired"}, {"cookie", qqAuthState_->cookie}});
    }
    if (qqAuthState_->busy) return serialize(failure("busy"));
  }
  qqAuthState_->busy = true;
  setDelayedReply(true);
  const auto bus = connection();
  const auto call = message();
  auto job = std::make_shared<QqAccountJob>();
  job->http = &http_;
  job->state = qqAuthState_;
  job->key = qqAuthState_->key;
  job->cookie = operation == "qrKey" ? QString() : cookie;
  job->initialCookie = job->cookie;
  job->operation = operation;
  job->authEndpoint = qqAuthEndpoint_;
  job->modernEndpoint = qqEndpoint_;
  job->legacyEndpoint = qqLegacy_;
  job->done = [bus, call](const QVariantMap& result) { bus.send(call.createReply(QVariantList{serialize(result)})); };
  job->elapsed.start();
  if (operation == "qrKey") job->qrKey();
  else job->qrCheck();
  return {};
}
