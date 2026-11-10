#include "native_library.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QUuid>

QVariantMap NativeSongRow(const QVariantMap& song) {
  return {{"id", song.value("id")}, {"platform", song.value("platform")}, {"title", song.value("name")},
          {"artist", NativeArtistNames(song)}, {"duration", song.value("durationMs", 0)}};
}

QVariantMap NativePlaylistRow(const QVariantMap& playlist) {
  return {{"id", playlist.value("id")}, {"title", playlist.value("name")}, {"platform", playlist.value("platform")},
          {"count", playlist.value("songCount", 0)}, {"creator", playlist.value("creatorName", "")}};
}

QVariantMap NativeAlbumRow(const QVariantMap& album) {
  return {{"id", album.value("id")}, {"title", album.value("name")}, {"artist", album.value("artistName", "")},
          {"date", album.value("releaseDate").toString().section('T', 0, 0)}, {"count", album.value("songCount", 0)}};
}

QVariantMap NativeStoredPlaybackSong(const QVariantMap& song) {
  QVariantMap result;
  for (const auto& key : QStringList{"id", "platform", "name", "artists", "album", "durationMs", "coverUrl", "availableQualities"}) {
    result[key] = song.value(key);
  }
  return result;
}

QVariantList NativeLibrary::songs(const QVariantList& rows) {
  QVariantList result;
  for (const auto& raw : rows) {
    const auto row = raw.toMap();
    QVariantList artists;
    for (const auto& name : row.value("artists").toString().split(',')) {
      artists.append(QVariantMap{{"id", ""}, {"name", name.trimmed()}});
    }
    const auto album = row.value("album_name").isNull() ? QVariant() : QVariant(QVariantMap{
        {"id", row.value("album_id", "")}, {"name", row.value("album_name")}, {"coverUrl", row.value("album_cover")}});
    const auto song = NativePlaybackSong({{"id", row.value("id")}, {"platform", row.value("platform")},
        {"name", row.value("name")}, {"artists", artists}, {"album", album}, {"coverUrl", row.value("album_cover")},
        {"durationMs", row.value("duration_ms")}, {"artistId", row.value("artist_id")},
        {"albumId", row.value("album_id")}, {"trackNumber", row.value("track_number")}});
    if (NativeValidSong(song)) result.append(song);
  }
  return result;
}

QVariantList NativeLibrary::likes() {
  return songs(storage_.database().query("SELECT s.* FROM user_likes l JOIN songs s ON s.id=l.song_id AND s.platform=l.platform ORDER BY l.added_at DESC LIMIT 500"));
}

QVariantList NativeLibrary::history() {
  return songs(storage_.database().query("SELECT s.* FROM listening_history h JOIN songs s ON s.id=h.song_id AND s.platform=h.platform ORDER BY h.listened_at DESC LIMIT 200"));
}

bool NativeLibrary::isLiked(const QVariantMap& song) {
  return !storage_.database().query("SELECT id FROM user_likes WHERE song_id=? AND platform=? LIMIT 1",
                                  {song.value("id"), song.value("platform")}).isEmpty();
}

void NativeLibrary::saveSong(const QVariantMap& song) {
  NativeSongKey(song);
  const auto album = song.value("album").toMap();
  QStringList artists;
  for (const auto& artist : song.value("artists").toList()) artists.append(artist.toMap().value("name").toString());
  const auto fingerprint = song.value("name").toString().toLower() + '_' + artists.join(',').toLower() + '_' +
      QString::number(song.value("durationMs").toLongLong() / 1000);
  storage_.database().execute("INSERT OR REPLACE INTO songs(id,platform,name,artists,album_name,album_cover,duration_ms,fingerprint,album_id,artist_id,track_number) VALUES(?,?,?,?,?,?,?,?,?,?,?)",
      {song.value("id"), song.value("platform"), song.value("name"), artists.join(','), album.value("name"),
       song.value("coverUrl").isNull() ? album.value("coverUrl") : song.value("coverUrl"), song.value("durationMs", 0), fingerprint,
       song.value("albumId", album.value("id")), song.value("artistId"), song.value("trackNumber")});
}

