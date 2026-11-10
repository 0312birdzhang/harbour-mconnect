import 'dart:async';
import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/sailfish/recommendation_cache.dart';
import 'package:mconnect/models/song.dart';
import 'package:mconnect/models/artist.dart';
import 'package:mconnect/models/album.dart';
import 'package:mconnect/models/platform_type.dart';

const song = Song(
  id: '缓存/中文',
  platform: PlatformType.qq,
  name: '推荐歌曲',
  artists: [Artist(id: 'artist-id', name: '歌手')],
  album: Album(id: 'album-id', name: '专辑'),
  albumId: 'album-id',
  artistId: 'artist-id',
  fee: 1,
  trackNumber: 2,
);
const data = (songs: [song], label: '每日推荐', note: '来源说明');
void main() {
  late Directory root;
  RecommendationCache cache() =>
      RecommendationCache(directory: () async => root);
  setUp(() async {
    root = await Directory.systemTemp.createTemp('mconnect-recs-');
  });
  tearDown(() async {
    await root.delete(recursive: true);
  });
  test(
    'concurrent pane requests join one fetch and preserve original songs',
    () async {
      final store = cache();
      var calls = 0;
      final pending = Completer<RecommendationData>();
      Future<RecommendationData> fetch() {
        calls++;
        return pending.future;
      }

      final a = store.get('qq:user-a', refresh: false, fetch: fetch);
      final b = store.get('qq:user-a', refresh: false, fetch: fetch);
      await Future<void>.delayed(const Duration(milliseconds: 30));
      expect(calls, 1);
      pending.complete(data);
      expect(identical((await a).songs.single, song), true);
      expect(identical((await b).songs.single, song), true);
      await store.get('qq:user-a', refresh: false, fetch: fetch);
      expect(calls, 1);
    },
  );
  test(
    'restart reads private metadata locally and manual refresh fetches once',
    () async {
      var calls = 0;
      Future<RecommendationData> fetch() async {
        calls++;
        return data;
      }

      await cache().get('qq:user-a', refresh: false, fetch: fetch);
      final restored = await cache().get(
        'qq:user-a',
        refresh: false,
        fetch: () async => throw StateError('No network'),
      );
      expect(restored.songs.single.id, song.id);
      expect(restored.songs.single.platform, song.platform);
      expect(restored.songs.single.artists.single.id, 'artist-id');
      expect(restored.songs.single.albumId, 'album-id');
      expect(restored.songs.single.fee, 1);
      expect(restored.songs.single.trackNumber, 2);
      expect(restored.note, contains('本地缓存'));
      await cache().get('qq:user-a', refresh: true, fetch: fetch);
      expect(calls, 2);
      final files = await root.list().where((e) => e is File).toList();
      expect(files.any((f) => f.path.endsWith('.nomedia')), true);
      expect(files.where((f) => f.path.endsWith('.mcr')).length, 1);
    },
  );
  test(
    'account scope stays separate and failed refresh keeps usable cache',
    () async {
      final store = cache();
      var calls = 0;
      Future<RecommendationData> fetch() async {
        calls++;
        return data;
      }

      await store.get('qq:user-a', refresh: false, fetch: fetch);
      await store.get('qq:user-b', refresh: false, fetch: fetch);
      expect(calls, 2);
      final fallback = await store.get(
        'qq:user-a',
        refresh: true,
        fetch: () async => throw StateError('offline'),
      );
      expect(fallback.note, contains('刷新失败'));
      expect(fallback.songs.single.id, song.id);
      await store.get('qq:user-a', refresh: false, fetch: fetch);
      expect(calls, 2);
    },
  );
  test(
    'corrupt disk data is replaced and failures do not poison in-flight state',
    () async {
      await cache().get('qq:user-a', refresh: false, fetch: () async => data);
      final file =
          (await root.list().where((f) => f.path.endsWith('.mcr')).toList())
                  .single
              as File;
      await file.writeAsString('corrupt');
      final store = cache();
      await expectLater(
        store.get(
          'qq:user-a',
          refresh: false,
          fetch: () async => throw StateError('offline'),
        ),
        throwsStateError,
      );
      expect(
        (await store.get(
          'qq:user-a',
          refresh: false,
          fetch: () async => data,
        )).songs.single.id,
        song.id,
      );
    },
  );
}
