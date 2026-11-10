// Run under dbus-run-session, never on the user's music session bus.
#include "../elinux/silica/native_platforms.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QEventLoop>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QDateTime>
#include <cstdio>

#define CHECK(x) do {if(!(x)){fprintf(stderr,"native content failed at %d\n",__LINE__);return 1;}}while(false)
QVariantMap call(QDBusConnection& bus,const QString& operation,const QString& id,int page=1,int limit=30) {
  auto message=QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Silica","/org/sailfishos/Mconnect/Platforms","org.sailfishos.Mconnect.Platforms","NeteaseContent");
  message<<operation<<id<<page<<limit<<QString("os=pc; appver=3.0.18.203152");
  QDBusPendingCallWatcher watcher(bus.asyncCall(message,12000));QEventLoop loop;
  QObject::connect(&watcher,&QDBusPendingCallWatcher::finished,&loop,&QEventLoop::quit);loop.exec();
  const QDBusPendingReply<QString> reply=watcher;
  return reply.isError()?QVariantMap{{"error","dbus_error"}}:QJsonDocument::fromJson(reply.value().toUtf8()).toVariant().toMap();
}
QVariantMap artist(const QString& id) {return {{"id",id},{"name",QString::fromUtf8("歌手")},{"briefDesc",QString::fromUtf8("中文简介")},{"cover","//image.test/artist"},{"musicSize",100},{"albumSize",10}};}
QVariantMap song(const QString& id,int no) {return {{"id",id},{"name",QString::fromUtf8("歌曲")},{"artists",QVariantList{QVariantMap{{"id","123456789"},{"name","artist"}}}},{"album",QVariantMap{{"id","1234567890123"},{"name","album"}}},{"duration",123456},{"no",no},{"fee",8}};}
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  auto serverBus=QDBusConnection::sessionBus();auto client=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"content-client");
  CHECK(serverBus.registerService("org.sailfishos.Mconnect.Silica"));
  if(argc>1 && QByteArray(argv[1])=="--live") {
    NativePlatforms service;CHECK(serverBus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));
    const auto info=call(client,"artist","6452");
    CHECK(!info.contains("error") && info.value("artist").toMap().value("id")=="6452");
    const auto songs=call(client,"artistSongs","6452",1,100);
    CHECK(!songs.contains("error") && !songs.value("songs").toList().isEmpty());
    const auto first=call(client,"artistAlbums","6452",1,5),second=call(client,"artistAlbums","6452",2,5);
    CHECK(!first.contains("error") && !second.contains("error") && !first.value("albums").toList().isEmpty() && !second.value("albums").toList().isEmpty());
    const auto albumId=first.value("albums").toList().first().toMap().value("id").toString();
    CHECK(albumId!=second.value("albums").toList().first().toMap().value("id").toString());
    const auto album=call(client,"album",albumId),tracks=call(client,"albumSongs",albumId);
    CHECK(!album.contains("error") && album.value("album").toMap().value("id")==albumId);
    CHECK(!tracks.contains("error") && !tracks.value("songs").toList().isEmpty());
    printf("NATIVE_CONTENT_LIVE_PASS artist songs=%d album-pages=%d,%d tracks=%d\n",songs.value("songs").toList().size(),first.value("albums").toList().size(),second.value("albums").toList().size(),tracks.value("songs").toList().size());return 0;
  }
  const auto parsed=ParseNeteaseContent("albumSongs",QJsonDocument::fromVariant(QVariantMap{{"code",200},{"album",QVariantMap{{"songs",QVariantList()}}},{"songs",QVariantList{song("2",2),song("1",1)}}}).toJson());
  CHECK(parsed.value("songs").toList().size()==2 && parsed.value("songs").toList().first().toMap().value("id")=="1");
  auto unnumbered=song("1",1);unnumbered.remove("no");
  const auto ordered=ParseNeteaseContent("albumSongs",QJsonDocument::fromVariant(QVariantMap{{"code",200},{"songs",QVariantList{song("2",2),unnumbered}}}).toJson());
  CHECK(ordered.value("songs").toList().first().toMap().value("id")=="2");
  CHECK(ParseNeteaseContent("album","{\"code\":200,\"album\":null}").value("album").isNull());
  CHECK(ParseNeteaseContent("artist","{\"code\":301}").value("error")=="login_required");
  CHECK(ParseNeteaseContent("artistAlbums","{\"code\":200,\"hotAlbums\":{}}").value("error")=="invalid_response");
  const auto publish=QDateTime(QDate(2011,11,11),QTime(0,0)).toMSecsSinceEpoch();
  const auto rich=ParseNeteaseContent("album",QJsonDocument::fromVariant(QVariantMap{{"code",200},{"album",QVariantMap{{"id","1234567890123"},{"name","album"},{"artist",QVariantMap{{"id","6452"},{"name","artist"}}},{"publishTime",publish},{"description",QString::fromUtf8("中文专辑简介")},{"company","record company"},{"size",2},{"tags",QVariantList{"pop","", "rock"}}}}}).toJson()).value("album").toMap();
  CHECK(rich.value("artistId")=="6452" && rich.value("genre")=="pop/rock" && rich.value("songCount").toInt()==2);
  CHECK(rich.value("releaseDate").toString()=="2011-11-11T00:00:00.000" && rich.value("description")==QString::fromUtf8("中文专辑简介"));
  QTcpServer fixture;CHECK(fixture.listen(QHostAddress::LocalHost));QMap<QString,int> counts;
  QObject::connect(&fixture,&QTcpServer::newConnection,&app,[&](){
    while(fixture.hasPendingConnections()) {
      auto* socket=fixture.nextPendingConnection();auto buffer=std::make_shared<QByteArray>();auto started=std::make_shared<bool>(false);
      QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
      QObject::connect(socket,&QTcpSocket::readyRead,&app,[&,socket,buffer,started](){
        if(*started)return;*buffer+=socket->readAll();const auto headerEnd=buffer->indexOf("\r\n\r\n");if(headerEnd<0)return;
        int length=0;for(const auto& line:buffer->left(headerEnd).split('\n'))if(line.toLower().startsWith("content-length:"))length=line.mid(15).trimmed().toInt();
        if(buffer->size()<headerEnd+4+length)return;*started=true;
        const auto parts=buffer->left(buffer->indexOf("\r\n")).split(' ');const QUrl url(QString("http://localhost")+QString::fromLatin1(parts[1].constData(),parts[1].size()));
        QString id;QVariantMap result{{"code",200}};bool bad=false;
        if(url.path()=="/api/artist/head/info/get") {
          const QUrlQuery body(QString::fromUtf8(buffer->mid(headerEnd+4)));id=body.queryItemValue("id");
          if(id=="2")result["data"]=QVariantMap{{"artist",artist(id)}};
          else if(id=="3")bad=true;
          else if(id=="5")result["code"]=301;
        } else if(url.path()=="/api/artist/top/song")id=QUrlQuery(url).queryItemValue("id");
        else {
          id=url.path().section('/',-1);
          if(id=="3" || id=="4")bad=true;
          else {result["artist"]=artist(id);result["hotSongs"]=QVariantList{song("legacy",2)};}
        }
        ++counts[url.path()+":"+id];
        const auto body=QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact);
        socket->write(bad?"HTTP/1.1 503 Unavailable\r\n":"HTTP/1.1 200 OK\r\n");
        socket->write("Content-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();
      });
    }
  });
  NativePlatforms service(nullptr,QUrl(QString("http://127.0.0.1:%1").arg(fixture.serverPort())));
  CHECK(serverBus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));
  const auto fallback=call(client,"artist","1");CHECK(fallback.value("artist").toMap().value("briefDesc")==QString::fromUtf8("中文简介"));
  CHECK(counts["/api/artist/head/info/get:1"]==1 && counts["/api/artist/1:1"]==1);
  CHECK(call(client,"artist","2").value("artist").toMap().value("id")=="2" && counts["/api/artist/2:2"]==0);
  CHECK(call(client,"artist","3").value("error")=="network_error");
  CHECK(counts["/api/artist/head/info/get:3"]==1 && counts["/api/artist/3:3"]==2);
  CHECK(call(client,"artist","4").value("artist").isNull());
  CHECK(call(client,"artist","5").value("error")=="login_required" && counts["/api/artist/5:5"]==0);
  CHECK(call(client,"artistSongs","1").value("songs").toList().first().toMap().value("id")=="legacy");
  CHECK(call(client,"album","../escape").value("error")=="invalid_arguments");
  puts("NATIVE_CONTENT_PASS fallback primary error-vs-empty login-expiry no-POST-retry track-order schema IDs");return 0;
}
