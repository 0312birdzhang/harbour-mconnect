#include "native_core.h"

#include <QDateTime>
#include <QDBusError>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopedValueRollback>
#include <QTimer>
#include <QUuid>

namespace {
constexpr int inlineBytes = 48 * 1024;
constexpr int partBytes = 24 * 1024;
constexpr int maximumBytes = 8 * 1024 * 1024;

QByteArray encodeJson(const QVariant& value) {
  // QJsonDocument has no scalar-root representation on Qt 5.
  const auto document = QJsonDocument::fromVariant(value);
  if (!document.isNull()) return document.toJson(QJsonDocument::Compact);
  const auto bytes = QJsonDocument(QJsonArray{QJsonValue::fromVariant(value)}).toJson(QJsonDocument::Compact);
  return bytes.mid(1, bytes.size() - 2);
}
}

NativeCoreService::NativeCoreService(Dispatch dispatch, QObject* parent, Clock clock)
    : QObject(parent), dispatch_(std::move(dispatch)), clock_(clock ? std::move(clock) : Clock(QDateTime::currentMSecsSinceEpoch)) {}

bool NativeCoreService::start() {
  if (started_) return true;
  auto bus = QDBusConnection::sessionBus();
  if (!bus.registerService("org.sailfishos.Mconnect.Core")) return false;
  if (!bus.registerObject("/org/sailfishos/Mconnect/Core", this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
    bus.unregisterService("org.sailfishos.Mconnect.Core");
    return false;
  }
  started_ = true;
  return true;
}

void NativeCoreService::close() {
  if (started_) {
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterObject("/org/sailfishos/Mconnect/Core");
    bus.unregisterService("org.sailfishos.Mconnect.Core");
    started_ = false;
  }
  while (!pending_.isEmpty()) QDBusConnection::sessionBus().send(pending_.dequeue().call.createErrorReply(QDBusError::Failed, QStringLiteral("请求已取消")));
  transfers_.clear();
  transferOrder_.clear();
}

void NativeCoreService::purgeTransfers() {
  for (const auto& key : QStringList(transferOrder_)) {
    if (transfers_.value(key).expires <= clock_()) {
      transfers_.remove(key);
      transferOrder_.removeAll(key);
    }
  }
}

QString NativeCoreService::encode(const QVariant& value) {
  purgeTransfers();
  const auto bytes = encodeJson(value);
  if (bytes.size() <= inlineBytes) return QString::fromUtf8(bytes);
  if (bytes.size() > maximumBytes) throw NativeRequestFailure(QStringLiteral("请求结果过大"), true);
  const auto random = QUuid::createUuid().toRfc4122() + QUuid::createUuid().toRfc4122().left(4);
  const auto token = QString::fromLatin1(random.toBase64(QByteArray::Base64UrlEncoding));
  transfers_[token] = {bytes, clock_() + 60000};
  transferOrder_.append(token);
  int total = 0;
  for (const auto& transfer : transfers_) total += transfer.bytes.size();
  while (transferOrder_.size() > 4 || total > 2 * maximumBytes) {
    const auto oldest = transferOrder_.takeFirst();
    total -= transfers_.take(oldest).bytes.size();
  }
  return QString::fromUtf8(encodeJson(QVariantMap{{"_mconnectTransfer", token}, {"parts", (bytes.size() + partBytes - 1) / partBytes}}));
}

QVariantMap NativeCoreService::transferPart(const QVariantMap& arguments) {
  purgeTransfers();
  const auto token = NativeIdentifier(arguments.value("token"));
  if (!transfers_.contains(token)) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  const int index = NativeIntegerArgument(arguments.value("index"), 0, (transfers_.value(token).bytes.size() - 1) / partBytes);
  const auto bytes = transfers_.value(token).bytes.mid(index * partBytes, partBytes);
  if ((index + 1) * partBytes >= transfers_.value(token).bytes.size()) {
    transfers_.remove(token);
    transferOrder_.removeAll(token);
  }
  return {{"data", QString::fromLatin1(bytes.toBase64())}};
}

void NativeCoreService::publish(const QString& topic, const QVariant& value) {
  const auto bytes = encodeJson(value);
  emit Changed(topic, bytes.size() > inlineBytes ? QString("{\"_mconnectRefresh\":true}") : QString::fromUtf8(bytes));
}

QString NativeCoreService::execute(const QString& method, const QVariantMap& arguments) {
  QScopedValueRollback<int> executing(executing_, executing_ + 1);
  return method == "_readTransfer" ? encode(transferPart(arguments)) : encode(dispatch_(method, arguments));
}

QString NativeCoreService::Request(const QString& method, const QString& arguments) {
  const auto invalid = [&]() -> QString {
    if (calledFromDBus()) { sendErrorReply(QDBusError::InvalidArgs); return {}; }
    throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  };
  if (arguments.toUtf8().size() > (method == "importPlaylist" ? 262144 : 65536)) return invalid();
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(arguments.toUtf8(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return invalid();
  const auto args = document.toVariant().toMap();
  if (!calledFromDBus()) return execute(method, args);
  // State reads and cancellation remain available while a provider is awaiting
  // its reply. Writes with read/modify/write state run in arrival order.
  if (QStringList{"_readTransfer", "snapshot", "queue", "lyrics", "artwork", "accounts", "loginStatus", "downloads", "downloadDirectory", "localSnapshot", "cacheSettings", "loginCancel", "logout",
      "toggle", "next", "previous", "seek", "playIndex", "playResult", "playDownload", "toggleShuffle", "cycleRepeat"}.contains(method)) {
    try { return execute(method, args); }
    catch (const NativeRequestFailure& failure) {
      sendErrorReply(failure.invalidArguments ? QDBusError::InvalidArgs : QDBusError::Failed, failure.message);
    } catch (...) { sendErrorReply(QDBusError::Failed, QStringLiteral("请求失败，请稍后重试")); }
    return {};
  }
  if (pending_.size() >= 32) { sendErrorReply(QDBusError::Failed, QStringLiteral("请求较多，请稍后重试")); return {}; }
  setDelayedReply(true);
  pending_.enqueue({message(), method, args});
  if (!processing_) QTimer::singleShot(0, this, [this]() { process(); });
  return {};
}

void NativeCoreService::process() {
  if (processing_ || pending_.isEmpty()) return;
  processing_ = true;
  const auto pending = pending_.dequeue();
  QDBusMessage reply;
  try { reply = pending.call.createReply(QVariantList{execute(pending.method, pending.arguments)}); }
  catch (const NativeRequestFailure& error) { reply = pending.call.createErrorReply(error.invalidArguments ? QDBusError::InvalidArgs : QDBusError::Failed, error.message); }
  catch (...) { reply = pending.call.createErrorReply(QDBusError::Failed, QStringLiteral("请求失败，请稍后重试")); }
  QDBusConnection::sessionBus().send(reply);
  processing_ = false;
  if (!pending_.isEmpty()) QTimer::singleShot(0, this, [this]() { process(); });
}
