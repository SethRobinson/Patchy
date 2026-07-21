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

#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;

void ui_blend_if_range_editor_renders_and_edits_split_handles() {
  patchy::ui::BlendIfRangeEditorWidget editor;
  editor.resize(420, 56);
  editor.set_accessibility_text(QStringLiteral("Test Blend If range"),
                                QStringLiteral("Alt-drag splits a joined handle"));
  editor.set_ramp_channel(patchy::BlendIfChannel::Blue);
  editor.set_thresholds(patchy::BlendIfThresholds{40, 40, 210, 210});
  editor.show();
  QApplication::processEvents();
  editor.setFocus(Qt::OtherFocusReason);

  struct Change {
    patchy::BlendIfThresholds value;
    bool immediate{false};
  };
  std::vector<Change> changes;
  editor.changed = [&](patchy::BlendIfThresholds value, bool immediate) {
    changes.push_back(Change{value, immediate});
    editor.set_thresholds(value);
  };

  constexpr int ramp_left = 12;
  const auto x_for_value = [&](int value) {
    return ramp_left + static_cast<int>(std::lround(static_cast<double>(value) / 255.0 *
                                                    static_cast<double>(editor.width() - 26)));
  };
  const QPoint joined_black_handle(x_for_value(40) + 3, 36);
  const QPoint split_black_handle = joined_black_handle + QPoint(60, 0);
  send_mouse(editor, QEvent::MouseButtonPress, joined_black_handle, Qt::LeftButton, Qt::LeftButton,
             Qt::AltModifier);
  send_mouse(editor, QEvent::MouseMove, split_black_handle, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
  send_mouse(editor, QEvent::MouseButtonRelease, split_black_handle, Qt::LeftButton, Qt::NoButton,
             Qt::AltModifier);

  CHECK(editor.value().black_low == 40);
  CHECK(editor.value().black_high > editor.value().black_low);
  CHECK(changes.size() >= 2);
  CHECK(!changes.front().immediate);
  CHECK(changes.back().immediate);

  editor.set_thresholds(patchy::BlendIfThresholds{20, 50, 180, 230});
  changes.clear();
  send_key(editor, Qt::Key_Right, Qt::ShiftModifier);
  CHECK(editor.value().black_high == 60);
  CHECK(!changes.empty() && changes.back().immediate);
  send_key(editor, Qt::Key_PageDown);
  send_key(editor, Qt::Key_Home);
  CHECK(editor.value().white_low == 60);
  const auto changes_at_white_minimum = changes.size();
  send_key(editor, Qt::Key_Left);
  CHECK(editor.value().white_low == 60);
  CHECK(changes.size() == changes_at_white_minimum);
  send_key(editor, Qt::Key_PageDown);
  send_key(editor, Qt::Key_End);
  CHECK(editor.value().white_high == 255);
  send_key(editor, Qt::Key_PageDown);
  send_key(editor, Qt::Key_Home);
  send_key(editor, Qt::Key_Right, Qt::ShiftModifier);
  CHECK(editor.value().black_low == 10);
  const auto before_page_up = editor.value();
  send_key(editor, Qt::Key_PageUp);
  CHECK(editor.value() == before_page_up);
  CHECK(editor.accessibleName() == QStringLiteral("Test Blend If range"));
  CHECK(editor.accessibleDescription().contains(QStringLiteral("Alt-drag")));
  editor.set_thresholds(patchy::BlendIfThresholds{20, 70, 175, 235});
  QApplication::processEvents();
  save_widget_artifact("ui_blend_if_range_editor", editor);
}

void ui_layer_style_blend_if_controls_load_channels_and_map_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Blend If Controls",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  const auto original = blend_if_ui_test_settings();
  layer.set_blend_if_payload(patchy::encode_layer_blend_if(original), true);
  CHECK(layer.blend_if_payload_status() == patchy::BlendIfPayloadStatus::Supported);

  bool inspected = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* group = dialog->findChild<QGroupBox*>(QStringLiteral("layerStyleBlendIfGroup"));
    auto* channel = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBlendIfChannelCombo"));
    auto* reset_channel =
        dialog->findChild<QPushButton*>(QStringLiteral("layerStyleBlendIfResetChannelButton"));
    auto* this_editor = dynamic_cast<patchy::ui::BlendIfRangeEditorWidget*>(
        dialog->findChild<QWidget*>(QStringLiteral("layerStyleBlendIfThisEditor")));
    auto* underlying_editor = dynamic_cast<patchy::ui::BlendIfRangeEditorWidget*>(
        dialog->findChild<QWidget*>(QStringLiteral("layerStyleBlendIfUnderlyingEditor")));
    auto* this_black_low =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackLowSpin"));
    auto* this_black_high =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackHighSpin"));
    auto* this_white_low =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisWhiteLowSpin"));
    auto* this_white_high =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisWhiteHighSpin"));
    auto* underlying_black_low =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfUnderlyingBlackLowSpin"));
    auto* underlying_black_high =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfUnderlyingBlackHighSpin"));
    auto* underlying_white_low =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfUnderlyingWhiteLowSpin"));
    auto* underlying_white_high =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfUnderlyingWhiteHighSpin"));
    CHECK(group != nullptr && group->isEnabled());
    CHECK(channel != nullptr);
    CHECK(reset_channel != nullptr);
    CHECK(this_editor != nullptr);
    CHECK(underlying_editor != nullptr);
    CHECK(this_black_low != nullptr);
    CHECK(this_black_high != nullptr);
    CHECK(this_white_low != nullptr);
    CHECK(this_white_high != nullptr);
    CHECK(underlying_black_low != nullptr);
    CHECK(underlying_black_high != nullptr);
    CHECK(underlying_white_low != nullptr);
    CHECK(underlying_white_high != nullptr);
    CHECK(this_editor->accessibleName() == QStringLiteral("This Layer Blend If range"));
    CHECK(underlying_editor->accessibleName() == QStringLiteral("Underlying Layer Blend If range"));
    CHECK(this_editor->accessibleDescription().contains(QStringLiteral("Page Up or Page Down")));
    CHECK(this_black_low->accessibleName() == QStringLiteral("This Layer black transition start"));
    CHECK(this_black_high->accessibleName() == QStringLiteral("This Layer black transition end"));
    CHECK(this_white_low->accessibleName() == QStringLiteral("This Layer white transition start"));
    CHECK(this_white_high->accessibleName() == QStringLiteral("This Layer white transition end"));
    CHECK(underlying_black_low->accessibleName() ==
          QStringLiteral("Underlying Layer black transition start"));
    CHECK(underlying_black_high->accessibleName() ==
          QStringLiteral("Underlying Layer black transition end"));
    CHECK(underlying_white_low->accessibleName() ==
          QStringLiteral("Underlying Layer white transition start"));
    CHECK(underlying_white_high->accessibleName() ==
          QStringLiteral("Underlying Layer white transition end"));

    CHECK(channel->currentData().toInt() == static_cast<int>(patchy::BlendIfChannel::Gray));
    CHECK(this_editor->ramp_channel() == patchy::BlendIfChannel::Gray);
    CHECK(this_black_low->value() == 10);
    CHECK(this_black_high->value() == 20);
    CHECK(underlying_white_low->value() == 190);

    auto* low_minus = dialog->findChild<QPushButton*>(
        QStringLiteral("layerStyleBlendIfThisBlackLowSpinDecreaseButton"));
    auto* low_plus = dialog->findChild<QPushButton*>(
        QStringLiteral("layerStyleBlendIfThisBlackLowSpinIncreaseButton"));
    auto* high_minus = dialog->findChild<QPushButton*>(
        QStringLiteral("layerStyleBlendIfThisBlackHighSpinDecreaseButton"));
    auto* high_plus = dialog->findChild<QPushButton*>(
        QStringLiteral("layerStyleBlendIfThisBlackHighSpinIncreaseButton"));
    CHECK(low_minus != nullptr && low_plus != nullptr);
    CHECK(high_minus != nullptr && high_plus != nullptr);
    for (auto* button : {low_minus, low_plus, high_minus, high_plus}) {
      CHECK(button->width() >= 20 && button->height() >= 20);
      CHECK(!button->icon().isNull());
    }
    const auto click_step_button = [](QPushButton* button) {
      send_mouse(*button, QEvent::MouseButtonPress, button->rect().center(), Qt::LeftButton,
                 Qt::LeftButton);
      send_mouse(*button, QEvent::MouseButtonRelease, button->rect().center(), Qt::LeftButton,
                 Qt::NoButton);
    };

    // Reproduce the joined-handle case from the UI: the first field's minus
    // moves the left half down, while the second field's plus moves the right
    // half up. The visible controls must not swap either direction.
    this_black_high->setValue(74);
    this_black_low->setValue(74);
    CHECK(low_minus->isEnabled());
    CHECK(!low_plus->isEnabled());
    click_step_button(low_minus);
    CHECK(this_black_low->value() == 73);
    CHECK(low_plus->isEnabled());
    click_step_button(low_plus);
    CHECK(this_black_low->value() == 74);
    CHECK(!high_minus->isEnabled());
    CHECK(high_plus->isEnabled());
    click_step_button(high_plus);
    CHECK(this_black_high->value() == 75);
    CHECK(high_minus->isEnabled());
    click_step_button(high_minus);
    CHECK(this_black_high->value() == 74);
    this_black_low->setValue(10);
    this_black_high->setValue(20);
    auto* white_value_edit = this_white_low->findChild<QLineEdit*>();
    CHECK(white_value_edit != nullptr);
    CHECK(white_value_edit->width() >= 36);
    CHECK(white_value_edit->text() == QStringLiteral("200"));
    save_widget_artifact("ui_layer_style_blend_if_controls", *dialog);

    this_black_high->setValue(25);
    underlying_white_low->setValue(185);

    channel->setCurrentIndex(channel->findData(static_cast<int>(patchy::BlendIfChannel::Red)));
    CHECK(this_editor->ramp_channel() == patchy::BlendIfChannel::Red);
    CHECK(this_black_low->value() == 30);
    CHECK(this_black_high->value() == 40);
    CHECK(underlying_black_low->value() == 32);
    this_black_high->setValue(45);

    channel->setCurrentIndex(channel->findData(static_cast<int>(patchy::BlendIfChannel::Blue)));
    CHECK(this_editor->ramp_channel() == patchy::BlendIfChannel::Blue);
    CHECK(this_black_low->value() == 70);
    CHECK(underlying_black_high->value() == 82);
    CHECK(underlying_white_high->value() == 170);
    underlying_white_high->setValue(175);

    channel->setCurrentIndex(channel->findData(static_cast<int>(patchy::BlendIfChannel::Gray)));
    CHECK(this_black_high->value() == 25);
    CHECK(underlying_white_low->value() == 185);
    inspected = true;
    dialog->accept();
  });

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(inspected);
  CHECK(settings.has_value());
  CHECK((settings->blend_if.channels[0].this_layer == patchy::BlendIfThresholds{10, 25, 200, 240}));
  CHECK((settings->blend_if.channels[0].underlying_layer == patchy::BlendIfThresholds{12, 24, 185, 230}));
  CHECK((settings->blend_if.channels[1].this_layer == patchy::BlendIfThresholds{30, 45, 180, 220}));
  CHECK(settings->blend_if.channels[2] == original.channels[2]);
  CHECK((settings->blend_if.channels[3].underlying_layer == patchy::BlendIfThresholds{72, 82, 130, 175}));
}

