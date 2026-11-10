#include "bridge.h"
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QTimer>
#include <QNetworkReply>
#include <QGuiApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include "native_lyrics.h"
#include "native_storage.h"
#include "../runner/cover_artwork.h"

namespace {
const char service[] = "org.sailfishos.Mconnect.Core";
const char path[] = "/org/sailfishos/Mconnect/Core";
QVariant decode(const QString& json) {
  const auto doc=QJsonDocument::fromJson(json.toUtf8());
  if (!doc.isNull()) return doc.toVariant();
  const auto list=QJsonDocument::fromJson("["+json.toUtf8()+"]").toVariant().toList();
  return list.isEmpty() ? QVariant() : list.first();
}
}
struct CoreBridge::Transfer {
  QString token;
  int parts=0, index=0;
  QByteArray bytes;
  QElapsedTimer elapsed;
  std::function<void(QVariant,QString)> done;
};
class LyricsReadJob : public QThread {
 public:
  QString database, platform, song;
  QVariantMap result;
  void run() override { result=ReadNativeLyrics(database,platform,song); }
};
CoreBridge::CoreBridge(QObject* parent, bool enabled) : QObject(parent),
    bus_(QDBusConnection::connectToBus(QDBusConnection::SessionBus, "mconnect-native-ui-client")) {
  reconnectTimer_.setInterval(2000);
  connect(&reconnectTimer_, &QTimer::timeout, this, &CoreBridge::reconnect);
  if (enabled) start();
}
void CoreBridge::start() {
  if (started_) return;
  started_ = true;
  bus_.connect(service, path, service, "Changed",
      this, SLOT(changed(QString,QString)));
  ready_ = false;
  emit readyChanged();
  reconnectTimer_.start();
  QTimer::singleShot(0, this, SLOT(reconnect()));
}
void CoreBridge::suspend() {
  started_ = false;
  reconnectTimer_.stop();
  bus_.disconnect(service, path, service, "Changed", this, SLOT(changed(QString,QString)));
}
CoreBridge::~CoreBridge() {
  for (auto* job : lyricJobs_) { job->disconnect(this); job->wait(); delete job; }
}
int CoreBridge::request(const QString& method, const QVariantMap& args) {
  const int id = ++sequence_;
  pendingRequests_.insert(id);
  const auto song=state_.value("song").toMap();
  if (method=="lyrics" && !song.value("id").toString().isEmpty() && song.value("platform")!="local") {
    auto* job=new LyricsReadJob;
    lyricJobs_.insert(job);
    job->database=NativeDataDirectory()+"/mconnect.sqlite";
    job->platform=song.value("platform").toString();job->song=song.value("id").toString();
    const auto track=track_;
    connect(job,&QThread::finished,this,[this,job,id,method,args,track](){
      lyricJobs_.remove(job);
      if(track!=track_){finishRequest(id,method,QVariant(),QStringLiteral("歌曲已切换"));return;}
      if(job->result.isEmpty())sendRequest(id,method,args);
      else {qInfo("MCONNECT_NATIVE_LYRICS_CACHE_HIT");finishRequest(id,method,job->result,QString());}
    });
    connect(job,&QThread::finished,job,&QObject::deleteLater);
    job->start();
  } else sendRequest(id,method,args);
  return id;
}
void CoreBridge::sendRequest(int id,const QString& method,const QVariantMap& args) {
  auto message = QDBusMessage::createMethodCall(service, path, service, "Request");
  message << method << QString::fromUtf8(
      QJsonDocument::fromVariant(args).toJson(QJsonDocument::Compact));
  auto* watcher = new QDBusPendingCallWatcher(
      bus_.asyncCall(message, 30000), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this,
      [this, id, method](QDBusPendingCallWatcher* pending) {
    QDBusPendingReply<QString> reply = *pending;
    if (reply.isError()) {
      QString error = QStringLiteral("请求失败，请检查网络或稍后重试");
      if (reply.error().name() == "org.freedesktop.DBus.Error.Failed") error = reply.error().message();
      finishRequest(id,method,QVariant(),error);
    } else {
      decodeResult(reply.value(),[this,id,method](QVariant value,QString error){finishRequest(id,method,value,error);});
    }
    pending->deleteLater();
  });
}
void CoreBridge::finishRequest(int id,const QString& method,const QVariant& value,const QString& error) {
  if(error.isEmpty() && (method=="artwork" || method=="resultArtwork") && value.toMap().contains("source")) {
    auto mapped=value.toMap();mapped["url"]=artworkCache_.MediaUrl(mapped.take("source").toString());
    // Preserve the established public result shape for the QML image loader.
    finishRequest(id,method,mapped,error);return;
  }
  if(error.isEmpty() && (method=="artistInfo" || method=="albumInfo") && value.toMap().contains("artworkSource")) {
    auto mapped=value.toMap();mapped["artwork"]=artworkCache_.MediaUrl(mapped.take("artworkSource").toString());
    finishRequest(id,method,mapped,error);return;
  }
  pendingRequests_.remove(id);
  if (method=="snapshot") {
    connecting_=false;
    if (!error.isEmpty()) { if(ready_){ready_=false;emit readyChanged();} }
    else {
      const bool initial=!ready_;ready_=true;applyState(value.toMap());
      if(initial){emit readyChanged();if(started_){request("lyrics");request("queue");request("accounts");}}
    }
  } else if(error.isEmpty()) {
    if(method=="lyrics"){lyrics_=value.toMap();emit lyricsChanged();}
    else if(method=="queue"){queue_=value.toList();emit queueChanged();}
    else if(method=="accounts"){accounts_=value.toList();emit accountsChanged();}
    else if(method=="loginStatus"){login_=value.toMap();login_.remove("image");emit loginChanged();}
  }
  emit completed(id,value,error);
}
void CoreBridge::decodeResult(const QString& json,std::function<void(QVariant,QString)> done) {
  const auto value=decode(json);
  const auto map=value.toMap();
  if(!map.contains("_mconnectTransfer")){done(value,QString());return;}
  auto transfer=std::make_shared<Transfer>();
  transfer->token=map.value("_mconnectTransfer").toString();
  transfer->parts=map.value("parts").toInt();
  transfer->done=std::move(done);transfer->elapsed.start();
  if(transfer->token.isEmpty() || transfer->token.size()>128 || transfer->parts<1 || transfer->parts>342){transfer->done(QVariant(),QStringLiteral("数据格式错误，请重试"));return;}
  readPart(transfer);
}
void CoreBridge::readPart(std::shared_ptr<Transfer> transfer) {
  if(transfer->elapsed.elapsed()>30000){transfer->done(QVariant(),QStringLiteral("数据接收超时，请重试"));return;}
  auto message=QDBusMessage::createMethodCall(service,path,service,"Request");
  message<<QString("_readTransfer")<<QString::fromUtf8(QJsonDocument::fromVariant(
      QVariantMap{{"token",transfer->token},{"index",transfer->index}}).toJson(QJsonDocument::Compact));
  auto* watcher=new QDBusPendingCallWatcher(bus_.asyncCall(message,3000),this);
  connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,transfer](QDBusPendingCallWatcher* pending){
    QDBusPendingReply<QString> reply=*pending;
    pending->deleteLater();
    if(reply.isError()){transfer->done(QVariant(),QStringLiteral("数据已过期或接收失败，请刷新"));return;}
    const auto encoded=decode(reply.value()).toMap().value("data").toString().toLatin1();
    const auto bytes=QByteArray::fromBase64(encoded);
    if(encoded.size()>32768 || bytes.isEmpty() || bytes.size()>24576 || transfer->bytes.size()+bytes.size()>8*1024*1024){transfer->done(QVariant(),QStringLiteral("数据格式错误，请重试"));return;}
    transfer->bytes+=bytes;
    if(++transfer->index==transfer->parts) transfer->done(decode(QString::fromUtf8(transfer->bytes)),QString());
    else readPart(transfer);
  });
}
bool CoreBridge::copyText(const QString& text) {
  auto* clipboard=QGuiApplication::clipboard();
  if (!clipboard || text.isEmpty() || text.size()>250000) return false;
  clipboard->setText(text); return true;
}
QString CoreBridge::clipboardText() {
  auto* clipboard=QGuiApplication::clipboard();
  if (!clipboard) return {};
  const auto text=clipboard->text();
  return text.size()<=250000 ? text : QString();
}
void CoreBridge::reconnect() {
  if (!started_ || connecting_) return;
  connecting_ = true;
  request("snapshot");
}
void CoreBridge::applyState(const QVariantMap& value) {
  state_ = value;
  const auto song = state_.value("song").toMap();
  const QString track = song.value("platform").toString() + ":" + song.value("id").toString();
  if (track != track_) {
    track_ = track;
    artwork_.clear(); emit artworkChanged();
    lyrics_.clear(); emit lyricsChanged();
    const int artId = request("artwork");
    auto* connection = new QMetaObject::Connection;
    *connection = connect(this, &CoreBridge::completed, this,
        [this, track, artId, connection](int id, const QVariant& result, const QString& error) {
      if (id != artId) return;
      if (track == track_ && error.isEmpty()) {
        artwork_ = result.toMap().value("url").toString(); emit artworkChanged();
        loadCoverInk(artwork_, track);
      }
      disconnect(*connection); delete connection;
    });
    request("lyrics");
  }
  emit stateChanged();
}
void CoreBridge::loadCoverInk(const QString& url, const QString& track) {
  coverInk_ = "#ffffff";
  if (url.isEmpty()) return;
  auto* reply = network_.get(QNetworkRequest(QUrl(url)));
  reply->setReadBufferSize(16 * 1024 * 1024);
  QTimer::singleShot(8000, reply, &QNetworkReply::abort);
  connect(reply, &QNetworkReply::finished, this, [this, reply, track]() {
    if (track == track_ && reply->error() == QNetworkReply::NoError && reply->bytesAvailable() <= 16 * 1024 * 1024) {
      QImage image = QImage::fromData(reply->readAll());
      if (!image.isNull()) {
        image = image.scaled(200, 300, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        image = image.copy((image.width() - 200) / 2, (image.height() - 300) / 2, 200, 300);
        coverInk_ = CoverTitleInk(image, QRect(10, 170, 180, 80)).name();
        emit artworkChanged();
      }
    }
    reply->deleteLater();
  });
}
void CoreBridge::changed(const QString& topic, const QString& json) {
  if(decode(json).toMap().value("_mconnectRefresh").toBool()) {
    if(topic=="state")request("snapshot");
    else if(topic=="queue" || topic=="lyrics" || topic=="accounts")request(topic);
    else if(topic=="login")request("loginStatus");
    return;
  }
  if (topic == "state") applyState(decode(json).toMap());
  else if (topic == "lyrics") { lyrics_ = decode(json).toMap(); emit lyricsChanged(); }
  else if (topic == "queue") { queue_ = decode(json).toList(); emit queueChanged(); }
  else if (topic == "accounts") { accounts_ = decode(json).toList(); emit accountsChanged(); }
  else if (topic == "login") { login_ = decode(json).toMap(); emit loginChanged(); }
}
