#include "../elinux/silica/native_car_content.h"
#include "sailplaycontent.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "CAR_CONTENT_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
QVariantMap song(const QString& id) {
  return NativePlaybackSong({{"id", id}, {"platform", "qq"}, {"name", "歌曲 " + id}, {"durationMs", 100000},
      {"artists", QVariantList{QVariantMap{{"id", "artist"}, {"name", "Artist"}}}}});
}
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  NativeStorage storage(temporary.path(), temporary.path() + "/cache");
  QVariantList queue;
  for (int index = 0; index < 125; ++index) queue.append(song(QString::number(index)));
  storage.savePlaybackMemory({{"currentSong", queue.first()}, {"playlist", queue}, {"currentIndex", 0}});
  NativeMusicServices music(storage);
  NativeDownloads downloads(storage, music, nullptr, []() { return false; }, {}, false);
  NativeLibrary library(storage);
  NativePlayer player(storage, music, downloads, library);
  SailplayProvider provider("harbour-mconnect", {{"content", SailplayContent::page()}}, {"content"});
  NativeCarContent content(provider);
  CHECK(provider.start());
  CHECK(SailplayContent::validPage(content.root()));
  CHECK(content.browse({{"id", "queue"}, {"offset", uint(0)}, {"limit", uint(50)}}).isEmpty());
  content.attach(player);
  CHECK(content.root().value("items").toList().first().toMap().value("id") == "queue");
  const auto parameters = QVariantMap{{"id", "queue"}, {"offset", uint(0)}, {"limit", uint(50)}};
  const auto first = content.browse(parameters);
  CHECK(SailplayContent::validPage(first) && first.value("items").toList().size() == 50 && first.value("hasMore").toBool());
  auto next = parameters;
  next["offset"] = uint(100);
  const auto final = content.browse(next);
  CHECK(SailplayContent::validPage(final) && final.value("items").toList().size() == 25 && !final.value("hasMore").toBool());
  auto search = parameters;
  search["query"] = "歌曲 12";
  CHECK(content.browse(search).value("items").toList().size() == 6);
  search["query"] = "aRtIsT";
  CHECK(content.browse(search).value("items").toList().size() == 50);
  search["query"] = "absent";
  CHECK(content.browse(search).value("messageKind") == "info");
  auto invalid = parameters;
  invalid["limit"] = uint(51);
  CHECK(content.browse(invalid).isEmpty());
  const auto selected = first.value("items").toList()[1].toMap().value("parameters").toMap();
  auto wrong = selected;
  wrong["key"] = "wrong";
  CHECK(!content.select(wrong));
  wrong = selected;
  wrong["index"] = -1;
  CHECK(!content.select(wrong));
  CHECK(content.select(selected));
  CHECK(player.currentSong().value("id") == "1");
  player.playPlaylist({song("replacement")}, 0);
  CHECK(!content.select(selected));
  player.close();
  provider.close();
  fprintf(stderr, "CAR_CONTENT_PASS typed-pages pagination unicode search selected-song stale-queue\n");
}
