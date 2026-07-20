#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/text_warp.hpp"
#include "core/warp_mesh.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_tree.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/pixel_tools.hpp"
#include "formats/palette_io.hpp"
#include "filters/builtin_filters.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/heif_document_io.hpp"
#include "formats/raw_document_io.hpp"
#include "plugins/legacy_photoshop_adapter.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_smart_objects.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "render/compositor.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_presets.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/default_brush_tips.hpp"
#include "ui/compatibility_report.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/raw_develop_dialog.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "ui/gradient_library.hpp"
#include "ui/gradient_manager_dialog.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/font_picker.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/measurement_units.hpp"
#include "ui/palette_convert_dialog.hpp"
#include "ui/palette_panel.hpp"
#include "ui/paths_panel.hpp"
#include "ui/pattern_library.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/style_library.hpp"
#include "ui/print_dialog.hpp"
#include "ui/smart_object_render.hpp"
#include "ui/scanner_import.hpp"
#include "ui/image_sequence_dialog.hpp"
#include "ui/sprite_sheet_dialog.hpp"
#include "ui/start_panel.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/splash_dialog.hpp"
#include "ui/update_checker.hpp"
#include "ui/zoom_status_bar.hpp"
#include "support/string_utils.hpp"

#include <QAbstractItemView>
#include <QAbstractItemModel>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QBuffer>
#include <QButtonGroup>
#include <QByteArray>
#include <QDateTime>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QColorSpace>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLayout>
#include <QResizeEvent>
#include <QIcon>
#include <QImageReader>
#include <QInputDialog>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QKeySequence>
#include <QListWidget>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPolygon>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QRegion>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QScopeGuard>
#include <QSettings>
#include <QShowEvent>
#include <QStandardPaths>
#include <QStandardItem>
#include <QStyledItemDelegate>
#include <QMutex>
#include <QRawFont>
#include <QTextCharFormat>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextOption>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTransform>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <tchar.h>
#include <tpcshrd.h>
#endif

#ifndef PATCHY_VERSION
#define PATCHY_VERSION "0.0.0"
#endif

// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

constexpr int kLayerOpacityApplyDelayMs = 33;
constexpr int kLayerOpacityIdleFinishDelayMs = 250;
// Tool-option sliders apply to the canvas live but persist to disk only this
// long after the last change, so dragging a slider does not write the whole
// settings file on every intermediate value.
constexpr int kToolSettingsSaveDelayMs = 250;

struct TextToolSettings {
  QString text;
  QString html;
  QString family;
  int size{36};
  bool bold{false};
  bool italic{false};
  int anti_alias{3};
  bool boxed{false};
  int box_width{0};
  int box_height{0};
  // Photoshop leading-model layout (kLayerMetadataTextLayoutMode == "photoshop"): baselines
  // advance by each entered line's max leading instead of Qt's natural spacing.
  bool photoshop_layout{false};
};

constexpr auto kTextEditorPreviewEnabledProperty = "patchy.textPreviewEnabled";
constexpr auto kTextEditorPreviewExpensiveProperty = "patchy.expensiveTextStylePreview";
constexpr auto kTextEditorPreviewGenerationProperty = "patchy.textPreviewGeneration";
constexpr auto kTextEditorPreviewPaintProperty = "patchy.previewPaintsText";
constexpr auto kTextEditorPreviewCaretProperty = "patchy.previewCaretRect";
constexpr auto kTextEditorPreviewSelectionProperty = "patchy.previewSelectionRects";
constexpr auto kTextEditorTransformedOverlayProperty = "patchy.transformedPreviewOverlayActive";
constexpr auto kTextEditorChangedProperty = "patchy.textEditorChanged";
constexpr auto kTextEditorSourceRasterPreviewProperty = "patchy.sourceRasterPreview";
constexpr auto kTextEditorForceBakedPreviewProperty = "patchy.forceBakedPreview";
constexpr auto kTextEditorTransformOverrideProperty = "patchy.textTransformOverride";
constexpr auto kTextEditorSourceVisibleAnchorProperty = "patchy.sourceVisibleAnchor";
constexpr auto kTextEditorSourceVisibleSizeProperty = "patchy.sourceVisibleSize";
constexpr auto kTextEditorVisibleLocalRectProperty = "patchy.textVisibleLocalRect";
constexpr auto kTextEditorRenderLocalRectProperty = "patchy.textRenderLocalRect";
constexpr auto kTextEditorExtendedBoxPreviewProperty = "patchy.extendedBoxPreview";
constexpr auto kTextEditorLineAwareBoxPreviewProperty = "patchy.lineAwareBoxPreview";
constexpr auto kTextEditorMetricScaleProperty = "patchy.textMetricScale";
constexpr auto kTextEditorProvisionalLayerProperty = "patchy.provisionalTextLayerId";
constexpr auto kLayerMetadataTextLineAwareBoxPreview = "patchy.text.line_aware_box_preview";
// Marks the empty text layer inserted the moment the Type tool starts a NEW edit (Photoshop
// shows the layer immediately); commit/cancel remove it again, and the marker is the identity
// check so a stale id can never delete an unrelated layer (layer ids restart per document).
constexpr auto kLayerMetadataProvisionalTextMarker = "patchy.internal.provisional_text";
constexpr auto kTransformedTextEditOverlayObjectName = "transformedTextEditOverlay";
constexpr auto kTextFlowPoint = "point";
constexpr auto kTextFlowBox = "box";
constexpr int kTextDisplayFamilyFormatProperty = QTextFormat::UserProperty + 31;
constexpr int kTextLeadingFormatProperty = QTextFormat::UserProperty + 32;
// Photoshop-layout char properties (runs v3): auto-leading flag (leading = paragraph fraction x
// size), tracking in Photoshop's 1/1000-em units, and the unrounded font size in the document's
// current scale (QFont pixel sizes are ints; layout math needs the fractional value).
constexpr int kTextAutoLeadingFormatProperty = QTextFormat::UserProperty + 33;
constexpr int kTextTrackingFormatProperty = QTextFormat::UserProperty + 34;
constexpr int kTextExactSizeFormatProperty = QTextFormat::UserProperty + 35;
// Block property: paragraph auto-leading fraction (Photoshop default 1.2).
constexpr int kTextBlockAutoLeadFractionProperty = QTextFormat::UserProperty + 36;
// Character-panel glyph scales (runs v3): width x horizontal, height x vertical. The glyph
// pixel size folds the vertical scale in; leading math stays FontSize-based, so the exact-size
// property intentionally excludes it.
constexpr int kTextHorizontalScaleFormatProperty = QTextFormat::UserProperty + 37;
constexpr int kTextVerticalScaleFormatProperty = QTextFormat::UserProperty + 38;
constexpr int kTextEditorCaretWidth = 3;
constexpr int kMinimumTextBoxDocumentSize = 16;
constexpr int kTextEditorPreviewDelayMs = 33;
constexpr int kExpensiveTextEditorPreviewDelayMs = 200;
constexpr int kTextEditorCaretBlinkSpeedMultiplier = 3;
constexpr int kMinimumTextEditorCaretBlinkPhaseMs = 80;

QString text_flow_metadata_value(bool boxed) {
  return boxed ? QString::fromLatin1(kTextFlowBox) : QString::fromLatin1(kTextFlowPoint);
}

bool text_flow_is_box(const QString& value) {
  return value.compare(QString::fromLatin1(kTextFlowBox), Qt::CaseInsensitive) == 0;
}

void update_text_editor_preview_caret(QTextEdit& editor, double zoom);
int text_editor_caret_width(const QTextEdit& editor) noexcept;
int text_editor_caret_blink_phase_ms();
QRect text_editor_viewport_caret_rect(const QTextEdit& editor);
std::vector<QRect> text_editor_viewport_selection_rects(const QTextEdit& editor, int start, int end);
double text_editor_metric_scale(const QTextEdit& editor);

// The document a text layer is rasterized from, plus the font/width the layout was built against.
// Built by build_text_render_document -- the single construction shared by the rasterizer and the
// caret/selection layout so glyphs and caret geometry can never diverge.
struct TextRenderDocument {
  std::unique_ptr<QTextDocument> document;
  QFont font;
  int text_width{0};
};
TextRenderDocument build_text_render_document(const TextToolSettings& settings, QColor color,
                                              std::int32_t max_width, const QString& paragraph_runs,
                                              const QString& rich_text_runs, double metric_scale,
                                              double layout_scale = 1.0, double box_scale = 1.0);
QString rich_text_runs_from_document(const QTextDocument& document, const TextToolSettings& fallback,
                                     QColor fallback_color);
QString paragraph_runs_from_document(const QTextDocument& document);
QTextDocument* text_editor_document_space_layout(const QTextEdit& editor, double& zoom_out);
void configure_text_font_smoothing(QFont& font, int anti_alias);
std::optional<QRectF> valid_text_local_rect(QRectF rect);

QString preferred_latin_text_fallback_family() {
  const auto available = QFontDatabase::families();
  for (const auto& candidate : {QStringLiteral("Adobe Arabic"), QStringLiteral("Adobe Clean Serif"),
                                QStringLiteral("Minion Pro"), QStringLiteral("Times New Roman"),
                                QStringLiteral("Georgia"), QStringLiteral("Noto Serif"), QStringLiteral("Arial")}) {
    if (available.contains(candidate, Qt::CaseInsensitive)) {
      return candidate;
    }
  }
  return QApplication::font().family();
}

bool text_family_uses_photoshop_latin_fallback(const QString& family) {
  return family.simplified().compare(QStringLiteral("Noto Naskh Arabic"), Qt::CaseInsensitive) == 0;
}

QString compact_text_family_key(const QString& value) {
  QString compact;
  compact.reserve(value.size());
  for (const auto ch : value.toCaseFolded()) {
    if (ch.isLetterOrNumber()) {
      compact.append(ch);
    }
  }
  return compact;
}

std::optional<QString> available_text_family_match(const QString& family) {
  const auto requested = family.trimmed();
  if (requested.isEmpty()) {
    return std::nullopt;
  }

  const auto available = QFontDatabase::families();
  for (const auto& candidate : available) {
    if (candidate.compare(requested, Qt::CaseInsensitive) == 0) {
      return candidate;
    }
  }

  const auto requested_key = compact_text_family_key(requested);
  if (requested_key.isEmpty()) {
    return std::nullopt;
  }
  for (const auto& candidate : available) {
    if (compact_text_family_key(candidate) == requested_key) {
      return candidate;
    }
  }
  return std::nullopt;
}

QString canonical_text_display_family(const QString& family) {
  const auto requested = family.trimmed();
  if (requested.isEmpty()) {
    return QApplication::font().family();
  }
  return available_text_family_match(requested).value_or(requested);
}

struct AvailableTextFamilyStyle {
  QString family;
  QString style;
};

// A GDI-style name such as "Arial Black" may exist on some platforms only as family "Arial" with
// style "Black" -- the OpenType typographic family/subfamily split the FreeType database uses
// (the Windows database exposes the legacy family directly, so this is a fallback).  Find the
// longest available family that prefixes the requested name and whose remaining words name one
// of that family's styles.
std::optional<AvailableTextFamilyStyle> available_text_family_style_match(const QString& family) {
  const auto requested = family.trimmed();
  if (requested.isEmpty()) {
    return std::nullopt;
  }
  std::optional<AvailableTextFamilyStyle> best;
  for (const auto& candidate : QFontDatabase::families()) {
    if (candidate.isEmpty() || requested.size() <= candidate.size() ||
        !requested.startsWith(candidate, Qt::CaseInsensitive)) {
      continue;
    }
    const auto separator = requested.at(candidate.size());
    if (separator != QLatin1Char(' ') && separator != QLatin1Char('-')) {
      continue;
    }
    if (best.has_value() && best->family.size() >= candidate.size()) {
      continue;
    }
    const auto style = requested.mid(candidate.size() + 1).trimmed();
    if (style.isEmpty()) {
      continue;
    }
    for (const auto& available_style : QFontDatabase::styles(candidate)) {
      if (available_style.compare(style, Qt::CaseInsensitive) == 0) {
        best = AvailableTextFamilyStyle{candidate, available_style};
        break;
      }
    }
  }
  return best;
}

void append_missing_text_family(QStringList& missing, const QString& family) {
  const auto requested = family.trimmed();
  if (requested.isEmpty() || requested.compare(QStringLiteral("PSD Text"), Qt::CaseInsensitive) == 0) {
    return;
  }
  if (available_text_family_match(requested).has_value() ||
      available_text_family_style_match(requested).has_value()) {
    return;
  }

  const auto requested_key = compact_text_family_key(requested);
  const bool already_listed = std::any_of(missing.begin(), missing.end(), [&requested, &requested_key](const QString& item) {
    return item.compare(requested, Qt::CaseInsensitive) == 0 ||
           (!requested_key.isEmpty() && compact_text_family_key(item) == requested_key);
  });
  if (!already_listed) {
    missing.push_back(requested);
  }
}

QStringList missing_text_families_for_psd_raster_preview(const QString& primary_family, const QString& runs_text) {
  QStringList missing;
  append_missing_text_family(missing, primary_family);

  const auto lines = runs_text.split(QLatin1Char('\n'));
  for (const auto& raw_line : lines) {
    const auto line = raw_line.trimmed();
    if (line.isEmpty() || line == QStringLiteral("v1")) {
      continue;
    }
    const auto fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 7) {
      continue;
    }
    append_missing_text_family(missing, QString::fromUtf8(QByteArray::fromPercentEncoding(fields[6].toLatin1())));
  }
  return missing;
}

bool confirm_psd_raster_preview_font_substitution(QWidget* parent, const QStringList& missing_fonts) {
  if (missing_fonts.isEmpty()) {
    return true;
  }

  QMessageBox dialog(QMessageBox::Warning, QObject::tr("Missing Font"), QString(), QMessageBox::NoButton, parent);
  dialog.setObjectName(QStringLiteral("missingPsdTextFontMessageBox"));
  if (missing_fonts.size() == 1) {
    dialog.setText(QObject::tr("Patchy can't locate the font \"%1\". Editing this PSD raster preview will substitute "
                               "another font. Continue?")
                       .arg(missing_fonts.front()));
  } else {
    dialog.setText(QObject::tr("Patchy can't locate these fonts: %1. Editing this PSD raster preview will substitute "
                               "other fonts. Continue?")
                       .arg(missing_fonts.join(QStringLiteral(", "))));
  }
  auto* continue_button = dialog.addButton(QObject::tr("Continue"), QMessageBox::AcceptRole);
  dialog.addButton(QMessageBox::Cancel);
  dialog.setDefaultButton(continue_button);
  exec_dialog(dialog);
  return dialog.clickedButton() == continue_button;
}

QString display_text_family_from_font(const QFont& font) {
  const auto families = font.families();
  if (families.size() >= 2 && text_family_uses_photoshop_latin_fallback(families.at(1))) {
    return families.at(1);
  }
  const auto family = font.family().trimmed();
  return family.isEmpty() ? QApplication::font().family() : family;
}

QStringList render_text_families_for_display_family(const QString& family) {
  const auto display_family = canonical_text_display_family(family);
  if (text_family_uses_photoshop_latin_fallback(display_family)) {
    return QStringList{preferred_latin_text_fallback_family(), display_family};
  }
  return QStringList{display_family};
}

QFont render_text_font_for_display_family(const QString& family, int pixel_size, bool bold, bool italic,
                                          int anti_alias) {
  QFont font;
  font.setFamilies(render_text_families_for_display_family(family));
  font.setPixelSize(std::max(1, pixel_size));
  font.setBold(bold);
  font.setItalic(italic);
  // When the requested name is not a family of its own, resolve it as family + style (e.g.
  // "Arial Black" -> "Arial"/"Black") so the proper face renders instead of a regular-weight
  // substitute.  The style name is set last: it takes precedence over the bold flag in matching.
  if (!available_text_family_match(family).has_value()) {
    if (const auto match = available_text_family_style_match(family); match.has_value()) {
      font.setFamilies(QStringList{match->family});
      font.setStyleName(match->style);
    }
  }
  configure_text_font_smoothing(font, anti_alias);
  return font;
}

void set_text_display_family(QTextCharFormat& format, const QString& family) {
  format.setProperty(kTextDisplayFamilyFormatProperty, canonical_text_display_family(family));
}

QString text_display_family_from_format(const QTextCharFormat& format, const QString& fallback) {
  const auto stored = format.property(kTextDisplayFamilyFormatProperty).toString().trimmed();
  if (!stored.isEmpty()) {
    return stored;
  }
  const auto families = format.font().families();
  if (families.size() >= 2 && text_family_uses_photoshop_latin_fallback(families.at(1))) {
    return families.at(1);
  }
  const auto family = format.font().family().trimmed();
  if (!family.isEmpty()) {
    return family;
  }
  return fallback.trimmed().isEmpty() ? QApplication::font().family() : fallback.trimmed();
}

bool affine_transform_has_non_translation_linear_part(const LayerAffineTransform& transform) {
  constexpr double kEpsilon = 0.000001;
  return std::abs(transform[0] - 1.0) > kEpsilon || std::abs(transform[1]) > kEpsilon ||
         std::abs(transform[2]) > kEpsilon || std::abs(transform[3] - 1.0) > kEpsilon;
}

bool qtransform_has_non_translation_linear_part(const QTransform& transform) {
  constexpr double kEpsilon = 0.000001;
  return std::abs(transform.m11() - 1.0) > kEpsilon || std::abs(transform.m12()) > kEpsilon ||
         std::abs(transform.m21()) > kEpsilon || std::abs(transform.m22() - 1.0) > kEpsilon ||
         std::abs(transform.m13()) > kEpsilon || std::abs(transform.m23()) > kEpsilon;
}

bool layer_has_non_translation_text_transform(const Layer& layer) {
  if (const auto found = layer.metadata().find(kLayerMetadataTextTransform); found != layer.metadata().end()) {
    if (const auto transform = parse_layer_affine_transform(found->second); transform.has_value()) {
      return affine_transform_has_non_translation_linear_part(*transform);
    }
  }
  if (const auto found = layer.metadata().find(kLayerMetadataPsdTextTransform); found != layer.metadata().end()) {
    if (const auto transform = parse_layer_affine_transform(found->second); transform.has_value()) {
      return affine_transform_has_non_translation_linear_part(*transform);
    }
  }
  return false;
}

bool layer_patchy_text_transform_overrides_psd_source(const Layer& layer) {
  const auto patchy_transform = layer.metadata().find(kLayerMetadataTextTransform);
  if (patchy_transform == layer.metadata().end()) {
    return false;
  }
  const auto parsed_patchy = parse_layer_affine_transform(patchy_transform->second);
  if (!parsed_patchy.has_value()) {
    return false;
  }
  const auto psd_transform = layer.metadata().find(kLayerMetadataPsdTextTransform);
  if (psd_transform == layer.metadata().end()) {
    return true;
  }
  const auto parsed_psd = parse_layer_affine_transform(psd_transform->second);
  if (!parsed_psd.has_value()) {
    return true;
  }
  constexpr double kEpsilon = 0.000001;
  for (std::size_t index = 0; index < parsed_patchy->size(); ++index) {
    if (std::abs((*parsed_patchy)[index] - (*parsed_psd)[index]) > kEpsilon) {
      return true;
    }
  }
  return false;
}

bool layer_should_edit_with_psd_text_frame(const Layer& layer, bool boxed_text) {
  return boxed_text && !layer_patchy_text_transform_overrides_psd_source(layer) &&
         layer.metadata().contains(kLayerMetadataPsdTextTransform) &&
         layer.metadata().contains(kLayerMetadataPsdTextBoxBounds);
}

bool layer_requires_text_editor_preview(const Layer& layer) {
  return std::abs(layer.opacity() - 1.0F) > 0.001F || std::abs(layer.fill_opacity() - 1.0F) > 0.001F ||
         layer.blend_mode() != BlendMode::Normal ||
         (layer.layer_style().effects_visible && !layer.layer_style().empty()) ||
         layer_has_non_translation_text_transform(layer);
}

void reset_text_editor_scroll(QTextEdit* editor) {
  if (editor == nullptr) {
    return;
  }
  for (auto* bar : {editor->horizontalScrollBar(), editor->verticalScrollBar()}) {
    if (bar != nullptr && bar->value() != 0) {
      QSignalBlocker blocker(bar);
      bar->setValue(0);
    }
  }
}

QString paragraph_alignment_name(Qt::Alignment alignment) {
  if ((alignment & Qt::AlignHCenter) != 0) {
    return QStringLiteral("center");
  }
  if ((alignment & Qt::AlignRight) != 0) {
    return QStringLiteral("right");
  }
  if ((alignment & Qt::AlignJustify) != 0) {
    return QStringLiteral("justify");
  }
  return QStringLiteral("left");
}

Qt::Alignment paragraph_alignment_from_name(const QString& name) {
  if (name.compare(QStringLiteral("center"), Qt::CaseInsensitive) == 0) {
    return Qt::AlignHCenter;
  }
  if (name.compare(QStringLiteral("right"), Qt::CaseInsensitive) == 0) {
    return Qt::AlignRight;
  }
  if (name.compare(QStringLiteral("justify"), Qt::CaseInsensitive) == 0) {
    return Qt::AlignJustify;
  }
  return Qt::AlignLeft;
}

bool mark_text_editor_finished(QTextEdit* editor) {
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return false;
  }
  editor->setProperty(kTextEditorFinishedProperty, true);
  return true;
}

void configure_text_font_smoothing(QFont& font, int anti_alias) {
  anti_alias = std::clamp(anti_alias, 0, 16);
  if (anti_alias <= 0) {
    font.setStyleStrategy(QFont::NoAntialias);
    font.setHintingPreference(QFont::PreferFullHinting);
    return;
  }

  font.setStyleStrategy(QFont::PreferAntialias);
  switch (anti_alias) {
    case 1:
    case 4:
    case 5:
      font.setHintingPreference(QFont::PreferFullHinting);
      break;
    case 2:
    case 6:
      font.setHintingPreference(QFont::PreferVerticalHinting);
      break;
    default:
      font.setHintingPreference(QFont::PreferNoHinting);
      break;
  }
}

QTextCharFormat text_format_with_smoothing(QTextCharFormat format, int anti_alias) {
  auto font = format.font();
  configure_text_font_smoothing(font, anti_alias);
  format.setFont(font);
  return format;
}

void apply_text_smoothing_to_document(QTextDocument& document, int anti_alias) {
  auto default_font = document.defaultFont();
  configure_text_font_smoothing(default_font, anti_alias);
  document.setDefaultFont(default_font);

  struct FormatRange {
    int position{0};
    int length{0};
    QTextCharFormat format;
  };
  std::vector<FormatRange> ranges;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      ranges.push_back(FormatRange{fragment.position(), fragment.length(),
                                    text_format_with_smoothing(fragment.charFormat(), anti_alias)});
    }
  }

  for (const auto& range : ranges) {
    QTextCursor cursor(&document);
    cursor.setPosition(range.position);
    cursor.setPosition(range.position + range.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(range.format);
  }
}

class InlineTextEdit final : public QTextEdit {
public:
  explicit InlineTextEdit(QWidget* parent = nullptr) : QTextEdit(parent), caret_blink_timer_(this) {
    setCursorWidth(kTextEditorCaretWidth);
    caret_blink_clock_.start();
    const auto flash_time = text_editor_caret_blink_phase_ms();
    caret_blink_timer_.setInterval(std::max(40, flash_time > 0 ? flash_time / 2 : 80));
    connect(&caret_blink_timer_, &QTimer::timeout, this, [this] {
      if (hasFocus() && property(kTextEditorPreviewPaintProperty).toBool()) {
        viewport()->update();
      }
    });
    caret_blink_timer_.start();
  }

protected:
  void focusInEvent(QFocusEvent* event) override {
    caret_blink_clock_.restart();
    QTextEdit::focusInEvent(event);
  }

  bool canInsertFromMimeData(const QMimeData* source) const override {
    return source != nullptr && (source->hasText() || QTextEdit::canInsertFromMimeData(source));
  }

  void insertFromMimeData(const QMimeData* source) override {
    if (source == nullptr) {
      return;
    }
    if (!source->hasText()) {
      QTextEdit::insertFromMimeData(source);
      return;
    }
    auto cursor = textCursor();
    cursor.insertText(source->text(), currentCharFormat());
    setTextCursor(cursor);
    ensureCursorVisible();
  }

  void paintEvent(QPaintEvent* event) override {
    if (!property(kTextEditorPreviewPaintProperty).toBool()) {
      QTextEdit::paintEvent(event);
      return;
    }

    bool zoom_ok = false;
    const auto zoom = property("patchy.editorZoom").toDouble(&zoom_ok);
    update_text_editor_preview_caret(*this, zoom_ok ? zoom : 1.0);
    if (property(kTextEditorTransformedOverlayProperty).toBool()) {
      return;
    }
    QPainter painter(viewport());
    painter.setClipRect(event->rect());
    paint_selection_highlight(painter);
    if (hasFocus() && caret_visible()) {
      auto caret = property(kTextEditorPreviewCaretProperty).toRect();
      if (caret.isNull() || caret.isEmpty()) {
        caret = text_editor_viewport_caret_rect(*this);
        caret.setWidth(text_editor_caret_width(*this));
      }
      painter.fillRect(caret.intersected(viewport()->rect()), palette().color(QPalette::Text));
    }
  }

private:
  bool caret_visible() const {
    const auto flash_time = text_editor_caret_blink_phase_ms();
    if (flash_time <= 0 || !caret_blink_clock_.isValid()) {
      return true;
    }
    return ((caret_blink_clock_.elapsed() / flash_time) % 2) == 0;
  }

  void paint_selection_highlight(QPainter& painter) const {
    const auto selection = textCursor();
    if (!selection.hasSelection()) {
      return;
    }

    QColor highlight = palette().color(QPalette::Highlight);
    highlight.setAlpha(120);
    if (property(kTextEditorPreviewPaintProperty).toBool()) {
      const auto preview_rects = property(kTextEditorPreviewSelectionProperty).toList();
      for (const auto& rect_value : preview_rects) {
        const auto rect = rect_value.toRect().intersected(viewport()->rect());
        if (!rect.isEmpty()) {
          painter.fillRect(rect, highlight);
        }
      }
      return;
    }

    const auto start = std::min(selection.selectionStart(), selection.selectionEnd());
    const auto end = std::max(selection.selectionStart(), selection.selectionEnd());
    const QPointF scroll_offset(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    auto* layout = document()->documentLayout();
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
      const auto* text_layout = block.layout();
      if (layout == nullptr || text_layout == nullptr) {
        continue;
      }
      const auto block_start = block.position();
      const auto block_end = block_start + block.length();
      const auto selected_start = std::max(start, block_start);
      const auto selected_end = std::min(end, block_end);
      if (selected_start >= selected_end) {
        continue;
      }

      const auto block_origin = layout->blockBoundingRect(block).topLeft() + scroll_offset;
      for (int line_index = 0; line_index < text_layout->lineCount(); ++line_index) {
        const auto line = text_layout->lineAt(line_index);
        const auto line_start = block_start + line.textStart();
        const auto line_end = line_start + line.textLength();
        const auto line_selected_start = std::max(selected_start, line_start);
        const auto line_selected_end = std::min(selected_end, line_end);
        if (line_selected_start >= line_selected_end) {
          continue;
        }

        const auto start_x = line.cursorToX(line_selected_start - block_start);
        const auto end_x = line.cursorToX(line_selected_end - block_start);
        QRectF highlight_rect(block_origin.x() + std::min(start_x, end_x), block_origin.y() + line.y(),
                              std::max<qreal>(1.0, std::abs(end_x - start_x)), line.height());
        painter.fillRect(highlight_rect.toAlignedRect().intersected(viewport()->rect()), highlight);
      }
    }
  }

  QElapsedTimer caret_blink_clock_;
  QTimer caret_blink_timer_;
};

class TextCommitFilter final : public QObject {
public:
  TextCommitFilter(QPointer<QTextEdit> editor, std::function<void()> commit, QObject* parent)
      : QObject(parent), editor_(std::move(editor)), commit_(std::move(commit)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if ((watched == editor_ || (editor_ != nullptr && watched == editor_->viewport())) &&
        event->type() == QEvent::FocusOut) {
      QTimer::singleShot(0, this, [this] {
        if (editor_ != nullptr && !editor_->hasFocus() && !editor_->viewport()->hasFocus()) {
          commit_();
        }
      });
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QPointer<QTextEdit> editor_;
  std::function<void()> commit_;
};

void scale_font_size(QFont& font, double scale) {
  if (scale <= 0.0 || !std::isfinite(scale)) {
    return;
  }
  if (font.pixelSize() > 0) {
    font.setPixelSize(std::max(1, static_cast<int>(std::round(static_cast<double>(font.pixelSize()) * scale))));
  } else if (font.pointSizeF() > 0.0) {
    font.setPointSizeF(std::max(1.0, font.pointSizeF() * scale));
  }
}

void scale_font_width(QFont& font, double scale) {
  if (scale <= 0.0 || !std::isfinite(scale) || std::abs(scale - 1.0) < 0.0001) {
    return;
  }
  const auto stretch = font.stretch() > 0 ? font.stretch() : 100;
  font.setStretch(std::clamp(static_cast<int>(std::round(static_cast<double>(stretch) * scale)), 1, 400));
}

void scale_document_font_sizes(QTextDocument& document, double scale) {
  if (scale <= 0.0 || !std::isfinite(scale) || std::abs(scale - 1.0) < 0.0001) {
    return;
  }

  auto default_font = document.defaultFont();
  scale_font_size(default_font, scale);
  document.setDefaultFont(default_font);

  struct FormatRange {
    int position{0};
    int length{0};
    QTextCharFormat format;
  };
  struct BlockFormatRange {
    int position{0};
    int length{0};
    QTextBlockFormat format;
  };
  std::vector<FormatRange> ranges;
  std::vector<BlockFormatRange> block_ranges;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    auto block_format = block.blockFormat();
    auto scaled_block_format = block_format;
    scaled_block_format.setLeftMargin(block_format.leftMargin() * scale);
    scaled_block_format.setRightMargin(block_format.rightMargin() * scale);
    scaled_block_format.setTextIndent(block_format.textIndent() * scale);
    scaled_block_format.setTopMargin(block_format.topMargin() * scale);
    scaled_block_format.setBottomMargin(block_format.bottomMargin() * scale);
    if (std::abs(scaled_block_format.leftMargin() - block_format.leftMargin()) > 0.0001 ||
        std::abs(scaled_block_format.rightMargin() - block_format.rightMargin()) > 0.0001 ||
        std::abs(scaled_block_format.textIndent() - block_format.textIndent()) > 0.0001 ||
        std::abs(scaled_block_format.topMargin() - block_format.topMargin()) > 0.0001 ||
        std::abs(scaled_block_format.bottomMargin() - block_format.bottomMargin()) > 0.0001) {
      block_ranges.push_back(BlockFormatRange{block.position(), std::max(1, block.length()), scaled_block_format});
    }

    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }

      auto format = fragment.charFormat();
      auto format_font = format.font();
      const auto before_pixel_size = format_font.pixelSize();
      const auto before_point_size = format_font.pointSizeF();
      const auto before_leading = format.hasProperty(kTextLeadingFormatProperty)
                                      ? format.property(kTextLeadingFormatProperty).toDouble()
                                      : 0.0;
      // The exact (fractional) size scales alongside the int pixel size so Photoshop-layout
      // math stays sub-pixel across editor zoom changes and editor->document conversion.
      const auto before_exact_size = format.hasProperty(kTextExactSizeFormatProperty)
                                         ? format.property(kTextExactSizeFormatProperty).toDouble()
                                         : 0.0;
      scale_font_size(format_font, scale);
      bool changed = format_font.pixelSize() != before_pixel_size ||
                     std::abs(format_font.pointSizeF() - before_point_size) >= 0.0001;
      if (format.hasProperty(kTextLeadingFormatProperty) && std::isfinite(before_leading) && before_leading > 0.0) {
        const auto scaled_leading = before_leading * scale;
        format.setProperty(kTextLeadingFormatProperty, scaled_leading);
        changed = changed || std::abs(scaled_leading - before_leading) >= 0.0001;
      }
      if (std::isfinite(before_exact_size) && before_exact_size > 0.0) {
        const auto scaled_exact = before_exact_size * scale;
        format.setProperty(kTextExactSizeFormatProperty, scaled_exact);
        // Re-derive the int pixel size from the exact value so repeated zoom round trips
        // do not accumulate integer-rounding drift. The exact size is the FontSize basis;
        // the glyph pixel size folds the character panel's vertical scale back in.
        if (format_font.pixelSize() > 0) {
          auto vertical_glyph_scale = 1.0;
          if (format.hasProperty(kTextVerticalScaleFormatProperty)) {
            const auto value = format.property(kTextVerticalScaleFormatProperty).toDouble();
            if (std::isfinite(value) && value > 0.01 && value < 100.0) {
              vertical_glyph_scale = value;
            }
          }
          format_font.setPixelSize(
              std::max(1, static_cast<int>(std::lround(scaled_exact * vertical_glyph_scale))));
        }
        changed = true;
      }
      if (format.hasProperty(kTextTrackingFormatProperty) && format_font.letterSpacingType() == QFont::AbsoluteSpacing) {
        const auto tracking = format.property(kTextTrackingFormatProperty).toDouble();
        if (std::isfinite(tracking) && std::abs(tracking) > 0.0001) {
          const auto exact = std::isfinite(before_exact_size) && before_exact_size > 0.0
                                 ? before_exact_size * scale
                                 : static_cast<double>(format_font.pixelSize());
          auto horizontal_glyph_scale = 1.0;
          if (format.hasProperty(kTextHorizontalScaleFormatProperty)) {
            const auto value = format.property(kTextHorizontalScaleFormatProperty).toDouble();
            if (std::isfinite(value) && value > 0.01 && value < 100.0) {
              horizontal_glyph_scale = value;
            }
          }
          format_font.setLetterSpacing(QFont::AbsoluteSpacing,
                                       tracking / 1000.0 * exact * horizontal_glyph_scale);
          changed = true;
        }
      }
      if (!changed) {
        continue;
      }
      format.setFont(format_font);
      ranges.push_back(FormatRange{fragment.position(), fragment.length(), format});
    }
  }

  for (const auto& range : block_ranges) {
    QTextCursor cursor(&document);
    cursor.setPosition(range.position);
    cursor.setPosition(range.position + range.length, QTextCursor::KeepAnchor);
    cursor.mergeBlockFormat(range.format);
  }

  for (const auto& range : ranges) {
    QTextCursor cursor(&document);
    cursor.setPosition(range.position);
    cursor.setPosition(range.position + range.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(range.format);
  }
}

void scale_document_font_widths(QTextDocument& document, double scale) {
  if (scale <= 0.0 || !std::isfinite(scale) || std::abs(scale - 1.0) < 0.0001) {
    return;
  }

  auto default_font = document.defaultFont();
  scale_font_width(default_font, scale);
  document.setDefaultFont(default_font);

  struct FormatRange {
    int position{0};
    int length{0};
    QTextCharFormat format;
  };
  std::vector<FormatRange> ranges;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }

      auto format = fragment.charFormat();
      auto format_font = format.font();
      const auto before_stretch = format_font.stretch();
      scale_font_width(format_font, scale);
      if (format_font.stretch() == before_stretch) {
        continue;
      }
      format.setFont(format_font);
      ranges.push_back(FormatRange{fragment.position(), fragment.length(), format});
    }
  }

  for (const auto& range : ranges) {
    QTextCursor cursor(&document);
    cursor.setPosition(range.position);
    cursor.setPosition(range.position + range.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(range.format);
  }
}

QString normalized_rich_text_html(const QTextDocument& source) {
  QTextDocument copy;
  copy.setDocumentMargin(0);
  copy.setDefaultFont(source.defaultFont());
  copy.setDefaultTextOption(source.defaultTextOption());
  copy.setHtml(source.toHtml());
  return copy.toHtml();
}

std::unique_ptr<QTextDocument> copy_text_document_formats(const QTextDocument& source) {
  auto document = std::make_unique<QTextDocument>();
  document->setDocumentMargin(0);
  document->setDefaultFont(source.defaultFont());
  document->setDefaultTextOption(source.defaultTextOption());
  document->setPlainText(source.toPlainText());

  const int plain_length = static_cast<int>(document->toPlainText().size());
  for (auto block = source.begin(); block.isValid(); block = block.next()) {
    QTextCursor block_cursor(document.get());
    const auto block_start = std::clamp(block.position(), 0, std::max(0, plain_length));
    const auto block_end = std::clamp(block.position() + std::max(1, block.length()), 0, std::max(0, plain_length));
    block_cursor.setPosition(block_start);
    block_cursor.setPosition(block_end, QTextCursor::KeepAnchor);
    block_cursor.mergeBlockFormat(block.blockFormat());

    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      const auto start = std::clamp(fragment.position(), 0, std::max(0, plain_length));
      const auto end = std::clamp(fragment.position() + fragment.length(), 0, std::max(0, plain_length));
      if (start >= end) {
        continue;
      }
      QTextCursor cursor(document.get());
      cursor.setPosition(start);
      cursor.setPosition(end, QTextCursor::KeepAnchor);
      cursor.mergeCharFormat(fragment.charFormat());
    }
  }
  return document;
}

std::unique_ptr<QTextDocument> document_from_editor_in_document_units(const QTextEdit& editor, double zoom) {
  auto document = copy_text_document_formats(*editor.document());
  scale_document_font_sizes(*document, zoom > 0.0 ? 1.0 / zoom : 1.0);
  return document;
}

constexpr auto kTextEditorCaretLayoutObjectName = "patchy.caretLayoutDocument";
constexpr auto kTextEditorCaretLayoutGenerationProperty = "patchy.caretLayoutGeneration";
constexpr auto kTextEditorCaretLayoutBuiltGenerationProperty = "patchy.caretLayoutBuiltGeneration";

// Builds the editor's text laid out at document (1:1) resolution -- through the exact same
// construction the rasterizer uses (build_text_render_document), so caret/selection geometry is
// computed on the identical document the on-screen glyphs come from.  Copying the editor
// document's formats directly is NOT equivalent: blank paragraphs carry their line height in
// block char formats that a fragment-level copy misses, so files with empty lines (old PSDs
// separate lines with control characters) drifted the caret off the text.
std::unique_ptr<QTextDocument> build_text_editor_document_space_layout(const QTextEdit& editor) {
  bool zoom_ok = false;
  double zoom = editor.property("patchy.editorZoom").toDouble(&zoom_ok);
  if (!zoom_ok || !(zoom > 0.0)) {
    zoom = 1.0;
  }

  const auto source_units = document_from_editor_in_document_units(editor, zoom);
  const auto text_size = std::max(1, editor.property("patchy.documentTextSize").toInt());
  const auto text_width =
      std::max(kMinimumTextBoxDocumentSize, editor.property("patchy.documentTextWidth").toInt());
  const auto text_height =
      std::max(kMinimumTextBoxDocumentSize, editor.property("patchy.documentTextHeight").toInt());
  const auto anti_alias = std::clamp(editor.property("patchy.documentTextAntiAlias").toInt(), 0, 16);
  auto family = editor.property("patchy.documentTextFamily").toString();
  if (family.trimmed().isEmpty()) {
    family = display_text_family_from_font(editor.font());
  }
  const auto stored_color = editor.property("patchy.documentTextColor").value<QColor>();
  const auto color = stored_color.isValid() ? stored_color : QColor(Qt::black);
  TextToolSettings settings{source_units->toPlainText(),
                            QString(),
                            family,
                            text_size,
                            editor.font().bold(),
                            editor.font().italic(),
                            anti_alias,
                            text_flow_is_box(editor.property("patchy.documentTextFlow").toString()),
                            text_width,
                            text_height};
  const auto rich_text_runs = rich_text_runs_from_document(*source_units, settings, color);
  const auto paragraph_runs = paragraph_runs_from_document(*source_units);
  auto built = build_text_render_document(settings, color, text_width, paragraph_runs, rich_text_runs,
                                          text_editor_metric_scale(editor));
  return std::move(built.document);
}

// Returns the editor's text laid out at document (1:1) resolution. Caret and selection geometry must
// be derived from this layout and then scaled by the zoom: the rendered text is composited at document
// resolution, so laying out directly from the zoom-shrunk editor font produces line metrics that do not
// scale linearly and the caret/selection drift away from the text as the user zooms out.
//
// The layout is cached and rebuilt only when the text content actually changes (tracked by a generation
// counter that is bumped on genuine edits but not when the editor is merely re-scaled for a new zoom).
// This keeps the document-space geometry identical across zoom levels, so the caret tracks the text.
QTextDocument* text_editor_document_space_layout(const QTextEdit& editor, double& zoom_out) {
  bool zoom_ok = false;
  double zoom = editor.property("patchy.editorZoom").toDouble(&zoom_ok);
  zoom_out = (!zoom_ok || !(zoom > 0.0)) ? 1.0 : zoom;

  auto& mutable_editor = const_cast<QTextEdit&>(editor);
  const auto generation = editor.property(kTextEditorCaretLayoutGenerationProperty).toInt();
  auto* cached = editor.findChild<QTextDocument*>(QString::fromLatin1(kTextEditorCaretLayoutObjectName),
                                                  Qt::FindDirectChildrenOnly);
  const auto built_generation_value = editor.property(kTextEditorCaretLayoutBuiltGenerationProperty);
  if (cached != nullptr && built_generation_value.isValid() && built_generation_value.toInt() == generation) {
    return cached;
  }

  delete cached;
  auto document = build_text_editor_document_space_layout(editor);
  auto* layout_document = document.release();
  layout_document->setParent(&mutable_editor);
  layout_document->setObjectName(QString::fromLatin1(kTextEditorCaretLayoutObjectName));
  mutable_editor.setProperty(kTextEditorCaretLayoutBuiltGenerationProperty, generation);
  return layout_document;
}

QRectF scale_document_rect_to_viewport(const QRect& document_rect, double zoom, QPointF scroll_offset) {
  return QRectF(static_cast<qreal>(document_rect.x()) * zoom - scroll_offset.x(),
                static_cast<qreal>(document_rect.y()) * zoom - scroll_offset.y(),
                std::max<qreal>(1.0, static_cast<qreal>(document_rect.width()) * zoom),
                std::max<qreal>(1.0, static_cast<qreal>(document_rect.height()) * zoom));
}

QRect text_document_cursor_rect(const QTextDocument& document, int position) {
  const auto maximum_position = std::max(0, document.characterCount() - 1);
  position = std::clamp(position, 0, maximum_position);
  const auto block = document.findBlock(position);
  if (!block.isValid() || block.layout() == nullptr || document.documentLayout() == nullptr) {
    return {};
  }

  const auto* text_layout = block.layout();
  const auto block_origin = document.documentLayout()->blockBoundingRect(block).topLeft();
  const auto relative_position = std::max(0, position - block.position());
  for (int line_index = 0; line_index < text_layout->lineCount(); ++line_index) {
    const auto line = text_layout->lineAt(line_index);
    const auto line_start = line.textStart();
    const auto line_end = line_start + line.textLength();
    if (relative_position < line_start || (relative_position > line_end && line_index + 1 < text_layout->lineCount())) {
      continue;
    }
    const auto x = line.cursorToX(std::clamp(relative_position, line_start, line_end));
    const auto glyph_height =
        std::max<qreal>(1.0, std::ceil(std::max<qreal>(1.0, line.ascent()) + std::max<qreal>(0.0, line.descent())));
    const auto top_padding = std::max<qreal>(0.0, (line.height() - glyph_height) / 2.0);
    return QRectF(block_origin.x() + x, block_origin.y() + line.y() + top_padding, 1.0, glyph_height).toAlignedRect();
  }

  const auto block_rect = document.documentLayout()->blockBoundingRect(block);
  return QRectF(block_rect.left(), block_rect.top(), 1.0, std::max<qreal>(1.0, block_rect.height())).toAlignedRect();
}

std::vector<QRect> text_document_selection_rects(const QTextDocument& document, int start, int end) {
  const auto maximum_position = std::max(0, document.characterCount() - 1);
  start = std::clamp(start, 0, maximum_position);
  end = std::clamp(end, 0, maximum_position);
  if (start > end) {
    std::swap(start, end);
  }
  if (start == end || document.documentLayout() == nullptr) {
    return {};
  }

  std::vector<QRect> rects;
  const auto* layout = document.documentLayout();
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    const auto* text_layout = block.layout();
    if (text_layout == nullptr) {
      continue;
    }

    const auto block_start = block.position();
    const auto block_end = block_start + block.length();
    const auto selected_start = std::max(start, block_start);
    const auto selected_end = std::min(end, block_end);
    if (selected_start >= selected_end) {
      continue;
    }

    const auto block_origin = layout->blockBoundingRect(block).topLeft();
    for (int line_index = 0; line_index < text_layout->lineCount(); ++line_index) {
      const auto line = text_layout->lineAt(line_index);
      const auto line_start = block_start + line.textStart();
      const auto line_end = line_start + line.textLength();
      const auto line_selected_start = std::max(selected_start, line_start);
      const auto line_selected_end = std::min(selected_end, line_end);
      if (line_selected_start >= line_selected_end) {
        continue;
      }

      const auto start_x = line.cursorToX(line_selected_start - block_start);
      const auto end_x = line.cursorToX(line_selected_end - block_start);
      const QRectF rect(block_origin.x() + std::min(start_x, end_x), block_origin.y() + line.y(),
                        std::max<qreal>(1.0, std::abs(end_x - start_x)), line.height());
      rects.push_back(rect.toAlignedRect());
    }
  }
  return rects;
}

void clear_text_editor_preview_overlays(QTextEdit& editor) {
  editor.setProperty(kTextEditorPreviewCaretProperty, QVariant());
  editor.setProperty(kTextEditorPreviewSelectionProperty, QVariant());
}

int text_editor_caret_width(const QTextEdit& editor) noexcept {
  if (editor.cursorWidth() <= 0) {
    return 0;
  }
  return std::max(kTextEditorCaretWidth, editor.cursorWidth());
}

int text_editor_caret_blink_phase_ms() {
  const auto flash_time = QApplication::cursorFlashTime();
  if (flash_time <= 0) {
    return flash_time;
  }
  return std::max(kMinimumTextEditorCaretBlinkPhaseMs, flash_time / kTextEditorCaretBlinkSpeedMultiplier);
}

QRect text_editor_viewport_caret_rect(const QTextEdit& editor) {
  double zoom = 1.0;
  const auto layout_document = text_editor_document_space_layout(editor, zoom);
  auto caret_document_rect = text_document_cursor_rect(*layout_document, editor.textCursor().position());
  QRect caret;
  if (!caret_document_rect.isEmpty()) {
    const QPointF scroll_offset(editor.horizontalScrollBar()->value(), editor.verticalScrollBar()->value());
    caret = scale_document_rect_to_viewport(caret_document_rect, zoom, scroll_offset).toAlignedRect();
  }
  if (caret.isEmpty()) {
    caret = editor.cursorRect();
  }
  return caret;
}

std::vector<QRect> text_editor_viewport_selection_rects(const QTextEdit& editor, int start, int end) {
  double zoom = 1.0;
  const auto layout_document = text_editor_document_space_layout(editor, zoom);
  const auto document_selection_rects = text_document_selection_rects(*layout_document, start, end);
  const QPointF scroll_offset(editor.horizontalScrollBar()->value(), editor.verticalScrollBar()->value());
  std::vector<QRect> rects;
  rects.reserve(document_selection_rects.size());
  for (const auto& rect : document_selection_rects) {
    const auto aligned = scale_document_rect_to_viewport(rect, zoom, scroll_offset).toAlignedRect();
    if (!aligned.isEmpty()) {
      rects.push_back(aligned);
    }
  }
  return rects;
}

void update_text_editor_preview_caret(QTextEdit& editor, double zoom) {
  Q_UNUSED(zoom);
  if (!editor.property(kTextEditorPreviewPaintProperty).toBool()) {
    clear_text_editor_preview_overlays(editor);
    return;
  }

  double layout_zoom = 1.0;
  const auto layout_document = text_editor_document_space_layout(editor, layout_zoom);
  const QPointF scroll_offset(editor.horizontalScrollBar()->value(), editor.verticalScrollBar()->value());

  QVariantList selection_rects;
  const auto cursor = editor.textCursor();
  if (cursor.hasSelection()) {
    for (const auto& rect :
         text_document_selection_rects(*layout_document, cursor.selectionStart(), cursor.selectionEnd())) {
      const auto aligned = scale_document_rect_to_viewport(rect, layout_zoom, scroll_offset).toAlignedRect();
      if (!aligned.isEmpty()) {
        selection_rects.push_back(aligned);
      }
    }
  }
  editor.setProperty(kTextEditorPreviewSelectionProperty, selection_rects);

  const auto caret_document_rect = text_document_cursor_rect(*layout_document, editor.textCursor().position());
  auto caret = caret_document_rect.isEmpty()
                   ? editor.cursorRect()
                   : scale_document_rect_to_viewport(caret_document_rect, layout_zoom, scroll_offset).toAlignedRect();
  const auto caret_width = text_editor_caret_width(editor);
  if (caret.isEmpty() || caret_width <= 0) {
    editor.setProperty(kTextEditorPreviewCaretProperty, QVariant());
    return;
  }
  caret.setWidth(caret_width);
  editor.setProperty(kTextEditorPreviewCaretProperty, caret);
}

class TransformedTextEditOverlay final : public QWidget {
  enum class ResizeHandle { None, TopLeft, TopRight, BottomLeft, BottomRight };

  struct ResizeDragState {
    ResizeHandle handle{ResizeHandle::None};
    QTransform start_transform;
    QPointF start_editor_point;
    int start_width{kMinimumTextBoxDocumentSize};
    int start_height{kMinimumTextBoxDocumentSize};
  };

  struct ResizeGeometry {
    QTransform transform;
    int width{kMinimumTextBoxDocumentSize};
    int height{kMinimumTextBoxDocumentSize};
  };

  static constexpr std::array<ResizeHandle, 4> kResizeHandles = {
      ResizeHandle::TopLeft,
      ResizeHandle::TopRight,
      ResizeHandle::BottomLeft,
      ResizeHandle::BottomRight,
  };

public:
  explicit TransformedTextEditOverlay(CanvasWidget* canvas)
      : QWidget(canvas), canvas_(canvas), caret_blink_timer_(this) {
    setObjectName(QString::fromLatin1(kTransformedTextEditOverlayObjectName));
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    caret_blink_clock_.start();
    const auto flash_time = text_editor_caret_blink_phase_ms();
    caret_blink_timer_.setInterval(std::max(40, flash_time > 0 ? flash_time / 2 : 80));
    connect(&caret_blink_timer_, &QTimer::timeout, this, [this] {
      if (editor_ != nullptr && editor_->hasFocus()) {
        update();
      }
    });
    caret_blink_timer_.start();
  }

  [[nodiscard]] QTextEdit* editor() const noexcept {
    return editor_;
  }

  void configure(QTextEdit* editor, QTransform transform, std::function<void(QTextEdit*)> resize_callback) {
    editor_ = editor;
    text_transform_ = std::move(transform);
    resize_callback_ = std::move(resize_callback);
    resize_preview_transform_.reset();
    resize_preview_document_size_.reset();
    sync_geometry();
  }

  void sync_geometry() {
    if (canvas_ == nullptr || editor_ == nullptr || editor_->viewport() == nullptr) {
      hide();
      return;
    }

    const QRectF editor_rect = editor_visual_rect().adjusted(-4.0, -4.0, 4.0, 4.0);
    const auto canvas_polygon = map_editor_rect_to_canvas(editor_rect);
    if (canvas_polygon.isEmpty()) {
      hide();
      return;
    }

    const auto overlay_geometry = canvas_polygon.boundingRect().adjusted(-8.0, -8.0, 8.0, 8.0).toAlignedRect();
    if (overlay_geometry.isEmpty()) {
      hide();
      return;
    }

    setGeometry(overlay_geometry);
    QPolygon mask_polygon;
    QVariantList editor_polygon;
    const QPointF origin(overlay_geometry.topLeft());
    for (const auto& point : canvas_polygon) {
      mask_polygon << (point - origin).toPoint();
      editor_polygon.push_back(point);
    }
    auto mask = QRegion(mask_polygon);
    QVariantList handle_centers;
    for (const auto handle : kResizeHandles) {
      const auto handle_rect = resize_handle_rect(handle);
      mask = mask.united(QRegion(handle_rect.toAlignedRect()));
      handle_centers.push_back(handle_rect.center() + origin);
    }
    setProperty("patchy.transformedTextEditorPolygon", editor_polygon);
    setProperty("patchy.transformedTextResizeHandleCenters", handle_centers);
    setMask(mask);
    show();
    raise();
    update();
  }

  [[nodiscard]] bool wants_canvas_mouse_event(QPointF canvas_point, QEvent::Type event_type,
                                              Qt::MouseButtons buttons) const {
    if (editor_ == nullptr || !isVisible()) {
      return false;
    }
    const QPointF overlay_point = canvas_point - QPointF(geometry().topLeft());
    if (selecting_ || resize_drag_.has_value()) {
      return event_type == QEvent::MouseMove || event_type == QEvent::MouseButtonRelease;
    }
    if (event_type == QEvent::MouseButtonRelease) {
      return false;
    }
    if (event_type == QEvent::MouseMove && (buttons & Qt::LeftButton) != 0) {
      return false;
    }
    if (resize_handle_at(overlay_point) != ResizeHandle::None) {
      return true;
    }
    if (!rect().adjusted(-2, -2, 2, 2).contains(overlay_point.toPoint())) {
      return false;
    }
    const auto editor_point = map_overlay_point_to_editor(overlay_point);
    return editor_point.has_value() && editor_local_rect().adjusted(-4, -4, 4, 4).contains(editor_point->toPoint());
  }

  [[nodiscard]] bool has_resize_handle_at_canvas_point(QPointF canvas_point) const {
    if (editor_ == nullptr || !isVisible()) {
      return false;
    }
    return resize_handle_at(canvas_point - QPointF(pos())) != ResizeHandle::None;
  }

  bool handle_canvas_mouse_event(QMouseEvent* mouse_event) {
    if (mouse_event == nullptr) {
      return false;
    }
    return handle_canvas_mouse_event(mouse_event, mouse_event->position());
  }

  bool handle_canvas_mouse_event(QMouseEvent* mouse_event, QPointF canvas_position) {
    if (mouse_event == nullptr ||
        !wants_canvas_mouse_event(canvas_position, mouse_event->type(), mouse_event->buttons())) {
      return false;
    }

    const QPointF overlay_position = canvas_position - QPointF(pos());
    QMouseEvent forwarded(mouse_event->type(), overlay_position, mouse_event->globalPosition(), mouse_event->button(),
                          mouse_event->buttons(), mouse_event->modifiers());
    switch (mouse_event->type()) {
      case QEvent::MouseButtonPress:
        mousePressEvent(&forwarded);
        break;
      case QEvent::MouseMove:
        mouseMoveEvent(&forwarded);
        break;
      case QEvent::MouseButtonRelease:
        mouseReleaseEvent(&forwarded);
        break;
      case QEvent::MouseButtonDblClick:
        mouseDoubleClickEvent(&forwarded);
        break;
      default:
        break;
    }
    if (!forwarded.isAccepted()) {
      return false;
    }
    mouse_event->accept();
    return true;
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    if (canvas_ == nullptr || editor_ == nullptr || editor_->viewport() == nullptr) {
      return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paint_selection(painter);
    paint_caret(painter);
    paint_text_box_controls(painter);
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton || editor_ == nullptr) {
      event->ignore();
      return;
    }
    if (const auto handle = resize_handle_at(event->position()); handle != ResizeHandle::None) {
      editor_->setFocus(Qt::MouseFocusReason);
      resize_drag_ = ResizeDragState{handle,
                                     text_transform_,
                                     editor_point_for_handle(handle),
                                     std::max(kMinimumTextBoxDocumentSize,
                                              editor_->property("patchy.documentTextWidth").toInt()),
                                     std::max(kMinimumTextBoxDocumentSize,
                                              editor_->property("patchy.documentTextHeight").toInt())};
      event->accept();
      return;
    }

    const auto position = cursor_position_for_overlay_point(event->position());
    if (!position.has_value()) {
      event->ignore();
      return;
    }

    editor_->setFocus(Qt::MouseFocusReason);
    auto cursor = editor_->textCursor();
    if ((event->modifiers() & Qt::ShiftModifier) != 0) {
      selection_anchor_ = cursor.position();
      cursor.setPosition(selection_anchor_);
      cursor.setPosition(*position, QTextCursor::KeepAnchor);
    } else {
      selection_anchor_ = *position;
      cursor.setPosition(*position);
    }
    editor_->setTextCursor(cursor);
    selecting_ = true;
    event->accept();
    update();
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (resize_drag_.has_value()) {
      if (const auto geometry = resize_geometry_for_event(event); geometry.has_value()) {
        resize_preview_transform_ = geometry->transform;
        resize_preview_document_size_ = QSizeF(geometry->width, geometry->height);
        sync_geometry();
      }
      event->accept();
      return;
    }
    if (!selecting_ || (event->buttons() & Qt::LeftButton) == 0 || editor_ == nullptr) {
      update_hover_cursor(event->position());
      event->ignore();
      return;
    }
    const auto position = cursor_position_for_overlay_point(event->position());
    if (!position.has_value()) {
      event->ignore();
      return;
    }

    auto cursor = editor_->textCursor();
    cursor.setPosition(selection_anchor_);
    cursor.setPosition(*position, QTextCursor::KeepAnchor);
    editor_->setTextCursor(cursor);
    event->accept();
    update();
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && resize_drag_.has_value()) {
      resize_text_box(event);
      resize_drag_.reset();
      update_hover_cursor(event->position());
      schedule_resize_callback();
      event->accept();
      return;
    }
    if (event->button() == Qt::LeftButton && selecting_) {
      selecting_ = false;
      event->accept();
      return;
    }
    event->ignore();
  }

  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton || editor_ == nullptr) {
      event->ignore();
      return;
    }
    const auto editor_point = map_overlay_point_to_editor(event->position());
    if (!editor_point.has_value() || !editor_local_rect().contains(editor_point->toPoint())) {
      event->ignore();
      return;
    }

    editor_->setFocus(Qt::MouseFocusReason);
    auto cursor = editor_->cursorForPosition(editor_point->toPoint());
    cursor.select(QTextCursor::WordUnderCursor);
    selection_anchor_ = cursor.selectionStart();
    editor_->setTextCursor(cursor);
    selecting_ = false;
    event->accept();
    update();
  }

private:
  [[nodiscard]] double zoom() const noexcept {
    return canvas_ == nullptr ? 1.0 : std::max(0.05, canvas_->zoom());
  }

  [[nodiscard]] QPointF document_origin_on_canvas() const {
    return canvas_ == nullptr ? QPointF() : QPointF(canvas_->widget_position_for_document_point(QPoint(0, 0)));
  }

  [[nodiscard]] QPointF map_editor_point_to_canvas(QPointF editor_point) const {
    const auto current_zoom = zoom();
    const QPointF document_local(editor_point.x() / current_zoom, editor_point.y() / current_zoom);
    const auto document_point = active_text_transform().map(document_local);
    const auto origin = document_origin_on_canvas();
    return QPointF(origin.x() + document_point.x() * current_zoom,
                   origin.y() + document_point.y() * current_zoom);
  }

  [[nodiscard]] std::optional<QPointF> map_canvas_point_to_editor(QPointF canvas_point,
                                                                  const QTransform& transform) const {
    bool invertible = false;
    const auto inverse = transform.inverted(&invertible);
    if (!invertible) {
      return std::nullopt;
    }

    const auto current_zoom = zoom();
    const auto origin = document_origin_on_canvas();
    const QPointF document_point((canvas_point.x() - origin.x()) / current_zoom,
                                 (canvas_point.y() - origin.y()) / current_zoom);
    const auto document_local = inverse.map(document_point);
    return QPointF(document_local.x() * current_zoom, document_local.y() * current_zoom);
  }

  [[nodiscard]] std::optional<QPointF> map_canvas_point_to_editor(QPointF canvas_point) const {
    return map_canvas_point_to_editor(canvas_point, active_text_transform());
  }

  [[nodiscard]] std::optional<QPointF> map_overlay_point_to_editor(QPointF overlay_point) const {
    return map_canvas_point_to_editor(overlay_point + QPointF(geometry().topLeft()));
  }

  [[nodiscard]] QRect editor_local_rect() const {
    return editor_ == nullptr || editor_->viewport() == nullptr ? QRect() : editor_->viewport()->rect();
  }

  [[nodiscard]] QRectF editor_visual_rect() const {
    if (resize_preview_document_size_.has_value()) {
      const auto current_zoom = zoom();
      return QRectF(0.0, 0.0, resize_preview_document_size_->width() * current_zoom,
                    resize_preview_document_size_->height() * current_zoom);
    }
    const auto visible_rect_value =
        editor_ == nullptr ? QVariant() : editor_->property(kTextEditorVisibleLocalRectProperty);
    if (visible_rect_value.isValid()) {
      const auto visible_rect = visible_rect_value.toRectF();
      if (visible_rect.isValid() && !visible_rect.isEmpty()) {
        const auto current_zoom = zoom();
        return QRectF(visible_rect.left() * current_zoom,
                      visible_rect.top() * current_zoom,
                      std::max<qreal>(1.0, visible_rect.width() * current_zoom),
                      std::max<qreal>(1.0, visible_rect.height() * current_zoom));
      }
    }
    return QRectF(editor_local_rect());
  }

  [[nodiscard]] QTransform active_text_transform() const {
    return resize_preview_transform_.value_or(text_transform_);
  }

  [[nodiscard]] std::optional<int> cursor_position_for_overlay_point(QPointF overlay_point) const {
    if (editor_ == nullptr) {
      return std::nullopt;
    }
    const auto editor_point = map_overlay_point_to_editor(overlay_point);
    if (!editor_point.has_value() || !editor_local_rect().adjusted(-4, -4, 4, 4).contains(editor_point->toPoint())) {
      return std::nullopt;
    }
    return editor_->cursorForPosition(editor_point->toPoint()).position();
  }

  [[nodiscard]] QPolygonF map_editor_rect_to_canvas(QRectF rect) const {
    QPolygonF polygon;
    polygon << map_editor_point_to_canvas(rect.topLeft()) << map_editor_point_to_canvas(rect.topRight())
            << map_editor_point_to_canvas(rect.bottomRight()) << map_editor_point_to_canvas(rect.bottomLeft());
    return polygon;
  }

  [[nodiscard]] QPolygonF map_editor_rect_to_overlay(QRectF rect) const {
    const QPointF overlay_origin(geometry().topLeft());
    auto polygon = map_editor_rect_to_canvas(rect);
    for (auto& point : polygon) {
      point -= overlay_origin;
    }
    return polygon;
  }

  [[nodiscard]] QPointF editor_point_for_handle(ResizeHandle handle) const {
    const auto rect = editor_visual_rect();
    switch (handle) {
      case ResizeHandle::TopLeft:
        return rect.topLeft();
      case ResizeHandle::TopRight:
        return rect.topRight();
      case ResizeHandle::BottomLeft:
        return rect.bottomLeft();
      case ResizeHandle::BottomRight:
        return rect.bottomRight();
      case ResizeHandle::None:
        break;
    }
    return QPointF(rect.center());
  }

  [[nodiscard]] QRectF resize_handle_rect(ResizeHandle handle) const {
    constexpr double kHandleSize = 6.0;
    const auto center = map_editor_rect_to_overlay(QRectF(editor_point_for_handle(handle), QSizeF(1.0, 1.0))).at(0);
    return QRectF(center.x() - kHandleSize / 2.0, center.y() - kHandleSize / 2.0, kHandleSize, kHandleSize);
  }

  [[nodiscard]] ResizeHandle resize_handle_at(QPointF point) const {
    for (const auto handle : kResizeHandles) {
      if (resize_handle_rect(handle).adjusted(-5.0, -5.0, 5.0, 5.0).contains(point)) {
        return handle;
      }
    }
    return ResizeHandle::None;
  }

  [[nodiscard]] Qt::CursorShape cursor_for_handle(ResizeHandle handle) const {
    switch (handle) {
      case ResizeHandle::TopLeft:
      case ResizeHandle::BottomRight:
        return Qt::SizeFDiagCursor;
      case ResizeHandle::TopRight:
      case ResizeHandle::BottomLeft:
        return Qt::SizeBDiagCursor;
      case ResizeHandle::None:
        return Qt::IBeamCursor;
    }
    return Qt::IBeamCursor;
  }

  void update_hover_cursor(QPointF point) {
    setCursor(cursor_for_handle(resize_handle_at(point)));
  }

  [[nodiscard]] bool caret_visible() const {
    const auto flash_time = text_editor_caret_blink_phase_ms();
    if (flash_time <= 0 || !caret_blink_clock_.isValid()) {
      return true;
    }
    return ((caret_blink_clock_.elapsed() / flash_time) % 2) == 0;
  }

  void paint_selection(QPainter& painter) const {
    const auto cursor = editor_->textCursor();
    if (!cursor.hasSelection()) {
      return;
    }

    QColor highlight = editor_->palette().color(QPalette::Highlight);
    highlight.setAlpha(120);
    painter.setPen(Qt::NoPen);
    painter.setBrush(highlight);
    const auto selection_rects =
        text_editor_viewport_selection_rects(*editor_, cursor.selectionStart(), cursor.selectionEnd());
    for (const auto& rect : selection_rects) {
      painter.drawPolygon(map_editor_rect_to_overlay(QRectF(rect)));
    }
  }

  void paint_caret(QPainter& painter) const {
    if (!editor_->hasFocus() || !caret_visible()) {
      return;
    }

    auto caret = text_editor_viewport_caret_rect(*editor_);
    const auto caret_width = text_editor_caret_width(*editor_);
    if (caret.isEmpty() || caret_width <= 0) {
      return;
    }
    caret.setWidth(caret_width);
    painter.setPen(Qt::NoPen);
    painter.setBrush(editor_->palette().color(QPalette::Text));
    painter.drawPolygon(map_editor_rect_to_overlay(QRectF(caret)));
  }

  void paint_text_box_controls(QPainter& painter) const {
    const auto rect = editor_visual_rect();
    if (rect.isEmpty()) {
      return;
    }

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(99, 168, 255), 1.0, Qt::DashLine));
    painter.drawPolygon(map_editor_rect_to_overlay(rect));

    painter.setPen(QPen(QColor(35, 38, 44), 1.0));
    painter.setBrush(QColor(245, 248, 252));
    for (const auto handle : kResizeHandles) {
      painter.drawRect(resize_handle_rect(handle));
    }
  }

  [[nodiscard]] std::optional<ResizeGeometry> resize_geometry_for_event(QMouseEvent* event) const {
    if (canvas_ == nullptr || editor_ == nullptr || !resize_drag_.has_value()) {
      return std::nullopt;
    }

    const auto canvas_point = QPointF(canvas_->mapFromGlobal(event->globalPosition().toPoint()));
    const auto current_editor_point = map_canvas_point_to_editor(canvas_point, resize_drag_->start_transform);
    if (!current_editor_point.has_value()) {
      return std::nullopt;
    }

    const auto current_zoom = zoom();
    const QPointF delta((current_editor_point->x() - resize_drag_->start_editor_point.x()) / current_zoom,
                        (current_editor_point->y() - resize_drag_->start_editor_point.y()) / current_zoom);
    double left = 0.0;
    double top = 0.0;
    double right = static_cast<double>(resize_drag_->start_width);
    double bottom = static_cast<double>(resize_drag_->start_height);

    switch (resize_drag_->handle) {
      case ResizeHandle::TopLeft:
        left = std::min(right - kMinimumTextBoxDocumentSize, delta.x());
        top = std::min(bottom - kMinimumTextBoxDocumentSize, delta.y());
        break;
      case ResizeHandle::TopRight:
        right = std::max(left + kMinimumTextBoxDocumentSize,
                         static_cast<double>(resize_drag_->start_width) + delta.x());
        top = std::min(bottom - kMinimumTextBoxDocumentSize, delta.y());
        break;
      case ResizeHandle::BottomLeft:
        left = std::min(right - kMinimumTextBoxDocumentSize, delta.x());
        bottom = std::max(top + kMinimumTextBoxDocumentSize,
                          static_cast<double>(resize_drag_->start_height) + delta.y());
        break;
      case ResizeHandle::BottomRight:
        right = std::max(left + kMinimumTextBoxDocumentSize,
                         static_cast<double>(resize_drag_->start_width) + delta.x());
        bottom = std::max(top + kMinimumTextBoxDocumentSize,
                          static_cast<double>(resize_drag_->start_height) + delta.y());
        break;
      case ResizeHandle::None:
        return std::nullopt;
    }

    const auto new_origin = resize_drag_->start_transform.map(QPointF(left, top));
    QTransform adjusted(resize_drag_->start_transform.m11(),
                        resize_drag_->start_transform.m12(),
                        resize_drag_->start_transform.m21(),
                        resize_drag_->start_transform.m22(),
                        new_origin.x(),
                        new_origin.y());
    return ResizeGeometry{adjusted,
                          std::max(kMinimumTextBoxDocumentSize, static_cast<int>(std::round(right - left))),
                          std::max(kMinimumTextBoxDocumentSize, static_cast<int>(std::round(bottom - top)))};
  }

  void resize_text_box(QMouseEvent* event) {
    const auto geometry = resize_geometry_for_event(event);
    if (!geometry.has_value() || editor_ == nullptr) {
      return;
    }
    resize_preview_transform_ = geometry->transform;
    resize_preview_document_size_ = QSizeF(geometry->width, geometry->height);

    const auto& adjusted = geometry->transform;
    const LayerAffineTransform affine{adjusted.m11(), adjusted.m12(), adjusted.m21(),
                                      adjusted.m22(), adjusted.dx(),  adjusted.dy()};
    editor_->setProperty(kTextEditorTransformOverrideProperty,
                         QString::fromStdString(serialize_layer_affine_transform(affine)));
    editor_->setProperty(kTextEditorSourceVisibleAnchorProperty, QVariant());
    editor_->setProperty(kTextEditorSourceVisibleSizeProperty, QVariant());
    editor_->setProperty(kTextEditorVisibleLocalRectProperty, QVariant());
    // Box geometry changed: drop the import-frame render rect so glyphs lay out against the live
    // box instead of the original Photoshop frame (free helper isn't visible inside this class).
    editor_->setProperty(kTextEditorRenderLocalRectProperty, QVariant());
    editor_->setProperty("patchy.documentTextX", static_cast<int>(std::floor(adjusted.dx())));
    editor_->setProperty("patchy.documentTextY", static_cast<int>(std::floor(adjusted.dy())));
    editor_->setProperty("patchy.documentTextFlow", QString::fromLatin1(kTextFlowBox));
    editor_->setProperty("patchy.documentTextWidth", geometry->width);
    editor_->setProperty("patchy.documentTextHeight", geometry->height);
    sync_geometry();
  }

  void schedule_resize_callback() {
    if (resize_callback_pending_ || editor_ == nullptr || !resize_callback_) {
      return;
    }
    resize_callback_pending_ = true;
    QTimer::singleShot(0, this, [this, editor = QPointer<QTextEdit>(editor_)] {
      resize_callback_pending_ = false;
      if (editor != nullptr && resize_callback_) {
        resize_callback_(editor);
      }
    });
  }

  CanvasWidget* canvas_{nullptr};
  QPointer<QTextEdit> editor_;
  QTransform text_transform_;
  std::function<void(QTextEdit*)> resize_callback_;
  bool selecting_{false};
  bool resize_callback_pending_{false};
  std::optional<QTransform> resize_preview_transform_;
  std::optional<QSizeF> resize_preview_document_size_;
  std::optional<ResizeDragState> resize_drag_;
  int selection_anchor_{0};
  QElapsedTimer caret_blink_clock_;
  QTimer caret_blink_timer_;
};

TransformedTextEditOverlay* transformed_text_edit_overlay_for_canvas(CanvasWidget* canvas) {
  if (canvas == nullptr) {
    return nullptr;
  }
  auto* widget = canvas->findChild<QWidget*>(QString::fromLatin1(kTransformedTextEditOverlayObjectName),
                                             Qt::FindDirectChildrenOnly);
  return widget == nullptr ? nullptr : static_cast<TransformedTextEditOverlay*>(widget);
}

QString document_html_for_editor(const QString& document_html, const QFont& editor_font, double zoom) {
  QTextDocument document;
  document.setDocumentMargin(0);
  document.setDefaultFont(editor_font);
  document.setHtml(document_html);
  scale_document_font_sizes(document, zoom);
  return document.toHtml();
}

QString document_html_from_editor(const QTextEdit& editor, double zoom) {
  return normalized_rich_text_html(*document_from_editor_in_document_units(editor, zoom));
}

QString rich_text_runs_from_document(const QTextDocument& document, const TextToolSettings& fallback,
                                     QColor fallback_color) {
  QStringList lines;
  lines << QStringLiteral("v1");
  bool includes_leading = false;
  bool photoshop_layout = false;
  const auto fallback_family = fallback.family.isEmpty() ? QApplication::font().family() : fallback.family;
  const auto fallback_size = std::max(1, fallback.size);
  const auto fallback_color_name = (fallback_color.isValid() ? fallback_color : QColor(Qt::black)).name(QColor::HexRgb);

  struct SerializedRun {
    int start{0};
    int length{0};
    double size{1.0};
    bool bold{false};
    bool italic{false};
    QString color;
    QString family;
    bool auto_leading{false};
    double leading{0.0};
    double tracking{0.0};
    double horizontal_scale{1.0};
    double vertical_scale{1.0};
  };
  std::vector<SerializedRun> collected;

  const auto append_run = [&collected, &includes_leading, &photoshop_layout, &fallback_family, fallback_size,
                           &fallback_color_name](int start, int length, const QTextCharFormat& format) {
    if (length <= 0) {
      return;
    }
    auto format_font = format.font();
    auto family = text_display_family_from_format(format, fallback_family);
    int size = format_font.pixelSize();
    if (size <= 0) {
      size = format_font.pointSizeF() > 0.0 ? static_cast<int>(std::round(format_font.pointSizeF())) : fallback_size;
    }
    QColor color = format.foreground().color();
    if (!color.isValid()) {
      color = QColor(fallback_color_name);
    }
    SerializedRun run;
    run.start = start;
    run.length = length;
    if (format.hasProperty(kTextHorizontalScaleFormatProperty)) {
      const auto value = format.property(kTextHorizontalScaleFormatProperty).toDouble();
      if (std::isfinite(value) && value > 0.01 && value < 100.0) {
        run.horizontal_scale = value;
      }
    }
    if (format.hasProperty(kTextVerticalScaleFormatProperty)) {
      const auto value = format.property(kTextVerticalScaleFormatProperty).toDouble();
      if (std::isfinite(value) && value > 0.01 && value < 100.0) {
        run.vertical_scale = value;
      }
    }
    // The serialized size is the FontSize basis; the font's pixel size folds the vertical
    // glyph scale in, so divide it back out when no exact value survives.
    run.size = std::max(1, size) / run.vertical_scale;
    // The exact (fractional) size wins while it still agrees with the int pixel size; a UI
    // size change only touches the pixel size, which orphans the stale exact value.
    if (format.hasProperty(kTextExactSizeFormatProperty)) {
      const auto exact = format.property(kTextExactSizeFormatProperty).toDouble();
      if (std::isfinite(exact) && exact > 0.0 &&
          static_cast<int>(std::lround(exact * run.vertical_scale)) == std::max(1, size)) {
        run.size = exact;
        photoshop_layout = true;
      }
    }
    run.bold = format_font.weight() >= QFont::Bold;
    run.italic = format_font.italic();
    run.color = color.name(QColor::HexRgb);
    run.family = QString::fromLatin1(family.toUtf8().toPercentEncoding());
    run.auto_leading = format.hasProperty(kTextAutoLeadingFormatProperty) &&
                       format.property(kTextAutoLeadingFormatProperty).toBool();
    const auto leading = format.hasProperty(kTextLeadingFormatProperty)
                             ? format.property(kTextLeadingFormatProperty).toDouble()
                             : 0.0;
    if (!run.auto_leading && std::isfinite(leading) && leading > 0.0) {
      run.leading = leading;
      includes_leading = true;
    }
    if (format.hasProperty(kTextTrackingFormatProperty)) {
      const auto tracking = format.property(kTextTrackingFormatProperty).toDouble();
      if (std::isfinite(tracking) && std::abs(tracking) > 0.0001 && std::abs(tracking) < 10000.0) {
        run.tracking = tracking;
      }
    }
    photoshop_layout = photoshop_layout || run.auto_leading || std::abs(run.tracking) > 0.0001 ||
                       std::abs(run.horizontal_scale - 1.0) > 0.0001 ||
                       std::abs(run.vertical_scale - 1.0) > 0.0001;
    collected.push_back(std::move(run));
  };

  auto fallback_format = [&fallback_family, fallback_size, &fallback_color_name, &fallback]() {
    QTextCharFormat format;
    auto font = render_text_font_for_display_family(fallback_family, fallback_size, fallback.bold,
                                                    fallback.italic, fallback.anti_alias);
    format.setFont(font);
    set_text_display_family(format, fallback_family);
    format.setForeground(QBrush(QColor(fallback_color_name)));
    return format;
  }();

  bool found_run = false;
  QTextCharFormat last_format = fallback_format;
  const int plain_length = static_cast<int>(document.toPlainText().size());
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    bool block_has_fragment = false;
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      block_has_fragment = true;
      found_run = true;
      last_format = fragment.charFormat();
      append_run(fragment.position(), fragment.length(), fragment.charFormat());
    }
    if (block.next().isValid()) {
      const auto separator_position = block.position() + block.length() - 1;
      if (separator_position >= 0 && separator_position < plain_length) {
        const auto separator_format = block_has_fragment ? last_format
                                                         : (block.charFormat().isValid() ? block.charFormat()
                                                                                         : last_format);
        found_run = true;
        append_run(separator_position, 1, separator_format);
        last_format = separator_format;
      }
    }
  }

  if (!found_run) {
    append_run(0, document.toPlainText().size(), fallback_format);
  }
  if (photoshop_layout) {
    lines[0] = QStringLiteral("v3");
  } else if (includes_leading) {
    lines[0] = QStringLiteral("v2");
  }
  for (const auto& run : collected) {
    auto line = QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7")
                    .arg(run.start)
                    .arg(run.length)
                    .arg(photoshop_layout ? QString::number(run.size, 'g', 17)
                                          : QString::number(static_cast<int>(std::lround(run.size))))
                    .arg(run.bold ? 1 : 0)
                    .arg(run.italic ? 1 : 0)
                    .arg(run.color)
                    .arg(run.family);
    if (photoshop_layout) {
      // v3 leading column: fixed value or the literal "auto" (paragraph fraction x size).
      line += QStringLiteral("\t%1").arg(
          run.auto_leading || run.leading <= 0.0 ? QStringLiteral("auto")
                                                 : QString::number(run.leading, 'g', 17));
      line += QStringLiteral("\t%1").arg(QString::number(run.tracking, 'g', 17));
      line += QStringLiteral("\t%1").arg(QString::number(run.horizontal_scale, 'g', 17));
      line += QStringLiteral("\t%1").arg(QString::number(run.vertical_scale, 'g', 17));
    } else if (includes_leading) {
      line += QStringLiteral("\t%1").arg(QString::number(run.leading, 'g', 17));
    }
    lines << line;
  }
  return lines.join(QLatin1Char('\n'));
}

QString paragraph_runs_from_document(const QTextDocument& document) {
  struct ParagraphRunLine {
    int start{0};
    int length{0};
    QString alignment;
    double first_line_indent{0.0};
    double start_indent{0.0};
    double end_indent{0.0};
    double space_before{0.0};
    double space_after{0.0};
    double auto_lead_fraction{1.2};
  };
  const auto normalized_metric = [](double value) {
    return std::isfinite(value) && std::abs(value) >= 0.0001 ? value : 0.0;
  };
  const auto metric_text = [](double value) {
    value = std::isfinite(value) && std::abs(value) >= 0.0001 ? value : 0.0;
    return QString::number(value, 'g', 17);
  };

  std::vector<ParagraphRunLine> run_lines;
  bool include_layout = false;
  bool include_fraction = false;
  const int plain_length = static_cast<int>(document.toPlainText().size());
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    const auto start = std::clamp(block.position(), 0, std::max(0, plain_length));
    const auto length = std::max(0, std::min(block.length(), std::max(0, plain_length - start)));
    const auto format = block.blockFormat();
    ParagraphRunLine run;
    run.start = start;
    run.length = length;
    run.alignment = paragraph_alignment_name(format.alignment());
    run.first_line_indent = normalized_metric(format.textIndent());
    run.start_indent = normalized_metric(format.leftMargin());
    run.end_indent = normalized_metric(format.rightMargin());
    run.space_before = normalized_metric(format.topMargin());
    run.space_after = normalized_metric(format.bottomMargin());
    if (format.hasProperty(kTextBlockAutoLeadFractionProperty)) {
      const auto fraction = format.property(kTextBlockAutoLeadFractionProperty).toDouble();
      if (std::isfinite(fraction) && fraction > 0.01 && fraction < 10.0) {
        run.auto_lead_fraction = fraction;
        include_fraction = include_fraction || std::abs(fraction - 1.2) > 0.0001;
      }
    }
    include_layout = include_layout || run.first_line_indent != 0.0 || run.start_indent != 0.0 ||
                     run.end_indent != 0.0 || run.space_before != 0.0 || run.space_after != 0.0;
    run_lines.push_back(std::move(run));
  }
  include_layout = include_layout || include_fraction;

  QStringList lines;
  lines << (include_fraction ? QStringLiteral("v3")
                             : (include_layout ? QStringLiteral("v2") : QStringLiteral("v1")));
  for (const auto& run : run_lines) {
    auto line = QStringLiteral("%1\t%2\t%3").arg(run.start).arg(run.length).arg(run.alignment);
    if (include_layout) {
      line += QStringLiteral("\t%1\t%2\t%3\t%4\t%5")
                  .arg(metric_text(run.first_line_indent))
                  .arg(metric_text(run.start_indent))
                  .arg(metric_text(run.end_indent))
                  .arg(metric_text(run.space_before))
                  .arg(metric_text(run.space_after));
    }
    if (include_fraction) {
      line += QStringLiteral("\t%1").arg(QString::number(run.auto_lead_fraction, 'g', 17));
    }
    lines << line;
  }
  return lines.join(QLatin1Char('\n'));
}

void apply_paragraph_runs_to_document(QTextDocument& document, const QString& paragraph_runs, double scale = 1.0) {
  const auto lines = paragraph_runs.split(QLatin1Char('\n'));
  const int plain_length = static_cast<int>(document.toPlainText().size());
  const auto metric_value = [scale](const QString& field) {
    bool ok = false;
    const auto value = field.toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
      return 0.0;
    }
    return value * std::max(0.0, scale);
  };
  for (const auto& raw_line : lines) {
    const auto line = raw_line.trimmed();
    if (line.isEmpty() || line == QStringLiteral("v1") || line == QStringLiteral("v2") ||
        line == QStringLiteral("v3")) {
      continue;
    }
    const auto fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 3) {
      continue;
    }
    bool start_ok = false;
    bool length_ok = false;
    const auto start = std::clamp(fields[0].toInt(&start_ok), 0, std::max(0, plain_length));
    const auto length = std::max(0, fields[1].toInt(&length_ok));
    if (!start_ok || !length_ok || start > plain_length) {
      continue;
    }
    QTextCursor cursor(&document);
    cursor.setPosition(start);
    cursor.setPosition(std::min(plain_length, start + std::max(1, length)), QTextCursor::KeepAnchor);
    QTextBlockFormat format;
    format.setAlignment(paragraph_alignment_from_name(fields[2]));
    if (fields.size() >= 8) {
      format.setTextIndent(metric_value(fields[3]));
      format.setLeftMargin(metric_value(fields[4]));
      format.setRightMargin(metric_value(fields[5]));
      format.setTopMargin(metric_value(fields[6]));
      format.setBottomMargin(metric_value(fields[7]));
    }
    if (fields.size() >= 9) {
      bool fraction_ok = false;
      const auto fraction = fields[8].toDouble(&fraction_ok);
      if (fraction_ok && std::isfinite(fraction) && fraction > 0.01 && fraction < 10.0) {
        // Unscaled: the auto-leading fraction multiplies each run's own size at layout time.
        format.setProperty(kTextBlockAutoLeadFractionProperty, fraction);
      }
    }
    cursor.mergeBlockFormat(format);
  }
}

void apply_patchy_text_runs_to_document(QTextDocument& document, const QString& runs_text, const QFont& fallback_font,
                                        QColor fallback_color, double scale, int anti_alias) {
  const auto lines = runs_text.split(QLatin1Char('\n'));
  const int plain_length = static_cast<int>(document.toPlainText().size());
  bool applied_run = false;
  for (const auto& raw_line : lines) {
    const auto line = raw_line.trimmed();
    if (line.isEmpty() || line == QStringLiteral("v1") || line == QStringLiteral("v2") ||
        line == QStringLiteral("v3")) {
      continue;
    }
    const auto fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 7) {
      continue;
    }
    bool start_ok = false;
    bool length_ok = false;
    bool size_ok = false;
    const auto start = std::clamp(fields[0].toInt(&start_ok), 0, std::max(0, plain_length));
    const auto length = std::max(0, fields[1].toInt(&length_ok));
    const auto exact_document_size = std::max(1.0, fields[2].toDouble(&size_ok));
    const auto document_size = std::max(1, static_cast<int>(std::lround(exact_document_size)));
    if (!start_ok || !length_ok || !size_ok || length <= 0 || start >= plain_length) {
      continue;
    }

    // Character-panel glyph scales (v3 fields 10/11): glyph height x vertical, width x
    // horizontal. Read before the font so the pixel size can fold the vertical scale in.
    double horizontal_glyph_scale = 1.0;
    double vertical_glyph_scale = 1.0;
    if (fields.size() >= 10) {
      bool scale_ok = false;
      const auto value = fields[9].toDouble(&scale_ok);
      if (scale_ok && std::isfinite(value) && value > 0.01 && value < 100.0) {
        horizontal_glyph_scale = value;
      }
    }
    if (fields.size() >= 11) {
      bool scale_ok = false;
      const auto value = fields[10].toDouble(&scale_ok);
      if (scale_ok && std::isfinite(value) && value > 0.01 && value < 100.0) {
        vertical_glyph_scale = value;
      }
    }

    auto family = QString::fromUtf8(QByteArray::fromPercentEncoding(fields[6].toLatin1()));
    if (family.trimmed().isEmpty()) {
      family = display_text_family_from_font(fallback_font);
    } else {
      family = canonical_text_display_family(family);
    }
    auto font = render_text_font_for_display_family(
        family, std::max(1, static_cast<int>(std::round(exact_document_size * vertical_glyph_scale * scale))),
        fields[3].toInt() != 0, fields[4].toInt() != 0, anti_alias);
    // Photoshop scales glyph width by H and height by V; Qt's pixel size scales both, so the
    // stretch carries the width-to-height ratio.
    if (std::abs(horizontal_glyph_scale - vertical_glyph_scale) > 0.0001) {
      font.setStretch(std::clamp(
          static_cast<int>(std::lround(horizontal_glyph_scale / vertical_glyph_scale * 100.0)), 1, 400));
    }

    QColor color(fields[5]);
    if (!color.isValid()) {
      color = fallback_color.isValid() ? fallback_color : QColor(Qt::black);
    }

    QTextCharFormat format;
    format.setFont(font);
    set_text_display_family(format, family);
    format.setForeground(QBrush(color));
    const auto scaled_exact_size = exact_document_size * std::max(0.0, scale);
    if (std::abs(exact_document_size - document_size) > 0.0001 ||
        std::abs(vertical_glyph_scale - 1.0) > 0.0001) {
      // The exact size excludes the vertical glyph scale: it is the leading/tracking basis
      // (FontSize), while the font's pixel size above folds V in.
      format.setProperty(kTextExactSizeFormatProperty, scaled_exact_size);
    }
    if (std::abs(horizontal_glyph_scale - 1.0) > 0.0001) {
      format.setProperty(kTextHorizontalScaleFormatProperty, horizontal_glyph_scale);
    }
    if (std::abs(vertical_glyph_scale - 1.0) > 0.0001) {
      format.setProperty(kTextVerticalScaleFormatProperty, vertical_glyph_scale);
    }
    if (fields.size() >= 8) {
      if (fields[7] == QStringLiteral("auto")) {
        format.setProperty(kTextAutoLeadingFormatProperty, true);
      } else {
        bool leading_ok = false;
        const auto leading = fields[7].toDouble(&leading_ok);
        if (leading_ok && std::isfinite(leading) && leading > 0.0) {
          format.setProperty(kTextLeadingFormatProperty, leading * std::max(0.0, scale));
        }
      }
    }
    if (fields.size() >= 9) {
      bool tracking_ok = false;
      const auto tracking = fields[8].toDouble(&tracking_ok);
      if (tracking_ok && std::isfinite(tracking) && std::abs(tracking) > 0.0001 && std::abs(tracking) < 10000.0) {
        // Photoshop tracking: 1/1000 em per inter-glyph gap, applied as absolute letter
        // spacing; the horizontal glyph scale multiplies the whole advance, tracking included.
        format.setProperty(kTextTrackingFormatProperty, tracking);
        format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        format.setFontLetterSpacing(tracking / 1000.0 * scaled_exact_size * horizontal_glyph_scale);
      }
    }
    QTextCursor cursor(&document);
    cursor.setPosition(start);
    cursor.setPosition(std::min(plain_length, start + length), QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(format);
    applied_run = true;
  }

  if (!applied_run && plain_length > 0) {
    auto font = fallback_font;
    scale_font_size(font, scale);
    configure_text_font_smoothing(font, anti_alias);
    QTextCharFormat format;
    format.setFont(font);
    set_text_display_family(format, display_text_family_from_font(font));
    format.setForeground(QBrush(fallback_color.isValid() ? fallback_color : QColor(Qt::black)));
    QTextCursor cursor(&document);
    cursor.select(QTextCursor::Document);
    cursor.mergeCharFormat(format);
  }
}

QString document_html_from_text_runs(const QString& text, const QString& rich_text_runs,
                                     const TextToolSettings& fallback, QColor fallback_color) {
  const auto fallback_family = fallback.family.isEmpty() ? QApplication::font().family() : fallback.family;
  auto fallback_font = render_text_font_for_display_family(fallback_family, std::max(1, fallback.size),
                                                          fallback.bold, fallback.italic, fallback.anti_alias);

  QTextDocument document;
  document.setDocumentMargin(0);
  document.setDefaultFont(fallback_font);
  document.setPlainText(text);
  apply_patchy_text_runs_to_document(document, rich_text_runs, fallback_font, fallback_color, 1.0,
                                     fallback.anti_alias);
  apply_text_smoothing_to_document(document, fallback.anti_alias);
  return normalized_rich_text_html(document);
}

void apply_plain_text_format(QTextDocument& document, const QFont& font, QColor color) {
  QTextCursor cursor(&document);
  cursor.select(QTextCursor::Document);
  QTextCharFormat format;
  format.setFont(font);
  set_text_display_family(format, display_text_family_from_font(font));
  format.setForeground(QBrush(color));
  cursor.mergeCharFormat(format);
}

// Re-resolve character formats whose primary family is not an available family but matches a
// family + style pair (see available_text_family_style_match).  HTML round-trips drop the style
// name, so documents built from settings.html would otherwise fall back to a regular-weight face
// on platforms whose font database splits GDI-style names.
void resolve_document_font_styles(QTextDocument& document) {
  struct StylePatch {
    int position{0};
    int length{0};
    QFont font;
  };
  std::vector<StylePatch> patches;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      const auto format = fragment.charFormat();
      const auto families = format.font().families();
      if (families.isEmpty()) {
        continue;
      }
      const auto& primary = families.front();
      if (available_text_family_match(primary).has_value()) {
        continue;
      }
      const auto match = available_text_family_style_match(primary);
      if (!match.has_value()) {
        continue;
      }
      auto font = format.font();
      font.setFamilies(QStringList{match->family});
      font.setStyleName(match->style);
      patches.push_back(StylePatch{fragment.position(), fragment.length(), std::move(font)});
    }
  }
  for (const auto& patch : patches) {
    QTextCharFormat format;
    format.setFont(patch.font);
    QTextCursor cursor(&document);
    cursor.setPosition(patch.position);
    cursor.setPosition(patch.position + patch.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(format);
  }
}

QTextCharFormat text_editor_typing_format(const QFont& font, QColor color) {
  QTextCharFormat format;
  format.setFont(font);
  set_text_display_family(format, display_text_family_from_font(font));
  format.setForeground(QBrush(color.isValid() ? color : QColor(Qt::black)));
  return format;
}

QTextCharFormat text_editor_reference_format(const QTextEdit& editor) {
  auto cursor = editor.textCursor();
  if (cursor.hasSelection()) {
    const auto start = std::min(cursor.selectionStart(), cursor.selectionEnd());
    const auto end = std::max(cursor.selectionStart(), cursor.selectionEnd());
    cursor.setPosition(start);
    if (start < end) {
      cursor.setPosition(start + 1, QTextCursor::KeepAnchor);
    }
  }
  return cursor.charFormat();
}

std::optional<double> text_leading_from_document_formats(const QTextDocument& document) {
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      const auto format = fragment.charFormat();
      if (!format.hasProperty(kTextLeadingFormatProperty)) {
        continue;
      }
      const auto leading = format.property(kTextLeadingFormatProperty).toDouble();
      if (std::isfinite(leading) && leading > 0.0) {
        return leading;
      }
    }
  }
  return std::nullopt;
}

int document_text_size_from_editor_format(const QTextCharFormat& format, double zoom, int fallback) noexcept {
  const auto font = format.font();
  int editor_pixel_size = font.pixelSize();
  if (editor_pixel_size <= 0 && font.pointSizeF() > 0.0) {
    editor_pixel_size = static_cast<int>(std::round(font.pointSizeF()));
  }
  if (editor_pixel_size <= 0) {
    return std::max(1, fallback);
  }
  return std::max(1, static_cast<int>(std::round(static_cast<double>(editor_pixel_size) / std::max(0.001, zoom))));
}

std::optional<QColor> text_color_from_format(const QTextCharFormat& format) {
  if (format.foreground().style() == Qt::NoBrush) {
    return std::nullopt;
  }
  const auto color = format.foreground().color();
  if (!color.isValid()) {
    return std::nullopt;
  }
  return QColor(color.red(), color.green(), color.blue());
}

void merge_text_char_format(QTextEdit& editor, const QTextCharFormat& format) {
  if (editor.toPlainText().isEmpty()) {
    auto current = editor.currentCharFormat();
    current.merge(format);
    editor.setCurrentCharFormat(current);
    return;
  }

  auto cursor = editor.textCursor();
  if (cursor.hasSelection()) {
    cursor.mergeCharFormat(format);
    editor.setTextCursor(cursor);
    return;
  }

  editor.mergeCurrentCharFormat(format);
}

void preserve_text_editor_typing_format(QTextEdit& editor, const QFont& font, QColor color) {
  const auto block = editor.textCursor().block();
  const auto empty_document = editor.toPlainText().isEmpty();
  if (!empty_document && editor.textCursor().hasSelection()) {
    return;
  }
  const auto should_preserve_format = empty_document || (block.isValid() && block.text().isEmpty());
  if (!should_preserve_format) {
    return;
  }

  editor.document()->setDefaultFont(font);
  editor.setCurrentCharFormat(text_editor_typing_format(font, color));
  editor.viewport()->update();
}

struct RenderedTextPixels {
  PixelBuffer pixels;
  QRectF local_rect;
};

struct BoxTextLineRenderItem {
  QTextLine line;
  QPointF block_origin;
  QRectF clip_rect;
};

struct BoxTextRenderPlan {
  QRectF local_rect;
  std::vector<BoxTextLineRenderItem> lines;
};

std::vector<BoxTextLineRenderItem> boxed_text_line_render_items(const QTextDocument& document, QRectF gate_rect,
                                                                qreal top_bleed, qreal bottom_bleed,
                                                                qreal horizontal_bleed) {
  gate_rect = gate_rect.normalized();
  std::vector<BoxTextLineRenderItem> items;
  if (!std::isfinite(gate_rect.left()) || !std::isfinite(gate_rect.top()) ||
      !std::isfinite(gate_rect.right()) || !std::isfinite(gate_rect.bottom()) ||
      gate_rect.width() <= 0.0 || gate_rect.height() <= 0.0) {
    return items;
  }

  const auto* layout = document.documentLayout();
  if (layout == nullptr) {
    return items;
  }

  constexpr qreal kLineGateTolerance = 0.01;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    auto* text_layout = block.layout();
    if (text_layout == nullptr) {
      continue;
    }
    const auto block_rect = layout->blockBoundingRect(block);
    for (int i = 0; i < text_layout->lineCount(); ++i) {
      const auto line = text_layout->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const auto line_rect = line.rect().translated(block_rect.topLeft());
      const auto line_top = line_rect.top();
      const auto line_bottom = line_rect.bottom();
      if (!std::isfinite(line_top) || !std::isfinite(line_bottom)) {
        continue;
      }
      if (line_top >= gate_rect.bottom() - kLineGateTolerance ||
          line_bottom <= gate_rect.top() - kLineGateTolerance) {
        continue;
      }
      items.push_back(BoxTextLineRenderItem{
          line,
          block_rect.topLeft(),
          QRectF(gate_rect.left() - horizontal_bleed,
                 line_top - top_bleed,
                 gate_rect.width() + horizontal_bleed * 2.0,
                 std::max<qreal>(1.0, line_rect.height() + top_bleed + bottom_bleed))});
    }
  }
  return items;
}

BoxTextRenderPlan boxed_text_render_plan(const QTextDocument& document, const QFont& font, QRectF frame_rect,
                                         std::optional<QRectF> requested_local_rect) {
  frame_rect = frame_rect.normalized();
  QRectF gate_rect = frame_rect;
  if (requested_local_rect.has_value()) {
    gate_rect = gate_rect.united(requested_local_rect->normalized());
  }

  const QFontMetricsF metrics(font);
  const auto top_bleed = 2.0;
  const auto bottom_bleed =
      std::max<qreal>(2.0, std::ceil(std::max<qreal>(metrics.descent(), metrics.leading())) + 2.0);
  constexpr qreal kHorizontalBleed = 2.0;

  BoxTextRenderPlan plan{gate_rect, boxed_text_line_render_items(document, gate_rect, top_bleed, bottom_bleed,
                                                                 kHorizontalBleed)};
  if (plan.lines.empty()) {
    return plan;
  }
  for (const auto& item : plan.lines) {
    plan.local_rect = plan.local_rect.united(item.clip_rect);
  }
  return plan;
}

// OS/2 sTypoAscender as a fraction of the em. Photoshop positions the first baseline of box
// (paragraph) text at typoAscender x size below the box top (COM-calibrated against PS 2026:
// Arial/Times/Verdana/Courier probes land within ~1% of typoAscender; Qt's ascent() is the
// much larger usWinAscent and would sit the first line visibly too low).
double typographic_ascent_fraction(const QFont& font) {
  static QHash<QString, double> cache;
  static QMutex cache_mutex;
  const auto key = font.families().join(QLatin1Char('|')) + QLatin1Char('#') + font.styleName() +
                   QLatin1Char('#') + QString::number(font.weight()) + (font.italic() ? QLatin1String("i") : QLatin1String("r"));
  {
    QMutexLocker lock(&cache_mutex);
    if (const auto found = cache.constFind(key); found != cache.constEnd()) {
      return found.value();
    }
  }
  double fraction = 0.0;
  const auto raw_font = QRawFont::fromFont(font);
  if (raw_font.isValid()) {
    const auto table = raw_font.fontTable("OS/2");
    const auto upem = raw_font.unitsPerEm();
    if (table.size() >= 70 && upem > 0.0) {
      const auto* bytes = reinterpret_cast<const unsigned char*>(table.constData());
      const auto ascender = static_cast<qint16>(static_cast<quint16>((bytes[68] << 8) | bytes[69]));
      if (ascender > 0) {
        fraction = static_cast<double>(ascender) / upem;
      }
    }
  }
  if (fraction <= 0.0 || fraction > 2.0) {
    const QFontMetricsF metrics(font);
    const auto pixel_size = font.pixelSize() > 0 ? static_cast<double>(font.pixelSize())
                                                 : std::max(1.0, metrics.height());
    fraction = std::clamp(metrics.ascent() / pixel_size, 0.5, 1.2);
  }
  QMutexLocker lock(&cache_mutex);
  cache.insert(key, fraction);
  return fraction;
}

// The fractional font size a Photoshop-layout char format contributes to leading math.
double photoshop_char_exact_size(const QTextCharFormat& format) {
  if (format.hasProperty(kTextExactSizeFormatProperty)) {
    const auto exact = format.property(kTextExactSizeFormatProperty).toDouble();
    if (std::isfinite(exact) && exact > 0.0) {
      return exact;
    }
  }
  const auto font = format.font();
  if (font.pixelSize() > 0) {
    return font.pixelSize();
  }
  if (font.pointSizeF() > 0.0) {
    return font.pointSizeF();
  }
  return 12.0;
}

// Photoshop effective leading of one char format: the fixed value, or auto leading =
// paragraph auto-leading fraction x font size.
double photoshop_char_leading(const QTextCharFormat& format, double paragraph_fraction) {
  const bool auto_leading = format.hasProperty(kTextAutoLeadingFormatProperty) &&
                            format.property(kTextAutoLeadingFormatProperty).toBool();
  if (!auto_leading && format.hasProperty(kTextLeadingFormatProperty)) {
    const auto fixed = format.property(kTextLeadingFormatProperty).toDouble();
    if (std::isfinite(fixed) && fixed > 0.0) {
      return fixed;
    }
  }
  return paragraph_fraction * photoshop_char_exact_size(format);
}

struct PhotoshopLineMetrics {
  double leading{0.0};      // max effective leading among the line's chars
  double first_baseline{0.0};  // box text: max typoAscender x size among the line's chars
};

// Char formats intersecting one visual line, folded into the line's leading metrics. An empty
// line (blank paragraph) has no fragments and uses the block's char format.
PhotoshopLineMetrics photoshop_line_metrics(const QTextBlock& block, const QTextLine& line,
                                            double paragraph_fraction) {
  PhotoshopLineMetrics metrics;
  const auto line_start = block.position() + line.textStart();
  const auto line_end = line_start + std::max(1, line.textLength());
  bool found_format = false;
  for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
    const auto fragment = fragment_it.fragment();
    if (!fragment.isValid() || fragment.length() <= 0) {
      continue;
    }
    const auto fragment_start = fragment.position();
    const auto fragment_end = fragment_start + fragment.length();
    if (fragment_end <= line_start || fragment_start >= line_end) {
      continue;
    }
    const auto format = fragment.charFormat();
    metrics.leading = std::max(metrics.leading, photoshop_char_leading(format, paragraph_fraction));
    metrics.first_baseline =
        std::max(metrics.first_baseline, typographic_ascent_fraction(format.font()) * photoshop_char_exact_size(format));
    found_format = true;
  }
  if (!found_format) {
    const auto format = block.charFormat();
    metrics.leading = photoshop_char_leading(format, paragraph_fraction);
    metrics.first_baseline = typographic_ascent_fraction(format.font()) * photoshop_char_exact_size(format);
  }
  return metrics;
}

struct PhotoshopTextLayoutPlan {
  std::vector<BoxTextLineRenderItem> lines;
  QRectF ink_rect;  // union of the repositioned line rects (line-box based, pre-bleed)
  bool valid{false};
};

// Lay the document's lines out with Photoshop's leading model: the first line keeps Qt's
// natural position for point text (the anchor machinery aligns rasters by the first line) or
// sits typoAscender below the box top for box text; every following baseline advances by the
// *entered* line's max leading (auto = paragraph fraction x size) plus paragraph spacing.
// Line x positions stay Qt's own (alignment against the layout width).
PhotoshopTextLayoutPlan photoshop_text_layout_plan(const QTextDocument& document, bool boxed) {
  PhotoshopTextLayoutPlan plan;
  const auto* layout = document.documentLayout();
  if (layout == nullptr) {
    return plan;
  }

  bool first_line = true;
  double baseline = 0.0;
  double previous_space_after = 0.0;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    auto* text_layout = block.layout();
    if (text_layout == nullptr) {
      continue;
    }
    const auto block_format = block.blockFormat();
    const auto paragraph_fraction = [&block_format] {
      if (block_format.hasProperty(kTextBlockAutoLeadFractionProperty)) {
        const auto fraction = block_format.property(kTextBlockAutoLeadFractionProperty).toDouble();
        if (std::isfinite(fraction) && fraction > 0.01 && fraction < 10.0) {
          return fraction;
        }
      }
      return 1.2;
    }();
    const auto block_rect = layout->blockBoundingRect(block);
    for (int i = 0; i < text_layout->lineCount(); ++i) {
      const auto line = text_layout->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const auto metrics = photoshop_line_metrics(block, line, paragraph_fraction);
      const auto natural_rect = line.rect().translated(block_rect.topLeft());
      if (first_line) {
        if (boxed) {
          // Box text: first baseline = box top + paragraph space-before + typographic ascent.
          baseline = std::max(0.0, block_format.topMargin()) + metrics.first_baseline;
        } else {
          // Point text: keep Qt's own first line so raster anchoring stays put.
          baseline = natural_rect.top() + line.ascent();
        }
        first_line = false;
      } else {
        const auto space_before = i == 0 ? std::max(0.0, block_format.topMargin()) : 0.0;
        baseline += std::max(0.01, metrics.leading) + space_before + previous_space_after;
      }
      previous_space_after =
          i == text_layout->lineCount() - 1 ? std::max(0.0, block_format.bottomMargin()) : 0.0;

      const auto target_top = baseline - line.ascent();
      const auto offset_y = target_top - natural_rect.top();
      const auto block_origin = block_rect.topLeft() + QPointF(0.0, offset_y);
      plan.lines.push_back(BoxTextLineRenderItem{line, block_origin, QRectF()});
      plan.ink_rect = plan.ink_rect.isNull() ? natural_rect.translated(0.0, offset_y)
                                             : plan.ink_rect.united(natural_rect.translated(0.0, offset_y));
    }
  }
  plan.valid = !plan.lines.empty();
  return plan;
}

// Build the QTextDocument a text layer is rasterized from.  The caret/selection layout is built
// through this same function (build_text_editor_document_space_layout), so the painted glyphs and
// the caret geometry always come from one identical document -- any divergence in construction
// (formats, blank-line heights, wrapping width) shows up as the caret drifting off the text.
TextRenderDocument build_text_render_document(const TextToolSettings& settings, QColor color,
                                              std::int32_t max_width, const QString& paragraph_runs,
                                              const QString& rich_text_runs, double metric_scale,
                                              double layout_scale, double box_scale) {
  TextRenderDocument result;
  metric_scale = std::clamp(std::isfinite(metric_scale) ? metric_scale : 1.0, 0.5, 1.5);
  layout_scale = std::isfinite(layout_scale) && layout_scale > 0.01 ? layout_scale : 1.0;
  // Box dims scale separately from glyph sizes: a transform fold scales both (raw box, raw
  // runs), but a PSD-frame session's box is already in document space while its runs are raw.
  box_scale = std::isfinite(box_scale) && box_scale > 0.01 ? box_scale : 1.0;
  result.font = render_text_font_for_display_family(
      settings.family, std::max(1, static_cast<int>(std::lround(settings.size * layout_scale))), settings.bold,
      settings.italic, settings.anti_alias);
  scale_font_width(result.font, metric_scale);

  result.text_width = settings.boxed
                          ? std::max(kMinimumTextBoxDocumentSize,
                                     static_cast<int>(std::lround(settings.box_width * box_scale)))
                          : std::max(64, max_width);
  result.document = std::make_unique<QTextDocument>();
  auto& document = *result.document;
  document.setDocumentMargin(0);
  document.setDefaultFont(result.font);
  QTextOption option;
  option.setWrapMode(settings.boxed ? QTextOption::WordWrap : QTextOption::NoWrap);
  option.setUseDesignMetrics(true);
  document.setDefaultTextOption(option);
  if (!rich_text_runs.trimmed().isEmpty()) {
    // Build from the plain text + serialized runs -- the same construction the editor uses.  The
    // HTML round-trip below loses character formats on empty paragraphs (old PSDs separate lines
    // with control characters that import as blank paragraphs), which made the drawn text and the
    // selection highlights drift apart.
    document.setPlainText(settings.text);
    apply_patchy_text_runs_to_document(document, rich_text_runs, result.font, color, layout_scale,
                                       settings.anti_alias);
    scale_document_font_widths(document, metric_scale);
  } else if (!settings.html.trimmed().isEmpty()) {
    document.setHtml(settings.html);
    document.setDocumentMargin(0);
    document.setDefaultTextOption(option);
    // HTML carries family names only; re-resolve names that exist as family + style (e.g.
    // "Arial Black" on platforms whose database splits them) so the correct faces render.
    resolve_document_font_styles(document);
    scale_document_font_widths(document, metric_scale);
    scale_document_font_sizes(document, layout_scale);
  } else {
    document.setPlainText(settings.text);
    apply_plain_text_format(document, result.font, color);
  }
  // NoWrap point text must size to the tight glyph idealWidth.  Passing an explicit textWidth makes
  // Qt report THAT width from document.size().width() (not the ideal width) whenever it exceeds the
  // content, which -- when max_width is a layer's already-scaled pixel bounds -- inflated the rendered
  // image and, after the document transform re-scaled it, produced a free-transform rect several times
  // wider than the glyphs.  Use -1 (auto) for point text so size().width() is the ideal content width.
  document.setTextWidth(settings.boxed ? static_cast<qreal>(result.text_width) : -1.0);
  if (!paragraph_runs.trimmed().isEmpty()) {
    apply_paragraph_runs_to_document(document, paragraph_runs, layout_scale);
  }
  apply_text_smoothing_to_document(document, settings.anti_alias);
  if (!settings.boxed) {
    // Qt only honors paragraph alignment against a finite layout width; leaving point text
    // unconstrained (-1) silently lays every line out flush-left, so centered/right-justified
    // multi-line point text (Photoshop aligns each line around the type anchor) collapsed to
    // left-justified on commit.  The ideal width is the tight content width, so this keeps the
    // glyph-snug bounds the -1 form was chosen for while letting alignment position the lines.
    document.setTextWidth(document.idealWidth());
  }
  return result;
}

RenderedTextPixels render_text_pixels_with_local_rect(const TextToolSettings& settings, QColor color,
                                                      std::int32_t max_width,
                                                      const QString& paragraph_runs = QString(),
                                                      const QString& rich_text_runs = QString(),
                                                      std::optional<QRectF> requested_local_rect = std::nullopt,
                                                      double metric_scale = 1.0,
                                                      const QTransform& document_transform_in = QTransform(),
                                                      double layout_scale_in = 1.0) {
  // Photoshop-layout text rendered through a scaling transform folds the transform's vertical
  // scale into the glyph sizes (fractional engine sizes round to whole pixels AFTER scaling,
  // not before) and renders through the residual matrix. The returned rect is post-transform
  // either way, so callers see identical geometry with crisper, correctly-sized glyphs.
  // layout_scale_in additionally scales run sizes WITHOUT scaling box dims -- a PSD-frame box
  // session works in a document-space frame while its runs stay in raw engine units.
  QTransform document_transform = document_transform_in;
  double fold_scale = 1.0;
  if (settings.photoshop_layout && !document_transform_in.isIdentity()) {
    const auto vertical_scale = std::hypot(document_transform_in.m21(), document_transform_in.m22());
    if (std::isfinite(vertical_scale) && vertical_scale > 0.01 && std::abs(vertical_scale - 1.0) > 0.0001) {
      fold_scale = vertical_scale;
      document_transform =
          QTransform(document_transform_in.m11() / vertical_scale, document_transform_in.m12() / vertical_scale,
                     document_transform_in.m21() / vertical_scale, document_transform_in.m22() / vertical_scale,
                     document_transform_in.dx(), document_transform_in.dy());
    }
  }
  const double layout_scale =
      fold_scale * (std::isfinite(layout_scale_in) && layout_scale_in > 0.01 ? layout_scale_in : 1.0);
  auto built = build_text_render_document(settings, color, max_width, paragraph_runs, rich_text_runs,
                                          metric_scale, layout_scale, fold_scale);
  auto& document = *built.document;
  const auto& font = built.font;
  const auto text_width = built.text_width;

  const auto size = document.size();
  QRectF local_rect;
  std::vector<BoxTextLineRenderItem> line_render_items;
  PhotoshopTextLayoutPlan photoshop_plan;
  if (settings.photoshop_layout) {
    photoshop_plan = photoshop_text_layout_plan(document, settings.boxed);
  }
  if (settings.boxed) {
    local_rect = QRectF(0.0, 0.0, static_cast<qreal>(text_width),
                        static_cast<qreal>(std::max(
                            kMinimumTextBoxDocumentSize,
                            static_cast<int>(std::lround(settings.box_height * fold_scale)))));
    if (photoshop_plan.valid) {
      // Photoshop-model line positions, gated and clipped with the same bleed rules as the
      // native boxed plan (descenders paint past the line box; neighbours stay clipped).
      auto gate_rect = local_rect;
      if (requested_local_rect.has_value()) {
        gate_rect = gate_rect.united(requested_local_rect->normalized());
      }
      const QFontMetricsF metrics(font);
      const qreal top_bleed = 2.0;
      const qreal bottom_bleed =
          std::max<qreal>(2.0, std::ceil(std::max<qreal>(metrics.descent(), metrics.leading())) + 2.0);
      constexpr qreal kHorizontalBleed = 2.0;
      constexpr qreal kLineGateTolerance = 0.01;
      auto united_rect = gate_rect;
      for (auto& item : photoshop_plan.lines) {
        const auto positioned = item.line.rect().translated(item.block_origin);
        if (positioned.top() >= gate_rect.bottom() - kLineGateTolerance ||
            positioned.bottom() <= gate_rect.top() - kLineGateTolerance) {
          continue;
        }
        item.clip_rect = QRectF(gate_rect.left() - kHorizontalBleed, positioned.top() - top_bleed,
                                gate_rect.width() + kHorizontalBleed * 2.0,
                                std::max<qreal>(1.0, positioned.height() + top_bleed + bottom_bleed));
        united_rect = united_rect.united(item.clip_rect);
        line_render_items.push_back(item);
      }
      local_rect = united_rect;
    } else if (requested_local_rect.has_value()) {
      auto plan = boxed_text_render_plan(document, font, local_rect, requested_local_rect);
      local_rect = plan.local_rect;
      line_render_items = std::move(plan.lines);
    }
  } else if (photoshop_plan.valid) {
    // Point text under the Photoshop leading model: width still comes from the document's
    // ideal width, height and top from the repositioned lines (tight leading can push a
    // later line's ascender above the first line's top).
    const auto ink = photoshop_plan.ink_rect;
    const auto top = std::min<qreal>(0.0, std::floor(ink.top()));
    const auto bottom = std::max<qreal>(top + 1.0, std::ceil(ink.bottom()) + 2.0);
    local_rect = QRectF(0.0, top, std::max<qreal>(1.0, std::ceil(size.width()) + 2.0), bottom - top);
    line_render_items = std::move(photoshop_plan.lines);
    for (auto& item : line_render_items) {
      item.clip_rect = local_rect;
    }
  } else {
    local_rect = QRectF(0.0, 0.0, std::max<qreal>(1.0, std::ceil(size.width()) + 2.0),
                        std::max<qreal>(1.0, std::ceil(size.height()) + 2.0));
  }
  if (!std::isfinite(local_rect.left()) || !std::isfinite(local_rect.top()) ||
      !std::isfinite(local_rect.right()) || !std::isfinite(local_rect.bottom()) ||
      local_rect.width() <= 0.0 || local_rect.height() <= 0.0) {
    local_rect = QRectF(0.0, 0.0, static_cast<qreal>(std::max(1, text_width)),
                        static_cast<qreal>(std::max(kMinimumTextBoxDocumentSize, settings.box_height)));
  }
  // When a document transform is supplied the glyphs are rasterized *through* the affine (scale,
  // rotation, shear), so scaled-up text stays crisp instead of resampling an already-rendered bitmap.
  // The output image is sized to the transformed bounds and `local_rect` is returned in document space.
  const bool has_document_transform = !document_transform.isIdentity();
  const auto target_rect = has_document_transform ? document_transform.mapRect(local_rect) : local_rect;
  const auto image_left = static_cast<int>(std::floor(target_rect.left()));
  const auto image_top = static_cast<int>(std::floor(target_rect.top()));
  const auto image_right = static_cast<int>(std::ceil(target_rect.right()));
  const auto image_bottom = static_cast<int>(std::ceil(target_rect.bottom()));
  // Guard against a degenerate transform ballooning the dimensions: an over-large QImage allocates a
  // null image, which would silently drop the crisp render and fall back to the blocky resample.
  constexpr int kMaxTextRasterDimension = 16384;
  const auto image_width = std::clamp(image_right - image_left, 1, kMaxTextRasterDimension);
  const auto image_height = std::clamp(image_bottom - image_top, 1, kMaxTextRasterDimension);
  QImage image(image_width, image_height, QImage::Format_RGBA8888);
  if (image.isNull()) {
    return RenderedTextPixels{PixelBuffer{}, local_rect};
  }
  image.fill(Qt::transparent);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, settings.anti_alias > 0);
  painter.setRenderHint(QPainter::TextAntialiasing, settings.anti_alias > 0);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, settings.anti_alias > 0);
  painter.translate(-static_cast<qreal>(image_left), -static_cast<qreal>(image_top));
  if (has_document_transform) {
    painter.setTransform(document_transform, true);
  }
  if (!line_render_items.empty()) {
    for (const auto& item : line_render_items) {
      painter.save();
      painter.setClipRect(item.clip_rect);
      item.line.draw(&painter, item.block_origin);
      painter.restore();
    }
  } else {
    document.drawContents(&painter, local_rect);
  }
  painter.end();
  if (settings.anti_alias <= 0) {
    for (int y = 0; y < image.height(); ++y) {
      auto* line = image.scanLine(y);
      for (int x = 0; x < image.width(); ++x) {
        auto* pixel = line + static_cast<std::size_t>(x) * 4U;
        pixel[3] = pixel[3] >= 128 ? 255 : 0;
      }
    }
  }
  return RenderedTextPixels{pixels_from_image_rgba(image),
                            QRectF(static_cast<qreal>(image_left), static_cast<qreal>(image_top),
                                   static_cast<qreal>(image_width), static_cast<qreal>(image_height))};
}

PixelBuffer render_text_pixels(const TextToolSettings& settings, QColor color, std::int32_t max_width,
                               const QString& paragraph_runs = QString(),
                               const QString& rich_text_runs = QString()) {
  return render_text_pixels_with_local_rect(settings, color, max_width, paragraph_runs, rich_text_runs).pixels;
}

struct TransformedTextPixels {
  PixelBuffer pixels;
  Rect bounds{};
};

std::optional<LayerAffineTransform> canonical_text_affine_transform_for_layer(const Layer& layer) {
  if (const auto found = layer.metadata().find(kLayerMetadataTextTransform); found != layer.metadata().end()) {
    return parse_layer_affine_transform(found->second);
  }
  if (const auto found = layer.metadata().find(kLayerMetadataPsdTextTransform); found != layer.metadata().end()) {
    return parse_layer_affine_transform(found->second);
  }
  return std::nullopt;
}

std::optional<QTransform> patchy_text_transform_for_layer(const Layer& layer) {
  const auto parsed = canonical_text_affine_transform_for_layer(layer);
  if (!parsed.has_value()) {
    return std::nullopt;
  }
  return QTransform((*parsed)[0], (*parsed)[1], (*parsed)[2], (*parsed)[3], (*parsed)[4], (*parsed)[5]);
}

QTransform qtransform_from_affine(const LayerAffineTransform& transform) {
  return QTransform(transform[0], transform[1], transform[2], transform[3], transform[4], transform[5]);
}

std::optional<LayerAffineTransform> text_editor_transform_override(const QTextEdit& editor) {
  const auto value = editor.property(kTextEditorTransformOverrideProperty).toString().trimmed();
  if (value.isEmpty()) {
    return std::nullopt;
  }
  return parse_layer_affine_transform(value.toStdString());
}

void set_text_editor_transform_override(QTextEdit& editor, const LayerAffineTransform& transform) {
  editor.setProperty(kTextEditorTransformOverrideProperty,
                     QString::fromStdString(serialize_layer_affine_transform(transform)));
  editor.setProperty("patchy.documentTextX", static_cast<int>(std::floor(transform[4])));
  editor.setProperty("patchy.documentTextY", static_cast<int>(std::floor(transform[5])));
}

void set_text_editor_visible_local_rect(QTextEdit& editor, const Rect& bounds) {
  editor.setProperty(kTextEditorVisibleLocalRectProperty,
                     QRectF(bounds.x, bounds.y, std::max(1, bounds.width), std::max(1, bounds.height)));
}

void clear_text_editor_visible_local_rect(QTextEdit& editor) {
  editor.setProperty(kTextEditorVisibleLocalRectProperty, QVariant());
}

void set_text_editor_render_local_rect(QTextEdit& editor, const QRectF& rect) {
  if (const auto valid = valid_text_local_rect(rect); valid.has_value()) {
    editor.setProperty(kTextEditorRenderLocalRectProperty, *valid);
  }
}

void clear_text_editor_render_local_rect(QTextEdit& editor) {
  editor.setProperty(kTextEditorRenderLocalRectProperty, QVariant());
}

std::optional<QRectF> text_editor_render_local_rect(const QTextEdit& editor) {
  const auto value = editor.property(kTextEditorRenderLocalRectProperty);
  if (!value.isValid()) {
    return std::nullopt;
  }
  return valid_text_local_rect(value.toRectF());
}

std::optional<QPointF> text_editor_source_visible_anchor(const QTextEdit& editor) {
  const auto value = editor.property(kTextEditorSourceVisibleAnchorProperty);
  if (!value.isValid()) {
    return std::nullopt;
  }
  const auto point = value.toPointF();
  if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
    return std::nullopt;
  }
  return point;
}

std::optional<QSizeF> text_editor_source_visible_size(const QTextEdit& editor) {
  const auto value = editor.property(kTextEditorSourceVisibleSizeProperty);
  if (!value.isValid()) {
    return std::nullopt;
  }
  const auto size = value.toSizeF();
  if (!std::isfinite(size.width()) || !std::isfinite(size.height()) || size.width() <= 0.0 ||
      size.height() <= 0.0) {
    return std::nullopt;
  }
  return size;
}

// The horizontal fraction of a text block's width that its justification pins in place: Photoshop
// point text keeps the anchor at the line start for left text, the line middle for centered text,
// and the line end for right-justified text.
double horizontal_alignment_anchor_factor(Qt::Alignment alignment) {
  if ((alignment & Qt::AlignHCenter) != 0) {
    return 0.5;
  }
  if ((alignment & Qt::AlignRight) != 0) {
    return 1.0;
  }
  return 0.0;
}

// Multi-paragraph point text can mix justifications, but the rendered raster is placed as one
// block; the first paragraph's justification decides which point of the block stays fixed.
double text_editor_anchor_alignment_factor(const QTextEdit& editor) {
  const auto block = editor.document()->begin();
  return block.isValid() ? horizontal_alignment_anchor_factor(block.blockFormat().alignment()) : 0.0;
}

// Same as text_editor_anchor_alignment_factor, derived from a layer's stored paragraph runs for
// paths that re-render text without an open editor (e.g. free transform of a text layer).
double layer_anchor_alignment_factor(const Layer& layer) {
  const auto found = layer.metadata().find(kLayerMetadataTextParagraphRuns);
  if (found == layer.metadata().end()) {
    return 0.0;
  }
  const auto lines = QString::fromStdString(found->second).split(QLatin1Char('\n'));
  for (const auto& raw_line : lines) {
    const auto line = raw_line.trimmed();
    if (line.isEmpty() || line == QStringLiteral("v1") || line == QStringLiteral("v2")) {
      continue;
    }
    const auto fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 3) {
      continue;
    }
    return horizontal_alignment_anchor_factor(paragraph_alignment_from_name(fields[2]));
  }
  return 0.0;
}

bool text_document_has_non_left_alignment(const QTextDocument& document) {
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    if ((block.blockFormat().alignment() & (Qt::AlignHCenter | Qt::AlignRight | Qt::AlignJustify)) != 0) {
      return true;
    }
  }
  return false;
}

double text_editor_metric_scale(const QTextEdit& editor) {
  const auto value = editor.property(kTextEditorMetricScaleProperty);
  if (!value.isValid()) {
    return 1.0;
  }
  const auto scale = value.toDouble();
  return std::clamp(std::isfinite(scale) ? scale : 1.0, 0.5, 1.5);
}

void set_text_editor_metric_scale(QTextEdit& editor, double scale) {
  scale = std::clamp(std::isfinite(scale) ? scale : 1.0, 0.5, 1.5);
  if (std::abs(scale - 1.0) < 0.001) {
    editor.setProperty(kTextEditorMetricScaleProperty, QVariant());
    return;
  }
  editor.setProperty(kTextEditorMetricScaleProperty, scale);
}

struct AlphaRowBand {
  int top{0};
  int bottom{0};
};

std::vector<AlphaRowBand> alpha_row_bands(const PixelBuffer& pixels) {
  std::vector<AlphaRowBand> bands;
  if (pixels.empty() || pixels.width() <= 0 || pixels.height() <= 0) {
    return bands;
  }
  const auto channels = pixels.format().channels;
  if (channels != 1U && channels < 4U) {
    return bands;
  }
  const auto alpha_channel = channels == 1U ? 0U : 3U;
  const auto stride = pixels.stride_bytes();
  const auto bytes = pixels.data();
  const auto active_threshold = std::max(2, pixels.width() / 180);
  constexpr int kAlphaThreshold = 12;
  constexpr int kMergeGap = 2;

  bool in_band = false;
  int band_top = 0;
  int last_active = -1;
  for (int y = 0; y < pixels.height(); ++y) {
    int active_pixels = 0;
    const auto row_offset = static_cast<std::size_t>(y) * stride;
    for (int x = 0; x < pixels.width(); ++x) {
      const auto offset = row_offset + static_cast<std::size_t>(x) * channels + alpha_channel;
      if (offset < bytes.size() && bytes[offset] >= kAlphaThreshold) {
        ++active_pixels;
      }
    }
    const bool active = active_pixels >= active_threshold;
    if (active) {
      if (!in_band) {
        in_band = true;
        band_top = y;
      }
      last_active = y;
    } else if (in_band && last_active >= 0 && y - last_active > kMergeGap) {
      if (last_active + 1 - band_top >= 2) {
        bands.push_back(AlphaRowBand{band_top, last_active + 1});
      }
      in_band = false;
      last_active = -1;
    }
  }
  if (in_band && last_active + 1 - band_top >= 2) {
    bands.push_back(AlphaRowBand{band_top, last_active + 1});
  }
  return bands;
}

int alpha_row_band_span(const std::vector<AlphaRowBand>& bands) {
  if (bands.empty()) {
    return 0;
  }
  return std::max(0, bands.back().bottom - bands.front().top);
}

double alpha_row_band_score(const std::vector<AlphaRowBand>& source_bands,
                            const std::vector<AlphaRowBand>& candidate_bands,
                            double scale) {
  if (source_bands.empty() || candidate_bands.empty()) {
    return std::numeric_limits<double>::infinity();
  }
  const auto source_count = static_cast<int>(source_bands.size());
  const auto candidate_count = static_cast<int>(candidate_bands.size());
  const auto count_delta = std::abs(candidate_count - source_count);
  double score = static_cast<double>(count_delta) * (candidate_count > source_count ? 1000.0 : 650.0);

  const auto source_span = std::max(1, alpha_row_band_span(source_bands));
  const auto candidate_span = std::max(1, alpha_row_band_span(candidate_bands));
  score += std::abs(static_cast<double>(candidate_span - source_span)) /
           static_cast<double>(source_span) * 180.0;

  const auto compared = std::min(source_count, candidate_count);
  for (int index = 0; index < compared; ++index) {
    const auto source_height = std::max(1, source_bands[static_cast<std::size_t>(index)].bottom -
                                              source_bands[static_cast<std::size_t>(index)].top);
    const auto candidate_height = std::max(1, candidate_bands[static_cast<std::size_t>(index)].bottom -
                                                 candidate_bands[static_cast<std::size_t>(index)].top);
    score += std::abs(static_cast<double>(candidate_height - source_height)) /
             static_cast<double>(source_height) * 12.0;
  }

  score += std::max(0.0, 1.0 - scale) * 20.0;
  return score;
}

std::optional<double> calibrated_box_text_metric_scale(const Layer& source_layer,
                                                       const TextToolSettings& settings,
                                                       QColor text_color,
                                                       int text_width,
                                                       const QString& paragraph_runs,
                                                       const QString& rich_text_runs,
                                                       std::optional<QRectF> render_local_rect,
                                                       double layout_scale = 1.0) {
  if (!settings.boxed || source_layer.pixels().empty()) {
    return std::nullopt;
  }
  const auto source_bands = alpha_row_bands(source_layer.pixels());
  if (source_bands.size() < 2U) {
    return std::nullopt;
  }

  const auto baseline = render_text_pixels_with_local_rect(settings, text_color, text_width, paragraph_runs,
                                                           rich_text_runs, render_local_rect, 1.0, QTransform(),
                                                           layout_scale);
  const auto baseline_bands = alpha_row_bands(baseline.pixels);
  const auto baseline_score = alpha_row_band_score(source_bands, baseline_bands, 1.0);
  if (!std::isfinite(baseline_score)) {
    return std::nullopt;
  }
  const auto source_count = static_cast<int>(source_bands.size());
  const auto baseline_count = static_cast<int>(baseline_bands.size());
  const auto source_span = std::max(1, alpha_row_band_span(source_bands));
  const auto baseline_span = std::max(1, alpha_row_band_span(baseline_bands));
  if (baseline_count <= source_count &&
      std::abs(static_cast<double>(baseline_span - source_span)) <= static_cast<double>(source_span) * 0.10) {
    return std::nullopt;
  }

  double best_scale = 1.0;
  double best_score = baseline_score;
  for (int step = 1; step <= 35; ++step) {
    const auto scale = 1.0 - static_cast<double>(step) * 0.01;
    if (scale < 0.65) {
      break;
    }
    const auto candidate = render_text_pixels_with_local_rect(settings, text_color, text_width, paragraph_runs,
                                                              rich_text_runs, render_local_rect, scale,
                                                              QTransform(), layout_scale);
    const auto candidate_bands = alpha_row_bands(candidate.pixels);
    const auto score = alpha_row_band_score(source_bands, candidate_bands, scale);
    if (score < best_score) {
      best_score = score;
      best_scale = scale;
    }
  }

  if (best_scale < 0.995 && best_score + 20.0 < baseline_score) {
    return best_scale;
  }
  return std::nullopt;
}

// Whether this edit session belongs to a layer using the Photoshop leading model (the session
// captures kLayerMetadataTextLayoutMode as an editor property so previews and the commit render
// the same layout).
bool text_editor_uses_photoshop_layout(const QTextEdit& editor) {
  return editor.property("patchy.textLayoutMode").toString() == QLatin1String(kTextLayoutModePhotoshop);
}

// Display scale for the options-bar size fields: an imported Photoshop text layer stores engine
// sizes that render through the transform's vertical scale, so the spinbox shows/accepts the
// EFFECTIVE size (engine x scale -- what Photoshop's own UI shows) while runs stay in engine
// units. 1.0 for everything else.
double text_editor_size_display_scale(const QTextEdit& editor) {
  const auto value = editor.property("patchy.textSizeDisplayScale").toDouble();
  return std::isfinite(value) && value > 0.01 ? value : 1.0;
}

// The font the options-bar combo should select for a stored family name. A style-split name
// like "Arial Black" is not a family on this platform's database: raw QFont("Arial Black")
// falls through Windows' substitution chain to Tahoma, which the combo then displays. Resolve
// to the real family (the style rides the bold flag / the runs, not the combo).
QFont text_font_combo_font_for_family(const QString& family) {
  if (available_text_family_match(family).has_value()) {
    return QFont(family);
  }
  if (const auto match = available_text_family_style_match(family); match.has_value()) {
    return QFont(match->family);
  }
  return QFont(family);
}

std::optional<double> calibrated_box_text_metric_scale_for_editor(const QTextEdit& editor,
                                                                  const Layer& source_layer,
                                                                  double zoom,
                                                                  QColor fallback_color) {
  // The layer text is stored untrimmed: rich-text-run and paragraph-run offsets index the full
  // document text, so trimming here would shift every run when the text starts or ends with
  // whitespace (old PSDs often begin with a blank line).  Trim only for the emptiness check.
  const auto text = editor.toPlainText();
  if (text.trimmed().isEmpty() || !text_flow_is_box(editor.property("patchy.documentTextFlow").toString())) {
    return std::nullopt;
  }
  const auto text_size = std::max(1, editor.property("patchy.documentTextSize").toInt());
  const auto text_width = std::max(kMinimumTextBoxDocumentSize, editor.property("patchy.documentTextWidth").toInt());
  const auto text_height = std::max(kMinimumTextBoxDocumentSize, editor.property("patchy.documentTextHeight").toInt());
  const auto stored_color = editor.property("patchy.documentTextColor").value<QColor>();
  const auto text_color = stored_color.isValid() ? stored_color : (fallback_color.isValid() ? fallback_color
                                                                                           : QColor(Qt::black));
  const auto text_anti_alias = std::clamp(editor.property("patchy.documentTextAntiAlias").toInt(), 0, 16);
  auto text_family = editor.property("patchy.documentTextFamily").toString();
  if (text_family.trimmed().isEmpty()) {
    text_family = text_display_family_from_format(text_editor_reference_format(editor),
                                                  display_text_family_from_font(editor.font()));
  }

  auto document_text = document_from_editor_in_document_units(editor, zoom);
  TextToolSettings settings{text,
                            QString(),
                            text_family,
                            text_size,
                            editor.font().bold(),
                            editor.font().italic(),
                            text_anti_alias,
                            true,
                            text_width,
                            text_height};
  settings.photoshop_layout = text_editor_uses_photoshop_layout(editor);
  const auto rich_text_runs = rich_text_runs_from_document(*document_text, settings, text_color);
  const auto paragraph_runs = paragraph_runs_from_document(*document_text);
  settings.html = document_html_from_text_runs(document_text->toPlainText(), rich_text_runs, settings, text_color);
  const auto frame_layout_scale = settings.photoshop_layout && editor.property("patchy.usesPsdTextFrame").toBool()
                                      ? text_editor_size_display_scale(editor)
                                      : 1.0;
  return calibrated_box_text_metric_scale(source_layer, settings, text_color, text_width, paragraph_runs,
                                          rich_text_runs, text_editor_render_local_rect(editor),
                                          frame_layout_scale);
}

Rect rendered_text_bounds_for_editor(const QTextEdit& editor, QPoint document_point,
                                     const RenderedTextPixels& rendered) {
  if (text_editor_render_local_rect(editor).has_value()) {
    if (const auto anchor = text_editor_source_visible_anchor(editor); anchor.has_value()) {
      if (const auto visible_bounds = visible_alpha_local_bounds(rendered.pixels); visible_bounds.has_value()) {
        return Rect{static_cast<std::int32_t>(std::floor(anchor->x() - static_cast<double>(visible_bounds->x))),
                    static_cast<std::int32_t>(std::floor(anchor->y() - static_cast<double>(visible_bounds->y))),
                    rendered.pixels.width(),
                    rendered.pixels.height()};
      }
    }
    return Rect{static_cast<std::int32_t>(std::floor(static_cast<double>(document_point.x()) +
                                                     rendered.local_rect.left())),
                static_cast<std::int32_t>(std::floor(static_cast<double>(document_point.y()) +
                                                     rendered.local_rect.top())),
                rendered.pixels.width(),
                rendered.pixels.height()};
  }
  return Rect{document_point.x(), document_point.y(), rendered.pixels.width(), rendered.pixels.height()};
}

std::optional<LayerAffineTransform> anchored_text_transform_for_pixels(const QTextEdit& editor,
                                                                       const PixelBuffer& pixels) {
  const auto anchor = text_editor_source_visible_anchor(editor);
  if (!anchor.has_value()) {
    return std::nullopt;
  }
  const auto visible_bounds = visible_alpha_local_bounds(pixels);
  if (!visible_bounds.has_value()) {
    return std::nullopt;
  }
  // The anchor pins the source raster's visible top-left.  When the text is centered or
  // right-justified the justification point -- not the left edge -- must stay fixed (Photoshop
  // keeps each line aligned around the type anchor), so shift by the alignment fraction of the
  // width difference between the source raster and this render (font substitution or edits
  // change the width).  The source visible size is captured alongside the anchor on edit start.
  auto anchor_x = anchor->x();
  if (const auto factor = text_editor_anchor_alignment_factor(editor); factor > 0.0) {
    if (const auto source_size = text_editor_source_visible_size(editor); source_size.has_value()) {
      anchor_x += factor * (source_size->width() - static_cast<double>(visible_bounds->width));
    }
  }
  return LayerAffineTransform{1.0,
                              0.0,
                              0.0,
                              1.0,
                              anchor_x - static_cast<double>(visible_bounds->x),
                              anchor->y() - static_cast<double>(visible_bounds->y)};
}

bool update_text_editor_transform_from_source_anchor(QTextEdit& editor, const PixelBuffer& pixels) {
  const auto transform = anchored_text_transform_for_pixels(editor, pixels);
  if (!transform.has_value()) {
    return false;
  }
  set_text_editor_transform_override(editor, *transform);
  clear_text_editor_visible_local_rect(editor);
  return true;
}

std::optional<PixelBuffer> render_text_editor_pixels_for_source_anchor(const QTextEdit& editor, double zoom,
                                                                       QColor fallback_color) {
  // Untrimmed: run offsets index the full document text (see calibrated_box_text_metric_scale_for_editor).
  const auto text = editor.toPlainText();
  if (text.trimmed().isEmpty()) {
    return std::nullopt;
  }
  const auto text_size = std::max(1, editor.property("patchy.documentTextSize").toInt());
  const auto text_width = std::max(kMinimumTextBoxDocumentSize, editor.property("patchy.documentTextWidth").toInt());
  const auto text_height = std::max(kMinimumTextBoxDocumentSize, editor.property("patchy.documentTextHeight").toInt());
  const auto boxed_text = text_flow_is_box(editor.property("patchy.documentTextFlow").toString());
  if (boxed_text || editor.property("patchy.usesPsdTextFrame").toBool()) {
    return std::nullopt;
  }
  const auto stored_color = editor.property("patchy.documentTextColor").value<QColor>();
  const auto text_color = stored_color.isValid() ? stored_color : (fallback_color.isValid() ? fallback_color
                                                                                           : QColor(Qt::black));
  const auto text_anti_alias = std::clamp(editor.property("patchy.documentTextAntiAlias").toInt(), 0, 16);
  auto text_family = editor.property("patchy.documentTextFamily").toString();
  if (text_family.trimmed().isEmpty()) {
    text_family = text_display_family_from_format(text_editor_reference_format(editor),
                                                  display_text_family_from_font(editor.font()));
  }
  auto document_text = document_from_editor_in_document_units(editor, zoom);
  TextToolSettings settings{text,
                            QString(),
                            text_family,
                            text_size,
                            editor.font().bold(),
                            editor.font().italic(),
                            text_anti_alias,
                            boxed_text,
                            text_width,
                            text_height};
  settings.photoshop_layout = text_editor_uses_photoshop_layout(editor);
  const auto rich_text_runs = rich_text_runs_from_document(*document_text, settings, text_color);
  const auto paragraph_runs = paragraph_runs_from_document(*document_text);
  settings.html = document_html_from_text_runs(document_text->toPlainText(), rich_text_runs, settings, text_color);
  auto pixels = render_text_pixels(settings, text_color, text_width, paragraph_runs, rich_text_runs);
  if (pixels.empty()) {
    return std::nullopt;
  }
  return pixels;
}

bool update_text_editor_transform_from_source_anchor(QTextEdit& editor, double zoom, QColor fallback_color) {
  const auto pixels = render_text_editor_pixels_for_source_anchor(editor, zoom, fallback_color);
  if (!pixels.has_value()) {
    return false;
  }
  return update_text_editor_transform_from_source_anchor(editor, *pixels);
}

std::optional<QTransform> text_transform_for_editor_or_layer(const QTextEdit& editor, const Layer& layer) {
  if (const auto override_transform = text_editor_transform_override(editor); override_transform.has_value()) {
    return qtransform_from_affine(*override_transform);
  }
  return patchy_text_transform_for_layer(layer);
}

LayerAffineTransform affine_from_qtransform(const QTransform& transform) {
  return LayerAffineTransform{transform.m11(), transform.m12(), transform.m21(),
                              transform.m22(), transform.dx(),  transform.dy()};
}

TransformedTextPixels apply_text_transform_to_pixels(const PixelBuffer& pixels, const QTransform& transform) {
  const auto source = qimage_from_pixel_buffer(pixels).convertToFormat(QImage::Format_RGBA8888);
  const auto mapped = transform.mapRect(QRectF(0.0, 0.0, source.width(), source.height()));
  const auto left = static_cast<int>(std::floor(mapped.left()));
  const auto top = static_cast<int>(std::floor(mapped.top()));
  const auto right = static_cast<int>(std::ceil(mapped.right()));
  const auto bottom = static_cast<int>(std::ceil(mapped.bottom()));
  QImage transformed(std::max(1, right - left), std::max(1, bottom - top), QImage::Format_RGBA8888);
  transformed.fill(Qt::transparent);

  QPainter painter(&transformed);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.translate(-left, -top);
  painter.setTransform(transform, true);
  painter.drawImage(QPointF(0.0, 0.0), source);
  painter.end();

  return TransformedTextPixels{pixels_from_image_rgba(transformed),
                               Rect{left, top, transformed.width(), transformed.height()}};
}

// Re-rasterize a text editor session's glyphs *through* the layer transform -- vector glyphs
// rasterized at the final scale -- so scaled/rotated point text stays crisp instead of resampling
// the base-size bitmap.  Shared by commit_text_editor and the live edit preview so the user sees
// the same pixels while typing that the commit will produce.  Returns std::nullopt when the
// bitmap-resample path must be kept: box text, layouts that depend on a local-rect offset, a
// transform with no scale/rotation, or a PSD-anchored layer whose glyph-top-aligned override is
// absent (rendering through the raw PSD baseline transform would drop the text ~one ascent).
// A missing (substituted) font is deliberately NOT a refusal here: the session's base raster is
// already rendered with the substituted face, so the resample fallback preserves nothing of the
// original glyphs -- it just delivers those same substituted glyphs blurry, rendered at engine
// size and scaled up ~4x on files like the restaurant menu.
std::optional<TransformedTextPixels> render_crisp_transformed_text_for_editor(
    const QTextEdit& editor, bool psd_anchored_text, bool boxed_text, bool has_local_offset,
    const TextToolSettings& settings, QColor text_color, int text_width, const QString& paragraph_runs,
    const QString& rich_text_runs, const QTransform& text_transform, const QTransform& transform_for_pixels) {
  if (boxed_text || has_local_offset || !qtransform_has_non_translation_linear_part(text_transform)) {
    return std::nullopt;
  }
  if (psd_anchored_text && !text_editor_transform_override(editor).has_value()) {
    return std::nullopt;
  }
  auto crisp = render_text_pixels_with_local_rect(settings, text_color, text_width, paragraph_runs,
                                                  rich_text_runs, text_editor_render_local_rect(editor),
                                                  text_editor_metric_scale(editor), transform_for_pixels);
  if (crisp.pixels.empty()) {
    return std::nullopt;
  }
  const Rect bounds{static_cast<std::int32_t>(std::floor(crisp.local_rect.left())),
                    static_cast<std::int32_t>(std::floor(crisp.local_rect.top())), crisp.pixels.width(),
                    crisp.pixels.height()};
  return TransformedTextPixels{std::move(crisp.pixels), bounds};
}

std::vector<double> parse_space_separated_doubles(std::string_view text) {
  std::vector<double> values;
  std::istringstream stream{std::string(text)};
  double value = 0.0;
  while (stream >> value) {
    if (std::isfinite(value)) {
      values.push_back(value);
    }
  }
  return values;
}

std::optional<QRectF> psd_text_metadata_local_rect(const Layer& layer, const char* bounds_key) {
  const auto& metadata = layer.metadata();
  const auto bounds_found = metadata.find(bounds_key);
  if (bounds_found == metadata.end()) {
    return std::nullopt;
  }
  const auto bounds = parse_space_separated_doubles(bounds_found->second);
  if (bounds.size() < 4U) {
    return std::nullopt;
  }
  const auto left = bounds[0];
  const auto top = bounds[1];
  const auto right = bounds[2];
  const auto bottom = bounds[3];
  if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom) ||
      right <= left || bottom <= top) {
    return std::nullopt;
  }
  return QRectF(QPointF(left, top), QPointF(right, bottom));
}

std::optional<QRectF> valid_text_local_rect(QRectF rect) {
  rect = rect.normalized();
  if (!std::isfinite(rect.left()) || !std::isfinite(rect.top()) || !std::isfinite(rect.right()) ||
      !std::isfinite(rect.bottom()) || rect.width() <= 0.0 || rect.height() <= 0.0) {
    return std::nullopt;
  }
  return rect;
}

bool rect_extends_beyond(QRectF outer, QRectF inner) {
  outer = outer.normalized();
  inner = inner.normalized();
  constexpr qreal kEpsilon = 0.5;
  return outer.left() < inner.left() - kEpsilon || outer.top() < inner.top() - kEpsilon ||
         outer.right() > inner.right() + kEpsilon || outer.bottom() > inner.bottom() + kEpsilon;
}

std::optional<QRectF> psd_box_text_editor_render_local_rect(const Layer& layer) {
  const auto box = psd_text_metadata_local_rect(layer, kLayerMetadataPsdTextBoxBounds);
  if (!box.has_value()) {
    return std::nullopt;
  }

  QRectF editor_local(QPointF(0.0, 0.0), box->size());
  if (const auto visual = psd_text_metadata_local_rect(layer, kLayerMetadataPsdTextBoundingBox); visual.has_value()) {
    editor_local = editor_local.united(visual->translated(-box->topLeft()));
  }
  return valid_text_local_rect(editor_local);
}

std::optional<QRectF> layer_document_rect_text_local_rect(const Layer& layer, QRectF document_rect) {
  const auto transform = canonical_text_affine_transform_for_layer(layer);
  if (!transform.has_value()) {
    return std::nullopt;
  }
  document_rect = document_rect.normalized();
  if (!std::isfinite(document_rect.left()) || !std::isfinite(document_rect.top()) ||
      !std::isfinite(document_rect.right()) || !std::isfinite(document_rect.bottom()) ||
      document_rect.width() <= 0.0 || document_rect.height() <= 0.0) {
    return std::nullopt;
  }
  const auto determinant = (*transform)[0] * (*transform)[3] - (*transform)[1] * (*transform)[2];
  if (!std::isfinite(determinant) || std::abs(determinant) < 0.000001) {
    return std::nullopt;
  }
  const auto map_doc_to_local = [transform, determinant](double x, double y) {
    const auto dx = x - (*transform)[4];
    const auto dy = y - (*transform)[5];
    return QPointF(((*transform)[3] * dx - (*transform)[2] * dy) / determinant,
                   (-(*transform)[1] * dx + (*transform)[0] * dy) / determinant);
  };

  const auto left = document_rect.left();
  const auto top = document_rect.top();
  const auto right = document_rect.right();
  const auto bottom = document_rect.bottom();
  const std::array<QPointF, 4> points = {
      map_doc_to_local(left, top),
      map_doc_to_local(right, top),
      map_doc_to_local(right, bottom),
      map_doc_to_local(left, bottom),
  };
  auto min_x = points.front().x();
  auto max_x = points.front().x();
  auto min_y = points.front().y();
  auto max_y = points.front().y();
  for (const auto& point : points) {
    if (!std::isfinite(point.x()) || !std::isfinite(point.y())) {
      return std::nullopt;
    }
    min_x = std::min(min_x, point.x());
    max_x = std::max(max_x, point.x());
    min_y = std::min(min_y, point.y());
    max_y = std::max(max_y, point.y());
  }
  return valid_text_local_rect(QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y)));
}

std::optional<QRectF> layer_visible_alpha_text_local_rect(const Layer& layer) {
  const auto visible = visible_alpha_local_bounds(layer.pixels());
  if (!visible.has_value()) {
    return std::nullopt;
  }
  return layer_document_rect_text_local_rect(
      layer, QRectF(static_cast<qreal>(layer.bounds().x + visible->x),
                    static_cast<qreal>(layer.bounds().y + visible->y),
                    static_cast<qreal>(visible->width),
                    static_cast<qreal>(visible->height)));
}

std::optional<QRectF> patchy_box_text_editor_render_local_rect(const Layer& layer, int box_width, int box_height) {
  const auto raster_status = layer.metadata().find(kLayerMetadataTextRasterStatus);
  if (raster_status == layer.metadata().end() || raster_status->second != "patchy_raster") {
    return std::nullopt;
  }
  const auto local_rect = layer_visible_alpha_text_local_rect(layer);
  if (!local_rect.has_value()) {
    return std::nullopt;
  }
  const QRectF frame_rect(0.0, 0.0, static_cast<qreal>(std::max(kMinimumTextBoxDocumentSize, box_width)),
                          static_cast<qreal>(std::max(kMinimumTextBoxDocumentSize, box_height)));
  if (!rect_extends_beyond(*local_rect, frame_rect)) {
    return std::nullopt;
  }
  return valid_text_local_rect(frame_rect.united(*local_rect));
}

std::optional<QRectF> transformed_psd_text_metadata_rect(const Layer& layer, const char* bounds_key) {
  const auto& metadata = layer.metadata();
  const auto transform_found = metadata.find(kLayerMetadataPsdTextTransform);
  const auto bounds_found = metadata.find(bounds_key);
  if (transform_found == metadata.end() || bounds_found == metadata.end()) {
    return std::nullopt;
  }
  const auto transform = parse_space_separated_doubles(transform_found->second);
  const auto bounds = parse_space_separated_doubles(bounds_found->second);
  if (transform.size() < 6U || bounds.size() < 4U) {
    return std::nullopt;
  }

  const auto map_point = [&transform](double x, double y) {
    return QPointF(transform[0] * x + transform[2] * y + transform[4],
                   transform[1] * x + transform[3] * y + transform[5]);
  };
  const std::array<QPointF, 4> points = {
      map_point(bounds[0], bounds[1]),
      map_point(bounds[2], bounds[1]),
      map_point(bounds[2], bounds[3]),
      map_point(bounds[0], bounds[3]),
  };
  auto min_x = points.front().x();
  auto max_x = points.front().x();
  auto min_y = points.front().y();
  auto max_y = points.front().y();
  for (const auto& point : points) {
    min_x = std::min(min_x, point.x());
    max_x = std::max(max_x, point.x());
    min_y = std::min(min_y, point.y());
    max_y = std::max(max_y, point.y());
  }
  if (max_x <= min_x || max_y <= min_y) {
    return std::nullopt;
  }
  return QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y));
}

std::optional<QRectF> psd_text_frame_rect(const Layer& layer) {
  return transformed_psd_text_metadata_rect(layer, kLayerMetadataPsdTextBoxBounds);
}

std::optional<QRectF> psd_point_text_visual_rect(const Layer& layer) {
  return transformed_psd_text_metadata_rect(layer, kLayerMetadataPsdTextBoundingBox);
}

std::optional<QRectF> psd_point_text_local_visual_rect(const Layer& layer) {
  return psd_text_metadata_local_rect(layer, kLayerMetadataPsdTextBoundingBox);
}

QPointF rect_corner(QRectF rect, int corner_index) {
  switch (corner_index) {
    case 0:
      return rect.topLeft();
    case 1:
      return rect.topRight();
    case 2:
      return rect.bottomRight();
    case 3:
      return rect.bottomLeft();
    default:
      break;
  }
  return rect.topLeft();
}

int visual_top_left_corner_index(QRectF local_rect, const QTransform& transform) {
  int best_index = 0;
  auto best_point = transform.map(rect_corner(local_rect, best_index));
  constexpr double kEpsilon = 0.000001;
  for (int index = 1; index < 4; ++index) {
    const auto point = transform.map(rect_corner(local_rect, index));
    if (point.y() < best_point.y() - kEpsilon ||
        (std::abs(point.y() - best_point.y()) <= kEpsilon && point.x() < best_point.x())) {
      best_index = index;
      best_point = point;
    }
  }
  return best_index;
}

LayerAffineTransform affine_with_local_translation(const LayerAffineTransform& transform, QPointF delta) {
  return LayerAffineTransform{
      transform[0],
      transform[1],
      transform[2],
      transform[3],
      transform[0] * delta.x() + transform[2] * delta.y() + transform[4],
      transform[1] * delta.x() + transform[3] * delta.y() + transform[5],
  };
}

std::optional<LayerAffineTransform> psd_point_text_local_bounds_transform_for_pixels(const Layer& layer,
                                                                                    const PixelBuffer& pixels,
                                                                                    bool boxed_text,
                                                                                    double alignment_factor) {
  if (boxed_text || !layer.metadata().contains(kLayerMetadataPsdTextTransform) ||
      !layer.metadata().contains(kLayerMetadataPsdTextBoundingBox)) {
    return std::nullopt;
  }
  // A free transform (no text-content change) composes a new patchy text transform that diverges
  // from the PSD source, so layer_patchy_text_transform_overrides_psd_source() becomes true.  We
  // still want to align the editor to the live glyphs here: the PSD glyph metrics (psd_local_rect)
  // remain valid because committing an actual text edit clears the PSD source entirely (see
  // store_patchy_text_metadata -> clear_layer_psd_text_source), so reaching this point with the PSD
  // transform still present means only the position/scale changed.  Anchoring to the PSD baseline
  // instead would make a scaled/rotated PSD point-text layer jump down ~one ascent on edit.
  const auto transform = canonical_text_affine_transform_for_layer(layer);
  if (!transform.has_value() || !affine_transform_has_non_translation_linear_part(*transform)) {
    return std::nullopt;
  }
  const auto psd_local_rect = psd_point_text_local_visual_rect(layer);
  const auto visible_bounds = visible_alpha_local_bounds(pixels);
  if (!psd_local_rect.has_value() || !visible_bounds.has_value()) {
    return std::nullopt;
  }

  const QRectF visible_rect(visible_bounds->x, visible_bounds->y, visible_bounds->width, visible_bounds->height);
  const auto transform_qt = qtransform_from_affine(*transform);
  const auto anchor_index = visual_top_left_corner_index(*psd_local_rect, transform_qt);
  auto delta = rect_corner(*psd_local_rect, anchor_index) - rect_corner(visible_rect, anchor_index);
  // The reading axis (local x) always pins the justification point: line STARTS for left text
  // (factor 0), the middle for centered, the ends for right-justified. Both rects live in the
  // same local text space, so interpolating between their left and right edges by the fraction
  // stays correct under the layer's linear transform (including flips). The visual-corner pick
  // above chose its corner by DOCUMENT orientation -- under a 90-degree rotation that is the
  // line-END corner, and pinning it slid left-justified rotated text along its reading axis by
  // any line-length delta (the SNES back-panel jump). The vertical component keeps the
  // visual-top-corner anchoring.
  delta.setX((psd_local_rect->left() + alignment_factor * psd_local_rect->width()) -
             (visible_rect.left() + alignment_factor * visible_rect.width()));
  if (!std::isfinite(delta.x()) || !std::isfinite(delta.y())) {
    return std::nullopt;
  }
  return affine_with_local_translation(*transform, delta);
}

// Fallback for PSDs whose TySh descriptor predates the bounds/boundingBox fields (Photoshop
// CS-era files import them as a degenerate "0 0 0 0"): without the local glyph rect, align in
// document space instead.  Pin the bounding box of the re-rendered glyphs -- mapped through the
// layer transform -- to the imported raster's visible bounds: the justification fraction
// horizontally and the visible top vertically (matching the local-bounds variant's anchoring).
// A local translation shifts the mapped bounding box rigidly, so the pin is exact for any
// invertible affine.
std::optional<LayerAffineTransform> psd_point_text_document_bounds_transform_for_pixels(
    const Layer& layer, const PixelBuffer& pixels, bool boxed_text, double alignment_factor) {
  if (boxed_text || !layer.metadata().contains(kLayerMetadataPsdTextTransform)) {
    return std::nullopt;
  }
  const auto transform = canonical_text_affine_transform_for_layer(layer);
  if (!transform.has_value() || !affine_transform_has_non_translation_linear_part(*transform)) {
    return std::nullopt;
  }
  const auto source_visible = visible_alpha_local_bounds(layer.pixels());
  const auto render_visible = visible_alpha_local_bounds(pixels);
  if (!source_visible.has_value() || !render_visible.has_value()) {
    return std::nullopt;
  }
  const QRectF source_doc(static_cast<double>(layer.bounds().x + source_visible->x),
                          static_cast<double>(layer.bounds().y + source_visible->y),
                          static_cast<double>(source_visible->width),
                          static_cast<double>(source_visible->height));
  const QRectF render_local(render_visible->x, render_visible->y, render_visible->width,
                            render_visible->height);
  const auto transform_qt = qtransform_from_affine(*transform);
  const auto mapped = transform_qt.mapRect(render_local);
  if (!(mapped.width() > 0.0) || !(mapped.height() > 0.0)) {
    return std::nullopt;
  }
  // Pin the TEXT-SPACE anchor point -- the justification fraction along the reading axis, the
  // first-line side on the stack axis -- not a fixed document corner: under a 90-degree
  // rotation the document's top edge is the line-END side, and pinning it slid left-justified
  // rotated text along its reading axis by any line-length delta (the SNES back-panel jump).
  // The source ink box is only known in document space, so pin the fractionally-corresponding
  // point of the two document boxes: the local anchor's normalized position inside the mapped
  // render box picks the same relative point of the source box for any invertible affine.
  const QPointF local_anchor(render_local.left() + alignment_factor * render_local.width(),
                             render_local.top());
  const auto mapped_anchor = transform_qt.map(local_anchor);
  const auto u = (mapped_anchor.x() - mapped.left()) / mapped.width();
  const auto v = (mapped_anchor.y() - mapped.top()) / mapped.height();
  const QPointF source_anchor(source_doc.left() + u * source_doc.width(),
                              source_doc.top() + v * source_doc.height());
  const QPointF document_delta = source_anchor - mapped_anchor;
  // affine_with_local_translation applies the transform's linear part to the delta, so convert
  // the document-space displacement back into local text space.
  bool invertible = false;
  const auto linear_inverse =
      QTransform((*transform)[0], (*transform)[1], (*transform)[2], (*transform)[3], 0.0, 0.0)
          .inverted(&invertible);
  if (!invertible) {
    return std::nullopt;
  }
  const auto local_delta = linear_inverse.map(document_delta);
  if (!std::isfinite(local_delta.x()) || !std::isfinite(local_delta.y())) {
    return std::nullopt;
  }
  return affine_with_local_translation(*transform, local_delta);
}

bool update_text_editor_transform_from_psd_local_bounds(QTextEdit& editor, const Layer& layer,
                                                        const PixelBuffer& pixels, bool boxed_text) {
  const auto alignment_factor = text_editor_anchor_alignment_factor(editor);
  auto transform =
      psd_point_text_local_bounds_transform_for_pixels(layer, pixels, boxed_text, alignment_factor);
  if (!transform.has_value()) {
    transform =
        psd_point_text_document_bounds_transform_for_pixels(layer, pixels, boxed_text, alignment_factor);
  }
  if (!transform.has_value()) {
    return false;
  }
  set_text_editor_transform_override(editor, *transform);
  if (const auto visible_bounds = visible_alpha_local_bounds(pixels); visible_bounds.has_value()) {
    set_text_editor_visible_local_rect(editor, *visible_bounds);
  }
  return true;
}

std::optional<QPointF> layer_source_visible_anchor(const Layer& layer) {
  if (const auto visible_bounds = visible_alpha_local_bounds(layer.pixels()); visible_bounds.has_value()) {
    return QPointF(static_cast<double>(layer.bounds().x + visible_bounds->x),
                   static_cast<double>(layer.bounds().y + visible_bounds->y));
  }
  return std::nullopt;
}

std::optional<QRectF> psd_point_text_source_visible_rect(const Layer& layer) {
  if (const auto visible_bounds = visible_alpha_local_bounds(layer.pixels()); visible_bounds.has_value()) {
    return QRectF(static_cast<double>(layer.bounds().x + visible_bounds->x),
                  static_cast<double>(layer.bounds().y + visible_bounds->y),
                  static_cast<double>(visible_bounds->width), static_cast<double>(visible_bounds->height));
  }
  if (const auto visual_rect = psd_point_text_visual_rect(layer); visual_rect.has_value()) {
    return visual_rect;
  }
  return std::nullopt;
}

struct LayerTextRenderInputs {
  TextToolSettings settings;
  QColor color;
  std::int32_t max_width{320};
  QString paragraph_runs;
  QString rich_text_runs;
};

std::optional<LayerTextRenderInputs> text_render_inputs_from_layer(const Layer& layer) {
  const auto& metadata = layer.metadata();
  // Untrimmed: the stored run offsets index the full text (leading blank lines included).
  const auto text = [&metadata] {
    const auto found = metadata.find(kLayerMetadataText);
    return found == metadata.end() ? QString() : QString::fromStdString(found->second);
  }();
  if (text.trimmed().isEmpty()) {
    return std::nullopt;
  }

  const auto value = [&metadata](const char* key) -> std::optional<QString> {
    const auto found = metadata.find(key);
    if (found == metadata.end()) {
      return std::nullopt;
    }
    return QString::fromStdString(found->second);
  };

  const auto html = value(kLayerMetadataTextHtml).value_or(QString());
  const auto paragraph_runs = value(kLayerMetadataTextParagraphRuns).value_or(QString());
  const auto rich_text_runs = value(kLayerMetadataTextRuns).value_or(QString());
  auto family = value(kLayerMetadataTextFont).value_or(QApplication::font().family());
  if (family.compare(QStringLiteral("PSD Text"), Qt::CaseInsensitive) == 0) {
    family = QApplication::font().family();
  }
  const auto size = value(kLayerMetadataTextSize).has_value()
                        ? std::max(1, std::atoi(value(kLayerMetadataTextSize)->toUtf8().constData()))
                        : 36;
  const auto anti_alias =
      value(kLayerMetadataTextAntiAlias).has_value()
          ? std::clamp(std::atoi(value(kLayerMetadataTextAntiAlias)->toUtf8().constData()), 0, 16)
          : kDefaultTextAntiAlias;
  const auto color_value = value(kLayerMetadataTextColor).value_or(QStringLiteral("#000000"));
  const QColor color(color_value);
  const auto boxed = text_flow_is_box(value(kLayerMetadataTextFlow).value_or(QString::fromLatin1(kTextFlowPoint)));
  const auto box_width =
      value(kLayerMetadataTextBoxWidth).has_value()
          ? std::max(kMinimumTextBoxDocumentSize, std::atoi(value(kLayerMetadataTextBoxWidth)->toUtf8().constData()))
          : std::max(kMinimumTextBoxDocumentSize, layer.bounds().width);
  const auto box_height =
      value(kLayerMetadataTextBoxHeight).has_value()
          ? std::max(kMinimumTextBoxDocumentSize, std::atoi(value(kLayerMetadataTextBoxHeight)->toUtf8().constData()))
          : std::max(kMinimumTextBoxDocumentSize, layer.bounds().height);
  TextToolSettings settings{text, html, family, size,
                            value(kLayerMetadataTextBold).value_or(QString()) == QStringLiteral("true"),
                            value(kLayerMetadataTextItalic).value_or(QString()) == QStringLiteral("true"),
                            anti_alias,
                            boxed,
                            box_width,
                            box_height};
  settings.photoshop_layout =
      value(kLayerMetadataTextLayoutMode).value_or(QString()) == QLatin1String(kTextLayoutModePhotoshop);
  return LayerTextRenderInputs{std::move(settings), color.isValid() ? color : QColor(Qt::black),
                               layer.bounds().width > 0 ? layer.bounds().width : 320, paragraph_runs,
                               rich_text_runs};
}

std::optional<PixelBuffer> render_text_layer_pixels_from_metadata(const Layer& layer) {
  const auto inputs = text_render_inputs_from_layer(layer);
  if (!inputs.has_value()) {
    return std::nullopt;
  }
  return render_text_pixels(inputs->settings, inputs->color, inputs->max_width, inputs->paragraph_runs,
                            inputs->rich_text_runs);
}

// Re-render a point-text layer's glyphs *through* the layer transform so a scaled/rotated layer stays
// crisp (vector glyphs rasterized at the final scale) instead of resampling an already-baked bitmap.
// Returns std::nullopt for box text, which has its own transform-aware layout path.
std::optional<TransformedTextPixels> render_text_layer_pixels_through_transform(const Layer& layer,
                                                                               const QTransform& transform) {
  auto inputs = text_render_inputs_from_layer(layer);
  if (!inputs.has_value() || inputs->settings.boxed) {
    return std::nullopt;
  }
  const auto rendered = render_text_pixels_with_local_rect(inputs->settings, inputs->color, inputs->max_width,
                                                           inputs->paragraph_runs, inputs->rich_text_runs,
                                                           std::nullopt, 1.0, transform);
  if (rendered.pixels.empty()) {
    return std::nullopt;
  }
  const auto origin_x = static_cast<std::int32_t>(std::floor(rendered.local_rect.left()));
  const auto origin_y = static_cast<std::int32_t>(std::floor(rendered.local_rect.top()));
  // Trim transparent margins (e.g. the bounding box of rotated glyphs, or any residual padding) so the
  // layer bounds -- and therefore the free-transform handles -- hug the inked glyphs.
  if (const auto visible = visible_alpha_local_bounds(rendered.pixels);
      visible.has_value() && (visible->x > 0 || visible->y > 0 || visible->width < rendered.pixels.width() ||
                              visible->height < rendered.pixels.height())) {
    const auto cropped = qimage_from_pixel_buffer(rendered.pixels)
                             .convertToFormat(QImage::Format_RGBA8888)
                             .copy(visible->x, visible->y, visible->width, visible->height);
    return TransformedTextPixels{pixels_from_image_rgba(cropped),
                                 Rect{origin_x + visible->x, origin_y + visible->y, visible->width, visible->height}};
  }
  return TransformedTextPixels{rendered.pixels,
                               Rect{origin_x, origin_y, rendered.pixels.width(), rendered.pixels.height()}};
}

// First-line baseline y in the text render's local space (layout top = 0): the PSD
// writer anchors a warped point-text transform here, matching Photoshop's own
// warped files (box top = -ascent, baseline at the transform origin), so a type
// re-render in Photoshop lands where Patchy's raster is instead of one descent
// lower.
std::optional<double> first_line_baseline_for_text_inputs(const LayerTextRenderInputs& inputs) {
  const auto built = build_text_render_document(inputs.settings, inputs.color, inputs.max_width,
                                                inputs.paragraph_runs, inputs.rich_text_runs, 1.0);
  if (built.document == nullptr) {
    return std::nullopt;
  }
  built.document->size();  // QTextDocument lays out lazily; force it or lineCount() is 0
  for (auto block = built.document->begin(); block.isValid(); block = block.next()) {
    const auto* layout = block.layout();
    if (layout == nullptr || layout->lineCount() < 1) {
      continue;
    }
    const auto line = layout->lineAt(0);
    if (!line.isValid()) {
      continue;
    }
    const double baseline = layout->position().y() + line.y() + line.ascent();
    if (std::isfinite(baseline) && baseline > 0.0) {
      return baseline;
    }
  }
  return std::nullopt;
}

// Photoshop Warp Text rendering: the unwarped glyph raster (supersampled so the
// warp samples crisp ink) is resampled through the style warp surface composed with
// the text-local -> document transform. The warp box defaults to the fresh layout
// rect when the stored box is empty (Photoshop re-derives its box on every layout
// change; for BOX text that means the dragged FRAME, corner effects and all - the
// wt_*_para_smalltext captures pin that a short line in a big box rides the warp
// surface's shoulder in Photoshop too). `effective_warp` receives the warp with the
// box actually used so callers can persist it. Returns nullopt for identity warps
// or unknown styles so callers fall back to the unwarped paths.
std::optional<TransformedTextPixels> render_warped_text_pixels_for_layer(const LayerTextRenderInputs& inputs,
                                                                         TextWarp warp,
                                                                         const QTransform& text_to_document,
                                                                         TextWarp* effective_warp = nullptr) {
  if (text_warp_is_identity(warp) || !can_generate_style_warp_mesh(warp.style)) {
    return std::nullopt;
  }
  const auto base = render_text_pixels_with_local_rect(inputs.settings, inputs.color, inputs.max_width,
                                                       inputs.paragraph_runs, inputs.rich_text_runs);
  if (base.pixels.empty()) {
    return std::nullopt;
  }
  const QRectF window = base.local_rect;
  if (warp.bounds_right - warp.bounds_left <= 0.0 || warp.bounds_bottom - warp.bounds_top <= 0.0) {
    warp.bounds_left = window.left();
    warp.bounds_top = window.top();
    warp.bounds_right = window.right();
    warp.bounds_bottom = window.bottom();
    warp.baseline = inputs.settings.boxed
                        ? 0.0  // box-text transforms anchor at the frame origin (PS convention)
                        : first_line_baseline_for_text_inputs(inputs).value_or(0.0);
  }
  if (effective_warp != nullptr) {
    *effective_warp = warp;
  }
  // Supersample the source raster within a memory budget (RGBA bytes).
  int supersample = 3;
  const double base_area = window.width() * window.height();
  if (base_area * 9.0 * 4.0 > 256.0 * 1024.0 * 1024.0) {
    supersample = 2;
  }
  if (base_area * 4.0 * 4.0 > 256.0 * 1024.0 * 1024.0) {
    supersample = 1;
  }
  QImage source;
  QRectF source_window = window;
  if (supersample > 1) {
    const auto scaled = render_text_pixels_with_local_rect(
        inputs.settings, inputs.color, inputs.max_width, inputs.paragraph_runs, inputs.rich_text_runs,
        std::nullopt, 1.0, QTransform::fromScale(supersample, supersample));
    if (!scaled.pixels.empty()) {
      source = qimage_from_pixel_buffer(scaled.pixels).convertToFormat(QImage::Format_RGBA8888);
      source_window =
          QRectF(scaled.local_rect.left() / supersample, scaled.local_rect.top() / supersample,
                 scaled.local_rect.width() / supersample, scaled.local_rect.height() / supersample);
    }
  }
  if (source.isNull()) {
    source = qimage_from_pixel_buffer(base.pixels).convertToFormat(QImage::Format_RGBA8888);
    source_window = window;
  }
  // Restrict the resample window to the INKED part of the raster (plus the warp
  // box, whose whole span shapes the lattice sizing): a box-text frame is mostly
  // empty, and running the surface across all of it would both extrapolate far
  // outside the warp box and starve the lattice resolution where the ink is.
  if (const auto ink = visible_alpha_local_bounds(base.pixels); ink.has_value()) {
    const QRectF ink_window = QRectF(window.left() + ink->x, window.top() + ink->y,
                                     ink->width, ink->height)
                                  .adjusted(-1.0, -1.0, 1.0, 1.0)
                                  .intersected(source_window);
    if (ink_window.isValid() && ink_window.width() > 0.0 && ink_window.height() > 0.0 &&
        ink_window != source_window) {
      const double scale_x = source.width() / source_window.width();
      const double scale_y = source.height() / source_window.height();
      const int crop_x = std::clamp(
          static_cast<int>(std::floor((ink_window.left() - source_window.left()) * scale_x)), 0,
          source.width() - 1);
      const int crop_y = std::clamp(
          static_cast<int>(std::floor((ink_window.top() - source_window.top()) * scale_y)), 0,
          source.height() - 1);
      const int crop_width = std::clamp(
          static_cast<int>(std::ceil(ink_window.width() * scale_x)), 1, source.width() - crop_x);
      const int crop_height = std::clamp(
          static_cast<int>(std::ceil(ink_window.height() * scale_y)), 1, source.height() - crop_y);
      source = source.copy(crop_x, crop_y, crop_width, crop_height);
      source_window = QRectF(source_window.left() + crop_x / scale_x,
                             source_window.top() + crop_y / scale_y, crop_width / scale_x,
                             crop_height / scale_y);
    }
  }
  const auto mesh = generate_text_warp_mesh(warp);
  if (!mesh.has_value()) {
    return std::nullopt;
  }
  const std::array<double, 6> affine{text_to_document.m11(), text_to_document.m21(),
                                     text_to_document.dx(),  text_to_document.m12(),
                                     text_to_document.m22(), text_to_document.dy()};
  const auto grid = build_warp_surface_grid_over_window(
      *mesh, warp.bounds_left, warp.bounds_top, warp.bounds_right, warp.bounds_bottom,
      source_window.left(), source_window.top(), source_window.right(), source_window.bottom(),
      source.width(), source.height(), affine, 4.0, 192);
  if (!grid.has_value()) {
    return std::nullopt;
  }
  auto warped = resample_warped_rgba8(source, *grid, CanvasWidget::TransformInterpolation::Bilinear);
  if (warped.image.isNull() || warped.bounds.width <= 0 || warped.bounds.height <= 0) {
    return std::nullopt;
  }
  auto pixels = pixels_from_image_rgba(warped.image);
  // Trim transparent margins so the layer bounds hug the warped ink.
  if (const auto visible = visible_alpha_local_bounds(pixels);
      visible.has_value() && (visible->x > 0 || visible->y > 0 || visible->width < pixels.width() ||
                              visible->height < pixels.height())) {
    const auto cropped =
        warped.image.convertToFormat(QImage::Format_RGBA8888)
            .copy(visible->x, visible->y, visible->width, visible->height);
    return TransformedTextPixels{pixels_from_image_rgba(cropped),
                                 Rect{warped.bounds.x + visible->x, warped.bounds.y + visible->y,
                                      visible->width, visible->height}};
  }
  return TransformedTextPixels{std::move(pixels), warped.bounds};
}

void clear_layer_text_metadata(Layer& layer) {
  static constexpr std::array<const char*, 24> kTextMetadataKeys = {
      kLayerMetadataText,
      kLayerMetadataTextHtml,
      kLayerMetadataTextRuns,
      kLayerMetadataTextParagraphRuns,
      kLayerMetadataTextFlow,
      kLayerMetadataTextBoxWidth,
      kLayerMetadataTextBoxHeight,
      kLayerMetadataTextFont,
      kLayerMetadataTextSize,
      kLayerMetadataTextColor,
      kLayerMetadataTextBold,
      kLayerMetadataTextItalic,
      kLayerMetadataTextAntiAlias,
      kLayerMetadataTextRasterStatus,
      kLayerMetadataTextLayoutMode,
      kLayerMetadataTextTransform,
      kLayerMetadataTextWarp,
      kLayerMetadataPsdTextTransform,
      kLayerMetadataPsdTextBounds,
      kLayerMetadataPsdTextBoundingBox,
      kLayerMetadataPsdTextBoxBounds,
      kLayerMetadataPsdTextTailBounds,
      kLayerMetadataPsdTextIndex,
      kLayerMetadataTextLineAwareBoxPreview,
  };
  for (const auto* key : kTextMetadataKeys) {
    layer.metadata().erase(key);
  }
  layer.metadata().erase(kLayerMetadataTextSourceBlock);
  auto& blocks = layer.unknown_psd_blocks();
  std::erase_if(blocks, [](const UnknownPsdBlock& block) {
    return block.key == "TySh" || block.key == "tySh";
  });
}

void clear_layer_psd_text_source(Layer& layer) {
  layer.metadata().erase(kLayerMetadataTextSourceBlock);
  layer.metadata().erase(kLayerMetadataPsdTextTransform);
  layer.metadata().erase(kLayerMetadataPsdTextBounds);
  layer.metadata().erase(kLayerMetadataPsdTextBoundingBox);
  layer.metadata().erase(kLayerMetadataPsdTextBoxBounds);
  layer.metadata().erase(kLayerMetadataPsdTextTailBounds);
  layer.metadata().erase(kLayerMetadataPsdTextIndex);
}

// Flattens the layer tree into compositor paint order (depth-first pre-order). doc.layers()[0] is the
// bottom of the stack, so an id's position in this list is its global bottom-to-top stacking order --
// used to order an arbitrary multi-selection (across folders) and pick the bottom-most merge target.
void collect_layer_render_order(const std::vector<Layer>& layers, std::vector<LayerId>& out) {
  for (const auto& layer : layers) {
    out.push_back(layer.id());
    collect_layer_render_order(layer.children(), out);
  }
}

bool layer_is_effectively_visible(const std::vector<Layer>& layers, LayerId id, bool ancestor_visible = true) {
  for (const auto& layer : layers) {
    const auto visible = ancestor_visible && layer.visible();
    if (layer.id() == id) {
      return visible;
    }
    if (layer.kind() == LayerKind::Group && layer_is_effectively_visible(layer.children(), id, visible)) {
      return true;
    }
  }
  return false;
}

void store_patchy_text_metadata(Layer& layer, const TextToolSettings& settings, QColor color,
                                const QString& rich_text_runs, const QString& paragraph_runs, int text_width,
                                int text_height) {
  layer.metadata()[kLayerMetadataText] = settings.text.toStdString();
  layer.metadata()[kLayerMetadataTextHtml] = settings.html.toStdString();
  layer.metadata()[kLayerMetadataTextRuns] = rich_text_runs.toStdString();
  layer.metadata()[kLayerMetadataTextParagraphRuns] = paragraph_runs.toStdString();
  layer.metadata()[kLayerMetadataTextFlow] = text_flow_metadata_value(settings.boxed).toStdString();
  layer.metadata()[kLayerMetadataTextBoxWidth] = std::to_string(std::max(1, text_width));
  layer.metadata()[kLayerMetadataTextBoxHeight] = std::to_string(std::max(1, text_height));
  layer.metadata()[kLayerMetadataTextFont] = settings.family.toStdString();
  layer.metadata()[kLayerMetadataTextSize] = std::to_string(settings.size);
  layer.metadata()[kLayerMetadataTextColor] = color.name(QColor::HexRgb).toStdString();
  layer.metadata()[kLayerMetadataTextBold] = settings.bold ? "true" : "false";
  layer.metadata()[kLayerMetadataTextItalic] = settings.italic ? "true" : "false";
  layer.metadata()[kLayerMetadataTextAntiAlias] = std::to_string(std::clamp(settings.anti_alias, 0, 16));
  layer.metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
  clear_layer_psd_text_source(layer);
}

// Scale the per-run font sizes (and any explicit leading) in a serialized rich-text-runs string.
// Format (see rich_text_runs_from_document): line 0 is the version tag, each subsequent line is
// "start\tlength\tsize\tbold\titalic\tcolor\tfamily[\tleading[\ttracking]]". v3 sizes are
// doubles and the leading column may be the literal "auto" (scale-free); tracking is in
// 1/1000-em units and therefore never scales.
QString scale_rich_text_runs(const QString& runs, double scale) {
  if (runs.trimmed().isEmpty() || !std::isfinite(scale) || std::abs(scale - 1.0) < 0.0001) {
    return runs;
  }
  auto lines = runs.split(QLatin1Char('\n'));
  const bool v3 = !lines.isEmpty() && lines[0].trimmed() == QStringLiteral("v3");
  for (int i = 1; i < lines.size(); ++i) {
    auto fields = lines[i].split(QLatin1Char('\t'));
    if (fields.size() < 3) {
      continue;
    }
    bool ok = false;
    if (v3) {
      if (const double size = fields[2].toDouble(&ok); ok && std::isfinite(size) && size > 0.0) {
        fields[2] = QString::number(std::clamp(size * scale, 1.0, 8192.0), 'g', 17);
      }
    } else if (const int size = fields[2].toInt(&ok); ok) {
      fields[2] = QString::number(std::clamp(static_cast<int>(std::lround(size * scale)), 1, 8192));
    }
    if (fields.size() >= 8 && fields[7] != QStringLiteral("auto")) {
      if (const double leading = fields[7].toDouble(&ok); ok && std::isfinite(leading) && leading > 0.0) {
        fields[7] = QString::number(leading * scale, 'g', 17);
      }
    }
    lines[i] = fields.join(QLatin1Char('\t'));
  }
  return lines.join(QLatin1Char('\n'));
}

// Fold the vertical scale of a Patchy point-text layer's transform into its font size -- matching how
// imported PSD text records the *visual* size -- and return the residual matrix (vertical scale
// removed, rotation/aspect/translation kept) to render and store.  Updates the size-bearing metadata
// (base size, per-run sizes, regenerated HTML) so the Type panel and PSD export show the new point
// size.  Returns the input transform unchanged when there is no meaningful scale to fold.
QTransform fold_text_transform_scale_into_font_size(Layer& layer, const QTransform& transform) {
  auto inputs = text_render_inputs_from_layer(layer);
  if (!inputs.has_value() || inputs->settings.boxed) {
    return transform;
  }
  const double vertical_scale = std::hypot(transform.m21(), transform.m22());
  if (!std::isfinite(vertical_scale) || vertical_scale <= 0.01) {
    return transform;
  }
  const int old_size = std::max(1, inputs->settings.size);
  const int new_size = std::clamp(static_cast<int>(std::lround(old_size * vertical_scale)), 1, 8192);
  if (new_size == old_size) {
    return transform;
  }
  const double applied_scale = static_cast<double>(new_size) / static_cast<double>(old_size);
  const QTransform residual(transform.m11() / applied_scale, transform.m12() / applied_scale,
                            transform.m21() / applied_scale, transform.m22() / applied_scale, transform.dx(),
                            transform.dy());

  const auto& metadata = layer.metadata();
  const auto string_value = [&metadata](const char* key) -> QString {
    const auto found = metadata.find(key);
    return found == metadata.end() ? QString() : QString::fromStdString(found->second);
  };
  const auto int_value = [&metadata](const char* key, int fallback) -> int {
    const auto found = metadata.find(key);
    return found == metadata.end() ? fallback : std::max(1, std::atoi(found->second.c_str()));
  };
  const auto rich_runs = string_value(kLayerMetadataTextRuns);
  const auto paragraph_runs = string_value(kLayerMetadataTextParagraphRuns);
  const auto box_width = int_value(kLayerMetadataTextBoxWidth, inputs->settings.box_width);
  const auto box_height = int_value(kLayerMetadataTextBoxHeight, inputs->settings.box_height);
  auto settings = inputs->settings;
  settings.size = new_size;
  const auto scaled_runs = scale_rich_text_runs(rich_runs, applied_scale);
  settings.html = document_html_from_text_runs(settings.text, scaled_runs, settings, inputs->color);
  store_patchy_text_metadata(layer, settings, inputs->color, scaled_runs, paragraph_runs, box_width, box_height);
  layer.metadata()[kLayerMetadataTextTransform] =
      serialize_layer_affine_transform(affine_from_qtransform(residual));
  return residual;
}

LayerAffineTransform committed_text_transform(QPoint document_point,
                                              const std::optional<LayerAffineTransform>& active_transform) {
  auto transform = active_transform.value_or(LayerAffineTransform{1.0, 0.0, 0.0, 1.0, 0.0, 0.0});
  transform[4] = static_cast<double>(document_point.x());
  transform[5] = static_cast<double>(document_point.y());
  return transform;
}

struct RasterizedLayerPixels {
  PixelBuffer pixels;
  Rect bounds;
};

std::optional<RasterizedLayerPixels> render_rasterized_layer_pixels(const Document& document, const Layer& source,
                                                                    bool include_layer_style) {
  if (!layer_has_rasterizable_content(source)) {
    return std::nullopt;
  }

  auto layer = source;
  layer.set_visible(true);
  layer.set_opacity(1.0F);
  if (!include_layer_style) {
    layer.set_fill_opacity(1.0F);
  }
  layer.set_blend_mode(BlendMode::Normal);
  layer.clear_mask();
  // Blend If remains a live advanced-blending option on the source layer; do
  // not bake it into text/smart-object pixels or a Rasterize Layer Style result.
  layer.set_blend_if_payload({}, true);
  if (!include_layer_style) {
    layer.layer_style() = {};
  }

  if ((layer.kind() == LayerKind::Text || layer.pixels().empty()) && layer_is_text(layer)) {
    auto text_pixels = render_text_layer_pixels_from_metadata(layer);
    if (!text_pixels.has_value() || text_pixels->empty()) {
      return std::nullopt;
    }
    const auto origin = layer.bounds().empty() ? Rect{} : layer.bounds();
    layer.set_pixels(std::move(*text_pixels));
    layer.set_bounds(Rect{origin.x, origin.y, layer.pixels().width(), layer.pixels().height()});
  }
  if (layer.pixels().empty()) {
    return std::nullopt;
  }

  auto bounds = include_layer_style ? layer_render_bounds(layer) : layer_pixel_bounds(layer);
  bounds = intersect_rect(bounds, Rect::from_size(document.width(), document.height()));
  if (bounds.empty()) {
    return std::nullopt;
  }

  Document raster_document(document.width(), document.height(), document.format());
  raster_document.add_layer(std::move(layer));
  const auto image =
      qimage_from_document_rect(raster_document, QRect(bounds.x, bounds.y, bounds.width, bounds.height), true);
  if (image.isNull()) {
    return std::nullopt;
  }
  return RasterizedLayerPixels{pixels_from_image_rgba(image), bounds};
}

std::optional<Layer> renderable_merge_layer_copy(const Layer& source) {
  if (!layer_has_rasterizable_content(source)) {
    return std::nullopt;
  }

  auto layer = source;
  if ((layer.kind() == LayerKind::Text || layer.pixels().empty()) && layer_is_text(layer)) {
    auto text_pixels = render_text_layer_pixels_from_metadata(layer);
    if (!text_pixels.has_value() || text_pixels->empty()) {
      return std::nullopt;
    }
    const auto origin = layer.bounds().empty() ? Rect{} : layer.bounds();
    layer.set_pixels(std::move(*text_pixels));
    layer.set_bounds(Rect{origin.x, origin.y, layer.pixels().width(), layer.pixels().height()});
  }
  if (layer.kind() != LayerKind::Pixel || layer.pixels().empty()) {
    return std::nullopt;
  }
  return layer;
}

QString rasterized_text_layer_name(const Layer& layer) {
  auto name = QString::fromStdString(layer.name()).trimmed();
  const QStringList prefixes = {
      QStringLiteral("Text: "),
      QCoreApplication::translate(kMainWindowTranslationContext, "Text: %1").arg(QString()),
  };
  for (const auto& prefix : prefixes) {
    if (prefix.isEmpty() || !name.startsWith(prefix, Qt::CaseInsensitive)) {
      continue;
    }
    const auto stripped = name.mid(prefix.size()).trimmed();
    if (!stripped.isEmpty()) {
      return stripped;
    }
  }
  return name;
}

QString text_layer_auto_name(const QString& text) {
  auto name = text.simplified();
  if (name.size() > 24) {
    name = name.left(24) + QStringLiteral("...");
  }
  return name;
}

// True when the layer's current name was derived from its text content -- bare, or with the
// legacy "Text: " prefix older Patchy builds added -- rather than chosen by the user (or carried
// in from the source PSD).  Auto-derived names follow the text on commit; chosen names stay.
bool text_layer_name_is_auto(const Layer& layer) {
  const auto current = QString::fromStdString(layer.name()).trimmed();
  if (current.isEmpty()) {
    return true;
  }
  const auto found = layer.metadata().find(kLayerMetadataText);
  const auto auto_name = text_layer_auto_name(
      found == layer.metadata().end() ? QString() : QString::fromStdString(found->second));
  if (auto_name.isEmpty()) {
    return false;
  }
  if (current == auto_name) {
    return true;
  }
  const QStringList prefixes = {
      QStringLiteral("Text: "),
      QCoreApplication::translate(kMainWindowTranslationContext, "Text: %1").arg(QString()),
  };
  for (const auto& prefix : prefixes) {
    if (!prefix.isEmpty() && current.compare(prefix + auto_name, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  // Installed before the first statusBar() call so every showMessage goes through the
  // subclass that hosts the zoom percentage box (see ui/zoom_status_bar.hpp).
  zoom_status_bar_ = new ZoomStatusBar(this);
  setStatusBar(zoom_status_bar_);
  register_builtin_filters(filters_);
  install_ico_png_codec();
  print_page_layout_ = default_print_page_layout();
  if (use_custom_window_chrome()) {
    setWindowFlag(Qt::FramelessWindowHint, true);
  }

  document_tabs_ = new QTabWidget(this);
  document_tabs_->setObjectName(QStringLiteral("documentTabs"));
  document_tabs_->setDocumentMode(true);
  document_tabs_->setTabsClosable(true);
  document_tabs_->setMovable(true);
  setAcceptDrops(true);
  document_tabs_->setAcceptDrops(true);
  document_tabs_->installEventFilter(this);
  suppress_native_tab_bar_base(*document_tabs_);
  if (auto* tab_bar = document_tabs_->findChild<QTabBar*>(); tab_bar != nullptr) {
    tab_bar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tab_bar, &QWidget::customContextMenuRequested, this, &MainWindow::show_document_tab_context_menu);
    // Tear-off gesture: dragging a tab out of the bar floats its document.
    tab_bar->installEventFilter(this);
    // Clicking the already-current tab emits no currentChanged, but it must
    // still activate that document when a float window holds the active one.
    connect(tab_bar, &QTabBar::tabBarClicked, this, [this](int index) {
      if (document_tabs_ == nullptr || index < 0 || index != document_tabs_->currentIndex()) {
        return;
      }
      auto* canvas = dynamic_cast<CanvasWidget*>(document_tabs_->widget(index));
      if (canvas != nullptr && canvas != canvas_) {
        activate_document_canvas(canvas);
      }
    });
  }
  setCentralWidget(document_tabs_);
  connect(document_tabs_, &QTabWidget::currentChanged, this, [this](int index) { activate_document_tab(index); });
  connect(document_tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { close_document_tab(index); });
  start_panel_ = new StartPanel(document_tabs_);
  start_panel_->hide();
  connect(start_panel_, &StartPanel::new_document_requested, this, [this] { create_new_document(); });
  connect(start_panel_, &StartPanel::open_requested, this, [this] { open_document(); });
  connect(start_panel_, &StartPanel::recent_file_requested, this,
          [this](const QString& path) { open_recent_document(path); });
  connect(QApplication::clipboard(), &QClipboard::dataChanged, this,
          [this] { clear_internal_clipboard_on_external_change(); });
  layer_opacity_apply_timer_ = new QTimer(this);
  layer_opacity_apply_timer_->setSingleShot(true);
  layer_opacity_apply_timer_->setInterval(kLayerOpacityApplyDelayMs);
  connect(layer_opacity_apply_timer_, &QTimer::timeout, this, [this] { apply_pending_layer_opacity(); });
  layer_opacity_idle_timer_ = new QTimer(this);
  layer_opacity_idle_timer_->setSingleShot(true);
  layer_opacity_idle_timer_->setInterval(kLayerOpacityIdleFinishDelayMs);
  connect(layer_opacity_idle_timer_, &QTimer::timeout, this, [this] { finish_pending_layer_opacity_edit(); });
  layer_fill_opacity_apply_timer_ = new QTimer(this);
  layer_fill_opacity_apply_timer_->setSingleShot(true);
  layer_fill_opacity_apply_timer_->setInterval(kLayerOpacityApplyDelayMs);
  connect(layer_fill_opacity_apply_timer_, &QTimer::timeout, this,
          [this] { apply_pending_layer_fill_opacity(); });
  layer_fill_opacity_idle_timer_ = new QTimer(this);
  layer_fill_opacity_idle_timer_->setSingleShot(true);
  layer_fill_opacity_idle_timer_->setInterval(kLayerOpacityIdleFinishDelayMs);
  connect(layer_fill_opacity_idle_timer_, &QTimer::timeout, this,
          [this] { finish_pending_layer_fill_opacity_edit(); });
  tool_settings_save_timer_ = new QTimer(this);
  tool_settings_save_timer_->setSingleShot(true);
  tool_settings_save_timer_->setInterval(kToolSettingsSaveDelayMs);
  connect(tool_settings_save_timer_, &QTimer::timeout, this, [this] { save_tool_settings(); });
  load_pen_input_settings();
  // Startup opens with an empty workspace (the start panel); no document is
  // auto-created. The one-time load_tool_settings() runs when the first
  // document session brings a canvas (startup_tool_settings_pending_).

  create_actions();
  load_view_settings();
  configure_window_chrome();
  load_recent_files();
  rebuild_recent_files_menu();
  load_recent_folders();
  rebuild_recent_folders_menu();
  update_start_panel_visibility();
  load_bundled_legacy_plugins();
  create_docks();
  hotkey_registry_.apply_to_actions();
  refresh_layer_list();
  refresh_layer_controls();
  update_document_action_state();  update_undo_redo_actions();
  qApp->installEventFilter(this);

  refresh_document_window_title();
  setWindowIcon(patchy_app_icon());
  if (!restore_window_geometry()) {
    resize(1280, 860);
    clamp_window_to_available_screen();
  }
  setStyleSheet(photoshop_style());
  ensure_native_resizable_frame();
  mask_edit_mode_chip_ = new QToolButton(statusBar());
  mask_edit_mode_chip_->setObjectName(QStringLiteral("maskEditModeChip"));
  mask_edit_mode_chip_->setCursor(Qt::PointingHandCursor);
  mask_edit_mode_chip_->setFocusPolicy(Qt::NoFocus);
  bind_widget_text(mask_edit_mode_chip_, "Editing layer mask (click to exit)");
  bind_tooltip(mask_edit_mode_chip_, "Paint tools are editing the layer mask. Click to edit the layer pixels again.");
  connect(mask_edit_mode_chip_, &QToolButton::clicked, this, [this] {
    if (canvas_ != nullptr && canvas_->quick_mask_active()) {
      toggle_quick_mask_mode();
    } else {
      set_layer_edit_target_ui(CanvasWidget::LayerEditTarget::Content, true);
    }
  });
  statusBar()->addPermanentWidget(mask_edit_mode_chip_);
  mask_edit_mode_chip_->hide();
  palette_mode_chip_ = new QToolButton(statusBar());
  palette_mode_chip_->setObjectName(QStringLiteral("paletteModeChip"));
  palette_mode_chip_->setCursor(Qt::PointingHandCursor);
  palette_mode_chip_->setFocusPolicy(Qt::NoFocus);
  connect(palette_mode_chip_, &QToolButton::clicked, this, [this] {
    if (palette_dock_ != nullptr) {
      palette_dock_->show();
      palette_dock_->raise();
    }
  });
  statusBar()->addPermanentWidget(palette_mode_chip_);
  palette_mode_chip_->hide();
  palette_compliance_timer_ = new QTimer(this);
  palette_compliance_timer_->setSingleShot(true);
  palette_compliance_timer_->setInterval(400);
  connect(palette_compliance_timer_, &QTimer::timeout, this, [this] { run_palette_compliance_check(); });
  zoom_status_edit_ = new ZoomPercentEdit(zoom_status_bar_);
  bind_tooltip(zoom_status_edit_, "Zoom percentage. Type a new value and press Enter.");
  connect(zoom_status_edit_, &ZoomPercentEdit::zoom_percent_committed, this, [this](double percent) {
    if (canvas_ == nullptr || !has_active_document()) {
      return;
    }
    canvas_->set_zoom_centered(percent / 100.0);
  });
  zoom_status_bar_->set_left_widget(zoom_status_edit_);
  refresh_document_info();
  statusBar()->showMessage(tr("Ready"));
}

bool MainWindow::handle_spacebar_canvas_pan_event(QObject* watched, QEvent* event) {
  if (canvas_ == nullptr || event == nullptr) {
    return false;
  }

  // Cancel an in-progress spacebar pan when focus genuinely leaves the app, but
  // not for intra-app window switches. Clicking the canvas while a non-modal
  // child dialog is focused deactivates that dialog and activates the main
  // window; that WindowDeactivate must be ignored, otherwise the pan we armed on
  // the keypress is disarmed the instant the canvas press arrives and the drag
  // never grabs. The application stays Active across intra-app transitions, so
  // gate WindowDeactivate on the app no longer being active.
  if ((spacebar_canvas_pan_down_ || spacebar_canvas_pan_dragging_) &&
      (event->type() == QEvent::ApplicationDeactivate ||
       (event->type() == QEvent::WindowDeactivate &&
        QGuiApplication::applicationState() != Qt::ApplicationActive))) {
    reset_spacebar_canvas_pan();
    return false;
  }

  auto* widget = qobject_cast<QWidget*>(watched);
  if (widget == nullptr) {
    widget = QApplication::focusWidget();
  }

  const auto target_in_window = spacebar_canvas_pan_target_in_window(widget);
  const auto target_is_canvas = spacebar_canvas_pan_target_is_canvas(widget);
  switch (event->type()) {
    case QEvent::KeyPress: {
      auto* key_event = static_cast<QKeyEvent*>(event);
      if (key_event->key() != Qt::Key_Space || !target_in_window || target_is_canvas ||
          spacebar_canvas_pan_blocked_by_text_input(widget)) {
        return false;
      }
      if (!key_event->isAutoRepeat()) {
        spacebar_canvas_pan_down_ = true;
        canvas_->set_spacebar_panning(true);
        update_spacebar_canvas_pan_cursor(spacebar_canvas_pan_dragging_ ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
      }
      key_event->accept();
      return true;
    }
    case QEvent::KeyRelease: {
      auto* key_event = static_cast<QKeyEvent*>(event);
      if (key_event->key() != Qt::Key_Space || (!spacebar_canvas_pan_down_ && !spacebar_canvas_pan_dragging_)) {
        return false;
      }
      if (!key_event->isAutoRepeat()) {
        spacebar_canvas_pan_down_ = false;
        canvas_->set_spacebar_panning(false);
        if (spacebar_canvas_pan_dragging_) {
          update_spacebar_canvas_pan_cursor(Qt::ClosedHandCursor);
        } else {
          clear_spacebar_canvas_pan_cursor();
        }
      }
      key_event->accept();
      return true;
    }
    case QEvent::MouseButtonPress: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (!spacebar_canvas_pan_down_ || mouse_event->button() != Qt::LeftButton || !target_in_window) {
        return false;
      }
      if (target_is_canvas) {
        spacebar_canvas_pan_dragging_ = true;
        update_spacebar_canvas_pan_cursor(Qt::ClosedHandCursor);
        return false;
      }
      if (!canvas_->begin_pan_at_global_position(mouse_event->globalPosition().toPoint())) {
        return false;
      }
      spacebar_canvas_pan_dragging_ = true;
      update_spacebar_canvas_pan_cursor(Qt::ClosedHandCursor);
      mouse_event->accept();
      return true;
    }
    case QEvent::MouseMove: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (!spacebar_canvas_pan_dragging_ || (mouse_event->buttons() & Qt::LeftButton) == 0 || !target_in_window) {
        return false;
      }
      update_spacebar_canvas_pan_cursor(Qt::ClosedHandCursor);
      if (target_is_canvas) {
        return false;
      }
      if (!canvas_->pan_to_global_position(mouse_event->globalPosition().toPoint())) {
        return false;
      }
      mouse_event->accept();
      return true;
    }
    case QEvent::MouseButtonRelease: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (!spacebar_canvas_pan_dragging_ || mouse_event->button() != Qt::LeftButton) {
        return false;
      }
      spacebar_canvas_pan_dragging_ = false;
      if (target_is_canvas) {
        if (spacebar_canvas_pan_down_) {
          update_spacebar_canvas_pan_cursor(Qt::OpenHandCursor);
        } else {
          clear_spacebar_canvas_pan_cursor();
        }
        return false;
      }
      static_cast<void>(canvas_->end_pan());
      if (spacebar_canvas_pan_down_) {
        update_spacebar_canvas_pan_cursor(Qt::OpenHandCursor);
      } else {
        clear_spacebar_canvas_pan_cursor();
      }
      mouse_event->accept();
      return true;
    }
    default:
      break;
  }

  return false;
}

bool MainWindow::spacebar_canvas_pan_target_in_window(QWidget* widget) const noexcept {
  if (widget == nullptr) {
    return false;
  }
  if (widget == this || isAncestorOf(widget) || widget->window() == this) {
    return true;
  }
  // Non-modal child dialogs (layer style, adjustment, filter, ...) are separate
  // top-level windows, so the checks above miss them and spacebar panning would
  // not engage until the canvas is clicked. Walk the dialog's owner chain: if it
  // is ultimately parented to this window, treat its focus widget as in-window so
  // holding Space pans the canvas behind the dialog (Photoshop behavior). The
  // text-input guard still keeps Space typing into the dialog's fields.
  for (QWidget* owner = widget->window(); owner != nullptr;) {
    if (owner == this) {
      return true;
    }
    QWidget* parent = owner->parentWidget();
    owner = parent != nullptr ? parent->window() : nullptr;
  }
  return false;
}

bool MainWindow::spacebar_canvas_pan_target_is_canvas(QWidget* widget) const noexcept {
  return canvas_ != nullptr && widget != nullptr && (widget == canvas_ || canvas_->isAncestorOf(widget));
}

bool MainWindow::spacebar_canvas_pan_blocked_by_text_input(QWidget* widget) const noexcept {
  for (auto* current = widget; current != nullptr; current = current->parentWidget()) {
    if (qobject_cast<QLineEdit*>(current) != nullptr || qobject_cast<QTextEdit*>(current) != nullptr ||
        qobject_cast<QPlainTextEdit*>(current) != nullptr || qobject_cast<QAbstractSpinBox*>(current) != nullptr) {
      return true;
    }
    if (const auto* combo = qobject_cast<QComboBox*>(current); combo != nullptr && combo->isEditable()) {
      return true;
    }
    if (current == this) {
      break;
    }
  }
  return false;
}

void MainWindow::update_spacebar_canvas_pan_cursor(Qt::CursorShape cursor) {
  // The spacebar pan and the pen-hover override share the single application
  // override-cursor slot; release the pen override before taking it over.
  set_canvas_pen_cursor_override(false);
  const QCursor next_cursor(cursor);
  if (spacebar_canvas_pan_cursor_active_) {
    QApplication::changeOverrideCursor(next_cursor);
  } else {
    QApplication::setOverrideCursor(next_cursor);
    spacebar_canvas_pan_cursor_active_ = true;
  }
}

void MainWindow::set_canvas_pen_cursor_override(bool active) {
  if (active) {
    // Qt does not reliably deliver enter/leave events for a pen crossing widget
    // borders (QTBUG-65199), so a plain setCursor on the canvas is not applied
    // until a real mouse event arrives. Drive the tool cursor through the
    // application override cursor instead, which updates immediately while the
    // pen hovers over the canvas. Never stack on top of the spacebar override.
    if (canvas_ == nullptr || spacebar_canvas_pan_cursor_active_) {
      return;
    }
    const QCursor cursor = canvas_->cursor();
    if (canvas_pen_cursor_active_) {
      QApplication::changeOverrideCursor(cursor);
    } else {
      QApplication::setOverrideCursor(cursor);
      canvas_pen_cursor_active_ = true;
    }
  } else {
    if (!canvas_pen_cursor_active_) {
      return;
    }
    canvas_pen_cursor_active_ = false;
    QApplication::restoreOverrideCursor();
  }
}

void MainWindow::update_pen_cursor_override(QObject* watched, QEvent* event) {
  if (canvas_ == nullptr) {
    return;
  }
  switch (event->type()) {
    case QEvent::TabletMove:
    case QEvent::TabletPress:
    case QEvent::TabletRelease: {
      // Each tablet event is dispatched both to the native window object (a
      // QWindow) and to the target widget. Ignore the non-widget delivery;
      // otherwise the override would be cleared by the window event and re-set
      // by the widget event, making the cursor flicker between arrow and brush.
      auto* widget = qobject_cast<QWidget*>(watched);
      if (widget == nullptr) {
        break;
      }
      if (spacebar_canvas_pan_target_is_canvas(widget)) {
        set_canvas_pen_cursor_override(true);
      } else {
        set_canvas_pen_cursor_override(false);
      }
      break;
    }
    case QEvent::TabletLeaveProximity:
      set_canvas_pen_cursor_override(false);
      break;
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease: {
      // Only a genuine mouse event hands control back to Qt's per-widget cursor
      // handling. While a pen hovers, Windows also emits companion mouse-move
      // events flagged as synthesized; clearing on those would make the cursor
      // flip-flop between the arrow and the brush, so they are ignored here.
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->source() == Qt::MouseEventNotSynthesized) {
        set_canvas_pen_cursor_override(false);
      }
      break;
    }
    case QEvent::WindowDeactivate:
    case QEvent::ApplicationDeactivate:
      set_canvas_pen_cursor_override(false);
      break;
    default:
      break;
  }
}

void MainWindow::update_pen_hover_tooltip(QObject* watched, QEvent* event) {
  // Qt only wakes its tooltip machinery on genuine mouse hover, so a stylus
  // hovering over a toolbar button never shows the tip. While a pen hovers,
  // Windows emits companion mouse-move events flagged as synthesized; we use
  // those to drive a tooltip ourselves, mirroring Qt's hover-delay behavior.
  switch (event->type()) {
    case QEvent::MouseMove: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->source() == Qt::MouseEventNotSynthesized) {
        // A real mouse hands tooltips back to Qt; drop any pen-driven tip.
        cancel_pen_hover_tooltip();
        break;
      }
      const QPoint global_pos = mouse_event->globalPosition().toPoint();
      auto* widget = qApp->widgetAt(global_pos);
      if (widget == nullptr || widget->toolTip().isEmpty()) {
        cancel_pen_hover_tooltip();
        break;
      }
      // Restart the delay whenever the pen moves to a different target so the
      // tip only appears once the pen settles, like the mouse behavior.
      if (widget != pen_hover_tooltip_widget_.data()) {
        pen_hover_tooltip_widget_ = widget;
        pen_hover_tooltip_global_pos_ = global_pos;
        if (pen_hover_tooltip_timer_ == nullptr) {
          pen_hover_tooltip_timer_ = new QTimer(this);
          pen_hover_tooltip_timer_->setSingleShot(true);
          connect(pen_hover_tooltip_timer_, &QTimer::timeout, this, &MainWindow::show_pen_hover_tooltip);
        }
        const int delay = std::max(1, style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay));
        pen_hover_tooltip_timer_->start(delay);
      } else {
        pen_hover_tooltip_global_pos_ = global_pos;
      }
      break;
    }
    case QEvent::TabletPress:
    case QEvent::TabletRelease:
    case QEvent::TabletLeaveProximity:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::WindowDeactivate:
    case QEvent::ApplicationDeactivate:
      cancel_pen_hover_tooltip();
      break;
    default:
      break;
  }
  Q_UNUSED(watched);
}

void MainWindow::show_pen_hover_tooltip() {
  auto* widget = pen_hover_tooltip_widget_.data();
  if (widget == nullptr) {
    return;
  }
  // Only show if the pen is still resting over the same widget.
  if (qApp->widgetAt(pen_hover_tooltip_global_pos_) != widget) {
    return;
  }
  const QString text = widget->toolTip();
  if (text.isEmpty()) {
    return;
  }
  QToolTip::showText(pen_hover_tooltip_global_pos_, text, widget);
}

void MainWindow::cancel_pen_hover_tooltip() {
  if (pen_hover_tooltip_timer_ != nullptr) {
    pen_hover_tooltip_timer_->stop();
  }
  pen_hover_tooltip_widget_.clear();
}

void MainWindow::clear_spacebar_canvas_pan_cursor() {
  if (!spacebar_canvas_pan_cursor_active_) {
    return;
  }
  spacebar_canvas_pan_cursor_active_ = false;
  QApplication::restoreOverrideCursor();
}

void MainWindow::reset_spacebar_canvas_pan() {
  spacebar_canvas_pan_down_ = false;
  spacebar_canvas_pan_dragging_ = false;
  if (canvas_ != nullptr) {
    static_cast<void>(canvas_->end_pan());
    canvas_->set_spacebar_panning(false);
  }
  clear_spacebar_canvas_pan_cursor();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (handle_spacebar_canvas_pan_event(watched, event)) {
    return true;
  }

  // The start panel is a manual overlay of the tab area (no layout owns it).
  if (watched == document_tabs_ && event->type() == QEvent::Resize && start_panel_ != nullptr &&
      start_panel_->isVisible()) {
    start_panel_->setGeometry(document_tabs_->rect());
  }

  if (event->type() == QEvent::ShortcutOverride) {
    if (auto* editor = qobject_cast<QTextEdit*>(watched);
        editor != nullptr && editor->objectName() == QStringLiteral("inlineTextEditor")) {
      auto* key_event = static_cast<QKeyEvent*>(event);
      if (key_event->matches(QKeySequence::Bold) || key_event->matches(QKeySequence::Italic)) {
        key_event->accept();
        return true;
      }
    }
  }

  if (event->type() == QEvent::FocusIn && !shutting_down_) {
    // Focusing a document canvas makes its document active. Tab switches do this
    // through currentChanged; this covers the paths that move focus without a
    // tab change - clicking the visible tab page while a float window holds the
    // active document, or clicking a float's canvas.
    if (auto* canvas = qobject_cast<CanvasWidget*>(watched);
        canvas != nullptr && canvas != canvas_ && session_for_canvas(canvas) != nullptr) {
      activate_document_canvas(canvas);
    }
  }

  update_pen_cursor_override(watched, event);
  update_pen_hover_tooltip(watched, event);

  if (event->type() == QEvent::KeyPress) {
    if (auto* editor = qobject_cast<QTextEdit*>(watched);
        editor != nullptr && editor->objectName() == QStringLiteral("inlineTextEditor")) {
      auto* key_event = static_cast<QKeyEvent*>(event);
      std::optional<LayerId> layer_id;
      if (editor->property("patchy.editingLayerId").isValid()) {
        layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
      }
      if (key_event->key() == Qt::Key_Escape) {
        cancel_text_editor(editor, layer_id);
        key_event->accept();
        return true;
      }
      if ((key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter) &&
          (key_event->modifiers() & Qt::ControlModifier) != 0) {
        const QPoint document_point(editor->property("patchy.documentTextX").toInt(),
                                    editor->property("patchy.documentTextY").toInt());
        commit_text_editor(editor, document_point, layer_id);
        key_event->accept();
        return true;
      }
      if (key_event->matches(QKeySequence::Bold) && text_bold_button_ != nullptr) {
        text_bold_button_->setChecked(!text_editor_reference_format(*editor).font().bold());
        key_event->accept();
        return true;
      }
      if (key_event->matches(QKeySequence::Italic) && text_italic_button_ != nullptr) {
        text_italic_button_->setChecked(!text_editor_reference_format(*editor).font().italic());
        key_event->accept();
        return true;
      }
    }
  }

  if (auto* recent_menu = qobject_cast<QMenu*>(watched);
      recent_menu != nullptr && recent_menu->property(kRecentFilesMenuProperty).toBool()) {
    switch (event->type()) {
      case QEvent::MouseButtonPress: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::RightButton) {
          mouse_event->accept();
          return true;
        }
        break;
      }
      case QEvent::MouseButtonRelease: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::RightButton) {
          show_recent_file_context_menu(recent_menu, mouse_event->pos());
          mouse_event->accept();
          return true;
        }
        break;
      }
      case QEvent::ContextMenu: {
        auto* context_event = static_cast<QContextMenuEvent*>(event);
        show_recent_file_context_menu(recent_menu, context_event->pos());
        context_event->accept();
        return true;
      }
      default:
        break;
    }
  }

  if (handle_layer_action_button_drag_event(watched, event)) {
    return true;
  }

  if (handle_right_dock_resize_event(watched, event)) {
    return true;
  }

  if (auto* editor = qobject_cast<QTextEdit*>(watched);
      editor != nullptr && editor->objectName() == QStringLiteral("inlineTextEditor") &&
      event->type() == QEvent::Wheel) {
    auto* wheel_event = static_cast<QWheelEvent*>(event);
    const auto wheel_delta = !wheel_event->pixelDelta().isNull() ? wheel_event->pixelDelta() : wheel_event->angleDelta();
    const auto primary_delta = wheel_delta.y() != 0 ? wheel_delta.y() : wheel_delta.x();
    if (canvas_ != nullptr && primary_delta != 0 && (wheel_event->modifiers() & Qt::AltModifier) != 0) {
      canvas_->zoom_at_widget_point(canvas_->mapFromGlobal(wheel_event->globalPosition().toPoint()),
                                    primary_delta > 0 ? 1.1 : 0.9);
      refresh_document_info();
    }
    reset_text_editor_scroll(editor);
    wheel_event->accept();
    return true;
  }
  if (auto* viewport = qobject_cast<QWidget*>(watched);
      viewport != nullptr && viewport->parentWidget() != nullptr &&
      viewport->parentWidget()->objectName() == QStringLiteral("inlineTextEditor") &&
      event->type() == QEvent::Wheel) {
    auto* editor = qobject_cast<QTextEdit*>(viewport->parentWidget());
    auto* wheel_event = static_cast<QWheelEvent*>(event);
    const auto wheel_delta = !wheel_event->pixelDelta().isNull() ? wheel_event->pixelDelta() : wheel_event->angleDelta();
    const auto primary_delta = wheel_delta.y() != 0 ? wheel_delta.y() : wheel_delta.x();
    if (canvas_ != nullptr && primary_delta != 0 && (wheel_event->modifiers() & Qt::AltModifier) != 0) {
      canvas_->zoom_at_widget_point(canvas_->mapFromGlobal(wheel_event->globalPosition().toPoint()),
                                    primary_delta > 0 ? 1.1 : 0.9);
      refresh_document_info();
    }
    reset_text_editor_scroll(editor);
    wheel_event->accept();
    return true;
  }

  if (canvas_ != nullptr) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (widget != nullptr && (widget == canvas_ || canvas_->isAncestorOf(widget))) {
      auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
      if (editor != nullptr && !editor->property(kTextEditorFinishedProperty).toBool() &&
          handle_text_editor_transform_overlay_event(editor, event)) {
        return true;
      }
    }
  }

  if (watched == canvas_ && canvas_ != nullptr) {
    if (swallow_next_canvas_left_press_) {
      if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        swallow_next_canvas_left_press_ = false;
        if (mouse_event->button() == Qt::LeftButton) {
          mouse_event->accept();
          return true;
        }
      } else if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseButtonDblClick) {
        swallow_next_canvas_left_press_ = false;
      }
    }
    auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    if (editor != nullptr && !editor->property(kTextEditorFinishedProperty).toBool()) {
      switch (event->type()) {
        case QEvent::MouseButtonPress: {
          auto* mouse_event = static_cast<QMouseEvent*>(event);
          if (mouse_event->button() == Qt::LeftButton) {
            if (auto* handle = text_editor_resize_handle_at(mouse_event->pos()); handle != nullptr) {
              return handle_text_editor_resize_event(handle, editor, event);
            }
            if (commit_active_text_editor()) {
              mouse_event->accept();
              return true;
            }
          }
          break;
        }
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease: {
          const auto handle_name = editor->property("patchy.activeTextResizeHandleName").toString();
          if (!handle_name.isEmpty()) {
            if (auto* handle = canvas_->findChild<QWidget*>(handle_name, Qt::FindDirectChildrenOnly);
                handle != nullptr && handle->property("patchy.textResizeHandle").toBool()) {
              return handle_text_editor_resize_event(handle, editor, event);
            }
          }
          break;
        }
        default:
          break;
      }
    }
  }

  if (auto* handle = qobject_cast<QWidget*>(watched);
      handle != nullptr && handle->property("patchy.textResizeHandle").toBool()) {
    auto* editor = canvas_ == nullptr ? nullptr : canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    if (editor == nullptr) {
      return false;
    }
    return handle_text_editor_resize_event(handle, editor, event);
  }

  if (handle_window_resize_event(watched, event)) {
    return true;
  }

  if (watched == menuBar()) {
    auto* bar = menuBar();
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
      position_window_chrome_controls();
    }

    const auto is_chrome_drag_area = [this, bar](const QPoint& position) {
      if (bar->actionAt(position) != nullptr) {
        return false;
      }
      if (window_chrome_controls_ != nullptr) {
        const QRect controls_rect(window_chrome_controls_->pos(), window_chrome_controls_->size());
        if (controls_rect.contains(position)) {
          return false;
        }
      }
      return true;
    };

    switch (event->type()) {
      case QEvent::MouseButtonDblClick: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton && is_chrome_drag_area(mouse_event->pos())) {
          isMaximized() ? restore_window_from_maximize() : showMaximized();
          mouse_event->accept();
          return true;
        }
        break;
      }
      case QEvent::MouseButtonPress: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton && is_chrome_drag_area(mouse_event->pos())) {
          // Dragging the title bar of a maximized window must restore it first. Letting the
          // OS system-move a maximized window leaves Qt's isMaximized() stale, which then
          // disables our edge-resize hit-testing until the next explicit state change. Restore
          // under the cursor (like a native title bar) so the state stays in sync.
          if (isMaximized()) {
            restore_maximized_under_cursor(mouse_event->globalPosition().toPoint());
          }
          chrome_drag_position_ = mouse_event->globalPosition().toPoint() - frameGeometry().topLeft();
          chrome_dragging_ = true;
          if (auto* handle = windowHandle(); handle != nullptr && handle->startSystemMove()) {
            chrome_dragging_ = false;
          }
          mouse_event->accept();
          return true;
        }
        break;
      }
      case QEvent::MouseMove: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (chrome_dragging_ && (mouse_event->buttons() & Qt::LeftButton) != 0) {
          if (!isMaximized() && !isFullScreen()) {
            move(mouse_event->globalPosition().toPoint() - chrome_drag_position_);
          }
          mouse_event->accept();
          return true;
        }
        break;
      }
      case QEvent::MouseButtonRelease:
        chrome_dragging_ = false;
        break;
      default:
        break;
    }
  }

  if (document_tabs_ != nullptr && watched == document_tabs_->tabBar()) {
    auto* tab_bar = document_tabs_->tabBar();
    switch (event->type()) {
      case QEvent::MouseButtonPress: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton) {
          tab_tear_press_index_ = tab_bar->tabAt(mouse_event->position().toPoint());
          tab_tear_press_global_ = mouse_event->globalPosition().toPoint();
        }
        break;
      }
      case QEvent::MouseMove: {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (tab_tear_press_index_ >= 0 && (mouse_event->buttons() & Qt::LeftButton) != 0) {
          const auto global = mouse_event->globalPosition().toPoint();
          const QRect bar_rect(tab_bar->mapToGlobal(QPoint(0, 0)), tab_bar->size());
          // Leaving the bar VERTICALLY tears the tab off; horizontal movement is
          // QTabBar's own reorder drag.
          constexpr int kTearOffMargin = 24;
          if (global.y() < bar_rect.top() - kTearOffMargin || global.y() > bar_rect.bottom() + kTearOffMargin) {
            const auto tear_index = tab_tear_press_index_;
            tab_tear_press_index_ = -1;
            tear_off_document_tab(tear_index, global);
            return true;
          }
        }
        break;
      }
      case QEvent::MouseButtonRelease:
        tab_tear_press_index_ = -1;
        break;
      default:
        break;
    }
    return QMainWindow::eventFilter(watched, event);
  }

  if (watched != document_tabs_) {
    return QMainWindow::eventFilter(watched, event);
  }

  switch (event->type()) {
    case QEvent::DragEnter:
      return accept_open_file_drag(static_cast<QDragEnterEvent*>(event));
    case QEvent::DragMove:
      return accept_open_file_drag(static_cast<QDragMoveEvent*>(event));
    case QEvent::Drop:
      return open_dropped_files(static_cast<QDropEvent*>(event));
    default:
      break;
  }
  return QMainWindow::eventFilter(watched, event);
}


void MainWindow::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslate_ui();
  }
  if (event->type() == QEvent::ActivationChange && isActiveWindow() && canvas_ != nullptr) {
    // Windows resets the cursor to an arrow when the window is re-activated;
    // re-apply the tool cursor so a hovering pen shows the brush immediately
    // instead of waiting for the first move event.
    canvas_->refresh_tool_cursor();
  }
  if (event->type() == QEvent::WindowStateChange) {
    // Maximize/restore/fullscreen each use a different frameless client rect (see
    // nativeEvent's WM_NCCALCSIZE handling). Drop any in-flight edge-resize state left over
    // from the transition and re-sync the frame so edge-resize hit-testing and the dock
    // separators line up with the new geometry.
    chrome_resizing_ = false;
    chrome_resize_edges_ = Qt::Edges{};
    clear_window_resize_cursor();
#ifdef Q_OS_WIN
    // An OS snap (drag-to-top, Win+Up) maximizes with a real native zoom, but Qt restores
    // this frameless window with a plain resize that leaves WS_MAXIMIZE set. A stale zoom
    // bit keeps WM_NCCALCSIZE in its maximized branch and makes the next startSystemMove()
    // run DefWindowProc's restore-on-drag against Windows' stale placement, teleporting
    // the window far from the cursor. Strip the bit in place without touching geometry.
    if (!isMaximized() && !isFullScreen() && !isMinimized()) {
      HWND hwnd = reinterpret_cast<HWND>(winId());
      const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
      if ((style & WS_MAXIMIZE) != 0) {
        SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~WS_MAXIMIZE);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
      }
    }
#endif
    resync_native_frame_geometry();
  }
  QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  // On a cancelled close, floats an OS session-end closeAllWindows already hid
  // (DocumentFloatWindow::closeEvent accepts without closing while
  // isSavingSession) must come back, or their documents stay open but unreachable.
  const auto restore_hidden_floats = [this] {
    for (const auto& target_session : sessions_) {
      if (target_session->float_window != nullptr && !target_session->float_window->isVisible()) {
        target_session->float_window->show();
      }
    }
  };
  if (preview_dialog_edit_locked()) {
    show_preview_dialog_edit_lock_message();
    restore_hidden_floats();
    event->ignore();
    return;
  }
  // Tear down any active inline text editor before shutdown.  Left alive, its
  // QTextEdit (caret-blink timer, event filters, signal lambdas referencing
  // canvas_ and the document) is destroyed after the session's Document is
  // freed, dereferencing freed memory.  Committing matches every other path
  // that leaves an edit (focus loss, tool change, tab switch) and preserves
  // the typed text so it counts toward the unsaved-changes prompt below.
  finish_active_text_editor();
  for (auto& target_session : sessions_) {
    if (target_session != nullptr && !confirm_close_session(*target_session)) {
      restore_hidden_floats();
      event->ignore();
      return;
    }
  }
  save_window_geometry();
  // Flush any tool-option change still waiting on the save debounce; the timer
  // will not fire once the window is gone.
  save_tool_settings();
  // Close the tile preview with the main window: left visible, it has no visible
  // transient parent anymore, so it blocks lastWindowClosed and the process
  // lingers headless with only the preview on screen.
  if (tile_preview_window_ != nullptr) {
    tile_preview_window_->close();
  }
  // Same hazard for floated documents (they are top-level windows): hide, not
  // close - every session was already confirmed above, and close() would run
  // the float's own close request and re-prompt.
  for (const auto& target_session : sessions_) {
    if (target_session->float_window != nullptr) {
      target_session->float_window->hide();
    }
  }
  event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (!accept_open_file_drag(event)) {
    QMainWindow::dragEnterEvent(event);
  }
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
  if (!accept_open_file_drag(event)) {
    QMainWindow::dragMoveEvent(event);
  }
}

void MainWindow::dropEvent(QDropEvent* event) {
  if (!open_dropped_files(event)) {
    QMainWindow::dropEvent(event);
  }
}

MainWindow::~MainWindow() {
  // Runs before member destruction. Closing the window can deliver a focus-out to a
  // still-open inline text editor while the session list is being (or has been) torn
  // down; the commit path must see this flag and bail before touching any member
  // container (observed as an uncaught "No active document" on macOS teardown).
  shutting_down_ = true;
  // The color-picker palette hook captures this window; drop it so a picker
  // created after teardown (tests build windows serially) cannot call into a
  // destroyed MainWindow.
  set_color_picker_document_palette_editor({});
  set_color_picker_document_palette({}, false);
  // Detach every canvas from its Document while sessions_ is still intact.
  // Member destruction frees the session Documents BEFORE ~QWidget runs, and
  // ~QWidget still delivers close/deactivate focus-out events to the live child
  // canvases, whose handlers (set_tool's transform-controls rect, stroke/mask
  // finishes) walk canvas->document_ — the same ordering rule
  // close_document_session enforces by destroying the canvas before erasing the
  // session. Without this, teardown walks a freed Document (heap corruption
  // that surfaced as the linux full-suite pen-test segfault).
  for (const auto& session : sessions_) {
    if (session != nullptr && session->canvas != nullptr) {
      session->canvas->set_document(nullptr);
    }
  }
}

void MainWindow::show_status_error(const QString& text) {
  zoom_status_bar_->show_error_message(text);
}

void MainWindow::configure_canvas(CanvasWidget* canvas) {
  canvas->setObjectName(QStringLiteral("canvas"));
  // A canvas can be born while a preview dialog holds the edit lock (drag & drop,
  // second-instance open); every canvas joins the lock here so no creation path
  // can produce an editable canvas behind an open preview dialog.
  canvas->set_edit_locked(preview_dialog_edit_locked());
  apply_canvas_aid_settings(canvas);
  apply_pen_input_settings(canvas);
  // History callbacks resolve the canvas's OWN session at fire time: with float
  // windows two canvases are live at once, and an edit (or an async completion)
  // must never snapshot whichever document happens to be active.
  canvas->set_before_edit_callback([this, canvas](QString label) {
    if (scripted_stroke_undo_suppressed_) {
      return;  // Stroke Path already pushed one snapshot for the whole command
    }
    if (auto* target_session = session_for_canvas(canvas); target_session != nullptr) {
      push_undo_snapshot(*target_session, std::move(label));
    }
  });
  canvas->set_smart_filter_mask_committed_callback(
      [this, canvas](LayerId layer_id, QString label, PixelBuffer pixels,
                     QRegion) {
        return commit_smart_filter_mask_edit(
            canvas, layer_id, std::move(label), std::move(pixels));
      });
  canvas->set_selection_history_callback(
      [this, canvas](QString label, CanvasWidget::SelectionSnapshot before, bool coalesce) {
        if (auto* target_session = session_for_canvas(canvas); target_session != nullptr) {
          push_selection_history(*target_session, std::move(label), std::move(before), coalesce);
        }
      });
  canvas->set_quick_mask_changed_callback([this, canvas] {
    QTimer::singleShot(0, this, [this, canvas] {
      if (canvas == canvas_) {
        refresh_quick_mask_ui();
      }
    });
  });
  canvas->set_selection_mode_changed_callback([this, canvas](CanvasWidget::SelectionMode mode) {
    if (canvas == canvas_) {
      update_selection_mode_buttons(mode);
      // Canvas-driven mode changes (Quick Select's auto New->Add after a stroke) must land in
      // the per-tool store too, or the next document would revert the tool to New. The equality
      // gate filters the transient Shift/Alt overrides reported by the modifier event filter,
      // which do not change the canvas's stored mode.
      if (mode == canvas->selection_mode()) {
        if (const auto index = CanvasWidget::selection_tool_index(current_tool_); index >= 0) {
          selection_modes_[static_cast<std::size_t>(index)] = mode;
        }
      }
    }
  });
  canvas->set_color_picked_callback([this, canvas](QColor color) {
    canvas->set_primary_color(color);
    if (color_dialog_ != nullptr &&
        color_dialog_->property("patchy.colorTarget").toString() == QStringLiteral("foreground")) {
      if (auto* picker = color_dialog_->findChild<PatchyColorPicker*>(
              QStringLiteral("patchyAdvancedColorPicker"))) {
        // Blocked so the panel's currentColorChanged callback does not echo back into
        // set_primary_color and stomp the status message below.
        const QSignalBlocker blocker(picker);
        picker->setCurrentColor(color);
      }
    }
    refresh_color_buttons();
    statusBar()->showMessage(tr("Picked color %1, %2, %3 (%4)")
                                 .arg(color.red())
                                 .arg(color.green())
                                 .arg(color.blue())
                                 .arg(color.name(QColor::HexRgb).toUpper()));
  });
  canvas->set_pen_button_action_callback(
      [this](PenButtonAction action) { handle_pen_button_action(action); });
  canvas->set_brush_settings_changed_callback([this, canvas] {
    if (canvas != canvas_) {
      return;
    }
    sync_brush_controls_from_canvas();
    refresh_gradient_controls_from_canvas();
    save_tool_settings();
    refresh_document_info();
  });
  canvas->set_text_requested_callback([this](QPoint point, QRect requested_text_box) {
    add_text_at(point, requested_text_box);
  });
  canvas->set_vector_tool_mode(current_vector_tool_mode_);
  canvas->set_vector_shape_drawn_callback(
      [this, canvas](patchy::LiveShapeKind kind, QRectF bounds, QPointF line_start, QPointF line_end) {
        if (canvas != canvas_) {
          return;
        }
        handle_vector_shape_drawn(kind, bounds, line_start, line_end);
      });
  canvas->set_vector_path_committed_callback(
      [this, canvas](patchy::VectorPath path, bool closed, VectorPathSource source) {
        if (canvas != canvas_) {
          return;
        }
        handle_vector_path_committed(std::move(path), closed, source);
      });
  canvas->set_path_display_dismiss_callback([this, canvas] {
    if (canvas != canvas_) {
      return;
    }
    handle_paths_panel_deselect();
    refresh_paths_panel();
  });
  canvas->set_path_load_selection_callback([this, canvas] {
    if (canvas != canvas_ || paths_panel_ == nullptr) {
      return;
    }
    if (const auto row = paths_panel_->selected_row(); row.has_value()) {
      load_path_as_selection(static_cast<int>(row->kind), row->id);
    }
  });
  canvas->set_path_edited_callback([this, canvas] {
    if (canvas != canvas_) {
      return;
    }
    refresh_paths_panel();
  });
  canvas->set_shape_preview_appearance_callback(
      [this]() -> std::optional<CanvasWidget::ShapePreviewAppearance> {
        // Pulled at draw time from the application-wide options-bar state, so
        // the preview can never desync from what the commit will produce.
        CanvasWidget::ShapePreviewAppearance appearance;
        appearance.fill = vector_fill_preview_brush(current_vector_fill_);
        appearance.stroke_enabled = current_vector_stroke_enabled_;
        appearance.stroke = vector_fill_preview_brush(current_vector_stroke_paint_);
        appearance.stroke_width = current_vector_stroke_width_;
        appearance.line_weight = current_vector_line_weight_;
        return appearance;
      });
  canvas->set_polygon_sides(
      findChild<QSpinBox*>(QStringLiteral("polygonSidesSpin")) != nullptr
          ? findChild<QSpinBox*>(QStringLiteral("polygonSidesSpin"))->value()
          : 5);
  canvas->set_polygon_star_inset(
      findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin")) != nullptr
          ? findChild<QSpinBox*>(QStringLiteral("polygonStarInsetSpin"))->value()
          : 0);
  canvas->set_active_layer_changed_callback([this, canvas](LayerId layer_id) {
    if (canvas != canvas_) {
      return;
    }
    reveal_layer_in_layer_list(layer_id);
    refresh_layer_controls();
    refresh_options_bar();
    // The Paths panel's transient layer-path row (and its auto-targeting)
    // follows the active layer; canvas-driven changes (Move-tool auto-select)
    // bypass set_active_layer_from_selection.
    refresh_paths_panel();
  });
  canvas->set_status_callback([this](QString message) { statusBar()->showMessage(message); });
  canvas->set_error_status_callback([this](QString message) { show_status_error(message); });
  canvas->set_ruler_unit_change_requested_callback(
      [this](MeasurementUnit unit) { set_ruler_unit_preference(unit); });
  canvas->set_info_callback([this, canvas](CanvasInfoState info) {
    if (canvas != canvas_) {
      return;
    }
    update_canvas_info(std::move(info));
  });
  canvas->set_document_changed_callback([this, canvas](CanvasWidget::DocumentChangeReason reason) {
    if (canvas != canvas_) {
      return;
    }
    if (canvas->editing_document_channel()) {
      if (reason == CanvasWidget::DocumentChangeReason::BrushStrokePreview) {
        pending_channel_thumbnail_refresh_ = true;
        return;
      }
      pending_channel_thumbnail_refresh_ = false;
      refresh_channel_panel();
      refresh_document_info();
      return;
    }
    if (reason == CanvasWidget::DocumentChangeReason::BrushStrokePreview) {
      pending_layer_thumbnail_refresh_ = true;
      return;
    }
    if (pending_layer_thumbnail_refresh_ || reason == CanvasWidget::DocumentChangeReason::BrushStrokeFinished) {
      pending_layer_thumbnail_refresh_ = false;
    }
    refresh_layer_thumbnails();
  });
  canvas->set_view_changed_callback([this, canvas] { handle_canvas_view_changed(canvas); });
  canvas->set_transform_controls_changed_callback([this, canvas] {
    if (canvas == canvas_) {
      refresh_options_bar();
    }
  });
  canvas->set_smart_object_transform_render_callback([this, canvas](LayerId id) -> bool {
    auto* owner_session = session_for_canvas(canvas);
    if (owner_session == nullptr) {
      return false;
    }
    auto* layer = owner_session->document.find_layer(id);
    if (layer == nullptr) {
      return false;
    }
    const auto parent_document_dir =
        owner_session->path.isEmpty()
            ? QString()
            : QFileInfo(owner_session->path).absolutePath();
    const auto refreshed = refresh_smart_object_layer_preview(
        owner_session->document, *layer, canvas->transform_interpolation(),
        true, parent_document_dir);
    if (!refreshed) {
      show_status_error(
          tr("Could not rebuild the Smart Filter preview and cache"));
    }
    return refreshed;
  });
  canvas->set_text_layer_transform_render_callback([this, canvas](LayerId id) -> bool {
    auto* session = session_for_canvas(canvas);
    if (session == nullptr) {
      return false;
    }
    auto* layer = session->document.find_layer(id);
    if (layer == nullptr || !layer_is_text(*layer)) {
      return false;
    }
    const auto transform = canonical_text_affine_transform_for_layer(*layer);
    if (!transform.has_value()) {
      return false;
    }
    if (const auto warp = text_warp_from_layer(*layer); warp.has_value() && !text_warp_is_identity(*warp)) {
      if (layer->metadata().contains(kLayerMetadataPsdTextTransform)) {
        // Imported warped text keeps Photoshop's raster; transforms resample it
        // (re-rendering would need the PSD glyph alignment the warp box predates).
        return false;
      }
      // Patchy-authored warped text: fold scale into the point size like the
      // unwarped path, then re-render through the warp surface with a box freshly
      // derived from the (possibly rescaled) layout.
      const auto residual = fold_text_transform_scale_into_font_size(*layer, qtransform_from_affine(*transform));
      const auto inputs = text_render_inputs_from_layer(*layer);
      if (!inputs.has_value()) {
        return false;
      }
      TextWarp refreshed = *warp;
      refreshed.bounds_left = 0.0;
      refreshed.bounds_top = 0.0;
      refreshed.bounds_right = 0.0;
      refreshed.bounds_bottom = 0.0;
      auto rendered = render_warped_text_pixels_for_layer(*inputs, refreshed, residual, &refreshed);
      if (!rendered.has_value()) {
        return false;
      }
      layer->set_pixels(std::move(rendered->pixels));
      layer->set_bounds(rendered->bounds);
      layer->metadata()[kLayerMetadataTextWarp] = serialize_text_warp(refreshed);
      return true;
    }
    if (layer->metadata().contains(kLayerMetadataPsdTextTransform)) {
      // PSD type layers anchor their transform at the typographic baseline, so rendering the glyph
      // raster (top-left origin) through it directly would drop the text ~one ascent.  Render crisp
      // through the glyph-top-aligned transform instead (the same alignment the editor uses), keeping
      // the imported size + matrix representation.  Falls back to the resampled bitmap when it can't be
      // aligned (box text, missing PSD glyph metrics, or a pure move with no scale).
      //
      // Only do this when the original font is installed: re-rasterizing a missing font would
      // substitute a generic face (e.g. Arial), so the decorative type would change appearance on
      // scale.  In that case keep the resampled bitmap, which preserves the imported glyph shapes.
      const auto font_value = layer->metadata().find(kLayerMetadataTextFont);
      const QString family =
          font_value == layer->metadata().end() ? QString() : QString::fromStdString(font_value->second);
      const auto runs_value = layer->metadata().find(kLayerMetadataTextRuns);
      const QString runs =
          runs_value == layer->metadata().end() ? QString() : QString::fromStdString(runs_value->second);
      const bool font_substituted = family.trimmed().isEmpty() ||
                                    family.compare(QStringLiteral("PSD Text"), Qt::CaseInsensitive) == 0 ||
                                    !missing_text_families_for_psd_raster_preview(family, runs).isEmpty();
      if (font_substituted) {
        return false;
      }
      const auto flow = layer->metadata().find(kLayerMetadataTextFlow);
      const bool boxed =
          flow != layer->metadata().end() && text_flow_is_box(QString::fromStdString(flow->second));
      const auto base_pixels = render_text_layer_pixels_from_metadata(*layer);
      if (!base_pixels.has_value()) {
        return false;
      }
      const auto aligned = psd_point_text_local_bounds_transform_for_pixels(*layer, *base_pixels, boxed,
                                                                            layer_anchor_alignment_factor(*layer));
      if (!aligned.has_value()) {
        return false;
      }
      auto rendered = render_text_layer_pixels_through_transform(*layer, qtransform_from_affine(*aligned));
      if (!rendered.has_value()) {
        return false;
      }
      layer->set_pixels(std::move(rendered->pixels));
      layer->set_bounds(rendered->bounds);
      return true;
    }
    // Patchy-authored text: fold the transform's scale into the point size (so the Type panel and a
    // saved PSD show the new size, matching imported text), then re-rasterize through the residual.
    const auto residual = fold_text_transform_scale_into_font_size(*layer, qtransform_from_affine(*transform));
    auto rendered = render_text_layer_pixels_through_transform(*layer, residual);
    if (!rendered.has_value()) {
      return false;
    }
    layer->set_pixels(std::move(rendered->pixels));
    layer->set_bounds(rendered->bounds);
    return true;
  });
}

void MainWindow::add_text_at(QPoint document_point, QRect requested_text_box) {
  if (canvas_ == nullptr) {
    return;
  }
  if (canvas_->free_transform_active()) {
    canvas_->finish_free_transform();
  }
  if (current_tool_ != CanvasTool::Text) {
    if (type_tool_action_ != nullptr) {
      type_tool_action_->trigger();
    } else {
      current_tool_ = CanvasTool::Text;
      canvas_->set_tool(current_tool_);
      refresh_options_bar();
      refresh_document_info();
    }
  }
  if (canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) != nullptr) {
    finish_active_text_editor();
  }

  const QPoint requested_document_point = document_point;
  std::optional<LayerId> editing_layer;
  std::optional<bool> editing_layer_was_visible;
  std::optional<LayerId> restore_active_layer = document().active_layer_id();
  bool editing_layer_needs_preview = false;
  bool editing_layer_expensive_preview = false;
  bool editing_layer_uses_source_raster_preview = false;
  bool editing_layer_force_baked_preview = false;
  bool editing_layer_uses_psd_text_frame = false;
  bool editing_layer_has_transformed_preview = false;
  bool editing_layer_uses_extended_box_preview = false;
  bool editing_layer_uses_line_aware_box_preview = false;
  std::optional<QPointF> editing_layer_source_visible_anchor;
  std::optional<QSizeF> editing_layer_source_visible_size;
  std::optional<QRectF> editing_layer_render_local_rect;
  QString initial_text;
  QString initial_html;
  QString initial_rich_text_runs;
  QString initial_paragraph_runs;
  bool photoshop_text_layout = false;
  double text_size_display_scale = 1.0;
  std::optional<QPointF> initial_cursor_local_position;
  QString family = text_font_combo_ != nullptr ? text_font_combo_->currentFont().family() : font().family();
  int document_text_size = text_size_spin_ != nullptr ? text_points_to_pixels(text_size_spin_->value(), document()) : 48;
  bool text_bold = text_bold_button_ != nullptr && text_bold_button_->isChecked();
  bool text_italic = text_italic_button_ != nullptr && text_italic_button_->isChecked();
  int text_anti_alias = text_smoothing_combo_value(text_smoothing_combo_);
  QColor text_color = canvas_->primary_color();
  bool boxed_text = requested_text_box.isValid() && requested_text_box.width() >= kMinimumTextBoxDocumentSize &&
                    requested_text_box.height() >= kMinimumTextBoxDocumentSize;
  if (boxed_text) {
    requested_text_box = requested_text_box.normalized();
    document_point = requested_text_box.topLeft();
  }
  int document_editor_width =
      boxed_text ? requested_text_box.width() : std::max(160, std::min(520, document().width() - document_point.x() - 8));
  int document_editor_height = boxed_text ? requested_text_box.height() : 96;
  if (const auto active = document().active_layer_id(); active.has_value()) {
    if (auto* layer = document().find_layer(*active); layer != nullptr && layer_is_text(*layer) &&
        layer->bounds().contains(document_point.x(), document_point.y())) {
      if (layer_id_locks_image_pixels(*active)) {
        show_status_error(tr("Layer pixels are locked."));
        return;
      }
      editing_layer = *active;
      editing_layer_was_visible = layer->visible();
      initial_text = QString::fromStdString(layer->metadata().at(kLayerMetadataText));
      if (const auto found = layer->metadata().find(kLayerMetadataTextHtml); found != layer->metadata().end()) {
        initial_html = QString::fromStdString(found->second);
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextRuns); found != layer->metadata().end()) {
        initial_rich_text_runs = QString::fromStdString(found->second);
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextParagraphRuns); found != layer->metadata().end()) {
        initial_paragraph_runs = QString::fromStdString(found->second);
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextFont); found != layer->metadata().end()) {
        const auto stored_family = QString::fromStdString(found->second);
        if (stored_family.compare(QStringLiteral("PSD Text"), Qt::CaseInsensitive) != 0) {
          family = canonical_text_display_family(stored_family);
        }
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextSize); found != layer->metadata().end()) {
        document_text_size = std::max(1, std::atoi(found->second.c_str()));
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextColor); found != layer->metadata().end()) {
        const QColor stored(QString::fromStdString(found->second));
        if (stored.isValid()) {
          text_color = stored;
        }
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextBold); found != layer->metadata().end()) {
        text_bold = found->second == "true";
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextItalic); found != layer->metadata().end()) {
        text_italic = found->second == "true";
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextAntiAlias); found != layer->metadata().end()) {
        text_anti_alias = std::clamp(std::atoi(found->second.c_str()), 0, 16);
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextLayoutMode);
          found != layer->metadata().end()) {
        photoshop_text_layout = found->second == kTextLayoutModePhotoshop;
      }
      if (photoshop_text_layout) {
        if (const auto affine = canonical_text_affine_transform_for_layer(*layer); affine.has_value()) {
          const auto vertical_scale = std::hypot((*affine)[2], (*affine)[3]);
          if (std::isfinite(vertical_scale) && vertical_scale > 0.01) {
            text_size_display_scale = vertical_scale;
          }
        }
      }
      if (const auto found = layer->metadata().find(kLayerMetadataTextRasterStatus);
          found != layer->metadata().end()) {
        editing_layer_uses_source_raster_preview =
            *editing_layer_was_visible && found->second == "psd_raster_preview";
      }
      if (editing_layer_uses_source_raster_preview && !cli_automation_mode_) {
        // Automation (run_cli_export) substitutes silently: the whole point of its edit
        // sessions is forcing Patchy's own render, and a prompt would block unattended runs.
        const auto missing_fonts = missing_text_families_for_psd_raster_preview(family, initial_rich_text_runs);
        if (!confirm_psd_raster_preview_font_substitution(this, missing_fonts)) {
          statusBar()->showMessage(tr("Canceled text edit"));
          return;
        }
      }
      if (text_font_combo_ != nullptr) {
        QSignalBlocker blocker(text_font_combo_);
        text_font_combo_->setCurrentFont(text_font_combo_font_for_family(family));
      }
      if (text_size_spin_ != nullptr) {
        text_size_spin_->setValue(text_pixels_to_points(
            std::max(1, static_cast<int>(std::lround(document_text_size * text_size_display_scale))), document()));
      }
      if (text_bold_button_ != nullptr) {
        text_bold_button_->setChecked(text_bold);
      }
      if (text_italic_button_ != nullptr) {
        text_italic_button_->setChecked(text_italic);
      }
      set_text_smoothing_combo_value(text_smoothing_combo_, text_anti_alias);
      boxed_text = text_flow_is_box(
          layer->metadata().contains(kLayerMetadataTextFlow)
              ? QString::fromStdString(layer->metadata().at(kLayerMetadataTextFlow))
              : QString::fromLatin1(kTextFlowPoint));
      const auto text_transform = patchy_text_transform_for_layer(*layer);
      const auto psd_frame = psd_text_frame_rect(*layer);
      const bool using_psd_frame = psd_frame.has_value() && boxed_text;
      editing_layer_uses_psd_text_frame = layer_should_edit_with_psd_text_frame(*layer, boxed_text) && using_psd_frame;
      editing_layer_has_transformed_preview =
          text_transform.has_value() && qtransform_has_non_translation_linear_part(*text_transform) &&
          !editing_layer_uses_psd_text_frame;
      if (text_transform.has_value() && !editing_layer_uses_psd_text_frame) {
        const auto text_affine_transform = canonical_text_affine_transform_for_layer(*layer);
        // A PSD-imported point-text layer stores its transform translation at the typographic
        // baseline, which sits well below the visible glyph top.  When the transform is a pure
        // translation we anchor the editor to the live visible glyph top instead of the transform
        // origin so the caret lands on the glyphs.  The anchor is derived from the current raster,
        // so it stays valid for any translation-only point text -- a fresh PSD import, a layer the
        // user moved, or one already committed by Patchy (whose transform origin would drop the
        // editor onto the baseline and make the text jump down ~one ascent on edit).  Capturing it
        // every session also records the visible width, which the commit needs to keep the
        // justification point of centered/right text pinned across repeated edits.
        const bool can_use_psd_visual_origin =
            !boxed_text && text_affine_transform.has_value() &&
            !affine_transform_has_non_translation_linear_part(*text_affine_transform);
        const auto psd_visible_rect =
            can_use_psd_visual_origin ? psd_point_text_source_visible_rect(*layer) : std::optional<QRectF>{};
        if (psd_visible_rect.has_value()) {
          editing_layer_source_visible_anchor = psd_visible_rect->topLeft();
          editing_layer_source_visible_size = psd_visible_rect->size();
          document_point = QPoint(static_cast<int>(std::floor(psd_visible_rect->left())),
                                  static_cast<int>(std::floor(psd_visible_rect->top())));
        } else {
          bool invertible = false;
          const auto inverse = text_transform->inverted(&invertible);
          if (invertible) {
            initial_cursor_local_position = inverse.map(QPointF(requested_document_point));
          }
          const auto transformed_origin = text_transform->map(QPointF(0.0, 0.0));
          document_point = QPoint(static_cast<int>(std::floor(transformed_origin.x())),
                                  static_cast<int>(std::floor(transformed_origin.y())));
        }
      } else if (using_psd_frame) {
        document_point = QPoint(static_cast<int>(std::floor(psd_frame->left())),
                                static_cast<int>(std::floor(psd_frame->top())));
        document_editor_width =
            std::max(kMinimumTextBoxDocumentSize, static_cast<int>(std::ceil(psd_frame->width())));
        document_editor_height =
            std::max(kMinimumTextBoxDocumentSize, static_cast<int>(std::ceil(psd_frame->height())));
      } else {
        document_point = QPoint(layer->bounds().x, layer->bounds().y);
      }
      if (boxed_text && (!using_psd_frame || (text_transform.has_value() && !editing_layer_uses_psd_text_frame))) {
        if (const auto found = layer->metadata().find(kLayerMetadataTextBoxWidth); found != layer->metadata().end()) {
          document_editor_width = std::max(kMinimumTextBoxDocumentSize, std::atoi(found->second.c_str()));
        } else {
          document_editor_width = std::max(kMinimumTextBoxDocumentSize, layer->bounds().width);
        }
        if (const auto found = layer->metadata().find(kLayerMetadataTextBoxHeight); found != layer->metadata().end()) {
          document_editor_height = std::max(kMinimumTextBoxDocumentSize, std::atoi(found->second.c_str()));
        } else {
          document_editor_height = std::max(kMinimumTextBoxDocumentSize, layer->bounds().height);
        }
      } else if (!boxed_text) {
        if (const auto found = layer->metadata().find(kLayerMetadataTextBoxWidth); found != layer->metadata().end()) {
          document_editor_width = std::max(160, std::atoi(found->second.c_str()));
        } else {
          document_editor_width = std::max(160, layer->bounds().width);
        }
        if (const auto found = layer->metadata().find(kLayerMetadataTextBoxHeight); found != layer->metadata().end()) {
          document_editor_height = std::max(64, std::atoi(found->second.c_str()));
        } else {
          document_editor_height = std::max(64, layer->bounds().height + document_text_size);
        }
      }
      if (boxed_text) {
        const QRectF frame_rect(0.0, 0.0, static_cast<qreal>(document_editor_width),
                                static_cast<qreal>(document_editor_height));
        if (editing_layer_uses_psd_text_frame) {
          editing_layer_uses_line_aware_box_preview = true;
          editing_layer_render_local_rect = psd_box_text_editor_render_local_rect(*layer).value_or(frame_rect);
          if (rect_extends_beyond(*editing_layer_render_local_rect, frame_rect)) {
            editing_layer_uses_extended_box_preview = true;
            if (!editing_layer_source_visible_anchor.has_value()) {
              editing_layer_source_visible_anchor = layer_source_visible_anchor(*layer);
            }
          }
        } else {
          const auto raster_status = layer->metadata().find(kLayerMetadataTextRasterStatus);
          const bool patchy_box_raster =
              raster_status != layer->metadata().end() && raster_status->second == "patchy_raster";
          const bool patchy_box_raster_uses_line_aware_preview =
              patchy_box_raster && layer->metadata().contains(kLayerMetadataTextLineAwareBoxPreview);
          const bool patchy_box_raster_has_linear_transform =
              text_transform.has_value() && qtransform_has_non_translation_linear_part(*text_transform);
          if (!patchy_box_raster_has_linear_transform) {
            if (const auto render_rect =
                    patchy_box_text_editor_render_local_rect(*layer, document_editor_width, document_editor_height);
                render_rect.has_value()) {
              editing_layer_render_local_rect = *render_rect;
              editing_layer_uses_extended_box_preview = true;
              editing_layer_uses_line_aware_box_preview = true;
              editing_layer_source_visible_anchor = layer_source_visible_anchor(*layer);
            } else if (patchy_box_raster_uses_line_aware_preview) {
              editing_layer_render_local_rect = frame_rect;
              editing_layer_uses_line_aware_box_preview = true;
            }
          } else if (patchy_box_raster_uses_line_aware_preview) {
            editing_layer_render_local_rect = frame_rect;
            editing_layer_uses_line_aware_box_preview = true;
          }
        }
      }
      editing_layer_needs_preview = *editing_layer_was_visible &&
                                    (layer_requires_text_editor_preview(*layer) ||
                                     editing_layer_uses_extended_box_preview ||
                                     editing_layer_uses_line_aware_box_preview);
      // Text edits show the live render from the very start of the session.  Once the user is
      // past the substitution warning (it only appears when a font is missing; cancelling it
      // returned above), keeping Photoshop's raster on screen made the caret and selection
      // unmatchable: they are computed from Patchy's layout, which wraps and meters differently
      // from Photoshop's raster (hyphenation, substituted faces), and the font swap then landed
      // mid-typing.  Showing the live render immediately keeps glyphs, caret, and selection in
      // one geometry.  Applying the session -- even with no text change -- commits that live
      // render (apply keeps what you saw); Escape is the way to leave Photoshop's original
      // pixels untouched.
      if (editing_layer_uses_source_raster_preview && editing_layer_was_visible.value_or(true)) {
        editing_layer_uses_source_raster_preview = false;
      }
      // Plain point text would otherwise render with the editor's own widget glyphs, which are
      // rasterized differently from render_text_pixels (the committed layer's renderer):
      // entering/leaving edit then visibly shifts the text and changes its antialiasing.  Drive
      // the edit through the same live baked-preview path that effect text uses (the glyphs are
      // re-rasterized by render_text_pixels every keystroke), so the on-screen result comes from
      // the exact renderer the commit uses -- no shift, and the caret still lands on the glyphs.
      // Transformed point text never reaches this block: a non-translation transform already
      // counts as requiring a preview (layer_requires_text_editor_preview), so those sessions
      // render live through the regular preview path from the start.
      if (editing_layer_was_visible.value_or(true) && !boxed_text && !editing_layer_needs_preview) {
        editing_layer_needs_preview = true;
        editing_layer_force_baked_preview = true;
      }
      const auto document_bounds = Rect::from_size(document().width(), document().height());
      editing_layer_expensive_preview =
          editing_layer_needs_preview && layer_style_preview_is_expensive(*layer, document_bounds);
      if (!editing_layer_uses_source_raster_preview) {
        layer->set_visible(false);
        canvas_->document_changed_effect_bounds(to_qrect(layer_render_bounds(*layer)));
      }
    }
  }

  std::optional<LayerId> provisional_layer;
  if (!editing_layer.has_value()) {
    // Photoshop shows the new type layer the moment the tool clicks, so a NEW session inserts
    // a provisional (1x1 transparent, marker-tagged) text layer immediately. No undo snapshot
    // and no modified flag here: commit removes it again before taking the single "Type"
    // snapshot and recreates the committed layer under the same id, while cancel and an empty
    // commit remove it outright, leaving history exactly as before the click.
    TextToolSettings placeholder_settings{tr("Type"),
                                          QString(),
                                          family,
                                          document_text_size,
                                          text_bold,
                                          text_italic,
                                          text_anti_alias,
                                          boxed_text,
                                          document_editor_width,
                                          document_editor_height};
    Layer provisional(document().allocate_layer_id(), placeholder_settings.text.toStdString(),
                      make_solid_pixels(1, 1, QColor(0, 0, 0, 0), PixelFormat::rgba8()));
    provisional.set_bounds(Rect{document_point.x(), document_point.y(), 1, 1});
    store_patchy_text_metadata(provisional, placeholder_settings, text_color, QString(), QString(),
                               document_editor_width, document_editor_height);
    provisional.metadata()[kLayerMetadataProvisionalTextMarker] = "true";
    provisional_layer = provisional.id();
    document().add_layer(std::move(provisional));
    refresh_layer_list();
    refresh_layer_controls();
    canvas_->document_changed_effect_bounds(QRect(document_point, QSize(1, 1)));
  }

  auto* editor = new InlineTextEdit(canvas_);
  editor->setObjectName(QStringLiteral("inlineTextEditor"));
  editor->setAcceptRichText(true);
  auto editor_font = render_text_font_for_display_family(
      family, std::max(8, static_cast<int>(std::round(document_text_size * canvas_->zoom()))), text_bold,
      text_italic, text_anti_alias);
  editor->setFont(editor_font);
  editor->document()->setDocumentMargin(0);
  editor->document()->setDefaultFont(editor_font);
  editor->setFrameShape(QFrame::NoFrame);
  editor->setAttribute(Qt::WA_TranslucentBackground, true);
  editor->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
  editor->viewport()->setAutoFillBackground(false);
  editor->setLineWrapMode(boxed_text ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
  editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor->setProperty("patchy.documentTextSize", document_text_size);
  editor->setProperty("patchy.documentTextWidth", document_editor_width);
  editor->setProperty("patchy.documentTextHeight", document_editor_height);
  editor->setProperty("patchy.documentTextFlow", text_flow_metadata_value(boxed_text));
  editor->setProperty("patchy.documentTextAntiAlias", text_anti_alias);
  editor->setProperty("patchy.textLayoutMode",
                      photoshop_text_layout ? QString::fromLatin1(kTextLayoutModePhotoshop) : QString());
  editor->setProperty("patchy.textSizeDisplayScale", text_size_display_scale);
  editor->setProperty("patchy.documentTextFamily", family);
  editor->setProperty("patchy.editorZoom", canvas_->zoom());
  editor->setProperty("patchy.documentTextColor", text_color);
  editor->setProperty("patchy.documentTextX", document_point.x());
  editor->setProperty("patchy.documentTextY", document_point.y());
  editor->setProperty(kTextEditorPreviewEnabledProperty, editing_layer_needs_preview);
  editor->setProperty(kTextEditorPreviewExpensiveProperty, editing_layer_expensive_preview);
  editor->setProperty(kTextEditorPreviewPaintProperty,
                      editing_layer_uses_source_raster_preview ||
                          (editing_layer_needs_preview &&
                           (!editing_layer_expensive_preview || editing_layer_has_transformed_preview)));
  editor->setProperty(kTextEditorPreviewGenerationProperty, 0);
  editor->setProperty(kTextEditorChangedProperty, false);
  editor->setProperty(kTextEditorSourceRasterPreviewProperty, editing_layer_uses_source_raster_preview);
  editor->setProperty(kTextEditorForceBakedPreviewProperty, editing_layer_force_baked_preview);
  editor->setProperty(kTextEditorExtendedBoxPreviewProperty, editing_layer_uses_extended_box_preview);
  editor->setProperty(kTextEditorLineAwareBoxPreviewProperty, editing_layer_uses_line_aware_box_preview);
  editor->setProperty("patchy.usesPsdTextFrame", editing_layer_uses_psd_text_frame);
  if (editing_layer_render_local_rect.has_value()) {
    set_text_editor_render_local_rect(*editor, *editing_layer_render_local_rect);
  }
  if (editing_layer_source_visible_anchor.has_value()) {
    editor->setProperty(kTextEditorSourceVisibleAnchorProperty, QVariant::fromValue(*editing_layer_source_visible_anchor));
  }
  if (editing_layer_source_visible_size.has_value()) {
    editor->setProperty(kTextEditorSourceVisibleSizeProperty, QVariant::fromValue(*editing_layer_source_visible_size));
  }
  if (restore_active_layer.has_value()) {
    editor->setProperty("patchy.restoreActiveLayerId", QVariant::fromValue<qulonglong>(*restore_active_layer));
  }
  if (editing_layer.has_value()) {
    editor->setProperty("patchy.editingLayerId", QVariant::fromValue<qulonglong>(*editing_layer));
  }
  if (provisional_layer.has_value()) {
    editor->setProperty(kTextEditorProvisionalLayerProperty, QVariant::fromValue<qulonglong>(*provisional_layer));
  }
  if (editing_layer_was_visible.has_value()) {
    editor->setProperty("patchy.editingLayerWasVisible", *editing_layer_was_visible);
  }
  editor->setStyleSheet(inline_text_editor_style(text_color, editor_font.pixelSize()));
  if (!initial_rich_text_runs.trimmed().isEmpty()) {
    auto document_font =
        render_text_font_for_display_family(family, std::max(1, document_text_size), text_bold, text_italic,
                                            text_anti_alias);
    editor->setPlainText(initial_text.isEmpty() ? tr("Type") : initial_text);
    apply_patchy_text_runs_to_document(*editor->document(), initial_rich_text_runs, document_font, text_color,
                                       canvas_->zoom(), text_anti_alias);
    if (!initial_paragraph_runs.trimmed().isEmpty()) {
      apply_paragraph_runs_to_document(*editor->document(), initial_paragraph_runs, canvas_->zoom());
    }
    apply_text_smoothing_to_document(*editor->document(), text_anti_alias);
    auto typing_format = text_editor_typing_format(editor_font, text_color);
    if (const auto leading = text_leading_from_document_formats(*editor->document()); leading.has_value()) {
      typing_format.setProperty(kTextLeadingFormatProperty, *leading);
    }
    editor->setCurrentCharFormat(typing_format);
  } else if (!initial_html.trimmed().isEmpty()) {
    auto document_font =
        render_text_font_for_display_family(family, std::max(1, document_text_size), text_bold, text_italic,
                                            text_anti_alias);
    editor->setHtml(document_html_for_editor(initial_html, document_font, canvas_->zoom()));
    if (!initial_paragraph_runs.trimmed().isEmpty()) {
      apply_paragraph_runs_to_document(*editor->document(), initial_paragraph_runs, canvas_->zoom());
    }
    apply_text_smoothing_to_document(*editor->document(), text_anti_alias);
  } else {
    editor->setPlainText(initial_text.isEmpty() ? tr("Type") : initial_text);
    QTextCursor cursor(editor->document());
    cursor.select(QTextCursor::Document);
    const auto format = text_editor_typing_format(editor_font, text_color);
    cursor.mergeCharFormat(format);
    editor->setCurrentCharFormat(format);
    if (!initial_paragraph_runs.trimmed().isEmpty()) {
      apply_paragraph_runs_to_document(*editor->document(), initial_paragraph_runs, canvas_->zoom());
    }
    apply_text_smoothing_to_document(*editor->document(), text_anti_alias);
  }
  if (editing_layer.has_value() && editing_layer_uses_line_aware_box_preview) {
    if (auto* layer = document().find_layer(*editing_layer); layer != nullptr) {
      if (const auto scale =
              calibrated_box_text_metric_scale_for_editor(*editor, *layer, canvas_->zoom(), text_color);
          scale.has_value()) {
        set_text_editor_metric_scale(*editor, *scale);
      }
    }
  }
  if (editing_layer_source_visible_anchor.has_value()) {
    update_text_editor_transform_from_source_anchor(*editor, canvas_->zoom(), text_color);
    document_point = QPoint(editor->property("patchy.documentTextX").toInt(),
                            editor->property("patchy.documentTextY").toInt());
  } else if (editing_layer.has_value()) {
    if (auto* layer = document().find_layer(*editing_layer); layer != nullptr) {
      if (const auto pixels = render_text_editor_pixels_for_source_anchor(*editor, canvas_->zoom(), text_color);
          pixels.has_value() &&
          update_text_editor_transform_from_psd_local_bounds(*editor, *layer, *pixels, boxed_text)) {
        document_point = QPoint(editor->property("patchy.documentTextX").toInt(),
                                editor->property("patchy.documentTextY").toInt());
        if (const auto transform = text_editor_transform_override(*editor); transform.has_value()) {
          bool invertible = false;
          const auto inverse = qtransform_from_affine(*transform).inverted(&invertible);
          if (invertible) {
            initial_cursor_local_position = inverse.map(QPointF(requested_document_point));
          }
        }
      }
    }
  }
  if (!editing_layer.has_value()) {
    editor->selectAll();
  }
  const auto widget_point = canvas_->widget_position_for_document_point(document_point);
  editor->setGeometry(widget_point.x(), widget_point.y(),
                      std::max(80, static_cast<int>(std::round(document_editor_width * canvas_->zoom()))),
                      std::max(32, static_cast<int>(std::round(document_editor_height * canvas_->zoom()))));
  const auto resize_editor = [editor = QPointer<QTextEdit>(editor), this] {
    if (editor == nullptr) {
      return;
    }
    if (editor->property("patchy.relayoutingTextEditor").toBool()) {
      return;
    }
    relayout_text_editor(editor, true);
    reset_text_editor_scroll(editor);
    schedule_text_editor_preview(editor);
  };
  connect(editor, &QTextEdit::textChanged, editor, [this, editor = QPointer<QTextEdit>(editor), resize_editor] {
    mark_text_editor_changed(editor);
    resize_editor();
  });
  const auto lock_scroll = [editor = QPointer<QTextEdit>(editor)] {
    if (editor != nullptr) {
      reset_text_editor_scroll(editor);
    }
  };
  connect(editor->horizontalScrollBar(), &QScrollBar::valueChanged, editor, [lock_scroll](int) { lock_scroll(); });
  connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, editor, [lock_scroll](int) { lock_scroll(); });
  connect(editor->horizontalScrollBar(), &QScrollBar::rangeChanged, editor, [lock_scroll](int, int) { lock_scroll(); });
  connect(editor->verticalScrollBar(), &QScrollBar::rangeChanged, editor, [lock_scroll](int, int) { lock_scroll(); });
  connect(editor, &QTextEdit::cursorPositionChanged, editor, [this, editor = QPointer<QTextEdit>(editor)] {
    if (editor == nullptr) {
      return;
    }
    reset_text_editor_scroll(editor);
    sync_text_options_from_active_editor();
    sync_text_alignment_buttons_from_editor();
    update_text_editor_preview_caret(*editor, canvas_ != nullptr ? canvas_->zoom() : 1.0);
    update_text_editor_transform_overlay(editor);
  });
  connect(editor, &QTextEdit::selectionChanged, editor, [this, editor = QPointer<QTextEdit>(editor)] {
    sync_text_options_from_active_editor();
    if (editor != nullptr) {
      update_text_editor_preview_caret(*editor, canvas_ != nullptr ? canvas_->zoom() : 1.0);
      update_text_editor_transform_overlay(editor);
    }
  });
  editor->show();
  editor->installEventFilter(this);
  editor->viewport()->installEventFilter(this);
  resize_editor();
  if (editing_layer.has_value()) {
    QPoint click_viewport_point;
    if (initial_cursor_local_position.has_value()) {
      click_viewport_point =
          QPoint(static_cast<int>(std::round(initial_cursor_local_position->x() * canvas_->zoom())),
                 static_cast<int>(std::round(initial_cursor_local_position->y() * canvas_->zoom())));
    } else {
      const auto click_widget_point = canvas_->widget_position_for_document_point(requested_document_point);
      click_viewport_point = editor->viewport()->mapFrom(canvas_, click_widget_point);
    }
    editor->setTextCursor(editor->cursorForPosition(click_viewport_point));
  }
  update_text_editor_handles(editor);
  if (editor->property(kTextEditorPreviewEnabledProperty).toBool() &&
      (!editor->property(kTextEditorPreviewExpensiveProperty).toBool() || editing_layer_has_transformed_preview)) {
    update_text_editor_preview(editor);
  }
  schedule_text_editor_preview(editor);
  editor->setFocus(Qt::OtherFocusReason);
  sync_text_options_from_active_editor();
  sync_text_alignment_buttons_from_editor();
  update_text_editor_preview_caret(*editor, canvas_->zoom());
  refresh_text_color_button();

  auto committed = std::make_shared<bool>(false);
  const auto commit = [this, editor = QPointer<QTextEdit>(editor), editing_layer, committed] {
    if (*committed || editor == nullptr) {
      return;
    }
    *committed = true;
    const QPoint current_document_point(editor->property("patchy.documentTextX").toInt(),
                                        editor->property("patchy.documentTextY").toInt());
    commit_text_editor(editor, current_document_point, editing_layer);
  };
  auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), editor);
  connect(shortcut, &QShortcut::activated, editor, commit);
  auto* cancel_shortcut = new QShortcut(QKeySequence(Qt::Key_Escape), editor);
  cancel_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(cancel_shortcut, &QShortcut::activated, editor,
          [this, editor = QPointer<QTextEdit>(editor), editing_layer, committed] {
            if (*committed || editor == nullptr) {
              return;
            }
            *committed = true;
            cancel_text_editor(editor, editing_layer);
          });
  connect(qApp, &QApplication::focusChanged, editor, [this, editor = QPointer<QTextEdit>(editor), commit](QWidget* old,
                                                                                                          QWidget* now) {
    if (shutting_down_ || editor == nullptr) {
      return;
    }
    const auto left_editor = old == editor || editor->isAncestorOf(old);
    const auto entered_editor = now == editor || editor->isAncestorOf(now);
    const auto entered_canvas = canvas_ != nullptr && (now == canvas_ || canvas_->isAncestorOf(now));
    const auto text_color_dialog_has_focus_change = [this](QWidget* widget) {
      if (color_dialog_ == nullptr) {
        return false;
      }
      const auto target = color_dialog_->property("patchy.colorTarget").toString();
      return (target == QStringLiteral("text") || target == QStringLiteral("foreground")) &&
             (widget == color_dialog_ || color_dialog_->isAncestorOf(widget));
    };
    if (text_color_dialog_has_focus_change(old) || text_color_dialog_has_focus_change(now)) {
      return;
    }
    if (canvas_ != nullptr && now == canvas_ &&
        text_editor_resize_handle_at(canvas_->mapFromGlobal(QCursor::pos())) != nullptr) {
      return;
    }
    if (canvas_ != nullptr && now == canvas_) {
      auto* overlay = transformed_text_edit_overlay_for_canvas(canvas_);
      if (overlay != nullptr && overlay->editor() == editor &&
          overlay->has_resize_handle_at_canvas_point(QPointF(canvas_->mapFromGlobal(QCursor::pos())))) {
        return;
      }
    }
    if (((left_editor && !entered_editor) || entered_canvas) && !entered_editor && !is_text_option_widget(now)) {
      if (entered_canvas && (QApplication::mouseButtons() & Qt::LeftButton) != 0) {
        swallow_next_canvas_left_press_ = true;
      }
      commit();
    }
  });
  refresh_options_bar();  // shows the session apply/cancel buttons
}

void MainWindow::cancel_text_editor(QTextEdit* editor, std::optional<LayerId> layer_id) {
  if (!mark_text_editor_finished(editor)) {
    return;
  }
  const auto restore_existing_visibility =
      editor->property("patchy.editingLayerWasVisible").isValid()
          ? editor->property("patchy.editingLayerWasVisible").toBool()
          : true;
  remove_text_editor_preview(editor);
  remove_text_editor_transform_overlay(editor);
  remove_text_editor_handles(editor);
  editor->hide();
  editor->setParent(nullptr);
  editor->deleteLater();

  // A canceled NEW session removes the layer that appeared at click time, Photoshop-style,
  // leaving history and the modified state exactly as before the click.
  if (const auto removed_provisional = take_provisional_text_layer(editor); removed_provisional.has_value()) {
    canvas_->document_changed_effect_bounds(QRect(QPoint(editor->property("patchy.documentTextX").toInt(),
                                                         editor->property("patchy.documentTextY").toInt()),
                                                  QSize(1, 1)));
    refresh_layer_list();
    refresh_layer_controls();
  }
  if (layer_id.has_value() && canvas_ != nullptr && has_active_document()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_visible(restore_existing_visibility);
      canvas_->document_changed_effect_bounds(to_qrect(layer_render_bounds(*layer)));
      refresh_layer_list();
      refresh_layer_controls();
    }
  }
  refresh_text_color_button();
  refresh_options_bar();  // hides the session apply/cancel buttons
  statusBar()->showMessage(tr("Canceled text edit"));
}

void MainWindow::commit_text_editor(QTextEdit* editor, QPoint document_point, std::optional<LayerId> layer_id) {
  if (shutting_down_) {
    return;
  }
  if (!mark_text_editor_finished(editor)) {
    return;
  }
  if (canvas_ == nullptr || !has_active_document()) {
    // A stray commit with no canvas/document to rasterize into (e.g. a focus
    // change while the owning tab is torn down): drop the edit but still
    // dismantle the editor chrome.  The helpers below tolerate a null
    // canvas_; document() would throw.
    remove_text_editor_preview(editor);
    remove_text_editor_transform_overlay(editor);
    remove_text_editor_handles(editor);
    editor->hide();
    editor->setParent(nullptr);
    editor->deleteLater();
    refresh_options_bar();  // hides the session apply/cancel buttons
    return;
  }
  // The provisional layer inserted at click time comes out before anything else: the undo
  // snapshot below must capture the pre-click document, and every dropped-commit path must
  // leave the document as if the click never happened.
  const auto removed_provisional = take_provisional_text_layer(editor);
  // Untrimmed: the stored text must keep indexing the rich-text/paragraph runs, which are
  // extracted from the full editor document -- old PSDs often start with a blank line, and
  // trimming here shifted every run on the next edit session.  Trim only for the emptiness test.
  const auto text = editor->toPlainText();
  // Applying always keeps what the session showed.  A box/PSD-frame session still displays
  // Photoshop's raster (kTextEditorSourceRasterPreviewProperty), so an unchanged apply keeps that
  // raster; a point-text session shows the live render from entry, so applying commits it even
  // with no text change -- snapping back to the raster after the user saw the live layout was
  // jarring.  Escape is the way to leave the original pixels untouched.
  const auto source_raster_unchanged = editor->property(kTextEditorSourceRasterPreviewProperty).toBool() &&
                                       !editor->property(kTextEditorChangedProperty).toBool();
  const auto restore_existing_visibility =
      editor->property("patchy.editingLayerWasVisible").isValid()
          ? editor->property("patchy.editingLayerWasVisible").toBool()
          : true;
  const auto restore_hidden_text_layer = [this, layer_id, restore_existing_visibility, removed_provisional,
                                          document_point] {
    if (removed_provisional.has_value()) {
      // A dropped commit of a NEW session: the provisional layer is already removed, the panel
      // row just has to vanish (its pixels were a 1x1 transparent placeholder).
      canvas_->document_changed_effect_bounds(QRect(document_point, QSize(1, 1)));
      refresh_layer_list();
      refresh_layer_controls();
    }
    if (!layer_id.has_value()) {
      return;
    }
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_visible(restore_existing_visibility);
      canvas_->document_changed_effect_bounds(to_qrect(layer_render_bounds(*layer)));
      refresh_layer_list();
      refresh_layer_controls();
    }
  };
  remove_text_editor_preview(editor);
  remove_text_editor_transform_overlay(editor);
  remove_text_editor_handles(editor);
  editor->hide();
  editor->setParent(nullptr);
  editor->deleteLater();
  // The editor is detached, so this hides the session apply/cancel buttons on
  // every path below (early returns included).
  refresh_options_bar();
  if (source_raster_unchanged) {
    restore_hidden_text_layer();
    refresh_text_color_button();
    return;
  }
  if (text.trimmed().isEmpty()) {
    restore_hidden_text_layer();
    return;
  }

  const auto text_size = std::max(1, editor->property("patchy.documentTextSize").toInt());
  const auto text_width =
      std::max(kMinimumTextBoxDocumentSize, editor->property("patchy.documentTextWidth").toInt());
  const auto text_height =
      std::max(kMinimumTextBoxDocumentSize, editor->property("patchy.documentTextHeight").toInt());
  const auto boxed_text =
      text_flow_is_box(editor->property("patchy.documentTextFlow").toString());
  const auto text_color = editor->property("patchy.documentTextColor").value<QColor>().isValid()
                              ? editor->property("patchy.documentTextColor").value<QColor>()
                              : canvas_->primary_color();
  const auto text_anti_alias = std::clamp(editor->property("patchy.documentTextAntiAlias").toInt(), 0, 16);
  auto text_family = editor->property("patchy.documentTextFamily").toString();
  if (text_family.trimmed().isEmpty()) {
    text_family = text_display_family_from_format(text_editor_reference_format(*editor),
                                                  display_text_family_from_font(editor->font()));
  }
  auto document_text = document_from_editor_in_document_units(*editor, canvas_->zoom());
  TextToolSettings settings{text,
                            QString(),
                            text_family,
                            text_size,
                            editor->font().bold(),
                            editor->font().italic(),
                            text_anti_alias,
                            boxed_text,
                            text_width,
                            text_height};
  settings.photoshop_layout = text_editor_uses_photoshop_layout(*editor);
  const auto rich_text_runs = rich_text_runs_from_document(*document_text, settings, text_color);
  const auto paragraph_runs = paragraph_runs_from_document(*document_text);
  settings.html = document_html_from_text_runs(document_text->toPlainText(), rich_text_runs, settings, text_color);
  // A PSD-frame box session works in a document-space frame while its runs stay in raw engine
  // units; fold the transform's vertical scale into the glyph sizes for the render.
  const auto frame_layout_scale = settings.photoshop_layout && editor->property("patchy.usesPsdTextFrame").toBool()
                                      ? text_editor_size_display_scale(*editor)
                                      : 1.0;
  auto rendered = render_text_pixels_with_local_rect(settings, text_color, text_width, paragraph_runs,
                                                     rich_text_runs, text_editor_render_local_rect(*editor),
                                                     text_editor_metric_scale(*editor), QTransform(),
                                                     frame_layout_scale);
  if (rendered.pixels.empty()) {
    restore_hidden_text_layer();
    return;
  }
  if (!boxed_text && !editor->property("patchy.usesPsdTextFrame").toBool()) {
    bool updated_transform = false;
    if (layer_id.has_value()) {
      if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
        updated_transform = update_text_editor_transform_from_psd_local_bounds(*editor, *layer, rendered.pixels,
                                                                              boxed_text);
      }
    }
    if (!updated_transform) {
      update_text_editor_transform_from_source_anchor(*editor, rendered.pixels);
    }
    document_point = QPoint(editor->property("patchy.documentTextX").toInt(),
                            editor->property("patchy.documentTextY").toInt());
  }

  auto committed_bounds = rendered_text_bounds_for_editor(*editor, document_point, rendered);
  auto pixels = std::move(rendered.pixels);
  const auto local_text_height = pixels.height();
  std::optional<QTransform> text_transform;
  std::optional<LayerAffineTransform> text_affine_transform;
  bool psd_anchored_text = false;
  if (layer_id.has_value()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      psd_anchored_text = layer->metadata().contains(kLayerMetadataPsdTextTransform);
      if (const auto override_transform = text_editor_transform_override(*editor); override_transform.has_value()) {
        text_affine_transform = *override_transform;
        text_transform = qtransform_from_affine(*override_transform);
      } else {
        text_transform = patchy_text_transform_for_layer(*layer);
        text_affine_transform = canonical_text_affine_transform_for_layer(*layer);
      }
    }
  }
  // Warp Text: a warped layer re-renders through the warp surface instead of the
  // affine paths below. The warp box is re-derived from the fresh layout so the
  // bend follows the edited text (Photoshop recomputes its box on every change).
  std::optional<TextWarp> committed_warp_used;
  QTransform committed_warp_transform;
  if (layer_id.has_value()) {
    if (auto* warp_layer = document().find_layer(*layer_id); warp_layer != nullptr) {
      if (auto warp = text_warp_from_layer(*warp_layer); warp.has_value() && !text_warp_is_identity(*warp)) {
        LayerTextRenderInputs warp_inputs{settings, text_color, text_width, paragraph_runs, rich_text_runs};
        TextWarp refreshed = *warp;
        refreshed.bounds_left = 0.0;
        refreshed.bounds_top = 0.0;
        refreshed.bounds_right = 0.0;
        refreshed.bounds_bottom = 0.0;
        if (text_transform.has_value() && qtransform_has_non_translation_linear_part(*text_transform)) {
          committed_warp_transform = *text_transform;
        } else {
          committed_warp_transform =
              QTransform::fromTranslate(static_cast<qreal>(committed_bounds.x) - rendered.local_rect.left(),
                                        static_cast<qreal>(committed_bounds.y) - rendered.local_rect.top());
        }
        if (auto warped = render_warped_text_pixels_for_layer(warp_inputs, refreshed,
                                                              committed_warp_transform, &refreshed);
            warped.has_value()) {
          pixels = std::move(warped->pixels);
          committed_bounds = warped->bounds;
          committed_warp_used = refreshed;
        }
      }
    }
  }
  const auto anchor_places_rendered_pixels =
      text_transform.has_value() && text_editor_render_local_rect(*editor).has_value() &&
      text_editor_source_visible_anchor(*editor).has_value() &&
      !qtransform_has_non_translation_linear_part(*text_transform);
  if (!committed_warp_used.has_value() && text_transform.has_value() &&
      !editor->property("patchy.usesPsdTextFrame").toBool() && !anchor_places_rendered_pixels) {
    auto transform_for_pixels = *text_transform;
    const bool has_local_offset =
        text_editor_render_local_rect(*editor).has_value() &&
        (std::abs(rendered.local_rect.left()) > 0.0001 || std::abs(rendered.local_rect.top()) > 0.0001);
    if (has_local_offset) {
      transform_for_pixels = qtransform_from_affine(
          affine_with_local_translation(affine_from_qtransform(*text_transform), rendered.local_rect.topLeft()));
    }
    // For a scaled/rotated point-text layer, re-rasterize the glyphs *through* the transform so the
    // committed result stays crisp instead of resampling the base-size bitmap (which is what made an
    // edit of a scaled PSD layer go blocky).  Eligibility rules live on the shared helper.
    bool used_crisp = false;
    if (auto crisp = render_crisp_transformed_text_for_editor(*editor, psd_anchored_text, boxed_text,
                                                              has_local_offset, settings, text_color, text_width,
                                                              paragraph_runs, rich_text_runs, *text_transform,
                                                              transform_for_pixels);
        crisp.has_value()) {
      committed_bounds = crisp->bounds;
      pixels = std::move(crisp->pixels);
      used_crisp = true;
    }
    if (!used_crisp) {
      auto transformed = apply_text_transform_to_pixels(pixels, transform_for_pixels);
      pixels = std::move(transformed.pixels);
      committed_bounds = transformed.bounds;
    }
  }

  if (layer_id.has_value() && layer_id_locks_image_pixels(*layer_id)) {
    restore_hidden_text_layer();
    refresh_text_color_button();
    show_status_error(tr("Layer pixels are locked."));
    return;
  }

  if (layer_id.has_value()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      layer->set_visible(restore_existing_visibility);
    }
  }
  push_undo_snapshot(tr("Type"));
  const auto name = text_layer_auto_name(settings.text);
  // A PSD-frame session rendered a document-space frame around raw-unit runs; persist the box
  // dims back in the runs' raw engine space so the stored runs + box + transform stay one
  // consistent coordinate system for re-edits and metadata re-renders.
  auto stored_box_width = text_width;
  auto stored_box_height = boxed_text ? text_height : local_text_height;
  if (std::abs(frame_layout_scale - 1.0) > 0.0001) {
    stored_box_width = std::max(1, static_cast<int>(std::lround(text_width / frame_layout_scale)));
    if (boxed_text) {
      stored_box_height = std::max(1, static_cast<int>(std::lround(text_height / frame_layout_scale)));
    }
  }
  // Transformed point text: the session width arrived in document pixels (the imported layer's
  // raster width) while the stored height is text-local -- persisting that mix made the next
  // session map the document width through the transform AGAIN (a ~4x-scaled menu layer grew a
  // dashed edit rect several canvases wide). Store the base render's local width instead so
  // width and height live in the same text-local space as the runs.
  if (!boxed_text && settings.photoshop_layout && text_transform.has_value() &&
      qtransform_has_non_translation_linear_part(*text_transform)) {
    stored_box_width = std::max(1, static_cast<int>(std::ceil(rendered.local_rect.width())));
  }
  if (layer_id.has_value()) {
    if (auto* layer = document().find_layer(*layer_id); layer != nullptr) {
      // Follow the text content only for auto-derived names; a name the user (or the source PSD)
      // chose stays put.  No "Text: " prefix -- the layer list already shows a type badge.
      if (text_layer_name_is_auto(*layer)) {
        layer->set_name(name.toStdString());
      }
      layer->set_pixels(std::move(pixels));
      layer->set_bounds(committed_bounds);
      layer->set_visible(restore_existing_visibility);
      store_patchy_text_metadata(*layer, settings, text_color, rich_text_runs, paragraph_runs, stored_box_width,
                                 boxed_text ? stored_box_height : local_text_height);
      if (settings.photoshop_layout) {
        // Imported layers keep their marker through store (it never clears the key); a native
        // layer that opted in via the Character panel's leading controls persists it here.
        layer->metadata()[kLayerMetadataTextLayoutMode] = kTextLayoutModePhotoshop;
      }
      if (editor->property(kTextEditorLineAwareBoxPreviewProperty).toBool()) {
        layer->metadata()[kLayerMetadataTextLineAwareBoxPreview] = "true";
      } else {
        layer->metadata().erase(kLayerMetadataTextLineAwareBoxPreview);
      }
      if (boxed_text) {
        layer->metadata()[kLayerMetadataTextTransform] =
            serialize_layer_affine_transform(committed_text_transform(document_point, text_affine_transform));
      } else if (text_affine_transform.has_value()) {
        layer->metadata()[kLayerMetadataTextTransform] = serialize_layer_affine_transform(*text_affine_transform);
      }
      if (committed_warp_used.has_value()) {
        layer->metadata()[kLayerMetadataTextWarp] = serialize_text_warp(*committed_warp_used);
        if (!layer->metadata().contains(kLayerMetadataTextTransform)) {
          // The warp render mapped text-local space through this transform; keep the
          // layer self-describing so later re-renders and the PSD writer agree.
          layer->metadata()[kLayerMetadataTextTransform] = serialize_layer_affine_transform(
              LayerAffineTransform{committed_warp_transform.m11(), committed_warp_transform.m12(),
                                   committed_warp_transform.m21(), committed_warp_transform.m22(),
                                   committed_warp_transform.dx(), committed_warp_transform.dy()});
        }
      }
    }
  } else {
    // Reuse the provisional's id when one was just removed: the row the user has watched since
    // the click keeps identifying the same layer across the commit.
    const auto committed_layer_id =
        removed_provisional.has_value() ? *removed_provisional : document().allocate_layer_id();
    Layer text_layer(committed_layer_id, name.toStdString(), std::move(pixels));
    text_layer.set_bounds(
        Rect{document_point.x(), document_point.y(), text_layer.pixels().width(), text_layer.pixels().height()});
    store_patchy_text_metadata(text_layer, settings, text_color, rich_text_runs, paragraph_runs, text_width,
                               boxed_text ? text_height : text_layer.pixels().height());
    if (settings.photoshop_layout) {
      text_layer.metadata()[kLayerMetadataTextLayoutMode] = kTextLayoutModePhotoshop;
    }
    if (editor->property(kTextEditorLineAwareBoxPreviewProperty).toBool()) {
      text_layer.metadata()[kLayerMetadataTextLineAwareBoxPreview] = "true";
    }
    if (boxed_text) {
      text_layer.metadata()[kLayerMetadataTextTransform] =
          serialize_layer_affine_transform(committed_text_transform(document_point, std::nullopt));
    }
    document().add_layer(std::move(text_layer));
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed();
  refresh_text_color_button();
  statusBar()->showMessage(tr("Created text layer"));
}

bool MainWindow::commit_active_text_editor() {
  if (canvas_ == nullptr) {
    return false;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return false;
  }
  const QPoint document_point(editor->property("patchy.documentTextX").toInt(),
                              editor->property("patchy.documentTextY").toInt());
  std::optional<LayerId> layer_id;
  if (editor->property("patchy.editingLayerId").isValid()) {
    layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
  }
  commit_text_editor(editor, document_point, layer_id);
  return true;
}

bool MainWindow::cancel_active_text_editor() {
  if (canvas_ == nullptr) {
    return false;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return false;
  }
  std::optional<LayerId> layer_id;
  if (editor->property("patchy.editingLayerId").isValid()) {
    layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
  }
  cancel_text_editor(editor, layer_id);
  return true;
}

void MainWindow::finish_active_text_editor() {
  if (!commit_active_text_editor()) {
    return;
  }
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

int MainWindow::cli_append_text_to_text_layers(const QString& suffix) {
  if (canvas_ == nullptr || !has_active_document() || suffix.isEmpty()) {
    return 0;
  }
  // Collect ids up front: committing a session rewrites rasters/bounds but never ids.
  std::vector<LayerId> text_layer_ids;
  std::function<void(const std::vector<Layer>&)> collect = [&](const std::vector<Layer>& layers) {
    for (const auto& layer : layers) {
      if (layer_is_text(layer)) {
        text_layer_ids.push_back(layer.id());
      }
      collect(layer.children());
    }
  };
  collect(std::as_const(document()).layers());

  int mutated = 0;
  for (const auto id : text_layer_ids) {
    const auto* layer = std::as_const(document()).find_layer(id);
    if (layer == nullptr || layer_id_locks_image_pixels(id)) {
      continue;
    }
    // add_text_at targets the ACTIVE layer when the point is inside its bounds, so no
    // hit-testing/occlusion concerns: activate, aim at the bounds center, open the session.
    const auto bounds = layer->bounds();
    const QPoint anchor(bounds.x + std::max(1, bounds.width) / 2, bounds.y + std::max(1, bounds.height) / 2);
    document().set_active_layer(id);
    add_text_at(anchor);
    QTextEdit* editor = nullptr;
    QElapsedTimer wait;
    wait.start();
    while (wait.elapsed() < 5000) {
      editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
      if (editor != nullptr) {
        break;
      }
      QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
    }
    if (editor == nullptr) {
      continue;
    }
    if (editor->property("patchy.editingLayerId").toULongLong() != static_cast<qulonglong>(id)) {
      // Defensive: the session latched onto some other layer; leave it untouched.
      cancel_active_text_editor();
      continue;
    }
    auto cursor = editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    editor->setTextCursor(cursor);
    editor->insertPlainText(suffix);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    finish_active_text_editor();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    ++mutated;
  }
  return mutated;
}

bool MainWindow::apply_text_warp_to_layer(Layer& layer, const patchy::TextWarp& warp) {
  const auto inputs = text_render_inputs_from_layer(layer);
  if (!inputs.has_value()) {
    return false;
  }
  const auto metadata_value = [&layer](const char* key) -> QString {
    const auto found = layer.metadata().find(key);
    return found == layer.metadata().end() ? QString() : QString::fromStdString(found->second);
  };
  // Resolve the text-local -> document transform in the space the Qt render uses.
  // Imported Photoshop previews anchor their transform at the typographic baseline
  // while the Qt raster is top-left-origin, so point text adopts the glyph-aligned
  // transform (the layer becomes Patchy-rendered from here on).
  QTransform transform;
  bool have_transform = false;
  bool adopt_transform = false;
  std::optional<QRectF> imported_box;
  const bool imported_preview =
      metadata_value(kLayerMetadataTextRasterStatus) != QStringLiteral("patchy_raster") &&
      layer.metadata().contains(kLayerMetadataPsdTextTransform);
  if (imported_preview && inputs->settings.boxed) {
    // Box text: the frame origin is the transform origin in both engines, so no ink
    // alignment is needed; adopting Photoshop's box keeps its exact warp geometry
    // (the 'bounds' top hangs the first line's ascent-to-cap gap above the frame).
    if (const auto psd_bounds = psd_text_metadata_local_rect(layer, kLayerMetadataPsdTextBounds);
        psd_bounds.has_value()) {
      imported_box = *psd_bounds;
    }
  }
  if (imported_preview && !inputs->settings.boxed) {
    const auto base = render_text_pixels_with_local_rect(inputs->settings, inputs->color, inputs->max_width,
                                                         inputs->paragraph_runs, inputs->rich_text_runs);
    if (!base.pixels.empty()) {
      if (const auto aligned = psd_point_text_local_bounds_transform_for_pixels(
              layer, base.pixels, false, layer_anchor_alignment_factor(layer));
          aligned.has_value()) {
        transform = qtransform_from_affine(*aligned);
        have_transform = true;
        adopt_transform = true;
      } else if (const auto canonical = canonical_text_affine_transform_for_layer(layer);
                 canonical.has_value()) {
        // Pure-translation import (the alignment helper above only handles
        // scaled/rotated layers): pin the re-rendered ink to the imported
        // boundingBox in text-local space, honoring the justification anchor.
        const auto psd_local_rect = psd_point_text_local_visual_rect(layer);
        const auto visible = visible_alpha_local_bounds(base.pixels);
        if (psd_local_rect.has_value() && visible.has_value()) {
          const QRectF visible_rect(visible->x, visible->y, visible->width, visible->height);
          QPointF delta = psd_local_rect->topLeft() - visible_rect.topLeft();
          const auto factor = layer_anchor_alignment_factor(layer);
          if (factor > 0.0) {
            delta.setX((psd_local_rect->left() + factor * psd_local_rect->width()) -
                       (visible_rect.left() + factor * visible_rect.width()));
          }
          if (std::isfinite(delta.x()) && std::isfinite(delta.y())) {
            transform = qtransform_from_affine(affine_with_local_translation(*canonical, delta));
            have_transform = true;
            adopt_transform = true;
            // Photoshop's own warp box (the imported 'bounds'), expressed in the
            // raster's local space, keeps the re-render's warp geometry exact.
            if (const auto psd_bounds =
                    psd_text_metadata_local_rect(layer, kLayerMetadataPsdTextBounds);
                psd_bounds.has_value()) {
              imported_box = psd_bounds->translated(-delta);
            }
          }
        }
      }
    }
  }
  if (!have_transform) {
    if (const auto canonical = canonical_text_affine_transform_for_layer(layer); canonical.has_value()) {
      transform = qtransform_from_affine(*canonical);
      have_transform = true;
    }
  }
  if (!have_transform) {
    transform = QTransform::fromTranslate(layer.bounds().x, layer.bounds().y);
    adopt_transform = true;
  }

  TextWarp requested = warp;
  // The reference box is re-derived from the live layout (Photoshop recomputes its
  // warp box on every text change), except for aligned imports where Photoshop's
  // own box is available; a stored box never goes stale here.
  if (imported_box.has_value()) {
    requested.bounds_left = imported_box->left();
    requested.bounds_top = imported_box->top();
    requested.bounds_right = imported_box->right();
    requested.bounds_bottom = imported_box->bottom();
  } else {
    requested.bounds_left = 0.0;
    requested.bounds_top = 0.0;
    requested.bounds_right = 0.0;
    requested.bounds_bottom = 0.0;
  }
  if (!text_warp_is_identity(requested)) {
    TextWarp effective = requested;
    auto rendered = render_warped_text_pixels_for_layer(*inputs, requested, transform, &effective);
    if (!rendered.has_value()) {
      return false;
    }
    layer.set_pixels(std::move(rendered->pixels));
    layer.set_bounds(rendered->bounds);
    layer.metadata()[kLayerMetadataTextWarp] = serialize_text_warp(effective);
  } else {
    // Style None: back to the plain affine text render.
    std::optional<TransformedTextPixels> rendered;
    if (!inputs->settings.boxed) {
      rendered = render_text_layer_pixels_through_transform(layer, transform);
    }
    if (!rendered.has_value()) {
      const auto base = render_text_pixels_with_local_rect(inputs->settings, inputs->color, inputs->max_width,
                                                           inputs->paragraph_runs, inputs->rich_text_runs);
      if (base.pixels.empty()) {
        return false;
      }
      if (qtransform_has_non_translation_linear_part(transform)) {
        auto transformed = apply_text_transform_to_pixels(
            base.pixels, qtransform_from_affine(affine_with_local_translation(
                             affine_from_qtransform(transform), base.local_rect.topLeft())));
        rendered = TransformedTextPixels{std::move(transformed.pixels), transformed.bounds};
      } else {
        const auto mapped = transform.map(base.local_rect.topLeft());
        rendered = TransformedTextPixels{base.pixels,
                                         Rect{static_cast<std::int32_t>(std::floor(mapped.x())),
                                              static_cast<std::int32_t>(std::floor(mapped.y())),
                                              base.pixels.width(), base.pixels.height()}};
      }
    }
    layer.set_pixels(std::move(rendered->pixels));
    layer.set_bounds(rendered->bounds);
    layer.metadata().erase(kLayerMetadataTextWarp);
  }
  if (adopt_transform || !layer.metadata().contains(kLayerMetadataTextTransform)) {
    layer.metadata()[kLayerMetadataTextTransform] = serialize_layer_affine_transform(
        LayerAffineTransform{transform.m11(), transform.m12(), transform.m21(), transform.m22(),
                             transform.dx(), transform.dy()});
  }
  layer.metadata()[kLayerMetadataTextRasterStatus] = "patchy_raster";
  return true;
}

namespace {

// Character-panel edits depend on each fragment's own exact size (mixed-size selections), so
// they mutate per-fragment formats instead of merging one uniform format. Applies to the
// selection, or the whole document when nothing is selected, plus the typing format so newly
// typed characters inherit the change.
void mutate_text_editor_character_formats(QTextEdit& editor,
                                          const std::function<void(QTextCharFormat&)>& mutate) {
  auto* document = editor.document();
  if (document == nullptr) {
    return;
  }
  const auto selection = editor.textCursor();
  const int document_end = std::max(0, static_cast<int>(document->characterCount()) - 1);
  const int begin = selection.hasSelection() ? selection.selectionStart() : 0;
  const int end = selection.hasSelection() ? selection.selectionEnd() : document_end;
  struct FormatRange {
    int position{0};
    int length{0};
    QTextCharFormat format;
  };
  std::vector<FormatRange> ranges;
  for (auto block = document->begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      const int from = std::max(begin, fragment.position());
      const int to = std::min(end, fragment.position() + fragment.length());
      if (from >= to) {
        continue;
      }
      auto format = fragment.charFormat();
      mutate(format);
      ranges.push_back(FormatRange{from, to - from, std::move(format)});
    }
  }
  for (const auto& range : ranges) {
    QTextCursor cursor(document);
    cursor.setPosition(range.position);
    cursor.setPosition(range.position + range.length, QTextCursor::KeepAnchor);
    cursor.setCharFormat(range.format);
  }
  auto typing = editor.currentCharFormat();
  mutate(typing);
  editor.setCurrentCharFormat(typing);
}

// The vertical-scale-free FontSize basis of a format, tolerating formats whose pixel size
// already folds an older vertical scale in (photoshop_char_exact_size would double-fold it).
double character_format_size_basis(const QTextCharFormat& format) {
  if (format.hasProperty(kTextExactSizeFormatProperty)) {
    const auto exact = format.property(kTextExactSizeFormatProperty).toDouble();
    if (std::isfinite(exact) && exact > 0.0) {
      return exact;
    }
  }
  double old_vertical = 1.0;
  if (format.hasProperty(kTextVerticalScaleFormatProperty)) {
    const auto value = format.property(kTextVerticalScaleFormatProperty).toDouble();
    if (std::isfinite(value) && value > 0.01 && value < 100.0) {
      old_vertical = value;
    }
  }
  const auto font = format.font();
  if (font.pixelSize() > 0) {
    return static_cast<double>(font.pixelSize()) / old_vertical;
  }
  return font.pointSizeF() > 0.0 ? font.pointSizeF() : 12.0;
}

double character_format_scale_property(const QTextCharFormat& format, int property_id) {
  if (format.hasProperty(property_id)) {
    const auto value = format.property(property_id).toDouble();
    if (std::isfinite(value) && value > 0.01 && value < 100.0) {
      return value;
    }
  }
  return 1.0;
}

}  // namespace

void MainWindow::open_text_character_dialog() {
  if (text_character_dialog_ != nullptr) {
    text_character_dialog_->show();
    text_character_dialog_->raise();
    text_character_dialog_->activateWindow();
    sync_text_character_dialog_from_editor();
    return;
  }
  auto* dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setObjectName(QStringLiteral("textCharacterDialog"));
  dialog->setWindowTitle(tr("Character"));
  text_character_dialog_ = dialog;
  auto* layout = new QFormLayout(dialog);

  text_character_hint_label_ = new QLabel(tr("Click in text with the Type tool to edit these settings."), dialog);
  text_character_hint_label_->setObjectName(QStringLiteral("textCharacterHint"));
  text_character_hint_label_->setWordWrap(true);
  text_character_hint_label_->setStyleSheet(QStringLiteral("color: #999999;"));
  layout->addRow(text_character_hint_label_);

  text_character_auto_leading_ = new QCheckBox(tr("Auto leading"), dialog);
  text_character_auto_leading_->setObjectName(QStringLiteral("textCharacterAutoLeading"));
  layout->addRow(QString(), text_character_auto_leading_);

  text_character_leading_spin_ = new QDoubleSpinBox(dialog);
  text_character_leading_spin_->setObjectName(QStringLiteral("textCharacterLeadingSpin"));
  text_character_leading_spin_->setDecimals(2);
  text_character_leading_spin_->setRange(0.01, 10000.0);
  text_character_leading_spin_->setSingleStep(0.5);
  text_character_leading_spin_->setSuffix(tr(" pt"));
  configure_dialog_spinbox(text_character_leading_spin_);
  layout->addRow(tr("Leading:"), text_character_leading_spin_);

  text_character_tracking_spin_ = new QSpinBox(dialog);
  text_character_tracking_spin_->setObjectName(QStringLiteral("textCharacterTrackingSpin"));
  text_character_tracking_spin_->setRange(-1000, 1000);
  text_character_tracking_spin_->setSingleStep(10);
  text_character_tracking_spin_->setToolTip(tr("Space between characters, in 1/1000 em (Photoshop tracking)"));
  configure_dialog_spinbox(text_character_tracking_spin_);
  layout->addRow(tr("Tracking:"), text_character_tracking_spin_);

  text_character_h_scale_spin_ = new QSpinBox(dialog);
  text_character_h_scale_spin_->setObjectName(QStringLiteral("textCharacterHScaleSpin"));
  text_character_h_scale_spin_->setRange(1, 1000);
  text_character_h_scale_spin_->setSuffix(tr(" %"));
  configure_dialog_spinbox(text_character_h_scale_spin_);
  layout->addRow(tr("Horizontal scale:"), text_character_h_scale_spin_);

  text_character_v_scale_spin_ = new QSpinBox(dialog);
  text_character_v_scale_spin_->setObjectName(QStringLiteral("textCharacterVScaleSpin"));
  text_character_v_scale_spin_->setRange(1, 1000);
  text_character_v_scale_spin_->setSuffix(tr(" %"));
  configure_dialog_spinbox(text_character_v_scale_spin_);
  layout->addRow(tr("Vertical scale:"), text_character_v_scale_spin_);

  connect(text_character_auto_leading_, &QCheckBox::toggled, this,
          [this](bool) { apply_text_character_leading_to_active_editor(); });
  connect(text_character_leading_spin_, &QDoubleSpinBox::valueChanged, this,
          [this](double) { apply_text_character_leading_to_active_editor(); });
  connect(text_character_tracking_spin_, &QSpinBox::valueChanged, this,
          [this](int) { apply_text_character_tracking_to_active_editor(); });
  connect(text_character_h_scale_spin_, &QSpinBox::valueChanged, this,
          [this](int) { apply_text_character_glyph_scales_to_active_editor(); });
  connect(text_character_v_scale_spin_, &QSpinBox::valueChanged, this,
          [this](int) { apply_text_character_glyph_scales_to_active_editor(); });

  // Sub-control gotcha: the spin-button style must land AFTER all children exist.
  dialog->setStyleSheet(dialog_spinbox_button_style());
  sync_text_character_dialog_from_editor();
  run_non_modal_dialog(*dialog);
  // WA_DeleteOnClose destroyed the dialog when the nested loop unwound.
  text_character_hint_label_ = nullptr;
  text_character_auto_leading_ = nullptr;
  text_character_leading_spin_ = nullptr;
  text_character_tracking_spin_ = nullptr;
  text_character_h_scale_spin_ = nullptr;
  text_character_v_scale_spin_ = nullptr;
}

void MainWindow::sync_text_character_dialog_from_editor() {
  if (text_character_dialog_ == nullptr || text_character_auto_leading_ == nullptr ||
      text_character_leading_spin_ == nullptr || text_character_tracking_spin_ == nullptr ||
      text_character_h_scale_spin_ == nullptr || text_character_v_scale_spin_ == nullptr ||
      text_character_hint_label_ == nullptr) {
    return;
  }
  auto* editor =
      canvas_ != nullptr ? canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) : nullptr;
  const bool session_open = editor != nullptr && !editor->property(kTextEditorFinishedProperty).toBool();
  text_character_hint_label_->setVisible(!session_open);
  text_character_auto_leading_->setEnabled(session_open);
  text_character_tracking_spin_->setEnabled(session_open);
  text_character_h_scale_spin_->setEnabled(session_open);
  text_character_v_scale_spin_->setEnabled(session_open);
  if (!session_open) {
    text_character_leading_spin_->setEnabled(false);
    return;
  }
  const auto format = text_editor_reference_format(*editor);
  const auto zoom = std::max(0.01, canvas_->zoom());
  const auto display_scale = text_editor_size_display_scale(*editor);
  const auto to_display_pt = [this, zoom, display_scale](double editor_px) {
    return editor_px / zoom * display_scale * 72.0 / text_size_ppi(document());
  };
  const auto fixed_leading = format.hasProperty(kTextLeadingFormatProperty)
                                 ? format.property(kTextLeadingFormatProperty).toDouble()
                                 : 0.0;
  const bool has_fixed = std::isfinite(fixed_leading) && fixed_leading > 0.0;
  const bool auto_leading = format.hasProperty(kTextAutoLeadingFormatProperty)
                                ? format.property(kTextAutoLeadingFormatProperty).toBool()
                                : !has_fixed;
  QSignalBlocker block_auto(text_character_auto_leading_);
  QSignalBlocker block_leading(text_character_leading_spin_);
  QSignalBlocker block_tracking(text_character_tracking_spin_);
  QSignalBlocker block_h(text_character_h_scale_spin_);
  QSignalBlocker block_v(text_character_v_scale_spin_);
  text_character_auto_leading_->setChecked(auto_leading);
  text_character_leading_spin_->setEnabled(!auto_leading);
  const auto leading_pt = auto_leading || !has_fixed
                              ? to_display_pt(1.2 * character_format_size_basis(format))
                              : to_display_pt(fixed_leading);
  text_character_leading_spin_->setValue(std::clamp(leading_pt, text_character_leading_spin_->minimum(),
                                                    text_character_leading_spin_->maximum()));
  const auto tracking = format.hasProperty(kTextTrackingFormatProperty)
                            ? format.property(kTextTrackingFormatProperty).toDouble()
                            : 0.0;
  text_character_tracking_spin_->setValue(
      std::clamp(static_cast<int>(std::lround(std::isfinite(tracking) ? tracking : 0.0)), -1000, 1000));
  text_character_h_scale_spin_->setValue(std::clamp(
      static_cast<int>(std::lround(character_format_scale_property(format, kTextHorizontalScaleFormatProperty) * 100.0)),
      1, 1000));
  text_character_v_scale_spin_->setValue(std::clamp(
      static_cast<int>(std::lround(character_format_scale_property(format, kTextVerticalScaleFormatProperty) * 100.0)),
      1, 1000));
}

void MainWindow::apply_text_character_leading_to_active_editor() {
  if (canvas_ == nullptr || text_character_auto_leading_ == nullptr || text_character_leading_spin_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }
  const bool auto_leading = text_character_auto_leading_->isChecked();
  text_character_leading_spin_->setEnabled(!auto_leading);
  const auto zoom = std::max(0.01, canvas_->zoom());
  const auto display_scale = text_editor_size_display_scale(*editor);
  const auto leading_editor_px =
      auto_leading ? 0.0
                   : std::max(0.0, text_character_leading_spin_->value() * text_size_ppi(document()) / 72.0 /
                                       display_scale * zoom);
  mutate_text_editor_character_formats(*editor, [auto_leading, leading_editor_px](QTextCharFormat& format) {
    format.setProperty(kTextAutoLeadingFormatProperty, auto_leading);
    format.setProperty(kTextLeadingFormatProperty, leading_editor_px);
  });
  // Explicit leading only renders under the Photoshop layout model; a native layer edited
  // through the panel opts in for this session, and the commit persists the marker.
  editor->setProperty("patchy.textLayoutMode", QString::fromLatin1(kTextLayoutModePhotoshop));
  mark_text_editor_changed(editor);
  schedule_text_editor_preview(editor);
}

void MainWindow::apply_text_character_tracking_to_active_editor() {
  if (canvas_ == nullptr || text_character_tracking_spin_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }
  const auto tracking = static_cast<double>(text_character_tracking_spin_->value());
  mutate_text_editor_character_formats(*editor, [tracking](QTextCharFormat& format) {
    format.setProperty(kTextTrackingFormatProperty, tracking);
    // Photoshop tracking: 1/1000 em of the FontSize basis, scaled by the horizontal glyph
    // scale (the whole advance scales with H; V never affects it).
    const auto size_basis = character_format_size_basis(format);
    const auto horizontal = character_format_scale_property(format, kTextHorizontalScaleFormatProperty);
    auto font = format.font();
    font.setLetterSpacing(QFont::AbsoluteSpacing, tracking / 1000.0 * size_basis * horizontal);
    format.setFont(font);
  });
  mark_text_editor_changed(editor);
  schedule_text_editor_preview(editor);
}

void MainWindow::apply_text_character_glyph_scales_to_active_editor() {
  if (canvas_ == nullptr || text_character_h_scale_spin_ == nullptr || text_character_v_scale_spin_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }
  const auto horizontal = std::clamp(text_character_h_scale_spin_->value(), 1, 1000) / 100.0;
  const auto vertical = std::clamp(text_character_v_scale_spin_->value(), 1, 1000) / 100.0;
  mutate_text_editor_character_formats(*editor, [horizontal, vertical](QTextCharFormat& format) {
    const auto exact = character_format_size_basis(format);
    format.setProperty(kTextExactSizeFormatProperty, exact);
    format.setProperty(kTextHorizontalScaleFormatProperty, horizontal);
    format.setProperty(kTextVerticalScaleFormatProperty, vertical);
    auto font = format.font();
    if (font.pixelSize() > 0 || font.pointSizeF() <= 0.0) {
      font.setPixelSize(std::max(1, static_cast<int>(std::lround(exact * vertical))));
    }
    font.setStretch(
        std::clamp(static_cast<int>(std::lround(horizontal / vertical * 100.0)), 1, 400));
    const auto tracking = format.hasProperty(kTextTrackingFormatProperty)
                              ? format.property(kTextTrackingFormatProperty).toDouble()
                              : 0.0;
    if (std::isfinite(tracking) && std::abs(tracking) > 0.0001) {
      font.setLetterSpacing(QFont::AbsoluteSpacing, tracking / 1000.0 * exact * horizontal);
    }
    format.setFont(font);
  });
  mark_text_editor_changed(editor);
  schedule_text_editor_preview(editor);
}

void MainWindow::request_warp_text_dialog() {
  if (canvas_ == nullptr) {
    return;
  }
  // The dialog operates on the committed layer; finish any open inline edit first.
  commit_active_text_editor();
  auto& doc = document();
  const auto active_id = doc.active_layer_id();
  Layer* layer = active_id.has_value() ? doc.find_layer(*active_id) : nullptr;
  if (layer == nullptr || !layer_is_text(*layer)) {
    show_status_error(tr("Select a text layer to warp."));
    return;
  }
  if (layer_id_locks_image_pixels(layer->id())) {
    show_status_error(tr("Layer pixels are locked."));
    return;
  }
  const auto layer_id = layer->id();
  const auto original_pixels = layer->pixels();
  const auto original_bounds = layer->bounds();
  const auto original_metadata = layer->metadata();
  const auto initial = text_warp_from_layer(*layer).value_or(TextWarp{});

  const auto restore_original = [&, layer_id] {
    if (auto* target = document().find_layer(layer_id); target != nullptr) {
      target->set_pixels(PixelBuffer(original_pixels));
      target->set_bounds(original_bounds);
      target->metadata() = original_metadata;
    }
  };
  const auto preview = [&, layer_id](const TextWarp& warp) {
    auto* target = document().find_layer(layer_id);
    if (target == nullptr) {
      return;
    }
    // Reset first so consecutive previews never compound.
    target->set_pixels(PixelBuffer(original_pixels));
    target->set_bounds(original_bounds);
    target->metadata() = original_metadata;
    // A pristine unwarped layer with style None stays untouched (keeps an imported
    // Photoshop raster bit for bit until a real warp is chosen).
    if (!text_warp_is_identity(warp) || text_warp_from_layer(*target).has_value()) {
      apply_text_warp_to_layer(*target, warp);
    }
    canvas_->document_changed();
    refresh_layer_list();
  };
  const auto result = ui::request_text_warp(this, initial, preview);
  restore_original();
  const auto warp_settings_equal = [](const TextWarp& a, const TextWarp& b) {
    if (text_warp_is_identity(a) && text_warp_is_identity(b)) {
      return true;
    }
    return a.style == b.style && a.rotate == b.rotate && a.value == b.value &&
           a.perspective == b.perspective && a.perspective_other == b.perspective_other;
  };
  if (!result.has_value() || warp_settings_equal(*result, initial)) {
    canvas_->document_changed();
    refresh_layer_list();
    return;
  }
  push_undo_snapshot(tr("Warp Text"));
  bool applied = false;
  if (auto* target = document().find_layer(layer_id); target != nullptr) {
    applied = apply_text_warp_to_layer(*target, *result);
  }
  if (!applied) {
    show_status_error(tr("Could not warp the text layer."));
  } else {
    statusBar()->showMessage(text_warp_is_identity(*result) ? tr("Removed text warp")
                                                            : tr("Warped text layer"));
  }
  canvas_->document_changed();
  refresh_layer_list();
  refresh_layer_controls();
}

// SVG post-open pass. The Qt-free SVG reader stores each <text> element's
// content and font as ordinary patchy.text.* metadata plus a pending-render
// marker with the SVG baseline point and text-anchor; this renders the glyphs
// through the internal text pipeline and positions the layer so the first
// baseline lands on that point (start/middle/end anchoring against the
// rendered width). Runs on the main thread before the document becomes a
// session (fonts are not worker-thread material).
void MainWindow::render_pending_svg_text_layers(Document& target) {
  const auto process = [&](auto&& self, std::vector<Layer>& layers) -> void {
    for (auto& layer : layers) {
      if (!layer.children().empty()) {
        self(self, layer.children());
      }
      auto& metadata = layer.metadata();
      if (!metadata.contains(kLayerMetadataSvgPendingText)) {
        continue;
      }
      const auto take_double = [&metadata](const char* key) {
        const auto found = metadata.find(key);
        return found == metadata.end() ? 0.0 : QString::fromStdString(found->second).toDouble();
      };
      const double baseline_x = take_double(kLayerMetadataSvgTextBaselineX);
      const double baseline_y = take_double(kLayerMetadataSvgTextBaselineY);
      const auto anchor_entry = metadata.find(kLayerMetadataSvgTextAnchor);
      const auto anchor = anchor_entry == metadata.end() ? std::string("start") : anchor_entry->second;
      metadata.erase(kLayerMetadataSvgPendingText);
      metadata.erase(kLayerMetadataSvgTextAnchor);
      metadata.erase(kLayerMetadataSvgTextBaselineX);
      metadata.erase(kLayerMetadataSvgTextBaselineY);

      auto pixels = render_text_layer_pixels_from_metadata(layer);
      if (!pixels.has_value() || pixels->empty()) {
        continue;  // stays an empty text layer; the text tool can still edit it
      }
      const auto inputs = text_render_inputs_from_layer(layer);
      double ascent = inputs.has_value() ? static_cast<double>(inputs->settings.size) : 0.0;
      if (inputs.has_value()) {
        QFont font(inputs->settings.family);
        font.setPixelSize(std::max(1, inputs->settings.size));
        font.setBold(inputs->settings.bold);
        font.setItalic(inputs->settings.italic);
        ascent = QFontMetricsF(font).ascent();
      }
      const double shift = anchor == "middle" ? pixels->width() / 2.0 : anchor == "end" ? pixels->width() : 0.0;
      const Rect placed{static_cast<std::int32_t>(std::lround(baseline_x - shift)),
                        static_cast<std::int32_t>(std::lround(baseline_y - ascent)), pixels->width(),
                        pixels->height()};
      // set_pixels resets bounds to the buffer at the origin, so the bounds
      // must follow it (the text-commit ordering convention).
      layer.set_pixels(std::move(*pixels));
      layer.set_bounds(placed);
      metadata[kLayerMetadataTextRasterStatus] = "patchy_raster";
    }
  };
  process(process, target.layers());
}

void MainWindow::render_pending_af_text_layers(Document& target) {
  const auto process = [&](auto&& self, std::vector<Layer>& layers) -> void {
    for (auto& layer : layers) {
      if (!layer.children().empty()) {
        self(self, layer.children());
      }
      auto& metadata = layer.metadata();
      if (!metadata.contains(kLayerMetadataAfPendingText)) {
        continue;
      }
      std::array<double, 4> frame{0.0, 0.0, 0.0, 0.0};
      if (const auto found = metadata.find(kLayerMetadataAfTextFrame); found != metadata.end()) {
        const QStringList parts = QString::fromStdString(found->second).split(' ');
        for (int i = 0; i < 4 && i < parts.size(); ++i) {
          frame[static_cast<std::size_t>(i)] = parts[i].toDouble();
        }
      }
      const double affinity_ascent = [&] {
        const auto found = metadata.find(kLayerMetadataAfTextAscent);
        return found == metadata.end() ? 0.0 : QString::fromStdString(found->second).toDouble();
      }();
      const int align = [&] {
        const auto found = metadata.find(kLayerMetadataAfTextAlign);
        return found == metadata.end() ? 0 : QString::fromStdString(found->second).toInt();
      }();
      metadata.erase(kLayerMetadataAfPendingText);
      metadata.erase(kLayerMetadataAfTextFrame);
      metadata.erase(kLayerMetadataAfTextAscent);
      metadata.erase(kLayerMetadataAfTextAlign);

      auto pixels = render_text_layer_pixels_from_metadata(layer);
      if (!pixels.has_value() || pixels->empty()) {
        continue;  // stays an empty text layer; the text tool can still edit it
      }
      // Artistic text anchors its baseline at frame-top + Affinity's ascent;
      // re-anchor with Patchy's own ascent so the baseline stays put. Frame
      // text puts the first line's CAP at the frame top (pinned against
      // Affinity's render), so back out Patchy's ascent-above-cap leading.
      // Alignment is horizontal within the frame box.
      double top = frame[1];
      const auto inputs = text_render_inputs_from_layer(layer);
      QFontMetricsF metrics{QFont{}};
      if (inputs.has_value()) {
        QFont font(inputs->settings.family);
        font.setPixelSize(std::max(1, inputs->settings.size));
        font.setBold(inputs->settings.bold);
        font.setItalic(inputs->settings.italic);
        metrics = QFontMetricsF(font);
      }
      if (affinity_ascent > 0.0) {
        top = frame[1] + affinity_ascent - metrics.ascent();
      } else {
        top = frame[1] - (metrics.ascent() - metrics.capHeight());
      }
      double left = frame[0];
      const double frame_width = frame[2] - frame[0];
      if (align > 0 && frame_width > 0.0) {
        const double slack = frame_width - pixels->width();
        left = frame[0] + (align == 1 ? slack / 2.0 : slack);
      }
      const Rect placed{static_cast<std::int32_t>(std::lround(left)),
                        static_cast<std::int32_t>(std::lround(top)), pixels->width(),
                        pixels->height()};
      // set_pixels resets bounds to the buffer at the origin, so the bounds
      // must follow it (the text-commit ordering convention).
      layer.set_pixels(std::move(*pixels));
      layer.set_bounds(placed);
      metadata[kLayerMetadataTextRasterStatus] = "patchy_raster";
    }
  };
  process(process, target.layers());
}

void MainWindow::rasterize_active_layers() {
  finish_active_text_editor();
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  struct PendingRasterize {
    LayerId id{};
    RasterizedLayerPixels pixels;
    Rect before;
    bool clear_text{false};
    bool clear_smart_object{false};
    bool clear_vector{false};
  };
  auto& doc = document();
  std::vector<PendingRasterize> pending;
  pending.reserve(ids.size());
  for (const auto id : ids) {
    const auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer_id_locks_image_pixels(id) || !layer_can_rasterize(*layer)) {
      continue;
    }
    auto rasterized = render_rasterized_layer_pixels(doc, *layer, false);
    if (!rasterized.has_value()) {
      continue;
    }
    pending.push_back(PendingRasterize{id, std::move(*rasterized), layer_render_bounds(*layer),
                                       layer->kind() == LayerKind::Text || layer_is_text(*layer),
                                       layer_is_smart_object(*layer), layer_is_vector_shape(*layer)});
  }
  if (pending.empty()) {
    if (std::any_of(ids.begin(), ids.end(), [this](LayerId id) { return layer_id_locks_image_pixels(id); })) {
      show_status_error(tr("Layer pixels are locked."));
      return;
    }
    show_status_error(tr("No rasterizable layers selected"));
    return;
  }

  push_undo_snapshot(pending.size() == 1U ? tr("Rasterize layer") : tr("Rasterize layers"));
  Rect affected;
  for (auto& change : pending) {
    auto* layer = doc.find_layer(change.id);
    if (layer == nullptr) {
      continue;
    }
    affected = unite_rect(affected, change.before);
    layer->set_pixels(std::move(change.pixels.pixels));
    layer->set_bounds(change.pixels.bounds);
    if (change.clear_text) {
      layer->set_name(rasterized_text_layer_name(*layer).toStdString());
      clear_layer_text_metadata(*layer);
    }
    if (change.clear_smart_object) {
      // The pixels already are the preview; dropping the metadata and the preserved
      // placed-layer blocks makes this a plain pixel layer everywhere, resave included.
      strip_layer_smart_object_data(doc, *layer);
    }
    if (change.clear_vector) {
      strip_layer_vector_data(*layer);
    }
    affected = unite_rect(affected, layer_render_bounds(*layer));
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed(affected.empty() ? QRect() : to_qrect(affected));
  statusBar()->showMessage(pending.size() == 1U ? tr("Rasterized layer") : tr("Rasterized layers"));
}

void MainWindow::rasterize_active_layer_styles() {
  finish_active_text_editor();
  const auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    return;
  }

  struct PendingRasterizeStyle {
    LayerId id{};
    RasterizedLayerPixels pixels;
    Rect before;
    bool clear_text{false};
  };
  auto& doc = document();
  std::vector<PendingRasterizeStyle> pending;
  pending.reserve(ids.size());
  for (const auto id : ids) {
    const auto* layer = doc.find_layer(id);
    if (layer == nullptr || layer_id_locks_image_pixels(id) || !layer_can_rasterize_layer_style(*layer)) {
      continue;
    }
    auto rasterized = render_rasterized_layer_pixels(doc, *layer, true);
    if (!rasterized.has_value()) {
      continue;
    }
    pending.push_back(PendingRasterizeStyle{id, std::move(*rasterized), layer_render_bounds(*layer),
                                            layer->kind() == LayerKind::Text || layer_is_text(*layer)});
  }
  if (pending.empty()) {
    if (std::any_of(ids.begin(), ids.end(), [this](LayerId id) { return layer_id_locks_image_pixels(id); })) {
      show_status_error(tr("Layer pixels are locked."));
      return;
    }
    show_status_error(tr("No layer styles to rasterize"));
    return;
  }

  push_undo_snapshot(pending.size() == 1U ? tr("Rasterize layer style") : tr("Rasterize layer styles"));
  Rect affected;
  for (auto& change : pending) {
    auto* layer = doc.find_layer(change.id);
    if (layer == nullptr) {
      continue;
    }
    affected = unite_rect(affected, change.before);
    layer->set_pixels(std::move(change.pixels.pixels));
    layer->set_bounds(change.pixels.bounds);
    clear_layer_psd_style_source(*layer);
    layer->layer_style() = {};
    layer->set_fill_opacity(1.0F);
    if (change.clear_text) {
      layer->set_name(rasterized_text_layer_name(*layer).toStdString());
      clear_layer_text_metadata(*layer);
    }
    // Baking effects into the pixels breaks the preview's tie to the placed source, so
    // a smart object also demotes to a plain pixel layer here - and so does a
    // shape layer (the cache is no longer a pure path render).
    strip_layer_smart_object_data(doc, *layer);
    strip_layer_vector_data(*layer);
    affected = unite_rect(affected, layer_render_bounds(*layer));
  }
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed(affected.empty() ? QRect() : to_qrect(affected));
  statusBar()->showMessage(pending.size() == 1U ? tr("Rasterized layer style") : tr("Rasterized layer styles"));
}

void MainWindow::merge_down() {
  if (canvas_ != nullptr) {
    canvas_->finish_free_transform();
  }
  finish_active_text_editor();

  auto ids = selected_or_active_layer_ids();
  if (ids.empty()) {
    show_status_error(tr("Select a layer to merge down"));
    return;
  }

  auto& doc = document();

  // Normalize the selection: dedupe, drop ids missing from the tree, and drop any id whose ancestor
  // group is also selected (the folder already contains it, so merging both would double-count).
  const std::set<LayerId> selected(ids.begin(), ids.end());
  std::set<LayerId> seen_ids;
  std::vector<LayerId> normalized;
  normalized.reserve(ids.size());
  for (const auto id : ids) {
    if (!seen_ids.insert(id).second) {
      continue;
    }
    if (doc.find_layer(id) == nullptr) {
      continue;
    }
    std::vector<LayerId> ancestors;
    if (collect_layer_ancestor_groups(doc.layers(), id, ancestors) &&
        std::any_of(ancestors.begin(), ancestors.end(),
                    [&selected](LayerId ancestor) { return selected.count(ancestor) != 0; })) {
      continue;
    }
    normalized.push_back(id);
  }

  if (normalized.empty()) {
    show_status_error(tr("Select a layer to merge down"));
    return;
  }

  // Order the selection bottom-to-top by global compositor paint order so blend modes composite
  // correctly and the bottom-most item becomes the merge target.
  std::vector<LayerId> render_order;
  collect_layer_render_order(doc.layers(), render_order);
  const auto stack_position = [&render_order](LayerId id) {
    return static_cast<std::size_t>(
        std::distance(render_order.begin(), std::find(render_order.begin(), render_order.end(), id)));
  };
  std::sort(normalized.begin(), normalized.end(),
            [&stack_position](LayerId lhs, LayerId rhs) { return stack_position(lhs) < stack_position(rhs); });

  // Build the bottom-to-top merge list. A single folder flattens in place (Photoshop "Merge Group");
  // a single non-folder merges with the layer directly below it ("Merge Down"); several selected
  // items merge the whole selection together.
  std::vector<LayerId> merge_list;
  if (normalized.size() == 1U) {
    const auto id = normalized.front();
    const auto* single = doc.find_layer(id);
    if (single != nullptr && single->kind() == LayerKind::Group) {
      merge_list = {id};
    } else {
      const auto location = find_layer_location(doc.layers(), id);
      if (!location.has_value() || location->index == 0U) {
        show_status_error(tr("No layer below to merge"));
        return;
      }
      merge_list = {(*location->siblings)[location->index - 1U].id(), id};
    }
  } else {
    merge_list = normalized;  // already sorted bottom-to-top
  }

  // Render the visible items into a scratch document. Hidden items contribute nothing and are simply
  // dropped from the render -- they are still removed below, matching how Photoshop discards hidden
  // layers when merging. Folders and adjustment layers are added as-is so the compositor flattens or
  // applies them; anything the compositor can't draw is skipped the same way. The merged pixels land
  // in the bottom-most visible item, which keeps its id and name.
  Rect affected;
  Document merge_document(doc.width(), doc.height(), doc.format());
  LayerId target_id = 0;
  for (const auto id : merge_list) {
    const auto* layer = doc.find_layer(id);
    if (layer == nullptr) {
      show_status_error(tr("Select a layer to merge down"));
      return;
    }
    if (!layer_is_effectively_visible(doc.layers(), id)) {
      continue;
    }
    std::optional<Layer> renderable;
    if (layer->kind() == LayerKind::Group || layer->kind() == LayerKind::Adjustment) {
      renderable = *layer;
    } else {
      renderable = renderable_merge_layer_copy(*layer);
    }
    if (!renderable.has_value()) {
      continue;
    }
    if (target_id == 0) {
      target_id = id;
    }
    affected = unite_rect(affected, layer_render_bounds(*layer));
    auto& added = merge_document.add_layer(std::move(*renderable));
    affected = unite_rect(affected, layer_render_bounds(added));
  }

  if (target_id == 0) {
    statusBar()->showMessage(tr("Nothing to merge down"));
    return;
  }
  if (layer_id_locks_image_pixels(target_id)) {
    show_status_error(tr("Target layer pixels are locked. Unlock image pixels to merge down."));
    return;
  }

  auto merge_bounds = affected;
  merge_bounds = intersect_rect(merge_bounds, Rect::from_size(doc.width(), doc.height()));
  if (merge_bounds.empty()) {
    statusBar()->showMessage(tr("Nothing to merge down"));
    return;
  }

  const auto image =
      qimage_from_document_rect(merge_document, QRect(merge_bounds.x, merge_bounds.y, merge_bounds.width, merge_bounds.height), true);
  if (image.isNull()) {
    statusBar()->showMessage(tr("Nothing to merge down"));
    return;
  }
  auto merged_pixels = pixels_from_image_rgba(image);

  push_undo_snapshot(tr("Merge down"));
  const auto target_location = find_layer_location(doc.layers(), target_id);
  if (!target_location.has_value()) {
    refresh_layer_list();
    refresh_layer_controls();
    show_status_error(tr("Select a layer to merge down"));
    return;
  }

  Layer& target = (*target_location->siblings)[target_location->index];
  if (target.kind() != LayerKind::Pixel) {
    // A folder/adjustment/text target can't simply hold the merged pixels; replace it in place with a
    // fresh pixel layer that keeps its id and name (dropping any now-flattened children/metadata).
    Layer merged(target_id, target.name(), std::move(merged_pixels));
    merged.set_bounds(merge_bounds);
    target = std::move(merged);
  } else {
    target.set_pixels(std::move(merged_pixels));
    target.set_bounds(merge_bounds);
    target.set_opacity(1.0F);
    target.set_fill_opacity(1.0F);
    target.set_blend_mode(BlendMode::Normal);
    target.clear_mask();
    target.layer_style() = {};
    clear_layer_text_metadata(target);
    strip_layer_smart_object_data(doc, target);
    strip_layer_vector_data(target);
    clear_layer_psd_style_source(target);
    target.set_visible(true);
  }

  // Remove every other merged item by id. remove_layer searches the whole tree, so cross-folder
  // selections are handled; do not touch the `target` reference after this -- erasing siblings can
  // invalidate it.
  for (const auto id : merge_list) {
    if (id != target_id) {
      doc.remove_layer(id);
    }
  }
  doc.set_active_layer(target_id);
  affected = unite_rect(affected, merge_bounds);
  refresh_layer_list();
  refresh_layer_controls();
  canvas_->document_changed(to_qrect(affected));
  statusBar()->showMessage(merge_list.size() == 2U ? tr("Merged layer down") : tr("Merged layers down"));
}

QColor MainWindow::current_text_color() const {
  if (canvas_ != nullptr) {
    if (const auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")); editor != nullptr) {
      if (const auto color = text_color_from_format(text_editor_reference_format(*editor)); color.has_value()) {
        return *color;
      }
      const auto color = editor->property("patchy.documentTextColor").value<QColor>();
      if (color.isValid()) {
        return color;
      }
    }
    return canvas_->primary_color();
  }
  return QColor(Qt::black);
}

void MainWindow::sync_text_options_from_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }

  const auto format = text_editor_reference_format(*editor);
  const auto format_font = format.font();
  const auto display_family = text_display_family_from_format(
      format, editor->property("patchy.documentTextFamily").toString());
  if (!display_family.trimmed().isEmpty() && text_font_combo_ != nullptr) {
    QSignalBlocker blocker(text_font_combo_);
    text_font_combo_->setCurrentFont(text_font_combo_font_for_family(display_family));
  }
  if (!display_family.trimmed().isEmpty()) {
    editor->setProperty("patchy.documentTextFamily", display_family);
  }

  const auto document_text_size = document_text_size_from_editor_format(
      format, canvas_->zoom(), std::max(1, editor->property("patchy.documentTextSize").toInt()));
  if (text_size_spin_ != nullptr) {
    QSignalBlocker blocker(text_size_spin_);
    const auto display_size = std::max(
        1, static_cast<int>(std::lround(document_text_size * text_editor_size_display_scale(*editor))));
    text_size_spin_->setValue(
        std::clamp(text_pixels_to_points(display_size, document()), text_size_spin_->minimum(),
                   text_size_spin_->maximum()));
  }

  if (text_bold_button_ != nullptr) {
    QSignalBlocker blocker(text_bold_button_);
    text_bold_button_->setChecked(format_font.bold());
  }
  if (text_italic_button_ != nullptr) {
    QSignalBlocker blocker(text_italic_button_);
    text_italic_button_->setChecked(format_font.italic());
  }
  set_text_smoothing_combo_value(text_smoothing_combo_,
                                 std::clamp(editor->property("patchy.documentTextAntiAlias").toInt(), 0, 16));

  if (const auto color = text_color_from_format(format); color.has_value()) {
    editor->setProperty("patchy.documentTextColor", *color);
  }
  refresh_text_color_button();
  sync_text_character_dialog_from_editor();
}

void MainWindow::remove_text_editor_transform_overlay(QTextEdit* editor) {
  if (editor != nullptr) {
    const auto was_active = editor->property(kTextEditorTransformedOverlayProperty).toBool();
    editor->setProperty(kTextEditorTransformedOverlayProperty, false);
    if (was_active && editor->viewport() != nullptr) {
      editor->viewport()->update();
    }
  }

  auto* overlay = transformed_text_edit_overlay_for_canvas(canvas_);
  if (overlay == nullptr || (editor != nullptr && overlay->editor() != editor)) {
    return;
  }
  overlay->hide();
  overlay->deleteLater();
}

void MainWindow::update_text_editor_transform_overlay(QTextEdit* editor) {
  if (canvas_ == nullptr || editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool() ||
      !editor->property(kTextEditorPreviewPaintProperty).toBool() ||
      editor->property("patchy.usesPsdTextFrame").toBool() ||
      !editor->property("patchy.editingLayerId").isValid()) {
    remove_text_editor_transform_overlay(editor);
    return;
  }

  const auto layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
  auto* layer = document().find_layer(layer_id);
  if (layer == nullptr) {
    remove_text_editor_transform_overlay(editor);
    return;
  }

  const auto transform = text_transform_for_editor_or_layer(*editor, *layer);
  if (!transform.has_value() || !qtransform_has_non_translation_linear_part(*transform)) {
    remove_text_editor_transform_overlay(editor);
    return;
  }

  auto* overlay = transformed_text_edit_overlay_for_canvas(canvas_);
  if (overlay == nullptr) {
    overlay = new TransformedTextEditOverlay(canvas_);
  }

  const auto was_active = editor->property(kTextEditorTransformedOverlayProperty).toBool();
  editor->setProperty(kTextEditorTransformedOverlayProperty, true);
  overlay->configure(editor, *transform, [this](QTextEdit* active_editor) {
    mark_text_editor_changed(active_editor);
    relayout_text_editor(active_editor, false);
    schedule_text_editor_preview(active_editor);
  });
  if (!was_active && editor->viewport() != nullptr) {
    editor->viewport()->update();
  }
}

void MainWindow::relayout_text_editor(QTextEdit* editor, bool allow_point_auto_expand) {
  if (canvas_ == nullptr || editor == nullptr) {
    return;
  }
  if (editor->property("patchy.relayoutingTextEditor").toBool()) {
    return;
  }
  struct RelayoutGuard {
    QTextEdit* editor = nullptr;
    ~RelayoutGuard() {
      if (editor != nullptr) {
        editor->setProperty("patchy.relayoutingTextEditor", false);
      }
    }
  } guard{editor};
  editor->setProperty("patchy.relayoutingTextEditor", true);
  const auto zoom = std::max(0.05, canvas_->zoom());
  const auto boxed = text_flow_is_box(editor->property("patchy.documentTextFlow").toString());
  const QPoint document_point(editor->property("patchy.documentTextX").toInt(),
                              editor->property("patchy.documentTextY").toInt());
  bool old_zoom_ok = false;
  const auto old_zoom = editor->property("patchy.editorZoom").toDouble(&old_zoom_ok);
  if (old_zoom_ok && old_zoom > 0.0 && std::abs(old_zoom - zoom) > 0.0001) {
    editor->setProperty("patchy.editorZoom", zoom);
    scale_document_font_sizes(*editor->document(), zoom / old_zoom);
  } else if (!old_zoom_ok) {
    editor->setProperty("patchy.editorZoom", zoom);
  }
  const auto text_color = editor->property("patchy.documentTextColor").value<QColor>().isValid()
                              ? editor->property("patchy.documentTextColor").value<QColor>()
                              : canvas_->primary_color();
  const auto document_text_size = std::max(1, editor->property("patchy.documentTextSize").toInt());
  auto editor_font = editor->font();
  editor_font.setPixelSize(std::max(8, static_cast<int>(std::round(document_text_size * zoom))));
  configure_text_font_smoothing(editor_font, std::clamp(editor->property("patchy.documentTextAntiAlias").toInt(), 0, 16));
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);
  apply_text_smoothing_to_document(*editor->document(),
                                   std::clamp(editor->property("patchy.documentTextAntiAlias").toInt(), 0, 16));
  editor->setStyleSheet(inline_text_editor_style(text_color, editor_font.pixelSize()));
  preserve_text_editor_typing_format(*editor, editor_font, text_color);
  auto document_editor_width =
      std::max(kMinimumTextBoxDocumentSize, editor->property("patchy.documentTextWidth").toInt());
  auto document_editor_height =
      std::max(kMinimumTextBoxDocumentSize, editor->property("patchy.documentTextHeight").toInt());

  QTextOption option;
  option.setWrapMode(boxed ? QTextOption::WordWrap : QTextOption::NoWrap);
  option.setUseDesignMetrics(true);
  editor->document()->setDefaultTextOption(option);
  editor->setLineWrapMode(boxed ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);

  int width = std::max(1, static_cast<int>(std::round(document_editor_width * zoom)));
  int height = std::max(1, static_cast<int>(std::round(document_editor_height * zoom)));
  if (boxed) {
    editor->document()->setTextWidth(width);
  } else {
    editor->document()->setTextWidth(-1);
    const auto minimum_width = std::max(80, width);
    const auto content_width = static_cast<int>(std::ceil(editor->document()->idealWidth())) + 6;
    if (text_document_has_non_left_alignment(*editor->document())) {
      // QTextEdit lays aligned NoWrap text out against the viewport width, so any slack between
      // the widget and the content shifts the editor's glyphs/caret away from the rendered layer
      // pixels, which lay out against the tight ideal width.  Track the content exactly (shrink
      // as well as grow) for centered/right point text; left text keeps the roomier behavior.
      width = std::max(80, content_width);
      document_editor_width =
          std::max(1, static_cast<int>(std::ceil(static_cast<double>(width) / zoom)));
    } else {
      width = allow_point_auto_expand ? std::max(minimum_width, content_width) : minimum_width;
      document_editor_width = std::max(document_editor_width,
                                       static_cast<int>(std::ceil(static_cast<double>(width) / zoom)));
    }
    editor->setProperty("patchy.documentTextWidth", document_editor_width);
    editor->document()->setTextWidth(width);
    const auto text_height =
        std::max(32, static_cast<int>(std::ceil(editor->document()->size().height())) + 2);
    const auto minimum_height =
        std::max(32, static_cast<int>(std::ceil(static_cast<double>(document_text_size) * zoom * 1.45)));
    height = std::max(text_height, minimum_height);
    document_editor_height = std::max(kMinimumTextBoxDocumentSize,
                                      static_cast<int>(std::ceil(static_cast<double>(height) / zoom)));
    editor->setProperty("patchy.documentTextHeight", document_editor_height);
  }

  const auto widget_point = canvas_->widget_position_for_document_point(document_point);
  editor->setGeometry(widget_point.x(), widget_point.y(), std::max(1, width), std::max(1, height));
  update_text_editor_transform_overlay(editor);
  update_text_editor_handles(editor);
  editor->updateGeometry();
  reset_text_editor_scroll(editor);
  update_text_editor_preview_caret(*editor, zoom);
  editor->update();
}

void MainWindow::update_text_editor_handles(QTextEdit* editor) {
  if (canvas_ == nullptr || editor == nullptr) {
    return;
  }
  if (editor->property(kTextEditorTransformedOverlayProperty).toBool()) {
    remove_text_editor_handles(editor);
    return;
  }

  struct HandleSpec {
    const char* name;
    const char* corner;
    Qt::CursorShape cursor;
  };
  static constexpr std::array<HandleSpec, 4> kHandles = {{
      {"textBoxResizeHandleTopLeft", "topLeft", Qt::SizeFDiagCursor},
      {"textBoxResizeHandleTopRight", "topRight", Qt::SizeBDiagCursor},
      {"textBoxResizeHandleBottomLeft", "bottomLeft", Qt::SizeBDiagCursor},
      {"textBoxResizeHandleBottomRight", "bottomRight", Qt::SizeFDiagCursor},
  }};

  const auto editor_rect = editor->geometry();
  const auto handle_size = 9;
  for (const auto& spec : kHandles) {
    auto* handle = canvas_->findChild<QWidget*>(QString::fromLatin1(spec.name), Qt::FindDirectChildrenOnly);
    if (handle == nullptr) {
      handle = new QWidget(canvas_);
      handle->setObjectName(QString::fromLatin1(spec.name));
      handle->setProperty("patchy.textResizeHandle", true);
      handle->setProperty("patchy.textResizeCorner", QString::fromLatin1(spec.corner));
      handle->setCursor(spec.cursor);
      handle->setFocusPolicy(Qt::NoFocus);
      handle->setAttribute(Qt::WA_StyledBackground, true);
      handle->setMouseTracking(true);
      handle->setStyleSheet(QStringLiteral(
          "background: rgb(245, 248, 252); border: 1px solid rgb(35, 38, 44); border-radius: 1px;"));
      handle->installEventFilter(this);
    }
    QPoint position;
    const auto corner = QString::fromLatin1(spec.corner);
    if (corner == QStringLiteral("topLeft")) {
      position = editor_rect.topLeft();
    } else if (corner == QStringLiteral("topRight")) {
      position = editor_rect.topRight() + QPoint(1, 0);
    } else if (corner == QStringLiteral("bottomLeft")) {
      position = editor_rect.bottomLeft() + QPoint(0, 1);
    } else {
      position = editor_rect.bottomRight() + QPoint(1, 1);
    }
    handle->setGeometry(position.x() - handle_size / 2, position.y() - handle_size / 2, handle_size, handle_size);
    handle->show();
    handle->raise();
  }
}

QWidget* MainWindow::text_editor_resize_handle_at(QPoint canvas_position) const {
  if (canvas_ == nullptr) {
    return nullptr;
  }
  QWidget* best_handle = nullptr;
  int best_distance = std::numeric_limits<int>::max();
  const auto handles = canvas_->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
  for (auto* handle : handles) {
    if (handle == nullptr || !handle->isVisible() || !handle->property("patchy.textResizeHandle").toBool()) {
      continue;
    }
    const auto hit_rect = handle->geometry().adjusted(-8, -8, 8, 8);
    if (!hit_rect.contains(canvas_position)) {
      continue;
    }
    const auto delta = hit_rect.center() - canvas_position;
    const auto distance = delta.x() * delta.x() + delta.y() * delta.y();
    if (distance < best_distance) {
      best_distance = distance;
      best_handle = handle;
    }
  }
  return best_handle;
}

bool MainWindow::handle_text_editor_resize_event(QWidget* handle, QTextEdit* editor, QEvent* event) {
  if (canvas_ == nullptr || handle == nullptr || editor == nullptr) {
    return false;
  }
  switch (event->type()) {
    case QEvent::MouseButtonPress: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if (mouse_event->button() != Qt::LeftButton) {
        break;
      }
      handle->setProperty("patchy.dragStartGlobal", mouse_event->globalPosition().toPoint());
      handle->setProperty("patchy.dragStartX", editor->property("patchy.documentTextX").toInt());
      handle->setProperty("patchy.dragStartY", editor->property("patchy.documentTextY").toInt());
      handle->setProperty("patchy.dragStartWidth", editor->property("patchy.documentTextWidth").toInt());
      handle->setProperty("patchy.dragStartHeight", editor->property("patchy.documentTextHeight").toInt());
      editor->setProperty("patchy.activeTextResizeHandleName", handle->objectName());
      handle->grabMouse();
      mouse_event->accept();
      return true;
    }
    case QEvent::MouseMove: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      if ((mouse_event->buttons() & Qt::LeftButton) == 0 ||
          !handle->property("patchy.dragStartGlobal").isValid()) {
        break;
      }
      const auto start_global = handle->property("patchy.dragStartGlobal").toPoint();
      const auto delta = mouse_event->globalPosition().toPoint() - start_global;
      const auto zoom = std::max(0.05, canvas_->zoom());
      const auto delta_x = static_cast<int>(std::round(static_cast<double>(delta.x()) / zoom));
      const auto delta_y = static_cast<int>(std::round(static_cast<double>(delta.y()) / zoom));
      const auto start_x = handle->property("patchy.dragStartX").toInt();
      const auto start_y = handle->property("patchy.dragStartY").toInt();
      const auto start_width = std::max(kMinimumTextBoxDocumentSize, handle->property("patchy.dragStartWidth").toInt());
      const auto start_height = std::max(kMinimumTextBoxDocumentSize, handle->property("patchy.dragStartHeight").toInt());
      auto left = start_x;
      auto top = start_y;
      auto right = start_x + start_width;
      auto bottom = start_y + start_height;
      const auto corner = handle->property("patchy.textResizeCorner").toString();
      if (corner.contains(QStringLiteral("Left"))) {
        left = std::min(right - kMinimumTextBoxDocumentSize, start_x + delta_x);
      }
      if (corner.contains(QStringLiteral("Right"))) {
        right = std::max(left + kMinimumTextBoxDocumentSize, start_x + start_width + delta_x);
      }
      if (corner.contains(QStringLiteral("top"), Qt::CaseInsensitive)) {
        top = std::min(bottom - kMinimumTextBoxDocumentSize, start_y + delta_y);
      }
      if (corner.contains(QStringLiteral("bottom"), Qt::CaseInsensitive)) {
        bottom = std::max(top + kMinimumTextBoxDocumentSize, start_y + start_height + delta_y);
      }
      mark_text_editor_changed(editor);
      editor->setProperty("patchy.documentTextFlow", QString::fromLatin1(kTextFlowBox));
      editor->setProperty(kTextEditorSourceVisibleAnchorProperty, QVariant());
      editor->setProperty(kTextEditorSourceVisibleSizeProperty, QVariant());
      // Dragging the box invalidates the import-frame caches: drop the anchor/local-rect so the
      // glyphs lay out against the live box rather than the original Photoshop frame.
      clear_text_editor_render_local_rect(*editor);
      clear_text_editor_visible_local_rect(*editor);
      // Retarget (rather than drop) any text transform to the new box origin. Clearing it would let
      // text_transform_for_editor_or_layer() fall back to the layer's original import transform,
      // which re-pins the rendered glyphs at their old position while the caret/markers move -- the
      // regression this fixes. The handle path only runs for translation-only text (rotated/skewed
      // text uses the overlay resize path), so a pure-translation override is sufficient here.
      bool retarget_transform = text_editor_transform_override(*editor).has_value();
      if (!retarget_transform && editor->property("patchy.editingLayerId").isValid()) {
        const auto layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
        if (const auto* layer = document().find_layer(layer_id); layer != nullptr) {
          retarget_transform = patchy_text_transform_for_layer(*layer).has_value();
        }
      }
      if (retarget_transform) {
        set_text_editor_transform_override(
            *editor, LayerAffineTransform{1.0, 0.0, 0.0, 1.0, static_cast<double>(left),
                                          static_cast<double>(top)});
      } else {
        editor->setProperty(kTextEditorTransformOverrideProperty, QVariant());
      }
      editor->setProperty("patchy.documentTextX", left);
      editor->setProperty("patchy.documentTextY", top);
      editor->setProperty("patchy.documentTextWidth", std::max(kMinimumTextBoxDocumentSize, right - left));
      editor->setProperty("patchy.documentTextHeight", std::max(kMinimumTextBoxDocumentSize, bottom - top));
      relayout_text_editor(editor, false);
      schedule_text_editor_preview(editor);
      mouse_event->accept();
      return true;
    }
    case QEvent::MouseButtonRelease: {
      auto* mouse_event = static_cast<QMouseEvent*>(event);
      handle->setProperty("patchy.dragStartGlobal", QVariant());
      editor->setProperty("patchy.activeTextResizeHandleName", QVariant());
      handle->releaseMouse();
      mouse_event->accept();
      return true;
    }
    default:
      break;
  }
  return false;
}

bool MainWindow::handle_text_editor_transform_overlay_event(QTextEdit* editor, QEvent* event) {
  if (canvas_ == nullptr || editor == nullptr) {
    return false;
  }
  if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseMove &&
      event->type() != QEvent::MouseButtonRelease && event->type() != QEvent::MouseButtonDblClick) {
    return false;
  }

  auto* overlay = transformed_text_edit_overlay_for_canvas(canvas_);
  if (overlay == nullptr || overlay->editor() != editor || !overlay->isVisible()) {
    return false;
  }

  auto* mouse_event = static_cast<QMouseEvent*>(event);
  const QPointF canvas_position = QPointF(canvas_->mapFromGlobal(mouse_event->globalPosition().toPoint()));
  return overlay->handle_canvas_mouse_event(mouse_event, canvas_position);
}

void MainWindow::remove_text_editor_handles(QTextEdit* /*editor*/) {
  if (canvas_ == nullptr) {
    return;
  }
  const auto handles = canvas_->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
  for (auto* handle : handles) {
    if (handle != nullptr && handle->property("patchy.textResizeHandle").toBool()) {
      handle->hide();
      handle->deleteLater();
    }
  }
}

void MainWindow::mark_text_editor_changed(QTextEdit* editor) {
  if (canvas_ == nullptr || editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }
  if (editor->property("patchy.relayoutingTextEditor").toBool()) {
    return;
  }
  editor->setProperty(kTextEditorChangedProperty, true);
  editor->setProperty(kTextEditorCaretLayoutGenerationProperty,
                      editor->property(kTextEditorCaretLayoutGenerationProperty).toInt() + 1);
  if (!editor->property(kTextEditorSourceRasterPreviewProperty).toBool()) {
    return;
  }

  editor->setProperty(kTextEditorSourceRasterPreviewProperty, false);
  // The Photoshop raster can no longer represent the edited text.  Plain point text previously
  // fell back to the editor widget's own glyph painting here, but the widget lays aligned text
  // out against its own width -- centered/right-justified text then shows one layout while
  // editing and a different one after commit.  Hand the session to the live baked preview
  // instead, so the on-screen glyphs come from render_text_pixels, the same rasterizer the
  // commit uses (the installed-font flow already edits this way from the start).
  const bool adopt_baked_preview =
      !text_flow_is_box(editor->property("patchy.documentTextFlow").toString()) &&
      !editor->property("patchy.usesPsdTextFrame").toBool() &&
      !editor->property(kTextEditorPreviewEnabledProperty).toBool();
  if (adopt_baked_preview) {
    editor->setProperty(kTextEditorForceBakedPreviewProperty, true);
    editor->setProperty(kTextEditorPreviewEnabledProperty, true);
  }
  editor->setProperty(kTextEditorPreviewPaintProperty,
                      adopt_baked_preview ||
                          editor->property(kTextEditorExtendedBoxPreviewProperty).toBool() ||
                          editor->property(kTextEditorLineAwareBoxPreviewProperty).toBool());
  update_text_editor_transform_overlay(editor);
  clear_text_editor_preview_overlays(*editor);
  remove_text_editor_preview(editor);

  if (editor->property("patchy.editingLayerId").isValid()) {
    const auto layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
    if (auto* layer = document().find_layer(layer_id); layer != nullptr && layer->visible()) {
      layer->set_visible(false);
      canvas_->document_changed_effect_bounds(to_qrect(layer_render_bounds(*layer)));
      refresh_layer_list();
      refresh_layer_controls();
    }
  }
  if (adopt_baked_preview) {
    // Render the first live preview immediately: the editor no longer paints its own glyphs and
    // the debounced preview pass would otherwise leave the text invisible for a beat.
    update_text_editor_preview(editor);
  }
}

void MainWindow::schedule_text_editor_preview(QTextEdit* editor) {
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }
  if (editor->property(kTextEditorSourceRasterPreviewProperty).toBool()) {
    editor->setProperty("patchy.textPreviewPending", false);
    update_text_editor_preview(editor);
    return;
  }
  if (!editor->property(kTextEditorPreviewEnabledProperty).toBool()) {
    editor->setProperty(kTextEditorPreviewPaintProperty, false);
    update_text_editor_transform_overlay(editor);
    clear_text_editor_preview_overlays(*editor);
    editor->setProperty("patchy.textPreviewPending", false);
    remove_text_editor_preview(editor);
    editor->viewport()->update();
    return;
  }
  if (editor->property(kTextEditorExtendedBoxPreviewProperty).toBool() &&
      !editor->property(kTextEditorSourceRasterPreviewProperty).toBool()) {
    editor->setProperty("patchy.textPreviewPending", false);
    update_text_editor_preview(editor);
    return;
  }

  const auto generation = editor->property(kTextEditorPreviewGenerationProperty).toInt() + 1;
  editor->setProperty(kTextEditorPreviewGenerationProperty, generation);
  const auto expensive_preview = editor->property(kTextEditorPreviewExpensiveProperty).toBool();
  const auto transformed_preview = editor->property(kTextEditorTransformedOverlayProperty).toBool();
  if (expensive_preview && !transformed_preview) {
    editor->setProperty(kTextEditorPreviewPaintProperty, false);
    update_text_editor_transform_overlay(editor);
    clear_text_editor_preview_overlays(*editor);
    remove_text_editor_preview(editor);
    editor->viewport()->update();
  }
  editor->setProperty("patchy.textPreviewPending", true);
  QTimer::singleShot(expensive_preview && !transformed_preview ? kExpensiveTextEditorPreviewDelayMs
                                                               : kTextEditorPreviewDelayMs,
                     editor,
                     [this, editor = QPointer<QTextEdit>(editor), generation] {
    if (editor == nullptr) {
      return;
    }
    if (editor->property(kTextEditorFinishedProperty).toBool() ||
        !editor->property(kTextEditorPreviewEnabledProperty).toBool()) {
      editor->setProperty("patchy.textPreviewPending", false);
      return;
    }
    if (editor->property(kTextEditorPreviewGenerationProperty).toInt() != generation) {
      return;
    }
    editor->setProperty("patchy.textPreviewPending", false);
    update_text_editor_preview(editor);
  });
}

void MainWindow::update_text_editor_preview(QTextEdit* editor) {
  if (canvas_ == nullptr || editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }
  if (editor->property(kTextEditorSourceRasterPreviewProperty).toBool()) {
    editor->setProperty(kTextEditorPreviewPaintProperty, true);
    editor->setProperty("patchy.textPreviewPending", false);
    remove_text_editor_preview(editor);
    update_text_editor_preview_caret(*editor, canvas_->zoom());
    update_text_editor_transform_overlay(editor);
    editor->viewport()->update();
    return;
  }
  auto& doc = document();
  std::optional<LayerId> editing_layer_id;
  if (editor->property("patchy.editingLayerId").isValid()) {
    editing_layer_id = static_cast<LayerId>(editor->property("patchy.editingLayerId").toULongLong());
  }
  auto* source = editing_layer_id.has_value() ? doc.find_layer(*editing_layer_id) : nullptr;
  const auto source_was_visible = !editor->property("patchy.editingLayerWasVisible").isValid() ||
                                  editor->property("patchy.editingLayerWasVisible").toBool();
  const auto extended_box_preview = editor->property(kTextEditorExtendedBoxPreviewProperty).toBool();
  const auto line_aware_box_preview = editor->property(kTextEditorLineAwareBoxPreviewProperty).toBool();
  // Plain installed-font point text carries no layer effect, so it would not normally need a preview;
  // force one anyway (kTextEditorForceBakedPreviewProperty) so its on-screen glyphs come from the same
  // render_text_pixels rasterizer the committed layer uses -- no antialiasing/position shift on edit.
  const auto force_baked_preview = editor->property(kTextEditorForceBakedPreviewProperty).toBool();
  const auto needs_text_preview =
      source != nullptr && source_was_visible &&
      (layer_requires_text_editor_preview(*source) || extended_box_preview || line_aware_box_preview ||
       force_baked_preview);
  editor->setProperty(kTextEditorPreviewEnabledProperty, needs_text_preview);
  if (!needs_text_preview) {
    editor->setProperty(kTextEditorPreviewPaintProperty, false);
    update_text_editor_transform_overlay(editor);
    clear_text_editor_preview_overlays(*editor);
    editor->viewport()->update();
    remove_text_editor_preview(editor);
    return;
  }

  // Untrimmed: run offsets index the full document text (see commit_text_editor).
  const auto text = editor->toPlainText();
  if (text.trimmed().isEmpty()) {
    editor->setProperty(kTextEditorPreviewPaintProperty, false);
    update_text_editor_transform_overlay(editor);
    clear_text_editor_preview_overlays(*editor);
    editor->viewport()->update();
    remove_text_editor_preview(editor);
    return;
  }

  const auto text_size = std::max(1, editor->property("patchy.documentTextSize").toInt());
  const auto text_width =
      std::max(kMinimumTextBoxDocumentSize, editor->property("patchy.documentTextWidth").toInt());
  const auto text_height =
      std::max(kMinimumTextBoxDocumentSize, editor->property("patchy.documentTextHeight").toInt());
  const auto boxed_text =
      text_flow_is_box(editor->property("patchy.documentTextFlow").toString());
  const auto text_color = editor->property("patchy.documentTextColor").value<QColor>().isValid()
                              ? editor->property("patchy.documentTextColor").value<QColor>()
                              : canvas_->primary_color();
  const auto text_anti_alias = std::clamp(editor->property("patchy.documentTextAntiAlias").toInt(), 0, 16);
  auto text_family = editor->property("patchy.documentTextFamily").toString();
  if (text_family.trimmed().isEmpty()) {
    text_family = text_display_family_from_format(text_editor_reference_format(*editor),
                                                  display_text_family_from_font(editor->font()));
  }
  auto document_text = document_from_editor_in_document_units(*editor, canvas_->zoom());
  TextToolSettings settings{text,
                            QString(),
                            text_family,
                            text_size,
                            editor->font().bold(),
                            editor->font().italic(),
                            text_anti_alias,
                            boxed_text,
                            text_width,
                            text_height};
  settings.photoshop_layout = text_editor_uses_photoshop_layout(*editor);
  const auto paragraph_runs = paragraph_runs_from_document(*document_text);
  const auto rich_text_runs = rich_text_runs_from_document(*document_text, settings, text_color);
  settings.html = document_html_from_text_runs(document_text->toPlainText(), rich_text_runs, settings, text_color);
  const auto frame_layout_scale = settings.photoshop_layout && editor->property("patchy.usesPsdTextFrame").toBool()
                                      ? text_editor_size_display_scale(*editor)
                                      : 1.0;
  auto rendered = render_text_pixels_with_local_rect(settings, text_color, text_width, paragraph_runs,
                                                     rich_text_runs, text_editor_render_local_rect(*editor),
                                                     text_editor_metric_scale(*editor), QTransform(),
                                                     frame_layout_scale);
  if (rendered.pixels.empty()) {
    editor->setProperty(kTextEditorPreviewPaintProperty, false);
    update_text_editor_transform_overlay(editor);
    clear_text_editor_preview_overlays(*editor);
    editor->viewport()->update();
    remove_text_editor_preview(editor);
    return;
  }
  if (!boxed_text && !editor->property("patchy.usesPsdTextFrame").toBool()) {
    bool updated_transform = false;
    if (source != nullptr) {
      updated_transform = update_text_editor_transform_from_psd_local_bounds(*editor, *source, rendered.pixels,
                                                                            boxed_text);
    }
    if (!updated_transform) {
      update_text_editor_transform_from_source_anchor(*editor, rendered.pixels);
    }
  }

  const QPoint document_point(editor->property("patchy.documentTextX").toInt(),
                              editor->property("patchy.documentTextY").toInt());
  auto preview_bounds = rendered_text_bounds_for_editor(*editor, document_point, rendered);
  auto pixels = std::move(rendered.pixels);
  if (source != nullptr) {
    if (const auto transform = text_transform_for_editor_or_layer(*editor, *source);
        transform.has_value() && !editor->property("patchy.usesPsdTextFrame").toBool() &&
        !(text_editor_render_local_rect(*editor).has_value() &&
          text_editor_source_visible_anchor(*editor).has_value() &&
          !qtransform_has_non_translation_linear_part(*transform))) {
      const bool has_local_offset =
          text_editor_render_local_rect(*editor).has_value() &&
          (std::abs(rendered.local_rect.left()) > 0.0001 || std::abs(rendered.local_rect.top()) > 0.0001);
      auto transform_for_pixels = *transform;
      if (has_local_offset) {
        transform_for_pixels = qtransform_from_affine(
            affine_with_local_translation(affine_from_qtransform(*transform), rendered.local_rect.topLeft()));
      }
      // Show the same crisp glyphs the commit will produce: previewing a scaled-up text layer by
      // resampling its base-size bitmap left the text blurry for the whole edit session (then it
      // snapped sharp on commit), which made small-point/large-scale PSD text unreadable while typing.
      if (auto crisp = render_crisp_transformed_text_for_editor(
              *editor, source->metadata().contains(kLayerMetadataPsdTextTransform), boxed_text,
              has_local_offset, settings, text_color, text_width, paragraph_runs, rich_text_runs, *transform,
              transform_for_pixels);
          crisp.has_value()) {
        pixels = std::move(crisp->pixels);
        preview_bounds = crisp->bounds;
      } else {
        auto transformed = apply_text_transform_to_pixels(pixels, transform_for_pixels);
        pixels = std::move(transformed.pixels);
        preview_bounds = transformed.bounds;
      }
    }
  }
  std::optional<LayerId> restore_active_layer;
  if (editor->property("patchy.restoreActiveLayerId").isValid()) {
    restore_active_layer = static_cast<LayerId>(editor->property("patchy.restoreActiveLayerId").toULongLong());
  }

  QRect dirty = to_qrect(preview_bounds);
  if (editor->property("patchy.textPreviewLayerId").isValid()) {
    const auto preview_id = static_cast<LayerId>(editor->property("patchy.textPreviewLayerId").toULongLong());
    if (auto* layer = doc.find_layer(preview_id); layer != nullptr) {
      dirty = dirty.united(to_qrect(layer_render_bounds(*layer)));
      layer->set_pixels(std::move(pixels));
      layer->set_bounds(preview_bounds);
      layer->set_opacity(source->opacity());
      layer->set_fill_opacity(source->fill_opacity());
      layer->set_blend_mode(source->blend_mode());
      layer->layer_style() = source->layer_style();
      dirty = dirty.united(to_qrect(layer_render_bounds(*layer)));
      canvas_->document_changed_effect_bounds(dirty);
      editor->setProperty(kTextEditorPreviewPaintProperty, true);
      update_text_editor_preview_caret(*editor, canvas_->zoom());
      update_text_editor_transform_overlay(editor);
      editor->viewport()->update();
      return;
    }
    editor->setProperty("patchy.textPreviewLayerId", QVariant());
  }

  Layer preview(doc.allocate_layer_id(), tr("Text Preview").toStdString(), std::move(pixels));
  preview.set_bounds(preview_bounds);
  preview.metadata()["patchy.internal.text_preview"] = "true";
  preview.set_opacity(source->opacity());
  preview.set_fill_opacity(source->fill_opacity());
  preview.set_blend_mode(source->blend_mode());
  preview.layer_style() = source->layer_style();
  const auto preview_id = preview.id();
  insert_layer_after_anchor(doc, std::move(preview), editing_layer_id);
  editor->setProperty("patchy.textPreviewLayerId", QVariant::fromValue<qulonglong>(preview_id));
  if (restore_active_layer.has_value() && doc.find_layer(*restore_active_layer) != nullptr) {
    doc.set_active_layer(*restore_active_layer);
  }
  if (auto* layer = doc.find_layer(preview_id); layer != nullptr) {
    dirty = dirty.united(to_qrect(layer_render_bounds(*layer)));
  }
  canvas_->document_changed_effect_bounds(dirty);
  editor->setProperty(kTextEditorPreviewPaintProperty, true);
  update_text_editor_preview_caret(*editor, canvas_->zoom());
  update_text_editor_transform_overlay(editor);
  editor->viewport()->update();
}

void MainWindow::remove_text_editor_preview(QTextEdit* editor) {
  if (canvas_ == nullptr || editor == nullptr || !editor->property("patchy.textPreviewLayerId").isValid()) {
    return;
  }
  auto& doc = document();
  const auto preview_id = static_cast<LayerId>(editor->property("patchy.textPreviewLayerId").toULongLong());
  QRect dirty;
  if (auto* layer = doc.find_layer(preview_id); layer != nullptr) {
    dirty = to_qrect(layer_render_bounds(*layer));
  }
  const auto removed = doc.remove_layer(preview_id);
  editor->setProperty("patchy.textPreviewLayerId", QVariant());
  if (editor->property("patchy.restoreActiveLayerId").isValid()) {
    const auto restore_id = static_cast<LayerId>(editor->property("patchy.restoreActiveLayerId").toULongLong());
    if (doc.find_layer(restore_id) != nullptr) {
      doc.set_active_layer(restore_id);
    }
  }
  if (removed) {
    canvas_->document_changed_effect_bounds(dirty);
  }
}

std::optional<LayerId> MainWindow::take_provisional_text_layer(QTextEdit* editor) {
  if (editor == nullptr || !editor->property(kTextEditorProvisionalLayerProperty).isValid()) {
    return std::nullopt;
  }
  const auto provisional_id =
      static_cast<LayerId>(editor->property(kTextEditorProvisionalLayerProperty).toULongLong());
  editor->setProperty(kTextEditorProvisionalLayerProperty, QVariant());
  if (canvas_ == nullptr || !has_active_document()) {
    return std::nullopt;
  }
  auto& doc = document();
  // Only remove the exact marker-tagged layer this session created: layer ids restart per
  // document, so after a tab switch or a mid-edit undo the id may be gone or belong to an
  // unrelated layer that must not be deleted.
  const auto* layer = std::as_const(doc).find_layer(provisional_id);
  if (layer == nullptr || !layer->metadata().contains(kLayerMetadataProvisionalTextMarker)) {
    return std::nullopt;
  }
  doc.remove_layer(provisional_id);
  if (editor->property("patchy.restoreActiveLayerId").isValid()) {
    const auto restore_id = static_cast<LayerId>(editor->property("patchy.restoreActiveLayerId").toULongLong());
    if (doc.find_layer(restore_id) != nullptr) {
      doc.set_active_layer(restore_id);
    }
  }
  return provisional_id;
}

void MainWindow::handle_canvas_view_changed(CanvasWidget* canvas) {
  if (canvas == nullptr || canvas != canvas_) {
    return;
  }
  refresh_document_info();
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }
  relayout_text_editor(editor, false);
  reset_text_editor_scroll(editor);
}

void MainWindow::apply_text_alignment_to_active_editor(Qt::Alignment alignment) {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }
  editor->setAlignment(alignment);
  mark_text_editor_changed(editor);
  sync_text_alignment_buttons_from_editor();
  schedule_text_editor_preview(editor);
}

void MainWindow::sync_text_alignment_buttons_from_editor() {
  if (text_align_left_button_ == nullptr || text_align_center_button_ == nullptr ||
      text_align_right_button_ == nullptr || canvas_ == nullptr) {
    return;
  }
  const auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  const auto alignment = editor != nullptr ? editor->alignment() : Qt::AlignLeft;
  const auto set_checked = [](QPushButton* button, bool checked) {
    QSignalBlocker blocker(button);
    button->setChecked(checked);
  };
  set_checked(text_align_left_button_, (alignment & (Qt::AlignHCenter | Qt::AlignRight)) == 0);
  set_checked(text_align_center_button_, (alignment & Qt::AlignHCenter) != 0);
  set_checked(text_align_right_button_, (alignment & Qt::AlignRight) != 0);
}

void MainWindow::apply_text_family_to_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }

  const auto family =
      text_font_combo_ != nullptr
          ? text_font_combo_->currentFont().family()
          : text_display_family_from_format(text_editor_reference_format(*editor),
                                            editor->property("patchy.documentTextFamily").toString());
  QTextCharFormat format;
  format.setFontFamilies(render_text_families_for_display_family(family));
  set_text_display_family(format, family);
  merge_text_char_format(*editor, format);
  auto editor_font = editor->font();
  editor_font.setFamilies(render_text_families_for_display_family(family));
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);
  editor->setProperty("patchy.documentTextFamily", family);

  mark_text_editor_changed(editor);
  relayout_text_editor(editor, true);
  schedule_text_editor_preview(editor);
}

void MainWindow::apply_text_size_to_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }

  // The spinbox shows the effective (transform-folded) size for imported Photoshop text; the
  // editor and stored runs work in engine units, so divide the display scale back out.
  const auto display_scale = text_editor_size_display_scale(*editor);
  const auto document_text_size =
      text_size_spin_ != nullptr
          ? std::max(1, static_cast<int>(std::lround(
                            text_points_to_pixels(text_size_spin_->value(), document()) / display_scale)))
          : std::max(1, editor->property("patchy.documentTextSize").toInt());
  const auto editor_pixel_size = std::max(8, static_cast<int>(std::round(document_text_size * canvas_->zoom())));
  QTextCharFormat format;
  format.setProperty(QTextFormat::FontPixelSize, editor_pixel_size);
  editor->setProperty("patchy.documentTextSize", document_text_size);
  merge_text_char_format(*editor, format);
  auto editor_font = editor->font();
  editor_font.setPixelSize(editor_pixel_size);
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);

  mark_text_editor_changed(editor);
  relayout_text_editor(editor, true);
  schedule_text_editor_preview(editor);
  refresh_text_color_button();
}

void MainWindow::apply_text_bold_to_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }

  const auto bold = text_bold_button_ != nullptr && text_bold_button_->isChecked();
  QTextCharFormat format;
  format.setFontWeight(bold ? QFont::Bold : QFont::Normal);
  merge_text_char_format(*editor, format);
  auto editor_font = editor->font();
  editor_font.setBold(bold);
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);

  mark_text_editor_changed(editor);
  relayout_text_editor(editor, true);
  schedule_text_editor_preview(editor);
  refresh_text_color_button();
}

void MainWindow::apply_text_italic_to_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }

  const auto italic = text_italic_button_ != nullptr && text_italic_button_->isChecked();
  QTextCharFormat format;
  format.setFontItalic(italic);
  merge_text_char_format(*editor, format);
  auto editor_font = editor->font();
  editor_font.setItalic(italic);
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);

  mark_text_editor_changed(editor);
  relayout_text_editor(editor, true);
  schedule_text_editor_preview(editor);
  refresh_text_color_button();
}

void MainWindow::apply_text_color_to_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }

  const auto text_color = editor->property("patchy.documentTextColor").value<QColor>().isValid()
                              ? editor->property("patchy.documentTextColor").value<QColor>()
                              : canvas_->primary_color();
  QTextCharFormat format;
  format.setForeground(QBrush(text_color));
  merge_text_char_format(*editor, format);

  mark_text_editor_changed(editor);
  relayout_text_editor(editor, true);
  schedule_text_editor_preview(editor);
  refresh_text_color_button();
}

void MainWindow::apply_primary_color_to_active_text_editor(QColor color) {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr || editor->property(kTextEditorFinishedProperty).toBool()) {
    return;
  }

  color.setAlpha(255);
  editor->setProperty("patchy.documentTextColor", color);
  apply_text_color_to_active_editor();
}

void MainWindow::apply_text_smoothing_to_active_editor() {
  if (canvas_ == nullptr) {
    return;
  }
  auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  if (editor == nullptr) {
    return;
  }

  const auto text_anti_alias = text_smoothing_combo_value(text_smoothing_combo_);
  editor->setProperty("patchy.documentTextAntiAlias", text_anti_alias);

  auto editor_font = editor->font();
  configure_text_font_smoothing(editor_font, text_anti_alias);
  editor->setFont(editor_font);
  editor->document()->setDefaultFont(editor_font);

  const auto cursor = editor->textCursor();
  apply_text_smoothing_to_document(*editor->document(), text_anti_alias);
  editor->setTextCursor(cursor);
  editor->setCurrentCharFormat(text_format_with_smoothing(editor->currentCharFormat(), text_anti_alias));

  mark_text_editor_changed(editor);
  relayout_text_editor(editor, true);
  schedule_text_editor_preview(editor);
}

bool MainWindow::is_text_option_widget(QWidget* widget) const {
  if (widget == nullptr) {
    return false;
  }
  if (widget->property("patchy.textResizeHandle").toBool()) {
    return true;
  }
  if (widget->objectName() == QString::fromLatin1(kTransformedTextEditOverlayObjectName)) {
    return true;
  }
  const auto owns = [widget](const QWidget* candidate) {
    return candidate != nullptr && (widget == candidate || candidate->isAncestorOf(widget));
  };
  const auto in_named_ancestor = [widget](QStringView object_name) {
    for (auto* current = widget; current != nullptr; current = current->parentWidget()) {
      if (current->objectName() == object_name) {
        return true;
      }
    }
    return false;
  };
  return owns(text_font_combo_) || owns(text_size_spin_) || owns(text_bold_button_) || owns(text_italic_button_) ||
         owns(text_smoothing_combo_) || owns(text_color_button_) || owns(text_align_left_button_) ||
         owns(text_align_center_button_) || owns(text_align_right_button_) || owns(text_apply_button_) ||
         owns(text_cancel_button_) || owns(text_character_button_) || owns(primary_color_button_) ||
         // The Character panel edits the LIVE session; focus moving into it must not commit.
         in_named_ancestor(QStringLiteral("textCharacterDialog")) ||
         // The font picker popup is a Qt::Popup window, so isAncestorOf-based ownership stops
         // at its boundary; without the name match, focusing its search box would auto-commit
         // an open inline text editor.
         in_named_ancestor(QString::fromLatin1(kFontPickerPopupObjectName));
}

MainWindow::PreviewDialogEditLock MainWindow::lock_preview_dialog_edits() {
  return PreviewDialogEditLock(*this);
}

void MainWindow::begin_preview_dialog_edit_lock() {
  ++preview_dialog_edit_lock_depth_;
  if (preview_dialog_edit_lock_depth_ != 1) {
    return;
  }
  preview_dialog_edit_lock_canvas_ = canvas_;
  // Every session's canvas locks, not just the active one: a document in a float
  // window stays clickable while the dialog is open, and activation snap-back
  // alone would still let its first click land an edit.
  for (const auto& target_session : sessions_) {
    if (target_session->canvas != nullptr) {
      target_session->canvas->set_edit_locked(true);
    }
  }
  if (document_tabs_ != nullptr) {
    if (auto* tab_bar = document_tabs_->tabBar(); tab_bar != nullptr) {
      tab_bar->setEnabled(false);
    }
  }
  update_document_action_state();
  update_undo_redo_actions();
}

void MainWindow::end_preview_dialog_edit_lock() {
  if (preview_dialog_edit_lock_depth_ <= 0) {
    preview_dialog_edit_lock_depth_ = 0;
    return;
  }
  --preview_dialog_edit_lock_depth_;
  if (preview_dialog_edit_lock_depth_ != 0) {
    return;
  }
  for (const auto& target_session : sessions_) {
    if (target_session->canvas != nullptr) {
      target_session->canvas->set_edit_locked(false);
    }
  }
  preview_dialog_edit_lock_canvas_ = nullptr;
  if (document_tabs_ != nullptr) {
    if (auto* tab_bar = document_tabs_->tabBar(); tab_bar != nullptr) {
      tab_bar->setEnabled(true);
    }
  }
  update_document_action_state();
  update_undo_redo_actions();
  refresh_layer_controls();
}

bool MainWindow::preview_dialog_edit_locked() const noexcept {
  return preview_dialog_edit_lock_depth_ > 0;
}

bool MainWindow::document_action_enabled_during_preview_lock(const QAction* action) const {
  if (action == nullptr) {
    return false;
  }
  const auto object_name = action->objectName();
  if (object_name == QStringLiteral("viewZoomInAction") ||
      object_name == QStringLiteral("viewZoomOutAction") ||
      object_name == QStringLiteral("viewFitOnScreenAction") ||
      object_name == QStringLiteral("viewActualPixelsAction") ||
      object_name == QStringLiteral("viewToggleSelectionEdgesAction") ||
      object_name == QStringLiteral("viewToggleRulersAction") ||
      object_name == QStringLiteral("viewToggleGridAction") ||
      object_name == QStringLiteral("viewToggleGuidesAction") ||
      object_name == QStringLiteral("viewToggleSnapAction") ||
      object_name == QStringLiteral("viewSnapToMenu") ||
      object_name == QStringLiteral("viewSnapToGuidesAction") ||
      object_name == QStringLiteral("viewSnapToGridAction") ||
      object_name == QStringLiteral("viewSnapToDocumentAction") ||
      object_name == QStringLiteral("viewSnapToLayersAction") ||
      object_name == QStringLiteral("viewSnapToSelectionAction") ||
      object_name == QStringLiteral("windowForceRefreshAction")) {
    return true;
  }
  if (action->data().isValid()) {
    const auto tool = static_cast<CanvasTool>(action->data().toInt());
    return tool == CanvasTool::Pan || tool == CanvasTool::Zoom;
  }
  return false;
}

bool MainWindow::show_preview_dialog_edit_lock_message() {
  show_status_error(tr("Finish the open dialog before editing the document"));
  return true;
}

void MainWindow::register_document_action(QAction* action) {
  if (action == nullptr) {
    return;
  }
  document_actions_.push_back(action);
}

void MainWindow::register_hotkey(QAction* action, QString id, QList<QKeySequence> default_shortcuts,
                                 QString category) {
  hotkey_registry_.register_command(action, std::move(id), std::move(default_shortcuts), std::move(category));
}

void MainWindow::register_hotkey(QAction* action, QString id, QKeySequence default_shortcut, QString category) {
  QList<QKeySequence> defaults;
  if (!default_shortcut.isEmpty()) {
    defaults << default_shortcut;
  }
  register_hotkey(action, std::move(id), std::move(defaults), std::move(category));
}

void MainWindow::register_document_widget(QWidget* widget) {
  if (widget == nullptr) {
    return;
  }
  document_widgets_.push_back(widget);
}

void MainWindow::update_document_action_state() {
  const bool has_document = has_active_document();
  const bool locked = preview_dialog_edit_locked();
  for (auto* action : document_actions_) {
    if (action != nullptr) {
      action->setEnabled(has_document && (!locked || document_action_enabled_during_preview_lock(action)));
    }
  }
  for (auto* widget : document_widgets_) {
    if (widget != nullptr) {
      widget->setEnabled(has_document && !locked);
    }
  }
  if (primary_color_button_ != nullptr) {
    primary_color_button_->setEnabled(has_document && !locked);
  }
  if (secondary_color_button_ != nullptr) {
    secondary_color_button_->setEnabled(has_document && !locked);
  }
  if (layer_list_ != nullptr) {
    layer_list_->setEnabled(has_document && !locked);
  }
  const bool quick_mask_view =
      canvas_ != nullptr && canvas_->quick_mask_active();
  const bool smart_filter_mask_view =
      canvas_ != nullptr && canvas_->editing_smart_filter_mask();
  const bool channel_view = canvas_ != nullptr &&
                            (quick_mask_view || smart_filter_mask_view ||
                             canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::DocumentChannel ||
                             canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentRed ||
                             canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentGreen ||
                             canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentBlue);
  const bool writable_channel =
      quick_mask_view || smart_filter_mask_view ||
      (channel_view && canvas_->document_channel_is_editable());
  const auto set_command_enabled = [this, has_document, locked](const QString& id, bool enabled) {
    if (const auto* command = hotkey_registry_.find_command(id); command != nullptr && command->action != nullptr) {
      command->action->setEnabled(has_document && !locked && enabled);
    }
  };
  if (channel_view) {
    for (auto* action : document_actions_) {
      if (action != nullptr && action->property("patchy.channelViewBlocked").toBool()) {
        action->setEnabled(false);
      }
    }
    for (const auto& id : {QStringLiteral("edit.cut"), QStringLiteral("edit.copy"),
                           QStringLiteral("edit.copy_merged"), QStringLiteral("edit.paste"),
                           QStringLiteral("edit.free_transform"), QStringLiteral("edit.warp_transform"),
                           QStringLiteral("layer.flip_horizontal"), QStringLiteral("layer.flip_vertical"),
                           QStringLiteral("tools.move"), QStringLiteral("tools.clone"),
                           QStringLiteral("tools.smudge"), QStringLiteral("tools.type"),
                           QStringLiteral("image.levels"), QStringLiteral("image.curves"),
                           QStringLiteral("image.hue_saturation"), QStringLiteral("image.color_balance")}) {
      set_command_enabled(id, false);
    }
    for (const auto& command : hotkey_registry_.commands()) {
      if (command.id.startsWith(QStringLiteral("patchy.filters.")) && command.action != nullptr) {
        command.action->setEnabled(false);
      }
    }
    set_command_enabled(QStringLiteral("filter.gallery"), false);
    for (const auto& id : {QStringLiteral("layer.fill"), QStringLiteral("layer.fill_background"),
                           QStringLiteral("layer.clear"), QStringLiteral("tools.brush"),
                           QStringLiteral("tools.eraser"), QStringLiteral("tools.gradient"),
                           QStringLiteral("tools.line"), QStringLiteral("tools.rect"),
                           QStringLiteral("tools.ellipse"), QStringLiteral("tools.fill")}) {
      set_command_enabled(id, writable_channel);
    }
    if (quick_mask_view || smart_filter_mask_view) {
      for (auto* action : document_actions_) {
        if (action != nullptr && quick_mask_view &&
            action->property("patchy.quickMaskBlocked").toBool()) {
          action->setEnabled(false);
        }
      }
      for (const auto& id : {
               QStringLiteral("tools.move"),
               QStringLiteral("tools.marquee"),
               QStringLiteral("tools.elliptical_marquee"),
               QStringLiteral("tools.lasso"),
               QStringLiteral("tools.magnetic_lasso"),
               QStringLiteral("tools.magic_wand"),
               QStringLiteral("tools.quick_select"),
               QStringLiteral("select.deselect"),
               QStringLiteral("select.reselect"),
               QStringLiteral("select.grow"),
               QStringLiteral("select.similar"),
               QStringLiteral("select.expand"),
               QStringLiteral("select.contract"),
               QStringLiteral("select.border"),
               QStringLiteral("select.layer_transparency"),
               QStringLiteral("edit.stroke_selection")}) {
        set_command_enabled(id, false);
      }
    }
  }
  const auto* current_session = active_session();
  const bool active_floated = current_session != nullptr && current_session->float_window != nullptr;
  const bool any_floated = any_document_floated();
  const bool any_docked = document_tabs_ != nullptr && document_tabs_->count() > 0;
  if (float_document_action_ != nullptr) {
    float_document_action_->setEnabled(has_document && !locked && !active_floated);
  }
  if (dock_document_action_ != nullptr) {
    dock_document_action_->setEnabled(has_document && !locked && active_floated);
  }
  if (consolidate_tabs_action_ != nullptr) {
    consolidate_tabs_action_->setEnabled(has_document && !locked && any_floated);
  }
  if (float_all_action_ != nullptr) {
    float_all_action_->setEnabled(has_document && !locked && any_docked);
  }
  if (tile_windows_action_ != nullptr) {
    tile_windows_action_->setEnabled(has_document && !locked);
  }
  if (cascade_windows_action_ != nullptr) {
    cascade_windows_action_->setEnabled(has_document && !locked);
  }
  if (paths_panel_ != nullptr) {
    // Keep after the broad document-action pass above, which just blanket-enabled
    // the five path actions: Fill/Stroke/Make Selection/Delete follow the panel's
    // row selection (set_document_available re-runs refresh_action_states).
    paths_panel_->set_document_available(has_document && !locked);
  }
  if (channel_panel_ != nullptr) {
    // Keep these panel-specific rules after the broad document action pass above:
    // component/spot rows and full-capacity documents intentionally disable a
    // subset of the channel actions even though a document is active.
    channel_panel_->set_document_available(has_document && !locked);
    const bool has_capacity = has_document &&
                              std::as_const(document()).channels().size() <
                                  std::as_const(document()).maximum_saved_channel_count();
    channel_panel_->set_channel_creation_available(!locked && has_capacity &&
                                                   !quick_mask_view);
  }
  refresh_convert_for_smart_filters_action_state();
  refresh_options_bar();
}

void MainWindow::refresh_convert_for_smart_filters_action_state() {
  if (filter_convert_smart_filters_action_ == nullptr) {
    return;
  }

  const bool has_document = has_active_document();
  const bool channel_view =
      canvas_ != nullptr &&
      (canvas_->quick_mask_active() || canvas_->editing_smart_filter_mask() ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::DocumentChannel ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentRed ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentGreen ||
       canvas_->layer_edit_target() == CanvasWidget::LayerEditTarget::ComponentBlue);
  const Layer* active_layer = nullptr;
  if (has_document) {
    const auto active_id = std::as_const(document()).active_layer_id();
    if (active_id.has_value()) {
      active_layer = std::as_const(document()).find_layer(*active_id);
    }
  }
  const bool eligible =
      active_layer != nullptr && active_layer->kind() == LayerKind::Pixel &&
      !layer_is_smart_object(*active_layer) &&
      active_layer->pixels().format().bit_depth == BitDepth::UInt8 &&
      active_layer->pixels().format().channels >= 3U;
  filter_convert_smart_filters_action_->setEnabled(
      has_document && !preview_dialog_edit_locked() && !channel_view && eligible);
}

void MainWindow::show_about() {
  show_about_splash(this);
}

}  // namespace patchy::ui
