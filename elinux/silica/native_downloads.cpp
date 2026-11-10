#include "native_downloads.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>
#include <QTemporaryFile>
#include <QUrl>
#include <algorithm>

namespace {
constexpr qint64 reserveBytes = 32 * 1024 * 1024;

QString stamp() { return QDateTime::currentDateTime().toString("yyyy-MM-dd'T'HH:mm:ss.zzz"); }
QString pathKey(const QString& path) { return QDir::cleanPath(QDir::fromNativeSeparators(path)); }
qint64 fileBytes(const QString& path) { const QFileInfo file(path); return file.isFile() ? file.size() : 0; }
bool filePath(const QString& path) { return !path.isEmpty() && !path.startsWith("content:") && !path.startsWith("file:") && QDir::isAbsolutePath(path); }
bool mediaUrl(const QUrl& url) { return url.isValid() && QStringList{"http", "https"}.contains(url.scheme()) && !url.host().isEmpty() && url.userInfo().isEmpty() && url.toString().size() <= 16384; }
bool biliMedia(const QUrl& url) {
  for (const auto& domain : QStringList{"bilivideo.com", "bilivideo.cn", "bilibili.com"}) if (url.host() == domain || url.host().endsWith('.' + domain)) return true;
  return false;
}
QString taskId(const QVariantMap& song, int quality, bool cache) {
  return song.value("platform").toString() + '_' + song.value("id").toString() + '_' + NativeQualityName(quality) + (cache ? "_cache" : "");
}
}

bool NativeWifiDefaultRoute(const QString& ipv4Path, const QString& ipv6Path, const QString& networkRoot) {
  QList<QPair<QString, quint64>> routes;
  QFile ipv4(ipv4Path);
  if (!ipv4.open(QIODevice::ReadOnly)) return false;
  const auto whitespace = QRegularExpression("\\s+");
  const auto lines = QString::fromLatin1(ipv4.readAll()).split('\n');
  for (const auto& line : lines.mid(1)) {
    const auto fields = line.trimmed().split(whitespace);
    if (fields.size() >= 8 && fields[1] == "00000000" && (fields[3].toUInt(nullptr, 16) & 1)) {
      routes.append({fields[0], fields[6].toULongLong()});
    }
  }
  if (routes.isEmpty()) {
    QFile ipv6(ipv6Path);
    if (ipv6.open(QIODevice::ReadOnly)) for (const auto& line : QString::fromLatin1(ipv6.readAll()).split('\n')) {
      const auto fields = line.trimmed().split(whitespace);
      if (fields.size() < 10 || fields[0] != QString(32, '0') || fields[1] != "00" || fields[9] == "lo") continue;
      const auto flags = fields[8].toUInt(nullptr, 16);
      if ((flags & 1) && !(flags & 0x200)) routes.append({fields[9], fields[5].toULongLong(nullptr, 16)});
    }
  }
  if (routes.isEmpty()) return false;
  std::stable_sort(routes.begin(), routes.end(), [](const QPair<QString, quint64>& a, const QPair<QString, quint64>& b) { return a.second < b.second; });
  return QDir(QDir(networkRoot).filePath(routes.first().first + "/wireless")).exists();
}

QString NativeDownloadFileName(const QVariantMap& song, int quality) {
  auto name = NativeArtistNames(song) + " - " + song.value("name").toString();
  name.replace(QRegularExpression("[\\\\/:*?\"<>|\\x00-\\x1f]"), "_");
  const auto extension = song.value("platform") == "bilibili" ? "m4a" : NativeQualityLossless(quality) ? "flac" : "mp3";
  const auto suffix = " [" + NativeQualityName(quality) + "]." + extension;
  while ((name + suffix).toUtf8().size() > 240 && !name.isEmpty()) name.chop(name.at(name.size() - 1).isLowSurrogate() ? 2 : 1);
  return name + suffix;
}

