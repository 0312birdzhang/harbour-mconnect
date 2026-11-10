import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:mconnect/features/player/data/playback_notification_service.dart';
import 'package:mconnect/features/player/presentation/providers/player_provider.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/models/song.dart';
import 'package:mconnect/models/playlist.dart';
import 'package:mconnect/platform/base/music_platform.dart';
import 'package:mconnect/platform/base/platform_registry.dart';
import 'package:mconnect/sailfish/qml_core_adapter.dart';

const song = Song(
  id: '中文/1',
  platform: PlatformType.netease,
  name: '测试歌曲',
  artists: [],
);

class FakePlatform extends MusicPlatform {
  int? playlistPage;
  @override
  Future<List<Playlist>> searchPlaylists(
    String keyword, {
    int page = 1,
    int limit = 30,
  }) async {
    playlistPage = page;
    return const [
      Playlist(
        id: '列表/中文',
        name: '测试歌单',
        platform: PlatformType.netease,
        songCount: 12,
        creatorName: '作者',
      ),
    ];
  }

  @override
  PlatformType get platformType => PlatformType.netease;
  @override
  String get platformName => '网易云音乐';
  @override
  Future<List<Song>> search(
    String keyword, {
    int page = 1,
    int limit = 30,
  }) async => [song];
  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

class FakePlayer extends PlayerNotifier {
  FakePlayer()
    : super(
        notificationController: NoopPlaybackNotificationController(),
        audioControllerFactory: () =>
            throw StateError('No audio allowed in bridge tests'),
      );
  List<Song>? played;
  int? index;
  @override
  Future<void> playPlaylist(List<Song> songs, {int startIndex = 0}) async {
    played = songs;
    index = startIndex;
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late FakePlayer player;
  late ProviderContainer container;
  late QmlCoreAdapter adapter;
  setUp(() {
    PlatformRegistry.register(FakePlatform());
    player = FakePlayer();
    container = ProviderContainer(
      overrides: [playerProvider.overrideWith((ref) => player)],
    );
    adapter = QmlCoreAdapter(container);
  });
  tearDown(() {
    container.dispose();
  });
  test(
    'snapshot is paused and reading it never creates an audio controller',
    () async {
      final result = await adapter.dispatch('snapshot', {}) as Map;
      expect(result['version'], 1);
      expect(result['playing'], false);
      expect(player.played, null);
      expect(result.containsKey('queue'), false);
    },
  );
  test('search tokens preserve original Song records for playback', () async {
    final result =
        await adapter.dispatch('search', {'query': '中文', 'platform': 'netease'})
            as Map;
    expect((result['songs'] as List).single['title'], '测试歌曲');
    await adapter.dispatch('playResult', {
      'token': result['token'],
      'index': 0,
    });
    expect(identical(player.played!.single, song), true);
    expect(player.index, 0);
    expect((result['songs'] as List).single.containsKey('cookie'), false);
  });
  test(
    'large library lists page the UI but play the complete original queue',
    () async {
      final songs = List.generate(
        235,
        (index) => Song(
          id: '$index',
          platform: PlatformType.netease,
          name: '歌曲 $index',
          artists: const [],
        ),
      );
      final first = adapter.storeSongs(songs);
      expect((first['songs'] as List).length, 100);
      expect(first['total'], 235);
      final second =
          await adapter.dispatch('resultPage', {
                'token': first['token'],
                'offset': 100,
              })
              as Map;
      expect((second['songs'] as List).first['id'], '100');
      final last =
          await adapter.dispatch('resultPage', {
                'token': first['token'],
                'offset': 200,
              })
              as Map;
      expect((last['songs'] as List).length, 35);
      await adapter.dispatch('playResult', {
        'token': first['token'],
        'index': 234,
      });
      expect(identical(player.played, songs), true);
      expect(player.index, 234);
    },
  );
  test(
    'native playback mode actions never create an audio controller',
    () async {
      await adapter.dispatch('toggleShuffle', {});
      await adapter.dispatch('cycleRepeat', {});
      final state = adapter.snapshot();
      expect(state['shuffle'], true);
      expect(state['repeat'], 'all');
      expect(state['playing'], false);
      expect(player.played, null);
    },
  );
  test(
    'native playlist search preserves source identity and page without playback',
    () async {
      final result =
          await adapter.dispatch('searchPlaylists', {
                'query': '中文',
                'platform': 'netease',
                'page': 2,
              })
              as Map;
      final row = (result['playlists'] as List).single as Map;
      expect(row['id'], '列表/中文');
      expect(row['platform'], 'netease');
      expect(row['count'], 12);
      expect(
        (PlatformRegistry.get(PlatformType.netease) as FakePlatform)
            .playlistPage,
        2,
      );
      expect(player.played, isNull);
      await expectLater(
        adapter.dispatch('searchPlaylists', {
          'query': '中文',
          'platform': 'netease',
          'page': 0,
        }),
        throwsArgumentError,
      );
    },
  );
  test(
    'result artwork handles missing covers without fetching or playing',
    () async {
      final result = adapter.storeSongs([song]);
      expect(
        await adapter.dispatch('resultArtwork', {
          'token': result['token'],
          'index': 0,
        }),
        {'source': ''},
      );
      expect(player.played, isNull);
    },
  );
  test(
    'untrusted result indices, seek positions and platforms are rejected',
    () async {
      for (final args in [
        {'platform': 'invalid', 'query': 'test'},
        {'platform': 'netease', 'query': ''},
        {'platform': 'netease', 'query': 'test', 'page': -1},
      ]) {
        await expectLater(
          adapter.dispatch('search', args),
          throwsArgumentError,
        );
      }
      await expectLater(
        adapter.dispatch('playResult', {'token': 'missing', 'index': 0}),
        throwsArgumentError,
      );
      await expectLater(
        adapter.dispatch('seek', {'position': -1}),
        throwsArgumentError,
      );
      await expectLater(
        adapter.dispatch('playIndex', {'index': 0}),
        throwsArgumentError,
      );
      expect(player.played, null);
    },
  );
  test('recommendation results survive search paging pressure', () async {
    final pinned = adapter.storeSongs([song], recommendation: 'netease');
    for (var i = 0; i < 130; i++) {
      adapter.storeSongs([song]);
    }
    await adapter.dispatch('playResult', {
      'token': pinned['token'],
      'index': 0,
    });
    expect(identical(player.played!.single, song), true);
  });
}
