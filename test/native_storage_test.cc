#include "../elinux/silica/native_storage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <cstdio>
#include <zlib.h>

#define CHECK(condition) do { \
  if (!(condition)) { fprintf(stderr, "native storage failed at %d\n", __LINE__); return 1; } \
} while (false)

namespace {
QByteArray contents(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) throw NativeStorageError("Fixture read failed");
  return file.readAll();
}

bool rejects(const std::function<void()>& action) {
  try { action(); } catch (const NativeStorageError&) { return true; }
  return false;
}

QVariantMap song(const QString& id, const QString& platform = "qq") {
  return {{"id", id}, {"name", QString::fromUtf8("中文歌曲 ") + id}, {"platform", platform},
          {"artists", QVariantList{QVariantMap{{"id", "artist-id"}, {"name", QString::fromUtf8("歌手")}}}},
          {"album", QVariantMap{{"id", "album-id"}, {"name", QString::fromUtf8("专辑")}, {"coverUrl", "https://image.test/art"}}},
          {"coverUrl", "https://image.test/art"}, {"durationMs", 123456}};
}

QVariantMap task(const QString& id, const QString& status = "waiting") {
  return {{"id", id}, {"song", song(id)}, {"quality", "master"}, {"status", status}, {"progress", 0.25},
          {"downloadedBytes", 250}, {"totalBytes", 1000}, {"createdAt", "2026-10-01T12:30:00.000"}, {"isOfflineCache", false}};
}

QVariantMap schema(NativeDatabase& database) {
  QVariantMap result;
  for (const auto& raw : database.query("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name")) {
    const auto name = raw.toMap().value("name").toString();
    result[name] = database.query("PRAGMA table_info(\"" + name + "\")");
  }
  return result;
}

QStringList indexes(NativeDatabase& database) {
  QStringList result;
  for (const auto& raw : database.query("SELECT name FROM sqlite_master WHERE type='index' AND name NOT LIKE 'sqlite_%' ORDER BY name")) {
    result.append(raw.toMap().value("name").toString());
  }
  return result;
}

QByteArray opaqueFrame() {
  QByteArray bytes(4, char(0));
  bytes.append(char(0)); // Preserve an integer key owned by another setting.
  quint32 key = qToLittleEndian(quint32(77));
  bytes.append(reinterpret_cast<const char*>(&key), 4);
  bytes.append(char(42)); // A registered adapter that native storage never edits.
  bytes.append("opaque-value");
  qToLittleEndian<quint32>(quint32(bytes.size() + 4), reinterpret_cast<uchar*>(bytes.data()));
  const quint32 checksum = qToLittleEndian(quint32(::crc32(0,
      reinterpret_cast<const Bytef*>(bytes.constData()), uInt(bytes.size()))));
  bytes.append(reinterpret_cast<const char*>(&checksum), 4);
  return bytes;
}

