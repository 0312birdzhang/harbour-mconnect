import 'dart:async';
import 'dart:convert';
import 'package:dbus/dbus.dart';
import 'package:dio/dio.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/core/network/api_exception.dart';
import 'package:mconnect/core/network/platform_http.dart';
import 'package:mconnect/core/storage/session_storage.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/models/user.dart';
import 'package:mconnect/platform/base/music_platform.dart';
import 'package:mconnect/platform/kugou/kugou_api.dart';
import 'package:mconnect/platform/kugou/kugou_platform.dart';
import 'package:mconnect/sailfish/native_platforms.dart';

class KugouAccountMock extends NativePlatformClient {
  KugouAccountMock(DBusClient bus) : super(bus: bus);
  int sequence = 0;
  String? activeKey, error;
  final operations = <String>[];
  final statuses = <String>['waiting'];
  Map<String, dynamic>? arguments, receivedSession;
  Map<String, dynamic> qrSession = {};
  bool extendedVip = true, createdStableId = true;
  Completer<void>? heldReply;
  void Function(String)? onRequest;
  final cancelled = Completer<void>();

  Future<void> complete(String operation) async {
    onRequest?.call(operation);
    if (operation == 'qrCancel') return;
    final held = heldReply;
    heldReply = null;
    if (held != null) await held.future;
    if (error == 'cancelled') throw RequestCancelledException();
    if (error == 'expired') throw LoginExpiredException();
    if (error != null) throw ApiException(message: 'fixture request failed');
  }

  @override
  Future<NativeKugouAccount> kugouAccount(
    String operation,
    Map<String, dynamic> args, {
    required String session,
  }) async {
    operations.add(operation);
    arguments = args;
    receivedSession = jsonDecode(session) as Map<String, dynamic>;
    final result = <String, dynamic>{'ok': true};
    if (operation == 'qrKey') {
      activeKey = 'key-${++sequence}';
      qrSession = {...receivedSession!}
        ..remove('token')
        ..remove('userid')
        ..remove('vip_token')
        ..remove('vip_type');
      result.addAll({
        'qrKey': activeKey,
        'qrUrl':
            'https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=${qrSession['client'] == 'lite' ? 3116 : 1005}&qrcode=provider-$sequence',
        'qrImage': 'iVBORw0KGgo=',
        'session': jsonEncode(qrSession),
      });
    } else if (operation == 'qrCheck') {
      if (args['key'] != activeKey) throw RequestCancelledException();
      final status = statuses.length > 1
          ? statuses.removeAt(0)
          : statuses.single;
      final updated = <String, dynamic>{...qrSession};
      if (status == 'success') {
        updated.addAll({
          'token': 'new-token',
          'userid': '12345',
          if (extendedVip) 'vip_token': 'new-vip',
          if (extendedVip) 'vip_type': '6',
        });
        result['user'] = {
          'id': '12345',
          'nickname': '中文用户',
          'platform': 'kugou',
          'vipLevel': 0,
        };
      }
      result.addAll({'qrStatus': status, 'session': jsonEncode(updated)});
    } else if (operation == 'qrCancel') {
      if (activeKey == args['key']) activeKey = null;
      if (!cancelled.isCompleted) cancelled.complete();
    } else if (operation == 'vip') {
      final type =
          int.tryParse(receivedSession!['vip_type'] as String? ?? '') ?? 0;
      result.addAll({
        'session': session,
        'vipLevel': type >= 2
            ? 2
            : type > 0
            ? 1
            : 0,
      });
    }
    await complete(operation);
    return NativeKugouAccount.fromJson(result, operation);
  }

  @override
  Future<NativeKugouLibrary> kugouLibrary(
    String operation,
    Map<String, dynamic> args, {
    required String session,
  }) async {
    operations.add(operation);
    arguments = args;
    receivedSession = jsonDecode(session) as Map<String, dynamic>;
    final playlist = {
      'id': operation == 'create' ? 'new-global' : 'created-global',
      'editId': operation == 'create' ? 'new-list' : 'list-456',
      'name': operation == 'create' ? args['name'] : '中文歌单',
      'platform': 'kugou',
      'songCount': 250,
      'editable': operation != 'share',
      'collected': false,
    };
    final result = <String, dynamic>{
      if (const ['userPlaylists', 'create', 'share'].contains(operation))
        'playlists': operation == 'create' && !createdStableId
            ? []
            : [playlist],
      if (operation == 'liked' || operation == 'playlist')
        'songs': [
          {
            'id': '0123456789abcdef0123456789abcdef',
            'name': '中文歌曲',
            'platform': 'kugou',
            'durationMs': 234567,
            'artists': [
              {'id': '456', 'name': '歌手'},
            ],
          },
        ],
      if (const ['like', 'create', 'collect'].contains(operation))
        'ok': operation != 'collect' || args['collect'] == true,
    };
    await complete(operation);
    return NativeKugouLibrary.fromJson(result, operation);
  }
}

