#pragma once

#include <QDialog>
#include <QPointer>
#include <QString>

#include <functional>
#include <optional>

class QLabel;
class QVBoxLayout;

namespace patchy::ui {

// A floating brush window (Window > Brush Tips, Window > Brush Dynamics and Effects, or a popup's Keep Open
// button; GitHub issue 506 follow-up): a non-modal Qt::Tool dialog owned by the main window, so
// it stays above it and closes with it. Its content edits the active tool live while the user
// paints. Keys that no text field in it takes (tool letters, [ and ], Space) go to the canvas,
// so painting shortcuts keep working while it has focus. The position follows the dialog
// position memory; the size and whether it was open persist under `<settings_prefix>Size` and
// `<settings_prefix>Open`. Closing hides it; the owner keeps the one instance.
class BrushToolWindow : public QDialog {
  Q_OBJECT

public:
  BrushToolWindow(const QString& object_name, const QString& title, const QString& settings_prefix,
                  QWidget* owner);

  // The window's content (it takes ownership); call once.
  void set_body(QWidget* body);
  // Empty = usable. Otherwise the body is disabled under this one-line reason (the active
  // tool has no brush tip, no document is open).
  void set_unavailable_reason(const QString& reason);
  [[nodiscard]] bool available() const noexcept { return unavailable_reason_.isEmpty(); }
  // Where unclaimed keys go: the active canvas.
  void set_key_target(std::function<QWidget*()> target) { key_target_ = std::move(target); }

  // Shows the window (at `global_top_left` when given, else where it was last), raises it and
  // records it open. `activate` false leaves the main window active (a restore at startup).
  void open_window(std::optional<QPoint> global_top_left = std::nullopt, bool activate = true);
  // The main window is closing: hide without recording the window closed, so it comes back
  // with the next launch.
  void hide_for_shutdown();

  [[nodiscard]] static bool remembered_open(const QString& settings_prefix);

protected:
  void done(int result) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  bool forward_key(QKeyEvent* event);
  void record_open(bool open) const;

  QString settings_prefix_;
  QVBoxLayout* content_{nullptr};
  QLabel* unavailable_label_{nullptr};
  QPointer<QWidget> body_;
  QString unavailable_reason_;
  std::function<QWidget*()> key_target_;
  bool size_restored_{false};
  bool shutting_down_{false};
};

// Gives every button, check box, slider, combo box and list in `root` no keyboard focus, so a
// click on one leaves the keys with the canvas (BrushToolWindow forwards them); text fields
// and spin boxes keep their focus for typing.
void leave_keys_to_canvas(QWidget& root);

}  // namespace patchy::ui
