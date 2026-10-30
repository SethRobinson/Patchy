#pragma once

// Shared UI-test helpers moved verbatim from tests/ui_visual_tests.cpp (used by
// more than one test group). Moved, never copied. Default arguments live on
// these declarations only; tablet_test_device's three static QPointingDevice
// locals are defined exactly once, in ui_test_support.cpp.

#include "core/layer.hpp"
#include "core/smart_object.hpp"
#include "ui/alpha_row_bands.hpp"
#include "ui/app_settings.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_manager.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"

#include <QApplication>
#include <QDialog>
#include <QListWidget>
#include <QByteArray>
#include <QColor>
#include <QEvent>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPaintEvent>
#include <QPointingDevice>
#include <QRect>
#include <QRegion>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class QAction;
class QDialog;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QTextEdit;
class QWidget;
namespace patchy { class Document; class FilterRegistry; struct FilterInvocation; }
namespace patchy::ui { class CanvasWidget; }

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

// Recursive delete for test folders: refuses (throws) a blank path or one outside
// test-artifacts, the QStandardPaths test-mode folder, and patchy* temp folders. Never call
// QDir::removeRecursively directly in tests; see tests/test_scratch_remove.hpp.
bool remove_test_scratch_dir(const QString& path);

// Default ppi matches the MainWindow startup document (72). Tests that construct a
// patchy::Document directly get the core default (300) and must pass it explicitly.
double text_points_for_pixels(int pixels, double ppi = 72.0) noexcept;

// Crispness probe for a rendered glyph raster: {opaque pixels on the densest row, widest run of
// partial-alpha (anti-aliased edge) pixels on that row}. Crisp vector rasterization keeps the
// ramp to ~1-2 px; a bilinear upscale of a small bitmap smears each edge into a gradient about
// as wide as the scale factor.
std::pair<int, int> max_edge_ramp(const QImage& image);

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

// The MainWindow constructor loads tools/* settings, so pin the vector keys
// to their defaults for deterministic runs on developer machines.
class VectorSettingsGuard {
public:
  VectorSettingsGuard() {
    auto settings = patchy::ui::app_settings();
    for (const auto* key :
         {"tools/vectorToolMode", "tools/vectorFillColor", "tools/vectorStrokeColor",
          "tools/vectorStrokeEnabled", "tools/vectorStrokeWidth", "tools/vectorLineWeight",
          "tools/vectorFillKind", "tools/vectorFillPatternId", "tools/vectorFillGradientId",
          "tools/vectorStrokePaintKind", "tools/vectorStrokePatternId",
          "tools/vectorStrokeGradientId", "tools/penAutoAddDelete", "paths/fillPatternScale",
          "paths/fillPatternAngle",
          "paths/fillPatternOffsetX", "paths/fillPatternOffsetY", "paths/fillPatternAlignLayer"}) {
      settings.remove(QString::fromLatin1(key));
    }
    settings.sync();
  }

private:
  SettingsValueRestorer mode_{QStringLiteral("tools/vectorToolMode")};
  SettingsValueRestorer fill_{QStringLiteral("tools/vectorFillColor")};
  SettingsValueRestorer stroke_color_{QStringLiteral("tools/vectorStrokeColor")};
  SettingsValueRestorer stroke_enabled_{QStringLiteral("tools/vectorStrokeEnabled")};
  SettingsValueRestorer stroke_width_{QStringLiteral("tools/vectorStrokeWidth")};
  SettingsValueRestorer line_weight_{QStringLiteral("tools/vectorLineWeight")};
  SettingsValueRestorer fill_kind_{QStringLiteral("tools/vectorFillKind")};
  SettingsValueRestorer fill_pattern_id_{QStringLiteral("tools/vectorFillPatternId")};
  SettingsValueRestorer fill_gradient_id_{QStringLiteral("tools/vectorFillGradientId")};
  SettingsValueRestorer stroke_paint_kind_{QStringLiteral("tools/vectorStrokePaintKind")};
  SettingsValueRestorer stroke_pattern_id_{QStringLiteral("tools/vectorStrokePatternId")};
  SettingsValueRestorer stroke_gradient_id_{QStringLiteral("tools/vectorStrokeGradientId")};
  SettingsValueRestorer corner_radius_{QStringLiteral("tools/shapeCornerRadius")};
  SettingsValueRestorer pen_auto_add_delete_{QStringLiteral("tools/penAutoAddDelete")};
  SettingsValueRestorer work_path_tolerance_{QStringLiteral("paths/makeWorkPathTolerance")};
  SettingsValueRestorer simulate_pressure_{QStringLiteral("paths/strokeSimulatePressure")};
  SettingsValueRestorer fill_contents_{QStringLiteral("paths/fillContents")};
  SettingsValueRestorer fill_pattern_{QStringLiteral("paths/fillPatternId")};
  SettingsValueRestorer fill_opacity_{QStringLiteral("paths/fillOpacity")};
  SettingsValueRestorer fill_pattern_scale_{QStringLiteral("paths/fillPatternScale")};
  SettingsValueRestorer fill_pattern_angle_{QStringLiteral("paths/fillPatternAngle")};
  SettingsValueRestorer fill_pattern_offset_x_{QStringLiteral("paths/fillPatternOffsetX")};
  SettingsValueRestorer fill_pattern_offset_y_{QStringLiteral("paths/fillPatternOffsetY")};
  SettingsValueRestorer fill_pattern_align_{QStringLiteral("paths/fillPatternAlignLayer")};
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

// The suite runs pinned to Dark (see tests/ui/main.cpp). Any test that renders in
// Light must go through this so a failure cannot leave the rest of the corpus in
// the wrong scheme, and so the system override never leaks.
class ColorSchemeRestorer {
public:
  ColorSchemeRestorer()
      : preference_(patchy::ui::ThemeManager::instance().preference()) {}