void ui_layer_style_blend_if_preview_off_accumulates_and_cancel_restores() {
  patchy::Document direct_document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer direct_layer(direct_document.allocate_layer_id(), "Blend If Preview",
                             solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  const auto original = blend_if_ui_test_settings();
  const auto original_payload = patchy::encode_layer_blend_if(original);
  direct_layer.set_blend_if_payload(original_payload, true);

  std::vector<patchy::ui::LayerStyleSettings> previews;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePreviewCheck"));
    auto* black_high = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackHighSpin"));
    CHECK(preview != nullptr && preview->isChecked());
    CHECK(black_high != nullptr);
    black_high->setValue(28);
    QTest::qWait(80);
    CHECK(!previews.empty());
    CHECK(previews.back().blend_if.channels[0].this_layer.black_high == 28);

    preview->setChecked(false);
    QApplication::processEvents();
    CHECK(previews.back().blend_if == original);
    black_high->setValue(32);
    QTest::qWait(80);
    CHECK(previews.back().blend_if == original);
    dialog->accept();
  });

  const auto accepted = patchy::ui::request_layer_style_settings(
      nullptr, direct_layer,
      [&](const patchy::ui::LayerStyleSettings& settings) { previews.push_back(settings); });
  CHECK(accepted.has_value());
  CHECK(accepted->blend_if.channels[0].this_layer.black_high == 32);
  CHECK(!previews.empty());
  CHECK(previews.back().blend_if == original);

  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Blend If Cancel",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  const auto layer_id = layer.id();
  layer.set_blend_if_payload(original_payload, true);
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Blend If Cancel"));
  show_window(window);
  bool saw_live_preview = false;
  QTimer::singleShot(0, &window, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* black_high = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackHighSpin"));
    CHECK(black_high != nullptr);
    black_high->setValue(35);
    QTest::qWait(80);
    const auto* preview_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
    CHECK(preview_layer != nullptr);
    saw_live_preview = preview_layer->blend_if().channels[0].this_layer.black_high == 35;
    dialog->reject();
  });
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_live_preview);
  const auto* restored = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(restored != nullptr);
  CHECK(restored->raw_psd_blending_ranges() == original_payload);
  CHECK(restored->blend_if() == original);
}

