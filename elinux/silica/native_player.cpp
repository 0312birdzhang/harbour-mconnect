#include "native_player.h"

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr qint64 maximumMilliseconds = std::numeric_limits<qint64>::max() / 1000000;

qint64 milliseconds(const QVariant& value) {
  return qBound(qint64(0), value.toLongLong(), maximumMilliseconds);
}

QString localUrl(const QString& id) {
  const QUrl url(id, QUrl::StrictMode);
  if (url.scheme() == "file" || url.scheme() == "content") return id;
  if (!QDir::isAbsolutePath(id)) throw NativeRequestFailure(QStringLiteral("本地文件路径无效"));
  return QUrl::fromLocalFile(id).toString(QUrl::FullyEncoded);
}

void waitStep(int duration) {
  QEventLoop loop;
  QTimer::singleShot(duration, &loop, &QEventLoop::quit);
  loop.exec();
}
}

NativePlayer::NativePlayer(NativeStorage& storage, NativeMusicServices& music, NativeDownloads& downloads, NativeLibrary& library, QObject* parent)
    : QObject(parent), storage_(storage), music_(music), downloads_(downloads), library_(library), matching_(storage, music) {
  clock_.start();
  connect(&music_, &NativeMusicServices::accountsChanged, this, [this]() {
    if (busy_) pause();
    else if (running_) { ++generation_; prefetchPending_ = false; }
  });
  try {
    const auto memory = storage_.playbackMemory();
    song_ = memory.value("currentSong").toMap();
    playlist_ = memory.value("playlist").toList();
    if (!song_.isEmpty()) {
      index_ = memory.value("currentIndex").toInt();
      position_ = milliseconds(memory.value("positionMs"));
      duration_ = milliseconds(memory.value("durationMs"));
      if (duration_ > 0) position_ = qMin(position_, duration_);
      quality_ = NativeQualityIndex(memory.value("currentQuality"));
      highest_ = memory.value("qualityPreference") == "highest";
      speed_ = memory.value("playbackSpeed", 1.0).toDouble();
      shuffle_ = memory.value("isShuffle").toBool();
      skipSilence_ = memory.value("skipSilence").toBool();
      const auto repeat = memory.value("repeatMode").toString();
      if (QStringList{"off", "all", "one"}.contains(repeat)) repeat_ = repeat;
      if (!memory.value("abLoopStartMs").isNull() && !memory.value("abLoopEndMs").isNull()) {
        abStart_ = milliseconds(memory.value("abLoopStartMs"));
        abEnd_ = milliseconds(memory.value("abLoopEndMs"));
      }
    }
    effects_ = storage_.box("settings").get("audio_effects_settings").toMap();
  } catch (const std::runtime_error&) { qWarning("MCONNECT_NATIVE_PLAYBACK_RESTORE_FAILED"); }
  poll_.setInterval(200);
  connect(&poll_, &QTimer::timeout, this, &NativePlayer::tick);
  poll_.start();
  save_.setSingleShot(true);
  save_.setInterval(5000);
  connect(&save_, &QTimer::timeout, this, &NativePlayer::persist);
  connect(&fade_, &QTimer::timeout, this, [this]() {
    if (!audio_ || closed_ || fadeGeneration_ != generation_ || !playing_) { cancelFade(); return; }
    const auto fraction = qBound(0.0, double(fadeClock_.elapsed()) / qMax(1, fadeDuration_), 1.0);
    mc_audio_volume(audio_, fadeFrom_ + (1 - fadeFrom_) * fraction);
    if (fraction >= 1) fade_.stop();
  });
  if (effects_.value("sleepTimerEnabled").toBool()) {
    sleepRemaining_ = qBound(qint64(0), effects_.value("sleepTimerRemainingSeconds").toLongLong(), qint64(7200));
    if (sleepRemaining_ == 0) sleepRemaining_ = qBound(qint64(5), effects_.value("sleepTimerDurationMinutes", 30).toLongLong(), qint64(120)) * 60;
    connect(&sleep_, &QTimer::timeout, this, [this]() {
      if (--sleepRemaining_ <= 0) {
        sleepRemaining_ = 0;
        sleep_.stop();
        persistSleep();
        if (playing_) {
          const auto generation = ++generation_;
          pending_ = false;
          busy_ = true;
          if (!fadeOut(1500, generation)) return;
          busy_ = false;
          if (audio_) mc_audio_state(audio_, 1);
          playing_ = false;
          cancelFade();
          changedState();
        }
      } else if (sleepRemaining_ % 10 == 0) persistSleep();
    });
    sleep_.start(1000);
  }
}

