#include "native_storage.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

namespace {
const QStringList platforms = {"netease", "qq", "kugou", "bilibili", "local"};
const QStringList qualities = {"low", "medium", "high", "lossless", "hires", "spatial", "dolby", "master"};

qint64 integer(const QVariant& value, qint64 fallback = 0) {
  bool valid = false;
  const auto result = value.toLongLong(&valid);
  if (value.type() == QVariant::Bool || (value.type() == QVariant::Double &&
      (!std::isfinite(value.toDouble()) || value.toDouble() != double(result)))) return fallback;
  return valid ? result : fallback;
}

QString storageDirectory(const char* environment, const QString& fallback) {
  const auto value = QString::fromLocal8Bit(qgetenv(environment));
  const auto root = value.isEmpty() ? QDir(QDir::homePath()).filePath(fallback) : value;
  return QDir(root).filePath("harbour-mconnect");
}

QVariantMap jsonObject(const QString& path, qint64 limit = 64 * 1024 * 1024) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > limit) {
    throw NativeStorageError("Cannot read stored JSON");
  }
  const auto bytes = file.readAll();
  if (file.error() != QFileDevice::NoError) throw NativeStorageError("Cannot read stored JSON");
  if (bytes.trimmed().isEmpty()) return {};
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    throw NativeStorageError("Invalid stored JSON");
  }
  return document.toVariant().toMap();
}

QVariantMap playlistRecord(const QVariantMap& entry) {
  return {{"id", entry.value("id")}, {"platform", "local"}, {"name", entry.value("name")},
          {"songCount", entry.value("songKeys").toList().size()}, {"editable", true},
          {"editId", entry.value("id")}, {"creatorName", ""}};
}

QVariantMap newPlaylist(const QVariantMap& state, const QString& name, const QVariantList& keys) {
  const auto now = QDateTime::currentMSecsSinceEpoch();
  QSet<QString> ids;
  for (const auto& entry : state.value("playlists").toList()) ids.insert(entry.toMap().value("id").toString());
  const auto prefix = "my_" + QString::number(now);
  QString id = prefix;
  int suffix = 0;
  while (ids.contains(id)) id = prefix + '_' + QString::number(++suffix);
  return {{"id", id}, {"name", name.trimmed().isEmpty() ? QStringLiteral("未命名歌单") : name.trimmed()},
          {"createdAt", now}, {"updatedAt", now}, {"songKeys", keys}};
}

void removeUnreferencedSongs(QVariantMap& state) {
  QSet<QString> references;
  for (const auto& entry : state.value("playlists").toList()) {
    for (const auto& key : entry.toMap().value("songKeys").toList()) references.insert(key.toString());
  }
  auto songs = state.value("songs").toMap();
  for (auto item = songs.begin(); item != songs.end();) {
    if (references.contains(item.key())) ++item;
    else item = songs.erase(item);
  }
  state["songs"] = songs;
}

QVariantMap downloadTask(const QVariant& value) {
  if (value.type() != QVariant::Map) return {};
  auto row = value.toMap();
  const auto song = NativePlaybackSong(row.value("song").toMap());
  if (row.value("id").toString().isEmpty() || !NativeValidSong(song)) return {};
  row["id"] = row.value("id").toString();
  row["song"] = song;
  if (!qualities.contains(row.value("quality").toString())) row["quality"] = "low";
  if (!QStringList{"waiting", "downloading", "paused", "completed", "failed"}.contains(row.value("status").toString())) {
    row["status"] = "waiting";
  }
  const double progress = row.value("progress").toDouble();
  row["progress"] = std::isfinite(progress) ? qBound(0.0, progress, 1.0) : 0.0;
  row["downloadedBytes"] = integer(row.value("downloadedBytes"));
  row["isOfflineCache"] = row.value("isOfflineCache").type() == QVariant::Bool && row.value("isOfflineCache").toBool();
  return row;
}

