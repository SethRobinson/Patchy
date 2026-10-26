#pragma once

// Help > Add Patchy to Applications Menu (GitHub issue 226). A Wayland compositor finds a
// window's icon through the installed .desktop file named after its app id
// (com.rtsoft.patchy, QGuiApplication::setDesktopFileName in main.cpp), and an AppImage
// installs none, so the taskbar shows a generic icon. On request Patchy writes that desktop
// file and its icons into the user's XDG data folder, pointing at the .AppImage file. Every
// write and delete is limited to the fixed file list below. See docs/appimage.md.

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

#ifdef PATCHY_HAVE_QT_DBUS
class QDBusConnection;
#endif

namespace patchy::ui {

// The desktop file id and icon name: the app id the window reports.
inline constexpr char kAppImageMenuEntryId[] = "com.rtsoft.patchy";
// Marks a desktop file Patchy wrote, so Remove and the launch-time refresh never touch a
// com.rtsoft.patchy.desktop someone else put there.
inline constexpr char kAppImageMenuEntryMarkerKey[] = "X-Patchy-AppImage";
// The D-Bus signal every KDE program's icon loader listens for (KIconLoader::emitChange):
// on it they rebuild their icon themes and so see icon folders created since they started.
inline constexpr char kKdeIconLoaderPath[] = "/KIconLoader";
inline constexpr char kKdeIconLoaderInterface[] = "org.kde.KIconLoader";

struct AppImageMenuEntryPaths {
  // The .AppImage file the entry starts; empty when Patchy is not running from one.
  QString appimage_file;
  // The AppImage's own usr/share: the shipped desktop file and the hicolor icons.
  QString bundled_share;
  // $XDG_DATA_HOME, else ~/.local/share: where the entry is written.
  QString data_home;
  // $XDG_DATA_DIRS, else /usr/local/share and /usr/share: where a packaged Patchy
  // (a Flatpak export, a distribution package) would have put its own entry.
  QStringList data_dirs;

  [[nodiscard]] QString desktop_file() const;
  // Every icon file Add may write, as paths below data_home.
  [[nodiscard]] QStringList icon_files() const;
};

// The paths for this process: appimage_file is appimage_file_path(), the bundled files
// come from $APPDIR/usr/share.
[[nodiscard]] AppImageMenuEntryPaths resolve_appimage_menu_entry_paths();
// $XDG_DATA_HOME when it is an absolute path (the spec ignores a relative one), else
// ~/.local/share.
[[nodiscard]] QString xdg_data_home();
[[nodiscard]] QStringList xdg_data_dirs();

// The icon paths relative to a share folder (icons/hicolor/<size>/apps/...), in the order
// Add copies them.
[[nodiscard]] QStringList appimage_menu_entry_icon_names();
// The entry's Icon value: the absolute path of the 256 px PNG Add copies below data_home,
// so no desktop needs to reload its icon theme to show it. Empty (the shipped themed
// name stays) when the AppImage has no such icon or the path cannot be written.
[[nodiscard]] QString appimage_menu_entry_icon_file(const AppImageMenuEntryPaths& paths);

enum class AppImageMenuEntryState {
  Unavailable,     // not running from an AppImage
  NotAdded,
  Added,           // the entry Patchy wrote is in place
  AddedElsewhere,  // another tool or package already put Patchy in the menu
};

[[nodiscard]] AppImageMenuEntryState appimage_menu_entry_state(const AppImageMenuEntryPaths& paths);

// True when the startup offer may appear: a desktop session (XDG_CURRENT_DESKTOP or
// DESKTOP_SESSION set) that is not a CI run (CI unset). A bare X server, like the AppImage
// catalog's test that captures the first window, gets no offer (GitHub issue 208).
// Help > Add Patchy to Applications Menu does not depend on it.
[[nodiscard]] bool appimage_menu_entry_offer_session_allowed();

// The desktop file another tool wrote for a Patchy AppImage (AppImageLauncher's
// appimagekit_*.desktop, a Gear Lever entry, any desktop file in the user's applications
// folder whose Exec starts a Patchy .AppImage), or a com.rtsoft.patchy.desktop that
// Patchy did not write. Empty when there is none.
[[nodiscard]] QString appimage_menu_entry_provided_by(const AppImageMenuEntryPaths& paths);

// True when `desktop_entry` is a desktop file that starts a Patchy AppImage.
[[nodiscard]] bool desktop_entry_starts_patchy_appimage(const QByteArray& desktop_entry);

// The shipped desktop file with Exec and TryExec naming the .AppImage file, Icon naming
// appimage_menu_entry_icon_file(), and the marker key added. nullopt (and `error`) when
// the shipped file is missing or has no Exec line.
[[nodiscard]] std::optional<QByteArray> build_appimage_desktop_entry(const AppImageMenuEntryPaths& paths,
                                                                     QString* error = nullptr);

// The Exec argument for `path`: quoted and escaped as the Desktop Entry specification
// requires, then escaped again as a string value.
[[nodiscard]] QString desktop_entry_exec_argument(const QString& path);

struct AppImageMenuEntryResult {
  bool ok{false};
  bool changed{false};  // a file was written or removed
  QString error;        // English detail for the status bar when !ok
};

// Writes the desktop file and copies the AppImage's icons (only files whose bytes differ),
// icons first so the entry never appears without them. Refuses when the state is
// Unavailable or AddedElsewhere.
[[nodiscard]] AppImageMenuEntryResult add_appimage_menu_entry(const AppImageMenuEntryPaths& paths);
// Removes the desktop file and icons Add writes, and nothing else. Refuses unless the
// state is Added.
[[nodiscard]] AppImageMenuEntryResult remove_appimage_menu_entry(const AppImageMenuEntryPaths& paths);
// At launch, when the entry is Added: rewrites it if the AppImage moved or a new release
// ships a different desktop file or icons. Consent was given when it was added.
[[nodiscard]] AppImageMenuEntryResult refresh_appimage_menu_entry(const AppImageMenuEntryPaths& paths);

// Asks the desktop to notice the change: update-desktop-database; a new modification
// time on the user's icons/hicolor folder (GTK and Qt rescan an icon theme whose folder
// changed); gtk-update-icon-cache only for an icon cache that already exists, since a new
// user cache would hide icons other programs add later without refreshing it; and the
// KDE icon loader signal on the session bus (KDE programs list an icon theme's folders
// when they load it, so they miss folders created later; GitHub issue 226). Best effort:
// detached, Linux only, silently skipped when a tool is not installed.
void refresh_desktop_menu_caches(const AppImageMenuEntryPaths& paths);

// Sets the user's icons/hicolor folder's modification time to now. False when it does
// not exist or on other systems than Linux.
[[nodiscard]] bool mark_icon_theme_changed(const AppImageMenuEntryPaths& paths);

#ifdef PATCHY_HAVE_QT_DBUS
// Sends the KDE icon loader's iconChanged signal on `connection`. False when it is not
// connected or the send fails.
bool send_kde_icon_theme_changed(const QDBusConnection& connection);
#endif

}  // namespace patchy::ui
