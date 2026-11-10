import 'dart:typed_data';
import 'package:flutter/material.dart';
import '../cache/artwork_cache.dart';

class CachedArtworkImage extends StatefulWidget {
  const CachedArtworkImage({
    super.key,
    required this.imageUrl,
    this.width,
    this.height,
    this.fit,
    this.memCacheWidth,
    this.placeholder,
    this.errorWidget,
  });
  final String imageUrl;
  final double? width, height;
  final BoxFit? fit;
  final int? memCacheWidth;
  final Widget Function(BuildContext, String)? placeholder;
  final Widget Function(BuildContext, String, Object)? errorWidget;
  @override
  State<CachedArtworkImage> createState() => _CachedArtworkImageState();
}

class _CachedArtworkImageState extends State<CachedArtworkImage> {
  late Future<Uint8List> _data;
  @override
  void initState() {
    super.initState();
    _load();
  }

  void _load() {
    _data = ArtworkCache.instance.get(widget.imageUrl);
  }

  @override
  void didUpdateWidget(CachedArtworkImage oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.imageUrl != widget.imageUrl) _load();
  }

  @override
  Widget build(BuildContext context) => FutureBuilder<Uint8List>(
    future: _data,
    builder: (context, snapshot) {
      Widget error(Object e) =>
          widget.errorWidget?.call(context, widget.imageUrl, e) ??
          const SizedBox.shrink();
      final bytes = snapshot.connectionState == ConnectionState.done
          ? snapshot.data
          : null;
      if (bytes != null) {
        return Image.memory(
          bytes,
          width: widget.width,
          height: widget.height,
          fit: widget.fit,
          cacheWidth: widget.memCacheWidth,
          gaplessPlayback: true,
          errorBuilder: (_, e, _) => error(e),
        );
      }
      return SizedBox(
        width: widget.width,
        height: widget.height,
        child: snapshot.hasError
            ? error(snapshot.error!)
            : widget.placeholder?.call(context, widget.imageUrl),
      );
    },
  );
}
