#include "native_local_music.h"
#include "native_lyrics.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {
const QStringList audioExtensions = {"mp3", "flac", "wav", "m4a", "aac", "ogg", "opus", "mp4", "alac", "aiff", "aif"};
const QStringList lyricExtensions = {"lrc", "krc", "qrc", "txt"};

QString title(const QVariantMap& track) {
  const auto value = track.value("title").toString().trimmed();
  const QFileInfo path(track.value("path").toString());
  return value.isEmpty() ? (path.completeBaseName().trimmed().isEmpty() ? path.fileName() : path.completeBaseName().trimmed()) : value;
}
QString artist(const QVariantMap& track) {
  const auto value = track.value("artist_name").toString().trimmed();
  return value.isEmpty() ? QStringLiteral("未知歌手") : value;
}
QString album(const QVariantMap& track) {
  const auto value = track.value("album_name").toString().trimmed();
  return value.isEmpty() ? QStringLiteral("未知专辑") : value;
}
bool underRoot(const QString& root, const QString& path) {
  const auto parent = QDir::cleanPath(root).toLower();
  const auto child = QDir::cleanPath(path).toLower();
  return child == parent || child.startsWith(parent + '/');
}
QStringList stems(const QString& raw) {
  const auto stem = raw.trimmed();
  if (stem.isEmpty()) return {};
  QStringList result{stem};
  const int dash = stem.lastIndexOf(" - ");
  if (dash > 0 && dash + 3 < stem.size() && !result.contains(stem.mid(dash + 3).trimmed())) result.append(stem.mid(dash + 3).trimmed());
  return result;
}
QStringList fuzzyStems(QString stem) {
  const QRegularExpression prefix("^\\s*(?:\\[[0-9]{1,3}\\]|[0-9]{1,3}\\s*[-._)]\\s*|[0-9]{1,3}\\s+)");
  for (int attempt = 0; attempt < 2; ++attempt) stem.remove(prefix);
  return stems(stem);
}
QString stemKey(const QString& directory, const QString& stem, const QString& extension) {
  return directory.toLower() + QChar(1) + stem.toLower() + QChar(1) + extension;
}
QStringList lyricCandidates(const QString& path, const QMap<QString, QString>& exact, const QMap<QString, QString>& fuzzy) {
  const QFileInfo file(path);
  const QStringList directories = {file.absolutePath(), QDir(file.absolutePath()).filePath("lyrics")};
  QStringList result;
  for (const auto& extension : lyricExtensions) for (const auto& directory : directories) {
    const auto found = exact.value(QDir(directory).filePath(file.completeBaseName() + '.' + extension).toLower());
    if (!found.isEmpty() && !result.contains(found)) result.append(found);
  }
  for (const auto& extension : lyricExtensions) for (const auto& stem : fuzzyStems(file.completeBaseName())) for (const auto& directory : directories) {
    const auto found = fuzzy.value(stemKey(directory, stem, extension));
    if (!found.isEmpty() && !result.contains(found)) result.append(found);
  }
  return result;
}

