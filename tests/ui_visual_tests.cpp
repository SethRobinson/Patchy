#include "ui/canvas_widget.hpp"
#include "core/layer_metadata.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "psd/psd_document_io.hpp"
#include "test_harness.hpp"

#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QColorDialog>
#include <QDialog>
#include <QDockWidget>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QWheelEvent>
#include <QWidget>

#include <cstdint>
#include <cmath>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

QFont visual_test_font(int point_size = 9) {
  const QStringList font_files = {
      QStringLiteral("C:/Windows/Fonts/arial.ttf"),
      QStringLiteral("C:/Windows/Fonts/arialbd.ttf"),
      QStringLiteral("C:/Windows/Fonts/ariali.ttf"),
      QStringLiteral("C:/Windows/Fonts/arialbi.ttf"),
      QStringLiteral("C:/Windows/Fonts/segoeui.ttf"),
      QStringLiteral("C:/Windows/Fonts/segoeuib.ttf"),
      QStringLiteral("C:/Windows/Fonts/segoeuii.ttf"),
      QStringLiteral("C:/Windows/Fonts/segoeuiz.ttf"),
      QStringLiteral("C:/Windows/Fonts/calibri.ttf"),
      QStringLiteral("C:/Windows/Fonts/calibrib.ttf"),
      QStringLiteral("C:/Windows/Fonts/calibrii.ttf"),
      QStringLiteral("C:/Windows/Fonts/calibriz.ttf"),
  };
  QString preferred_family;
  for (const auto& path : font_files) {
    if (!QFileInfo::exists(path)) {
      continue;
    }
    const auto font_id = QFontDatabase::addApplicationFont(path);
    const auto families = QFontDatabase::applicationFontFamilies(font_id);
    if (families.contains(QStringLiteral("Arial"))) {
      preferred_family = QStringLiteral("Arial");
    } else if (preferred_family.isEmpty() && !families.isEmpty()) {
      preferred_family = families.front();
    }
  }
  if (!preferred_family.isEmpty()) {
    QFont font(preferred_family);
    font.setPointSize(point_size);
    return font;
  }

  auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
  font.setPointSize(point_size);
  return font;
}

using photoslop::test::TestCase;

class PaintCounterFilter final : public QObject {
public:
  int paint_events{0};

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::Paint) {
      ++paint_events;
    }
    return QObject::eventFilter(watched, event);
  }
};

photoslop::PixelBuffer solid_pixels(std::int32_t width, std::int32_t height, photoslop::PixelFormat format,
                                    QColor color) {
  photoslop::PixelBuffer pixels(width, height, format);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      if (format.channels >= 4) {
        px[3] = static_cast<std::uint8_t>(color.alpha());
      }
    }
  }
  return pixels;
}

void ensure_artifact_dir() {
  std::filesystem::create_directories("test-artifacts");
}

void save_widget_artifact(const std::string& name, QWidget& widget) {
  ensure_artifact_dir();
  const auto path = QString::fromStdString((std::filesystem::path("test-artifacts") / (name + ".png")).string());
  const auto pixmap = widget.grab();
  CHECK(!pixmap.isNull());
  CHECK(pixmap.save(path));
}

void send_mouse(QWidget& widget, QEvent::Type type, QPoint position, Qt::MouseButton button, Qt::MouseButtons buttons,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QMouseEvent event(type, position, widget.mapToGlobal(position), button, buttons, modifiers);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

void drag(QWidget& widget, QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  send_mouse(widget, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers);
  send_mouse(widget, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, modifiers);
  send_mouse(widget, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers);
}

void send_double_click(QWidget& widget, QPoint position, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QMouseEvent event(QEvent::MouseButtonDblClick, position, widget.mapToGlobal(position), Qt::LeftButton,
                    Qt::LeftButton, modifiers);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

void send_key(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent press(QEvent::KeyPress, key, modifiers);
  QApplication::sendEvent(&widget, &press);
  QKeyEvent release(QEvent::KeyRelease, key, modifiers);
  QApplication::sendEvent(&widget, &release);
  QApplication::processEvents();
}

void send_key_press(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent event(QEvent::KeyPress, key, modifiers);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

void send_key_release(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent event(QEvent::KeyRelease, key, modifiers);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

void send_wheel(QWidget& widget, QPoint position, int delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QWheelEvent event(QPointF(position), QPointF(widget.mapToGlobal(position)), QPoint(), QPoint(0, delta),
                    Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

QAction* require_action(QWidget& root, const char* object_name) {
  auto* action = root.findChild<QAction*>(QString::fromLatin1(object_name));
  CHECK(action != nullptr);
  return action;
}

QAction* find_action_by_text(QWidget& root, const QString& text) {
  const auto actions = root.findChildren<QAction*>();
  for (auto* action : actions) {
    if (action->menu() != nullptr) {
      continue;
    }
    if (action->text().remove('&') == text) {
      return action;
    }
  }
  return nullptr;
}

QAction* require_action_by_text(QWidget& root, const QString& text) {
  auto* action = find_action_by_text(root, text);
  CHECK(action != nullptr);
  return action;
}

photoslop::ui::CanvasWidget* require_canvas(photoslop::ui::MainWindow& window) {
  auto* canvas = dynamic_cast<photoslop::ui::CanvasWidget*>(window.centralWidget());
  if (canvas == nullptr) {
    auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
    if (tabs != nullptr) {
      canvas = dynamic_cast<photoslop::ui::CanvasWidget*>(tabs->currentWidget());
    }
  }
  CHECK(canvas != nullptr);
  return canvas;
}

void show_window(photoslop::ui::MainWindow& window) {
  window.resize(1180, 780);
  window.show();
  QApplication::processEvents();
}

bool color_close(QColor actual, QColor expected, int tolerance) {
  return std::abs(actual.red() - expected.red()) <= tolerance &&
         std::abs(actual.green() - expected.green()) <= tolerance &&
         std::abs(actual.blue() - expected.blue()) <= tolerance;
}

QColor canvas_pixel(photoslop::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto widget_point = canvas.widget_position_for_document_point(document_point);
  return canvas.grab().toImage().pixelColor(widget_point);
}

void accept_layer_style_dialog(bool stroke_enabled, bool gradient_enabled, bool shadow_enabled);

QAction* require_legacy_plugin_action(QWidget& root, const QString& text) {
  for (auto* action : root.findChildren<QAction*>(QStringLiteral("legacyPluginAction"))) {
    if (action->text().contains(text, Qt::CaseInsensitive)) {
      return action;
    }
  }
  CHECK(false);
  return nullptr;
}

QListWidgetItem* find_layer_item(QListWidget& list, const QString& text) {
  for (int row = 0; row < list.count(); ++row) {
    if (list.item(row)->text() == text) {
      return list.item(row);
    }
  }
  return nullptr;
}

QListWidgetItem* require_layer_item(QListWidget& list, const QString& text) {
  if (auto* item = find_layer_item(list, text); item != nullptr) {
    return item;
  }
  CHECK(false);
  return nullptr;
}

void ui_main_window_renders_color_swatches() {
  photoslop::ui::MainWindow window;
  show_window(window);

  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  auto* background = window.findChild<QPushButton*>(QStringLiteral("backgroundColorButton"));
  CHECK(foreground != nullptr);
  CHECK(background != nullptr);
  CHECK(foreground->text() == QStringLiteral("FG"));
  CHECK(background->text() == QStringLiteral("BG"));
  CHECK(!foreground->text().contains('#'));
  CHECK(!background->text().contains('#'));
  CHECK(window.findChildren<QPushButton*>(QStringLiteral("swatchButton")).size() >= 16);
  const QStringList expected_menus = {QStringLiteral("File"),   QStringLiteral("Edit"),   QStringLiteral("Image"),
                                      QStringLiteral("Layer"),  QStringLiteral("Type"),   QStringLiteral("Select"),
                                      QStringLiteral("Filter"), QStringLiteral("Plugins"), QStringLiteral("View"),
                                      QStringLiteral("Window"), QStringLiteral("Help")};
  QStringList actual_menus;
  for (auto* action : window.menuBar()->actions()) {
    actual_menus << action->text().remove('&');
  }
  CHECK(actual_menus == expected_menus);
  CHECK(window.menuBar()->height() >= 30);
  CHECK(window.windowFlags().testFlag(Qt::FramelessWindowHint));
  CHECK(window.menuBar()->findChild<QLabel*>(QStringLiteral("photoshopBadge")) != nullptr);
  auto* window_close = window.findChild<QToolButton*>(QStringLiteral("windowCloseButton"));
  CHECK(window_close != nullptr);
  CHECK(window_close->mapTo(&window, QPoint(window_close->width(), 0)).x() >= window.width() - 1);
  CHECK(window.findChild<QAction*>(QStringLiteral("workspaceHomeAction")) == nullptr);
  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(recent_menu != nullptr);
  auto* filter_menu = window.findChild<QMenu*>(QStringLiteral("filterMenu"));
  CHECK(filter_menu != nullptr);
  QStringList filter_action_texts;
  for (auto* action : filter_menu->actions()) {
    if (action->isSeparator()) {
      continue;
    }
    auto text = action->text();
    text.remove('&');
    filter_action_texts << text;
  }
  CHECK(filter_action_texts.contains(QStringLiteral("Soft Glow")));
  CHECK(filter_action_texts.contains(QStringLiteral("Punchy Color")));
  CHECK(filter_action_texts.contains(QStringLiteral("Noir")));
  CHECK(filter_action_texts.contains(QStringLiteral("Cinematic Matte")));
  CHECK(filter_action_texts.contains(QStringLiteral("Vintage Fade")));
  CHECK(filter_action_texts.contains(QStringLiteral("Twirl")));
  CHECK(filter_action_texts.contains(QStringLiteral("Clouds")));
  CHECK(filter_action_texts.contains(QStringLiteral("Pixel Mosaic")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Brightness +24")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Contrast +25%")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Brightness")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Contrast")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Auto Contrast")));
  CHECK(!filter_action_texts.contains(QStringLiteral("Desaturate")));
  for (auto* action : filter_menu->actions()) {
    CHECK(!action->isIconVisibleInMenu());
  }
  auto* adjustments_menu = window.findChild<QMenu*>(QStringLiteral("imageAdjustmentsMenu"));
  CHECK(adjustments_menu != nullptr);
  for (auto* action : adjustments_menu->actions()) {
    CHECK(!action->isIconVisibleInMenu());
  }
  auto* new_adjustments_menu = window.findChild<QMenu*>(QStringLiteral("layerNewAdjustmentMenu"));
  CHECK(new_adjustments_menu != nullptr);
  CHECK(require_action(window, "layerNewLevelsAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewCurvesAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewHueSaturationAdjustmentAction") != nullptr);
  CHECK(require_action(window, "layerNewColorBalanceAdjustmentAction") != nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("layerNewAdjustmentButton")) != nullptr);
  CHECK(window.findChild<QSpinBox*>(QStringLiteral("selectionFeatherSpin")) != nullptr);
  for (auto* button : window.findChildren<QPushButton*>()) {
    CHECK(button->text() != QStringLiteral("Select and Mask..."));
  }
  auto* options_bar = window.findChild<QToolBar*>(QStringLiteral("Options"));
  CHECK(options_bar != nullptr);
  CHECK(options_bar->height() >= 36);
  auto* tool_palette = window.findChild<QToolBar*>(QStringLiteral("toolPalette"));
  CHECK(tool_palette != nullptr);
  CHECK(tool_palette->width() <= 45);
  auto* marquee_button = window.findChild<QToolButton*>(QStringLiteral("marqueeToolButton"));
  CHECK(marquee_button != nullptr);
  CHECK(marquee_button->menu() != nullptr);
  CHECK(marquee_button->menu()->actions().size() == 2);
  CHECK(marquee_button->defaultAction() == require_action_by_text(window, QStringLiteral("Marquee")));
  auto* shape_button = window.findChild<QToolButton*>(QStringLiteral("shapeToolButton"));
  CHECK(shape_button != nullptr);
  CHECK(shape_button->menu() != nullptr);
  CHECK(shape_button->menu()->actions().size() == 3);
  CHECK(shape_button->defaultAction() == require_action_by_text(window, QStringLiteral("Rect")));

  save_widget_artifact("ui_main_window", window);
}

void ui_color_picker_changes_foreground_color() {
  ensure_artifact_dir();
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* foreground = window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"));
  CHECK(foreground != nullptr);

  foreground->click();
  QApplication::processEvents();
  QDialog* dialog = nullptr;
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == QStringLiteral("photoslopColorDialog")) {
      dialog = qobject_cast<QDialog*>(widget);
      break;
    }
  }
  CHECK(dialog != nullptr);
  CHECK(dialog->isVisible());
  CHECK(!dialog->isModal());
  CHECK(dialog->windowModality() == Qt::NonModal);
  CHECK(dialog->windowFlags().testFlag(Qt::FramelessWindowHint));
  CHECK(dialog->findChild<QWidget*>(QStringLiteral("dialogChromeTitleBar")) != nullptr);
  CHECK(dialog->findChild<QToolButton*>(QStringLiteral("dialogChromeCloseButton")) != nullptr);

  auto* picker = dialog->findChild<QColorDialog*>(QStringLiteral("photoslopAdvancedColorPicker"));
  CHECK(picker != nullptr);
  CHECK(picker->testOption(QColorDialog::DontUseNativeDialog));
  CHECK(picker->testOption(QColorDialog::NoButtons));
  picker->setCurrentColor(QColor(80, 120, 200));
  QApplication::processEvents();
  CHECK(canvas->primary_color() == QColor(80, 120, 200));
  dialog->grab().save(QStringLiteral("test-artifacts/ui_color_picker_gradient.png"));
  picker->setCurrentColor(QColor(12, 180, 240));
  QApplication::processEvents();
  dialog->grab().save(QStringLiteral("test-artifacts/ui_color_picker.png"));
  CHECK(canvas->primary_color() == QColor(12, 180, 240));
  CHECK(std::filesystem::exists(std::filesystem::path("test-artifacts") / "ui_color_picker.png"));
  save_widget_artifact("ui_color_picker_result", window);
  dialog->close();
  QApplication::processEvents();
}

void ui_dialog_position_memory_restores_last_position() {
  const auto settings_group = QStringLiteral("dialogPositions/photoslopDialogPositionMemoryTest");
  {
    QSettings settings(QStringLiteral("Photoslop"), QStringLiteral("Photoslop"));
    settings.remove(settings_group);
    settings.sync();
  }

  const auto screen_rect = QApplication::primaryScreen() != nullptr
                               ? QApplication::primaryScreen()->availableGeometry()
                               : QRect(0, 0, 640, 480);
  const auto target_position = screen_rect.topLeft() + QPoint(37, 43);
  QPoint saved_position;

  {
    QDialog dialog;
    dialog.setObjectName(QStringLiteral("photoslopDialogPositionMemoryTest"));
    dialog.resize(140, 90);
    photoslop::ui::remember_dialog_position(dialog);
    dialog.show();
    QApplication::processEvents();
    dialog.move(target_position);
    QApplication::processEvents();
    saved_position = dialog.pos();
    dialog.close();
    QApplication::processEvents();
  }

  {
    QDialog dialog;
    dialog.setObjectName(QStringLiteral("photoslopDialogPositionMemoryTest"));
    dialog.resize(140, 90);
    photoslop::ui::remember_dialog_position(dialog);
    dialog.show();
    QApplication::processEvents();
    CHECK((dialog.pos() - saved_position).manhattanLength() <= 2);
    dialog.close();
    QApplication::processEvents();
  }

  QSettings settings(QStringLiteral("Photoslop"), QStringLiteral("Photoslop"));
  settings.remove(settings_group);
  settings.sync();
}

void ui_dirty_state_marks_tabs_and_undo_restores_saved_revision() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  CHECK(!tabs->tabText(tabs->currentIndex()).endsWith(QStringLiteral("*")));

  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(tabs->tabText(tabs->currentIndex()).endsWith(QStringLiteral("*")));

  require_action_by_text(window, QStringLiteral("Undo"))->trigger();
  QApplication::processEvents();
  CHECK(!tabs->tabText(tabs->currentIndex()).endsWith(QStringLiteral("*")));
}

void ui_compatibility_report_flags_psd_text_placeholders() {
  photoslop::Document document(120, 90, photoslop::PixelFormat::rgba8());
  auto& layer = document.add_pixel_layer("PSD Text", solid_pixels(120, 90, photoslop::PixelFormat::rgba8(),
                                                                  QColor(0, 0, 0, 0)));
  layer.metadata()[photoslop::kLayerMetadataText] = "Title";
  layer.metadata()[photoslop::kLayerMetadataTextFont] = "PSD Text";
  layer.unknown_psd_blocks().push_back(photoslop::UnknownPsdBlock{"TySh", {1, 2, 3}});
  auto warnings = photoslop::ui::compatibility_warnings_for_document(document);
  CHECK(!warnings.isEmpty());
  CHECK(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("placeholder")));
  CHECK(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("unknown PSD layer block")));
}

void ui_alt_left_click_samples_foreground_color() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(12, 180, 240));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  const QPoint sample_document_point(80, 80);
  const auto sampled_before_click = canvas_pixel(*canvas, sample_document_point);
  CHECK(color_close(sampled_before_click, QColor(12, 180, 240), 4));

  canvas->set_primary_color(QColor(230, 20, 20));
  const auto sample_widget_point = canvas->widget_position_for_document_point(sample_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, sample_widget_point, Qt::LeftButton, Qt::LeftButton,
             Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, sample_widget_point, Qt::LeftButton, Qt::NoButton,
             Qt::AltModifier);
  QApplication::processEvents();

  CHECK(color_close(canvas->primary_color(), QColor(12, 180, 240), 4));
  CHECK(color_close(canvas_pixel(*canvas, sample_document_point), sampled_before_click, 0));
}