struct NativeDownloads::Job {
  QString id, platform, path, failure;
  quint64 epoch = 0;
  QUrl url;
  QFile file;
  QPointer<QNetworkReply> reply;
  QTimer* timeout = nullptr;
  qint64 offset = 0, expected = -1, received = 0;
  int redirects = 0;
  bool prepared = false, done = false;
};

NativeDownloads::NativeDownloads(NativeStorage& storage, NativeMusicServices& music, QObject* parent,
                                 WifiProbe wifi, SpaceProbe space, bool resumeStored)
    : QObject(parent), storage_(storage), music_(music), wifi_(wifi ? std::move(wifi) : WifiProbe([]() { return NativeWifiDefaultRoute(); })),
      space_(space ? std::move(space) : SpaceProbe([](const QString& path) { const QStorageInfo volume(path); return volume.isValid() && volume.isReady() ? volume.bytesAvailable() : qint64(-1); })) {
  const auto restored = storage_.downloadTasks();
  QSet<QString> reservedIds;
  for (const auto& raw : restored) reservedIds.insert(raw.toMap().value("id").toString());
  for (const auto& raw : restored) {
    auto task = raw.toMap();
    auto id = task.value("id").toString();
    const auto song = task.value("song").toMap();
    const auto quality = NativeQualityIndex(task.value("quality"));
    if (task.value("isOfflineCache").toBool() && id == taskId(song, quality, false)) {
      const auto canonical = taskId(song, quality, true);
      id = canonical;
      int collision = 0;
      while (reservedIds.contains(id)) id = canonical + "_legacy_" + QString::number(++collision);
      reservedIds.insert(id);
      task["id"] = id;
    }
    const auto status = task.value("status").toString();
    if (status == "completed" && (task.value("filePath").toString().isEmpty() ||
        (filePath(task.value("filePath").toString()) && !QFileInfo(task.value("filePath").toString()).isFile()))) continue;
    if (QStringList{"waiting", "downloading", "paused"}.contains(status)) {
      task["status"] = "paused";
      if (resumeStored && !storage_.cacheSettings().value("offlineMode").toBool()) {
        task["status"] = "waiting";
        waiting_.append(id);
      }
    }
    if (tasks_.contains(id)) continue;
    tasks_[id] = task;
    order_.append(id);
  }
  flushTimer_.setSingleShot(true);
  flushTimer_.setInterval(2000);
  connect(&flushTimer_, &QTimer::timeout, this, [this]() { persist(); });
  connect(&connectionTimer_, &QTimer::timeout, this, [this]() { pump(); });
  connectionTimer_.start(5000);
  connect(&music_, &NativeMusicServices::accountsChanged, this, [this]() {
    for (const auto& job : jobs_.values()) if (job->epoch != music_.epoch(job->platform)) finish(job, "auth");
  });
  persist();
  QTimer::singleShot(0, this, [this]() { pump(); });
}

NativeDownloads::~NativeDownloads() { close(); }

void NativeDownloads::persist(bool immediate) {
  emit changed();
  if (!immediate) { if (!flushTimer_.isActive()) flushTimer_.start(); return; }
  flushTimer_.stop();
  QVariantList rows;
  for (const auto& id : order_) rows.append(tasks_.value(id));
  try { storage_.saveDownloadTasks(rows); storageFailed_ = false; }
  catch (const NativeStorageError&) {
    storageFailed_ = true;
    paused_ = true;
    qWarning("MCONNECT_NATIVE_DOWNLOAD_STORE_FAILED");
    QTimer::singleShot(0, this, [this]() { for (const auto& id : jobs_.keys()) stop(id); });
  }
}

