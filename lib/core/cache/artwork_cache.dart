import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';
import 'package:crypto/crypto.dart';
import '../../sailfish/paths.dart' as paths;

/// Private, cache-first artwork storage. MCA files contain an integrity digest
/// and a masked payload, rather than an image header a media scanner recognises.
/// This is a storage envelope, not encryption or an access-control mechanism.
class ArtworkCache {
  ArtworkCache({
    Future<Directory> Function()? directory,
    Future<Uint8List> Function(String)? fetch,
    Future<File?> Function(String)? legacyFile,
    this.maxDiskBytes = 200 * 1024 * 1024,
  }) : _directory =
           directory ??
           (() async => Directory(
             '${(await paths.getTemporaryDirectory()).path}/.artwork',
           )),
       _fetch = fetch ?? _download {
    _legacyFile =
        legacyFile ?? (directory == null ? _findLegacy : (_) async => null);
  }
  static final instance = ArtworkCache();
  final Future<Directory> Function() _directory;
  final Future<Uint8List> Function(String) _fetch;
  late final Future<File?> Function(String) _legacyFile;
  Future<Map<String, File>>? _legacyIndex;
  final int maxDiskBytes;
  final _pending = <String, Future<Uint8List>>{};
  final Map<String, Uint8List> _memory = <String, Uint8List>{};
  int _memoryBytes = 0;
  static const maxImageBytes = 16 * 1024 * 1024;
  final _routes = <String, String>{};
  final _token = List.generate(
    20,
    (_) => Random.secure().nextInt(256).toRadixString(16).padLeft(2, '0'),
  ).join();
  Future<HttpServer>? _server;
  Future<void> _pruning = Future.value();

  static String _normalise(String url) =>
      url.startsWith('//') ? 'https:$url' : url;
  static String _key(String url) => sha256.convert(utf8.encode(url)).toString();
  Future<Uint8List> get(String source) {
    final url = _normalise(source);
    final bytes = _memory.remove(url);
    if (bytes != null) {
      _memory[url] = bytes;
      return Future.value(bytes);
    }
    return _pending.putIfAbsent(url, () async {
      try {
        final data = await _load(url);
        _memory[url] = data;
        _memoryBytes += data.length;
        while (_memoryBytes > 32 * 1024 * 1024 || _memory.length > 20) {
          _memoryBytes -= _memory.remove(_memory.keys.first)!.length;
        }
        return data;
      } finally {
        unawaited(_pending.remove(url));
      }
    });
  }

  Future<Uint8List> _load(String url) async {
    final uri = Uri.tryParse(url);
    if (uri?.scheme == 'file' || url.startsWith('/')) {
      final file = uri?.scheme == 'file' ? File.fromUri(uri!) : File(url);
      if (await file.length() >
          maxImageBytes + (file.path.endsWith('.mca') ? 36 : 0)) {
        throw const FormatException('Artwork too large');
      }
      final bytes = await file.readAsBytes();
      return file.path.endsWith('.mca') ? decode(bytes) : bytes;
    }
    if (uri == null || !{'http', 'https'}.contains(uri.scheme)) {
      throw const FormatException('Unsupported artwork URL');
    }
    File? file;
    try {
      final dir = await _directory();
      await dir.create(recursive: true);
      await File('${dir.path}/.nomedia').writeAsString('');
      file = File('${dir.path}/${_key(url)}.mca');
      if (await file.exists()) {
        if (await file.length() > maxImageBytes + 36) {
          throw const FormatException('Artwork too large');
        }
        final result = decode(await file.readAsBytes());
        // Last access drives bounded LRU eviction; expiration never causes a fetch.
        try {
          await file.setLastModified(DateTime.now());
        } catch (_) {}
        return result;
      }
    } catch (_) {
      /* Corrupt/unreadable cache falls back to a fresh download. */
    }
    File? legacy;
    Uint8List? migrated;
    try {
      legacy = await _legacyFile(url);
      if (legacy != null && await legacy.length() <= maxImageBytes) {
        migrated = await legacy.readAsBytes();
      }
    } catch (_) {}
    final data =
        migrated ?? await _fetch(url).timeout(const Duration(seconds: 25));
    if (data.length > maxImageBytes || !_imageHeader(data)) {
      throw const FormatException('Invalid artwork size');
    }
    if (file != null) {
      final temporary = File('${file.path}.part');
      try {
        await temporary.writeAsBytes(encode(data), flush: true);
        await temporary.rename(file.path);
        if (migrated != null && legacy != null) {
          try {
            await legacy.delete();
          } catch (_) {}
        }
        _pruning = _pruning
            .then((_) => _prune(file!.parent))
            .catchError((Object _) {});
      } catch (_) {
        try {
          if (await temporary.exists()) await temporary.delete();
        } catch (_) {}
      }
    }
    return data;
  }

  Future<File?> _findLegacy(String url) async {
    final index = await (_legacyIndex ??= _readLegacyIndex());
    final file = index[url];
    return file != null && await file.exists() ? file : null;
  }

