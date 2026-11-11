#include "ui/desktop_only_feature.hpp"

#include "ui/command_search.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/hotkey_registry.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QWidget>

#include <array>
#include <utility>

namespace patchy::ui {
namespace {

std::function<void(const QUrl&)>& download_opener() {
  static std::function<void(const QUrl&)> opener;
  return opener;
}

// The commands the web build keeps in its menus and answers with the prompt, by
// objectName. Why each cannot run in a browser tab is in docs/wasm.md ("Desktop-only
// commands"). Adding a command here is all the web build needs, as long as the window
// itself connects the command's handler; main_window_actions_menus.cpp builds the rows
// the web build would otherwise lack.
constexpr std::array kWebDesktopOnlyCommands{
    // File: folders, several files at once, links to files, scanners and printers.
    DesktopOnlyCommand{"fileOpenFolderAction"},
    DesktopOnlyCommand{"fileImportScannerAction"},
    DesktopOnlyCommand{"fileImportPhotocopyAction"},
    DesktopOnlyCommand{"fileImportDivideScannedAction"},
    DesktopOnlyCommand{"fileImportScanBookAction"},
    DesktopOnlyCommand{"fileImportFilesAsLayersAction"},
    DesktopOnlyCommand{"filePlaceLinkedAction"},
    DesktopOnlyCommand{"fileExportDocumentsToFolderAction"},
    DesktopOnlyCommand{"fileExportImageSequenceAction"},
    DesktopOnlyCommand{"filePageSetupAction"},
    DesktopOnlyCommand{"filePrintAction"},
    // Filter: the G'MIC engine and the GEGL helper process are not in the web build.
    DesktopOnlyCommand{"filterGmicLibraryAction",
                       QT_TRANSLATE_NOOP("patchy::ui::MainWindow",
                                         "The G'MIC Filter Library, nearly 400 filters, is part of the desktop "
                                         "version of Patchy for Windows, macOS and Linux."),
                       "gmicDesktopOnlyMessageBox"},
    DesktopOnlyCommand{"filterGeglFiltersAction"},
    // Plugins: no network backend for the OpenAI requests, no other apps to talk to.
    DesktopOnlyCommand{"pluginsOpenAiImagesAction"},
    // Window: a browser tab is one window.
    DesktopOnlyCommand{"windowFloatDocumentAction"},
    DesktopOnlyCommand{"windowFloatAllAction"},
    DesktopOnlyCommand{"windowTileAction"},
    DesktopOnlyCommand{"windowCascadeAction"},
    // Help: the MCP connector is a desktop program.
    DesktopOnlyCommand{"helpAiSetupAction"},
};

}  // namespace

QUrl desktop_download_url() {
  return QUrl(QStringLiteral("https://github.com/SethRobinson/Patchy#download"));
}

void open_desktop_download_page() {
  if (const auto& opener = download_opener(); opener) {
    opener(desktop_download_url());
    return;
  }
  QDesktopServices::openUrl(desktop_download_url());
}

void set_desktop_download_opener_for_testing(std::function<void(const QUrl&)> opener) {
  download_opener() = std::move(opener);
}

void exec_message_box_with_desktop_download(QMessageBox& box) {
  auto* download_button = box.addButton(QObject::tr("Get the Desktop Version"), QMessageBox::ActionRole);
  download_button->setObjectName(QStringLiteral("desktopDownloadButton"));
  box.setDefaultButton(download_button);
  exec_dialog(box);
  if (box.clickedButton() == download_button) {
    open_desktop_download_page();
  }
}

void show_desktop_only_message(QWidget* parent, const QString& title, const QString& text,
                               const QString& object_name) {
  QMessageBox box(QMessageBox::Information, title, text, QMessageBox::Ok, parent);
  // Command names are plain text; one with a '<' must not turn the box into rich text.
  box.setTextFormat(Qt::PlainText);
  box.setObjectName(object_name.isEmpty() ? QStringLiteral("desktopOnlyFeatureMessageBox") : object_name);
  exec_message_box_with_desktop_download(box);
}

void show_desktop_only_feature_message(QWidget* parent, const QString& feature) {
  show_desktop_only_message(
      parent, QObject::tr("Desktop Version Only"),
      QObject::tr("%1 does not work in the web version of Patchy. To use it, download the desktop version, "
                  "which is free for Windows, macOS and Linux.")
          .arg(feature));
}

QString action_menu_path(const QAction* action, const QWidget* window) {
  if (action == nullptr) {
    return {};
  }
  QStringList names;
  // The walk Search Commands lists commands by, so the prompt and the search agree on
  // where a command is. Hidden menus count: the place is the same when one is disabled.
  if (const auto* menu_bar = window != nullptr ? window->findChild<QMenuBar*>() : nullptr; menu_bar != nullptr) {
    for_each_menu_bar_command(menu_bar, /*visible_only=*/false,
                              [&](QAction* candidate, const QString& top, const QStringList& submenus) {
                                if (candidate == action && names.isEmpty()) {
                                  names << top << submenus;
                                }
                              });
  }
  names << strip_menu_ellipsis(clean_action_text(action));
  return names.join(QStringLiteral(" › "));
}

std::span<const DesktopOnlyCommand> web_desktop_only_commands() {
  return kWebDesktopOnlyCommands;
}

void make_desktop_only_command(QAction* action, QWidget* owner, const DesktopOnlyCommand& command) {
  if (action == nullptr || owner == nullptr || action->property(kDesktopOnlyCommandProperty).toBool()) {
    return;
  }
  action->setProperty(kDesktopOnlyCommandProperty, true);
  // Only the owner's handlers go: a menu holding the action keeps its own connection,
  // which is what makes QMenu::triggered fire.
  QObject::disconnect(action, &QAction::triggered, owner, nullptr);
  const QString message = command.message != nullptr ? QString::fromUtf8(command.message) : QString();
  const QString box_name = command.box_name != nullptr ? QString::fromLatin1(command.box_name) : QString();
  QObject::connect(action, &QAction::triggered, owner, [action, owner, message, box_name] {
    if (message.isEmpty()) {
      show_desktop_only_feature_message(owner, action_menu_path(action, owner));
      return;
    }
    show_desktop_only_message(owner, strip_menu_ellipsis(clean_action_text(action)),
                              QCoreApplication::translate("patchy::ui::MainWindow", message.toUtf8().constData()),
                              box_name);
  });
}

void apply_web_desktop_only_commands(QWidget& window) {
  for (const auto& command : web_desktop_only_commands()) {
    make_desktop_only_command(window.findChild<QAction*>(QString::fromLatin1(command.action_name)), &window, command);
  }
}

}  // namespace patchy::ui
