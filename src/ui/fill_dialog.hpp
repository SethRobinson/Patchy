#pragma once

// Edit > Fill... (GitHub issues 530, 575; docs/fill-command.md): what to fill with, and how
// it combines with the layer. MainWindow::edit_fill_dialog runs it and fills through fill_rect.

#include "core/paint_mode.hpp"

#include <QColor>

#include <cstdint>
#include <optional>

class QString;
class QWidget;

namespace patchy::ui {

// ContentAware (GitHub issue 575) is not a color: it fills the selection from its surroundings
// with Edit > Remove Object's engine (MainWindow::content_aware_fill_selection).
enum class FillContents : std::uint8_t { Foreground, Background, Color, Black, Gray50, White, ContentAware };

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
// color, or black, 50% gray (128, 128, 128) or white. ContentAware has none (the foreground).
[[nodiscard]] QColor fill_contents_color(const FillDialogSettings& settings, QColor foreground, QColor background);

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