QString NativeDownloads::blocked() const {
  if (storageFailed_) return QStringLiteral("保存下载状态失败，请检查存储空间");
  if (paused_) return QStringLiteral("下载队列已暂停");
  const auto policy = storage_.cacheSettings();
  for (const auto& id : waiting_) if (tasks_.value(id).value("isOfflineCache").toBool()) {
    if (policy.value("offlineMode").toBool()) return QStringLiteral("离线模式已开启，缓存任务不会自动开始");
    if (policy.value("wifiOnly").toBool() && !wifi_()) return QStringLiteral("已开启「仅 Wi-Fi 下载」，等待 Wi-Fi 连接");
  }
  return {};
}

QVariantMap NativeDownloads::snapshot() const {
  QVariantList rows;
  for (const auto& id : order_) {
    const auto task = tasks_.value(id);
    const auto song = task.value("song").toMap();
    rows.append(QVariantMap{{"id", id}, {"title", song.value("name")}, {"artist", NativeArtistNames(song)},
        {"platform", song.value("platform")}, {"quality", NativeQualityLabel(NativeQualityIndex(task.value("quality")), song.value("platform").toString())},
        {"status", task.value("status")}, {"progress", task.value("progress", 0.0)}, {"bytes", task.value("downloadedBytes", 0)},
        {"totalBytes", task.value("totalBytes").isNull() ? QVariant(0) : task.value("totalBytes")},
        {"cache", task.value("isOfflineCache", false)}, {"failure", task.value("failureKind").isNull() ? QVariant("") : task.value("failureKind")}});
  }
  return {{"paused", paused_}, {"blocked", blocked()}, {"tasks", rows}};
}

void NativeDownloads::enqueue(const QVariantMap& song, int quality, bool cache) {
  if (closed_) throw NativeRequestFailure(QStringLiteral("下载服务已关闭"));
  if (!NativeValidSong(song) || quality < 0 || quality > 7) throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  if (song.value("platform") == "local") throw NativeRequestFailure(QStringLiteral("本地歌曲无需下载"));
  if (!cache && song.value("platform") != "bilibili") {
    const auto vip = music_.vipLevel(song.value("platform").toString());
    if ((vip == 0 && quality != 0) || (vip == 1 && NativeQualityLossless(quality))) {
      throw NativeRequestFailure(QStringLiteral("当前账号不支持该音质，请选择标准音质或登录会员账号"));
    }
  }
  const auto id = taskId(song, quality, cache);
  if (tasks_.contains(id)) {
    if (tasks_.value(id).value("status") == "failed") control({{"action", "resume"}, {"id", id}});
    return;
  }
  if (cache && tasks_.value(taskId(song, quality, false)).value("status") == "completed") return;
  tasks_[id] = {{"id", id}, {"song", NativeStoredPlaybackSong(song)}, {"quality", NativeQualityName(quality)},
      {"status", "waiting"}, {"progress", 0.0}, {"downloadedBytes", qint64(0)}, {"totalBytes", QVariant()},
      {"filePath", QVariant()}, {"error", QVariant()}, {"createdAt", stamp()}, {"completedAt", QVariant()},
      {"isOfflineCache", cache}, {"failureKind", QVariant()}, {"lastAccessedAt", QVariant()}};
  order_.append(id);
  waiting_.append(id);
  persist();
  if (storageFailed_) throw NativeStorageError("Cannot save download queue");
  QTimer::singleShot(0, this, [this]() { pump(); });
}

QString NativeDownloads::directory() const {
  const auto saved = storage_.box("download_settings").get("custom_root_path").toString();
  const auto root = filePath(saved) ? saved : saved.trimmed();
  return filePath(root) && QDir::cleanPath(root) != QDir(root).rootPath()
      ? root : QDir(storage_.dataDirectory).filePath("downloads");
}

