// Isolated D-Bus + local HTTP; no real login or account mutations.
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
#define CHECK(x) do{if(!(x)){fprintf(stderr,"native QQ failed at %d\n",__LINE__);return 1;}}while(false)
QString requestCookie="qqmusic_uin=o12345; p_skey=test";
QVariantMap call(QDBusConnection& bus,const QString& op,const QVariantMap& args={}) {
  auto msg=QDBusMessage::createMethodCall("org.sailfishos.Mconnect.Silica","/org/sailfishos/Mconnect/Platforms","org.sailfishos.Mconnect.Platforms","QqCatalog");msg<<op<<QString::fromUtf8(QJsonDocument::fromVariant(args).toJson(QJsonDocument::Compact))<<requestCookie;
  QDBusPendingCallWatcher watcher(bus.asyncCall(msg,20000));QEventLoop loop;QObject::connect(&watcher,&QDBusPendingCallWatcher::finished,&loop,&QEventLoop::quit);loop.exec();QDBusPendingReply<QString> reply=watcher;
  return reply.isError()?QVariantMap{{"error","dbus_error"}}:QJsonDocument::fromJson(reply.value().toUtf8()).toVariant().toMap();
}
QVariantMap song(int n){return {{"songmid",QString("mid%1").arg(n)},{"songname",QString::fromUtf8("中文歌曲")},{"songid",1234567890123LL},{"singer",QVariantList{QVariantMap{{"mid","singerMid"},{"name",QString::fromUtf8("歌手")}}}},{"albummid","albumMid"},{"albumname",QString::fromUtf8("专辑")},{"interval",321}};}
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);auto bus=QDBusConnection::sessionBus();auto client=QDBusConnection::connectToBus(QDBusConnection::SessionBus,"qq-client");CHECK(bus.registerService("org.sailfishos.Mconnect.Silica"));
  if(argc>1 && QByteArray(argv[1])=="--live-extra") {
    requestCookie.clear();NativePlatforms service;CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));
    const auto lists=call(client,"searchPlaylists",{{"query",QString::fromUtf8("周杰伦")},{"page",1},{"limit",3}});if(lists.contains("error")){fprintf(stderr,"QQ_LIVE_PLAYLIST_ERROR %s\n",qPrintable(lists.value("error").toString()));return 2;}CHECK(!lists.value("playlists").toList().isEmpty());
    const auto top=call(client,"toplists");QString period;for(const auto& row:top.value("charts").toList())if(row.toMap().value("id")=="26")period=row.toMap().value("period").toString();CHECK(!period.isEmpty());
    const auto current=call(client,"ranked",{{"id","26"},{"offset",0},{"limit",3},{"period",period}});if(current.contains("error")){fprintf(stderr,"QQ_LIVE_PERIOD_ERROR %s\n",qPrintable(current.value("error").toString()));return 3;}CHECK(current.value("songs").toList().size()==3);
    const auto songs=call(client,"artistSongs",{{"id","001BLpXF2DyJe2"},{"limit",5}});CHECK(!songs.contains("error") && !songs.value("songs").toList().isEmpty());
    for(int region=1;region<=6;++region){const auto news=call(client,"newSongs",{{"id",QString::number(region)},{"limit",3}});CHECK(!news.contains("error") && news.value("songs").toList().size()==3);}
    const auto mid=songs.value("songs").toList().first().toMap().value("id").toString();const auto stream=call(client,"stream",{{"id",mid},{"quality",0}});CHECK(!stream.value("url").toString().isEmpty() || stream.value("error")=="unavailable");
    printf("NATIVE_QQ_LIVE_EXTRA_PASS playlists=%d periodTracks=%d artistSongs=%d regions=6 streamResolved=%d\n",lists.value("playlists").toList().size(),current.value("songs").toList().size(),songs.value("songs").toList().size(),!stream.value("url").toString().isEmpty());return 0;
  }
  if(argc>1 && QByteArray(argv[1])=="--live") {
    requestCookie.clear();
    NativePlatforms service;CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));
    const auto charts=call(client,"toplists");if(charts.contains("error")){fprintf(stderr,"QQ_LIVE_CHARTS_ERROR %s\n",qPrintable(charts.value("error").toString()));return 2;}CHECK(!charts.value("charts").toList().isEmpty());
    const auto ranked=call(client,"ranked",{{"id","26"},{"offset",0},{"limit",100},{"period",""}});CHECK(!ranked.contains("error") && ranked.value("songs").toList().size()==100);
    const auto artist=call(client,"artist",{{"id","001BLpXF2DyJe2"}});CHECK(!artist.contains("error") && !artist.value("artist").toMap().isEmpty());
    const auto albums=call(client,"artistAlbums",{{"id","001BLpXF2DyJe2"},{"page",1},{"limit",3}});CHECK(!albums.contains("error") && !albums.value("albums").toList().isEmpty());
    const auto id=albums.value("albums").toList().first().toMap().value("id").toString();const auto album=call(client,"album",{{"id",id}}),tracks=call(client,"albumSongs",{{"id",id}});CHECK(!album.contains("error") && !tracks.contains("error") && !tracks.value("songs").toList().isEmpty());
    const auto news=call(client,"newSongs",{{"id","1"},{"limit",3}});CHECK(!news.contains("error") && news.value("songs").toList().size()==3);
    const auto search=call(client,"search",{{"query",QString::fromUtf8("周杰伦")},{"page",1},{"limit",3}});if(search.contains("error"))fprintf(stderr,"QQ_LIVE_SEARCH_ERROR %s\n",qPrintable(search.value("error").toString()));
    printf("NATIVE_QQ_LIVE_PASS charts=%d ranked=%d albums=%d tracks=%d search=%d\n",charts.value("charts").toList().size(),ranked.value("songs").toList().size(),albums.value("albums").toList().size(),tracks.value("songs").toList().size(),search.value("songs").toList().size());return search.contains("error")?3:0;
  }
  QTcpServer fixture;CHECK(fixture.listen(QHostAddress::LocalHost));QMap<QString,int> counts;QMap<QString,QVariantMap> params;
  bool malformedUtf8=false, encryptedQrc=false, qrcFail=false, playlistFail=false, playlistStall=false, playlistIncomplete=false, playlistZeroCount=false, playlistLaterFail=false, accountReject=false;
  int vipType=2;bool missingWriteCode=false,dataWriteReject=false;
  bool writeFail=false,writeReject=false,writeMissingCode=false,createMissingDir=false,addRedirect=false,dailyMissing=false;
  bool valid=true,albumFallback=false,searchFallback=false,legacyFail=false,modernFail=false,stall=false,emptyStream=false,doubleEncoded=false,postFail=false;QString period;
  QObject::connect(&fixture,&QTcpServer::newConnection,&app,[&](){while(fixture.hasPendingConnections()){
    auto* socket=fixture.nextPendingConnection();auto buffer=std::make_shared<QByteArray>();auto started=std::make_shared<bool>(false);QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
    QObject::connect(socket,&QTcpSocket::readyRead,&app,[&,socket,buffer,started](){
      if(*started)return;*buffer+=socket->readAll();const int header=buffer->indexOf("\r\n\r\n");if(header<0)return;int length=0;for(const auto& line:buffer->left(header).split('\n'))if(line.toLower().startsWith("content-length:"))length=line.mid(15).trimmed().toInt();if(buffer->size()<header+4+length)return;*started=true;
      const auto pieces=buffer->left(buffer->indexOf("\r\n")).split(' ');const QUrl url(QString("http://localhost")+QString::fromLatin1(pieces[1].constData(),pieces[1].size()));const auto path=url.path();const QUrlQuery query(url);++counts[path];QVariantMap root{{"code",0}};bool httpError=false;
      if(path=="/cgi-bin/musicu.fcg") {
        const auto request=QJsonDocument::fromJson(buffer->mid(header+4)).toVariant().toMap();valid=valid && pieces[0]=="POST";httpError=postFail;
        for(auto it=request.begin();it!=request.end();++it){if(it.value().type()!=QVariant::Map || !it.value().toMap().contains("method"))continue;const auto req=it.value().toMap();const auto method=req.value("method").toString();const auto p=req.value("param").toMap();params[method]=p;++counts[method];QVariantMap data;int code=0;
          if(method=="DoSearchForQQMusicDesktop" || method=="DoSearchForQQMusicMobile") {
            if(searchFallback && method=="DoSearchForQQMusicDesktop")code=2001;
            QVariantList results;
            if(p.value("search_type").toInt()==2)results={QVariantMap{{"albummid","fallbackAlbum"},{"name",QString::fromUtf8("专辑")},{"singer_id",301},{"singer",QString::fromUtf8("歌手")},{"publish_date","2020-01-02"}},QVariantMap{{"albummid","wrongArtist"},{"name","wrong"},{"singer_id",999},{"singer","other"}}};
            else results={song(1),song(2)};
            QVariantMap item;item["list"]=results;QVariantMap body;
            body[p.value("search_type").toInt()==2?QString("item_album"):(method=="DoSearchForQQMusicMobile"?QString("item_song"):QString("song"))]=item;
            data["body"]=body;

          } else if(method=="GetCdnDispatch")data={{"sip",QVariantList{"https://audio.test/"}}};
          else if(method=="CgiGetVkey") {valid=valid && p.value("uin")=="12345" && p.value("guid")==request.value("req_0").toMap().value("param").toMap().value("guid");data={{"sip",QVariantList{"https://audio.test/"}},{"midurlinfo",emptyStream?QVariantList():QVariantList{QVariantMap{{"purl","file.mp3?vkey=test"}}}}};}
          else if(method=="GetSingerDetail")data={{"singer_list",QVariantList{QVariantMap{{"basic_info",QVariantMap{{"singer_pmid","avatarPMid"}}}}}}};
          else if(method=="GetAlbumList") {if(albumFallback)code=500003;data={{"albumList",QVariantList{QVariantMap{{"albumMid",QString("album%1").arg(p.value("begin").toInt())},{"albumName",QString::fromUtf8("专辑")},{"singerName",QString::fromUtf8("歌手")},{"publishDate","2020-01-02"}}}}};}
          else if(method=="get_new_song_info")data={{"songlist",QVariantList{QVariantMap{{"musicData",song(1)}},song(2),song(3)}}};
          else if(method=="GetAll") {
            QVariantMap world{{"groupName",QString::fromUtf8("全球榜")}},peak{{"groupName",QString::fromUtf8("巅峰榜")}};
            world["toplist"]=QVariantList{QVariantMap{{"topId",108},{"title","Billboard"},{"totalNum",100},{"period","2026_40"}}};
            peak["toplist"]=QVariantList{QVariantMap{{"topId",26},{"title",QString::fromUtf8("热歌榜")},{"totalNum",300},{"updateTips",QString::fromUtf8("每日更新")}}};
            data["group"]=QVariantList{world,peak};
          }
          else if(method=="GetDetail") {if(modernFail)code=500003;period=p.value("period").toString();QVariantList songs,positions;for(int i=0;i<qMin(3,p.value("num").toInt());++i){songs.append(song(i+101));positions.append(QVariantMap{{"rank",p.value("offset").toInt()+i+1},{"rankType",i==2?4:i+1},{"rankValue",3}});}data={{"songInfoList",songs},{"data",QVariantMap{{"song",positions}}}};}
          else if(method=="GetPlayListDetail") {
            if(playlistFail || (playlistLaterFail && p.value("song_begin").toInt()>0))code=500003;
            const int start=playlistStall?0:p.value("song_begin").toInt();QVariantList songs;
            for(int i=start;i<qMin(start+200,405);++i)songs.append(song(i+1));
            data["songlist"]=songs;data["dirinfo"]=QVariantMap{{"songnum",playlistZeroCount?0:405},{"title",QString::fromUtf8("中文歌单")},{"picurl","//image.test/list"}};
          }
          else if(method=="GetUserFavSongList")data["songlist"]=QVariantList{song(42)};
          else if(method=="GetVipInfo") {valid=valid && p.value("uin")=="12345";data["vipType"]=vipType;}
          else if(method=="AddSongFav" || method=="DeleteSongFav") {valid=valid && p.value("v_songMid")=="songMid" && p.value("v_songId")=="songMid";if(accountReject)code=500003;if(dataWriteReject)data["result"]=7;}
          else valid=false;
          root[it.key()]=QVariantMap{{"code",code},{"data",data}};
          if(missingWriteCode && (method=="AddSongFav" || method=="DeleteSongFav"))root[it.key()]=QVariantMap{{"data",data}};
        }
      } else if(path=="/v8/fcg-bin/fcg_v8_singer_detail_cp.fcg") {
        root["getSingerInfo"]=QVariantMap{{"Fsinger_mid","singerMid"},{"Fsinger_name",QString::fromUtf8("歌手")},{"Fsinger_id",301}};root["singerBrief"]=QString::fromUtf8("中文简介");root["total_song"]="100";root["total_album"]="50";root["getSongInfo"]=QVariantList{QVariantMap{{"songInfo",song(1)},{"extra",QVariantMap{{"Flisten_count1",10}}}},QVariantMap{{"songInfo",song(2)},{"extra",QVariantMap{{"Flisten_count1",100}}}}};
      } else if(path=="/v8/fcg-bin/fcg_v8_album_info_cp.fcg") {
        QVariantMap data{{"mid","albumMid"},{"name",QString::fromUtf8("中文专辑")},{"aDate","2003-04-23"},{"singername",QString::fromUtf8("歌手")},{"singermid","singerMid"},{"company","record"},{"genre","pop"},{"lan",QString::fromUtf8("国语")},{"desc",QString::fromUtf8("简介")},{"total_song_num",2}};
        data["list"]=QVariantList{song(1),QVariantMap{{"data",song(2)}}};root["data"]=data;
      }
      else if(path=="/soso/fcgi-bin/client_music_search_songlist")root["list"]=QVariantList{QVariantMap{{"disstid",1234567890123LL},{"dissname",QString::fromUtf8("中文歌单")},{"song_cnt",5},{"imgurl","//image.test/list"},{"creator",QVariantMap{{"nick","creator"}}}}};
      else if(path=="/v8/fcg-bin/fcg_v8_toplist_cp.fcg") {
        httpError=legacyFail;const int begin=stall?0:query.queryItemValue("song_begin").toInt(),num=query.queryItemValue("song_num").toInt();valid=valid && num<=50;QVariantList rows;for(int i=0;i<num;++i){const int n=begin+i+1;rows.append(QVariantMap{{"data",song(n)},{"cur_count",query.queryItemValue("topid")=="4"?9999-n:n},{"old_count",n==2?0:n+2},{"Franking_value",QString::number(n)}});}root["songlist"]=rows;
      } else if(path=="/lyric/fcgi-bin/fcg_query_lyric_new.fcg") {
        valid=valid && query.queryItemValue("songmid")=="songMid" && query.queryItemValue("nobase64")=="1";
        if(query.queryItemValue("qrc")=="1"){
          httpError=qrcFail;root["lyric"]=encryptedQrc?QString("deadbeefcafebabe"):QString("<L T=\"100\" D=\"200\"><P T=\"100\" D=\"200\">word</P></L>");
        } else {root["lyric"]=QString::fromLatin1(QString::fromUtf8("[00:01.00]中文歌词").toUtf8().toBase64());root["trans"]=QString("[00:01.00]translation");}
      } else if(path=="/qzone/fcg-bin/fcg_ucc_getcdinfo_byids_cp.fcg") {
        root["cdlist"]=QVariantList{QVariantMap{{"songlist",QVariantList{song(1),song(2)}},{"songnum",playlistIncomplete?5:2},{"dissname",QString::fromUtf8("旧接口歌单")},{"logo","//image.test/legacy"}}};
      } else if(path=="/rsc/fcgi-bin/fcg_get_profile_homepage.fcg") {
        valid=valid && query.queryItemValue("userid")=="12345" && buffer->contains("https://y.qq.com/portal/profile.html");
        root["data"]=QVariantMap{{"home",QVariantMap{{"creator",QVariantMap{{"nick",QString::fromUtf8("中文用户")},{"headurl","//image.test/user"}}}}}};
      } else if(path=="/rsc/fcgi-bin/fcg_user_created_diss") {
        quint32 hash=5381;for(const auto ch:QString("test"))hash=hash*33+ch.unicode();
        valid=valid && query.queryItemValue("hostuin")=="12345" && query.queryItemValue("g_tk")==QString::number(hash&0x7fffffff);
        root["data"]=QVariantMap{{"disslist",QVariantList{QVariantMap{{"disstid",1234567890123LL},{"dirid",42},{"diss_name",QString::fromUtf8("我的歌单")},{"song_cnt",10},{"creator",QVariantMap{{"nick",QString::fromUtf8("中文用户")}}}}}}};
      } else if(path=="/splcloud/fcgi-bin/fcg_music_add2songdir.fcg") {
        valid=valid && pieces[0]=="GET" && query.queryItemValue("midlist")=="songMid" && query.queryItemValue("dirid")=="42" && query.queryItemValue("g_tk")=="2090756197";
        httpError=writeFail;root["code"]=writeReject?7:0;if(writeMissingCode)root.remove("code");
      } else if(path=="/splcloud/fcgi-bin/create_playlist.fcg" || path=="/folder/fcgi-bin/fcg_qm_order_diss.fcg") {
        const QUrlQuery form(QString::fromUtf8(buffer->mid(header+4)));valid=valid && pieces[0]=="POST" && buffer->contains("application/x-www-form-urlencoded") && form.queryItemValue("uin")=="12345" && form.queryItemValue("g_tk")=="2090756197";
        if(path.contains("create_playlist")){valid=valid && form.queryItemValue("name",QUrl::FullyDecoded)==QString::fromUtf8("中文 A+B & #");if(!createMissingDir)root["dirid"]=42;}
        else {valid=valid && form.queryItemValue("dissid")=="123" && (form.queryItemValue("optype")=="1" || form.queryItemValue("optype")=="2");params["collect"]={{"optype",form.queryItemValue("optype")}};}
        root["code"]=writeReject?7:0;httpError=writeFail;if(writeMissingCode)root.remove("code");
      } else if(path=="/node/musicmac/v6/index.html") {
        const auto body=dailyMissing?QByteArray("<html>no daily card</html>"):QString::fromUtf8("<li class='playlist__item' data-rid='999'>其他推荐</li><li class='playlist__item' data-rid='1234567890123'>今日私享</li>").toUtf8();
        socket->write("HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();return;
      } else valid=false;
      if(addRedirect && path=="/splcloud/fcgi-bin/fcg_music_add2songdir.fcg") {socket->write("HTTP/1.1 302 Found\r\nLocation: /replayed-write\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");socket->disconnectFromHost();return;}
      auto body=QJsonDocument::fromVariant(root).toJson(QJsonDocument::Compact);if(malformedUtf8){body.chop(1);body+=",\"unusedIntroduction\":\"";body+=QByteArray::fromHex("e5be");body+=".\"}";}if(doubleEncoded){QJsonArray wrapper{QString::fromUtf8(body)};const auto encoded=QJsonDocument(wrapper).toJson(QJsonDocument::Compact);body=encoded.mid(1,encoded.size()-2);}
      socket->write(httpError?"HTTP/1.1 503 Unavailable\r\n":"HTTP/1.1 200 OK\r\n");socket->write("Set-Cookie: trace=test; Path=/\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();
    });
  }});
  const QUrl base(QString("http://127.0.0.1:%1").arg(fixture.serverPort()));NativePlatforms service(nullptr,QUrl("https://music.163.com"),QUrl("https://api.bilibili.com"),QUrl("https://passport.bilibili.com"),base,base);CHECK(bus.registerObject("/org/sailfishos/Mconnect/Platforms",&service,QDBusConnection::ExportAllSlots));
  const QVariantMap searchArgs{{"query",QString::fromUtf8("歌曲 A+B & #")},{"page",2},{"limit",5}};
  auto result=call(client,"search",searchArgs);CHECK(result.value("songs").toList().size()==2 && result.value("songs").toList().first().toMap().value("platform")=="qq");CHECK(!result.value("cookie").toString().contains("trace=test") && result.value("cookie").toString().contains("p_skey=test"));
  searchFallback=true;CHECK(call(client,"search",searchArgs).value("songs").toList().size()==2 && counts["DoSearchForQQMusicMobile"]==1);searchFallback=false;
  doubleEncoded=true;CHECK(call(client,"searchPlaylists",searchArgs).value("playlists").toList().first().toMap().value("id")=="1234567890123");doubleEncoded=false;
  malformedUtf8=true;CHECK(call(client,"searchPlaylists",searchArgs).value("playlists").toList().size()==1);malformedUtf8=false;
  CHECK(call(client,"stream",{{"id","songMid"},{"quality",7}}).value("url")=="https://audio.test/file.mp3?vkey=test");CHECK(params["CgiGetVkey"].value("filename").toList().first()=="AI00songMidsongMid.flac");emptyStream=true;CHECK(call(client,"stream",{{"id","songMid"},{"quality",0}}).value("error")=="unavailable");emptyStream=false;
  CHECK(call(client,"qualities",{{"id","songMid"}}).value("qualities").toList().size()==7);
  result=call(client,"artist",{{"id","singerMid"}});CHECK(result.value("artist").toMap().value("songCount").toInt()==100 && result.value("artist").toMap().value("avatarUrl").toString().contains("avatarPMid"));
  CHECK(call(client,"artistSongs",{{"id","singerMid"},{"limit",1}}).value("songs").toList().first().toMap().value("id")=="mid2");
  CHECK(call(client,"artistAlbums",{{"id","singerMid"},{"page",2},{"limit",5}}).value("albums").toList().first().toMap().value("id")=="album5");
  albumFallback=true;CHECK(call(client,"artistAlbums",{{"id","singerMid"},{"page",2},{"limit",5}}).value("albums").toList().size()==1 && params["DoSearchForQQMusicMobile"].value("page_num").toInt()==2);albumFallback=false;
  result=call(client,"album",{{"id","albumMid"}});CHECK(result.value("album").toMap().value("language")==QString::fromUtf8("国语") && result.value("album").toMap().value("releaseDate")=="2003-04-23T00:00:00.000");CHECK(call(client,"albumSongs",{{"id","albumMid"}}).value("songs").toList().size()==2);
  for(int area=1;area<=6;++area)CHECK(call(client,"newSongs",{{"id",QString::number(area)},{"limit",2}}).value("songs").toList().size()==2 && params["get_new_song_info"].value("type").toInt()==area);
  const auto charts=call(client,"toplists").value("charts").toList();CHECK(charts.size()==2 && charts.first().toMap().value("id")=="26" && charts[1].toMap().value("period")=="2026_40");
  const QVariantMap hot{{"id","26"},{"offset",0},{"limit",300},{"period",""}};
  const int before=counts["/v8/fcg-bin/fcg_v8_toplist_cp.fcg"];result=call(client,"ranked",hot);CHECK(result.value("songs").toList().size()==300 && counts["/v8/fcg-bin/fcg_v8_toplist_cp.fcg"]==before+6);CHECK(result.value("positions").toList()[1].toMap().value("isNew").toBool() && result.value("positions").toList().last().toMap().value("rank").toInt()==300);
  result=call(client,"ranked",{{"id","4"},{"offset",50},{"limit",5},{"period",""}});CHECK(result.value("positions").toList().first().toMap().value("rank").toInt()==51 && !result.value("positions").toList().first().toMap().contains("rankChange"));
  legacyFail=true;result=call(client,"ranked",{{"id","26"},{"offset",0},{"limit",3},{"period",""}});CHECK(result.value("positions").toList()[1].toMap().value("rankChange").toInt()==-3);legacyFail=false;
  result=call(client,"ranked",{{"id","58"},{"offset",10},{"limit",3},{"period","2026_40"}});CHECK(period=="2026_40" && result.value("positions").toList().first().toMap().value("rank").toInt()==11);
  modernFail=true;const int calls=counts["/v8/fcg-bin/fcg_v8_toplist_cp.fcg"];CHECK(call(client,"ranked",{{"id","58"},{"offset",0},{"limit",3},{"period","2026_40"}}).value("error")=="provider_error" && counts["/v8/fcg-bin/fcg_v8_toplist_cp.fcg"]==calls);modernFail=false;
  stall=true;CHECK(call(client,"ranked",hot).value("error")=="pagination_stalled");stall=false;
  const int posts=counts["DoSearchForQQMusicDesktop"];postFail=true;CHECK(call(client,"search",searchArgs).value("error")=="network_error" && counts["DoSearchForQQMusicDesktop"]==posts+1);postFail=false;
  result=call(client,"lyrics",{{"id","songMid"}});CHECK(result.value("lyrics").toMap().value("lrc")==QString::fromUtf8("[00:01.00]中文歌词") && result.value("lyrics").toMap().contains("qrc"));
  encryptedQrc=true;CHECK(!call(client,"lyrics",{{"id","songMid"}}).value("lyrics").toMap().contains("qrc"));encryptedQrc=false;
  qrcFail=true;CHECK(call(client,"lyrics",{{"id","songMid"}}).value("lyrics").toMap().contains("lrc"));qrcFail=false;
  result=call(client,"playlist",{{"id","1234567890123"}});CHECK(result.value("songs").toList().size()==405 && params["GetPlayListDetail"].value("disstid").toLongLong()==1234567890123LL && params["GetPlayListDetail"].value("song_begin").toInt()==400);
  const int metadataBefore=counts["GetPlayListDetail"];
  result=call(client,"playlistInfo",{{"id","1234567890123"}});
  const auto metadata=result.value("playlists").toList().first().toMap();
  CHECK(metadata.value("id")=="1234567890123" && metadata.value("name")==QString::fromUtf8("中文歌单") && metadata.value("songCount").toInt()==405 && metadata.value("coverUrl")=="https://image.test/list");
  CHECK(counts["GetPlayListDetail"]==metadataBefore+1 && params["GetPlayListDetail"].value("song_begin").toInt()==0 && !result.contains("songs"));
  playlistStall=true;CHECK(call(client,"playlist",{{"id","123"}}).value("error")=="pagination_stalled");playlistStall=false;
  playlistZeroCount=true;CHECK(call(client,"playlist",{{"id","123"}}).value("songs").toList().size()==405);playlistZeroCount=false;
  const int legacyBefore=counts["/qzone/fcg-bin/fcg_ucc_getcdinfo_byids_cp.fcg"];
  playlistLaterFail=true;CHECK(call(client,"playlist",{{"id","123"}}).value("error")=="provider_error" && counts["/qzone/fcg-bin/fcg_ucc_getcdinfo_byids_cp.fcg"]==legacyBefore);playlistLaterFail=false;
  playlistFail=true;CHECK(call(client,"playlist",{{"id","123"}}).value("songs").toList().size()==2);
  result=call(client,"playlistInfo",{{"id","123"}});CHECK(result.value("playlists").toList().first().toMap().value("name")==QString::fromUtf8("旧接口歌单"));
  playlistIncomplete=true;CHECK(call(client,"playlist",{{"id","123"}}).value("error")=="incomplete_response");playlistFail=false;playlistIncomplete=false;
  CHECK(call(client,"liked").value("songs").toList().first().toMap().value("id")=="mid42");
  CHECK(call(client,"playlist",{{"id","bad"}}).value("error")=="invalid_arguments");
  const auto user=call(client,"profile",{{"id","12345"}}).value("user").toMap();CHECK(user.value("nickname")==QString::fromUtf8("中文用户") && user.value("id")=="12345" && user.value("avatarUrl")=="https://image.test/user");
  const auto mine=call(client,"userPlaylists",{{"id","12345"}}).value("playlists").toList();CHECK(mine.size()==1 && mine.first().toMap().value("id")=="1234567890123" && mine.first().toMap().value("editId")=="42");
  for(int level=0;level<4;++level){vipType=level;CHECK(call(client,"vip",{{"id","12345"}}).value("vipLevel").toInt()==qMin(level,2));}
  CHECK(call(client,"like",{{"id","songMid"},{"like",true}}).value("ok").toBool());CHECK(call(client,"like",{{"id","songMid"},{"like",false}}).value("ok").toBool());
  accountReject=true;CHECK(call(client,"like",{{"id","songMid"},{"like",true}}).value("error")=="provider_error");accountReject=false;
  missingWriteCode=true;CHECK(call(client,"like",{{"id","songMid"},{"like",true}}).value("error")=="provider_error");missingWriteCode=false;
  dataWriteReject=true;CHECK(call(client,"like",{{"id","songMid"},{"like",true}}).value("error")=="provider_error");dataWriteReject=false;
  postFail=true;const int writes=counts["AddSongFav"];CHECK(call(client,"like",{{"id","songMid"},{"like",true}}).value("error")=="network_error" && counts["AddSongFav"]==writes+1);postFail=false;
  const QVariantMap addArgs{{"id","42"},{"songId","songMid"}},createArgs{{"name",QString::fromUtf8("中文 A+B & #")}};
  CHECK(call(client,"add",addArgs).value("ok").toBool());CHECK(call(client,"create",createArgs).value("dirId")=="42");
  CHECK(call(client,"collect",{{"id","123"},{"collect",true}}).value("ok").toBool() && params["collect"].value("optype")=="1");
  CHECK(call(client,"collect",{{"id","123"},{"collect",false}}).value("ok").toBool() && params["collect"].value("optype")=="2");
  writeReject=true;CHECK(call(client,"add",addArgs).value("error")=="provider_error");CHECK(call(client,"create",createArgs).value("error")=="provider_error");writeReject=false;
  writeMissingCode=true;CHECK(call(client,"add",addArgs).value("error")=="provider_error");writeMissingCode=false;
  createMissingDir=true;CHECK(call(client,"create",createArgs).value("error")=="invalid_response");createMissingDir=false;
  writeFail=true;const int adds=counts["/splcloud/fcgi-bin/fcg_music_add2songdir.fcg"],creates=counts["/splcloud/fcgi-bin/create_playlist.fcg"];
  CHECK(call(client,"add",addArgs).value("error")=="network_error" && counts["/splcloud/fcgi-bin/fcg_music_add2songdir.fcg"]==adds+1);
  CHECK(call(client,"create",createArgs).value("error")=="network_error" && counts["/splcloud/fcgi-bin/create_playlist.fcg"]==creates+1);writeFail=false;
  addRedirect=true;CHECK(call(client,"add",addArgs).value("error")=="redirect_rejected" && counts["/replayed-write"]==0);addRedirect=false;
  CHECK(call(client,"dailyId").value("dailyId")=="1234567890123");dailyMissing=true;CHECK(call(client,"dailyId").value("dailyId").isNull());dailyMissing=false;
  requestCookie.clear();CHECK(call(client,"like",{{"id","songMid"},{"like",true}}).value("error")=="login_required");CHECK(call(client,"add",addArgs).value("error")=="login_required");
  const int dailyCalls=counts["/node/musicmac/v6/index.html"];CHECK(call(client,"dailyId").value("dailyId").isNull() && counts["/node/musicmac/v6/index.html"]==dailyCalls);
  CHECK(call(client,"like",{{"id","songMid"},{"like",1}}).value("error")=="invalid_arguments");CHECK(call(client,"profile",{{"id","../bad"}}).value("error")=="invalid_arguments");
  CHECK(call(client,"artist",{{"id","../bad"}}).value("error")=="invalid_arguments");CHECK(valid);puts("NATIVE_QQ_FIXTURE_PASS 24 operations metadata writes form encoding GET-no-retry GET-no-redirect daily card scope profile lyrics pagination");return 0;
}
