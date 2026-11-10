#include "native_storage.h"

#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <limits>
#include <zlib.h>

#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
QMutex lockMutex;
QSet<QString> heldLocks;
constexpr int maxFrameBytes = 64 * 1024 * 1024;
constexpr int maxDepth = 64;

void syncFile(QFileDevice& file) {
  if (!file.flush()) throw NativeStorageError("Cannot flush storage");
#ifdef Q_OS_WIN
  const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()));
  if (!FlushFileBuffers(handle)) throw NativeStorageError("Cannot synchronize storage");
#else
  if (::fsync(file.handle()) != 0) throw NativeStorageError("Cannot synchronize storage");
#endif
}

void appendUint32(QByteArray& bytes, quint32 value) {
  const quint32 encoded = qToLittleEndian(value);
  bytes.append(reinterpret_cast<const char*>(&encoded), sizeof(encoded));
}

void appendNumber(QByteArray& bytes, double value) {
  if (!std::isfinite(value)) throw NativeStorageError("Invalid Hive number");
  quint64 bits;
  static_assert(sizeof(bits) == sizeof(value), "Hive requires IEEE-754 doubles");
  std::memcpy(&bits, &value, sizeof(bits));
  bits = qToLittleEndian(bits);
  bytes.append(reinterpret_cast<const char*>(&bits), sizeof(bits));
}

QByteArray encodedKey(const QString& key) {
  const auto text = key.toUtf8();
  if (text.size() > 255) throw NativeStorageError("Hive key is too long");
  QByteArray result;
  result.append(char(1));
  result.append(char(text.size()));
  result.append(text);
  return result;
}

class HiveReader {
 public:
  HiveReader(const QByteArray& bytes, int offset, int limit)
      : bytes_(bytes), offset_(offset), limit_(limit) {}

  int position() const { return offset_; }
  int remaining() const { return limit_ - offset_; }

  quint8 byte() {
    require(1);
    return quint8(bytes_.at(offset_++));
  }

  quint32 uint32() {
    require(4);
    const auto result = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(bytes_.constData() + offset_));
    offset_ += 4;
    return result;
  }

  QByteArray bytes(quint32 length) {
    require(length);
    const auto result = bytes_.mid(offset_, int(length));
    offset_ += int(length);
    return result;
  }

  QString text(quint32 length) {
    const auto raw = bytes(length);
    const auto result = QString::fromUtf8(raw);
    if (result.toUtf8() != raw) throw NativeStorageError("Invalid Hive UTF-8");
    return result;
  }

  QVariant key() {
    const auto type = byte();
    if (type == 0) return QVariant::fromValue(uint32());
    if (type == 1) return text(byte());
    throw NativeStorageError("Invalid Hive key");
  }

  double number() {
    require(8);
    quint64 bits = qFromLittleEndian<quint64>(
        reinterpret_cast<const uchar*>(bytes_.constData() + offset_));
    offset_ += 8;
    double result;
    std::memcpy(&result, &bits, sizeof(result));
    if (!std::isfinite(result)) throw NativeStorageError("Invalid Hive number");
    return result;
  }

  QVariant value(int depth = 0) {
    if (depth > maxDepth) throw NativeStorageError("Hive value is too deeply nested");
    const auto type = byte();
    switch (type) {
      case 0: return {};
      case 1: {
        const double result = std::trunc(number());
        if (result < -9223372036854775808.0 || result >= 9223372036854775808.0) {
          throw NativeStorageError("Hive integer is out of range");
        }
        return QVariant::fromValue(qint64(result));
      }
      case 2: return number();
      case 3: return byte() > 0;
      case 4: return text(uint32());
      case 5: return bytes(uint32());
      case 6:
      case 7:
      case 8:
      case 9:
      case 10: {
        const auto count = uint32();
        const int minimum = type == 6 || type == 7 ? 8 : type == 9 ? 4 : 1;
        if (count > quint32(remaining() / minimum)) {
          throw NativeStorageError("Invalid Hive list length");
        }
        QVariantList result;
        result.reserve(int(count));
        for (quint32 index = 0; index < count; ++index) {
          if (type == 6) {
            const double integer = std::trunc(number());
            if (integer < -9223372036854775808.0 || integer >= 9223372036854775808.0) {
              throw NativeStorageError("Hive integer is out of range");
            }
            result.append(QVariant::fromValue(qint64(integer)));
          } else if (type == 7) result.append(number());
          else if (type == 8) result.append(byte() > 0);
          else if (type == 9) result.append(text(uint32()));
          else result.append(value(depth + 1));
        }
        return result;
      }
      case 11: {
        const auto count = uint32();
        if (count > quint32(remaining() / 2)) throw NativeStorageError("Invalid Hive map length");
        QVariantMap result;
        for (quint32 index = 0; index < count; ++index) {
          const auto name = value(depth + 1);
          if (name.type() != QVariant::String) {
            throw NativeStorageError("Native settings require string map keys");
          }
          result.insert(name.toString(), value(depth + 1));
        }
        return result;
      }
      default: throw NativeStorageError("Unsupported Hive value type");
    }
  }

 private:
  const QByteArray& bytes_;
  int offset_;
  const int limit_;
  void require(quint32 length) const {
    if (length > quint32(remaining())) throw NativeStorageError("Truncated Hive value");
  }
};

