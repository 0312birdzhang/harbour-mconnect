#include "native_storage.h"

#include <QDir>
#include <QFileInfo>
#include <sqlite3.h>

namespace {
const QString songsTable = QStringLiteral(R"sql(
CREATE TABLE songs (
  id TEXT NOT NULL, platform TEXT NOT NULL, name TEXT NOT NULL,
  artists TEXT NOT NULL, album_name TEXT, album_cover TEXT,
  duration_ms INTEGER NOT NULL DEFAULT 0, fingerprint TEXT NOT NULL,
  album_id TEXT, artist_id TEXT, track_number INTEGER,
  PRIMARY KEY (id, platform))
)sql");
const QStringList originalTables = {
  QStringLiteral(R"sql(CREATE TABLE listening_history (
    id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, song_id TEXT NOT NULL,
    platform TEXT NOT NULL, listened_at INTEGER NOT NULL,
    duration_listened INTEGER NOT NULL DEFAULT 0,
    UNIQUE (song_id, platform, listened_at)))sql"),
  QStringLiteral(R"sql(CREATE TABLE user_likes (
    id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, song_id TEXT NOT NULL,
    platform TEXT NOT NULL, added_at INTEGER NOT NULL,
    UNIQUE (song_id, platform)))sql"),
  QStringLiteral(R"sql(CREATE TABLE lyrics_cache (
    song_id TEXT NOT NULL, platform TEXT NOT NULL, content TEXT NOT NULL,
    format TEXT NOT NULL, synced_at INTEGER NOT NULL,
    PRIMARY KEY (song_id, platform)))sql")
};
const QStringList version2Tables = {
  // The v1 upgrade creates the current local_tracks definition, as Drift did.
  QStringLiteral(R"sql(CREATE TABLE local_tracks (
    path TEXT NOT NULL, mtime INTEGER NOT NULL, size INTEGER NOT NULL,
    title TEXT, artist_name TEXT, album_name TEXT,
    duration_ms INTEGER NOT NULL DEFAULT 0, track_number INTEGER,
    cover_path TEXT, scanned_at INTEGER, lyrics_mtime INTEGER, lyrics_size INTEGER,
    PRIMARY KEY (path)))sql"),
  QStringLiteral(R"sql(CREATE TABLE toplists_cache (
    platform TEXT NOT NULL, toplist_id TEXT NOT NULL, name TEXT NOT NULL,
    cover_url TEXT, update_frequency TEXT, period TEXT, song_count INTEGER,
    group_name TEXT, intro TEXT, fetched_at INTEGER NOT NULL,
    PRIMARY KEY (platform, toplist_id)))sql"),
  QStringLiteral(R"sql(CREATE TABLE play_events (
    id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, song_id TEXT NOT NULL,
    platform TEXT NOT NULL, started_at INTEGER NOT NULL, ended_at INTEGER,
    duration_listened INTEGER NOT NULL DEFAULT 0,
    completed_ratio REAL NOT NULL DEFAULT 0.0, source TEXT))sql"),
  QStringLiteral(R"sql(CREATE TABLE daily_stats (
    day TEXT NOT NULL, song_id TEXT NOT NULL, platform TEXT NOT NULL,
    play_count INTEGER NOT NULL DEFAULT 0, listen_ms INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (day, song_id, platform)))sql"),
  QStringLiteral(R"sql(CREATE TABLE smart_playlist_snapshots (
    rule_id TEXT NOT NULL, song_keys TEXT NOT NULL, generated_at INTEGER NOT NULL,
    PRIMARY KEY (rule_id)))sql")
};
const QStringList version3Tables = {
  QStringLiteral(R"sql(CREATE TABLE lyrics_offsets (
    song_key TEXT NOT NULL, offset_ms INTEGER NOT NULL DEFAULT 0,
    updated_at INTEGER NOT NULL, PRIMARY KEY (song_key)))sql"),
  QStringLiteral(R"sql(CREATE TABLE source_match_caches (
    song_key TEXT NOT NULL, target_platform TEXT NOT NULL, target_song_id TEXT NOT NULL,
    url TEXT, url_fetched_at INTEGER, score REAL NOT NULL DEFAULT 0.0,
    expires_at INTEGER NOT NULL, PRIMARY KEY (song_key, target_platform)))sql")
};
const QStringList version4Tables = {
  QStringLiteral(R"sql(CREATE TABLE track_ratings (
    song_key TEXT NOT NULL, rating INTEGER NOT NULL DEFAULT 0,
    play_count INTEGER NOT NULL DEFAULT 0, last_played_at INTEGER,
    PRIMARY KEY (song_key)))sql"),
  QStringLiteral(R"sql(CREATE TABLE scrobble_queue (
    id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT, event_id INTEGER NOT NULL,
    service TEXT NOT NULL, song_key TEXT NOT NULL, title TEXT NOT NULL,
    artist TEXT NOT NULL, album TEXT, duration_ms INTEGER NOT NULL DEFAULT 0,
    played_at INTEGER NOT NULL, status TEXT NOT NULL DEFAULT 'pending',
    attempts INTEGER NOT NULL DEFAULT 0, next_attempt_at INTEGER, lease_until INTEGER,
    created_at INTEGER NOT NULL, sent_at INTEGER, last_error TEXT))sql")
};
const QStringList version3Indexes = {
  "CREATE INDEX IF NOT EXISTS listening_history_listened_at ON listening_history (listened_at)",
  "CREATE INDEX IF NOT EXISTS play_events_song_platform ON play_events (song_id, platform)",
  "CREATE INDEX IF NOT EXISTS play_events_started_at ON play_events (started_at)"
};
const QStringList version4Indexes = {
  "CREATE INDEX IF NOT EXISTS scrobble_queue_status ON scrobble_queue (status)",
  "CREATE INDEX IF NOT EXISTS scrobble_queue_service_event ON scrobble_queue (service, event_id)"
};

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

