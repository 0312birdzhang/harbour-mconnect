import 'dart:io';
import 'package:path/path.dart' as p;
import 'package:path_provider_platform_interface/path_provider_platform_interface.dart';

String get sailfishHome => Platform.environment['HOME'] ?? '/home/defaultuser';
String get sailfishDataPath => p.join(
  Platform.environment['XDG_DATA_HOME'] ??
      p.join(sailfishHome, '.local', 'share'),
  'harbour-mconnect',
);

Future<Directory> _directory(String path) =>
    Directory(path).create(recursive: true);

// Keep private app state separate from the user's Documents and Music folders.
Future<Directory> getApplicationDocumentsDirectory() =>
    getApplicationSupportDirectory();
Future<Directory> getApplicationSupportDirectory() =>
    _directory(sailfishDataPath);
Future<Directory> getTemporaryDirectory() => _directory(
  p.join(
    Platform.environment['XDG_CACHE_HOME'] ?? p.join(sailfishHome, '.cache'),
    'harbour-mconnect',
  ),
);
Future<Directory?> getDownloadsDirectory() =>
    _directory(p.join(sailfishHome, 'Downloads'));

// Android-only callers expect this symbol; Sailfish uses ordinary home paths.
Future<Directory?> getExternalStorageDirectory() async => null;

/// Also serves transitive users such as cached_network_image's disk cache.
class SailfishPathProvider extends PathProviderPlatform {
  @override
  Future<String?> getTemporaryPath() async =>
      (await getTemporaryDirectory()).path;
  @override
  Future<String?> getApplicationSupportPath() async =>
      (await getApplicationSupportDirectory()).path;
  @override
  Future<String?> getApplicationDocumentsPath() async =>
      (await getApplicationDocumentsDirectory()).path;
  @override
  Future<String?> getApplicationCachePath() async =>
      (await getTemporaryDirectory()).path;
  @override
  Future<String?> getDownloadsPath() async =>
      (await getDownloadsDirectory())?.path;
}
