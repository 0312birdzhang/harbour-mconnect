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

class ContentMock extends DBusObject {
  ContentMock() : super(DBusObjectPath('/org/sailfishos/Mconnect/Platforms'));
  List<DBusValue>? arguments;
  String? error;
  bool missing = false;
  void Function()? onRequest;
  @override
  Future<DBusMethodResponse> handleMethodCall(DBusMethodCall call) async {
    if (call.interface != 'org.sailfishos.Mconnect.Platforms' ||
        call.name != 'NeteaseContent') {
      return DBusMethodErrorResponse.unknownMethod();
    }
    arguments = call.values;
    onRequest?.call();
    final operation = (call.values[0] as DBusString).value;
    final result = <String, dynamic>{
      'backend': 'cpp-netease-content',
      if (error != null)
        'error': error
      else ...{
        'cookie': 'os=pc; MUSIC_U=content-test',
        if (operation == 'artist')
          'artist': missing
              ? null
              : {
                  'id': '6452',
                  'name': '歌手',
                  'avatarUrl': 'https://image.test/artist',
                  'briefDesc': '中文简介',
                  'songCount': 100,
                  'albumCount': 30,
                },
        if (operation == 'album')
          'album': missing
              ? null
              : {
                  'id': '18905',
                  'name': '专辑',
                  'artistId': '6452',
                  'artistName': '歌手',
                  'releaseDate': '2011-11-11T00:00:00.000',
                  'description': '专辑简介',
                  'songCount': 120,
                  'company': '发行公司',
                  'genre': 'pop/rock',
                },
        if (operation == 'artistAlbums')
          'albums': [
            {
              'id': '18905',
              'name': '专辑',
              'artistId': '6452',
              'releaseDate': '2011-11-11T00:00:00.000',
            },
          ],
        if (operation == 'artistSongs' || operation == 'albumSongs')
          'songs': [
            for (var i = 0; i < 120; i++)
              {
                'id': '${1000000000000 + i}',
                'name': '曲目$i',
                'platform': 'netease',
                'artists': [
                  {'id': '6452', 'name': '歌手'},
                ],
                'durationMs': 123456,
                'albumId': '18905',
                'artistId': '6452',
                'trackNumber': i + 1,
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
  late ContentMock mock;
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
    mock = ContentMock();
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
    'native artist and album retain biography calendar date and metadata',
    () async {
      final artist = await platform.getArtistDetail('6452');
      expect(artist!.briefDesc, '中文简介');
      expect(artist.songCount, 100);
      expect(artist.fansCount, isNull);
      expect(artist.avatarUrl, 'https://image.test/artist');
      final album = await platform.getAlbumDetail('18905');
      expect(album!.artistId, '6452');
      expect(album.description, '专辑简介');
      expect(album.company, '发行公司');
      expect(album.genre, 'pop/rock');
      expect(album.releaseDate!.isUtc, false);
      expect(album.releaseDate!.day, 11);
      expect(album.songCount, 120);
      expect(legacyRequests, 0);
    },
  );
  test(
    'native album paging and large song lists retain IDs without truncation',
    () async {
      final albums = await platform.getArtistAlbums('6452', page: 2, limit: 5);
      expect(albums.single.id, '18905');
      expect((mock.arguments![2] as DBusInt32).value, 2);
      final songs = await platform.getAlbumSongs('18905');
      expect(songs.length, 120);
      expect(songs.last.id, '1000000000119');
      expect(songs.last.trackNumber, 120);
      expect(songs.last.fee, 8);
      final top = await platform.getArtistTopSongs('6452', limit: 100);
      expect(top.first.artistId, '6452');
      expect(legacyRequests, 0);
    },
  );
  test(
    'missing entities remain null and content errors never use Dart HTTP',
    () async {
      mock.missing = true;
      expect(await platform.getArtistDetail('6452'), isNull);
      expect(await platform.getAlbumDetail('18905'), isNull);
      mock.error = 'timeout';
      await expectLater(
        platform.getAlbumSongs('18905'),
        throwsA(
          isA<ApiException>().having(
            (error) => error.code,
            'code',
            ApiErrorCode.requestTimeout,
          ),
        ),
      );
      await expectLater(
        platform.getAlbumDetail('../escape'),
        throwsArgumentError,
      );
      expect(legacyRequests, 0);
    },
  );
  test(
    'native content has the same protection against late old-account expiry',
    () async {
      PlatformType? expired;
      SessionExpiryReporter.handler = (value) => expired = value;
      mock.error = 'login_required';
      mock.onRequest = () => api.setCookie('MUSIC_U=new-account');
      await expectLater(
        platform.getArtistDetail('6452'),
        throwsA(isA<LoginExpiredException>()),
      );
      expect(expired, isNull);
      expect(api.cookie, contains('MUSIC_U=new-account'));
      expect(legacyRequests, 0);
    },
  );
}
