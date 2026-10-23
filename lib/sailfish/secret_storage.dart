import 'dart:convert';
import 'dart:io';
import 'package:path/path.dart' as p;
import 'package:flutter_secure_storage_platform_interface/flutter_secure_storage_platform_interface.dart';
import 'paths.dart';

/// Sailfish file-backed credentials, protected with Unix permissions.
/// This is NOT an OS keystore or encrypted-at-rest storage.
class SailfishCredentialFiles {
  const SailfishCredentialFiles({this.directory});
  final Directory? directory;
  static Future<void> _pending = Future<void>.value();

  Future<T> _serialized<T>(Future<T> Function() action) {
    final result = _pending.then((_) => action());
    _pending = result.then<void>((_) {}, onError: (Object _, StackTrace _) {});
    return result;
  }

  Future<Directory> _root() async {
    final base = directory ?? await getApplicationSupportDirectory();
    final root = await Directory(
      p.join(base.path, 'credentials'),
    ).create(recursive: true);
    await _chmod('700', root.path);
    return root;
  }

  Future<void> _chmod(String mode, String path) async {
    final result = await Process.run('/bin/chmod', [mode, path]);
    if (result.exitCode != 0) {
      throw FileSystemException('Cannot protect credentials', path);
    }
  }

  String _name(String key) => base64Url.encode(utf8.encode(key));

  Future<String?> read({required String key}) => _serialized(() async {
    final file = File(p.join((await _root()).path, _name(key)));
    return await file.exists() ? file.readAsString() : null;
  });

  Future<void> write({required String key, required String? value}) =>
      _serialized(() async {
        final file = File(p.join((await _root()).path, _name(key)));
        if (value == null) {
          if (await file.exists()) await file.delete();
          return;
        }
        final temporary = File('${file.path}.tmp');
        await temporary.create();
        await _chmod('600', temporary.path);
        await temporary.writeAsString(value, flush: true);
        await temporary.rename(file.path);
      });

  Future<void> delete({required String key}) => write(key: key, value: null);

  Future<Map<String, String>> readAll() => _serialized(() async {
    final result = <String, String>{};
    await for (final entity in (await _root()).list()) {
      if (entity is! File || entity.path.endsWith('.tmp')) continue;
      final key = utf8.decode(base64Url.decode(p.basename(entity.path)));
      result[key] = await entity.readAsString();
    }
    return result;
  });
}

class SailfishSecureStoragePlatform extends FlutterSecureStoragePlatform {
  final _files = const SailfishCredentialFiles();

  @override
  Future<void> write({
    required String key,
    required String value,
    required Map<String, String> options,
  }) => _files.write(key: key, value: value);
  @override
  Future<String?> read({
    required String key,
    required Map<String, String> options,
  }) => _files.read(key: key);
  @override
  Future<void> delete({
    required String key,
    required Map<String, String> options,
  }) => _files.delete(key: key);
  @override
  Future<bool> containsKey({
    required String key,
    required Map<String, String> options,
  }) async => await _files.read(key: key) != null;
  @override
  Future<Map<String, String>> readAll({required Map<String, String> options}) =>
      _files.readAll();
  @override
  Future<void> deleteAll({required Map<String, String> options}) async {
    for (final key in (await _files.readAll()).keys) {
      await _files.delete(key: key);
    }
  }
}
