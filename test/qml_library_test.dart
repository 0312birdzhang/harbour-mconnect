import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:mconnect/features/library/data/my_playlists_repository.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/models/playlist.dart';
import 'package:mconnect/models/song.dart';
import 'package:mconnect/platform/base/music_platform.dart';
import 'package:mconnect/platform/base/platform_registry.dart';
import 'package:mconnect/sailfish/qml_library.dart';
import 'package:mconnect/sailfish/core_service.dart';

class LibraryPlatform extends MusicPlatform {
  bool loggedIn = true;
  int networkReads = 0;
  @override
  PlatformType get platformType => PlatformType.netease;
  @override
  bool get isLoggedIn => loggedIn;
  @override
  Future<List<Playlist>> getUserPlaylists() async {
    networkReads++;
    return const [
      Playlist(id: 'remote', name: '平台歌单', platform: PlatformType.netease),
    ];
  }

  @override
  Future<List<Song>> getPlaylistDetail(String id) async {
    networkReads++;
    return const [
      Song(
        id: 'offline-song',
        name: '原始歌曲',
        artists: [],
        platform: PlatformType.netease,
      ),
    ];
  }

  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

void main() {
  late Directory directory;
  late ProviderContainer container;
  late QmlLibrary library;
  late LibraryPlatform platform;
  setUp(() async {
    directory = await Directory.systemTemp.createTemp('qml-library-');
    container = ProviderContainer();
    platform = LibraryPlatform();
    PlatformRegistry.register(platform);
    library = QmlLibrary(
      container,
      local: MyPlaylistsRepository(storageDirectory: directory),
    );
  });
  tearDown(() async {
    container.dispose();
    await directory.delete(recursive: true);
  });
  test(
    'persisted local playlists and song identities load without any network',
    () async {
      final playlist = await library.local.importPlaylist(
        name: '离线歌单',
        songs: const [
          Song(
            id: 'offline-song',
            name: '原始歌曲',
            artists: [],
            platform: PlatformType.netease,
          ),
        ],
      );
      final lists = await library.playlists('mine');
      expect(lists.single['title'], '离线歌单');
      final songs = await library.songs({'source': 'mine', 'id': playlist.id});
      expect(songs.single.id, 'offline-song');
      expect(songs.single.platform, PlatformType.netease);
      expect(platform.networkReads, 0);
    },
  );
  test('platform playlists use the retained platform implementation', () async {
    expect((await library.playlists('netease')).single['id'], 'remote');
    expect(
      (await library.songs({
        'source': 'platform',
        'platform': 'netease',
        'id': 'remote',
      })).single.id,
      'offline-song',
    );
    expect(platform.networkReads, 2);
  });
  test('logged-out platforms and invalid sources fail explicitly', () async {
    platform.loggedIn = false;
    await expectLater(
      library.playlists('netease'),
      throwsA(isA<CoreRequestFailure>()),
    );
    await expectLater(library.playlists('local'), throwsArgumentError);
    await expectLater(
      library.songs({'source': 'untrusted'}),
      throwsArgumentError,
    );
    expect(platform.networkReads, 0);
  });
}
