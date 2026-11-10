import 'dart:convert';
import 'dart:io';
import 'package:dbus/dbus.dart';
import 'package:dio/dio.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/core/network/api_exception.dart';
import 'package:mconnect/core/network/platform_http.dart';
import 'package:mconnect/platform/netease/netease_api.dart';
import 'package:mconnect/platform/netease/netease_platform.dart';
import 'package:mconnect/sailfish/native_platforms.dart';

import 'package:mconnect/models/user.dart';
import 'package:mconnect/models/song.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/core/storage/session_storage.dart';
import 'dart:async';
import 'package:mconnect/platform/base/music_platform.dart';

class AccountMock extends DBusObject {
  AccountMock() : super(DBusObjectPath('/org/sailfishos/Mconnect/Platforms'));
  String? error;
  bool ok = true;
  bool omitUser = false;
  final statuses = <String>['waiting', 'scanned', 'success'];
  final operations = <String>[];
  Map<String, dynamic>? arguments;
  Future<void> Function()? onRequest;
  @override
  Future<DBusMethodResponse> handleMethodCall(DBusMethodCall call) async {
    if (call.name != 'NeteaseAccount') {
      return DBusMethodErrorResponse.unknownMethod();
    }
    final op = (call.values[0] as DBusString).value;
    operations.add(op);
    arguments =
        jsonDecode((call.values[1] as DBusString).value)
            as Map<String, dynamic>;
    await onRequest?.call();
    final status = op == 'qrCheck'
        ? (statuses.length > 1 ? statuses.removeAt(0) : statuses.single)
        : null;
    final data = {
      'backend': 'cpp-netease-account',
      if (error != null)
        'error': error
      else ...{
        'ok': ok,
        'cookie': 'MUSIC_U=native-account; __csrf=next',
        if (!ok) 'message': '服务器拒绝',
        if (op == 'qrKey') ...{
          'qrKey': 'test-key',
          'qrUrl': 'https://music.163.com/login?codekey=test-key',
        },
        if (op == 'qrCheck') 'qrStatus': status,
        if (!omitUser &&
            (['phoneLogin', 'profile', 'vip'].contains(op) ||
                status == 'success'))
          'user': {
            'id': '1234567890123',
            'nickname': '中文用户',
            'avatarUrl': 'https://image.test/user',
            'platform': 'netease',
            'vipLevel': 2,
          },
        if (op == 'profile' ||
            op == 'vip' ||
            op == 'phoneLogin' ||
            status == 'success')
          'vipLevel': 2,
        if (op == 'create' && ok)
          'playlists': [
            {
              'id': '9876543210123',
              'platform': 'netease',
              'name': '中文歌单',
              'songCount': 0,
            },
          ],
      },
    };
    return DBusMethodSuccessResponse([DBusString(jsonEncode(data))]);
  }
}

class DelayedSession extends SessionStorage {
  final pending = Completer<String?>();
  @override
  Future<String?> loadCookie(PlatformType platform) => pending.future;
  @override
  Future<User?> loadUser(PlatformType platform) async =>
      const User(id: '9', nickname: 'old', platform: PlatformType.netease);
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late Directory directory;
  late DBusServer server;
  late DBusClient nativeBus, coreBus;
  late AccountMock mock;
  late NeteaseApi api;
  late NeteasePlatform platform;
  var legacyRequests = 0;
  setUp(() async {
    directory = await Directory.systemTemp.createTemp('native-content-');
    server = DBusServer();
    final address = await server.listenAddress(
      DBusAddress.unix(dir: directory),
    );
    nativeBus = DBusClient(address);
    coreBus = DBusClient(address);
    mock = AccountMock();
    await nativeBus.registerObject(mock);
    await nativeBus.requestName('org.sailfishos.Mconnect.Silica');
    legacyRequests = 0;
    final dio = Dio();
    dio.interceptors.add(
      InterceptorsWrapper(
        onRequest: (request, handler) {
          legacyRequests++;
          handler.reject(
            DioException(
              requestOptions: request,
              error: 'legacy HTTP must not run',
            ),
          );
        },
      ),
    );
    api = NeteaseApi(dio: dio);
    platform = NeteasePlatform(
      api: api,
      nativeClient: NativePlatformClient(bus: coreBus),
    );
  });
  tearDown(() async {
    SessionExpiryReporter.reset();
    await coreBus.close();
    await nativeBus.close();
    await server.close();
    await directory.delete(recursive: true);
  });

