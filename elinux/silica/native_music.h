#pragma once

#include "native_storage.h"
#include <QObject>

class NativeRequestFailure : public std::runtime_error {
 public:
  explicit NativeRequestFailure(const QString& message, bool invalidArguments = false, const QString& code = {})
      : std::runtime_error(message.toUtf8().constData()), message(message), invalidArguments(invalidArguments), code(code) {}
  const QString message;
  const bool invalidArguments;
  const QString code;
};

QString NativeIdentifier(const QVariant& value, int maximum = 512);
QString NativeServicePlatform(const QVariant& value);
int NativeIntegerArgument(const QVariant& value, int minimum, int maximum);
int NativeQualityIndex(const QVariant& value);
QString NativeQualityName(int index);
bool NativeQualityLossless(int index);
QString NativeQualityLabel(int index, const QString& platform);
QVariantList NativeMusicSongs(const QVariantMap& response, const QString& platform);

// Reuses the provider services' existing C++ D-Bus entry points. Session and
// account ownership move here independently of the provider wire decoders.
class NativeMusicServices : public QObject {
  Q_OBJECT
 public:
  using ProviderCall = std::function<QVariantMap(const QString&, const QVariantList&)>;
  explicit NativeMusicServices(NativeStorage& storage, QObject* parent = nullptr, ProviderCall call = {});
  QVariantList platforms() const;
  QVariantList discoveryPlatforms() const;
  QVariantList accounts() const;
  bool loggedIn(const QString& platform) const;
  QString accountScope(const QString& platform) const;
  QString session(const QString& platform) const;
  quint64 epoch(const QString& platform) const;
  QVariantMap request(const QString& platform, const QString& operation, const QVariantMap& arguments = {}, int timeoutMs = 20000);
  QVariantMap loginRequest(const QString& platform, const QString& operation,
                          const QVariantMap& arguments, const QString& session);
  void acceptLogin(const QString& platform, const QString& session, const QVariantMap& user);
  void logout(const QString& platform);
  QVariantMap daily(const QString& platform);
  QVariantMap share(const QString& platform, const QString& url);
  int vipLevel(const QString& platform);
 signals:
  void accountsChanged();
 private:
  struct Account { QString session; QVariantMap user; quint64 epoch = 0; };
  NativeStorage& storage_;
  QMap<QString, Account> accounts_;
  ProviderCall call_;
  QVariantMap provider(const QString& platform, const QString& operation,
                       QVariantMap arguments, const QString& session, int timeoutMs);
  void persist(const QString& platform);
};