class HeldSessionStorage extends SessionStorage {
  final cookie = Completer<String?>();
  @override
  Future<String?> loadCookie(PlatformType platform) => cookie.future;
  @override
  Future<User?> loadUser(PlatformType platform) async => const User(
    id: '12345',
    nickname: 'stored user',
    platform: PlatformType.kugou,
  );
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late DBusClient bus;
  late KugouAccountMock mock;
  late KugouApi api;
  late KugouPlatform platform;
  var legacyRequests = 0;
  final expiryReports = <PlatformType>[];
  setUp(() {
    bus = DBusClient(DBusAddress.tcp('127.0.0.1', port: 1));
    mock = KugouAccountMock(bus);
    legacyRequests = 0;
    expiryReports.clear();
    SessionExpiryReporter.handler = expiryReports.add;
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
    api = KugouApi(dio: dio);
    platform = KugouPlatform(api: api, nativeClient: mock);
  });
  tearDown(() async {
    SessionExpiryReporter.reset();
    await bus.close();
  });
  test(
    'native QR flow retains concept app identity and stores extended credentials',
    () async {
      platform.setClientVariant('概念版');
      mock.statuses.replaceRange(0, mock.statuses.length, [
        'waiting',
        'scanned',
        'success',
      ]);
      final qr = await platform.getQrCode();
      expect(qr.qrUrl, contains('appid=3116'));
      expect(qr.qrBytes, [137, 80, 78, 71, 13, 10, 26, 10]);
      expect(await platform.pollQrStatus(qr.key).toList(), [
        QrLoginStatus.waiting,
        QrLoginStatus.scanned,
        QrLoginStatus.success,
      ]);
      await mock.cancelled.future;
      expect(api.token, 'new-token');
      expect(api.userid, '12345');
      expect(api.vipToken, 'new-vip');
      expect(api.vipType, '6');
      expect((await platform.getUserInfo())!.nickname, '中文用户');
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
  test('new account drops previous account VIP credentials', () async {
    api.setSessionFields(
      token: 'old-token',
      userid: '42',
      vipToken: 'old-vip',
      vipType: '6',
      mid: 'stored-mid',
    );
    mock.extendedVip = false;
    mock.statuses[0] = 'success';
    final qr = await platform.getQrCode();
    expect(api.token, 'old-token');
    expect(await platform.pollQrStatus(qr.key).toList(), [
      QrLoginStatus.success,
    ]);
    await mock.cancelled.future;
    expect(api.token, 'new-token');
    expect(api.vipToken, isNull);
    expect(api.vipType, isNull);
    expect(api.mid, 'stored-mid');
  });
  test('expired QR retires ownership', () async {
    mock.statuses[0] = 'expired';
    final qr = await platform.getQrCode();
    expect(await platform.pollQrStatus(qr.key).toList(), [
      QrLoginStatus.expired,
    ]);
    await mock.cancelled.future;
    expect(mock.activeKey, isNull);
  });
  test('closing a waiting QR stream retires ownership', () async {
    final qr = await platform.getQrCode();
    expect(await platform.pollQrStatus(qr.key).first, QrLoginStatus.waiting);
    await mock.cancelled.future;
    expect(mock.activeKey, isNull);
  });
  test('a delayed key cannot replace a newer flow', () async {
    final held = Completer<void>(), requested = Completer<void>();
    mock.heldReply = held;
    mock.onRequest = (operation) {
      if (operation == 'qrKey' && !requested.isCompleted) requested.complete();
    };
    final pending = expectLater(
      platform.getQrCode(),
      throwsA(isA<RequestCancelledException>()),
    );
    await requested.future;
    final current = await platform.getQrCode();
    held.complete();
    await pending;
    expect(current.key, 'key-2');
    expect(mock.activeKey, 'key-2');
  });
  test('logout during key creation retires the late key', () async {
    mock.onRequest = (operation) {
      if (operation == 'qrKey') unawaited(platform.logout());
    };
    await expectLater(
      platform.getQrCode(),
      throwsA(isA<RequestCancelledException>()),
    );
    await mock.cancelled.future;
    expect(mock.activeKey, isNull);
    expect(api.token, isNull);
  });
  test('old login success cannot overwrite replaced credentials', () async {
    mock.statuses[0] = 'success';
    final qr = await platform.getQrCode();
    mock.onRequest = (operation) {
      if (operation == 'qrCheck') {
        api.setSessionFields(token: 'replacement-token', userid: '67890');
      }
    };
    expect(await platform.pollQrStatus(qr.key).toList(), isEmpty);
    await mock.cancelled.future;
    expect(api.token, 'replacement-token');
    expect(platform.isLoggedIn, false);
  });
  test('changing the client variant retires the active QR', () async {
    final qr = await platform.getQrCode();
    platform.setClientVariant('lite');
    expect(await platform.pollQrStatus(qr.key).toList(), isEmpty);
    await mock.cancelled.future;
    expect(api.clientMode, KugouPlaybackClient.lite);
  });
  test(
    'native library entry points retain song IDs and playlist edit IDs',
    () async {
      api.setSessionFields(token: 'fixture-token', userid: '12345');
      final lists = await platform.getUserPlaylists();
      expect(lists.single.id, 'created-global');
      expect(lists.single.editableId, 'list-456');
      expect(
        (await platform.getPlaylistDetail('created-global')).single.platform,
        PlatformType.kugou,
      );
      expect(
        (await platform.getLikedSongs()).single.duration.inMilliseconds,
        234567,
      );
      expect(await platform.likeSong('0123456789abcdef0123456789abcdef'), true);
      expect(
        await platform.likeSong(
          '0123456789abcdef0123456789abcdef',
          like: false,
        ),
        true,
      );
      final created = await platform.createPlaylist('新建歌单');
      expect(created!.id, 'new-global');
      expect(created.editableId, 'new-list');
      expect(await platform.collectPlaylist('created-global'), true);
      expect(
        await platform.collectPlaylist('created-global', collect: false),
        false,
      );
      final imported = await platform.parseShareLink(
        'https://t1.kugou.com/fixture-short',
      );
      expect(imported!.songCount, 250);
      expect(imported.editable, false);
      expect(imported.collected, false);
      expect(mock.arguments, {'url': 'https://t1.kugou.com/fixture-short'});
      expect(mock.operations, [
        'userPlaylists',
        'playlist',
        'liked',
        'like',
        'like',
        'create',
        'collect',
        'collect',
        'share',
      ]);
      expect(legacyRequests, 0);
    },
  );
  test(
    'create without a stable ID keeps the optional-result contract',
    () async {
      mock.createdStableId = false;
      expect(await platform.createPlaylist('no stable ID'), isNull);
      expect(mock.operations, ['create']);
      expect(legacyRequests, 0);
    },
  );
  test('VIP session decisions use the native account endpoint', () async {
    api.setSessionFields(
      token: 'fixture-token',
      userid: '12345',
      vipToken: 'fixture-vip',
      vipType: '6',
    );
    expect(await platform.getVipStatus(), VipLevel.svip);
    expect(mock.operations, ['vip']);
    expect(legacyRequests, 0);
  });
  test(
    'current library expiry is reported and preserves the empty-list contract',
    () async {
      mock.error = 'expired';
      expect(await platform.getUserPlaylists(), isEmpty);
      expect(expiryReports, [PlatformType.kugou]);
      expect(legacyRequests, 0);
    },
  );
  test(
    'old library expiry cannot invalidate replacement credentials',
    () async {
      api.setSessionFields(token: 'old-token', userid: '12345');
      mock.error = 'expired';
      mock.onRequest = (_) =>
          api.setSessionFields(token: 'new-token', userid: '67890');
      expect(await platform.getUserPlaylists(), isEmpty);
      expect(expiryReports, isEmpty);
      expect(api.token, 'new-token');
    },
  );
  test('logout while storage is loading prevents stale restoration', () async {
    final storage = HeldSessionStorage();
    final restoring = platform.restoreSession(storage);
    await platform.logout();
    storage.cookie.complete('stored-token|12345');
    await restoring;
    expect(api.token, isNull);
    expect(platform.isLoggedIn, false);
  });
  test(
    'account decoder rejects malformed credentials identities and QR images',
    () {
      final session = jsonEncode({
        'client': 'android',
        'token': 'fixture-token',
        'userid': '12345',
      });
      final valid = {
        'ok': true,
        'session': session,
        'qrStatus': 'success',
        'user': {
          'id': '12345',
          'nickname': '中文用户',
          'platform': 'kugou',
          'vipLevel': 0,
        },
      };
      expect(NativeKugouAccount.fromJson(valid, 'qrCheck').user!.id, '12345');
      for (final data in [
        {
          ...valid,
          'session': jsonEncode({'client': 'android', 'userid': '12345'}),
        },
        {
          ...valid,
          'session': jsonEncode({
            'client': 'android',
            'token': 'bad\r\nheader',
            'userid': '12345',
          }),
        },
        {
          ...valid,
          'user': {
            'id': '67890',
            'nickname': 'wrong account',
            'platform': 'kugou',
            'vipLevel': 0,
          },
        },
        {...valid, 'qrStatus': 'waiting'},
      ]) {
        expect(
          () => NativeKugouAccount.fromJson(data, 'qrCheck'),
          throwsFormatException,
        );
      }
      expect(
        () => NativeKugouAccount.fromJson({
          'ok': true,
          'session': jsonEncode({'client': 'android'}),
          'qrKey': 'key-1',
          'qrUrl':
              'https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=1005&qrcode=provider-1',
          'qrImage': base64Encode(utf8.encode('<html>captcha</html>')),
        }, 'qrKey'),
        throwsFormatException,
      );
    },
  );
}