void ui_layer_style_blend_if_unsupported_requires_explicit_replace() {
  constexpr std::array<std::uint8_t, 8> unsupported_payload{0, 0, 255, 255, 0, 0, 255, 255};
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Unsupported Blend If",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  const auto layer_id = layer.id();
  layer.set_blend_if_payload(std::vector<std::uint8_t>(unsupported_payload.begin(), unsupported_payload.end()), true);
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Unsupported Blend If"));
  show_window(window);
  auto* blending_options = require_action(window, "layerBlendingOptionsAction");
  const auto run_style_dialog = [&](const std::function<void(QDialog&)>& interaction) {
    bool handled = false;
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
      CHECK(dialog != nullptr);
      interaction(*dialog);
      handled = true;
    });
    blending_options->trigger();
    QApplication::processEvents();
    CHECK(handled);
  };

  run_style_dialog([](QDialog& dialog) {
    auto* warning = dialog.findChild<QLabel*>(QStringLiteral("layerStyleBlendIfUnsupportedWarning"));
    auto* group = dialog.findChild<QGroupBox*>(QStringLiteral("layerStyleBlendIfGroup"));
    auto* replace = dialog.findChild<QPushButton*>(QStringLiteral("layerStyleBlendIfReplaceButton"));
    CHECK(warning != nullptr && warning->isVisible());
    CHECK(warning->text().contains(QStringLiteral("unsupported color mode or payload shape")));
    CHECK(group != nullptr && !group->isEnabled());
    CHECK(replace != nullptr && replace->isEnabled());
    dialog.accept();
  });
  const auto* preserved_without_replace =
      patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(preserved_without_replace != nullptr);
  CHECK(preserved_without_replace->raw_psd_blending_ranges() ==
        std::vector<std::uint8_t>(unsupported_payload.begin(), unsupported_payload.end()));

  run_style_dialog([](QDialog& dialog) {
    auto* warning = dialog.findChild<QLabel*>(QStringLiteral("layerStyleBlendIfUnsupportedWarning"));
    auto* group = dialog.findChild<QGroupBox*>(QStringLiteral("layerStyleBlendIfGroup"));
    auto* replace = dialog.findChild<QPushButton*>(QStringLiteral("layerStyleBlendIfReplaceButton"));
    auto* black_high = dialog.findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackHighSpin"));
    auto* black_low = dialog.findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackLowSpin"));
    CHECK(warning != nullptr);
    CHECK(group != nullptr);
    CHECK(replace != nullptr);
    CHECK(black_high != nullptr);
    CHECK(black_low != nullptr);
    replace->click();
    QApplication::processEvents();
    CHECK(group->isEnabled());
    CHECK(!replace->isEnabled());
    CHECK(warning->text().contains(QStringLiteral("will be replaced with editable RGB defaults")));
    black_high->setValue(35);
    black_low->setValue(15);
    dialog.reject();
  });
  const auto* preserved_after_cancel = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(preserved_after_cancel != nullptr);
  CHECK(preserved_after_cancel->raw_psd_blending_ranges() ==
        std::vector<std::uint8_t>(unsupported_payload.begin(), unsupported_payload.end()));

  run_style_dialog([](QDialog& dialog) {
    auto* group = dialog.findChild<QGroupBox*>(QStringLiteral("layerStyleBlendIfGroup"));
    auto* replace = dialog.findChild<QPushButton*>(QStringLiteral("layerStyleBlendIfReplaceButton"));
    auto* black_high = dialog.findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackHighSpin"));
    auto* black_low = dialog.findChild<QSpinBox*>(QStringLiteral("layerStyleBlendIfThisBlackLowSpin"));
    CHECK(group != nullptr);
    CHECK(replace != nullptr);
    CHECK(black_high != nullptr);
    CHECK(black_low != nullptr);
    replace->click();
    QApplication::processEvents();
    CHECK(group->isEnabled());
    black_high->setValue(35);
    black_low->setValue(15);
    dialog.accept();
  });
  const auto* replaced = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(replaced != nullptr);
  CHECK(replaced->blend_if_payload_status() == patchy::BlendIfPayloadStatus::Supported);
  CHECK(replaced->raw_psd_blending_ranges().size() == 40);
  CHECK((replaced->blend_if().channels[0].this_layer == patchy::BlendIfThresholds{15, 35, 255, 255}));

  patchy::Document non_rgb_document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer non_rgb_layer(non_rgb_document.allocate_layer_id(), "Non-RGB Blend If",
                              solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  const auto non_rgb_payload = patchy::encode_layer_blend_if(blend_if_ui_test_settings());
  non_rgb_layer.set_blend_if_payload(non_rgb_payload, false);
  CHECK(non_rgb_layer.blend_if_payload_status() == patchy::BlendIfPayloadStatus::Unsupported);
  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* warning = dialog->findChild<QLabel*>(QStringLiteral("layerStyleBlendIfUnsupportedWarning"));
    auto* group = dialog->findChild<QGroupBox*>(QStringLiteral("layerStyleBlendIfGroup"));
    CHECK(warning != nullptr && warning->isVisible());
    CHECK(group != nullptr && !group->isEnabled());
    dialog->reject();
  });
  const auto non_rgb_result = patchy::ui::request_layer_style_settings(nullptr, non_rgb_layer, {});
  CHECK(!non_rgb_result.has_value());
  CHECK(non_rgb_layer.raw_psd_blending_ranges() == non_rgb_payload);
  CHECK(!non_rgb_layer.blend_if_rgb_compatible());
}

void ui_layer_style_dialog_coalesces_rapid_slider_preview_callbacks() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Coalesced Style",
                      solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.opacity = 0.6F;
  shadow.distance = 6.0F;
  shadow.size = 4.0F;
  layer.layer_style().drop_shadows.push_back(shadow);

  int preview_calls = 0;
  int latest_preview_opacity = -1;
  int latest_preview_shadow_distance = -1;
  bool queued_slow_changes = false;

  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOpacitySpin"));
    auto* opacity_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOpacitySlider"));
    auto* shadow_distance = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowDistanceSpin"));
    auto* shadow_distance_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleDropShadowDistanceSlider"));
    CHECK(categories != nullptr);
    CHECK(opacity != nullptr);
    CHECK(opacity_slider != nullptr);
    CHECK(shadow_distance != nullptr);
    CHECK(shadow_distance_slider != nullptr);
    const auto shadow_items = categories->findItems(QStringLiteral("Drop Shadow"), Qt::MatchExactly);
    CHECK(!shadow_items.empty());
    categories->setCurrentItem(shadow_items.front());
    for (int value = 1; value <= 24; ++value) {
      opacity_slider->setValue(value);
      shadow_distance_slider->setValue(value);
      CHECK(opacity->value() == value);
      CHECK(shadow_distance->value() == value);
    }
    QTimer::singleShot(360, dialog, [dialog] { dialog->accept(); });
  });

  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, [&](const patchy::ui::LayerStyleSettings& preview) {
        ++preview_calls;
        latest_preview_opacity = preview.opacity;
        latest_preview_shadow_distance =
            preview.style.drop_shadows.empty()
                ? -1
                : static_cast<int>(std::lround(preview.style.drop_shadows.front().distance));
        if (!queued_slow_changes && latest_preview_opacity == 24 && latest_preview_shadow_distance == 24) {
          queued_slow_changes = true;
          QTimer::singleShot(0, [] {
            auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
            CHECK(dialog != nullptr);
            auto* opacity_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOpacitySlider"));
            auto* shadow_distance_slider =
                dialog->findChild<QSlider*>(QStringLiteral("layerStyleDropShadowDistanceSlider"));
            CHECK(opacity_slider != nullptr);
            CHECK(shadow_distance_slider != nullptr);
            for (int value = 25; value <= 48; ++value) {
              opacity_slider->setValue(value);
              shadow_distance_slider->setValue(value + 6);
            }
          });
          QElapsedTimer slow_preview;
          slow_preview.start();
          while (slow_preview.elapsed() < 45) {
          }
        }
      });

  CHECK(settings.has_value());
  CHECK(settings->opacity == 48);
  CHECK(!settings->style.drop_shadows.empty());
  CHECK(static_cast<int>(std::lround(settings->style.drop_shadows.front().distance)) == 54);
  CHECK(queued_slow_changes);
  CHECK(preview_calls >= 2);
  CHECK(preview_calls < 10);
  CHECK(latest_preview_opacity == 48);
  CHECK(latest_preview_shadow_distance == 54);
}