void validateSessionPair(const QString& platform, const QVariant& cookie, const QVariant& user) {
  if (cookie.isNull() && user.isNull()) return;
  if (cookie.type() != QVariant::String || user.type() != QVariant::String ||
      user.toString().toUtf8().size() > 1024 * 1024) throw NativeStorageError("Invalid persisted session");
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(user.toString().toUtf8(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject() ||
      NativeSessionUser(platform, NativeSessionCookie(platform, cookie.toString(), document.toVariant().toMap()),
                        document.toVariant().toMap()).isEmpty()) {
    throw NativeStorageError("Inconsistent persisted session");
  }
}
}

QString NativeDataDirectory() { return storageDirectory("XDG_DATA_HOME", ".local/share"); }
QString NativeCacheDirectory() { return storageDirectory("XDG_CACHE_HOME", ".cache"); }

bool NativeValidSong(const QVariantMap& song) {
  return !song.value("id").toString().isEmpty() && !song.value("name").toString().isEmpty() &&
         platforms.contains(song.value("platform").toString());
}

QString NativeSongKey(const QVariantMap& song) {
  if (!NativeValidSong(song)) throw NativeStorageError("Invalid stored song");
  return song.value("platform").toString() + ':' + song.value("id").toString();
}

QString NativeArtistNames(const QVariantMap& song) {
  QStringList names;
  for (const auto& artist : song.value("artists").toList()) names.append(artist.toMap().value("name").toString());
  return names.join(", ");
}

QVariantMap NativePlaybackSong(const QVariantMap& song) {
  if (!NativeValidSong(song)) return {};
  auto result = song;
  result["id"] = song.value("id").toString();
  result["name"] = song.value("name").toString();
  QVariantList artists;
  for (const auto& item : song.value("artists").toList()) {
    if (item.type() != QVariant::Map) continue;
    auto artist = item.toMap();
    artist["id"] = artist.value("id").toString();
    artist["name"] = artist.value("name").toString();
    artists.append(artist);
  }
  if (artists.isEmpty()) artists.append(QVariantMap{{"id", ""}, {"name", ""}});
  result["artists"] = artists;
  result["durationMs"] = integer(song.value("durationMs"));
  if (!result.contains("coverUrl")) result["coverUrl"] = QVariant();
  if (!result.contains("album")) result["album"] = QVariant();
  QVariantList available;
  for (const auto& raw : song.value("availableQualities").toList()) {
    if (raw.type() != QVariant::Map) continue;
    auto quality = raw.toMap();
    const auto level = quality.value("level");
    quality["level"] = level.type() == QVariant::String && qualities.contains(level.toString()) ? level :
        QVariant(qualities[qBound(qint64(0), integer(level), qint64(7))]);
    quality["bitrate"] = integer(quality.value("bitrate"));
    quality["format"] = quality.value("format").toString();
    available.append(quality);
  }
  result["availableQualities"] = available;
  return result;
}

QVariantMap NativePlaylistStoredSong(const QVariantMap& song) {
  NativeSongKey(song);
  QVariantList artists;
  for (const auto& artist : song.value("artists").toList()) artists.append(artist.toMap().value("name").toString());
  const auto album = song.value("album").toMap();
  const auto cover = song.value("coverUrl").isNull() ? album.value("coverUrl") : song.value("coverUrl");
  return {{"id", song.value("id").toString()}, {"platform", song.value("platform")}, {"name", song.value("name")},
          {"artists", artists}, {"albumName", album.value("name")}, {"albumCover", cover},
          {"durationMs", integer(song.value("durationMs"))}};
}

