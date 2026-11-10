import 'dart:async';
import 'dart:convert';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../features/player/presentation/providers/lyrics_provider.dart';
import '../features/player/presentation/providers/player_provider.dart';
import '../features/auth/presentation/providers/auth_provider.dart';
import '../models/song.dart';
import '../platform/base/platform_registry.dart';
import 'core_service.dart';
import 'qml_library.dart';
import 'qml_login_controller.dart';
import 'qml_discovery.dart';
import 'qml_downloads.dart';
import 'qml_local_music.dart';
import 'qml_media.dart';
import 'qml_playlist_editor.dart';
import 'recommendation_cache.dart';
import '../models/platform_type.dart';
import '../features/library/presentation/providers/likes_provider.dart';

/// Owns only presentation state; playback/cache/platform ownership stays intact.
class QmlCoreAdapter {
  QmlCoreAdapter(this.container);
  final ProviderContainer container;
  late final service = SailfishCoreService(dispatch: dispatch);
  late final library = QmlLibrary(container);
  late final discovery = QmlDiscovery(
    cache: RecommendationCache(),
    accountScope: (type) =>
        container.read(authProvider).userFor(type)?.id ?? 'anonymous',
  );
  late final downloads = QmlDownloads(container);
  late final cacheSettings = QmlCacheSettings(container);
  late final localMusic = QmlLocalMusic(container);
  final media = QmlMedia(nativeArtworkSources: true);
  late final editor = QmlPlaylistEditor(library.local);
  late final login = QmlLoginController(
    auth: container.read(authProvider.notifier),
    publish: service.publish,
  );
  final Map<String, List<Song>> _results = {};
  final Map<String, String> _recommendationTokens = {};
  Timer? _timer;
  String _lastState = '';
  int _resultId = 0;
  List<Song>? _lastQueue;
  ProviderSubscription<dynamic>? _lyrics;
  ProviderSubscription<dynamic>? _auth;
  Song resolveSong(Map<String, dynamic> args) {
    if (args['current'] == true) {
      final song = container.read(playerProvider).currentSong;
      if (song == null) throw const CoreRequestFailure('请先选择歌曲');
      return song;
    }
    final songs = _results[args['token']];
    final index = args['index'];
    if (songs == null || index is! int || index < 0 || index >= songs.length) {
      throw const CoreRequestFailure('列表已过期，请返回刷新');
    }
    return songs[index];
  }

  Map<String, Object?> storeSongs(List<Song> songs, {String? recommendation}) {
    final token = '${++_resultId}';
    _results[token] = songs;
    if (recommendation != null) _recommendationTokens[recommendation] = token;
    while (_results.length > 100 ||
        (_results.length > 1 &&
            _results.values.fold<int>(0, (n, s) => n + s.length) > 10000)) {
      final removable = _results.keys.where(
        (key) => key != token && !_recommendationTokens.containsValue(key),
      );
      if (removable.isEmpty) break;
      _results.remove(removable.first);
    }
    return resultPage(token, 0);
  }

  Map<String, Object?> resultPage(String token, int offset) {
    final songs = _results[token];
    if (songs == null) throw const CoreRequestFailure('列表已过期，请返回刷新');
    if (offset < 0 || offset > songs.length) throw ArgumentError();
    return {
      'token': token,
      'total': songs.length,
      'offset': offset,
      'songs': songs.skip(offset).take(100).map(songJson).toList(),
    };
  }

  Map<String, Object?> songJson(Song song) => {
    'id': song.id,
    'platform': song.platform.name,
    'title': song.name,
    'artist': song.artistNames,
    'duration': song.duration.inMilliseconds,
  };
  Map<String, Object?> snapshot() {
    final state = container.read(playerProvider);
    return {
      'version': 1,
      'song': state.currentSong == null ? null : songJson(state.currentSong!),
      'playing': state.isPlaying,
      'busy': state.isTransitioning,
      'position': state.position.inMilliseconds,
      'duration': state.duration.inMilliseconds,
      'index': state.currentIndex,
      'shuffle': state.isShuffle,
      'repeat': state.repeatMode.name,
      'error': state.error == null ? '' : '播放失败，请检查网络或切换歌曲',
    };
  }

  Future<void> start() async {
    await service.start();
    _timer = Timer.periodic(const Duration(milliseconds: 500), (_) {
      final state = snapshot();
      final encoded = jsonEncode(state);
      if (encoded != _lastState) {
        _lastState = encoded;
        unawaited(service.publish('state', state));
      }
      final queue = container.read(playerProvider).playlist;
      if (!identical(queue, _lastQueue)) {
        _lastQueue = queue;
        unawaited(service.publish('queue', queue.map(songJson).toList()));
      }
    });
    _lyrics = container.listen(lyricsProvider, (_, _) {
      unawaited(service.publish('lyrics', lyrics()));
    });
    _auth = container.listen(authProvider, (_, _) {
      unawaited(service.publish('accounts', library.accounts()));
    });
  }

