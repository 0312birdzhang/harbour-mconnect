#include "../elinux/silica/native_local_music.h"
#include "../elinux/silica/native_artwork.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "LOCAL_MUSIC_CHECK_FAILED line=%d %s\n", __LINE__, #condition); std::exit(1); } } while (false)

namespace {
QByteArray number(quint64 value, int size, bool littleEndian = false) {
  QByteArray result(size, '\0');
  for (int index = 0; index < size; ++index) result[littleEndian ? index : size - index - 1] = char(value >> (8 * index));
  return result;
}
QByteArray block(int type, const QByteArray& data, bool last = false) {
  return QByteArray(1, char(type | (last ? 128 : 0))) + number(data.size(), 3) + data;
}
QByteArray flac(const QString& title, const QString& embedded = {}, bool picture = true) {
  const auto packed = (quint64(44100) << 44) | (quint64(1) << 41) | (quint64(15) << 36) | 132300;
  const auto stream = number(4096, 2) + number(4096, 2) + QByteArray(6, '\0') + number(packed, 8) + QByteArray(16, '\0');
  QVariantMap tags{{"TITLE", title}, {"ARTIST", "fixture artist"}, {"ALBUM", "fixture album"}, {"TRACKNUMBER", "2"}};
  if (!embedded.isEmpty()) tags["LYRICS"] = embedded;
  const auto vendor = QByteArray("mconnect-test");
  QByteArray comments = number(vendor.size(), 4, true) + vendor + number(tags.size(), 4, true);
  for (auto item = tags.cbegin(); item != tags.cend(); ++item) {
    const auto entry = (item.key() + '=' + item.value().toString()).toUtf8();
    comments += number(entry.size(), 4, true) + entry;
  }
  QByteArray result = "fLaC" + block(0, stream) + block(1, QByteArray(4096, '\0')) + block(4, comments, !picture);
  if (picture) {
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray png;
    QBuffer buffer(&png);
    CHECK(buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG"));
    const QByteArray mime = "image/png";
    const auto data = number(3, 4) + number(mime.size(), 4) + mime + number(0, 4) + number(2, 4) + number(2, 4) +
        number(24, 4) + number(0, 4) + number(png.size(), 4) + png;
    result += block(6, data, true);
  }
  return result;
}
void write(const QString& path, const QByteArray& bytes) { NativeAtomicWrite(path, bytes); }
bool waitFor(const std::function<bool()>& ready) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!ready() && elapsed.elapsed() < 5000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(1); }
  return ready();
}
QVariantMap indexed(NativeStorage& storage, const QString& path) {
  const auto rows = storage.database().query("SELECT * FROM local_tracks WHERE path=?", {path});
  return rows.isEmpty() ? QVariantMap() : rows.first().toMap();
}
QString lyric(NativeStorage& storage, const QString& path) {
  const auto rows = storage.database().query("SELECT content FROM lyrics_cache WHERE platform='local' AND song_id=?", {path});
  return rows.isEmpty() ? QString() : rows.first().toMap().value("content").toString();
}
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir temporary;
  CHECK(temporary.isValid());
  const auto root = temporary.path() + "/music";
  const auto path = root + "/01 - 中文歌曲.flac";
  const auto second = root + "/other/embedded.flac";
  write(path, flac(QStringLiteral("标记标题"), "[00:01.00]embedded"));
  write(second, flac("Second", "[00:02.00]inside", false));
  write(root + "/lyrics/中文歌曲.lrc", QStringLiteral("[00:01.00]外部歌词").toUtf8());
  NativeStorage storage(temporary.path() + "/data", temporary.path() + "/cache");
  NativeLocalMusic local(storage);
  CHECK(local.snapshot().value("count") == 0);
  local.scan(root);
  CHECK(waitFor([&]() { return !local.snapshot().value("scanning").toBool(); }));
  CHECK(local.snapshot().value("error").toString().isEmpty());
  CHECK(local.snapshot().value("count") == 2 && local.snapshot().value("parsed") == 2);
  CHECK(indexed(storage, path).value("title") == QStringLiteral("标记标题"));
  CHECK(indexed(storage, path).value("duration_ms") == 3000 && indexed(storage, path).value("track_number") == 2);
  CHECK(lyric(storage, path) == QStringLiteral("[00:01.00]外部歌词"));
  CHECK(lyric(storage, second) == "[00:02.00]inside");
  const auto cover = indexed(storage, path).value("cover_path").toString();
  const auto coverName = QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex()) + ".mca";
  CHECK(cover.endsWith(coverName) && QFile::exists(cover));
  QFile artwork(cover);
  CHECK(artwork.open(QIODevice::ReadOnly) && !DecodeNativeArtwork(artwork.readAll()).isEmpty());
  artwork.close();
  const auto firstScan = indexed(storage, path).value("scanned_at");
  local.scan(root);
  CHECK(waitFor([&]() { return !local.snapshot().value("scanning").toBool(); }));
  CHECK(local.snapshot().value("parsed") == 0 && local.snapshot().value("reused") == 2);
  CHECK(indexed(storage, path).value("scanned_at") == firstScan);
  CHECK(local.groups({{"view", "albums"}}).size() == 1);
  CHECK(local.groups({{"view", "folders"}}).size() == 2);
  CHECK(local.songs({{"keyword", QStringLiteral("标记标题")}}).size() == 1);
  write(root + "/lyrics/中文歌曲.lrc", QStringLiteral("[00:03.00]更新后的外部歌词").toUtf8());
  local.scan(root);
  CHECK(waitFor([&]() { return !local.snapshot().value("scanning").toBool(); }));
  CHECK(lyric(storage, path) == QStringLiteral("[00:03.00]更新后的外部歌词"));
  CHECK(local.snapshot().value("parsed") == 0);
  CHECK(QFile::remove(root + "/lyrics/中文歌曲.lrc"));
  local.scan(root);
  CHECK(waitFor([&]() { return !local.snapshot().value("scanning").toBool(); }));
  CHECK(lyric(storage, path) == QStringLiteral("[00:03.00]更新后的外部歌词"));
  CHECK(QFile::remove(path));
  local.scan(root);
  CHECK(waitFor([&]() { return !local.snapshot().value("scanning").toBool(); }));
  CHECK(indexed(storage, path).isEmpty() && lyric(storage, path).isEmpty());
  CHECK(local.snapshot().value("removed") == 1);
  const auto otherRoot = temporary.path() + "/other-library";
  write(otherRoot + "/renamed.mp3", flac("content-marker", {}, false));
  local.scan(otherRoot);
  CHECK(waitFor([&]() { return !local.snapshot().value("scanning").toBool(); }));
  CHECK(indexed(storage, otherRoot + "/renamed.mp3").value("title") == "content-marker");
  CHECK(!indexed(storage, second).isEmpty() && lyric(storage, second) == "[00:02.00]inside");
  const auto directory = local.snapshot().value("directory");
  {
    NativeLocalMusic restored(storage);
    CHECK(restored.snapshot().value("directory") == directory && restored.snapshot().value("count") == 2);
    restored.scan(root);
    restored.close();
    CHECK(!restored.snapshot().value("scanning").toBool());
  }
  write(root + "/invalid.wav", "unrecognised audio");
  CHECK(ReadNativeLocalMetadata(root + "/invalid.wav", {}).isEmpty());
  CHECK(QFile::remove(root + "/invalid.wav"));
  puts("NATIVE_LOCAL_MUSIC_PASS real-flac unicode metadata artwork sidecar-first embedded incremental-stamps removed-files outside-root renamed-container grouping restore shutdown");
  return 0;
}
