#include "native_login.h"
#include <QScopedValueRollback>

#include <QBuffer>
#include <QDateTime>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include "../third_party/qrcodegen/qrcodegen.hpp"

QString NativeQrImage(const QVariantMap& response) {
  QImage image;
  if (response.value("qrImage").type() == QVariant::String) {
    auto bytes = QByteArray::fromBase64(response.value("qrImage").toString().toLatin1());
    QBuffer input(&bytes);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    const auto size = reader.size();
    if (bytes.isEmpty() || bytes.size() > 1024 * 1024 || !size.isValid() || size.width() > 4096 || size.height() > 4096) throw NativeRequestFailure(QStringLiteral("登录二维码无效"));
    image = reader.read();
    if (image.isNull()) throw NativeRequestFailure(QStringLiteral("登录二维码无效"));
  } else {
    const auto url = response.value("qrUrl").toString().toUtf8();
    if (url.isEmpty() || url.size() > 4096) throw NativeRequestFailure(QStringLiteral("登录二维码无效"));
    try {
      const std::vector<std::uint8_t> data(url.begin(), url.end());
      const auto segment = qrcodegen::QrSegment::makeBytes(data);
      const auto code = qrcodegen::QrCode::encodeSegments(
          {segment}, qrcodegen::QrCode::Ecc::LOW, 1, 40, -1, false);
      image = QImage(code.getSize() + 8, code.getSize() + 8, QImage::Format_RGB32);
      image.fill(Qt::white);
      for (int row = 0; row < code.getSize(); ++row) {
        for (int column = 0; column < code.getSize(); ++column) {
          if (code.getModule(column, row)) image.setPixel(column + 4, row + 4, qRgb(0, 0, 0));
        }
      }
      image = image.scaled(512, 512, Qt::KeepAspectRatio, Qt::FastTransformation);
    } catch (const qrcodegen::data_too_long&) {
      throw NativeRequestFailure(QStringLiteral("无法生成登录二维码"));
    }
  }
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) throw NativeRequestFailure(QStringLiteral("无法显示登录二维码"));
  return "data:image/png;base64," + QString::fromLatin1(bytes.toBase64());
}

NativeLogin::NativeLogin(NativeMusicServices& music, QObject* parent, int lifetimeMs, int pollIntervalMs)
    : QObject(parent), music_(music), lifetimeMs_(lifetimeMs), pollIntervalMs_(pollIntervalMs) {
  if (lifetimeMs <= 0 || pollIntervalMs <= 0) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  poll_.setSingleShot(true);
  expiry_.setSingleShot(true);
  connect(&poll_, &QTimer::timeout, this, &NativeLogin::check);
  connect(&expiry_, &QTimer::timeout, this, [this]() { status("expired"); });
  connect(&music_, &NativeMusicServices::accountsChanged, this, [this]() {
    if (!saving_ && !platform_.isEmpty() && epoch_ != music_.epoch(platform_)) cancel();
  });
}

QVariantMap NativeLogin::begin(const QString& platform, const QString& clientSession, const QString& variant) {
  QScopedValueRollback<bool> running(running_, true);
  NativeServicePlatform(platform);
  NativeIdentifier(clientSession, 128);
  if (!variant.isEmpty() && (platform != "kugou" || !QStringList{"lite", "android"}.contains(variant))) {
    throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  }
  cancel();
  const auto generation = ++generation_;
  platform_ = platform;
  epoch_ = music_.epoch(platform);
  cookie_ = music_.session(platform);
  if (platform == "kugou" && !variant.isEmpty()) {
    auto fields = QJsonDocument::fromJson(cookie_.toUtf8()).toVariant().toMap();
    fields["client"] = variant;
    cookie_ = QString::fromUtf8(QJsonDocument::fromVariant(fields).toJson(QJsonDocument::Compact));
  }
  state_ = {{"session", clientSession}, {"platform", platform}, {"status", "loading"}, {"image", ""}};
  emit changed();
  try {
    const auto response = music_.loginRequest(platform, "qrKey", {}, cookie_);
    if (generation != generation_ || epoch_ != music_.epoch(platform)) return {{"cancelled", true}};
    key_ = NativeIdentifier(response.value("qrKey"), 256);
    const auto session = response.value(platform == "kugou" ? "session" : "cookie");
    if (session.type() == QVariant::String) cookie_ = session.toString();
    state_["image"] = NativeQrImage(response);
    state_["status"] = "waiting";
    expires_ = QDateTime::currentMSecsSinceEpoch() + lifetimeMs_;
    expiry_.start(lifetimeMs_);
    poll_.start(pollIntervalMs_);
    emit changed();
    return state_;
  } catch (...) {
    if (generation != generation_) return {{"cancelled", true}};
    status("failed");
    throw;
  }
}

void NativeLogin::status(const QString& value) {
  state_["status"] = value;
  if (QStringList{"success", "expired", "failed", "saving"}.contains(value)) { poll_.stop(); expiry_.stop(); }
  emit changed();
}

void NativeLogin::check() {
  QScopedValueRollback<bool> running(running_, true);
  const auto generation = generation_;
  const auto platform = platform_;
  if (state_.isEmpty()) return;
  if (QDateTime::currentMSecsSinceEpoch() >= expires_) { status("expired"); return; }
  try {
    const auto response = music_.loginRequest(platform, "qrCheck", {{"key", key_}}, cookie_);
    if (generation != generation_ || epoch_ != music_.epoch(platform)) return;
    if (QDateTime::currentMSecsSinceEpoch() >= expires_) { status("expired"); return; }
    const auto session = response.value(platform == "kugou" ? "session" : "cookie");
    if (session.type() == QVariant::String) cookie_ = session.toString();
    const auto value = response.value("qrStatus").toString();
    if (value == "success") {
      saving_ = true;
      status("saving");
      if (generation != generation_) { saving_ = false; return; }
      music_.acceptLogin(platform, cookie_, response.value("user").toMap());
      saving_ = false;
      if (generation == generation_) {
        if (music_.epoch(platform) != epoch_ + 1 || !music_.loggedIn(platform)) { cancel(); return; }
        epoch_ = music_.epoch(platform);
        status("success");
      }
      return;
    }
    if (!QStringList{"waiting", "scanned", "expired", "failed"}.contains(value)) { status("failed"); return; }
    status(value);
    if (value == "waiting" || value == "scanned") poll_.start(pollIntervalMs_);
  } catch (...) {
    saving_ = false;
    if (generation == generation_) status("failed");
  }
}

void NativeLogin::cancel(const QVariant& clientSession) {
  if (!clientSession.isNull() && clientSession.type() != QVariant::String) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  if (!clientSession.isNull() && state_.value("session") != clientSession) return;
  ++generation_;
  poll_.stop();
  expiry_.stop();
  const auto platform = platform_, key = key_, cookie = cookie_;
  platform_.clear(); key_.clear(); cookie_.clear(); state_.clear();
  emit changed();
  auto* music = &music_;
  if (!platform.isEmpty() && !key.isEmpty()) {
    ++pendingCancels_;
    QTimer::singleShot(0, this, [this, music, platform, key, cookie]() {
      try { music->loginRequest(platform, "qrCancel", {{"key", key}}, cookie); } catch (...) {}
      --pendingCancels_;
    });
  }
}
