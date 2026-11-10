import 'dart:convert';
import 'dart:io';
import 'package:dbus/dbus.dart';
import 'package:dio/dio.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/core/network/api_exception.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/models/recommendation_source.dart';
import 'package:mconnect/platform/kugou/kugou_api.dart';
import 'package:mconnect/platform/kugou/kugou_platform.dart';
import 'package:mconnect/sailfish/native_platforms.dart';

class CatalogMock extends DBusObject {
  CatalogMock() : super(DBusObjectPath('/org/sailfishos/Mconnect/Platforms'));
  String? error;
  bool refusedRecommend = false;
  String? operation;
  Map<String, dynamic>? args;
  @override
  Future<DBusMethodResponse> handleMethodCall(DBusMethodCall call) async {
    if (call.name != 'KugouCatalog' || call.values.length != 2) {
      return DBusMethodErrorResponse.unknownMethod();
    }
    final op = (call.values[0] as DBusString).value;
    operation = op;
    args =
        jsonDecode((call.values[1] as DBusString).value)
            as Map<String, dynamic>;
    final album = {
      'id': '123',
      'name': '中文专辑',
      'coverUrl': 'https://image.test/480/album',
      'releaseDate': '2020-01-02T00:00:00',
      'artistId': '456',
      'artistName': '歌手',
      'songCount': 201,
    };
    final body = {
      'backend': 'cpp-kugou-catalog',
      if (error != null || op == 'recommend' && refusedRecommend)
        'error': error ?? 'invalid_response'
      else ...{
        if (!const ['artist', 'album', 'artistAlbums', 'toplists'].contains(op))
          'songs': [
            {
              'id': '0123456789ABCDEF0123456789ABCDEF',
              'platform': 'kugou',
              'name': '中文歌曲',
              'durationMs': 234567,
              'artists': [
                {'id': '456', 'name': '歌手'},
                {'id': '789', 'name': '合唱'},
              ],
              'album': album,
              'albumId': '123',
              'coverUrl': 'https://image.test/480/cover',
            },
          ],
        if (op == 'artist')
          'artist': {
            'id': '456',
            'name': '中文歌手',
            'avatarUrl': 'https://image.test/480/artist',
            'songCount': 100,
            'albumCount': 20,
            'fansCount': 345,
          },
        if (op == 'album') 'album': album,
        if (op == 'artistAlbums') 'albums': [album],
        if (op == 'toplists')
          'charts': [
            {
              'id': '8888',
              'name': 'TOP500',
              'songCount': 500,
              'period': '2026-11-02',
            },
          ],
        if (op == 'ranked')
          'positions': [
            {'rank': 4, 'rankChange': 3, 'isNew': true},
          ],
      },
    };
    return DBusMethodSuccessResponse([DBusString(jsonEncode(body))]);
  }
}

void main() {
  late Directory directory;
  late DBusServer server;
  late DBusClient nativeBus, coreBus;
  late CatalogMock mock;
  late KugouPlatform platform;
  late NativePlatformClient client;
  late int legacy;
  setUp(() async {
    directory = await Directory.systemTemp.createTemp('native-kugou-');
    server = DBusServer();
    final address = await server.listenAddress(
      DBusAddress.unix(dir: directory),
    );
    nativeBus = DBusClient(address);
    coreBus = DBusClient(address);
    mock = CatalogMock();
    await nativeBus.registerObject(mock);
    await nativeBus.requestName('org.sailfishos.Mconnect.Silica');
    legacy = 0;
    final dio = Dio();
    dio.interceptors.add(
      InterceptorsWrapper(
        onRequest: (request, handler) {
          legacy++;
          handler.reject(
            DioException(
              requestOptions: request,
              error: 'legacy HTTP must not run',
            ),
          );
        },
      ),
    );
    client = NativePlatformClient(bus: coreBus);
    platform = KugouPlatform(
      api: KugouApi(dio: dio),
      nativeClient: client,
    );
  });
  tearDown(() async {
    await coreBus.close();
    await nativeBus.close();
    await server.close();
    await directory.delete(recursive: true);
  });
  test(
    'ten operations preserve owning platform, hashes, metadata, pagination and rank movement',
    () async {
      final song = (await platform.search(
        '中文 A+B & #',
        page: 2,
        limit: 5,
      )).single;
      expect(song.platform, PlatformType.kugou);
      expect(song.id, '0123456789ABCDEF0123456789ABCDEF');
      expect(song.artists.length, 2);
      expect(song.duration.inMilliseconds, 234567);
      expect(mock.args, {'query': '中文 A+B & #', 'page': 2, 'limit': 5});
      expect((await platform.getArtistDetail('456'))!.fansCount, 345);
      expect(
        (await platform.getArtistTopSongs('456', limit: 5)).single.id,
        song.id,
      );
      expect(
        (await platform.getArtistAlbums(
          '456',
          page: 2,
          limit: 30,
        )).single.artistId,
        '456',
      );
      expect(mock.args, {'id': '456', 'page': 2, 'limit': 30});
      expect((await platform.getAlbumDetail('123'))!.releaseDate!.day, 2);
      expect(
        (await platform.getAlbumSongs('123')).single.platform,
        PlatformType.kugou,
      );
      expect((await platform.getToplists()).single.songCount, 500);
      final ranked = (await platform.getRankedSongs(
        '8888',
        offset: 3,
        num: 5,
        period: '2026-11-02',
      )).single;
      expect(ranked.rank, 4);
      expect(ranked.rankChange, 3);
      expect(ranked.isNew, true);
      expect(mock.args, {
        'id': '8888',
        'offset': 3,
        'limit': 5,
        'period': '2026-11-02',
      });
      expect(
        (await platform.fetchHomepageRecommendations()).single.id,
        song.id,
      );
      expect(
        (await client.kugouCatalog('recommend', {})).songs.single.platform,
        PlatformType.kugou,
      );
      expect((await platform.getRankingList()).single.id, song.id);
      expect(legacy, 0);
    },
  );
  test(
    'WAF failure falls back to native homepage with honest provenance; no Dart networking',
    () async {
      mock.refusedRecommend = true;
      final result = await platform.getDailyRecommendation();
      expect(result.source?.kind, RecommendationKind.fallbackHomepage);
      expect(result.songs.single.platform, PlatformType.kugou);
      expect(mock.operation, 'homepage');
      mock.error = 'provider_error';
      expect(
        (await platform.getDailyRecommendation()).source?.kind,
        RecommendationKind.unavailable,
      );
      expect(legacy, 0);
    },
  );
  test('native errors retain types and never retry via Dio', () async {
    mock.error = 'timeout';
    await expectLater(
      platform.search('music'),
      throwsA(
        isA<ApiException>().having(
          (e) => e.code,
          'code',
          ApiErrorCode.requestTimeout,
        ),
      ),
    );
    mock.error = 'provider_error';
    await expectLater(platform.getToplists(), throwsA(isA<ApiException>()));
    expect(legacy, 0);
    expect(
      () => NativeQqResult.fromJson(
        {
          'songs': [
            {'id': 'x', 'name': 'wrong', 'platform': 'qq'},
          ],
        },
        'search',
        platform: PlatformType.kugou,
      ),
      throwsFormatException,
    );
  });
}
