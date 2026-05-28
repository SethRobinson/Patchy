#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

class QDialog;
class QWidget;

namespace patchy::ui {

class PatchyColorPickerPrivate;

class PatchyColorPicker final : public QWidget {
  Q_OBJECT

public:
  explicit PatchyColorPicker(QColor initial, QWidget* parent = nullptr);
  ~PatchyColorPicker() override;

  [[nodiscard]] QColor currentColor() const;

public slots:
  void setCurrentColor(QColor color);

signals:
  void currentColorChanged(QColor color);

private:
  std::unique_ptr<PatchyColorPickerPrivate> impl_;
};

[[nodiscard]] QString color_button_style(QColor color);
[[nodiscard]] QString swatch_button_style(QColor color, bool large = false);
[[nodiscard]] QString inline_text_editor_style(QColor color, int pixel_size);
[[nodiscard]] QDialog* create_patchy_color_panel(QWidget* parent, QColor initial, const QString& title,
                                                    std::function<void(QColor)> color_changed);
[[nodiscard]] std::optional<QColor> request_patchy_color(QWidget* parent, QColor initial, const QString& title);

}  // namespace patchy::ui