NativePlayer::~NativePlayer() { close(); }

QVariantMap NativePlayer::snapshot() const {
  return {{"version", 1}, {"song", song_.isEmpty() ? QVariant() : QVariant(NativeSongRow(song_))},
      {"playing", playing_}, {"busy", busy_}, {"position", position_}, {"duration", duration_}, {"index", index_},
      {"shuffle", shuffle_}, {"repeat", repeat_}, {"error", error_.isEmpty() ? QString() : QStringLiteral("播放失败，请检查网络或切换歌曲")}};
}

void NativePlayer::changedState() {
  emit changed();
  if (!song_.isEmpty() && !save_.isActive()) save_.start();
}

void NativePlayer::persist() {
  if (song_.isEmpty()) return;
  QVariantList stored;
  for (const auto& song : playlist_) stored.append(NativeStoredPlaybackSong(song.toMap()));
  try {
    storage_.savePlaybackMemory({{"currentSong", NativeStoredPlaybackSong(song_)}, {"playlist", stored}, {"currentIndex", index_},
        {"positionMs", position_}, {"durationMs", duration_}, {"currentQuality", NativeQualityName(quality_)},
        {"qualityPreference", highest_ ? "highest" : "fixed"}, {"playbackSpeed", speed_}, {"skipSilence", skipSilence_},
        {"isShuffle", shuffle_}, {"repeatMode", repeat_}, {"abLoopStartMs", abStart_ < 0 ? QVariant() : QVariant(abStart_)},
        {"abLoopEndMs", abEnd_ < 0 ? QVariant() : QVariant(abEnd_)}, {"savedAt", QDateTime::currentDateTime().toString("yyyy-MM-dd'T'HH:mm:ss.zzz")}});
  } catch (const NativeStorageError&) { qWarning("MCONNECT_NATIVE_PLAYBACK_STORE_FAILED"); }
}

void NativePlayer::persistSleep() {
  effects_["sleepTimerEnabled"] = sleepRemaining_ > 0;
  effects_["sleepTimerRemainingSeconds"] = sleepRemaining_;
  try { storage_.box("settings").put("audio_effects_settings", effects_); }
  catch (const NativeStorageError&) { qWarning("MCONNECT_NATIVE_SLEEP_STORE_FAILED"); }
}

void NativePlayer::requestPlay(const QVariantMap& raw, bool resume, bool resetFailures, int queueIndex) {
  if (closed_) throw NativeRequestFailure(QStringLiteral("播放服务已关闭"));
  const auto song = NativePlaybackSong(raw);
  if (song.isEmpty()) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  ++generation_;
  cancelFade();
  if (resetFailures) { failureSkips_ = 0; recoveryAttempts_ = 0; pendingNotice_.clear(); }
  if (!resume) { position_ = 0; abStart_ = -1; abEnd_ = -1; }
  const auto key = NativeSongKey(song);
  bool found = queueIndex >= 0 && queueIndex < playlist_.size() && NativeSongKey(playlist_[queueIndex].toMap()) == key;
  if (found) {
    index_ = queueIndex;
  } else {
    for (int index = 0; index < playlist_.size(); ++index) {
      if (NativeSongKey(playlist_[index].toMap()) == key) { index_ = index; found = true; break; }
    }
  }
  if (!found) {
    playlist_.append(song);
    index_ = playlist_.size() - 1;
    emit queueChanged();
  }
  const bool changedSong = song_.isEmpty() || NativeSongKey(song_) != NativeSongKey(song);
  song_ = song;
  duration_ = resume && duration_ > 0 ? duration_ : milliseconds(song.value("durationMs"));
  source_.clear();
  error_.clear();
  busy_ = true;
  pending_ = true;
  prefetchPending_ = true;
  if (changedSong) emit songChanged();
  changedState();
  schedule();
}

