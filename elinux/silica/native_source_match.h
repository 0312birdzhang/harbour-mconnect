#pragma once

#include "native_music.h"

QString NativeMatchText(const QString& text);
double NativeSourceMatchScore(const QVariantMap& song, const QVariantMap& candidate);
bool NativeStreamUrl(const QString& url);

class NativeSourceMatch : public QObject {
  Q_OBJECT
 public:
  NativeSourceMatch(NativeStorage& storage, NativeMusicServices& music, QObject* parent = nullptr);
  QVariantMap resolve(const QVariantMap& song, int quality, const std::function<bool()>& valid);
  void invalidate(const QVariantMap& song, const QString& platform);
 private:
  NativeStorage& storage_;
  NativeMusicServices& music_;
  QMap<QString, quint64> epochs_;
};
