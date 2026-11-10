#pragma once

#include "native_music.h"

QVariantMap NativeSongRow(const QVariantMap& song);
QVariantMap NativePlaylistRow(const QVariantMap& playlist);
QVariantMap NativeAlbumRow(const QVariantMap& album);
QVariantMap NativeStoredPlaybackSong(const QVariantMap& song);

class NativeLibrary {
 public:
  explicit NativeLibrary(NativeStorage& storage) : storage_(storage) {}
  QVariantList likes();
  QVariantList history();
  bool isLiked(const QVariantMap& song);
  bool toggleLike(const QVariantMap& song);
  void recordListen(const QVariantMap& song, qint64 durationMs = 0, qint64 at = -1);
  void saveSong(const QVariantMap& song);
 private:
  NativeStorage& storage_;
  QVariantList songs(const QVariantList& rows);
};

class NativeResults {
 public:
  explicit NativeResults(NativeMusicServices& music);
  QVariantMap store(const QVariantList& songs, const QString& platform = {}, const QString& recommendation = {});
  QVariantMap page(const QString& token, int offset);
  QVariantList songs(const QString& token);
  QVariantMap song(const QVariantMap& arguments, const QVariantMap& current);
 private:
  struct Entry { QVariantList songs; QString platform; quint64 epoch; };
  NativeMusicServices& music_;
  QHash<QString, Entry> entries_;
  QStringList order_;
  QMap<QString, QString> recommendations_;
  QString prefix_;
  quint64 sequence_ = 0;
  void purgeAccounts();
};

class NativeRecommendations {
 public:
  NativeRecommendations(NativeStorage& storage, NativeMusicServices& music) : storage_(storage), music_(music) {}
  QVariantMap get(const QString& platform, bool refresh);
 private:
  NativeStorage& storage_;
  NativeMusicServices& music_;
  QHash<QString, QVariantMap> memory_;
};