void NativeDownloads::setDirectory(const QString& selected) {
  if (!filePath(selected) || selected.size() > 4096) {
    throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  }
  const QFileInfo folder(selected);
  const auto path = folder.canonicalFilePath();
  if (!folder.isDir() || path.isEmpty() || path == QDir(path).rootPath()) {
    throw NativeRequestFailure(QStringLiteral("请选择可访问的文件夹"));
  }
  QTemporaryFile probe(QDir(path).filePath(".mconnect-write-test-XXXXXX"));
  if (!probe.open() || probe.write("mconnect") != 8 || !probe.flush() || !probe.remove()) {
    throw NativeRequestFailure(QStringLiteral("无法写入所选文件夹，请选择有访问权限的目录"));
  }
  storage_.box("download_settings").put("custom_root_path", path);
}

QString NativeDownloads::pathFor(const QVariantMap& task) const {
  const auto recorded = task.value("filePath").toString();
  if (filePath(recorded) && QFileInfo(recorded).isFile()) return recorded;
  const auto song = task.value("song").toMap();
  const int quality = NativeQualityIndex(task.value("quality"));
  return QDir(directory()).filePath(song.value("platform").toString() + '/' + (NativeQualityLossless(quality) ? "flac/" : "mp3/") + NativeDownloadFileName(song, quality));
}

bool NativeDownloads::sharedPath(const QString& path, const QString& except) const {
  for (const auto& id : order_) {
    if (id != except && tasks_.value(id).value("status") == "completed" &&
        pathKey(tasks_.value(id).value("filePath").toString()) == pathKey(path)) return true;
  }
  return false;
}

void NativeDownloads::pump() {
  if (closed_ || paused_ || pumping_) return;
  pumping_ = true;
  const auto policy = storage_.cacheSettings();
  for (const auto& id : QStringList(waiting_)) {
    if (jobs_.size() >= 3 || paused_ || closed_) break;
    if (!tasks_.contains(id) || tasks_.value(id).value("status") != "waiting") { waiting_.removeAll(id); continue; }
    if (tasks_.value(id).value("isOfflineCache").toBool() &&
        (policy.value("offlineMode").toBool() || (policy.value("wifiOnly").toBool() && !wifi_()))) continue;
    start(id);
  }
  pumping_ = false;
  emit changed();
}

void NativeDownloads::start(const QString& id) {
  const auto task = tasks_.value(id);
  const auto song = task.value("song").toMap();
  const int quality = NativeQualityIndex(task.value("quality"));
  for (const auto& otherId : order_) {
    const auto other = tasks_.value(otherId);
    if (otherId != id && other.value("status") == "completed" && other.value("quality") == task.value("quality") &&
        NativeSongKey(other.value("song").toMap()) == NativeSongKey(song) && fileBytes(other.value("filePath").toString()) > 0) {
      auto completed = task;
      for (const auto& name : QStringList{"filePath", "downloadedBytes", "totalBytes"}) completed[name] = other.value(name);
      completed["status"] = "completed"; completed["progress"] = 1.0; completed["completedAt"] = stamp();
      tasks_[id] = completed;
      waiting_.removeAll(id);
      persist();
      return;
    }
  }
  auto job = std::make_shared<Job>();
  job->id = id;
  job->platform = song.value("platform").toString();
  job->epoch = music_.epoch(job->platform);
  job->path = pathFor(task);
  const auto existingBytes = fileBytes(job->path);
  const bool recordedOwner = pathKey(task.value("filePath").toString()) == pathKey(job->path) && existingBytes == task.value("downloadedBytes").toLongLong();
  if (sharedPath(job->path, id) || (QFileInfo(job->path).isFile() && !recordedOwner)) {
    const QFileInfo path(job->path);
    const auto suffix = QString::fromLatin1(QCryptographicHash::hash(NativeSongKey(song).toUtf8(), QCryptographicHash::Sha256).toHex().left(8));
    job->path = QDir(path.absolutePath()).filePath(path.completeBaseName() + " [" + suffix + "]." + path.suffix());
    int collision = 0;
    while (QFileInfo::exists(job->path)) {
      job->path = QDir(path.absolutePath()).filePath(path.completeBaseName() + " [" + suffix + '-' + QString::number(++collision) + "]." + path.suffix());
    }
  }
  for (const auto& running : jobs_) if (pathKey(running->path) == pathKey(job->path)) return;
  waiting_.removeAll(id);
  jobs_[id] = job;
  tasks_[id]["status"] = "downloading";
  tasks_[id]["error"] = QVariant(); tasks_[id]["failureKind"] = QVariant();
  persist();
  if (storageFailed_) { stop(id); return; }
  try {
    const auto response = music_.request(job->platform, "stream", {{"id", song.value("id")}, {"quality", quality}});
    if (job->done || closed_) return;
    if (job->epoch != music_.epoch(job->platform)) { finish(job, "auth"); return; }
    job->url = QUrl(response.value("url").toString(), QUrl::StrictMode);
    if (!mediaUrl(job->url)) { finish(job, "network"); return; }
    if (!QDir().mkpath(QFileInfo(job->path).absolutePath())) { finish(job, "storagePermission"); return; }
    const auto existing = fileBytes(job->path);
    const auto recorded = task.value("downloadedBytes").toLongLong();
    if (existing > 0 && existing == recorded && (task.value("totalBytes").isNull() || existing < task.value("totalBytes").toLongLong())) job->offset = existing;
    job->received = job->offset;
    send(job);
  } catch (const NativeRequestFailure& error) { if (!job->done) finish(job, error.code.isEmpty() ? QString("unknown") : error.code); }
  catch (const NativeStorageError&) { if (!job->done) finish(job, "disk"); }
}