Statement prepare(sqlite3* database, const QString& sql, const QVariantList& arguments) {
  sqlite3_stmt* raw = nullptr;
  const auto text = sql.toUtf8();
  if (sqlite3_prepare_v2(database, text.constData(), text.size(), &raw, nullptr) != SQLITE_OK) {
    if (raw) sqlite3_finalize(raw);
    throw NativeStorageError("Cannot prepare database request");
  }
  Statement statement(raw, sqlite3_finalize);
  if (!raw || sqlite3_bind_parameter_count(raw) != arguments.size()) {
    throw NativeStorageError("Invalid database arguments");
  }
  for (int index = 0; index < arguments.size(); ++index) {
    const auto& value = arguments[index];
    int result;
    switch (value.type()) {
      case QVariant::Invalid:
        result = sqlite3_bind_null(raw, index + 1);
        break;
      case QVariant::Bool:
      case QVariant::Int:
      case QVariant::UInt:
      case QVariant::LongLong:
      case QVariant::ULongLong:
        result = sqlite3_bind_int64(raw, index + 1, value.toLongLong());
        break;
      case QVariant::Double:
        result = sqlite3_bind_double(raw, index + 1, value.toDouble());
        break;
      case QVariant::String: {
        const auto bytes = value.toString().toUtf8();
        result = sqlite3_bind_text(raw, index + 1, bytes.constData(), bytes.size(), SQLITE_TRANSIENT);
        break;
      }
      case QVariant::ByteArray: {
        const auto bytes = value.toByteArray();
        result = sqlite3_bind_blob(raw, index + 1, bytes.constData(), bytes.size(), SQLITE_TRANSIENT);
        break;
      }
      default: throw NativeStorageError("Unsupported database value");
    }
    if (result != SQLITE_OK) throw NativeStorageError("Cannot bind database value");
  }
  return statement;
}
}

