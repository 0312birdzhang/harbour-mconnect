#include "../elinux/silica/native_lyrics.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <sqlite3.h>
#include <cstdio>

#define CHECK(x) do { if (!(x)) {fprintf(stderr,"native lyrics failed at %d\n",__LINE__);return 1;} } while(false)
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  const auto rows=ParseNativeLrc(QString::fromUtf8("[ti:标题]\n[00:01.00][00:02.000]中文歌词\n[00:01.00]Translation\n[00:01.00]中文歌词\n[00:01.00]第三行\n[offset:-250]"));
  CHECK(rows.size()==3);
  CHECK(rows[0].toMap().value("time").toInt()==750);
  CHECK(rows[0].toMap().value("translation")=="Translation");
  CHECK(rows[1].toMap().value("text")==QString::fromUtf8("第三行"));
  CHECK(rows[2].toMap().value("time").toInt()==1750);
  CHECK(ParseNativeLrc("[00:00.01]zero\n[offset:-500]")[0].toMap().value("time").toInt()==0);
  CHECK(ParseNativeLrc(QString(2097153,'a')).isEmpty());
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto path=directory.path()+"/test.sqlite";
  sqlite3* db=nullptr;
  CHECK(sqlite3_open(path.toUtf8().constData(),&db)==SQLITE_OK);
  CHECK(sqlite3_exec(db,"CREATE TABLE lyrics_cache(song_id TEXT,platform TEXT,content TEXT,format TEXT);"
      "INSERT INTO lyrics_cache VALUES('song','netease','[00:01.00]original','lrc');"
      "INSERT INTO lyrics_cache VALUES('song','qq','[00:02.00]other platform','lrc');"
      "INSERT INTO lyrics_cache VALUES('qrc','qq','unsupported','qrc');"
      "INSERT INTO lyrics_cache VALUES('envelope','netease','mconnect-lyrics-v1\n{\"content\":\"[00:03.00]wrapped\",\"source\":\"netease\"}','lrc');",nullptr,nullptr,nullptr)==SQLITE_OK);
  CHECK(ReadNativeLyrics(path,"netease","song").value("lines").toList()[0].toMap().value("time")==1000);
  CHECK(ReadNativeLyrics(path,"qq","song").value("lines").toList()[0].toMap().value("time")==2000);
  CHECK(ReadNativeLyrics(path,"netease","envelope").value("lines").toList()[0].toMap().value("text")=="wrapped");
  CHECK(ReadNativeLyrics(path,"qq","qrc").isEmpty());
  CHECK(ReadNativeLyrics(path,"local","song").isEmpty());
  CHECK(ReadNativeLyrics(path,"netease","song' OR 1=1 --").isEmpty());
  CHECK(ReadNativeLyrics(directory.path()+"/missing.sqlite","netease","song").isEmpty());
  CHECK(!QFile::exists(directory.path()+"/missing.sqlite"));
  CHECK(sqlite3_exec(db,"BEGIN EXCLUSIVE",nullptr,nullptr,nullptr)==SQLITE_OK);
  CHECK(ReadNativeLyrics(path,"netease","song").isEmpty());
  sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);sqlite3_close(db);
  puts("NATIVE_LYRICS_PASS unicode timestamps translations envelope source-isolation read-only lock-fallback");
  if(argc>1) {
    sqlite3* real=nullptr;
    CHECK(sqlite3_open_v2(argv[1],&real,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK);
    sqlite3_stmt* statement=nullptr;
    CHECK(sqlite3_prepare_v2(real,"SELECT song_id,platform FROM lyrics_cache WHERE format='lrc' LIMIT 100",-1,&statement,nullptr)==SQLITE_OK);
    int hits=0;
    while(sqlite3_step(statement)==SQLITE_ROW) {
      const auto id=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(statement,0)));
      const auto platform=QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(statement,1)));
      if(!ReadNativeLyrics(QString::fromUtf8(argv[1]),platform,id).isEmpty())++hits;
    }
    sqlite3_finalize(statement);sqlite3_close(real);
    printf("NATIVE_DEVICE_CACHE_READS %d\n",hits);
  }
  return 0;
}