void appendValue(QByteArray& bytes, const QVariant& value, int depth = 0) {
  if (depth > maxDepth || bytes.size() > maxFrameBytes) {
    throw NativeStorageError("Hive value is too large");
  }
  switch (value.type()) {
    case QVariant::Invalid:
      bytes.append(char(0));
      break;
    case QVariant::Int:
    case QVariant::UInt:
    case QVariant::LongLong:
    case QVariant::ULongLong:
      bytes.append(char(1));
      appendNumber(bytes, value.toDouble());
      break;
    case QVariant::Double:
      bytes.append(char(2));
      appendNumber(bytes, value.toDouble());
      break;
    case QVariant::Bool:
      bytes.append(char(3));
      bytes.append(value.toBool() ? char(1) : char(0));
      break;
    case QVariant::String:
    case QVariant::ByteArray: {
      bytes.append(value.type() == QVariant::String ? char(4) : char(5));
      const auto raw = value.type() == QVariant::String ? value.toString().toUtf8() : value.toByteArray();
      appendUint32(bytes, quint32(raw.size()));
      bytes.append(raw);
      break;
    }
    case QVariant::StringList:
    case QVariant::List: {
      bytes.append(char(10));
      const auto list = value.toList();
      appendUint32(bytes, quint32(list.size()));
      for (const auto& item : list) appendValue(bytes, item, depth + 1);
      break;
    }
    case QVariant::Map: {
      bytes.append(char(11));
      const auto map = value.toMap();
      appendUint32(bytes, quint32(map.size()));
      for (auto item = map.cbegin(); item != map.cend(); ++item) {
        appendValue(bytes, item.key(), depth + 1);
        appendValue(bytes, item.value(), depth + 1);
      }
      break;
    }
    default: throw NativeStorageError("Unsupported native Hive value");
  }
}

QByteArray frame(const QByteArray& key, const QVariant& value, bool deleted = false) {
  QByteArray bytes(4, char(0));
  bytes.append(key);
  if (!deleted) appendValue(bytes, value);
  if (bytes.size() > maxFrameBytes - 4) throw NativeStorageError("Hive frame is too large");
  qToLittleEndian<quint32>(quint32(bytes.size() + 4), reinterpret_cast<uchar*>(bytes.data()));
  const auto checksum = ::crc32(0, reinterpret_cast<const Bytef*>(bytes.constData()), uInt(bytes.size()));
  appendUint32(bytes, quint32(checksum));
  return bytes;
}