  explicit ColorSchemeRestorer(patchy::ui::ColorSchemePreference preference) : ColorSchemeRestorer() {
    apply(preference);
  }

  ~ColorSchemeRestorer() {
    patchy::ui::ThemeManager::instance().set_system_color_scheme_for_testing(std::nullopt);
    apply(preference_);
  }

  static void apply(patchy::ui::ColorSchemePreference preference) {
    patchy::ui::ThemeManager::instance().set_preference(preference, /*persist=*/false);
    QApplication::processEvents();
  }

private:
  patchy::ui::ColorSchemePreference preference_;
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

// Every offscreen shot goes through here. The app QSS lives on the MainWindow (and on
// make_themed_dialog_parent()); a dialog opened with no themed ancestor would otherwise be
// grabbed with Qt's default light widgets showing through its list, buttons and labels
// (the #262 Layer Style shots, October 2026). ensure_capture_themed gives such a window
// the active theme's sheet before the grab, in Dark and Light alike.
// Themes a window with no themed ancestor for the lifetime of the scope and puts
// its own sheet back afterwards, so a capture leaves the widget as it found it.
class ThemedCaptureScope {
 public:
  explicit ThemedCaptureScope(QWidget& widget);
  ~ThemedCaptureScope();
  ThemedCaptureScope(const ThemedCaptureScope&) = delete;
  ThemedCaptureScope& operator=(const ThemedCaptureScope&) = delete;
  // Keeps the sheet applied after the scope ends.
  void release() { window_ = nullptr; }

