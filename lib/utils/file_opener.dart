import '../l10n/l10n.dart';
import 'package:flutter/services.dart';
import '../core/platform/platform_utils.dart';
import '../sailfish/external_actions.dart';

class FileOpener {
  static const _channel = MethodChannel('com.mconnect.mconnect/file_opener');

  static Future<void> openFile(String filePath) async {
    if (PlatformUtils.isLinux) {
      if (!await openExternal(filePath)) {
        throw StateError(zhAppLocalizations.sfosCannotOpenFile);
      }
      return;
    }
    try {
      await _channel.invokeMethod('openFile', filePath);
    } on PlatformException catch (e) {
      throw Exception('无法打开文件: ${e.message}');
    }
  }

  static Future<void> openFolder(String folderPath) async {
    if (PlatformUtils.isLinux) {
      if (!await openExternal(folderPath)) {
        throw StateError(zhAppLocalizations.sfosCannotOpenFolder);
      }
      return;
    }
    try {
      await _channel.invokeMethod('openFolder', folderPath);
    } on PlatformException catch (e) {
      throw Exception('无法打开文件夹: ${e.message}');
    }
  }
}
