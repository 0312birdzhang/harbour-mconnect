import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/sailfish/secret_storage.dart';

void main() {
  test(
    'credentials survive a new instance, retain concurrent writes and restrict Unix permissions',
    () async {
      final root = await Directory.systemTemp.createTemp(
        'mconnect-credentials-',
      );
      addTearDown(() => root.delete(recursive: true));
      final storage = SailfishCredentialFiles(directory: root);
      await Future.wait([
        storage.write(key: 'cookie_netease', value: 'session-a'),
        storage.write(key: 'cookie_qq', value: 'session-b'),
        storage.write(key: '中文/特殊键', value: 'session-c'),
      ]);
      final reopened = SailfishCredentialFiles(directory: root);
      expect(await reopened.readAll(), {
        'cookie_netease': 'session-a',
        'cookie_qq': 'session-b',
        '中文/特殊键': 'session-c',
      });
      final directory = Directory('${root.path}/credentials');
      expect(
        (await Process.run('/usr/bin/stat', [
          '-c',
          '%a',
          directory.path,
        ])).stdout.toString().trim(),
        '700',
      );
      await for (final file in directory.list()) {
        expect(
          (await Process.run('/usr/bin/stat', [
            '-c',
            '%a',
            file.path,
          ])).stdout.toString().trim(),
          '600',
        );
      }
      await reopened.write(key: 'cookie_netease', value: 'replacement');
      expect(await reopened.read(key: 'cookie_netease'), 'replacement');
      await reopened.delete(key: 'cookie_qq');
      expect(await reopened.read(key: 'cookie_qq'), isNull);
      expect((await reopened.readAll()).length, 2);
    },
    skip: !Platform.isLinux,
  );
}
