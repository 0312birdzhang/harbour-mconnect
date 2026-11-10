import 'dart:convert';
import 'dart:math';
import 'dart:typed_data';

/// Keep each D-Bus write below the socket's partial-write boundary.
class CoreTransferStore {
  CoreTransferStore({DateTime Function()? now}) : _now = now ?? DateTime.now;
  final DateTime Function() _now;
  final _entries = <String, ({Uint8List bytes, DateTime expires})>{};
  static const partBytes = 24 * 1024;
  static const maxBytes = 8 * 1024 * 1024;

  String encode(Object? value) {
    _purge();
    final json = jsonEncode(value);
    final bytes = utf8.encode(json);
    if (bytes.length <= 48 * 1024) return json;
    if (bytes.length > maxBytes) throw ArgumentError('Result too large');
    final random = Random.secure();
    final token = base64UrlEncode(
      List.generate(20, (_) => random.nextInt(256)),
    );
    _entries[token] = (
      bytes: bytes,
      expires: _now().add(const Duration(minutes: 1)),
    );
    while (_entries.length > 4 ||
        _entries.values.fold<int>(0, (n, e) => n + e.bytes.length) >
            2 * maxBytes) {
      _entries.remove(_entries.keys.first);
    }
    return jsonEncode({
      '_mconnectTransfer': token,
      'parts': (bytes.length / partBytes).ceil(),
    });
  }

  Map<String, Object?> part(Map<String, dynamic> args) {
    _purge();
    final token = args['token'];
    final index = args['index'];
    final entry = _entries[token];
    if (entry == null ||
        index is! int ||
        index < 0 ||
        index * partBytes >= entry.bytes.length) {
      throw ArgumentError();
    }
    final start = index * partBytes;
    final end = min(start + partBytes, entry.bytes.length);
    final data = base64Encode(entry.bytes.sublist(start, end));
    if (end == entry.bytes.length) _entries.remove(token);
    return {'data': data};
  }

  void _purge() => _entries.removeWhere((_, e) => !e.expires.isAfter(_now()));
}