void ui_layer_style_opacity_slider_does_not_block_on_slow_preview_render() {
  patchy::Document document(220, 160, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(220, 160, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer layer(document.allocate_layer_id(), "Slow Style Preview",
                      solid_pixels(120, 90, patchy::PixelFormat::rgba8(), QColor(60, 120, 220, 255)));
  const auto layer_id = layer.id();
  layer.set_bounds(patchy::Rect{48, 38, 120, 90});
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("Slow Style Preview"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->force_refresh();
  QApplication::processEvents();

  EnvironmentVariableRestorer restore_delay("PATCHY_PROCESSING_OVERLAY_DELAY_MS");
  EnvironmentVariableRestorer restore_min_pixels("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS");
  EnvironmentVariableRestorer restore_render_delay("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS");
  qputenv("PATCHY_PROCESSING_OVERLAY_DELAY_MS", QByteArray("0"));
  qputenv("PATCHY_PROCESSING_OVERLAY_MIN_PIXELS", QByteArray("0"));
  qputenv("PATCHY_PROCESSING_RENDER_TEST_DELAY_MS", QByteArray("520"));

  bool exercised_slider = false;
  qint64 elapsed_ms = 0;
  // Keep the loop far below synchronous per-change rendering, while allowing
  // full-suite timer jitter around the synthetic 520 ms render delay below.
  constexpr qint64 kResponsiveSliderLoopBudgetMs = 1200;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOpacitySpin"));
    auto* opacity_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOpacitySlider"));
    CHECK(opacity != nullptr);
    CHECK(opacity_slider != nullptr);
    QElapsedTimer timer;
    timer.start();
    for (int value = 99; value >= 52; --value) {
      opacity_slider->setValue(value);
      CHECK(opacity->value() == value);
      QApplication::processEvents(QEventLoop::AllEvents, 1);
    }
    elapsed_ms = timer.elapsed();
    exercised_slider = true;
    CHECK(elapsed_ms < kResponsiveSliderLoopBudgetMs);
    QTimer::singleShot(120, dialog, [dialog] { dialog->accept(); });
  });

  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(exercised_slider);
  CHECK(elapsed_ms < kResponsiveSliderLoopBudgetMs);
  const auto* edited_layer = patchy::ui::MainWindowTestAccess::document(window).find_layer(layer_id);
  CHECK(edited_layer != nullptr);
  CHECK(std::abs(edited_layer->opacity() - 0.52F) <= 0.001F);
}

void ui_layer_style_dialog_does_not_open_a_second_dialog() {
  // Opening Blending Options while one is already open (e.g. by double-clicking
  // another layer) must NOT stack a second dialog -- the nested event loops
  // crash. edit_active_layer_style() guards on preview_dialog_edit_locked().
  patchy::Document document(160, 120, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
  patchy::Layer styled(document.allocate_layer_id(), "Styled Layer",
                       solid_pixels(80, 60, patchy::PixelFormat::rgba8(), QColor(60, 120, 220, 255)));
  const auto styled_id = styled.id();
  document.add_layer(std::move(styled));
  document.set_active_layer(styled_id);

  patchy::ui::MainWindow window;
  window.add_document_session(std::move(document), QStringLiteral("No Stack"));
  show_window(window);
  auto* canvas = require_canvas(window);
  canvas->force_refresh();
  QApplication::processEvents();

  const auto count_style_dialogs = [] {
    int count = 0;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (auto* dialog = qobject_cast<QDialog*>(widget);
          dialog != nullptr && dialog->objectName() == QStringLiteral("patchyLayerStyleDialog") && dialog->isVisible()) {
        ++count;
      }
    }
    return count;
  };

  // Watchdog: if a second (buggy) nested dialog opens, the inner trigger() blocks
  // forever. Force every style dialog closed so the test fails instead of hanging.
  bool watchdog_fired = false;
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, &window, [&] {
    watchdog_fired = true;
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (auto* dialog = qobject_cast<QDialog*>(widget);
          dialog != nullptr && dialog->objectName() == QStringLiteral("patchyLayerStyleDialog")) {
        dialog->reject();
      }
    }
  });

  bool reentered = false;
  int dialogs_after_second_trigger = 0;
  QTimer::singleShot(0, &window, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    if (dialog == nullptr) {
      return;
    }
    watchdog.start(2000);
    // Try to open another one. With the guard this returns immediately; without
    // it, this blocks in a second nested loop until the watchdog rescues us.
    require_action(window, "layerBlendingOptionsAction")->trigger();
    dialogs_after_second_trigger = count_style_dialogs();
    reentered = true;
    watchdog.stop();
    dialog->accept();
  });

  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();

  CHECK(reentered);
  CHECK(!watchdog_fired);
  CHECK(dialogs_after_second_trigger == 1);
}

patchy::Layer make_gradient_fill_test_layer(patchy::Document& document) {
  patchy::Layer layer(document.allocate_layer_id(), "Gradient Styled",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerGradientFill fill;
  fill.enabled = true;
  fill.gradient.color_stops.push_back(patchy::GradientColorStop{0.0F, patchy::RgbColor{255, 255, 255}});
  fill.gradient.color_stops.push_back(patchy::GradientColorStop{1.0F, patchy::RgbColor{0, 0, 0}});
  fill.gradient.alpha_stops.push_back(patchy::GradientAlphaStop{0.0F, 1.0F});
  fill.gradient.alpha_stops.push_back(patchy::GradientAlphaStop{1.0F, 1.0F});
  layer.layer_style().gradient_fills.push_back(fill);
  return layer;
}

void select_layer_style_gradient_page(QDialog& dialog) {
  auto* categories = dialog.findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
  CHECK(categories != nullptr);
  const auto gradient_items = categories->findItems(QStringLiteral("Gradient Overlay"), Qt::MatchExactly);
  CHECK(!gradient_items.empty());
  categories->setCurrentItem(gradient_items.front());
  QApplication::processEvents();
}

void ui_layer_style_gradient_editor_drag_updates_stops_and_previews() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  auto layer = make_gradient_fill_test_layer(document);

  int preview_calls = 0;
  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    select_layer_style_gradient_page(*dialog);
    auto* editor =
        dialog->findChild<patchy::ui::GradientStopsEditorWidget*>(QStringLiteral("layerStyleGradientStopsEditor"));
    auto* location = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientStopLocationSpin"));
    CHECK(editor != nullptr);
    CHECK(location != nullptr);
    CHECK(editor->isVisible());
    CHECK(location->value() == 0);
    // The color tags live below the bar (two-track geometry: bar 30..60, color
    // tags around y 66..89); the 0% tag sits at the left gutter x=10.
    const int tag_y = editor->height() - 18;
    const int bar_span = editor->width() - 22;
    const QPoint from(10, tag_y);
    const QPoint to(10 + (bar_span * 2) / 5, tag_y);
    drag(*editor, from, to);
    QApplication::processEvents();
    CHECK(location->value() == 40);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  });

  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, [&](const patchy::ui::LayerStyleSettings&) { ++preview_calls; });

  CHECK(settings.has_value());
  CHECK(settings->style.gradient_fills.size() == 1);
  const auto& stops = settings->style.gradient_fills.front().gradient.color_stops;
  CHECK(stops.size() == 2);
  CHECK(std::abs(stops.front().location - 0.40F) < 0.005F);
  CHECK(stops.back().location > 0.99F);
  CHECK(preview_calls >= 1);
}

