#pragma once

#include "native_music.h"
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QQueue>

class NativeCoreService : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.sailfishos.Mconnect.Core")
 public:
  using Dispatch = std::function<QVariant(const QString&, const QVariantMap&)>;
  using Clock = std::function<qint64()>;
  explicit NativeCoreService(Dispatch dispatch, QObject* parent = nullptr, Clock clock = {});
  bool start();
  void close();
  bool hasPendingWork() const { return processing_ || executing_ > 0 || !pending_.isEmpty(); }
  void publish(const QString& topic, const QVariant& value);
  QString encode(const QVariant& value);
  QVariantMap transferPart(const QVariantMap& arguments);
 public slots:
  QString Request(const QString& method, const QString& arguments);
 signals:
  void Changed(const QString& topic, const QString& json);
 private:
  struct Transfer { QByteArray bytes; qint64 expires; };
  struct Pending { QDBusMessage call; QString method; QVariantMap arguments; };
  Dispatch dispatch_;
  Clock clock_;
  QHash<QString, Transfer> transfers_;
  QStringList transferOrder_;
  QQueue<Pending> pending_;
  bool processing_ = false;
  int executing_ = 0;
  bool started_ = false;
  void purgeTransfers();
  void process();
  QString execute(const QString& method, const QVariantMap& arguments);
};