  Future<Map<String, File>> _readLegacyIndex() async {
    final result = <String, File>{};
    try {
      final metadata = File(
        '${(await paths.getApplicationSupportDirectory()).path}/libCachedImageData.json',
      );
      if (!await metadata.exists() ||
          await metadata.length() > 8 * 1024 * 1024) {
        return result;
      }
      final rows = jsonDecode(await metadata.readAsString()) as List;
      final base = (await paths.getTemporaryDirectory()).path;
      for (final entry in rows.whereType<Map>()) {
        final url = entry['url'], name = entry['relativePath'];
        if (url is! String ||
            name is! String ||
            !RegExp(r'^[A-Za-z0-9_.-]+$').hasMatch(name)) {
          continue;
        }
        if (!result.containsKey(url) || entry['key'] == url) {
          result[url] = File('$base/libCachedImageData/$name');
        }
      }
    } catch (_) {}
    return result;
  }

  static Uint8List encode(Uint8List bytes) {
    final result = Uint8List(bytes.length + 36);
    result.setAll(0, [77, 67, 65, 49]);
    result.setAll(4, sha256.convert(bytes).bytes);
    for (var i = 0; i < bytes.length; i++) {
      result[i + 36] = bytes[i] ^ 0xa7;
    }
    return result;
  }

  // Do not persist an HTML error page returned as HTTP 200 by an image CDN.
  static bool _imageHeader(Uint8List bytes) {
    if (bytes.length < 12) return false;
    return (bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff) ||
        (bytes[0] == 137 &&
            ascii.decode(bytes.sublist(1, 4), allowInvalid: true) == 'PNG') ||
        ascii.decode(bytes.sublist(0, 3), allowInvalid: true) == 'GIF' ||
        (ascii.decode(bytes.sublist(0, 4), allowInvalid: true) == 'RIFF' &&
            ascii.decode(bytes.sublist(8, 12), allowInvalid: true) == 'WEBP') ||
        (bytes[0] == 66 && bytes[1] == 77) ||
        ascii.decode(bytes.sublist(4, 8), allowInvalid: true) == 'ftyp';
  }

  static Uint8List decode(Uint8List bytes) {
    if (bytes.length < 37 ||
        bytes.length > maxImageBytes + 36 ||
        bytes[0] != 77 ||
        bytes[1] != 67 ||
        bytes[2] != 65 ||
        bytes[3] != 49) {
      throw const FormatException('Invalid MCA artwork');
    }
    final result = Uint8List(bytes.length - 36);
    for (var i = 0; i < result.length; i++) {
      result[i] = bytes[i + 36] ^ 0xa7;
    }
    final digest = sha256.convert(result).bytes;
    for (var i = 0; i < 32; i++) {
      if (bytes[i + 4] != digest[i]) {
        throw const FormatException('Corrupt MCA artwork');
      }
    }
    return result;
  }

  static Future<Uint8List> _download(String url) async {
    final client = HttpClient()
      ..connectionTimeout = const Duration(seconds: 10);
    try {
      final request = await client.getUrl(Uri.parse(url));
      request.headers.set('User-Agent', 'Mozilla/5.0');
      final host = Uri.parse(url).host;
      if (host == 'hdslb.com' ||
          host.endsWith('.hdslb.com') ||
          host.endsWith('.bilibili.com')) {
        request.headers.set('Referer', 'https://www.bilibili.com/');
      }
      final response = await request.close();
      if (response.statusCode != 200 ||
          response.contentLength > maxImageBytes) {
        throw HttpException('Artwork HTTP ${response.statusCode}');
      }
      final builder = BytesBuilder(copy: false);
      await for (final bytes in response.timeout(const Duration(seconds: 20))) {
        if (builder.length + bytes.length > maxImageBytes) {
          throw const FormatException('Artwork too large');
        }
        builder.add(bytes);
      }
      return builder.takeBytes();
    } finally {
      client.close(force: true);
    }
  }

  Future<void> _prune(Directory dir) async {
    final entries = <({File file, FileStat stat})>[];
    await for (final entity in dir.list()) {
      if (entity is File && entity.path.endsWith('.mca')) {
        entries.add((file: entity, stat: await entity.stat()));
      }
    }
    var size = entries.fold<int>(0, (n, e) => n + e.stat.size);
    entries.sort((a, b) => a.stat.modified.compareTo(b.stat.modified));
    for (final entry in entries) {
      if (size <= maxDiskBytes) break;
      await entry.file.delete();
      size -= entry.stat.size;
    }
  }

  /// System MPRIS/cover readers get decoded bytes over loopback. No ordinary
  /// JPEG/PNG file ever needs to be exported to Documents, Pictures or Music.
  Future<String> mediaUrl(String source) async {
    final url = _normalise(source), key = _key(_normalise(source));
    _routes[key] = url;
    if (_routes.length > 1000) _routes.remove(_routes.keys.first);
    final server = await (_server ??= _startServer());
    return 'http://127.0.0.1:${server.port}/$_token/$key';
  }

  Future<HttpServer> _startServer() async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    server.listen((request) async {
      try {
        final parts = request.uri.pathSegments;
        final source = parts.length == 2 && parts.first == _token
            ? _routes[parts.last]
            : null;
        if (source == null || !{'GET', 'HEAD'}.contains(request.method)) {
          request.response.statusCode = 404;
        } else {
          final data = await get(source);
          request.response.headers.contentType = ContentType.binary;
          request.response.contentLength = data.length;
          if (request.method == 'GET') request.response.add(data);
        }
      } catch (_) {
        try {
          request.response.statusCode = 404;
        } catch (_) {}
      }
      try {
        await request.response.close();
      } catch (_) {}
    });
    return server;
  }

  Future<void> dispose() async {
    if (_server != null) await (await _server!).close(force: true);
    await _pruning;
  }
}