QVariantMap NativePlaylistSong(const QVariantMap& stored) {
  if (!NativeValidSong(stored)) return {};
  auto song = stored;
  QVariantList artists;
  for (const auto& name : stored.value("artists").toList()) {
    if (!name.toString().isEmpty()) artists.append(QVariantMap{{"id", ""}, {"name", name.toString()}});
  }
  if (artists.isEmpty()) artists.append(QVariantMap{{"id", ""}, {"name", QStringLiteral("未知歌手")}});
  song["artists"] = artists;
  song["coverUrl"] = stored.value("albumCover");
  song["album"] = stored.value("albumName").isNull() ? QVariant() :
      QVariant(QVariantMap{{"id", ""}, {"name", stored.value("albumName")}, {"coverUrl", stored.value("albumCover")}});
  return NativePlaybackSong(song);
}

NativeLocalPlaylists::NativeLocalPlaylists(const QString& directory)
    : path_(QDir(directory).filePath("my_playlists.json")) {}

QVariantMap NativeLocalPlaylists::read() {
  if (!QFile::exists(path_) && QFile::exists(path_ + ".tmp")) {
    // The old writer removed the destination before renaming this file.
    // Validate the surviving write before recovering an interrupted upgrade.
    jsonObject(path_ + ".tmp");
    if (!QFile::rename(path_ + ".tmp", path_)) throw NativeStorageError("Cannot recover local playlists");
  }
  auto state = QFile::exists(path_) ? jsonObject(path_) : QVariantMap();
  if ((state.contains("playlists") && state.value("playlists").type() != QVariant::List) ||
      (state.contains("songs") && state.value("songs").type() != QVariant::Map)) {
    throw NativeStorageError("Invalid local playlist storage");
  }
  QVariantList entries;
  QHash<QString, int> positions;
  for (const auto& raw : state.value("playlists").toList()) {
    if (raw.type() != QVariant::Map) continue;
    auto entry = raw.toMap();
    const auto id = entry.value("id").toString();
    if (id.isEmpty()) continue;
    entry["id"] = id;
    if (!entry.contains("name")) entry["name"] = QStringLiteral("未命名歌单");
    QVariantList keys;
    for (const auto& key : entry.value("songKeys").toList()) {
      if (!key.toString().isEmpty()) keys.append(key.toString());
    }
    entry["songKeys"] = keys;
    if (positions.contains(id)) entries[positions.value(id)] = entry;
    else { positions[id] = entries.size(); entries.append(entry); }
  }
  state["version"] = 1;
  state["playlists"] = entries;
  if (!state.contains("songs")) state["songs"] = QVariantMap();
  return state;
}

void NativeLocalPlaylists::write(const QVariantMap& state) {
  NativeAtomicWrite(path_, QJsonDocument::fromVariant(state).toJson(QJsonDocument::Compact));
}

QVariantList NativeLocalPlaylists::playlists() {
  NativeFileLock lock(path_ + ".lock");
  auto entries = read().value("playlists").toList();
  std::stable_sort(entries.begin(), entries.end(), [](const QVariant& left, const QVariant& right) {
    return integer(left.toMap().value("updatedAt")) > integer(right.toMap().value("updatedAt"));
  });
  QVariantList result;
  for (const auto& entry : entries) result.append(playlistRecord(entry.toMap()));
  return result;
}

QVariantList NativeLocalPlaylists::songs(const QString& id) {
  NativeFileLock lock(path_ + ".lock");
  const auto state = read();
  const auto stored = state.value("songs").toMap();
  QVariantList result;
  for (const auto& entry : state.value("playlists").toList()) {
    const auto playlist = entry.toMap();
    if (playlist.value("id") != id) continue;
    for (const auto& key : playlist.value("songKeys").toList()) {
      const auto song = NativePlaylistSong(stored.value(key.toString()).toMap());
      if (NativeValidSong(song)) result.append(song);
    }
    break;
  }
  return result;
}

