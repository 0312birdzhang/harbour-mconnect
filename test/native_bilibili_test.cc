// Isolated D-Bus and local HTTP only. --live never plays audio or mutates accounts.
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
#define CHECK(x) do{if(!(x)){fprintf(stderr,"native bilibili failed at %d\n",__LINE__);return 1;}}while(false)
QVariantMap call(QDBusConnection& bus,const QString& op,const QVariantMap& args={},bool logged=false) {
  auto msg=QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Silica","/org/sailfishos/Mconnect/Platforms","org.sailfishos.Mconnect.Platforms","Bilibili");
  msg<<op<<QString::fromUtf8(QJsonDocument::fromVariant(args).toJson(QJsonDocument::Compact))<<(logged?QString("SESSDATA=session; bili_jct=csrf"):QString());
  QDBusPendingCallWatcher watcher(bus.asyncCall(msg,20000));QEventLoop loop;QObject::connect(&watcher,&QDBusPendingCallWatcher::finished,&loop,&QEventLoop::quit);loop.exec();QDBusPendingReply<QString> reply=watcher;
  return reply.isError()?QVariantMap{{"error","dbus_error"}}:QJsonDocument::fromJson(reply.value().toUtf8()).toVariant().toMap();
}
QVariantMap video(const QString& id,int attr=0){return {{"bvid",id},{"title",QString::fromUtf8("<em>音乐</em>&amp;测试")},{"duration","1:02:03"},{"pic","//image.test/a"},{"author","UP"},{"mid",1234567890123LL},{"attr",attr}};}
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);auto bus=QDBusConnection::sessionBus();auto client=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"bili-client");CHECK(bus.registerService("org.sailfishos.Mconnect.Silica"));
  const auto mixin=NativeBiliMixin("7cd084941338484aae1ad9425b84077c","4932caff0ff746eab6f01bf08b70ac45");
  CHECK(NativeBiliSign({{"foo","114"},{"bar","514"},{"zab","1919810"}},mixin,1702204169).value("w_rid")=="8f6f2b5b3d485fe1886cec6a0be8c5d4");
  CHECK(NativeBiliSign({{"word","a!'()*b"}},mixin,1).value("word")=="ab");
  if(argc>1 && QByteArray(argv[1])=="--live") {
    NativePlatforms service;CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));
    const auto search=call(client,"search",{{"query",QString::fromUtf8("贝多芬")},{"page",1},{"limit",5}});
    if(search.contains("error")){fprintf(stderr,"BILI_LIVE_SEARCH_ERROR %s\n",qPrintable(search.value("error").toString()));return 2;}
    CHECK(!search.value("songs").toList().isEmpty());const auto id=search.value("songs").toList().first().toMap().value("id").toString();
    const auto parts=call(client,"playlist",{{"id","video:"+id}});CHECK(!parts.contains("error") && !parts.value("songs").toList().isEmpty());
    const auto stream=call(client,"stream",{{"id",id},{"quality",0}});CHECK(!stream.value("url").toString().isEmpty() || stream.value("error")=="unavailable" || stream.value("error")=="verification_required");
    const auto key=call(client,"qrKey");CHECK(!key.contains("error") && !key.value("qrKey").toString().isEmpty());
    printf("NATIVE_BILIBILI_LIVE_PASS search=%d parts=%d streamResolved=%d QR-key\n",search.value("songs").toList().size(),parts.value("songs").toList().size(),!stream.value("url").toString().isEmpty());return 0;
  }
  QTcpServer fixture;CHECK(fixture.listen(QHostAddress::LocalHost));QMap<QString,int> counts;QMap<QString,QUrlQuery> forms,queries;
  bool valid=true,voucher=false,reject=false,httpFail=false,expired=false,stall=false,deletedPage=false,noSubtitles=false;int qrCode=86101;
  const QUrl base(QString("http://127.0.0.1:%1").arg(fixture.serverPort()));
  QObject::connect(&fixture,&QTcpServer::newConnection,&app,[&](){while(fixture.hasPendingConnections()){
    auto* socket=fixture.nextPendingConnection();auto buffer=std::make_shared<QByteArray>();auto started=std::make_shared<bool>(false);QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    QObject::connect(socket,&QTcpSocket::readyRead,&app,[&,socket,buffer,started](){
      if(*started)return;*buffer+=socket->readAll();const int header=buffer->indexOf("\r\n\r\n");if(header<0)return;int length=0;for(const auto& line:buffer->left(header).split('\n'))if(line.toLower().startsWith("content-length:"))length=line.mid(15).trimmed().toInt();if(buffer->size()<header+4+length)return;*started=true;
      const auto parts=buffer->left(buffer->indexOf("\r\n")).split(' ');const QUrl url(QString("http://localhost")+QString::fromLatin1(parts[1].constData(),parts[1].size()));const auto path=url.path();++counts[path];const QUrlQuery form(QString::fromUtf8(buffer->mid(header+4))),query(url);forms[path]=form;queries[path]=query;const bool post=parts[0]=="POST";
      QVariantMap root{{"code",0}};QVariant data;QByteArray headers;
      if(path=="/x/frontend/finger/spi")data=QVariantMap{{"b_3","anonymous"},{"b_4","anon4"}};
      else if(path=="/x/web-interface/nav") {
        const bool logged=buffer->left(header).contains("SESSDATA=");root["code"]=logged?0:-101;
        data=QVariantMap{{"wbi_img",QVariantMap{{"img_url","https://image.test/7cd084941338484aae1ad9425b84077c.png"},{"sub_url","https://image.test/4932caff0ff746eab6f01bf08b70ac45.png"}}},{"isLogin",logged},{"mid",1234567890123LL},{"uname",QString::fromUtf8("中文用户")},{"face","//image.test/user"},{"vipStatus",1}};
      } else if(path.endsWith("/search/type")) {
        if(path.contains("/wbi/")) {
          valid=valid && buffer->left(header).contains("buvid3=anonymous");QMap<QString,QString> signing;
          for(const auto& pair:query.queryItems(QUrl::FullyDecoded))if(pair.first!="w_rid" && pair.first!="wts")signing[pair.first]=pair.second;
          valid=valid && NativeBiliSign(signing,mixin,query.queryItemValue("wts").toLongLong()).value("w_rid")==query.queryItemValue("w_rid");
        }
        data=voucher && path.contains("/wbi/")?QVariantMap{{"v_voucher","challenge"}}:QVariantMap{{"result",QVariantList{video("BVsearch")}}};
      } else if(path=="/x/player/pagelist")data=QVariantList{QVariantMap{{"cid",11}},QVariantMap{{"cid",22}}};
      else if(path=="/x/player/wbi/playurl")data=QVariantMap{{"dash",QVariantMap{{"audio",QVariantList{QVariantMap{{"baseUrl","https://a.bilivideo.com/high"},{"bandwidth",192000}},QVariantMap{{"base_url","https://a.bilivideo.com/low"},{"bandwidth",64000}}}}}}};
      else if(path=="/x/v3/fav/resource/list") {
        const int pn=query.queryItemValue("pn").toInt();const bool first=pn==1 || stall;
        data=QVariantMap{{"has_more",first},{"medias",QVariantList{video(first?"BVfav1":"BVfav2",deletedPage && first?1:0),video("BVdeleted",9)}},{"info",QVariantMap{{"title","folder"},{"media_count",2},{"cover","//image.test/folder"}}}};
      } else if(path=="/x/v3/fav/folder/created/list") {
        const int pn=query.queryItemValue("pn").toInt();data=QVariantMap{{"has_more",pn==1},{"list",QVariantList{QVariantMap{{"id",pn},{"title",QString::fromUtf8("收藏夹")},{"media_count",2},{"cover","//image.test/folder"}}}}};
      } else if(path=="/x/web-interface/view") {
        auto row=video(query.queryItemValue("bvid"));row["aid"]=1234567890123LL;row["owner"]=QVariantMap{{"mid",1234567890123LL},{"name","UP"}};row["pages"]=QVariantList{QVariantMap{{"cid",11},{"part","part one"},{"duration",61}},QVariantMap{{"cid",22},{"part","part two"},{"duration",62}}};data=row;
      } else if(path=="/x/player/wbi/v2")data=QVariantMap{{"subtitle",QVariantMap{{"subtitles",noSubtitles?QVariantList():QVariantList{QVariantMap{{"lan","en"},{"subtitle_url",base.resolved(QUrl("/subtitle-en")).toString()}},QVariantMap{{"lan","zh-CN"},{"subtitle_url",base.resolved(QUrl("/subtitle-zh?auth_key=signature%2Btoken%3D")).toString()}}}}}}};
      else if(path.startsWith("/subtitle-")) {valid=valid && !buffer->left(header).toLower().contains("cookie:") && query.queryItemValue("auth_key",QUrl::FullyDecoded)=="signature+token=";root.clear();root["body"]=QVariantList{QVariantMap{{"from",62.34},{"content",QString::fromUtf8("歌词")}}};}
      else if(path.endsWith("/qrcode/generate"))data=QVariantMap{{"qrcode_key","test-key"},{"url","https://passport.bilibili.com/qrcode/h5/login?key=test-key"}};
      else if(path.endsWith("/qrcode/poll")){data=QVariantMap{{"code",qrCode}};if(qrCode==0)headers="Set-Cookie: SESSDATA=new=bili; Path=/\r\nSet-Cookie: bili_jct=csrf; Path=/\r\n";}
      else if(post){valid=valid && form.queryItemValue("csrf")=="csrf";if(reject)root["code"]=-111;}
      else valid=false;
      if(!path.startsWith("/subtitle-"))root["data"]=data;if(expired)root["code"]=-101;
      const auto body=QJsonDocument::fromVariant(root).toJson(QJsonDocument::Compact);socket->write(httpFail && post?"HTTP/1.1 503 Unavailable\r\n":"HTTP/1.1 200 OK\r\n");socket->write(headers+"Content-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();
    });
  }});
  NativePlatforms service(nullptr,QUrl("https://music.163.com"),base,base);CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));
  const QVariantMap searchArgs{{"query",QString::fromUtf8("音乐 A+B!")},{"page",2},{"limit",5}};
  const auto found=call(client,"search",searchArgs);CHECK(!found.contains("error") && found.value("songs").toList().size()==1);const auto row=found.value("songs").toList().first().toMap();CHECK(row.value("id")=="BVsearch" && row.value("platform")=="bilibili" && row.value("durationMs").toLongLong()==3723000 && row.value("name")==QString::fromUtf8("音乐&测试"));
  voucher=true;CHECK(call(client,"search",searchArgs).value("songs").toList().size()==1 && counts["/x/web-interface/search/type"]==1);voucher=false;
  const int navBefore=counts["/x/web-interface/nav"];
  CHECK(call(client,"stream",{{"id","BVtest:22"},{"quality",0}}).value("url")=="https://a.bilivideo.com/low" && queries["/x/player/wbi/playurl"].queryItemValue("cid")=="22");
  CHECK(call(client,"stream",{{"id","BVtest"},{"quality",7}}).value("url")=="https://a.bilivideo.com/high");
  CHECK(call(client,"qualities",{{"id","BVtest"}}).value("qualities").toList().size()==2 && counts["/x/web-interface/nav"]==navBefore);
  CHECK(call(client,"stream",{{"id","BVtest:999"},{"quality",0}}).value("error")=="unavailable");
  CHECK(call(client,"playlist",{{"id","123"}}).value("songs").toList().size()==2);deletedPage=true;CHECK(call(client,"playlist",{{"id","123"}}).value("songs").toList().size()==1);deletedPage=false;
  stall=true;CHECK(call(client,"playlist",{{"id","123"}}).value("error")=="pagination_stalled");stall=false;
  const auto parts=call(client,"playlist",{{"id","video:BVtest"}}).value("songs").toList();CHECK(parts.size()==2 && parts[1].toMap().value("id")=="BVtest:22");
  CHECK(call(client,"folders",{{"uid","123"},{"nickname",QString::fromUtf8("用户")}},true).value("playlists").toList().size()==2);
  CHECK(call(client,"lyrics",{{"id","BVtest:22"}},true).value("lyrics")==QString::fromUtf8("[01:02.34]歌词") && counts["/subtitle-en"]==0);noSubtitles=true;CHECK(call(client,"lyrics",{{"id","BVtest"}}).value("lyrics").isNull());noSubtitles=false;
  CHECK(!call(client,"qrKey").value("qrKey").toString().isEmpty());CHECK(call(client,"qrCheck",{{"key","test-key"}}).value("qrStatus")=="waiting");qrCode=86090;CHECK(call(client,"qrCheck",{{"key","test-key"}}).value("qrStatus")=="scanned");qrCode=86038;CHECK(call(client,"qrCheck",{{"key","test-key"}}).value("qrStatus")=="expired");qrCode=0;
  const auto login=call(client,"qrCheck",{{"key","test-key"}});CHECK(login.value("user").toMap().value("id")=="1234567890123" && login.value("cookie").toString().contains("SESSDATA=new=bili"));
  CHECK(call(client,"profile",{},true).value("user").toMap().value("vipLevel").toInt()==1);
  CHECK(call(client,"like",{{"id","BVtest:22"},{"enabled",false}},true).value("ok").toBool() && forms["/x/web-interface/archive/like"].queryItemValue("like")=="2");
  CHECK(call(client,"add",{{"id","123"},{"songId","BVtest:22"}},true).value("ok").toBool() && forms["/x/v3/fav/resource/batch-deal"].queryItemValue("resources",QUrl::FullyDecoded)=="1234567890123:2");
  CHECK(call(client,"remove",{{"id","123"},{"songId","BVtest"}},true).value("ok").toBool() && forms["/x/v3/fav/resource/batch-deal"].queryItemValue("del_media_ids")=="123");
  reject=true;CHECK(!call(client,"like",{{"id","BVtest"},{"enabled",true}},true).value("ok").toBool());reject=false;
  const int writes=counts["/x/web-interface/archive/like"];httpFail=true;CHECK(call(client,"like",{{"id","BVtest"},{"enabled",true}},true).value("error")=="network_error" && counts["/x/web-interface/archive/like"]==writes+1);httpFail=false;
  CHECK(call(client,"share",{{"url","https://www.bilibili.com/video/BVtest"}}).value("playlists").toList().first().toMap().value("id")=="video:BVtest");CHECK(call(client,"share",{{"url","https://space.bilibili.com/1?fid=123"}}).value("playlists").toList().size()==1);
  const int views=counts["/x/web-interface/view"];CHECK(call(client,"share",{{"url","https://bilibili.com.evil.test/video/BVtest"}}).value("playlists").toList().isEmpty() && counts["/x/web-interface/view"]==views);
  expired=true;CHECK(call(client,"profile",{},true).value("error")=="login_required");expired=false;
  CHECK(call(client,"stream",{{"id","../bad"},{"quality",0}}).value("error")=="invalid_arguments");CHECK(call(client,"like",{{"id","BVtest"},{"enabled",true}}).value("error")=="login_required");
  CHECK(valid);puts("NATIVE_BILIBILI_FIXTURE_PASS 13 operations WBI cookies CID audio pagination subtitles no-CDN-cookie login writes errors");return 0;
}
