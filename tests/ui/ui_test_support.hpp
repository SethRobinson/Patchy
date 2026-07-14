#pragma once

// Shared UI-test helpers moved verbatim from tests/ui_visual_tests.cpp (used by
// more than one test group). Moved, never copied. Default arguments live on
// these declarations only; tablet_test_device's three static QPointingDevice
// locals are defined exactly once, in ui_test_support.cpp.

#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/contour_presets.hpp"
#include "core/gradient_presets.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_presets.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_effects.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "ui/smart_object_render.hpp"
#include "core/layer_tree.hpp"
#include "core/palette.hpp"
#include "core/palette_presets.hpp"
#include "ui/palette_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_browser.hpp"
#include "ui/style_library.hpp"
#include "ui/style_manager_dialog.hpp"
#include "psd/asl_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_layer_effects.hpp"
#include "core/style_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/blend_if_range_editor.hpp"
#include "ui/color_panel.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/curves_editor.hpp"
#include "ui/curves_presets.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/filter_look_library.hpp"
#include "ui/font_picker.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/print_dialog.hpp"
#include "ui/selection_outline.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/app_settings.hpp"
#include "ui/update_checker.hpp"
#include "ui/visual_filter_gallery_dialog.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/zoom_status_bar.hpp"
#include "filters/builtin_filters.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "render/compositor.hpp"
#include "synthetic_dng.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"

#include <QAbstractItemModel>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDataStream>
#include <QDockWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QGroupBox>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QInputDevice>
#include <QInputDialog>
#include <QKeyEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListView>
#include <QLayout>
#include <QListWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QLocale>
#include <QSizeGrip>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QIODevice>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QThread>
#include <QPaintEvent>
#include <QPixmap>
#include <QPointingDevice>
#include <QProgressDialog>
#include <QPushButton>
#include <QStackedWidget>
#include <QRadioButton>
#include <QSpinBox>
#include <QStringList>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTabletEvent>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVariant>
#include <QWheelEvent>
#include <QWindow>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
// windows.h stays available to the tests that keep Windows-specific assertions
// behind #ifdef Q_OS_WIN; the dbghelp crash handler now lives in tests/ui/main.cpp.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "ui_test_access.hpp"

namespace patchy::test::ui {

// Suite-wide font + per-role registration live in tests/test_fonts.hpp so every
// platform-specific font path stays in that one header.
using patchy::test::TestFontRole;

using patchy::test::register_test_fonts;

using patchy::test::visual_test_font;

using patchy::test::TestCase;

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

class DragEventRecorder final : public QObject {
public:
  explicit DragEventRecorder(bool consume = false) : consume_(consume) {}

  int enters{0};
  int moves{0};
  int drops{0};
  int leaves{0};

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    bool drag_event = true;
    switch (event->type()) {
      case QEvent::DragEnter:
        ++enters;
        break;
      case QEvent::DragMove:
        ++moves;
        break;
      case QEvent::Drop:
        ++drops;
        break;
      case QEvent::DragLeave:
        ++leaves;
        break;
      default:
        drag_event = false;
        break;
    }
    if (drag_event && consume_) {
      event->accept();
      return true;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  bool consume_{false};
};

patchy::PixelBuffer solid_pixels(std::int32_t width, std::int32_t height, patchy::PixelFormat format,
                                    QColor color);

void fill_pixel_rect(patchy::PixelBuffer& pixels, QRect rect, QColor color);

void ensure_artifact_dir();

double text_points_for_pixels(int pixels, double ppi = 300.0) noexcept;

class SettingsValueRestorer {
public:
  explicit SettingsValueRestorer(QString key)
      : settings_(patchy::ui::app_settings()),
        key_(std::move(key)),
        had_value_(settings_.contains(key_)),
        value_(settings_.value(key_)) {}

  ~SettingsValueRestorer() {
    if (had_value_) {
      settings_.setValue(key_, value_);
    } else {
      settings_.remove(key_);
    }
    settings_.sync();
  }

private:
  QSettings settings_;
  QString key_;
  bool had_value_{false};
  QVariant value_;
};

class GallerySettingsRestorer {
public:
  GallerySettingsRestorer()
      : favorites_(QStringLiteral("filters/gallery/favorites")),
        category_(QStringLiteral("filters/gallery/category")),
        last_filter_(QStringLiteral("filters/gallery/lastFilterId")),
        live_preview_(QStringLiteral("filters/gallery/liveCanvasPreview")),
        size_(QStringLiteral("filters/gallery/size")) {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("filters/gallery/favorites"));
    settings.remove(QStringLiteral("filters/gallery/category"));
    settings.remove(QStringLiteral("filters/gallery/lastFilterId"));
    settings.remove(QStringLiteral("filters/gallery/liveCanvasPreview"));
    settings.remove(QStringLiteral("filters/gallery/size"));
    settings.sync();
  }

private:
  SettingsValueRestorer favorites_;
  SettingsValueRestorer category_;
  SettingsValueRestorer last_filter_;
  SettingsValueRestorer live_preview_;
  SettingsValueRestorer size_;
};

class LanguageRestorer {
public:
  LanguageRestorer()
      : language_(patchy::ui::LocalizationManager::instance()
                      .current_language()) {}