void NativeDownloads::send(const std::shared_ptr<Job>& job) {
  QNetworkRequest request(job->url);
  request.setRawHeader("User-Agent", "Mozilla/5.0");
  request.setRawHeader("Accept-Encoding", "identity");
  if (biliMedia(job->url)) request.setRawHeader("Referer", "https://www.bilibili.com/");
  if (job->offset > 0) request.setRawHeader("Range", "bytes=" + QByteArray::number(job->offset) + '-');
  request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
  request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
#endif
  auto* reply = network_.get(request);
  job->reply = reply;
  reply->setReadBufferSize(128 * 1024);
  auto* timeout = new QTimer(reply);
  job->timeout = timeout;
  timeout->setSingleShot(true);
  connect(timeout, &QTimer::timeout, this, [this, job]() { finish(job, "network"); });
  timeout->start(30000);
  connect(reply, &QNetworkReply::metaDataChanged, this, [this, job]() { prepare(job); });
  connect(reply, &QNetworkReply::readyRead, this, [this, job]() { read(job); });
  connect(reply, &QNetworkReply::finished, this, [this, job, reply]() {
    if (job->done || job->reply != reply) return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (status >= 300 && status < 400 && !target.isEmpty()) {
      const auto next = job->url.resolved(target);
      if (++job->redirects > 5 || !mediaUrl(next) || (job->url.scheme() == "https" && next.scheme() != "https")) { finish(job, "network"); return; }
      reply->disconnect(this); reply->deleteLater();
      job->url = next;
      job->prepared = false;
      send(job);
      return;
    }
    read(job);
    if (job->done) return;
    if (status == 401 || status == 403) { finish(job, "auth"); return; }
    if (status != 200 && status != 206) { finish(job, status == 404 ? "notFound" : "network"); return; }
    if (reply->error() != QNetworkReply::NoError || !job->prepared) { finish(job, "network"); return; }
    finish(job);
  });
}

