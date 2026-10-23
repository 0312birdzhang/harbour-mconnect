import 'dart:convert';
import 'dart:typed_data';
import 'package:dio/dio.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/platform/bilibili/bilibili_platform.dart';
import 'package:mconnect/models/audio_quality.dart';
import 'package:mconnect/models/platform_type.dart';

class FixtureAdapter implements HttpClientAdapter {
  final requests = <RequestOptions>[];
  @override
  Future<ResponseBody> fetch(
    RequestOptions options,
    Stream<Uint8List>? stream,
    Future<void>? cancel,
  ) async {
    requests.add(options);
    dynamic data;
    var code = 0;
    switch (options.uri.path) {
      case '/x/frontend/finger/spi':
        data = {'b_3': 'anonymous'};
      case '/x/web-interface/nav':
        code = -101;
        data = {
          'wbi_img': {
            'img_url':
                'https://i0.hdslb.com/bfs/wbi/7cd084941338484aae1ad9425b84077c.png',
            'sub_url':
                'https://i0.hdslb.com/bfs/wbi/4932caff0ff746eab6f01bf08b70ac45.png',
          },
        };
      case '/x/web-interface/wbi/search/type':
        data = {
          'result': [
            {
              'bvid': 'BVtest',
              'title': '<em>音乐</em>&amp;测试',
              'author': 'UP',
              'pic': '//example.test/a.jpg',
              'duration': '1:02:03',
            },
          ],
        };
      case '/x/player/pagelist':
        data = [
          {'cid': 11},
          {'cid': 22},
        ];
      case '/x/player/wbi/playurl':
        data = {
          'dash': {
            'audio': [
              {'baseUrl': 'https://a.bilivideo.com/high', 'bandwidth': 192000},
              {'base_url': 'https://a.bilivideo.com/low', 'bandwidth': 64000},
            ],
          },
        };
      case '/x/v3/fav/resource/list':
        data = {
          'has_more': options.queryParameters['pn'] == 1,
          'medias': [
            {
              'bvid': options.queryParameters['pn'] == 1 ? 'BV1' : 'BV2',
              'title': 'song',
              'duration': 60,
              'upper': {'name': 'UP'},
            },
          ],
        };
      case '/x/player/wbi/v2':
        data = {
          'subtitle': {
            'subtitles': [
              {'lan': 'zh-CN', 'subtitle_url': 'https://subtitle.test/content'},
            ],
          },
        };
      case '/content':
        return ResponseBody.fromString(
          jsonEncode({
            'body': [
              {'from': 62.34, 'content': '歌词'},
            ],
          }),
          200,
          headers: {
            Headers.contentTypeHeader: ['application/json'],
          },
        );
      default:
        throw StateError('Unexpected endpoint ${options.uri}');
    }
    return ResponseBody.fromString(
      jsonEncode({'code': code, 'data': data}),
      200,
      headers: {
        Headers.contentTypeHeader: ['application/json'],
      },
    );
  }

  @override
  void close({bool force = false}) {}
}

void main() {
  late FixtureAdapter adapter;
  late BilibiliPlatform platform;
  setUp(() {
    adapter = FixtureAdapter();
    platform = BilibiliPlatform(dio: Dio()..httpClientAdapter = adapter);
  });
  test('WBI published signing vector and filtered parameters', () {
    final result = BilibiliPlatform.sign(
      {'foo': '114', 'bar': '514', 'zab': 1919810},
      '7cd084941338484aae1ad9425b84077c',
      '4932caff0ff746eab6f01bf08b70ac45',
      1702204169,
    );
    expect(result['w_rid'], '8f6f2b5b3d485fe1886cec6a0be8c5d4');
    expect(
      BilibiliPlatform.sign(
        {'word': "a!'()*b"},
        '7cd084941338484aae1ad9425b84077c',
        '4932caff0ff746eab6f01bf08b70ac45',
        1,
      )['word'],
      'ab',
    );
  });
  test(
    'anonymous search accepts nav WBI keys and normalizes metadata',
    () async {
      final songs = await platform.search('音乐', page: 2);
      expect(songs.single.platform, PlatformType.bilibili);
      expect(songs.single.name, '音乐&测试');
      expect(songs.single.duration.inSeconds, 3723);
      expect(songs.single.coverUrl, 'https://example.test/a.jpg');
      final request = adapter.requests.last;
      expect(request.queryParameters['page'], '2');
      expect(request.headers['Cookie'], contains('buvid3=anonymous'));
      expect(request.queryParameters['w_rid'], isNotEmpty);
    },
  );
  test('audio-only quality selection preserves explicit part cid', () async {
    expect(await platform.getSongUrl('BVtest:22'), endsWith('/low'));
    expect(adapter.requests.last.queryParameters['cid'], '22');
    expect(
      await platform.getSongUrl('BVtest', quality: AudioLevel.high),
      endsWith('/high'),
    );
    expect(
      (await platform.getAvailableQualities('BVtest')).map((e) => e.format),
      everyElement('m4a'),
    );
  });
  test('favorite pagination reads subsequent pages', () async {
    expect((await platform.getPlaylistDetail('123')).map((e) => e.id), [
      'BV1',
      'BV2',
    ]);
  });
  test(
    'subtitles become timed lyrics without forwarding cookies to CDN',
    () async {
      await platform.search('音乐');
      expect(await platform.getLyrics('BVtest'), '[01:02.34]歌词');
      expect(adapter.requests.last.headers.containsKey('Cookie'), false);
    },
  );
  test('foreign share links are rejected without requests', () async {
    expect(
      await platform.parseShareLink(
        'https://bilibili.com.evil.test/video/BV123',
      ),
      null,
    );
    expect(adapter.requests, isEmpty);
  });
}
