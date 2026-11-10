#include "../elinux/silica/native_platforms.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <cstdio>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"native Kugou failed at %d\n",__LINE__);return 1;}}while(false)
QVariantMap call(QDBusConnection& bus,const QString& op,const QVariantMap& args={}){
  auto msg=QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Silica","/org/sailfishos/Mconnect/Platforms","org.sailfishos.Mconnect.Platforms","KugouCatalog");msg<<op<<QString::fromUtf8(QJsonDocument::fromVariant(args).toJson(QJsonDocument::Compact));QDBusPendingCallWatcher watcher(bus.asyncCall(msg,20000));QEventLoop loop;QObject::connect(&watcher,&QDBusPendingCallWatcher::finished,&loop,&QEventLoop::quit);loop.exec();QDBusPendingReply<QString> reply=watcher;return reply.isError()?QVariantMap{{"error","dbus_error"}}:QJsonDocument::fromJson(reply.value().toUtf8()).toVariant().toMap();
}
QVariantMap song(int n){QVariantMap row{{"hash",QString("%1").arg(n,32,16,QChar('0'))},{"filename",QString::fromUtf8("歌手 - 中文歌曲")},{"timelen",234567},{"album_name",QString::fromUtf8("专辑")},{"album_id",1234567890123LL}};row["authors"]=QVariantList{QVariantMap{{"author_id",123},{"author_name",QString::fromUtf8("歌手")}},QVariantMap{{"author_id",456},{"author_name",QString::fromUtf8("合唱")}}};row["trans_param"]=QVariantMap{{"union_cover","https://image.test/{size}/cover"}};return row;}
int main(int argc,char** argv){
  QCoreApplication app(argc,argv);auto bus=QDBusConnection::sessionBus();auto client=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"kugou-client");CHECK(bus.registerService("org.sailfishos.Mconnect.Silica"));
  if(argc>1 && QByteArray(argv[1])=="--live-extra"){
    NativePlatforms service;CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));
    const auto search=call(client,"search",{{"query",QString::fromUtf8("周杰伦")},{"page",1},{"limit",5}});CHECK(!search.value("songs").toList().isEmpty());const auto first=search.value("songs").toList().first().toMap();const auto albumId=first.value("albumId").toString();CHECK(!albumId.isEmpty());
    const auto info=call(client,"album",{{"id",albumId}});if(info.contains("error"))fprintf(stderr,"KUGOU_LIVE_ALBUM_ERROR %s\n",qPrintable(info.value("error").toString()));CHECK(!info.value("album").toMap().isEmpty());const auto tracks=call(client,"albumSongs",{{"id",albumId}});CHECK(!tracks.value("songs").toList().isEmpty());
    const auto artistId=info.value("album").toMap().value("artistId").toString();CHECK(!artistId.isEmpty());const auto artist=call(client,"artist",{{"id",artistId}}),songs=call(client,"artistSongs",{{"id",artistId},{"limit",5}}),albums=call(client,"artistAlbums",{{"id",artistId},{"page",1},{"limit",5}});CHECK(!artist.value("artist").toMap().isEmpty() && !songs.value("songs").toList().isEmpty() && !albums.value("albums").toList().isEmpty());
    const auto recommend=call(client,"recommend");CHECK(recommend.contains("error") || recommend.value("songs").type()==QVariant::List);printf("NATIVE_KUGOU_LIVE_EXTRA_PASS albumTracks=%d artistSongs=%d artistAlbums=%d officialAvailable=%d\n",tracks.value("songs").toList().size(),songs.value("songs").toList().size(),albums.value("albums").toList().size(),!recommend.contains("error"));return 0;
  }
  if(argc>1 && QByteArray(argv[1])=="--live"){
    NativePlatforms service;CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));const auto search=call(client,"search",{{"query",QString::fromUtf8("周杰伦")},{"page",1},{"limit",5}});if(search.contains("error")){fprintf(stderr,"KUGOU_LIVE_SEARCH_ERROR %s\n",qPrintable(search.value("error").toString()));return 2;}CHECK(search.value("songs").toList().size()>0);
    const auto charts=call(client,"toplists");CHECK(!charts.value("charts").toList().isEmpty());const auto ranked=call(client,"ranked",{{"id","8888"},{"offset",3},{"limit",5},{"period",""}});CHECK(ranked.value("songs").toList().size()==5);
    const auto home=call(client,"homepage");CHECK(!home.value("songs").toList().isEmpty());printf("NATIVE_KUGOU_LIVE_PASS search=%d charts=%d ranked=%d homepage=%d\n",search.value("songs").toList().size(),charts.value("charts").toList().size(),ranked.value("songs").toList().size(),home.value("songs").toList().size());return 0;
  }
  QTcpServer fixture;CHECK(fixture.listen(QHostAddress::LocalHost));bool valid=true,waf=false,wrapped=false,doubleEncoded=false,stall=false,reject=false;QMap<QString,int> counts;
  QObject::connect(&fixture,&QTcpServer::newConnection,&app,[&](){while(fixture.hasPendingConnections()){
    auto* socket=fixture.nextPendingConnection();auto bytes=std::make_shared<QByteArray>();auto started=std::make_shared<bool>(false);QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    QObject::connect(socket,&QTcpSocket::readyRead,&app,[&,socket,bytes,started](){
      if(*started)return;*bytes+=socket->readAll();if(!bytes->contains("\r\n\r\n"))return;*started=true;const auto first=bytes->left(bytes->indexOf("\r\n")).split(' ');const QUrl url("http://localhost"+QString::fromLatin1(first[1]));const auto path=url.path();const QUrlQuery query(url);++counts[path];valid=valid && first[0]=="GET" && !bytes->toLower().contains("cookie:") && !bytes->toLower().contains("authorization:");QVariantMap root{{"status",reject?0:1},{"error",""}},data;
      if(path=="/api/v3/search/song" || path=="/api/v3/singer/song" || path=="/api/v3/recommend/song"){
        if(path.contains("search"))valid=valid && query.queryItemValue("keyword",QUrl::FullyDecoded)==QString::fromUtf8("中文 A+B & #");data["info"]=QVariantList{song(1),song(2)};
      }else if(path=="/api/v3/singer/info")data={{"singerid",1234567890123LL},{"singername",QString::fromUtf8("中文歌手")},{"avatar","https://image.test/{size}/artist"},{"profile",QString::fromUtf8("简介")},{"songcount",100},{"albumcount",20},{"fansnums",345}};
      else if(path=="/api/v3/singer/album" || path=="/api/v3/album/info"){
        QVariantMap album{{"albumid",path.contains("singer")?query.queryItemValue("page"):QString("123")},{"albumname",QString::fromUtf8("中文专辑")},{"singerid",456},{"singername",QString::fromUtf8("歌手")},{"imgurl","https://image.test/{size}/album"},{"publishtime","2020-01-02 00:00:00"},{"songcount",201}};if(path.contains("singer"))data["info"]=QVariantList{album};else data=album;
      }else if(path=="/api/v3/album/song" || path=="/api/v3/rank/song"){
        const int page=query.queryItemValue("page").toInt(),size=query.queryItemValue("pagesize").toInt(),start=stall?0:(page-1)*size;QVariantList rows;for(int i=start;i<qMin(start+size,201);++i){auto row=song(i+1);row["sort"]=i+1;row["rank_count"]=i+4;if(i==3)row["isfirst"]=1;rows.append(row);}data={{"info",rows},{"total",201}};
      }else if(path=="/api/v3/rank/list"){
        QVariantMap chart{{"rankid",8888},{"rankname",QString::fromUtf8("TOP500")},{"imgurl","https://image.test/{size}/chart"},{"rank_id_publish_date","2026-11-02"}};chart["extra"]=QVariantMap{{"resp",QVariantMap{{"all_total",500}}}};data["info"]=QVariantList{chart};
      }else if(path=="/"){
        root["data"]=QVariantList{song(1),song(2)};QVariantMap item{{"songs",QVariantList{song(2),song(3)}}};QVariantMap list{{"info",QVariantList{item}}};root["special"]=QVariantMap{{"list",list}};
      }else valid=false;
      if(path!="/")root["data"]=data;auto body=QJsonDocument::fromVariant(root).toJson(QJsonDocument::Compact);if(wrapped)body="<!--KG_TAG_RES_START-->"+body+"<!--KG_TAG_RES_END-->";
      if(doubleEncoded){const auto encoded=QJsonDocument(QJsonArray{QString::fromUtf8(body)}).toJson(QJsonDocument::Compact);body=encoded.mid(1,encoded.size()-2);}if(waf)body="Access Deny ! No Actions !";
      socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();
    });
  }});
  const QUrl base(QString("http://127.0.0.1:%1").arg(fixture.serverPort()));NativePlatforms service(nullptr,QUrl("https://music.163.com"),QUrl("https://api.bilibili.com"),QUrl("https://passport.bilibili.com"),QUrl("https://u.y.qq.com"),QUrl("https://c.y.qq.com"),base);CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));
  const QVariantMap args{{"query",QString::fromUtf8("中文 A+B & #")},{"page",2},{"limit",5}};wrapped=true;auto result=call(client,"search",args);wrapped=false;CHECK(result.value("songs").toList().size()==2);const auto row=result.value("songs").toList().first().toMap();CHECK(row.value("name")==QString::fromUtf8("中文歌曲") && row.value("platform")=="kugou" && row.value("durationMs").toInt()==234567 && row.value("artists").toList().size()==2 && row.value("coverUrl")=="https://image.test/480/cover" && row.value("albumId")=="1234567890123");
  doubleEncoded=true;CHECK(call(client,"search",args).value("songs").toList().size()==2);doubleEncoded=false;
  CHECK(call(client,"artist",{{"id","123"}}).value("artist").toMap().value("fansCount").toInt()==345);CHECK(call(client,"artistSongs",{{"id","123"},{"limit",1}}).value("songs").toList().size()==1);
  CHECK(call(client,"artistAlbums",{{"id","123"},{"page",2},{"limit",30}}).value("albums").toList().first().toMap().value("id")=="2");CHECK(call(client,"album",{{"id","123"}}).value("album").toMap().value("releaseDate")=="2020-01-02T00:00:00");
  CHECK(call(client,"albumSongs",{{"id","123"}}).value("songs").toList().size()==201 && counts["/api/v3/album/song"]==3);
  result=call(client,"ranked",{{"id","8888"},{"offset",3},{"limit",5},{"period","2026-11-01 08:30:00"}});CHECK(result.value("songs").toList().size()==5 && result.value("positions").toList().first().toMap().value("rank").toInt()==4 && result.value("positions").toList().first().toMap().value("rankChange").toInt()==3 && result.value("positions").toList().last().toMap().value("rank").toInt()==8);
  CHECK(call(client,"toplists").value("charts").toList().first().toMap().value("songCount").toInt()==500);CHECK(call(client,"homepage").value("songs").toList().size()==3);CHECK(call(client,"recommend").value("songs").toList().size()==2);
  waf=true;CHECK(call(client,"recommend").value("error")=="invalid_response");waf=false;reject=true;CHECK(call(client,"search",args).value("error")=="provider_error");reject=false;stall=true;CHECK(call(client,"albumSongs",{{"id","123"}}).value("error")=="pagination_stalled");
  CHECK(call(client,"artist",{{"id","../bad"}}).value("error")=="invalid_arguments");CHECK(valid);puts("NATIVE_KUGOU_FIXTURE_PASS 10 operations hashes authors covers 201-track album paging offset ranks wrappers WAF rejection dedup");return 0;
}
