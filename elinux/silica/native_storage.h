#pragma once

#include <QFile>
#include <QHash>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>

struct sqlite3;

class NativeStorageError : public std::runtime_error {
 public:
  explicit NativeStorageError(const char* message) : std::runtime_error(message) {}
};

QString NativeDataDirectory();
QString NativeCacheDirectory();
void NativeAtomicWrite(const QString& path, const QByteArray& bytes, bool privateFile = false);

// Uses the same operating-system byte-range lock as dart:io's File.lock().
class NativeFileLock {
 public:
  explicit NativeFileLock(const QString& path);
  ~NativeFileLock();
  NativeFileLock(const NativeFileLock&) = delete;
  NativeFileLock& operator=(const NativeFileLock&) = delete;
 private:
  QFile file_;
  QString identity_;
};

// Hive 2.2.3 frames. Unmodified values remain byte-for-byte intact, including
// values belonging to settings that the native interface does not expose.
class NativeHiveBox {
 public:
  NativeHiveBox(const QString& directory, const QString& name);
  QVariant get(const QString& key) const;
  QStringList keys() const;
  void put(const QString& key, const QVariant& value);
  void remove(const QString& key);
  void apply(const QVariantMap& values, const QStringList& removed = {}, bool atomic = false);
 private:
  QString path_;
  NativeFileLock lock_;
  QFile file_;
  QHash<QByteArray, QByteArray> frames_;
  QList<QByteArray> order_;
  qint64 obsoleteBytes_ = 0;
  void load();
  void rewrite(const QHash<QByteArray, QByteArray>& frames, const QList<QByteArray>& order);
};

class NativeDatabase {
 public:
  explicit NativeDatabase(const QString& path);
  ~NativeDatabase();
  NativeDatabase(const NativeDatabase&) = delete;
  NativeDatabase& operator=(const NativeDatabase&) = delete;
  QVariantList query(const QString& sql, const QVariantList& arguments = {}) const;
  void execute(const QString& sql, const QVariantList& arguments = {});
  void transaction(const std::function<void()>& action);
 private:
  sqlite3* database_ = nullptr;
  void migrate();
};

bool NativeValidSong(const QVariantMap& song);
QString NativeSongKey(const QVariantMap& song);
QString NativeArtistNames(const QVariantMap& song);
QVariantMap NativePlaybackSong(const QVariantMap& song);
QVariantMap NativePlaylistStoredSong(const QVariantMap& song);
QVariantMap NativePlaylistSong(const QVariantMap& stored);
QString NativeSessionCookie(const QString& platform, const QString& stored, const QVariantMap& user = {});
QString NativePersistedCookie(const QString& platform, const QString& cookie);
QVariantMap NativeSessionUser(const QString& platform, const QString& cookie, const QVariantMap& user);

class NativeLocalPlaylists {
 public:
  explicit NativeLocalPlaylists(const QString& directory);
  QVariantList playlists();
  QVariantList songs(const QString& id);
  QVariantMap create(const QString& name, const QVariantList& songs = {});
  bool edit(const QString& action, const QString& id, const QVariant& value = {});
  QString exportLink(const QString& id);
  QVariantMap importLink(const QString& link);
 private:
  QString path_;
  QVariantMap read();
  void write(const QVariantMap& state);
};

class NativeStorage {
 public:
  explicit NativeStorage(const QString& dataDirectory = NativeDataDirectory(),
                         const QString& cacheDirectory = NativeCacheDirectory());
  const QString dataDirectory;
  const QString cacheDirectory;
  NativeDatabase& database();
  NativeLocalPlaylists& playlists();
  NativeHiveBox& box(const QString& name);
  QVariant credential(const QString& key);
  void setCredential(const QString& key, const QVariant& value);
  void recoverSession(const QString& platform);
  void saveSession(const QString& platform, const QVariant& cookie, const QVariant& user);
  QVariantMap cacheSettings();
  QVariantMap setCacheSetting(const QString& key, const QVariant& value);
  QVariantList downloadTasks();
  void saveDownloadTasks(const QVariantList& tasks);
  QVariantMap playbackMemory();
  void savePlaybackMemory(const QVariantMap& memory);
 private:
  std::unique_ptr<NativeDatabase> database_;
  std::unique_ptr<NativeLocalPlaylists> playlists_;
  std::unique_ptr<NativeFileLock> credentialsLock_;
  std::map<QString, std::unique_ptr<NativeHiveBox>> boxes_;
  QStringList downloadOrder_;
  QVariantMap writtenDownloads_;
  bool downloadOrderKnown_ = false;
  QString credentialPath(const QString& key);
};