  test(
    'native phone/profile/VIP and seven mutation methods bypass legacy HTTP',
    () async {
      expect((await platform.sendPhoneCode('13800000000')).success, true);
      final login = await platform.loginByPhone('13800000000', '123456');
      expect(login.success, true);
      expect(login.user!.id, '1234567890123');
      expect(login.cookie, contains('MUSIC_U=native-account'));
      expect(platform.isLoggedIn, true);
      expect((await platform.getUserInfo())!.nickname, '中文用户');
      expect(await platform.getVipStatus(), VipLevel.svip);
      expect(await platform.likeSong('186016', like: false), true);
      expect(mock.arguments, {'id': '186016', 'enabled': false});
      const song = Song(
        id: '186016',
        platform: PlatformType.netease,
        name: '歌曲',
        artists: [],
      );
      expect(await platform.addSongToPlaylist('1', song), true);
      expect(await platform.removeSongFromPlaylist('1', song), true);
      final created = await platform.createPlaylist('中文歌单');
      expect(created!.id, '9876543210123');
      expect(created.editable, true);
      expect(created.collected, false);
      expect(await platform.renamePlaylist('1', '新名称'), true);
      expect(await platform.deletePlaylist('1'), true);
      expect(await platform.collectPlaylist('1', collect: false), true);
      expect(mock.arguments, {'id': '1', 'enabled': false});
      expect(
        mock.operations,
        containsAll([
          'sms',
          'phoneLogin',
          'vip',
          'like',
          'addTrack',
          'removeTrack',
          'create',
          'rename',
          'delete',
          'collect',
        ]),
      );
      expect(legacyRequests, 0);
      await platform.logout();
      expect(platform.isLoggedIn, false);
      expect(api.cookie, '');
      expect((await platform.getUserInfo())!.vipLevel, VipLevel.svip);
      expect(mock.operations.last, 'profile');
    },
  );
  test('native QR statuses commit user only after verified success', () async {
    final qr = await platform.getQrCode();
    expect(qr.qrUrl, 'https://music.163.com/login?codekey=test-key');
    final values = await platform.pollQrStatus(qr.key).toList();
    expect(values, [
      QrLoginStatus.waiting,
      QrLoginStatus.scanned,
      QrLoginStatus.success,
    ]);
    expect(platform.isLoggedIn, true);
    expect((await platform.getUserInfo())!.id, '1234567890123');
    expect(legacyRequests, 0);
  });
  test(
    'expired QR terminates and incomplete login is never successful',
    () async {
      await platform.getQrCode();
      mock.statuses
        ..clear()
        ..add('expired');
      expect(await platform.pollQrStatus('test-key').toList(), [
        QrLoginStatus.expired,
      ]);
      mock.omitUser = true;
      expect(
        (await platform.loginByPhone('13800000000', '123456')).success,
        false,
      );
      expect(platform.isLoggedIn, false);
    },
  );
  test(
    'rejected writes/login do not pretend success or use old HTTP',
    () async {
      mock.ok = false;
      expect((await platform.sendPhoneCode('13800000000')).success, false);
      expect(
        (await platform.loginByPhone('13800000000', '123456')).error,
        '服务器拒绝',
      );
      expect(await platform.likeSong('1'), false);
      expect(await platform.createPlaylist('name'), isNull);
      expect(await platform.renamePlaylist('1', 'name'), false);
      expect(await platform.deletePlaylist('1'), false);
      expect(await platform.collectPlaylist('1'), false);
      mock.error = 'network_error';
      expect(await platform.likeSong('1'), false);
      expect(legacyRequests, 0);
    },
  );
  test(
    'late login and profile cannot restore state after logout or account switch',
    () async {
      mock.onRequest = () => platform.logout();
      final login = await platform.loginByPhone('13800000000', '123456');
      expect(login.success, false);
      expect(platform.isLoggedIn, false);
      expect(api.cookie, '');
      expect(await platform.getUserInfo(), isNull);
      expect(platform.isLoggedIn, false);
      mock.onRequest = () async => api.restoreCookie('MUSIC_U=other-account');
      expect(
        (await platform.loginByPhone('13800000000', '123456')).success,
        false,
      );
      expect(api.cookie, contains('MUSIC_U=other-account'));
      expect(platform.isLoggedIn, false);
    },
  );
  test(
    'logout during stored-session read cannot restore stale state',
    () async {
      final storage = DelayedSession();
      final restoring = platform.restoreSession(storage);
      await platform.logout();
      storage.pending.complete('MUSIC_U=stored');
      await restoring;
      expect(api.cookie, '');
      expect(platform.isLoggedIn, false);
    },
  );
  test('expiry reports only for the account which made the request', () async {
    mock.error = 'login_required';
    await expectLater(
      NativePlatformClient(
        bus: coreBus,
      ).accountNetease('profile', {}, cookie: ''),
      throwsA(isA<LoginExpiredException>()),
    );
    PlatformType? expired;
    SessionExpiryReporter.handler = (value) => expired = value;
    expect(await platform.likeSong('1'), false);
    expect(expired, PlatformType.netease);
    expired = null;
    mock.onRequest = () async => api.restoreCookie('MUSIC_U=new-account');
    expect(await platform.likeSong('1'), false);
    expect(expired, isNull);
    expect(api.cookie, contains('MUSIC_U=new-account'));
  });
  test('retired QR key never reaches the native login endpoint', () async {
    await platform.getQrCode();
    final before = mock.operations.length;
    expect(await platform.pollQrStatus('retired-key').toList(), isEmpty);
    expect(mock.operations.length, before);
  });
  test(
    'strict account decoder rejects wrong identity/status/URL and missing created playlist',
    () {
      expect(
        () => NativeNeteaseAccount.fromJson({
          'ok': true,
          'qrKey': 'x',
          'qrUrl': 'https://evil.test',
        }, 'qrKey'),
        throwsFormatException,
      );
      expect(
        () => NativeNeteaseAccount.fromJson({
          'ok': true,
          'qrStatus': 'unknown',
        }, 'qrCheck'),
        throwsFormatException,
      );
      expect(
        () => NativeNeteaseAccount.fromJson({'ok': true}, 'phoneLogin'),
        throwsFormatException,
      );
      expect(
        () => NativeNeteaseAccount.fromJson({
          'ok': true,
          'playlists': [],
        }, 'create'),
        throwsFormatException,
      );
    },
  );
}
