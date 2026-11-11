#pragma once

#include "native_player.h"
#include "sailplayprovider.h"

class NativeCarContent : public QObject {
  Q_OBJECT
 public:
  explicit NativeCarContent(SailplayProvider& provider, QObject* parent = nullptr);
  void attach(NativePlayer& player);
  QVariantMap root() const;
  QVariantMap browse(const QVariantMap& parameters) const;
  bool select(const QVariantMap& parameters);
 private:
  SailplayProvider& provider_;
  NativePlayer* player_ = nullptr;
  qulonglong version_ = 1;
  QString songKey(const QVariantMap& song) const;
};
