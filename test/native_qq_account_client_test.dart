import 'dart:async';
import 'dart:convert';
import 'package:dbus/dbus.dart';
import 'package:dio/dio.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/core/network/api_exception.dart';
import 'package:mconnect/platform/base/music_platform.dart';
import 'package:mconnect/platform/qq/qq_api.dart';
import 'package:mconnect/platform/qq/qq_platform.dart';
import 'package:mconnect/sailfish/native_platforms.dart';

class QqAccountMock extends NativePlatformClient {
  QqAccountMock(DBusClient bus) : super(bus: bus);
  int sequence = 0;
  String? activeKey, error;
  final statuses = <String>['waiting'];
  final operations = <String>[];
  void Function(String)? onRequest;
  Completer<void>? heldReply;
  final cancelled = Completer<void>();

  @override
  Future<NativeQqAccount> qqAccount(
    String operation,
    Map<String, dynamic> args, {
    required String cookie,
  }) async {
    operations.add(operation);
    final result = <String, dynamic>{'backend': 'cpp-qq-account', 'ok': true};
    if (operation == 'qrKey') {
      activeKey = 'key-${++sequence}';
      result.addAll({
        'qrKey': activeKey,
        'qrImage': 'iVBORw0KGgo=',
        'cookie': 'qrsig=fixture',
      });
    } else if (operation == 'qrCheck') {
      if (args['key'] != activeKey) {
        result['error'] = 'cancelled';
      } else {
        final status = statuses.length > 1
            ? statuses.removeAt(0)
            : statuses.single;
        result['qrStatus'] = status;
        result['cookie'] = status == 'success'
            ? 'uin=o12345; qm_keyst=fixture'
            : 'qrsig=fixture';
        if (status == 'success') {
          result['user'] = {
            'id': '12345',
            'nickname': '中文用户',
            'platform': 'qq',
            'vipLevel': 0,
          };
        }
      }
    } else if (operation == 'qrCancel') {
      if (args['key'] == activeKey) activeKey = null;
      if (!cancelled.isCompleted) cancelled.complete();
    }
    onRequest?.call(operation);
    if (operation != 'qrCancel' && heldReply != null) {
      final held = heldReply!;
      heldReply = null;
      await held.future;
    }
    if (operation != 'qrCancel') {
      if (error == 'cancelled' || result['error'] == 'cancelled') {
        throw RequestCancelledException();
      }
      if (error != null) throw ApiException(message: 'fixture request failed');
    }
    return NativeQqAccount.fromJson(result, operation);
  }