void ui_layer_style_gradient_opacity_track_adds_and_edits_alpha_stops() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  auto layer = make_gradient_fill_test_layer(document);

  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    select_layer_style_gradient_page(*dialog);
    auto* editor =
        dialog->findChild<patchy::ui::GradientStopsEditorWidget*>(QStringLiteral("layerStyleGradientStopsEditor"));
    auto* location = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientStopLocationSpin"));
    auto* stop_opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientStopOpacitySpin"));
    auto* stop_hex = dialog->findChild<QLineEdit*>(QStringLiteral("layerStyleGradientStopHexEdit"));
    CHECK(editor != nullptr);
    CHECK(location != nullptr);
    CHECK(stop_opacity != nullptr);
    CHECK(stop_hex != nullptr);
    CHECK(stop_hex->isVisible());
    CHECK(!stop_opacity->isVisible());
    // Click the empty opacity track above the bar at 50% to add an opacity stop.
    const int bar_span = editor->width() - 22;
    const QPoint add_point(10 + bar_span / 2, 8);
    send_mouse(*editor, QEvent::MouseButtonPress, add_point, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseButtonRelease, add_point, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(stop_opacity->isVisible());
    CHECK(!stop_hex->isVisible());
    CHECK(location->value() == 50);
    CHECK(stop_opacity->value() == 100);
    stop_opacity->setValue(0);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  });

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});

  CHECK(settings.has_value());
  CHECK(settings->style.gradient_fills.size() == 1);
  const auto& alpha_stops = settings->style.gradient_fills.front().gradient.alpha_stops;
  CHECK(alpha_stops.size() == 3);
  CHECK(std::abs(alpha_stops[1].location - 0.5F) < 0.01F);
  CHECK(alpha_stops[1].opacity == 0.0F);
  CHECK(alpha_stops.front().opacity == 1.0F);
  CHECK(alpha_stops.back().opacity == 1.0F);
}

void ui_layer_style_gradient_midpoints_edit_color_and_opacity_tracks() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  auto layer = make_gradient_fill_test_layer(document);
  auto& gradient = layer.layer_style().gradient_fills.front().gradient;
  gradient.color_stops.back().midpoint = 0.25F;
  gradient.alpha_stops.back().midpoint = 0.75F;

  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    select_layer_style_gradient_page(*dialog);
    auto* editor =
        dialog->findChild<patchy::ui::GradientStopsEditorWidget*>(QStringLiteral("layerStyleGradientStopsEditor"));
    auto* location = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientStopLocationSpin"));
    auto* midpoint = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientStopMidpointSpin"));
    CHECK(editor != nullptr);
    CHECK(location != nullptr);
    CHECK(midpoint != nullptr);
    CHECK(!midpoint->isEnabled());  // first sorted stop has no incoming segment

    const QPoint color_tag(editor->width() - 10, editor->height() - 18);
    send_mouse(*editor, QEvent::MouseButtonPress, color_tag, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseButtonRelease, color_tag, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(midpoint->isEnabled());
    CHECK(midpoint->value() == 25);
    midpoint->setValue(72);
    location->setValue(0);
    QApplication::processEvents();
    CHECK(!midpoint->isEnabled());  // an equal-position hard stop has no segment midpoint
    location->setValue(100);
    QApplication::processEvents();
    CHECK(midpoint->isEnabled());
    CHECK(midpoint->value() == 72);

    const QPoint opacity_tag(editor->width() - 10, 8);
    send_mouse(*editor, QEvent::MouseButtonPress, opacity_tag, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseButtonRelease, opacity_tag, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(midpoint->isEnabled());
    CHECK(midpoint->value() == 75);
    midpoint->setValue(33);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  });

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  const auto& result = settings->style.gradient_fills.front().gradient;
  CHECK(std::abs(result.color_stops.back().midpoint - 0.72F) < 0.001F);
  CHECK(std::abs(result.alpha_stops.back().midpoint - 0.33F) < 0.001F);
}

void ui_layer_style_gradient_page_controls_map_to_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  auto layer = make_gradient_fill_test_layer(document);

  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    select_layer_style_gradient_page(*dialog);
    auto* blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleGradientBlendModeCombo"));
    auto* reverse = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleGradientReverseCheck"));
    auto* style_combo = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleGradientStyleCombo"));
    auto* stop_hex = dialog->findChild<QLineEdit*>(QStringLiteral("layerStyleGradientStopHexEdit"));
    CHECK(blend != nullptr);
    CHECK(reverse != nullptr);
    CHECK(style_combo != nullptr);
    CHECK(stop_hex != nullptr);
    CHECK(!reverse->isChecked());
    blend->setCurrentIndex(std::max(0, blend->findData(static_cast<int>(patchy::BlendMode::Multiply))));
    reverse->setChecked(true);
    style_combo->setCurrentIndex(
        std::max(0, style_combo->findData(static_cast<int>(patchy::LayerStyleGradientType::Radial))));
    stop_hex->setText(QStringLiteral("00FF00"));
    send_key(*stop_hex, Qt::Key_Return);
    CHECK(stop_hex->text() == QStringLiteral("#00FF00"));
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  });

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});

  CHECK(settings.has_value());
  CHECK(settings->style.gradient_fills.size() == 1);
  const auto& fill = settings->style.gradient_fills.front();
  CHECK(fill.blend_mode == patchy::BlendMode::Multiply);
  CHECK(fill.gradient.reverse);
  CHECK(fill.gradient.type == patchy::LayerStyleGradientType::Radial);
  CHECK(fill.gradient.color_stops.front().color.red == 0);
  CHECK(fill.gradient.color_stops.front().color.green == 255);
  CHECK(fill.gradient.color_stops.front().color.blue == 0);
}

void ui_layer_style_preview_is_transient_and_show_effects_persists() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Preview State",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.size = 5.0F;
  layer.layer_style().strokes.push_back(stroke);
  layer.layer_style().effects_visible = false;

  std::vector<patchy::ui::LayerStyleSettings> previews;
  QTimer::singleShot(0, [&previews] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* show_effects = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleShowEffectsCheck"));
    auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePreviewCheck"));
    auto* stroke_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeSizeSpin"));
    CHECK(categories != nullptr);
    CHECK(show_effects != nullptr);
    CHECK(preview != nullptr);
    CHECK(stroke_size != nullptr);
    CHECK(!show_effects->isChecked());
    CHECK(preview->isChecked());

    show_effects->setChecked(true);
    const auto stroke_items = categories->findItems(QStringLiteral("Stroke"), Qt::MatchExactly);
    CHECK(!stroke_items.empty());
    categories->setCurrentItem(stroke_items.front());
    stroke_size->setValue(17);
    preview->setChecked(false);
    QApplication::processEvents();
    CHECK(preview->isVisible());
    CHECK(!previews.empty());
    CHECK(!previews.back().style.effects_visible);
    CHECK(previews.back().style.strokes.front().size == 5.0F);
    // Edits continue accumulating while the canvas is showing the untouched
    // original state.
    stroke_size->setValue(19);
    QTest::qWait(60);
    CHECK(!previews.back().style.effects_visible);
    CHECK(previews.back().style.strokes.front().size == 5.0F);
    preview->setChecked(true);
    QApplication::processEvents();
    CHECK(previews.back().style.effects_visible);
    CHECK(previews.back().style.strokes.front().size == 19.0F);
    preview->setChecked(false);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  });

  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, [&](const patchy::ui::LayerStyleSettings& value) { previews.push_back(value); });

  CHECK(settings.has_value());
  CHECK(settings->style.effects_visible);
  CHECK(settings->style.strokes.size() == 1);
  CHECK(settings->style.strokes.front().size == 19.0F);
  CHECK(!previews.empty());
  CHECK(!previews.back().style.effects_visible);
  CHECK(previews.back().style.strokes.size() == 1);
  CHECK(previews.back().style.strokes.front().size == 5.0F);
}

