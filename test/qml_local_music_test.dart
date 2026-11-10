import 'dart:async';
import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:mconnect/features/local_music/data/local_music_repository.dart';
import 'package:mconnect/features/local_music/data/local_track_store.dart';
import 'package:mconnect/features/local_music/data/local_lyrics_store.dart';
import 'package:mconnect/features/local_music/data/local_scan_root_store.dart';
import 'package:mconnect/features/local_music/data/online_library_snapshot.dart';
import 'package:mconnect/features/local_music/data/track_ratings_store.dart';
import 'package:mconnect/features/local_music/presentation/providers/local_music_provider.dart';
import 'package:mconnect/sailfish/qml_local_music.dart';
import 'package:mconnect/sailfish/core_service.dart';
import 'local_music_fixtures.dart';

class DelayedScan implements LocalMusicScanner {
  final pending = Completer<LocalMusicScanResult>();
  @override
  Future<LocalMusicScanResult> scanDirectory(String path) => pending.future;
}

void main() {
  late Directory temp;
  late ProviderContainer container;
  late QmlLocalMusic bridge;
  late MemoryLocalScanRootStore roots;
  void setupBridge({LocalMusicScanner? scanner}) {
    final tracks = MemoryLocalTrackStore();
    final lyrics = MemoryLocalLyricsStore();
    roots = MemoryLocalScanRootStore();
    final repository = LocalMusicRepository(
      trackStore: tracks,
      lyricsStore: lyrics,
      scanInIsolate: true,
      coverDirectoryPath: '${temp.path}/covers',
    );
    final notifier = LocalMusicNotifier(
      scanner: scanner ?? repository,
      trackStore: tracks,
      lyricsStore: lyrics,
      rootStore: roots,
      onlineSnapshot: MemoryOnlineLibrarySnapshot(),
      ratingsStore: MemoryTrackRatingsStore(),
    );
    container = ProviderContainer(
      overrides: [localMusicProvider.overrideWith((ref) => notifier)],
    );
    bridge = QmlLocalMusic(container, roots: roots);
  }

  Future<Map<String, Object?>> completeScan() async {
    for (var i = 0; i < 500; i++) {
      final state = await bridge.snapshot();
      if (state['scanning'] == false) return state;
      await Future<void>.delayed(const Duration(milliseconds: 10));
    }
    throw StateError('Scan did not finish');
  }

  setUp(() async {
    temp = await Directory.systemTemp.createTemp('qml-local-');
    setupBridge();
  });
  tearDown(() async {
    container.dispose();
    await temp.delete(recursive: true);
  });
  test(
    'opening local library does not scan, explicit scan reads real tags and lyrics',
    () async {
      final file = File('${temp.path}/song.flac');
      await file.writeAsBytes(
        buildFlacFixture(title: '歌曲', artist: '歌手', album: '专辑'),
      );
      await File('${temp.path}/song.lrc').writeAsString('[00:01.00]本地歌词');
      expect((await bridge.snapshot())['count'], 0);
      await bridge.scan(temp.path);
      final state = await completeScan();
      expect(state['count'], 1);
      expect(state['parsed'], 1);
      expect(await roots.read(), await temp.resolveSymbolicLinks());
      final groups = await bridge.groups({'view': 'albums'});
      expect(groups.single['title'], '专辑');
      final songs = await bridge.songs({'view': 'albums', 'id': '专辑'});
      expect(songs.single.id, file.path);
      expect(
        container.read(localMusicProvider).lyricsFor(file.path),
        contains('本地歌词'),
      );
      await bridge.scan(temp.path);
      final second = await completeScan();
      expect(second['reused'], 1);
      expect(second['parsed'], 0);
    },
  );
  test('parallel scan is rejected without waiting for metadata work', () async {
    container.dispose();
    final scanner = DelayedScan();
    setupBridge(scanner: scanner);
    await bridge.scan(temp.path);
    expect((await bridge.snapshot())['scanning'], true);
    await expectLater(
      bridge.scan(temp.path),
      throwsA(isA<CoreRequestFailure>()),
    );
    scanner.pending.complete(const LocalMusicScanResult());
    await completeScan();
  });
  test(
    'relative and system roots are rejected without starting a scan',
    () async {
      await expectLater(
        bridge.scan('Music'),
        throwsA(isA<CoreRequestFailure>()),
      );
      await expectLater(
        bridge.scan('/proc'),
        throwsA(isA<CoreRequestFailure>()),
      );
      expect((await bridge.snapshot())['scanning'], false);
    },
  );
}