bool NativeLibrary::toggleLike(const QVariantMap& song) {
  bool liked = false;
  storage_.database().transaction([&]() {
    liked = !isLiked(song);
    if (liked) {
      saveSong(song);
      storage_.database().execute("INSERT OR REPLACE INTO user_likes(song_id,platform,added_at) VALUES(?,?,?)",
          {song.value("id"), song.value("platform"), QDateTime::currentMSecsSinceEpoch()});
    } else storage_.database().execute("DELETE FROM user_likes WHERE song_id=? AND platform=?", {song.value("id"), song.value("platform")});
  });
  return liked;
}

void NativeLibrary::recordListen(const QVariantMap& song, qint64 durationMs, qint64 at) {
  if (at < 0) at = QDateTime::currentMSecsSinceEpoch();
  storage_.database().transaction([&]() {
    saveSong(song);
    const auto recent = storage_.database().query("SELECT id,listened_at,duration_listened FROM listening_history WHERE song_id=? AND platform=? ORDER BY listened_at DESC LIMIT 1",
        {song.value("id"), song.value("platform")});
    if (!recent.isEmpty() && at >= recent.first().toMap().value("listened_at").toLongLong() &&
        at - recent.first().toMap().value("listened_at").toLongLong() <= 10000) {
      const auto row = recent.first().toMap();
      storage_.database().execute("UPDATE listening_history SET duration_listened=? WHERE id=?",
          {qMax(durationMs, row.value("duration_listened").toLongLong()), row.value("id")});
    } else storage_.database().execute("INSERT INTO listening_history(song_id,platform,listened_at,duration_listened) VALUES(?,?,?,?)",
        {song.value("id"), song.value("platform"), at, qMax(qint64(0), durationMs)});
  });
}

NativeResults::NativeResults(NativeMusicServices& music)
    : music_(music), prefix_(QUuid::createUuid().toString().mid(1, 8)) {}

void NativeResults::purgeAccounts() {
  for (auto entry = entries_.begin(); entry != entries_.end();) {
    if (!entry.value().platform.isEmpty() && music_.epoch(entry.value().platform) != entry.value().epoch) {
      order_.removeAll(entry.key());
      entry = entries_.erase(entry);
    } else ++entry;
  }
  for (auto entry = recommendations_.begin(); entry != recommendations_.end();) {
    if (!entries_.contains(entry.value())) entry = recommendations_.erase(entry);
    else ++entry;
  }
}

QVariantMap NativeResults::store(const QVariantList& songs, const QString& platform, const QString& recommendation) {
  purgeAccounts();
  for (const auto& song : songs) if (!NativeValidSong(song.toMap())) throw NativeRequestFailure(QStringLiteral("歌曲列表数据无效"));
  const auto token = prefix_ + '-' + QString::number(++sequence_);
  entries_[token] = {songs, platform, music_.epoch(platform)};
  order_.append(token);
  if (!recommendation.isEmpty()) recommendations_[recommendation] = token;
  int count = 0;
  for (const auto& entry : entries_) count += entry.songs.size();
  for (const auto& key : QStringList(order_)) {
    if (order_.size() <= 100 && (order_.size() <= 1 || count <= 10000)) break;
    if (key == token || recommendations_.values().contains(key)) continue;
    count -= entries_.value(key).songs.size();
    entries_.remove(key);
    order_.removeAll(key);
  }
  return page(token, 0);
}

QVariantList NativeResults::songs(const QString& token) {
  purgeAccounts();
  if (!entries_.contains(token)) throw NativeRequestFailure(QStringLiteral("列表已过期，请返回刷新"));
  return entries_.value(token).songs;
}

