import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../features/auth/presentation/providers/auth_provider.dart';
import '../features/library/data/my_playlists_repository.dart';
import '../features/library/presentation/providers/history_provider.dart';
import '../features/library/presentation/providers/likes_provider.dart';
import '../models/platform_type.dart';
import '../models/playlist.dart';
import '../models/song.dart';
import '../platform/base/platform_registry.dart';
import 'core_service.dart';

class QmlLibrary {
  QmlLibrary(this.container, {this.local = const MyPlaylistsRepository()});
  final ProviderContainer container;
  final MyPlaylistsRepository local;

  static PlatformType platform(Object? name) {
    if (name is! String) throw ArgumentError();
    final type = PlatformType.parse(name);
    if (!type.isMusicService) throw ArgumentError();
    return type;
  }

  static String identifier(Object? value) {
    if (value is! String || value.trim().isEmpty || value.length > 512) {
      throw ArgumentError();
    }
    return value;
  }

  List<Map<String, Object?>> accounts() {
    final auth = container.read(authProvider);
    return [
      for (final p in PlatformRegistry.all)
        {
          'id': p.platformType.name,
          'name': p.platformName,
          'loggedIn': auth.isLoggedIn(p.platformType),
          'nickname': auth.userFor(p.platformType)?.nickname ?? '',
        },
    ];
  }

  Map<String, Object?> playlistJson(Playlist p) => {
    'id': p.id,
    'title': p.name,
    'platform': p.platform.name,
    'count': p.songCount,
    'creator': p.creatorName ?? '',
  };

  Future<List<Map<String, Object?>>> playlists(Object? source) async {
    final List<Playlist> items;
    if (source == 'mine') {
      items = await local.getPlaylists();
    } else {
      final impl = PlatformRegistry.get(platform(source));
      if (!impl.isLoggedIn) throw const CoreRequestFailure('请先登录该音乐平台');
      items = await impl.getUserPlaylists().timeout(
        const Duration(seconds: 20),
      );
    }
    return items.where((p) => p.id.isNotEmpty).map(playlistJson).toList();
  }

  Future<List<Song>> songs(Map<String, dynamic> args) async {
    switch (args['source']) {
      case 'mine':
        return local.getSongs(identifier(args['id']));
      case 'likes':
        await container.read(likesProvider.notifier).loadLikes();
        final value = container.read(likesProvider);
        if (value.error != null) throw StateError('Likes unavailable');
        return value.songs;
      case 'history':
        await container.read(historyProvider.notifier).loadHistory();
        final value = container.read(historyProvider);
        if (value.error != null) throw StateError('History unavailable');
        return value.entries.map((e) => e.song).toList();
      case 'platform':
        return PlatformRegistry.get(platform(args['platform']))
            .getPlaylistDetail(identifier(args['id']))
            .timeout(const Duration(seconds: 20));
      default:
        throw ArgumentError();
    }
  }
}
