import 'dart:convert';
import 'dart:typed_data';
import 'package:flutter_test/flutter_test.dart';
import 'package:mconnect/sailfish/core_transfer.dart';

void main() {
  test('large Unicode results round-trip in bounded ASCII chunks', () {
    final store = CoreTransferStore();
    final value = {'text': '中文🎵' * 30000};
    final descriptor = jsonDecode(store.encode(value));
    final bytes = BytesBuilder();
    for (var i = 0; i < descriptor['parts']; i++) {
      final part = store.part({
        'token': descriptor['_mconnectTransfer'],
        'index': i,
      });
      expect(jsonEncode(part).length, lessThan(48 * 1024));
      bytes.add(base64Decode(part['data'] as String));
    }
    expect(jsonDecode(utf8.decode(bytes.takeBytes())), value);
    expect(
      () => store.part({'token': descriptor['_mconnectTransfer'], 'index': 0}),
      throwsArgumentError,
    );
  });
  test('expired and invalid transfers are rejected', () {
    var now = DateTime(2026);
    final store = CoreTransferStore(now: () => now);
    final descriptor = jsonDecode(store.encode({'text': 'x' * 70000}));
    expect(
      () => store.part({'token': descriptor['_mconnectTransfer'], 'index': -1}),
      throwsArgumentError,
    );
    now = now.add(const Duration(minutes: 2));
    expect(
      () => store.part({'token': descriptor['_mconnectTransfer'], 'index': 0}),
      throwsArgumentError,
    );
  });
}
