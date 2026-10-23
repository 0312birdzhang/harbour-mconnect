import '../l10n/l10n.dart';
import 'dart:io';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:path/path.dart' as p;
import '../core/router/app_router.dart';
import 'paths.dart';

enum FileType { any, image, custom }

class PlatformFile {
  const PlatformFile({required this.path, required this.name, this.bytes});
  final String? path;
  final String name;
  final Uint8List? bytes;
}

class FilePickerResult {
  const FilePickerResult(this.files);
  final List<PlatformFile> files;
}

/// Flutter-only picker; no GTK desktop or Android document-provider dependency.
class FilePicker {
  static Future<String?> getDirectoryPath({String? dialogTitle}) => _pick(
    directory: true,
    title: dialogTitle ?? zhAppLocalizations.sfosSelectFolder,
  );

  static Future<FilePickerResult?> pickFiles({
    FileType type = FileType.any,
    List<String>? allowedExtensions,
    bool allowMultiple = false,
    bool withData = false,
  }) async {
    final path = await _pick(
      directory: false,
      title: zhAppLocalizations.sfosSelectFile,
      extensions: type == FileType.image
          ? ['jpg', 'jpeg', 'png', 'webp', 'gif', 'bmp']
          : allowedExtensions,
    );
    if (path == null) return null;
    return FilePickerResult([
      PlatformFile(
        path: path,
        name: p.basename(path),
        bytes: withData ? await File(path).readAsBytes() : null,
      ),
    ]);
  }

  static Future<String?> _pick({
    required bool directory,
    required String title,
    List<String>? extensions,
  }) {
    final context = appRouter.routerDelegate.navigatorKey.currentContext;
    if (context == null) {
      throw StateError(zhAppLocalizations.sfosPickerNotReady);
    }
    return Navigator.of(context).push<String>(
      MaterialPageRoute(
        fullscreenDialog: true,
        builder: (_) =>
            _Picker(directory: directory, title: title, extensions: extensions),
      ),
    );
  }
}

class _Picker extends StatefulWidget {
  const _Picker({
    required this.directory,
    required this.title,
    this.extensions,
  });
  final bool directory;
  final String title;
  final List<String>? extensions;
  @override
  State<_Picker> createState() => _PickerState();
}

class _PickerState extends State<_Picker> {
  late String _path = sailfishHome;
  late Future<List<FileSystemEntity>> _entries = _list();

  Future<List<FileSystemEntity>> _list() async {
    final entries = await Directory(_path).list(followLinks: false).toList();
    entries.removeWhere((entry) {
      if (p.basename(entry.path).startsWith('.')) return true;
      if (entry is Directory) return false;
      if (entry is! File || widget.directory) return true;
      final extensions = widget.extensions;
      final suffix = p.extension(entry.path).toLowerCase();
      return extensions != null &&
          (suffix.isEmpty || !extensions.contains(suffix.substring(1)));
    });
    entries.sort((a, b) {
      if ((a is Directory) != (b is Directory)) return a is Directory ? -1 : 1;
      return p
          .basename(a.path)
          .toLowerCase()
          .compareTo(p.basename(b.path).toLowerCase());
    });
    return entries;
  }

  void _navigate(String path) => setState(() {
    _path = path;
    _entries = _list();
  });

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(
      title: Text(widget.title),
      actions: [
        if (widget.directory)
          TextButton(
            onPressed: () => Navigator.pop(context, _path),
            child: Text(zhAppLocalizations.sfosUseFolder),
          ),
      ],
    ),
    body: SafeArea(
      child: Column(
        children: [
          ListTile(
            leading: const Icon(Icons.arrow_upward),
            title: Text(_path),
            onTap: _path == '/' ? null : () => _navigate(p.dirname(_path)),
          ),
          Wrap(
            children: [
              for (final root in [
                sailfishHome,
                p.join(sailfishHome, 'Music'),
                p.join(sailfishHome, 'Downloads'),
                sailfishDataPath,
                '/run/media',
              ])
                TextButton(
                  onPressed: () => _navigate(root),
                  child: Text(p.basename(root)),
                ),
            ],
          ),
          Expanded(
            child: FutureBuilder<List<FileSystemEntity>>(
              future: _entries,
              builder: (context, snapshot) {
                if (snapshot.hasError) {
                  return Center(
                    child: Text(
                      zhAppLocalizations.sfosReadDirectoryError(
                        snapshot.error.toString(),
                      ),
                    ),
                  );
                }
                if (!snapshot.hasData) {
                  return const Center(child: CircularProgressIndicator());
                }
                final entries = snapshot.data!;
                if (entries.isEmpty) {
                  return Center(child: Text(zhAppLocalizations.sfosNoFiles));
                }
                return ListView.builder(
                  itemCount: entries.length,
                  itemBuilder: (context, index) {
                    final entry = entries[index];
                    final directory = entry is Directory;
                    return ListTile(
                      leading: Icon(
                        directory ? Icons.folder : Icons.insert_drive_file,
                      ),
                      title: Text(p.basename(entry.path)),
                      onTap: () => directory
                          ? _navigate(entry.path)
                          : Navigator.pop(context, entry.path),
                    );
                  },
                );
              },
            ),
          ),
        ],
      ),
    ),
  );
}
