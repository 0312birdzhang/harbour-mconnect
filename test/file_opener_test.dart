import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/utils/file_opener.dart';
import 'package:mconnect/core/platform/platform_utils.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  const channel = MethodChannel('com.mconnect.mconnect/file_opener');
  final calls = <MethodCall>[];

  setUp(() {
    PlatformUtils.setDebugOverride(AppPlatform.windows);
    calls.clear();
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          calls.add(call);
          return true;
        });
  });

  tearDown(() {
    PlatformUtils.setDebugOverride(null);
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null);
  });

  test('openFolder invokes the native openFolder method', () async {
    await FileOpener.openFolder('/storage/emulated/0/Mconnect/netease/mp3');

    expect(calls, hasLength(1));
    expect(calls.single.method, 'openFolder');
    expect(calls.single.arguments, '/storage/emulated/0/Mconnect/netease/mp3');
  });
}
