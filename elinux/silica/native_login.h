#pragma once

#include "native_music.h"
#include <QTimer>

QString NativeQrImage(const QVariantMap& response);

class NativeLogin : public QObject {
  Q_OBJECT
 public:
  NativeLogin(NativeMusicServices& music, QObject* parent = nullptr, int lifetimeMs = 180000, int pollIntervalMs = 2000);
  QVariantMap begin(const QString& platform, const QString& clientSession, const QString& variant = {});
  QVariantMap state() const { return state_; }
  bool hasOngoingWork() const { return running_ || saving_ || pendingCancels_ > 0 || poll_.isActive() || expiry_.isActive(); }
  void cancel(const QVariant& clientSession = {});
 signals:
  void changed();
 private:
  NativeMusicServices& music_;
  QVariantMap state_;
  QString platform_, key_, cookie_;
  quint64 generation_ = 0, epoch_ = 0;
  qint64 expires_ = 0;
  int lifetimeMs_, pollIntervalMs_;
  bool saving_ = false;
  bool running_ = false;
  int pendingCancels_ = 0;
  QTimer poll_, expiry_;
  void check();
  void status(const QString& value);
};