QVariantMap NativeLocalPlaylists::create(const QString& name, const QVariantList& songs) {
  NativeFileLock lock(path_ + ".lock");
  auto state = read();
  auto stored = state.value("songs").toMap();
  QVariantList keys;
  QSet<QString> seen;
  for (const auto& raw : songs) {
    const auto song = raw.toMap();
    const auto key = NativeSongKey(song);
    stored[key] = NativePlaylistStoredSong(song);
    if (!seen.contains(key)) { keys.append(key); seen.insert(key); }
  }
  const auto entry = newPlaylist(state, name, keys);
  auto entries = state.value("playlists").toList();
  entries.append(entry);
  state["playlists"] = entries;
  state["songs"] = stored;
  write(state);
  return playlistRecord(entry);
}

bool NativeLocalPlaylists::edit(const QString& action, const QString& id, const QVariant& value) {
  NativeFileLock lock(path_ + ".lock");
  auto state = read();
  auto entries = state.value("playlists").toList();
  int index = -1;
  for (int item = 0; item < entries.size(); ++item) {
    if (entries[item].toMap().value("id") == id) { index = item; break; }
  }
  if (index < 0) return false;
  auto entry = entries[index].toMap();
  auto keys = entry.value("songKeys").toList();
  if (action == "rename") {
    const auto name = value.toString().trimmed();
    if (name.isEmpty()) return false;
    entry["name"] = name;
  } else if (action == "duplicate") {
    const auto name = value.toString().trimmed().isEmpty() ? entry.value("name").toString() + QStringLiteral(" 副本") : value.toString();
    entries.append(newPlaylist(state, name, keys));
    state["playlists"] = entries;
    write(state);
    return true;
  } else if (action == "delete") {
    entries.removeAt(index);
    state["playlists"] = entries;
    removeUnreferencedSongs(state);
    write(state);
    return true;
  } else if (action == "add") {
    const auto song = value.toMap();
    const auto key = NativeSongKey(song);
    auto stored = state.value("songs").toMap();
    stored[key] = NativePlaylistStoredSong(song);
    state["songs"] = stored;
    if (!keys.contains(key)) keys.append(key);
    else {
      write(state);
      return true;
    }
  } else if (action == "remove") {
    const auto key = NativeSongKey(value.toMap());
    const auto position = keys.indexOf(key);
    if (position < 0) return false;
    keys.removeAt(position);
  } else if (action == "reorder") {
    QVariantList ordered;
    for (const auto& item : value.toList()) {
      const auto key = item.type() == QVariant::Map ? NativeSongKey(item.toMap()) : item.toString();
      if (keys.contains(key) && !ordered.contains(key)) ordered.append(key);
    }
    for (const auto& key : keys) if (!ordered.contains(key)) ordered.append(key);
    if (ordered == keys) return true;
    keys = ordered;
  } else throw NativeStorageError("Unsupported playlist operation");
  entry["songKeys"] = keys;
  entry["updatedAt"] = QDateTime::currentMSecsSinceEpoch();
  entries[index] = entry;
  state["playlists"] = entries;
  if (action == "remove") removeUnreferencedSongs(state);
  write(state);
  return true;
}

QString NativeLocalPlaylists::exportLink(const QString& id) {
  NativeFileLock lock(path_ + ".lock");
  const auto state = read();
  const auto stored = state.value("songs").toMap();
  for (const auto& raw : state.value("playlists").toList()) {
    const auto entry = raw.toMap();
    if (entry.value("id") != id) continue;
    QVariantList songs;
    for (const auto& key : entry.value("songKeys").toList()) {
      const auto row = stored.value(key.toString());
      if (row.type() == QVariant::Map) songs.append(row);
    }
    const auto bytes = QJsonDocument::fromVariant(QVariantMap{
        {"version", 1}, {"name", entry.value("name")}, {"songs", songs}}).toJson(QJsonDocument::Compact);
    const auto data = bytes.toBase64(QByteArray::Base64UrlEncoding);
    return "mconnect://playlist?data=" + QString::fromLatin1(QUrl::toPercentEncoding(QString::fromLatin1(data)));
  }
  return {};
}

