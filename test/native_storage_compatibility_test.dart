import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

import 'package:drift/native.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:hive/hive.dart';
import 'package:mconnect/core/database/app_database.dart';
import 'package:mconnect/features/download/data/download_task_store.dart';
import 'package:mconnect/features/download/domain/entities/download_task.dart';
import 'package:mconnect/features/library/data/my_playlists_repository.dart';
import 'package:mconnect/features/local_music/data/local_scan_root_store.dart';
import 'package:mconnect/features/offline_cache/data/cache_settings_repository.dart';
import 'package:mconnect/features/offline_cache/presentation/providers/offline_cache_provider.dart';
import 'package:mconnect/features/player/data/player_playback_memory_store.dart';
import 'package:mconnect/models/album.dart';
import 'package:mconnect/models/artist.dart';
import 'package:mconnect/models/audio_quality.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/models/song.dart';
import 'package:path/path.dart' as p;

// The fixture executable reads only directories marked by this test. Both
// runtimes close their files before the other runtime opens them.
void main() {
  final executable = Platform.environment['MCONNECT_NATIVE_STORAGE_TEST'];

  test('Dart storage survives C++ reads and writes and reopens in Dart', () async {
    final sqliteLibrary = Platform.environment['MCONNECT_TEST_SQLITE_LIBRARY'];
    if (sqliteLibrary != null) {
      DynamicLibrary.open(sqliteLibrary);
    }
    final directory = await Directory.systemTemp.createTemp(
      'mconnect_native_storage_compatibility_',
    );
    addTearDown(() async {
      await Hive.close();
      await directory.delete(recursive: true);
    });
    await File(
      p.join(directory.path, '.native-test-fixture'),
    ).writeAsString('mconnect-native-storage-test');
    Hive.init(directory.path);
    const cache = CacheSettingsRepository();
    await cache.save(
      const OfflineCacheSettings(sizeLimitMb: 2048, wifiOnly: false),
    );
    final settings = Hive.box('settings');
    final unexposed = {
      'bytes': Uint8List.fromList([0, 1, 127, 255]),
      'strings': <String>['中文 😀', 'second'],
      'integers': <int>[1, 1234567890123],
      'doubles': <double>[1.25, 2.5],
      'booleans': <bool>[true, false],
      'nested': <dynamic>[
        {'nullable': null, 'text': '原生读取'},
        null,
      ],
    };
    await settings.put('unexposed', unexposed);
    await settings.put('deleted', true);
    await settings.delete('deleted');

    Song song(String id) => Song(
      id: id,
      name: '中文歌曲 $id',
      platform: PlatformType.qq,
      artists: const [Artist(id: 'artist-id', name: '歌手')],
      album: const Album(id: 'album-id', name: '专辑'),
      coverUrl: 'https://image.test/art',
      duration: const Duration(milliseconds: 123456),
    );
    DownloadTask task(String id, AudioLevel quality) => DownloadTask(
      id: DownloadTask.buildId(song(id), quality),
      song: song(id),
      quality: quality,
      progress: 0.25,
      downloadedBytes: 250,
      totalBytes: 1000,
      createdAt: DateTime(2026, 10, 1, 12, 30),
    );
    final first = task('first', AudioLevel.spatial);
    final second = task('second', AudioLevel.master);
    final third = task('third', AudioLevel.low);
    final tasks = HiveDownloadTaskStore();
    await tasks.save([first, second]);
    await tasks.save([
      first.copyWith(progress: 0.5, downloadedBytes: 500),
      second,
    ]);
    await Hive.box(
      'download_tasks',
    ).put(HiveDownloadTaskStore.taskKey(third.id), third.toJson());
    final legacy = jsonEncode(Hive.box('download_tasks').get('tasks'));

    await HivePlayerPlaybackMemoryStore().save(
      PlayerPlaybackMemory(
        currentSong: song('first'),
        playlist: [song('first'), song('second')],
        currentIndex: 0,
        position: const Duration(seconds: 12),
        duration: const Duration(milliseconds: 123456),
        currentQuality: AudioLevel.master,
        qualityPreference: AudioQualityPreference.highest,
        playbackSpeed: 1.5,
        isShuffle: true,
        repeatMode: 'all',
      ),
    );
    await HiveLocalScanRootStore().write('/fixture/music');

    final playlists = MyPlaylistsRepository(storageDirectory: directory);
    final playlist = await playlists.importPlaylist(
      name: 'Dart 歌单',
      songs: [song('first'), song('second')],
    );
    final credentials = await Directory(
      p.join(directory.path, 'credentials'),
    ).create();
    final credential = File(
      p.join(credentials.path, base64Url.encode(utf8.encode('cookie_qq'))),
    );
    await credential.writeAsString('fixture-qq-cookie');
    await Hive.close();

    final databaseFile = File(p.join(directory.path, 'mconnect.sqlite'));
    final database = AppDatabase.forTesting(NativeDatabase(databaseFile));
    final schema = <String, Object?>{};
    List<String> indexes;
    try {
      await database.customStatement(
        'INSERT INTO songs (id, platform, name, artists, fingerprint) '
        'VALUES (?, ?, ?, ?, ?)',
        ['db-song', 'qq', '数据库歌曲', '数据库歌手', 'fixture-fingerprint'],
      );
      await database.customStatement(
        'INSERT INTO user_likes (song_id, platform, added_at) VALUES (?, ?, ?)',
        ['db-song', 'qq', 123],
      );
      await database.customStatement(
        'INSERT INTO lyrics_cache (song_id, platform, content, format, synced_at) '
        'VALUES (?, ?, ?, ?, ?)',
        ['db-song', 'qq', '[00:01.00]fixture', 'lrc', 123],
      );
      await database.customStatement(
        'INSERT INTO lyrics_offsets (song_key, offset_ms, updated_at) VALUES (?, ?, ?)',
        ['qq:db-song', 125, 123],
      );
      final tables = await database
          .customSelect(
            "SELECT name FROM sqlite_master WHERE type='table' "
            "AND name NOT LIKE 'sqlite_%' ORDER BY name",
          )
          .get();
      for (final table in tables) {
        final name = table.read<String>('name');
        final columns = await database
            .customSelect('PRAGMA table_info("$name")')
            .get();
        schema[name] = columns.map((column) => column.data).toList();
      }
      indexes =
          (await database
                  .customSelect(
                    "SELECT name FROM sqlite_master WHERE type='index' "
                    "AND name NOT LIKE 'sqlite_%' ORDER BY name",
                  )
                  .get())
              .map((row) => row.read<String>('name'))
              .toList();
    } finally {
      await database.close();
    }
    await File(
      p.join(directory.path, '.native-test-metadata.json'),
    ).writeAsString(
      jsonEncode({
        'playlistId': playlist.id,
        'schema': schema,
        'indexes': indexes,
      }),
    );

    final result = await Process.run(executable!, [
      '--compatibility',
      directory.path,
    ]).timeout(const Duration(seconds: 30));
    expect(result.exitCode, 0, reason: '${result.stdout}\n${result.stderr}');
    expect(result.stdout, contains('NATIVE_STORAGE_DART_COMPATIBILITY_PASS'));

    Hive.init(directory.path);
    expect((await cache.load()).offlineMode, isTrue);
    expect((await cache.load()).sizeLimitMb, 4096);
    expect(Hive.box('settings').get('unexposed'), unexposed);
    expect(Hive.box('settings').containsKey('deleted'), isFalse);
    final restoredTasks = await HiveDownloadTaskStore().load();
    expect(restoredTasks.map((task) => task.id), [
      first.id,
      second.id,
      third.id,
    ]);
    expect(restoredTasks.first.progress, 0.75);
    expect(restoredTasks.first.downloadedBytes, 750);
    expect(restoredTasks.first.quality, AudioLevel.spatial);
    expect(jsonEncode(Hive.box('download_tasks').get('tasks')), legacy);
    final memory = await HivePlayerPlaybackMemoryStore().load();
    expect(memory!.position, const Duration(seconds: 24));
    expect(memory.qualityPreference, AudioQualityPreference.highest);
    expect(memory.currentQuality, AudioLevel.master);
    expect(memory.isShuffle, isTrue);
    expect(await credential.readAsString(), '原生 session==');
    expect((await playlists.getPlaylist(playlist.id))!.name, '原生歌单');
    expect((await playlists.getSongs(playlist.id)).map((song) => song.id), [
      'second',
      'first',
    ]);
    final reopened = AppDatabase.forTesting(NativeDatabase(databaseFile));
    try {
      expect(await reopened.likesDao.isLiked('db-song', 'qq'), isTrue);
      final offsets = await reopened
          .customSelect('SELECT offset_ms FROM lyrics_offsets')
          .get();
      expect(offsets.single.read<int>('offset_ms'), 250);
    } finally {
      await reopened.close();
    }
  }, skip: executable == null);
}