void NativePlayer::playSong(const QVariantMap& song) { requestPlay(song, false, true); }

void NativePlayer::playPlaylist(const QVariantList& songs, int startIndex) {
  if (songs.isEmpty() || startIndex < 0 || startIndex >= songs.size()) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  QVariantList normalized;
  for (const auto& raw : songs) {
    const auto song = NativePlaybackSong(raw.toMap());
    if (song.isEmpty()) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
    normalized.append(song);
  }
  playlist_ = normalized;
  index_ = startIndex;
  shuffledPlayed_.clear();
  shuffleHistory_.clear();
  emit queueChanged();
  requestPlay(playlist_[startIndex].toMap(), false, true, startIndex);
}

void NativePlayer::playIndex(int index) {
  if (index < 0 || index >= playlist_.size()) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  requestPlay(playlist_[index].toMap(), false, true, index);
}

void NativePlayer::schedule() {
  if (running_ || scheduled_ || closed_) return;
  scheduled_ = true;
  QTimer::singleShot(0, this, [this]() { scheduled_ = false; run(); });
}

void NativePlayer::run() {
  if (closed_ || running_ || song_.isEmpty()) return;
  const auto generation = generation_;
  running_ = true;
  if (pending_) {
    pending_ = false;
    const auto song = song_;
    const auto platform = song.value("platform").toString();
    const auto valid = [this, generation]() { return !closed_ && generation == generation_; };
    QString failure = "unknown";
    bool started = false, local = platform == "local";
    try {
      QString url;
      if (local) url = localUrl(song.value("id").toString());
      else {
        const auto file = downloads_.localFile(song);
        if (!file.isEmpty()) { url = localUrl(file); local = true; }
      }
      if (local) started = open(url, generation);
      else if (!storage_.cacheSettings().value("offlineMode").toBool()) {
        if (highest_) {
          auto qualities = song.value("availableQualities").toList();
          if (qualities.isEmpty()) {
            try { qualities = music_.request(platform, "qualities", {{"id", song.value("id")}}, 8000).value("qualities").toList(); }
            catch (const NativeRequestFailure&) {}
          }
          if (valid() && !qualities.isEmpty()) {
            int highest = 0;
            for (const auto& raw : qualities) {
              try { highest = qMax(highest, NativeQualityIndex(raw.toMap().value("level"))); }
              catch (const NativeRequestFailure&) {}
            }
            quality_ = highest;
          }
        }
        for (int quality = quality_; quality >= 0 && valid(); --quality) {
          try {
            const auto response = music_.request(platform, "stream", {{"id", song.value("id")}, {"quality", quality}}, 10000);
            if (!valid()) break;
            url = response.value("url").toString();
            if (NativeStreamUrl(url) && open(url, generation)) { quality_ = quality; started = true; break; }
          } catch (const NativeRequestFailure& error) { failure = error.code; }
        }
        if (!started && valid()) {
          const auto result = matching_.resolve(song, quality_, valid);
          if (valid() && !result.isEmpty()) {
            started = open(result.value("url").toString(), generation);
            if (started) source_ = result.value("platform").toString();
            else matching_.invalidate(song, result.value("platform").toString());
          }
        }
      }
    } catch (const NativeRequestFailure& error) { failure = error.code; }
    catch (const NativeStorageError&) { failure = "storage"; }
    if (valid()) {
      busy_ = false;
      playing_ = started;
      if (started) {
        loadedKey_ = NativeSongKey(song);
        failureSkips_ = 0;
        error_ = pendingNotice_;
        pendingNotice_.clear();
        lastAudioPosition_ = position_;
        lastProgressAt_ = clock_.elapsed();
        try { library_.recordListen(song); }
        catch (const NativeStorageError&) { qWarning("MCONNECT_NATIVE_HISTORY_STORE_FAILED"); }
        fadeIn();
        changedState();
      } else failed(failure, generation);
    }
  } else if (prefetchPending_) {
    prefetchPending_ = false;
    prefetch(generation);
  }
  running_ = false;
  if (!closed_ && (pending_ || (playing_ && prefetchPending_))) schedule();
}

