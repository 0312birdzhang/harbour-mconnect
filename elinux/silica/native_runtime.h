#pragma once

#include "native_core.h"
#include "native_local_music.h"
#include "native_login.h"
#include "native_lyric_loader.h"
#include "native_player.h"

class NativeCoreRuntime : public QObject {
  Q_OBJECT
 public:
  NativeCoreRuntime(NativeStorage& storage, QObject* parent = nullptr,
                    NativeMusicServices::ProviderCall provider = {}, NativeDownloads::WifiProbe wifi = {},
                    NativeLyricLoader::LrclibFetch lrclib = {});
  ~NativeCoreRuntime() override;
  QVariant dispatch(const QString& method, const QVariantMap& arguments);
  QStringList requestNames() const;
  bool start();
  void close();
  bool hasOngoingWork() const;
  NativePlayer& player() { return player_; }
  NativeLyricLoader& lyrics() { return lyrics_; }
  NativeMusicServices& music() { return music_; }
  NativeCoreService& service() { return service_; }
  QString artworkSource() const;
 private:
  using Request = std::function<QVariant(const QVariantMap&)>;
  NativeStorage& storage_;
  NativeMusicServices music_;
  NativeLibrary library_;
  NativeResults results_;
  NativeRecommendations recommendations_;
  NativeDownloads downloads_;
  NativeLogin login_;
  NativeLocalMusic local_;
  NativeLyricLoader lyrics_;
  NativePlayer player_;
  NativeCoreService service_;
  QTimer statePublish_;
  QVariantMap lastState_;
  QHash<QString, Request> requests_;
  bool closed_ = false;
  int executing_ = 0;
  QVariantMap song(const QVariantMap& arguments);
  QVariantMap songInfo(const QVariantMap& arguments);
  QVariantMap discoverySongs(const QVariantMap& arguments);
  QVariantMap importPlaylist(const QVariantMap& arguments);
  void editPlaylist(const QVariantMap& arguments, const QVariantMap& song = {});
  QVariantList libraryPlaylists(const QVariant& source);
  QVariantMap librarySongs(const QVariantMap& arguments);
  void requests();
};
