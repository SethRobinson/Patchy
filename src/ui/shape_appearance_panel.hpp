#pragma once

#include "core/layer.hpp"
#include "core/pattern_resource.hpp"
#include "ui/shape_appearance_dialog.hpp"

#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class QTimer;
class QWidget;

namespace patchy::ui {

class BrushToolWindow;

// The stay-open Shape Appearance window (docs/vector-tools-appearance-window.md): a floating
// tool window that follows the selected shape layers and applies every change at once, so
// the document stays editable while it is open.
//
// It keeps no Layer, Document, session or canvas pointer between events (the document can
// change or close under it): a target is a session id and layer ids, resolved again by
// the host on every use.

// The layers a page was built for, and what building it needs.
struct ShapeAppearanceTarget {
  std::int64_t session_id{0};
  std::vector<LayerId> ids;  // editable selected shape layers, the active one first
  // Each target's render and content revision: any change to a layer rebuilds the page.
  std::vector<std::pair<std::uint64_t, std::uint64_t>> revisions;
  std::size_t selected_count{0};
  AppearanceDialogContext<ShapeAppearanceSettings> context;  // originals, names
  // initial, reset_defaults, libraries, colors, units and custom_shapes; the panel fills
  // the rest.
  ShapeAppearanceBodyOptions options;

  [[nodiscard]] bool same_layers_as(const ShapeAppearanceTarget& other) const;
  [[nodiscard]] bool same_page_as(const ShapeAppearanceTarget& other) const;
};

// One gesture in the window (a scrub, a held stepper, a typed value): one undo step. The
// host pushes the undo snapshot before the first write, then rewrites the same layers from
// `bases` for every later value, so the document never holds a change that is not in
// history.
struct ShapeAppearanceLiveRun {
  std::int64_t session_id{0};
  std::vector<LayerId> ids;
  std::string key;               // the property being changed; another one starts a new run
  std::vector<Layer> bases;      // the targets as the run found them
  PatternStore base_patterns;    // the document's pattern store then
  // The targets' revisions after the run's last write: a layer that no longer has them was
  // changed by something else, and the run ends instead of writing over it.
  std::vector<std::pair<std::uint64_t, std::uint64_t>> written;
  int steps{0};
  // A large full-canvas gradient fill takes the draft bake while the run lasts (the
  // Gradient tool's handle drags do the same, docs/gradients-fill-handles.md); the run's
  // end bakes `last` exactly.
  bool drafted{false};
  std::optional<ShapeAppearanceSettings> last;
};

struct ShapeAppearancePanelHost {
  // Empty when the window can edit. Otherwise why not (no document, another dialog holds
  // the edit lock, a transform is running, no editable shape layer is selected).
  std::function<QString()> unavailable_reason;
  std::function<std::optional<ShapeAppearanceTarget>()> target;
  // One change from the page: the first of a run records the undo step.
  std::function<void(const ShapeAppearanceSettings&)> apply;
  // The run went idle.
  std::function<void()> finish_run;
  // A dialog the page opens (color picker, Gradient Manager, Shapes Manager). `body` runs
  // it and returns false on Cancel.
  std::function<void(const std::function<bool()>& body)> run_child_dialog;
  // Where the window's unclaimed keys go: the active canvas.
  std::function<QWidget*()> key_target;
  // gradient_handles_wanted() may have changed (the box was toggled, the window was shown
  // or hidden).
  std::function<void()> gradient_handles_changed;
};

// The settings keys of the "Show handles" boxes (persisted identifiers): whether a gradient
// shows its handles, and whether the stroke's does when the fill is a gradient too.
inline constexpr const char* kShapeAppearanceGradientHandlesKey = "shapeAppearance/showGradientHandles";
inline constexpr const char* kShapeAppearanceGradientHandlesOnStrokeKey = "shapeAppearance/gradientHandlesOnStroke";

class ShapeAppearancePanel : public QObject {
  Q_OBJECT

public:
  ShapeAppearancePanel(QWidget* owner, ShapeAppearancePanelHost host);
  ~ShapeAppearancePanel() override;

  // Shows the window for the current target and raises it.
  void open();
  [[nodiscard]] bool is_open() const;
  [[nodiscard]] QWidget* window() const;
  // The main window is closing.
  void hide_for_shutdown();

  // The selection, document or history may have changed: disable the window, or rebuild
  // its page for the new target. The rebuild is deferred: a page is never deleted from
  // inside one of its own handlers, or while a dialog opened from it is running.
  void sync();

  // The targets the visible page edits.
  [[nodiscard]] bool page_targets(std::int64_t session_id, const std::vector<LayerId>& ids) const;
  [[nodiscard]] ShapeAppearanceSettings page_settings() const;

  // The open run, owned here and driven by the host.
  [[nodiscard]] std::optional<ShapeAppearanceLiveRun>& run() noexcept { return run_; }
  void restart_idle_timer();
  // The host ended the run: its edits are applied, so the page captures later ones
  // against the layers as they are now.
  void run_finished();

  // A dialog a row opened runs under the edit lock, between these two calls: the page
  // stays as it is (no rebuild, no disabling) and its changes are previews, sent to
  // `preview` instead of applied. The host commits or restores when the dialog closes.
  void begin_child_dialog();
  void set_child_preview(std::function<void(const ShapeAppearanceSettings&)> preview);
  void end_child_dialog();
  [[nodiscard]] bool child_dialog_running() const noexcept { return child_dialog_depth_ > 0; }

  // Rebuilds waiting on the deferral run now when nothing blocks them (tests).
  void flush_pending_rebuild();

  // The "Show handles" boxes (GitHub issue 694): the canvas shows one gradient's handles
  // under any tool while the window is open and a box is ticked, the stroke's when
  // gradient_handles_on_stroke() and the stroke is a gradient. The state is the window's,
  // kept in the settings, so every page starts from it.
  [[nodiscard]] bool gradient_handles_wanted() const;
  [[nodiscard]] bool gradient_handles_on_stroke() const noexcept { return gradient_handles_on_stroke_; }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void set_gradient_handles(bool show, bool on_stroke);
  void changed(const ShapeAppearanceSettings& settings);
  void schedule_rebuild();
  void rebuild();
  [[nodiscard]] bool rebuild_blocked() const;
  void build_page(ShapeAppearanceTarget target);
  void show_unavailable(const QString& reason);
  void rebase_page();

  ShapeAppearancePanelHost host_;
  QPointer<QWidget> owner_;
  QPointer<BrushToolWindow> window_;
  QPointer<QWidget> page_;
  ShapeAppearanceBody body_;
  std::optional<ShapeAppearanceTarget> current_;
  std::optional<ShapeAppearanceLiveRun> run_;
  std::function<void(const ShapeAppearanceSettings&)> child_preview_;
  QTimer* rebuild_timer_{nullptr};
  QTimer* idle_timer_{nullptr};
  int child_dialog_depth_{0};
  bool applying_{false};
  bool sync_wanted_{false};
  bool gradient_handles_{true};
  bool gradient_handles_on_stroke_{false};
};

}  // namespace patchy::ui