QString boxPath(const QString& directory, const QString& name) {
  if (name.isEmpty() || name != name.toLower() ||
      name.contains('/') || name.contains('\\') || name.contains('.') || name.size() > 255) {
    throw NativeStorageError("Invalid Hive box name");
  }
  if (!QDir().mkpath(directory)) throw NativeStorageError("Cannot create data directory");
  return QDir(directory).filePath(name + ".hive");
}
}

void NativeAtomicWrite(const QString& path, const QByteArray& bytes, bool privateFile) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
    throw NativeStorageError("Cannot create storage directory");
  }
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) throw NativeStorageError("Cannot write storage");
  if (privateFile && !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    throw NativeStorageError("Cannot protect credentials");
  }
  if (file.write(bytes) != bytes.size()) throw NativeStorageError("Cannot write storage");
  syncFile(file);
  if (!file.commit()) throw NativeStorageError("Cannot commit storage");
}

NativeFileLock::NativeFileLock(const QString& path) : file_(path) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath()) || QFileInfo(path).isSymLink()) {
    throw NativeStorageError("Cannot open storage lock");
  }
  identity_ = QDir(QFileInfo(path).absolutePath()).canonicalPath() + '/' + QFileInfo(path).fileName();
  QMutexLocker guard(&lockMutex);
  if (heldLocks.contains(identity_)) throw NativeStorageError("Storage is already in use");
  if (!file_.open(QIODevice::ReadWrite)) throw NativeStorageError("Cannot open storage lock");
#ifdef Q_OS_WIN
  const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(file_.handle()));
  OVERLAPPED overlap = {};
  if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                  0, MAXDWORD, MAXDWORD, &overlap)) {
    throw NativeStorageError("Storage is already in use");
  }
#else
  struct flock lock = {};
  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_SET;
  if (::fcntl(file_.handle(), F_SETLK, &lock) == -1) {
    throw NativeStorageError("Storage is already in use");
  }
#endif
  heldLocks.insert(identity_);
}

NativeFileLock::~NativeFileLock() {
  QMutexLocker guard(&lockMutex);
  file_.close();
  heldLocks.remove(identity_);
}

NativeHiveBox::NativeHiveBox(const QString& directory, const QString& name)
    : path_(boxPath(directory, name)), lock_(QDir(directory).filePath(name + ".lock")), file_(path_) {
  if (!QFile::exists(path_) && QFile::exists(path_ + 'c')) {
    if (!QFile::rename(path_ + 'c', path_)) throw NativeStorageError("Cannot restore compacted Hive box");
  }
  if (!file_.open(QIODevice::ReadWrite)) throw NativeStorageError("Cannot open Hive box");
  load();
}

void NativeHiveBox::load() {
  qint64 validEnd = 0;
  while (validEnd < file_.size()) {
    const auto header = file_.read(4);
    if (header.size() < 4) break;
    const auto length = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(header.constData()));
    if (length < 10 || length > maxFrameBytes) throw NativeStorageError("Invalid Hive frame length");
    if (qint64(length) > file_.size() - validEnd) break;
    const auto bytes = header + file_.read(int(length) - 4);
    if (bytes.size() != int(length)) throw NativeStorageError("Cannot read Hive box");
    const auto checksum = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(bytes.constData() + bytes.size() - 4));
    const auto actual = ::crc32(0, reinterpret_cast<const Bytef*>(bytes.constData()), uInt(bytes.size() - 4));
    if (checksum != actual) throw NativeStorageError("Hive checksum mismatch");
    HiveReader reader(bytes, 4, bytes.size() - 4);
    reader.key();
    const auto key = bytes.mid(4, reader.position() - 4);
    if (reader.remaining() == 0) {
      obsoleteBytes_ += frames_.take(key).size() + bytes.size();
      order_.removeAll(key);
    } else {
      if (!frames_.contains(key)) order_.append(key);
      obsoleteBytes_ += frames_.value(key).size();
      frames_[key] = bytes;
    }
    validEnd += length;
  }
  // A killed append can leave an incomplete final frame. Fully present frames
  // with a bad checksum fail closed instead of overwriting the damaged file.
  if (validEnd != file_.size()) {
    if (!file_.resize(validEnd)) throw NativeStorageError("Cannot recover Hive append");
    syncFile(file_);
  }
  if (!file_.seek(validEnd)) throw NativeStorageError("Cannot seek Hive box");
}

