#pragma once

#include <QByteArray>
#include <QString>

#include <vector>

namespace patchy::ui {

// One mounted file system, as the OS reports it without touching the mount.
struct MountEntry {
  QString mount_point;  // absolute; "/" or no trailing slash
  QString file_system;  // OS type name: "apfs", "smbfs", "nfs4", "fuse.sshfs", "autofs"
  bool local{true};     // false for network file systems (macOS: MNT_LOCAL clear)
};

// File-system types Linux mounts across a network, plus automount triggers
// (`autofs`), whose first access mounts something and can block just as long.
[[nodiscard]] bool is_network_file_system_type(const QString& file_system);

// The entry whose mount point contains `path`, deepest one wins ("/" matches
// everything; "/Volumes/share" matches "/Volumes/share/x" but not
// "/Volumes/share2"). Null when no entry matches.
[[nodiscard]] const MountEntry* mount_for_path(const QString& path, const std::vector<MountEntry>& mounts);

// True when the mount holding `path` is a network file system or an automount
// trigger: a stat there can block for the whole network timeout.
[[nodiscard]] bool path_is_on_network_mount(const QString& path, const std::vector<MountEntry>& mounts);

// True when `path` is on a network location, decided from the path's text and cached OS
// tables alone: a UNC or device path (`\\host\share`, `//host/share`, `\\.\pipe`), a
// Windows drive letter mapped to a share (DRIVE_REMOTE), or a path under a network mount.
// Nothing here stats the path or contacts a server. Such a path is never stat'ed or read
// unasked: an asleep or unreachable host blocks the call for the SMB timeout, and main()
// waits for the workers at quit. The recent-history existence check and the Home Screen's
// thumbnails (recent_thumbnails.hpp) both ask this first.
[[nodiscard]] bool path_is_on_network_location(const QString& path, const std::vector<MountEntry>& mounts);

// Parses the text of /proc/self/mounts (Linux). Exposed for tests.
[[nodiscard]] std::vector<MountEntry> parse_proc_mounts(const QByteArray& text);

// The live mount table, read from cached kernel data only (getmntinfo with
// MNT_NOWAIT on macOS, /proc/self/mounts on Linux): it never contacts a
// server. Empty on Windows (drive types answer there) and on wasm.
[[nodiscard]] std::vector<MountEntry> read_system_mounts();

}  // namespace patchy::ui
