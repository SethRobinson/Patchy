#include "ui/palette_panel.hpp"

#include "core/palette_presets.hpp"
#include "formats/palette_io.hpp"
#include "ui/app_settings.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace patchy::ui {

namespace {

inline constexpr int kSwatchSize = 18;
inline constexpr int kSwatchGap = 2;
inline constexpr int kSwatchesPerRow = 12;

}  // namespace

// The swatch grid custom-paints up to 256 cells; item widgets would be needless
// churn at this scale and the selection/highlight rings are one drawRect each.
class PaletteSwatchGrid final : public QWidget {
public:
  explicit PaletteSwatchGrid(PalettePanel* panel) : QWidget(panel), panel_(panel) {
    setObjectName(QStringLiteral("paletteSwatchGrid"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Clicking a swatch takes keyboard focus so Edit > Copy/Paste route to the
    // palette while the panel is focused.
    setFocusPolicy(Qt::ClickFocus);
  }

  void set_selection_changed_callback(std::function<void()> callback) {
    selection_changed_ = std::move(callback);
  }

  [[nodiscard]] const std::vector<RgbColor>& colors() const noexcept { return colors_; }

  void set_colors(std::vector<RgbColor> colors) {
    colors_ = std::move(colors);
    const auto previous_selected = selected_;
    if (selected_ >= static_cast<int>(colors_.size())) {
      selected_ = colors_.empty() ? -1 : static_cast<int>(colors_.size()) - 1;
    }
    if (selected_ < 0 && !colors_.empty()) {
      selected_ = 0;
    }
    updateGeometry();
    update();
    if (selected_ != previous_selected && selection_changed_) {
      selection_changed_();
    }
  }

  void set_highlight(std::optional<RgbColor> color) {
    highlight_ = color;
    update();
  }

  [[nodiscard]] int selected_index() const noexcept { return selected_; }
  [[nodiscard]] int color_count() const noexcept { return static_cast<int>(colors_.size()); }

  [[nodiscard]] int index_at(QPoint position) const noexcept {
    const auto cell = kSwatchSize + kSwatchGap;
    const auto column = position.x() / cell;
    const auto row = position.y() / cell;
    if (column < 0 || column >= kSwatchesPerRow || row < 0) {
      return -1;
    }
    const auto index = row * kSwatchesPerRow + column;
    if (index >= static_cast<int>(colors_.size())) {
      return -1;
    }
    const auto local = QPoint(position.x() - column * cell, position.y() - row * cell);
    if (local.x() >= kSwatchSize || local.y() >= kSwatchSize) {
      return -1;
    }
    return index;
  }

  [[nodiscard]] QSize sizeHint() const override {
    const auto rows = colors_.empty() ? 0 : (static_cast<int>(colors_.size()) + kSwatchesPerRow - 1) / kSwatchesPerRow;
    const auto cell = kSwatchSize + kSwatchGap;
    return {kSwatchesPerRow * cell - kSwatchGap, rows == 0 ? 0 : rows * cell - kSwatchGap};
  }

  [[nodiscard]] QSize minimumSizeHint() const override { return sizeHint(); }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    const auto cell = kSwatchSize + kSwatchGap;
    for (int index = 0; index < static_cast<int>(colors_.size()); ++index) {
      const auto column = index % kSwatchesPerRow;
      const auto row = index / kSwatchesPerRow;
      const QRect rect(column * cell, row * cell, kSwatchSize, kSwatchSize);
      const auto& color = colors_[static_cast<std::size_t>(index)];
      painter.fillRect(rect, QColor(color.red, color.green, color.blue));
      painter.setPen(QColor(20, 22, 26));
      painter.drawRect(rect.adjusted(0, 0, -1, -1));
      const auto highlighted = highlight_.has_value() && highlight_->red == color.red &&
                               highlight_->green == color.green && highlight_->blue == color.blue;
      if (index == selected_ || highlighted) {
        painter.setPen(QPen(index == selected_ ? QColor(0x2f, 0x75, 0xbd) : QColor(245, 248, 252), 2));
        painter.drawRect(rect.adjusted(1, 1, -2, -2));
      }
      if (dragging_ && index == drag_target_ && index != selected_) {
        painter.setPen(QPen(QColor(245, 248, 252), 2, Qt::DashLine));
        painter.drawRect(rect.adjusted(1, 1, -2, -2));
      }
    }
  }

  void mousePressEvent(QMouseEvent* event) override {
    const auto index = index_at(event->pos());
    if (index < 0) {
      return;
    }
    const auto changed = selected_ != index;
    selected_ = index;
    if (event->button() == Qt::LeftButton) {
      press_position_ = event->pos();
      dragging_ = false;
      drag_target_ = -1;
    }
    update();
    if (changed && selection_changed_) {
      selection_changed_();
    }
    if (event->button() == Qt::LeftButton) {
      emit panel_->entry_clicked(index);
    }
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (!(event->buttons() & Qt::LeftButton) || selected_ < 0) {
      return;
    }
    if (!dragging_ &&
        (event->pos() - press_position_).manhattanLength() >= QApplication::startDragDistance()) {
      dragging_ = true;
    }
    if (dragging_) {
      const auto target = index_at(event->pos());
      if (target != drag_target_) {
        drag_target_ = target;
        update();
      }
    }
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton || !dragging_) {
      return;
    }
    const auto from = selected_;
    const auto to = index_at(event->pos());
    dragging_ = false;
    drag_target_ = -1;
    if (to >= 0 && to != from) {
      // The dragged color lands on the target cell; keep it selected there.
      selected_ = to;
      update();
      if (selection_changed_) {
        selection_changed_();
      }
      emit panel_->entry_swap_requested(from, to);
      return;
    }
    update();
  }