void ui_photoshop_shortcuts_are_registered() {
  photoslop::ui::MainWindow window;
  show_window(window);

  CHECK(require_action_by_text(window, QStringLiteral("New"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_N));
  CHECK(require_action_by_text(window, QStringLiteral("Open..."))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_O));
  CHECK(require_action_by_text(window, QStringLiteral("Save"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_S));
  CHECK(require_action_by_text(window, QStringLiteral("Save As..."))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  CHECK(require_action_by_text(window, QStringLiteral("Undo"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Z));
  CHECK(require_action_by_text(window, QStringLiteral("Redo"))->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
  CHECK(require_action_by_text(window, QStringLiteral("Cut"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_X));
  CHECK(require_action_by_text(window, QStringLiteral("Copy"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_C));
  CHECK(require_action_by_text(window, QStringLiteral("Paste"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_V));
  CHECK(require_action_by_text(window, QStringLiteral("Free Transform..."))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::Key_T));
  CHECK(require_action_by_text(window, QStringLiteral("Clear Selection"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_D));
  CHECK(require_action_by_text(window, QStringLiteral("Reselect"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
  CHECK(require_action_by_text(window, QStringLiteral("Inverse"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
  CHECK(require_action(window, "selectExpandAction") != nullptr);
  CHECK(require_action(window, "selectContractAction") != nullptr);
  CHECK(require_action(window, "selectBorderAction") != nullptr);
  CHECK(require_action(window, "selectLayerTransparencyAction") != nullptr);
  CHECK(require_action(window, "selectGrowAction") != nullptr);
  CHECK(require_action(window, "selectSimilarAction") != nullptr);
  CHECK(require_action(window, "layerEditAdjustmentAction") != nullptr);
  CHECK(require_action_by_text(window, QStringLiteral("New Layer"))->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
  CHECK(require_action_by_text(window, QStringLiteral("Layer Via Copy"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_J));
  CHECK(require_action_by_text(window, QStringLiteral("Layer Via Cut"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_J));
  CHECK(require_action_by_text(window, QStringLiteral("Duplicate Layer"))->shortcut().isEmpty());
  CHECK(require_action(window, "layerFillForegroundAction")->shortcut() == QKeySequence(Qt::ALT | Qt::Key_Backspace));
  CHECK(require_action(window, "layerFillBackgroundAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Backspace));
  CHECK(require_action_by_text(window, QStringLiteral("Merge Selected to New Layer"))->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_E));
  CHECK(require_action_by_text(window, QStringLiteral("Merge Visible to New Layer"))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
  CHECK(require_action_by_text(window, QStringLiteral("Canvas Size..."))->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
  CHECK(require_action(window, "imageAdjustInvertAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_I));
  CHECK(require_action(window, "imageAdjustLevelsAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_L));
  CHECK(require_action(window, "imageAdjustCurvesAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_M));
  CHECK(require_action(window, "imageAdjustHueSaturationAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_U));
  CHECK(require_action(window, "imageAdjustColorBalanceAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_B));
  CHECK(require_action(window, "imageAdjustDesaturateAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
  CHECK(require_action(window, "imageAdjustAutoContrastAction")->shortcut() ==
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_L));
  CHECK(require_action(window, "viewToggleSelectionEdgesAction")->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_H));
  CHECK(require_action_by_text(window, QStringLiteral("Default Colors"))->shortcut() == QKeySequence(Qt::Key_D));
  CHECK(require_action_by_text(window, QStringLiteral("Swap Colors"))->shortcut() == QKeySequence(Qt::Key_X));
  CHECK(require_action_by_text(window, QStringLiteral("Move"))->shortcut() == QKeySequence(Qt::Key_V));
  CHECK(require_action_by_text(window, QStringLiteral("Marquee"))->shortcut() == QKeySequence(Qt::Key_M));
  CHECK(require_action_by_text(window, QStringLiteral("Elliptical Marquee"))->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_M));
  CHECK(require_action_by_text(window, QStringLiteral("Lasso"))->shortcut() == QKeySequence(Qt::Key_L));
  CHECK(require_action_by_text(window, QStringLiteral("Magic Wand"))->shortcut() == QKeySequence(Qt::Key_W));
  CHECK(require_action_by_text(window, QStringLiteral("Brush"))->shortcut() == QKeySequence(Qt::Key_B));
  CHECK(require_action_by_text(window, QStringLiteral("Clone"))->shortcut() == QKeySequence(Qt::Key_S));
  CHECK(require_action_by_text(window, QStringLiteral("Smudge"))->shortcut() == QKeySequence(Qt::Key_R));
  CHECK(require_action_by_text(window, QStringLiteral("Eraser"))->shortcut() == QKeySequence(Qt::Key_E));
  CHECK(require_action_by_text(window, QStringLiteral("Gradient"))->shortcut() == QKeySequence(Qt::Key_G));
  CHECK(require_action_by_text(window, QStringLiteral("Fill"))->shortcut() == QKeySequence(Qt::SHIFT | Qt::Key_G));
  CHECK(require_action_by_text(window, QStringLiteral("Rect"))->shortcut() == QKeySequence(Qt::Key_U));
  CHECK(require_action_by_text(window, QStringLiteral("Line"))->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
  CHECK(require_action_by_text(window, QStringLiteral("Ellipse"))->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_U));
  CHECK(require_action_by_text(window, QStringLiteral("Pick"))->shortcut() == QKeySequence(Qt::Key_I));
  CHECK(require_action_by_text(window, QStringLiteral("Type"))->shortcut() == QKeySequence(Qt::Key_T));
  CHECK(require_action_by_text(window, QStringLiteral("Hand"))->shortcut() == QKeySequence(Qt::Key_H));
  CHECK(require_action_by_text(window, QStringLiteral("Zoom"))->shortcut() == QKeySequence(Qt::Key_Z));
  CHECK(require_action(window, "brushSmallerAction")->shortcut() == QKeySequence(Qt::Key_BracketLeft));
  CHECK(require_action(window, "brushLargerAction")->shortcut() == QKeySequence(Qt::Key_BracketRight));
  CHECK(require_action(window, "brushMuchSmallerAction")->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft));
  CHECK(require_action(window, "brushMuchLargerAction")->shortcut() ==
        QKeySequence(Qt::SHIFT | Qt::Key_BracketRight));
  const auto tooltip_matches_shortcut = [](QAction* action) {
    const auto shortcut = action->shortcut().toString(QKeySequence::NativeText);
    CHECK(!shortcut.isEmpty());
    CHECK(action->toolTip().contains(action->text().remove('&')));
    CHECK(action->toolTip().contains(shortcut));
  };
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Move")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Brush")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Smudge")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Clone")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Type")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Cut")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Default Colors")));
  tooltip_matches_shortcut(require_action_by_text(window, QStringLiteral("Swap Colors")));
  tooltip_matches_shortcut(require_action(window, "brushLargerAction"));

  auto* canvas = require_canvas(window);
  auto* blend_combo = window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"));
  CHECK(blend_combo != nullptr);
  CHECK(blend_combo->findText(QStringLiteral("Difference")) >= 0);
  CHECK(blend_combo->findText(QStringLiteral("Color Dodge")) >= 0);
  CHECK(blend_combo->findText(QStringLiteral("Pin Light")) >= 0);
  CHECK(blend_combo->findText(QStringLiteral("Luminosity")) >= 0);
  CHECK(window.findChild<QCheckBox*>(QStringLiteral("layerLockTransparencyCheck")) != nullptr);
  CHECK(require_action(window, "selectionNewModeAction")->isCheckable());
  CHECK(require_action(window, "selectionAddModeAction")->isCheckable());
  CHECK(require_action(window, "selectionSubtractModeAction")->isCheckable());
  CHECK(require_action(window, "selectionIntersectModeAction")->isCheckable());
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* brush_size_slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
  auto* brush_opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  auto* brush_opacity_slider = window.findChild<QSlider*>(QStringLiteral("brushOpacitySlider"));
  auto* brush_softness = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  auto* brush_softness_slider = window.findChild<QSlider*>(QStringLiteral("brushSoftnessSlider"));
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(brush_size != nullptr);
  CHECK(brush_size_slider != nullptr);
  CHECK(brush_opacity != nullptr);
  CHECK(brush_opacity_slider != nullptr);
  CHECK(brush_softness != nullptr);
  CHECK(brush_softness_slider != nullptr);
  CHECK(brush_preset != nullptr);
  CHECK(brush_size->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(brush_opacity->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(brush_softness->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(brush_softness->value() == 75);
  CHECK(brush_softness_slider->value() == 75);
  CHECK(canvas->brush_softness() == 75);
  const auto airbrush_index = brush_preset->findData(QStringLiteral("airbrush"));
  CHECK(airbrush_index >= 0);
  brush_preset->setCurrentIndex(airbrush_index);
  CHECK(brush_size->value() == 56);
  CHECK(brush_opacity->value() == 12);
  CHECK(brush_softness->value() == 100);
  CHECK(canvas->brush_build_up());
  const auto soft_round_index = brush_preset->findData(QStringLiteral("soft_round"));
  CHECK(soft_round_index >= 0);
  brush_preset->setCurrentIndex(soft_round_index);
  CHECK(!canvas->brush_build_up());
  brush_size->setValue(20);
  CHECK(brush_size_slider->value() == 20);
  require_action(window, "brushLargerAction")->trigger();
  CHECK(brush_size->value() == 21);
  CHECK(brush_size_slider->value() == 21);
  CHECK(canvas->brush_size() == 21);
  require_action(window, "brushSmallerAction")->trigger();
  CHECK(brush_size->value() == 20);
  require_action(window, "brushMuchLargerAction")->trigger();
  CHECK(brush_size->value() == 30);
  CHECK(canvas->brush_size() == 30);
  require_action(window, "brushMuchSmallerAction")->trigger();
  CHECK(brush_size->value() == 20);
  brush_opacity_slider->setValue(45);
  CHECK(brush_opacity->value() == 45);
  CHECK(canvas->brush_opacity() == 45);
  brush_softness_slider->setValue(65);
  CHECK(brush_softness->value() == 65);
  CHECK(canvas->brush_softness() == 65);
  QSettings settings(QStringLiteral("Photoslop"), QStringLiteral("Photoslop"));
  settings.remove(QStringLiteral("tools"));
  settings.sync();
}

void ui_canvas_wheel_matches_photoshop_navigation() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->setFocus();

  const auto initial_zoom = canvas->zoom();
  const auto initial_origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  send_wheel(*canvas, QPoint(300, 240), 120);
  const auto horizontal_pan_origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  CHECK(canvas->zoom() == initial_zoom);
  CHECK(horizontal_pan_origin.x() != initial_origin.x());
  CHECK(horizontal_pan_origin.y() == initial_origin.y());

  send_wheel(*canvas, QPoint(300, 240), 120, Qt::ControlModifier);
  const auto vertical_pan_origin = canvas->widget_position_for_document_point(QPoint(0, 0));
  CHECK(canvas->zoom() == initial_zoom);
  CHECK(vertical_pan_origin.y() != horizontal_pan_origin.y());

  send_wheel(*canvas, QPoint(300, 240), 120, Qt::AltModifier);
  CHECK(canvas->zoom() > initial_zoom);
  save_widget_artifact("ui_canvas_wheel_navigation", *canvas);
}

void ui_canvas_fractional_zoom_paints_to_document_edge() {
  photoslop::Document document(1024, 768, photoslop::PixelFormat::rgba8());
  photoslop::PixelBuffer pixels(1024, 768, photoslop::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = 210;
      px[1] = 80;
      px[2] = 40;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Opaque", std::move(pixels));

  photoslop::ui::CanvasWidget canvas;
  canvas.resize(915, 706);
  canvas.set_document(&document);
  canvas.fit_to_view();
  canvas.show();
  QApplication::processEvents();

  const auto preview = canvas.grab().toImage();
  const auto top_left = canvas.widget_position_for_document_point(QPoint(0, 0));
  const auto bottom_right = canvas.widget_position_for_document_point(QPoint(document.width(), document.height()));
  const QPoint right_edge_sample(bottom_right.x() - 1, (top_left.y() + bottom_right.y()) / 2);
  CHECK(preview.rect().contains(right_edge_sample));
  CHECK(!color_close(preview.pixelColor(right_edge_sample), QColor(36, 38, 41), 4));
}

void ui_shape_flyout_and_zoom_tool_work() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* marquee_button = window.findChild<QToolButton*>(QStringLiteral("marqueeToolButton"));
  auto* shape_button = window.findChild<QToolButton*>(QStringLiteral("shapeToolButton"));
  auto* zoom_button = window.findChild<QToolButton*>(QStringLiteral("zoomToolButton"));
  CHECK(marquee_button != nullptr);
  CHECK(marquee_button->menu() != nullptr);
  CHECK(shape_button != nullptr);
  CHECK(shape_button->menu() != nullptr);
  CHECK(zoom_button != nullptr);

  require_action_by_text(window, QStringLiteral("Elliptical Marquee"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == photoslop::ui::CanvasTool::EllipticalMarquee);
  CHECK(marquee_button->defaultAction() == require_action_by_text(window, QStringLiteral("Elliptical Marquee")));

  require_action_by_text(window, QStringLiteral("Ellipse"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == photoslop::ui::CanvasTool::Ellipse);
  CHECK(shape_button->defaultAction() == require_action_by_text(window, QStringLiteral("Ellipse")));

  require_action_by_text(window, QStringLiteral("Zoom"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == photoslop::ui::CanvasTool::Zoom);
  canvas->set_zoom(0.25);
  const auto before_zoom = canvas->zoom();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 100)),
       canvas->widget_position_for_document_point(QPoint(420, 320)));
  CHECK(canvas->zoom() > before_zoom);

  canvas->set_zoom(2.0);
  send_double_click(*zoom_button, zoom_button->rect().center());
  CHECK(std::abs(canvas->zoom() - 1.0) < 0.001);
  save_widget_artifact("ui_shape_flyout_zoom_tool", window);
}

void ui_filled_shape_preview_clears_after_commit() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(photoslop::ui::CanvasTool::Ellipse);
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(96);
  canvas->set_fill_shapes(true);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(170, 170)),
       canvas->widget_position_for_document_point(QPoint(370, 310)));
  QApplication::processEvents();

  const auto outside_final_shape = canvas_pixel(*canvas, QPoint(170, 125));
  CHECK(color_close(outside_final_shape, Qt::white, 10));
  save_widget_artifact("ui_filled_shape_preview_cleanup", *canvas);

  const auto immediate = canvas->grab().toImage();
  canvas->document_changed();
  QApplication::processEvents();
  const auto repainted = canvas->grab().toImage();
  CHECK(immediate.size() == repainted.size());
  CHECK(immediate.pixelColor(canvas->widget_position_for_document_point(QPoint(170, 125))) ==
        repainted.pixelColor(canvas->widget_position_for_document_point(QPoint(170, 125))));
}

void ui_options_bar_tracks_active_tool() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* move_auto_select = window.findChild<QCheckBox*>(QStringLiteral("moveAutoSelectCheck"));
  auto* text_font = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  auto* text_size = window.findChild<QSpinBox*>(QStringLiteral("textSizeSpin"));
  auto* text_bold = window.findChild<QPushButton*>(QStringLiteral("textBoldButton"));
  auto* text_italic = window.findChild<QPushButton*>(QStringLiteral("textItalicButton"));
  auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("brushSizeSpin"));
  auto* brush_size_slider = window.findChild<QSlider*>(QStringLiteral("brushSizeSlider"));
  auto* brush_opacity = window.findChild<QSpinBox*>(QStringLiteral("brushOpacitySpin"));
  auto* brush_opacity_slider = window.findChild<QSlider*>(QStringLiteral("brushOpacitySlider"));
  auto* brush_softness = window.findChild<QSpinBox*>(QStringLiteral("brushSoftnessSpin"));
  auto* brush_softness_slider = window.findChild<QSlider*>(QStringLiteral("brushSoftnessSlider"));
  auto* clone_aligned = window.findChild<QCheckBox*>(QStringLiteral("cloneAlignedCheck"));
  auto* wand_tolerance = window.findChild<QSpinBox*>(QStringLiteral("wandToleranceSpin"));
  auto* feather_group = window.findChild<QWidget*>(QStringLiteral("selectionFeatherGroup"));
  auto* anti_alias = window.findChild<QCheckBox*>(QStringLiteral("selectionAntiAliasCheck"));
  CHECK(move_auto_select != nullptr);
  CHECK(text_font != nullptr);
  CHECK(text_size != nullptr);
  CHECK(text_size->buttonSymbols() == QAbstractSpinBox::NoButtons);
  CHECK(text_bold != nullptr);
  CHECK(text_italic != nullptr);
  CHECK(brush_size != nullptr);
  CHECK(brush_size_slider != nullptr);
  CHECK(brush_opacity != nullptr);
  CHECK(brush_opacity_slider != nullptr);
  CHECK(brush_softness != nullptr);
  CHECK(brush_softness_slider != nullptr);
  CHECK(clone_aligned != nullptr);
  CHECK(wand_tolerance != nullptr);
  CHECK(feather_group != nullptr);
  CHECK(anti_alias != nullptr);
  CHECK(anti_alias->isChecked());

  CHECK(brush_size->isVisible());
  CHECK(brush_size_slider->isVisible());
  CHECK(brush_opacity->isVisible());
  CHECK(brush_opacity_slider->isVisible());
  CHECK(brush_softness->isVisible());
  CHECK(brush_softness_slider->isVisible());
  CHECK(!clone_aligned->isVisible());
  CHECK(!move_auto_select->isVisible());
  CHECK(!text_font->isVisible());

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  CHECK(move_auto_select->isVisible());
  CHECK(!brush_size->isVisible());
  CHECK(!brush_size_slider->isVisible());
  CHECK(!brush_opacity->isVisible());
  CHECK(!brush_opacity_slider->isVisible());
  CHECK(!brush_softness->isVisible());
  CHECK(!brush_softness_slider->isVisible());
  CHECK(!clone_aligned->isVisible());
  CHECK(!text_font->isVisible());
  move_auto_select->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->auto_select_layer());
  move_auto_select->setChecked(true);
  QApplication::processEvents();
  CHECK(move_auto_select->isChecked());
  CHECK(canvas->auto_select_layer());
  save_widget_artifact("ui_tool_options_move", window);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  QApplication::processEvents();
  CHECK(text_font->isVisible());
  CHECK(text_size->isVisible());
  CHECK(text_bold->isVisible());
  CHECK(text_italic->isVisible());
  CHECK(!move_auto_select->isVisible());
  CHECK(!brush_size->isVisible());
  CHECK(!brush_opacity->isVisible());
  CHECK(!brush_softness->isVisible());
  CHECK(!clone_aligned->isVisible());
  text_size->setValue(36);
  text_bold->setChecked(true);
  save_widget_artifact("ui_tool_options_text", window);

  require_action_by_text(window, QStringLiteral("Magic Wand"))->trigger();
  QApplication::processEvents();
  CHECK(wand_tolerance->isVisible());
  CHECK(feather_group->isVisible());
  CHECK(anti_alias->isVisible());
  CHECK(!text_font->isVisible());

  require_action_by_text(window, QStringLiteral("Clone"))->trigger();
  QApplication::processEvents();
  CHECK(brush_size->isVisible());
  CHECK(brush_size_slider->isVisible());
  CHECK(brush_opacity->isVisible());
  CHECK(brush_opacity_slider->isVisible());
  CHECK(brush_softness->isVisible());
  CHECK(brush_softness_slider->isVisible());
  CHECK(clone_aligned->isVisible());
  CHECK(clone_aligned->isChecked());
  clone_aligned->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->clone_aligned());
  clone_aligned->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->clone_aligned());
  CHECK(!wand_tolerance->isVisible());

  require_action_by_text(window, QStringLiteral("Smudge"))->trigger();
  QApplication::processEvents();
  CHECK(brush_size->isVisible());
  CHECK(brush_opacity->isVisible());
  CHECK(brush_softness->isVisible());
  CHECK(!clone_aligned->isVisible());
}

void ui_right_docks_collapse_layers_show_metadata_and_info_updates() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* info = window.findChild<QLabel*>(QStringLiteral("canvasInfoLabel"));
  auto* opacity_spin = window.findChild<QSpinBox*>(QStringLiteral("layerOpacitySpin"));
  CHECK(layer_list != nullptr);
  CHECK(info != nullptr);
  CHECK(opacity_spin != nullptr);
  CHECK(opacity_spin->buttonSymbols() == QAbstractSpinBox::NoButtons);
  auto* layers_dock = window.findChild<QDockWidget*>(QStringLiteral("layersDock"));
  auto* history_toggle = window.findChild<QToolButton*>(QStringLiteral("historyDockCollapseButton"));
  auto* swatches_toggle = window.findChild<QToolButton*>(QStringLiteral("swatchesDockCollapseButton"));
  auto* info_toggle = window.findChild<QToolButton*>(QStringLiteral("infoDockCollapseButton"));
  CHECK(layers_dock != nullptr);
  CHECK(layers_dock->minimumWidth() >= 280);
  CHECK(layers_dock->minimumHeight() >= 500);
  CHECK(layer_list->minimumHeight() >= 300);
  CHECK(layer_list->contextMenuPolicy() == Qt::CustomContextMenu);
  const auto layer_action_buttons = window.findChildren<QPushButton*>();
  int visible_layer_action_buttons = 0;
  for (const auto* button : layer_action_buttons) {
    if (button->property("layerActionButton").toBool()) {
      ++visible_layer_action_buttons;
      CHECK(button->minimumWidth() >= 40);
      CHECK(button->minimumHeight() >= 34);
      CHECK(button->iconSize().width() >= 24);
      CHECK(button->iconSize().height() >= 24);
    }
  }
  CHECK(visible_layer_action_buttons == 5);
  CHECK(history_toggle != nullptr);
  CHECK(swatches_toggle != nullptr);
  CHECK(info_toggle != nullptr);
  CHECK(history_toggle->text() == QStringLiteral("v"));
  CHECK(history_toggle->icon().isNull());
  history_toggle->setChecked(false);
  QApplication::processEvents();
  CHECK(layers_dock->width() >= 260);
  history_toggle->setChecked(true);
  QApplication::processEvents();

  auto* row_widget = layer_list->itemWidget(layer_list->item(0));
  CHECK(row_widget != nullptr);
  CHECK(row_widget->findChild<QLabel*>(QStringLiteral("layerRowDetails")) != nullptr);

  const auto point = canvas->widget_position_for_document_point(QPoint(64, 48));
  send_mouse(*canvas, QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton);
  CHECK(info->text().contains(QStringLiteral("X: 64")));
  CHECK(info->text().contains(QStringLiteral("Y: 48")));
  CHECK(info->text().contains(QStringLiteral("RGB:")));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  const auto marquee_start = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto marquee_end = canvas->widget_position_for_document_point(QPoint(140, 90));
  send_mouse(*canvas, QEvent::MouseButtonPress, marquee_start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, marquee_end, Qt::NoButton, Qt::LeftButton);
  CHECK(info->text().contains(QStringLiteral("Selection:")));
  CHECK(info->text().contains(QStringLiteral(" at 40, 40")));
  send_mouse(*canvas, QEvent::MouseButtonRelease, marquee_end, Qt::LeftButton, Qt::NoButton);
  save_widget_artifact("ui_info_panel_layers_docks", window);
}

void ui_layer_context_menu_exposes_blending_options_dialog() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  auto* blending_options = require_action(window, "layerBlendingOptionsAction");
  CHECK(layer_list != nullptr);
  CHECK(blending_options->text().remove('&') == QStringLiteral("Blending Options..."));

  bool saw_context_action = false;
  QTimer::singleShot(0, [&saw_context_action] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("layerContextMenu")) {
        continue;
      }
      for (auto* action : menu->actions()) {
        auto text = action->text();
        text.remove('&');
        if (text == QStringLiteral("Blending Options...")) {
          saw_context_action = true;
          break;
        }
      }
      menu->close();
      return;
    }
  });
  const auto context_point = layer_list->visualItemRect(layer_list->item(0)).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  layer_list->viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(layer_list->viewport(), &context_event);
  QApplication::processEvents();
  CHECK(saw_context_action);

  canvas->set_primary_color(QColor(230, 40, 40));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  const auto before = canvas_pixel(*canvas, QPoint(80, 80));

  bool saw_live_style_preview = false;
  bool saw_non_modal_dialog = false;
  bool saw_shared_color_picker = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopLayerStyleDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* gradient_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleGradientOverlayCategoryCheck"));
      auto* gradient_angle_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleGradientAngleSlider"));
      auto* gradient_stops = dialog->findChild<QTableWidget*>(QStringLiteral("layerStyleGradientStopsTable"));
      auto* pick_gradient_color = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleGradientPickColorButton"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePreviewCheck"));
      CHECK(gradient_check != nullptr);
      CHECK(gradient_angle_slider != nullptr);
      CHECK(gradient_stops != nullptr);
      CHECK(pick_gradient_color != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      saw_non_modal_dialog = !dialog->isModal() && dialog->windowModality() == Qt::NonModal &&
                             dialog->windowFlags().testFlag(Qt::FramelessWindowHint) &&
                             dialog->findChild<QWidget*>(QStringLiteral("dialogChromeTitleBar")) != nullptr &&
                             dialog->findChild<QToolButton*>(QStringLiteral("dialogChromeCloseButton")) != nullptr;
      gradient_check->setChecked(true);
      gradient_angle_slider->setValue(0);
      QApplication::processEvents();
      saw_live_style_preview = !color_close(canvas_pixel(*canvas, QPoint(80, 80)), before, 20);
      gradient_stops->setCurrentCell(0, 0);
      QTimer::singleShot(0, [&saw_shared_color_picker] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->objectName() != QStringLiteral("photoslopColorDialog") || !widget->isVisible()) {
            continue;
          }
          auto* color_dialog = qobject_cast<QDialog*>(widget);
          CHECK(color_dialog != nullptr);
          auto* picker = color_dialog->findChild<QColorDialog*>(QStringLiteral("photoslopAdvancedColorPicker"));
          CHECK(picker != nullptr);
          CHECK(picker->testOption(QColorDialog::DontUseNativeDialog));
          picker->setCurrentColor(QColor(64, 128, 192));
          saw_shared_color_picker = true;
          color_dialog->accept();
          return;
        }
        CHECK(false);
      });
      pick_gradient_color->click();
      CHECK(gradient_stops->item(0, 1)->text() == QStringLiteral("64"));
      CHECK(gradient_stops->item(0, 2)->text() == QStringLiteral("128"));
      CHECK(gradient_stops->item(0, 3)->text() == QStringLiteral("192"));
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  blending_options->trigger();
  QApplication::processEvents();
  CHECK(saw_non_modal_dialog);
  CHECK(saw_live_style_preview);
  CHECK(saw_shared_color_picker);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), before, 8));

  accept_layer_style_dialog(false, true, false);
  blending_options->trigger();
  QApplication::processEvents();
  const auto after = canvas_pixel(*canvas, QPoint(80, 80));
  CHECK(!color_close(before, after, 20));

  auto* styled_item = layer_list->item(0);
  CHECK(styled_item != nullptr);
  styled_item->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  styled_item->setCheckState(Qt::Checked);
  QApplication::processEvents();
  const auto after_visibility_toggle = canvas_pixel(*canvas, QPoint(80, 80));
  CHECK(color_close(after, after_visibility_toggle, 8));

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  const auto before_move_click = canvas->grab().toImage();
  const auto move_point = canvas->widget_position_for_document_point(QPoint(80, 80));
  send_mouse(*canvas, QEvent::MouseButtonPress, move_point, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  const auto during_move_click = canvas->grab().toImage();
  CHECK(color_close(during_move_click.pixelColor(move_point), before_move_click.pixelColor(move_point), 0));
  send_mouse(*canvas, QEvent::MouseButtonRelease, move_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  const auto after_move_click = canvas->grab().toImage();
  CHECK(color_close(after_move_click.pixelColor(move_point), before_move_click.pixelColor(move_point), 0));

  auto* details = layer_list->itemWidget(layer_list->item(0))->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  CHECK(details != nullptr);
  CHECK(details->text().contains(QStringLiteral("fx")));
  save_widget_artifact("ui_layer_style_result", window);
}

void accept_new_document_dialog(int width_value, int height_value) {
  QTimer::singleShot(0, [width_value, height_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopNewDocumentDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* width = dialog->findChild<QSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
      auto* height = dialog->findChild<QSpinBox*>(QStringLiteral("newDocumentHeightSpin"));
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      CHECK(width->buttonSymbols() == QAbstractSpinBox::NoButtons);
      CHECK(height->buttonSymbols() == QAbstractSpinBox::NoButtons);
      width->setValue(width_value);
      height->setValue(height_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_new_document_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_canvas_size_dialog(int width_value, int height_value) {
  QTimer::singleShot(0, [width_value, height_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopCanvasSizeDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* width = dialog->findChild<QSpinBox*>(QStringLiteral("canvasSizeWidthSpin"));
      auto* height = dialog->findChild<QSpinBox*>(QStringLiteral("canvasSizeHeightSpin"));
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      CHECK(width->buttonSymbols() == QAbstractSpinBox::NoButtons);
      CHECK(height->buttonSymbols() == QAbstractSpinBox::NoButtons);
      width->setValue(width_value);
      height->setValue(height_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_canvas_size_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_new_layer_dialog(const QString& layer_name, int opacity_value) {
  QTimer::singleShot(0, [layer_name, opacity_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopNewLayerDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("newLayerNameEdit"));
      auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("newLayerOpacitySpin"));
      CHECK(name != nullptr);
      CHECK(opacity != nullptr);
      name->setText(layer_name);
      opacity->setValue(opacity_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_new_layer_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_layer_style_dialog(bool stroke_enabled, bool gradient_enabled, bool shadow_enabled) {
  QTimer::singleShot(0, [stroke_enabled, gradient_enabled, shadow_enabled] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopLayerStyleDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
      auto* blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBlendModeCombo"));
      auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOpacitySpin"));
      auto* opacity_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOpacitySlider"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePreviewCheck"));
      auto* options_stack = dialog->findChild<QWidget*>(QStringLiteral("layerStyleOptionsStack"));
      auto* stroke_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleStrokeCategoryCheck"));
      auto* gradient_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleGradientOverlayCategoryCheck"));
      auto* outer_glow_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleOuterGlowCategoryCheck"));
      auto* shadow_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleDropShadowCategoryCheck"));
      auto* bevel_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBevelEmbossCategoryCheck"));
      auto* stroke_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeSizeSpin"));
      auto* stroke_size_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleStrokeSizeSlider"));
      auto* gradient_angle = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientAngleSpin"));
      auto* gradient_angle_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleGradientAngleSlider"));
      auto* gradient_stops = dialog->findChild<QTableWidget*>(QStringLiteral("layerStyleGradientStopsTable"));
      auto* add_gradient_stop = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleGradientAddStopButton"));
      auto* outer_glow_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowSizeSpin"));
      auto* outer_glow_size_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOuterGlowSizeSlider"));
      auto* shadow_distance = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowDistanceSpin"));
      auto* shadow_distance_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleDropShadowDistanceSlider"));
      auto* bevel_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelSizeSpin"));
      auto* bevel_size_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleBevelSizeSlider"));
      auto* stroke_red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeRedSpin"));
      auto* stroke_red_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleStrokeRedSlider"));
      auto* outer_glow_blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowBlueSpin"));
      auto* outer_glow_blue_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOuterGlowBlueSlider"));
      CHECK(categories != nullptr);
      CHECK(blend != nullptr);
      CHECK(opacity != nullptr);
      CHECK(opacity_slider != nullptr);
      CHECK(preview != nullptr);
      CHECK(options_stack != nullptr);
      CHECK(stroke_check != nullptr);
      CHECK(gradient_check != nullptr);
      CHECK(outer_glow_check != nullptr);
      CHECK(shadow_check != nullptr);
      CHECK(bevel_check != nullptr);
      CHECK(stroke_size != nullptr);
      CHECK(stroke_size_slider != nullptr);
      CHECK(gradient_angle != nullptr);
      CHECK(gradient_angle_slider != nullptr);
      CHECK(gradient_stops != nullptr);
      CHECK(add_gradient_stop != nullptr);
      CHECK(outer_glow_size != nullptr);
      CHECK(outer_glow_size_slider != nullptr);
      CHECK(shadow_distance != nullptr);
      CHECK(shadow_distance_slider != nullptr);
      CHECK(bevel_size != nullptr);
      CHECK(bevel_size_slider != nullptr);
      CHECK(stroke_red != nullptr);
      CHECK(stroke_red_slider != nullptr);
      CHECK(outer_glow_blue != nullptr);
      CHECK(outer_glow_blue_slider != nullptr);
      CHECK(!dialog->isModal());
      CHECK(dialog->windowModality() == Qt::NonModal);
      CHECK(preview->isChecked());
      auto find_item = [categories](const QString& text) {
        const auto items = categories->findItems(text, Qt::MatchExactly);
        CHECK(items.size() == 1);
        return items.front();
      };
      auto* blending_item = find_item(QStringLiteral("Blending Options"));
      auto* bevel_item = find_item(QStringLiteral("Bevel & Emboss"));
      auto* stroke_item = find_item(QStringLiteral("Stroke"));
      auto* gradient_item = find_item(QStringLiteral("Gradient Overlay"));
      auto* outer_glow_item = find_item(QStringLiteral("Outer Glow"));
      auto* shadow_item = find_item(QStringLiteral("Drop Shadow"));
      CHECK((bevel_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((stroke_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((gradient_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((outer_glow_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((shadow_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((blending_item->flags() & Qt::ItemIsUserCheckable) == 0);
      CHECK(blend->findText(QStringLiteral("Pin Light")) >= 0);
      opacity_slider->setValue(92);
      CHECK(opacity->value() == 92);
      stroke_item->setCheckState(stroke_enabled ? Qt::Checked : Qt::Unchecked);
      gradient_item->setCheckState(gradient_enabled ? Qt::Checked : Qt::Unchecked);
      outer_glow_item->setCheckState(Qt::Checked);
      shadow_item->setCheckState(shadow_enabled ? Qt::Checked : Qt::Unchecked);
      bevel_item->setCheckState(Qt::Checked);
      categories->setCurrentItem(gradient_enabled ? gradient_item : blending_item);
      stroke_size_slider->setValue(6);
      CHECK(stroke_size->value() == 6);
      bevel_size_slider->setValue(7);
      CHECK(bevel_size->value() == 7);
      outer_glow_size_slider->setValue(8);
      CHECK(outer_glow_size->value() == 8);
      gradient_angle_slider->setValue(0);
      CHECK(gradient_angle->value() == 0);
      stroke_red_slider->setValue(32);
      CHECK(stroke_red->value() == 32);
      outer_glow_blue_slider->setValue(210);
      CHECK(outer_glow_blue->value() == 210);
      const auto original_stop_count = gradient_stops->rowCount();
      CHECK(original_stop_count >= 2);
      gradient_stops->setCurrentCell(0, 0);
      add_gradient_stop->click();
      CHECK(gradient_stops->rowCount() == original_stop_count + 1);
      gradient_stops->item(gradient_stops->rowCount() - 1, 0)->setText(QStringLiteral("50"));
      gradient_stops->item(gradient_stops->rowCount() - 1, 1)->setText(QStringLiteral("255"));
      gradient_stops->item(gradient_stops->rowCount() - 1, 2)->setText(QStringLiteral("160"));
      gradient_stops->item(gradient_stops->rowCount() - 1, 3)->setText(QStringLiteral("0"));
      shadow_distance_slider->setValue(10);
      CHECK(shadow_distance->value() == 10);
      widget->grab().save(QStringLiteral("test-artifacts/ui_layer_style_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_transform_dialog(int x_value, int y_value, int width_value, int height_value) {
  QTimer::singleShot(0, [x_value, y_value, width_value, height_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopTransformDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* x = dialog->findChild<QSpinBox*>(QStringLiteral("transformXSpin"));
      auto* y = dialog->findChild<QSpinBox*>(QStringLiteral("transformYSpin"));
      auto* width = dialog->findChild<QSpinBox*>(QStringLiteral("transformWidthSpin"));
      auto* height = dialog->findChild<QSpinBox*>(QStringLiteral("transformHeightSpin"));
      CHECK(x != nullptr);
      CHECK(y != nullptr);
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      x->setValue(x_value);
      y->setValue(y_value);
      width->setValue(width_value);
      height->setValue(height_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_transform_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_hue_saturation_dialog(int hue_value, int saturation_value, int lightness_value) {
  QTimer::singleShot(0, [hue_value, saturation_value, lightness_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopHueSaturationDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
      auto* saturation = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationSaturationSpin"));
      auto* lightness = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationLightnessSpin"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("hueSaturationPreviewCheck"));
      CHECK(hue != nullptr);
      CHECK(saturation != nullptr);
      CHECK(lightness != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      hue->setValue(hue_value);
      saturation->setValue(saturation_value);
      lightness->setValue(lightness_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_hue_saturation_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_levels_dialog(int black_value, int white_value, int gamma_value) {
  QTimer::singleShot(0, [black_value, white_value, gamma_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopLevelsDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* black = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackInputSpin"));
      auto* white = dialog->findChild<QSpinBox*>(QStringLiteral("levelsWhiteInputSpin"));
      auto* gamma = dialog->findChild<QSpinBox*>(QStringLiteral("levelsGammaSpin"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("levelsPreviewCheck"));
      CHECK(black != nullptr);
      CHECK(white != nullptr);
      CHECK(gamma != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      black->setValue(black_value);
      white->setValue(white_value);
      gamma->setValue(gamma_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_levels_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_curves_dialog(int shadow_value, int midtone_value, int highlight_value) {
  QTimer::singleShot(0, [shadow_value, midtone_value, highlight_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopCurvesDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* shadow = dialog->findChild<QSpinBox*>(QStringLiteral("curvesShadowOutputSpin"));
      auto* midtone = dialog->findChild<QSpinBox*>(QStringLiteral("curvesMidtoneOutputSpin"));
      auto* highlight = dialog->findChild<QSpinBox*>(QStringLiteral("curvesHighlightOutputSpin"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("curvesPreviewCheck"));
      CHECK(shadow != nullptr);
      CHECK(midtone != nullptr);
      CHECK(highlight != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      shadow->setValue(shadow_value);
      midtone->setValue(midtone_value);
      highlight->setValue(highlight_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_curves_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_color_balance_dialog(int cyan_red_value, int magenta_green_value, int yellow_blue_value) {
  QTimer::singleShot(0, [cyan_red_value, magenta_green_value, yellow_blue_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopColorBalanceDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* cyan_red = dialog->findChild<QSpinBox*>(QStringLiteral("colorBalanceCyanRedSpin"));
      auto* magenta_green = dialog->findChild<QSpinBox*>(QStringLiteral("colorBalanceMagentaGreenSpin"));
      auto* yellow_blue = dialog->findChild<QSpinBox*>(QStringLiteral("colorBalanceYellowBlueSpin"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("colorBalancePreviewCheck"));
      CHECK(cyan_red != nullptr);
      CHECK(magenta_green != nullptr);
      CHECK(yellow_blue != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      cyan_red->setValue(cyan_red_value);
      magenta_green->setValue(magenta_green_value);
      yellow_blue->setValue(yellow_blue_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_color_balance_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_filter_dialog(std::vector<std::pair<QString, int>> spin_values = {}) {
  QTimer::singleShot(0, [spin_values = std::move(spin_values)] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopFilterDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("filterPreviewCheck")) != nullptr);
      for (const auto& [object_name, value] : spin_values) {
        auto* spin = dialog->findChild<QSpinBox*>(object_name);
        CHECK(spin != nullptr);
        spin->setValue(value);
      }
      QApplication::processEvents();
      dialog->accept();
      return;
    }
    CHECK(false);
  });
}

void ui_new_document_and_canvas_size_dialogs_work() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 1);

  accept_new_document_dialog(640, 360);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info != nullptr);
  CHECK(info->text().contains(QStringLiteral("640 x 360 px")));
  save_widget_artifact("ui_new_document_result", window);

  tabs->setCurrentIndex(0);
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("1024 x 768 px")));
  tabs->setCurrentIndex(1);
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("640 x 360 px")));
  save_widget_artifact("ui_multiple_documents", window);

  accept_canvas_size_dialog(720, 405);
  require_action(window, "imageCanvasSizeAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("720 x 405 px")));
  save_widget_artifact("ui_canvas_size_result", window);
}

void ui_first_tab_still_draws_after_second_tab_created() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);

  accept_new_document_dialog(360, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 2);

  tabs->setCurrentIndex(0);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(30, 190, 255));
  drag(*canvas, QPoint(80, 80), QPoint(200, 120));
  QApplication::processEvents();

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  CHECK(color_close(QColor(image.pixel(140, 100)), QColor(30, 190, 255), 80));
  save_widget_artifact("ui_first_tab_draw_after_second_tab", *canvas);
}

void ui_tab_switch_layers_follow_the_canvas_after_tab_reorder() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(tabs != nullptr);
  CHECK(layer_list != nullptr);

  accept_new_layer_dialog(QStringLiteral("First Extra"), 100);
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  auto* first_canvas = tabs->currentWidget();
  CHECK(layer_list->count() == 3);
  CHECK(require_layer_item(*layer_list, QStringLiteral("First Extra")) != nullptr);

  accept_new_document_dialog(360, 240);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  auto* second_canvas = tabs->currentWidget();
  CHECK(layer_list->count() == 2);

  auto* tab_bar = tabs->findChild<QTabBar*>();
  CHECK(tab_bar != nullptr);
  tab_bar->moveTab(tabs->indexOf(first_canvas), tabs->count() - 1);
  QApplication::processEvents();

  tabs->setCurrentWidget(first_canvas);
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  CHECK(require_layer_item(*layer_list, QStringLiteral("First Extra")) != nullptr);

  tabs->setCurrentWidget(second_canvas);
  QApplication::processEvents();
  CHECK(layer_list->count() == 2);
  save_widget_artifact("ui_tab_session_layers", window);
}

void ui_new_layer_dialog_and_multiselect_layers_work() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->selectionMode() == QAbstractItemView::ExtendedSelection);

  accept_new_layer_dialog(QStringLiteral("Retouch"), 75);
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Retouch"));
  save_widget_artifact("ui_new_layer_result", window);

  accept_new_layer_dialog(QStringLiteral("Highlights"), 90);
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 4);

  layer_list->clearSelection();
  layer_list->item(0)->setSelected(true);
  layer_list->item(1)->setSelected(true);
  CHECK(layer_list->selectedItems().size() == 2);
  require_action(window, "layerMergeSelectedAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 5);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Merged Selected"));
  save_widget_artifact("ui_multiselect_merge_selected", window);

  layer_list->clearSelection();
  layer_list->item(0)->setSelected(true);
  layer_list->item(1)->setSelected(true);
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 7);

  layer_list->clearSelection();
  layer_list->item(0)->setSelected(true);
  layer_list->item(1)->setSelected(true);
  require_action(window, "layerDeleteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == 5);
  save_widget_artifact("ui_multiselect_duplicate_delete", window);
}

void ui_duplicate_layer_copies_text_and_folder_trees() {
  photoslop::Document document(120, 90, photoslop::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, photoslop::PixelFormat::rgba8(), QColor(Qt::white)));

  photoslop::Layer text_layer(document.allocate_layer_id(), "Text: Title", photoslop::LayerKind::Text);
  text_layer.metadata()[photoslop::kLayerMetadataText] = "Title";
  text_layer.metadata()[photoslop::kLayerMetadataTextSize] = "32";
  document.add_layer(std::move(text_layer));

  photoslop::Layer folder(document.allocate_layer_id(), "Folder", photoslop::LayerKind::Group);
  folder.add_child(photoslop::Layer(document.allocate_layer_id(), "Nested Paint",
                                    solid_pixels(16, 16, photoslop::PixelFormat::rgba8(), QColor(20, 100, 220))));
  photoslop::Layer nested_folder(document.allocate_layer_id(), "Nested Folder", photoslop::LayerKind::Group);
  nested_folder.add_child(photoslop::Layer(document.allocate_layer_id(), "Deep Paint",
                                           solid_pixels(8, 8, photoslop::PixelFormat::rgba8(), QColor(220, 80, 30))));
  folder.add_child(std::move(nested_folder));
  document.add_layer(std::move(folder));

  photoslop::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Duplicate Trees"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Text: Title"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(text_item);
  text_item->setSelected(true);
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  CHECK(require_layer_item(*layer_list, QStringLiteral("Text: Title copy")) != nullptr);

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(folder_item);
  folder_item->setSelected(true);
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();

  auto* folder_copy = require_layer_item(*layer_list, QStringLiteral("Folder copy"));
  const auto copy_row = layer_list->row(folder_copy);
  CHECK(copy_row == 0);
  CHECK(folder_copy->data(Qt::UserRole + 2).toBool());
  CHECK(layer_list->item(copy_row + 1)->text() == QStringLiteral("Nested Folder"));
  CHECK(layer_list->item(copy_row + 1)->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(layer_list->item(copy_row + 2)->text() == QStringLiteral("Deep Paint"));
  CHECK(layer_list->item(copy_row + 2)->data(Qt::UserRole + 1).toInt() == 2);
  CHECK(layer_list->item(copy_row + 3)->text() == QStringLiteral("Nested Paint"));
  CHECK(layer_list->item(copy_row + 3)->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(layer_list->count() == 11);
  save_widget_artifact("ui_duplicate_text_folder_tree", window);
}

void ui_copy_paste_layer_panel_copies_layers_and_folder_trees() {
  photoslop::Document document(120, 90, photoslop::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, photoslop::PixelFormat::rgba8(), QColor(Qt::white)));

  photoslop::Layer text_layer(document.allocate_layer_id(), "Text: Title", photoslop::LayerKind::Text);
  text_layer.metadata()[photoslop::kLayerMetadataText] = "Title";
  text_layer.metadata()[photoslop::kLayerMetadataTextSize] = "32";
  document.add_layer(std::move(text_layer));

  photoslop::Layer folder(document.allocate_layer_id(), "Folder", photoslop::LayerKind::Group);
  folder.add_child(photoslop::Layer(document.allocate_layer_id(), "Nested Paint",
                                    solid_pixels(16, 16, photoslop::PixelFormat::rgba8(), QColor(20, 100, 220))));
  photoslop::Layer nested_folder(document.allocate_layer_id(), "Nested Folder", photoslop::LayerKind::Group);
  nested_folder.add_child(photoslop::Layer(document.allocate_layer_id(), "Deep Paint",
                                           solid_pixels(8, 8, photoslop::PixelFormat::rgba8(), QColor(220, 80, 30))));
  folder.add_child(std::move(nested_folder));
  document.add_layer(std::move(folder));

  photoslop::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Copy Paste Trees"));
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  require_action(window, "editSelectAllAction")->trigger();
  QApplication::processEvents();

  auto* text_item = require_layer_item(*layer_list, QStringLiteral("Text: Title"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(text_item);
  text_item->setSelected(true);
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->text() == QStringLiteral("Text: Title copy"));

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(folder_item);
  folder_item->setSelected(true);
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  auto* folder_copy = require_layer_item(*layer_list, QStringLiteral("Folder copy"));
  const auto copy_row = layer_list->row(folder_copy);
  CHECK(copy_row == 0);
  CHECK(layer_list->item(copy_row + 1)->text() == QStringLiteral("Nested Folder"));
  CHECK(layer_list->item(copy_row + 1)->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(layer_list->item(copy_row + 2)->text() == QStringLiteral("Deep Paint"));
  CHECK(layer_list->item(copy_row + 2)->data(Qt::UserRole + 1).toInt() == 2);
  CHECK(layer_list->item(copy_row + 3)->text() == QStringLiteral("Nested Paint"));
  CHECK(layer_list->item(copy_row + 3)->data(Qt::UserRole + 1).toInt() == 1);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background_item);
  background_item->setSelected(true);
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->text() == QStringLiteral("Background copy"));
  CHECK(layer_list->count() == 12);
  save_widget_artifact("ui_copy_paste_layer_panel_tree", window);
}

void ui_layer_rows_toggle_visibility_and_drag_reorder() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(layer_list->dragDropMode() == QAbstractItemView::InternalMove);
  CHECK(!layer_list->dragDropOverwriteMode());
  CHECK(layer_list->item(0)->checkState() == Qt::Checked);

  canvas->set_primary_color(QColor(240, 30, 30));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  accept_new_layer_dialog(QStringLiteral("Blue"), 100);
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  canvas->set_primary_color(QColor(20, 100, 255));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->text() == QStringLiteral("Blue"));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), QColor(20, 100, 255), 40));

  auto* blue_visibility = layer_list->itemWidget(layer_list->item(0))->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(blue_visibility != nullptr);
  CHECK(blue_visibility->text() == QStringLiteral("✓"));
  blue_visibility->click();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->checkState() == Qt::Unchecked);
  CHECK(blue_visibility->text().isEmpty());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), QColor(240, 30, 30), 40));

  blue_visibility = layer_list->itemWidget(layer_list->item(0))->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(blue_visibility != nullptr);
  blue_visibility->click();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->checkState() == Qt::Checked);
  CHECK(blue_visibility->text() == QStringLiteral("✓"));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), QColor(20, 100, 255), 40));

  auto* background_item = require_layer_item(*layer_list, QStringLiteral("Background"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Blue"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background_item);
  background_item->setSelected(true);
  QApplication::processEvents();
  auto* blue_name = layer_list->itemWidget(blue_item)->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(blue_name != nullptr);
  send_mouse(*blue_name, QEvent::MouseButtonPress, blue_name->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*blue_name, QEvent::MouseButtonRelease, blue_name->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  CHECK(background_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(!canvas->has_selection());

  send_mouse(*blue_name, QEvent::MouseButtonPress, blue_name->rect().center(), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*blue_name, QEvent::MouseButtonRelease, blue_name->rect().center(), Qt::LeftButton, Qt::NoButton);
  CHECK(!background_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 1);

  auto* paint_item = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  auto* background_name = layer_list->itemWidget(background_item)->findChild<QLabel*>(QStringLiteral("layerRowName"));
  CHECK(background_name != nullptr);
  send_mouse(*background_name, QEvent::MouseButtonPress, background_name->rect().center(), Qt::LeftButton,
             Qt::LeftButton);
  send_mouse(*background_name, QEvent::MouseButtonRelease, background_name->rect().center(), Qt::LeftButton,
             Qt::NoButton);
  CHECK(background_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 1);

  send_mouse(*blue_name, QEvent::MouseButtonPress, blue_name->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ShiftModifier);
  send_mouse(*blue_name, QEvent::MouseButtonRelease, blue_name->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ShiftModifier);
  CHECK(background_item->isSelected());
  CHECK(paint_item->isSelected());
  CHECK(blue_item->isSelected());
  CHECK(layer_list->selectedItems().size() == 3);
  CHECK(layer_list->currentItem() == blue_item);

  canvas->clear_selection();
  const auto blue_was_checked = blue_item->checkState();
  blue_visibility = layer_list->itemWidget(blue_item)->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(blue_visibility != nullptr);
  send_mouse(*blue_visibility, QEvent::MouseButtonPress, blue_visibility->rect().center(), Qt::LeftButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*blue_visibility, QEvent::MouseButtonRelease, blue_visibility->rect().center(), Qt::LeftButton,
             Qt::NoButton, Qt::ControlModifier);
  CHECK(!canvas->has_selection());
  CHECK(blue_item->checkState() == blue_was_checked);

  auto* blue_thumbnail = layer_list->itemWidget(blue_item)->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(blue_thumbnail != nullptr);
  send_mouse(*blue_thumbnail, QEvent::MouseButtonPress, blue_thumbnail->rect().center(), Qt::LeftButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*blue_thumbnail, QEvent::MouseButtonRelease, blue_thumbnail->rect().center(), Qt::LeftButton,
             Qt::NoButton, Qt::ControlModifier);
  CHECK(canvas->has_selection());
  CHECK(blue_item->checkState() == blue_was_checked);

  CHECK(layer_list->model()->moveRow(QModelIndex(), 0, QModelIndex(), layer_list->count()));
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(layer_list->count() == 3);
  CHECK(layer_list->item(layer_list->count() - 1)->text() == QStringLiteral("Blue"));
  QStringList names_after_drop;
  for (int row = 0; row < layer_list->count(); ++row) {
    names_after_drop << layer_list->item(row)->text();
  }
  CHECK(names_after_drop.contains(QStringLiteral("Background")));
  CHECK(names_after_drop.contains(QStringLiteral("Paint Layer")));
  CHECK(names_after_drop.contains(QStringLiteral("Blue")));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 80)), QColor(240, 30, 30), 40));
  save_widget_artifact("ui_layer_visibility_drag_reorder", window);
}

void ui_layer_folders_create_with_drag_drop_affordances() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(require_action(window, "layerNewFolderAction") != nullptr);
  CHECK(window.findChild<QPushButton*>(QStringLiteral("layerNewFolderButton")) != nullptr);

  accept_new_layer_dialog(QStringLiteral("Blue"), 100);
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  require_action(window, "layerNewFolderAction")->trigger();
  QApplication::processEvents();

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder 1"));
  auto* blue_item = require_layer_item(*layer_list, QStringLiteral("Blue"));
  CHECK(folder_item->data(Qt::UserRole + 1).toInt() == 0);
  CHECK(blue_item->data(Qt::UserRole + 1).toInt() == 0);
  CHECK((folder_item->flags() & Qt::ItemIsDropEnabled) != 0);
  CHECK((blue_item->flags() & Qt::ItemIsDragEnabled) != 0);
  CHECK(layer_list->dragDropMode() == QAbstractItemView::InternalMove);
  CHECK(layer_list->defaultDropAction() == Qt::MoveAction);
  save_widget_artifact("ui_layer_folder_drag_drop", window);
}

void ui_layer_folders_expand_and_contract_children() {
  photoslop::Document document(32, 32, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(32, 32, photoslop::PixelFormat::rgb8(), QColor(245, 245, 245)));
  photoslop::Layer group(document.allocate_layer_id(), "Folder", photoslop::LayerKind::Group);
  group.add_child(photoslop::Layer(document.allocate_layer_id(), "Nested 1",
                                   solid_pixels(8, 8, photoslop::PixelFormat::rgba8(), QColor(220, 40, 40))));
  group.add_child(photoslop::Layer(document.allocate_layer_id(), "Nested 2",
                                   solid_pixels(8, 8, photoslop::PixelFormat::rgba8(), QColor(40, 80, 220))));
  document.add_layer(std::move(group));

  photoslop::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Folder Disclosure"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto find_layer_item = [layer_list](const QString& text) -> QListWidgetItem* {
    for (int row = 0; row < layer_list->count(); ++row) {
      if (layer_list->item(row)->text() == text) {
        return layer_list->item(row);
      }
    }
    return nullptr;
  };

  CHECK(layer_list->count() == 4);
  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  CHECK(folder_item->data(Qt::UserRole + 3).toBool());
  CHECK(find_layer_item(QStringLiteral("Nested 1")) != nullptr);
  CHECK(find_layer_item(QStringLiteral("Nested 2")) != nullptr);
  auto* folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  auto* disclosure = folder_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
  CHECK(disclosure != nullptr);
  CHECK(disclosure->isChecked());
  CHECK(disclosure->text() == QStringLiteral("v"));

  disclosure->click();
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(layer_list->count() == 2);
  CHECK(find_layer_item(QStringLiteral("Nested 1")) == nullptr);
  CHECK(find_layer_item(QStringLiteral("Nested 2")) == nullptr);
  folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  CHECK(!folder_item->data(Qt::UserRole + 3).toBool());
  folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  disclosure = folder_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
  CHECK(disclosure != nullptr);
  CHECK(!disclosure->isChecked());
  CHECK(disclosure->text() == QStringLiteral(">"));

  disclosure->click();
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(layer_list->count() == 4);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Nested 1"))->data(Qt::UserRole + 1).toInt() == 1);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Nested 2"))->data(Qt::UserRole + 1).toInt() == 1);
  folder_item = require_layer_item(*layer_list, QStringLiteral("Folder"));
  CHECK(folder_item->data(Qt::UserRole + 3).toBool());
  save_widget_artifact("ui_layer_folder_expand_contract", window);
}

void ui_layer_folders_open_with_saved_expansion_state() {
  photoslop::Document document(32, 32, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(32, 32, photoslop::PixelFormat::rgb8(), QColor(245, 245, 245)));

  photoslop::Layer closed_group(document.allocate_layer_id(), "Closed Folder", photoslop::LayerKind::Group);
  closed_group.metadata()[photoslop::kLayerMetadataGroupExpanded] = "false";
  closed_group.add_child(photoslop::Layer(document.allocate_layer_id(), "Closed Child",
                                          solid_pixels(8, 8, photoslop::PixelFormat::rgba8(), QColor(220, 40, 40))));
  document.add_layer(std::move(closed_group));

  photoslop::Layer open_group(document.allocate_layer_id(), "Open Folder", photoslop::LayerKind::Group);
  open_group.metadata()[photoslop::kLayerMetadataGroupExpanded] = "true";
  open_group.add_child(photoslop::Layer(document.allocate_layer_id(), "Open Child",
                                        solid_pixels(8, 8, photoslop::PixelFormat::rgba8(), QColor(40, 80, 220))));
  document.add_layer(std::move(open_group));

  photoslop::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Saved Folder State"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto find_layer_item = [layer_list](const QString& text) -> QListWidgetItem* {
    for (int row = 0; row < layer_list->count(); ++row) {
      if (layer_list->item(row)->text() == text) {
        return layer_list->item(row);
      }
    }
    return nullptr;
  };

  auto* open_item = require_layer_item(*layer_list, QStringLiteral("Open Folder"));
  auto* closed_item = require_layer_item(*layer_list, QStringLiteral("Closed Folder"));
  CHECK(open_item->data(Qt::UserRole + 3).toBool());
  CHECK(!closed_item->data(Qt::UserRole + 3).toBool());
  CHECK(find_layer_item(QStringLiteral("Open Child")) != nullptr);
  CHECK(find_layer_item(QStringLiteral("Closed Child")) == nullptr);

  auto* closed_widget = layer_list->itemWidget(closed_item);
  CHECK(closed_widget != nullptr);
  auto* closed_disclosure = closed_widget->findChild<QToolButton*>(QStringLiteral("layerFolderDisclosureButton"));
  CHECK(closed_disclosure != nullptr);
  CHECK(closed_disclosure->text() == QStringLiteral(">"));
  save_widget_artifact("ui_layer_folder_saved_state", window);
}

void ui_move_auto_select_reveals_layers_in_collapsed_folders() {
  photoslop::Document document(48, 48, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(48, 48, photoslop::PixelFormat::rgb8(), QColor(245, 245, 245)));
  photoslop::Layer group(document.allocate_layer_id(), "Collapsed Folder", photoslop::LayerKind::Group);
  group.metadata()[photoslop::kLayerMetadataGroupExpanded] = "false";
  auto child = photoslop::Layer(document.allocate_layer_id(), "Hidden Child",
                                solid_pixels(12, 12, photoslop::PixelFormat::rgba8(), QColor(40, 80, 220)));
  const auto child_id = child.id();
  child.set_bounds(photoslop::Rect{12, 12, 12, 12});
  group.add_child(std::move(child));
  document.add_layer(std::move(group));
  document.set_active_layer(child_id);

  photoslop::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Reveal Collapsed Auto Select"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(require_layer_item(*layer_list, QStringLiteral("Collapsed Folder"))->data(Qt::UserRole + 3).toBool() == false);
  CHECK(find_layer_item(*layer_list, QStringLiteral("Hidden Child")) == nullptr);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(true);
  const auto click = canvas->widget_position_for_document_point(QPoint(16, 16));
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  QApplication::processEvents();

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Collapsed Folder"));
  auto* child_item = require_layer_item(*layer_list, QStringLiteral("Hidden Child"));
  CHECK(folder_item->data(Qt::UserRole + 3).toBool());
  CHECK(child_item->isSelected());
  CHECK(layer_list->currentItem() == child_item);
  CHECK(layer_list->visualItemRect(child_item).intersects(layer_list->viewport()->rect()));
  save_widget_artifact("ui_auto_select_reveals_collapsed_folder", window);
}

void ui_folder_visibility_preserves_layer_panel_scroll() {
  photoslop::Document document(64, 64, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, photoslop::PixelFormat::rgb8(), QColor(245, 245, 245)));
  photoslop::Layer group(document.allocate_layer_id(), "Scrollable Folder", photoslop::LayerKind::Group);
  for (int index = 0; index < 42; ++index) {
    auto child = photoslop::Layer(
        document.allocate_layer_id(), "Child " + std::to_string(index + 1),
        solid_pixels(8, 8, photoslop::PixelFormat::rgba8(), QColor(40 + index * 3 % 180, 80, 220, 255)));
    child.set_bounds(photoslop::Rect{index % 16, index % 16, 8, 8});
    group.add_child(std::move(child));
  }
  document.add_layer(std::move(group));

  photoslop::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Layer Panel Scroll"));
  QApplication::processEvents();

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  layer_list->setFixedHeight(180);
  QApplication::processEvents();
  auto* scroll = layer_list->verticalScrollBar();
  CHECK(scroll != nullptr);
  CHECK(scroll->maximum() > 0);
  scroll->setValue(scroll->maximum() / 2);
  QApplication::processEvents();
  const auto scroll_before = scroll->value();
  CHECK(scroll_before > 0);

  QListWidgetItem* folder_item = nullptr;
  for (int row = 0; row < layer_list->count(); ++row) {
    if (layer_list->item(row)->text() == QStringLiteral("Scrollable Folder")) {
      folder_item = layer_list->item(row);
      break;
    }
  }
  CHECK(folder_item != nullptr);
  auto* folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  auto* visibility = folder_widget->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(visibility != nullptr);
  visibility->click();
  QApplication::processEvents();
  CHECK(std::abs(scroll->value() - scroll_before) <= 1);

  folder_item = nullptr;
  for (int row = 0; row < layer_list->count(); ++row) {
    if (layer_list->item(row)->text() == QStringLiteral("Scrollable Folder")) {
      folder_item = layer_list->item(row);
      break;
    }
  }
  CHECK(folder_item != nullptr);
  folder_widget = layer_list->itemWidget(folder_item);
  CHECK(folder_widget != nullptr);
  visibility = folder_widget->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(visibility != nullptr);
  visibility->click();
  QApplication::processEvents();
  CHECK(std::abs(scroll->value() - scroll_before) <= 1);
}

void ui_move_preview_preserves_layer_order() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  accept_new_layer_dialog(QStringLiteral("Top Marker"), 100);
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 20, 30));
  canvas->set_brush_size(30);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 110)),
       canvas->widget_position_for_document_point(QPoint(121, 110)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 110)), QColor(230, 20, 30), 55));

  auto* background = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background);
  background->setSelected(true);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(40, 40));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(70, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 110)), QColor(230, 20, 30), 55));
  save_widget_artifact("ui_move_preview_layer_order", window);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(70, 0), Qt::LeftButton, Qt::NoButton);
}

void ui_move_tool_moves_selected_layers_together() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(12);
  canvas->set_primary_color(QColor(230, 30, 30));
  auto* paint_layer = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(paint_layer);
  paint_layer->setSelected(true);
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 70)),
       canvas->widget_position_for_document_point(QPoint(71, 70)));
  QApplication::processEvents();

  accept_new_layer_dialog(QStringLiteral("Blue Move"), 100);
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  canvas->set_primary_color(QColor(20, 90, 240));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(140, 70)),
       canvas->widget_position_for_document_point(QPoint(141, 70)));
  QApplication::processEvents();

  auto* blue_layer = require_layer_item(*layer_list, QStringLiteral("Blue Move"));
  paint_layer = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(blue_layer);
  blue_layer->setSelected(true);
  paint_layer->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 2);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(100, 100));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(30, 20), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(30, 20), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 90)), QColor(230, 30, 30), 70));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(170, 90)), QColor(20, 90, 240), 70));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(230, 30, 30), 70));
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(140, 70)), QColor(20, 90, 240), 70));
  CHECK(layer_list->selectedItems().size() == 2);
  CHECK(blue_layer->isSelected());
  CHECK(paint_layer->isSelected());
  save_widget_artifact("ui_move_selected_layers", window);
}