NativeDatabase::NativeDatabase(const QString& path) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    throw NativeStorageError("Cannot create database directory");
  }
  const auto filename = path.toUtf8();
  const int opened = sqlite3_open_v2(filename.constData(), &database_,
                                    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
  try {
    if (opened != SQLITE_OK) throw NativeStorageError("Cannot open database");
    sqlite3_busy_timeout(database_, 5000);
    migrate();
    execute("PRAGMA journal_mode=WAL");
  } catch (...) {
    if (database_) sqlite3_close(database_);
    database_ = nullptr;
    throw;
  }
}

NativeDatabase::~NativeDatabase() {
  if (database_) sqlite3_close(database_);
}

QVariantList NativeDatabase::query(const QString& sql, const QVariantList& arguments) const {
  const auto statement = prepare(database_, sql, arguments);
  QVariantList rows;
  int status;
  while ((status = sqlite3_step(statement.get())) == SQLITE_ROW) {
    QVariantMap row;
    for (int index = 0; index < sqlite3_column_count(statement.get()); ++index) {
      QVariant value;
      switch (sqlite3_column_type(statement.get(), index)) {
        case SQLITE_INTEGER:
          value = QVariant::fromValue(qint64(sqlite3_column_int64(statement.get(), index)));
          break;
        case SQLITE_FLOAT:
          value = sqlite3_column_double(statement.get(), index);
          break;
        case SQLITE_TEXT:
          value = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), index)),
                                    sqlite3_column_bytes(statement.get(), index));
          break;
        case SQLITE_BLOB:
          value = QByteArray(reinterpret_cast<const char*>(sqlite3_column_blob(statement.get(), index)),
                             sqlite3_column_bytes(statement.get(), index));
          break;
        default: break;
      }
      row.insert(QString::fromUtf8(sqlite3_column_name(statement.get(), index)), value);
    }
    rows.append(row);
  }
  if (status != SQLITE_DONE) throw NativeStorageError("Cannot read database");
  return rows;
}

void NativeDatabase::execute(const QString& sql, const QVariantList& arguments) {
  if (arguments.isEmpty()) {
    const auto text = sql.toUtf8();
    if (sqlite3_exec(database_, text.constData(), nullptr, nullptr, nullptr) != SQLITE_OK) {
      throw NativeStorageError("Cannot update database");
    }
    return;
  }
  const auto statement = prepare(database_, sql, arguments);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) throw NativeStorageError("Cannot update database");
}

void NativeDatabase::transaction(const std::function<void()>& action) {
  execute("BEGIN IMMEDIATE");
  try {
    action();
    execute("COMMIT");
  } catch (...) {
    sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
    throw;
  }
}

void NativeDatabase::migrate() {
  transaction([this]() {
    // Read the version after acquiring the write transaction. Concurrent first
    // starts cannot both decide to create or upgrade the same database.
    const int version = query("PRAGMA user_version").first().toMap().value("user_version").toInt();
    if (version < 0 || version > 4) throw NativeStorageError("Unsupported database version");
    if (version == 4) return;
    const auto create = [this](const QStringList& tables) {
      for (const auto& table : tables) execute(table);
    };
    if (version == 0) {
      if (!query("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'").isEmpty()) {
        throw NativeStorageError("Database schema version is missing");
      }
      execute(songsTable);
      create(originalTables);
      create(version2Tables);
    } else if (version == 1) {
      execute("ALTER TABLE songs ADD COLUMN album_id TEXT");
      execute("ALTER TABLE songs ADD COLUMN artist_id TEXT");
      execute("ALTER TABLE songs ADD COLUMN track_number INTEGER");
      create(version2Tables);
      execute("DROP TABLE IF EXISTS playlists");
    }
    if (version == 2) {
      execute("ALTER TABLE local_tracks ADD COLUMN lyrics_mtime INTEGER");
      execute("ALTER TABLE local_tracks ADD COLUMN lyrics_size INTEGER");
    }
    if (version < 3) {
      create(version3Tables);
      create(version3Indexes);
    }
    create(version4Tables);
    create(version4Indexes);
    execute("DROP INDEX IF EXISTS local_tracks_path");
    execute("PRAGMA user_version=4");
  });
}