bool NativeDownloads::prepare(const std::shared_ptr<Job>& job) {
  if (job->done || !job->reply) return false;
  if (job->prepared) return true;
  const int status = job->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (status != 200 && status != 206) return false;
  bool lengthValid = false;
  const auto length = job->reply->rawHeader("Content-Length").toLongLong(&lengthValid);
  if (lengthValid && (length < 0 || length > (qint64(1) << 40))) { finish(job, "network"); return false; }
  if (status == 206) {
    const auto range = QRegularExpression("^bytes ([0-9]+)-([0-9]+)/([0-9]+|\\*)$").match(QString::fromLatin1(job->reply->rawHeader("Content-Range")));
    if (!range.hasMatch() || range.captured(1).toLongLong() != job->offset || range.captured(2).toLongLong() < job->offset ||
        (lengthValid && range.captured(2).toLongLong() - job->offset + 1 != length) ||
        (range.captured(3) != "*" && range.captured(3).toLongLong() != range.captured(2).toLongLong() + 1)) {
      finish(job, "network"); return false;
    }
  } else job->offset = 0;
  job->expected = lengthValid ? job->offset + length : -1;
  const auto free = space_(QFileInfo(job->path).absolutePath());
  if (free >= 0 && free < (lengthValid ? length : 0) + reserveBytes) { finish(job, "disk"); return false; }
  job->file.setFileName(job->path);
  if (!job->file.open(QIODevice::WriteOnly | (job->offset > 0 ? QIODevice::Append : QIODevice::Truncate))) { finish(job, "storagePermission"); return false; }
  job->received = job->offset;
  job->prepared = true;
  tasks_[job->id]["filePath"] = job->path;
  tasks_[job->id]["downloadedBytes"] = job->received;
  tasks_[job->id]["totalBytes"] = job->expected >= 0 ? QVariant(job->expected) : QVariant();
  job->timeout->start(600000);
  persist();
  return !job->done;
}

void NativeDownloads::read(const std::shared_ptr<Job>& job) {
  if (!prepare(job)) {
    if (!job->done && job->reply && job->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 300) job->reply->readAll();
    return;
  }
  while (job->reply && job->reply->bytesAvailable() > 0) {
    const auto bytes = job->reply->read(64 * 1024);
    if (job->file.write(bytes) != bytes.size()) { finish(job, "disk"); return; }
    job->received += bytes.size();
  }
  if (job->timeout) job->timeout->start(600000);
  auto& task = tasks_[job->id];
  task["downloadedBytes"] = job->received;
  task["progress"] = job->expected > 0 ? qBound(0.0, double(job->received) / job->expected, 1.0) : 0.0;
  persist(false);
}

void NativeDownloads::finish(const std::shared_ptr<Job>& job, const QString& failure) {
  if (job->done) return;
  job->done = true;
  if (job->reply) { job->reply->disconnect(this); job->reply->abort(); job->reply->deleteLater(); }
  QString kind = failure;
  if (job->file.isOpen()) { if (!job->file.flush() && kind.isEmpty()) kind = "disk"; job->file.close(); }
  const auto bytes = fileBytes(job->path);
  if (kind.isEmpty() && (bytes <= 0 || (job->expected >= 0 && bytes != job->expected))) kind = "network";
  jobs_.remove(job->id);
  if (!tasks_.contains(job->id)) return;
  auto& task = tasks_[job->id];
  if (QFileInfo(job->path).isFile()) task["filePath"] = job->path;
  task["downloadedBytes"] = bytes;
  if (kind.isEmpty()) {
    task["status"] = "completed"; task["totalBytes"] = bytes; task["progress"] = 1.0; task["completedAt"] = stamp();
    task["error"] = QVariant(); task["failureKind"] = QVariant(); retries_.remove(job->id);
  } else {
    task["status"] = "failed"; task["failureKind"] = kind; task["error"] = QStringLiteral("下载失败，请检查网络或存储空间后重试");
    task["progress"] = task.value("totalBytes").toLongLong() > 0 ? qBound(0.0, double(bytes) / task.value("totalBytes").toLongLong(), 1.0) : 0.0;
  }
  const bool cache = task.value("isOfflineCache").toBool();
  persist();
  if (!kind.isEmpty()) scheduleRetry(job->id, kind);
  if (kind.isEmpty() && cache && storage_.cacheSettings().value("autoCleanup").toBool()) cleanup();
  QTimer::singleShot(0, this, [this]() { pump(); });
}

