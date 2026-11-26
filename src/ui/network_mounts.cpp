#include "ui/network_mounts.hpp"

#include <QByteArrayView>
#include <QFile>
#include <QLatin1String>
#include <QStringView>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cstddef>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(Q_OS_MACOS)
#include <sys/mount.h>
#include <sys/param.h>
#endif

namespace patchy::ui {

namespace {

// Network types as /proc/self/mounts names them, FUSE clients as "fuse.<client>".
// Local FUSE file systems (ntfs-3g, exfat, portal, snapfuse) stay local: a wrong
// "network" only skips an existence check, but a wrong "local" blocks quit.
constexpr std::array<QStringView, 25> kNetworkFileSystems{
    u"nfs",        u"nfs4",         u"cifs",        u"smb3",         u"smbfs",     u"ncpfs",
    u"afs",        u"afpfs",        u"coda",        u"9p",           u"davfs",     u"ceph",
    u"glusterfs",  u"autofs",       u"fuse.sshfs",  u"fuse.davfs2",  u"fuse.gvfsd-fuse",
    u"fuse.rclone", u"fuse.s3fs",   u"fuse.glusterfs", u"fuse.ceph", u"fuse.ceph-fuse",
    u"fuse.curlftpfs", u"fuse.smbnetfs", u"fuse.afpfs",
};

QString normalized_mount_path(QString path) {
  path = path.normalized(QString::NormalizationForm_C);
  while (path.size() > 1 && path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }
  return path;
}

// /proc/self/mounts escapes space, tab, newline and backslash as \040 \011 \012 \134.
QString decode_mount_field(QByteArrayView field) {
  QByteArray decoded;
  decoded.reserve(field.size());
  for (qsizetype index = 0; index < field.size(); ++index) {
    const char ch = field[index];
    if (ch == '\\' && index + 3 < field.size()) {
      const auto digits = field.mid(index + 1, 3);
      bool octal = true;
      int value = 0;
      for (const char digit : digits) {
        if (digit < '0' || digit > '7') {
          octal = false;
          break;
        }
        value = value * 8 + (digit - '0');
      }
      if (octal) {
        decoded.append(static_cast<char>(value));
        index += 3;
        continue;
      }
    }
    decoded.append(ch);
  }
  return QString::fromUtf8(decoded);
}

}  // namespace

bool is_network_file_system_type(const QString& file_system) {
  const auto type = file_system.trimmed();
  return std::any_of(kNetworkFileSystems.begin(), kNetworkFileSystems.end(),
                     [&type](QStringView candidate) { return type == candidate; });
}

const MountEntry* mount_for_path(const QString& path, const std::vector<MountEntry>& mounts) {
  const auto wanted = normalized_mount_path(path);
  if (wanted.isEmpty()) {
    return nullptr;
  }
  const MountEntry* best = nullptr;
  qsizetype best_length = -1;
  for (const auto& entry : mounts) {
    const auto point = normalized_mount_path(entry.mount_point);
    if (point.isEmpty()) {
      continue;
    }
    const bool matches = point == QLatin1String("/") ? wanted.startsWith(QLatin1Char('/'))
                         : wanted == point ||
                               (wanted.startsWith(point) && wanted.size() > point.size() &&
                                wanted[point.size()] == QLatin1Char('/'));
    if (matches && point.size() > best_length) {
      best = &entry;
      best_length = point.size();
    }
  }
  return best;
}

bool path_is_on_network_mount(const QString& path, const std::vector<MountEntry>& mounts) {
  const auto* entry = mount_for_path(path, mounts);
  return entry != nullptr && (!entry->local || is_network_file_system_type(entry->file_system));
}

bool path_is_on_network_location(const QString& path, const std::vector<MountEntry>& mounts) {
  if (path.startsWith(QStringLiteral("\\\\")) || path.startsWith(QStringLiteral("//"))) {
    return true;
  }
#ifdef Q_OS_WIN
  if (path.size() >= 2 && path[1] == QLatin1Char(':') && path[0].isLetter()) {
    const wchar_t root[] = {static_cast<wchar_t>(path[0].unicode()), L':', L'\\', L'\0'};
    return GetDriveTypeW(root) == DRIVE_REMOTE;
  }
#endif
  return path_is_on_network_mount(path, mounts);
}

std::vector<MountEntry> parse_proc_mounts(const QByteArray& text) {
  std::vector<MountEntry> mounts;
  for (const auto& line : text.split('\n')) {
    const auto fields = line.simplified().split(' ');
    if (fields.size() < 3) {
      continue;
    }
    MountEntry entry;
    entry.mount_point = decode_mount_field(fields[1]);
    entry.file_system = QString::fromUtf8(fields[2]);
    entry.local = !is_network_file_system_type(entry.file_system);
    mounts.push_back(std::move(entry));
  }
  return mounts;
}

std::vector<MountEntry> read_system_mounts() {
  std::vector<MountEntry> mounts;
#if defined(Q_OS_MACOS)
  struct statfs* table = nullptr;
  // MNT_NOWAIT: the kernel's cached statfs data; MNT_WAIT would ask every server.
  const int count = getmntinfo(&table, MNT_NOWAIT);
  for (int index = 0; index < count; ++index) {
    MountEntry entry;
    entry.mount_point = QString::fromUtf8(table[index].f_mntonname);
    entry.file_system = QString::fromUtf8(table[index].f_fstypename);
    entry.local = (table[index].f_flags & MNT_LOCAL) != 0 && !is_network_file_system_type(entry.file_system);
    mounts.push_back(std::move(entry));
  }
#elif defined(Q_OS_LINUX)
  QFile file(QStringLiteral("/proc/self/mounts"));
  if (file.open(QIODevice::ReadOnly)) {
    mounts = parse_proc_mounts(file.readAll());
  }
#endif
  return mounts;
}

}  // namespace patchy::ui
