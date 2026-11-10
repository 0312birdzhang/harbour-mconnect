#pragma once
#include <QString>
#include <QVariantMap>
#include <QList>

struct NativeArtist {
  QString id, name;
  QString avatarUrl={}, briefDesc={};
  QVariant songCount={}, albumCount={}, fansCount={};
  QVariantMap json() const;
};
struct NativeAlbum {
  QString id, name, coverUrl;
  QString artistId={}, artistName={}, releaseDate={}, description={}, company={}, genre={};
  QVariant songCount={};
  QVariantMap json() const;
};
struct NativeSong {
  QString id, platform, name, coverUrl;
  QList<NativeArtist> artists;
  NativeAlbum album;
  bool hasAlbum=false;
  qint64 durationMs=0;
  QVariant trackNumber, fee;
  QVariantMap json() const;
};
struct NativePlaylist {
  QString id, platform, name, coverUrl, creatorName;
  qint64 songCount=0;
  QVariantMap json() const;
};
// Shared wire models retain owning-platform IDs and optional album/artist IDs.
// Provider parsers do not create playback URLs, local paths or new identities.
NativeSong ParseNeteaseSong(const QVariantMap& value);
NativePlaylist ParseNeteasePlaylist(const QVariantMap& value);
NativeArtist ParseNeteaseArtist(const QVariantMap& value);
NativeAlbum ParseNeteaseAlbum(const QVariantMap& value);
