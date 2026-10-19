// Help > Add Patchy to Applications Menu (GitHub issue 226, docs/appimage.md): the desktop
// file and icons it writes into a temporary XDG data folder, that Remove deletes only
// those, the moved-AppImage refresh, other tools' entries, and the menu item and startup
// offer under a faked AppImage environment.
#include "ui/app_settings.hpp"
#include "ui/appimage_menu_entry.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::AppImageMenuEntryPaths;
using patchy::ui::AppImageMenuEntryState;
using patchy::ui::MainWindowTestAccess;

// Scoped environment variables, restored (or unset) on destruction.
class ScopedEnvironment {
 public:
  ScopedEnvironment(std::initializer_list<std::pair<const char*, QByteArray>> values) {
    for (const auto& [name, value] : values) {
      saved_.push_back({name, qEnvironmentVariableIsSet(name), qgetenv(name)});
      qputenv(name, value);
    }
  }
  ~ScopedEnvironment() {
    for (const auto& entry : saved_) {
      if (entry.had) {
        qputenv(entry.name, entry.value);
      } else {
        qunsetenv(entry.name);
      }
    }
  }
  ScopedEnvironment(const ScopedEnvironment&) = delete;
  ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

 private:
  struct Saved {
    const char* name;
    bool had;
    QByteArray value;
  };
  std::vector<Saved> saved_;
};