QVariant NativeHiveBox::get(const QString& key) const {
  const auto bytes = frames_.value(encodedKey(key));
  if (bytes.isEmpty()) return {};
  HiveReader reader(bytes, 4, bytes.size() - 4);
  reader.key();
  const auto result = reader.value();
  if (reader.remaining() != 0) throw NativeStorageError("Invalid Hive value length");
  return result;
}

QStringList NativeHiveBox::keys() const {
  QStringList result;
  for (const auto& key : order_) {
    if (quint8(key.at(0)) != 1) continue;
    result.append(QString::fromUtf8(key.constData() + 2, key.size() - 2));
  }
  return result;
}

void NativeHiveBox::put(const QString& key, const QVariant& value) {
  apply({{key, value}});
}

void NativeHiveBox::remove(const QString& key) {
  apply({}, {key});
}

void NativeHiveBox::rewrite(const QHash<QByteArray, QByteArray>& frames, const QList<QByteArray>& order) {
  QSaveFile replacement(path_);
  replacement.setDirectWriteFallback(false);
  if (!replacement.open(QIODevice::WriteOnly)) throw NativeStorageError("Cannot compact Hive box");
  for (const auto& key : order) {
    const auto& bytes = frames[key];
    if (replacement.write(bytes) != bytes.size()) throw NativeStorageError("Cannot compact Hive box");
  }
  syncFile(replacement);
  file_.close();
  const bool committed = replacement.commit();
  if (!file_.open(QIODevice::ReadWrite) || !file_.seek(file_.size())) {
    throw NativeStorageError("Cannot reopen Hive box");
  }
  if (!committed) throw NativeStorageError("Cannot commit Hive compaction");
}

void NativeHiveBox::apply(const QVariantMap& values, const QStringList& removed, bool atomic) {
  auto nextFrames = frames_;
  auto nextOrder = order_;
  QByteArray appended;
  qint64 obsolete = obsoleteBytes_;
  for (auto item = values.cbegin(); item != values.cend(); ++item) {
    const auto key = encodedKey(item.key());
    const auto bytes = frame(key, item.value());
    if (nextFrames.value(key) == bytes) continue;
    if (!nextFrames.contains(key)) nextOrder.append(key);
    obsolete += nextFrames.value(key).size();
    nextFrames[key] = bytes;
    appended.append(bytes);
  }
  for (const auto& name : removed) {
    if (values.contains(name)) throw NativeStorageError("Conflicting Hive update");
    const auto key = encodedKey(name);
    if (!nextFrames.contains(key)) continue;
    const auto bytes = frame(key, {}, true);
    obsolete += nextFrames.take(key).size() + bytes.size();
    nextOrder.removeAll(key);
    appended.append(bytes);
  }
  if (appended.isEmpty()) return;
  if (atomic || (obsolete > 1024 * 1024 && obsolete > file_.size() / 2)) {
    rewrite(nextFrames, nextOrder);
    obsolete = 0;
  } else {
    const auto before = file_.size();
    try {
      if (!file_.seek(before) || file_.write(appended) != appended.size()) {
        throw NativeStorageError("Cannot append Hive values");
      }
      syncFile(file_);
    } catch (...) {
      if (!file_.resize(before) || !file_.seek(before)) {
        throw NativeStorageError("Cannot recover failed Hive write");
      }
      throw;
    }
  }
  frames_ = std::move(nextFrames);
  order_ = std::move(nextOrder);
  obsoleteBytes_ = obsolete;
}
