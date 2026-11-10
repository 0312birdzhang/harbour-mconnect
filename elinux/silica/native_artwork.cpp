#include "native_artwork.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QPointer>
#include <QSaveFile>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <memory>
#include <utime.h>

namespace {
constexpr int maxBytes=16*1024*1024;
QMutex diskMutex;
bool imageHeader(const QByteArray& b) {
  return b.size()>=12 && (b.startsWith(QByteArray::fromHex("ffd8ff")) || b.startsWith("\x89PNG") ||
      b.startsWith("GIF") || (b.startsWith("RIFF") && b.mid(8,4)=="WEBP") || b.startsWith("BM") || b.mid(4,4)=="ftyp");
}
QString key(const QString& source) {const auto hex=QCryptographicHash::hash(source.toUtf8(),QCryptographicHash::Sha256).toHex();return QString::fromLatin1(hex.constData(),hex.size());}
QByteArray readFile(const QString& path,bool envelope) {
  QFile file(path);
  if(!file.open(QIODevice::ReadOnly) || file.size()>maxBytes+(envelope?36:0))return {};
  const auto bytes=file.readAll();
  if(envelope)return DecodeNativeArtwork(bytes);
  return imageHeader(bytes)?bytes:QByteArray();
}
void store(const QString& directory,const QString& source,const QByteArray& bytes) {
  if(!QDir().mkpath(directory))return;
  QFile::setPermissions(directory,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
  QFile marker(directory+"/.nomedia");marker.open(QIODevice::WriteOnly);marker.close();
  QSaveFile file(directory+"/"+key(source)+".mca");
  if(!file.open(QIODevice::WriteOnly))return;
  file.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
  const auto envelope=EncodeNativeArtwork(bytes);
  if(file.write(envelope)!=envelope.size() || !file.commit())return;
  auto entries=QDir(directory).entryInfoList({"*.mca"},QDir::Files|QDir::NoSymLinks,QDir::Time|QDir::Reversed);
  qint64 total=0;for(const auto& entry:entries)total+=entry.size();
  for(const auto& entry:entries){if(total<=200*1024*1024)break;if(QFile::remove(entry.absoluteFilePath()))total-=entry.size();}
}
class DiskJob : public QThread {
 public:
  QString directory, source;
  QByteArray bytes;
  bool write=false;
  void run() override {
    QMutexLocker lock(&diskMutex);
    if(write){store(directory,source,bytes);return;}
    const QUrl url(source);
    if(url.isLocalFile() || source.startsWith('/')) {
      const auto path=url.isLocalFile()?url.toLocalFile():source;
      bytes=readFile(path,path.endsWith(".mca"));return;
    }
    bytes=readFile(directory+"/"+key(source)+".mca",true);
    if(!bytes.isEmpty()) {
      // Qt 5.6 lacks QFile::setFileTime; utime touches access order without
      // rewriting a large envelope. Cache reads and writes share the disk lock.
      const auto path=QFile::encodeName(directory+"/"+key(source)+".mca");
      ::utime(path.constData(),nullptr);
      return;
    }
    // Migrate pre-MCA cached_network_image entries without redownloading or
    // deleting the original file/index still referenced by older installations.
    const auto data=qgetenv("XDG_DATA_HOME");
    QFile index((data.isEmpty()?QDir::homePath()+"/.local/share":QString::fromLocal8Bit(data.constData(),data.size()))+"/harbour-mconnect/libCachedImageData.json");
    if(!index.open(QIODevice::ReadOnly) || index.size()>8*1024*1024)return;
    QString candidate;
    const QRegularExpression safeName("^[A-Za-z0-9_.-]+$");
    for(const auto& value:QJsonDocument::fromJson(index.readAll()).toVariant().toList()) {
      const auto row=value.toMap();const auto name=row.value("relativePath").toString();
      if(row.value("url").toString()!=source || !safeName.match(name).hasMatch() || name=="." || name=="..")continue;
      if(candidate.isEmpty() || row.value("key").toString()==source)candidate=name;
    }
    if(candidate.isEmpty())return;
    bytes=readFile(QFileInfo(directory).dir().absolutePath()+"/libCachedImageData/"+candidate,false);
    if(!bytes.isEmpty())store(directory,source,bytes);
  }
};
}

QByteArray EncodeNativeArtwork(const QByteArray& bytes) {
  if(bytes.size()>maxBytes || !imageHeader(bytes))return {};
  QByteArray result="MCA1";result+=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
  for(char value:bytes)result.append(char(static_cast<unsigned char>(value)^0xa7));
  return result;
}
QByteArray DecodeNativeArtwork(const QByteArray& bytes) {
  if(bytes.size()<48 || bytes.size()>maxBytes+36 || !bytes.startsWith("MCA1"))return {};
  QByteArray decoded=bytes.mid(36);for(char& value:decoded)value=char(static_cast<unsigned char>(value)^0xa7);
  if(QCryptographicHash::hash(decoded,QCryptographicHash::Sha256)!=bytes.mid(4,32) || !imageHeader(decoded))return {};
  return decoded;
}
NativeArtwork::NativeArtwork(QObject* parent,const QString& directory):QObject(parent),directory_(directory) {
  if(directory_.isEmpty()) {
    const auto cache=qgetenv("XDG_CACHE_HOME");
    directory_=(cache.isEmpty()?QDir::homePath()+"/.cache":QString::fromLocal8Bit(cache.constData(),cache.size()))+"/harbour-mconnect/.artwork";
  }
  token_=QUuid::createUuid().toString().remove('{').remove('}').remove('-');
  connect(&server_,&QTcpServer::newConnection,this,&NativeArtwork::accept);
}
NativeArtwork::~NativeArtwork() {
  server_.close();
  for(auto* reply:network_.findChildren<QNetworkReply*>()){reply->disconnect(this);reply->abort();}
  for(auto* socket:server_.findChildren<QTcpSocket*>())socket->disconnect(this);
  for(auto* job:jobs_){job->wait();delete job;}
}
void NativeArtwork::startJob(QThread* job) {
  jobs_.insert(job);
  connect(job,&QThread::finished,this,[this,job](){jobs_.remove(job);job->deleteLater();});
  job->start();
}
QString NativeArtwork::MediaUrl(const QString& input) {
  const auto source=input.startsWith("//")?"https:"+input:input;
  const QUrl url(source);
  if(source.isEmpty() || source.size()>16384 || !(source.startsWith('/') || url.isLocalFile() || url.scheme()=="http" || url.scheme()=="https"))return {};
  if(!server_.isListening() && !server_.listen(QHostAddress::LocalHost,0))return {};
  const auto hash=key(source);
  if(!routes_.contains(hash))routeOrder_.append(hash);
  routes_[hash]=source;
  while(routeOrder_.size()>1000)routes_.remove(routeOrder_.takeFirst());
  return QString("http://127.0.0.1:%1/%2/%3").arg(server_.serverPort()).arg(token_,hash);
}
void NativeArtwork::accept() {
  while(server_.hasPendingConnections()) {
    auto* socket=server_.nextPendingConnection();
    auto buffer=std::make_shared<QByteArray>();
    auto started=std::make_shared<bool>(false);
    connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    QTimer::singleShot(30000,socket,[socket](){socket->abort();});
    connect(socket,&QTcpSocket::readyRead,this,[this,socket,buffer,started](){
      if(*started)return;
      *buffer+=socket->readAll();
      if(buffer->size()>8192){socket->disconnectFromHost();return;}
      if(!buffer->contains("\r\n\r\n"))return;
      *started=true;
      const auto request=buffer->left(buffer->indexOf("\r\n")).split(' ');
      const auto parts=request.size()==3?request[1].split('/'):QList<QByteArray>();
      const bool head=request.size()==3 && request[0]=="HEAD";
      const bool allowed=request.size()==3 && (head || request[0]=="GET") && parts.size()==3 && parts[1]==token_.toLatin1() && routes_.contains(QString::fromLatin1(parts[2].constData(),parts[2].size()));
      const QPointer<QTcpSocket> guard(socket);
      const auto respond=[guard,head](QByteArray bytes){
        if(!guard || guard->state()!=QAbstractSocket::ConnectedState)return;
        const bool ok=!bytes.isEmpty();
        guard->write(ok?"HTTP/1.1 200 OK\r\n":"HTTP/1.1 404 Not Found\r\n");
        guard->write("Connection: close\r\nContent-Type: application/octet-stream\r\nContent-Length: "+QByteArray::number(bytes.size())+"\r\n\r\n");
        if(!head)guard->write(bytes);
        guard->disconnectFromHost();
      };
      if(!allowed){respond({});return;}
      load(routes_.value(QString::fromLatin1(parts[2].constData(),parts[2].size())),respond);
    });
  }
}
void NativeArtwork::load(const QString& source,Done done) {
  if(auto* cached=memory_.object(source)){done(*cached);return;}
  if(pending_.contains(source)){pending_[source].append(std::move(done));return;}
  pending_[source].append(std::move(done));
  auto* job=new DiskJob;job->source=source;job->directory=directory_;
  connect(job,&QThread::finished,this,[this,job,source](){
    if(!job->bytes.isEmpty()){qInfo("MCONNECT_NATIVE_ARTWORK_DISK_HIT");finish(source,job->bytes);return;}
    const QUrl url(source);
    if(url.scheme()!="http" && url.scheme()!="https"){finish(source,{});return;}
    download(source,url,0,[this,source](QByteArray bytes){
      if(!bytes.isEmpty()) {
        auto* writer=new DiskJob;writer->source=source;writer->directory=directory_;writer->bytes=bytes;writer->write=true;
        startJob(writer);
      }
      finish(source,bytes);
    });
  });
  startJob(job);
}
void NativeArtwork::finish(const QString& source,const QByteArray& bytes) {
  if(!bytes.isEmpty()) {
    memory_.insert(source,new QByteArray(bytes),std::max(1,(bytes.size()+1023)/1024));
    while(memory_.size()>20)memory_.remove(memory_.keys().first());
  }
  const auto callbacks=pending_.take(source);for(const auto& done:callbacks)done(bytes);
}
void NativeArtwork::download(const QString& source,const QUrl& url,int redirects,Done done,std::shared_ptr<QElapsedTimer> deadline) {
  if(!deadline){deadline=std::make_shared<QElapsedTimer>();deadline->start();}
  const int remaining=25000-int(deadline->elapsed());
  if(remaining<=0){done({});return;}
  QNetworkRequest request(url);request.setRawHeader("User-Agent","Mozilla/5.0");
  const auto host=url.host().toLower();
  if(host=="hdslb.com" || host.endsWith(".hdslb.com") || host=="bilibili.com" || host.endsWith(".bilibili.com"))request.setRawHeader("Referer","https://www.bilibili.com/");
  auto* reply=network_.get(request);
  auto bytes=std::make_shared<QByteArray>();
  QTimer::singleShot(remaining,reply,&QNetworkReply::abort);
  connect(reply,&QNetworkReply::readyRead,this,[reply,bytes](){
    if(reply->bytesAvailable()>maxBytes-bytes->size()){reply->abort();return;}
    bytes->append(reply->readAll());
  });
  connect(reply,&QNetworkReply::finished,this,[this,source,url,redirects,reply,bytes,done,deadline](){
    reply->deleteLater();
    const auto target=reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if(!target.isEmpty() && redirects<5) {
      const auto next=url.resolved(target);
      if((next.scheme()=="https" || next.scheme()=="http") && !(url.scheme()=="https" && next.scheme()=="http")){download(source,next,redirects+1,done,deadline);return;}
    }
    if(reply->bytesAvailable()>maxBytes-bytes->size()){done({});return;}
    bytes->append(reply->readAll());
    if(reply->error()!=QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200 || bytes->size()>maxBytes || !imageHeader(*bytes)){done({});return;}
    qInfo("MCONNECT_NATIVE_ARTWORK_NETWORK_FETCH");done(*bytes);
  });
}
