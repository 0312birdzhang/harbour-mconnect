import 'dart:convert';
import 'dart:io';
import 'package:dbus/dbus.dart';
import 'package:dio/dio.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/core/network/api_exception.dart';
import 'package:mconnect/core/network/platform_http.dart';
import 'package:mconnect/platform/bilibili/bilibili_platform.dart';
import 'package:mconnect/sailfish/native_platforms.dart';

import 'package:mconnect/models/user.dart';
import 'package:mconnect/models/song.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/core/storage/session_storage.dart';
import 'package:mconnect/platform/base/music_platform.dart';

import 'package:mconnect/models/audio_quality.dart';

class BiliMock extends DBusObject {
  BiliMock() : super(DBusObjectPath('/org/sailfishos/Mconnect/Platforms'));
  String? error;
  bool ok = true;
  List<DBusValue>? values;
  final operations = <String>[];
  final statuses = <String>['waiting', 'scanned', 'success'];
  Future<void> Function(String)? onRequest;
  @override
  Future<DBusMethodResponse> handleMethodCall(DBusMethodCall call) async {
    if (call.name != 'Bilibili') {
      return DBusMethodErrorResponse.unknownMethod();
    }
    values = call.values;
    final op = (call.values[0] as DBusString).value;
    operations.add(op);
    await onRequest?.call(op);
    final status = op == 'qrCheck'
        ? (statuses.length > 1 ? statuses.removeAt(0) : statuses.single)
        : null;
    final incoming = (call.values[2] as DBusString).value;
    final data = {
      'backend': 'cpp-bilibili',
      if (error != null)
        'error': error
      else ...{
        'cookie': status == 'success'
            ? 'SESSDATA=native=bili; bili_jct=csrf; buvid3=anon'
            : '$incoming; buvid3=anon',
        if (op == 'search' || op == 'playlist')
          'songs': [
            {
              'id': 'BVtest:22',
              'platform': 'bilibili',
              'name': '中文标题&字幕',
              'durationMs': 62340,
              'artists': [
                {'id': '1234567890123', 'name': 'UP主'},
              ],
              'coverUrl': 'https://image.test/a',
            },
          ],
        if (op == 'folders' || op == 'share')
          'playlists': [
            {
              'id': op == 'share' ? 'video:BVtest' : '123',
              'platform': 'bilibili',
              'name': '中文收藏夹',
              'songCount': 2,
            },
          ],
        if (op == 'stream') 'url': 'https://a.bilivideo.com/audio',
        if (op == 'qualities')
          'qualities': [
            {'level': 0, 'bitrate': 64, 'format': 'm4a'},
          ],
        if (op == 'qrKey') ...{
          'qrKey': 'test-key',
          'qrUrl': 'https://passport.bilibili.com/qrcode/h5/login?key=test-key',
        },
        if (op == 'qrCheck') 'qrStatus': status,
        if (op == 'profile' || status == 'success')
          'user': {
            'id': '1234567890123',
            'nickname': '中文用户',
            'platform': 'bilibili',
            'avatarUrl': 'https://image.test/user',
            'vipLevel': 1,
          },
        if (op == 'lyrics') 'lyrics': '[01:02.34]中文歌词',
        if (['like', 'add', 'remove'].contains(op)) 'ok': ok,
      },
    };
    return DBusMethodSuccessResponse([DBusString(jsonEncode(data))]);
  }
}

class BiliSession extends SessionStorage {
  BiliSession({this.cookie = '{"SESSDATA":"stored","bili_jct":"csrf"}'});
  String? cookie;
  User? user = const User(
    id: '1234567890123',
    nickname: '中文用户',
    platform: PlatformType.bilibili,
    vipLevel: VipLevel.vip,
  );
  @override
  Future<String?> loadCookie(PlatformType p) async => cookie;
  @override
  Future<User?> loadUser(PlatformType p) async => user;
  @override
  Future<void> saveCookie(PlatformType p, String value) async {
    cookie = value;
  }

