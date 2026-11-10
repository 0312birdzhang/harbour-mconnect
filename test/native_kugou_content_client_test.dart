import 'dart:async';
import 'dart:convert';
import 'package:dbus/dbus.dart';
import 'package:dio/dio.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/core/network/api_exception.dart';
import 'package:mconnect/core/network/platform_http.dart';
import 'package:mconnect/models/audio_quality.dart';
import 'package:mconnect/models/platform_type.dart';
import 'package:mconnect/platform/kugou/kugou_api.dart';
import 'package:mconnect/platform/kugou/kugou_platform.dart';
import 'package:mconnect/sailfish/native_platforms.dart';

class KugouContentMock extends NativePlatformClient {
  KugouContentMock(DBusClient bus) : super(bus: bus);
  final operations = <String>[];
  Map<String, dynamic>? arguments, session;
  Completer<void>? heldReply;
  void Function()? onRequest;
  bool expired = false;

  @override
  Future<NativeQqResult> kugouContent(
    String operation,
    Map<String, dynamic> args, {
    required String session,
  }) async {
    operations.add(operation);
    arguments = args;
    this.session = jsonDecode(session) as Map<String, dynamic>;
    onRequest?.call();
    final held = heldReply;
    heldReply = null;
    if (held != null) await held.future;
    if (expired) throw LoginExpiredException();
    return NativeQqResult.fromJson(
      {
        if (operation == 'stream') 'url': 'https://audio.test/selected.flac',
        if (operation == 'qualities')
          'qualities': [
            {'level': 0, 'bitrate': 128000, 'format': 'mp3'},
            {'level': 3, 'bitrate': 999000, 'format': 'flac'},
          ],
        if (operation == 'searchPlaylists')
          'playlists': [
            {
              'id': 'collection-123',
              'name': '中文歌单',
              'platform': 'kugou',
              'songCount': 405,
              'coverUrl': 'https://image.test/480/cover',
              'editId': '456',
            },
          ],
        if (operation == 'lyrics' || operation == 'lyricsByInfo')
          'lyrics': {'lrc': '[0,2000]<0,500,0>中文歌词'},
      },
      operation == 'lyricsByInfo' ? 'lyrics' : operation,
      platform: PlatformType.kugou,
    );
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late DBusClient bus;
  late KugouContentMock mock;
  late KugouPlatform platform;
  late KugouApi api;
  var legacyRequests = 0;
  final expiryReports = <PlatformType>[];
  setUp(() {
    bus = DBusClient(DBusAddress.tcp('127.0.0.1', port: 1));
    mock = KugouContentMock(bus);
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
    'all content entry points use native results and preserve owning IDs',
    () async {
      const hash = '0123456789ABCDEF0123456789ABCDEF';
      expect(
        await platform.getSongUrl(hash, quality: AudioLevel.lossless),
        'https://audio.test/selected.flac',
      );
      expect(mock.arguments, {'id': hash, 'quality': 3});
      expect(
        (await platform.getAvailableQualities(
          'historical arbitrary id',
        )).last.level,
        AudioLevel.lossless,
      );
      expect(await platform.getLyrics(hash), contains('中文歌词'));
      expect(
        await platform.getLyricsByInfo('中文 A+B & #', 123000),
        contains('中文歌词'),
      );
      expect(mock.arguments, {'query': '中文 A+B & #', 'duration': 123000});
      final playlist = (await platform.searchPlaylists(
        '中文 A+B & #',
        page: 2,
        limit: 5,
      )).single;
      expect(playlist.platform, PlatformType.kugou);
      expect(playlist.id, 'collection-123');
      expect(playlist.editableId, '456');
      expect(playlist.collected, true);
      expect(playlist.editable, false);
      expect(playlist.songCount, 405);
      expect(mock.arguments, {'query': '中文 A+B & #', 'page': 2, 'limit': 5});
      expect(mock.operations, [
        'stream',
        'qualities',
        'lyrics',
        'lyricsByInfo',
        'searchPlaylists',
      ]);
      expect(legacyRequests, 0);
    },
  );
  test(
    'native requests receive all stored credentials and device identities',
    () async {
      api.setSessionFields(
        token: 'fixture-token',
        userid: '12345',
        vipToken: 'fixture-vip',
        vipType: '6',
        dfid: 'fixture-dfid',
        mid: 'fixture-mid',
        uuid: 'fixture-uuid',
      );
      platform.setClientVariant('概念版');
      await platform.getSongUrl('0123456789abcdef0123456789abcdef');
      expect(mock.session, {
        'token': 'fixture-token',
        'userid': '12345',
        'vip_token': 'fixture-vip',
        'vip_type': '6',
        'dfid': 'fixture-dfid',
        'mid': 'fixture-mid',
        'uuid': 'fixture-uuid',
        'client': 'lite',
      });
      expect(legacyRequests, 0);
    },
  );
  test('logout rejects a delayed stream result', () async {
    api.setSessionFields(token: 'fixture-token', userid: '12345');
    final held = Completer<void>(), requested = Completer<void>();
    mock.heldReply = held;
    mock.onRequest = requested.complete;
    final pending = expectLater(
      platform.getSongUrl('0123456789abcdef0123456789abcdef'),
      throwsA(isA<RequestCancelledException>()),
    );
    await requested.future;
    await platform.logout();
    held.complete();
    await pending;
    expect(api.token, isNull);
    expect(expiryReports, isEmpty);
  });
  test(
    'changing credentials rejects the previous account stream result',
    () async {
      api.setSessionFields(token: 'old-token', userid: '12345');
      mock.onRequest = () =>
          api.setSessionFields(token: 'new-token', userid: '67890');
      await expectLater(
        platform.getSongUrl('0123456789abcdef0123456789abcdef'),
        throwsA(isA<RequestCancelledException>()),
      );
      expect(api.token, 'new-token');
    },
  );
  test(
    'current account expiry is reported through the shared handler',
    () async {
      mock.expired = true;
      await expectLater(
        platform.getSongUrl('0123456789abcdef0123456789abcdef'),
        throwsA(isA<LoginExpiredException>()),
      );
      expect(expiryReports, [PlatformType.kugou]);
      expect(legacyRequests, 0);
    },
  );
  test('old account expiry cannot invalidate new credentials', () async {
    api.setSessionFields(token: 'old-token', userid: '12345');
    mock.expired = true;
    mock.onRequest = () =>
        api.setSessionFields(token: 'new-token', userid: '67890');
    await expectLater(
      platform.getSongUrl('0123456789abcdef0123456789abcdef'),
      throwsA(isA<RequestCancelledException>()),
    );
    expect(expiryReports, isEmpty);
    expect(api.token, 'new-token');
  });
  test('failed lyrics keep the existing optional-result contract', () async {
    mock.expired = true;
    expect(
      await platform.getLyrics('0123456789abcdef0123456789abcdef'),
      isNull,
    );
    expect(await platform.getLyricsByInfo('中文歌', 123000), isNull);
    expect(expiryReports, [PlatformType.kugou, PlatformType.kugou]);
    expect(legacyRequests, 0);
  });
  test('decoder matches the four MiB native lyric bound', () {
    final large = List.filled(2 * 1024 * 1024 + 1, 'a').join();
    expect(
      NativeQqResult.fromJson(
        {
          'lyrics': {'lrc': large},
        },
        'lyrics',
        platform: PlatformType.kugou,
      ).lyrics!.lrc!.length,
      large.length,
    );
    expect(
      () => NativeQqResult.fromJson({
        'lyrics': {'lrc': large},
      }, 'lyrics'),
      throwsFormatException,
    );
    for (final data in [
      {
        'lyrics': {'lrc': 123},
      },
      {
        'lyrics': {'unknown': 'text'},
      },
      {'lyrics': 'malformed'},
    ]) {
      expect(
        () => NativeQqResult.fromJson(
          data,
          'lyrics',
          platform: PlatformType.kugou,
        ),
        throwsFormatException,
      );
    }
  });
}
