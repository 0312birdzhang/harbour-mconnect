import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/sailfish/external_actions.dart';

void main() {
  test(
    'Sailfish external action passes a literal argument without a shell',
    () async {
      const path = '/home/defaultuser/Music/中文 & music; song.mp3';
      final result = await openExternal(
        path,
        runner: (executable, arguments) async {
          expect(executable, '/usr/bin/xdg-open');
          expect(arguments, [path]);
          return ProcessResult(0, 0, '', '');
        },
      );
      expect(result, isTrue);
      expect(
        await openExternal(
          path,
          runner: (_, _) async => ProcessResult(0, 1, '', 'failed'),
        ),
        isFalse,
      );
    },
  );
}
