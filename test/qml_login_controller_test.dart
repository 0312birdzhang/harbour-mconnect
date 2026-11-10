import 'dart:async';
import 'dart:convert';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/features/auth/presentation/providers/auth_provider.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/models/user.dart';
import 'package:mconnect/platform/base/music_platform.dart';
import 'package:mconnect/sailfish/qml_login_controller.dart';

class FakeAuth extends AuthNotifier {
  final statuses = StreamController<QrLoginStatus>.broadcast();
  Completer<QrLoginResult>? pending;
  int saves = 0;
  @override
  Future<QrLoginResult> getQrCodeWithVariant(
    PlatformType platform, {
    String? authVariant,
  }) async =>
      pending?.future ??
      const QrLoginResult(
        key: 'private-qr-key',
        qrUrl: 'https://example.test/login',
      );
  @override
  Stream<QrLoginStatus> pollQrStatus(
    PlatformType platform,
    String key, {
    String? authVariant,
  }) => statuses.stream;
  @override
  Future<User?> onQrLoginSuccess(PlatformType platform) async {
    saves++;
    return User(id: '1', nickname: '测试用户', platform: platform);
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late FakeAuth auth;
  late QmlLoginController login;
  late List<Map> events;
  setUp(() {
    auth = FakeAuth();
    events = [];
    login = QmlLoginController(
      auth: auth,
      render: (_) async => 'data:image/png;base64,aW1hZ2U=',
      publish: (_, value) async {
        events.add(value as Map);
      },
    );
  });
  tearDown(() async {
    await login.cancel(null);
    await auth.statuses.close();
    auth.dispose();
  });
  test(
    'login exposes image and statuses without exposing platform QR key',
    () async {
      final initial = await login.begin(PlatformType.netease, session: 'ui-1');
      expect(initial['status'], 'waiting');
      expect(jsonEncode(initial), isNot(contains('private-qr-key')));
      auth.statuses.add(QrLoginStatus.scanned);
      await Future<void>.delayed(Duration.zero);
      expect(login.state['status'], 'scanned');
      auth.statuses.add(QrLoginStatus.success);
      await Future<void>.delayed(Duration.zero);
      expect(auth.saves, 1);
      expect(login.state['status'], 'success');
      expect(events.every((event) => !event.containsKey('image')), true);
    },
  );
  test('closing a page cancels an in-flight QR acquisition', () async {
    auth.pending = Completer<QrLoginResult>();
    final pending = login.begin(PlatformType.netease, session: 'ui-1');
    await Future<void>.delayed(Duration.zero);
    await login.cancel('ui-1');
    auth.pending!.complete(
      const QrLoginResult(key: 'secret', qrUrl: 'payload'),
    );
    expect(await pending, {'cancelled': true});
    expect(auth.statuses.hasListener, false);
    expect(login.state, isEmpty);
  });
  test('an old page cannot cancel a newer QR attempt', () async {
    await login.begin(PlatformType.kugou, session: 'ui-1', variant: 'lite');
    await login.begin(PlatformType.kugou, session: 'ui-2', variant: 'android');
    await login.cancel('ui-1');
    expect(login.state['session'], 'ui-2');
    expect(auth.statuses.hasListener, true);
  });
  test('expiry releases polling and reports expired state', () async {
    await login.cancel(null);
    final expired = Completer<void>();
    login = QmlLoginController(
      auth: auth,
      lifetime: const Duration(milliseconds: 20),
      render: (_) async => 'image',
      publish: (_, value) async {
        if ((value as Map)['status'] == 'expired') expired.complete();
      },
    );
    await login.begin(PlatformType.netease);
    await expired.future.timeout(const Duration(seconds: 2));
    expect(auth.statuses.hasListener, false);
    expect(login.state['status'], 'expired');
  });
  test('QR payload renders a real PNG for native Image', () async {
    final image = await QmlLoginController.renderQr(
      const QrLoginResult(key: 'secret', qrUrl: 'https://example.test/login'),
    );
    final bytes = base64Decode(image.split(',').last);
    expect(bytes.take(8), [137, 80, 78, 71, 13, 10, 26, 10]);
  });
}