  Map<String, Object?> lyrics() {
    final value = container.read(lyricsProvider);
    return {
      'loading': value.isLoading,
      'error': value.hasError,
      'lines': [
        for (final line in value.asData?.value?.lines ?? [])
          {
            'time': line.timestamp.inMilliseconds,
            'text': line.text,
            'translation': line.translation ?? '',
          },
      ],
    };
  }

  Future<Object?> dispatch(String method, Map<String, dynamic> args) async {
    final player = container.read(playerProvider.notifier);
    switch (method) {
      case 'localSnapshot':
        return localMusic.snapshot();
      case 'localScan':
        await localMusic.scan(args['directory']);
        return {'started': true};
      case 'localGroups':
        return localMusic.groups(args);
      case 'localSongs':
        return storeSongs(await localMusic.songs(args));
      case 'artistInfo':
        return media.artistInfo(args);
      case 'artistSongs':
        return storeSongs(await media.artistSongs(args));
      case 'artistAlbums':
        return media.artistAlbums(args);
      case 'albumInfo':
        return media.albumInfo(args);
      case 'albumSongs':
        return storeSongs(await media.albumSongs(args));
      case 'songInfo':
        final song = resolveSong(args);
        final impl = song.platform == PlatformType.local
            ? null
            : PlatformRegistry.get(song.platform);
        final artists = [
          for (final a in song.artists)
            if (a.id.isNotEmpty && a.id != '0') {'id': a.id, 'name': a.name},
        ];
        if (artists.isEmpty && song.artistId?.isNotEmpty == true) {
          artists.add({'id': song.artistId!, 'name': song.artistNames});
        }
        final liked = await container
            .read(likesProvider.notifier)
            .isLiked(song.id, song.platform);
        return {
          'song': {
            ...songJson(song),
            'artists': artists,
            'albumId': song.albumId?.isNotEmpty == true
                ? song.albumId!
                : song.album?.id ?? '',
            'album': song.album?.name ?? '',
          },
          'reference': storeSongs([song])['token'],
          'canArtist': impl?.supportsArtistPage ?? false,
          'canAlbum': impl?.supportsAlbumPage ?? false,
          'liked': liked,
        };
      case 'resultArtwork':
        return {'source': resolveSong(args).coverUrl ?? ''};
      case 'toggleLike':
        final liked = await container
            .read(likesProvider.notifier)
            .toggleLike(resolveSong(args));
        if (container.read(likesProvider).error != null) {
          throw const CoreRequestFailure('收藏操作失败，请重试');
        }
        return {'liked': liked};
      case 'addToPlaylist':
        await editor.edit({
          'action': 'add',
          'id': args['id'],
        }, song: resolveSong(args));
        return {'ok': true};
      case 'playlistEdit':
        final action = args['action'];
        final song = const ['move', 'remove'].contains(action)
            ? resolveSong(args)
            : null;
        await editor.edit(args, song: song);
        return {'ok': true};
      case 'copyResultToPlaylist':
        final songs = _results[args['token']];
        if (songs == null || songs.isEmpty) {
          throw const CoreRequestFailure('列表为空或已过期，请刷新');
        }
        return library.playlistJson(
          await editor.copy(QmlPlaylistEditor.name(args['name']), songs),
        );
      case 'importPlaylist':
        final link = args['link'];
        if (link is! String) throw ArgumentError();
        return library.playlistJson(await editor.importLink(link));
      case 'exportPlaylist':
        return {
          'link': await editor.exportLink(QmlLibrary.identifier(args['id'])),
        };
      case 'discoveryPlatforms':
        return discovery.platforms();
      case 'discoveryCharts':
        return discovery.charts(args);
      case 'discoverySongs':
        final result = await discovery.songs(args);
        return {
          ...storeSongs(
            result.songs,
            recommendation: args['kind'] == 'daily'
                ? args['platform'] as String?
                : null,
          ),
          'sourceLabel': result.label,
          'sourceNote': result.note,
        };
      case 'downloads':
        return downloads.snapshot();
      case 'downloadControl':
        await downloads.control(args);
        return {'ok': true};
      case 'downloadEnqueue':
        Song? song;
        if (args['current'] == true) {
          song = container.read(playerProvider).currentSong;
        } else {
          final songs = _results[args['token']];
          final index = args['index'];
          if (songs == null ||
              index is! int ||
              index < 0 ||
              index >= songs.length) {
            throw ArgumentError();
          }
          song = songs[index];
        }
        if (song == null) throw const CoreRequestFailure('请先选择歌曲');
        await downloads.enqueue(song, args['quality']);
        return {'ok': true};
      case 'playDownload':
        await player.playSong(await downloads.taskSong(args['id']));
        return {'ok': true};
      case 'cacheSettings':
        return cacheSettings.snapshot();
      case 'setCacheSetting':
        return cacheSettings.set(args);
      case 'toggleShuffle':
        player.toggleShuffle();
        return {'ok': true};
      case 'cycleRepeat':
        player.cycleRepeatMode();
        return {'ok': true};
      case 'accounts':
        return library.accounts();
      case 'loginBegin':
        return login.begin(
          QmlLibrary.platform(args['platform']),
          variant: args['variant'] as String?,
          session: QmlLibrary.identifier(args['session']),
        );
      case 'loginStatus':
        return login.state;
      case 'loginCancel':
        await login.cancel(args['session'] as String?);
        return {'ok': true};
      case 'logout':
        final platform = QmlLibrary.platform(args['platform']);
        if (login.state['platform'] == platform.name) await login.cancel(null);
        await container.read(authProvider.notifier).logout(platform);
        return {'ok': true};
      case 'libraryPlaylists':
        return library.playlists(args['source']);
      case 'librarySongs':
        return storeSongs(await library.songs(args));
      case 'resultPage':
        final offset = args['offset'];
        if (offset is! int) throw ArgumentError();
        return resultPage(QmlLibrary.identifier(args['token']), offset);
      case 'createPlaylist':
        final name = QmlLibrary.identifier(args['name']);
        if (name.length > 200) throw ArgumentError();
        return library.playlistJson(await editor.create(name));
      case 'addCurrentToPlaylist':
        final song = container.read(playerProvider).currentSong;
        if (song == null) throw const CoreRequestFailure('请先选择歌曲');
        await editor.edit({
          'action': 'add',
          'id': QmlLibrary.identifier(args['id']),
        }, song: song);
        return {'ok': true};
      case 'snapshot':
        return snapshot();
      case 'platforms':
        return [
          for (final p in PlatformRegistry.all)
            {'id': p.platformType.name, 'name': p.platformName},
        ];
      case 'queue':
        return container.read(playerProvider).playlist.map(songJson).toList();
      case 'lyrics':
        return lyrics();
      case 'artwork':
        final song = container.read(playerProvider).currentSong;
        final url =
            song?.coverUrl ??
            (song?.platform == PlatformType.local
                ? await localMusic.cover(song!.id)
                : null);
        return {'source': url ?? ''};
      case 'search':
      case 'searchPlaylists':
        final query = args['query'];
        final platform = args['platform'];
        final page = args['page'] ?? 1;
        if (query is! String ||
            query.trim().isEmpty ||
            query.length > 256 ||
            page is! int ||
            page < 1 ||
            page > 100) {
          throw ArgumentError();
        }
        final matches = PlatformRegistry.all.where(
          (p) => p.platformType.name == platform,
        );
        if (matches.length != 1) throw ArgumentError();
        if (method == 'searchPlaylists') {
          final playlists = await matches.single
              .searchPlaylists(query.trim(), page: page, limit: 30)
              .timeout(const Duration(seconds: 20));
          return {'playlists': playlists.map(library.playlistJson).toList()};
        }
        final songs = await matches.single
            .search(query.trim(), page: page, limit: 30)
            .timeout(const Duration(seconds: 20));
        return storeSongs(songs);
      case 'playResult':
        final songs = _results[args['token']];
        final index = args['index'];
        if (songs == null ||
            index is! int ||
            index < 0 ||
            index >= songs.length) {
          throw ArgumentError();
        }
        await player.playPlaylist(songs, startIndex: index);
        return true;
      case 'toggle':
        await player.togglePlay();
        return true;
      case 'next':
        await player.skipToNext();
        return true;
      case 'previous':
        await player.skipToPrevious();
        return true;
      case 'seek':
        final position = args['position'];
        final duration = container.read(playerProvider).duration.inMilliseconds;
        if (position is! int || position < 0 || position > duration) {
          throw ArgumentError();
        }
        await player.seek(Duration(milliseconds: position));
        return true;
      case 'playIndex':
        final index = args['index'];
        final length = container.read(playerProvider).playlist.length;
        if (index is! int || index < 0 || index >= length) {
          throw ArgumentError();
        }
        await player.playAtIndex(index);
        return true;
      default:
        throw ArgumentError('Unsupported operation');
    }
  }

  Future<void> close() async {
    _timer?.cancel();
    _lyrics?.close();
    _auth?.close();
    await login.cancel(null);
    await service.close();
  }
}