  void mouseDoubleClickEvent(QMouseEvent* event) override {
    const auto index = index_at(event->pos());
    if (index >= 0 && event->button() == Qt::LeftButton) {
      const auto changed = selected_ != index;
      selected_ = index;
      update();
      if (changed && selection_changed_) {
        selection_changed_();
      }
      emit panel_->entry_edit_requested(index);
    }
  }

private:
  PalettePanel* panel_{nullptr};
  std::vector<RgbColor> colors_;
  std::optional<RgbColor> highlight_;
  std::function<void()> selection_changed_;
  QPoint press_position_;
  int selected_{-1};
  int drag_target_{-1};
  bool dragging_{false};
};

PalettePanel::PalettePanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("palettePanel"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);

  auto* top_row = new QHBoxLayout();
  top_row->setSpacing(4);
  preset_combo_ = new QComboBox(this);
  preset_combo_->setObjectName(QStringLiteral("palettePresetCombo"));
  preset_combo_->setToolTip(tr("Load a built-in palette"));
  preset_combo_->addItem(tr("Presets..."), QString());
  for (const auto& preset : builtin_palette_presets()) {
    preset_combo_->addItem(tr(preset.english_name), QString::fromLatin1(preset.id));
  }
  connect(preset_combo_, &QComboBox::activated, this, [this](int index) {
    const auto id = preset_combo_->itemData(index).toString();
    preset_combo_->setCurrentIndex(0);
    if (!id.isEmpty()) {
      emit preset_requested(id);
    }
  });
  top_row->addWidget(preset_combo_, 1);

  const auto add_tool_button = [this](const char* object_name, QString text, QString tooltip) {
    auto* button = new QToolButton(this);
    button->setObjectName(QString::fromLatin1(object_name));
    button->setText(std::move(text));
    button->setToolTip(std::move(tooltip));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
  };
  auto* load_button = add_tool_button("paletteLoadButton", tr("Load"), tr("Load a palette file (.pal, .gpl, .hex, .act, .aco, .ase, indexed .bmp)"));
  connect(load_button, &QToolButton::clicked, this, [this] { emit load_from_file_requested(); });
  top_row->addWidget(load_button);
  auto* save_button = add_tool_button("paletteSaveButton", tr("Save"), tr("Save the palette to a file"));
  connect(save_button, &QToolButton::clicked, this, [this] { emit save_to_file_requested(); });
  top_row->addWidget(save_button);
  layout->addLayout(top_row);

  auto* action_row = new QHBoxLayout();
  action_row->setSpacing(4);
  auto* extract_button = add_tool_button("paletteExtractButton", tr("Extract"),
                                         tr("Build the palette from the image's colors"));
  connect(extract_button, &QToolButton::clicked, this, [this] { emit extract_from_image_requested(); });
  action_row->addWidget(extract_button);
  auto* add_button = add_tool_button("paletteAddButton", QStringLiteral("+"), tr("Add the foreground color"));
  connect(add_button, &QToolButton::clicked, this, [this] { emit add_from_foreground_requested(); });
  action_row->addWidget(add_button);
  remove_button_ = add_tool_button("paletteRemoveButton", QStringLiteral("-"), tr("Remove the selected color"));
  connect(remove_button_, &QToolButton::clicked, this, [this] {
    if (grid_->selected_index() >= 0) {
      emit remove_entry_requested(grid_->selected_index());
    }
  });
  action_row->addWidget(remove_button_);
  action_row->addStretch(1);
  count_label_ = new QLabel(this);
  count_label_->setObjectName(QStringLiteral("paletteCountLabel"));
  action_row->addWidget(count_label_);
  copy_button_ = add_tool_button("paletteCopyHexButton", tr("Copy"),
                                 tr("Copy the selected color's hex code to the clipboard"));
  connect(copy_button_, &QToolButton::clicked, this, [this] {
    if (grid_->selected_index() >= 0) {
      emit copy_color_requested(grid_->selected_index());
    }
  });
  action_row->addWidget(copy_button_);
  layout->addLayout(action_row);

  grid_ = new PaletteSwatchGrid(this);
  grid_->setContextMenuPolicy(Qt::CustomContextMenu);
  grid_->set_selection_changed_callback([this] { update_selection_readout(); });
  connect(grid_, &QWidget::customContextMenuRequested, this,
          [this](const QPoint& position) { show_grid_context_menu(position); });
  layout->addWidget(grid_);

  empty_hint_ = new QLabel(tr("No palette. Pick a preset, load a palette file, or extract one from the image."), this);
  empty_hint_->setObjectName(QStringLiteral("paletteEmptyHint"));
  empty_hint_->setWordWrap(true);
  layout->addWidget(empty_hint_);

  convert_button_ = add_tool_button("paletteConvertButton", tr("Convert to Indexed (Palette)..."),
                                    tr("Constrain painting to this palette"));
  convert_button_->setAutoRaise(false);
  connect(convert_button_, &QToolButton::clicked, this, [this] { emit convert_requested(); });
  layout->addWidget(convert_button_);
  layout->addStretch(1);

  set_palette({}, false);
}