void ui_layer_style_gradient_stroke_controls_map_to_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Gradient Stroke",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerStroke stroke;
  stroke.enabled = true;
  stroke.uses_gradient = true;
  stroke.gradient.color_stops = {{0.0F, patchy::RgbColor{255, 255, 255}},
                                 {1.0F, patchy::RgbColor{0, 0, 0}}};
  stroke.gradient.alpha_stops = {{0.0F, 1.0F}, {1.0F, 1.0F}};
  layer.layer_style().strokes.push_back(stroke);

  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* fill = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleStrokeFillCombo"));
    auto* gradient_group = dialog->findChild<QGroupBox*>(QStringLiteral("layerStyleStrokeGradientGroup"));
    auto* editor = dialog->findChild<patchy::ui::GradientStopsEditorWidget*>(
        QStringLiteral("layerStyleStrokeGradientStopsEditor"));
    auto* reverse = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleStrokeGradientReverseCheck"));
    auto* style_combo = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleStrokeGradientStyleCombo"));
    auto* angle = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeGradientAngleSpin"));
    auto* scale = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeGradientScaleSpin"));
    auto* stop_hex = dialog->findChild<QLineEdit*>(QStringLiteral("layerStyleStrokeGradientStopHexEdit"));
    auto* midpoint = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeGradientStopMidpointSpin"));
    CHECK(categories != nullptr);
    CHECK(fill != nullptr);
    CHECK(gradient_group != nullptr);
    CHECK(editor != nullptr);
    CHECK(reverse != nullptr);
    CHECK(style_combo != nullptr);
    CHECK(angle != nullptr);
    CHECK(scale != nullptr);
    CHECK(stop_hex != nullptr);
    CHECK(midpoint != nullptr);
    const auto stroke_items = categories->findItems(QStringLiteral("Stroke"), Qt::MatchExactly);
    CHECK(!stroke_items.empty());
    categories->setCurrentItem(stroke_items.front());
    QApplication::processEvents();
    CHECK(fill->currentData().toBool());
    CHECK(gradient_group->isVisible());
    CHECK(editor->isVisible());
    CHECK(!editor->visibleRegion().isEmpty());
    CHECK(!scale->visibleRegion().isEmpty());
    CHECK(!midpoint->isEnabled());
    fill->setCurrentIndex(0);
    CHECK(!gradient_group->isVisible());
    fill->setCurrentIndex(1);
    CHECK(gradient_group->isVisible());
    reverse->setChecked(true);
    style_combo->setCurrentIndex(
        std::max(0, style_combo->findData(static_cast<int>(patchy::LayerStyleGradientType::Diamond))));
    angle->setValue(-35);
    scale->setValue(175);
    stop_hex->setText(QStringLiteral("#123456"));
    send_key(*stop_hex, Qt::Key_Return);
    const QPoint color_tag(editor->width() - 10, editor->height() - 18);
    send_mouse(*editor, QEvent::MouseButtonPress, color_tag, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseButtonRelease, color_tag, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(midpoint->isEnabled());
    midpoint->setValue(64);
    const QPoint opacity_tag(editor->width() - 10, 8);
    send_mouse(*editor, QEvent::MouseButtonPress, opacity_tag, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*editor, QEvent::MouseButtonRelease, opacity_tag, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    CHECK(midpoint->isEnabled());
    midpoint->setValue(36);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  });

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  CHECK(settings->style.strokes.size() == 1);
  const auto& result = settings->style.strokes.front();
  CHECK(result.uses_gradient);
  CHECK(result.gradient.reverse);
  CHECK(result.gradient.type == patchy::LayerStyleGradientType::Diamond);
  CHECK(result.gradient.angle_degrees == -35.0F);
  CHECK(std::abs(result.gradient.scale - 1.75F) < 0.001F);
  CHECK(result.gradient.color_stops.front().color.red == 0x12);
  CHECK(result.gradient.color_stops.front().color.green == 0x34);
  CHECK(result.gradient.color_stops.front().color.blue == 0x56);
  CHECK(std::abs(result.gradient.color_stops.back().midpoint - 0.64F) < 0.001F);
  CHECK(std::abs(result.gradient.alpha_stops.back().midpoint - 0.36F) < 0.001F);
}