void ui_move_tool_moves_selected_folder_tree() {
  photoslop::Document document(120, 90, photoslop::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(120, 90, photoslop::PixelFormat::rgba8(), QColor(Qt::white)));

  photoslop::Layer folder(document.allocate_layer_id(), "Move Folder", photoslop::LayerKind::Group);
  auto red = photoslop::Layer(document.allocate_layer_id(), "Red Child",
                              solid_pixels(10, 10, photoslop::PixelFormat::rgba8(), QColor(230, 30, 30)));
  red.set_bounds(photoslop::Rect{20, 20, 10, 10});
  folder.add_child(std::move(red));

  photoslop::Layer nested_folder(document.allocate_layer_id(), "Nested Move Folder", photoslop::LayerKind::Group);
  auto blue = photoslop::Layer(document.allocate_layer_id(), "Blue Grandchild",
                               solid_pixels(10, 10, photoslop::PixelFormat::rgba8(), QColor(20, 90, 240)));
  blue.set_bounds(photoslop::Rect{50, 20, 10, 10});
  nested_folder.add_child(std::move(blue));
  folder.add_child(std::move(nested_folder));
  document.add_layer(std::move(folder));

  photoslop::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Move Folder Tree"));
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(230, 30, 30), 20));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(54, 24)), QColor(20, 90, 240), 20));

  auto* folder_item = require_layer_item(*layer_list, QStringLiteral("Move Folder"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(folder_item);
  folder_item->setSelected(true);
  QApplication::processEvents();
  CHECK(layer_list->selectedItems().size() == 1);

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(80, 60));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(18, 12), Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(18, 12), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(42, 36)), QColor(230, 30, 30), 35));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(72, 36)), QColor(20, 90, 240), 35));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 24)), QColor(Qt::white), 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(54, 24)), QColor(Qt::white), 12));
  CHECK(folder_item->isSelected());
  save_widget_artifact("ui_move_selected_folder_tree", window);
}

