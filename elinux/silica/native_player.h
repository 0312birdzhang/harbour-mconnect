#pragma once

#include "native_downloads.h"
#include "native_source_match.h"
#include "../runner/sailfish_audio.h"
#include <QElapsedTimer>
#include <QSet>
#include <QTimer>
#include <random>

class NativePlayer : public QObject {
  Q_OBJECT
 public:
  NativePlayer(NativeStorage& storage, NativeMusicServices& music, NativeDownloads& downloads, NativeLibrary& library, QObject* parent = nullptr);
  ~NativePlayer() override;
  QVariantMap snapshot() const;
  bool hasOngoingWork() const { return !closed_ && (playing_ || busy_ || pending_ || running_ || scheduled_); }
  QVariantMap currentSong() const { return song_; }
  QVariantList playlist() const { return playlist_; }
  QString sourcePlatform() const { return source_; }
  double playbackSpeed() const { return speed_; }
  void playSong(const QVariantMap& song);
  void playPlaylist(const QVariantList& songs, int startIndex);
  void playIndex(int index);
  void play();
  void pause();
  void toggle();
  void next();
  void previous();
  void seek(qint64 positionMs);
  void toggleShuffle();
  void cycleRepeat();
  void close();
 signals:
  void changed();
  void queueChanged();
  void songChanged();
  void seeked(qint64 positionUs);
 private:
  NativeStorage& storage_;
  NativeMusicServices& music_;
  NativeDownloads& downloads_;
  NativeLibrary& library_;
  NativeSourceMatch matching_;
  Audio* audio_ = nullptr;
  QVariantMap song_, effects_;
  QVariantList playlist_;
  QString repeat_ = "off", source_, loadedKey_, error_, pendingNotice_;
  int index_ = -1, quality_ = 0, failureSkips_ = 0, recoveryAttempts_ = 0, lastStatus_ = -1;
  qint64 position_ = 0, duration_ = 0, abStart_ = -1, abEnd_ = -1, sleepRemaining_ = 0;
  qint64 lastAudioPosition_ = 0, lastProgressAt_ = 0;
  double speed_ = 1;
  bool shuffle_ = false, highest_ = false, skipSilence_ = false, playing_ = false, busy_ = false;
  bool pending_ = false, running_ = false, scheduled_ = false, closed_ = false, prefetchPending_ = false;
  quint64 generation_ = 0;
  QSet<QString> shuffledPlayed_;
  QList<int> shuffleHistory_;
  std::mt19937 random_{std::random_device{}()};
  QTimer poll_, save_, fade_, sleep_;
  QElapsedTimer clock_, fadeClock_;
  quint64 fadeGeneration_ = 0;
  int fadeDuration_ = 0;
  double fadeFrom_ = 0;
  void requestPlay(const QVariantMap& song, bool resume, bool resetFailures, int queueIndex = -1);
  void schedule();
  void run();
  bool open(const QString& url, quint64 generation);
  bool fadeOut(int duration, quint64 generation);
  void fadeIn();
  void cancelFade();
  void tick();
  void persist();
  void persistSleep();
  void changedState();
  void failed(const QString& code, quint64 generation);
  int shuffledIndex();
  void prefetch(quint64 generation);
};
