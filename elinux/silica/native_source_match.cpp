#include "native_source_match.h"
#include "native_lyric_loader.h"

#include <QDateTime>
#include <QEventLoop>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace {
double textScore(const QString& left, const QString& right) {
  if (left.isEmpty() || right.isEmpty()) return 0;
  if (left == right) return 1;
  return left.contains(right) || right.contains(left) ? 0.7 : 0;
}

QString primaryArtist(const QVariantMap& song) {
  const auto artists = song.value("artists").toList();
  return artists.isEmpty() ? QString() : NativeMatchText(artists.first().toMap().value("name").toString());
}

void retryDelay(int milliseconds) {
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}
}

QString NativeMatchText(const QString& text) {
  auto value = text.toLower();
  value.replace(QRegularExpression("[\\(（\\[【][^\\)）\\]】]*[\\)）\\]】]"), " ");
  value.replace(QRegularExpression("\\b(feat|ft)\\.?\\s.*$"), " ");
  value.replace(QRegularExpression("\\b(feat|ft|live|remaster|remastered|version|explicit|hq|sq|ost)\\b"), " ");
  value.remove(QRegularExpression("[^0-9a-z\\x{4e00}-\\x{9fff}]+"));
  return value;
}

double NativeSourceMatchScore(const QVariantMap& song, const QVariantMap& candidate) {
  const auto duration = song.value("durationMs").toLongLong();
  const auto otherDuration = candidate.value("durationMs").toLongLong();
  const bool known = duration > 0 && otherDuration > 0;
  const auto delta = std::abs(double(duration) - double(otherDuration));
  if (known && delta > 3000) return 0;
  const auto title = textScore(NativeMatchText(song.value("name").toString()), NativeMatchText(candidate.value("name").toString()));
  const auto artist = primaryArtist(song);
  const auto otherArtist = primaryArtist(candidate);
  const auto artistScore = artist.isEmpty() || otherArtist.isEmpty() ? 0.5 : textScore(artist, otherArtist);
  const auto durationScore = known ? 1 - delta / 3000 : 0.5;
  return qBound(0.0, title * 0.5 + artistScore * 0.3 + durationScore * 0.2, 1.0);
}

bool NativeStreamUrl(const QString& raw) {
  const QUrl url(raw, QUrl::StrictMode);
  return raw.size() <= 16384 && url.isValid() && QStringList{"http", "https"}.contains(url.scheme()) &&
      !url.host().isEmpty() && url.userInfo().isEmpty();
}

NativeSourceMatch::NativeSourceMatch(NativeStorage& storage, NativeMusicServices& music, QObject* parent)
    : QObject(parent), storage_(storage), music_(music) {
  for (const auto& platform : music_.platforms()) {
    const auto id = platform.toMap().value("id").toString();
    epochs_[id] = music_.epoch(id);
  }
  connect(&music_, &NativeMusicServices::accountsChanged, this, [this]() {
    for (auto epoch = epochs_.begin(); epoch != epochs_.end(); ++epoch) {
      if (epoch.value() == music_.epoch(epoch.key())) continue;
      epoch.value() = music_.epoch(epoch.key());
      try { storage_.database().execute("DELETE FROM source_match_caches WHERE target_platform=?", {epoch.key()}); }
      catch (const NativeStorageError&) { qWarning("MCONNECT_NATIVE_SOURCE_CACHE_INVALIDATE_FAILED"); }
    }
  });
}

QVariantMap NativeSourceMatch::resolve(const QVariantMap& song, int quality, const std::function<bool()>& valid) {
  if (song.value("platform") == "local" || storage_.cacheSettings().value("offlineMode").toBool() ||
      storage_.box("settings").get("auto_source_switch") == false) return {};
  const auto key = NativeSongDedupeKey(song);
  int queried = 0;
  for (const auto& platform : music_.platforms()) {
    const auto target = platform.toMap().value("id").toString();
    if (target == song.value("platform") || target == "local" || !music_.loggedIn(target)) continue;
    if (!valid()) return {};
    const auto epoch = music_.epoch(target);
    try {
      const auto cached = storage_.database().query("SELECT * FROM source_match_caches WHERE song_key=? AND target_platform=? AND expires_at>? LIMIT 1",
          {key, target, QDateTime::currentMSecsSinceEpoch() + 60000});
      if (!cached.isEmpty() && NativeStreamUrl(cached.first().toMap().value("url").toString())) {
        auto result = cached.first().toMap();
        result["platform"] = target;
        result["fromCache"] = true;
        return result;
      }
    } catch (const NativeStorageError&) { qWarning("MCONNECT_NATIVE_SOURCE_CACHE_READ_FAILED"); }
    if (queried++ >= 2) break;
    QVariantList candidates;
    for (int attempt = 0; attempt < 3; ++attempt) {
      try {
        candidates = NativeMusicSongs(music_.request(target, "search", {{"query", song.value("name")}, {"limit", 10}, {"page", 1}}, 8000), target);
        break;
      } catch (const NativeRequestFailure&) {
        if (!valid() || music_.epoch(target) != epoch || !music_.loggedIn(target)) return {};
        if (attempt < 2) retryDelay(300 * (1 << attempt));
      }
      if (!valid() || music_.epoch(target) != epoch) return {};
    }
    if (!valid() || music_.epoch(target) != epoch) return {};
    QVariantMap best;
    double score = -1;
    for (const auto& raw : candidates) {
      const auto candidate = raw.toMap();
      const auto candidateScore = NativeSourceMatchScore(song, candidate);
      if (candidateScore > score) { best = candidate; score = candidateScore; }
    }
    if (score < 0.8) continue;
    for (int level = quality; level >= 0; --level) {
      try {
        const auto response = music_.request(target, "stream", {{"id", best.value("id")}, {"quality", level}}, 8000);
        if (!valid() || music_.epoch(target) != epoch) return {};
        const auto url = response.value("url").toString();
        if (!NativeStreamUrl(url)) continue;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        try {
          storage_.database().execute("INSERT OR REPLACE INTO source_match_caches(song_key,target_platform,target_song_id,url,url_fetched_at,score,expires_at) VALUES(?,?,?,?,?,?,?)",
              {key, target, best.value("id"), url, now, score, now + 1200000});
        } catch (const NativeStorageError&) { qWarning("MCONNECT_NATIVE_SOURCE_CACHE_WRITE_FAILED"); }
        return {{"url", url}, {"platform", target}, {"target_song_id", best.value("id")}, {"score", score}, {"fromCache", false}};
      } catch (const NativeRequestFailure&) { if (!valid() || music_.epoch(target) != epoch) return {}; }
    }
  }
  return {};
}

void NativeSourceMatch::invalidate(const QVariantMap& song, const QString& platform) {
  if (platform.isEmpty() || platform == song.value("platform")) return;
  try { storage_.database().execute("DELETE FROM source_match_caches WHERE song_key=? AND target_platform=?", {NativeSongDedupeKey(song), platform}); }
  catch (const NativeStorageError&) { qWarning("MCONNECT_NATIVE_SOURCE_CACHE_INVALIDATE_FAILED"); }
}