void PalettePanel::set_palette(const std::vector<RgbColor>& colors, bool mode_active) {
  mode_active_ = mode_active;
  colors_ = colors;
  std::unordered_set<std::uint32_t> unique;
  unique.reserve(colors_.size());
  has_duplicate_colors_ = false;
  for (const auto& color : colors_) {
    has_duplicate_colors_ = has_duplicate_colors_ || !unique.insert(palette_color_key(color)).second;
  }
  grid_->set_colors(colors);
  grid_->setVisible(!colors.empty());
  empty_hint_->setVisible(colors.empty());
  remove_button_->setEnabled(colors.size() > 1);
  // The Convert button belongs to documents that have a palette attached but are
  // still editing unconstrained RGB.
  convert_button_->setVisible(!mode_active && !colors.empty());
  update_selection_readout();
}

void PalettePanel::set_highlight_color(std::optional<RgbColor> color) {
  grid_->set_highlight(color);
}

int PalettePanel::selected_index() const noexcept {
  return grid_->selected_index();
}

int PalettePanel::color_count() const noexcept {
  return grid_->color_count();
}

std::optional<RgbColor> PalettePanel::selected_color() const {
  const auto index = grid_->selected_index();
  if (index < 0 || index >= static_cast<int>(colors_.size())) {
    return std::nullopt;
  }
  return colors_[static_cast<std::size_t>(index)];
}

void PalettePanel::update_selection_readout() {
  if (colors_.empty()) {
    count_label_->setText(QString());
    count_label_->setToolTip(QString());
    copy_button_->setVisible(false);
    return;
  }
  const auto index = grid_->selected_index();
  copy_button_->setVisible(true);
  copy_button_->setEnabled(index >= 0 && index < static_cast<int>(colors_.size()));
  auto text = index >= 0 && index < static_cast<int>(colors_.size())
                  ? tr("Index %1: %2")
                        .arg(index)
                        .arg(QColor(colors_[static_cast<std::size_t>(index)].red,
                                    colors_[static_cast<std::size_t>(index)].green,
                                    colors_[static_cast<std::size_t>(index)].blue)
                                 .name())
                  : tr("%n colors", nullptr, static_cast<int>(colors_.size()));
  if (has_duplicate_colors_) {
    text += tr(" (duplicates)");
  }
  count_label_->setText(text);
  count_label_->setToolTip(
      has_duplicate_colors_
          ? tr("Two or more entries share the same color. Identical colors cannot be told apart in the "
               "artwork: exports and palette remaps always use the first matching index. Nudge one channel "
               "by 1 (for example #000000 and #010101) to control indexes separately.")
          : QString());
}

void PalettePanel::show_grid_context_menu(const QPoint& grid_position) {
  const auto index = grid_->index_at(grid_position);
  QMenu menu(this);
  if (index >= 0) {
    menu.addAction(tr("Edit Color..."), this, [this, index] { emit entry_edit_requested(index); });
    menu.addAction(tr("Copy Hex Code"), this, [this, index] { emit copy_color_requested(index); });
    auto* remove = menu.addAction(tr("Remove Color"), this, [this, index] { emit remove_entry_requested(index); });
    remove->setEnabled(grid_->color_count() > 1);
    menu.addSeparator();
  }
  menu.addAction(tr("Add Foreground Color"), this, [this] { emit add_from_foreground_requested(); });
  menu.exec(grid_->mapToGlobal(grid_position));
}