void ui_move_preview_clears_transparent_trails_and_keeps_layer_styles() {
  photoslop::Document document(180, 120, photoslop::PixelFormat::rgba8());

  photoslop::PixelBuffer gradient_pixels(16, 16, photoslop::PixelFormat::rgba8());
  gradient_pixels.clear(0);
  for (std::int32_t y = 0; y < gradient_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < gradient_pixels.width(); ++x) {
      auto* px = gradient_pixels.pixel(x, y);
      px[0] = 210;
      px[1] = 20;
      px[2] = 20;
      px[3] = 255;
    }
  }
  photoslop::Layer gradient_layer(document.allocate_layer_id(), "Gradient Move", std::move(gradient_pixels));
  gradient_layer.set_bounds(photoslop::Rect{20, 30, 16, 16});
  photoslop::LayerGradientFill gradient_fill;
  gradient_fill.enabled = true;
  gradient_fill.blend_mode = photoslop::BlendMode::Normal;
  gradient_fill.opacity = 1.0F;
  gradient_fill.gradient.color_stops.push_back(photoslop::GradientColorStop{0.0F, photoslop::RgbColor{30, 210, 80}});
  gradient_fill.gradient.color_stops.push_back(photoslop::GradientColorStop{1.0F, photoslop::RgbColor{30, 210, 80}});
  gradient_layer.layer_style().gradient_fills.push_back(gradient_fill);
  document.add_layer(std::move(gradient_layer));

  photoslop::PixelBuffer color_pixels(16, 16, photoslop::PixelFormat::rgba8());
  color_pixels.clear(0);
  for (std::int32_t y = 0; y < color_pixels.height(); ++y) {
    for (std::int32_t x = 0; x < color_pixels.width(); ++x) {
      auto* px = color_pixels.pixel(x, y);
      px[0] = 210;
      px[1] = 20;
      px[2] = 20;
      px[3] = 255;
    }
  }
  photoslop::Layer color_layer(document.allocate_layer_id(), "Color Move", std::move(color_pixels));
  color_layer.set_bounds(photoslop::Rect{20, 60, 16, 16});
  photoslop::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = photoslop::BlendMode::Normal;
  overlay.color = photoslop::RgbColor{40, 90, 235};
  overlay.opacity = 1.0F;
  color_layer.layer_style().color_overlays.push_back(overlay);
  document.add_layer(std::move(color_layer));

  photoslop::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Move Style Cache"));
  QApplication::processEvents();

  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  auto* gradient_item = require_layer_item(*layer_list, QStringLiteral("Gradient Move"));
  auto* color_item = require_layer_item(*layer_list, QStringLiteral("Color Move"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(color_item);
  color_item->setSelected(true);
  gradient_item->setSelected(true);
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->set_auto_select_layer(false);
  const auto start = canvas->widget_position_for_document_point(QPoint(24, 34));
  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(30, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();
  send_mouse(*canvas, QEvent::MouseMove, start + QPoint(60, 0), Qt::NoButton, Qt::LeftButton);
  QApplication::processEvents();

  CHECK(!color_close(canvas_pixel(*canvas, QPoint(54, 34)), QColor(30, 210, 80), 45));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(84, 34)), QColor(30, 210, 80), 45));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(84, 64)), QColor(40, 90, 235), 45));

  send_mouse(*canvas, QEvent::MouseButtonRelease, start + QPoint(60, 0), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(!color_close(canvas_pixel(*canvas, QPoint(24, 34)), QColor(210, 20, 20), 35));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(84, 34)), QColor(30, 210, 80), 45));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(84, 64)), QColor(40, 90, 235), 45));
  save_widget_artifact("ui_move_preview_style_cache", window);
}

