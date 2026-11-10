#include "../elinux/silica/native_artwork.h"
#include <QCoreApplication>
#include <QBuffer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QTimer>
#include <cstdio>

#define CHECK(x) do {if(!(x)){fprintf(stderr,"native artwork failed at %d\n",__LINE__);return 1;}}while(false)
QByteArray get(QNetworkAccessManager& manager,const QString& url,int* status=nullptr,const QByteArray& verb="GET") {
  auto* reply=verb=="HEAD"?manager.head(QNetworkRequest(QUrl(url))):manager.get(QNetworkRequest(QUrl(url)));
  QEventLoop loop;QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
  QTimer::singleShot(8000,&loop,&QEventLoop::quit);loop.exec();
  if(status)*status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  auto bytes=reply->readAll();reply->abort();reply->deleteLater();return bytes;
}
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  QTemporaryDir directory;CHECK(directory.isValid());
  QImage image(16,16,QImage::Format_ARGB32);image.fill(Qt::cyan);
  QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);CHECK(image.save(&buffer,"PNG"));
  const auto envelope=EncodeNativeArtwork(png);
  CHECK(envelope.startsWith("MCA1") && envelope.size()==png.size()+36 && DecodeNativeArtwork(envelope)==png);
  auto corrupt=envelope;corrupt[corrupt.size()-1]=char(corrupt.at(corrupt.size()-1)^1);CHECK(DecodeNativeArtwork(corrupt).isEmpty());
  CHECK(EncodeNativeArtwork("<html>error</html>").isEmpty());
  CHECK(DecodeNativeArtwork(QByteArray(16*1024*1024+37,'x')).isEmpty());
  QTcpServer fixture;CHECK(fixture.listen(QHostAddress::LocalHost));
  int requests=0;
  QObject::connect(&fixture,&QTcpServer::newConnection,&app,[&](){
    while(fixture.hasPendingConnections()) {
      auto* socket=fixture.nextPendingConnection();
      QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
      QObject::connect(socket,&QTcpSocket::readyRead,&app,[&,socket](){
        const auto request=socket->readAll();++requests;
        if(request.contains(" /redirect "))socket->write("HTTP/1.1 302 Found\r\nLocation: /image\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        else {
          const auto bytes=request.contains(" /invalid ")?QByteArray("<html>error</html>"):png;
          socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(bytes.size())+"\r\nConnection: close\r\n\r\n"+bytes);
        }
        socket->disconnectFromHost();
      });
    }
  });
  QNetworkAccessManager manager;
  const auto source=QString("http://127.0.0.1:%1/image").arg(fixture.serverPort());
  NativeArtwork cache(nullptr,directory.path());
  const auto url=cache.MediaUrl(source);CHECK(url.startsWith("http://127.0.0.1:"));
  int status=0;CHECK(get(manager,url,&status)==png && status==200 && requests==1);
  CHECK(get(manager,url)==png && requests==1);
  CHECK(get(manager,url,&status,"HEAD").isEmpty() && status==200 && requests==1);
  CHECK(get(manager,url+"bad",&status).isEmpty() && status==404);
  CHECK(cache.MediaUrl("javascript:alert(1)").isEmpty());
  CHECK(cache.MediaUrl("").isEmpty());
  // Fresh service uses the existing private disk envelope without any network.
  { NativeArtwork restarted(nullptr,directory.path());CHECK(get(manager,restarted.MediaUrl(source))==png && requests==1); }
  CHECK(QFile::exists(directory.path()+"/.nomedia"));
  const auto files=QDir(directory.path()).entryList({"*.mca"},QDir::Files);CHECK(files.size()==1);
  const auto file=directory.path()+"/"+files[0];
#ifdef Q_OS_UNIX
  CHECK((QFile::permissions(file)&(QFile::ReadOther|QFile::WriteOther|QFile::ReadGroup|QFile::WriteGroup))==0);
#endif
  CHECK(QDir(directory.path()).entryList({"*.png","*.jpg"},QDir::Files).isEmpty());
  // A corrupt cached entry is replaced by a fresh, valid download.
  {QFile damaged(file);CHECK(damaged.open(QIODevice::WriteOnly));damaged.write("broken");}
  { NativeArtwork restarted(nullptr,directory.path());CHECK(get(manager,restarted.MediaUrl(source))==png && requests==2); }
  const auto invalid=cache.MediaUrl(QString("http://127.0.0.1:%1/invalid").arg(fixture.serverPort()));
  CHECK(get(manager,invalid,&status).isEmpty() && status==404);
  const int before=requests;
  CHECK(get(manager,invalid,&status).isEmpty() && status==404 && requests==before+1);
  CHECK(get(manager,cache.MediaUrl(QString("http://127.0.0.1:%1/redirect").arg(fixture.serverPort())))==png);
  QFile local(directory.path()+"/local.mca");CHECK(local.open(QIODevice::WriteOnly));local.write(envelope);local.close();
  CHECK(get(manager,cache.MediaUrl(QUrl::fromLocalFile(local.fileName()).toString()))==png);
  const auto parallel=cache.MediaUrl(QString("http://127.0.0.1:%1/parallel").arg(fixture.serverPort()));
  const int count=requests;
  auto* a=manager.get(QNetworkRequest(QUrl(parallel)));
  auto* b=manager.get(QNetworkRequest(QUrl(parallel)));
  QEventLoop loop;int finished=0;
  const auto complete=[&](){if(++finished==2)loop.quit();};
  QObject::connect(a,&QNetworkReply::finished,&loop,complete);
  QObject::connect(b,&QNetworkReply::finished,&loop,complete);
  QTimer::singleShot(8000,&loop,&QEventLoop::quit);loop.exec();
  CHECK(finished==2 && a->readAll()==png && b->readAll()==png && requests==count+1);
  a->deleteLater();b->deleteLater();
  const auto oldData=qgetenv("XDG_DATA_HOME");
  const auto data=directory.path()+"/data";
  QDir().mkpath(data+"/harbour-mconnect");qputenv("XDG_DATA_HOME",data.toUtf8());
  QDir().mkpath(directory.path()+"/cache/libCachedImageData");
  {QFile legacy(directory.path()+"/cache/libCachedImageData/old.png");CHECK(legacy.open(QIODevice::WriteOnly));legacy.write(png);}
  {QFile index(data+"/harbour-mconnect/libCachedImageData.json");CHECK(index.open(QIODevice::WriteOnly));index.write("[{\"url\":\"http://127.0.0.1:9/legacy\",\"relativePath\":\"old.png\"}]");}
  {NativeArtwork legacy(nullptr,directory.path()+"/cache/.artwork");CHECK(get(manager,legacy.MediaUrl("http://127.0.0.1:9/legacy"))==png);}
  CHECK(QDir(directory.path()+"/cache/.artwork").entryList({"*.mca"},QDir::Files).size()==1);
  if(oldData.isNull())qunsetenv("XDG_DATA_HOME");else qputenv("XDG_DATA_HOME",oldData);
  puts("NATIVE_ARTWORK_PASS envelope memory disk restart corruption retry redirect auth HEAD private-files local-source coalescing legacy-migration");
  return 0;
}
