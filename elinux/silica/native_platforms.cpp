#include "native_platforms.h"
#include "platform_models.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkCookie>
#include <QDateTime>

namespace {
QString json(const QVariantMap& value) {return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));}
QVariantMap error(const QString& code) {return {{"error",code},{"backend","cpp-netease-search"}};}
}
QVariantMap ParseNeteaseSearch(const QByteArray& body,int type) {
  if(type!=1 && type!=1000)return error("invalid_arguments");
  QJsonParseError failure;const auto document=QJsonDocument::fromJson(body,&failure);
  if(failure.error!=QJsonParseError::NoError || !document.isObject())return error("invalid_response");
  const auto root=document.toVariant().toMap();const auto code=root.value("code").toInt();
  if(code==301 || code==401)return error("login_required");
  if(code!=200)return error("provider_error");
  const auto result=root.value("result");
  // An absent result is the documented empty search response. A wrong shape
  // must not silently turn a parser failure into an empty song list.
  if(result.isValid() && !result.isNull() && result.type()!=QVariant::Map)return error("invalid_response");
  const QString field=type==1?"songs":"playlists";
  const auto raw=result.toMap().value(field);
  if(raw.isValid() && !raw.isNull() && raw.type()!=QVariant::List)return error("invalid_response");
  if(raw.toList().size()>100)return error("invalid_response");
  QVariantList entries;
  for(const auto& item:raw.toList()) {
    if(item.type()!=QVariant::Map)continue;
    if(type==1){const auto song=ParseNeteaseSong(item.toMap());if(!song.id.isEmpty() && !song.name.trimmed().isEmpty())entries.append(song.json());}
    else {const auto list=ParseNeteasePlaylist(item.toMap());if(!list.id.isEmpty() && !list.name.trimmed().isEmpty())entries.append(list.json());}
  }
  if(!raw.toList().isEmpty() && entries.isEmpty())return error("invalid_response");
  return {{field,entries},{"backend","cpp-netease-search"}};
}
QString MergeNativeCookies(const QString& original,const QList<QPair<QByteArray,QByteArray>>& headers) {
  QMap<QString,QString> cookies;
  for(const auto& raw:original.split(';')){const auto part=raw.trimmed();const int split=part.indexOf('=');if(split>0)cookies[part.left(split)]=part.mid(split+1);}
  for(const auto& pair:headers) {
    if(pair.first.toLower()!="set-cookie")continue;
    for(const auto& cookie:QNetworkCookie::parseCookies(pair.second)) {
      const auto rawName=cookie.name(),rawValue=cookie.value();
      const auto name=QString::fromLatin1(rawName.constData(),rawName.size());
      const auto value=(!cookie.isSessionCookie() && cookie.expirationDate()<=QDateTime::currentDateTimeUtc())?QString():QString::fromLatin1(rawValue.constData(),rawValue.size());
      if(name.isEmpty() || name.contains('\r') || name.contains('\n') || value.contains('\r') || value.contains('\n'))continue;
      cookies[name]=value;
    }
  }
  QStringList result;for(auto it=cookies.cbegin();it!=cookies.cend();++it)result.append(it.key()+"="+it.value());
  return result.join("; ");
}
QNetworkRequest MakeNeteaseRequest(const QUrl& url,const QString& cookie) {
  QNetworkRequest request(url);
  request.setRawHeader("User-Agent","Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Safari/537.36 Chrome/125.0.0.0 NeteaseMusicDesktop/3.0.18.203152");
  request.setRawHeader("Referer","https://music.163.com/");request.setRawHeader("Origin","https://music.163.com");request.setRawHeader("Accept","application/json");
  if(!cookie.isEmpty())request.setRawHeader("Cookie",cookie.toUtf8());
  return request;
}
QString NativePlatforms::SearchNetease(const QString& query,int page,int limit,int type,const QString& cookie) {
  if(query.trimmed().isEmpty() || query.size()>256 || page<1 || page>100 || limit<1 || limit>50 || (type!=1 && type!=1000) || cookie.size()>32768 || cookie.contains('\r') || cookie.contains('\n') || cookie.contains(QChar(0)))return json(error("invalid_arguments"));
  for(const auto ch:cookie)if(ch.unicode()<32 || ch.unicode()>126)return json(error("invalid_arguments"));
  if(!calledFromDBus())return json(error("dbus_required"));
  setDelayedReply(true);const auto bus=connection();const auto call=message();
  const auto url=NativePlatformUrl(endpoint_.resolved(QUrl("/api/cloudsearch/pc")),
    {{"s",query.trimmed()},{"type",QString::number(type)},{"limit",QString::number(limit)},{"offset",QString::number((page-1)*limit)}});
  const auto request=MakeNeteaseRequest(url,cookie);
  http_.request(request,"GET",{},[bus,call,type,limit,cookie](NativeHttpResult response){
    auto result=response.error.isEmpty()?ParseNeteaseSearch(response.body,type):error(response.error);
    if(!result.contains("error")) {
      const auto field=type==1?"songs":"playlists";
      result[field]=result.value(field).toList().mid(0,limit);
      result["cookie"]=MergeNativeCookies(cookie,response.headers);
      qInfo("MCONNECT_NATIVE_NETEASE_SEARCH_OK type=%d count=%d",type,result.value(type==1?"songs":"playlists").toList().size());
    }
    bus.send(call.createReply(QVariantList{json(result)}));
  });
  return {};
}