void ui_layer_move_repaints_only_active_document_tab() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);

  accept_new_document_dialog(420, 260);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  accept_new_document_dialog(420, 260);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  CHECK(tabs->count() == 3);

  std::vector<photoslop::ui::CanvasWidget*> canvases;
  std::vector<std::unique_ptr<PaintCounterFilter>> counters;
  for (int index = 0; index < tabs->count(); ++index) {
    auto* canvas = dynamic_cast<photoslop::ui::CanvasWidget*>(tabs->widget(index));
    CHECK(canvas != nullptr);
    canvases.push_back(canvas);
    auto counter = std::make_unique<PaintCounterFilter>();
    canvas->installEventFilter(counter.get());
    counters.push_back(std::move(counter));
  }

  tabs->setCurrentIndex(2);
  QApplication::processEvents();
  auto* active_canvas = canvases[2];
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  active_canvas->set_primary_color(QColor(20, 150, 240));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  for (auto& counter : counters) {
    counter->paint_events = 0;
  }

  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  active_canvas->set_auto_select_layer(false);
  const auto start = active_canvas->widget_position_for_document_point(QPoint(40, 40));
  send_mouse(*active_canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  for (int step = 1; step <= 8; ++step) {
    send_mouse(*active_canvas, QEvent::MouseMove, start + QPoint(step * 12, step * 3), Qt::NoButton, Qt::LeftButton);
  }
  send_mouse(*active_canvas, QEvent::MouseButtonRelease, start + QPoint(96, 24), Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(counters[2]->paint_events > 0);
  CHECK(counters[0]->paint_events == 0);
  CHECK(counters[1]->paint_events == 0);
  save_widget_artifact("ui_move_active_tab_only", window);
}

void ui_arduboy_psd_render_path_if_available() {
  const auto path = std::filesystem::path("D:/projects/C2/MiscPrints/Arduboy.psd");
  if (!std::filesystem::exists(path)) {
    return;
  }

  const auto document = photoslop::psd::DocumentIo::read_file(path);
  const auto image = photoslop::ui::qimage_from_document(document, true);
  CHECK(!image.isNull());

  std::size_t non_white_pixels = 0;
  for (int y = 0; y < image.height(); y += 16) {
    for (int x = 0; x < image.width(); x += 16) {
      const auto color = image.pixelColor(x, y);
      if (color.alpha() != 0 && (color.red() < 245 || color.green() < 245 || color.blue() < 245)) {
        ++non_white_pixels;
      }
    }
  }
  CHECK(non_white_pixels > 1000);

  ensure_artifact_dir();
  const auto preview = image.scaled(QSize(360, 480), Qt::KeepAspectRatio, Qt::SmoothTransformation);
  CHECK(preview.save(QStringLiteral("test-artifacts/ui_arduboy_psd_render.png")));
}

void ui_marquee_selection_modifiers_work() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);

  drag(*canvas, QPoint(60, 60), QPoint(100, 100));
  auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->contains(QPoint(25, 25)));

  drag(*canvas, QPoint(130, 130), QPoint(170, 170), Qt::ShiftModifier);
  selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->contains(QPoint(25, 25)));
  CHECK(selection->contains(QPoint(125, 125)));
  CHECK(canvas->selected_document_region().contains(QPoint(25, 25)));
  CHECK(canvas->selected_document_region().contains(QPoint(125, 125)));
  CHECK(!canvas->selected_document_region().contains(QPoint(75, 75)));
  const auto added_width = selection->width();

  drag(*canvas, QPoint(120, 50), QPoint(180, 180), Qt::AltModifier);
  selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->width() < added_width);
  save_widget_artifact("ui_selection_modifiers", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
}

void ui_selection_toolbar_modes_work() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();

  auto* new_mode = require_action(window, "selectionNewModeAction");
  auto* add_mode = require_action(window, "selectionAddModeAction");
  auto* subtract_mode = require_action(window, "selectionSubtractModeAction");
  auto* intersect_mode = require_action(window, "selectionIntersectModeAction");
  CHECK(new_mode->isChecked());

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(35, 35)));

  add_mode->trigger();
  QApplication::processEvents();
  CHECK(add_mode->isChecked());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(120, 120)),
       canvas->widget_position_for_document_point(QPoint(170, 170)));
  CHECK(canvas->selected_document_region().contains(QPoint(35, 35)));
  CHECK(canvas->selected_document_region().contains(QPoint(145, 145)));
  CHECK(!canvas->selected_document_region().contains(QPoint(95, 95)));

  subtract_mode->trigger();
  QApplication::processEvents();
  CHECK(subtract_mode->isChecked());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(34, 34)),
       canvas->widget_position_for_document_point(QPoint(56, 56)));
  CHECK(!canvas->selected_document_region().contains(QPoint(45, 45)));
  CHECK(canvas->selected_document_region().contains(QPoint(25, 25)));
  CHECK(canvas->selected_document_region().contains(QPoint(145, 145)));

  intersect_mode->trigger();
  QApplication::processEvents();
  CHECK(intersect_mode->isChecked());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(132, 132)),
       canvas->widget_position_for_document_point(QPoint(158, 158)));
  CHECK(!canvas->selected_document_region().contains(QPoint(25, 25)));
  CHECK(canvas->selected_document_region().contains(QPoint(145, 145)));
  CHECK(!canvas->selected_document_region().contains(QPoint(125, 125)));

  new_mode->trigger();
  QApplication::processEvents();
  CHECK(new_mode->isChecked());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(210, 30)),
       canvas->widget_position_for_document_point(QPoint(250, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(225, 45)));
  CHECK(!canvas->selected_document_region().contains(QPoint(145, 145)));
  save_widget_artifact("ui_selection_toolbar_modes", *canvas);
}

void ui_alt_backspace_fills_selection_with_foreground() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_primary_color(QColor(20, 140, 230));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 70)),
       canvas->widget_position_for_document_point(QPoint(180, 150)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());

  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(120, 100)), QColor(20, 140, 230), 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), Qt::white, 8));
  save_widget_artifact("ui_alt_backspace_fill_selection", *canvas);
}

void ui_marquee_fixed_size_and_ratio_options_work() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();

  auto* style = window.findChild<QComboBox*>(QStringLiteral("selectionStyleCombo"));
  auto* width = window.findChild<QSpinBox*>(QStringLiteral("selectionFixedWidthSpin"));
  auto* height = window.findChild<QSpinBox*>(QStringLiteral("selectionFixedHeightSpin"));
  CHECK(style != nullptr);
  CHECK(width != nullptr);
  CHECK(height != nullptr);
  CHECK(style->currentText() == QStringLiteral("Normal"));

  width->setValue(80);
  height->setValue(50);
  style->setCurrentText(QStringLiteral("Fixed Size"));
  QApplication::processEvents();
  CHECK(canvas->marquee_style() == photoslop::ui::CanvasWidget::MarqueeStyle::FixedSize);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(30, 30)),
       canvas->widget_position_for_document_point(QPoint(55, 55)));
  auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->topLeft() == QPoint(30, 30));
  CHECK(selection->width() == 80);
  CHECK(selection->height() == 50);
  CHECK(canvas->selected_document_region().contains(QPoint(105, 75)));
  CHECK(!canvas->selected_document_region().contains(QPoint(112, 84)));

  style->setCurrentText(QStringLiteral("Fixed Ratio"));
  width->setValue(2);
  height->setValue(1);
  QApplication::processEvents();
  CHECK(canvas->marquee_style() == photoslop::ui::CanvasWidget::MarqueeStyle::FixedRatio);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(180, 40)),
       canvas->widget_position_for_document_point(QPoint(300, 140)));
  selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  const auto ratio = static_cast<double>(selection->width()) / static_cast<double>(selection->height());
  CHECK(ratio > 1.85);
  CHECK(ratio < 2.15);
  CHECK(canvas->selected_document_region().contains(QPoint(230, 64)));
  CHECK(!canvas->selected_document_region().contains(QPoint(230, 130)));
  save_widget_artifact("ui_marquee_fixed_size_ratio", *canvas);
}

void ui_elliptical_marquee_selects_oval_region() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  require_action_by_text(window, QStringLiteral("Elliptical Marquee"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == photoslop::ui::CanvasTool::EllipticalMarquee);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 60)),
       canvas->widget_position_for_document_point(QPoint(180, 140)));
  const auto selection = canvas->selected_document_rect();
  CHECK(selection.has_value());
  CHECK(selection->contains(QPoint(130, 100)));
  CHECK(canvas->selected_document_region().contains(QPoint(130, 100)));
  CHECK(!canvas->selected_document_region().contains(QPoint(82, 62)));
  CHECK(!canvas->selected_document_region().contains(QPoint(178, 62)));
  save_widget_artifact("ui_elliptical_marquee", *canvas);
}

void ui_marquee_space_drag_repositions_active_rect() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);

  const auto start = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto first_corner = canvas->widget_position_for_document_point(QPoint(100, 80));
  const auto moved_corner = canvas->widget_position_for_document_point(QPoint(130, 110));
  const auto resized_corner = canvas->widget_position_for_document_point(QPoint(150, 140));

  send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, first_corner, Qt::NoButton, Qt::LeftButton);
  const auto original = canvas->selected_document_rect();
  CHECK(original.has_value());

  send_key_press(*canvas, Qt::Key_Space);
  send_mouse(*canvas, QEvent::MouseMove, moved_corner, Qt::NoButton, Qt::LeftButton);
  const auto moved = canvas->selected_document_rect();
  CHECK(moved.has_value());
  CHECK(moved->size() == original->size());
  CHECK(moved->topLeft() == original->topLeft() + QPoint(30, 30));
  CHECK(canvas->selected_document_region().contains(QPoint(80, 75)));
  CHECK(!canvas->selected_document_region().contains(QPoint(50, 45)));

  send_key_release(*canvas, Qt::Key_Space);
  send_mouse(*canvas, QEvent::MouseMove, resized_corner, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, resized_corner, Qt::LeftButton, Qt::NoButton);
  const auto resized = canvas->selected_document_rect();
  CHECK(resized.has_value());
  CHECK(resized->topLeft() == moved->topLeft());
  CHECK(resized->width() > moved->width());
  CHECK(resized->height() > moved->height());
  save_widget_artifact("ui_marquee_space_drag_reposition", *canvas);
}

void ui_complex_selection_draws_region_outline() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(160, 90)));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 90)),
       canvas->widget_position_for_document_point(QPoint(160, 210)), Qt::ShiftModifier);
  QApplication::processEvents();

  const auto& selection = canvas->selected_document_region();
  CHECK(selection.contains(QPoint(30, 30)));
  CHECK(selection.contains(QPoint(120, 150)));
  CHECK(!selection.contains(QPoint(40, 150)));

  const auto image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  int inner_outline_pixels = 0;
  for (int document_y = 96; document_y <= 204; ++document_y) {
    const auto boundary = canvas->widget_position_for_document_point(QPoint(80, document_y));
    bool found_dark_ant_pixel = false;
    for (int dx = -1; dx <= 1; ++dx) {
      const auto sample = image.pixelColor(boundary + QPoint(dx, 0));
      if (sample.red() < 70 && sample.green() < 70 && sample.blue() < 70) {
        found_dark_ant_pixel = true;
      }
    }
    if (found_dark_ant_pixel) {
      ++inner_outline_pixels;
    }
  }
  CHECK(inner_outline_pixels >= 8);
  save_widget_artifact("ui_complex_selection_outline", *canvas);
}

void ui_ctrl_h_hides_selection_edges_without_blue_tint() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(90, 80)),
       canvas->widget_position_for_document_point(QPoint(190, 155)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  CHECK(canvas->selection_edges_visible());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(130, 115)), Qt::white, 2));

  const auto count_black_edge_pixels = [&canvas] {
    const auto image = canvas->grab().toImage();
    const auto left = canvas->widget_position_for_document_point(QPoint(90, 80));
    const auto right = canvas->widget_position_for_document_point(QPoint(190, 80));
    int pixels = 0;
    for (int y = left.y() - 2; y <= left.y() + 2; ++y) {
      for (int x = left.x(); x <= right.x(); ++x) {
        if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
          continue;
        }
        const auto color = image.pixelColor(x, y);
        if (color.red() < 70 && color.green() < 70 && color.blue() < 70) {
          ++pixels;
        }
      }
    }
    return pixels;
  };

  CHECK(count_black_edge_pixels() > 4);
  save_widget_artifact("ui_selection_edges_visible_no_tint", *canvas);

  send_key(*canvas, Qt::Key_H, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(!canvas->selection_edges_visible());
  CHECK(canvas->has_selection());
  CHECK(count_black_edge_pixels() == 0);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(130, 115)), Qt::white, 2));
  save_widget_artifact("ui_selection_edges_hidden", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  CHECK(canvas->selection_edges_visible());

  send_key(*canvas, Qt::Key_H, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(!canvas->selection_edges_visible());
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(30, 30)),
       canvas->widget_position_for_document_point(QPoint(80, 70)));
  QApplication::processEvents();
  CHECK(canvas->has_selection());
  CHECK(canvas->selection_edges_visible());
}

void ui_select_inverse_and_extended_blend_modes_work() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);
  const QPoint inside(35, 35);
  const QPoint outside(150, 150);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(inside));
  CHECK(!canvas->selected_document_region().contains(outside));
  require_action(window, "selectInverseAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->selected_document_region().contains(inside));
  CHECK(canvas->selected_document_region().contains(outside));
  save_widget_artifact("ui_select_inverse", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  require_action(window, "selectReselectAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(outside));
  CHECK(!canvas->selected_document_region().contains(inside));
  save_widget_artifact("ui_select_reselect", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  auto* blend_combo = window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"));
  CHECK(blend_combo != nullptr);
  canvas->set_primary_color(QColor(255, 0, 0));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  const auto difference_index = blend_combo->findText(QStringLiteral("Difference"));
  CHECK(difference_index >= 0);
  blend_combo->setCurrentIndex(difference_index);
  QApplication::processEvents();

  const auto sample = canvas_pixel(*canvas, QPoint(30, 30));
  CHECK(sample.red() < 40);
  CHECK(sample.green() > 220);
  CHECK(sample.blue() > 220);
  save_widget_artifact("ui_extended_blend_modes", window);
}

void ui_selection_expand_contract_and_layer_transparency_work() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 100)));
  canvas->set_primary_color(QColor(40, 180, 255));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  require_action(window, "selectLayerTransparencyAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(42, 50)));
  CHECK(!canvas->selected_document_region().contains(QPoint(34, 50)));

  canvas->contract_selection(8);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(!canvas->selected_document_region().contains(QPoint(42, 50)));

  canvas->expand_selection(6);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(44, 50)));
  CHECK(!canvas->selected_document_region().contains(QPoint(30, 50)));

  require_action(window, "selectInverseAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(30, 50)));
  save_widget_artifact("ui_selection_expand_contract_transparency", *canvas);

  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(160, 40)),
       canvas->widget_position_for_document_point(QPoint(230, 110)));
  canvas->border_selection(7);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(162, 70)));
  CHECK(canvas->selected_document_region().contains(QPoint(226, 70)));
  CHECK(!canvas->selected_document_region().contains(QPoint(195, 75)));
  CHECK(!canvas->selected_document_region().contains(QPoint(148, 70)));
  save_widget_artifact("ui_selection_border", *canvas);
}

void ui_ctrl_click_layer_loads_layer_transparency() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(55, 45)),
       canvas->widget_position_for_document_point(QPoint(120, 95)));
  canvas->set_primary_color(QColor(20, 130, 230));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());

  QListWidgetItem* paint_layer_item = nullptr;
  for (int row = 0; row < layer_list->count(); ++row) {
    if (layer_list->item(row)->text() == QStringLiteral("Paint Layer")) {
      paint_layer_item = layer_list->item(row);
      break;
    }
  }
  CHECK(paint_layer_item != nullptr);
  auto* paint_layer_row = layer_list->itemWidget(paint_layer_item);
  CHECK(paint_layer_row != nullptr);
  auto* visibility = paint_layer_row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck"));
  CHECK(visibility != nullptr);
  const auto check_state_before = paint_layer_item->checkState();
  send_mouse(*visibility, QEvent::MouseButtonPress, visibility->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*visibility, QEvent::MouseButtonRelease, visibility->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();

  CHECK(!canvas->has_selection());
  CHECK(paint_layer_item->checkState() == check_state_before);

  auto* thumbnail = paint_layer_row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(thumbnail != nullptr);
  send_mouse(*thumbnail, QEvent::MouseButtonPress, thumbnail->rect().center(), Qt::LeftButton, Qt::LeftButton,
             Qt::ControlModifier);
  send_mouse(*thumbnail, QEvent::MouseButtonRelease, thumbnail->rect().center(), Qt::LeftButton, Qt::NoButton,
             Qt::ControlModifier);
  QApplication::processEvents();

  CHECK(canvas->selected_document_region().contains(QPoint(70, 60)));
  CHECK(!canvas->selected_document_region().contains(QPoint(30, 60)));
  CHECK(paint_layer_item->checkState() == check_state_before);
  save_widget_artifact("ui_ctrl_click_layer_transparency", window);
}

void ui_select_grow_and_similar_use_magic_wand_tolerance() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);
  canvas->set_wand_tolerance(8);

  canvas->set_primary_color(QColor(220, 20, 40));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(70, 70)));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(140, 20)),
       canvas->widget_position_for_document_point(QPoint(190, 70)));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  canvas->set_primary_color(QColor(30, 80, 230));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 140)),
       canvas->widget_position_for_document_point(QPoint(70, 190)));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(34, 34)),
       canvas->widget_position_for_document_point(QPoint(40, 40)));
  CHECK(canvas->selected_document_region().contains(QPoint(36, 36)));
  CHECK(!canvas->selected_document_region().contains(QPoint(66, 66)));

  require_action(window, "selectGrowAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(66, 66)));
  CHECK(!canvas->selected_document_region().contains(QPoint(150, 40)));
  CHECK(!canvas->selected_document_region().contains(QPoint(40, 150)));
  save_widget_artifact("ui_select_grow", *canvas);

  require_action(window, "selectSimilarAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(66, 66)));
  CHECK(canvas->selected_document_region().contains(QPoint(150, 40)));
  CHECK(!canvas->selected_document_region().contains(QPoint(40, 150)));
  CHECK(!canvas->selected_document_region().contains(QPoint(240, 40)));
  save_widget_artifact("ui_select_similar", *canvas);
}

void ui_complex_selection_stroke_uses_region_outline() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(24, 24)),
       canvas->widget_position_for_document_point(QPoint(72, 72)));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(132, 132)),
       canvas->widget_position_for_document_point(QPoint(180, 180)), Qt::ShiftModifier);
  CHECK(canvas->selected_document_region().contains(QPoint(40, 40)));
  CHECK(canvas->selected_document_region().contains(QPoint(150, 150)));
  CHECK(!canvas->selected_document_region().contains(QPoint(98, 98)));

  canvas->set_primary_color(QColor(20, 230, 90));
  canvas->set_brush_size(7);
  require_action(window, "editStrokeSelectionAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(24, 45)), QColor(20, 230, 90), 55));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(132, 150)), QColor(20, 230, 90), 55));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(98, 98)), QColor(255, 255, 255), 8));
  save_widget_artifact("ui_complex_stroke_selection", *canvas);
}

void ui_layer_lock_transparency_and_keyboard_nudge_work() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* lock = window.findChild<QCheckBox*>(QStringLiteral("layerLockTransparencyCheck"));
  CHECK(lock != nullptr);

  canvas->set_primary_color(QColor(220, 20, 40));
  lock->setChecked(true);
  QApplication::processEvents();
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(255, 255, 255), 8));

  lock->setChecked(false);
  QApplication::processEvents();
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 20, 40), 8));

  canvas->set_primary_color(QColor(20, 90, 220));
  lock->setChecked(true);
  QApplication::processEvents();
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(20, 90, 220), 8));

  require_action(window, "layerClearAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(20, 90, 220), 8));
  save_widget_artifact("ui_layer_lock_transparency", window);

  canvas->setFocus();
  send_key(*canvas, Qt::Key_Right, Qt::ShiftModifier);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(5, 30)), QColor(255, 255, 255), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(20, 30)), QColor(20, 90, 220), 8));
  save_widget_artifact("ui_keyboard_nudge_layer", window);
}

void ui_lasso_selection_draws_freeform_region() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(photoslop::ui::CanvasTool::Lasso);

  const auto a = canvas->widget_position_for_document_point(QPoint(40, 40));
  const auto b = canvas->widget_position_for_document_point(QPoint(115, 42));
  const auto c = canvas->widget_position_for_document_point(QPoint(96, 105));
  const auto d = canvas->widget_position_for_document_point(QPoint(48, 112));
  send_mouse(*canvas, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseMove, c, Qt::NoButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, d, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  CHECK(canvas->selected_document_region().contains(QPoint(70, 70)));
  CHECK(!canvas->selected_document_region().contains(QPoint(25, 25)));
  save_widget_artifact("ui_lasso_selection", *canvas);
}

void ui_copy_paste_and_transform_pasted_layer_work() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(255, 80, 20));
  drag(*canvas, QPoint(80, 80), QPoint(150, 110));
  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);
  drag(*canvas, QPoint(60, 60), QPoint(180, 140));
  const auto copied_selection_rect = canvas->selected_document_rect();
  CHECK(copied_selection_rect.has_value());

  const auto layers_before = layer_list->count();
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == copied_selection_rect->topLeft());
  CHECK(pasted_rect->size() == copied_selection_rect->size());
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  drag(*canvas, QPoint(120, 100), QPoint(150, 130));
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);

  auto before_transform = canvas->active_layer_document_rect();
  CHECK(before_transform.has_value());
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto bottom_right =
      canvas->widget_position_for_document_point(QPoint(before_transform->x() + before_transform->width(),
                                                       before_transform->y() + before_transform->height()));
  drag(*canvas, bottom_right, bottom_right + QPoint(90, 12), Qt::ShiftModifier);
  QApplication::processEvents();
  auto after_transform = canvas->active_layer_document_rect();
  CHECK(after_transform.has_value());
  CHECK(after_transform->width() > before_transform->width() + 50);
  const auto original_ratio = static_cast<double>(before_transform->width()) / before_transform->height();
  const auto transformed_ratio = static_cast<double>(after_transform->width()) / after_transform->height();
  CHECK(std::abs(original_ratio - transformed_ratio) < 0.2);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto top_center = canvas->widget_position_for_document_point(
      QPoint(after_transform->x() + after_transform->width() / 2, after_transform->y()));
  drag(*canvas, top_center + QPoint(0, -32), top_center + QPoint(80, 20));
  QApplication::processEvents();
  auto after_rotate = canvas->active_layer_document_rect();
  CHECK(after_rotate.has_value());
  CHECK(after_rotate->width() >= after_transform->width());
  save_widget_artifact("ui_copy_paste_transform", window);
}

void ui_free_transform_uses_opaque_pixel_bounds() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(80, 70)),
       canvas->widget_position_for_document_point(QPoint(140, 115)));
  const auto filled_rect = canvas->selected_document_rect();
  CHECK(filled_rect.has_value());
  canvas->set_primary_color(QColor(230, 60, 35));
  require_action(window, "layerFillForegroundAction")->trigger();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  const auto full_layer_rect = canvas->active_layer_document_rect();
  CHECK(full_layer_rect.has_value());
  CHECK(full_layer_rect->width() > 900);

  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());

  const auto handle = canvas->widget_position_for_document_point(filled_rect->bottomRight() + QPoint(1, 1));
  const auto expanded = canvas->widget_position_for_document_point(filled_rect->bottomRight() + QPoint(75, 55));
  drag(*canvas, handle, expanded, Qt::ShiftModifier);
  QApplication::processEvents();

  const auto transformed_rect = canvas->active_layer_document_rect();
  CHECK(transformed_rect.has_value());
  CHECK(transformed_rect->width() > filled_rect->width() + 20);
  CHECK(transformed_rect->height() > filled_rect->height() + 15);
  CHECK(transformed_rect->width() < 180);
  CHECK(transformed_rect->height() < 140);
  save_widget_artifact("ui_transform_opaque_bounds", window);
}

