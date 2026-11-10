#pragma once
#include <QString>
#include <QVariantMap>

// Native first-stage backend. A null map means the legacy provider must handle
// this format/source. It never changes the database schema or cached contents.
QVariantMap ReadNativeLyrics(const QString& database, const QString& platform,
                             const QString& song);
QVariantList ParseNativeLrc(const QString& content);
QVariantList ParseNativeLyrics(const QString& content, const QString& format);
QVariantMap BuildNativeLyricsBundle(const QVariantMap& bundle);
QString DecodeNativeLyricBytes(QByteArray bytes);
QVariantMap DecodeNativeSidecar(const QByteArray& bytes, const QString& extension);