QVariantMap walk(const QString& root, const QString& covers, const QVariantMap& known, const QVariantMap& cached) {
  QStringList audio;
  QMap<QString, QString> exact, fuzzy;
  QDirIterator iterator(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    if (QThread::currentThread()->isInterruptionRequested()) return {{"cancelled", true}};
    const auto path = QDir::fromNativeSeparators(iterator.next());
    const auto file = iterator.fileInfo();
    if (file.isDir()) { if (!file.isReadable()) throw NativeStorageError("Cannot read scan directory"); continue; }
    if (!file.isFile()) continue;
    const auto extension = file.suffix().toLower();
    if (audioExtensions.contains(extension)) audio.append(path);
    if (lyricExtensions.contains(extension)) {
      exact[path.toLower()] = path;
      for (const auto& stem : stems(file.completeBaseName())) {
        const auto key = stemKey(file.absolutePath(), stem, extension);
        if (!fuzzy.contains(key) || path < fuzzy.value(key)) fuzzy[key] = path;
      }
    }
  }
  QVariantList entries, changes, lyricChanges;
  QSet<QString> present;
  int parsed = 0, reused = 0, skipped = 0;
  const auto scanned = QDateTime::currentMSecsSinceEpoch();
  for (const auto& path : audio) {
    if (QThread::currentThread()->isInterruptionRequested()) return {{"cancelled", true}};
    const QFileInfo info(path);
    if (!info.exists()) continue;
    const auto previous = known.value(path).toMap();
    const auto mtime = info.lastModified().toMSecsSinceEpoch();
    const bool changed = previous.isEmpty() || previous.value("mtime").toLongLong() != mtime || previous.value("size").toLongLong() != info.size();
    auto entry = previous;
    QVariantMap metadata;
    if (changed) {
      ++parsed;
      if (info.size() > 0) metadata = ReadNativeLocalMetadata(path, covers);
      entry.clear();
      for (const auto& key : QStringList{"title", "artist_name", "album_name", "duration_ms", "track_number", "cover_path"}) entry[key] = metadata.value(key);
      if (entry.value("duration_ms").isNull()) entry["duration_ms"] = 0;
      entry["path"] = path; entry["mtime"] = mtime; entry["size"] = info.size(); entry["scanned_at"] = scanned;
    } else ++reused;
    const auto candidates = lyricCandidates(path, exact, fuzzy);
    const auto stored = cached.value(path).toMap();
    bool reusable = !stored.value("content").toString().isEmpty() && candidates.isEmpty();
    for (const auto& candidate : candidates) {
      const QFileInfo sidecar(candidate);
      if (!stored.value("content").toString().isEmpty() && !previous.value("lyrics_mtime").isNull() &&
          sidecar.lastModified().toMSecsSinceEpoch() == previous.value("lyrics_mtime").toLongLong() &&
          sidecar.size() == previous.value("lyrics_size").toLongLong()) reusable = true;
    }
    entry["lyrics_mtime"] = previous.value("lyrics_mtime");
    entry["lyrics_size"] = previous.value("lyrics_size");
    if (!reusable) {
      QVariantMap payload;
      for (const auto& candidate : candidates) {
        QFile file(candidate);
        if (file.open(QIODevice::ReadOnly) && file.size() <= 4 * 1024 * 1024) payload = DecodeNativeSidecar(file.readAll(), QFileInfo(candidate).suffix());
        if (!payload.isEmpty()) {
          const QFileInfo sidecar(candidate);
          entry["lyrics_mtime"] = sidecar.lastModified().toMSecsSinceEpoch(); entry["lyrics_size"] = sidecar.size();
          break;
        }
        ++skipped;
      }
      if (payload.isEmpty() && !metadata.value("embeddedLyrics").toString().trimmed().isEmpty()) {
        payload = {{"content", metadata.value("embeddedLyrics")}, {"format", "embedded"}};
        entry["lyrics_mtime"] = QVariant(); entry["lyrics_size"] = QVariant();
      }
      if (!payload.isEmpty()) { payload["path"] = path; lyricChanges.append(payload); }
    }
    entries.append(entry);
    present.insert(path);
    if (changed || previous.value("lyrics_mtime") != entry.value("lyrics_mtime") || previous.value("lyrics_size") != entry.value("lyrics_size")) changes.append(entry);
  }
  QStringList removed, removedLyrics;
  for (auto item = known.cbegin(); item != known.cend(); ++item) if (!present.contains(item.key()) && underRoot(root, item.key())) removed.append(item.key());
  for (auto item = cached.cbegin(); item != cached.cend(); ++item) if (!present.contains(item.key()) && underRoot(root, item.key())) removedLyrics.append(item.key());
  std::stable_sort(entries.begin(), entries.end(), [](const QVariant& a, const QVariant& b) { return title(a.toMap()).toLower() < title(b.toMap()).toLower(); });
  return {{"entries", entries}, {"changes", changes}, {"lyrics", lyricChanges}, {"removedPaths", removed}, {"removedLyrics", removedLyrics},
          {"counts", QVariantMap{{"parsed", parsed}, {"reused", reused}, {"removed", removed.size()}, {"skipped", skipped}}}};
}

class ScanWorker : public QThread {
 public:
  std::function<QVariantMap()> scan;
  QVariantMap result;
 protected:
  void run() override {
    try { result = scan(); }
    catch (...) { result = {{"failed", true}}; }
  }
};
}

QVariantMap NativeLocalTrackSong(const QVariantMap& record) {
  const auto hasAlbum = !record.value("album_name").toString().trimmed().isEmpty();
  return NativePlaybackSong({{"id", record.value("path")}, {"platform", "local"}, {"name", title(record)},
      {"artists", QVariantList{QVariantMap{{"id", "local"}, {"name", artist(record)}}}}, {"durationMs", record.value("duration_ms", 0)},
      {"album", hasAlbum ? QVariant(QVariantMap{{"id", ""}, {"name", album(record)}, {"artistName", artist(record)}}) : QVariant()},
      {"trackNumber", record.value("track_number")}, {"availableQualities", QVariantList{QVariantMap{{"level", "low"}, {"bitrate", 0}, {"format", "local"}}}}});
}

