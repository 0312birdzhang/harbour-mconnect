#include "../elinux/silica/native_player.h"
#include "sailfish_audio_fixture.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "PLAYER_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
QVariantMap song(const QString& id) {
  return NativePlaybackSong({{"id", id}, {"platform", "qq"}, {"name", id}, {"durationMs", 100000},
      {"artists", QVariantList{QVariantMap{{"id", "1"}, {"name", "fixture"}}}}});
}
bool waitFor(const std::function<bool()>& ready) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!ready() && elapsed.elapsed() < 5000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(1); }
  return ready();
}
bool fails(const std::function<void()>& action) { try { action(); } catch (const std::runtime_error&) { return true; } return false; }
bool ready(const NativePlayer& player) { return player.snapshot().value("playing").toBool() && !player.snapshot().value("busy").toBool(); }
void waitForPolls() {
  QEventLoop loop;
  QTimer::singleShot(450, &loop, &QEventLoop::quit);
  loop.exec();
}
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  NativeStorage storage(temporary.path(), temporary.path() + "/cache");
  storage.savePlaybackMemory({{"currentSong", song("restored")}, {"playlist", QVariantList{song("restored"), song("next")}},
      {"currentIndex", 0}, {"positionMs", 12000}, {"durationMs", 100000}, {"currentQuality", "high"}, {"qualityPreference", "fixed"},
      {"playbackSpeed", 1.25}, {"isShuffle", false}, {"repeatMode", "off"}, {"abLoopStartMs", 10000}, {"abLoopEndMs", 20000}});
  storage.box("settings").put("audio_effects_settings", QVariantMap{{"equalizerEnabled", true}, {"equalizerPreset", "vocal"}});
  NativePlayer* current = nullptr;
  NativeMusicServices* currentMusic = nullptr;
  int streams = 0;
  bool stale = false, reject = false, replaceAccount = false;
  NativeMusicServices music(storage, nullptr, [&](const QString&, const QVariantList& arguments) {
    const auto args = QJsonDocument::fromJson(arguments[1].toString().toUtf8()).toVariant().toMap();
    if (arguments[0] == "stream") {
      ++streams;
      if (stale) { stale = false; current->playSong(song("replacement")); }
      if (replaceAccount) { replaceAccount = false; currentMusic->logout("qq"); }
      if (args.value("quality").toInt() > 0) return QVariantMap{{"backend", "cpp-qq-catalog"}, {"error", "vip_required"}};
      return QVariantMap{{"backend", "cpp-qq-catalog"}, {"url", "https://fixture.test/" + (reject ? QString("reject") : args.value("id").toString())}};
    }
    return QVariantMap{{"backend", "cpp-qq-catalog"}, {"qualities", QVariantList()}};
  });
  currentMusic = &music;
  NativeDownloads downloads(storage, music, nullptr, []() { return false; }, {}, false);
  NativeLibrary library(storage);
  {
    NativePlayer player(storage, music, downloads, library);
    current = &player;
    CHECK(player.currentSong().value("id") == "restored" && player.snapshot().value("position") == 12000);
    CHECK(!player.snapshot().value("playing").toBool() && NativeTestAudio().opens == 0);
    player.seek(15000);
    CHECK(NativeTestAudio().opens == 0);
    player.play();
    CHECK(waitFor([&]() { return ready(player); }));
    CHECK(streams == 3 && NativeTestAudio().position == 15000000 && NativeTestAudio().rate == 1.25);
    CHECK(NativeTestAudio().bands == QList<double>({-2, 0, 5, 3, 1}));
    NativeTestAudio().position = 21000000;
    CHECK(waitFor([&]() { return NativeTestAudio().position == 10000000; }));
    player.pause();
    CHECK(!player.snapshot().value("playing").toBool());
    const auto pausedStreams = streams;
    player.play();
    CHECK(player.snapshot().value("playing").toBool() && streams == pausedStreams);
    player.next();
    CHECK(waitFor([&]() { return player.currentSong().value("id") == "next" && ready(player); }));
    CHECK(player.snapshot().value("position") == 0);
    NativeTestAudio().position = 21000000;
    CHECK(waitFor([&]() { return player.snapshot().value("position") == 21000; }));
    CHECK(NativeTestAudio().position == 21000000);
    player.previous();
    CHECK(player.currentSong().value("id") == "next" && NativeTestAudio().position == 0);
    player.previous();
    CHECK(waitFor([&]() { return player.currentSong().value("id") == "restored" && ready(player); }));
    CHECK(fails([&]() { player.seek(-1); }));
    CHECK(fails([&]() { player.playIndex(99); }));
    player.cycleRepeat();
    CHECK(player.snapshot().value("repeat") == "all");
    player.cycleRepeat();
    CHECK(player.snapshot().value("repeat") == "one");
    player.next();
    CHECK(player.currentSong().value("id") == "restored");
    player.cycleRepeat();
    player.toggleShuffle();
    player.next();
    CHECK(waitFor([&]() { return player.currentSong().value("id") == "next" && ready(player); }));
    player.previous();
    CHECK(waitFor([&]() { return player.currentSong().value("id") == "restored" && ready(player); }));
    player.toggleShuffle();
    stale = true;
    player.playSong(song("stale"));
    CHECK(waitFor([&]() { return player.currentSong().value("id") == "replacement" && ready(player); }));
    CHECK(NativeTestAudio().uri.endsWith("/replacement"));
    replaceAccount = true;
    const auto staleOpens = NativeTestAudio().opens;
    const auto staleStreams = streams;
    player.playSong(song("account-stale"));
    CHECK(waitFor([&]() { return !player.snapshot().value("busy").toBool(); }));
    CHECK(!player.snapshot().value("playing").toBool() && NativeTestAudio().opens == staleOpens && streams == staleStreams + 1);
    player.playSong(song("cancelled"));
    NativeTestAudio().position = 54000000;
    player.pause();
    QEventLoop pausedTransition;
    QTimer::singleShot(250, &pausedTransition, &QEventLoop::quit);
    pausedTransition.exec();
    CHECK(!player.snapshot().value("playing").toBool() && !player.snapshot().value("busy").toBool());
    CHECK(player.snapshot().value("position") == 0);
    player.play();
    CHECK(waitFor([&]() { return ready(player); }));
    CHECK(NativeTestAudio().uri.endsWith("/cancelled"));
    player.playPlaylist({song("eos-a"), song("eos-b")}, 0);
    CHECK(waitFor([&]() { return ready(player); }));
    NativeTestAudio().status = 4;
    CHECK(waitFor([&]() { return player.currentSong().value("id") == "eos-b" && ready(player); }));
    NativeTestAudio().status = 4;
    CHECK(waitFor([&]() { return !player.snapshot().value("playing").toBool(); }));
    CHECK(player.currentSong().value("id") == "eos-b" && player.snapshot().value("position") == 100000);
    const auto replayOpens = NativeTestAudio().opens;
    player.play();
    CHECK(ready(player) && NativeTestAudio().status == 3 && NativeTestAudio().position == 0);
    CHECK(player.snapshot().value("position") == 0 && NativeTestAudio().opens == replayOpens);
    player.cycleRepeat();
    player.cycleRepeat();
    for (int repeat = 0; repeat < 2; ++repeat) {
      NativeTestAudio().status = 4;
      CHECK(waitFor([&]() { return NativeTestAudio().status == 3 && ready(player); }));
      CHECK(player.snapshot().value("position") == 0 && NativeTestAudio().opens == replayOpens);
    }
    player.cycleRepeat();
    NativeTestAudio().status = 4;
    CHECK(waitFor([&]() { return !player.snapshot().value("playing").toBool(); }));
    NativeTestAudio().seekSucceeds = false;
    player.play();
    CHECK(waitFor([&]() { return ready(player) && NativeTestAudio().opens > replayOpens; }));
    NativeTestAudio().seekSucceeds = true;
    CHECK(player.snapshot().value("position") == 0);
    player.playPlaylist({song("paused-eos-a"), song("paused-eos-b")}, 0);
    CHECK(waitFor([&]() { return ready(player); }));
    player.pause();
    const auto pausedEosOpens = NativeTestAudio().opens;
    NativeTestAudio().status = 4;
    waitForPolls();
    CHECK(player.currentSong().value("id") == "paused-eos-a" && !player.snapshot().value("playing").toBool());
    CHECK(NativeTestAudio().opens == pausedEosOpens && NativeTestAudio().status == 4);
    player.play();
    CHECK(ready(player) && NativeTestAudio().position == 0 && NativeTestAudio().status == 3);
    player.pause();
    const auto pausedErrorOpens = NativeTestAudio().opens;
    NativeTestAudio().status = 5;
    waitForPolls();
    CHECK(player.currentSong().value("id") == "paused-eos-a" && !player.snapshot().value("playing").toBool());
    CHECK(NativeTestAudio().opens == pausedErrorOpens && NativeTestAudio().status == 5);
    player.play();
    CHECK(waitFor([&]() { return ready(player) && NativeTestAudio().opens > pausedErrorOpens; }));
    player.playPlaylist({song("duplicate"), song("middle"), song("duplicate"), song("tail")}, 2);
    CHECK(waitFor([&]() { return ready(player); }));
    CHECK(player.snapshot().value("index") == 2);
    player.next();
    CHECK(waitFor([&]() { return ready(player); }));
    CHECK(player.snapshot().value("index") == 3 && player.currentSong().value("id") == "tail");
    player.previous();
    CHECK(waitFor([&]() { return ready(player); }));
    CHECK(player.snapshot().value("index") == 2);
    player.playIndex(0);
    CHECK(waitFor([&]() { return ready(player); }));
    CHECK(player.snapshot().value("index") == 0);
    player.playIndex(2);
    CHECK(waitFor([&]() { return ready(player); }));
    player.seek(3999);
    player.previous();
    CHECK(waitFor([&]() { return ready(player); }));
    CHECK(player.snapshot().value("index") == 1 && player.currentSong().value("id") == "middle");
    player.seek(4000);
    player.previous();
    CHECK(player.snapshot().value("index") == 1 && player.snapshot().value("position") == 0);
    player.next();
    CHECK(waitFor([&]() { return ready(player); }));
    CHECK(player.snapshot().value("index") == 2);
    const auto duplicateOpens = NativeTestAudio().opens;
    NativeTestAudio().status = 5;
    CHECK(waitFor([&]() { return ready(player) && NativeTestAudio().opens > duplicateOpens; }));
    CHECK(player.snapshot().value("index") == 2);
    NativeTestAudio().status = 4;
    CHECK(waitFor([&]() { return player.currentSong().value("id") == "tail" && ready(player); }));
    CHECK(player.snapshot().value("index") == 3);
    player.playSong(song("recovery"));
    CHECK(waitFor([&]() { return ready(player); }));
    for (int attempt = 0; attempt < 2; ++attempt) {
      const auto opens = NativeTestAudio().opens;
      NativeTestAudio().status = 5;
      CHECK(waitFor([&]() { return NativeTestAudio().opens > opens && ready(player); }));
    }
    const auto recoveredOpens = NativeTestAudio().opens;
    NativeTestAudio().status = 5;
    CHECK(waitFor([&]() { return !player.snapshot().value("playing").toBool(); }));
    CHECK(NativeTestAudio().opens == recoveredOpens && !player.snapshot().value("error").toString().isEmpty());
    storage.setCacheSetting("offlineMode", true);
    const auto offlineStreams = streams;
    player.playPlaylist({song("offline")}, 0);
    CHECK(waitFor([&]() { return !player.snapshot().value("busy").toBool(); }));
    CHECK(streams == offlineStreams && !player.snapshot().value("playing").toBool());
    const auto path = temporary.path() + "/local song.mp3";
    NativeAtomicWrite(path, "fixture");
    auto local = song(path);
    local["platform"] = "local";
    player.playSong(local);
    CHECK(waitFor([&]() { return ready(player); }));
    CHECK(NativeTestAudio().uri.startsWith("file:") && NativeTestAudio().uri.contains("local%20song.mp3"));
    player.seek(25000);
    player.close();
    CHECK(storage.playbackMemory().value("positionMs") == 25000);
    CHECK(NativeTestAudio().destroys > 0);
  }
  {
    NativePlayer restored(storage, music, downloads, library);
    const auto opens = NativeTestAudio().opens;
    CHECK(restored.currentSong().value("platform") == "local" && restored.snapshot().value("position") == 25000);
    CHECK(!restored.snapshot().value("playing").toBool() && NativeTestAudio().opens == opens);
  }
  storage.setCacheSetting("offlineMode", false);
  storage.box("settings").put("audio_effects_settings", QVariantMap{{"fadeEnabled", true}, {"fadeDurationMs", 200}});
  {
    NativePlayer fading(storage, music, downloads, library);
    current = &fading;
    fading.playPlaylist({song("fade-a"), song("fade-b")}, 0);
    CHECK(waitFor([&]() { return ready(fading); }));
    QTimer::singleShot(40, &application, []() { NativeTestAudio().status = 4; });
    fading.pause();
    waitForPolls();
    CHECK(!fading.snapshot().value("playing").toBool() && fading.currentSong().value("id") == "fade-a");
    CHECK(NativeTestAudio().status == 4);
  }
  storage.box("settings").put("audio_effects_settings", QVariantMap{{"sleepTimerEnabled", true}, {"sleepTimerRemainingSeconds", 1}});
  {
    NativePlayer sleeping(storage, music, downloads, library);
    current = &sleeping;
    sleeping.playPlaylist({song("sleep-a"), song("sleep-b")}, 0);
    CHECK(waitFor([&]() { return ready(sleeping); }));
    QTimer::singleShot(1100, &sleeping, [&]() { sleeping.playSong(song("sleep-b")); });
    QEventLoop deadline;
    QTimer::singleShot(1800, &deadline, &QEventLoop::quit);
    deadline.exec();
    CHECK(sleeping.currentSong().value("id") == "sleep-b" && ready(sleeping));
  }
  storage.box("settings").put("audio_effects_settings", QVariantMap());
  reject = true;
  {
    NativePlayer failed(storage, music, downloads, library);
    failed.playPlaylist({song("bad-a"), song("bad-b")}, 0);
    CHECK(waitFor([&]() { return !failed.snapshot().value("busy").toBool(); }));
    CHECK(!failed.snapshot().value("playing").toBool() && !failed.snapshot().value("error").toString().isEmpty());
    CHECK(failed.currentSong().value("id") == "bad-b");
  }
  CHECK(!library.history().isEmpty());
  puts("NATIVE_PLAYER_PASS paused-restore deferred-load downshift rate equalizer ab-loop queue duplicate-indices previous-threshold repeat shuffle-history stale-source account-cancel transition-seek eos replay paused-eos paused-error recovery fade sleep-generation offline local-uri memory shutdown bounded-failure");
  return 0;
}