QVariantMap NativeLocalPlaylists::importLink(const QString& link) {
  const QUrl url(link.trimmed(), QUrl::StrictMode);
  const auto encoded = QUrlQuery(url).queryItemValue("data", QUrl::FullyDecoded).toLatin1();
  if (url.scheme() != "mconnect" || url.host() != "playlist" || encoded.isEmpty() || encoded.size() > 250000 ||
      !QRegularExpression("^[A-Za-z0-9_-]+={0,2}$").match(QString::fromLatin1(encoded)).hasMatch()) {
    throw NativeStorageError("Invalid playlist link");
  }
  const auto bytes = QByteArray::fromBase64(encoded, QByteArray::Base64UrlEncoding);
  auto canonical = bytes.toBase64(QByteArray::Base64UrlEncoding);
  auto input = encoded;
  while (canonical.endsWith('=')) canonical.chop(1);
  while (input.endsWith('=')) input.chop(1);
  if (canonical != input) throw NativeStorageError("Invalid playlist encoding");
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) throw NativeStorageError("Invalid playlist payload");
  const auto data = document.toVariant().toMap();
  if (data.contains("songs") && data.value("songs").type() != QVariant::List) {
    throw NativeStorageError("Invalid playlist songs");
  }
  QVariantList songs;
  for (const auto& item : data.value("songs").toList()) {
    if (item.type() != QVariant::Map) continue;
    const auto song = NativePlaylistSong(item.toMap());
    if (!NativeValidSong(song)) throw NativeStorageError("Unsupported song in playlist");
    songs.append(song);
  }
  return create(data.value("name", QStringLiteral("导入歌单")).toString(), songs);
}

NativeStorage::NativeStorage(const QString& data, const QString& cache)
    : dataDirectory(QDir::cleanPath(data)), cacheDirectory(QDir::cleanPath(cache)) {}

NativeDatabase& NativeStorage::database() {
  if (!database_) database_.reset(new NativeDatabase(QDir(dataDirectory).filePath("mconnect.sqlite")));
  return *database_;
}

NativeLocalPlaylists& NativeStorage::playlists() {
  if (!playlists_) playlists_.reset(new NativeLocalPlaylists(dataDirectory));
  return *playlists_;
}

NativeHiveBox& NativeStorage::box(const QString& name) {
  auto& box = boxes_[name];
  if (!box) box.reset(new NativeHiveBox(dataDirectory, name));
  return *box;
}

QString NativeStorage::credentialPath(const QString& key) {
  const auto name = key.toUtf8().toBase64(QByteArray::Base64UrlEncoding);
  if (key.isEmpty() || name.size() > 255) throw NativeStorageError("Invalid credential key");
  const auto directory = QDir(dataDirectory).filePath("credentials");
  if (!QDir().mkpath(directory) ||
      !QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
    throw NativeStorageError("Cannot protect credential directory");
  }
  if (!credentialsLock_) credentialsLock_.reset(new NativeFileLock(QDir(dataDirectory).filePath("credentials.lock")));
  return QDir(directory).filePath(QString::fromLatin1(name));
}

QVariant NativeStorage::credential(const QString& key) {
  const auto path = credentialPath(key);
  if (!QFile::exists(path)) return {};
  if (QFileInfo(path).isSymLink()) throw NativeStorageError("Invalid credential file");
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) throw NativeStorageError("Cannot read credential");
  const auto bytes = file.readAll();
  if (file.error() != QFileDevice::NoError) throw NativeStorageError("Cannot read credential");
  const auto result = QString::fromUtf8(bytes);
  if (result.toUtf8() != bytes) throw NativeStorageError("Invalid credential encoding");
  return result;
}