  @override
  Future<void> saveUser(PlatformType p, User value) async {
    user = value;
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late Directory directory;
  late DBusServer server;
  late DBusClient nativeBus, coreBus;
  late BiliMock mock;
  late BilibiliPlatform platform;
  var legacyRequests = 0;
  setUp(() async {
    directory = await Directory.systemTemp.createTemp('native-bili-');
    server = DBusServer();
    final address = await server.listenAddress(
      DBusAddress.unix(dir: directory),
    );
    nativeBus = DBusClient(address);
    coreBus = DBusClient(address);
    mock = BiliMock();
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
              error: 'legacy HTTP forbidden',
            ),
          );
        },
      ),
    );
    platform = BilibiliPlatform(
      dio: dio,
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
    'native reads retain BVID/CID, provider, metadata, source headers and quality selection',
    () async {
      final songs = await platform.search('音乐 A+B!', page: 2, limit: 5);
      expect(songs.single.id, 'BVtest:22');
      expect(songs.single.platform, PlatformType.bilibili);
      expect(songs.single.name, '中文标题&字幕');
      expect(songs.single.artists.single.id, '1234567890123');
      final args = jsonDecode((mock.values![1] as DBusString).value);
      expect(args, {'query': '音乐 A+B!', 'page': 2, 'limit': 5});
      expect(
        await platform.getSongUrl('BVtest:22', quality: AudioLevel.high),
        endsWith('/audio'),
      );
      expect(jsonDecode((mock.values![1] as DBusString).value), {
        'id': 'BVtest:22',
        'quality': 2,
      });
      expect(
        (await platform.getAvailableQualities('BVtest')).single.format,
        'm4a',
      );
      expect(
        (await platform.getPlaylistDetail('video:BVtest')).single.id,
        'BVtest:22',
      );
      expect(await platform.getLyrics('BVtest:22'), '[01:02.34]中文歌词');
      expect(
        (await platform.parseShareLink(
          'https://www.bilibili.com/video/BVtest',
        ))!.id,
        'video:BVtest',
      );
      expect(
        BilibiliPlatform.mediaHeaders['Referer'],
        'https://www.bilibili.com/',
      );
      expect(legacyRequests, 0);
    },
  );
  test(
    'stored JSON sessions and folders/writes stay compatible without legacy HTTP',
    () async {
      final storage = BiliSession();
      await platform.restoreSession(storage);
      expect(platform.isLoggedIn, true);
      final folders = await platform.getUserPlaylists();
      expect(folders.single.editable, true);
      expect(folders.single.collected, false);
      expect(folders.single.platform, PlatformType.bilibili);
      const song = Song(
        id: 'BVtest:22',
        platform: PlatformType.bilibili,
        name: 'title',
        artists: [],
      );
      expect(await platform.addSongToPlaylist('123', song), true);
      expect(await platform.removeSongFromPlaylist('123', song), true);
      expect(await platform.likeSong('BVtest:22', like: false), true);
      expect(jsonDecode((mock.values![1] as DBusString).value), {
        'id': 'BVtest:22',
        'enabled': false,
      });
      mock.ok = false;
      expect(await platform.likeSong('BVtest'), false);
      expect(await platform.addSongToPlaylist('123', song), false);
      await platform.saveSession(storage);
      expect((jsonDecode(storage.cookie!) as Map)['SESSDATA'], 'stored');
      expect((jsonDecode(storage.cookie!) as Map)['buvid3'], 'anon');
      expect(legacyRequests, 0);
    },
  );
  test(
    'native QR flow validates user and preserves cookie equals when saved',
    () async {
      final key = await platform.getQrCode();
      expect(key.qrUrl, startsWith('https://passport.bilibili.com/'));
      expect(await platform.pollQrStatus(key.key).toList(), [
        QrLoginStatus.waiting,
        QrLoginStatus.scanned,
        QrLoginStatus.success,
      ]);
      expect(platform.isLoggedIn, true);
      expect((await platform.getUserInfo())!.id, '1234567890123');
      expect(await platform.getVipStatus(), VipLevel.vip);
      final storage = BiliSession(cookie: null);
      await platform.saveSession(storage);
      expect((jsonDecode(storage.cookie!) as Map)['SESSDATA'], 'native=bili');
      expect(legacyRequests, 0);
    },
  );
  test(
    'late QR success cannot restore a logged-out session; retired key is not polled',
    () async {
      final key = await platform.getQrCode();
      final before = mock.operations.length;
      expect(await platform.pollQrStatus('retired-key').toList(), isEmpty);
      expect(mock.operations.length, before);
      mock.statuses
        ..clear()
        ..add('success');
      mock.onRequest = (op) async {
        if (op == 'qrCheck') await platform.logout();
      };
      expect(await platform.pollQrStatus(key.key).toList(), isEmpty);
      expect(platform.isLoggedIn, false);
      expect(await platform.getUserInfo(), isNull);
    },
  );
  test(
    'native errors do not retry through Dio; expiry does not erase a switched account',
    () async {
      mock.error = 'verification_required';
      await expectLater(
        platform.search('音乐'),
        throwsA(
          isA<ApiException>().having(
            (e) => e.message,
            'message',
            contains('安全验证'),
          ),
        ),
      );
      mock.error = 'unavailable';
      await expectLater(
        platform.getSongUrl('BVtest'),
        throwsA(isA<SongNotAvailableException>()),
      );
      PlatformType? expired;
      SessionExpiryReporter.handler = (p) => expired = p;
      mock.error = 'login_required';
      await expectLater(
        platform.search('音乐'),
        throwsA(isA<LoginExpiredException>()),
      );
      expect(expired, PlatformType.bilibili);
      expired = null;
      mock.onRequest = (_) => platform.restoreSession(
        BiliSession(cookie: '{"SESSDATA":"new-account","bili_jct":"next"}'),
      );
      await expectLater(
        platform.search('音乐'),
        throwsA(isA<LoginExpiredException>()),
      );
      expect(expired, isNull);
      expect(legacyRequests, 0);
    },
  );
  test(
    'wire decoder rejects wrong providers, bad URLs, incomplete success and missing write state',
    () {
      expect(
        () => NativeBilibiliResult.fromJson({
          'songs': [
            {'id': '1', 'platform': 'netease', 'name': 'wrong'},
          ],
        }, 'search'),
        throwsFormatException,
      );
      expect(
        () =>
            NativeBilibiliResult.fromJson({'url': 'file:///private'}, 'stream'),
        throwsFormatException,
      );
      expect(
        () => NativeBilibiliResult.fromJson({'qrStatus': 'success'}, 'qrCheck'),
        throwsFormatException,
      );
      expect(
        () => NativeBilibiliResult.fromJson({}, 'like'),
        throwsFormatException,
      );
    },
  );
}