void ui_layer_style_bevel_lighting_controls_map_to_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Bevel Lighting",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode = patchy::BlendMode::Screen;
  bevel.highlight_color = patchy::RgbColor{240, 230, 220};
  bevel.shadow_blend_mode = patchy::BlendMode::Multiply;
  bevel.shadow_color = patchy::RgbColor{20, 30, 40};
  bevel.style = patchy::BevelEmbossStyleKind::OuterBevel;
  bevel.technique = patchy::BevelTechnique::ChiselHard;
  bevel.soften = 3.0F;
  layer.layer_style().bevels.push_back(bevel);

  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* highlight_blend =
        dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBevelHighlightBlendModeCombo"));
    auto* shadow_blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBevelShadowBlendModeCombo"));
    auto* highlight_opacity =
        dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelHighlightOpacitySpin"));
    auto* shadow_opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelShadowOpacitySpin"));
    auto* style = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBevelStyleCombo"));
    auto* technique = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBevelTechniqueCombo"));
    auto* soften = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelSoftenSpin"));
    auto* highlight_color =
        dialog->findChild<QPushButton*>(QStringLiteral("layerStyleBevelHighlightColorButton"));
    auto* shadow_color = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleBevelShadowColorButton"));
    CHECK(categories != nullptr);
    CHECK(highlight_blend != nullptr);
    CHECK(shadow_blend != nullptr);
    CHECK(highlight_opacity != nullptr);
    CHECK(shadow_opacity != nullptr);
    CHECK(style != nullptr);
    CHECK(technique != nullptr);
    CHECK(soften != nullptr);
    CHECK(highlight_color != nullptr);
    CHECK(shadow_color != nullptr);
    const auto bevel_items = categories->findItems(QStringLiteral("Bevel & Emboss"), Qt::MatchExactly);
    CHECK(!bevel_items.empty());
    categories->setCurrentItem(bevel_items.front());
    QApplication::processEvents();
    CHECK(style->count() == 5);
    CHECK(technique->count() == 3);
    CHECK(style->currentData().toInt() == static_cast<int>(patchy::BevelEmbossStyleKind::OuterBevel));
    CHECK(technique->currentData().toInt() == static_cast<int>(patchy::BevelTechnique::ChiselHard));
    CHECK(soften->value() == 3);
    CHECK(!highlight_color->visibleRegion().isEmpty());
    CHECK(!shadow_color->visibleRegion().isEmpty());
    highlight_blend->setCurrentIndex(
        std::max(0, highlight_blend->findData(static_cast<int>(patchy::BlendMode::ColorDodge))));
    shadow_blend->setCurrentIndex(
        std::max(0, shadow_blend->findData(static_cast<int>(patchy::BlendMode::LinearBurn))));
    highlight_opacity->setValue(62);
    shadow_opacity->setValue(37);
    style->setCurrentIndex(
        std::max(0, style->findData(static_cast<int>(patchy::BevelEmbossStyleKind::PillowEmboss))));
    technique->setCurrentIndex(
        std::max(0, technique->findData(static_cast<int>(patchy::BevelTechnique::ChiselSoft))));
    soften->setValue(5);

    const auto choose_color = [](QPushButton* button, QColor color) {
      QTimer::singleShot(0, [color] {
        auto* picker_dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyColorDialog")));
        CHECK(picker_dialog != nullptr);
        auto* picker = picker_dialog->findChild<patchy::ui::PatchyColorPicker*>(
            QStringLiteral("patchyAdvancedColorPicker"));
        CHECK(picker != nullptr);
        picker->setCurrentColor(color);
        picker_dialog->accept();
      });
      button->click();
    };
    choose_color(highlight_color, QColor(12, 34, 56));
    choose_color(shadow_color, QColor(78, 90, 123));
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  });

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  CHECK(settings->style.bevels.size() == 1);
  const auto& result = settings->style.bevels.front();
  CHECK(result.highlight_blend_mode == patchy::BlendMode::ColorDodge);
  CHECK(result.highlight_color.red == 12 && result.highlight_color.green == 34 && result.highlight_color.blue == 56);
  CHECK(std::abs(result.highlight_opacity - 0.62F) < 0.001F);
  CHECK(result.shadow_blend_mode == patchy::BlendMode::LinearBurn);
  CHECK(result.shadow_color.red == 78 && result.shadow_color.green == 90 && result.shadow_color.blue == 123);
  CHECK(std::abs(result.shadow_opacity - 0.37F) < 0.001F);
  CHECK(result.style == patchy::BevelEmbossStyleKind::PillowEmboss);
  CHECK(result.technique == patchy::BevelTechnique::ChiselSoft);
  CHECK(result.soften == 5.0F);
}

void ui_layer_style_satin_controls_map_to_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Satin Styled",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerSatin satin;
  satin.enabled = true;
  satin.unsupported_contour_options = true;
  layer.layer_style().satins.push_back(satin);

  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleSatinBlendModeCombo"));
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleSatinOpacitySpin"));
    auto* angle = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleSatinAngleSpin"));
    auto* distance = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleSatinDistanceSpin"));
    auto* size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleSatinSizeSpin"));
    auto* invert = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleSatinInvertCheck"));
    auto* red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleSatinRedSpin"));
    auto* green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleSatinGreenSpin"));
    auto* blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleSatinBlueSpin"));
    auto* contour_warning = dialog->findChild<QLabel*>(QStringLiteral("layerStyleSatinContourWarning"));
    CHECK(categories != nullptr);
    CHECK(blend != nullptr);
    CHECK(opacity != nullptr);
    CHECK(angle != nullptr);
    CHECK(distance != nullptr);
    CHECK(size != nullptr);
    CHECK(invert != nullptr);
    CHECK(red != nullptr);
    CHECK(green != nullptr);
    CHECK(blue != nullptr);
    CHECK(contour_warning != nullptr);
    CHECK(contour_warning->isVisible());
    CHECK(contour_warning->text().contains(QStringLiteral("non-anti-aliased Linear contour")));
    const auto satin_items = categories->findItems(QStringLiteral("Satin"), Qt::MatchExactly);
    CHECK(!satin_items.empty());
    categories->setCurrentItem(satin_items.front());
    QApplication::processEvents();
    CHECK(!opacity->visibleRegion().isEmpty());
    blend->setCurrentIndex(std::max(0, blend->findData(static_cast<int>(patchy::BlendMode::Screen))));
    opacity->setValue(63);
    angle->setValue(-42);
    distance->setValue(27);
    size->setValue(19);
    invert->setChecked(false);
    red->setValue(12);
    green->setValue(34);
    blue->setValue(56);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  });

  int previews = 0;
  const auto settings = patchy::ui::request_layer_style_settings(
      nullptr, layer, [&](const patchy::ui::LayerStyleSettings&) { ++previews; });
  CHECK(settings.has_value());
  CHECK(settings->style.satins.size() == 1);
  const auto& result = settings->style.satins.front();
  CHECK(result.enabled);
  CHECK(result.blend_mode == patchy::BlendMode::Screen);
  CHECK(std::abs(result.opacity - 0.63F) < 0.001F);
  CHECK(result.angle_degrees == -42.0F);
  CHECK(result.distance == 27.0F);
  CHECK(result.size == 19.0F);
  CHECK(!result.invert);
  CHECK(result.color.red == 12 && result.color.green == 34 && result.color.blue == 56);
  CHECK(!result.unsupported_contour_options);
  CHECK(previews >= 1);
}

void ui_layer_style_outer_glow_technique_and_range_map_to_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Glow Styled",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerOuterGlow glow;
  glow.enabled = true;
  glow.technique = patchy::LayerGlowTechnique::Precise;
  glow.range = 80.0F;
  layer.layer_style().outer_glows.push_back(glow);

  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* technique = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleOuterGlowTechniqueCombo"));
    auto* range = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowRangeSpin"));
    auto* spread = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowSpreadSpin"));
    CHECK(categories != nullptr);
    CHECK(technique != nullptr);
    CHECK(range != nullptr);
    CHECK(spread != nullptr);
    // The imported values load into the controls.
    CHECK(technique->currentData().toInt() == static_cast<int>(patchy::LayerGlowTechnique::Precise));
    CHECK(range->value() == 80);
    const auto glow_items = categories->findItems(QStringLiteral("Outer Glow"), Qt::MatchExactly);
    CHECK(!glow_items.empty());
    categories->setCurrentItem(glow_items.front());
    QApplication::processEvents();
    CHECK(!range->visibleRegion().isEmpty());
    technique->setCurrentIndex(
        std::max(0, technique->findData(static_cast<int>(patchy::LayerGlowTechnique::Softer))));
    range->setValue(25);
    spread->setValue(8);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  });

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  CHECK(settings->style.outer_glows.size() == 1);
  const auto& result = settings->style.outer_glows.front();
  CHECK(result.enabled);
  CHECK(result.technique == patchy::LayerGlowTechnique::Softer);
  CHECK(result.range == 25.0F);
  CHECK(result.spread == 8.0F);
}