int compatibility(const QString& directory) {
  CHECK(contents(QDir(directory).filePath(".native-test-fixture")) == "mconnect-native-storage-test");
  const auto metadata = QJsonDocument::fromJson(contents(QDir(directory).filePath(".native-test-metadata.json"))).toVariant().toMap();
  NativeStorage storage(directory, QDir(directory).filePath("cache"));
  CHECK(storage.cacheSettings().value("sizeLimitMb").toInt() == 2048);
  CHECK(storage.cacheSettings().value("wifiOnly") == false);
  const auto unknown = storage.box("settings").get("unexposed").toMap();
  CHECK(unknown.value("bytes").toByteArray() == QByteArray::fromHex("00017fff"));
  CHECK(unknown.value("strings").toList().size() == 2);
  CHECK(unknown.value("integers").toList().last().toLongLong() == 1234567890123LL);
  CHECK(unknown.value("booleans").toList().last() == false);
  CHECK(unknown.value("nested").toList().last().isNull());
  CHECK(!storage.box("settings").keys().contains("deleted"));
  const auto beforeSettings = contents(QDir(directory).filePath("settings.hive"));
  CHECK(storage.setCacheSetting("offlineMode", true).value("offlineMode") == true);
  CHECK(storage.setCacheSetting("sizeLimitMb", 4096).value("sizeLimitMb").toInt() == 4096);
  CHECK(contents(QDir(directory).filePath("settings.hive")).startsWith(beforeSettings));
  CHECK(storage.box("settings").get("unexposed").toMap() == unknown);

  auto tasks = storage.downloadTasks();
  CHECK(tasks.size() == 3);
  CHECK(tasks[0].toMap().value("id") == "qq_first_spatial");
  CHECK(tasks[1].toMap().value("id") == "qq_second_master");
  CHECK(tasks[2].toMap().value("id") == "qq_third_low");
  CHECK(tasks[0].toMap().value("progress").toDouble() == 0.5);
  const auto legacy = storage.box("download_tasks").get("tasks");
  auto first = tasks[0].toMap();
  first["progress"] = 0.75;
  first["downloadedBytes"] = 750;
  tasks[0] = first;
  storage.saveDownloadTasks(tasks);
  CHECK(storage.box("download_tasks").get("tasks") == legacy);
  CHECK(storage.box("download_tasks").get("task/qq_first_spatial").toMap().value("progress") == 0.75);

  auto memory = storage.playbackMemory();
  CHECK(memory.value("currentQuality") == "master" && memory.value("qualityPreference") == "highest");
  CHECK(memory.value("positionMs").toInt() == 12000 && memory.value("isShuffle") == true);
  CHECK(memory.value("repeatMode") == "all" && memory.value("playbackSpeed") == 1.5);
  memory["positionMs"] = 24000;
  storage.savePlaybackMemory(memory);
  CHECK(storage.box("local_music").get("scan_root") == "/fixture/music");
  CHECK(storage.credential("cookie_qq") == "fixture-qq-cookie");
  storage.setCredential("cookie_qq", QString::fromUtf8("原生 session=="));

  const auto playlistId = metadata.value("playlistId").toString();
  CHECK(storage.playlists().songs(playlistId).size() == 2);
  CHECK(storage.playlists().edit("rename", playlistId, QString::fromUtf8("原生歌单")));
  CHECK(storage.playlists().edit("reorder", playlistId, QVariantList{"qq:second"}));
  CHECK(storage.playlists().songs(playlistId).first().toMap().value("id") == "second");

  auto& database = storage.database();
  CHECK(database.query("SELECT name FROM songs WHERE id='db-song'").first().toMap().value("name") == QString::fromUtf8("数据库歌曲"));
  CHECK(database.query("SELECT song_id FROM user_likes").size() == 1);
  CHECK(database.query("SELECT content FROM lyrics_cache").first().toMap().value("content") == "[00:01.00]fixture");
  const auto expectedSchema = QJsonDocument::fromVariant(metadata.value("schema")).toJson(QJsonDocument::Compact);
  CHECK(QJsonDocument::fromVariant(schema(database)).toJson(QJsonDocument::Compact) == expectedSchema);
  NativeDatabase fresh(QDir(directory).filePath("native-fresh.sqlite"));
  const auto nativeSchema = QJsonDocument::fromVariant(schema(fresh)).toJson(QJsonDocument::Compact);
  if (nativeSchema != expectedSchema) {
    fprintf(stderr, "Expected schema %s\nNative schema %s\n", expectedSchema.constData(), nativeSchema.constData());
  }
  CHECK(nativeSchema == expectedSchema);
  CHECK(indexes(database) == metadata.value("indexes").toStringList());
  database.execute("UPDATE lyrics_offsets SET offset_ms=? WHERE song_key=?", {250, "qq:db-song"});
  puts("NATIVE_STORAGE_DART_COMPATIBILITY_PASS");
  return 0;
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().size() == 3 && app.arguments()[1] == "--compatibility") {
    return compatibility(app.arguments()[2]);
  }
  if (app.arguments().size() == 3 && app.arguments()[1] == "--probe-lock") {
    return rejects([&]() { NativeHiveBox box(app.arguments()[2], "settings"); }) ? 0 : 1;
  }
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  const auto data = temporary.path();
  const auto hivePath = QDir(data).filePath("settings.hive");
  const auto opaque = opaqueFrame();
  {
    NativeHiveBox box(data, "settings");
    CHECK(!box.get("absent").isValid());
    const QVariantMap mixed{{"unicode", QString::fromUtf8("中文 😀")}, {"bytes", QByteArray::fromHex("007fff")},
      {"nested", QVariantList{QVariantMap{{"integer", 1234567890123LL}}, 2.5, true, QVariant()}}};
    box.put("mixed", mixed);
    box.put("delete", false);
    CHECK(box.get("mixed").toMap() == mixed);
    box.remove("delete");
    CHECK(box.keys() == QStringList{"mixed"});
    CHECK(rejects([&]() { NativeHiveBox second(data, "settings"); }));
    QProcess probe;
    probe.start(app.applicationFilePath(), {"--probe-lock", data});
    CHECK(probe.waitForFinished(5000) && probe.exitCode() == 0);
  }
  {
    QFile file(hivePath);
    CHECK(file.open(QIODevice::Append));
    CHECK(file.write(opaque) == opaque.size());
    const char partial[] = {30, 0, 0, 0, 1};
    CHECK(file.write(partial, sizeof(partial)) == sizeof(partial));
  }
  {
    NativeHiveBox box(data, "settings");
    CHECK(contents(hivePath).endsWith(opaque));
    box.apply({{"native", 99}}, {}, true);
    CHECK(contents(hivePath).contains(opaque));
    CHECK(box.get("mixed").toMap().value("unicode") == QString::fromUtf8("中文 😀"));
    CHECK(box.get("native").toInt() == 99);
    CHECK(rejects([&]() { box.put(QString(256, 'x'), 1); }));
    CHECK(rejects([&]() { box.apply({{"a", 1}}, {"a"}); }));
  }
  {
    auto damaged = contents(hivePath);
    damaged[damaged.size() - 1] = char(damaged.at(damaged.size() - 1) ^ 1);
    NativeAtomicWrite(hivePath, damaged);
    CHECK(rejects([&]() { NativeHiveBox box(data, "settings"); }));
    CHECK(contents(hivePath) == damaged);
  }

  const auto library = QDir(data).filePath("library");
  NativeLocalPlaylists playlists(library);
  const auto first = song("first"), second = song("second", "kugou");
  const auto playlist = playlists.create(QString::fromUtf8("测试歌单"), {first, second, first});
  const auto id = playlist.value("id").toString();
  CHECK(id.startsWith("my_") && playlists.songs(id).size() == 2);
  CHECK(playlists.edit("add", id, first) && playlists.songs(id).size() == 2);
  CHECK(playlists.edit("reorder", id, QVariantList{"kugou:second", "qq:absent", "kugou:second"}));
  CHECK(playlists.songs(id).first().toMap().value("platform") == "kugou");
  CHECK(!playlists.edit("rename", id, "  "));
  CHECK(playlists.edit("duplicate", id));
  CHECK(playlists.playlists().size() == 2);
  CHECK(playlists.edit("remove", id, first));
  CHECK(playlists.songs(id).size() == 1);
  CHECK(playlists.importLink(playlists.exportLink(id)).value("songCount").toInt() == 1);
  CHECK(playlists.edit("delete", id) && !playlists.edit("delete", id));
  CHECK(rejects([&]() { playlists.importLink("mconnect://playlist?data=%%%bad"); }));
  const auto playlistPath = QDir(library).filePath("my_playlists.json");
  CHECK(QFile::rename(playlistPath, playlistPath + ".tmp"));
  CHECK(playlists.playlists().size() == 2 && QFile::exists(playlistPath));
  NativeAtomicWrite(playlistPath, "broken JSON");
  CHECK(rejects([&]() { playlists.create("must not replace broken storage"); }));
  CHECK(contents(playlistPath) == "broken JSON");

  const auto databasePath = QDir(data).filePath("mconnect.sqlite");
  QVariantMap freshSchema;
  QStringList freshIndexes;
  {
    NativeDatabase database(databasePath);
    freshSchema = schema(database);
    freshIndexes = indexes(database);
    CHECK(freshSchema.size() == 13 && freshIndexes.size() == 5);
    CHECK(database.query("PRAGMA user_version").first().toMap().value("user_version").toInt() == 4);
    CHECK(database.query("PRAGMA journal_mode").first().toMap().value("journal_mode") == "wal");
    CHECK(rejects([&]() { database.transaction([&]() {
      database.execute("INSERT INTO lyrics_offsets (song_key,updated_at) VALUES (?,?)", {"qq:first", 123});
      throw NativeStorageError("Fixture rollback");
    }); }));
    CHECK(database.query("SELECT * FROM lyrics_offsets").isEmpty());
    database.execute("PRAGMA user_version=3");
    database.execute("DROP TABLE track_ratings; DROP TABLE scrobble_queue;");
    database.execute("CREATE INDEX local_tracks_path ON local_tracks (path)");
  }
  {
    NativeDatabase upgraded(databasePath);
    CHECK(schema(upgraded) == freshSchema && indexes(upgraded) == freshIndexes);
    upgraded.execute("PRAGMA user_version=2");
    upgraded.execute("DROP TABLE lyrics_offsets; DROP TABLE source_match_caches; DROP TABLE track_ratings; DROP TABLE scrobble_queue;");
    upgraded.execute("DROP INDEX listening_history_listened_at; DROP INDEX play_events_song_platform; DROP INDEX play_events_started_at;");
    upgraded.execute("ALTER TABLE local_tracks RENAME TO old_local_tracks");
    upgraded.execute(R"sql(CREATE TABLE local_tracks (
      path TEXT NOT NULL, mtime INTEGER NOT NULL, size INTEGER NOT NULL, title TEXT,
      artist_name TEXT, album_name TEXT, duration_ms INTEGER NOT NULL DEFAULT 0,
      track_number INTEGER, cover_path TEXT, scanned_at INTEGER, PRIMARY KEY (path)))sql");
    upgraded.execute("INSERT INTO local_tracks(path,mtime,size,title) VALUES(?,?,?,?)", {"/fixture/local.flac", 1, 2, QString::fromUtf8("本地歌曲")});
    upgraded.execute("DROP TABLE old_local_tracks");
  }
  {
    NativeDatabase upgraded(databasePath);
    CHECK(schema(upgraded) == freshSchema && indexes(upgraded) == freshIndexes);
    CHECK(upgraded.query("SELECT title,lyrics_mtime FROM local_tracks").first().toMap().value("title") == QString::fromUtf8("本地歌曲"));
    upgraded.execute("PRAGMA user_version=5");
  }
  CHECK(rejects([&]() { NativeDatabase future(databasePath); }));

  const auto legacyPath = QDir(data).filePath("v1.sqlite");
  {
    // Make the actual v1 shape. Its rows and null metadata must survive the
    // direct v1-to-v4 upgrade, including the historical INTEGER primary key.
    NativeDatabase seed(legacyPath);
    seed.execute("DROP TABLE songs; DROP TABLE local_tracks; DROP TABLE toplists_cache; DROP TABLE play_events; DROP TABLE daily_stats; DROP TABLE smart_playlist_snapshots; DROP TABLE lyrics_offsets; DROP TABLE source_match_caches; DROP TABLE track_ratings; DROP TABLE scrobble_queue;");
    seed.execute("DROP INDEX listening_history_listened_at");
    seed.execute(R"sql(CREATE TABLE songs (
      id TEXT NOT NULL, platform TEXT NOT NULL, name TEXT NOT NULL, artists TEXT NOT NULL,
      album_name TEXT, album_cover TEXT, duration_ms INTEGER NOT NULL DEFAULT 0,
      fingerprint TEXT NOT NULL, PRIMARY KEY (id,platform)))sql");
    seed.execute("CREATE TABLE playlists(id TEXT,platform TEXT)");
    seed.execute("INSERT INTO songs(id,platform,name,artists,fingerprint) VALUES(?,?,?,?,?)",
                 {"old", "qq", QString::fromUtf8("旧歌"), QString::fromUtf8("旧歌手"), "old-fingerprint"});
    seed.execute("INSERT INTO user_likes(song_id,platform,added_at) VALUES(?,?,?)", {"old", "qq", 123});
    seed.execute("PRAGMA user_version=1");
  }
  {
    NativeDatabase upgraded(legacyPath);
    CHECK(schema(upgraded) == freshSchema && indexes(upgraded) == freshIndexes);
    const auto row = upgraded.query("SELECT * FROM songs").first().toMap();
    CHECK(row.value("name") == QString::fromUtf8("旧歌") && row.value("album_id").isNull());
    CHECK(upgraded.query("SELECT * FROM user_likes").size() == 1);
  }

  const auto other = QDir(data).filePath("other");
  {
    NativeStorage storage(other, QDir(data).filePath("cache"));
    storage.box("settings").put("offline_cache_settings", QVariantMap{{"wifiOnly", 0}, {"sizeLimitMb", 99999}});
    CHECK(storage.cacheSettings().value("wifiOnly") == true && storage.cacheSettings().value("sizeLimitMb").toInt() == 32768);
    storage.box("download_tasks").put("tasks", QVariantList{task("a"), task("b")});
    storage.box("download_tasks").put("task/a", task("a", "completed"));
    auto restored = storage.downloadTasks();
    CHECK(restored.size() == 2 && restored.first().toMap().value("status") == "completed");
    restored.removeLast();
    storage.saveDownloadTasks(restored);
    CHECK(storage.downloadTasks().size() == 1);
    storage.savePlaybackMemory({{"currentSong", first}, {"playlist", QVariantList{first, second}}, {"currentIndex", 10},
      {"currentQuality", "master"}, {"qualityPreference", "highest"}, {"playbackSpeed", 4}, {"abLoopStartMs", 10}, {"abLoopEndMs", 5}});
    const auto memory = storage.playbackMemory();
    CHECK(memory.value("currentIndex").toInt() == 1 && memory.value("playbackSpeed") == 2.0);
    CHECK(memory.value("abLoopStartMs").isNull() && memory.value("currentQuality") == "master");
    storage.setCredential("cookie_qq", QString::fromUtf8("中文 cookie=="));
    CHECK(storage.credential("cookie_qq") == QString::fromUtf8("中文 cookie=="));
    const auto name = QString::fromLatin1(QByteArray("cookie_qq").toBase64(QByteArray::Base64UrlEncoding));
    CHECK(QFile::exists(QDir(other).filePath("credentials/" + name)));
#ifndef Q_OS_WIN
    CHECK(!(QFileInfo(QDir(other).filePath("credentials/" + name)).permissions() &
          (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther)));
#endif
    storage.setCredential("cookie_qq", QVariant());
    CHECK(!storage.credential("cookie_qq").isValid());
    const auto sessionPath = QDir(other).filePath("credentials/.native-session-qq.tmp");
    const auto userJson = [](const QString& id) {
      return QString::fromUtf8(QJsonDocument::fromVariant(QVariantMap{{"id", id}, {"platform", "qq"}, {"nickname", "fixture"}}).toJson(QJsonDocument::Compact));
    };
    const auto journal = [&](const QVariant& cookie, const QVariant& user) {
      NativeAtomicWrite(sessionPath, QJsonDocument::fromVariant(QVariantMap{{"version", 1}, {"cookie", cookie}, {"user", user}}).toJson(QJsonDocument::Compact), true);
    };
    storage.saveSession("qq", "uin=o123; qm_keyst=old", userJson("123"));
    CHECK(!QFile::exists(sessionPath));
    journal("uin=o456; qm_keyst=new", userJson("456"));
    storage.setCredential("cookie_qq", "uin=o456; qm_keyst=new");
    storage.recoverSession("qq");
    CHECK(storage.credential("user_qq") == userJson("456"));
    journal("uin=o789; qm_keyst=interrupted", userJson("789"));
    storage.saveSession("qq", "uin=o123; qm_keyst=last", userJson("123"));
    CHECK(storage.credential("cookie_qq") == "uin=o123; qm_keyst=last");
    CHECK(storage.credential("user_qq") == userJson("123") && !QFile::exists(sessionPath));
    journal("uin=o456; qm_keyst=mismatch", userJson("789"));
    CHECK(rejects([&]() { storage.recoverSession("qq"); }));
    CHECK(rejects([&]() { storage.saveSession("qq", "uin=o123", userJson("123")); }));
    CHECK(storage.credential("user_qq") == userJson("123") && QFile::exists(sessionPath));
    CHECK(QFile::remove(sessionPath));
    CHECK(rejects([&]() { storage.saveSession("qq", QVariant(), userJson("123")); }));
    journal(QVariant(), QVariant());
    storage.recoverSession("qq");
    CHECK(!storage.credential("user_qq").isValid() && !storage.credential("cookie_qq").isValid());
  }
  puts("NATIVE_STORAGE_PASS hive-frames locks recovery opaque-values schema-v1-v2-v3-v4 atomic-playlists download-order playback-memory credentials session-journal");
  return 0;
}
