import 'dart:convert';
import 'dart:io';
import 'package:dbus/dbus.dart';
import 'package:dio/dio.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/core/network/api_exception.dart';
import 'package:mconnect/core/network/platform_http.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/platform/netease/netease_api.dart';
import 'package:mconnect/platform/netease/netease_platform.dart';
import 'package:mconnect/sailfish/native_platforms.dart';

class NativeSearchMock extends DBusObject {
  NativeSearchMock()
    : super(DBusObjectPath('/org/sailfishos/Mconnect/Platforms'));
  List<DBusValue>? arguments;
  String? error;
  String platform = 'netease';
  void Function()? onRequest;
  @override
  Future<DBusMethodResponse> handleMethodCall(DBusMethodCall call) async {
    if (call.interface != 'org.sailfishos.Mconnect.Platforms' ||
        call.name != 'SearchNetease') {
      return DBusMethodErrorResponse.unknownMethod();
    }
    arguments = call.values;
    onRequest?.call();
    final playlists = (call.values[3] as DBusInt32).value == 1000;
    final result = <String, dynamic>{
      'backend': 'cpp-netease-search',
      if (error != null)
        'error': error
      else ...{
        'cookie': 'os=pc; MUSIC_U=native-test',
        playlists ? 'playlists' : 'songs': [
          if (playlists)
            {
              'id': '12345678901',
              'name': '中文歌单',
              'platform': platform,
              'songCount': 42,
              'coverUrl': 'https://image.test/list',
              'creatorName': '创建者',
              'editable': false,
              'collected': true,
            }
          else
            {
              'id': '1234567890123',
              'name': '中文歌曲',
              'platform': platform,
              'durationMs': 123456,
              'coverUrl': 'https://image.test/song',
              'artists': [
                {'id': '123456789', 'name': '歌手'},
              ],
              'album': {
                'id': '234567890',
                'name': '专辑',
                'coverUrl': 'https://image.test/song',
              },
              'artistId': '123456789',
              'albumId': '234567890',
              'trackNumber': 2,
              'fee': 8,
            },
        ],
      },
    };
    return DBusMethodSuccessResponse([DBusString(jsonEncode(result))]);
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late Directory directory;
  late DBusServer server;
  late DBusClient nativeBus, coreBus;
  late NativeSearchMock mock;
  late NativePlatformClient client;
  late NeteaseApi api;
  late NeteasePlatform platform;
  var legacyRequests = 0;
  setUp(() async {
    directory = await Directory.systemTemp.createTemp('native-platform-');
    server = DBusServer();
    final address = await server.listenAddress(
      DBusAddress.unix(dir: directory),
    );
    nativeBus = DBusClient(address);
    coreBus = DBusClient(address);
    mock = NativeSearchMock();
    await nativeBus.registerObject(mock);
    await nativeBus.requestName('org.sailfishos.Mconnect.Silica');
    client = NativePlatformClient(bus: coreBus);
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
    platform = NeteasePlatform(api: api, nativeClient: client);
  });
  tearDown(() async {
    SessionExpiryReporter.reset();
    await coreBus.close();
    await nativeBus.close();
    await server.close();
    await directory.delete(recursive: true);
  });
  test(
    'native song search preserves IDs metadata and current account cookie',
    () async {
      api.restoreCookie('MUSIC_U=account-before-search');
      final songs = await platform.search(' 中文 ', page: 2, limit: 30);
      final song = songs.single;
      expect(song.id, '1234567890123');
      expect(song.name, '中文歌曲');
      expect(song.platform, PlatformType.netease);
      expect(song.albumId, '234567890');
      expect(song.artistId, '123456789');
      expect(song.album!.id, song.albumId);
      expect(song.artists.single.id, song.artistId);
      expect(song.trackNumber, 2);
      expect(song.fee, 8);
      expect(song.duration.inMilliseconds, 123456);
      expect((mock.arguments![0] as DBusString).value, '中文');
      expect((mock.arguments![1] as DBusInt32).value, 2);
      expect(
        (mock.arguments![4] as DBusString).value,
        contains('MUSIC_U=account-before-search'),
      );
      expect(api.cookie, contains('MUSIC_U=native-test'));
      expect(legacyRequests, 0);
    },
  );
  test(
    'native playlist search preserves owning platform and read-only status',
    () async {
      final list = (await platform.searchPlaylists('歌单')).single;
      expect(list.id, '12345678901');
      expect(list.songCount, 42);
      expect(list.creatorName, '创建者');
      expect(list.platform, PlatformType.netease);
      expect(list.editable, false);
      expect(list.collected, true);
      expect(legacyRequests, 0);
    },
  );
  test(
    'late native cookie response cannot resurrect a logged-out session',
    () async {
      mock.onRequest = () => api.setCookie('');
      await platform.search('中文');
      expect(api.cookie, '');
      expect(legacyRequests, 0);
    },
  );
  test(
    'timeout and malformed native models fail without legacy network fallback',
    () async {
      mock.error = 'timeout';
      await expectLater(
        platform.search('中文'),
        throwsA(
          isA<ApiException>().having(
            (error) => error.code,
            'code',
            ApiErrorCode.requestTimeout,
          ),
        ),
      );
      mock.error = null;
      mock.platform = 'qq';
      await expectLater(platform.search('中文'), throwsA(isA<ApiException>()));
      expect(legacyRequests, 0);
    },
  );
  test('native session expiry is reported centrally', () async {
    PlatformType? expired;
    SessionExpiryReporter.handler = (value) => expired = value;
    mock.error = 'login_required';
    await expectLater(
      platform.search('中文'),
      throwsA(isA<LoginExpiredException>()),
    );
    expect(expired, PlatformType.netease);
    expect(legacyRequests, 0);
  });
  test(
    'late expiry from an old account cannot invalidate a new account',
    () async {
      PlatformType? expired;
      SessionExpiryReporter.handler = (value) => expired = value;
      mock.error = 'login_required';
      mock.onRequest = () => api.setCookie('MUSIC_U=new-account');
      await expectLater(
        platform.search('中文'),
        throwsA(isA<LoginExpiredException>()),
      );
      expect(expired, isNull);
      expect(api.cookie, contains('MUSIC_U=new-account'));
      expect(legacyRequests, 0);
    },
  );
  test(
    'invalid query page limit and injected cookie are rejected before D-Bus',
    () async {
      await expectLater(platform.search(''), throwsArgumentError);
      await expectLater(platform.search('中文', page: 0), throwsArgumentError);
      await expectLater(platform.search('中文', limit: 51), throwsArgumentError);
      await expectLater(
        client.searchNetease(
          '中文',
          page: 1,
          limit: 30,
          playlists: false,
          cookie: 'a=b\r\nX: injected',
        ),
        throwsArgumentError,
      );
      expect(mock.arguments, isNull);
      expect(legacyRequests, 0);
    },
  );
}
