// Run under dbus-run-session. All writes/SMS/login completion use local fixtures.
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
#define CHECK(x) do{if(!(x)){fprintf(stderr,"native account failed at %d\n",__LINE__);return 1;}}while(false)
QVariantMap call(QDBusConnection& bus,const QString& operation,const QVariantMap& args={}) {
  auto msg=QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Silica","/org/sailfishos/Mconnect/Platforms","org.sailfishos.Mconnect.Platforms","NeteaseAccount");
  msg<<operation<<QString::fromUtf8(QJsonDocument::fromVariant(args).toJson(QJsonDocument::Compact))<<QString("os=pc; appver=3.0.18.203152; __csrf=test; MUSIC_U=old");
  QDBusPendingCallWatcher watcher(bus.asyncCall(msg,20000));QEventLoop loop;
  QObject::connect(&watcher,&QDBusPendingCallWatcher::finished,&loop,&QEventLoop::quit);loop.exec();QDBusPendingReply<QString> reply=watcher;
  return reply.isError()?QVariantMap{{"error","dbus_error"}}:QJsonDocument::fromJson(reply.value().toUtf8()).toVariant().toMap();
}
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);auto bus=QDBusConnection::sessionBus();auto client=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"account-client");
  CHECK(bus.registerService("org.sailfishos.Mconnect.Silica"));
  if(argc>1 && QByteArray(argv[1])=="--live") {
    NativePlatforms service;CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));
    const auto key=call(client,"qrKey");CHECK(key.value("ok").toBool() && !key.value("qrKey").toString().isEmpty());
    const auto profile=call(client,"profile");CHECK(profile.value("ok").toBool() || profile.value("error")=="login_required");
    puts("NATIVE_ACCOUNT_LIVE_PASS QR-key anonymous-profile only; no SMS or mutations");return 0;
  }
  QTcpServer fixture;CHECK(fixture.listen(QHostAddress::LocalHost));QMap<QString,int> counts;QMap<QString,QUrlQuery> forms;
  bool good=true,bad=false,httpFail=false,reject=false,missing=false,expired=false,inlineProfile=false;int qrCode=801;
  const QVariantMap user{{"userId",1234567890123LL},{"nickname",QString::fromUtf8("中文用户")},{"avatarUrl","//image.test/avatar"},{"vipType",11}};
  QObject::connect(&fixture,&QTcpServer::newConnection,&app,[&](){while(fixture.hasPendingConnections()) {
    auto* socket=fixture.nextPendingConnection();auto buffer=std::make_shared<QByteArray>();auto started=std::make_shared<bool>(false);
    QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    QObject::connect(socket,&QTcpSocket::readyRead,&app,[&,socket,buffer,started](){
      if(*started)return;*buffer+=socket->readAll();const int header=buffer->indexOf("\r\n\r\n");if(header<0)return;
      int length=0;for(const auto& line:buffer->left(header).split('\n'))if(line.toLower().startsWith("content-length:"))length=line.mid(15).trimmed().toInt();
      if(buffer->size()<header+4+length)return;*started=true;
      const auto parts=buffer->left(buffer->indexOf("\r\n")).split(' ');const QUrl url(QString("http://localhost")+QString::fromLatin1(parts[1].constData(),parts[1].size()));
      const auto path=url.path();++counts[path];const QUrlQuery form(QString::fromUtf8(buffer->mid(header+4)));forms[path]=form;good=good && parts[0]=="POST";
      QVariantMap root{{"code",reject?500:200},{"message",QString::fromUtf8("服务器拒绝")}};
      if(path=="/api/login/qrcode/unikey")root["data"]=QVariantMap{{"unikey","test-key"}};
      if(path=="/api/login/qrcode/client/login"){root["code"]=qrCode;if(qrCode==803)root["cookie"]="MUSIC_U=body=token; __csrf=next; Path=/";}
      if(path=="/api/login/cellphone")root["cookie"]="MUSIC_U=phone=token; __csrf=next";
      if(path=="/api/nuser/account/get") {
        root["profile"]=missing?QVariant():QVariant(user);
        good=good && buffer->left(header).contains("MUSIC_U=");
      }
      if(inlineProfile && (path=="/api/login/cellphone" || path=="/api/login/qrcode/client/login"))root["profile"]=user;
      if(path=="/api/playlist/create")root["playlist"]=QVariantMap{{"id",9876543210123LL},{"name",form.queryItemValue("name",QUrl::FullyDecoded)},{"trackCount",0}};
      if(path.startsWith("/api/playlist/"))good=good && form.queryItemValue("csrf")=="test";
      if(expired)root["code"]=301;
      const auto body=bad?QByteArray("<html>"):QJsonDocument::fromVariant(root).toJson(QJsonDocument::Compact);
      socket->write(httpFail?"HTTP/1.1 503 Unavailable\r\n":"HTTP/1.1 200 OK\r\n");
      socket->write("Set-Cookie: MUSIC_A=anonymous; Path=/\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();
    });
  }});
  NativePlatforms service(nullptr,QUrl(QString("http://127.0.0.1:%1").arg(fixture.serverPort())));CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));
  CHECK(call(client,"qrKey").value("qrUrl")=="https://music.163.com/login?codekey=test-key");
  CHECK(call(client,"qrCheck",{{"key","test-key"}}).value("qrStatus")=="waiting");qrCode=802;
  CHECK(call(client,"qrCheck",{{"key","test-key"}}).value("qrStatus")=="scanned");qrCode=800;
  CHECK(call(client,"qrCheck",{{"key","test-key"}}).value("qrStatus")=="expired");qrCode=803;
  const auto qr=call(client,"qrCheck",{{"key","test-key"}});CHECK(qr.value("qrStatus")=="success" && qr.value("user").toMap().value("id")=="1234567890123");
  CHECK(qr.value("cookie").toString().contains("MUSIC_U=body=token") && qr.value("cookie").toString().contains("__csrf=next") && !qr.value("cookie").toString().contains("Path="));
  CHECK(call(client,"sms",{{"phone","13800000000"}}).value("ok").toBool());
  const auto phone=call(client,"phoneLogin",{{"phone","13800000000"},{"code","123456"}});CHECK(phone.value("user").toMap().value("vipLevel").toInt()==2 && phone.value("cookie").toString().contains("MUSIC_U=phone=token"));
  CHECK(forms["/api/login/cellphone"].queryItemValue("captcha")=="123456");
  const int before=counts["/api/nuser/account/get"];inlineProfile=true;CHECK(call(client,"phoneLogin",{{"phone","13800000000"},{"code","123456"}}).value("ok").toBool() && counts["/api/nuser/account/get"]==before);inlineProfile=false;
  CHECK(call(client,"profile").value("user").toMap().value("avatarUrl")=="https://image.test/avatar");CHECK(call(client,"vip").value("vipLevel").toInt()==2);
  CHECK(call(client,"like",{{"id","186016"},{"enabled",false}}).value("ok").toBool() && forms["/api/radio/like"].queryItemValue("like")=="false");
  CHECK(call(client,"addTrack",{{"id","1"},{"songId","186016"}}).value("ok").toBool() && forms["/api/playlist/manipulate/tracks"].queryItemValue("op")=="add");
  CHECK(call(client,"removeTrack",{{"id","1"},{"songId","186016"}}).value("ok").toBool() && forms["/api/playlist/manipulate/tracks"].queryItemValue("op")=="del");
  const auto name=QString::fromUtf8("中文 & + # 歌单");const auto created=call(client,"create",{{"name",name}});
  CHECK(created.value("playlists").toList().first().toMap().value("name")==name);
  CHECK(call(client,"rename",{{"id","1"},{"name",name}}).value("ok").toBool() && forms["/api/playlist/update"].queryItemValue("name",QUrl::FullyDecoded)==name);
  CHECK(call(client,"delete",{{"id","1234567890123"}}).value("ok").toBool() && forms["/api/playlist/delete"].queryItemValue("ids",QUrl::FullyDecoded)=="[1234567890123]");
  CHECK(call(client,"collect",{{"id","1"},{"enabled",true}}).value("ok").toBool() && counts["/api/playlist/subscribe"]==1);
  CHECK(call(client,"collect",{{"id","1"},{"enabled",false}}).value("ok").toBool() && counts["/api/playlist/unsubscribe"]==1);
  reject=true;CHECK(!call(client,"like",{{"id","1"},{"enabled",true}}).value("ok").toBool());CHECK(!call(client,"sms",{{"phone","13800000000"}}).value("ok").toBool());reject=false;
  missing=true;CHECK(call(client,"profile").value("user").isNull());CHECK(call(client,"phoneLogin",{{"phone","13800000000"},{"code","123456"}}).value("error")=="missing_profile");missing=false;
  expired=true;CHECK(call(client,"vip").value("error")=="login_required");expired=false;
  bad=true;CHECK(call(client,"qrKey").value("error")=="invalid_response");bad=false;
  const int posts=counts["/api/radio/like"];httpFail=true;CHECK(call(client,"like",{{"id","1"},{"enabled",true}}).value("error")=="network_error" && counts["/api/radio/like"]==posts+1);httpFail=false;
  const int smsCount=counts["/api/sms/captcha/sent"];
  CHECK(call(client,"sms",{{"phone","invalid"}}).value("error")=="invalid_arguments" && counts["/api/sms/captcha/sent"]==smsCount);
  CHECK(call(client,"delete",{{"id","1"},{"extra","bad"}}).value("error")=="invalid_arguments");
  CHECK(call(client,"like",{{"id","1"},{"enabled","true"}}).value("error")=="invalid_arguments");
  CHECK(good);puts("NATIVE_ACCOUNT_FIXTURE_PASS 13 operations QR phone profile VIP cookies UTF8 CSRF rejected-writes no-POST-retry validation");return 0;
}
