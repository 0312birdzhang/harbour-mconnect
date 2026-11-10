#pragma once

#include "native_library.h"
#include <QThread>

QVariantMap NativeLocalTrackSong(const QVariantMap& record);
QVariantMap ReadNativeLocalMetadata(const QString& path, const QString& coverDirectory);

class NativeLocalMusic : public QObject {
  Q_OBJECT
 public:
  NativeLocalMusic(NativeStorage& storage, QObject* parent = nullptr);
  ~NativeLocalMusic() override;
  QVariantMap snapshot() const;
  bool hasOngoingWork() const { return worker_ != nullptr; }
  void scan(const QString& directory);
  QVariantList groups(const QVariantMap& arguments) const;
  QVariantList songs(const QVariantMap& arguments) const;
  QString cover(const QString& song) const;
  void close();
 signals:
  void changed();
 private:
  NativeStorage& storage_;
  QVariantList tracks_;
  QString directory_, error_;
  QVariantMap counts_;
  QThread* worker_ = nullptr;
  QVariantList groupRecords(const QString& view) const;
};