void NativeDownloads::scheduleRetry(const QString& id, const QString& failure) {
  if (closed_ || !QStringList{"network", "disk"}.contains(failure) ||
      !storage_.cacheSettings().value("autoRetry").toBool() || retries_.value(id) >= 2) return;
  const int attempt = retries_.value(id) + 1;
  retries_[id] = attempt;
  if (retryTimers_.contains(id)) { retryTimers_.take(id)->deleteLater(); }
  auto* timer = new QTimer(this);
  timer->setSingleShot(true);
  retryTimers_[id] = timer;
  connect(timer, &QTimer::timeout, this, [this, id, timer]() {
    retryTimers_.remove(id); timer->deleteLater();
    if (closed_ || !tasks_.contains(id) || tasks_.value(id).value("status") != "failed" ||
        storage_.cacheSettings().value("offlineMode").toBool() || !storage_.cacheSettings().value("autoRetry").toBool()) return;
    tasks_[id]["status"] = "waiting";
    waiting_.append(id);
    persist();
    pump();
  });
  timer->start(3000 * attempt);
}

void NativeDownloads::stop(const QString& id, bool remove) {
  waiting_.removeAll(id);
  if (retryTimers_.contains(id)) { auto* timer = retryTimers_.take(id); timer->stop(); timer->deleteLater(); }
  retries_.remove(id);
  const auto job = jobs_.take(id);
  if (job) {
    job->done = true;
    if (job->reply) { job->reply->disconnect(this); job->reply->abort(); job->reply->deleteLater(); }
    if (job->file.isOpen()) { job->file.flush(); job->file.close(); }
    if (job->prepared) {
      tasks_[id]["filePath"] = job->path;
      tasks_[id]["downloadedBytes"] = fileBytes(job->path);
    }
  }
  if (!tasks_.contains(id)) return;
  if (remove) {
    const auto path = tasks_.value(id).value("filePath").toString();
    const bool owned = filePath(path) && fileBytes(path) == tasks_.value(id).value("downloadedBytes").toLongLong();
    if (owned && !sharedPath(path, id) && QFileInfo(path).isFile() && !QFile::remove(path)) throw NativeRequestFailure(QStringLiteral("无法删除下载文件，请检查目录权限"));
    tasks_.remove(id); order_.removeAll(id);
  } else tasks_[id]["status"] = "paused";
  persist();
  QTimer::singleShot(0, this, [this]() { pump(); });
}

void NativeDownloads::control(const QVariantMap& arguments) {
  const auto action = NativeIdentifier(arguments.value("action"));
  if (action == "pauseQueue") { paused_ = true; for (const auto& id : jobs_.keys()) stop(id); persist(); return; }
  if (action == "resumeQueue") {
    paused_ = false;
    for (const auto& id : order_) if (tasks_.value(id).value("status") == "paused") { tasks_[id]["status"] = "waiting"; if (!waiting_.contains(id)) waiting_.append(id); }
    persist(); pump(); return;
  }
  const auto id = NativeIdentifier(arguments.value("id"), 8192);
  if (!tasks_.contains(id)) throw NativeRequestFailure(QStringLiteral("下载任务不存在，请刷新"));
  const auto status = tasks_.value(id).value("status").toString();
  if (action == "pause") { if (status != "completed") stop(id); return; }
  if (action == "cancel") { if (status == "completed") throw NativeRequestFailure(QStringLiteral("请求参数无效"), true); stop(id, true); return; }
  if (action != "resume") throw NativeRequestFailure(QStringLiteral("请求参数无效"), true);
  if (status != "paused" && status != "failed" && status != "waiting") return;
  if (retryTimers_.contains(id)) { auto* timer = retryTimers_.take(id); timer->stop(); timer->deleteLater(); }
  retries_.remove(id);
  tasks_[id]["status"] = "waiting"; tasks_[id]["error"] = QVariant(); tasks_[id]["failureKind"] = QVariant();
  if (!waiting_.contains(id)) waiting_.append(id);
  persist(); pump();
}