QVariantMap NativeResults::page(const QString& token, int offset) {
  const auto all = songs(token);
  if (offset < 0 || offset > all.size()) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  QVariantList rows;
  for (const auto& song : all.mid(offset, 100)) rows.append(NativeSongRow(song.toMap()));
  return {{"token", token}, {"total", all.size()}, {"offset", offset}, {"songs", rows}};
}

QVariantMap NativeResults::song(const QVariantMap& arguments, const QVariantMap& current) {
  if (arguments.value("current").type() == QVariant::Bool && arguments.value("current").toBool()) {
    if (!NativeValidSong(current)) throw NativeRequestFailure(QStringLiteral("请先选择歌曲"));
    return current;
  }
  const auto all = songs(NativeIdentifier(arguments.value("token")));
  if (all.isEmpty()) throw NativeRequestFailure(QStringLiteral("列表已过期，请返回刷新"));
  const int index = NativeIntegerArgument(arguments.value("index"), 0, all.size() - 1);
  return all[index].toMap();
}

QVariantMap NativeRecommendations::get(const QString& platform, bool refresh) {
  const auto scope = platform + ':' + music_.accountScope(platform);
  const auto key = QString::fromLatin1(QCryptographicHash::hash(scope.toUtf8(), QCryptographicHash::Sha256).toHex());
  const auto directory = QDir(storage_.cacheDirectory).filePath(".recommendations");
  const auto path = QDir(directory).filePath(key + ".mcr");
  auto cached = memory_.value(key);
  if (cached.isEmpty()) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly) && file.size() <= 4 * 1024 * 1024) {
      const auto record = QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
      try {
        if (record.value("version").toInt() == 1 && record.value("label").type() == QVariant::String && record.value("note").type() == QVariant::String) {
          cached = {{"songs", NativeMusicSongs(record, platform).mid(0, 1000)}, {"label", record.value("label")}, {"note", record.value("note")}};
          memory_[key] = cached;
        }
      } catch (const NativeRequestFailure&) {}
    }
  }
  const auto cachedResult = [&](const QString& note) {
    auto result = cached;
    result["note"] = cached.value("note").toString().isEmpty() ? note : cached.value("note").toString() + QStringLiteral(" · ") + note;
    return result;
  };
  if (!cached.isEmpty() && !refresh) return cachedResult(QStringLiteral("本地缓存"));
  const auto originalEpoch = music_.epoch(platform);
  QVariantMap result;
  try { result = music_.daily(platform); }
  catch (const NativeRequestFailure&) {
    if (music_.epoch(platform) != originalEpoch || platform + ':' + music_.accountScope(platform) != scope) throw;
    if (!cached.isEmpty()) return cachedResult(QStringLiteral("刷新失败，使用本地缓存"));
    throw;
  }
  if (music_.epoch(platform) != originalEpoch) throw NativeRequestFailure(QStringLiteral("请求已取消，请刷新列表"));
  memory_[key] = result;
  QVariantList stored;
  for (const auto& raw : result.value("songs").toList().mid(0, 1000)) {
    const auto song = raw.toMap();
    auto row = NativeStoredPlaybackSong(song);
    for (const auto& name : QStringList{"albumId", "artistId", "trackNumber", "fee"}) row[name] = song.value(name);
    stored.append(row);
  }
  const auto bytes = QJsonDocument::fromVariant(QVariantMap{{"version", 1}, {"savedAt", QDateTime::currentDateTime().toString("yyyy-MM-dd'T'HH:mm:ss.zzz")},
      {"label", result.value("label")}, {"note", result.value("note")}, {"songs", stored}}).toJson(QJsonDocument::Compact);
  try {
    if (bytes.size() <= 4 * 1024 * 1024) {
      NativeAtomicWrite(QDir(directory).filePath(".nomedia"), {});
      NativeAtomicWrite(path, bytes);
    }
  } catch (const NativeStorageError&) {}
  return result;
}