void NativeStorage::setCredential(const QString& key, const QVariant& value) {
  const auto path = credentialPath(key);
  if (!value.isValid() || value.isNull()) {
    if (QFile::exists(path) && !QFile::remove(path)) throw NativeStorageError("Cannot remove credential");
    return;
  }
  if (value.type() != QVariant::String) throw NativeStorageError("Invalid credential value");
  NativeAtomicWrite(path, value.toString().toUtf8(), true);
}

void NativeStorage::recoverSession(const QString& platform) {
  if (!platforms.contains(platform) || platform == "local") throw NativeStorageError("Invalid session platform");
  credentialPath("cookie_" + platform);
  const auto journal = QDir(dataDirectory).filePath("credentials/.native-session-" + platform + ".tmp");
  if (!QFile::exists(journal)) return;
  if (QFileInfo(journal).isSymLink()) throw NativeStorageError("Invalid session recovery journal");
  const auto state = jsonObject(journal, 2 * 1024 * 1024);
  if (state.value("version").toInt() != 1 || !state.contains("cookie") || !state.contains("user") ||
      (!state.value("cookie").isNull() && state.value("cookie").type() != QVariant::String) ||
      (!state.value("user").isNull() && state.value("user").type() != QVariant::String)) {
    throw NativeStorageError("Invalid session recovery journal");
  }
  validateSessionPair(platform, state.value("cookie"), state.value("user"));
  setCredential("cookie_" + platform, state.value("cookie"));
  setCredential("user_" + platform, state.value("user"));
  if (!QFile::remove(journal)) throw NativeStorageError("Cannot finish session recovery");
}

void NativeStorage::saveSession(const QString& platform, const QVariant& cookie, const QVariant& user) {
  if (!platforms.contains(platform) || platform == "local") throw NativeStorageError("Invalid session platform");
  validateSessionPair(platform, cookie, user);
  recoverSession(platform);
  credentialPath("cookie_" + platform);
  const auto journal = QDir(dataDirectory).filePath("credentials/.native-session-" + platform + ".tmp");
  // A protected, replayable journal keeps the two historical credential files
  // consistent when a process is killed between their individual renames.
  NativeAtomicWrite(journal, QJsonDocument::fromVariant(QVariantMap{
      {"version", 1}, {"cookie", cookie}, {"user", user}}).toJson(QJsonDocument::Compact), true);
  recoverSession(platform);
}

QVariantMap NativeStorage::cacheSettings() {
  const auto raw = box("settings").get("offline_cache_settings").toMap();
  return {{"sizeLimitMb", qBound(qint64(128), integer(raw.value("sizeLimitMb"), 1024), qint64(32768))},
          {"wifiOnly", raw.value("wifiOnly").type() != QVariant::Bool || raw.value("wifiOnly").toBool()},
          {"autoRetry", raw.value("autoRetry").type() != QVariant::Bool || raw.value("autoRetry").toBool()},
          {"autoCleanup", raw.value("autoCleanup").type() != QVariant::Bool || raw.value("autoCleanup").toBool()},
          {"offlineMode", raw.value("offlineMode").type() == QVariant::Bool && raw.value("offlineMode").toBool()}};
}

QVariantMap NativeStorage::setCacheSetting(const QString& key, const QVariant& value) {
  auto settings = cacheSettings();
  if (!settings.contains(key)) throw NativeStorageError("Unsupported cache setting");
  if (key == "sizeLimitMb") {
    const auto size = integer(value, -1);
    if (size < 128 || size > 32768) throw NativeStorageError("Invalid cache size");
    settings[key] = size;
  } else {
    if (value.type() != QVariant::Bool) throw NativeStorageError("Invalid cache switch");
    settings[key] = value;
  }
  auto stored = box("settings").get("offline_cache_settings").toMap();
  for (auto item = settings.cbegin(); item != settings.cend(); ++item) stored[item.key()] = item.value();
  box("settings").put("offline_cache_settings", stored);
  return settings;
}

