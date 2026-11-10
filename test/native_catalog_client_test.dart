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

import 'package:mconnect/models/audio_quality.dart';
import 'package:mconnect/platform/base/music_platform.dart';

class CatalogMock extends DBusObject {
  CatalogMock() : super(DBusObjectPath('/org/sailfishos/Mconnect/Platforms'));
  String? error;
  List<DBusValue>? arguments;
  void Function()? onRequest;
  @override
  Future<DBusMethodResponse> handleMethodCall(DBusMethodCall call) async {
    if (call.name != 'NeteaseCatalog') {
      return DBusMethodErrorResponse.unknownMethod();
    }
    arguments = call.values;
    onRequest?.call();
    final op = (call.values[0] as DBusString).value;
    final row = {
      'id': '1234567890123',
      'platform': 'netease',
      'name': '中文歌曲',
      'artists': [
        {'id': '6452', 'name': 'artist'},
      ],
      'durationMs': 123456,
      'albumId': '18905',
      'artistId': '6452',
      'fee': 8,
      'trackNumber': 3,
    };
    final data = {
      'backend': 'cpp-netease-catalog',
      if (error != null)
        'error': error
      else ...{
        'cookie': 'MUSIC_U=native-catalog',
        if (['playlist', 'daily', 'ranked', 'newSongs'].contains(op))
          'songs': [row],
        if (op == 'ranked') 'ranks': [31],
        if (['toplists', 'userPlaylists', 'playlistInfo'].contains(op))
          'playlists': [
            {
              'id': '3778678',
              'platform': 'netease',
              'name': '中文榜单',
              'songCount': 200,
              'updateFrequency': '每日更新',
              'coverUrl': 'https://image.test/a',
            },
          ],
        if (op == 'lyrics')
          'lyrics': {
            'lrc': '[00:01]歌词',
            'translation': '[00:01]翻译',
            'yrc': '[1000,1000](0,1000,0)歌词',
            'yrcTranslation': '[00:01]逐字翻译',
            'romaji': '[00:01]romaji',
          },
        if (op == 'stream') 'url': 'https://audio.test/music',
        if (op == 'qualities')
          'qualities': [
            {'level': 7, 'bitrate': 999000, 'format': 'flac'},
          ],
      },
    };
    return DBusMethodSuccessResponse([DBusString(jsonEncode(data))]);
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late Directory directory;
  late DBusServer server;
  late DBusClient nativeBus, coreBus;
  late CatalogMock mock;
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
    mock = CatalogMock();
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

  test('all catalog paths use native DBus without legacy HTTP', () async {
    expect(
      (await platform.getPlaylistDetail('3778678')).single.id,
      '1234567890123',
    );
    expect((await platform.getDailyRecommendations()).single.albumId, '18905');
    final ranked = await platform.getRankedSongs('3778678', offset: 30, num: 5);
    expect(ranked.single.rank, 31);
    expect((mock.arguments![2] as DBusInt32).value, 30);
    expect((mock.arguments![3] as DBusInt32).value, 5);
    expect((await platform.getRankingList()).single.fee, 8);
    expect((await platform.getToplists()).single.updateFrequency, '每日更新');
    expect(
      (await platform.getNewSongs(
        region: NewSongRegion.japanese,
        limit: 5,
      )).single.artistId,
      '6452',
    );
    expect((mock.arguments![1] as DBusString).value, '8');
    expect(
      (await platform.parseShareLink(
        'https://music.163.com/playlist?id=3778678',
      ))!.id,
      '3778678',
    );
    expect(
      await platform.getSongUrl('186016', quality: AudioLevel.hires),
      'https://audio.test/music',
    );
    expect((mock.arguments![2] as DBusInt32).value, 4);
    expect(
      (await platform.getAvailableQualities('186016')).single.level,
      AudioLevel.master,
    );
    final bundle = await platform.getLyricsBundle('186016');
    expect(bundle!.yrcTranslation, contains('逐字翻译'));
    expect(bundle.romaji, contains('romaji'));
    expect(await platform.getLyrics('186016'), contains('翻译'));
    expect(api.cookie, contains('MUSIC_U=native-catalog'));
    expect(legacyRequests, 0);
  });
  test(
    'native errors retain VIP/unavailable/session types without fallback',
    () async {
      mock.error = 'vip_required';
      await expectLater(
        platform.getSongUrl('1'),
        throwsA(isA<NoVipMembershipException>()),
      );
      mock.error = 'unavailable';
      await expectLater(
        platform.getSongUrl('1'),
        throwsA(isA<SongNotAvailableException>()),
      );
      mock.error = 'login_required';
      await expectLater(
        platform.getDailyRecommendations(),
        throwsA(isA<LoginExpiredException>()),
      );
      mock.error = 'network_error';
      await expectLater(
        platform.getPlaylistDetail('1'),
        throwsA(isA<ApiException>()),
      );
      expect(legacyRequests, 0);
    },
  );
  test('account switch ignores stale cookies and expiry', () async {
    mock.onRequest = () => api.restoreCookie('MUSIC_U=new-account');
    await platform.getToplists();
    expect(api.cookie, contains('MUSIC_U=new-account'));
    mock.error = 'login_required';
    mock.onRequest = () => api.restoreCookie('MUSIC_U=third-account');
    await expectLater(
      platform.getToplists(),
      throwsA(isA<LoginExpiredException>()),
    );
    expect(api.cookie, contains('MUSIC_U=third-account'));
  });
  test('large native lists and rank validation preserve original IDs', () {
    final rows = [
      for (var i = 0; i < 2500; i++)
        {
          'id': '${1000000000000 + i}',
          'platform': 'netease',
          'name': 'song',
          'artists': <dynamic>[],
          'durationMs': 1,
        },
    ];
    expect(
      NativeNeteaseCatalog.fromJson({'songs': rows}, 'playlist').songs.length,
      2500,
    );
    expect(
      () => NativeNeteaseCatalog.fromJson({
        'songs': rows.take(2).toList(),
        'ranks': [3, 2],
      }, 'ranked'),
      throwsFormatException,
    );
    expect(
      () => NativeNeteaseCatalog.fromJson({'url': 'file:///secret'}, 'stream'),
      throwsFormatException,
    );
    expect(
      () => NativeNeteaseCatalog.fromJson({
        'qualities': [
          {'level': 99},
        ],
      }, 'qualities'),
      throwsFormatException,
    );
  });
}
