import 'dart:convert';
import 'dart:io';
import 'package:crypto/crypto.dart';
import '../features/player/data/player_playback_memory_store.dart';
import '../models/song.dart';
import 'paths.dart' as paths;

typedef RecommendationData = ({List<Song> songs, String label, String note});

/// Metadata only. Never writes playback URLs, cookies or ordinary image files.
class RecommendationCache {
  RecommendationCache({Future<Directory> Function()? directory})
    : directory = directory ?? _defaultDirectory;
  final Future<Directory> Function() directory;
  final _memory = <String, RecommendationData>{};
  final _pending = <String, Future<RecommendationData>>{};
  static Future<Directory> _defaultDirectory() async => Directory(
    '${(await paths.getTemporaryDirectory()).path}/.recommendations',
  );

  Future<RecommendationData> get(
    String scope, {
    required bool refresh,
    required Future<RecommendationData> Function() fetch,
  }) async {
    final key = sha256.convert(utf8.encode(scope)).toString();
    final running = _pending[key];
    if (running != null) return running;
    final result = _load(key, refresh: refresh, fetch: fetch);
    _pending[key] = result;
    try {
      return await result;
    } finally {
      if (identical(_pending[key], result)) {
        _pending.remove(key)?.ignore();
      }
    }
  }

  Future<RecommendationData> _load(
    String key, {
    required bool refresh,
    required Future<RecommendationData> Function() fetch,
  }) async {
    final cached = _memory[key] ?? await _read(key);
    if (cached != null && !refresh) {
      return (
        songs: cached.songs,
        label: cached.label,
        note: [cached.note, '本地缓存'].where((s) => s.isNotEmpty).join(' · '),
      );
    }
    try {
      final value = await fetch();
      _memory[key] = value;
      await _write(key, value);
      return value;
    } catch (_) {
      if (cached != null) {
        return (
          songs: cached.songs,
          label: cached.label,
          note: [
            cached.note,
            '刷新失败，使用本地缓存',
          ].where((s) => s.isNotEmpty).join(' · '),
        );
      }
      rethrow;
    }
  }

  Future<RecommendationData?> _read(String key) async {
    try {
      final file = File('${(await directory()).path}/$key.mcr');
      if (!await file.exists() || await file.length() > 4 * 1024 * 1024) {
        return null;
      }
      final data =
          jsonDecode(await file.readAsString()) as Map<String, dynamic>;
      if (data['version'] != 1 || data['songs'] is! List) return null;
      final songs = <Song>[];
      for (final raw in (data['songs'] as List).take(1000)) {
        final base = PlayerPlaybackMemory.decodeSong(raw);
        if (base == null) return null;
        final row = raw as Map;
        songs.add(
          Song(
            id: base.id,
            platform: base.platform,
            name: base.name,
            artists: base.artists,
            album: base.album,
            duration: base.duration,
            coverUrl: base.coverUrl,
            availableQualities: base.availableQualities,
            albumId: row['albumId'] as String?,
            artistId: row['artistId'] as String?,
            trackNumber: row['trackNumber'] as int?,
            fee: row['fee'] as int?,
          ),
        );
      }
      final result = (
        songs: songs,
        label: data['label'] as String,
        note: data['note'] as String,
      );
      _memory[key] = result;
      return result;
    } catch (_) {
      return null;
    }
  }

  Future<void> _write(String key, RecommendationData value) async {
    try {
      final root = await directory();
      await root.create(recursive: true);
      await File('${root.path}/.nomedia').writeAsString('');
      final text = jsonEncode({
        'version': 1,
        'savedAt': DateTime.now().toIso8601String(),
        'label': value.label,
        'note': value.note,
        'songs': [
          for (final song in value.songs.take(1000))
            {
              ...PlayerPlaybackMemory.encodeSong(song),
              'albumId': song.albumId,
              'artistId': song.artistId,
              'trackNumber': song.trackNumber,
              'fee': song.fee,
            },
        ],
      });
      if (utf8.encode(text).length > 4 * 1024 * 1024) return;
      final temporary = File('${root.path}/$key.tmp');
      await temporary.writeAsString(text, flush: true);
      await temporary.rename('${root.path}/$key.mcr');
    } catch (_) {
      /* A read-only/full cache must not block recommendations. */
    }
  }
}
