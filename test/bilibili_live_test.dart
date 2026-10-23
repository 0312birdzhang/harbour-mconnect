import 'dart:io';
import 'package:dio/dio.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/platform/bilibili/bilibili_platform.dart';

// Explicit opt-in: reads public APIs and a small audio range; never plays audio.
void main() {
  test('public search -> DASH audio -> CDN range', () async {
    final platform = BilibiliPlatform();
    final songs = await platform.search('周杰伦 晴天', limit: 5);
    expect(songs, isNotEmpty);
    final url = await platform.getSongUrl(songs.first.id);
    final client = Dio();
    final response = await client.get<ResponseBody>(url,
      options: Options(responseType: ResponseType.stream, receiveTimeout: const Duration(seconds: 20),
        headers: {...BilibiliPlatform.mediaHeaders, 'Range': 'bytes=0-1023'}));
    expect(response.statusCode, anyOf(200, 206));
    final first = await response.data!.stream.first;
    expect(first, isNotEmpty);
    client.close(force: true);
    // Log identifiers only: signed CDN URLs may contain session-related data.
    // ignore: avoid_print
    print('Bilibili live API/CDN passed: ${songs.length} search results, ${first.length} bytes');
  }, skip: Platform.environment['BILIBILI_LIVE'] != '1', timeout: const Timeout(Duration(minutes: 2)));
}