bool NativePlayer::open(const QString& url, quint64 generation) {
  const auto valid = [this, generation]() { return !closed_ && generation == generation_; };
  const auto fadeDuration = effects_.value("fadeDurationMs", 800).toInt();
  if (audio_ && playing_ && effects_.value("fadeEnabled").toBool() &&
      !fadeOut(qBound(200, fadeDuration, 400), generation)) return false;
  for (int attempt = 0; attempt < 2 && valid(); ++attempt) {
    if (!audio_) audio_ = mc_audio_create();
    if (!audio_) return false;
    mc_audio_state(audio_, 0);
    const auto uri = url.toUtf8();
    bool ready = mc_audio_open(audio_, uri.constData()) != 0;
    QElapsedTimer elapsed;
    elapsed.start();
    while (ready && valid() && elapsed.elapsed() < 12000) {
      const auto status = mc_audio_status(audio_);
      if (status == 2 || status == 3) break;
      if (status == 5 || status == 4) { ready = false; break; }
      waitStep(20);
    }
    if (!valid()) return false;
    if (elapsed.elapsed() >= 12000) ready = false;
    if (ready) {
      const qint64 actualDuration = mc_audio_duration(audio_) / 1000;
      if (actualDuration > 0) duration_ = qMin(actualDuration, maximumMilliseconds);
      if (duration_ > 0) position_ = qMin(position_, duration_);
      if (position_ > 0 && !mc_audio_seek(audio_, position_ * 1000)) qWarning("MCONNECT_NATIVE_RESTORE_SEEK_FAILED");
      if (speed_ != 1 && !mc_audio_rate(audio_, speed_)) qWarning("MCONNECT_NATIVE_RATE_FAILED");
      double bands[5] = {};
      if (effects_.value("equalizerEnabled").toBool()) {
        const QMap<QString, QList<double>> presets{{"bassBoost", {6, 4, 1, 0, 0}}, {"vocal", {-2, 0, 5, 3, 1}}, {"rock", {4, 2, 0, 3, 5}}};
        const auto preset = effects_.value("equalizerPreset").toString();
        if (presets.contains(preset)) {
          for (int index = 0; index < 5; ++index) bands[index] = presets.value(preset)[index];
        } else if (preset == "custom") {
          const auto gains = effects_.value("equalizerBandGains").toList();
          for (int index = 0; index < qMin(5, gains.size()); ++index) {
            const auto gain = gains[index].toDouble();
            if (std::isfinite(gain)) bands[index] = qBound(-24.0, gain, 12.0);
          }
        }
      }
      mc_audio_equalizer(audio_, bands);
      mc_audio_volume(audio_, effects_.value("fadeEnabled").toBool() ? 0 : 1);
      if (mc_audio_state(audio_, 2)) { lastStatus_ = -1; return true; }
    }
    mc_audio_destroy(audio_);
    audio_ = nullptr;
  }
  return false;
}

bool NativePlayer::fadeOut(int duration, quint64 generation) {
  if (!audio_ || !playing_) return generation == generation_ && !closed_;
  QElapsedTimer elapsed;
  elapsed.start();
  while (!closed_ && generation == generation_ && elapsed.elapsed() < duration) {
    mc_audio_volume(audio_, qMax(0.0, 1.0 - double(elapsed.elapsed()) / duration));
    waitStep(20);
  }
  return !closed_ && generation == generation_;
}

void NativePlayer::fadeIn() {
  if (!audio_) return;
  if (!effects_.value("fadeEnabled").toBool()) { mc_audio_volume(audio_, 1); return; }
  fadeGeneration_ = generation_;
  fadeDuration_ = qBound(200, effects_.value("fadeDurationMs", 800).toInt(), 3000);
  fadeFrom_ = 0;
  fadeClock_.restart();
  mc_audio_volume(audio_, 0);
  fade_.start(20);
}

