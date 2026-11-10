#include "../elinux/silica/native_platforms.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusMessage>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QRegularExpression>
#include <memory>
#include <cstdio>

#define CHECK(x) do {if(!(x)){fprintf(stderr,"native platforms failed at %d\n",__LINE__);return 1;}}while(false)
NativeHttpResult perform(NativePlatformHttp& http,const QNetworkRequest& request,const QByteArray& method="GET",int maximum=4096,int timeout=1500,bool publicKugou=false) {
  QEventLoop loop;NativeHttpResult result;result.error="test_timeout";
  http.request(request,method,method=="POST"?QByteArray("x=1"):QByteArray(),[&](NativeHttpResult response){result=response;loop.quit();},timeout,maximum,true,publicKugou);
  QTimer::singleShot(3000,&loop,&QEventLoop::quit);loop.exec();return result;
}
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  if(argc>1 && QByteArray(argv[1])=="--live") {
    NativePlatforms service;
    auto bus=QDBusConnection::sessionBus();
    CHECK(bus.registerService("org.sailfishos.Mconnect.Silica"));
    CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));
    auto client=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"native-platform-client");
    bool passed=true;
    for(const int type:{1,1000}) {
      auto call=QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Silica","/org/sailfishos/Mconnect/Platforms","org.sailfishos.Mconnect.Platforms","SearchNetease");
      call<<QString::fromUtf8("周杰伦")<<1<<30<<type<<QString("os=pc; appver=3.0.18.203152");
      QDBusPendingCallWatcher watcher(client.asyncCall(call,22000));
      QEventLoop loop;QObject::connect(&watcher,&QDBusPendingCallWatcher::finished,&loop,&QEventLoop::quit);loop.exec();
      const QDBusPendingReply<QString> reply=watcher;
      const auto result=reply.isError()?QVariantMap():QJsonDocument::fromJson(reply.value().toUtf8()).toVariant().toMap();
      const auto entries=result.value(type==1?"songs":"playlists").toList();
      const bool ok=!reply.isError() && !result.contains("error") && result.value("backend")=="cpp-netease-search" && !entries.isEmpty();
      fprintf(stderr,"NATIVE_NETEASE_LIVE_%s type=%d count=%d error=%s\n",ok?"PASS":"FAIL",type,entries.size(),qPrintable(result.value("error").toString()));
      passed=passed && ok;
    }
    return passed?0:1;
  }
  const auto encoded=NativePlatformUrl(QUrl("https://music.163.com/api/cloudsearch/pc"),{{"s",QString::fromUtf8("中+A&B #")},{"offset","30"}}).toEncoded();
  CHECK(encoded.contains("s=%E4%B8%AD%2BA%26B%20%23") && encoded.contains("offset=30"));
  const auto parsed=ParseNeteaseSearch(R"({"code":200,"result":{"songs":[{"id":1234567890123,"name":"中文标题","ar":[{"id":123456789,"name":"歌手"}],"al":{"id":234567890,"name":"专辑","picUrl":"//image.test/cover"},"dt":123456,"no":2,"fee":8}]}})",1);
  CHECK(!parsed.contains("error"));
  const auto song=parsed.value("songs").toList().first().toMap();
  CHECK(song.value("id")=="1234567890123" && song.value("albumId")=="234567890" && song.value("artistId")=="123456789");
  CHECK(song.value("name")==QString::fromUtf8("中文标题") && song.value("durationMs").toInt()==123456);
  CHECK(song.value("coverUrl")=="https://image.test/cover" && song.value("fee").toInt()==8 && song.value("trackNumber").toInt()==2);
  const auto legacy=ParseNeteaseSearch(R"({"code":200,"result":{"songs":[{"id":"00123","name":"old","artists":[{"id":"42","name":"artist"}],"album":{"id":"19","name":"album"},"duration":3210}]}})",1);
  CHECK(legacy.value("songs").toList().first().toMap().value("id")=="00123");
  CHECK(legacy.value("songs").toList().first().toMap().value("durationMs").toInt()==3210);
  CHECK(ParseNeteaseSearch("<html>error</html>",1).value("error")=="invalid_response");
  CHECK(ParseNeteaseSearch("{\"code\":301}",1).value("error")=="login_required");
  CHECK(ParseNeteaseSearch("{\"code\":200,\"result\":{\"songs\":{}}}",1).contains("error"));
  CHECK(ParseNeteaseSearch("{\"code\":200,\"result\":{\"songs\":[{}]}}",1).contains("error"));
  CHECK(ParseNeteaseSearch("{\"code\":200}",1).value("songs").toList().isEmpty());
  const auto playlists=ParseNeteaseSearch(R"({"code":200,"result":{"playlists":[{"id":12345678901,"name":"歌单","bookCount":23,"coverImgUrl":"https://image.test/a","creator":{"nickname":"创建者"}}]}})",1000);
  const auto list=playlists.value("playlists").toList().first().toMap();
  CHECK(list.value("id")=="12345678901" && list.value("songCount").toInt()==23 && list.value("editable")==false && list.value("collected")==true);
  const auto cookie=MergeNativeCookies("os=pc; MUSIC_U=abc==; old=1",{{"Set-Cookie","new=2; Path=/; HttpOnly"},{"set-cookie","old=; Max-Age=0"}});
  CHECK(cookie.contains("MUSIC_U=abc==") && cookie.contains("new=2") && cookie.contains("old=") && !cookie.contains("Path"));
  NativePlatforms boundary;
  CHECK(boundary.SearchNetease("",1,30,1,"").contains("invalid_arguments"));
  CHECK(boundary.SearchNetease("valid",1,30,1,"a=b\r\nX: bad").contains("invalid_arguments"));
  CHECK(boundary.SearchNetease("valid",1,30,1,"").contains("dbus_required"));

  QTcpServer fixture;CHECK(fixture.listen(QHostAddress::LocalHost));
  QMap<QByteArray,int> counts;QByteArray lastCookie;
  QObject::connect(&fixture,&QTcpServer::newConnection,&app,[&](){
    while(fixture.hasPendingConnections()) {
      auto* socket=fixture.nextPendingConnection();auto bytes=std::make_shared<QByteArray>();auto started=std::make_shared<bool>(false);
      QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
      QObject::connect(socket,&QTcpSocket::readyRead,&app,[&,socket,bytes,started](){
        if(*started)return;*bytes+=socket->readAll();const int headerEnd=bytes->indexOf("\r\n\r\n");if(headerEnd<0)return;
        const int length=QRegularExpression("Content-Length:\\s*(\\d+)",QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(bytes->left(headerEnd))).captured(1).toInt();
        if(bytes->size()<headerEnd+4+length)return;
        const auto line=bytes->left(bytes->indexOf("\r\n")).split(' ');if(line.size()<2)return;
        *started=true;const auto path=line[1];const int count=++counts[path];lastCookie.clear();
        for(const auto& header:bytes->split('\n'))if(header.toLower().startsWith("cookie:"))lastCookie=header.trimmed();
        if(path=="/slow")return;
        if(path=="/cross" || path=="/redirect") {
          socket->write("HTTP/1.1 302 Found\r\nLocation: "+QByteArray(path=="/cross"?"https://example.invalid/private":"/ok")+"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        } else {
          const bool failure=path=="/post" || (path=="/retry" && count==1);
          const auto body=path=="/large"?QByteArray(512,'x'):QByteArray("{\"code\":200}");
          socket->write(failure?"HTTP/1.1 503 Unavailable\r\n":"HTTP/1.1 200 OK\r\n");
          socket->write("Set-Cookie: hidden=first-account; Path=/\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
        }
        socket->disconnectFromHost();
      });
    }
  });
  const auto request=[&](const QString& path){return QNetworkRequest(QUrl(QString("http://127.0.0.1:%1%2").arg(fixture.serverPort()).arg(path)));};
  NativePlatformHttp http;
  CHECK(perform(http,request("/retry")).error.isEmpty() && counts["/retry"]==2);
  const auto post=perform(http,request("/post"),"POST");
  if(post.error.isEmpty() || counts["/post"]!=1)fprintf(stderr,"POST error=%s status=%d requests=%d\n",qPrintable(post.error),post.status,counts["/post"]);
  CHECK(!post.error.isEmpty() && counts["/post"]==1);
  CHECK(perform(http,request("/redirect")).error.isEmpty() && counts["/redirect"]==1 && counts["/ok"]==1);
  CHECK(perform(http,request("/cross")).error=="redirect_rejected");
  CHECK(perform(http,request("/large"),"GET",64).error=="response_too_large");
  CHECK(perform(http,request("/slow"),"GET",4096,150).error=="timeout");
  auto account=request("/account");account.setRawHeader("Cookie","account=second");
  CHECK(perform(http,account).error.isEmpty() && lastCookie=="Cookie: account=second");
  CHECK(perform(http,request("/guest")).error.isEmpty() && lastCookie.isEmpty());
  CHECK(perform(http,QNetworkRequest(QUrl("http://example.invalid/api"))).error=="invalid_request");
  CHECK(perform(http,QNetworkRequest(QUrl("http://example.invalid/api")),"GET",4096,1500,true).error=="invalid_request");
  CHECK(perform(http,QNetworkRequest(QUrl("http://mobilecdn.kugou.com/private")),"GET",4096,1500,true).error=="invalid_request");
  auto credentials=QNetworkRequest(QUrl("http://mobilecdn.kugou.com/api/v3/search/song"));credentials.setRawHeader("Cookie","token=never-send");CHECK(perform(http,credentials,"GET",4096,1500,true).error=="invalid_request");
  CHECK(perform(http,QNetworkRequest(QUrl("http://mobilecdn.kugou.com/api/v3/search/song")),"POST",4096,1500,true).error=="invalid_request");
  CHECK(perform(http,QNetworkRequest(QUrl("https://name:secret@example.invalid/api"))).error=="invalid_request");
  puts("NATIVE_PLATFORMS_PASS stable-IDs unicode old-new-models playlists cookies GET-retry POST-no-retry bounds timeout redirects account-isolation");
  return 0;
}