std::optional<LoadedPaletteFile> read_palette_file_quietly(const QString& path) {
  if (path.isEmpty()) {
    return std::nullopt;
  }
  try {
    // toStdU16String -> fs::path converts UTF-16 to the native encoding on every
    // platform (toStdWString would be UTF-32 on POSIX and take the
    // locale-dependent wchar_t path).
    auto data = patchy::palette_io::read_palette_file(std::filesystem::path(path.toStdU16String()));
    if (data.colors.empty()) {
      return std::nullopt;
    }
    return LoadedPaletteFile{std::move(data.colors), QFileInfo(path).fileName(), path};
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<LoadedPaletteFile> prompt_load_palette_file(QWidget* parent) {
  auto settings = app_settings();
  const auto last_dir = settings.value(QStringLiteral("palettes/lastDirectory")).toString();
  const auto path = QFileDialog::getOpenFileName(
      parent, QObject::tr("Load Palette"), last_dir,
      QObject::tr("Palette Files (*.pal *.gpl *.hex *.act *.aco *.ase *.bmp);;All Files (*)"));
  if (path.isEmpty()) {
    return std::nullopt;
  }
  settings.setValue(QStringLiteral("palettes/lastDirectory"), QFileInfo(path).absolutePath());
  try {
    auto data = patchy::palette_io::read_palette_file(std::filesystem::path(path.toStdU16String()));
    if (data.colors.empty()) {
      throw std::runtime_error("The palette file contains no colors");
    }
    return LoadedPaletteFile{std::move(data.colors), QFileInfo(path).fileName(), path};
  } catch (const std::exception& error) {
    QMessageBox::warning(parent, QObject::tr("Load Palette"),
                         QObject::tr("Could not load the palette file.\n%1").arg(QString::fromUtf8(error.what())));
    return std::nullopt;
  }
}

std::optional<QString> prompt_save_palette_file(QWidget* parent, const std::vector<RgbColor>& colors) {
  if (colors.empty()) {
    return std::nullopt;
  }
  auto settings = app_settings();
  const auto last_dir = settings.value(QStringLiteral("palettes/lastDirectory")).toString();
  QString selected_filter;
  auto path = QFileDialog::getSaveFileName(
      parent, QObject::tr("Save Palette"), last_dir,
      QObject::tr("GIMP Palette (*.gpl);;Hex Colors (*.hex);;JASC Palette (*.pal);;Adobe Color Table (*.act);;"
                  "Adobe Color Swatches (*.aco);;PNG Swatch Strip (*.png)"),
      &selected_filter);
  if (path.isEmpty()) {
    return std::nullopt;
  }
  auto suffix = QFileInfo(path).suffix().toLower();
  if (suffix.isEmpty()) {
    // Derive the extension from the chosen filter, e.g. "... (*.gpl)".
    const auto star = selected_filter.indexOf(QStringLiteral("(*."));
    if (star >= 0) {
      suffix = selected_filter.mid(star + 3, selected_filter.indexOf(')') - star - 3).toLower();
      path += QLatin1Char('.') + suffix;
    }
  }
  settings.setValue(QStringLiteral("palettes/lastDirectory"), QFileInfo(path).absolutePath());
  try {
    if (suffix == QStringLiteral("png")) {
      constexpr int kCell = 16;
      QImage strip(static_cast<int>(colors.size()) * kCell, kCell, QImage::Format_RGB32);
      for (int index = 0; index < static_cast<int>(colors.size()); ++index) {
        const auto& color = colors[static_cast<std::size_t>(index)];
        for (int y = 0; y < kCell; ++y) {
          for (int x = 0; x < kCell; ++x) {
            strip.setPixel(index * kCell + x, y, qRgb(color.red, color.green, color.blue));
          }
        }
      }
      if (!strip.save(path)) {
        throw std::runtime_error("Could not write the PNG file");
      }
    } else {
      const auto format = patchy::palette_io::palette_format_for_extension(suffix.toStdString());
      if (!format.has_value()) {
        throw std::runtime_error("Unsupported palette file extension");
      }
      patchy::palette_io::write_palette_file(std::filesystem::path(path.toStdU16String()), colors, *format,
                                             QFileInfo(path).completeBaseName().toStdString());
    }
    return QFileInfo(path).fileName();
  } catch (const std::exception& error) {
    QMessageBox::warning(parent, QObject::tr("Save Palette"),
                         QObject::tr("Could not save the palette file.\n%1").arg(QString::fromUtf8(error.what())));
    return std::nullopt;
  }
}

}  // namespace patchy::ui