 private:
  QWidget* window_{nullptr};
  QString previous_sheet_;
};
void ensure_capture_themed(QWidget& widget);
// The capture guard: empty when every visible widget under `root` carries the active
// theme, otherwise a description of the first offender (class, objectName, role and
// color). A widget fails when it is unpolished, when no window in its owner chain
// carries the app sheet, or when the background or text color the stylesheet engine
// left in its palette is Qt's default or the other scheme's value and matches no role
// of the active palette. Widgets that show document content or a user color
// (objectName inlineTextEditor, *ColorButton, *ColorSwatch) and their children are
// exempt. Every capture helper runs it before saving and records the failure through
// patchy::test::report_failure instead of writing a misleading picture.
QString capture_theme_mismatch(QWidget& root);
QImage capture_widget_artifact(const std::string& name, QWidget& widget);
void save_widget_artifact(const std::string& name, QWidget& widget);
// Shows the tooltip Qt shows for `anchor` and renders the tip panel alone onto a
// transparent sheet (no window background, as the translucent Windows 11 tip
// shows on screen). A null image means no tip appeared.
QImage render_widget_tooltip(QWidget& anchor);
// The share (0 to 1) of `tip`'s pixels left fully transparent. A tip whose
// stylesheet cascade cleared its ToolTipBase reads near 1 (issue 347).
double unpainted_share(const QImage& tip);
// Saves several captures stacked top to bottom (a 6 px gap between them) as one artifact,
// for before/after shots that cover more than one widget or state.
void save_stacked_artifact(const std::string& name, const QList<QImage>& strips);
// A window grab with one of its popups (an options-bar slider popup is its own top-level
// window, so a plain grab would not show it) painted in place, cropped around `field` and
// the popup. For issue shots of a popup open at a value.
void save_window_popup_artifact(const std::string& name, QWidget& window, QWidget& field, QWidget& popup);

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

// Clicks at a point the way the window system does: the press goes to the deepest child under
// the point, and focus is handed out BEFORE the press to the first ancestor that accepts click
// focus. Use this instead of send_mouse whenever the question is "would a real click reach the
// right widget" - sending straight to a parent answers a different question and hides bugs where
// an overlay swallows the click or redirects focus. See the implementation for the details.
void click_widget_like_a_user(QWidget& widget, QPoint position,
                              Qt::KeyboardModifiers modifiers = Qt::NoModifier);

void drag(QWidget& widget, QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
          Qt::MouseButton button = Qt::LeftButton);

// Press at document_path.front(), move through steps_per_segment interpolated
// positions per segment, release at document_path.back(). Document coordinates,
// mapped through the canvas view (the multi-point stroke shape the stress
// harness proved out; canvas must be sized so every point maps inside it).
void drag_document_path(patchy::ui::CanvasWidget& canvas, const std::vector<QPoint>& document_path,
                        int steps_per_segment, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

// The widget point at a document pixel's center. A brush stroke point is a pixel index (the dab
// centers on that pixel), so a pointer here passes exactly the integer point a script names.
QPoint widget_center_of_document_pixel(const patchy::ui::CanvasWidget& canvas, QPoint document_point);

// drag_document_path through pixel centers: native strokes that must match a script's points.
void drag_document_pixel_centers(patchy::ui::CanvasWidget& canvas, const std::vector<QPoint>& document_path,
                                 int steps_per_segment, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

void save_image_artifact(const std::string& name, const QImage& image);

void send_double_click(QWidget& widget, QPoint position, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

void send_key(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

void send_key_press(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

void send_key_release(QWidget& widget, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

void send_wheel(QWidget& widget, QPoint position, int delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

// Wheel event carrying a pixelDelta alongside the angleDelta, as the wasm
// platform plugin and desktop trackpads deliver.
void send_pixel_wheel(QWidget& widget, QPoint position, int pixel_delta,
                      Qt::KeyboardModifiers modifiers = Qt::NoModifier);

// One event of a continuous two-finger scroll (a trackpad): a 2D pixelDelta with a
// scroll phase and an angleDelta of twice the pixels, as Qt's cocoa plugin reports it.
void send_scroll(QWidget& widget, QPoint position, QPoint pixel_delta, Qt::ScrollPhase phase = Qt::ScrollUpdate,
                 Qt::KeyboardModifiers modifiers = Qt::NoModifier);

void send_layer_drag_enter(QListWidget& list, QPoint position, const std::vector<patchy::LayerId>& ids);

void send_layer_drag_move(QListWidget& list, QPoint position, const std::vector<patchy::LayerId>& ids);

void send_layer_drag_leave(QListWidget& list);

void send_layer_drop(QListWidget& list, QPoint position, const std::vector<patchy::LayerId>& ids);

void send_layer_button_drop(QWidget& button, const std::vector<patchy::LayerId>& ids);

// A Layers-panel drag dropped on `target` (another document's canvas or the tab
// bar): both mime formats, Enter, Move, Drop, then two event pumps so the
// deferred copy runs. `entered` reports whether the DragEnter was accepted; the
// Drop is sent regardless so a refusing target proves it ignores it too.
void send_layer_drop_to_widget(QWidget& target, QPoint position, const std::vector<patchy::LayerId>& ids,
                               std::optional<std::int64_t> source_session_id,
                               Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool* entered = nullptr);

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

// The menu a right-click on a Layers row part opened: its objectName and, per entry, the
// action's objectName ("-" for a separator), text, and enabled and checked states.
struct LayerRowPartMenu {
  QString name;
  QStringList actions;
  QStringList texts;
  QList<bool> enabled;
  QList<bool> checked;
};
// Right-clicks `widget_name` in `row_name`'s row the way a user does (right press and release on
// it, then its context-menu event) and picks `pick` (an action objectName; empty closes the
// menu). `artifact` saves the window around the part with the open menu.
LayerRowPartMenu right_click_layer_row_widget(QListWidget& layers, const QString& row_name,
                                              const QString& widget_name, const QString& pick,
                                              const std::string& artifact = {});

// Paths an Open Recent menu lists, in order: the rows of its scrolling list
// (fileOpenRecentList / fileOpenRecentFolderList), then data-carrying actions, submenus
// included (the plain paged menu under a native menu bar).
QStringList recent_menu_paths(QMenu& menu);
// The Open Recent menu list's row for `path`, or nullptr.
QListWidgetItem* recent_menu_list_item(QMenu& menu, const QString& path);

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

// Object names (or texts, for unnamed labels) of every visible QLabel under `root`
// whose text does not fit the space the layout gave it: a word-wrapped label needing
// more height than it has, or a single-line label narrower than its own text. A
// non-empty result means the window is too small for its content, which is what
// clipped hint text looks like to a user. Dialogs sized by a hardcoded resize() are
// the usual cause; fonts differ per platform, so a size that fits on Windows can clip
// on macOS or in the browser build.
[[nodiscard]] QStringList clipped_labels(QWidget& root);

void show_window(patchy::ui::MainWindow& window);

// show_window without the compatibility document: the true empty-startup state
// (start panel visible, no sessions).
void show_window_empty(patchy::ui::MainWindow& window);

// A MainWindow shown through show_window (so it holds the historical 1024x768 startup
// document) whose canvas has already passed require_canvas. One line names both and
// keeps the fixture alive to the end of the scope, exactly like the window local it
// replaces:
//
//   auto&& [window, canvas] = ShownWindowFixture{};
//
// A test that never touches the canvas still names it: no supported compiler warns
// about one unused name of a structured binding while another is used. Tests that
// open their own document before showing, or need show_window_empty, keep the
// explicit MainWindow local.
struct ShownWindowFixture {
  patchy::ui::MainWindow window;
  patchy::ui::CanvasWidget* canvas{nullptr};

  ShownWindowFixture();
  ShownWindowFixture(const ShownWindowFixture&) = delete;
  ShownWindowFixture& operator=(const ShownWindowFixture&) = delete;
};

void process_events_for(int milliseconds);

bool process_events_until(const std::function<bool()>& condition, int timeout_ms = 3000);

QDialog* find_top_level_dialog(const QString& object_name);

// Runs `drive` from inside the event loop of the top-level dialog `object_name` once it is
// visible, polling every 10 ms for up to `timeout_ms`. Arm it before the action that opens
// the dialog. Use it instead of a fixed-delay QTimer::singleShot driver: openers that defer
// through the event loop (a button whose click posts a 0 ms timer) make a fixed delay race
// the dialog's construction, and a slow host loses (docs/testing.md, "Driving dialogs").
// A throw inside `drive` is recorded as a failure and rejects the dialog instead of
// unwinding through Qt's event dispatch; a dialog that never shows is recorded too, and a
// different modal dialog open in its place is rejected so the test cannot park in it.
void when_dialog_visible(const QString& object_name, std::function<void(QDialog&)> drive, int timeout_ms = 10000);

// Prints what decides whether a shape drag makes a shape layer (the current canvas, its
// tool, vector mode, style, edit target, quick mask, zoom, the layers and the active one).
// Call when a drag that should have made one did not: two tests saw that once on the ARM64
// host and never again, so the next occurrence must explain itself.
void print_shape_drag_state(patchy::ui::MainWindow& window, patchy::ui::CanvasWidget& canvas, const char* label);

// The Preferences dialog lists its pages down the left (GitHub issue 451): the list
// (preferencesPageList) and the stacked pages beside it (preferencesPageStack).
QListWidget* preferences_page_list(QWidget& dialog);
QWidget* preferences_current_page(QWidget& dialog);
// A page's row by its title as listed ("Units & Grids"); -1 when missing. Pages get added
// over time, so tests that want one page find it by name.
int preferences_page_row(QWidget& dialog, const QString& title);
// Selects that page as a click would and processes events until the page is built;
// false when it is missing.
bool select_preferences_page(QWidget& dialog, const QString& title);
// Processes events until the page Preferences opened on is built: a Hotkeys or Colors and
// Themes start page fills in after the dialog shows, under a preferencesLoadingNote.
void wait_for_preferences_start_page(QWidget& dialog);
// The Colors and Themes page's theme list, after selecting the page (its first visit
// reads the themes). Rows carry their token in Qt::UserRole: "system", "dark", "light"
// or "custom:" + theme id; section headers have none.
QListWidget* preferences_theme_list(QWidget& dialog);
// The row holding `token`, or -1.
int preferences_theme_row(QListWidget& list, const QString& token);
// Selects the row holding `token` as a click would (a live preview) and processes
// events; false when it is missing.
bool select_preferences_theme(QWidget& dialog, const QString& token);
// The token of the selected theme row.
QString preferences_selected_theme(QWidget& dialog);

// A hidden stand-in parent for a dialog a test opens without a MainWindow. The app's QSS
// (photoshop_style()) is set on the MainWindow, not on qApp, and reaches a dialog only
// through its parent: with a null parent, combo boxes, spin boxes and buttons render in
// Qt's light defaults. Parent any dialog whose artifact is meant as a screenshot to this.
std::unique_ptr<QWidget> make_themed_dialog_parent();
// Once the save dialog `object_name` is up, chooses `path` in it and accepts, then runs
// `next` for the dialog that follows (PDF Options after its file dialog). Call before
// triggering the command.
void accept_save_file_dialog_then(const QString& object_name, const QString& path, std::function<void()> next);

void accept_missing_psd_text_font_warning_if_present();

void accept_compatibility_report_when_present(const std::shared_ptr<bool>& done, int attempts = 2400);

QString write_psd_import_warning_fixture(const QString& file_name);

void verify_open_progress_dialog(const QString& expected_file_name, bool& saw_dialog);

void cleanup_after_visual_test();

// Child-process body of ui_bundled_web_fonts_register_and_create_engines: registers the
// whole third_party/fonts-web inventory and validates that every family produces a working
// engine, printing "family: <name>" lines to stdout. Runs only in a child spawned with
// --bundled-web-fonts-probe (see tests/ui/main.cpp): application fonts are never removed at
// runtime, and permanently registering the inventory in the suite process changes Qt's
// missing-family fallback for every later test. Returns a process exit code.
int run_bundled_web_fonts_probe();

bool color_close(QColor actual, QColor expected, int tolerance);

bool images_equal_rgba(const QImage& left, const QImage& right);

std::optional<QRect> image_mismatch_bounds_rgba(const QImage& left, const QImage& right);

QImage render_widget_image(QWidget& widget, const QRegion& region = QRegion());

// The part of a canvas that actually shows canvas pixels: its rect minus the scroll bars it
// overlays on its own right and bottom edges (they are child widgets, not a
// QAbstractScrollArea viewport, and stay visible whenever a document is open). A document
// point can map underneath one, where render_widget_image reads scroll-bar chrome, so any
// test sampling a rendered pixel near an edge should confirm the point lands in here first.
// Scroll-bar thickness is style-dependent, which makes an edge-adjacent sample point
// silently platform-dependent otherwise.
QRect canvas_content_rect(const QWidget& canvas);

QImage grab_widget_window_image(QWidget& widget);

int count_pixels_close(const QImage& image, QRect region, QColor expected, int tolerance);

QColor canvas_pixel(patchy::ui::CanvasWidget& canvas, QPoint document_point);

void use_solid_fill_settings(patchy::ui::CanvasWidget* canvas);

int count_blended_document_pixels(patchy::ui::CanvasWidget& canvas, QRect document_rect, QColor foreground,
                                  QColor background, int tolerance);

QColor canvas_pixel_center(patchy::ui::CanvasWidget& canvas, QPoint document_point);

std::optional<QRect> dark_document_bounds(patchy::ui::CanvasWidget& canvas, QRect document_rect);

// The production row-band measure (ui/alpha_row_bands.hpp): the tests measure
// the same bands MainWindow scores text re-renders with, never a copy.
using patchy::ui::AlphaRowBand;
using patchy::ui::alpha_row_bands;
using patchy::ui::alpha_row_band_span;

std::optional<QRect> alpha_pixel_bounds_in_rows(const patchy::PixelBuffer& pixels, int top, int bottom);

// True when no pixel on the buffer's outer rows or columns has alpha above `threshold`: a text
// raster that kept its bleed, so no glyph ink was cut off at the buffer edge.
bool pixel_buffer_border_is_clear(const patchy::PixelBuffer& pixels, int threshold = 0);

// Every TySh transform (xx xy yx yy tx ty) in a PSD, in layer order (bottom layer first).
std::vector<std::array<double, 6>> tysh_transforms_in_psd(const std::vector<std::uint8_t>& bytes);

patchy::Layer* preview_layer_for_editor(patchy::Document& document, const QTextEdit& editor);

int count_internal_text_preview_layers(const std::vector<patchy::Layer>& layers);

int count_internal_text_preview_layers(const patchy::Document& document);

std::optional<QRectF> editor_document_line_rect_containing(const QTextEdit& editor, const QString& needle);

std::optional<QRect> dark_bounds_in_editor_line_lower_half(patchy::ui::CanvasWidget& canvas,
                                                          const QRectF& document_line_rect);

// The skip_without_* helpers end the test as [SKIP] (patchy::test::skip) when the machine
// lacks the face; the *_reason helpers return why instead, for a test that checks other
// things without it.
void skip_without_arial_for_psd_text_preview();

std::optional<std::string> missing_font_face_reason(const QString& family, const char* fixture_role);
void skip_without_font_face(const QString& family, const char* fixture_role);

std::optional<std::string> missing_psd_text_face_reason(const patchy::Layer& layer, const QString& expected_family);
void skip_without_psd_text_face(const patchy::Layer& layer, const QString& expected_family);

QAction* require_legacy_plugin_action(QWidget& root, const QString& text);
// Blocks (pumping events) until the window's startup plug-in scan has applied
// its result; the scan runs on a worker and the menu fills in afterwards.
void wait_for_legacy_plugin_scan(patchy::ui::MainWindow& window);
// The Filter Foundry test plug-ins are fetched into local-test-fixtures (scripts/
// fetch-test-plugins), never committed. Skips the test when they are missing; otherwise
// stages copies in test-fixtures/photoshop-plugins next to the test binary, the developer
// scan root MainWindow::legacy_plugin_scan_roots adds, so a fresh window's startup scan
// offers them. Returns the staged folder. Call it before creating the window.
QString require_filter_foundry_fixtures();

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
// Arms the Edit > Stroke Selection dialog: sets the width, the location token ("inside",
// "center", "outside"), optionally the color, and accepts.
void accept_stroke_selection_dialog(int width, const QString& location,
                                    std::optional<QColor> color = std::nullopt);

void accept_canvas_size_dialog(int width_value, int height_value);
// Arms the Image > Rotate Arbitrary dialog: sets the angle, picks the direction, accepts.
void accept_rotate_canvas_dialog(double degrees, bool clockwise);

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
