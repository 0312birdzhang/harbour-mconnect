import '../core/cache/artwork_cache.dart';
import '../models/album.dart';
import '../models/song.dart';
import '../platform/base/music_platform.dart';
import '../platform/base/platform_registry.dart';
import 'core_service.dart';
import 'qml_library.dart';

class QmlMedia {
  QmlMedia({this.artwork = cachedArtwork, this.nativeArtworkSources = false});
  final bool nativeArtworkSources;
  final Future<String> Function(String? url) artwork;
  static Future<String> cachedArtwork(String? url) async {
    if (url == null || url.isEmpty) return '';
    try {
      return await ArtworkCache.instance
          .mediaUrl(url)
          .timeout(const Duration(seconds: 6));
    } catch (_) {
      return '';
    }
  }

  MusicPlatform _platform(Map<String, dynamic> args, {bool artist = false}) {
    final impl = PlatformRegistry.get(QmlLibrary.platform(args['platform']));
    if (artist ? !impl.supportsArtistPage : !impl.supportsAlbumPage) {
      throw const CoreRequestFailure('该平台暂不支持此资料页');
    }
    return impl;
  }

  Map<String, Object?> albumJson(Album album) => {
    'id': album.id,
    'title': album.name,
    'artist': album.artistName ?? '',
    'date': album.releaseDate?.toIso8601String().split('T').first ?? '',
    'count': album.songCount ?? 0,
  };
  Future<Map<String, Object?>> artistInfo(Map<String, dynamic> args) async {
    final impl = _platform(args, artist: true);
    final artist = await impl
        .getArtistDetail(QmlLibrary.identifier(args['id']))
        .timeout(const Duration(seconds: 12));
    if (artist == null) throw const CoreRequestFailure('歌手资料暂不可用');
    return {
      'title': artist.name,
      'description': artist.briefDesc ?? '',
      'songCount': artist.songCount ?? 0,
      'albumCount': artist.albumCount ?? 0,
      'albumsSupported': impl.supportsAlbumPage,
      if (nativeArtworkSources)
        'artworkSource': artist.avatarUrl ?? ''
      else
        'artwork': await artwork(artist.avatarUrl),
    };
  }

  Future<List<Song>> artistSongs(Map<String, dynamic> args) =>
      _platform(args, artist: true)
          .getArtistTopSongs(QmlLibrary.identifier(args['id']), limit: 100)
          .timeout(const Duration(seconds: 20));
  Future<List<Map<String, Object?>>> artistAlbums(
    Map<String, dynamic> args,
  ) async {
    final page = args['page'] ?? 1;
    if (page is! int || page < 1 || page > 100) throw ArgumentError();
    final albums = await _platform(args, artist: true)
        .getArtistAlbums(
          QmlLibrary.identifier(args['id']),
          page: page,
          limit: 30,
        )
        .timeout(const Duration(seconds: 20));
    return albums.map(albumJson).toList();
  }

  Future<Map<String, Object?>> albumInfo(Map<String, dynamic> args) async {
    final album = await _platform(args)
        .getAlbumDetail(QmlLibrary.identifier(args['id']))
        .timeout(const Duration(seconds: 12));
    if (album == null) throw const CoreRequestFailure('专辑资料暂不可用');
    return {
      ...albumJson(album),
      'description': album.description ?? '',
      'company': album.company ?? '',
      'genre': album.genre ?? '',
      if (nativeArtworkSources)
        'artworkSource': album.coverUrl ?? ''
      else
        'artwork': await artwork(album.coverUrl),
    };
  }

  Future<List<Song>> albumSongs(Map<String, dynamic> args) => _platform(args)
      .getAlbumSongs(QmlLibrary.identifier(args['id']))
      .timeout(const Duration(seconds: 20));
}
