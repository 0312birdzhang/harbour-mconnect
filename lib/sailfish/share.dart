import '../l10n/l10n.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:share_plus/share_plus.dart' show XFile;
import '../core/router/app_router.dart';
import 'external_actions.dart';
export 'package:share_plus/share_plus.dart' show XFile;

class ShareParams {
  const ShareParams({
    this.text,
    this.subject,
    this.files,
    this.sharePositionOrigin,
  });
  final String? text;
  final String? subject;
  final List<XFile>? files;
  final Rect? sharePositionOrigin;
}

/// Sailfish has no Android share-sheet plugin. Provide an explicit copy/open
/// workflow so exports remain usable instead of throwing MissingPluginException.
class SharePlus {
  static final instance = SharePlus();

  Future<void> share(ShareParams params) async {
    final context = appRouter.routerDelegate.navigatorKey.currentContext;
    if (context == null) throw StateError(zhAppLocalizations.sfosShareNotReady);
    final content = [
      if (params.text != null) params.text!,
      for (final file in params.files ?? <XFile>[]) file.path,
    ].join('\n');
    await showDialog<void>(
      context: context,
      builder: (context) => AlertDialog(
        title: Text(params.subject ?? zhAppLocalizations.sfosShareExport),
        content: SingleChildScrollView(child: SelectableText(content)),
        actions: [
          TextButton(
            onPressed: () async {
              await Clipboard.setData(ClipboardData(text: content));
              if (context.mounted) Navigator.pop(context);
            },
            child: Text(zhAppLocalizations.sfosCopy),
          ),
          if (params.files?.isNotEmpty ?? false)
            TextButton(
              onPressed: () async {
                final messenger = ScaffoldMessenger.of(context);
                try {
                  if (!await openExternal(params.files!.first.path)) {
                    messenger.showSnackBar(
                      SnackBar(
                        content: Text(zhAppLocalizations.sfosCannotOpenFile),
                      ),
                    );
                  }
                } catch (error) {
                  messenger.showSnackBar(
                    SnackBar(
                      content: Text(
                        zhAppLocalizations.sfosOpenFileError(error.toString()),
                      ),
                    ),
                  );
                }
              },
              child: Text(zhAppLocalizations.sfosOpenFile),
            ),
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: Text(zhAppLocalizations.sfosClose),
          ),
        ],
      ),
    );
  }
}
