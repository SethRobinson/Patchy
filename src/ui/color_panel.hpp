#pragma once

#include <QColor>
#include <QString>

#include <functional>

class QDialog;
class QWidget;

namespace photoslop::ui {

[[nodiscard]] QString color_button_style(QColor color);
[[nodiscard]] QString swatch_button_style(QColor color, bool large = false);
[[nodiscard]] QString inline_text_editor_style(QColor color, int pixel_size);
[[nodiscard]] QDialog* create_photoslop_color_panel(QWidget* parent, QColor initial, const QString& title,
                                                    std::function<void(QColor)> color_changed);

}  // namespace photoslop::ui
