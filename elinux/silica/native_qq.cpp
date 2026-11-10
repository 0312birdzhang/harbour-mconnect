#include "native_platforms.h"
#include "platform_models.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDate>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>

namespace {
const QStringList operations{"search","searchPlaylists","stream","qualities","artist","artistSongs","artistAlbums","album","albumSongs","newSongs","toplists","ranked","lyrics","playlist","playlistInfo","liked","profile","vip","userPlaylists","like","add","create","collect","dailyId"};
QVariantMap fail(const QString& error){return {{"backend","cpp-qq-catalog"},{"error",error}};}
QString json(const QVariantMap& data){return QString::fromUtf8(QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact));}
bool matches(const QString& text,const char* pattern){return QRegularExpression(pattern).match(text).hasMatch();}
QString identity(const QVariant& value){return ParseNeteaseArtist({{"id",value}}).id;}
QString first(const QVariantMap& row,const QStringList& fields,const QString& fallback={}){for(const auto& key:fields){const auto v=row.value(key);if(v.isNull() || v.type()==QVariant::Bool)continue;const auto s=v.type()==QVariant::String?v.toString().trimmed():identity(v);if(!s.isEmpty() && s!="null")return s;}return fallback;}
QString image(QString value){if(value.startsWith("//"))value="https:"+value;const QUrl url(value);return url.isValid() && (url.scheme()=="http" || url.scheme()=="https")?value:QString();}
QString albumCover(const QString& id){return id.isEmpty()?QString():"https://y.qq.com/music/photo_new/T002R300x300M000"+id+".jpg";}
QString artistCover(const QString& id){return "https://y.gtimg.cn/music/photo_new/T001R300x300M000"+id+".jpg";}
qint64 integer(const QVariant& value,qint64 fallback=0){bool ok=false;const auto n=value.toLongLong(&ok);return ok && value.type()!=QVariant::Bool && (value.type()!=QVariant::Double || value.toDouble()==double(n))?n:fallback;}
QVariantMap payload(QVariantMap raw){for(const auto& key:QStringList{"songInfo","song","musicData","data"}){const auto row=raw.value(key).toMap();if(row.contains("songmid") || row.contains("songname") || row.contains("mid") || row.contains("songId") || row.contains("singer"))return row;}return raw;}
QVariantMap song(const QVariantMap& source) {
  const auto raw=payload(source);NativeSong value;value.platform="qq";value.id=first(raw,{"mid","songmid","songMid","strMediaMid","id","songid"});value.name=first(raw,{"name","title","songname","songName"});
  for(const auto& item:raw.value("singer").toList()){const auto row=item.toMap();const auto name=first(row,{"name","title"});if(!name.isEmpty())value.artists.append({first(row,{"mid","id"},name),name});}
  const auto album=raw.value("album").toMap();const auto mid=first(album,{"mid"},first(raw,{"albumMid","albummid","album_mid"}));const auto name=first(album,{"name","title"},first(raw,{"albumName","albumname"}));
  value.coverUrl=albumCover(mid);if(value.coverUrl.isEmpty())value.coverUrl=image(first(album,{"cover"},first(raw,{"cover","pic"})));
  if(!name.isEmpty()){value.hasAlbum=true;value.album={mid,name,value.coverUrl};}
  const auto duration=integer(raw.value("interval",raw.value("duration")));value.durationMs=duration>=0 && duration<=604800?duration*1000:0;return value.json();
}
QVariantMap album(const QVariantMap& raw,int style) {
  const auto mid=first(raw,style==2?QStringList{"mid"}:QStringList{"albumMid","albummid"});
  QVariantMap result{{"id",mid},{"name",first(raw,style==0?QStringList{"albumName","albumTranName"}:QStringList{"name","albumName"},QString::fromUtf8("未知专辑"))},{"coverUrl",style==1?image(first(raw,{"pic"},albumCover(mid))):albumCover(mid)}};
  const auto copy=[&](const QString& name,const QStringList& fields){const auto value=first(raw,fields);if(!value.isEmpty())result[name]=value;};
  copy("artistName",style==2?QStringList{"singername"}:(style==1?QStringList{"singer"}:QStringList{"singerName"}));
  if(style==2){copy("artistId",{"singermid"});copy("description",{"desc"});copy("company",{"company"});copy("genre",{"genre"});copy("language",{"lan"});}
  const auto date=QDate::fromString(first(raw,style==2?QStringList{"aDate"}:(style==1?QStringList{"publish_date"}:QStringList{"publishDate"})),"yyyy-MM-dd");if(date.isValid())result["releaseDate"]=date.toString("yyyy-MM-dd")+"T00:00:00.000";
  const auto count=integer(style==2?raw.value("total_song_num",raw.value("cur_song_num")):raw.value("song_num"),-1);if(count>=0)result["songCount"]=count;return result;
}
}
QVariantMap ParseNativeQqResponse(QByteArray body) {
  // QQ search may truncate an unused introduction in the middle of a UTF-8
  // character while declaring charset=utf-8. Repair encoding only; malformed
  // JSON structure and module failures still fail normally.
  const auto utf8=QString::fromUtf8(body.constData(),body.size()).toUtf8();
  if(utf8!=body){qInfo("MCONNECT_NATIVE_QQ_UTF8_REPAIRED");body=utf8;}
  QVariant value;for(int depth=0;depth<3;++depth){QJsonParseError error;auto doc=QJsonDocument::fromJson(body,&error);if(error.error==QJsonParseError::NoError && doc.isObject())return doc.toVariant().toMap();doc=QJsonDocument::fromJson('['+body+']',&error);if(error.error!=QJsonParseError::NoError || !doc.isArray() || doc.array().size()!=1)return {{"error","invalid_response"}};value=doc.array().first().toVariant();if(value.type()!=QVariant::String)return {{"error","invalid_response"}};body=value.toString().toUtf8();}return {{"error","invalid_response"}};
}
namespace {
bool codeOk(const QVariantMap& data){return !data.contains("code") || (data.value("code").type()!=QVariant::Bool && integer(data.value("code"),-999)==0);}
bool valid(const QString& op,const QVariantMap& args) {
  QSet<QString> fields;
  if(op=="search" || op=="searchPlaylists")fields={"query","page","limit"};
  else if(op=="stream")fields={"id","quality"};
  else if(op=="artistSongs" || op=="newSongs")fields={"id","limit"};
  else if(op=="artistAlbums")fields={"id","page","limit"};
  else if(op=="ranked")fields={"id","offset","limit","period"};
  else if(op=="like")fields={"id","like"};
  else if(op=="add")fields={"id","songId"};
  else if(op=="create")fields={"name"};
  else if(op=="collect")fields={"id","collect"};
  else if(op=="dailyId")fields={};
  else if(op!="toplists" && op!="liked")fields={"id"};
  if(args.size()!=fields.size())return false;
  for(auto it=args.begin();it!=args.end();++it){if(!fields.contains(it.key()))return false;const auto key=it.key();const auto value=it.value();
    if(key=="like" || key=="collect"){if(value.type()!=QVariant::Bool)return false;continue;}
    if(QStringList{"page","limit","quality","offset"}.contains(key)){
      const qint64 n=integer(value,-1);if(value.type()==QVariant::String || n<0 || n>10000)return false;if(key=="page" && (n<1 || n>100))return false;if(key=="limit" && n<1)return false;if(key=="quality" && n>7)return false;continue;
    }
    if(value.type()!=QVariant::String)return false;const auto text=value.toString();
    if(key=="id" && !matches(text,"^[A-Za-z0-9_-]{1,64}$"))return false;
    if(key=="songId" && !matches(text,"^[A-Za-z0-9_-]{1,64}$"))return false;
    if(key=="name" && (text.trimmed().isEmpty() || text.size()>200))return false;
    if(key=="query" && (text.trimmed().isEmpty() || text.size()>256))return false;
    if(key=="period" && !matches(text,"^[A-Za-z0-9_-]{0,64}$"))return false;
  }
  if((op=="search" || op=="searchPlaylists") && args.value("limit").toInt()>50)return false;
  if(op=="newSongs" && !matches(args.value("id").toString(),"^[1-6]$"))return false;
  if(op=="ranked" && (!matches(args.value("id").toString(),"^[0-9]{1,9}$") || args.value("offset").toInt()+args.value("limit").toInt()>10000))return false;
  if((op=="playlist" || op=="playlistInfo") && !matches(args.value("id").toString(),"^[0-9]{1,18}$"))return false;
  if((op=="add" || op=="collect") && !matches(args.value("id").toString(),"^[0-9]{1,18}$"))return false;
  if(QStringList{"profile","vip","userPlaylists"}.contains(op) && !matches(args.value("id").toString(),"^[0-9]{1,18}$"))return false;
  return true;
}
class QqJob:public std::enable_shared_from_this<QqJob> {
 public:
  NativePlatformHttp* http=nullptr;QUrl modern,legacy;QString operation,cookie,guid;QVariantMap args;QElapsedTimer elapsed;
  QVariantList rows,positions;QSet<QString> pageHashes;bool fallback=false;int begin=0;
  using Callback=std::function<void(QVariantMap)>;Callback done;
  QString id()const{return args.value("id").toString();}int limit()const{return args.value("limit",100).toInt();}
  void finish(QVariantMap value){value["backend"]="cpp-qq-catalog";if(!value.contains("error")){value["cookie"]=cookie;if(json(value).toUtf8().size()>8*1024*1024){done(fail("response_too_large"));return;}qInfo("MCONNECT_NATIVE_QQ_CATALOG_OK operation=%s",qPrintable(operation));}done(value);}
  void fetch(QUrl url,const QByteArray& method,QByteArray body,Callback callback,bool form=false,bool safeGet=true,bool html=false) {
    const int remaining=18000-int(elapsed.elapsed());if(remaining<=0){callback(fail("timeout"));return;}
    QByteArray referer="https://y.qq.com/";
    if(operation=="profile" || operation=="userPlaylists")referer="https://y.qq.com/portal/profile.html";
    else if(operation=="collect")referer=("https://y.qq.com/n/yqq/playlist/"+id()+".html").toLatin1();
    else if(operation=="add" || operation=="create")referer="https://y.qq.com/n/yqq/playlist";
    QNetworkRequest req(url);req.setRawHeader("User-Agent","Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");req.setRawHeader("Referer",referer);req.setRawHeader("Origin","https://y.qq.com");req.setRawHeader("Accept",html?"text/html":"application/json");if(!cookie.isEmpty())req.setRawHeader("Cookie",cookie.toLatin1());if(method=="POST")req.setHeader(QNetworkRequest::ContentTypeHeader,form?"application/x-www-form-urlencoded":"application/json");
    const auto self=shared_from_this();http->request(req,method,body,[self,callback,html](NativeHttpResult response){if(!response.error.isEmpty()){callback(fail(response.error));return;}if(html){callback({{"html",QString::fromUtf8(response.body)}});return;}// Public QQ reads retain the caller-owned session, as the old QqApi did.
      auto root=ParseNativeQqResponse(response.body);if(!root.contains("error") && !codeOk(root))root=fail("provider_error");callback(root);},remaining,8*1024*1024,safeGet);
  }
  void get(const QString& path,const QList<QPair<QString,QString>>& fields,Callback callback){fetch(NativePlatformUrl(legacy.resolved(QUrl(path)),fields),"GET",{},callback);}
  QVariantMap request(const QString& module,const QString& method,const QVariantMap& params){return {{"module",module},{"method",method},{"param",params}};}
  void music(const QString& key,const QString& module,const QString& method,const QVariantMap& params,Callback callback){
    const auto self=shared_from_this();const QVariantMap body{{"comm",QVariantMap{{"ct",19},{"cv",1845}}},{key,request(module,method,params)}};
    fetch(modern.resolved(QUrl("/cgi-bin/musicu.fcg")),"POST",QJsonDocument::fromVariant(body).toJson(QJsonDocument::Compact),[self,key,callback](QVariantMap root){if(root.contains("error")){callback(root);return;}if(root.value(key).type()!=QVariant::Map){callback(fail("invalid_response"));return;}const auto response=root.value(key).toMap();if(!codeOk(response) || (self->operation=="like" && !response.contains("code"))){callback(fail("provider_error"));return;}const auto data=response.value("data");if(data.type()!=QVariant::Map){callback(fail("invalid_response"));return;}callback(data.toMap());});
  }
  bool append(const QVariant& raw,QVariantList& target,int maximum=10000) {
    if(raw.type()!=QVariant::List || raw.toList().size()>maximum)return false;for(const auto& value:raw.toList()){if(value.type()!=QVariant::Map)return false;const auto parsed=song(value.toMap());if(parsed.value("id").toString().isEmpty() || parsed.value("name").toString().isEmpty())continue;target.append(parsed);}return raw.toList().isEmpty() || !target.isEmpty();
  }
  static QString lyricText(const QVariant& raw) {
    if(raw.type()!=QVariant::String)return {};
    const auto text=raw.toString();
    // Qt 5.6's decoder silently ignores invalid base64; validate first and
    // require a lossless UTF-8 round trip before treating it as encoded text.
    if(!text.isEmpty() && matches(text,"^[A-Za-z0-9+/]*={0,2}$") && text.size()%4==0){
      const auto bytes=QByteArray::fromBase64(text.toLatin1());
      const auto decoded=QString::fromUtf8(bytes.constData(),bytes.size());
      if(decoded.toUtf8()==bytes)return decoded;
    }
    return text;
  }
  void lyrics() {
    const auto self=shared_from_this();
    const auto endpoint=legacy.host()=="127.0.0.1" || legacy.host()=="localhost"?legacy:QUrl("https://i.y.qq.com");
    const auto url=NativePlatformUrl(endpoint.resolved(QUrl("/lyric/fcgi-bin/fcg_query_lyric_new.fcg")),{{"songmid",id()},{"format","json"},{"nobase64","1"}});
    fetch(url,"GET",{},[self,url](QVariantMap root){
      if(root.contains("error")){self->finish(root);return;}
      QVariantMap tracks;const auto lrc=lyricText(root.value("lyric")),translation=lyricText(root.value("trans",root.value("translation",root.value("transLyric"))));
      if(!lrc.trimmed().isEmpty())tracks["lrc"]=lrc;
      if(!translation.trimmed().isEmpty())tracks["translation"]=translation;
      auto qrcUrl=url;QUrlQuery query(qrcUrl);query.addQueryItem("qrc","1");qrcUrl.setQuery(query);
      self->fetch(qrcUrl,"GET",{},[self,tracks](QVariantMap qrc)mutable{
        // Optional QRC failure must never discard an already available LRC.
        const auto text=lyricText(qrc.value("lyric",qrc.value("qrc")));
        const QRegularExpression line("<L\\s+T=\"(\\d+)\"\\s+D=\"(\\d+)\">(.*?)</L>",QRegularExpression::DotMatchesEverythingOption);
        if(!qrc.contains("error") && line.match(text).hasMatch())tracks["qrc"]=text;
        self->finish({{"lyrics",tracks}});
      });
    });
  }
  void playlist(bool legacyPath=false) {
    const auto self=shared_from_this();
    const auto consume=[self,legacyPath](QVariantMap data){
      if(data.contains("error")){if(!legacyPath && self->begin==0 && data.value("error")!="login_required"){self->playlist(true);return;}self->finish(data);return;}
      auto info=data.value("dirinfo").toMap();auto raw=data.value("songlist");
      if(legacyPath){const auto cds=data.value("cdlist").toList();if(!cds.isEmpty()){info=cds.first().toMap();raw=info.value("songlist");}}
      if(self->operation=="playlistInfo") {
        if(info.isEmpty() && !legacyPath){self->playlist(true);return;}
        if(info.isEmpty() && raw.type()!=QVariant::List){self->finish(fail("invalid_response"));return;}
        NativePlaylist playlist;
        playlist.id=self->id();
        playlist.platform="qq";
        playlist.name=first(info,{"title","dissname","name"},QString::fromUtf8("QQ歌单"));
        playlist.coverUrl=image(first(info,{"picurl","logo","coverurl"}));
        playlist.songCount=raw.toList().size();
        for(const auto& field:QStringList{"songnum","song_cnt","song_count","total_song_num","total_song_count","count"}) {
          const auto count=integer(info.value(field),-1);
          if(count>0){playlist.songCount=qMax(playlist.songCount,count);break;}
        }
        self->finish({{"playlists",QVariantList{playlist.json()}}});
        return;
      }
      if(raw.type()!=QVariant::List){if(!legacyPath && self->begin==0){self->playlist(true);return;}self->finish(fail("invalid_response"));return;}
      const auto page=raw.toList();int total=-1;for(const auto& key:QStringList{"songnum","song_cnt","song_count","total_song_num","total_song_count","count"}){const auto n=integer(info.value(key),-1);if(n>0 || (n==0 && page.isEmpty())){total=int(qMin<qint64>(n,10001));break;}}
      if(total>10000){self->finish(fail("response_too_large"));return;}
      if(!legacyPath && self->begin==0 && page.isEmpty() && total!=0){self->playlist(true);return;}
      if(!page.isEmpty()){
        const auto hash=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument::fromVariant(page).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
        if(self->pageHashes.contains(hash)){self->finish(fail("pagination_stalled"));return;}self->pageHashes.insert(hash);
      }
      if(!self->append(page,self->rows) || self->rows.size()>10000){self->finish(fail("invalid_response"));return;}
      self->begin+=page.size();
      if(total>=0 && self->begin>=total){self->finish({{"songs",self->rows}});return;}
      if(legacyPath || page.isEmpty() || (total<0 && page.size()<200)){
        if(total>self->begin){self->finish(fail("incomplete_response"));return;}self->finish({{"songs",self->rows}});return;
      }
      if(self->begin>=10000){self->finish(fail("response_too_large"));return;}self->playlist();
    };
    if(legacyPath){get("/qzone/fcg-bin/fcg_ucc_getcdinfo_byids_cp.fcg",{{"type","1"},{"json","1"},{"utf8","1"},{"onlysong","0"},{"disstid",id()},{"format","json"}},consume);return;}
    music("req_0","playlist.PlayListPlayDetailService","GetPlayListDetail",{{"disstid",id().toLongLong()},{"dirid",0},{"tag",1},{"song_num",200},{"song_begin",begin},{"userinfo",1}},consume);
  }
  void liked() {
    const auto self=shared_from_this();music("req_0","music.musicasset.SongFavRead","GetUserFavSongList",{{"v_songId",0},{"v_begin",0},{"v_num",50}},[self](QVariantMap data){if(data.contains("error")){self->finish(data);return;}if(!self->append(data.value("songlist"),self->rows)){self->finish(fail("invalid_response"));return;}self->finish({{"songs",self->rows}});});
  }
  void profile() {
    const auto self=shared_from_this();get("/rsc/fcgi-bin/fcg_get_profile_homepage.fcg",{{"cid","205360838"},{"userid",id()},{"reqfrom","1"}},[self](QVariantMap data){
      if(data.contains("error")){self->finish(data);return;}
      self->finish({{"user",NormalizeQqProfile(data,self->id())}});
    });
  }
  void userPlaylists() {
    quint32 hash=5381;QString skey;
    for(const auto& field:cookie.split(';')){const auto part=field.trimmed();if(part.startsWith("p_skey=")){skey=part.mid(7);break;}}
    for(const auto ch:skey)hash=hash*33+ch.unicode();
    const auto self=shared_from_this();get("/rsc/fcgi-bin/fcg_user_created_diss",{{"hostUin","0"},{"hostuin",id()},{"sin","0"},{"size","200"},{"g_tk",QString::number(hash&0x7fffffff)},{"loginUin","0"},{"format","json"},{"inCharset","utf8"},{"outCharset","utf-8"},{"notice","0"},{"platform","yqq.json"},{"needNewCode","0"}},[self](QVariantMap root){
      if(root.contains("error")){self->finish(root);return;}
      auto raw=root.value("data").toMap().value("disslist");if(raw.type()!=QVariant::List)raw=root.value("req_0").toMap().value("data").toMap().value("plistlist");
      if(raw.type()!=QVariant::List || raw.toList().size()>1000){self->finish(fail("invalid_response"));return;}
      QVariantList lists;for(const auto& value:raw.toList()){
        if(value.type()!=QVariant::Map){self->finish(fail("invalid_response"));return;}const auto row=value.toMap();NativePlaylist p;
        p.id=first(row,{"disstid","dissid","tid","dirid"});p.name=first(row,{"diss_name","title","dissname","name"});p.platform="qq";
        p.songCount=qMax<qint64>(0,integer(row.value("song_cnt",row.value("songcnt",row.value("song_count")))));
        p.coverUrl=image(first(row,{"diss_cover","dirpicurl","imgurl","cover","picurl"}));p.creatorName=first(row.value("creator").toMap(),{"name","nick"},first(row,{"nickname"}));
        if(p.id.isEmpty() || p.name.isEmpty())continue;auto parsed=p.json();const auto edit=first(row,{"dirid","tid"});if(!edit.isEmpty() && edit!=p.id)parsed["editId"]=edit;lists.append(parsed);
      }self->finish({{"playlists",lists}});
    });
  }
  void vip() {
    const auto self=shared_from_this();music("req_0","music.vip.VipInfo","GetVipInfo",{{"uin",id()}},[self](QVariantMap data){if(data.contains("error")){self->finish(data);return;}const auto level=integer(data.value("vipType"),0);self->finish({{"vipLevel",level>=2?2:level>=1?1:0}});});
  }
  void like() {
    if(cookie.isEmpty()){finish(fail("login_required"));return;}
    const auto self=shared_from_this();music("req_0","music.musicasset.SongFavWrite",args.value("like").toBool()?"AddSongFav":"DeleteSongFav",{{"v_songMid",id()},{"v_songId",id()}},[self](QVariantMap data){if(data.contains("error")){self->finish(data);return;}if((data.contains("result") && integer(data.value("result"),-1)!=0 && integer(data.value("result"),-1)!=100) || !codeOk(data)){self->finish(fail("provider_error"));return;}self->finish({{"ok",true}});});
  }
  QString cookieValue(const QString& key)const {
    for(const auto& part:cookie.split(';')){const auto field=part.trimmed();if(field.startsWith(key+'='))return field.mid(key.size()+1);}return {};
  }
  QString gtk()const {quint32 hash=5381;for(const auto ch:cookieValue("p_skey"))hash=hash*33+ch.unicode();return QString::number(hash&0x7fffffff);}
  QString uin()const {
    for(const auto& key:QStringList{"uin","qqmusic_uin","loginUin","musicid","web_uin","wxuin"}){auto value=cookieValue(key);if(value.startsWith('o'))value.remove(0,1);if(matches(value,"^[0-9]{1,18}$") && value!="0")return value;}return {};
  }
  void writePlaylist() {
    const auto user=uin();if(cookie.isEmpty() || user.isEmpty()){finish(fail("login_required"));return;}
    const auto self=shared_from_this();const auto consume=[self](QVariantMap root){
      if(root.contains("error")){self->finish(root);return;}
      const auto result=integer(root.value("result"),-1);
      if((root.contains("result") && result!=0 && result!=100) ||
         (!(root.contains("code") && codeOk(root)) && result!=100)){self->finish(fail("provider_error"));return;}
      QVariantMap out{{"ok",true}};
      if(self->operation=="create"){
        const auto dir=first(root,{"dirid"},first(root.value("data").toMap(),{"dirid"},first(root,{"id"})));
        if(!matches(dir,"^[0-9]{1,18}$")){self->finish(fail("invalid_response"));return;}out["dirId"]=dir;
      }self->finish(out);
    };
    if(operation=="add"){
      const auto url=NativePlatformUrl(legacy.resolved(QUrl("/splcloud/fcgi-bin/fcg_music_add2songdir.fcg")),{{"g_tk",gtk()},{"midlist",args.value("songId").toString()},{"typelist","13"},{"dirid",id()},{"addtype",""},{"formsender","4"},{"r2","0"},{"r3","1"},{"utf8","1"}});
      // QQ mutates via GET. Never replay it after a network failure or redirect.
      fetch(url,"GET",{},consume,false,false);return;
    }
    QList<QPair<QString,QString>> form{{"loginUin",user},{"hostUin","0"},{"format","json"},{"inCharset",operation=="collect"?"GB2312":"utf8"},{"outCharset","utf8"},{"notice","0"},{"platform","yqq"},{"needNewCode","0"},{"g_tk",gtk()},{"uin",user},{"utf8","1"}};
    QString path;
    if(operation=="create"){
      path="/splcloud/fcgi-bin/create_playlist.fcg";
      form.append(QPair<QString,QString>{"name",args.value("name").toString()});
      form.append(QPair<QString,QString>{"show","1"});
      form.append(QPair<QString,QString>{"formsender","1"});
      form.append(QPair<QString,QString>{"qzreferrer","https://y.qq.com/portal/profile.html#sub=other&tab=create&"});
    } else {
      path="/folder/fcgi-bin/fcg_qm_order_diss.fcg";
      form.append(QPair<QString,QString>{"dissid",id()});
      form.append(QPair<QString,QString>{"from","1"});
      form.append(QPair<QString,QString>{"optype",args.value("collect").toBool()?"1":"2"});
      form.append(QPair<QString,QString>{"qzreferrer","https://y.qq.com/n/yqq/playlist/"+id()+".html"});
    }
    const auto body=NativePlatformUrl(QUrl("https://y.qq.com/"),form).query(QUrl::FullyEncoded).toLatin1();
    fetch(legacy.resolved(QUrl(path)),"POST",body,consume,true);
  }
  void dailyId() {
    if(cookie.isEmpty()){finish({{"dailyId",QVariant()}});return;}
    const auto endpoint=legacy.host()=="127.0.0.1" || legacy.host()=="localhost"?legacy:QUrl("https://y.qq.com");
    const auto self=shared_from_this();fetch(endpoint.resolved(QUrl("/node/musicmac/v6/index.html")),"GET",{},[self](QVariantMap root){
      if(root.contains("error")){self->finish(root);return;}const auto html=root.value("html").toString();const auto marker=QString::fromUtf8("今日私享");QString scope;
      const QRegularExpression item("<li\\b[^>]*playlist__item[^>]*>.*?</li>",QRegularExpression::DotMatchesEverythingOption);auto items=item.globalMatch(html);
      while(items.hasNext()){const auto text=items.next().captured();if(text.contains(marker)){scope=text;break;}}
      if(scope.isEmpty()){const auto index=html.indexOf(marker);if(index>=0){const int start=qMax(0,index-1600);scope=html.mid(start,qMin(html.size(),index+1600)-start);}}
      QString id;for(const auto& pattern:QStringList{"data-rid=[\"']?(\\d+)","dissid[=:][\"']?(\\d+)","playlist/(\\d+)","id=(\\d+)"}){const auto match=QRegularExpression(pattern).match(scope);if(match.hasMatch()){id=match.captured(1);break;}}
      self->finish({{"dailyId",matches(id,"^[0-9]{1,18}$")?QVariant(id):QVariant()}});
    },false,true,true);
  }
  void search(bool mobile=false) {
    const auto self=shared_from_this();music("req_0","music.search.SearchCgiService",mobile?"DoSearchForQQMusicMobile":"DoSearchForQQMusicDesktop",{{"num_per_page",limit()},{"page_num",args.value("page")},{"query",args.value("query")},{"search_type",0}},[self,mobile](QVariantMap data){
      if(data.contains("error")){if(!mobile && data.value("error")=="provider_error"){self->search(true);return;}self->finish(data);return;}
      const auto body=data.value("body").toMap();const auto raw=body.value("song",body.value("item_song")).toMap().value("list");
      if(!self->append(raw,self->rows)){self->finish(fail("invalid_response"));return;}self->finish({{"songs",self->rows}});
    });
  }
  void searchPlaylists() {
    const auto self=shared_from_this();get("/soso/fcgi-bin/client_music_search_songlist",{{"remoteplace","txt.yqq.playlist"},{"page_no",QString::number(args.value("page").toInt()-1)},{"num_per_page",QString::number(limit())},{"query",args.value("query").toString()},{"format","json"}},[self](QVariantMap root){
      if(root.contains("error")){self->finish(root);return;}const auto raw=root.value("list",root.value("data").toMap().value("list"));if(raw.type()!=QVariant::List || raw.toList().size()>1000){self->finish(fail("invalid_response"));return;}
      QVariantList lists;for(const auto& item:raw.toList()){const auto row=item.toMap();NativePlaylist p;p.id=first(row,{"disstid","dissid","tid","dirid"});p.platform="qq";p.name=first(row,{"diss_name","title","dissname","name"});p.songCount=qMax<qint64>(0,integer(row.value("song_cnt",row.value("songcnt",row.value("song_count")))));p.coverUrl=image(first(row,{"diss_cover","dirpicurl","imgurl","cover","picurl"}));p.creatorName=first(row.value("creator").toMap(),{"name","nick"},first(row,{"nickname"}));if(!p.id.isEmpty() && !p.name.isEmpty())lists.append(p.json());}
      self->finish({{"playlists",lists}});
    });
  }
  void stream() {
    QString uin="0";for(const auto& name:QStringList{"qqmusic_uin","uin","loginUin"}){for(const auto& part:cookie.split(';'))if(part.trimmed().startsWith(name+'=')){uin=part.trimmed().mid(name.size()+1);uin.remove(QRegularExpression("[^0-9]"));break;}if(uin!="0" && !uin.isEmpty())break;}
    if(uin.isEmpty())uin="0";const int quality=args.value("quality").toInt();const auto prefix=QStringList{"M500","M800","M800","F000","RS01","Q000","Q001","AI00"}[quality];const auto filename=prefix+id()+id()+(quality<3?".mp3":".flac");
    QVariantMap body{{"comm",QVariantMap{{"ct",19},{"cv",1845},{"uin",uin}}},{"loginUin",uin},{"req_0",request("CDN.SrfCdnDispatchServer","GetCdnDispatch",{{"guid",guid},{"calltype",0},{"userip",""}})},{"req_1",request("vkey.GetVkeyServer","CgiGetVkey",{{"guid",guid},{"songmid",QVariantList{id()}},{"songtype",QVariantList{0}},{"uin",uin},{"loginflag",1},{"platform","20"},{"filename",QVariantList{filename}}})}};
    const auto self=shared_from_this();fetch(modern.resolved(QUrl("/cgi-bin/musicu.fcg")),"POST",QJsonDocument::fromVariant(body).toJson(QJsonDocument::Compact),[self](QVariantMap root){
      if(root.contains("error")){self->finish(root);return;}const auto module=root.value("req_1").toMap();if(!codeOk(module)){self->finish(fail("provider_error"));return;}const auto data=module.value("data").toMap();const auto info=data.value("midurlinfo").toList();if(info.isEmpty() || info.first().toMap().value("purl").toString().isEmpty()){self->finish(fail("unavailable"));return;}
      const auto purl=info.first().toMap().value("purl").toString();const auto hosts=data.value("sip").toList();const auto source=image(QUrl(purl).isRelative()?(hosts.isEmpty()?purl:hosts.first().toString()+purl):purl);if(source.isEmpty()){self->finish(fail("invalid_response"));return;}self->finish({{"url",source}});
    });
  }
  void singer(Callback callback){get("/v8/fcg-bin/fcg_v8_singer_detail_cp.fcg",{{"singermid",id()},{"format","json"},{"utf8","1"},{"outCharset","utf-8"}},callback);}
  void artist(bool songsOnly=false) {
    const auto self=shared_from_this();singer([self,songsOnly](QVariantMap data){if(data.contains("error")){self->finish(data);return;}if(songsOnly){auto raw=data.value("getSongInfo");if(raw.type()!=QVariant::List){self->finish(fail("invalid_response"));return;}auto items=raw.toList();std::stable_sort(items.begin(),items.end(),[](const QVariant& a,const QVariant& b){return integer(a.toMap().value("extra").toMap().value("Flisten_count1"))>integer(b.toMap().value("extra").toMap().value("Flisten_count1"));});if(!self->append(items.mid(0,self->limit()),self->rows)){self->finish(fail("invalid_response"));return;}self->finish({{"songs",self->rows}});return;}
      const auto info=data.value("getSingerInfo").toMap();if(info.isEmpty()){self->finish({{"artist",QVariant()}});return;}NativeArtist p;p.id=first(info,{"Fsinger_mid"},self->id());p.name=first(info,{"Fsinger_name"},first(data,{"singer_name"},self->id()));p.briefDesc=first(data,{"singerBrief"});const auto count=integer(data.value("total_song"),-1);if(count>=0)p.songCount=count;const auto albums=integer(data.value("total_album"),-1);if(albums>=0)p.albumCount=albums;auto result=p.json();result["avatarUrl"]=artistCover(self->id());
      self->music("singer","music.musichallSinger.SingerInfoInter","GetSingerDetail",{{"singer_mids",QVariantList{self->id()}},{"ex_singer",1},{"wiki_singer",1},{"group_singer",1},{"pic",1}},[self,result](QVariantMap modern)mutable{if(modern.value("error")=="login_required"){self->finish(modern);return;}const auto entries=modern.value("singer_list").toList();if(!entries.isEmpty()){const auto raw=entries.first().toMap();const auto direct=image(first(raw.value("pic").toMap(),{"pic","big_black"}));result["avatarUrl"]=direct.isEmpty()?artistCover(first(raw.value("basic_info").toMap(),{"singer_pmid"},self->id())):direct;}self->finish({{"artist",result}});});
    });
  }
  void artistAlbums(bool fallbackSearch=false) {
    const auto self=shared_from_this();if(!fallbackSearch){music("req_0","music.musichallAlbum.AlbumListServer","GetAlbumList",{{"singerMid",id()},{"begin",(args.value("page").toInt()-1)*limit()},{"num",limit()},{"order",1}},[self](QVariantMap data){const auto rows=data.value("albumList");if(rows.type()==QVariant::List && !rows.toList().isEmpty()){self->albums(rows,0);return;}if(data.value("error")=="login_required"){self->finish(data);return;}self->artistAlbums(true);});return;}
    singer([self](QVariantMap profile){if(profile.contains("error")){self->finish(profile);return;}const auto info=profile.value("getSingerInfo").toMap();const auto numeric=first(info,{"Fsinger_id"}),name=first(info,{"Fsinger_name"});self->music("req_0","music.search.SearchCgiService","DoSearchForQQMusicMobile",{{"search_type",2},{"query",name.isEmpty()?self->id():name},{"num_per_page",self->limit()},{"page_num",self->args.value("page")},{"grp",1}},[self,numeric,name](QVariantMap data){if(data.contains("error")){self->finish(data);return;}const auto rows=data.value("body").toMap().value("item_album").toMap().value("list");if(rows.type()!=QVariant::List){self->finish(fail("invalid_response"));return;}QVariantList filtered;for(const auto& value:rows.toList()){const auto row=value.toMap();bool belongs=!numeric.isEmpty() && first(row,{"singer_id"})==numeric;if(!name.isEmpty()){belongs=belongs || first(row,{"singer"})==name;for(const auto& singer:row.value("singer_list").toList())belongs=belongs || first(singer.toMap(),{"name"})==name;}if(belongs)filtered.append(row);}self->albums(filtered.mid(0,self->limit()),1);});});
  }
  void albums(const QVariant& raw,int style){if(raw.type()!=QVariant::List || raw.toList().size()>1000){finish(fail("invalid_response"));return;}QVariantList result;for(const auto& value:raw.toList()){const auto row=album(value.toMap(),style);if(!row.value("id").toString().isEmpty())result.append(row);}finish({{"albums",result}});}
  void albumInfo(){const auto self=shared_from_this();get("/v8/fcg-bin/fcg_v8_album_info_cp.fcg",{{"albummid",id()},{"format","json"},{"utf8","1"},{"outCharset","utf-8"}},[self](QVariantMap root){if(root.contains("error")){self->finish(root);return;}const auto data=root.value("data");if(data.isNull() || data.toMap().isEmpty()){self->finish(self->operation=="album"?QVariantMap{{"album",QVariant()}}:QVariantMap{{"songs",QVariantList()}});return;}if(data.type()!=QVariant::Map){self->finish(fail("invalid_response"));return;}if(self->operation=="album"){self->finish({{"album",album(data.toMap(),2)}});return;}if(!self->append(data.toMap().value("list"),self->rows)){self->finish(fail("invalid_response"));return;}self->finish({{"songs",self->rows}});});}
  void toplists() {
    const auto self=shared_from_this();music("toplist","musicToplist.ToplistInfoServer","GetAll",{},[self](QVariantMap data){if(data.contains("error")){self->finish(data);return;}const auto groups=data.value("group");if(groups.type()!=QVariant::List || groups.toList().size()>100){self->finish(fail("invalid_response"));return;}QVariantList charts;for(const auto& value:groups.toList()){const auto group=value.toMap();for(const auto& item:group.value("toplist").toList()){const auto row=item.toMap();const auto key=identity(row.value("topId"));if(key.isEmpty())continue;QVariantMap chart{{"id",key},{"name",first(row,{"title","titleDetail"},QString::fromUtf8("QQ榜单"))},{"coverUrl",image(first(row,{"frontPicUrl","headPicUrl","mbFrontPicUrl"}))},{"groupName",group.value("groupName").toString()}};for(const auto& field:QList<QPair<QString,QString>>{{"updateFrequency","updateTips"},{"period","period"},{"intro","intro"}}){const auto text=first(row,{field.second});if(!text.isEmpty())chart[field.first]=text;}if(!chart.contains("period")){const auto period=first(row,{"updateTime"});if(!period.isEmpty())chart["period"]=period;}const auto count=integer(row.value("totalNum"),-1);if(count>=0)chart["songCount"]=count;charts.append(chart);if(charts.size()>1000){self->finish(fail("response_too_large"));return;}}}
      const QStringList order{QString::fromUtf8("巅峰榜"),QString::fromUtf8("地区榜"),QString::fromUtf8("特色榜"),QString::fromUtf8("全球榜")};std::stable_sort(charts.begin(),charts.end(),[order](const QVariant& a,const QVariant& b){const int x=order.indexOf(a.toMap().value("groupName").toString()),y=order.indexOf(b.toMap().value("groupName").toString());return (x<0?4:x)<(y<0?4:y);});self->finish({{"charts",charts}});
    });
  }
  void ranked(bool modernPath=false) {
    const auto self=shared_from_this();if(modernPath){music("toplist","musicToplist.ToplistInfoServer","GetDetail",[&](){QVariantMap params{{"topId",id().toInt()},{"offset",args.value("offset")},{"num",limit()}};const auto period=args.value("period").toString();if(!period.isEmpty())params["period"]=period;return params;}(),[self](QVariantMap data){if(data.contains("error")){self->finish(data);return;}const auto songs=data.value("songInfoList");if(songs.type()!=QVariant::List || songs.toList().size()>10000){self->finish(fail("invalid_response"));return;}const auto ranks=data.value("data").toMap().value("song").toList();int i=0;for(const auto& value:songs.toList().mid(0,self->limit())){const auto parsed=song(value.toMap());if(!parsed.value("id").toString().isEmpty() && !parsed.value("name").toString().isEmpty()){const auto r=i<ranks.size()?ranks[i].toMap():QVariantMap();QVariantMap pos{{"rank",integer(r.value("rank"),self->args.value("offset").toInt()+i+1)}};const auto type=integer(r.value("rankType"),-1),number=integer(r.value("rankValue"),-1);if(type==3)pos["rankChange"]=0;else if((type==1 || type==2) && number>=0)pos["rankChange"]=type==1?number:-number;if(type>=0)pos["isNew"]=type==4;const auto hint=first(r,{"rankValue"});if(!hint.isEmpty())pos["rankValue"]=hint;self->rows.append(parsed);self->positions.append(pos);}++i;}self->finish({{"songs",self->rows},{"positions",self->positions}});});return;}
    const int end=args.value("offset").toInt()+limit();const int wanted=qMin(50,end-begin);get("/v8/fcg-bin/fcg_v8_toplist_cp.fcg",{{"topid",id()},{"format","json"},{"page","detail"},{"tpl","3"},{"type","top"},{"song_begin",QString::number(begin)},{"song_num",QString::number(wanted)}},[self,wanted,end](QVariantMap root){
      if(root.contains("error")){if(self->rows.isEmpty() && !self->fallback && root.value("error")!="login_required"){self->fallback=true;self->ranked(true);return;}self->finish(root);return;}const auto raw=root.value("songlist");if(raw.type()!=QVariant::List){self->finish(fail("invalid_response"));return;}const auto page=raw.toList();if(page.isEmpty()){if(self->rows.isEmpty() && !self->fallback){self->fallback=true;self->ranked(true);return;}self->finish({{"songs",self->rows},{"positions",self->positions}});return;}
      const auto hash=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument::fromVariant(page).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());if(self->pageHashes.contains(hash)){self->finish(fail("pagination_stalled"));return;}self->pageHashes.insert(hash);
      bool rankLike=true,movement=false;for(int i=0;i<page.size();++i)rankLike=rankLike && integer(page[i].toMap().value("cur_count"),-1)==self->begin+i+1;if(rankLike)for(const auto& item:page)movement=movement || integer(item.toMap().value("old_count"))>0;
      for(int i=0;i<qMin(wanted,page.size());++i){const auto row=page[i].toMap();const auto parsed=song(row);if(parsed.value("id").toString().isEmpty() || parsed.value("name").toString().isEmpty())continue;const auto current=integer(row.value("cur_count")),old=integer(row.value("old_count"));QVariantMap pos{{"rank",rankLike?current:self->begin+i+1}};if(movement){pos["rankChange"]=old-current;pos["isNew"]=old==0;}const auto hint=first(row,{"Franking_value"});if(!hint.isEmpty())pos["rankValue"]=hint;self->rows.append(parsed);self->positions.append(pos);}
      self->begin+=page.size();if(page.size()<wanted || self->begin>=end){self->finish({{"songs",self->rows},{"positions",self->positions}});return;}self->ranked();
    });
  }
  void start(){const auto self=shared_from_this();begin=args.value("offset").toInt();
    if(operation=="add" || operation=="create" || operation=="collect"){writePlaylist();return;}if(operation=="dailyId"){dailyId();return;}
    if(operation=="profile"){profile();return;}if(operation=="userPlaylists"){userPlaylists();return;}if(operation=="vip"){vip();return;}if(operation=="like"){like();return;}
    if(operation=="lyrics"){lyrics();return;}if(operation=="playlist" || operation=="playlistInfo"){playlist();return;}if(operation=="liked"){liked();return;}
    if(operation=="search"){search();return;}if(operation=="searchPlaylists"){searchPlaylists();return;}if(operation=="stream"){stream();return;}
    if(operation=="qualities"){QVariantList result;for(const auto& pair:QList<QPair<int,int>>{{0,128000},{1,320000},{3,999000},{4,2400000},{5,999000},{6,999000},{7,999000}})result.append(QVariantMap{{"level",pair.first},{"bitrate",pair.second},{"format",pair.first<3?"mp3":"flac"}});finish({{"qualities",result}});return;}
    if(operation=="artist" || operation=="artistSongs"){artist(operation=="artistSongs");return;}if(operation=="artistAlbums"){artistAlbums();return;}if(operation=="album" || operation=="albumSongs"){albumInfo();return;}if(operation=="toplists"){toplists();return;}if(operation=="ranked"){ranked(!args.value("period").toString().isEmpty());return;}
    music("newsong","newsong.NewSongServer","get_new_song_info",{{"type",id().toInt()},{"num",100}},[self](QVariantMap data){if(data.contains("error")){self->finish(data);return;}if(!self->append(data.value("songlist"),self->rows)){self->finish(fail("invalid_response"));return;}self->finish({{"songs",self->rows.mid(0,self->limit())}});});
  }
};
}
QVariantMap NormalizeQqProfile(const QVariantMap& data,const QString& uin) {
  const auto root=data.value("data").type()==QVariant::Map?data.value("data").toMap():data;
  const auto home=root.value("home").toMap();
  const auto pick=[](const QVariantList& values){for(const auto& v:values)if(v.type()==QVariant::Map)return v.toMap();return QVariantMap();};
  const auto creator=pick({root.value("creator"),home.value("creator"),root.value("host"),root.value("user")});
  const auto profile=pick({root.value("profile"),home.value("profile"),root.value("userinfo"),root.value("userInfo")});
  const auto info=pick({root.value("info"),home.value("info"),root.value("base")});
  QString userId=uin,nick,avatar;
  for(const auto& row:QList<QVariantMap>{creator,profile,info,root}) {
    if(userId.isEmpty())userId=first(row,{"uin","userid","user_id","id"});
    if(nick.isEmpty())nick=first(row,{"nick","nickname","nick_name","hostname","name"});
    if(avatar.isEmpty())avatar=image(first(row,{"headpic","headurl","avatar","avatarUrl"}));
  }
  if(!matches(userId,"^[0-9]{1,18}$") || userId=="0")return {};
  return {{"id",userId},{"nickname",nick.isEmpty()?QString::fromUtf8("QQ用户"):nick},{"avatarUrl",avatar},{"platform","qq"},{"vipLevel",0}};
}
QString NativePlatforms::QqCatalog(const QString& operation,const QString& arguments,const QString& cookie){
  bool allowed=operations.contains(operation) && arguments.toUtf8().size()<=16384 && cookie.size()<=32768;for(const auto ch:cookie)allowed=allowed && ch.unicode()>=32 && ch.unicode()<=126;QJsonParseError error;const auto doc=QJsonDocument::fromJson(arguments.toUtf8(),&error);if(!allowed || error.error!=QJsonParseError::NoError || !doc.isObject() || !valid(operation,doc.toVariant().toMap()))return json(fail("invalid_arguments"));if(!calledFromDBus())return json(fail("dbus_required"));
  if(qqGuid_.isEmpty())qqGuid_=QString::number((QDateTime::currentMSecsSinceEpoch()*1000)%9000000000LL+1000000000LL);setDelayedReply(true);const auto bus=connection();const auto call=message();auto job=std::make_shared<QqJob>();job->http=&http_;job->modern=qqEndpoint_;job->legacy=qqLegacy_;job->guid=qqGuid_;job->operation=operation;job->args=doc.toVariant().toMap();job->cookie=cookie;job->done=[bus,call](const QVariantMap& value){bus.send(call.createReply(QVariantList{json(value)}));};job->elapsed.start();job->start();return {};
}
