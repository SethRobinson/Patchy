#pragma once

#include <QString>
#include <QUrl>

#include <functional>
#include <span>

class QAction;
class QMessageBox;
class QWidget;

namespace patchy::ui {

// The web build's answer to a command it cannot run (docs/wasm.md, "Desktop-only
// commands"): the command stays in its menu, and running it says that it needs the
// desktop version and offers the download page. Compiled on every platform so the
// desktop UI suite can check the list and the prompt; only the web build applies it.

// Set on a command made desktop-only (make_desktop_only_command).
inline constexpr char kDesktopOnlyCommandProperty[] = "patchy.desktopOnlyCommand";

// Where "Get the Desktop Version" leads: the README's download section.
[[nodiscard]] QUrl desktop_download_url();
// Opens that page in the browser (a new tab, from the web build).
void open_desktop_download_page();
// Tests replace how the page opens; an empty function restores the browser.
void set_desktop_download_opener_for_testing(std::function<void(const QUrl&)> opener);

// Adds "Get the Desktop Version" to `box` as its default button, runs the box and opens
// the download page when that button answers it. The box keeps the buttons it has.
void exec_message_box_with_desktop_download(QMessageBox& box);

// The box itself, for a message of the caller's own (the G'MIC row's).
void show_desktop_only_message(QWidget* parent, const QString& title, const QString& text,
                               const QString& object_name = QString());
// The standard wording: `feature` does not work in the web version.
void show_desktop_only_feature_message(QWidget* parent, const QString& feature);

// A command's place in the menu bar of `window`, as the prompt names it: "File › Import ›
// Files as Layers" (the walk Search Commands lists commands by; mnemonics and the trailing
// ellipsis removed). An action in none of those menus, or no window, gives its own name.
[[nodiscard]] QString action_menu_path(const QAction* action, const QWidget* window);

// One command the web build keeps but cannot run.
struct DesktopOnlyCommand {
  const char* action_name;         // the QAction's objectName (permanent)
  const char* message = nullptr;   // replaces the standard sentence (MainWindow context)
  const char* box_name = nullptr;  // the message box's objectName when it has its own
};
[[nodiscard]] std::span<const DesktopOnlyCommand> web_desktop_only_commands();

// Turns `action` into a desktop-only command: every handler `owner` connected to its
// triggered signal is replaced by the prompt. Its text, place, key and enabled state
// stay as they are.
void make_desktop_only_command(QAction* action, QWidget* owner, const DesktopOnlyCommand& command);
// Applies web_desktop_only_commands to the actions of `window` (found by objectName; a
// command this platform does not build is skipped).
void apply_web_desktop_only_commands(QWidget& window);

}  // namespace patchy::ui