void ui_layer_style_pattern_warning_follows_resolvability() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Photoshop Effects",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));
  patchy::LayerPatternOverlay pattern;
  pattern.enabled = true;
  pattern.pattern_id = patchy::builtin_pattern_presets().front().id;
  pattern.pattern_name = patchy::builtin_pattern_presets().front().english_name;
  layer.layer_style().pattern_overlays.push_back(pattern);

  // A resolvable reference (built-in preset) shows neither the legacy
  // preserved-only banner nor the missing-pattern banner.
  bool checked_resolvable = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    CHECK(dialog->findChild<QLabel*>(QStringLiteral("layerStyleUnsupportedEffectsWarning")) == nullptr);
    CHECK(dialog->findChild<QLabel*>(QStringLiteral("layerStyleMissingPatternWarning")) == nullptr);
    checked_resolvable = true;
    dialog->reject();
  });
  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(!settings.has_value());
  CHECK(checked_resolvable);

  // An unresolvable reference (id absent from the document store and not a
  // built-in) shows the missing-pattern banner naming the pattern.
  layer.layer_style().pattern_overlays.front().pattern_id = "00000000-dead-beef-0000-000000000000";
  layer.layer_style().pattern_overlays.front().pattern_name = "Lost Pattern";
  bool checked_missing = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* warning = dialog->findChild<QLabel*>(QStringLiteral("layerStyleMissingPatternWarning"));
    CHECK(warning != nullptr);
    CHECK(warning->isVisible());
    CHECK(warning->text().contains(QStringLiteral("Lost Pattern")));
    CHECK(warning->text().contains(QStringLiteral("not embedded")));
    checked_missing = true;
    dialog->reject();
  });
  const auto missing_settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(!missing_settings.has_value());
  CHECK(checked_missing);
}

void ui_layer_style_pattern_overlay_controls_map_to_settings() {
  patchy::Document document(96, 72, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Patterned",
                      solid_pixels(48, 36, patchy::PixelFormat::rgba8(), QColor(80, 140, 220, 255)));

  const auto bricks_id = QString::fromLatin1(patchy::builtin_pattern_presets()[9].id);
  QTimer::singleShot(0, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyLayerStyleDialog")));
    CHECK(dialog != nullptr);
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePatternOverlayCategoryCheck"));
    auto* blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStylePatternOverlayBlendModeCombo"));
    auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStylePatternOverlayOpacitySpin"));
    auto* pattern_combo = dialog->findChild<QComboBox*>(QStringLiteral("layerStylePatternOverlayPatternCombo"));
    auto* angle = dialog->findChild<QSpinBox*>(QStringLiteral("layerStylePatternOverlayAngleSpin"));
    auto* scale = dialog->findChild<QSpinBox*>(QStringLiteral("layerStylePatternOverlayScaleSpin"));
    auto* link = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePatternOverlayLinkCheck"));
    auto* snap = dialog->findChild<QPushButton*>(QStringLiteral("layerStylePatternOverlaySnapOriginButton"));
    CHECK(categories != nullptr);
    CHECK(check != nullptr);
    CHECK(blend != nullptr);
    CHECK(opacity != nullptr);
    CHECK(pattern_combo != nullptr);
    CHECK(angle != nullptr);
    CHECK(scale != nullptr);
    CHECK(link != nullptr);
    CHECK(snap != nullptr);
    // Every built-in preset is offered with its persisted id.
    CHECK(pattern_combo->count() >= static_cast<int>(patchy::builtin_pattern_presets().size()));
    const auto pattern_items = categories->findItems(QStringLiteral("Pattern Overlay"), Qt::MatchExactly);
    CHECK(!pattern_items.empty());
    categories->setCurrentItem(pattern_items.front());
    QApplication::processEvents();
    check->setChecked(true);
    blend->setCurrentIndex(std::max(0, blend->findData(static_cast<int>(patchy::BlendMode::Multiply))));
    opacity->setValue(63);
    pattern_combo->setCurrentIndex(std::max(0, pattern_combo->findData(bricks_id)));
    angle->setValue(15);
    scale->setValue(250);
    link->setChecked(false);
    QTimer::singleShot(80, dialog, [dialog] { dialog->accept(); });
  });

  const auto settings = patchy::ui::request_layer_style_settings(nullptr, layer, {});
  CHECK(settings.has_value());
  CHECK(settings->style.pattern_overlays.size() == 1);
  const auto& result = settings->style.pattern_overlays.front();
  CHECK(result.enabled);
  CHECK(result.blend_mode == patchy::BlendMode::Multiply);
  CHECK(std::abs(result.opacity - 0.63F) < 0.001F);
  CHECK(result.pattern_id == patchy::builtin_pattern_presets()[9].id);
  CHECK(result.pattern_name == patchy::builtin_pattern_presets()[9].english_name);
  CHECK(result.angle_degrees == 15.0F);
  CHECK(std::abs(result.scale - 2.5F) < 0.001F);
  CHECK(!result.link_with_layer);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_style_gradient_tests_part1() {
  return {
      {"ui_layer_style_dialog_coalesces_rapid_slider_preview_callbacks",
       ui_layer_style_dialog_coalesces_rapid_slider_preview_callbacks},
      {"ui_layer_style_opacity_slider_does_not_block_on_slow_preview_render",
       ui_layer_style_opacity_slider_does_not_block_on_slow_preview_render},
      {"ui_layer_style_dialog_does_not_open_a_second_dialog",
       ui_layer_style_dialog_does_not_open_a_second_dialog},
      {"ui_blend_if_range_editor_renders_and_edits_split_handles",
       ui_blend_if_range_editor_renders_and_edits_split_handles},
      {"ui_layer_style_blend_if_controls_load_channels_and_map_settings",
       ui_layer_style_blend_if_controls_load_channels_and_map_settings},
      {"ui_layer_style_blend_if_preview_off_accumulates_and_cancel_restores",
       ui_layer_style_blend_if_preview_off_accumulates_and_cancel_restores},
      {"ui_layer_style_blend_if_unsupported_requires_explicit_replace",
       ui_layer_style_blend_if_unsupported_requires_explicit_replace},
      {"ui_layer_style_gradient_editor_drag_updates_stops_and_previews",
       ui_layer_style_gradient_editor_drag_updates_stops_and_previews},
      {"ui_layer_style_gradient_opacity_track_adds_and_edits_alpha_stops",
       ui_layer_style_gradient_opacity_track_adds_and_edits_alpha_stops},
      {"ui_layer_style_gradient_midpoints_edit_color_and_opacity_tracks",
       ui_layer_style_gradient_midpoints_edit_color_and_opacity_tracks},
      {"ui_layer_style_gradient_page_controls_map_to_settings",
       ui_layer_style_gradient_page_controls_map_to_settings},
      {"ui_layer_style_preview_is_transient_and_show_effects_persists",
       ui_layer_style_preview_is_transient_and_show_effects_persists},
      {"ui_layer_style_gradient_stroke_controls_map_to_settings",
       ui_layer_style_gradient_stroke_controls_map_to_settings},
      {"ui_layer_style_bevel_lighting_controls_map_to_settings",
       ui_layer_style_bevel_lighting_controls_map_to_settings},
      {"ui_layer_style_satin_controls_map_to_settings", ui_layer_style_satin_controls_map_to_settings},
      {"ui_layer_style_outer_glow_technique_and_range_map_to_settings",
       ui_layer_style_outer_glow_technique_and_range_map_to_settings},
      {"ui_layer_style_pattern_warning_follows_resolvability",
       ui_layer_style_pattern_warning_follows_resolvability},
      {"ui_layer_style_pattern_overlay_controls_map_to_settings",
       ui_layer_style_pattern_overlay_controls_map_to_settings},
  };
}
