import 'dart:io';
import 'package:mconnect/sailfish/recommendation_cache.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/models/song.dart';
import 'package:mconnect/models/toplist.dart';
import 'package:mconnect/models/recommendation_source.dart';
import 'package:mconnect/platform/base/music_platform.dart';
import 'package:mconnect/platform/base/platform_registry.dart';
import 'package:mconnect/sailfish/qml_discovery.dart';
import 'package:mconnect/sailfish/core_service.dart';

const song = Song(
  id: 'test',
  platform: PlatformType.netease,
  name: '推荐歌曲',
  artists: [],
);

class DiscoveryPlatform extends MusicPlatform {
  DiscoveryPlatform(this.platformType);
  @override
  final PlatformType platformType;
  @override
  String get platformName => platformType.displayName;
  @override
  bool get supportsDailyRecommendations =>
      platformType != PlatformType.bilibili;
  @override
  bool get supportsNewSongs => platformType != PlatformType.bilibili;
  String? period;
  int? limit;
  NewSongRegion? region;
  @override
  Future<RecommendationResult> getDailyRecommendation() async =>
      const RecommendationResult(
        songs: [song],
        source: RecommendationSource(
          platform: PlatformType.netease,
          kind: RecommendationKind.fallbackToplist,
          label: '榜单推荐',
          note: '非个性化推荐',
        ),
      );
  @override
  Future<List<Toplist>> getToplists() async => const [
    Toplist(id: 'chart', name: '热歌榜', period: '2026_44'),
  ];
  @override
  Future<List<RankedSong>> getRankedSongs(
    String id, {
    int offset = 0,
    int num = 100,
    String? period,
  }) async {
    this.period = period;
    limit = num;
    return const [RankedSong(song: song, rank: 1)];
  }

  @override
  Future<List<Song>> getNewSongs({
    int limit = 100,
    NewSongRegion region = NewSongRegion.all,
  }) async {
    this.region = region;
    return const [song];
  }

  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

class CountingDiscoveryPlatform extends DiscoveryPlatform {
  CountingDiscoveryPlatform() : super(PlatformType.netease);
  int calls = 0;
  @override
  Future<RecommendationResult> getDailyRecommendation() async {
    calls++;
    return super.getDailyRecommendation();
  }
}

void main() {
  late DiscoveryPlatform platform;
  final discovery = QmlDiscovery();
  setUp(() {
    platform = DiscoveryPlatform(PlatformType.netease);
    PlatformRegistry.register(platform);
    PlatformRegistry.register(DiscoveryPlatform(PlatformType.bilibili));
  });
  test(
    'recommendations retain their fallback provenance and Song identity',
    () async {
      final result = await discovery.songs({
        'platform': 'netease',
        'kind': 'daily',
      });
      expect(result.label, '榜单推荐');
      expect(result.note, '非个性化推荐');
      expect(identical(result.songs.single, song), true);
    },
  );
  test(
    'chart periods and new-song regions reach the existing adapter',
    () async {
      expect(
        (await discovery.charts({'platform': 'netease'})).single['period'],
        '2026_44',
      );
      await discovery.songs({
        'platform': 'netease',
        'kind': 'chart',
        'id': 'chart',
        'period': '2026_44',
      });
      expect(platform.period, '2026_44');
      expect(platform.limit, 1000);
      await discovery.songs({
        'platform': 'netease',
        'kind': 'new',
        'region': 'chinese',
      });
      expect(platform.region, NewSongRegion.chinese);
    },
  );
  test(
    'unsupported capabilities and unknown categories do not invent content',
    () async {
      final bili = discovery.platforms().singleWhere(
        (p) => p['id'] == 'bilibili',
      );
      expect(bili['charts'], false);
      expect(bili['daily'], false);
      await expectLater(
        discovery.songs({'platform': 'bilibili', 'kind': 'daily'}),
        throwsA(isA<CoreRequestFailure>()),
      );
      await expectLater(
        discovery.songs({'platform': 'netease', 'kind': 'unknown'}),
        throwsArgumentError,
      );
    },
  );
  test(
    'daily discovery uses account-scoped cache and explicit refresh',
    () async {
      final root = await Directory.systemTemp.createTemp('discovery-cache-');
      try {
        final platform = CountingDiscoveryPlatform();
        PlatformRegistry.register(platform);
        var user = 'user-a';
        final cached = QmlDiscovery(
          cache: RecommendationCache(directory: () async => root),
          accountScope: (_) => user,
        );
        final args = {'platform': 'netease', 'kind': 'daily'};
        await cached.songs(args);
        await cached.songs(args);
        expect(platform.calls, 1);
        await cached.songs({...args, 'refresh': true});
        expect(platform.calls, 2);
        user = 'user-b';
        await cached.songs(args);
        expect(platform.calls, 3);
      } finally {
        await root.delete(recursive: true);
      }
    },
  );
}
