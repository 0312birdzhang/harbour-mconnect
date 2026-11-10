import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/models/song.dart';
import 'package:mconnect/models/artist.dart';
import 'package:mconnect/models/album.dart';
import 'package:mconnect/platform/base/music_platform.dart';
import 'package:mconnect/platform/base/platform_registry.dart';
import 'package:mconnect/sailfish/qml_media.dart';
import 'package:mconnect/sailfish/core_service.dart';

const track = Song(
  id: 'media',
  platform: PlatformType.netease,
  name: '专辑歌曲',
  artists: [],
);

class MediaPlatform extends MusicPlatform {
  MediaPlatform(this.platformType);
  @override
  final PlatformType platformType;
  @override
  bool get supportsArtistPage => platformType == PlatformType.netease;
  @override
  bool get supportsAlbumPage => platformType == PlatformType.netease;
  int lastPage = 0;
  @override
  Future<Artist?> getArtistDetail(String id) async => Artist(
    id: id,
    name: '歌手',
    briefDesc: '简介',
    avatarUrl: 'https://image.test/artist',
  );
  @override
  Future<List<Song>> getArtistTopSongs(String id, {int limit = 50}) async =>
      const [track];
  @override
  Future<List<Album>> getArtistAlbums(
    String id, {
    int page = 1,
    int limit = 30,
  }) async {
    lastPage = page;
    return const [Album(id: 'album', name: '专辑')];
  }

  @override
  Future<Album?> getAlbumDetail(String id) async => Album(
    id: id,
    name: '专辑',
    artistName: '歌手',
    coverUrl: 'https://image.test/album',
    releaseDate: DateTime(2026, 10, 31),
  );
  @override
  Future<List<Song>> getAlbumSongs(String id) async => const [track];
  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}

void main() {
  test(
    'native metadata returns original sources without Dart image I/O',
    () async {
      PlatformRegistry.register(MediaPlatform(PlatformType.netease));
      final media = QmlMedia(
        nativeArtworkSources: true,
        artwork: (_) async {
          fail('native metadata must not start the Dart cache');
        },
      );
      final artist = await media.artistInfo({
        'platform': 'netease',
        'id': 'artist',
      });
      expect(artist['artworkSource'], 'https://image.test/artist');
      expect(artist.containsKey('artwork'), false);
      final album = await media.albumInfo({
        'platform': 'netease',
        'id': 'album',
      });
      expect(album.containsKey('artworkSource'), true);
    },
  );
  late MediaPlatform platform;
  late QmlMedia media;
  final artwork = <String?>[];
  setUp(() {
    platform = MediaPlatform(PlatformType.netease);
    PlatformRegistry.register(platform);
    PlatformRegistry.register(MediaPlatform(PlatformType.bilibili));
    artwork.clear();
    media = QmlMedia(
      artwork: (url) async {
        artwork.add(url);
        return 'http://127.0.0.1/private-cache';
      },
    );
  });
  test(
    'artist metadata uses shared artwork resolver and preserves songs',
    () async {
      final info = await media.artistInfo({
        'platform': 'netease',
        'id': 'artist',
      });
      expect(info['title'], '歌手');
      expect(info['artwork'], 'http://127.0.0.1/private-cache');
      expect(artwork, ['https://image.test/artist']);
      expect(
        identical(
          (await media.artistSongs({
            'platform': 'netease',
            'id': 'artist',
          })).single,
          track,
        ),
        true,
      );
      await media.artistAlbums({
        'platform': 'netease',
        'id': 'artist',
        'page': 2,
      });
      expect(platform.lastPage, 2);
    },
  );
  test(
    'album information and complete track list come from retained adapter',
    () async {
      final info = await media.albumInfo({
        'platform': 'netease',
        'id': 'album',
      });
      expect(info['date'], '2026-10-31');
      expect(info['artist'], '歌手');
      expect(
        identical(
          (await media.albumSongs({
            'platform': 'netease',
            'id': 'album',
          })).single,
          track,
        ),
        true,
      );
    },
  );
  test('unsupported platform and invalid paging fail explicitly', () async {
    await expectLater(
      media.artistInfo({'platform': 'bilibili', 'id': 'artist'}),
      throwsA(isA<CoreRequestFailure>()),
    );
    await expectLater(
      media.artistAlbums({'platform': 'netease', 'id': 'artist', 'page': 0}),
      throwsArgumentError,
    );
  });
}