QVariantList NativeStorage::downloadTasks() {
  auto& tasks = box("download_tasks");
  QVariantMap byId;
  QStringList order;
  const auto legacy = tasks.get("tasks").toList();
  for (const auto& raw : legacy) {
    const auto row = downloadTask(raw);
    const auto id = row.value("id").toString();
    if (row.isEmpty() || byId.contains(id)) continue;
    byId[id] = row;
    order.append(id);
  }
  writtenDownloads_.clear();
  for (const auto& key : tasks.keys()) {
    if (!key.startsWith("task/")) continue;
    const auto row = downloadTask(tasks.get(key));
    const auto id = row.value("id").toString();
    if (row.isEmpty()) continue;
    if (!byId.contains(id)) order.append(id);
    byId[id] = row;
    writtenDownloads_[id] = row;
  }
  downloadOrder_ = order;
  downloadOrderKnown_ = true;
  QVariantList result;
  for (const auto& id : order) result.append(byId.value(id));
  return result;
}

void NativeStorage::saveDownloadTasks(const QVariantList& tasks) {
  auto& stored = box("download_tasks");
  if (!downloadOrderKnown_) downloadTasks();
  QStringList order;
  QVariantMap values, nextRows;
  QSet<QString> ids;
  for (const auto& raw : tasks) {
    const auto row = downloadTask(raw);
    const auto id = row.value("id").toString();
    if (row.isEmpty() || ids.contains(id)) throw NativeStorageError("Invalid download rows");
    ids.insert(id);
    order.append(id);
    nextRows[id] = row;
    if (writtenDownloads_.value(id) != QVariant(row)) values["task/" + id] = row;
  }
  QStringList removed;
  for (auto row = writtenDownloads_.cbegin(); row != writtenDownloads_.cend(); ++row) {
    if (!ids.contains(row.key())) removed.append("task/" + row.key());
  }
  const bool orderChanged = order != downloadOrder_;
  if (orderChanged) values["tasks"] = tasks;
  stored.apply(values, removed, orderChanged || !removed.isEmpty());
  writtenDownloads_ = nextRows;
  downloadOrder_ = order;
}

QVariantMap NativeStorage::playbackMemory() {
  auto memory = box("player_memory").get("last_playback").toMap();
  const auto song = NativePlaybackSong(memory.value("currentSong").toMap());
  if (!NativeValidSong(song)) return {};
  memory["currentSong"] = song;
  QVariantList playlist;
  for (const auto& item : memory.value("playlist").toList()) {
    const auto row = NativePlaybackSong(item.toMap());
    if (NativeValidSong(row)) playlist.append(row);
  }
  if (playlist.isEmpty()) playlist.append(song);
  memory["playlist"] = playlist;
  memory["currentIndex"] = qBound(qint64(0), integer(memory.value("currentIndex")), qint64(playlist.size() - 1));
  if (!qualities.contains(memory.value("currentQuality").toString())) memory["currentQuality"] = "low";
  if (!QStringList{"fixed", "highest"}.contains(memory.value("qualityPreference").toString())) memory["qualityPreference"] = "fixed";
  bool valid = false;
  const auto speed = memory.value("playbackSpeed").toDouble(&valid);
  memory["playbackSpeed"] = valid && std::isfinite(speed) ? qBound(0.5, speed, 2.0) : 1.0;
  const auto start = integer(memory.value("abLoopStartMs"), -1);
  const auto end = integer(memory.value("abLoopEndMs"), -1);
  if (start < 0 || end <= start) { memory["abLoopStartMs"] = QVariant(); memory["abLoopEndMs"] = QVariant(); }
  return memory;
}

void NativeStorage::savePlaybackMemory(const QVariantMap& memory) {
  if (memory.isEmpty()) box("player_memory").remove("last_playback");
  else {
    if (!NativeValidSong(memory.value("currentSong").toMap())) throw NativeStorageError("Invalid playback memory");
    box("player_memory").put("last_playback", memory);
  }
}
