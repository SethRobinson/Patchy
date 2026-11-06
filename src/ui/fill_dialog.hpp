#pragma once

// Edit > Fill... (GitHub issues 530, 575; docs/fill-command.md): what to fill with, and how
// it combines with the layer. MainWindow::edit_fill_dialog runs it and fills through fill_rect.

#include "core/paint_mode.hpp"

#include <QColor>

#include <QObject>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

class QComboBox;
class QHBoxLayout;
class QPushButton;
class QWidget;

namespace patchy::ui {

// ContentAware (GitHub issue 575) is not a color: it fills the selection from its surroundings
// with Edit > Remove Object's engine (MainWindow::content_aware_fill_selection). Transparent and
// Neutral (the blend mode's neutral color) are New Layer's (GitHub issue 654), never Edit > Fill's.
enum class FillContents : std::uint8_t {
  Foreground,
  Background,
  Color,
  Black,
  Gray50,
  White,
  ContentAware,
  Transparent,
  Neutral
};

struct FillDialogSettings {
  FillContents contents{FillContents::Foreground};
  QColor color{Qt::black};  // the Color... choice; kept while another Contents is picked
  PaintMode mode{};
  int opacity{100};  // 1..100
  bool preserve_transparency{false};
  bool content_aware_new_layer{false};  // Content-Aware's Add to New Layer
};

// Settings tokens for Contents (tools/editFillContents). Permanent identifiers.
[[nodiscard]] const char* fill_contents_token(FillContents contents) noexcept;
[[nodiscard]] FillContents fill_contents_from_token(const QString& token, FillContents fallback) noexcept;

// The color a Contents choice fills with: the foreground or background color, the chosen
// color, or black, 50% gray (128, 128, 128) or white. ContentAware, Transparent and Neutral
// have none here (the foreground; FillContentsField knows the neutral color).
[[nodiscard]] QColor fill_contents_color(const FillDialogSettings& settings, QColor foreground, QColor background);

// The Contents list and its color swatch (GitHub issues 530, 575, 654): Edit > Fill's, shared
// with Layer > New > New Layer. Builds the combo and the swatch as children of `parent` and adds
// them to `row`. The list holds `choices` in order, with a separator before the colors that
// follow Transparent, before Black, and before Content-Aware and the neutral color. Every entry
// fits closed and open (fit_combo_to_entries, GitHub issue 655). The swatch shows the color the
// current choice fills with and hides (keeping its room) when there is none (Transparent,
// Content-Aware). Its "fillColor" property holds the Color... choice and is that choice's
// source of truth, so automation can set it without driving the picker. Choosing Color... (from
// the list or a swatch click) opens the color picker at once, from the color the swatch showed;
// cancelling it goes back to the previous choice. Owned by the combo.
struct FillContentsFieldOptions {
  std::vector<FillContents> choices;
  FillContents initial{FillContents::Foreground};
  QColor color{Qt::black};  // the Color... choice
  QColor foreground;
  QColor background;
  QString combo_object_name;
  QString swatch_object_name;
};

class FillContentsField final : public QObject {
public:
  FillContentsField(QWidget* parent, QHBoxLayout* row, const FillContentsFieldOptions& options);

  [[nodiscard]] QComboBox* combo() const noexcept { return combo_; }
  [[nodiscard]] QPushButton* swatch() const noexcept { return swatch_; }
  [[nodiscard]] FillContents contents() const;
  void set_contents(FillContents contents);
  // The Color... choice (the swatch's property).
  [[nodiscard]] QColor chosen_color() const;
  // What the current choice fills with; invalid for Transparent and Content-Aware.
  [[nodiscard]] QColor fill_color() const;
  // Turns an entry on or off; a tooltip says why while it is off.
  void set_choice_enabled(FillContents contents, bool enabled, const QString& disabled_tooltip = QString());
  // The Neutral entry's color, listed last while set (it must be among `choices`); nullopt
  // takes the entry out, and a list showing it goes back to its first entry.
  void set_neutral_color(std::optional<QColor> color);

private:
  void refresh_swatch();
  void pick_color();
  [[nodiscard]] int index_of(FillContents contents) const;

  QComboBox* combo_{};
  QPushButton* swatch_{};
  QColor foreground_;
  QColor background_;
  std::optional<QColor> neutral_;
  bool has_neutral_choice_{false};
  int previous_index_{0};
};

// The last accepted settings (tools/editFill*), or the defaults.
[[nodiscard]] FillDialogSettings load_fill_dialog_settings();
void save_fill_dialog_settings(const FillDialogSettings& settings);

// Runs the modal Fill dialog (object name patchyFillDialog), starting from the remembered
// settings. Choosing Color... in Contents opens the color picker at once; cancelling the
// picker goes back to the previous choice. Content-Aware is offered only when
// `content_aware_available` (a selection on a layer's pixels); while it is chosen Add to New
// Layer is enabled, Preserve Transparency is disabled, and Mode and Opacity are enabled only
// with Add to New Layer (layer blend modes only: Behind and Clear are off). Returns the accepted
// settings (and remembers them), or nullopt on Cancel.
[[nodiscard]] std::optional<FillDialogSettings> request_fill_settings(QWidget* parent, QColor foreground,
                                                                      QColor background,
                                                                      bool content_aware_available = false);

}  // namespace patchy::ui
