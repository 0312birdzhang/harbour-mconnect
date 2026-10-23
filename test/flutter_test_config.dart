import 'dart:async';
import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

/// The Sailfish runner links system SQLite before starting Dart. flutter_tester
/// doesn't, so give host tests the same process-global SQLite symbols.
Future<void> testExecutable(FutureOr<void> Function() testMain) async {
  if (Platform.isLinux) {
    final open = DynamicLibrary.open('libdl.so.2')
        .lookupFunction<
          Pointer<Void> Function(Pointer<Utf8>, Int32),
          Pointer<Void> Function(Pointer<Utf8>, int)
        >('dlopen');
    final name = 'libsqlite3.so.0'.toNativeUtf8();
    try {
      if (open(name, 0x101) == nullptr) {
        throw StateError('Host tests require system libsqlite3.so.0');
      }
    } finally {
      calloc.free(name);
    }
  }
  await testMain();
}
