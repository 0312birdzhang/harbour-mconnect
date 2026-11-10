#pragma once

#include "native_music.h"
#include <QNetworkAccessManager>

QString NativeSongDedupeKey(const QVariantMap& song);
QString NativeClosestLrclibLyrics(const QVariant& response, qint64 durationMs);
bool NativeShouldAskLrclib(const QVariantMap& document, qint64 durationMs);

class NativeLyricLoader : public QObject {
  Q_OBJECT
 public:
  using LrclibFetch = std::function<QString(const QVariantMap&)>;
  NativeLyricLoader(NativeStorage& storage, NativeMusicServices& music, QObject* parent = nullptr, LrclibFetch lrclib = {});
  QVariantMap snapshot() const;
  bool hasOngoingWork() const { return !closed_ && (loading_ || running_ || scheduled_); }
  QVariantMap document() const { return document_; }
  void load(const QVariantMap& song, bool preferCached = true);
  void close();
 signals:
  void changed();
 private:
  NativeStorage& storage_;
  NativeMusicServices& music_;
  QNetworkAccessManager network_;
  LrclibFetch lrclib_;
  QVariantMap song_, document_;
  quint64 generation_ = 0;
  bool loading_ = false, error_ = false, running_ = false, scheduled_ = false, closed_ = false, preferCached_ = true;
  void schedule();
  void run();
  QVariantMap resolve(const QVariantMap& song, quint64 generation, bool preferCached);
  QString fetchLrclib(const QVariantMap& song);
  void cache(const QVariantMap& song, const QVariantMap& document);
};