void ui_layer_via_copy_and_cut_match_photoshop_shortcuts() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(245, 30, 30));
  canvas->set_brush_size(24);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 80)),
       canvas->widget_position_for_document_point(QPoint(180, 80)));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 80)), QColor(245, 30, 30), 45));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(52, 58)),
       canvas->widget_position_for_document_point(QPoint(150, 112)));

  const auto initial_layers = layer_list->count();
  require_action(window, "layerViaCopyAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == initial_layers + 1);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer Via Copy"));
  layer_list->item(0)->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 80)), QColor(245, 30, 30), 45));

  layer_list->clearSelection();
  auto* paint_layer = require_layer_item(*layer_list, QStringLiteral("Paint Layer"));
  layer_list->setCurrentItem(paint_layer);
  paint_layer->setSelected(true);
  QApplication::processEvents();
  require_action(window, "layerViaCutAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->item(0)->text() == QStringLiteral("Layer Via Cut"));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(110, 80)), QColor(245, 30, 30), 45));

  layer_list->item(0)->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  const auto revealed = canvas_pixel(*canvas, QPoint(110, 80));
  CHECK(!color_close(revealed, QColor(245, 30, 30), 45));
  CHECK(revealed.red() > 210 && revealed.green() > 210 && revealed.blue() > 210);
  save_widget_artifact("ui_layer_via_copy_cut", window);
}

void ui_layer_mask_from_selection_hides_pixels_and_shows_thumbnail() {
  photoslop::Document document(96, 72, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(96, 72, photoslop::PixelFormat::rgb8(), QColor(255, 255, 255)));
  document.add_pixel_layer("Red Fill", solid_pixels(96, 72, photoslop::PixelFormat::rgb8(), QColor(220, 30, 30)));

  photoslop::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Layer Mask"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);

  const auto start = canvas->widget_position_for_document_point(QPoint(20, 20));
  const auto end = canvas->widget_position_for_document_point(QPoint(70, 54));
  drag(*canvas, start, end);
  require_action(window, "layerAddMaskFromSelectionAction")->trigger();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 30), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(8, 8)), QColor(255, 255, 255), 8));

  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  auto* item = require_layer_item(*layers, QStringLiteral("Red Fill"));
  auto* row = layers->itemWidget(item);
  CHECK(row != nullptr);
  CHECK(row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail")) != nullptr);
  auto* mask_thumbnail = row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail"));
  CHECK(mask_thumbnail != nullptr);
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();
  CHECK(!canvas->has_selection());
  send_mouse(*mask_thumbnail, QEvent::MouseButtonPress, mask_thumbnail->rect().center(), Qt::LeftButton,
             Qt::LeftButton, Qt::ControlModifier);
  send_mouse(*mask_thumbnail, QEvent::MouseButtonRelease, mask_thumbnail->rect().center(), Qt::LeftButton,
             Qt::NoButton, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(canvas->selected_document_region().contains(QPoint(30, 30)));
  CHECK(!canvas->selected_document_region().contains(QPoint(8, 8)));
  CHECK(!canvas->selected_document_region().contains(QPoint(80, 30)));
  auto* link = row->findChild<QToolButton*>(QStringLiteral("layerMaskLinkButton"));
  CHECK(link != nullptr);
  CHECK(link->isChecked());
  link->click();
  QApplication::processEvents();

  canvas->set_tool(photoslop::ui::CanvasTool::Move);
  const auto move_start = canvas->widget_position_for_document_point(QPoint(30, 30));
  const auto move_end = canvas->widget_position_for_document_point(QPoint(50, 30));
  drag(*canvas, move_start, move_end);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(30, 30)), QColor(220, 30, 30), 8));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 30)), QColor(255, 255, 255), 8));

  require_action(window, "layerDeleteMaskAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(80, 30)), QColor(220, 30, 30), 8));
  item = require_layer_item(*layers, QStringLiteral("Red Fill"));
  row = layers->itemWidget(item);
  CHECK(row != nullptr);
  CHECK(row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail")) == nullptr);
  save_widget_artifact("ui_layer_mask_from_selection", window);
}

void ui_layer_thumbnail_updates_after_brush_edit() {
  photoslop::Document document(64, 64, photoslop::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(64, 64, photoslop::PixelFormat::rgb8(), QColor(255, 255, 255)));
  document.add_pixel_layer("Paint", solid_pixels(64, 64, photoslop::PixelFormat::rgba8(), QColor(0, 0, 0, 0)));

  photoslop::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Thumbnail Refresh"));
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);

  auto thumbnail_center = [&]() {
    auto* item = require_layer_item(*layers, QStringLiteral("Paint"));
    auto* row = layers->itemWidget(item);
    CHECK(row != nullptr);
    auto* thumbnail = row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
    CHECK(thumbnail != nullptr);
    const auto image = thumbnail->pixmap(Qt::ReturnByValue).toImage();
    CHECK(!image.isNull());
    return image.pixelColor(image.width() / 2, image.height() / 2);
  };

  const auto before = thumbnail_center();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(240, 30, 20));
  canvas->set_brush_size(28);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(32, 32)),
       canvas->widget_position_for_document_point(QPoint(34, 32)));
  QApplication::processEvents();

  const auto after = thumbnail_center();
  CHECK(after.red() > before.red() + 80);
  CHECK(after.green() < 90);
  CHECK(after.blue() < 90);
  save_widget_artifact("ui_layer_thumbnail_refresh", window);
}

void ui_cut_selection_clears_source_and_keeps_clipboard() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  const QColor paint_color(255, 80, 20);
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(paint_color);
  canvas->set_brush_size(22);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(92, 92)),
       canvas->widget_position_for_document_point(QPoint(130, 110)));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 100)), paint_color, 55));

  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 70)),
       canvas->widget_position_for_document_point(QPoint(150, 130)));
  const auto cut_selection_rect = canvas->selected_document_rect();
  CHECK(cut_selection_rect.has_value());
  const auto layers_before = layer_list->count();
  require_action(window, "editCutAction")->trigger();
  QApplication::processEvents();
  CHECK(!color_close(canvas_pixel(*canvas, QPoint(100, 100)), paint_color, 55));

  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);
  const auto pasted_rect = canvas->active_layer_document_rect();
  CHECK(pasted_rect.has_value());
  CHECK(pasted_rect->topLeft() == cut_selection_rect->topLeft());
  CHECK(color_close(canvas_pixel(*canvas, QPoint(100, 100)), paint_color, 65));
  save_widget_artifact("ui_cut_selection", window);
}

void ui_brush_on_pasted_layer_expands_layer_bounds() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(255, 80, 20));
  canvas->set_brush_size(18);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 60)),
       canvas->widget_position_for_document_point(QPoint(70, 60)));
  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(45, 45)),
       canvas->widget_position_for_document_point(QPoint(85, 85)));
  const auto layers_before = layer_list->count();
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);

  require_action(window, "editDeselectAction")->trigger();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(20, 80, 240));
  canvas->set_brush_size(24);
  canvas->set_brush_opacity(100);
  const QPoint outside_original_paste(260, 190);
  drag(*canvas, canvas->widget_position_for_document_point(outside_original_paste),
       canvas->widget_position_for_document_point(outside_original_paste + QPoint(1, 1)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, outside_original_paste), QColor(20, 80, 240), 45));
  save_widget_artifact("ui_brush_expands_pasted_layer", window);
}

void ui_brush_opacity_caps_per_stroke() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  canvas->set_brush_size(32);
  canvas->set_brush_opacity(20);
  canvas->set_brush_build_up(false);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 120)),
       canvas->widget_position_for_document_point(QPoint(280, 120)));
  QApplication::processEvents();
  const auto first_stroke = canvas_pixel(*canvas, QPoint(175, 120));
  CHECK(first_stroke.red() >= 190);
  CHECK(first_stroke.red() <= 220);
  CHECK(std::abs(first_stroke.red() - first_stroke.green()) <= 3);
  CHECK(std::abs(first_stroke.green() - first_stroke.blue()) <= 3);

  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 120)),
       canvas->widget_position_for_document_point(QPoint(280, 120)));
  QApplication::processEvents();
  const auto second_stroke = canvas_pixel(*canvas, QPoint(175, 120));
  CHECK(second_stroke.red() < first_stroke.red() - 20);
  CHECK(second_stroke.red() >= 145);
  CHECK(second_stroke.red() <= 180);
  save_widget_artifact("ui_brush_opacity_per_stroke", window);
}

void ui_airbrush_preset_builds_up_within_one_stroke() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* brush_preset = window.findChild<QComboBox*>(QStringLiteral("brushPresetCombo"));
  CHECK(brush_preset != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(Qt::black);
  const auto airbrush_index = brush_preset->findData(QStringLiteral("airbrush"));
  CHECK(airbrush_index >= 0);
  brush_preset->setCurrentIndex(airbrush_index);
  QApplication::processEvents();
  CHECK(canvas->brush_build_up());
  CHECK(canvas->brush_opacity() == 12);

  const auto center = canvas->widget_position_for_document_point(QPoint(150, 120));
  send_mouse(*canvas, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  for (int i = 0; i < 6; ++i) {
    send_mouse(*canvas, QEvent::MouseMove, center + QPoint(i % 2, 0), Qt::NoButton, Qt::LeftButton);
  }
  send_mouse(*canvas, QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();

  const auto built_up = canvas_pixel(*canvas, QPoint(150, 120));
  CHECK(built_up.red() < 180);
  CHECK(std::abs(built_up.red() - built_up.green()) <= 4);
  CHECK(std::abs(built_up.green() - built_up.blue()) <= 4);
  save_widget_artifact("ui_airbrush_builds_up", window);
}

void ui_clone_tool_samples_source_and_paints_offset() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 40, 30));
  canvas->set_brush_size(22);
  canvas->set_brush_opacity(100);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(70, 72)),
       canvas->widget_position_for_document_point(QPoint(72, 72)));
  canvas->set_primary_color(QColor(30, 80, 230));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(110, 72)),
       canvas->widget_position_for_document_point(QPoint(112, 72)));
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Clone"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == photoslop::ui::CanvasTool::Clone);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  canvas->set_brush_softness(100);
  auto* clone_aligned = window.findChild<QCheckBox*>(QStringLiteral("cloneAlignedCheck"));
  CHECK(clone_aligned != nullptr);
  CHECK(clone_aligned->isChecked());
  CHECK(canvas->clone_aligned());

  const auto source = canvas->widget_position_for_document_point(QPoint(70, 72));
  send_mouse(*canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);

  const auto target = canvas->widget_position_for_document_point(QPoint(170, 102));
  drag(*canvas, target, target + QPoint(2, 0));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(170, 102)), QColor(230, 40, 30), 45));
  const auto feathered_edge = canvas_pixel(*canvas, QPoint(178, 102));
  CHECK(feathered_edge.red() > feathered_edge.green());
  CHECK(feathered_edge.green() > 80);
  CHECK(feathered_edge.green() < 245);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(210, 102)),
       canvas->widget_position_for_document_point(QPoint(212, 102)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(210, 102)), QColor(30, 80, 230), 45));

  clone_aligned->setChecked(false);
  QApplication::processEvents();
  CHECK(!canvas->clone_aligned());
  send_mouse(*canvas, QEvent::MouseButtonPress, source, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(*canvas, QEvent::MouseButtonRelease, source, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(250, 102)),
       canvas->widget_position_for_document_point(QPoint(252, 102)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(250, 102)), QColor(230, 40, 30), 45));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(290, 102)),
       canvas->widget_position_for_document_point(QPoint(292, 102)));
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(290, 102)), QColor(230, 40, 30), 45));

  auto* history = window.findChild<QListWidget*>(QStringLiteral("historyList"));
  CHECK(history != nullptr);
  CHECK(history->item(0) != nullptr);
  CHECK(history->item(0)->text().contains(QStringLiteral("Clone")));
  save_widget_artifact("ui_clone_tool_stamp", window);
}

void ui_smudge_tool_drags_painted_pixels() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(230, 50, 30));
  canvas->set_brush_size(28);
  canvas->set_brush_opacity(100);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 120)),
       canvas->widget_position_for_document_point(QPoint(102, 120)));
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Smudge"))->trigger();
  QApplication::processEvents();
  CHECK(canvas->tool() == photoslop::ui::CanvasTool::Smudge);
  canvas->set_brush_size(28);
  canvas->set_brush_opacity(70);
  canvas->set_brush_softness(100);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(100, 120)),
       canvas->widget_position_for_document_point(QPoint(170, 120)));
  QApplication::processEvents();

  const auto smeared = canvas_pixel(*canvas, QPoint(165, 120));
  CHECK(smeared.red() > 175);
  CHECK(smeared.green() < 170);
  CHECK(smeared.blue() < 160);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(230, 120)), Qt::white, 10));
  auto* history = window.findChild<QListWidget*>(QStringLiteral("historyList"));
  CHECK(history != nullptr);
  CHECK(history->item(0) != nullptr);
  CHECK(history->item(0)->text().contains(QStringLiteral("Smudge")));
  save_widget_artifact("ui_smudge_tool", window);
}

void ui_copy_ignores_hidden_active_layer() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  QApplication::clipboard()->clear();

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(0, 0, 0));
  drag(*canvas, QPoint(80, 80), QPoint(150, 110));
  require_action(window, "editSelectAllAction")->trigger();
  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();

  require_layer_item(*layer_list, QStringLiteral("Paint Layer"))->setCheckState(Qt::Unchecked);
  QApplication::processEvents();

  const auto layers_before = layer_list->count();
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  CHECK(layer_list->count() == layers_before);
  save_widget_artifact("ui_hidden_layer_copy_ignored", window);
}

void ui_copy_selected_layers_copies_composited_selection() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(22);
  canvas->set_primary_color(QColor(230, 20, 30));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 42)),
       canvas->widget_position_for_document_point(QPoint(42, 42)));

  accept_new_layer_dialog(QStringLiteral("Blue Bits"), 100);
  require_action(window, "layerNewAction")->trigger();
  QApplication::processEvents();
  canvas->set_primary_color(QColor(20, 70, 240));
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(82, 42)),
       canvas->widget_position_for_document_point(QPoint(84, 42)));

  layer_list->clearSelection();
  require_layer_item(*layer_list, QStringLiteral("Blue Bits"))->setSelected(true);
  require_layer_item(*layer_list, QStringLiteral("Paint Layer"))->setSelected(true);
  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 20)),
       canvas->widget_position_for_document_point(QPoint(120, 80)));

  const auto layers_before = layer_list->count();
  require_action(window, "editCopyAction")->trigger();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 42)), QColor(230, 20, 30), 45));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(82, 42)), QColor(20, 70, 240), 45));
  save_widget_artifact("ui_copy_selected_layers", window);
}

void ui_eraser_on_background_reveals_transparency_and_size_cursor() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(38);
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);

  require_action_by_text(window, QStringLiteral("Eraser"))->trigger();
  CHECK(canvas->cursor().shape() == Qt::BitmapCursor);
  auto* background = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background);
  background->setSelected(true);
  QApplication::processEvents();

  const auto erase_point = canvas->widget_position_for_document_point(QPoint(90, 90));
  drag(*canvas, erase_point, erase_point + QPoint(1, 1));
  QApplication::processEvents();
  const auto erased = canvas_pixel(*canvas, QPoint(90, 90));
  CHECK(erased.red() >= 170);
  CHECK(erased.red() <= 245);
  CHECK(std::abs(erased.red() - erased.green()) <= 4);
  CHECK(std::abs(erased.green() - erased.blue()) <= 4);
  save_widget_artifact("ui_background_eraser_transparency", window);
}

void ui_text_tool_creates_visible_text_layer() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  canvas->set_primary_color(QColor(40, 220, 120));
  const QPoint text_document_point(100, 105);
  const auto text_widget_point = canvas->widget_position_for_document_point(text_document_point);
  send_mouse(*canvas, QEvent::MouseButtonPress, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, text_widget_point, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  CHECK(editor->pos() == text_widget_point);
  CHECK(editor->frameShape() == QFrame::NoFrame);
  CHECK(editor->font().pixelSize() > 0);
  CHECK(editor->document()->documentMargin() == 0.0);
  CHECK(editor->property("photoslop.documentTextSize").toInt() == 48);
  CHECK(editor->document()->textWidth() == editor->width());
  CHECK(editor->styleSheet().contains(QStringLiteral("font-size: 48px")));
  auto* text_size = window.findChild<QSpinBox*>(QStringLiteral("textSizeSpin"));
  auto* text_bold = window.findChild<QPushButton*>(QStringLiteral("textBoldButton"));
  auto* text_italic = window.findChild<QPushButton*>(QStringLiteral("textItalicButton"));
  CHECK(text_size != nullptr);
  CHECK(text_bold != nullptr);
  CHECK(text_italic != nullptr);
  text_size->setFocus();
  text_size->setValue(64);
  text_bold->setChecked(true);
  text_italic->setChecked(true);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == editor);
  CHECK(editor->property("photoslop.documentTextSize").toInt() == 64);
  CHECK(editor->font().bold());
  CHECK(editor->font().italic());
  CHECK(editor->styleSheet().contains(QStringLiteral("font-size: 64px")));
  editor->setPlainText(QStringLiteral("Photoslop Type"));
  save_widget_artifact("ui_inline_text_editor", *canvas);
  const auto layer_count_before_commit = layer_list->count();
  editor->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  canvas->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();

  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->item(0)->text().startsWith(QStringLiteral("Text: Photoslop Type")));
  CHECK(layer_list->count() == layer_count_before_commit + 1);
  QApplication::processEvents();
  const auto committed_text_image = canvas->grab().toImage();
  bool found_text_pixel = false;
  for (int y = 0; y < 120 && !found_text_pixel; y += 2) {
    for (int x = 0; x < 420 && !found_text_pixel; x += 2) {
      const auto widget_point = canvas->widget_position_for_document_point(text_document_point + QPoint(x, y));
      if (!committed_text_image.rect().contains(widget_point)) {
        continue;
      }
      found_text_pixel =
          !color_close(committed_text_image.pixelColor(widget_point), QColor(255, 255, 255), 15);
    }
  }
  CHECK(found_text_pixel);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  auto* background = require_layer_item(*layer_list, QStringLiteral("Background"));
  layer_list->clearSelection();
  layer_list->setCurrentItem(background);
  background->setSelected(true);
  QApplication::processEvents();
  send_mouse(*canvas, QEvent::MouseButtonDblClick, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  auto* reedit = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(reedit != nullptr);
  CHECK(reedit->toPlainText() == QStringLiteral("Photoslop Type"));
  CHECK(reedit->pos() == text_widget_point);
  reedit->setPlainText(QStringLiteral("Canceled Type"));
  send_key(*reedit, Qt::Key_Escape);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before_commit + 1);
  CHECK(layer_list->item(0)->text().startsWith(QStringLiteral("Text: Photoslop Type")));

  send_mouse(*canvas, QEvent::MouseButtonDblClick, text_widget_point, Qt::LeftButton, Qt::LeftButton);
  QApplication::processEvents();
  reedit = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(reedit != nullptr);
  CHECK(reedit->toPlainText() == QStringLiteral("Photoslop Type"));
  text_size->setValue(72);
  text_bold->setChecked(false);
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == reedit);
  CHECK(reedit->property("photoslop.documentTextSize").toInt() == 72);
  CHECK(!reedit->font().bold());
  CHECK(reedit->font().italic());
  reedit->setPlainText(QStringLiteral("Continue"));
  QApplication::processEvents();
  CHECK(reedit->lineWrapMode() == QTextEdit::NoWrap);
  CHECK(reedit->document()->idealWidth() <= static_cast<qreal>(reedit->width() + 1));
  CHECK(reedit->document()->size().height() <
        static_cast<qreal>(reedit->fontMetrics().lineSpacing()) * 1.75);
  reedit->setPlainText(QStringLiteral("Updated Type"));
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == nullptr);
  CHECK(layer_list->count() == layer_count_before_commit + 1);
  CHECK(layer_list->item(0)->text().startsWith(QStringLiteral("Text: Updated Type")));

  const auto before_brush = canvas->grab().toImage();
  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_brush_size(34);
  canvas->set_primary_color(QColor(230, 20, 30));
  drag(*canvas, text_widget_point, text_widget_point + QPoint(1, 1));
  QApplication::processEvents();
  const auto after_brush = canvas->grab().toImage();
  CHECK(color_close(after_brush.pixelColor(text_widget_point), before_brush.pixelColor(text_widget_point), 2));
  save_widget_artifact("ui_text_tool_layer", window);
}

void ui_qimage_import_export_preserves_alpha_and_formats() {
  ensure_artifact_dir();
  QImage source(3, 2, QImage::Format_RGBA8888);
  source.fill(Qt::transparent);
  source.setPixelColor(0, 0, QColor(255, 0, 0, 128));
  source.setPixelColor(1, 0, QColor(0, 255, 0, 255));
  source.setPixelColor(2, 0, QColor(0, 0, 255, 32));

  const auto document = photoslop::ui::document_from_qimage(source, "Alpha Import");
  const auto exported = photoslop::ui::qimage_from_document(document, true);
  CHECK(exported.hasAlphaChannel());
  CHECK(exported.pixelColor(0, 0).alpha() == 128);
  CHECK(exported.pixelColor(1, 0).green() == 255);

  CHECK(exported.save(QStringLiteral("test-artifacts/format_alpha.png")));
  CHECK(photoslop::ui::qimage_from_document(document, false).save(QStringLiteral("test-artifacts/format_flat.jpg")));
  CHECK(photoslop::ui::qimage_from_document(document, false).save(QStringLiteral("test-artifacts/format_flat.bmp")));
  CHECK(QImage(QStringLiteral("test-artifacts/format_alpha.png")).pixelColor(0, 0).alpha() == 128);
}

