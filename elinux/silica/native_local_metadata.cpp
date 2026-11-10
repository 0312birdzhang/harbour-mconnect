#include "native_local_music.h"
#include "native_artwork.h"

#include <QCryptographicHash>
#include <QDir>
#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/xiphcomment.h>
#include <taglib/vorbisfile.h>
#include <taglib/opusfile.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mp4coverart.h>
#include <taglib/wavfile.h>

namespace {
QString text(const TagLib::String& value) { return QString::fromUtf8(value.toCString(true)).trimmed(); }
QByteArray bytes(const TagLib::ByteVector& value) {
  if (value.size() > 16 * 1024 * 1024) return {};
  return QByteArray(value.data(), int(value.size()));
}
QByteArray id3Cover(TagLib::ID3v2::Tag* tag) {
  if (!tag) return {};
  const auto frames = tag->frameList("APIC");
  for (auto* frame : frames) if (auto* picture = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(frame)) return bytes(picture->picture());
  return {};
}
QByteArray xiphCover(TagLib::Ogg::XiphComment* tag) {
  if (!tag || tag->pictureList().isEmpty()) return {};
  return bytes(tag->pictureList().front()->data());
}
QByteArray cover(TagLib::File* file) {
  if (auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(file)) return id3Cover(mpeg->ID3v2Tag(false));
  if (auto* flac = dynamic_cast<TagLib::FLAC::File*>(file)) {
    if (!flac->pictureList().isEmpty()) return bytes(flac->pictureList().front()->data());
    return id3Cover(flac->ID3v2Tag(false));
  }
  if (auto* ogg = dynamic_cast<TagLib::Ogg::Vorbis::File*>(file)) return xiphCover(ogg->tag());
  if (auto* opus = dynamic_cast<TagLib::Ogg::Opus::File*>(file)) return xiphCover(opus->tag());
  if (auto* wav = dynamic_cast<TagLib::RIFF::WAV::File*>(file)) return id3Cover(wav->ID3v2Tag());
  if (auto* mp4 = dynamic_cast<TagLib::MP4::File*>(file)) {
    const auto items = mp4->tag()->itemMap();
    if (items.contains("covr")) {
      const auto pictures = items["covr"].toCoverArtList();
      if (!pictures.isEmpty()) return bytes(pictures.front().data());
    }
  }
  return {};
}
}

QVariantMap ReadNativeLocalMetadata(const QString& path, const QString& coverDirectory) {
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) return {};
  const auto header = input.read(128);
  QByteArray trailer;
  if (input.size() >= 128 && input.seek(input.size() - 128)) trailer = input.read(3);
  input.close();
#ifdef Q_OS_WIN
  const auto filename = reinterpret_cast<const wchar_t*>(path.utf16());
#else
  const auto name = QFile::encodeName(path);
  const auto filename = name.constData();
#endif
  // Container markers retain metadata when the user renamed the extension.
  TagLib::FileRef reference;
  if (header.startsWith("fLaC")) reference = TagLib::FileRef(new TagLib::FLAC::File(filename, true, TagLib::AudioProperties::Accurate));
  else if (header.startsWith("ID3") || trailer == "TAG") reference = TagLib::FileRef(new TagLib::MPEG::File(filename, true, TagLib::AudioProperties::Accurate));
  else if (header.startsWith("RIFF") && header.mid(8, 4) == "WAVE") reference = TagLib::FileRef(new TagLib::RIFF::WAV::File(filename, true, TagLib::AudioProperties::Accurate));
  else if (header.mid(4, 4) == "ftyp") reference = TagLib::FileRef(new TagLib::MP4::File(filename, true, TagLib::AudioProperties::Accurate));
  else if (header.startsWith("OggS") && header.contains("OpusHead")) reference = TagLib::FileRef(new TagLib::Ogg::Opus::File(filename, true, TagLib::AudioProperties::Accurate));
  else if (header.startsWith("OggS")) reference = TagLib::FileRef(new TagLib::Ogg::Vorbis::File(filename, true, TagLib::AudioProperties::Accurate));
  else reference = TagLib::FileRef(filename, true, TagLib::AudioProperties::Accurate);
  if (reference.isNull() || !reference.file()->isValid() || !reference.tag()) return {};
  const auto tag = reference.tag();
  QVariantMap result;
  result["title"] = text(tag->title()).isEmpty() ? QVariant() : QVariant(text(tag->title()));
  result["artist_name"] = text(tag->artist()).isEmpty() ? QVariant() : QVariant(text(tag->artist()));
  result["album_name"] = text(tag->album()).isEmpty() ? QVariant() : QVariant(text(tag->album()));
  result["duration_ms"] = reference.audioProperties() ? reference.audioProperties()->lengthInMilliseconds() : 0;
  result["track_number"] = tag->track() ? QVariant(int(tag->track())) : QVariant();
  const auto properties = reference.file()->properties();
  for (const auto& key : {"LYRICS", "UNSYNCEDLYRICS"}) {
    if (properties.contains(key) && !properties[key].isEmpty()) { result["embeddedLyrics"] = text(properties[key].front()); break; }
  }
  const auto artwork = cover(reference.file());
  if (result.value("title").isNull() && result.value("artist_name").isNull() && result.value("album_name").isNull() &&
      result.value("duration_ms").toLongLong() == 0 && result.value("track_number").isNull() &&
      result.value("embeddedLyrics").toString().isEmpty() && artwork.isEmpty()) return {};
  if (!artwork.isEmpty() && !coverDirectory.isEmpty()) {
    const auto key = QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex());
    const auto target = QDir(coverDirectory).filePath(key + ".mca");
    try {
      NativeAtomicWrite(QDir(coverDirectory).filePath(".nomedia"), {});
      const auto encoded = EncodeNativeArtwork(artwork);
      if (!encoded.isEmpty()) { NativeAtomicWrite(target, encoded); result["cover_path"] = target; }
    } catch (const NativeStorageError&) {}
  }
  return result;
}