void NativePlayer::cancelFade() {
  fade_.stop();
  if (audio_) mc_audio_volume(audio_, 1);
}

void NativePlayer::failed(const QString& code, quint64 generation) {
  if (generation != generation_ || closed_) return;
  playing_ = false;
  busy_ = false;
  loadedKey_.clear();
  error_ = code.isEmpty() ? "unknown" : code;
  if (audio_) mc_audio_state(audio_, 0);
  const bool canSkip = failureSkips_ < 3 && repeat_ != "one" && playlist_.size() > 1 && index_ >= 0 &&
      (shuffle_ || index_ + 1 < playlist_.size() || repeat_ == "all");
  changedState();
  if (canSkip) {
    ++failureSkips_;
    pendingNotice_ = "skipped";
    const auto nextIndex = shuffle_ ? shuffledIndex() : (index_ + 1) % playlist_.size();
    requestPlay(playlist_[nextIndex].toMap(), false, false, nextIndex);
  }
}

void NativePlayer::prefetch(quint64 generation) {
  const auto valid = [this, generation]() { return !closed_ && generation == generation_ && !pending_; };
  for (int step = 1; step <= 2 && valid(); ++step) {
    const auto index = index_ + step;
    if (index >= playlist_.size()) break;
    try { matching_.resolve(playlist_[index].toMap(), quality_, valid); }
    catch (const std::runtime_error&) { qWarning("MCONNECT_NATIVE_PREFETCH_FAILED"); }
  }
}

void NativePlayer::tick() {
  if (!audio_ || closed_ || busy_ || song_.isEmpty() || loadedKey_ != NativeSongKey(song_)) return;
  const auto status = mc_audio_status(audio_);
  const bool changedStatus = status != lastStatus_;
  lastStatus_ = status;
  const qint64 position = mc_audio_position(audio_) / 1000;
  const qint64 duration = mc_audio_duration(audio_) / 1000;
  if (duration > 0) duration_ = qMin(duration, maximumMilliseconds);
  if (status == 2 || status == 3 || status == 1) position_ = qBound(qint64(0), position, duration_ > 0 ? duration_ : maximumMilliseconds);
  if (position_ > lastAudioPosition_) { lastAudioPosition_ = position_; lastProgressAt_ = clock_.elapsed(); }
  if (playing_ && abStart_ >= 0 && abEnd_ > abStart_ && position_ >= abEnd_) { seek(abStart_); return; }
  if (playing_ && status == 4 && changedStatus) {
    playing_ = false;
    if (repeat_ == "one") { seek(0); play(); }
    else if (shuffle_ || index_ + 1 < playlist_.size() || repeat_ == "all") next();
    else { position_ = duration_; changedState(); }
  } else if (playing_ && status == 5 && changedStatus) {
    matching_.invalidate(song_, source_);
    if (recoveryAttempts_++ < 2) requestPlay(song_, true, false, index_);
    else failed("network", generation_);
  } else if (playing_ && !running_ && clock_.elapsed() - lastProgressAt_ >= 30000 && recoveryAttempts_ < 2) {
    ++recoveryAttempts_;
    requestPlay(song_, true, false, index_);
  } else {
    if (status == 4 || status == 5) playing_ = false;
    changedState();
  }
}

void NativePlayer::play() {
  if (song_.isEmpty() || closed_) return;
  if (busy_) return;
  const auto status = audio_ ? mc_audio_status(audio_) : 0;
  if (!audio_ || loadedKey_ != NativeSongKey(song_) || status == 5) {
    requestPlay(song_, true, true, index_);
    return;
  }
  if (status == 4) {
    position_ = 0;
    if (!mc_audio_seek(audio_, 0)) {
      requestPlay(song_, true, true, index_);
      return;
    }
    emit seeked(0);
  }
  ++generation_;
  cancelFade();
  playing_ = mc_audio_state(audio_, 2) != 0;
  lastStatus_ = -1;
  lastAudioPosition_ = position_;
  lastProgressAt_ = clock_.elapsed();
  fadeIn();
  changedState();
}