void ui_qimage_multiply_uses_empty_backdrop_as_transparent() {
  photoslop::Document transparent_document(1, 1, photoslop::PixelFormat::rgba8());
  auto& transparent_multiply = transparent_document.add_pixel_layer(
      "Multiply", solid_pixels(1, 1, photoslop::PixelFormat::rgba8(), QColor(200, 100, 50, 128)));
  transparent_multiply.set_blend_mode(photoslop::BlendMode::Multiply);

  const auto transparent = photoslop::ui::qimage_from_document(transparent_document, true);
  const auto transparent_color = transparent.pixelColor(0, 0);
  CHECK(transparent_color.red() == 200);
  CHECK(transparent_color.green() == 100);
  CHECK(transparent_color.blue() == 50);
  CHECK(transparent_color.alpha() == 128);

  photoslop::Document opaque_document(1, 1, photoslop::PixelFormat::rgb8());
  opaque_document.add_pixel_layer("Base", solid_pixels(1, 1, photoslop::PixelFormat::rgb8(), QColor(100, 160, 240)));
  auto& opaque_multiply = opaque_document.add_pixel_layer(
      "Multiply", solid_pixels(1, 1, photoslop::PixelFormat::rgba8(), QColor(200, 100, 50, 255)));
  opaque_multiply.set_blend_mode(photoslop::BlendMode::Multiply);

  const auto opaque = photoslop::ui::qimage_from_document(opaque_document, true);
  const auto opaque_color = opaque.pixelColor(0, 0);
  CHECK(opaque_color.red() == 78);
  CHECK(opaque_color.green() == 62);
  CHECK(opaque_color.blue() == 47);
  CHECK(opaque_color.alpha() == 255);
}

void ui_dragged_image_file_opens_document_tab() {
  ensure_artifact_dir();
  const auto image_path = std::filesystem::absolute(std::filesystem::path("test-artifacts") / "drag-open.png");
  const auto image_path_qt = QString::fromStdString(image_path.string());

  QImage source(6, 4, QImage::Format_RGB32);
  source.fill(QColor(20, 40, 60));
  source.setPixelColor(2, 1, QColor(30, 200, 240));
  CHECK(source.save(image_path_qt));

  photoslop::ui::MainWindow window;
  show_window(window);

  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  CHECK(tabs->count() == 1);
  auto* canvas = require_canvas(window);

  QMimeData mime_data;
  mime_data.setUrls(QList<QUrl>{QUrl::fromLocalFile(image_path_qt)});
  const auto drop_position = canvas->rect().center();

  QDragEnterEvent drag_enter(drop_position, Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drag_enter);
  QApplication::processEvents();
  CHECK(drag_enter.isAccepted());

  QDragMoveEvent drag_move(drop_position, Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drag_move);
  QApplication::processEvents();
  CHECK(drag_move.isAccepted());

  QDropEvent drop(QPointF(drop_position), Qt::CopyAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(canvas, &drop);
  QApplication::processEvents();

  CHECK(drop.isAccepted());
  CHECK(tabs->count() == 2);
  CHECK(tabs->tabText(tabs->currentIndex()) == QStringLiteral("drag-open.png"));
  canvas = require_canvas(window);
  const auto bounds = canvas->active_layer_document_rect();
  CHECK(bounds.has_value());
  CHECK(*bounds == QRect(0, 0, 6, 4));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(2, 1)), QColor(30, 200, 240), 8));
}

void ui_qimage_render_respects_hidden_layer_groups() {
  photoslop::Document document(1, 1, photoslop::PixelFormat::rgb8());
  photoslop::PixelBuffer background(1, 1, photoslop::PixelFormat::rgb8());
  auto* background_px = background.pixel(0, 0);
  background_px[0] = 255;
  background_px[1] = 255;
  background_px[2] = 255;
  document.add_pixel_layer("Background", std::move(background));

  photoslop::PixelBuffer child_pixels(1, 1, photoslop::PixelFormat::rgba8());
  auto* child_px = child_pixels.pixel(0, 0);
  child_px[0] = 220;
  child_px[1] = 20;
  child_px[2] = 30;
  child_px[3] = 255;
  photoslop::Layer group(document.allocate_layer_id(), "Folder", photoslop::LayerKind::Group);
  group.add_child(photoslop::Layer(document.allocate_layer_id(), "Child", std::move(child_pixels)));
  document.add_layer(std::move(group));

  auto shown = photoslop::ui::qimage_from_document(document, false);
  CHECK(shown.pixelColor(0, 0).red() == 220);

  document.layers()[1].set_visible(false);
  CHECK(document.layers()[1].children().front().visible());
  auto hidden = photoslop::ui::qimage_from_document(document, false);
  CHECK(hidden.pixelColor(0, 0).red() == 255);
  CHECK(hidden.pixelColor(0, 0).green() == 255);
  CHECK(hidden.pixelColor(0, 0).blue() == 255);
}

void ui_qimage_region_render_matches_full_layer_styles() {
  photoslop::Document document(64, 48, photoslop::PixelFormat::rgba8());
  photoslop::PixelBuffer background(64, 48, photoslop::PixelFormat::rgba8());
  for (std::int32_t y = 0; y < background.height(); ++y) {
    for (std::int32_t x = 0; x < background.width(); ++x) {
      auto* px = background.pixel(x, y);
      px[0] = 52;
      px[1] = 58;
      px[2] = 66;
      px[3] = 255;
    }
  }
  document.add_pixel_layer("Background", std::move(background));

  photoslop::PixelBuffer badge(24, 16, photoslop::PixelFormat::rgba8());
  badge.clear(0);
  for (std::int32_t y = 2; y < 14; ++y) {
    for (std::int32_t x = 3; x < 21; ++x) {
      auto* px = badge.pixel(x, y);
      px[0] = 230;
      px[1] = 150;
      px[2] = 35;
      px[3] = 220;
    }
  }

  auto layer = photoslop::Layer(document.allocate_layer_id(), "Styled Badge", std::move(badge));
  const auto styled_layer_id = layer.id();
  layer.set_bounds(photoslop::Rect{18, 14, 24, 16});
  photoslop::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.distance = 4.0F;
  shadow.size = 5.0F;
  shadow.opacity = 0.6F;
  layer.layer_style().drop_shadows.push_back(shadow);
  photoslop::LayerOuterGlow glow;
  glow.enabled = true;
  glow.size = 6.0F;
  glow.opacity = 0.45F;
  glow.color = photoslop::RgbColor{255, 230, 120};
  layer.layer_style().outer_glows.push_back(glow);
  photoslop::LayerStroke stroke;
  stroke.enabled = true;
  stroke.size = 3.0F;
  stroke.color = photoslop::RgbColor{15, 25, 35};
  layer.layer_style().strokes.push_back(stroke);
  document.add_layer(std::move(layer));

  const QRect region(10, 8, 45, 34);
  const auto full = photoslop::ui::qimage_from_document(document, true).copy(region);
  const auto partial = photoslop::ui::qimage_from_document_rect(document, region, true);
  CHECK(partial.size() == full.size());
  for (int y = 0; y < partial.height(); ++y) {
    for (int x = 0; x < partial.width(); ++x) {
      CHECK(color_close(partial.pixelColor(x, y), full.pixelColor(x, y), 0));
    }
  }

  const auto moved_bounds = photoslop::Rect{24, 17, 24, 16};
  const QRect moved_region(10, 8, 50, 36);
  const auto moved_override =
      photoslop::ui::qimage_from_document_rect_with_layer_bounds(document, moved_region, true, styled_layer_id,
                                                                 moved_bounds);
  auto* moved_layer = document.find_layer(styled_layer_id);
  CHECK(moved_layer != nullptr);
  moved_layer->set_bounds(moved_bounds);
  const auto moved_actual = photoslop::ui::qimage_from_document_rect(document, moved_region, true);
  CHECK(moved_override.size() == moved_actual.size());
  for (int y = 0; y < moved_override.height(); ++y) {
    for (int x = 0; x < moved_override.width(); ++x) {
      CHECK(color_close(moved_override.pixelColor(x, y), moved_actual.pixelColor(x, y), 0));
    }
  }
}

void ui_image_adjustments_menu_applies_active_layer_filters() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(10, 120, 240));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(10, 120, 240), 6));

  bool saw_live_filter_preview = false;
  bool canvas_zoomed_with_dialog_open = false;
  bool canvas_panned_with_dialog_open = false;
  const auto zoom_before_dialog = canvas->zoom();
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopFilterDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      CHECK(!dialog->isModal());
      CHECK(dialog->windowModality() == Qt::NonModal);
      CHECK(window.isEnabled());
      auto* amount = dialog->findChild<QSpinBox*>(QStringLiteral("filterAmountSpin"));
      CHECK(amount != nullptr);
      CHECK(amount->value() == 100);
      QApplication::processEvents();
      saw_live_filter_preview = color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(245, 135, 15), 8);
      send_wheel(*canvas, QPoint(300, 240), 120, Qt::AltModifier);
      canvas_zoomed_with_dialog_open = canvas->zoom() > zoom_before_dialog;
      const auto origin_before_pan = canvas->widget_position_for_document_point(QPoint(0, 0));
      send_mouse(*canvas, QEvent::MouseButtonPress, QPoint(300, 240), Qt::MiddleButton, Qt::MiddleButton);
      send_mouse(*canvas, QEvent::MouseMove, QPoint(318, 252), Qt::NoButton, Qt::MiddleButton);
      send_mouse(*canvas, QEvent::MouseButtonRelease, QPoint(318, 252), Qt::MiddleButton, Qt::NoButton);
      canvas_panned_with_dialog_open = canvas->widget_position_for_document_point(QPoint(0, 0)) != origin_before_pan;
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageAdjustInvertAction")->trigger();
  CHECK(saw_live_filter_preview);
  CHECK(canvas_zoomed_with_dialog_open);
  CHECK(canvas_panned_with_dialog_open);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(10, 120, 240), 6));

  accept_filter_dialog();
  require_action(window, "imageAdjustInvertAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(40, 40)), QColor(245, 135, 15), 8));

  accept_filter_dialog();
  require_action(window, "imageAdjustDesaturateAction")->trigger();
  QApplication::processEvents();
  const auto gray = canvas_pixel(*canvas, QPoint(40, 40));
  CHECK(std::abs(gray.red() - gray.green()) <= 2);
  CHECK(std::abs(gray.green() - gray.blue()) <= 2);
  save_widget_artifact("ui_image_adjustments_invert_desaturate", *canvas);

  canvas->set_primary_color(QColor(50, 50, 50));
  canvas->set_secondary_color(QColor(180, 180, 180));
  require_action_by_text(window, QStringLiteral("Gradient"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 90)),
       canvas->widget_position_for_document_point(QPoint(260, 90)));
  QApplication::processEvents();
  CHECK(canvas_pixel(*canvas, QPoint(20, 90)).red() > 40);
  CHECK(canvas_pixel(*canvas, QPoint(260, 90)).red() < 190);

  accept_filter_dialog();
  require_action(window, "imageAdjustAutoContrastAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas_pixel(*canvas, QPoint(20, 90)).red() < 12);
  CHECK(canvas_pixel(*canvas, QPoint(260, 90)).red() > 242);
  save_widget_artifact("ui_image_adjustments_auto_contrast", *canvas);

  auto* edge_detect = require_action(window, "filterAction_photoslop_filters_edge_detect");
  CHECK(edge_detect->toolTip().contains(QStringLiteral("Edge Detect")));
  accept_filter_dialog({{QStringLiteral("filterStrengthSpin"), 150}});
  edge_detect->trigger();
  QApplication::processEvents();
  CHECK(canvas_pixel(*canvas, QPoint(140, 90)).red() < 80);
  auto* history = window.findChild<QListWidget*>(QStringLiteral("historyList"));
  CHECK(history != nullptr);
  CHECK(history->item(0) != nullptr);
  CHECK(history->item(0)->text().contains(QStringLiteral("Edge Detect")));
  save_widget_artifact("ui_filter_edge_detect", *canvas);

  auto* emboss = require_action(window, "filterAction_photoslop_filters_emboss");
  accept_filter_dialog({{QStringLiteral("filterAngleSpin"), 90},
                        {QStringLiteral("filterHeightSpin"), 4},
                        {QStringLiteral("filterDepthSpin"), 140}});
  emboss->trigger();
  QApplication::processEvents();
  CHECK(window.findChild<QListWidget*>(QStringLiteral("historyList"))->item(0)->text().contains(QStringLiteral("Emboss")));

  canvas->set_primary_color(QColor(255, 0, 0));
  canvas->set_secondary_color(QColor(0, 0, 255));
  auto* clouds = require_action(window, "filterAction_photoslop_filters_clouds");
  accept_filter_dialog({{QStringLiteral("filterScaleSpin"), 48},
                        {QStringLiteral("filterDetailSpin"), 4},
                        {QStringLiteral("filterContrastSpin"), 60},
                        {QStringLiteral("filterSeedSpin"), 3}});
  clouds->trigger();
  QApplication::processEvents();
  const auto cloud_pixel = canvas_pixel(*canvas, QPoint(40, 40));
  CHECK(cloud_pixel.green() < 8);
  CHECK(cloud_pixel.red() > 0);
  CHECK(cloud_pixel.blue() > 0);
  CHECK(std::abs(cloud_pixel.red() - cloud_pixel.blue()) > 4);
  save_widget_artifact("ui_filter_clouds_foreground_background", *canvas);
}

void ui_image_adjustments_respect_active_selection() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(20, 90, 220));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(100, 100)));
  QApplication::processEvents();
  accept_filter_dialog();
  require_action(window, "imageAdjustInvertAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(235, 165, 35), 10));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(160, 70)), QColor(20, 90, 220), 10));
  save_widget_artifact("ui_image_adjustment_selection_scope", *canvas);
}

void ui_hue_saturation_dialog_adjusts_selected_pixels() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(255, 0, 0));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(255, 0, 0), 8));

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(120, 120)));
  QApplication::processEvents();

  bool saw_hue_preview = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopHueSaturationDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
      CHECK(hue != nullptr);
      hue->setValue(120);
      QApplication::processEvents();
      saw_hue_preview = color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 255, 0), 12);
      dialog->reject();
      return;
    }
    CHECK(false);
  });
  require_action(window, "imageAdjustHueSaturationAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_hue_preview);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(255, 0, 0), 12));

  accept_hue_saturation_dialog(120, 0, 0);
  require_action(window, "imageAdjustHueSaturationAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 255, 0), 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(180, 70)), QColor(255, 0, 0), 12));
  save_widget_artifact("ui_hue_saturation_selection", *canvas);
}

void ui_hue_saturation_creates_masked_adjustment_layer() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_primary_color(QColor(255, 0, 0));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(120, 120)));
  QApplication::processEvents();

  bool saw_adjustment_layer_preview = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopHueSaturationDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
      CHECK(hue != nullptr);
      hue->setValue(120);
      QApplication::processEvents();
      saw_adjustment_layer_preview = color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 255, 0), 12);
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "layerNewHueSaturationAdjustmentAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_adjustment_layer_preview);

  CHECK(layer_list->item(0) != nullptr);
  CHECK(layer_list->item(0)->text() == QStringLiteral("Hue/Saturation"));
  auto* adjustment_row = layer_list->itemWidget(layer_list->item(0));
  CHECK(adjustment_row != nullptr);
  CHECK(adjustment_row->findChild<QLabel*>(QStringLiteral("layerMaskThumbnail")) != nullptr);
  auto* adjustment_thumbnail = adjustment_row->findChild<QLabel*>(QStringLiteral("layerContentThumbnail"));
  CHECK(adjustment_thumbnail != nullptr);
  const auto thumbnail_image = adjustment_thumbnail->pixmap(Qt::ReturnByValue).toImage();
  CHECK(thumbnail_image.pixelColor(2, 2).lightness() < 220);
  auto* details = adjustment_row->findChild<QLabel*>(QStringLiteral("layerRowDetails"));
  CHECK(details != nullptr);
  CHECK(details->text().contains(QStringLiteral("Hue/Saturation adjustment")));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 255, 0), 12));

  bool saw_initial_adjustment_settings = false;
  bool saw_adjustment_edit_preview = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("photoslopHueSaturationDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
      CHECK(hue != nullptr);
      saw_initial_adjustment_settings = hue->value() == 120;
      hue->setValue(-120);
      QApplication::processEvents();
      saw_adjustment_edit_preview = color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 0, 255), 20);
      dialog->accept();
      return;
    }
    CHECK(false);
  });
  require_action(window, "layerEditAdjustmentAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_initial_adjustment_settings);
  CHECK(saw_adjustment_edit_preview);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(0, 0, 255), 20));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(180, 70)), QColor(255, 0, 0), 12));

  layer_list->item(0)->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  CHECK(color_close(canvas_pixel(*canvas, QPoint(70, 70)), QColor(255, 0, 0), 12));
  CHECK(color_close(canvas_pixel(*canvas, QPoint(180, 70)), QColor(255, 0, 0), 12));
  save_widget_artifact("ui_hue_saturation_adjustment_layer", window);
}

void ui_levels_dialog_remaps_selected_tonal_range() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(50, 50, 50));
  canvas->set_secondary_color(QColor(180, 180, 180));
  require_action_by_text(window, QStringLiteral("Gradient"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 90)),
       canvas->widget_position_for_document_point(QPoint(260, 90)));
  QApplication::processEvents();

  const auto outside_before = canvas_pixel(*canvas, QPoint(320, 90));
  CHECK(outside_before.red() >= 170);
  CHECK(outside_before.red() <= 190);

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 50)),
       canvas->widget_position_for_document_point(QPoint(260, 130)));
  QApplication::processEvents();

  accept_levels_dialog(50, 180, 100);
  require_action(window, "imageAdjustLevelsAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  CHECK(canvas_pixel(*canvas, QPoint(20, 90)).red() < 12);
  CHECK(canvas_pixel(*canvas, QPoint(260, 90)).red() > 242);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(320, 90)), outside_before, 8));
  save_widget_artifact("ui_levels_selection", *canvas);
}

void ui_curves_dialog_remaps_midtones_in_selection() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_secondary_color(QColor(255, 255, 255));
  require_action_by_text(window, QStringLiteral("Gradient"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 90)),
       canvas->widget_position_for_document_point(QPoint(260, 90)));
  QApplication::processEvents();

  const auto selected_before = canvas_pixel(*canvas, QPoint(140, 90));
  const auto outside_before = canvas_pixel(*canvas, QPoint(320, 90));
  CHECK(selected_before.red() > 110);
  CHECK(selected_before.red() < 150);
  CHECK(outside_before.red() > 245);

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 50)),
       canvas->widget_position_for_document_point(QPoint(260, 130)));
  QApplication::processEvents();

  accept_curves_dialog(0, 220, 255);
  require_action(window, "imageAdjustCurvesAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  const auto selected_after = canvas_pixel(*canvas, QPoint(140, 90));
  CHECK(selected_after.red() > selected_before.red() + 60);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(320, 90)), outside_before, 8));
  save_widget_artifact("ui_curves_selection", *canvas);
}

void ui_color_balance_dialog_adjusts_selected_pixels() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(128, 128, 128));
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();

  require_action_by_text(window, QStringLiteral("Marquee"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 40)),
       canvas->widget_position_for_document_point(QPoint(120, 120)));
  QApplication::processEvents();

  accept_color_balance_dialog(50, -40, 30);
  require_action(window, "imageAdjustColorBalanceAction")->trigger();
  QApplication::processEvents();
  require_action(window, "editDeselectAction")->trigger();
  QApplication::processEvents();

  const auto adjusted = canvas_pixel(*canvas, QPoint(70, 70));
  CHECK(adjusted.red() > 245);
  CHECK(adjusted.green() < 40);
  CHECK(adjusted.blue() > 195);
  CHECK(color_close(canvas_pixel(*canvas, QPoint(180, 70)), QColor(128, 128, 128), 8));
  save_widget_artifact("ui_color_balance_selection", *canvas);
}

void ui_gradient_and_magic_wand_render_visually() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(255, 0, 0));
  canvas->set_secondary_color(QColor(0, 0, 255));
  canvas->set_tool(photoslop::ui::CanvasTool::Gradient);
  drag(*canvas, QPoint(60, 140), QPoint(280, 140));
  QApplication::processEvents();

  const auto gradient_image = canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
  CHECK(color_close(QColor(gradient_image.pixel(62, 140)), QColor(255, 0, 0), 35));
  CHECK(color_close(QColor(gradient_image.pixel(278, 140)), QColor(0, 0, 255), 35));
  save_widget_artifact("ui_gradient_tool", *canvas);

  canvas->set_tool(photoslop::ui::CanvasTool::MagicWand);
  send_mouse(*canvas, QEvent::MouseButtonPress, QPoint(62, 140), Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, QPoint(62, 140), Qt::LeftButton, Qt::NoButton);
  CHECK(canvas->selected_document_rect().has_value());
  save_widget_artifact("ui_magic_wand_selection", *canvas);
}

void ui_magic_wand_complex_selection_is_responsive() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(0, 0, 0));
  canvas->set_brush_size(10);
  for (int y = 80; y <= 240; y += 40) {
    for (int x = 90; x <= 290; x += 40) {
      const auto point = canvas->widget_position_for_document_point(QPoint(x, y));
      drag(*canvas, point, point + QPoint(1, 1));
    }
  }
  QApplication::processEvents();

  canvas->set_tool(photoslop::ui::CanvasTool::MagicWand);
  QElapsedTimer timer;
  timer.start();
  const auto click_point = canvas->widget_position_for_document_point(QPoint(25, 25));
  send_mouse(*canvas, QEvent::MouseButtonPress, click_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click_point, Qt::LeftButton, Qt::NoButton);
  const auto elapsed_ms = timer.elapsed();
  CHECK(elapsed_ms < 1500);
  CHECK(canvas->selected_document_region().contains(QPoint(25, 25)));
  CHECK(!canvas->selected_document_region().contains(QPoint(90, 80)));
  save_widget_artifact("ui_magic_wand_complex_selection", *canvas);
}

