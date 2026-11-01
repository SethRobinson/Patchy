#pragma once

// The companion box shown while a GIMP 3 plug-in runs (docs/gimp-plugins.md,
// "Running"). Like the legacy plug-in box it never takes keyboard focus, because the
// plug-in's own dialog lives in another process and must keep the foreground: while
// that dialog is up the box says to adjust the settings there (its preview updates as
// they change) and click OK; while the plug-in works it shows its progress. Its button
// ends the plug-in and leaves the layer as it was.

#include <QDialog>
#include <QRect>
#include <QString>

class QLabel;
class QProgressBar;
class QPushButton;

namespace patchy::ui {

class GimpPluginRunDialog : public QDialog {
  Q_OBJECT

public:
  enum class State {
    Starting,        // the plug-in is starting, no window yet
    WaitingForUser,  // its dialog is up
    Applying,        // it reports progress, or works without a window
  };

  explicit GimpPluginRunDialog(const QString& plugin_name, QWidget* parent = nullptr);

  // The plug-in's largest visible window (screen pixels), invalid when none is up.
  void set_plugin_window(const QRect& screen_rect);
  // fraction in 0..1, or < 0 for busy; text is the plug-in's progress message.
  void set_progress(double fraction, const QString& text);
  // Shows the box unless the user already asked to stop.
  void reveal();

  [[nodiscard]] bool stop_requested() const noexcept { return stop_requested_; }
  [[nodiscard]] State state() const noexcept { return state_; }

protected:
  void done(int result) override;

private:
  void update_state();
  void place_below_plugin_window();

  QString plugin_name_;
  QString progress_text_;
  QLabel* message_{nullptr};
  QLabel* hint_{nullptr};
  QProgressBar* bar_{nullptr};
  QPushButton* stop_button_{nullptr};
  State state_{State::Starting};
  QRect plugin_window_rect_;
  bool progress_seen_{false};
  bool stop_requested_{false};
  bool placed_below_plugin_{false};
};

}  // namespace patchy::ui
