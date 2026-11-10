#include "native_storage.h"

#include <QJsonDocument>
#include <QRegularExpression>

namespace {
QVariantMap object(const QString& text) {
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(text.toUtf8(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    throw NativeStorageError("Invalid stored session");
  }
  return document.toVariant().toMap();
}

QMap<QString, QString> cookies(const QString& cookie) {
  QMap<QString, QString> result;
  for (const auto& part : cookie.split(';')) {
    const auto field = part.trimmed();
    const int split = field.indexOf('=');
    if (split > 0) result[field.left(split)] = field.mid(split + 1);
  }
  return result;
}

void validateCookie(const QString& cookie) {
  if (cookie.size() > 32768) throw NativeStorageError("Stored session is too large");
  for (const auto character : cookie) {
    if (character.unicode() < 32 || character.unicode() > 126) {
      throw NativeStorageError("Invalid stored session encoding");
    }
  }
}

QString numericId(QString value) {
  if (value.startsWith('o')) value.remove(0, 1);
  return QRegularExpression("^[0-9]{1,32}$").match(value).hasMatch() && value != "0" ? value : QString();
}
}

QString NativeSessionCookie(const QString& platform, const QString& stored, const QVariantMap& user) {
  if (stored.isEmpty()) return {};
  if (stored.toUtf8().size() > 32768) throw NativeStorageError("Stored session is too large");
  if (platform == "kugou") {
    QVariantMap result;
    if (stored.trimmed().startsWith('{')) result = object(stored);
    else {
      const auto parts = stored.split('|');
      if (parts.size() > 2) throw NativeStorageError("Invalid legacy session");
      result["token"] = parts.first();
      if (parts.size() == 2) result["userid"] = parts.last();
    }
    // The historical token-only format obtained its user ID from user_kugou.
    if (result.value("userid").toString().isEmpty() && !user.value("id").toString().isEmpty()) {
      result["userid"] = user.value("id");
    }
    const QStringList fields = {"token", "userid", "vip_token", "vip_type", "mid", "dfid", "uuid", "client"};
    for (auto item = result.begin(); item != result.end();) {
      if (item.value().isNull()) { item = result.erase(item); continue; }
      if (!fields.contains(item.key()) ||
          (item.value().type() != QVariant::String && item.value().type() != QVariant::LongLong &&
           item.value().type() != QVariant::Int && item.value().type() != QVariant::Double)) {
        throw NativeStorageError("Invalid session field");
      }
      if (item.value().type() != QVariant::String &&
          (item.value().toDouble() < 0 || item.value().toDouble() != item.value().toLongLong())) {
        throw NativeStorageError("Invalid numeric session field");
      }
      item.value() = item.value().toString();
      if (item.key() != "client") validateCookie(item.value().toString());
      ++item;
    }
    const auto client = result.value("client").toString().toLower();
    result["client"] = QStringList{"lite", "concept", "kugou_concept", "kugou_lite", QStringLiteral("概念版"),
                                  QStringLiteral("酷狗概念版")}.contains(client) ? "lite" : "android";
    return QString::fromUtf8(QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact));
  }
  QString cookie = stored;
  if (platform == "bilibili" && stored.trimmed().startsWith('{')) {
    const auto fields = object(stored);
    QStringList parts;
    for (auto item = fields.cbegin(); item != fields.cend(); ++item) {
      if (item.value().type() != QVariant::String ||
          !QRegularExpression("^[A-Za-z0-9_-]+$").match(item.key()).hasMatch() ||
          item.value().toString().contains(';')) throw NativeStorageError("Invalid stored cookie");
      parts.append(item.key() + '=' + item.value().toString());
    }
    cookie = parts.join("; ");
  }
  validateCookie(cookie);
  return cookie;
}

QString NativePersistedCookie(const QString& platform, const QString& cookie) {
  if (platform != "bilibili") return cookie;
  QVariantMap fields;
  const auto pairs = cookies(cookie);
  for (auto item = pairs.cbegin(); item != pairs.cend(); ++item) fields[item.key()] = item.value();
  return QString::fromUtf8(QJsonDocument::fromVariant(fields).toJson(QJsonDocument::Compact));
}

QVariantMap NativeSessionUser(const QString& platform, const QString& cookie, const QVariantMap& user) {
  const auto id = user.value("id");
  if (cookie.isEmpty() || id.type() != QVariant::String || numericId(id.toString()).isEmpty() ||
      user.value("platform") != platform ||
      (!user.value("nickname").isNull() && user.value("nickname").type() != QVariant::String) ||
      (!user.value("avatarUrl").isNull() && user.value("avatarUrl").type() != QVariant::String)) return {};
  QString sessionId;
  if (platform == "kugou") {
    const auto session = object(cookie);
    if (session.value("token").toString().isEmpty()) return {};
    sessionId = session.value("userid").toString();
  } else {
    const auto pairs = cookies(cookie);
    if (platform == "qq") {
      for (const auto& key : QStringList{"uin", "qqmusic_uin", "loginUin", "musicid", "web_uin", "wxuin"}) {
        sessionId = numericId(pairs.value(key));
        if (!sessionId.isEmpty()) break;
      }
    } else if (platform == "bilibili") {
      if (pairs.value("SESSDATA").isEmpty()) return {};
      sessionId = pairs.value("DedeUserID");
    }
  }
  if (!sessionId.isEmpty() && sessionId != id.toString()) return {};
  auto result = user;
  result["nickname"] = user.value("nickname").isNull() ? QString() : user.value("nickname");
  const auto vip = user.value("vipLevel", 0);
  bool valid = false;
  const int level = vip.toInt(&valid);
  if (!valid || vip.type() == QVariant::Bool || vip.type() == QVariant::String || vip.toDouble() != level || level < 0 || level > 2) return {};
  result["vipLevel"] = level;
  return result;
}
