import 'dart:io';

enum LaunchMode { platformDefault, externalApplication }

Future<bool> launchUrl(
  Uri uri, {
  LaunchMode mode = LaunchMode.platformDefault,
}) async {
  if (!['http', 'https', 'mailto'].contains(uri.scheme)) return false;
  return openExternal(uri.toString());
}

Future<bool> openExternal(
  String target, {
  Future<ProcessResult> Function(String, List<String>)? runner,
}) async {
  final result = await (runner ?? Process.run)('/usr/bin/xdg-open', [target]);
  return result.exitCode == 0;
}
