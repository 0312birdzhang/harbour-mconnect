import 'dart:convert';
import 'dart:io';
import 'package:dbus/dbus.dart';
import 'package:dio/dio.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/core/network/api_exception.dart';
import 'package:mconnect/core/network/platform_http.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/models/user.dart';
import 'package:mconnect/models/recommendation_source.dart';
import 'package:mconnect/platform/qq/qq_api.dart';
import 'package:mconnect/platform/qq/qq_platform.dart';
import 'package:mconnect/sailfish/native_platforms.dart';

import 'package:mconnect/models/audio_quality.dart';
import 'package:mconnect/platform/base/music_platform.dart';

class QqMock extends DBusObject {
  QqMock() : super(DBusObjectPath('/org/sailfishos/Mconnect/Platforms'));
  String? error;
  Map<String, dynamic>? args;
  String? operation;
  bool emptyLists = false;
  void Function()? onRequest;
  @override
  Future<DBusMethodResponse> handleMethodCall(DBusMethodCall call) async {
    if (call.name != 'QqCatalog') {
      return DBusMethodErrorResponse.unknownMethod();
    }
    operation = (call.values[0] as DBusString).value;
    args =
        jsonDecode((call.values[1] as DBusString).value)
            as Map<String, dynamic>;
    onRequest?.call();
    final op = operation;
    final album = {
      'id': 'albumMid',
      'name': '中文专辑',
      'artistId': 'singerMid',
      'artistName': '歌手',
      'coverUrl': 'https://image.test/album',
      'releaseDate': '2003-04-23T00:00:00.000',
      'description': '简介',
      'songCount': 2,
      'company': 'record',
      'genre': 'pop',
      'language': '国语',
    };
    final result = {
      'backend': 'cpp-qq-catalog',
      if (error != null)
        'error': error
      else ...{
        'cookie': (call.values[2] as DBusString).value,
        if ([
          'search',
          'artistSongs',
          'albumSongs',
          'newSongs',
          'ranked',
          'playlist',
          'liked',
        ].contains(op))
          'songs': [
            {
              'id': 'songMid',
              'platform': 'qq',
              'name': '中文歌曲',
              'durationMs': 321000,
              'albumId': 'albumMid',
              'artistId': 'singerMid',
              'artists': [
                {'id': 'singerMid', 'name': '歌手'},
              ],
              'album': {
                'id': 'albumMid',
                'name': '中文专辑',
                'coverUrl': 'https://image.test/album',
              },
            },
          ],
        if (op == 'profile')
          'user': {
            'id': args!['id'],
            'nickname': '中文用户',
            'platform': 'qq',
            'vipLevel': 0,
            'avatarUrl': 'https://image.test/user',
          },
        if (op == 'vip') 'vipLevel': 2,
        if (const ['like', 'add', 'create', 'collect'].contains(op)) 'ok': true,
        if (op == 'create') 'dirId': '42',
        if (op == 'dailyId') 'dailyId': '1234567890123',
        if (const [
          'searchPlaylists',
          'userPlaylists',
          'playlistInfo',
        ].contains(op))
          'playlists': op == 'userPlaylists' && emptyLists
              ? []
              : [
                  {
                    'id': '1234567890123',
                    'name': '歌单',
                    'platform': 'qq',
                    'songCount': 100,
                    if (op == 'userPlaylists') 'editId': '42',
                  },
                ],
        if (op == 'lyrics')
          'lyrics': {
            'lrc': '[00:01.00]中文歌词',
            'translation': '[00:01.00]translation',
          },
        if (op == 'artist')
          'artist': {
            'id': 'singerMid',
            'name': '歌手',
            'avatarUrl': 'https://image.test/artist',
            'briefDesc': '简介',
            'songCount': 100,
            'albumCount': 50,
          },
        if (op == 'artistAlbums') 'albums': [album],
        if (op == 'album') 'album': album,
        if (op == 'toplists')
          'charts': [
            {
              'id': '26',
              'name': '热歌榜',
              'groupName': '巅峰榜',
              'songCount': 300,
              'period': '2026_44',
            },
          ],
        if (op == 'ranked')
          'positions': [
            {'rank': 51, 'rankChange': -3, 'isNew': false, 'rankValue': '3'},
          ],
        if (op == 'stream') 'url': 'https://audio.test/file?vkey=test',
        if (op == 'qualities')
          'qualities': [
            {'level': 7, 'bitrate': 999000, 'format': 'flac'},
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
  late QqMock mock;
  late QqApi api;
  late QqPlatform platform;
  var legacyRequests = 0;
  setUp(() async {
    directory = await Directory.systemTemp.createTemp('native-content-');
    server = DBusServer();
    final address = await server.listenAddress(DBusAddress.unix(dir: directory));
    nativeBus = DBusClient(address);
    coreBus = DBusClient(address);
    mock = QqMock();
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
    api = QqApi(dio: dio);
    platform = QqPlatform(
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
    'all native QQ reads preserve mids, album metadata, periods and movement',
    () async {
      final songs = await platform.search('歌曲 A+B!', page: 2, limit: 5);
      expect(songs.single.id, 'songMid');
      expect(songs.single.platform, PlatformType.qq);
      expect(songs.single.albumId, 'albumMid');
      expect(songs.single.artistId, 'singerMid');
      expect(mock.args, {'query': '歌曲 A+B!', 'page': 2, 'limit': 5});
      expect((await platform.searchPlaylists('音乐')).single.collected, true);
      expect((await platform.getArtistDetail('singerMid'))!.songCount, 100);
      expect(
        (await platform.getArtistTopSongs('singerMid', limit: 2)).single.id,
        'songMid',
      );
      final albums = await platform.getArtistAlbums(
        'singerMid',
        page: 2,
        limit: 5,
      );
      expect(albums.single.releaseDate!.day, 23);
      expect(albums.single.releaseDate!.isUtc, false);
      expect(mock.args, {'id': 'singerMid', 'page': 2, 'limit': 5});
      final album = await platform.getAlbumDetail('albumMid');
      expect(album!.language, '国语');
      expect(album.artistId, 'singerMid');
      expect(
        (await platform.getAlbumSongs('albumMid')).single.album!.id,
        'albumMid',
      );
      for (final region in NewSongRegion.values) {
        expect(
          (await platform.getNewSongs(region: region, limit: 3)).length,
          1,
        );
      }
      expect(mock.args, {'id': '6', 'limit': 3});
      expect((await platform.getToplists()).single.period, '2026_44');
      final ranked = await platform.getRankedSongs(
        '26',
        offset: 50,
        num: 5,
        period: '2026_44',
      );
      expect(ranked.single.rank, 51);
      expect(ranked.single.rankChange, -3);
      expect(ranked.single.isNew, false);
      expect(mock.args, {
        'id': '26',
        'offset': 50,
        'limit': 5,
        'period': '2026_44',
      });
      expect((await platform.getRankingList()).single.id, 'songMid');
      expect(
        await platform.getSongUrl('songMid', quality: AudioLevel.master),
        contains('vkey=test'),
      );
      expect(mock.args, {'id': 'songMid', 'quality': 7});
      expect(
        (await platform.getAvailableQualities('songMid')).single.level,
        AudioLevel.master,
      );
      expect(legacyRequests, 0);
    },
  );
  test(
    'native failures retain typed errors and never fall back to Dart HTTP',
    () async {
      mock.error = 'unavailable';
      await expectLater(
        platform.getSongUrl('songMid'),
        throwsA(isA<SongNotAvailableException>()),
      );
      mock.error = 'timeout';
      await expectLater(
        platform.getAlbumSongs('albumMid'),
        throwsA(
          isA<ApiException>().having(
            (e) => e.code,
            'code',
            ApiErrorCode.requestTimeout,
          ),
        ),
      );
      mock.error = 'provider_error';
      await expectLater(platform.search('音乐'), throwsA(isA<ApiException>()));
      expect(legacyRequests, 0);
    },
  );
  test(
    'old expiry and stream reply cannot overwrite switched or logged-out accounts',
    () async {
      PlatformType? expired;
      SessionExpiryReporter.handler = (p) => expired = p;
      mock.error = 'login_required';
      mock.onRequest = () =>
          api.restoreCookie('qqmusic_uin=67890; qm_keyst=new');
      await expectLater(
        platform.search('音乐'),
        throwsA(isA<LoginExpiredException>()),
      );
      expect(expired, isNull);
      expect(api.cookie, contains('qm_keyst=new'));
      mock.error = null;
      mock.onRequest = () =>
          api.restoreCookie('qqmusic_uin=99999; qm_keyst=third');
      await expectLater(
        platform.getSongUrl('songMid'),
        throwsA(isA<RequestCancelledException>()),
      );
      expect(api.cookie, contains('qm_keyst=third'));
      mock.onRequest = () {
        platform.logout();
      };
      await expectLater(
        platform.search('音乐'),
        throwsA(isA<RequestCancelledException>()),
      );
      expect(api.cookie, '');
    },
  );
  test('native lyrics and library reads bypass legacy HTTP', () async {
    expect((await platform.getLyricsBundle('songMid'))!.lrc, '[00:01.00]中文歌词');
    expect(
      await platform.getLyrics('songMid'),
      '[00:01.00]中文歌词\n[00:01.00]translation',
    );
    expect((await platform.getPlaylistDetail('123')).single.id, 'songMid');
    expect(mock.args, {'id': '123'});
    expect((await platform.getLikedSongs()).single.platform, PlatformType.qq);
    expect(legacyRequests, 0);
    mock.error = 'provider_error';
    expect(await platform.getLyricsBundle('songMid'), isNull);
    expect(await platform.getPlaylistDetail('123'), isEmpty);
    expect(legacyRequests, 0);
    expect(
      () => NativeQqResult.fromJson({
        'lyrics': {'qrc': 123},
      }, 'lyrics'),
      throwsFormatException,
    );
  });
  test(
    'shared playlist import fetches native metadata without track pagination',
    () async {
      final playlist = await platform.parseShareLink(
        'https://y.qq.com/n/ryqq/playlist/1234567890123',
      );
      expect(playlist!.id, '1234567890123');
      expect(playlist.songCount, 100);
      expect(mock.operation, 'playlistInfo');
      expect(mock.args, {'id': '1234567890123'});
      expect(legacyRequests, 0);
    },
  );
  test(
    'native account reads preserve user and playlist edit identity; writes bypass Dio',
    () async {
      api.restoreCookie('qqmusic_uin=o12345; p_skey=test');
      expect((await platform.getUserInfo())!.nickname, '中文用户');
      expect(mock.args, {'id': '12345'});
      expect(await platform.getVipStatus(), VipLevel.svip);
      final list = (await platform.getUserPlaylists()).single;
      expect(list.id, '1234567890123');
      expect(list.editableId, '42');
      expect(list.editable, true);
      expect(list.collected, false);
      expect(await platform.likeSong('songMid'), true);
      expect(mock.args, {'id': 'songMid', 'like': true});
      expect(await platform.likeSong('songMid', like: false), true);
      expect(mock.args, {'id': 'songMid', 'like': false});
      mock.error = 'provider_error';
      expect(await platform.likeSong('songMid'), false);
      expect(legacyRequests, 0);
    },
  );
  test(
    'switched-account profile response is cancelled without restoring stale user',
    () async {
      api.restoreCookie('qqmusic_uin=o12345; p_skey=test');
      mock.onRequest = () =>
          api.restoreCookie('qqmusic_uin=o67890; p_skey=new');
      await expectLater(
        platform.getUserInfo(),
        throwsA(isA<RequestCancelledException>()),
      );
      expect(api.cookie, contains('67890'));
      expect(legacyRequests, 0);
    },
  );
  test(
    'native playlist writes retain public identity and recommendations retain provenance',
    () async {
      api.restoreCookie('qqmusic_uin=o12345; p_skey=test');
      await platform.getUserInfo();
      final song = (await platform.search('music')).single;
      expect(await platform.addSongToPlaylist('42', song), true);
      expect(mock.args, {'id': '42', 'songId': 'songMid'});
      final created = await platform.createPlaylist('中文 A+B & #');
      expect(created!.id, '1234567890123');
      expect(created.editableId, '42');
      mock.emptyLists = true;
      expect(await platform.createPlaylist('missing'), isNull);
      mock.emptyLists = false;
      expect(await platform.collectPlaylist('123'), true);
      expect(mock.args, {'id': '123', 'collect': true});
      expect(await platform.collectPlaylist('123', collect: false), true);
      expect(mock.args, {'id': '123', 'collect': false});
      final daily = await platform.getDailyRecommendation();
      expect(daily.source?.kind, RecommendationKind.personalPrivate);
      expect(daily.songs.single.id, 'songMid');
      mock.error = 'provider_error';
      expect(await platform.addSongToPlaylist('42', song), false);
      expect(await platform.createPlaylist('test'), isNull);
      expect(await platform.collectPlaylist('123'), false);
      expect(legacyRequests, 0);
    },
  );
  test('strict wire decoder rejects invalid rank and wrong provider', () {
    expect(
      () => NativeQqResult.fromJson({'ok': true}, 'create'),
      throwsFormatException,
    );
    expect(
      () => NativeQqResult.fromJson({'dailyId': '../wrong'}, 'dailyId'),
      throwsFormatException,
    );
    expect(
      () => NativeQqResult.fromJson({'vipLevel': 3}, 'vip'),
      throwsFormatException,
    );
    expect(
      () => NativeQqResult.fromJson({'ok': false}, 'like'),
      throwsFormatException,
    );
    expect(
      () => NativeQqResult.fromJson({
        'songs': [
          {'id': 'x', 'name': 'wrong', 'platform': 'bilibili'},
        ],
      }, 'search'),
      throwsFormatException,
    );
    expect(
      () => NativeQqResult.fromJson({
        'songs': [],
        'positions': [
          {'rank': 0},
        ],
      }, 'ranked'),
      throwsFormatException,
    );
    expect(
      () => NativeQqResult.fromJson({'url': 'file:///private'}, 'stream'),
      throwsFormatException,
    );
  });
}
