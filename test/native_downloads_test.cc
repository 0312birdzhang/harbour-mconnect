#include "../elinux/silica/native_downloads.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "DOWNLOAD_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
bool waitFor(const std::function<bool()>& ready) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!ready() && elapsed.elapsed() < 5000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(1); }
  return ready();
}
QVariantMap song(const QString& id) {
  return NativePlaybackSong({{"id", id}, {"platform", "qq"}, {"name", id},
      {"artists", QVariantList{QVariantMap{{"id", "7"}, {"name", "fixture"}}}}, {"durationMs", 100000}});
}
QVariantMap row(const NativeDownloads& downloads, const QString& id) {
  for (const auto& raw : downloads.snapshot().value("tasks").toList()) if (raw.toMap().value("id") == id) return raw.toMap();
  return {};
}
QVariantMap stored(NativeStorage& storage, const QString& id) {
  for (const auto& raw : storage.downloadTasks()) if (raw.toMap().value("id") == id) return raw.toMap();
  return {};
}
QByteArray bytes(const QString& path) { QFile file(path); CHECK(file.open(QIODevice::ReadOnly)); return file.readAll(); }
bool fails(const std::function<void()>& action) { try { action(); } catch (const std::runtime_error&) { return true; } return false; }
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  QTcpServer fixture;
  CHECK(fixture.listen(QHostAddress::LocalHost));
  QString mode = "full", lastRange;
  int requests = 0;
  QObject::connect(&fixture, &QTcpServer::newConnection, &application, [&]() {
    while (fixture.hasPendingConnections()) {
      auto* socket = fixture.nextPendingConnection();
      auto input = std::make_shared<QByteArray>();
      auto answered = std::make_shared<bool>(false);
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      QObject::connect(socket, &QTcpSocket::readyRead, &application, [&, socket, input, answered]() {
        if (*answered) return;
        *input += socket->readAll();
        if (!input->contains("\r\n\r\n")) return;
        *answered = true;
        ++requests;
        lastRange.clear();
        for (const auto& line : input->split('\n')) if (line.toLower().startsWith("range:")) lastRange = QString::fromLatin1(line.mid(6).trimmed());
        QByteArray content(100, 'a');
        QByteArray headers = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n";
        if (mode == "partial") content.truncate(40);
        if (mode == "range" || mode == "badRange") {
          const int start = lastRange.section('=', 1).section('-', 0, 0).toInt();
          content = content.mid(start);
          headers = "HTTP/1.1 206 Partial Content\r\nContent-Length: " + QByteArray::number(content.size()) + "\r\nContent-Range: bytes " + QByteArray::number(mode == "badRange" ? start + 1 : start) + "-99/100\r\n";
        }
        socket->write(headers + "Connection: close\r\n\r\n");
        const QPointer<QTcpSocket> guarded(socket);
        const auto respond = [guarded, content]() { if (guarded && guarded->state() == QAbstractSocket::ConnectedState) { guarded->write(content); guarded->disconnectFromHost(); } };
        if (mode == "delayed") QTimer::singleShot(150, &application, respond);
        else respond();
      });
    }
  });
  const auto provider = [&](const QString&, const QVariantList& arguments) {
    const auto args = QJsonDocument::fromJson(arguments[1].toString().toUtf8()).toVariant().toMap();
    return QVariantMap{{"backend", "cpp-qq-catalog"}, {"vipLevel", 1},
        {"url", QString("http://127.0.0.1:%1/audio/%2").arg(fixture.serverPort()).arg(args.value("id").toString())}};
  };
  const auto configure = [](NativeStorage& storage) { storage.setCacheSetting("autoRetry", false); storage.setCacheSetting("autoCleanup", false); };
  const auto selected = temporary.path() + "/chosen downloads";
  CHECK(QDir().mkpath(selected));
  const auto selectedPath = QFileInfo(selected).canonicalFilePath();
  {
    NativeStorage storage(temporary.path() + "/directory", temporary.path() + "/directory-cache");
    configure(storage);
    storage.box("download_settings").put("unexposed", "preserved");
    NativeMusicServices music(storage, nullptr, provider);
    NativeDownloads downloads(storage, music, nullptr, []() { return false; }, {}, false);
    CHECK(downloads.directory() == QDir(storage.dataDirectory).filePath("downloads"));
    for (const auto& invalid : QStringList{"relative", "file:///tmp", "content://folder", QDir(selected).rootPath(), selected + "/missing"}) {
      CHECK(fails([&]() { downloads.setDirectory(invalid); }));
      CHECK(storage.box("download_settings").get("custom_root_path").isNull());
    }
    downloads.setDirectory(selected);
    CHECK(downloads.directory() == selectedPath);
    CHECK(storage.box("download_settings").get("custom_root_path") == selectedPath);
    storage.box("download_settings").put("custom_root_path", "  " + selectedPath + "  ");
    CHECK(downloads.directory() == selectedPath);
    storage.box("download_settings").put("custom_root_path", selectedPath + ' ');
    CHECK(downloads.directory() == selectedPath + ' ');
    storage.box("download_settings").put("custom_root_path", selectedPath);
    CHECK(storage.box("download_settings").get("unexposed") == "preserved");
    CHECK(QDir(selected).entryList({".mconnect-write-test-*"}, QDir::Files | QDir::Hidden).isEmpty());
    mode = "full";
    downloads.enqueue(song("chosen-directory"), 0);
    CHECK(waitFor([&]() { return row(downloads, "qq_chosen-directory_low").value("status") == "completed"; }));
    const auto target = stored(storage, "qq_chosen-directory_low").value("filePath").toString();
    CHECK(target.startsWith(selectedPath + '/') && bytes(target) == QByteArray(100, 'a'));
    CHECK(fails([&]() { downloads.setDirectory(target); }));
    CHECK(downloads.directory() == selectedPath);
    downloads.close();
  }
  {
    NativeStorage storage(temporary.path() + "/directory", temporary.path() + "/directory-cache");
    NativeMusicServices music(storage, nullptr, provider);
    NativeDownloads downloads(storage, music, nullptr, []() { return false; }, {}, false);
    CHECK(downloads.directory() == selectedPath);
  }
  {
    NativeStorage storage(temporary.path() + "/resume", temporary.path() + "/cache");
    configure(storage);
    NativeMusicServices music(storage, nullptr, provider);
    NativeDownloads downloads(storage, music, nullptr, []() { return false; }, {}, false);
    mode = "partial";
    downloads.enqueue(song("resume"), 0);
    CHECK(waitFor([&]() { return row(downloads, "qq_resume_low").value("status") == "failed"; }));
    auto task = stored(storage, "qq_resume_low");
    const auto path = task.value("filePath").toString();
    CHECK(task.value("downloadedBytes").toInt() == 40 && bytes(path).size() == 40);
    CHECK(row(downloads, "qq_resume_low").value("failure") == "network");
    mode = "range";
    downloads.control({{"action", "resume"}, {"id", "qq_resume_low"}});
    CHECK(waitFor([&]() { return row(downloads, "qq_resume_low").value("status") == "completed"; }));
    CHECK(lastRange == "bytes=40-" && bytes(path) == QByteArray(100, 'a'));
    CHECK(downloads.localFile(song("resume")) == path);
    CHECK(fails([&]() { downloads.control({{"action", "cancel"}, {"id", "qq_resume_low"}}); }));

    mode = "partial";
    downloads.enqueue(song("reset"), 0);
    CHECK(waitFor([&]() { return row(downloads, "qq_reset_low").value("status") == "failed"; }));
    const auto resetPath = stored(storage, "qq_reset_low").value("filePath").toString();
    mode = "full";
    downloads.control({{"action", "resume"}, {"id", "qq_reset_low"}});
    CHECK(waitFor([&]() { return row(downloads, "qq_reset_low").value("status") == "completed"; }));
    CHECK(lastRange == "bytes=40-" && bytes(resetPath).size() == 100);

    mode = "partial";
    downloads.enqueue(song("modified"), 0);
    CHECK(waitFor([&]() { return row(downloads, "qq_modified_low").value("status") == "failed"; }));
    const auto modifiedPath = stored(storage, "qq_modified_low").value("filePath").toString();
    QFile modified(modifiedPath); CHECK(modified.open(QIODevice::Append)); CHECK(modified.write("external") == 8); modified.close();
    mode = "full";
    downloads.control({{"action", "resume"}, {"id", "qq_modified_low"}});
    CHECK(waitFor([&]() { return row(downloads, "qq_modified_low").value("status") == "completed"; }));
    CHECK(lastRange.isEmpty() && bytes(modifiedPath).size() == 48);
    CHECK(stored(storage, "qq_modified_low").value("filePath") != modifiedPath);

    mode = "partial";
    downloads.enqueue(song("cancel"), 0);
    CHECK(waitFor([&]() { return row(downloads, "qq_cancel_low").value("status") == "failed"; }));
    const auto cancelPath = stored(storage, "qq_cancel_low").value("filePath").toString();
    downloads.control({{"action", "cancel"}, {"id", "qq_cancel_low"}});
    CHECK(row(downloads, "qq_cancel_low").isEmpty() && !QFile::exists(cancelPath));
  }
  {
    NativeStorage storage(temporary.path() + "/parallel", temporary.path() + "/cache2");
    configure(storage);
    NativeMusicServices music(storage, nullptr, provider);
    NativeDownloads downloads(storage, music, nullptr, []() { return false; }, {}, false);
    mode = "delayed";
    const int firstRequest = requests;
    int maximumActive = 0;
    QObject::connect(&downloads, &NativeDownloads::changed, [&]() {
      int count = 0;
      for (const auto& raw : downloads.snapshot().value("tasks").toList()) if (raw.toMap().value("status") == "downloading") ++count;
      maximumActive = qMax(maximumActive, count);
    });
    for (int index = 0; index < 5; ++index) downloads.enqueue(song(QString::number(index)), 0);
    CHECK(waitFor([&]() { return requests >= firstRequest + 3; }));
    CHECK(maximumActive == 3 && requests == firstRequest + 3);
    downloads.control({{"action", "pauseQueue"}});
    CHECK(downloads.snapshot().value("paused").toBool());
    downloads.control({{"action", "resumeQueue"}});
    CHECK(waitFor([&]() {
      for (const auto& raw : downloads.snapshot().value("tasks").toList()) if (raw.toMap().value("status") != "completed") return false;
      return true;
    }));
    CHECK(maximumActive == 3);
  }
  {
    NativeStorage storage(temporary.path() + "/policy", temporary.path() + "/cache3");
    configure(storage);
    NativeMusicServices music(storage, nullptr, provider);
    bool wifi = false;
    NativeDownloads downloads(storage, music, nullptr, [&]() { return wifi; }, {}, false);
    mode = "full";
    const auto count = requests;
    downloads.enqueue(song("shared"), 0, true);
    QCoreApplication::processEvents();
    CHECK(requests == count && downloads.snapshot().value("blocked").toString().contains("Wi-Fi"));
    downloads.enqueue(song("shared"), 0);
    CHECK(waitFor([&]() { return row(downloads, "qq_shared_low").value("status") == "completed"; }));
    CHECK(row(downloads, "qq_shared_low_cache").value("status") == "waiting" && requests == count + 1);
    wifi = true;
    downloads.policyChanged();
    CHECK(waitFor([&]() { return row(downloads, "qq_shared_low_cache").value("status") == "completed"; }));
    CHECK(requests == count + 1);
    const auto path = stored(storage, "qq_shared_low").value("filePath").toString();
    CHECK(stored(storage, "qq_shared_low_cache").value("filePath") == path);
    QFile large(path); CHECK(large.open(QIODevice::ReadWrite)); CHECK(large.resize(qint64(129) * 1024 * 1024)); large.close();
    storage.setCacheSetting("sizeLimitMb", 128);
    downloads.cleanup();
    CHECK(QFile::exists(path) && row(downloads, "qq_shared_low_cache").isEmpty() && !row(downloads, "qq_shared_low").isEmpty());
    storage.setCacheSetting("offlineMode", true);
    downloads.enqueue(song("offlineCache"), 0, true);
    downloads.policyChanged();
    CHECK(row(downloads, "qq_offlineCache_low_cache").value("status") == "waiting");
    downloads.enqueue(song("explicit"), 0);
    CHECK(waitFor([&]() { return row(downloads, "qq_explicit_low").value("status") == "completed"; }));
    music.acceptLogin("qq", "uin=o123; qm_keyst=fixture", {{"id", "123"}, {"nickname", "fixture"}, {"platform", "qq"}});
    CHECK(fails([&]() { downloads.enqueue(song("lossless"), 3); }));
    downloads.enqueue(song("spatial"), 5);
    CHECK(waitFor([&]() { return row(downloads, "qq_spatial_spatial").value("status") == "completed"; }));
    CHECK(stored(storage, "qq_spatial_spatial").value("filePath").toString().endsWith("[spatial].mp3"));
  }
  {
    NativeStorage storage(temporary.path() + "/legacy-cache", temporary.path() + "/legacy-cache-data");
    configure(storage);
    storage.setCacheSetting("wifiOnly", false);
    const auto cacheTask = QVariantMap{{"id", "qq_legacy_low"}, {"song", song("legacy")}, {"quality", "low"},
        {"status", "waiting"}, {"downloadedBytes", 0}, {"isOfflineCache", true}};
    storage.saveDownloadTasks({cacheTask});
    NativeMusicServices music(storage, nullptr, provider);
    NativeDownloads downloads(storage, music, nullptr, []() { return true; });
    mode = "delayed";
    CHECK(waitFor([&]() { return row(downloads, "qq_legacy_low_cache").value("status") == "downloading"; }));
    downloads.enqueue(song("legacy"), 0);
    CHECK(waitFor([&]() { return row(downloads, "qq_legacy_low_cache").value("status") == "completed" && row(downloads, "qq_legacy_low").value("status") == "completed"; }));
    CHECK(!stored(storage, "qq_legacy_low").value("isOfflineCache").toBool());
    CHECK(stored(storage, "qq_legacy_low_cache").value("filePath") == stored(storage, "qq_legacy_low").value("filePath"));
  }
  {
    NativeStorage storage(temporary.path() + "/legacy-collision", temporary.path() + "/legacy-collision-data");
    configure(storage);
    const auto cacheTask = QVariantMap{{"id", "qq_collision_low"}, {"song", song("collision")}, {"quality", "low"},
        {"status", "paused"}, {"downloadedBytes", 0}, {"isOfflineCache", true}};
    auto existing = cacheTask;
    existing["id"] = "qq_collision_low_cache";
    storage.saveDownloadTasks({cacheTask, existing});
    NativeMusicServices music(storage, nullptr, provider);
    NativeDownloads downloads(storage, music, nullptr, []() { return false; }, {}, false);
    CHECK(row(downloads, "qq_collision_low").isEmpty());
    CHECK(row(downloads, "qq_collision_low_cache_legacy_1").value("cache").toBool());
    CHECK(row(downloads, "qq_collision_low_cache").value("cache").toBool());
    downloads.enqueue(song("collision"), 0);
    mode = "full";
    CHECK(waitFor([&]() { return row(downloads, "qq_collision_low").value("status") == "completed"; }));
    CHECK(downloads.snapshot().value("tasks").toList().size() == 3);
  }
  {
    NativeStorage storage(temporary.path() + "/disk", temporary.path() + "/cache4");
    configure(storage);
    NativeMusicServices music(storage, nullptr, provider);
    NativeDownloads downloads(storage, music, nullptr, []() { return true; }, [](const QString&) { return qint64(0); }, false);
    mode = "full";
    downloads.enqueue(song("full"), 0);
    CHECK(waitFor([&]() { return row(downloads, "qq_full_low").value("status") == "failed"; }));
    CHECK(row(downloads, "qq_full_low").value("failure") == "disk");
  }
  CHECK(NativeDownloadFileName(song("fixture"), 6).endsWith("[dolby].mp3"));
  CHECK(NativeDownloadFileName(song("fixture"), 7).endsWith("[master].flac"));
  puts("NATIVE_DOWNLOADS_PASS range-resume reset-on-200 modified-file-preservation partial-cancel concurrency-three queue-pause wifi-cache-gate manual-bypass shared-files offline-mode vip-levels disk-reserve");
  return 0;
}