void ui_bundled_legacy_plugin_action_applies_filter() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  require_action_by_text(window, QStringLiteral("Brush"))->trigger();
  canvas->set_primary_color(QColor(255, 0, 0));
  canvas->set_brush_size(24);
  const QPoint sample_document_point(72, 72);
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(60, 72)),
       canvas->widget_position_for_document_point(QPoint(95, 72)));
  QApplication::processEvents();

  const auto sample_widget_point = canvas->widget_position_for_document_point(sample_document_point);
  const auto before = canvas->grab().toImage().pixelColor(sample_widget_point);
  CHECK(before.red() > 180);
  CHECK(before.green() < 100);
  CHECK(before.blue() < 100);

  QAction* greyscale = nullptr;
  for (auto* action : window.findChildren<QAction*>(QStringLiteral("legacyPluginAction"))) {
    if (action->text().contains(QStringLiteral("Greyscale"), Qt::CaseInsensitive)) {
      greyscale = action;
      break;
    }
  }
  CHECK(greyscale != nullptr);
  greyscale->trigger();
  QApplication::processEvents();

  const auto after = canvas->grab().toImage().pixelColor(sample_widget_point);
  CHECK(std::abs(after.red() - after.green()) <= 8);
  CHECK(std::abs(after.green() - after.blue()) <= 8);
  CHECK(after.red() > 45);
  CHECK(after.red() < 120);
  save_widget_artifact("ui_legacy_plugin_greyscale", window);
}

void ui_transparency_checkerboard_and_copy_paste_preserve_alpha() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);

  canvas->set_primary_color(Qt::white);
  require_action(window, "layerFillForegroundAction")->trigger();
  QApplication::processEvents();
  require_legacy_plugin_action(window, QStringLiteral("White to Transparent"))->trigger();
  QApplication::processEvents();

  require_layer_item(*layer_list, QStringLiteral("Background"))->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  auto transparent_preview = canvas_pixel(*canvas, QPoint(40, 40));
  CHECK(transparent_preview.alpha() == 255);
  CHECK(transparent_preview.red() >= 170);
  CHECK(transparent_preview.red() <= 245);
  CHECK(std::abs(transparent_preview.red() - transparent_preview.green()) <= 4);
  CHECK(std::abs(transparent_preview.green() - transparent_preview.blue()) <= 4);
  save_widget_artifact("ui_transparency_checkerboard", window);

  require_action(window, "editSelectAllAction")->trigger();
  require_action(window, "editCopyAction")->trigger();
  QApplication::processEvents();

  auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
  CHECK(tabs != nullptr);
  accept_new_document_dialog(220, 180);
  require_action_by_text(window, QStringLiteral("New"))->trigger();
  QApplication::processEvents();
  require_action(window, "editPasteAction")->trigger();
  QApplication::processEvents();

  canvas = require_canvas(window);
  layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  require_layer_item(*layer_list, QStringLiteral("Background"))->setCheckState(Qt::Unchecked);
  QApplication::processEvents();
  const auto pasted_preview = canvas_pixel(*canvas, QPoint(24, 24));
  CHECK(pasted_preview.red() >= 170);
  CHECK(pasted_preview.red() <= 245);
  CHECK(std::abs(pasted_preview.red() - pasted_preview.green()) <= 4);
  CHECK(std::abs(pasted_preview.green() - pasted_preview.blue()) <= 4);
  save_widget_artifact("ui_transparent_copy_paste", window);
}

void ui_crop_rotate_stroke_merge_and_filter_render_visually() {
  photoslop::ui::MainWindow window;
  show_window(window);
  auto* canvas = require_canvas(window);

  canvas->set_primary_color(QColor(0, 130, 255));
  canvas->set_tool(photoslop::ui::CanvasTool::Brush);
  drag(*canvas, QPoint(72, 72), QPoint(190, 140));
  save_widget_artifact("ui_brush_before_crop", *canvas);

  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);
  drag(*canvas, QPoint(60, 60), QPoint(200, 160));
  require_action(window, "imageCropToSelectionAction")->trigger();
  QApplication::processEvents();
  auto* info = window.findChild<QLabel*>(QStringLiteral("documentInfoLabel"));
  CHECK(info != nullptr);
  CHECK(info->text().contains(QStringLiteral("141 x 101 px")));
  save_widget_artifact("ui_crop_to_selection", window);

  require_action(window, "imageRotateClockwiseAction")->trigger();
  QApplication::processEvents();
  CHECK(info->text().contains(QStringLiteral("101 x 141 px")));
  save_widget_artifact("ui_rotate_canvas", window);

  canvas->set_primary_color(QColor(255, 50, 50));
  canvas->set_tool(photoslop::ui::CanvasTool::Marquee);
  drag(*canvas, QPoint(52, 52), QPoint(100, 90));
  require_action(window, "editStrokeSelectionAction")->trigger();
  QApplication::processEvents();
  save_widget_artifact("ui_stroke_selection", *canvas);

  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  const auto layers_before = layer_list->count();
  require_action(window, "layerMergeVisibleAction")->trigger();
  QApplication::processEvents();
  CHECK(layer_list->count() == layers_before + 1);

  auto* sepia = find_action_by_text(window, QStringLiteral("Vintage Sepia"));
  CHECK(sepia != nullptr);
  accept_filter_dialog();
  sepia->trigger();
  QApplication::processEvents();
  save_widget_artifact("ui_merge_visible_and_filter", window);
}

void visual_contact_sheet_contains_new_feature_artifacts() {
  ensure_artifact_dir();
  const std::vector<std::string> artifacts = {
      "ui_main_window.png",
      "ui_color_picker.png",
      "ui_color_picker_gradient.png",
      "ui_color_picker_result.png",
      "ui_canvas_wheel_navigation.png",
      "ui_shape_flyout_zoom_tool.png",
      "ui_filled_shape_preview_cleanup.png",
      "ui_tool_options_move.png",
      "ui_tool_options_text.png",
      "ui_info_panel_layers_docks.png",
      "ui_layer_style_dialog.png",
      "ui_layer_style_result.png",
      "ui_new_document_dialog.png",
      "ui_new_document_result.png",
      "ui_canvas_size_dialog.png",
      "ui_canvas_size_result.png",
      "ui_multiple_documents.png",
      "ui_first_tab_draw_after_second_tab.png",
      "ui_tab_session_layers.png",
      "ui_new_layer_dialog.png",
      "ui_new_layer_result.png",
      "ui_multiselect_merge_selected.png",
      "ui_multiselect_duplicate_delete.png",
      "ui_duplicate_text_folder_tree.png",
      "ui_copy_paste_layer_panel_tree.png",
      "ui_layer_visibility_drag_reorder.png",
      "ui_layer_folder_drag_drop.png",
      "ui_layer_folder_expand_contract.png",
      "ui_layer_folder_saved_state.png",
      "ui_auto_select_reveals_collapsed_folder.png",
      "ui_move_preview_layer_order.png",
      "ui_move_active_tab_only.png",
      "ui_move_selected_folder_tree.png",
      "ui_selection_modifiers.png",
      "ui_selection_toolbar_modes.png",
      "ui_alt_backspace_fill_selection.png",
      "ui_marquee_fixed_size_ratio.png",
      "ui_elliptical_marquee.png",
      "ui_marquee_space_drag_reposition.png",
      "ui_complex_selection_outline.png",
      "ui_selection_edges_visible_no_tint.png",
      "ui_selection_edges_hidden.png",
      "ui_select_inverse.png",
      "ui_select_reselect.png",
      "ui_selection_expand_contract_transparency.png",
      "ui_selection_border.png",
      "ui_ctrl_click_layer_transparency.png",
      "ui_select_grow.png",
      "ui_select_similar.png",
      "ui_complex_stroke_selection.png",
      "ui_extended_blend_modes.png",
      "ui_layer_lock_transparency.png",
      "ui_keyboard_nudge_layer.png",
      "ui_lasso_selection.png",
      "ui_copy_paste_transform.png",
      "ui_transform_opaque_bounds.png",
      "ui_layer_via_copy_cut.png",
      "ui_layer_mask_from_selection.png",
      "ui_layer_thumbnail_refresh.png",
      "ui_cut_selection.png",
      "ui_brush_expands_pasted_layer.png",
      "ui_brush_opacity_per_stroke.png",
      "ui_airbrush_builds_up.png",
      "ui_clone_tool_stamp.png",
      "ui_smudge_tool.png",
      "ui_hidden_layer_copy_ignored.png",
      "ui_copy_selected_layers.png",
      "ui_background_eraser_transparency.png",
      "ui_inline_text_editor.png",
      "ui_text_tool_layer.png",
      "format_alpha.png",
      "ui_image_adjustments_invert_desaturate.png",
      "ui_image_adjustments_auto_contrast.png",
      "ui_image_adjustment_selection_scope.png",
      "ui_hue_saturation_dialog.png",
      "ui_hue_saturation_selection.png",
      "ui_hue_saturation_adjustment_layer.png",
      "ui_levels_dialog.png",
      "ui_levels_selection.png",
      "ui_curves_dialog.png",
      "ui_curves_selection.png",
      "ui_color_balance_dialog.png",
      "ui_color_balance_selection.png",
      "ui_gradient_tool.png",
      "ui_magic_wand_selection.png",
      "ui_magic_wand_complex_selection.png",
      "ui_legacy_plugin_greyscale.png",
      "ui_transparency_checkerboard.png",
      "ui_transparent_copy_paste.png",
      "ui_crop_to_selection.png",
      "ui_rotate_canvas.png",
      "ui_stroke_selection.png",
      "ui_merge_visible_and_filter.png",
      "tool_gradient.bmp",
      "tool_soft_brush.bmp",
      "tool_brush_expand_layer.bmp",
      "document_crop.bmp",
      "document_canvas_resize.bmp",
      "document_rotate_clockwise.bmp",
      "document_rotate_counterclockwise.bmp",
      "tool_stroke_selection.bmp",
      "tool_lock_transparency.bmp",
      "layer_merge_visible.bmp",
      "filter_brightness.bmp",
      "filter_contrast.bmp",
      "filter_grayscale.bmp",
      "filter_desaturate.bmp",
      "filter_auto_contrast.bmp",
      "filter_sepia.bmp",
      "filter_threshold.bmp",
      "filter_posterize.bmp",
      "filter_box_blur.bmp",
      "filter_sharpen.bmp",
      "filter_gaussian_blur.bmp",
      "filter_edge_detect.bmp",
      "filter_emboss.bmp",
      "filter_pixelate.bmp",
      "filter_film_grain.bmp",
      "filter_vignette.bmp",
      "ui_filter_edge_detect.png",
  };

  constexpr int kColumns = 4;
  constexpr int kCellWidth = 280;
  constexpr int kCellHeight = 220;
  constexpr int kPadding = 12;
  const auto rows = static_cast<int>((artifacts.size() + kColumns - 1) / kColumns);
  QImage sheet(kColumns * kCellWidth, rows * kCellHeight, QImage::Format_RGB32);
  sheet.fill(QColor(30, 32, 36));

  QPainter painter(&sheet);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.setFont(visual_test_font());
  painter.setPen(QColor(225, 230, 238));
  for (std::size_t index = 0; index < artifacts.size(); ++index) {
    const auto path = std::filesystem::path("test-artifacts") / artifacts[index];
    CHECK(std::filesystem::exists(path));
    QImage image(QString::fromStdString(path.string()));
    CHECK(!image.isNull());

    const auto column = static_cast<int>(index % kColumns);
    const auto row = static_cast<int>(index / kColumns);
    const QRect cell(column * kCellWidth, row * kCellHeight, kCellWidth, kCellHeight);
    painter.fillRect(cell.adjusted(4, 4, -4, -4), QColor(42, 45, 51));
    painter.drawText(cell.adjusted(kPadding, 8, -kPadding, -kPadding), Qt::AlignTop | Qt::AlignLeft,
                     QString::fromStdString(artifacts[index]));

    const QRect image_rect = cell.adjusted(kPadding, 34, -kPadding, -kPadding);
    const auto scaled = image.scaled(image_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint image_pos(image_rect.x() + (image_rect.width() - scaled.width()) / 2,
                           image_rect.y() + (image_rect.height() - scaled.height()) / 2);
    painter.drawImage(image_pos, scaled);
  }
  painter.end();

  CHECK(sheet.save(QStringLiteral("test-artifacts/visual_feature_contact_sheet.png")));
}

}  // namespace

int main(int argc, char* argv[]) {
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
  QApplication app(argc, argv);
  app.setFont(visual_test_font());
  {
    QSettings settings(QStringLiteral("Photoslop"), QStringLiteral("Photoslop"));
    settings.remove(QStringLiteral("tools"));
    settings.sync();
  }

  const std::vector<TestCase> tests = {
      {"ui_main_window_renders_color_swatches", ui_main_window_renders_color_swatches},
      {"ui_color_picker_changes_foreground_color", ui_color_picker_changes_foreground_color},
      {"ui_dialog_position_memory_restores_last_position", ui_dialog_position_memory_restores_last_position},
      {"ui_dirty_state_marks_tabs_and_undo_restores_saved_revision",
       ui_dirty_state_marks_tabs_and_undo_restores_saved_revision},
      {"ui_compatibility_report_flags_psd_text_placeholders",
       ui_compatibility_report_flags_psd_text_placeholders},
      {"ui_alt_left_click_samples_foreground_color", ui_alt_left_click_samples_foreground_color},
      {"ui_photoshop_shortcuts_are_registered", ui_photoshop_shortcuts_are_registered},
      {"ui_canvas_wheel_matches_photoshop_navigation", ui_canvas_wheel_matches_photoshop_navigation},
      {"ui_canvas_fractional_zoom_paints_to_document_edge", ui_canvas_fractional_zoom_paints_to_document_edge},
      {"ui_shape_flyout_and_zoom_tool_work", ui_shape_flyout_and_zoom_tool_work},
      {"ui_filled_shape_preview_clears_after_commit", ui_filled_shape_preview_clears_after_commit},
      {"ui_options_bar_tracks_active_tool", ui_options_bar_tracks_active_tool},
      {"ui_right_docks_collapse_layers_show_metadata_and_info_updates",
       ui_right_docks_collapse_layers_show_metadata_and_info_updates},
      {"ui_layer_context_menu_exposes_blending_options_dialog",
       ui_layer_context_menu_exposes_blending_options_dialog},
      {"ui_new_document_and_canvas_size_dialogs_work", ui_new_document_and_canvas_size_dialogs_work},
      {"ui_first_tab_still_draws_after_second_tab_created", ui_first_tab_still_draws_after_second_tab_created},
      {"ui_tab_switch_layers_follow_the_canvas_after_tab_reorder",
       ui_tab_switch_layers_follow_the_canvas_after_tab_reorder},
      {"ui_new_layer_dialog_and_multiselect_layers_work", ui_new_layer_dialog_and_multiselect_layers_work},
      {"ui_duplicate_layer_copies_text_and_folder_trees", ui_duplicate_layer_copies_text_and_folder_trees},
      {"ui_copy_paste_layer_panel_copies_layers_and_folder_trees",
       ui_copy_paste_layer_panel_copies_layers_and_folder_trees},
      {"ui_layer_rows_toggle_visibility_and_drag_reorder", ui_layer_rows_toggle_visibility_and_drag_reorder},
      {"ui_layer_folders_create_with_drag_drop_affordances", ui_layer_folders_create_with_drag_drop_affordances},
      {"ui_layer_folders_expand_and_contract_children", ui_layer_folders_expand_and_contract_children},
      {"ui_layer_folders_open_with_saved_expansion_state", ui_layer_folders_open_with_saved_expansion_state},
      {"ui_move_auto_select_reveals_layers_in_collapsed_folders",
       ui_move_auto_select_reveals_layers_in_collapsed_folders},
      {"ui_folder_visibility_preserves_layer_panel_scroll", ui_folder_visibility_preserves_layer_panel_scroll},
      {"ui_move_preview_preserves_layer_order", ui_move_preview_preserves_layer_order},
      {"ui_move_tool_moves_selected_layers_together", ui_move_tool_moves_selected_layers_together},
      {"ui_move_tool_moves_selected_folder_tree", ui_move_tool_moves_selected_folder_tree},
      {"ui_move_preview_clears_transparent_trails_and_keeps_layer_styles",
       ui_move_preview_clears_transparent_trails_and_keeps_layer_styles},
      {"ui_layer_move_repaints_only_active_document_tab", ui_layer_move_repaints_only_active_document_tab},
      {"ui_arduboy_psd_render_path_if_available", ui_arduboy_psd_render_path_if_available},
      {"ui_marquee_selection_modifiers_work", ui_marquee_selection_modifiers_work},
      {"ui_selection_toolbar_modes_work", ui_selection_toolbar_modes_work},
      {"ui_alt_backspace_fills_selection_with_foreground", ui_alt_backspace_fills_selection_with_foreground},
      {"ui_marquee_fixed_size_and_ratio_options_work", ui_marquee_fixed_size_and_ratio_options_work},
      {"ui_elliptical_marquee_selects_oval_region", ui_elliptical_marquee_selects_oval_region},
      {"ui_marquee_space_drag_repositions_active_rect", ui_marquee_space_drag_repositions_active_rect},
      {"ui_complex_selection_draws_region_outline", ui_complex_selection_draws_region_outline},
      {"ui_ctrl_h_hides_selection_edges_without_blue_tint", ui_ctrl_h_hides_selection_edges_without_blue_tint},
      {"ui_select_inverse_and_extended_blend_modes_work", ui_select_inverse_and_extended_blend_modes_work},
      {"ui_selection_expand_contract_and_layer_transparency_work",
       ui_selection_expand_contract_and_layer_transparency_work},
      {"ui_ctrl_click_layer_loads_layer_transparency", ui_ctrl_click_layer_loads_layer_transparency},
      {"ui_select_grow_and_similar_use_magic_wand_tolerance",
       ui_select_grow_and_similar_use_magic_wand_tolerance},
      {"ui_complex_selection_stroke_uses_region_outline", ui_complex_selection_stroke_uses_region_outline},
      {"ui_layer_lock_transparency_and_keyboard_nudge_work", ui_layer_lock_transparency_and_keyboard_nudge_work},
      {"ui_lasso_selection_draws_freeform_region", ui_lasso_selection_draws_freeform_region},
      {"ui_copy_paste_and_transform_pasted_layer_work", ui_copy_paste_and_transform_pasted_layer_work},
      {"ui_free_transform_uses_opaque_pixel_bounds", ui_free_transform_uses_opaque_pixel_bounds},
      {"ui_layer_via_copy_and_cut_match_photoshop_shortcuts",
       ui_layer_via_copy_and_cut_match_photoshop_shortcuts},
      {"ui_layer_mask_from_selection_hides_pixels_and_shows_thumbnail",
       ui_layer_mask_from_selection_hides_pixels_and_shows_thumbnail},
      {"ui_layer_thumbnail_updates_after_brush_edit", ui_layer_thumbnail_updates_after_brush_edit},
      {"ui_cut_selection_clears_source_and_keeps_clipboard", ui_cut_selection_clears_source_and_keeps_clipboard},
      {"ui_brush_on_pasted_layer_expands_layer_bounds", ui_brush_on_pasted_layer_expands_layer_bounds},
      {"ui_brush_opacity_caps_per_stroke", ui_brush_opacity_caps_per_stroke},
      {"ui_airbrush_preset_builds_up_within_one_stroke", ui_airbrush_preset_builds_up_within_one_stroke},
      {"ui_clone_tool_samples_source_and_paints_offset", ui_clone_tool_samples_source_and_paints_offset},
      {"ui_smudge_tool_drags_painted_pixels", ui_smudge_tool_drags_painted_pixels},
      {"ui_copy_ignores_hidden_active_layer", ui_copy_ignores_hidden_active_layer},
      {"ui_copy_selected_layers_copies_composited_selection", ui_copy_selected_layers_copies_composited_selection},
      {"ui_eraser_on_background_reveals_transparency_and_size_cursor",
       ui_eraser_on_background_reveals_transparency_and_size_cursor},
      {"ui_text_tool_creates_visible_text_layer", ui_text_tool_creates_visible_text_layer},
      {"ui_qimage_import_export_preserves_alpha_and_formats", ui_qimage_import_export_preserves_alpha_and_formats},
      {"ui_qimage_multiply_uses_empty_backdrop_as_transparent",
       ui_qimage_multiply_uses_empty_backdrop_as_transparent},
      {"ui_dragged_image_file_opens_document_tab", ui_dragged_image_file_opens_document_tab},
      {"ui_qimage_render_respects_hidden_layer_groups", ui_qimage_render_respects_hidden_layer_groups},
      {"ui_qimage_region_render_matches_full_layer_styles",
       ui_qimage_region_render_matches_full_layer_styles},
      {"ui_image_adjustments_menu_applies_active_layer_filters",
       ui_image_adjustments_menu_applies_active_layer_filters},
      {"ui_image_adjustments_respect_active_selection", ui_image_adjustments_respect_active_selection},
      {"ui_hue_saturation_dialog_adjusts_selected_pixels", ui_hue_saturation_dialog_adjusts_selected_pixels},
      {"ui_hue_saturation_creates_masked_adjustment_layer", ui_hue_saturation_creates_masked_adjustment_layer},
      {"ui_levels_dialog_remaps_selected_tonal_range", ui_levels_dialog_remaps_selected_tonal_range},
      {"ui_curves_dialog_remaps_midtones_in_selection", ui_curves_dialog_remaps_midtones_in_selection},
      {"ui_color_balance_dialog_adjusts_selected_pixels", ui_color_balance_dialog_adjusts_selected_pixels},
      {"ui_gradient_and_magic_wand_render_visually", ui_gradient_and_magic_wand_render_visually},
      {"ui_magic_wand_complex_selection_is_responsive", ui_magic_wand_complex_selection_is_responsive},
      {"ui_bundled_legacy_plugin_action_applies_filter", ui_bundled_legacy_plugin_action_applies_filter},
      {"ui_transparency_checkerboard_and_copy_paste_preserve_alpha",
       ui_transparency_checkerboard_and_copy_paste_preserve_alpha},
      {"ui_crop_rotate_stroke_merge_and_filter_render_visually", ui_crop_rotate_stroke_merge_and_filter_render_visually},
      {"visual_contact_sheet_contains_new_feature_artifacts", visual_contact_sheet_contains_new_feature_artifacts},
  };

  int failures = 0;
  for (const auto& test : tests) {
    try {
      test.run();
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
    }
  }

  return failures == 0 ? 0 : 1;
}