QByteArray read_bytes(const QString& path) {
  QFile file(path);
  CHECK(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

void write_bytes(const QString& path, const QByteArray& bytes) {
  CHECK(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  CHECK(file.write(bytes) == bytes.size());
}

// What the AppImage carries in usr/share, copied from the files CMake installs there
// (packaging/linux, CMakeLists.txt's Linux install rules).
void populate_bundled_share(const QString& share) {
  write_bytes(QDir(share).filePath(QStringLiteral("applications/com.rtsoft.patchy.desktop")),
              read_bytes(QStringLiteral(PATCHY_SOURCE_DIR "/packaging/linux/com.rtsoft.patchy.desktop")));
  for (const int size : {16, 32, 48, 64, 128, 256, 512}) {
    const auto name = QStringLiteral("icons/hicolor/%1x%1/apps/com.rtsoft.patchy.png").arg(size);
    write_bytes(QDir(share).filePath(name),
                read_bytes(QStringLiteral(PATCHY_SOURCE_DIR "/packaging/linux/") + name));
  }
  write_bytes(QDir(share).filePath(QStringLiteral("icons/hicolor/scalable/apps/com.rtsoft.patchy.svg")),
              read_bytes(QStringLiteral(PATCHY_SOURCE_DIR "/packaging/branding/patchy-logo-folded.svg")));
}

// One [Desktop Entry] value, unescaped only for the backslash pair the tests use.
QString desktop_value(const QByteArray& entry, const QString& key) {
  for (const auto& line : entry.split('\n')) {
    const auto text = QString::fromUtf8(line);
    if (text.startsWith(key + QLatin1Char('='))) {
      return text.mid(key.size() + 1);
    }
  }
  return {};
}

QAction* help_menu_entry_action(patchy::ui::MainWindow& window) {
  return require_action(window, "helpAppImageMenuEntryAction");
}

QMenu* help_menu(patchy::ui::MainWindow& window) {
  for (auto* top : window.menuBar()->actions()) {
    if (auto* menu = top->menu(); menu != nullptr && menu->actions().contains(help_menu_entry_action(window))) {
      return menu;
    }
  }
  CHECK(false);
  return nullptr;
}

// Opens Help so its aboutToShow refresh runs, as a user opening the menu does.
void open_help_menu(patchy::ui::MainWindow& window, const char* artifact = nullptr) {
  auto* menu = help_menu(window);
  menu->popup(window.mapToGlobal(QPoint(40, 30)));
  QApplication::processEvents();
  if (artifact != nullptr) {
    save_widget_artifact(artifact, *menu);
  }
  menu->hide();
  QApplication::processEvents();
}

// The AppImage file name every test uses: the shared Unicode stem plus the characters
// the Exec quoting rules reserve.
QString appimage_name() {
  return QString::fromStdString(patchy::test::utf8_string(patchy::test::kUnicodeCombinedStem)) +
         QStringLiteral(" $HOME `x` \"q\" back\\slash.AppImage");
}

struct EntryFixture {
  QTemporaryDir root;
  AppImageMenuEntryPaths paths;

  EntryFixture() {
    CHECK(root.isValid());
    const auto artifacts = patchy::ui::to_qstring(
        std::filesystem::absolute(patchy::test::unicode_artifact_dir(u8"appimage-menu-entry")));
    paths.appimage_file = QDir(root.path()).filePath(QStringLiteral("Apps/") + appimage_name());
    paths.bundled_share = QDir(root.path()).filePath(QStringLiteral("mount/usr/share"));
    paths.data_home = QDir(artifacts).filePath(QStringLiteral("data-home"));
    paths.data_dirs = {QDir(root.path()).filePath(QStringLiteral("system-share"))};
    populate_bundled_share(paths.bundled_share);
  }
};

// Add writes the shipped desktop file pointing at the .AppImage (Exec quoted per the Desktop
// Entry spec, TryExec, the marker) plus every bundled icon; Remove deletes exactly those and
// leaves the folders and everyone else's files; neither touches an entry Patchy did not write.
void appimage_menu_entry_writes_and_removes_only_its_files() {
  EntryFixture fixture;
  const auto& paths = fixture.paths;
  const QStringList unrelated{
      QDir(paths.data_home).filePath(QStringLiteral("applications/org.example.Other.desktop")),
      QDir(paths.data_home).filePath(QStringLiteral("applications/mimeinfo.cache")),
      QDir(paths.data_home).filePath(QStringLiteral("icons/hicolor/48x48/apps/org.example.Other.png")),
  };
  write_bytes(unrelated[0], "[Desktop Entry]\nType=Application\nName=Other\nExec=other %F\nIcon=org.example.Other\n");
  write_bytes(unrelated[1], "[MIME Cache]\n");
  write_bytes(unrelated[2], "not really a png");

  CHECK(patchy::ui::appimage_menu_entry_state(paths) == AppImageMenuEntryState::NotAdded);
  const auto added = patchy::ui::add_appimage_menu_entry(paths);
  CHECK(added.ok && added.changed && added.error.isEmpty());
  CHECK(patchy::ui::appimage_menu_entry_state(paths) == AppImageMenuEntryState::Added);
  CHECK(paths.desktop_file() ==
        QDir(paths.data_home).filePath(QStringLiteral("applications/com.rtsoft.patchy.desktop")));

  const auto entry = read_bytes(paths.desktop_file());
  const auto shipped = read_bytes(QStringLiteral(PATCHY_SOURCE_DIR "/packaging/linux/com.rtsoft.patchy.desktop"));
  // Inside the quotes `"`, `` ` ``, `$` and `\` take a backslash and `%` doubles; the string
  // value then doubles every backslash.
  auto quoted = paths.appimage_file;
  quoted.replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
      .replace(QLatin1Char('"'), QStringLiteral("\\\""))
      .replace(QLatin1Char('`'), QStringLiteral("\\`"))
      .replace(QLatin1Char('$'), QStringLiteral("\\$"))
      .replace(QLatin1Char('%'), QStringLiteral("%%"));
  quoted.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
  CHECK(desktop_value(entry, QStringLiteral("Exec")) == QLatin1Char('"') + quoted + QStringLiteral("\" %F"));
  CHECK(patchy::ui::desktop_entry_exec_argument(paths.appimage_file) + QStringLiteral(" %F") ==
        desktop_value(entry, QStringLiteral("Exec")));
  auto try_exec = paths.appimage_file;
  CHECK(desktop_value(entry, QStringLiteral("TryExec")) == try_exec.replace(QLatin1Char('\\'), QStringLiteral("\\\\")));
  CHECK(desktop_value(entry, QStringLiteral("X-Patchy-AppImage")) == QStringLiteral("true"));
  // Everything else is the shipped file: the icon name the window's app id finds, the
  // formats Patchy opens, the localized names.
  for (const auto* key : {"Icon", "MimeType", "Name", "GenericName[ja]", "Categories", "StartupWMClass"}) {
    CHECK(!desktop_value(shipped, QString::fromLatin1(key)).isEmpty());
    CHECK(desktop_value(entry, QString::fromLatin1(key)) == desktop_value(shipped, QString::fromLatin1(key)));
  }
  CHECK(desktop_value(entry, QStringLiteral("Icon")) == QStringLiteral("com.rtsoft.patchy"));
  CHECK(patchy::ui::desktop_entry_starts_patchy_appimage(entry));

  const auto names = patchy::ui::appimage_menu_entry_icon_names();
  const auto targets = paths.icon_files();
  CHECK(names.size() == 8 && targets.size() == 8);
  for (qsizetype i = 0; i < names.size(); ++i) {
    CHECK(read_bytes(targets[i]) == read_bytes(QDir(paths.bundled_share).filePath(names[i])));
  }
  CHECK(QFileInfo::exists(
      QDir(paths.data_home).filePath(QStringLiteral("icons/hicolor/scalable/apps/com.rtsoft.patchy.svg"))));

  // A second Add finds everything in place and writes nothing.
  const auto again = patchy::ui::add_appimage_menu_entry(paths);
  CHECK(again.ok && !again.changed);

  const auto removed = patchy::ui::remove_appimage_menu_entry(paths);
  CHECK(removed.ok && removed.changed);
  CHECK(!QFileInfo::exists(paths.desktop_file()));
  for (const auto& target : targets) {
    CHECK(!QFileInfo::exists(target));
  }
  for (const auto& file : unrelated) {
    CHECK(QFileInfo::exists(file));
  }
  CHECK(QFileInfo(QDir(paths.data_home).filePath(QStringLiteral("icons/hicolor/256x256/apps"))).isDir());
  CHECK(patchy::ui::appimage_menu_entry_state(paths) == AppImageMenuEntryState::NotAdded);
  CHECK(!patchy::ui::remove_appimage_menu_entry(paths).ok);

  // A com.rtsoft.patchy.desktop Patchy did not write (a native install into ~/.local) is
  // someone else's: never replaced, never removed.
  const QByteArray foreign("[Desktop Entry]\nType=Application\nName=Patchy\nExec=/opt/patchy/bin/patchy %F\n");
  write_bytes(paths.desktop_file(), foreign);
  CHECK(patchy::ui::appimage_menu_entry_state(paths) == AppImageMenuEntryState::AddedElsewhere);
  CHECK(patchy::ui::appimage_menu_entry_provided_by(paths) == paths.desktop_file());
  CHECK(!patchy::ui::add_appimage_menu_entry(paths).ok);
  CHECK(!patchy::ui::remove_appimage_menu_entry(paths).ok);
  CHECK(read_bytes(paths.desktop_file()) == foreign);
  CHECK(QFile::remove(paths.desktop_file()));

  // No AppImage: nothing to add.
  auto native = paths;
  native.appimage_file.clear();
  CHECK(patchy::ui::appimage_menu_entry_state(native) == AppImageMenuEntryState::Unavailable);
  CHECK(!patchy::ui::add_appimage_menu_entry(native).ok);
  CHECK(!QFileInfo::exists(paths.desktop_file()));
}

// Consent was given once: when the AppImage file moves (or a new release ships another
// desktop file), the launch refresh rewrites the entry; it never creates one.
void appimage_menu_entry_follows_a_moved_appimage() {
  EntryFixture fixture;
  auto paths = fixture.paths;
  CHECK(patchy::ui::refresh_appimage_menu_entry(paths).ok);
  CHECK(!QFileInfo::exists(paths.desktop_file()));

  CHECK(patchy::ui::add_appimage_menu_entry(paths).ok);
  paths.appimage_file = QDir(fixture.root.path()).filePath(QStringLiteral("Moved/PatchyLinux.AppImage"));
  const auto moved = patchy::ui::refresh_appimage_menu_entry(paths);
  CHECK(moved.ok && moved.changed);
  const auto entry = read_bytes(paths.desktop_file());
  CHECK(desktop_value(entry, QStringLiteral("Exec")) == QLatin1Char('"') + paths.appimage_file + QStringLiteral("\" %F"));
  CHECK(desktop_value(entry, QStringLiteral("TryExec")) == paths.appimage_file);
  const auto unchanged = patchy::ui::refresh_appimage_menu_entry(paths);
  CHECK(unchanged.ok && !unchanged.changed);
  CHECK(patchy::ui::remove_appimage_menu_entry(paths).ok);
}

// AppImageLauncher (appimagekit_<hash>-Patchy.desktop with a rewritten icon name), Gear
// Lever (its own entry naming the moved .appimage) and a packaged Patchy (the Flatpak's
// export, a distribution package) each already put Patchy in the menu, so Patchy offers no
// second entry. Another program's AppImage entry does not count.
void appimage_menu_entry_detects_other_integrations() {
  EntryFixture fixture;
  const auto& paths = fixture.paths;
  const auto applications = QDir(paths.data_home).filePath(QStringLiteral("applications"));
  const auto other_appimage = QDir(applications).filePath(QStringLiteral("appimagekit_89ab-Krita.desktop"));
  write_bytes(other_appimage,
              "[Desktop Entry]\nType=Application\nName=Krita\nExec=/home/u/Applications/krita-5.2.AppImage %F\n"
              "Icon=appimagekit_89ab_krita\n");
  CHECK(patchy::ui::appimage_menu_entry_state(paths) == AppImageMenuEntryState::NotAdded);

  const struct {
    const char* file;
    QByteArray body;
  } integrations[] = {
      {"appimagekit_0123abcd-Patchy.desktop",
       "[Desktop Entry]\nType=Application\nName=Patchy\nExec=/home/u/Applications/PatchyLinux_0123abcd.AppImage %F\n"
       "TryExec=/home/u/Applications/PatchyLinux_0123abcd.AppImage\nIcon=appimagekit_0123abcd_com.rtsoft.patchy\n"
       "X-AppImage-Comment=Generated by appimagelauncherd\n"},
      {"patchy.desktop",
       "[Desktop Entry]\nType=Application\nName=Image Tool\nExec=\"/home/u/AppImages/patchy.appimage\" %F\n"
       "Icon=/home/u/.local/share/icons/patchy\n"},
  };
  for (const auto& integration : integrations) {
    const auto path = QDir(applications).filePath(QString::fromLatin1(integration.file));
    CHECK(patchy::ui::desktop_entry_starts_patchy_appimage(integration.body));
    write_bytes(path, integration.body);
    CHECK(patchy::ui::appimage_menu_entry_state(paths) == AppImageMenuEntryState::AddedElsewhere);
    CHECK(patchy::ui::appimage_menu_entry_provided_by(paths) == path);
    CHECK(!patchy::ui::add_appimage_menu_entry(paths).ok);
    CHECK(!QFileInfo::exists(paths.desktop_file()));
    CHECK(QFile::remove(path));
  }
  CHECK(!patchy::ui::desktop_entry_starts_patchy_appimage(read_bytes(other_appimage)));

  const auto packaged = QDir(paths.data_dirs.front()).filePath(QStringLiteral("applications/com.rtsoft.patchy.desktop"));
  write_bytes(packaged, read_bytes(QStringLiteral(PATCHY_SOURCE_DIR "/packaging/linux/com.rtsoft.patchy.desktop")));
  CHECK(patchy::ui::appimage_menu_entry_state(paths) == AppImageMenuEntryState::AddedElsewhere);
  CHECK(patchy::ui::appimage_menu_entry_provided_by(paths) == packaged);
}

// The Help item exists only for an AppImage run (never a native build, the Flatpak,
// Windows or macOS), reads Add or Remove by the entry's state, and is unavailable, with a
// tooltip saying why, when another tool already added Patchy.
void ui_appimage_menu_entry_item_follows_the_environment() {
  {
    patchy::ui::MainWindow window;
    show_window_empty(window);
    open_help_menu(window);
    CHECK(!help_menu_entry_action(window)->isVisible());
  }
  QTemporaryDir root;
  CHECK(root.isValid());
  const auto data_home = QDir(root.path()).filePath(QStringLiteral("data-home"));
  const auto share = QDir(root.path()).filePath(QStringLiteral("mount/usr/share"));
  populate_bundled_share(share);
  auto parent = QDir(QCoreApplication::applicationDirPath());
  parent.cdUp();
  const ScopedEnvironment env{
      {"APPIMAGE", QDir(root.path()).filePath(QStringLiteral("Apps/PatchyLinux.AppImage")).toUtf8()},
      {"APPDIR", parent.absolutePath().toUtf8()},
      {"XDG_DATA_HOME", data_home.toUtf8()},
      {"XDG_DATA_DIRS", QDir(root.path()).filePath(QStringLiteral("system-share")).toUtf8()},
  };
  patchy::ui::set_stored_appimage_menu_entry_prompt_declined(false);
  patchy::ui::MainWindow window;
  MainWindowTestAccess::set_appimage_bundled_share(window, share);
  show_window_empty(window);
  open_help_menu(window);
  auto* action = help_menu_entry_action(window);
#ifdef Q_OS_LINUX
  CHECK(action->isVisible() && action->isEnabled());
  CHECK(action->text() == QStringLiteral("Add Patchy to Applications Menu..."));
  CHECK(action->menuRole() == QAction::NoRole);
  open_help_menu(window, "ui_appimage_menu_entry_help_item");
  const auto desktop_file = QDir(data_home).filePath(QStringLiteral("applications/com.rtsoft.patchy.desktop"));

  action->trigger();
  CHECK(QFileInfo::exists(desktop_file));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Patchy was added to the applications menu"));
  open_help_menu(window);
  CHECK(action->isEnabled() && action->text() == QStringLiteral("Remove Patchy from Applications Menu"));
  open_help_menu(window, "ui_appimage_menu_entry_help_item_added");

  action->trigger();
  CHECK(!QFileInfo::exists(desktop_file));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Patchy was removed from the applications menu"));
  // Removing it answers the offer too.
  CHECK(patchy::ui::stored_appimage_menu_entry_prompt_declined());
  open_help_menu(window);
  CHECK(action->text() == QStringLiteral("Add Patchy to Applications Menu..."));

  const auto launcher = QDir(data_home).filePath(QStringLiteral("applications/appimagekit_0123abcd-Patchy.desktop"));
  write_bytes(launcher, "[Desktop Entry]\nType=Application\nName=Patchy\n"
                        "Exec=/home/u/Applications/PatchyLinux.AppImage %F\nIcon=appimagekit_0123abcd_com.rtsoft.patchy\n");
  open_help_menu(window, "ui_appimage_menu_entry_help_item_elsewhere");
  CHECK(action->isVisible() && !action->isEnabled());
  CHECK(action->toolTip().contains(QStringLiteral("already in the applications menu")));
  CHECK(QFile::remove(launcher));
#else
  // Windows and macOS have no AppImages: a stray APPIMAGE changes nothing.
  CHECK(!action->isVisible());
#endif
  patchy::ui::set_stored_appimage_menu_entry_prompt_declined(false);
}

// The startup offer: Add writes the entry, Not Now leaves the next launch free to ask,
// Don't Ask Again stops it for good, Add after Help already added it changes nothing, and
// an unattended run never sees it.
void ui_appimage_menu_entry_offer_dont_ask_again() {
  QTemporaryDir root;
  CHECK(root.isValid());
  const auto data_home = QDir(root.path()).filePath(QStringLiteral("data-home"));
  const auto share = QDir(root.path()).filePath(QStringLiteral("mount/usr/share"));
  populate_bundled_share(share);
  auto parent = QDir(QCoreApplication::applicationDirPath());
  parent.cdUp();
  const ScopedEnvironment env{
      {"APPIMAGE", QDir(root.path()).filePath(QStringLiteral("Apps/PatchyLinux.AppImage")).toUtf8()},
      {"APPDIR", parent.absolutePath().toUtf8()},
      {"XDG_DATA_HOME", data_home.toUtf8()},
      {"XDG_DATA_DIRS", QDir(root.path()).filePath(QStringLiteral("system-share")).toUtf8()},
  };
  patchy::ui::set_stored_appimage_menu_entry_prompt_declined(false);
  patchy::ui::MainWindow window;
  MainWindowTestAccess::set_appimage_bundled_share(window, share);
  show_window_empty(window);
  const auto desktop_file = QDir(data_home).filePath(QStringLiteral("applications/com.rtsoft.patchy.desktop"));
#ifdef Q_OS_LINUX
  // Answers the offer with `button`; returns whether it appeared.
  const auto answer = [&window](const char* button, const char* artifact = nullptr) {
    bool saw = false;
    QTimer::singleShot(0, [&saw, button, artifact] {
      auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("appImageMenuEntryOfferMessageBox")));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      CHECK(dialog->text().contains(QStringLiteral("Help > Add Patchy to Applications Menu")));
      CHECK(!dialog->isModal());
      if (artifact != nullptr) {
        save_widget_artifact(artifact, *dialog);
      }
      auto* choice = dialog->findChild<QAbstractButton*>(QString::fromLatin1(button));
      CHECK(choice != nullptr);
      saw = true;
      if (choice != nullptr) {
        choice->click();
      }
    });
    const bool shown = MainWindowTestAccess::offer_appimage_menu_entry(window);
    CHECK(shown == saw);
    return shown;
  };

  CHECK(answer("appImageMenuEntryNotNowButton", "ui_appimage_menu_entry_offer"));
  CHECK(!QFileInfo::exists(desktop_file));
  CHECK(!patchy::ui::stored_appimage_menu_entry_prompt_declined());

  CHECK(answer("appImageMenuEntryAddButton"));
  CHECK(QFileInfo::exists(desktop_file));
  // Added: nothing left to offer.
  CHECK(!MainWindowTestAccess::offer_appimage_menu_entry(window));
  CHECK(patchy::ui::remove_appimage_menu_entry(patchy::ui::resolve_appimage_menu_entry_paths()).ok);

  // The offer is non-modal: Help can add the entry while it is open, and its Add then leaves
  // that entry alone instead of flipping it back off.
  {
    bool saw = false;
    QTimer::singleShot(0, [&window, &saw] {
      auto* dialog = find_top_level_dialog(QStringLiteral("appImageMenuEntryOfferMessageBox"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      help_menu_entry_action(window)->trigger();
      auto* add = dialog->findChild<QAbstractButton*>(QStringLiteral("appImageMenuEntryAddButton"));
      CHECK(add != nullptr);
      saw = true;
      if (add != nullptr) {
        add->click();
      }
    });
    CHECK(MainWindowTestAccess::offer_appimage_menu_entry(window));
    CHECK(saw);
    CHECK(QFileInfo::exists(desktop_file));
    CHECK(patchy::ui::remove_appimage_menu_entry(patchy::ui::resolve_appimage_menu_entry_paths()).ok);
  }

  CHECK(answer("appImageMenuEntryDontAskButton"));
  CHECK(patchy::ui::stored_appimage_menu_entry_prompt_declined());
  CHECK(!QFileInfo::exists(desktop_file));
  CHECK(!MainWindowTestAccess::offer_appimage_menu_entry(window));

  patchy::ui::set_stored_appimage_menu_entry_prompt_declined(false);
  window.set_cli_automation_mode(true);
  CHECK(!MainWindowTestAccess::offer_appimage_menu_entry(window));
  window.set_cli_automation_mode(false);
#else
  CHECK(!MainWindowTestAccess::offer_appimage_menu_entry(window));
  CHECK(!QFileInfo::exists(desktop_file));
#endif
  patchy::ui::set_stored_appimage_menu_entry_prompt_declined(false);
}

}  // namespace

std::vector<patchy::test::TestCase> appimage_menu_entry_tests() {
  return {
      {"appimage_menu_entry_writes_and_removes_only_its_files", appimage_menu_entry_writes_and_removes_only_its_files},
      {"appimage_menu_entry_follows_a_moved_appimage", appimage_menu_entry_follows_a_moved_appimage},
      {"appimage_menu_entry_detects_other_integrations", appimage_menu_entry_detects_other_integrations},
      {"ui_appimage_menu_entry_item_follows_the_environment", ui_appimage_menu_entry_item_follows_the_environment},
      {"ui_appimage_menu_entry_offer_dont_ask_again", ui_appimage_menu_entry_offer_dont_ask_again},
  };
}