  ~LanguageRestorer() {
    patchy::ui::LocalizationManager::instance().set_language(language_, false);
    QApplication::processEvents();
  }

private:
  QString language_;
};

class EnvironmentVariableRestorer {
public:
  explicit EnvironmentVariableRestorer(const char* name)
      : name_(name),
        had_value_(qEnvironmentVariableIsSet(name)),
        value_(qgetenv(name)) {}

  ~EnvironmentVariableRestorer() {
    if (had_value_) {
      qputenv(name_.constData(), value_);
    } else {
      qunsetenv(name_.constData());
    }
  }

private:
  QByteArray name_;
  bool had_value_{false};
  QByteArray value_;
};

class PaintRegionRecorder final : public QObject {
public:
  explicit PaintRegionRecorder(QObject* parent = nullptr)
      : QObject(parent) {}

  void reset() {
    region_ = QRegion();
  }

  void set_recording(bool recording) noexcept {
    recording_ = recording;
  }

  [[nodiscard]] QRegion region() const {
    return region_;
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (recording_ && event->type() == QEvent::Paint) {
      if (auto* paint_event = static_cast<QPaintEvent*>(event); paint_event != nullptr) {
        region_ += paint_event->region();
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QRegion region_;
  bool recording_{true};
};

void save_widget_artifact(const std::string& name, QWidget& widget);

void send_mouse(QWidget& widget, QEvent::Type type, QPoint position, Qt::MouseButton button, Qt::MouseButtons buttons,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier);

const QPointingDevice& tablet_test_device(QPointingDevice::PointerType pointer_type,
                                          QInputDevice::Capabilities capabilities);

void send_tablet(QWidget& widget, QEvent::Type type, QPoint position, qreal pressure,
                 Qt::MouseButton button = Qt::LeftButton, Qt::MouseButtons buttons = Qt::LeftButton,
                 Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                 QPointingDevice::PointerType pointer_type = QPointingDevice::PointerType::Pen,
                 QInputDevice::Capabilities capabilities =
                     QInputDevice::Capability::Position | QInputDevice::Capability::Pressure |
                         QInputDevice::Capability::XTilt | QInputDevice::Capability::YTilt |
                         QInputDevice::Capability::Rotation | QInputDevice::Capability::TangentialPressure |
                         QInputDevice::Capability::ZPosition,
                 float x_tilt = 0.0F, float y_tilt = 0.0F, qreal rotation = 0.0,
                 float tangential_pressure = 0.0F, float z = 0.0F);

void drag(QWidget& widget, QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
          Qt::MouseButton button = Qt::LeftButton);

void send_double_click(QWidget& widget, QPoint position, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

void send_key(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

void send_key_press(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

void send_key_release(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

void send_wheel(QWidget& widget, QPoint position, int delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

void send_layer_drag_enter(QListWidget& list, QPoint position, const std::vector<patchy::LayerId>& ids);

void send_layer_drag_move(QListWidget& list, QPoint position, const std::vector<patchy::LayerId>& ids);

void send_layer_drag_leave(QListWidget& list);

void send_layer_drop(QListWidget& list, QPoint position, const std::vector<patchy::LayerId>& ids);

void send_layer_button_drop(QWidget& button, const std::vector<patchy::LayerId>& ids);

QAction* require_action(QWidget& root, const char* object_name);

QAction* require_hotkey_action(patchy::ui::MainWindow& window, const QString& id);

patchy::RgbColor filter_rgb(const QColor& color);

bool filter_rgb_equal(patchy::RgbColor lhs, patchy::RgbColor rhs);

bool filter_rect_equal(patchy::Rect lhs, patchy::Rect rhs);

bool smart_object_placements_equal(
    const std::optional<patchy::SmartObjectPlacement>& lhs,
    const std::optional<patchy::SmartObjectPlacement>& rhs);

patchy::FilterInvocation filter_invocation(patchy::FilterRegistry& registry, std::string_view id,
                                           QColor foreground = QColor(Qt::black),
                                           QColor background = QColor(Qt::white));

void set_filter_integer(patchy::FilterInvocation& invocation, std::string key, int value);

int filter_integer(const patchy::FilterInvocation& invocation, std::string_view key);

QAction* find_action_by_text(QWidget& root, const QString& text);

QAction* require_action_by_text(QWidget& root, const QString& text);

QImage image_from_pixels_for_visuals(const patchy::PixelBuffer& pixels);

QImage flattened_on_white(const patchy::PixelBuffer& pixels);

patchy::PixelBuffer make_filter_stroke_source();

patchy::Document make_filter_gallery_document(patchy::LayerId& layer_id, patchy::Rect& bounds,
                                              patchy::PixelBuffer& original_pixels);

bool spatial_filter_spreads_clean_red_alpha(const patchy::PixelBuffer& before, const patchy::PixelBuffer& after,
                                            int offset_x = 0, int offset_y = 0);

QAction* find_menu_action_by_text(QMenu& menu, const QString& text);

struct LayerStyleContextMenuState {
  bool saw_copy{false};
  bool saw_paste{false};
  bool saw_delete{false};
  bool copy_enabled{false};
  bool paste_enabled{false};
  bool delete_enabled{false};
};

LayerStyleContextMenuState layer_style_context_menu_state(QListWidget& layer_list, QListWidgetItem& item);

patchy::ui::CanvasWidget* require_canvas(patchy::ui::MainWindow& window);

void show_window(patchy::ui::MainWindow& window);

void process_events_for(int milliseconds);

bool process_events_until(const std::function<bool()>& condition, int timeout_ms = 3000);

QDialog* find_top_level_dialog(const QString& object_name);

void accept_missing_psd_text_font_warning_if_present();

void accept_compatibility_report_when_present(const std::shared_ptr<bool>& done, int attempts = 2400);

QString write_psd_import_warning_fixture(const QString& file_name);

void verify_open_progress_dialog(const QString& expected_file_name, bool& saw_dialog);

void cleanup_after_visual_test();

bool color_close(QColor actual, QColor expected, int tolerance);

bool images_equal_rgba(const QImage& left, const QImage& right);

std::optional<QRect> image_mismatch_bounds_rgba(const QImage& left, const QImage& right);

QImage render_widget_image(QWidget& widget, const QRegion& region = QRegion());

QImage grab_widget_window_image(QWidget& widget);

int count_pixels_close(const QImage& image, QRect region, QColor expected, int tolerance);

QColor canvas_pixel(patchy::ui::CanvasWidget& canvas, QPoint document_point);

void use_solid_fill_settings(patchy::ui::CanvasWidget* canvas);

int count_blended_document_pixels(patchy::ui::CanvasWidget& canvas, QRect document_rect, QColor foreground,
                                  QColor background, int tolerance);

QColor canvas_pixel_center(patchy::ui::CanvasWidget& canvas, QPoint document_point);

std::optional<QRect> dark_document_bounds(patchy::ui::CanvasWidget& canvas, QRect document_rect);

struct AlphaRowBand {
  int top{0};
  int bottom{0};
};

std::vector<AlphaRowBand> alpha_row_bands(const patchy::PixelBuffer& pixels);

int alpha_row_band_span(const std::vector<AlphaRowBand>& bands);

std::optional<QRect> alpha_pixel_bounds_in_rows(const patchy::PixelBuffer& pixels, int top, int bottom);

patchy::Layer* preview_layer_for_editor(patchy::Document& document, const QTextEdit& editor);

int count_internal_text_preview_layers(const std::vector<patchy::Layer>& layers);

int count_internal_text_preview_layers(const patchy::Document& document);

std::optional<QRectF> editor_document_line_rect_containing(const QTextEdit& editor, const QString& needle);

std::optional<QRect> dark_bounds_in_editor_line_lower_half(patchy::ui::CanvasWidget& canvas,
                                                          const QRectF& document_line_rect);

bool skip_without_arial_for_psd_text_preview();

QAction* require_legacy_plugin_action(QWidget& root, const QString& text);

QListWidgetItem* find_layer_item(QListWidget& list, const QString& text);

QListWidgetItem* require_layer_item(QListWidget& list, const QString& text);

void click_layer_row_thumbnail(QListWidget& layers, const QString& layer_name, const QString& thumbnail_name,
                               Qt::KeyboardModifiers modifiers = Qt::NoModifier);

bool top_level_widget_exists(const QString& object_name);

patchy::LayerBlendIf blend_if_ui_test_settings();

QListWidgetItem* require_gallery_filter_item(QListWidget& looks,
                                             const QString& filter_id);

void accept_new_document_dialog(int width_value, int height_value);

void accept_clipboard_new_document_dialog(QSize clipboard_size);

void accept_integer_dialog(const QString& object_name, int value);

void accept_canvas_size_dialog(int width_value, int height_value);

void accept_image_size_dialog(int width_value, int height_value);

void accept_image_size_resolution_dialog(int resolution_value);

void accept_layer_style_dialog(bool stroke_enabled, bool gradient_enabled, bool shadow_enabled);

void accept_transform_dialog(int x_value, int y_value, int width_value, int height_value);

void accept_hue_saturation_dialog(int hue_value, int saturation_value, int lightness_value);

void accept_levels_dialog(int black_value, int white_value, int gamma_value);

QPoint curves_graph_position(QWidget& graph, int input, int output);

void click_curves_graph(QWidget& graph, int input, int output);

void accept_curves_dialog(int shadow_value, int midtone_value, int highlight_value);

void accept_color_balance_dialog(int cyan_red_value, int magenta_green_value, int yellow_blue_value);

void accept_filter_dialog(std::vector<std::pair<QString, int>> spin_values = {});

QString brush_tip_test_storage_dir();

QString pattern_test_storage_dir();

void clear_pattern_test_state();

void clear_brush_tip_test_state();

patchy::LayerId open_smart_object_fixture(patchy::ui::MainWindow& window);

void convert_fixture_source_to_external(patchy::ui::MainWindow& window, patchy::LayerId layer_id,
                                        const QString& linked_path);

}  // namespace patchy::test::ui