NativeLocalMusic::NativeLocalMusic(NativeStorage& storage, QObject* parent) : QObject(parent), storage_(storage) {
  directory_ = storage_.box("local_music").get("scan_root").toString();
  if (directory_.isEmpty()) directory_ = QDir(QDir::homePath()).filePath("Music");
  tracks_ = storage_.database().query("SELECT * FROM local_tracks");
  std::stable_sort(tracks_.begin(), tracks_.end(), [](const QVariant& a, const QVariant& b) { return title(a.toMap()).toLower() < title(b.toMap()).toLower(); });
}

NativeLocalMusic::~NativeLocalMusic() { close(); }

QVariantMap NativeLocalMusic::snapshot() const {
  auto result = counts_;
  for (const auto& key : QStringList{"parsed", "reused", "removed", "skipped"}) if (!result.contains(key)) result[key] = 0;
  result["count"] = tracks_.size(); result["scanning"] = worker_ != nullptr; result["directory"] = directory_; result["error"] = error_;
  return result;
}

void NativeLocalMusic::scan(const QString& directory) {
  if (worker_) throw NativeRequestFailure(QStringLiteral("正在扫描，请等待完成"));
  if (directory.isEmpty() || directory.size() > 4096) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  const auto expanded = directory.startsWith("~/") ? QDir(QDir::homePath()).filePath(directory.mid(2)) : directory;
  if (!QDir::isAbsolutePath(expanded)) throw NativeRequestFailure(QStringLiteral("请输入完整的音乐目录路径"));
  const auto root = QDir(expanded).canonicalPath();
  if (root.isEmpty() || !QFileInfo(root).isReadable()) throw NativeRequestFailure(QStringLiteral("目录不存在或无法访问"));
  if (root == QDir(root).rootPath() || underRoot("/proc", root) || underRoot("/sys", root) || underRoot("/dev", root)) throw NativeRequestFailure(QStringLiteral("请选择音乐目录"));
  QVariantMap known, cached;
  for (const auto& row : storage_.database().query("SELECT * FROM local_tracks")) known[row.toMap().value("path").toString()] = row;
  for (const auto& row : storage_.database().query("SELECT song_id,content,format FROM lyrics_cache WHERE platform='local'")) cached[row.toMap().value("song_id").toString()] = row;
  const auto covers = QDir(storage_.dataDirectory).filePath("local_covers");
  auto* worker = new ScanWorker;
  worker->scan = [root, covers, known, cached]() { return walk(root, covers, known, cached); };
  worker_ = worker;
  error_.clear();
  connect(worker, &QThread::finished, this, [this, worker, root]() {
    if (worker_ != worker) return;
    worker_ = nullptr;
    const auto result = worker->result;
    worker->deleteLater();
    if (result.value("cancelled").toBool()) { emit changed(); return; }
    if (result.value("failed").toBool()) { error_ = QStringLiteral("本地音乐扫描失败，请检查目录后重试"); emit changed(); return; }
    try {
      storage_.database().transaction([&]() {
        for (const auto& raw : result.value("changes").toList()) {
          const auto row = raw.toMap();
          storage_.database().execute("INSERT OR REPLACE INTO local_tracks(path,mtime,size,title,artist_name,album_name,duration_ms,track_number,cover_path,scanned_at,lyrics_mtime,lyrics_size) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
              {row.value("path"), row.value("mtime"), row.value("size"), row.value("title"), row.value("artist_name"), row.value("album_name"), row.value("duration_ms", 0),
               row.value("track_number"), row.value("cover_path"), row.value("scanned_at"), row.value("lyrics_mtime"), row.value("lyrics_size")});
        }
        for (const auto& raw : result.value("lyrics").toList()) {
          const auto row = raw.toMap();
          storage_.database().execute("INSERT OR REPLACE INTO lyrics_cache(song_id,platform,content,format,synced_at) VALUES(?,'local',?,?,?)",
              {row.value("path"), row.value("content"), row.value("format"), QDateTime::currentMSecsSinceEpoch()});
        }
        for (const auto& path : result.value("removedPaths").toStringList()) storage_.database().execute("DELETE FROM local_tracks WHERE path=?", {path});
        for (const auto& path : result.value("removedLyrics").toStringList()) storage_.database().execute("DELETE FROM lyrics_cache WHERE platform='local' AND song_id=?", {path});
      });
      tracks_ = result.value("entries").toList(); counts_ = result.value("counts").toMap(); directory_ = root;
      try { storage_.box("local_music").put("scan_root", root); } catch (const NativeStorageError&) { qWarning("MCONNECT_NATIVE_SCAN_ROOT_STORE_FAILED"); }
    } catch (const NativeStorageError&) { error_ = QStringLiteral("保存本地音乐索引失败，请检查存储空间"); }
    emit changed();
  });
  worker->start();
  emit changed();
}

