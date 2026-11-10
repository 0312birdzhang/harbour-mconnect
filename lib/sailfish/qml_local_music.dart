import 'dart:async';
import 'dart:io';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:path/path.dart' as p;
import '../features/local_music/presentation/providers/local_music_provider.dart';
import '../features/local_music/data/local_scan_root_store.dart';
import '../features/local_music/domain/local_library_grouping.dart';
import '../models/song.dart';
import 'core_service.dart';

class QmlLocalMusic {
  QmlLocalMusic(this.container, {LocalScanRootStore? roots})
    : roots = roots ?? HiveLocalScanRootStore();
  final ProviderContainer container;
  final LocalScanRootStore roots;
  Future<void>? _ready;
  Future<void>? _scan;
  bool _starting = false;
  String? _error;
  Future<void> ready() =>
      _ready ??= container.read(localMusicProvider.notifier).loadFromIndex();
  Future<Map<String, Object?>> snapshot() async {
    await ready();
    final state = container.read(localMusicProvider);
    return {
      'count': state.songs.length,
      'scanning': _starting || _scan != null || state.isScanning,
      'directory':
          state.selectedDirectory ??
          p.join(Platform.environment['HOME'] ?? '/home/defaultuser', 'Music'),
      'error': _error ?? (state.error == null ? '' : '读取或扫描本地音乐失败，请检查目录'),
      'parsed': state.lastScan?.parsedCount ?? 0,
      'reused': state.lastScan?.reusedCount ?? 0,
      'removed': state.lastScan?.removedCount ?? 0,
      'skipped': state.skippedFiles.length,
    };
  }

  Future<void> scan(Object? input) async {
    await ready();
    if (_starting || _scan != null) {
      throw const CoreRequestFailure('正在扫描，请等待完成');
    }
    _starting = true;
    try {
      if (input is! String || input.isEmpty || input.length > 4096) {
        throw ArgumentError();
      }
      final expanded = input.startsWith('~/')
          ? p.join(
              Platform.environment['HOME'] ?? '/home/defaultuser',
              input.substring(2),
            )
          : input;
      if (!p.isAbsolute(expanded)) {
        throw const CoreRequestFailure('请输入完整的音乐目录路径');
      }
      final directory = Directory(expanded);
      if (!await directory.exists()) {
        throw const CoreRequestFailure('目录不存在或无法访问');
      }
      final path = await directory.resolveSymbolicLinks();
      if (path == '/' ||
          const [
            '/proc',
            '/sys',
            '/dev',
          ].any((root) => path == root || p.isWithin(root, path))) {
        throw const CoreRequestFailure('请选择音乐目录');
      }
      _error = null;
      _scan = _runScan(path);
      unawaited(_scan!);
    } finally {
      _starting = false;
    }
  }

  Future<void> _runScan(String path) async {
    try {
      await container.read(localMusicProvider.notifier).scanDirectory(path);
      if (container.read(localMusicProvider).error == null) {
        await roots.write(path);
      }
    } catch (_) {
      _error = '本地音乐扫描失败，请检查目录后重试';
    } finally {
      _scan = null;
    }
  }

  Future<List<LocalTrackGroup>> _groups(
    String view, {
    String keyword = '',
  }) async {
    await ready();
    final tracks = container
        .read(localMusicProvider)
        .tracks
        .where(
          (t) =>
              keyword.isEmpty ||
              '${t.displayTitle} ${t.displayArtist} ${t.displayAlbum}'
                  .toLowerCase()
                  .contains(keyword.toLowerCase()),
        );
    switch (view) {
      case 'albums':
        return LocalLibraryGrouping.byAlbum(tracks);
      case 'artists':
        return LocalLibraryGrouping.byArtist(tracks);
      case 'folders':
        return LocalLibraryGrouping.byFolder(tracks);
      default:
        throw ArgumentError();
    }
  }

  Future<List<Map<String, Object?>>> groups(Map<String, dynamic> args) async {
    final view = args['view'];
    if (view is! String) throw ArgumentError();
    return [
      for (final group in await _groups(view))
        {
          'id': group.id,
          'title': group.title,
          'subtitle': group.subtitle ?? '',
          'count': group.trackCount,
        },
    ];
  }

  Future<List<Song>> songs(Map<String, dynamic> args) async {
    await ready();
    final view = args['view'] ?? 'songs';
    if (view == 'songs') {
      final keyword = args['keyword'] ?? '';
      if (keyword is! String || keyword.length > 256) throw ArgumentError();
      return container
          .read(localMusicProvider)
          .songs
          .where(
            (s) =>
                keyword.isEmpty ||
                '${s.name} ${s.artistNames} ${s.album?.name ?? ''}'
                    .toLowerCase()
                    .contains(keyword.toLowerCase()),
          )
          .toList();
    }
    if (view is! String || args['id'] is! String) throw ArgumentError();
    final matches = (await _groups(view)).where((g) => g.id == args['id']);
    if (matches.length != 1) throw const CoreRequestFailure('本地分组已变化，请返回刷新');
    return matches.single.tracks.map((t) => t.toSong()).toList();
  }

  Future<String?> cover(String songId) async {
    await ready();
    for (final track in container.read(localMusicProvider).tracks) {
      if (track.path == songId) return track.coverPath;
    }
    return null;
  }
}