QVariantMap NativeDownloads::taskSong(const QString& id) const {
  if (!tasks_.contains(id) || tasks_.value(id).value("status") != "completed") throw NativeRequestFailure(QStringLiteral("下载尚未完成，请刷新列表"));
  return tasks_.value(id).value("song").toMap();
}

QString NativeDownloads::localFile(const QVariantMap& song, int quality) {
  QStringList candidates;
  for (const auto& id : order_) {
    const auto task = tasks_.value(id);
    if (task.value("status") == "completed" && NativeSongKey(task.value("song").toMap()) == NativeSongKey(song) &&
        (quality < 0 || task.value("quality") == NativeQualityName(quality))) candidates.append(id);
  }
  std::stable_sort(candidates.begin(), candidates.end(), [this](const QString& a, const QString& b) {
    const auto left = tasks_.value(a), right = tasks_.value(b);
    if (left.value("isOfflineCache").toBool() != right.value("isOfflineCache").toBool()) return !left.value("isOfflineCache").toBool();
    return NativeQualityIndex(left.value("quality")) > NativeQualityIndex(right.value("quality"));
  });
  for (const auto& id : candidates) {
    const auto path = tasks_.value(id).value("filePath").toString();
    if (!filePath(path) || !QFileInfo(path).isFile()) continue;
    const auto previous = QDateTime::fromString(tasks_.value(id).value("lastAccessedAt").toString(), Qt::ISODate);
    if (!previous.isValid() || previous.msecsTo(QDateTime::currentDateTime()) >= 60000) { tasks_[id]["lastAccessedAt"] = stamp(); persist(); }
    return path;
  }
  return {};
}

void NativeDownloads::cleanup() {
  const auto limit = storage_.cacheSettings().value("sizeLimitMb").toLongLong() * 1024 * 1024;
  qint64 total = 0;
  QStringList candidates;
  QSet<QString> paths;
  for (const auto& id : order_) {
    const auto task = tasks_.value(id);
    if (task.value("status") != "completed" || !task.value("isOfflineCache").toBool()) continue;
    const auto path = task.value("filePath").toString();
    if (!filePath(path)) continue;
    if (!paths.contains(pathKey(path))) { total += fileBytes(path); paths.insert(pathKey(path)); }
    candidates.append(id);
  }
  const auto recency = [this](const QString& id) {
    const auto task = tasks_.value(id);
    for (const auto& name : QStringList{"lastAccessedAt", "completedAt", "createdAt"}) {
      const auto date = QDateTime::fromString(task.value(name).toString(), Qt::ISODate);
      if (date.isValid()) return date.toMSecsSinceEpoch();
    }
    return qint64(0);
  };
  std::stable_sort(candidates.begin(), candidates.end(), [&](const QString& a, const QString& b) { return recency(a) < recency(b); });
  for (const auto& id : candidates) {
    if (total <= limit) break;
    const auto path = tasks_.value(id).value("filePath").toString();
    if (sharedPath(path, id)) { tasks_.remove(id); order_.removeAll(id); continue; }
    const auto bytes = fileBytes(path);
    if (QFileInfo(path).isFile() && !QFile::remove(path)) continue;
    tasks_.remove(id); order_.removeAll(id); total -= bytes;
  }
  persist();
}

void NativeDownloads::policyChanged() {
  pump();
  if (storage_.cacheSettings().value("autoCleanup").toBool()) cleanup();
  emit changed();
}

void NativeDownloads::close() {
  if (closed_) return;
  closed_ = true;
  paused_ = true;
  connectionTimer_.stop();
  for (auto* timer : retryTimers_) timer->stop();
  for (const auto& id : jobs_.keys()) stop(id);
  persist();
  flushTimer_.stop();
}
