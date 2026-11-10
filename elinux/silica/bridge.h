#pragma once
#include <QObject>
#include <QVariantMap>
#include <QDBusConnection>
#include <QNetworkAccessManager>
#include <QTimer>
#include <functional>
#include <memory>
#include "native_artwork.h"

class CoreBridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap lyrics READ lyrics NOTIFY lyricsChanged)
  Q_PROPERTY(QString artwork READ artwork NOTIFY artworkChanged)
  Q_PROPERTY(QString coverInk READ coverInk NOTIFY artworkChanged)
  Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
  Q_PROPERTY(QVariantList queue READ queue NOTIFY queueChanged)
  Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
  Q_PROPERTY(QVariantMap login READ login NOTIFY loginChanged)
 public:
  explicit CoreBridge(QObject* parent = nullptr, bool enabled = true);
  ~CoreBridge() override;
  void start();
  void suspend();
  bool hasPendingWork() const { return !pendingRequests_.isEmpty() || !lyricJobs_.isEmpty(); }
  QVariantMap state() const { return state_; }
  QVariantMap lyrics() const { return lyrics_; }
  QString artwork() const { return artwork_; }
  QString coverInk() const { return coverInk_; }
  bool ready() const { return ready_; }
  QVariantList queue() const { return queue_; }
  QVariantList accounts() const { return accounts_; }
  QVariantMap login() const { return login_; }
  Q_INVOKABLE int request(const QString& method, const QVariantMap& args = {});
  Q_INVOKABLE bool copyText(const QString& text);
  Q_INVOKABLE QString clipboardText();
  NativeArtwork* artworkBackend() { return &artworkCache_; }
 signals:
  void stateChanged();
  void lyricsChanged();
  void artworkChanged();
  void readyChanged();
  void queueChanged();
  void accountsChanged();
  void loginChanged();
  void completed(int id, const QVariant& value, const QString& error);
 private slots:
  void changed(const QString& topic, const QString& json);
  void reconnect();
 private:
  struct Transfer;
  void sendRequest(int id, const QString& method, const QVariantMap& args);
  void decodeResult(const QString& json, std::function<void(QVariant,QString)> done);
  void readPart(std::shared_ptr<Transfer> transfer);
  void finishRequest(int id, const QString& method, const QVariant& value, const QString& error);
  void applyState(const QVariantMap& state);
  void loadCoverInk(const QString& url, const QString& track);
  QVariantMap state_, lyrics_, login_;
  QVariantList queue_, accounts_;
  QString artwork_, track_;
  QString coverInk_ = "#ffffff";
  QDBusConnection bus_;
  QNetworkAccessManager network_;
  NativeArtwork artworkCache_;
  QSet<QThread*> lyricJobs_;
  QSet<int> pendingRequests_;
  QTimer reconnectTimer_;
  bool ready_ = false, connecting_ = false, started_ = false;
  int sequence_ = 0;
};