QVariantList NativeLocalMusic::groupRecords(const QString& view) const {
  if (!QStringList{"albums", "artists", "folders"}.contains(view)) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  QMap<QString, QVariantList> buckets;
  for (const auto& raw : tracks_) {
    const auto track = raw.toMap();
    const auto key = view == "albums" ? album(track) : view == "artists" ? artist(track) : QFileInfo(track.value("path").toString()).absolutePath();
    buckets[key].append(track);
  }
  QVariantList result;
  for (auto item = buckets.cbegin(); item != buckets.cend(); ++item) {
    auto tracks = item.value();
    std::stable_sort(tracks.begin(), tracks.end(), [&](const QVariant& left, const QVariant& right) {
      const auto a = left.toMap(), b = right.toMap();
      if (view == "albums") {
        if (a.value("track_number").isNull() != b.value("track_number").isNull()) return !a.value("track_number").isNull();
        if (!a.value("track_number").isNull() && a.value("track_number") != b.value("track_number")) return a.value("track_number").toInt() < b.value("track_number").toInt();
      }
      return (view == "folders" ? QFileInfo(a.value("path").toString()).fileName() : title(a)).toLower() <
             (view == "folders" ? QFileInfo(b.value("path").toString()).fileName() : title(b)).toLower();
    });
    QSet<QString> names;
    for (const auto& track : tracks) {
      if (view == "albums") names.insert(artist(track.toMap()));
      if (view == "artists" && !track.toMap().value("album_name").toString().trimmed().isEmpty()) names.insert(album(track.toMap()));
    }
    const auto subtitle = view == "folders" ? item.key() : names.size() == 1 ? *names.cbegin() :
        QString::number(names.size()) + (view == "albums" ? QStringLiteral(" 位歌手") : QStringLiteral(" 张专辑"));
    result.append(QVariantMap{{"id", item.key()}, {"title", view == "folders" ? QFileInfo(item.key()).fileName() : item.key()},
        {"subtitle", subtitle}, {"count", tracks.size()}, {"tracks", tracks}});
  }
  std::stable_sort(result.begin(), result.end(), [&](const QVariant& left, const QVariant& right) {
    const auto a = left.toMap().value("id").toString(), b = right.toMap().value("id").toString();
    const bool aUnknown = a == QStringLiteral("未知专辑") || a == QStringLiteral("未知歌手");
    const bool bUnknown = b == QStringLiteral("未知专辑") || b == QStringLiteral("未知歌手");
    if (view != "folders" && aUnknown != bUnknown) return !aUnknown;
    return a.toLower() < b.toLower();
  });
  return result;
}

QVariantList NativeLocalMusic::groups(const QVariantMap& arguments) const {
  auto result = groupRecords(NativeIdentifier(arguments.value("view")));
  for (auto& raw : result) { auto row = raw.toMap(); row.remove("tracks"); raw = row; }
  return result;
}

QVariantList NativeLocalMusic::songs(const QVariantMap& arguments) const {
  const auto view = arguments.value("view").isNull() ? QString("songs") : arguments.value("view").toString();
  QVariantList tracks = tracks_;
  if (view != "songs") {
    const auto id = NativeIdentifier(arguments.value("id"), 4096);
    tracks.clear();
    bool found = false;
    for (const auto& group : groupRecords(view)) if (group.toMap().value("id") == id) { found = true; tracks = group.toMap().value("tracks").toList(); }
    if (!found) throw NativeRequestFailure(QStringLiteral("本地分组已变化，请返回刷新"));
  }
  const auto keyword = arguments.value("keyword").isNull() ? QVariant("") : arguments.value("keyword");
  if (keyword.type() != QVariant::String || keyword.toString().size() > 256) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  QVariantList result;
  for (const auto& track : tracks) {
    const auto song = NativeLocalTrackSong(track.toMap());
    const auto text = song.value("name").toString() + ' ' + NativeArtistNames(song) + ' ' + song.value("album").toMap().value("name").toString();
    if (keyword.toString().isEmpty() || text.contains(keyword.toString(), Qt::CaseInsensitive)) result.append(song);
  }
  return result;
}

QString NativeLocalMusic::cover(const QString& song) const {
  for (const auto& row : tracks_) if (row.toMap().value("path") == song) return row.toMap().value("cover_path").toString();
  return {};
}

void NativeLocalMusic::close() {
  if (!worker_) return;
  auto* worker = worker_;
  worker_ = nullptr;
  worker->disconnect(this);
  worker->requestInterruption();
  worker->wait();
  delete worker;
}
