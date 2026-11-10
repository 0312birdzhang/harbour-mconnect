#include "native_platforms.h"
#include "platform_models.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

QString NativeBiliMixin(const QString& image,const QString& sub) {
  const auto original=image+sub;
  const int order[]={46,47,18,2,53,8,23,32,15,50,10,31,58,3,45,35,27,43,5,49,33,9,42,19,29,28,14,39,12,38,41,13};
  if(original.size()<59)return {};QString result;for(const int i:order)result+=original[i];return result;
}
QMap<QString,QString> NativeBiliSign(QMap<QString,QString> parameters,const QString& mixin,qint64 timestamp) {
  if(mixin.size()!=32)return {};
  for(auto it=parameters.begin();it!=parameters.end();++it)it.value().remove(QRegularExpression("[!'()*]"));
  parameters["wts"]=QString::number(timestamp);QByteArray query;
  for(auto it=parameters.cbegin();it!=parameters.cend();++it){if(!query.isEmpty())query+='&';query+=QUrl::toPercentEncoding(it.key())+'='+QUrl::toPercentEncoding(it.value());}
  parameters["w_rid"]=QString::fromLatin1(QCryptographicHash::hash(query+mixin.toUtf8(),QCryptographicHash::Md5).toHex());return parameters;
}
namespace {
const char* agent="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
const QStringList operations{"search","stream","qualities","qrKey","qrCheck","profile","folders","playlist","like","add","remove","lyrics","share"};
QVariantMap failure(const QString& error){return {{"backend","cpp-bilibili"},{"error",error}};}
QString json(const QVariantMap& value){return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));}
bool match(const QString& s,const char* pattern){return QRegularExpression(pattern).match(s).hasMatch();}
bool numberId(const QString& s){return match(s,"^[0-9]{1,32}$");}
bool videoId(const QString& s){return match(s,"^BV[A-Za-z0-9]{1,30}(:[0-9]{1,32})?$");}
QString identity(const QVariant& value){return ParseNeteaseArtist({{"id",value}}).id;}
QString text(QString value){value.remove(QRegularExpression("<[^>]*>"));return value.replace("&amp;","&").replace("&quot;","\"").replace("&#39;","'").replace("&lt;","<").replace("&gt;",">");}
QString mediaUrl(QString value){if(value.startsWith("//"))value="https:"+value;const QUrl url(value);return url.isValid() && (url.scheme()=="http" || url.scheme()=="https")?value:QString();}
QVariantMap song(const QVariantMap& raw) {
  NativeSong result;result.id=raw.value("bvid").toString();result.platform="bilibili";result.name=text(raw.value("title").toString());
  auto owner=raw.value("owner",raw.value("upper")).toMap();
  result.artists.append({identity(owner.value("mid",raw.value("mid"))),owner.value("name",raw.value("author",QString::fromUtf8("未知UP主"))).toString()});
  qint64 seconds=0;const auto duration=raw.value("duration").toString();bool valid=true;
  for(const auto& part:duration.split(':')){bool ok=false;const auto n=part.toLongLong(&ok);if(!ok || n<0 || n>604800 || seconds>10080){valid=false;break;}seconds=seconds*60+n;}
  result.durationMs=valid && seconds<=604800?seconds*1000:0;result.coverUrl=mediaUrl(raw.value("pic",raw.value("cover")).toString());
  return result.json();
}
bool validArgs(const QString& operation,const QVariantMap& args) {
  QSet<QString> fields;
  if(operation=="search")fields={"query","page","limit"};
  else if(operation=="stream")fields={"id","quality"};
  else if(operation=="qrCheck")fields={"key"};
  else if(operation=="folders")fields={"uid","nickname"};
  else if(operation=="playlist" || operation=="qualities" || operation=="lyrics")fields={"id"};
  else if(operation=="like")fields={"id","enabled"};
  else if(operation=="add" || operation=="remove")fields={"id","songId"};
  else if(operation=="share")fields={"url"};
  if(args.size()!=fields.size())return false;
  for(auto it=args.begin();it!=args.end();++it) {
    if(!fields.contains(it.key()))return false;const auto key=it.key();const auto value=it.value();
    if(key=="page" || key=="limit" || key=="quality") {
      bool ok=false;const int n=value.toInt(&ok);if(!ok || value.type()==QVariant::String || value.type()==QVariant::Bool || value.toDouble()!=n)return false;
      if(key=="page" && (n<1 || n>100))return false;if(key=="limit" && (n<1 || n>50))return false;if(key=="quality" && (n<0 || n>7))return false;continue;
    }
    if(key=="enabled"){if(value.type()!=QVariant::Bool)return false;continue;}
    if(value.type()!=QVariant::String)return false;const auto s=value.toString();
    if(key=="query" && (s.trimmed().isEmpty() || s.size()>256))return false;
    if(key=="nickname" && s.size()>8192)return false;
    if(key=="key" && !match(s,"^[A-Za-z0-9_-]{1,256}$"))return false;
    if(key=="url" && s.size()>2048)return false;
    if(key=="uid" && !numberId(s))return false;
    if(key=="songId" && !videoId(s))return false;
    if(key=="id") {
      if(operation=="playlist"){if(!numberId(s) && !(s.startsWith("video:") && videoId(s.mid(6)) && !s.mid(6).contains(':')))return false;}
      else if(operation=="add" || operation=="remove"){if(!numberId(s))return false;}
      else if(!videoId(s))return false;
    }
  }
  return true;
}
class BiliJob:public std::enable_shared_from_this<BiliJob> {
 public:
  NativePlatformHttp* http=nullptr;QUrl endpoint,passport;std::shared_ptr<NativeBiliState> signing;
  QString operation,cookie;QVariantMap args;QElapsedTimer elapsed;QVariantList rows;QSet<QString> seen,pages;int page=1;QString cid;
  std::function<void(QVariantMap)> done;
  QString cookieValue(const QString& name)const{for(const auto& part:cookie.split(';'))if(part.trimmed().startsWith(name+'='))return part.trimmed().mid(name.size()+1);return {};}
  void setCookie(const QString& name,const QString& value){if(value.isEmpty() || value.size()>4096 || value.contains(';') || value.contains('\r') || value.contains('\n'))return;cookie=MergeNativeCookies(cookie,{{"Set-Cookie",(name+'='+value).toLatin1()}});}
  void finish(QVariantMap result){result["backend"]="cpp-bilibili";if(!result.contains("error")){result["cookie"]=cookie;if(json(result).toUtf8().size()>8*1024*1024){done(failure("response_too_large"));return;}qInfo("MCONNECT_NATIVE_BILIBILI_OK operation=%s",qPrintable(operation));}done(result);}
  using Callback=std::function<void(QVariantMap)>;
  void fetch(QUrl url,const QMap<QString,QString>& params,bool post,Callback callback,bool nav=false,bool credentials=true,bool subtitle=false,bool rejected=false) {
    const int remaining=18000-int(elapsed.elapsed());if(remaining<=0){finish(failure("timeout"));return;}
    QList<QPair<QString,QString>> query;for(auto it=params.cbegin();it!=params.cend();++it)query.append({it.key(),it.value()});
    QByteArray body;
    if(post){for(const auto& field:query){if(!body.isEmpty())body+='&';body+=QUrl::toPercentEncoding(field.first)+'='+QUrl::toPercentEncoding(field.second);}}
    else if(!subtitle)url=NativePlatformUrl(url,query);
    QNetworkRequest request(url);request.setRawHeader("User-Agent",agent);request.setRawHeader("Referer","https://www.bilibili.com/");request.setRawHeader("Accept","application/json");
    if(credentials && !cookie.isEmpty())request.setRawHeader("Cookie",cookie.toLatin1());if(post)request.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
    const auto self=shared_from_this();
    http->request(request,post?QByteArray("POST"):QByteArray("GET"),body,[self,callback,nav,credentials,subtitle,rejected](NativeHttpResult response){
      if(!response.error.isEmpty()){self->finish(failure(response.error));return;}
      if(credentials)self->cookie=MergeNativeCookies(self->cookie,response.headers);
      QJsonParseError error;const auto doc=QJsonDocument::fromJson(response.body,&error);
      if(error.error!=QJsonParseError::NoError || (!doc.isObject() && !(subtitle && doc.isArray()))){self->finish(failure("invalid_response"));return;}
      if(subtitle){callback({{"data",doc.toVariant()}});return;}
      const auto root=doc.toVariant().toMap();const auto rawCode=root.value("code");bool ok=false;const int code=rawCode.toInt(&ok);
      if(!ok || rawCode.type()==QVariant::String || rawCode.type()==QVariant::Bool || rawCode.toDouble()!=code){self->finish(failure("invalid_response"));return;}
      if(code!=0 && !(nav && code==-101) && !(rejected && code!=-101 && code!=-412 && code!=-352)){self->finish(failure(code==-101?"login_required":(code==-412 || code==-352?"verification_required":"provider_error")));return;}
      callback(root);
    },remaining,8*1024*1024);
  }
  void get(const QString& path,QMap<QString,QString> params,Callback callback,bool nav=false){fetch(endpoint.resolved(QUrl(path)),params,false,callback,nav);}
  void signedGet(const QString& path,QMap<QString,QString> params,Callback callback) {
    if(!signing->mixin.isEmpty() && (QDateTime::currentMSecsSinceEpoch()/1000)-signing->timestamp<43200){get(path,NativeBiliSign(params,signing->mixin,(QDateTime::currentMSecsSinceEpoch()/1000)),callback);return;}
    const auto self=shared_from_this();get("/x/web-interface/nav",{},[self,path,params,callback](QVariantMap root){
      const auto images=root.value("data").toMap().value("wbi_img").toMap();
      const auto key=[](const QVariant& value){return QUrl(value.toString()).path().section('/',-1).section('.',0,0);};
      self->signing->mixin=NativeBiliMixin(key(images.value("img_url")),key(images.value("sub_url")));self->signing->timestamp=(QDateTime::currentMSecsSinceEpoch()/1000);
      if(self->signing->mixin.isEmpty()){self->finish(failure("invalid_response"));return;}self->get(path,NativeBiliSign(params,self->signing->mixin,(QDateTime::currentMSecsSinceEpoch()/1000)),callback);
    },true);
  }
  bool appendSongs(const QVariant& value,bool favorite=false) {
    if(value.isNull())return true;if(value.type()!=QVariant::List || value.toList().size()>10000)return false;
    for(const auto& entry:value.toList()) {
      if(entry.type()!=QVariant::Map)return false;const auto raw=entry.toMap();const auto id=raw.value("bvid").toString();
      if(!videoId(id) || (favorite && (raw.value("attr").toInt() & 9)!=0))continue;
      const auto parsed=song(raw);if(parsed.value("name").toString().trimmed().isEmpty())continue;
      if(!seen.contains(id)){rows.append(parsed);seen.insert(id);}if(rows.size()>10000)return false;
    }
    return true;
  }
  void search(bool legacy=false) {
    QMap<QString,QString> params{{"search_type","video"},{"keyword",args.value("query").toString()},{"page",args.value("page").toString()},{"page_size",args.value("limit").toString()},{"order","totalrank"}};
    const auto self=shared_from_this();const auto callback=[self,legacy](QVariantMap root){
      if(root.value("data").type()!=QVariant::Map){self->finish(failure("invalid_response"));return;}const auto data=root.value("data").toMap();
      if(data.contains("v_voucher")){if(!legacy){self->search(true);return;}self->finish(failure("verification_required"));return;}
      if(!data.contains("result") || !self->appendSongs(data.value("result"))){self->finish(failure("invalid_response"));return;}self->finish({{"songs",self->rows}});
    };
    if(legacy)get("/x/web-interface/search/type",params,callback);else{params["platform"]="pc";signedGet("/x/web-interface/wbi/search/type",params,callback);}
  }
  void user(Callback callback) {
    const auto self=shared_from_this();get("/x/web-interface/nav",{},[self,callback](QVariantMap root){
      const auto raw=root.value("data").toMap();if(raw.value("isLogin")!=true){self->finish(failure("login_required"));return;}
      const auto id=identity(raw.value("mid"));if(!numberId(id) || raw.value("uname").type()!=QVariant::String){self->finish(failure("invalid_response"));return;}
      callback({{"id",id},{"nickname",raw.value("uname")},{"avatarUrl",mediaUrl(raw.value("face").toString())},{"platform","bilibili"},{"vipLevel",raw.value("vipStatus").toInt()==1?1:0}});
    });
  }
  void parts(std::function<void()> callback) {
    const auto self=shared_from_this();const auto id=args.value("id").toString();get("/x/player/pagelist",{{"bvid",id.section(':',0,0)}},[self,id,callback](QVariantMap root){
      const auto data=root.value("data");if(data.type()!=QVariant::List){self->finish(failure("invalid_response"));return;}
      const auto desired=id.section(':',1,1);for(const auto& part:data.toList()){const auto key=identity(part.toMap().value("cid"));if(numberId(key) && (desired.isEmpty() || key==desired)){self->cid=key;callback();return;}}
      self->finish(failure("unavailable"));
    });
  }
  void audio() {
    const auto self=shared_from_this();signedGet("/x/player/wbi/playurl",{{"bvid",args.value("id").toString().section(':',0,0)},{"cid",cid},{"fnval","16"},{"fnver","0"},{"qn","80"},{"fourk","0"}},[self](QVariantMap root){
      const auto raw=root.value("data").toMap().value("dash").toMap().value("audio");if(!raw.isNull() && raw.type()!=QVariant::List){self->finish(failure("invalid_response"));return;}
      QList<QVariantMap> tracks;for(const auto& value:raw.toList()){auto row=value.toMap();const auto url=mediaUrl(row.value("baseUrl",row.value("base_url")).toString());if(!url.isEmpty()){row["url"]=url;tracks.append(row);}}
      std::stable_sort(tracks.begin(),tracks.end(),[](const QVariantMap& a,const QVariantMap& b){return a.value("bandwidth").toLongLong()<b.value("bandwidth").toLongLong();});
      if(tracks.isEmpty()){self->finish(failure("unavailable"));return;}
      if(self->operation=="stream"){const int quality=self->args.value("quality").toInt();const int index=quality==0?0:(quality==1?qMin(1,tracks.size()-1):tracks.size()-1);self->finish({{"url",tracks[index].value("url")}});return;}
      QVariantList qualities;for(int i=0;i<qMin(3,tracks.size());++i)qualities.append(QVariantMap{{"level",i},{"bitrate",qMax(0,int(std::round(tracks[i].value("bandwidth").toDouble()/1000)))},{"format","m4a"}});self->finish({{"qualities",qualities}});
    });
  }
  void paged() {
    const bool folders=operation=="folders";
    if(page>(folders?200:500)){finish(failure("response_too_large"));return;}
    const auto self=shared_from_this();
    get(folders?"/x/v3/fav/folder/created/list":"/x/v3/fav/resource/list",folders?QMap<QString,QString>{{"up_mid",args.value("uid").toString()},{"pn",QString::number(page)},{"ps","50"}}:QMap<QString,QString>{{"media_id",args.value("id").toString()},{"pn",QString::number(page)},{"ps","20"},{"platform","web"}},[self,folders](QVariantMap root){
      if(root.value("data").type()!=QVariant::Map){self->finish(failure("invalid_response"));return;}const auto data=root.value("data").toMap();const auto list=data.value(folders?"list":"medias");
      if(!list.isNull() && list.type()!=QVariant::List){self->finish(failure("invalid_response"));return;}
      const auto fingerprint=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument::fromVariant(list).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
      if(folders)for(const auto& item:list.toList()) {
        const auto raw=item.toMap();NativePlaylist p;p.platform="bilibili";p.id=identity(raw.value("id"));p.name=raw.value("title").toString();p.songCount=qMax(0,raw.value("media_count").toInt());p.coverUrl=mediaUrl(raw.value("cover").toString());p.creatorName=self->args.value("nickname").toString();
        if(!numberId(p.id) || p.name.trimmed().isEmpty())continue;if(!self->seen.contains(p.id)){self->rows.append(p.json());self->seen.insert(p.id);}
      } else {if(!self->appendSongs(list,true)){self->finish(failure("invalid_response"));return;}}
      if(self->rows.size()>10000){self->finish(failure("response_too_large"));return;}
      if(data.value("has_more")==true && !list.toList().isEmpty()) {
        if(self->pages.contains(fingerprint)){self->finish(failure("pagination_stalled"));return;}self->pages.insert(fingerprint);++self->page;self->paged();return;
      }
      self->finish({{folders?"playlists":"songs",self->rows}});
    });
  }
  void video(const QString& id,Callback callback){get("/x/web-interface/view",{{"bvid",id.section(':',0,0)}},callback);}
  void videoParts() {
    const auto self=shared_from_this();const auto id=args.value("id").toString().mid(6);video(id,[self,id](QVariantMap root){
      const auto raw=root.value("data");if(raw.type()!=QVariant::Map || raw.toMap().value("pages").type()!=QVariant::List){self->finish(failure("invalid_response"));return;}
      const auto data=raw.toMap();for(const auto& value:data.value("pages").toList()){const auto part=value.toMap();const auto cid=identity(part.value("cid"));if(!numberId(cid)){self->finish(failure("invalid_response"));return;}auto row=data;row["bvid"]=id+":"+cid;row["title"]=part.value("part");row["duration"]=part.value("duration");self->rows.append(song(row));if(self->rows.size()>10000){self->finish(failure("response_too_large"));return;}}
      self->finish({{"songs",self->rows}});
    });
  }
  void lyrics() {
    const auto self=shared_from_this();signedGet("/x/player/wbi/v2",{{"bvid",args.value("id").toString().section(':',0,0)},{"cid",cid}},[self](QVariantMap root){
      const auto subtitles=root.value("data").toMap().value("subtitle").toMap().value("subtitles");
      if(!subtitles.isNull() && subtitles.type()!=QVariant::List){self->finish(failure("invalid_response"));return;}
      if(subtitles.toList().isEmpty()){self->finish({{"lyrics",QVariant()}});return;}
      auto chosen=subtitles.toList().first().toMap();for(const auto& value:subtitles.toList())if(value.toMap().value("lan").toString().contains("zh")){chosen=value.toMap();break;}
      const QUrl url(mediaUrl(chosen.value("subtitle_url").toString()));const auto host=url.host().toLower();
      const bool fixture=self->endpoint.scheme()=="http" && url.scheme()=="http" && url.host()==self->endpoint.host() && url.port()==self->endpoint.port();
      if(!fixture && (url.scheme()!="https" || !(host=="hdslb.com" || host.endsWith(".hdslb.com") || host=="bilibili.com" || host.endsWith(".bilibili.com")))){self->finish(failure("invalid_response"));return;}
      self->fetch(url,{},false,[self](QVariantMap response){
        const auto data=response.value("data");const auto rows=data.type()==QVariant::List?data:data.toMap().value("body");
        if(rows.type()!=QVariant::List || rows.toList().size()>20000){self->finish(failure("invalid_response"));return;}
        QStringList lines;int bytes=0;for(const auto& value:rows.toList()) {
          const auto row=value.toMap();bool ok=false;const double seconds=row.value("from").toDouble(&ok);
          if(!ok || !std::isfinite(seconds) || seconds<0 || seconds>604800 || row.value("content").type()!=QVariant::String){self->finish(failure("invalid_response"));return;}
          const qint64 cs=std::llround(seconds*100);const auto content=row.value("content").toString();bytes+=content.toUtf8().size();if(bytes>2*1024*1024){self->finish(failure("response_too_large"));return;}
          lines.append(QString("[%1:%2.%3]%4").arg(cs/6000,2,10,QChar('0')).arg(cs/100%60,2,10,QChar('0')).arg(cs%100,2,10,QChar('0')).arg(content));
        }
        self->finish({{"lyrics",lines.join('\n')}});
      },false,false,true);
    });
  }
  void write() {
    if(cookieValue("SESSDATA").isEmpty() || cookieValue("bili_jct").isEmpty()){finish(failure("login_required"));return;}
    const auto self=shared_from_this();video(args.value(operation=="like"?"id":"songId").toString(),[self](QVariantMap root){
      const auto aid=identity(root.value("data").toMap().value("aid"));if(!numberId(aid)){self->finish(failure("invalid_response"));return;}
      QMap<QString,QString> fields{{"csrf",self->cookieValue("bili_jct")}};QString path;
      if(self->operation=="like"){path="/x/web-interface/archive/like";fields["aid"]=aid;fields["like"]=self->args.value("enabled").toBool()?"1":"2";}
      else {path="/x/v3/fav/resource/batch-deal";fields["resources"]=aid+":2";fields["add_media_ids"]=self->operation=="add"?self->args.value("id").toString():QString();fields["del_media_ids"]=self->operation=="remove"?self->args.value("id").toString():QString();fields["platform"]="web";}
      self->fetch(self->endpoint.resolved(QUrl(path)),fields,true,[self](QVariantMap root){self->finish({{"ok",root.value("code").toInt()==0}});},false,true,false,true);
    });
  }
  void share() {
    const QUrl url(args.value("url").toString());const auto host=url.host().toLower();if(!url.isValid() || (url.scheme()!="http" && url.scheme()!="https") || !(host=="bilibili.com" || host.endsWith(".bilibili.com"))){finish({{"playlists",QVariantList()}});return;}
    const auto query=QUrlQuery(url);const auto folder=query.queryItemValue("fid",QUrl::FullyDecoded).isEmpty()?query.queryItemValue("media_id",QUrl::FullyDecoded):query.queryItemValue("fid",QUrl::FullyDecoded);const auto self=shared_from_this();
    if(numberId(folder)){get("/x/v3/fav/resource/list",{{"media_id",folder},{"pn","1"},{"ps","1"}},[self,folder](QVariantMap root){const auto info=root.value("data").toMap().value("info").toMap();if(info.isEmpty()){self->finish(failure("invalid_response"));return;}NativePlaylist p;p.id=folder;p.platform="bilibili";p.name=info.value("title",QString::fromUtf8("哔哩哔哩收藏夹")).toString();p.songCount=qMax(0,info.value("media_count").toInt());p.coverUrl=mediaUrl(info.value("cover").toString());self->finish({{"playlists",QVariantList{p.json()}}});});return;}
    const auto found=QRegularExpression("BV[A-Za-z0-9]{1,30}").match(url.path());if(!found.hasMatch()){finish({{"playlists",QVariantList()}});return;}
    const auto id=found.captured();video(id,[self,id](QVariantMap root){const auto data=root.value("data").toMap();if(data.isEmpty()){self->finish(failure("invalid_response"));return;}NativePlaylist p;p.id="video:"+id;p.platform="bilibili";p.name=text(data.value("title").toString());p.songCount=data.value("pages").toList().size();p.coverUrl=mediaUrl(data.value("pic").toString());self->finish({{"playlists",QVariantList{p.json()}}});});
  }
  void start() {
    const auto self=shared_from_this();
    if(operation=="search") {
      if(cookieValue("buvid3").isEmpty()){get("/x/frontend/finger/spi",{},[self](QVariantMap root){const auto data=root.value("data").toMap();self->setCookie("buvid3",data.value("b_3").toString());self->setCookie("buvid4",data.value("b_4").toString());self->search();});return;}search();return;
    }
    if(operation=="stream" || operation=="qualities"){parts([self](){self->audio();});return;}
    if(operation=="lyrics"){parts([self](){self->lyrics();});return;}
    if(operation=="qrKey"){fetch(passport.resolved(QUrl("/x/passport-login/web/qrcode/generate")),{},false,[self](QVariantMap root){const auto data=root.value("data").toMap();const auto key=data.value("qrcode_key").toString();const QUrl url(data.value("url").toString());if(!match(key,"^[A-Za-z0-9_-]{1,256}$") || url.scheme()!="https" || !(url.host()=="bilibili.com" || url.host().endsWith(".bilibili.com"))){self->finish(failure("invalid_response"));return;}self->finish({{"qrKey",key},{"qrUrl",url.toString()}});});return;}
    if(operation=="qrCheck"){fetch(passport.resolved(QUrl("/x/passport-login/web/qrcode/poll")),{{"qrcode_key",args.value("key").toString()}},false,[self](QVariantMap root){const int code=root.value("data").toMap().value("code",-1).toInt();if(code==0){if(self->cookieValue("SESSDATA").isEmpty()){self->finish(failure("missing_session"));return;}self->user([self](QVariantMap user){self->finish({{"qrStatus","success"},{"user",user}});});return;}self->finish({{"qrStatus",code==86090?"scanned":(code==86101?"waiting":(code==86038?"expired":"failed"))}});});return;}
    if(operation=="profile"){user([self](QVariantMap user){self->finish({{"user",user}});});return;}
    if(operation=="folders"){if(cookieValue("SESSDATA").isEmpty()){finish(failure("login_required"));return;}paged();return;}
    if(operation=="playlist"){if(args.value("id").toString().startsWith("video:"))videoParts();else paged();return;}
    if(operation=="like" || operation=="add" || operation=="remove"){write();return;}
    share();
  }
};
}
QString NativePlatforms::Bilibili(const QString& operation,const QString& arguments,const QString& cookie) {
  bool valid=operations.contains(operation) && arguments.toUtf8().size()<=16384 && cookie.size()<=32768;for(const auto ch:cookie)valid=valid && ch.unicode()>=32 && ch.unicode()<=126;
  QJsonParseError error;const auto doc=QJsonDocument::fromJson(arguments.toUtf8(),&error);
  if(!valid || error.error!=QJsonParseError::NoError || !doc.isObject() || !validArgs(operation,doc.toVariant().toMap()))return json(failure("invalid_arguments"));
  if(!calledFromDBus())return json(failure("dbus_required"));
  setDelayedReply(true);const auto bus=connection();const auto call=message();auto job=std::make_shared<BiliJob>();job->http=&http_;job->endpoint=biliEndpoint_;job->passport=biliPassport_;job->signing=biliState_;job->operation=operation;job->args=doc.toVariant().toMap();job->cookie=cookie;
  job->done=[bus,call](const QVariantMap& result){bus.send(call.createReply(QVariantList{json(result)}));};job->elapsed.start();job->start();return {};
}
