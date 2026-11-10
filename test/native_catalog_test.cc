// Isolated D-Bus + local HTTP fixture. Does not play music or modify accounts.
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
#include <QUrlQuery>
#include <cstdio>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"native catalog failed at %d\n",__LINE__);return 1;}}while(false)
QVariantMap call(QDBusConnection& bus,const QString& op,const QString& id="0",int offset=0,int limit=100) {
  auto message=QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Silica","/org/sailfishos/Mconnect/Platforms","org.sailfishos.Mconnect.Platforms","NeteaseCatalog");
  message<<op<<id<<offset<<limit<<QString("os=pc; appver=3.0.18.203152; __csrf=test");
  QDBusPendingCallWatcher watcher(bus.asyncCall(message,32000));QEventLoop loop;
  QObject::connect(&watcher,&QDBusPendingCallWatcher::finished,&loop,&QEventLoop::quit);loop.exec();
  QDBusPendingReply<QString> reply=watcher;
  return reply.isError()?QVariantMap{{"error","dbus_error"}}:QJsonDocument::fromJson(reply.value().toUtf8()).toVariant().toMap();
}
QVariantMap song(const QString& id) {return {{"id",id},{"name",QString::fromUtf8("中文歌曲")},{"ar",QVariantList{QVariantMap{{"id",6452},{"name",QString::fromUtf8("歌手")}}}},{"al",QVariantMap{{"id",18905},{"name","album"}}},{"dt",123456},{"fee",8},{"no",3}};}
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);auto bus=QDBusConnection::sessionBus();auto client=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"catalog-client");
  CHECK(bus.registerService("org.sailfishos.Mconnect.Silica"));
  if(argc>1 && QByteArray(argv[1])=="--live") {
    NativePlatforms service;CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));
    const auto charts=call(client,"toplists");CHECK(!charts.contains("error") && !charts.value("playlists").toList().isEmpty());
    const auto list=call(client,"playlist","3778678");CHECK(!list.contains("error") && list.value("songs").toList().size()>=100);
    const auto ranked=call(client,"ranked","3778678",30,5);CHECK(!ranked.contains("error") && ranked.value("songs").toList().size()==5 && ranked.value("ranks").toList().first().toInt()==31);
    const auto info=call(client,"playlistInfo","3778678");CHECK(!info.contains("error") && info.value("playlists").toList().size()==1);
    for(const auto& area:QStringList{"0","7","96","8","16"}) {const auto value=call(client,"newSongs",area,0,7);CHECK(!value.contains("error") && value.value("songs").toList().size()==7);}
    const auto lyrics=call(client,"lyrics","186016");CHECK(!lyrics.contains("error") && !lyrics.value("lyrics").toMap().isEmpty());
    const auto stream=call(client,"stream","186016",0);CHECK(!stream.value("url").toString().isEmpty() || stream.value("error")=="vip_required" || stream.value("error")=="unavailable");
    const auto qualities=call(client,"qualities","186016");CHECK(!qualities.contains("error"));
    printf("NATIVE_CATALOG_LIVE_PASS charts=%d playlist=%d regions=5 lyrics stream qualities\n",charts.value("playlists").toList().size(),list.value("songs").toList().size());return 0;
  }
  QTcpServer fixture;CHECK(fixture.listen(QHostAddress::LocalHost));QMap<QString,int> counts;bool failNew=false,bad=false,expired=false,trial=false,fee=false;
  bool requestValid=true;int batches=0;QString playlistId;
  QObject::connect(&fixture,&QTcpServer::newConnection,&app,[&](){while(fixture.hasPendingConnections()) {
    auto* socket=fixture.nextPendingConnection();auto buffer=std::make_shared<QByteArray>();auto started=std::make_shared<bool>(false);
    QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    QObject::connect(socket,&QTcpSocket::readyRead,&app,[&,socket,buffer,started](){
      if(*started)return;*buffer+=socket->readAll();const int headerEnd=buffer->indexOf("\r\n\r\n");if(headerEnd<0)return;
      int length=0;for(const auto& line:buffer->left(headerEnd).split('\n'))if(line.toLower().startsWith("content-length:"))length=line.mid(15).trimmed().toInt();
      if(buffer->size()<headerEnd+4+length)return;*started=true;
      const auto parts=buffer->left(buffer->indexOf("\r\n")).split(' ');const QUrl url(QString("http://localhost")+QString::fromLatin1(parts[1].constData(),parts[1].size()));
      const auto path=url.path();++counts[path];
      const QUrlQuery form(QString::fromUtf8(buffer->mid(headerEnd+4))),query(url);
      QVariantMap result{{"code",200}};
      if(path=="/api/v6/playlist/detail") {
        playlistId=form.queryItemValue("id");QVariantList ids;
        const int count=playlistId=="2"?405:5;
        for(int i=1;i<=count;++i)ids.append(QVariantMap{{"id",i}});
        result["playlist"]=QVariantMap{{"id",playlistId},{"name",QString::fromUtf8("中文歌单")},{"trackCount",count},{"tracks",QVariantList{song("3"),song("1")}},{"trackIds",ids}};
      } else if(path=="/api/v3/song/detail") {
        ++batches;const auto ids=QJsonDocument::fromJson(form.queryItemValue("ids",QUrl::FullyDecoded).toUtf8()).toVariant().toList();
        requestValid=requestValid && ids.size()<=200 && form.queryItemValue("csrf")=="test" && parts[0]=="POST";
        QVariantList rows;for(int i=ids.size()-1;i>=0;--i)if(ids[i].toString()!="4")rows.append(song(ids[i].toString()));result["songs"]=rows;
      } else if(path=="/api/v1/discovery/new/songs") {
        if(failNew)result["code"]=500;else result["data"]=QVariantList{song("1"),song("2"),song("3")};
      } else if(path=="/api/personalized/newsong")result["result"]=QVariantList{QVariantMap{{"type",4},{"song",song("5")}},QVariantMap{{"type",1},{"song",song("6")}}};
      else if(path=="/api/song/lyric") {
        requestValid=requestValid && query.queryItemValue("rv")=="-1" && query.queryItemValue("yrv")=="-1";
        for(const auto& key:QStringList{"lrc","tlyric","yrc","ytlrc","yromalrc"})result[key]=QVariantMap{{"lyric",QString::fromUtf8("[00:01.00]中文歌词")}};
      } else if(path=="/api/song/enhance/player/url/v1") {
        requestValid=requestValid && form.queryItemValue("csrf")=="test" && parts[0]=="POST";
        QVariantMap row{{"url",fee?QVariant():QVariant("https://audio.test/music")},{"level","jymaster"},{"br",999000},{"fl",1000},{"fee",fee?1:0}};
        if(trial)row["freeTrialInfo"]=QVariantMap{{"start",0}};result["data"]=QVariantList{row};
      } else if(path=="/api/v3/discovery/recommend/songs")result["data"]=QVariantMap{{"dailySongs",QVariantList{song("1")}}};
      else if(path=="/api/toplist" || path=="/api/user/playlist") {
        result[path=="/api/toplist"?"list":"playlist"]=QVariantList{QVariantMap{{"id",1234567890123LL},{"name",QString::fromUtf8("榜单")},{"trackCount",200},{"updateFrequency",QString::fromUtf8("每日更新")},{"coverImgUrl","//image.test/a"},{"creator",QVariantMap{{"nickname","creator"}}}}};
      }
      if(expired)result["code"]=301;
      const auto body=bad?QByteArray("<html>"):QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact);
      socket->write("HTTP/1.1 200 OK\r\nSet-Cookie: MUSIC_A=native; Path=/\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();
    });
  }});
  NativePlatforms service(nullptr,QUrl(QString("http://127.0.0.1:%1").arg(fixture.serverPort())));
  CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));
  const auto playlist=call(client,"playlist","1");CHECK(!playlist.contains("error"));
  const auto rows=playlist.value("songs").toList();CHECK(rows.size()==4 && rows[0].toMap().value("id")=="1" && rows[1].toMap().value("id")=="2" && rows[3].toMap().value("id")=="5");
  CHECK(playlist.value("cookie").toString().contains("MUSIC_A=native"));
  const auto ranked=call(client,"ranked","1",2,3);CHECK(ranked.value("ranks").toList()==QVariantList({3,5}));
  batches=0;const auto big=call(client,"playlist","2");CHECK(big.value("songs").toList().size()==404 && batches==3);
  CHECK(call(client,"playlistInfo","1").value("playlists").toList().size()==1);
  CHECK(call(client,"newSongs","7",0,2).value("songs").toList().size()==2);
  failNew=true;CHECK(call(client,"newSongs","7").value("error")=="provider_error" && counts["/api/personalized/newsong"]==0);
  CHECK(call(client,"newSongs","0").value("songs").toList().first().toMap().value("id")=="5");
  expired=true;CHECK(call(client,"newSongs","0").value("error")=="login_required" && counts["/api/personalized/newsong"]==1);expired=false;
  CHECK(call(client,"lyrics","1").value("lyrics").toMap().size()==5);
  CHECK(call(client,"stream","1",4).value("url")=="https://audio.test/music");trial=true;CHECK(call(client,"stream","1").value("error")=="vip_required");trial=false;fee=true;CHECK(call(client,"stream","1").value("error")=="vip_required");fee=false;
  CHECK(call(client,"qualities","1").value("qualities").toList().size()==5);
  CHECK(call(client,"daily").value("songs").toList().size()==1);
  CHECK(call(client,"toplists").value("playlists").toList().first().toMap().value("updateFrequency")==QString::fromUtf8("每日更新"));
  CHECK(call(client,"userPlaylists","123").value("playlists").toList().first().toMap().value("id")=="1234567890123");
  bad=true;CHECK(call(client,"toplists").value("error")=="invalid_response");bad=false;
  CHECK(call(client,"newSongs","14").value("error")=="invalid_arguments");
  CHECK(call(client,"stream","1",8).value("error")=="invalid_arguments");
  CHECK(requestValid);puts("NATIVE_CATALOG_FIXTURE_PASS ordered hydration 3 batches rank gaps regions lyrics streams accounts errors");return 0;
}