  @override
  Future<NativeQqResult> qqCatalog(
    String operation,
    Map<String, dynamic> args, {
    required String cookie,
  }) async {
    operations.add(operation);
    return NativeQqResult.fromJson({
      'cookie': cookie,
      'playlists': [
        {'id': args['id'], 'name': '中文歌单', 'platform': 'qq', 'songCount': 405},
      ],
    }, operation);
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late DBusClient clientBus;
  late QqAccountMock mock;
  late QqPlatform platform;
  late QqApi api;
  var legacyRequests = 0;
  setUp(() async {
    clientBus = DBusClient(DBusAddress.tcp('127.0.0.1', port: 1));
    mock = QqAccountMock(clientBus);
    legacyRequests = 0;
    final dio = Dio();
    dio.interceptors.add(
      InterceptorsWrapper(
        onRequest: (request, handler) {
          legacyRequests++;
          handler.reject(
            DioException(
              requestOptions: request,
              error: 'unexpected Dart HTTP',
            ),
          );
        },
      ),
    );
    api = QqApi(dio: dio);
    platform = QqPlatform(api: api, nativeClient: mock);
  });
  tearDown(() async {
    await clientBus.close();
  });

  test(
    'QR polling publishes waiting scanned and success through native account API',
    () async {
      mock.statuses.replaceRange(0, mock.statuses.length, [
        'waiting',
        'scanned',
        'success',
      ]);
      final qr = await platform.getQrCode();
      expect(qr.qrBytes, [137, 80, 78, 71, 13, 10, 26, 10]);
      expect(await platform.pollQrStatus(qr.key).toList(), [
        QrLoginStatus.waiting,
        QrLoginStatus.scanned,
        QrLoginStatus.success,
      ]);
      await mock.cancelled.future;
      expect(platform.isLoggedIn, true);
      expect(api.cookie, contains('uin=o12345'));
      expect(mock.operations, [
        'qrKey',
        'qrCheck',
        'qrCheck',
        'qrCheck',
        'qrCancel',
      ]);
      expect(legacyRequests, 0);
    },
  );
  test('expired QR retires native ownership', () async {
    mock.statuses[0] = 'expired';
    final qr = await platform.getQrCode();
    expect(await platform.pollQrStatus(qr.key).toList(), [
      QrLoginStatus.expired,
    ]);
    await mock.cancelled.future;
    expect(mock.activeKey, isNull);
    expect(legacyRequests, 0);
  });
  test('a delayed QR reply cannot replace a newer native key', () async {
    final held = Completer<void>(), requested = Completer<void>();
    mock.heldReply = held;
    mock.onRequest = (operation) {
      if (operation == 'qrKey' && !requested.isCompleted) requested.complete();
    };
    final previousCheck = expectLater(
      platform.getQrCode(),
      throwsA(isA<RequestCancelledException>()),
    );
    await requested.future;
    final current = await platform.getQrCode();
    held.complete();
    await previousCheck;
    expect(current.key, 'key-2');
    expect(mock.activeKey, 'key-2');
  });
  test('shared playlist metadata uses the native catalog entry', () async {
    final playlist = await platform.parseShareLink(
      'https://y.qq.com/n/ryqq/playlist/1234567890123',
    );
    expect(playlist!.id, '1234567890123');
    expect(playlist.songCount, 405);
    expect(mock.operations, ['playlistInfo']);
    expect(legacyRequests, 0);
  });
  test('logout cancels old key before a further native poll', () async {
    final qr = await platform.getQrCode();
    await platform.logout();
    expect(await platform.pollQrStatus(qr.key).toList(), isEmpty);
    await mock.cancelled.future;
    expect(api.cookie, '');
    expect(mock.operations, ['qrKey', 'qrCancel']);
  });
  test(
    'logout while QR key is being fetched cancels the late native key',
    () async {
      mock.onRequest = (operation) {
        if (operation == 'qrKey') unawaited(platform.logout());
      };
      await expectLater(
        platform.getQrCode(),
        throwsA(isA<RequestCancelledException>()),
      );
      await mock.cancelled.future;
      expect(api.cookie, '');
      expect(mock.activeKey, isNull);
    },
  );
  test(
    'success from a replaced cookie cannot restore stale credentials or user',
    () async {
      mock.statuses[0] = 'success';
      final qr = await platform.getQrCode();
      mock.onRequest = (operation) {
        if (operation == 'qrCheck') {
          api.restoreCookie('uin=o67890; qm_keyst=new');
        }
      };
      expect(await platform.pollQrStatus(qr.key).toList(), isEmpty);
      await mock.cancelled.future;
      expect(api.cookie, 'uin=o67890; qm_keyst=new');
      expect(platform.isLoggedIn, false);
    },
  );
  test('cancelled backend reply retains cancellation semantics', () async {
    mock.error = 'cancelled';
    await expectLater(
      platform.getQrCode(),
      throwsA(isA<RequestCancelledException>()),
    );
    expect(legacyRequests, 0);
  });
  test('failed native account transport stays on the native route', () async {
    mock.error = 'provider_error';
    await expectLater(platform.getQrCode(), throwsA(isA<ApiException>()));
    expect(legacyRequests, 0);
  });
  test(
    'native account decoder rejects missing credentials and invalid QR payloads',
    () {
      final valid = <String, dynamic>{
        'ok': true,
        'qrKey': 'key-1',
        'qrImage': 'iVBORw0KGgo=',
        'cookie': 'qrsig=fixture',
      };
      expect(NativeQqAccount.fromJson(valid, 'qrKey').qrKey, 'key-1');
      for (final data in [
        {...valid}..remove('cookie'),
        {
          ...valid,
          'qrImage': base64Encode(utf8.encode('<html>captcha</html>')),
        },
        {...valid, 'qrKey': '../key'},
        {...valid, 'cookie': 'bad\r\nheader'},
        {...valid, 'qrStatus': 'success'},
      ]) {
        expect(
          () => NativeQqAccount.fromJson(data, 'qrKey'),
          throwsFormatException,
        );
      }
      expect(
        () => NativeQqAccount.fromJson({
          'ok': true,
          'qrStatus': 'success',
          'cookie': '',
        }, 'qrCheck'),
        throwsFormatException,
      );
      expect(
        () =>
            NativeQqAccount.fromJson({'ok': true, 'qrKey': 'key'}, 'qrCancel'),
        throwsFormatException,
      );
    },
  );
}