void NativePlayer::pause() {
  if (closed_) return;
  const auto generation = ++generation_;
  pending_ = false;
  busy_ = false;
  cancelFade();
  if (playing_ && effects_.value("fadeEnabled").toBool()) {
    busy_ = true;
    fadeOut(qBound(200, effects_.value("fadeDurationMs", 800).toInt(), 3000), generation);
  }
  if (generation != generation_ || closed_) return;
  busy_ = false;
  if (audio_) mc_audio_state(audio_, 1);
  playing_ = false;
  cancelFade();
  changedState();
}

void NativePlayer::toggle() { if (playing_ || busy_) pause(); else play(); }

void NativePlayer::seek(qint64 position) {
  if (position < 0 || position > duration_) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  position_ = position;
  if (audio_ && !busy_ && !song_.isEmpty() && loadedKey_ == NativeSongKey(song_) && !mc_audio_seek(audio_, position * 1000)) {
    qWarning("MCONNECT_NATIVE_SEEK_FAILED");
  }
  lastAudioPosition_ = position;
  lastProgressAt_ = clock_.elapsed();
  emit seeked(position * 1000);
  changedState();
}

int NativePlayer::shuffledIndex() {
  if (playlist_.size() <= 1) return qMax(0, index_);
  if (!song_.isEmpty()) shuffledPlayed_.insert(NativeSongKey(song_));
  QList<int> candidates;
  for (int index = 0; index < playlist_.size(); ++index) {
    if (index != index_ && !shuffledPlayed_.contains(NativeSongKey(playlist_[index].toMap()))) candidates.append(index);
  }
  if (candidates.isEmpty()) {
    shuffledPlayed_.clear();
    if (!song_.isEmpty()) shuffledPlayed_.insert(NativeSongKey(song_));
    for (int index = 0; index < playlist_.size(); ++index) if (index != index_) candidates.append(index);
  }
  std::uniform_int_distribution<int> choose(0, candidates.size() - 1);
  return candidates[choose(random_)];
}

void NativePlayer::next() {
  if (playlist_.isEmpty()) return;
  if (repeat_ == "one") { seek(0); play(); return; }
  auto index = index_ + 1;
  if (shuffle_) {
    if (shuffleHistory_.isEmpty() || shuffleHistory_.last() != index_) shuffleHistory_.append(index_);
    while (shuffleHistory_.size() > playlist_.size()) shuffleHistory_.removeFirst();
    index = shuffledIndex();
  } else if (index >= playlist_.size()) {
    if (repeat_ != "all") return;
    index = 0;
  }
  requestPlay(playlist_[index].toMap(), false, true, index);
}

void NativePlayer::previous() {
  if (playlist_.isEmpty()) return;
  if (position_ >= 4000) { seek(0); return; }
  int index;
  if (shuffle_) index = shuffleHistory_.isEmpty() ? shuffledIndex() : shuffleHistory_.takeLast();
  else index = index_ > 0 ? index_ - 1 : repeat_ == "all" ? playlist_.size() - 1 : 0;
  index = qBound(0, index, playlist_.size() - 1);
  requestPlay(playlist_[index].toMap(), false, true, index);
}

void NativePlayer::toggleShuffle() {
  shuffle_ = !shuffle_;
  shuffledPlayed_.clear();
  shuffleHistory_.clear();
  changedState();
}

void NativePlayer::cycleRepeat() {
  const QStringList modes{"off", "all", "one"};
  repeat_ = modes[(modes.indexOf(repeat_) + 1) % modes.size()];
  changedState();
}

void NativePlayer::close() {
  if (closed_) return;
  closed_ = true;
  ++generation_;
  poll_.stop();
  save_.stop();
  sleep_.stop();
  cancelFade();
  persist();
  if (sleepRemaining_ > 0) persistSleep();
  if (audio_) { mc_audio_destroy(audio_); audio_ = nullptr; }
}
