#pragma once

// The G'MIC Filter Library dialog: a searchable tree of the offered filters on
// the left, the selected filter's generated controls on the right. It chooses a
// filter and its parameter values and reports each change through the preview
// callback; MainWindow runs the engine (main_window_gmic.cpp). See docs/gmic.md.

#include "filters/gmic/gmic_catalog.hpp"

#include <QDialog>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace patchy::ui {

class GmicParameterPanel;

struct GmicFilterChoice {
  std::string filter_id;
  std::vector<std::string> values;  // one per value-carrying parameter
  // Which layers the filter receives (gmic::Filter::input_mode): '.' active, '*' all,
  // '+' active and below, '-' active and above, 'v' visible, 'i' hidden.
  char input_mode{'.'};
};

// What the canvas should show while the dialog is open: the filter and values to
// preview, or nothing (no filter chosen, or Preview unchecked).
struct GmicPreviewRequest {
  std::optional<GmicFilterChoice> choice;
  bool enabled{true};
};

template <typename Settings>
class CoalescedPreviewEmitter;

class GmicFilterLibraryDialog : public QDialog {
  Q_OBJECT

public:
  // `catalog` must outlive the dialog. `engine_version` is shown in the credit line.
  GmicFilterLibraryDialog(const gmic::Catalog& catalog, const QString& engine_version, QWidget* parent = nullptr);
  ~GmicFilterLibraryDialog() override;

  // Called when the filter, a preview-refreshing value, or the Preview box changes.
  // Slider drags are coalesced; everything else is delivered at once.
  void set_preview_callback(std::function<void(const GmicPreviewRequest&)> callback);
  // A finished run's returned status: updates the controls of `filter_id` if it is
  // still the one shown (gmic_parameter_panel.hpp).
  void apply_status(const std::string& filter_id, const std::string& status);
  // Puts the shown filter's pressed buttons back to 0 once a run has used them
  // (gmic_parameter_panel.hpp). Requests no preview.
  void release_buttons();
  // A line under the filter's name (a failed preview); empty hides it.
  void set_preview_message(const QString& text);
  // What applying the previewed result will do beyond what the canvas shows (layers
  // changed, resized or added), one sentence each, in a banner; empty hides it. Cleared
  // whenever the filter or the input layers change.
  void set_result_notes(const QStringList& notes);
  [[nodiscard]] QStringList result_notes() const;
  // About G'MIC: what it is, who makes it, how its name is said, and its website.
  // Modal; the footer's About G'MIC button opens it.
  void show_about_gmic();
  [[nodiscard]] bool preview_enabled() const;

  // The shown filter's point parameters as canvas handles: called with their positions
  // (percent of the image the filter works on) whenever the filter, a position or the
  // input layers change; empty when there is nothing to show. `move_point` is the way
  // back, for a handle the user dragged. Blur [Depth-of-Field] and Blur [Motion]
  // would get none (docs/legal-constraints-filters.md, "G'MIC engine"); neither is offered.
  void set_points_callback(std::function<void(const std::vector<QPointF>&)> callback);
  void move_point(int index, QPointF percent);

  // Selects a filter (false when the catalog has no such id) and, when given,
  // restores its parameter values.
  // `input_mode` 0 keeps the filter's declared one.
  bool select_filter(const std::string& filter_id, const std::vector<std::string>& values = {},
                     char input_mode = 0);
  [[nodiscard]] std::optional<GmicFilterChoice> choice() const;

  // Shows only the filters whose name or folder contains every word of `text`.
  void set_search_text(const QString& text);
  [[nodiscard]] int visible_filter_count() const;

protected:
  void keyPressEvent(QKeyEvent* event) override;

private:
  void rebuild_tree();
  void apply_search();
  void show_filter(const gmic::Filter* filter, const std::vector<std::string>& values);
  void request_preview(bool immediate);
  void report_points();

  const gmic::Catalog& catalog_;
  QString engine_version_;
  const gmic::Filter* current_{nullptr};
  QLineEdit* search_{nullptr};
  QTreeWidget* tree_{nullptr};
  QLabel* title_{nullptr};
  QLabel* folder_{nullptr};
  GmicParameterPanel* panel_{nullptr};
  QPushButton* reset_{nullptr};
  QPushButton* ok_{nullptr};
  QCheckBox* preview_{nullptr};
  QComboBox* input_{nullptr};
  QLabel* message_{nullptr};
  QLabel* notes_{nullptr};
  std::function<void(const GmicPreviewRequest&)> preview_callback_;
  std::function<void(const std::vector<QPointF>&)> points_callback_;
  std::unique_ptr<CoalescedPreviewEmitter<GmicPreviewRequest>> preview_emitter_;
};

}  // namespace patchy::ui
