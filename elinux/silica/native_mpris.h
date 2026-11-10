#pragma once

#include "native_artwork.h"
#include "native_player.h"
#include <QDBusMessage>
#include <QDBusObjectPath>

class NativeMpris : public QObject {
  Q_OBJECT
 public:
  NativeMpris(NativePlayer& player, NativeArtwork& artwork,
              std::function<QString()> artworkSource = {}, std::function<void()> raise = {}, QObject* parent = nullptr);
  ~NativeMpris() override;
  bool start();
  void close();
  bool canRaise() const { return bool(raise_); }
  bool hasSong() const { return !player_.currentSong().isEmpty(); }
  QString playbackStatus() const;
  qlonglong position() const;
  QDBusObjectPath trackPath() const;
  QVariantMap metadata() const;
  void raise();
  void seek(qlonglong offset);
  void setPosition(const QDBusObjectPath& track, qlonglong position);
  void execute(const QDBusMessage& call, const std::function<void()>& action);
 signals:
  void seeked(qlonglong position);
 private:
  NativePlayer& player_;
  NativeArtwork& artwork_;
  std::function<QString()> artworkSource_;
  std::function<void()> raise_;
  QString artworkSourceUrl_, artworkUrl_;
  QVariantMap published_;
  bool started_ = false;
  void update();
  QVariantMap properties() const;
};
