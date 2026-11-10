import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/core/cache/artwork_cache.dart';

void main() {
  late Directory dir;
  final png = base64Decode(
    'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aWQAAAABJRU5ErkJggg==',
  );
  setUp(() async {
    dir = await Directory.systemTemp.createTemp('mconnect-art-test-');
  });
  tearDown(() async {
    await dir.delete(recursive: true);
  });
  test('an HTML error response is never persisted as artwork', () async {
    final cache = ArtworkCache(
      directory: () async => dir,
      fetch: (_) async => Uint8List.fromList(utf8.encode('<html>error</html>')),
    );
    await expectLater(
      cache.get('https://example.test/error'),
      throwsFormatException,
    );
    expect(dir.listSync().where((f) => f.path.endsWith('.mca')), isEmpty);
    await cache.dispose();
  });
  test(
    'private envelope survives restart without network or image filenames',
    () async {
      var requests = 0;
      final first = ArtworkCache(
        directory: () async => dir,
        fetch: (_) async {
          requests++;
          return png;
        },
      );
      final loaded = await Future.wait([
        first.get('https://example.test/cover.jpg'),
        first.get('https://example.test/cover.jpg'),
      ]);
      expect(loaded.first, png);
      expect(requests, 1);
      await first.dispose();
      final files = dir
          .listSync()
          .whereType<File>()
          .where((f) => f.path.endsWith('.mca'))
          .toList();
      expect(files, hasLength(1));
      final bytes = await files.single.readAsBytes();
      expect(bytes.take(4), [77, 67, 65, 49]);
      expect(bytes.skip(36).take(8), isNot(png.take(8)));
      expect(
        dir.listSync().any(
          (f) => RegExp(r'\.(png|jpe?g|webp|gif)$').hasMatch(f.path),
        ),
        false,
      );
      final second = ArtworkCache(
        directory: () async => dir,
        fetch: (_) async => throw StateError('offline'),
      );
      expect(await second.get('https://example.test/cover.jpg'), png);
      await second.dispose();
    },
  );
  test('corrupt disk payload is replaced instead of being returned', () async {
    final first = ArtworkCache(
      directory: () async => dir,
      fetch: (_) async => png,
    );
    await first.get('https://example.test/a');
    await first.dispose();
    final file = dir.listSync().whereType<File>().firstWhere(
      (f) => f.path.endsWith('.mca'),
    );
    final broken = await file.readAsBytes();
    broken[broken.length - 1] ^= 1;
    await file.writeAsBytes(broken);
    var requests = 0;
    final second = ArtworkCache(
      directory: () async => dir,
      fetch: (_) async {
        requests++;
        return png;
      },
    );
    expect(await second.get('https://example.test/a'), png);
    expect(requests, 1);
    await second.dispose();
  });
  test('existing ordinary cache migrates without a network request', () async {
    final old = File('${dir.path}/old.jpg');
    await old.writeAsBytes(png);
    final cache = ArtworkCache(
      directory: () async => Directory('${dir.path}/private'),
      fetch: (_) async => throw StateError('offline'),
      legacyFile: (_) async => old,
    );
    expect(await cache.get('https://example.test/old'), png);
    expect(await old.exists(), false);
    await cache.dispose();
  });
  test(
    'loopback cover endpoint decodes cached art and rejects unknown routes',
    () async {
      final cache = ArtworkCache(
        directory: () async => dir,
        fetch: (_) async => png,
      );
      await cache.get('https://example.test/a');
      final url = await cache.mediaUrl('https://example.test/a');
      final client = HttpClient();
      final response = await (await client.getUrl(Uri.parse(url))).close();
      final body = await response.fold<BytesBuilder>(
        BytesBuilder(),
        (b, v) => b..add(v),
      );
      expect(response.statusCode, 200);
      expect(body.takeBytes(), png);
      final invalid = await (await client.getUrl(
        Uri.parse(url).replace(path: '/invalid/path'),
      )).close();
      expect(invalid.statusCode, 404);
      await invalid.drain<void>();
      client.close(force: true);
      await cache.dispose();
    },
  );
}
