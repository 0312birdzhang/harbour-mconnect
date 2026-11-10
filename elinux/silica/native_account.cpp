#include "native_platforms.h"
#include "platform_models.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

namespace {
const QStringList operations{"qrKey","qrCheck","sms","phoneLogin","profile","vip","like","addTrack","removeTrack","create","rename","delete","collect"};
QVariantMap failure(const QString& error){return {{"backend","cpp-netease-account"},{"error",error}};}
QString json(const QVariantMap& value){return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));}
bool cookieValid(const QString& value){if(value.size()>32768)return false;for(const auto ch:value)if(ch.unicode()<32 || ch.unicode()>126)return false;return true;}
bool matches(const QString& text,const char* pattern){return QRegularExpression(pattern).match(text).hasMatch();}
bool valid(const QString& operation,const QVariantMap& args) {
  QSet<QString> fields;
  if(operation=="qrCheck")fields={"key"};
  else if(operation=="sms")fields={"phone"};
  else if(operation=="phoneLogin")fields={"phone","code"};
  else if(operation=="like")fields={"id","enabled"};
  else if(operation=="addTrack" || operation=="removeTrack")fields={"id","songId"};
  else if(operation=="create")fields={"name"};
  else if(operation=="rename")fields={"id","name"};
  else if(operation=="delete")fields={"id"};
  else if(operation=="collect")fields={"id","enabled"};
  if(args.size()!=fields.size())return false;
  for(auto it=args.begin();it!=args.end();++it) {
    if(!fields.contains(it.key()))return false;
    if(it.key()=="enabled"){if(it.value().type()!=QVariant::Bool)return false;continue;}
    if(it.value().type()!=QVariant::String)return false;
    const auto text=it.value().toString();
    if((it.key()=="id" || it.key()=="songId") && !matches(text,"^[0-9]{1,32}$"))return false;
    if(it.key()=="name" && (text.trimmed().isEmpty() || text.size()>256 || text.contains(QChar(0))))return false;
    if(it.key()=="phone" && !matches(text,"^\\+?[0-9]{5,20}$"))return false;
    if(it.key()=="code" && !matches(text,"^[0-9]{4,10}$"))return false;
    if(it.key()=="key" && !matches(text,"^[A-Za-z0-9_-]{1,256}$"))return false;
  }
  return true;
}
QString bodyCookie(const QString& original,const QVariant& value) {
  if(value.type()!=QVariant::String || !cookieValid(value.toString()))return original;
  QList<QPair<QByteArray,QByteArray>> headers;
  for(const auto& part:value.toString().split(';')) {
    const auto name=part.section('=',0,0).trimmed();
    if(!matches(name,"^[A-Za-z0-9_!#$%&'*+.^`|~-]+$") || !part.contains('='))continue;
    if(QStringList{"path","domain","expires","max-age","samesite"}.contains(name.toLower()))continue;
    headers.append({"Set-Cookie",part.trimmed().toLatin1()});
  }
  return MergeNativeCookies(original,headers);
}
QVariantMap profile(const QVariant& value) {
  if(value.isNull())return {};
  if(value.type()!=QVariant::Map)return failure("invalid_response");
  const auto raw=value.toMap();const auto id=ParseNeteaseArtist({{"id",raw.value("userId")}}).id;
  if(id.isEmpty() || raw.value("nickname").type()!=QVariant::String || raw.value("nickname").toString().size()>8192)return failure("invalid_response");
  const auto avatar=ParseNeteaseArtist({{"cover",raw.value("avatarUrl")}}).avatarUrl;
  const int type=raw.value("vipType").toInt();
  return {{"id",id},{"nickname",raw.value("nickname")},{"avatarUrl",avatar},{"platform","netease"},{"vipLevel",type>=11?2:(type>=10?1:0)}};
}
class AccountJob:public std::enable_shared_from_this<AccountJob> {
 public:
  NativePlatformHttp* http=nullptr;QUrl endpoint;QString operation,cookie;QVariantMap args;QElapsedTimer elapsed;
  std::function<void(QVariantMap)> done;
  void finish(QVariantMap result){
    result["backend"]="cpp-netease-account";
    if(!result.contains("error")){result["cookie"]=cookie;qInfo("MCONNECT_NATIVE_NETEASE_ACCOUNT_OK operation=%s",qPrintable(operation));}
    done(result);
  }
  void fetch(const QString& path,QList<QPair<QString,QString>> fields,std::function<void(QVariantMap)> callback) {
    const int remaining=18000-int(elapsed.elapsed());if(remaining<=0){finish(failure("timeout"));return;}
    QByteArray body;for(const auto& field:fields){if(!body.isEmpty())body+='&';body+=QUrl::toPercentEncoding(field.first)+'='+QUrl::toPercentEncoding(field.second);}
    auto req=MakeNeteaseRequest(endpoint.resolved(QUrl(path)),cookie);req.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
    const auto self=shared_from_this();
    http->request(req,"POST",body,[self,callback](NativeHttpResult response){
      if(!response.error.isEmpty()){self->finish(failure(response.error));return;}
      QJsonParseError error;const auto doc=QJsonDocument::fromJson(response.body,&error);
      if(error.error!=QJsonParseError::NoError || !doc.isObject()){self->finish(failure("invalid_response"));return;}
      const auto root=doc.toVariant().toMap();
      if(root.value("code").type()!=QVariant::LongLong && root.value("code").type()!=QVariant::Int && root.value("code").type()!=QVariant::Double){self->finish(failure("invalid_response"));return;}
      self->cookie=MergeNativeCookies(self->cookie,response.headers);
      const int code=root.value("code").toInt();
      if(code==301 || code==401){self->finish(failure("login_required"));return;}
      callback(root);
    },remaining,1024*1024);
  }
  void user(QVariantMap result,const QVariant& supplied=QVariant()) {
    const auto data=profile(supplied);
    if(data.contains("error")){finish(data);return;}
    if(!data.isEmpty()){result["user"]=data;result["vipLevel"]=data.value("vipLevel");finish(result);return;}
    const auto self=shared_from_this();
    fetch("/api/nuser/account/get",{},[self,result](QVariantMap root)mutable{
      if(root.value("code").toInt()!=200){self->finish(failure("provider_error"));return;}
      const auto value=profile(root.value("profile"));
      if(value.contains("error")){self->finish(value);return;}
      if(value.isEmpty() && self->operation!="profile" && self->operation!="vip"){self->finish(failure("missing_profile"));return;}
      result["user"]=value.isEmpty()?QVariant():QVariant(value);result["vipLevel"]=value.value("vipLevel",0);self->finish(result);
    });
  }
  void start() {
    const auto self=shared_from_this();
    if(operation=="profile" || operation=="vip"){user({{"ok",true}});return;}
    QString csrf;for(const auto& part:cookie.split(';'))if(part.trimmed().startsWith("__csrf="))csrf=part.trimmed().mid(7);
    QString path;QList<QPair<QString,QString>> fields;
    if(operation=="qrKey"){path="/api/login/qrcode/unikey";fields={{"type","3"}};}
    else if(operation=="qrCheck"){path="/api/login/qrcode/client/login";fields={{"key",args.value("key").toString()},{"type","3"}};}
    else if(operation=="sms"){path="/api/sms/captcha/sent";fields={{"phone",args.value("phone").toString()}};}
    else if(operation=="phoneLogin"){path="/api/login/cellphone";fields={{"phone",args.value("phone").toString()},{"captcha",args.value("code").toString()}};}
    else if(operation=="like"){path="/api/radio/like";fields={{"trackId",args.value("id").toString()},{"like",args.value("enabled").toBool()?"true":"false"}};}
    else if(operation=="create"){path="/api/playlist/create";fields={{"name",args.value("name").toString()},{"privacy","0"},{"type","NORMAL"},{"csrf",csrf}};}
    else if(operation=="rename"){path="/api/playlist/update";fields={{"id",args.value("id").toString()},{"name",args.value("name").toString()},{"csrf",csrf}};}
    else if(operation=="delete"){path="/api/playlist/delete";fields={{"ids","["+args.value("id").toString()+"]"},{"csrf",csrf}};}
    else if(operation=="collect"){path=args.value("enabled").toBool()?"/api/playlist/subscribe":"/api/playlist/unsubscribe";fields={{"id",args.value("id").toString()},{"csrf",csrf}};}
    else {path="/api/playlist/manipulate/tracks";fields={{"op",operation=="addTrack"?"add":"del"},{"pid",args.value("id").toString()},{"trackIds","["+args.value("songId").toString()+"]"},{"imme","true"},{"csrf",csrf}};}
    fetch(path,fields,[self](QVariantMap root){self->response(root);});
  }
  void response(const QVariantMap& root) {
    const int code=root.value("code").toInt();
    if(operation=="qrCheck") {
      if(code==803){cookie=bodyCookie(cookie,root.value("cookie"));user({{"ok",true},{"qrStatus","success"}},root.value("profile"));return;}
      finish({{"ok",code==800 || code==801 || code==802},{"qrStatus",code==800?"expired":(code==801?"waiting":(code==802?"scanned":"failed"))}});return;
    }
    if(code!=200) {
      auto message=root.value("message",root.value("msg")).toString().left(512);if(message.isEmpty())message=QString::fromUtf8("平台拒绝了此次操作，请稍后重试");
      finish({{"ok",false},{"providerCode",code},{"message",message}});return;
    }
    if(operation=="qrKey") {
      const auto key=root.value("data").toMap().value("unikey",root.value("unikey")).toString();
      if(!matches(key,"^[A-Za-z0-9_-]{1,256}$")){finish(failure("invalid_response"));return;}
      finish({{"ok",true},{"qrKey",key},{"qrUrl","https://music.163.com/login?codekey="+key}});return;
    }
    if(operation=="phoneLogin"){cookie=bodyCookie(cookie,root.value("cookie"));user({{"ok",true}},root.value("profile"));return;}
    if(operation=="create") {
      if(root.value("playlist").type()!=QVariant::Map){finish(failure("invalid_response"));return;}
      const auto list=ParseNeteasePlaylist(root.value("playlist").toMap());
      if(list.id.isEmpty() || list.name.trimmed().isEmpty()){finish(failure("invalid_response"));return;}
      finish({{"ok",true},{"playlists",QVariantList{list.json()}}});return;
    }
    finish({{"ok",true}});
  }
};
}
QString NativePlatforms::NeteaseAccount(const QString& operation,const QString& arguments,const QString& cookie) {
  if(!operations.contains(operation) || arguments.toUtf8().size()>16384 || !cookieValid(cookie))return json(failure("invalid_arguments"));
  QJsonParseError error;const auto doc=QJsonDocument::fromJson(arguments.toUtf8(),&error);
  if(error.error!=QJsonParseError::NoError || !doc.isObject() || !valid(operation,doc.toVariant().toMap()))return json(failure("invalid_arguments"));
  if(!calledFromDBus())return json(failure("dbus_required"));
  setDelayedReply(true);const auto bus=connection();const auto call=message();
  auto job=std::make_shared<AccountJob>();job->http=&http_;job->endpoint=endpoint_;job->operation=operation;job->args=doc.toVariant().toMap();job->cookie=cookie;
  job->done=[bus,call](const QVariantMap& result){bus.send(call.createReply(QVariantList{json(result)}));};
  job->elapsed.start();job->start();return {};
}
