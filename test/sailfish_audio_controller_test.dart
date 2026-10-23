import 'dart:ffi';
import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:just_audio/just_audio.dart' show ProcessingState;
import 'package:mconnect/sailfish/audio_controller.dart';

void main() {
  group('Sailfish audio FFI lifecycle', () {
    late Directory temporary;
    late DynamicLibrary library;
    late void Function(int) status;
    late void Function(int) openStatus;
    late int Function() useAfterDestroy;

    setUpAll(() async {
      temporary = await Directory.systemTemp.createTemp('mconnect-audio-ffi-');
      final output = '${temporary.path}/audio.so';
      final result = await Process.run('cc', ['-shared', '-fPIC',
        'test/fixtures/audio_ffi_fixture.c', '-o', output]);
      expect(result.exitCode, 0, reason: result.stderr.toString());
      library = DynamicLibrary.open(output);
      status = library.lookupFunction<Void Function(Int32), void Function(int)>('mc_test_status');
      openStatus = library.lookupFunction<Void Function(Int32), void Function(int)>('mc_test_open_status');
      useAfterDestroy = library.lookupFunction<Int32 Function(), int Function()>('mc_test_use_after_destroy');
    });
    tearDownAll(() => temporary.delete(recursive: true));

    test('buffering retains play intent, EOS completes, and decoder errors reach the caller', () async {
      openStatus(2);
      final controller = SailfishAudioController(library: library);
      addTearDown(controller.dispose);
      await controller.setUrl('https://example.test/audio');
      await controller.play();
      expect(controller.playing, isTrue);
      final buffering = controller.playerStateStream.firstWhere(
        (state) => state.processingState == ProcessingState.buffering);
      status(1);
      expect((await buffering.timeout(const Duration(seconds: 2))).playing, isTrue);
      expect(controller.playing, isTrue);
      final completed = controller.playerStateStream.firstWhere(
        (state) => state.processingState == ProcessingState.completed);
      status(4);
      expect((await completed.timeout(const Duration(seconds: 2))).playing, isFalse);
      await controller.seek(Duration.zero);
      await controller.play();
      expect(controller.playing, isTrue);
      await controller.setVolume(2);
      expect(controller.volume, 1);
      await controller.setPlaybackSpeed(1.5);
      expect(controller.playbackSpeed, 1.5);
      await controller.applyEqualizer(enabled: true, bandGains: [1, 2, 3, 4, 5]);
      final failure = expectLater(controller.playerStateStream.first,
        throwsA(isA<StateError>().having((error) => error.message, 'message', 'Test decoder failure')));
      status(5);
      await failure;
    });

    test('disposing during load cancels the operation without touching a destroyed handle', () async {
      openStatus(1);
      final controller = SailfishAudioController(library: library);
      final load = controller.setUrl('https://example.test/pending');
      final failure = expectLater(load, throwsStateError);
      await controller.dispose();
      await failure;
      expect(useAfterDestroy(), 0);
    });
  }, skip: !Platform.isLinux);
}
