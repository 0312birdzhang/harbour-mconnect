import 'package:flutter_riverpod/flutter_riverpod.dart';
import '../features/download/domain/entities/download_task.dart';
import '../features/download/presentation/providers/download_provider.dart';
import '../features/offline_cache/presentation/providers/offline_cache_provider.dart';
import '../models/audio_quality.dart';
import '../models/platform_type.dart';
import '../models/song.dart';
import 'core_service.dart';
import 'qml_library.dart';

class QmlDownloads {
  QmlDownloads(this.container);
  final ProviderContainer container;

  Future<DownloadNotifier> ready() async {
    final notifier = container.read(downloadProvider.notifier);
    await notifier.ready.timeout(const Duration(seconds: 8));
    return notifier;
  }

  Future<Map<String, Object?>> snapshot() async {
    await ready();
    final state = container.read(downloadProvider);
    return {
      'paused': state.queuePaused,
      'blocked': state.queueBlockedReason ?? '',
      'tasks': [
        for (final task in state.tasks)
          {
            'id': task.id,
            'title': task.song.name,
            'artist': task.song.artistNames,
            'platform': task.song.platform.name,
            'quality': task.quality.displayNameFor(task.song.platform),
            'status': task.status.name,
            'progress': task.progress,
            'bytes': task.downloadedBytes,
            'totalBytes': task.totalBytes ?? 0,
            'cache': task.isOfflineCache,
            'failure': task.failureKind?.name ?? '',
          },
      ],
    };
  }

  Future<void> enqueue(Song song, Object? level) async {
    if (song.platform == PlatformType.local) {
      throw const CoreRequestFailure('本地歌曲无需下载');
    }
    final choices = AudioLevel.values.where((q) => q.name == level);
    if (choices.length != 1) throw ArgumentError();
    final notifier = await ready();
    if (!await notifier
        .checkVipForDownload(song, choices.single)
        .timeout(const Duration(seconds: 15))) {
      throw const CoreRequestFailure('当前账号不支持该音质，请选择标准音质或登录会员账号');
    }
    await notifier.startDownload(song, choices.single);
  }

  Future<Song> taskSong(Object? id) async {
    await ready();
    final key = QmlLibrary.identifier(id);
    final tasks = container
        .read(downloadProvider)
        .tasks
        .where((t) => t.id == key);
    if (tasks.length != 1 || tasks.single.status != DownloadStatus.completed) {
      throw const CoreRequestFailure('下载尚未完成，请刷新列表');
    }
    return tasks.single.song;
  }

  Future<void> control(Map<String, dynamic> args) async {
    final notifier = await ready();
    switch (args['action']) {
      case 'pauseQueue':
        notifier.pauseQueue();
        return;
      case 'resumeQueue':
        await notifier.resumeQueue();
        return;
    }
    final id = QmlLibrary.identifier(args['id']);
    final tasks = container
        .read(downloadProvider)
        .tasks
        .where((t) => t.id == id);
    if (tasks.length != 1) throw const CoreRequestFailure('下载任务不存在，请刷新');
    switch (args['action']) {
      case 'pause':
        notifier.pauseDownload(id);
      case 'resume':
        await notifier.resumeDownload(id);
      case 'cancel':
        if (tasks.single.status == DownloadStatus.completed) {
          throw ArgumentError();
        }
        notifier.cancelDownload(id);
      default:
        throw ArgumentError();
    }
  }
}

class QmlCacheSettings {
  QmlCacheSettings(this.container);
  final ProviderContainer container;

  Future<Map<String, dynamic>> snapshot() async {
    await container.read(offlineCacheSettingsProvider.notifier).ready;
    return container.read(offlineCacheSettingsProvider).toJson();
  }

  Future<Map<String, dynamic>> set(Map<String, dynamic> args) async {
    final notifier = container.read(offlineCacheSettingsProvider.notifier);
    await notifier.ready;
    final key = args['key'];
    final value = args['value'];
    if (key == 'sizeLimitMb') {
      if (value is! int || value < 128 || value > 32768) throw ArgumentError();
      await notifier.setSizeLimitMb(value);
    } else {
      if (value is! bool) throw ArgumentError();
      switch (key) {
        case 'wifiOnly':
          await notifier.setWifiOnly(value);
        case 'autoRetry':
          await notifier.setAutoRetry(value);
        case 'autoCleanup':
          await notifier.setAutoCleanup(value);
        case 'offlineMode':
          await notifier.setOfflineMode(value);
        default:
          throw ArgumentError();
      }
    }
    return snapshot();
  }
}
