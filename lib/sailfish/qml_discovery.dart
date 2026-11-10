import '../models/platform_type.dart';
import '../models/song.dart';
import '../platform/base/music_platform.dart';
import '../platform/base/platform_registry.dart';
import 'core_service.dart';
import 'qml_library.dart';
import 'recommendation_cache.dart';

class QmlDiscovery {
  QmlDiscovery({this.cache, this.accountScope});
  final RecommendationCache? cache;
  final String Function(PlatformType)? accountScope;
  List<Map<String, Object?>> platforms() => [
    for (final p in PlatformRegistry.all)
      {
        'id': p.platformType.name,
        'name': p.platformName,
        'daily': p.supportsDailyRecommendations,
        'newSongs': p.supportsNewSongs,
        'charts': p.platformType != PlatformType.bilibili,
      },
  ];

  MusicPlatform _platform(Map<String, dynamic> args) =>
      PlatformRegistry.get(QmlLibrary.platform(args['platform']));

  Future<List<Map<String, Object?>>> charts(Map<String, dynamic> args) async {
    final p = _platform(args);
    if (p.platformType == PlatformType.bilibili) {
      throw const CoreRequestFailure('该平台暂不提供榜单，请使用搜索或收藏夹');
    }
    final rows = await p.getToplists().timeout(const Duration(seconds: 20));
    return [
      for (final chart in rows)
        {
          'id': chart.id,
          'title': chart.name,
          'period': chart.period ?? '',
          'group': chart.groupName ?? '',
          'description': chart.updateFrequency ?? '',
        },
    ];
  }

  Future<({List<Song> songs, String label, String note})> songs(
    Map<String, dynamic> args,
  ) async {
    final p = _platform(args);
    switch (args['kind']) {
      case 'daily':
        if (!p.supportsDailyRecommendations) {
          throw const CoreRequestFailure('该平台暂不提供推荐，请使用搜索或收藏夹');
        }
        Future<RecommendationData> fetch() async {
          final result = await p.getDailyRecommendation().timeout(
            const Duration(seconds: 20),
          );
          if (result.error != null && result.songs.isEmpty) {
            throw const CoreRequestFailure('推荐暂时不可用，请检查登录状态或稍后重试');
          }
          return (
            songs: result.songs,
            label: result.source?.label ?? '推荐歌曲',
            note: result.source?.note ?? '',
          );
        }
        final refresh = args['refresh'] ?? false;
        if (refresh is! bool) throw ArgumentError();
        return cache == null
            ? fetch()
            : cache!.get(
                '${p.platformType.name}:${accountScope?.call(p.platformType) ?? 'anonymous'}',
                refresh: refresh,
                fetch: fetch,
              );
      case 'new':
        if (!p.supportsNewSongs) throw const CoreRequestFailure('该平台暂不提供新歌列表');
        final region = args['region'] ?? 'all';
        final regions = NewSongRegion.values.where((r) => r.name == region);
        if (regions.length != 1) throw ArgumentError();
        final songs = await p
            .getNewSongs(region: regions.single, limit: 100)
            .timeout(const Duration(seconds: 20));
        return (songs: songs, label: '新歌速递', note: '');
      case 'chart':
        if (p.platformType == PlatformType.bilibili) throw ArgumentError();
        final period = args['period'];
        if (period != null && (period is! String || period.length > 128)) {
          throw ArgumentError();
        }
        final ranked = await p
            .getRankedSongs(
              QmlLibrary.identifier(args['id']),
              num: 1000,
              period: period as String?,
            )
            .timeout(const Duration(seconds: 20));
        return (
          songs: ranked.map((r) => r.song).toList(),
          label: '榜单歌曲',
          note: '最多展示前 1000 首',
        );
      default:
        throw ArgumentError();
    }
  }
}
