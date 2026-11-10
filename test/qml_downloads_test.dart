import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:mconnect/features/download/presentation/providers/download_provider.dart';
import 'package:mconnect/features/download/domain/entities/download_task.dart';
import 'package:mconnect/features/offline_cache/presentation/providers/offline_cache_provider.dart';
import 'package:mconnect/features/offline_cache/data/cache_settings_repository.dart';
import 'package:mconnect/models/audio_quality.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/models/song.dart';
import 'package:mconnect/sailfish/qml_downloads.dart';
import 'package:mconnect/sailfish/core_service.dart';
import 'download_fakes.dart';

const song = Song(
  id: 'BVtest',
  platform: PlatformType.bilibili,
  name: '下载歌曲',
  artists: [],
);

class MemorySettings extends CacheSettingsRepository {
  OfflineCacheSettings value = const OfflineCacheSettings();
  @override
  Future<OfflineCacheSettings> load() async => value;
  @override
  Future<void> save(OfflineCacheSettings settings) async {
    value = settings;
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late ProviderContainer container;
  late FakeDownloadManager manager;
  late DownloadNotifier notifier;
  late QmlDownloads bridge;
  late MemorySettings settings;
  setUp(() {
    manager = FakeDownloadManager();
    settings = MemorySettings();
    notifier = DownloadNotifier(
      manager: manager,
      taskStore: MemoryDownloadTaskStore([]),
      initialState: DownloadState(
        tasks: [
          DownloadTask(
            id: 'done',
            song: song,
            quality: AudioLevel.low,
            status: DownloadStatus.completed,
            progress: 1,
            filePath: '/private/audio.m4a',
            createdAt: DateTime(2026),
          ),
        ],
      ),
    );
    container = ProviderContainer(
      overrides: [
        downloadProvider.overrideWith((ref) => notifier),
        offlineCacheSettingsProvider.overrideWith(
          (ref) => OfflineCacheSettingsNotifier(repository: settings),
        ),
      ],
    );
    bridge = QmlDownloads(container);
  });
  tearDown(() {
    container.dispose();
  });
  test(
    'snapshot is read-only and completed playback preserves original Song',
    () async {
      final state = await bridge.snapshot();
      expect((state['tasks'] as List).single['status'], 'completed');
      expect((state['tasks'] as List).single.containsKey('filePath'), false);
      expect(identical(await bridge.taskSong('done'), song), true);
      expect(manager.started, isEmpty);
    },
  );
  test(
    'manual download and queue controls reuse the real notifier with fake transport',
    () async {
      await bridge.enqueue(song, 'low');
      expect(manager.started, [DownloadTask.buildId(song, AudioLevel.low)]);
      await bridge.control({'action': 'pauseQueue'});
      expect((await bridge.snapshot())['paused'], true);
      await expectLater(
        bridge.control({'action': 'cancel', 'id': 'done'}),
        throwsArgumentError,
      );
      await expectLater(
        bridge.control({'action': 'resume', 'id': 'missing'}),
        throwsA(isA<CoreRequestFailure>()),
      );
    },
  );
  test(
    'settings persist through existing repository and reject invalid values',
    () async {
      final cache = QmlCacheSettings(container);
      expect((await cache.snapshot())['wifiOnly'], true);
      await cache.set({'key': 'offlineMode', 'value': true});
      await cache.set({'key': 'sizeLimitMb', 'value': 2048});
      expect(settings.value.offlineMode, true);
      expect(settings.value.sizeLimitMb, 2048);
      await expectLater(
        cache.set({'key': 'offlineMode', 'value': 'true'}),
        throwsArgumentError,
      );
      await expectLater(
        cache.set({'key': 'sizeLimitMb', 'value': 1}),
        throwsArgumentError,
      );
      await expectLater(
        cache.set({'key': 'unknown', 'value': true}),
        throwsArgumentError,
      );
    },
  );
}
