// MainWindow's preferences and settings implementation, split out of
// main_window.cpp: the Preferences dialog (show_preferences), the guide
// dialogs (new guide, guide layout, clear guides), and the settings
// load/save/apply members (ruler units, canvas aids, pen input, view
// settings). Pure function moves from main_window.cpp; behavior must
// stay identical.

#include "ui/canvas_types.hpp"
#include "color/color_management.hpp"
#include "core/document.hpp"
#include "core/guide_layout.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/main_window.hpp"
#include "core/pixel_tools.hpp"
#include "formats/raw_document_io.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/gradient_stops_editor.hpp"
#include "support/string_utils.hpp"
#include <QMenuBar>
#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "ui/main_window_shared.hpp"

#include "core/smart_object.hpp"
#include "support/atomic_file_write.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/modifier_names.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/hotkey_editor.hpp"
#include "ui/object_selection_model_download.hpp"
#include "ui/watermark_model_download.hpp"
#include "ui/save_metadata_settings.hpp"
#include "ai/watermark_engine.hpp"
#include "ai/segmentation_engine.hpp"
#include "ui/script_folders.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/measurement_units.hpp"
#include "ui/stress_test.hpp"
#include "ui/theme_file.hpp"
#include "ui/theme_manager.hpp"
#include "ui/theme_package.hpp"
#include "ui/theme_sounds.hpp"
#include "ui/theme_effects.hpp"
#include "ui/theme_script_host.hpp"
#include "ui/theme_palette.hpp"
#include "ui/unit_spin_box.hpp"
#include "ui/user_fonts.hpp"
#include "ui/scanner_import.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/update_checker.hpp"
#include "ui/theme_qss.hpp"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QDate>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QToolButton>
#include <QJsonValue>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QInputDialog>
#include <QIODevice>
#include <QIcon>
#include <QLabel>
#include <QLatin1Char>
#include <QLatin1String>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QOverload>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPoint>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include <QRadioButton>
#include <QRect>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QValidator>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <qlatin1stringview.h>
#include <span>
#include <string_view>
#include <utility>
#include <vector>


// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

QString pen_button_action_to_token(PenButtonAction action) {
  switch (action) {
    case PenButtonAction::None:
      return QStringLiteral("none");
    case PenButtonAction::PanCanvas:
      return QStringLiteral("pan");
    case PenButtonAction::ZoomCanvas:
      return QStringLiteral("zoom");
    case PenButtonAction::PickColor:
      return QStringLiteral("pickColor");
    case PenButtonAction::SetCloneSource:
      return QStringLiteral("setCloneSource");
    case PenButtonAction::SwapColors:
      return QStringLiteral("swapColors");
    case PenButtonAction::Undo:
      return QStringLiteral("undo");
    case PenButtonAction::Redo:
      return QStringLiteral("redo");
    case PenButtonAction::ToggleEraser:
      return QStringLiteral("toggleEraser");
    case PenButtonAction::IncreaseBrushSize:
      return QStringLiteral("increaseBrushSize");
    case PenButtonAction::DecreaseBrushSize:
      return QStringLiteral("decreaseBrushSize");
  }
  return QStringLiteral("none");
}

PenButtonAction pen_button_action_from_token(const QString& token) {
  if (token == QStringLiteral("pan")) {
    return PenButtonAction::PanCanvas;
  }
  if (token == QStringLiteral("zoom")) {
    return PenButtonAction::ZoomCanvas;
  }
  if (token == QStringLiteral("pickColor")) {
    return PenButtonAction::PickColor;
  }
  if (token == QStringLiteral("setCloneSource")) {
    return PenButtonAction::SetCloneSource;
  }
  if (token == QStringLiteral("swapColors")) {
    return PenButtonAction::SwapColors;
  }
  if (token == QStringLiteral("undo")) {
    return PenButtonAction::Undo;
  }
  if (token == QStringLiteral("redo")) {
    return PenButtonAction::Redo;
  }
  if (token == QStringLiteral("toggleEraser")) {
    return PenButtonAction::ToggleEraser;
  }
  if (token == QStringLiteral("increaseBrushSize")) {
    return PenButtonAction::IncreaseBrushSize;
  }
  if (token == QStringLiteral("decreaseBrushSize")) {
    return PenButtonAction::DecreaseBrushSize;
  }
  return PenButtonAction::None;
}

void fill_alpha_checkerboard(QPainter& painter, const QRect& rect, int cell_size) {
  if (rect.isEmpty() || cell_size <= 0) {
    return;
  }
  const QColor dark(44, 44, 44);
  const QColor light(188, 188, 188);
  for (int y = rect.top(); y <= rect.bottom(); y += cell_size) {
    for (int x = rect.left(); x <= rect.right(); x += cell_size) {
      const QRect cell(x, y, std::min(cell_size, rect.right() - x + 1),
                       std::min(cell_size, rect.bottom() - y + 1));
      const auto parity = ((x - rect.left()) / cell_size + (y - rect.top()) / cell_size) % 2;
      painter.fillRect(cell, parity == 0 ? dark : light);
    }
  }
}

int color_alpha_percent(QColor color) {
  return std::clamp(static_cast<int>(std::lround(color.alphaF() * 100.0)), 0, 100);
}

QString overlay_color_summary_text(QColor color) {
  // The opacity spin beside the button shows the alpha, so the button names only the color.
  return color.name(QColor::HexRgb).toUpper();
}

QIcon overlay_color_swatch_icon(QColor color) {
  QPixmap pixmap(48, 24);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  const QRect swatch = pixmap.rect().adjusted(1, 1, -2, -2);
  fill_alpha_checkerboard(painter, swatch, 6);
  painter.fillRect(swatch, color);
  painter.setPen(QPen(QColor(12, 12, 12), 1));
  painter.drawRect(swatch.adjusted(0, 0, -1, -1));
  return QIcon(pixmap);
}

QPixmap grid_overlay_preview_pixmap(QColor grid_color, QColor guide_color, int grid_style, int subdivisions) {
  QPixmap pixmap(218, 86);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, false);
  const QRect preview_rect = pixmap.rect().adjusted(1, 1, -2, -2);
  fill_alpha_checkerboard(painter, preview_rect, 14);
  painter.fillRect(QRect(preview_rect.left(), preview_rect.top(), preview_rect.width() / 2, preview_rect.height()),
                   QColor(28, 30, 34, 130));
  painter.fillRect(QRect(preview_rect.left() + preview_rect.width() / 2, preview_rect.top(),
                         preview_rect.width() - preview_rect.width() / 2, preview_rect.height()),
                   QColor(238, 238, 238, 110));

  auto minor_color = grid_color;
  minor_color.setAlpha(std::clamp(grid_color.alpha() / 2, 24, 120));
  auto major_color = grid_color;
  major_color.setAlpha(std::clamp(grid_color.alpha(), 45, 220));

  const int safe_subdivisions = std::clamp(subdivisions, 1, 64);
  constexpr int major_spacing = 48;
  const int minor_spacing = std::max(6, major_spacing / safe_subdivisions);
  const auto draw_grid = [&](int spacing, QColor color, Qt::PenStyle style) {
    if (spacing <= 0) {
      return;
    }
    QPen pen(color, 1.0, style);
    pen.setCosmetic(true);
    painter.setPen(pen);
    for (int x = preview_rect.left(); x <= preview_rect.right(); x += spacing) {
      painter.drawLine(QPoint(x, preview_rect.top()), QPoint(x, preview_rect.bottom()));
    }
    for (int y = preview_rect.top(); y <= preview_rect.bottom(); y += spacing) {
      painter.drawLine(QPoint(preview_rect.left(), y), QPoint(preview_rect.right(), y));
    }
  };
  if (safe_subdivisions > 1) {
    draw_grid(minor_spacing, minor_color, grid_style == 0 ? Qt::DotLine : Qt::DashLine);
  }
  draw_grid(major_spacing, major_color, grid_style == 0 ? Qt::SolidLine : Qt::DotLine);

  QPen guide_pen(guide_color, 2.0, Qt::SolidLine);
  guide_pen.setCosmetic(true);
  painter.setPen(guide_pen);
  const int vertical_guide = preview_rect.left() + (preview_rect.width() * 2) / 3;
  const int horizontal_guide = preview_rect.top() + preview_rect.height() / 2;
  painter.drawLine(QPoint(vertical_guide, preview_rect.top()), QPoint(vertical_guide, preview_rect.bottom()));
  painter.drawLine(QPoint(preview_rect.left(), horizontal_guide), QPoint(preview_rect.right(), horizontal_guide));

  painter.setPen(QPen(QColor(92, 92, 92), 1));
  painter.drawRect(preview_rect.adjusted(0, 0, -1, -1));
  return pixmap;
}

// Interface-scale text (GitHub issue 333) without its percent sign, trimmed.
QString strip_gui_scale_suffix(QString text) {
  text = text.trimmed();
  const auto suffix = percent_suffix().trimmed();
  if (!suffix.isEmpty() && text.endsWith(suffix)) {
    text.chop(suffix.size());
  } else if (text.endsWith(QLatin1Char('%'))) {
    text.chop(1);
  }
  return text.trimmed();
}

// A whole percent, with or without the percent sign. Returns the number as typed, not
// range-checked; nullopt for anything else.
std::optional<int> parse_gui_scale_text(const QString& input) {
  const auto text = strip_gui_scale_suffix(input);
  if (text.isEmpty() || text.size() > 4) {
    return std::nullopt;
  }
  for (const QChar c : text) {
    if (c < QLatin1Char('0') || c > QLatin1Char('9')) {
      return std::nullopt;
    }
  }
  return text.toInt();
}

QString gui_scale_text(int percent) { return QString::number(percent) + percent_suffix(); }

// A page title as the page list shows it: the titles are tab-style strings ("Units && Grids"),
// where && is a literal ampersand and a single & marks a mnemonic a list row has no use for.
QString preferences_page_list_text(const QString& title) {
  QString text;
  text.reserve(title.size());
  for (qsizetype index = 0; index < title.size(); ++index) {
    if (title.at(index) == QLatin1Char('&') && index + 1 < title.size()) {
      ++index;
    }
    text.append(title.at(index));
  }
  return text;
}

// Lets the editable interface-scale combo take any whole percent in the range. Over the
// maximum cannot be typed; under the minimum is held as intermediate and clamped up when
// the edit is committed, as normalize_gui_scale_percent clamps a stored value.
class GuiScaleValidator final : public QValidator {
 public:
  using QValidator::QValidator;

  State validate(QString& input, int& /*pos*/) const override {
    // Empty, or only the percent sign left while the digits are retyped.
    if (strip_gui_scale_suffix(input).isEmpty()) {
      return Intermediate;
    }
    const auto value = parse_gui_scale_text(input);
    if (!value || *value > kMaxGuiScalePercent) {
      return Invalid;
    }
    return *value < kMinGuiScalePercent ? Intermediate : Acceptable;
  }

  void fixup(QString& input) const override {
    if (const auto value = parse_gui_scale_text(input)) {
      input = gui_scale_text(std::clamp(*value, kMinGuiScalePercent, kMaxGuiScalePercent));
    }
  }
};

}  // namespace

void MainWindow::show_preferences(PreferencesPage page, const QString& hotkey_search) {
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("patchyPreferencesDialog"));
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, tr("Preferences"));

  auto settings = app_settings();
  dialog.setMinimumSize(650, 430);
  dialog.resize(700, 560);

  const auto make_tab_page = [](QWidget* parent) {
    // Wrap each page in a scroll area so a page whose content is taller than the
    // dialog scrolls instead of overlapping its own controls.
    // The host insets the scroll area from the pane's top and bottom edges so
    // the vertical scroll bar does not butt against the pane border.
    auto* host = new QWidget(parent);
    host->setObjectName(QStringLiteral("preferencesTabHost"));
    auto* host_layout = new QVBoxLayout(host);
    host_layout->setContentsMargins(0, 6, 0, 6);
    host_layout->setSpacing(0);
    auto* scroll = new QScrollArea(host);
    scroll->setObjectName(QStringLiteral("preferencesTabScroll"));
    host_layout->addWidget(scroll);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* page = new QWidget(scroll);
    page->setObjectName(QStringLiteral("preferencesTabPage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    scroll->setWidget(page);
    return std::pair<QWidget*, QVBoxLayout*>{host, layout};
  };
  const auto configure_panel = [](QFrame* panel) {
    panel->setProperty("preferencesPanel", true);
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  };
  const auto configure_form = [](QFormLayout* form) {
    form->setContentsMargins(12, 12, 12, 12);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  };

  // The pages are listed down the left (GitHub issue 451): a tab row made the dialog wider
  // with every page added. Each row carries its page's id, which preferences/lastPage
  // stores, so a page added later never shifts the remembered one.
  auto* page_list = new QListWidget(&dialog);
  page_list->setObjectName(QStringLiteral("preferencesPageList"));
  page_list->setSelectionMode(QAbstractItemView::SingleSelection);
  page_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  page_list->setTextElideMode(Qt::ElideNone);
  page_list->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  auto* page_stack = new QStackedWidget(&dialog);
  page_stack->setObjectName(QStringLiteral("preferencesPageStack"));
  const auto add_page = [page_list, page_stack](QWidget* page, const QString& title, const QString& id) {
    const int index = page_stack->addWidget(page);
    auto* item = new QListWidgetItem(preferences_page_list_text(title), page_list);
    item->setData(Qt::UserRole, id);
    return index;
  };
  connect(page_list, &QListWidget::currentRowChanged, page_stack, [page_stack](int row) {
    if (row >= 0) {
      page_stack->setCurrentIndex(row);
    }
  });

  auto [application_page, application_layout] = make_tab_page(page_stack);
  auto* application_group = new QFrame(application_page);
  application_group->setObjectName(QStringLiteral("preferencesApplicationGroup"));
  configure_panel(application_group);
  auto* application_form = new QFormLayout(application_group);
  configure_form(application_form);

  auto* language_combo = new QComboBox(application_group);
  language_combo->setObjectName(QStringLiteral("preferencesLanguageCombo"));
  // Native names are data, not translated text: a user who cannot read the current
  // language still finds their own.
  for (const auto& language : LocalizationManager::instance().available_languages()) {
    language_combo->addItem(language.native_name, language.code);
  }
  const auto current_language = LocalizationManager::instance().current_language();
  const auto current_index = language_combo->findData(current_language);
  language_combo->setCurrentIndex(current_index >= 0 ? current_index : 0);
  application_form->addRow(tr("Language:"), language_combo);

  // A list of presets that also takes any whole percent in the range (GitHub issue 333).
  // stored_gui_scale_percent() is already clamped to it, so the combo shows exactly what
  // startup applies, as typed text when it is not a preset.
  // It shares one row with the toolbar icon size below: both are short combos about how
  // big the chrome is. The form label's buddy is the scale combo; the icon size label
  // beside it is its own combo's buddy.
  auto* scale_row = new QWidget(application_group);
  scale_row->setObjectName(QStringLiteral("preferencesInterfaceScaleRow"));
  auto* scale_layout = new QHBoxLayout(scale_row);
  scale_layout->setContentsMargins(0, 0, 0, 0);
  auto* gui_scale_combo = new QComboBox(scale_row);
  gui_scale_combo->setObjectName(QStringLiteral("preferencesGuiScaleCombo"));
  gui_scale_combo->setEditable(true);
  gui_scale_combo->setInsertPolicy(QComboBox::NoInsert);
  // Inline completion would turn a typed "15" into "150%".
  gui_scale_combo->setCompleter(nullptr);
  gui_scale_combo->setValidator(new GuiScaleValidator(gui_scale_combo));
  for (const int percent : kGuiScalePresetPercents) {
    gui_scale_combo->addItem(gui_scale_text(percent), percent);
  }
  // A modest fixed width with room for "300%" and the caret, not the whole row.
  gui_scale_combo->setMinimumContentsLength(7);
  gui_scale_combo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  const int entry_gui_scale = stored_gui_scale_percent();
  const int entry_gui_scale_index = gui_scale_combo->findData(entry_gui_scale);
  if (entry_gui_scale_index >= 0) {
    gui_scale_combo->setCurrentIndex(entry_gui_scale_index);
  } else {
    gui_scale_combo->setCurrentIndex(-1);
    gui_scale_combo->setEditText(gui_scale_text(entry_gui_scale));
  }

  // The tool palette's icons and buttons on top of the interface scale, as named steps
  // (a second percentage beside the first would read as the same setting). Applied live
  // on OK; the item data is the stored token.
  auto* toolbar_icon_size_combo = new QComboBox(scale_row);
  toolbar_icon_size_combo->setObjectName(QStringLiteral("preferencesToolbarIconSizeCombo"));
  toolbar_icon_size_combo->setToolTip(
      tr("Size of the tool palette's icons and buttons, relative to the interface scale."));
  for (const auto size : kToolbarIconSizes) {
    toolbar_icon_size_combo->addItem(toolbar_icon_size_label(size), toolbar_icon_size_token(size));
  }
  toolbar_icon_size_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  toolbar_icon_size_combo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  toolbar_icon_size_combo->setCurrentIndex(
      std::max(0, toolbar_icon_size_combo->findData(toolbar_icon_size_token(stored_toolbar_icon_size()))));
  auto* toolbar_icon_size_label = new QLabel(tr("Toolbar icon size:"), scale_row);
  toolbar_icon_size_label->setObjectName(QStringLiteral("preferencesToolbarIconSizeLabel"));
  toolbar_icon_size_label->setBuddy(toolbar_icon_size_combo);
  scale_layout->addWidget(gui_scale_combo);
  scale_layout->addSpacing(12);
  scale_layout->addWidget(toolbar_icon_size_label);
  scale_layout->addWidget(toolbar_icon_size_combo);
  scale_layout->addStretch(1);
  auto* gui_scale_label = new QLabel(tr("Interface scale:"), application_group);
  gui_scale_label->setBuddy(gui_scale_combo);
  application_form->addRow(gui_scale_label, scale_row);
  // When a tool group's flyout opens (GitHub issue 459): press and hold (the default), a
  // click, or the pointer resting on the button. The customize dialog shows it too.
  auto* tool_flyout_open_combo = new QComboBox(application_group);
  tool_flyout_open_combo->setObjectName(QStringLiteral("preferencesToolFlyoutOpenCombo"));
  tool_flyout_open_combo->setToolTip(
      tr("How a tool group's menu opens: press and hold its button, click it, or rest the pointer on it."));
  for (const auto mode : kToolFlyoutOpenModes) {
    tool_flyout_open_combo->addItem(tool_flyout_open_mode_label(mode), tool_flyout_open_mode_token(mode));
  }
  tool_flyout_open_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  tool_flyout_open_combo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  tool_flyout_open_combo->setCurrentIndex(
      std::max(0, tool_flyout_open_combo->findData(tool_flyout_open_mode_token(tool_flyout_open_mode_))));
  auto* tool_flyout_open_label = new QLabel(tr("Open tool groups on:"), application_group);
  tool_flyout_open_label->setBuddy(tool_flyout_open_combo);
  application_form->addRow(tool_flyout_open_label, tool_flyout_open_combo);
  // Customize Tool Palette right under the toolbar icon size (GitHub issue 213), on a line of
  // its own: the scale row is already full in the longer languages. It opens as a child of
  // Preferences and applies its own choice on its own OK.
  auto* customize_palette_row = new QWidget(application_group);
  customize_palette_row->setObjectName(QStringLiteral("preferencesCustomizeToolPaletteRow"));
  auto* customize_palette_layout = new QHBoxLayout(customize_palette_row);
  customize_palette_layout->setContentsMargins(0, 0, 0, 0);
  auto* customize_palette_button = new QPushButton(tr("Customize Tool Palette..."), customize_palette_row);
  customize_palette_button->setObjectName(QStringLiteral("preferencesCustomizeToolPaletteButton"));
  customize_palette_button->setAutoDefault(false);
  connect(customize_palette_button, &QPushButton::clicked, &dialog,
          [this, &dialog] { show_tool_palette_customization(ToolPaletteCustomizeTab::Tools, &dialog); });
  customize_palette_layout->addWidget(customize_palette_button);
  customize_palette_layout->addStretch(1);
  application_form->addRow(QString(), customize_palette_row);

#if defined(Q_OS_LINUX) && !defined(Q_OS_WASM)
  // Linux only: the desktop's own title bar and frame instead of the frameless chrome
  // (GitHub issue 145; docs/wayland.md). Read at startup, so like the interface scale it
  // applies on the next launch and the accept branch shows the restart notice. It shows
  // the setting in effect: until the user picks, that is the desktop default (on with a
  // global menu bar, GitHub issue 161), and the key is written only when they change it.
  auto* system_title_bar_check = new QCheckBox(tr("Use the system title bar"), application_group);
  system_title_bar_check->setObjectName(QStringLiteral("preferencesSystemTitleBarCheck"));
  system_title_bar_check->setToolTip(
      tr("Shows the desktop's own title bar and window frame instead of Patchy's window "
         "controls in the menu bar. On by default where the desktop shows application "
         "menus in a global menu bar. Takes effect the next time Patchy starts."));
  const bool entry_system_title_bar = system_title_bar_effective();
  system_title_bar_check->setChecked(entry_system_title_bar);
  application_form->addRow(system_title_bar_check);
#endif

#ifndef Q_OS_WASM
  // The web build has no update check to configure: the deployed site is
  // always the current version. A store build has none either (the store updates it).
  QCheckBox* update_check = nullptr;
  QComboBox* update_interval_combo = nullptr;
  if (update_checks_available()) {
    // The checkbox and how often it applies on one row, like the recovery row below
    // (GitHub issue 92). The item data is the day count the setting stores.
    auto* update_row = new QWidget(application_group);
    update_row->setObjectName(QStringLiteral("preferencesUpdateCheckRow"));
    auto* update_layout = new QHBoxLayout(update_row);
    update_layout->setContentsMargins(0, 0, 0, 0);
    update_check = new QCheckBox(tr("Check for updates on startup"), update_row);
    update_check->setObjectName(QStringLiteral("preferencesCheckForUpdatesCheck"));
    update_check->setChecked(settings.value(QStringLiteral("updates/checkOnStartup"), true).toBool());
    update_interval_combo = new QComboBox(update_row);
    update_interval_combo->setObjectName(QStringLiteral("preferencesUpdateCheckIntervalCombo"));
    update_interval_combo->addItem(tr("Every launch"), 0);
    // At most once a calendar day, for people who start Patchy several times a day
    // (GitHub issue 189).
    update_interval_combo->addItem(tr("Daily"), 1);
    update_interval_combo->addItem(tr("Weekly"), 7);
    update_interval_combo->addItem(tr("Every 2 weeks"), 14);
    update_interval_combo->addItem(tr("Monthly"), 30);
    // Sized to the longest entry: the row's long checkbox text otherwise squeezes
    // the combo until its entries elide (GitHub issue 146).
    update_interval_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    update_interval_combo->setToolTip(
        tr("How often Patchy looks for a new version when it starts. The About window always checks."));
    update_interval_combo->setCurrentIndex(
        std::max(0, update_interval_combo->findData(stored_update_check_interval_days())));
    update_interval_combo->setEnabled(update_check->isChecked());
    connect(update_check, &QCheckBox::toggled, update_interval_combo, &QComboBox::setEnabled);
    update_layout->addWidget(update_check);
    update_layout->addWidget(update_interval_combo);
    update_layout->addStretch(1);
    application_form->addRow(update_row);
  }
  // Automatic document recovery (docs/document-recovery.md): a checkbox and the
  // interval combo on one row, the way Photoshop's File Handling page lays it out.
  auto* recovery_row = new QWidget(application_group);
  recovery_row->setObjectName(QStringLiteral("preferencesRecoveryRow"));
  auto* recovery_layout = new QHBoxLayout(recovery_row);
  recovery_layout->setContentsMargins(0, 0, 0, 0);
  auto* recovery_check = new QCheckBox(tr("Automatically save recovery information every"), recovery_row);
  recovery_check->setObjectName(QStringLiteral("preferencesRecoveryEnabledCheck"));
  recovery_check->setToolTip(
      tr("Writes a copy of each changed document to a recovery folder so it can be reopened after a "
         "crash. The file you saved is never touched, and the copies are removed when Patchy quits normally."));
  recovery_check->setChecked(stored_recovery_enabled());
  auto* recovery_combo = new QComboBox(recovery_row);
  recovery_combo->setObjectName(QStringLiteral("preferencesRecoveryIntervalCombo"));
  for (const int minutes : kRecoveryIntervalMinutes) {
    recovery_combo->addItem(tr("%n minute(s)", nullptr, minutes), minutes);
  }
  recovery_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);  // issue 146, as above
  recovery_combo->setCurrentIndex(recovery_combo->findData(stored_recovery_interval_minutes()));
  recovery_combo->setEnabled(recovery_check->isChecked());
  connect(recovery_check, &QCheckBox::toggled, recovery_combo, &QComboBox::setEnabled);
  recovery_layout->addWidget(recovery_check);
  recovery_layout->addWidget(recovery_combo);
  recovery_layout->addStretch(1);
  application_form->addRow(recovery_row);
#endif
  auto* psd_import_warnings_check =
      new QCheckBox(tr("Show import warnings and notes in a popup (status bar otherwise)"), application_group);
  psd_import_warnings_check->setObjectName(QStringLiteral("preferencesShowPsdImportWarningsCheck"));
  psd_import_warnings_check->setToolTip(
      tr("When enabled, opening a file shows the PSD compatibility report and an Import Notes popup. "
         "When disabled, import notes appear only in the status bar."));
  psd_import_warnings_check->setChecked(
      settings.value(QStringLiteral("imports/showPsdWarningsAndInfo"), false).toBool());
  application_form->addRow(psd_import_warnings_check);
  auto* raw_develop_check =
      new QCheckBox(tr("Show the develop dialog when opening camera raw files"), application_group);
  raw_develop_check->setObjectName(QStringLiteral("preferencesShowRawDevelopCheck"));
  raw_develop_check->setToolTip(
      tr("When disabled, camera raw files open immediately with neutral develop settings "
         "(as-shot white balance, no adjustments)."));
  raw_develop_check->setChecked(settings.value(QStringLiteral("imports/showRawDevelopDialog"), true).toBool());
  application_form->addRow(raw_develop_check);
  // docs/rgb-profiles.md: Patchy shows every RGB document as sRGB, so other RGB profiles
  // convert on open unless this is off.
  auto* convert_rgb_profiles_check =
      new QCheckBox(tr("Convert files with other RGB profiles to sRGB when opening"), application_group);
  convert_rgb_profiles_check->setObjectName(QStringLiteral("preferencesConvertRgbProfilesCheck"));
  convert_rgb_profiles_check->setToolTip(
      tr("Patchy shows and edits RGB colors as sRGB. Files in Display P3, Adobe RGB or another RGB profile "
         "look right only after converting. When disabled, they open with their own values and keep their "
         "profile."));
  convert_rgb_profiles_check->setChecked(
      settings.value(QStringLiteral("imports/convertRgbProfilesToSrgb"), true).toBool());
  application_form->addRow(convert_rgb_profiles_check);
  // GitHub issue 363: File > New can leave the document as the Background alone.
  auto* new_document_layer_check =
      new QCheckBox(tr("Add an empty layer above the Background in new documents"), application_group);
  new_document_layer_check->setObjectName(QStringLiteral("preferencesNewDocumentEmptyLayerCheck"));
  new_document_layer_check->setToolTip(
      tr("When disabled, File > New creates only the Background layer, selected. Artboard documents and "
         "documents from the clipboard are unchanged."));
  new_document_layer_check->setChecked(settings.value(QStringLiteral("newDocument/addEmptyLayer"), true).toBool());
  application_form->addRow(new_document_layer_check);
  auto* missing_fonts_check =
      new QCheckBox(tr("Show the Missing Fonts dialog when opening a document with missing fonts"), application_group);
  missing_fonts_check->setObjectName(QStringLiteral("preferencesShowMissingFontsDialogCheck"));
  missing_fonts_check->setToolTip(
      tr("Type > Resolve Missing Fonts opens the same dialog at any time."));
  missing_fonts_check->setChecked(settings.value(QStringLiteral("imports/showMissingFontsDialog"), true).toBool());
  application_form->addRow(missing_fonts_check);
  auto* missing_links_check = new QCheckBox(
      tr("Show the Missing Linked Files dialog when opening a document with missing linked files"), application_group);
  missing_links_check->setObjectName(QStringLiteral("preferencesShowMissingLinksDialogCheck"));
  missing_links_check->setToolTip(tr("Layer > Smart Objects > Relink to File... repairs a link at any time."));
  missing_links_check->setChecked(settings.value(QStringLiteral("imports/showMissingLinksDialog"), true).toBool());
  application_form->addRow(missing_links_check);
  auto* zoom_thumbnails_check =
      new QCheckBox(tr("Zoom layer thumbnails to the layer content"), application_group);
  zoom_thumbnails_check->setObjectName(QStringLiteral("preferencesZoomLayerThumbnailsCheck"));
  zoom_thumbnails_check->setToolTip(
      tr("When enabled, layer thumbnails crop to the layer's visible pixels instead of "
         "previewing the whole canvas, so small layers fill their thumbnail."));
  zoom_thumbnails_check->setChecked(zoom_layer_thumbnails_to_content_);
  application_form->addRow(zoom_thumbnails_check);
  auto* vector_preview_check = new QCheckBox(tr("Dynamic Vector Preview"), application_group);
  vector_preview_check->setObjectName(QStringLiteral("preferencesDynamicVectorPreviewCheck"));
  vector_preview_check->setToolTip(tr("Keep vector artwork sharp when zooming, including in documents with pixel layers. Saved files and exports keep their pixel resolution."));
  vector_preview_check->setChecked(view_vector_preview_enabled_);
  application_form->addRow(vector_preview_check);
  // Resets the "Do this for every indexed image" choice remembered by the
  // indexed-image adoption prompt.
  auto* indexed_open_combo = new QComboBox(application_group);
  indexed_open_combo->setObjectName(QStringLiteral("preferencesIndexedOpenCombo"));
  indexed_open_combo->addItem(tr("Ask every time"), QStringLiteral("ask"));
  indexed_open_combo->addItem(tr("Always use the palette"), QStringLiteral("always"));
  indexed_open_combo->addItem(tr("Always edit as RGB"), QStringLiteral("never"));
  const auto indexed_open_policy =
      settings.value(QStringLiteral("imports/adoptIndexedPalette"), QStringLiteral("ask")).toString();
  const auto indexed_open_index = indexed_open_combo->findData(indexed_open_policy);
  indexed_open_combo->setCurrentIndex(indexed_open_index >= 0 ? indexed_open_index : 0);
  application_form->addRow(tr("Opening indexed images:"), indexed_open_combo);
  // The Affinity "Image" layer import choice (also settable from the dialog
  // that appears while this is "Ask every time").
  auto* af_image_layers_combo = new QComboBox(application_group);
  af_image_layers_combo->setObjectName(QStringLiteral("preferencesAfImageLayersCombo"));
  af_image_layers_combo->addItem(tr("Ask every time"), QStringLiteral("ask"));
  af_image_layers_combo->addItem(tr("Keep as smart objects"), QStringLiteral("smart"));
  af_image_layers_combo->addItem(tr("Convert to pixel layers"), QStringLiteral("pixel"));
  af_image_layers_combo->setToolTip(
      tr("Affinity documents place image files as \"Image\" layers. Smart objects keep each "
         "placed file's full-resolution original for re-editing and PSD export; pixel layers "
         "keep only the pixels at their placed size."));
  const auto af_image_layers_policy =
      settings.value(QStringLiteral("imports/afImageLayers"), QStringLiteral("ask")).toString();
  const auto af_image_layers_index = af_image_layers_combo->findData(af_image_layers_policy);
  af_image_layers_combo->setCurrentIndex(af_image_layers_index >= 0 ? af_image_layers_index : 0);
  application_form->addRow(tr("Opening Affinity image layers:"), af_image_layers_combo);
  // How a layered document is written to PDF (also settable from the question that
  // appears while this is "Ask every time").
  auto* pdf_layers_combo = new QComboBox(application_group);
  pdf_layers_combo->setObjectName(QStringLiteral("preferencesPdfLayersCombo"));
  pdf_layers_combo->addItem(tr("Ask every time"), QStringLiteral("ask"));
  pdf_layers_combo->addItem(tr("Flatten to one image"), QStringLiteral("flatten"));
  pdf_layers_combo->addItem(tr("Keep layers as editable objects"), QStringLiteral("editable"));
  pdf_layers_combo->setToolTip(
      tr("Editable objects keep shape layers as paths, text as real text, and pixel layers as images, "
         "so the PDF opens as separate pieces; blend modes, adjustments, layer styles, and pixel masks "
         "are flattened into images where needed, so the page may not look exactly like the canvas. "
         "One flattened image always looks exactly like the canvas."));
  const auto pdf_layers_policy =
      settings.value(QStringLiteral("saveOptions/pdfLayerPolicy"), QStringLiteral("ask")).toString();
  const auto pdf_layers_index = pdf_layers_combo->findData(pdf_layers_policy);
  pdf_layers_combo->setCurrentIndex(pdf_layers_index >= 0 ? pdf_layers_index : 0);
  application_form->addRow(tr("Saving layered documents as PDF:"), pdf_layers_combo);
  // Fonts dropped onto the window persist (desktop: the AppData user-fonts
  // directory; wasm: IndexedDB). This is the one way to empty that store;
  // already-registered fonts stay usable because application fonts are never
  // removed at runtime, and on desktop their store files are only deleted by
  // the next launch (docs/fonts.md).
  auto* remove_fonts_button = new QPushButton(tr("Remove Added Fonts..."), application_group);
  remove_fonts_button->setObjectName(QStringLiteral("preferencesRemoveUserFontsButton"));
  application_form->addRow(remove_fonts_button);
  connect(remove_fonts_button, &QPushButton::clicked, &dialog, [&dialog] {
    QMessageBox confirm(QMessageBox::Question, tr("Remove Added Fonts"),
#ifdef Q_OS_WASM
                        tr("Remove all fonts you added to Patchy? They stay usable until you "
                           "reload the page."),
#else
                        tr("Remove all fonts you added to Patchy? They stay usable until you "
                           "restart Patchy."),
#endif
                        QMessageBox::NoButton, &dialog);
    confirm.setObjectName(QStringLiteral("preferencesRemoveUserFontsConfirm"));
    auto* remove_button = confirm.addButton(tr("Remove"), QMessageBox::AcceptRole);
    confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(remove_button);
    exec_dialog(confirm);
    if (confirm.clickedButton() == remove_button) {
      user_fonts::clear_user_font_store();
    }
  });
  application_layout->addWidget(application_group);

  // CMYK: the working space behind the picker's ink fields, the Info readout and CMYK
  // swatches (docs/cmyk.md). Applied on OK.
  auto* cmyk_group = new QFrame(application_page);
  cmyk_group->setObjectName(QStringLiteral("preferencesCmykGroup"));
  configure_panel(cmyk_group);
  auto* cmyk_form = new QFormLayout(cmyk_group);
  configure_form(cmyk_form);
  auto* cmyk_header = new QLabel(tr("CMYK"), cmyk_group);
  auto cmyk_header_font = cmyk_header->font();
  cmyk_header_font.setBold(true);
  cmyk_header->setFont(cmyk_header_font);
  cmyk_form->addRow(cmyk_header);
  auto* cmyk_info = new QLabel(
      tr("Patchy edits in RGB. This profile sets what CMYK numbers mean in the color picker, the Info panel "
         "and CMYK swatches. Image > Mode > CMYK Proof, View > Proof Colors, and CMYK saves and exports also "
         "convert colors through it."),
      cmyk_group);
  cmyk_info->setWordWrap(true);
  cmyk_form->addRow(cmyk_info);
  // Only the basICColor profile ships with Patchy (docs/cmyk.md, licence record); every
  // other entry is read from the operating system's and Adobe's profile folders.
  auto* cmyk_source_info = new QLabel(
      tr("Patchy includes one CMYK profile, ISO Coated v2 (basICColor). The other profiles in this list "
         "were found on this computer, installed by the operating system or by other software such as "
         "Adobe applications; Patchy does not include them."),
      cmyk_group);
  cmyk_source_info->setObjectName(QStringLiteral("preferencesCmykSourceInfo"));
  cmyk_source_info->setWordWrap(true);
  cmyk_form->addRow(cmyk_source_info);
  const auto entry_cmyk_settings = stored_cmyk_working_settings();
  auto* cmyk_profile_combo = new QComboBox(cmyk_group);
  cmyk_profile_combo->setObjectName(QStringLiteral("preferencesCmykProfileCombo"));
  cmyk_profile_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  cmyk_profile_combo->setMinimumContentsLength(24);
  // Item data is the stored `color/cmykProfile` value: empty for Automatic,
  // kCmykProfileFormula, or a profile path. The file-picker row carries kCmykChooseFile
  // and never stays selected.
  static const auto kCmykChooseFile = QStringLiteral("choose-file");
  const auto add_cmyk_profile_entry = [cmyk_profile_combo](int row, const QString& path, QString description) {
    if (description.isEmpty()) {
      description = QFileInfo(path).completeBaseName();
    }
    cmyk_profile_combo->insertItem(row, description, path);
    cmyk_profile_combo->setItemData(row, QDir::toNativeSeparators(path), Qt::ToolTipRole);
  };
  {
    const auto automatic_space = cmyk_working_space_for(CmykWorkingSettings{});
    const auto automatic_name = QString::fromStdString(automatic_space->profile_description());
    const bool automatic_included = automatic_space->profile_bytes() == bundled_cmyk_profile();
    cmyk_profile_combo->addItem(automatic_name.isEmpty() ? tr("Automatic")
                                : automatic_included
                                    ? tr("Automatic (%1, included with Patchy)").arg(automatic_name)
                                    : tr("Automatic (%1, found on this computer)").arg(automatic_name),
                                QString());
    for (const auto& installed : installed_cmyk_profiles()) {
      add_cmyk_profile_entry(cmyk_profile_combo->count(), to_qstring(installed.path),
                             QString::fromStdString(installed.description));
    }
    cmyk_profile_combo->addItem(tr("No profile (approximate formula)"), QLatin1String(kCmykProfileFormula));
#ifndef Q_OS_WASM
    // A picked file would not outlive the page in the browser, which has the bundled
    // profile and the formula.
    cmyk_profile_combo->insertSeparator(cmyk_profile_combo->count());
    cmyk_profile_combo->addItem(tr("Choose Profile File..."), kCmykChooseFile);
#endif
  }
  // A chosen file outside the scanned folders gets its own row just above "No profile".
  const auto select_cmyk_profile = [cmyk_profile_combo, add_cmyk_profile_entry](const QString& stored) {
    auto index = cmyk_profile_combo->findData(stored);
    if (index < 0 && !stored.isEmpty()) {
      const auto space = CmykWorkingSpace::from_profile_file(to_filesystem_path(stored),
                                                             RenderingIntent::RelativeColorimetric, true);
      if (space != nullptr) {
        index = cmyk_profile_combo->findData(QLatin1String(kCmykProfileFormula));
        add_cmyk_profile_entry(index, stored, QString::fromStdString(space->profile_description()));
      }
    }
    const QSignalBlocker blocker(cmyk_profile_combo);
    cmyk_profile_combo->setCurrentIndex(std::max(index, 0));
  };
  select_cmyk_profile(entry_cmyk_settings.profile);
  auto last_cmyk_profile = std::make_shared<QString>(cmyk_profile_combo->currentData().toString());
  connect(cmyk_profile_combo, &QComboBox::currentIndexChanged, &dialog,
          [&dialog, cmyk_profile_combo, select_cmyk_profile, last_cmyk_profile](int index) {
            const auto choice = cmyk_profile_combo->itemData(index).toString();
            if (choice != kCmykChooseFile) {
              *last_cmyk_profile = choice;
              return;
            }
            // The action row never stays selected: restore the real choice, then run the
            // file dialog once the popup has closed.
            select_cmyk_profile(*last_cmyk_profile);
            QTimer::singleShot(0, &dialog, [&dialog, select_cmyk_profile, last_cmyk_profile] {
              const auto path = get_open_file_name(&dialog, tr("Choose CMYK Profile"), QString(),
                                                   tr("ICC profiles (*.icc *.icm)"));
              if (path.isEmpty()) {
                return;
              }
              if (CmykWorkingSpace::from_profile_file(to_filesystem_path(path), RenderingIntent::RelativeColorimetric,
                                                      true) == nullptr) {
                show_critical_message(&dialog, tr("CMYK Profile"),
                                      tr("\"%1\" is not a CMYK profile Patchy can use.").arg(path),
                                      QStringLiteral("preferencesCmykProfileFailedMessageBox"));
                return;
              }
              select_cmyk_profile(path);
              *last_cmyk_profile = path;
            });
          });
  cmyk_form->addRow(tr("CMYK profile:"), cmyk_profile_combo);
  auto* cmyk_intent_combo = new QComboBox(cmyk_group);
  cmyk_intent_combo->setObjectName(QStringLiteral("preferencesCmykIntentCombo"));
  cmyk_intent_combo->addItem(tr("Perceptual"), static_cast<int>(RenderingIntent::Perceptual));
  cmyk_intent_combo->addItem(tr("Relative Colorimetric"), static_cast<int>(RenderingIntent::RelativeColorimetric));
  cmyk_intent_combo->addItem(tr("Saturation"), static_cast<int>(RenderingIntent::Saturation));
  cmyk_intent_combo->addItem(tr("Absolute Colorimetric"), static_cast<int>(RenderingIntent::AbsoluteColorimetric));
  cmyk_intent_combo->setCurrentIndex(
      std::max(cmyk_intent_combo->findData(static_cast<int>(entry_cmyk_settings.intent)), 0));
  cmyk_form->addRow(tr("Rendering intent:"), cmyk_intent_combo);
  auto* cmyk_black_point_check = new QCheckBox(tr("Black point compensation"), cmyk_group);
  cmyk_black_point_check->setObjectName(QStringLiteral("preferencesCmykBlackPointCheck"));
  cmyk_black_point_check->setChecked(entry_cmyk_settings.black_point_compensation);
  cmyk_form->addRow(cmyk_black_point_check);
  application_layout->addWidget(cmyk_group);

  // Development: the profiling stress test (see main_window_stress_test.cpp).
  auto* development_group = new QFrame(application_page);
  development_group->setObjectName(QStringLiteral("preferencesDevelopmentGroup"));
  configure_panel(development_group);
  auto* development_form = new QFormLayout(development_group);
  configure_form(development_form);
  auto* development_header = new QLabel(tr("Development"), development_group);
  auto development_header_font = development_header->font();
  development_header_font.setBold(true);
  development_header->setFont(development_header_font);
  development_form->addRow(development_header);
  auto* stress_info = new QLabel(
      tr("The profiling stress test builds a large scripted scene to measure rendering "
         "performance. It closes all open documents and takes several minutes. Primarily "
         "a development tool."),
      development_group);
  stress_info->setWordWrap(true);
  development_form->addRow(stress_info);
  auto* stress_size_combo = new QComboBox(development_group);
  stress_size_combo->setObjectName(QStringLiteral("preferencesStressSizeCombo"));
  stress_size_combo->addItem(tr("Quick (1024 px)"), stress_preset_token(StressPreset::Quick));
  stress_size_combo->addItem(tr("Small (2048 px)"), stress_preset_token(StressPreset::Small));
  stress_size_combo->addItem(tr("Standard (4096 px)"), stress_preset_token(StressPreset::Standard));
  stress_size_combo->addItem(tr("Huge (8192 px, needs lots of RAM)"), stress_preset_token(StressPreset::Huge));
  const auto stored_stress_preset =
      settings.value(QStringLiteral("development/stressPreset"), stress_preset_token(StressPreset::Quick))
          .toString();
  const auto stress_preset_index = stress_size_combo->findData(stored_stress_preset);
  stress_size_combo->setCurrentIndex(stress_preset_index >= 0 ? stress_preset_index : 0);
  development_form->addRow(tr("Stress test size:"), stress_size_combo);
  auto* run_stress_button = new QPushButton(tr("Run Profiling Stress Test..."), development_group);
  run_stress_button->setObjectName(QStringLiteral("preferencesRunStressTestButton"));
  development_form->addRow(run_stress_button);
  // Set by the run button; show_preferences starts the run only after the
  // dialog has closed and applied its settings (never from inside its nested
  // event loop).
  std::optional<StressPreset> pending_stress_preset;
  connect(run_stress_button, &QPushButton::clicked, &dialog,
          [&dialog, stress_size_combo, &pending_stress_preset] {
            const auto token = stress_size_combo->currentData().toString();
            auto stress_settings = app_settings();
            stress_settings.setValue(QStringLiteral("development/stressPreset"), token);
            pending_stress_preset = stress_preset_from_string(token).value_or(StressPreset::Standard);
            dialog.accept();
          });
  application_layout->addWidget(development_group);
  application_layout->addStretch(1);
  add_page(application_page, tr("Application"), QStringLiteral("application"));

  // Colors and Themes (main_window_preferences_themes.cpp). Reading every theme package
  // is slow, so the list fills on the page's first visit, like the Hotkeys panel below.
  auto [themes_page_widget, themes_layout] = make_tab_page(page_stack);
  const auto themes_page = build_preferences_themes_page(dialog, themes_layout, configure_panel);
  const int themes_tab_index = add_page(themes_page_widget, tr("Colors and Themes"), QStringLiteral("themes"));
  connect(page_stack, &QStackedWidget::currentChanged, &dialog, [themes_page, themes_tab_index](int index) {
    if (index == themes_tab_index) {
      themes_page.populate();
    }
  });
  // The theme list previews live, so every path out of the dialog that is not Accept
  // has to put the entry theme back. The chrome X and Esc both reject, and
  // run_stress_test_interactive runs past the accept branch, so a scope guard is safer
  // than an else.
  const auto restore_theme = qScopeGuard([themes_page] { themes_page.revert(); });

  // Tools: tool and canvas-input behavior (the mouse wheel, Free Transform), the way
  // Photoshop's Tools page groups them.
  auto [tools_page, tools_layout] = make_tab_page(page_stack);
  auto* tools_group = new QFrame(tools_page);
  tools_group->setObjectName(QStringLiteral("preferencesToolsGroup"));
  configure_panel(tools_group);
  auto* tools_form = new QFormLayout(tools_group);
  configure_form(tools_form);
  auto* wheel_zoom_check = new QCheckBox(tr("Mouse wheel zooms the canvas"), tools_group);
  wheel_zoom_check->setObjectName(QStringLiteral("preferencesWheelZoomCheck"));
  wheel_zoom_check->setChecked(wheel_zooms_);
  wheel_zoom_check->setToolTip(
      resolve_modifier_names(
          tr("Also applies to a pen button set to Scroll. Hold %CTRL% or Shift while scrolling to pan. "
             "Two-finger scrolling on a trackpad always pans; pinch to zoom.")));
  tools_form->addRow(wheel_zoom_check);
  auto* transform_shift_aspect_check =
      new QCheckBox(tr("Hold Shift to keep the aspect ratio when transforming"), tools_group);
  transform_shift_aspect_check->setObjectName(QStringLiteral("preferencesTransformShiftAspectCheck"));
  transform_shift_aspect_check->setToolTip(
      tr("When off, corner handles keep the aspect ratio and Shift resizes freely, matching "
         "current Photoshop. When on, corner handles resize freely and Shift keeps the aspect ratio."));
  transform_shift_aspect_check->setChecked(shift_keeps_transform_aspect_);
  tools_form->addRow(transform_shift_aspect_check);
  auto* transform_values_check =
      new QCheckBox(tr("Show transformation values while dragging"), tools_group);
  transform_values_check->setObjectName(QStringLiteral("preferencesShowTransformValuesCheck"));
  transform_values_check->setToolTip(
      tr("Shows a small readout beside the pointer while moving, scaling, or rotating: the "
         "reference point's position and the offset, the width and height with the scale "
         "percentages, or the angle and how far it turned."));
  transform_values_check->setChecked(show_transform_drag_values_);
  tools_form->addRow(transform_values_check);
  auto* transform_snap_check = new QCheckBox(tr("Snap transforms to the pixel grid"), tools_group);
  transform_snap_check->setObjectName(QStringLiteral("preferencesTransformSnapToPixelGridCheck"));
  transform_snap_check->setToolTip(
      tr("Positions and sizes typed into the Free Transform bar land on whole pixels, like "
         "Photoshop's \"Snap Vector Tools and Transforms to Pixel Grid\". Rotated transforms are "
         "not snapped. When off, a typed fraction such as 3.4 px is kept and the pixels are "
         "resampled."));
  transform_snap_check->setChecked(snap_transforms_to_pixel_grid_);
  tools_form->addRow(transform_snap_check);
  auto* escape_commits_text_check = new QCheckBox(tr("Use Esc to commit text"), tools_group);
  escape_commits_text_check->setObjectName(QStringLiteral("preferencesEscapeCommitsTextCheck"));
  escape_commits_text_check->setToolTip(resolve_modifier_names(
      tr("When on, Esc applies the text you are typing, like %CTRL%+Enter, and the options bar's "
         "Cancel button discards the edit. When off, Esc cancels the edit.")));
  escape_commits_text_check->setChecked(escape_commits_text_);
  tools_form->addRow(escape_commits_text_check);
  tools_layout->addWidget(tools_group);
#if !defined(Q_OS_WASM)
  // Object Selection's downloaded High Quality model (docs/ai-selection.md): its status and a
  // way to delete it (exactly its own files; Object Selection falls back to Standard).
  configure_automatic_selection_models();
  if (patchy::ai::segmentation_available()) {
    auto* model_group = new QFrame(tools_page);
    model_group->setObjectName(QStringLiteral("preferencesObjectSelectionModelGroup"));
    configure_panel(model_group);
    auto* model_layout = new QHBoxLayout(model_group);
    auto* model_status = new QLabel(model_group);
    model_status->setObjectName(QStringLiteral("preferencesObjectSelectionModelStatus"));
    model_status->setWordWrap(true);
    const auto model_megabytes = static_cast<int>((high_quality_model_download_bytes() + 500000) / 1000000);
    auto* download_model_button = new QPushButton(tr("Download"), model_group);
    download_model_button->setObjectName(QStringLiteral("preferencesDownloadObjectSelectionModelButton"));
    download_model_button->setToolTip(
        tr("Downloads the High Quality model (%1 MB from %2). Object Selection finds objects more accurately "
           "with it.")
            .arg(model_megabytes)
            .arg(model_download_source_text(high_quality_model_files())));
    auto* delete_model_button = new QPushButton(tr("Delete"), model_group);
    delete_model_button->setObjectName(QStringLiteral("preferencesDeleteObjectSelectionModelButton"));
    delete_model_button->setToolTip(tr("Deletes the downloaded High Quality model. Object Selection then uses the "
                                       "built-in model; you can download it again from the tool's options bar."));
    model_layout->addWidget(model_status, 1);
    model_layout->addWidget(download_model_button);
    model_layout->addWidget(delete_model_button);
    const auto refresh_model_status = [model_status, download_model_button, delete_model_button, model_megabytes] {
      const bool present = downloaded_high_quality_model_present(downloaded_models_folder());
      model_status->setText(present ? tr("High Quality Object Selection model: downloaded (%1 MB)").arg(model_megabytes)
                                    : tr("High Quality Object Selection model: not downloaded (%1 MB from %2)")
                                          .arg(model_megabytes)
                                          .arg(model_download_source_text(high_quality_model_files())));
      download_model_button->setEnabled(!present);
      delete_model_button->setEnabled(present);
    };
    refresh_model_status();
    connect(download_model_button, &QPushButton::clicked, &dialog, [this, &dialog, refresh_model_status] {
      QString error;
      if (download_high_quality_object_selection_model(true, {}, &error, &dialog)) {
        set_object_selection_high_quality_setting(true);
        refresh_object_select_model_combo();
        statusBar()->showMessage(tr("Object Selection now uses the High Quality model"));
      } else if (!error.isEmpty()) {
        show_status_error(tr("The High Quality model could not be downloaded: %1").arg(error));
      } else {
        statusBar()->showMessage(tr("Download cancelled"));
      }
      refresh_model_status();
    });
    connect(delete_model_button, &QPushButton::clicked, &dialog, [this, refresh_model_status] {
      QString error;
      if (delete_high_quality_object_selection_model(&error)) {
        statusBar()->showMessage(tr("Deleted the High Quality Object Selection model"));
      } else {
        show_status_error(tr("The High Quality model could not be deleted: %1").arg(error));
      }
      refresh_model_status();
    });
    tools_layout->addWidget(model_group);
  }
#endif
  tools_layout->addStretch(1);
  add_page(tools_page, tr("Tools"), QStringLiteral("tools"));

  // Metadata and Watermarks: what saves add of Patchy's own and the user's (docs/file-info.md,
  // "Saved metadata preference"), then the TrustMark text saves add, when they add it, the
  // check on open, and the optional models (docs/watermarks.md, "Preferences").
  auto [watermarks_page, watermarks_layout] = make_tab_page(page_stack);
  const auto section_heading = [](const QString& text, QWidget* parent, const char* name) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QLatin1String(name));
    auto font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
  };
  watermarks_layout->addWidget(
      section_heading(tr("Saved metadata"), watermarks_page, "preferencesSavedMetadataHeading"));
  auto* metadata_group = new QFrame(watermarks_page);
  metadata_group->setObjectName(QStringLiteral("preferencesSavedMetadataGroup"));
  configure_panel(metadata_group);
  auto* metadata_form = new QFormLayout(metadata_group);
  configure_form(metadata_form);
  const auto entry_save_metadata_mode = stored_save_metadata_mode();
  const auto entry_personal = stored_personal_save_metadata();
  auto* metadata_none_radio = new QRadioButton(tr("None: Patchy adds nothing of its own"), metadata_group);
  metadata_none_radio->setObjectName(QStringLiteral("preferencesSavedMetadataNoneRadio"));
  auto* metadata_basic_radio = new QRadioButton(
      tr("Basic: Patchy as the creating application, the save dates and a history entry"), metadata_group);
  metadata_basic_radio->setObjectName(QStringLiteral("preferencesSavedMetadataBasicRadio"));
  auto* metadata_personal_radio = new QRadioButton(tr("Basic plus my name and copyright"), metadata_group);
  metadata_personal_radio->setObjectName(QStringLiteral("preferencesSavedMetadataPersonalRadio"));
  auto* metadata_modes = new QButtonGroup(metadata_group);
  metadata_modes->addButton(metadata_none_radio);
  metadata_modes->addButton(metadata_basic_radio);
  metadata_modes->addButton(metadata_personal_radio);
  (entry_save_metadata_mode == SaveMetadataMode::None       ? metadata_none_radio
   : entry_save_metadata_mode == SaveMetadataMode::Personal ? metadata_personal_radio
                                                            : metadata_basic_radio)
      ->setChecked(true);
  auto* metadata_modes_column = new QWidget(metadata_group);
  metadata_modes_column->setObjectName(QStringLiteral("preferencesSavedMetadataModesRow"));
  auto* metadata_modes_layout = new QVBoxLayout(metadata_modes_column);
  metadata_modes_layout->setContentsMargins(0, 0, 0, 0);
  metadata_modes_layout->setSpacing(6);
  metadata_modes_layout->addWidget(metadata_none_radio);
  metadata_modes_layout->addWidget(metadata_basic_radio);
  metadata_modes_layout->addWidget(metadata_personal_radio);
  metadata_form->addRow(tr("Automatically add to saved files:"), metadata_modes_column);
  auto* metadata_name_edit = new QLineEdit(entry_personal.name, metadata_group);
  metadata_name_edit->setObjectName(QStringLiteral("preferencesSavedMetadataNameEdit"));
  metadata_name_edit->setPlaceholderText(tr("Your name as the author"));
  metadata_form->addRow(tr("Name:"), metadata_name_edit);
  auto* metadata_copyright_row = new QWidget(metadata_group);
  metadata_copyright_row->setObjectName(QStringLiteral("preferencesSavedMetadataCopyrightRow"));
  auto* metadata_copyright_layout = new QHBoxLayout(metadata_copyright_row);
  metadata_copyright_layout->setContentsMargins(0, 0, 0, 0);
  auto* metadata_copyright_edit = new QLineEdit(entry_personal.copyright, metadata_copyright_row);
  metadata_copyright_edit->setObjectName(QStringLiteral("preferencesSavedMetadataCopyrightEdit"));
  auto* metadata_suggest_button = new QPushButton(metadata_copyright_row);
  metadata_suggest_button->setObjectName(QStringLiteral("preferencesSavedMetadataSuggestButton"));
  metadata_copyright_layout->addWidget(metadata_copyright_edit, 1);
  metadata_copyright_layout->addWidget(metadata_suggest_button);
  metadata_form->addRow(tr("Copyright notice:"), metadata_copyright_row);
  auto* metadata_email_edit = new QLineEdit(entry_personal.email, metadata_group);
  metadata_email_edit->setObjectName(QStringLiteral("preferencesSavedMetadataEmailEdit"));
  metadata_email_edit->setPlaceholderText(tr("Optional"));
  metadata_form->addRow(tr("Email:"), metadata_email_edit);
  auto* metadata_website_edit = new QLineEdit(entry_personal.website, metadata_group);
  metadata_website_edit->setObjectName(QStringLiteral("preferencesSavedMetadataWebsiteEdit"));
  metadata_website_edit->setPlaceholderText(tr("Optional"));
  metadata_form->addRow(tr("Website:"), metadata_website_edit);
  auto* metadata_ask_check = new QCheckBox(tr("Ask before adding them to a file"), metadata_group);
  metadata_ask_check->setObjectName(QStringLiteral("preferencesSavedMetadataAskCheck"));
  metadata_ask_check->setChecked(entry_personal.ask);
  metadata_form->addRow(metadata_ask_check);
  auto* metadata_help = new QLabel(
      tr("Your name, copyright and contact go into PSD, JPEG, PNG, TIFF, WebP, GIF, JPEG XL and AVIF files that "
         "do not name you yet, beside any author already there. Metadata a file already carries is kept; File > "
         "File Info > Strip All Metadata removes it. Exports follow the Metadata choice in the Export dialog."),
      metadata_group);
  metadata_help->setObjectName(QStringLiteral("preferencesSavedMetadataHelp"));
  metadata_help->setWordWrap(true);
  metadata_form->addRow(metadata_help);
  auto* privacy_notice_check =
      new QCheckBox(tr("Show the File Info notice about information you may not want to share"), metadata_group);
  privacy_notice_check->setObjectName(QStringLiteral("preferencesFileInfoPrivacyNoticeCheck"));
  privacy_notice_check->setChecked(stored_show_file_info_privacy_notice());
  metadata_form->addRow(privacy_notice_check);
  watermarks_layout->addWidget(metadata_group);
  const int current_year = QDate::currentDate().year();
  const auto refresh_metadata_fields = [metadata_personal_radio, metadata_name_edit, metadata_copyright_edit,
                                        metadata_suggest_button, metadata_email_edit, metadata_website_edit,
                                        metadata_ask_check, current_year] {
    const bool personal = metadata_personal_radio->isChecked();
    for (QWidget* field : std::initializer_list<QWidget*>{metadata_name_edit, metadata_copyright_edit,
                                                          metadata_email_edit, metadata_website_edit,
                                                          metadata_ask_check}) {
      field->setEnabled(personal);
    }
    const auto suggestion = suggested_copyright_notice(metadata_name_edit->text(), current_year);
    metadata_suggest_button->setText(tr("Use \u201C%1\u201D").arg(suggestion));
    metadata_suggest_button->setEnabled(personal && !metadata_name_edit->text().trimmed().isEmpty());
  };
  connect(metadata_personal_radio, &QRadioButton::toggled, &dialog,
          [refresh_metadata_fields] { refresh_metadata_fields(); });
  connect(metadata_name_edit, &QLineEdit::textChanged, &dialog,
          [refresh_metadata_fields] { refresh_metadata_fields(); });
  connect(metadata_suggest_button, &QPushButton::clicked, &dialog,
          [metadata_copyright_edit, metadata_name_edit, current_year] {
            metadata_copyright_edit->setText(suggested_copyright_notice(metadata_name_edit->text(), current_year));
          });
  refresh_metadata_fields();
  watermarks_layout->addWidget(section_heading(tr("Watermarks"), watermarks_page, "preferencesWatermarksHeading"));
  auto* watermark_group = new QFrame(watermarks_page);
  watermark_group->setObjectName(QStringLiteral("preferencesWatermarksGroup"));
  configure_panel(watermark_group);
  auto* watermark_form = new QFormLayout(watermark_group);
  configure_form(watermark_form);
  const auto wrapped_note = [](const QString& text, QWidget* parent, const char* name) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QLatin1String(name));
    label->setWordWrap(true);
    return label;
  };
  watermark_form->addRow(wrapped_note(
      tr("Patchy can hide a short text in the images you save with Adobe TrustMark, an open invisible "
         "watermark. There is no key: anyone with a TrustMark reader can read it."),
      watermark_group, "preferencesWatermarkIntro"));
  auto* watermark_text_row = new QWidget(watermark_group);
  watermark_text_row->setObjectName(QStringLiteral("preferencesWatermarkTextRow"));
  auto* watermark_text_layout = new QHBoxLayout(watermark_text_row);
  watermark_text_layout->setContentsMargins(0, 0, 0, 0);
  auto* watermark_text_edit = new QLineEdit(stored_watermark_text(), watermark_text_row);
  watermark_text_edit->setObjectName(QStringLiteral("preferencesWatermarkTextEdit"));
  // TrustMark text is 7-bit ASCII; BCH_5, TrustMark's default, holds 8 characters.
  const int watermark_capacity = trustmark::schema_text_capacity(kSaveWatermarkSchema);
  watermark_text_edit->setValidator(new QRegularExpressionValidator(
      QRegularExpression(QStringLiteral("[\\x20-\\x7E]*")), watermark_text_edit));
  watermark_text_edit->setMaxLength(watermark_capacity);
  watermark_text_edit->setPlaceholderText(tr("Your name or a short ID"));
  auto* watermark_remaining = new QLabel(watermark_text_row);
  watermark_remaining->setObjectName(QStringLiteral("preferencesWatermarkRemainingLabel"));
  watermark_text_layout->addWidget(watermark_text_edit, 1);
  watermark_text_layout->addWidget(watermark_remaining);
  watermark_form->addRow(tr("Watermark text:"), watermark_text_row);
  auto* watermark_save_combo = new QComboBox(watermark_group);
  watermark_save_combo->setObjectName(QStringLiteral("preferencesWatermarkSaveCombo"));
  watermark_save_combo->addItem(tr("Never"), watermark_save_mode_token(WatermarkSaveMode::Never));
  watermark_save_combo->addItem(tr("Ask on save"), watermark_save_mode_token(WatermarkSaveMode::Ask));
  watermark_save_combo->addItem(tr("Add on every save"), watermark_save_mode_token(WatermarkSaveMode::Always));
  watermark_save_combo->setCurrentIndex(
      std::max(0, watermark_save_combo->findData(watermark_save_mode_token(stored_watermark_save_mode()))));
  watermark_form->addRow(tr("Add invisible watermark when saving:"), watermark_save_combo);
  watermark_form->addRow(wrapped_note(
      tr("Only the saved file's pixels are marked; the open document and its history do not change. PNG, JPEG, "
         "WebP, BMP, JPEG XL, AVIF and TIFF without layers are marked, and so are exports. Layered files (PSD, PSB, "
         "TIFF with layers) are not, since that would change layer pixels without you seeing it. Images under "
         "128 pixels and images that already carry your text are skipped."),
      watermark_group, "preferencesWatermarkSaveHelp"));
  auto* watermark_check_open = new QCheckBox(tr("Check opened images for watermarks"), watermark_group);
  watermark_check_open->setObjectName(QStringLiteral("preferencesWatermarkCheckOnOpenCheck"));
  watermark_check_open->setChecked(stored_watermark_check_on_open());
  watermark_form->addRow(watermark_check_open);
  watermark_form->addRow(wrapped_note(
      tr("Runs in the background after a file opens and shows a notice when it finds a Stable Diffusion "
         "watermark, or an Adobe TrustMark watermark once the models are downloaded."),
      watermark_group, "preferencesWatermarkCheckHelp"));
  watermarks_layout->addWidget(watermark_group);
  QLabel* watermark_models_needed = nullptr;
  std::function<void()> refresh_watermark_page;
#if !defined(Q_OS_WASM)
  // The TrustMark watermark models: status, Download and Delete (exactly their files).
  if (patchy::ai::trustmark_runtime_available()) {
    auto* models_group = new QFrame(watermarks_page);
    models_group->setObjectName(QStringLiteral("preferencesWatermarkModelsGroup"));
    configure_panel(models_group);
    auto* models_layout = new QVBoxLayout(models_group);
    auto* models_row = new QHBoxLayout();
    auto* watermark_status = new QLabel(models_group);
    watermark_status->setObjectName(QStringLiteral("preferencesWatermarkModelsStatus"));
    watermark_status->setWordWrap(true);
    const auto watermark_megabytes = watermark_model_download_megabytes();
    auto* download_watermark_button = new QPushButton(tr("Download"), models_group);
    download_watermark_button->setObjectName(QStringLiteral("preferencesDownloadWatermarkModelsButton"));
    download_watermark_button->setToolTip(
        tr("Downloads the Adobe TrustMark watermark models (Adobe's, MIT License; %1 MB from %2). File Info's Scan "
           "for Watermarks then reads TrustMark watermarks, and Image > Add Invisible Watermark can add one.")
            .arg(watermark_megabytes)
            .arg(model_download_source_text(watermark_model_files())));
    auto* delete_watermark_button = new QPushButton(tr("Delete"), models_group);
    delete_watermark_button->setObjectName(QStringLiteral("preferencesDeleteWatermarkModelsButton"));
    delete_watermark_button->setToolTip(tr("Deletes the downloaded watermark models."));
    models_row->addWidget(watermark_status, 1);
    models_row->addWidget(download_watermark_button);
    models_row->addWidget(delete_watermark_button);
    models_layout->addLayout(models_row);
    watermark_models_needed = wrapped_note(
        tr("Adding watermarks when saving and the TrustMark part of the check need these models. The Stable "
           "Diffusion check needs no download."),
        models_group, "preferencesWatermarkModelsNeededNote");
    models_layout->addWidget(watermark_models_needed);
    watermarks_layout->addWidget(models_group);
    connect(download_watermark_button, &QPushButton::clicked, &dialog, [this, &dialog, &refresh_watermark_page] {
      QString error;
      if (download_watermark_models(true, {}, &error, &dialog)) {
        statusBar()->showMessage(tr("Downloaded the watermark models"));
      } else if (!error.isEmpty()) {
        show_status_error(tr("The watermark models could not be downloaded: %1").arg(error));
      } else {
        statusBar()->showMessage(tr("Download cancelled"));
      }
      refresh_watermark_page();
    });
    connect(delete_watermark_button, &QPushButton::clicked, &dialog, [this, &refresh_watermark_page] {
      QString error;
      if (delete_watermark_models(&error)) {
        statusBar()->showMessage(tr("Deleted the watermark models"));
      } else {
        show_status_error(tr("The watermark models could not be deleted: %1").arg(error));
      }
      refresh_watermark_page();
    });
    refresh_watermark_page = [watermark_status, download_watermark_button, delete_watermark_button,
                              watermark_megabytes] {
      const bool present = downloaded_watermark_models_present(downloaded_models_folder());
      watermark_status->setText(present ? tr("TrustMark watermark models: downloaded (%1 MB)").arg(watermark_megabytes)
                                        : tr("TrustMark watermark models: not downloaded (%1 MB from %2)")
                                              .arg(watermark_megabytes)
                                              .arg(model_download_source_text(watermark_model_files())));
      download_watermark_button->setEnabled(!present);
      delete_watermark_button->setEnabled(present);
    };
  }
#endif
  const auto refresh_watermark_text = [watermark_text_edit, watermark_remaining, watermark_capacity,
                                       watermark_save_combo, watermark_check_open, watermark_models_needed,
                                       &refresh_watermark_page] {
    watermark_remaining->setText(tr("%1 of %2 characters left")
                                     .arg(watermark_capacity - static_cast<int>(watermark_text_edit->text().size()))
                                     .arg(watermark_capacity));
    if (refresh_watermark_page) {
      refresh_watermark_page();
    }
    if (watermark_models_needed != nullptr) {
      // Shown when a chosen option needs the models and they are missing.
      const bool wanted =
          watermark_save_combo->currentData().toString() != watermark_save_mode_token(WatermarkSaveMode::Never) ||
          watermark_check_open->isChecked();
      watermark_models_needed->setVisible(wanted && !downloaded_watermark_models_present(downloaded_models_folder()));
    }
  };
  connect(watermark_text_edit, &QLineEdit::textChanged, &dialog, [refresh_watermark_text] { refresh_watermark_text(); });
  connect(watermark_save_combo, &QComboBox::currentIndexChanged, &dialog,
          [refresh_watermark_text] { refresh_watermark_text(); });
  connect(watermark_check_open, &QCheckBox::toggled, &dialog, [refresh_watermark_text] { refresh_watermark_text(); });
  refresh_watermark_text();
  watermarks_layout->addStretch(1);
  add_page(watermarks_page, tr("Metadata and Watermarks"), QStringLiteral("metadata"));

  auto [pen_page, pen_layout] = make_tab_page(page_stack);
  auto* pen_group = new QFrame(pen_page);
  pen_group->setObjectName(QStringLiteral("preferencesPenGroup"));
  configure_panel(pen_group);
  auto* pen_form = new QFormLayout(pen_group);
  configure_form(pen_form);

  auto* pen_enabled_check = new QCheckBox(tr("Enable pen and tablet input"), pen_group);
  pen_enabled_check->setObjectName(QStringLiteral("preferencesPenEnabledCheck"));
  pen_enabled_check->setChecked(pen_input_settings_.enabled);
  auto* pen_pressure_size_check = new QCheckBox(tr("Pressure controls brush size"), pen_group);
  pen_pressure_size_check->setObjectName(QStringLiteral("preferencesPenPressureSizeCheck"));
  pen_pressure_size_check->setChecked(pen_input_settings_.pressure_size);
  auto* pen_pressure_size_min_spin = new QSpinBox(pen_group);
  pen_pressure_size_min_spin->setObjectName(QStringLiteral("preferencesPenPressureSizeMinSpin"));
  pen_pressure_size_min_spin->setRange(1, 100);
  pen_pressure_size_min_spin->setSuffix(percent_suffix());
  pen_pressure_size_min_spin->setValue(pen_input_settings_.pressure_size_min_percent);
  auto* pen_pressure_opacity_check = new QCheckBox(tr("Pressure controls opacity"), pen_group);
  pen_pressure_opacity_check->setObjectName(QStringLiteral("preferencesPenPressureOpacityCheck"));
  pen_pressure_opacity_check->setChecked(pen_input_settings_.pressure_opacity);
  auto* pen_pressure_opacity_min_spin = new QSpinBox(pen_group);
  pen_pressure_opacity_min_spin->setObjectName(QStringLiteral("preferencesPenPressureOpacityMinSpin"));
  pen_pressure_opacity_min_spin->setRange(1, 100);
  pen_pressure_opacity_min_spin->setSuffix(percent_suffix());
  pen_pressure_opacity_min_spin->setValue(pen_input_settings_.pressure_opacity_min_percent);
  auto* pen_eraser_check = new QCheckBox(tr("Use eraser tip as Eraser"), pen_group);
  pen_eraser_check->setObjectName(QStringLiteral("preferencesPenEraserTipCheck"));
  pen_eraser_check->setChecked(pen_input_settings_.use_eraser_tip);
  const auto populate_pen_button_combo = [](QComboBox* combo, PenButtonAction current) {
    const std::array<std::pair<PenButtonAction, QString>, 11> entries{{
        {PenButtonAction::None, tr("None")},
        {PenButtonAction::PanCanvas, tr("Pan canvas")},
        {PenButtonAction::ZoomCanvas, tr("Zoom canvas (drag)")},
        {PenButtonAction::PickColor, tr("Pick color")},
        {PenButtonAction::SetCloneSource, tr("Set clone source")},
        {PenButtonAction::SwapColors, tr("Swap colors")},
        {PenButtonAction::Undo, tr("Undo")},
        {PenButtonAction::Redo, tr("Redo")},
        {PenButtonAction::ToggleEraser, tr("Toggle eraser")},
        {PenButtonAction::IncreaseBrushSize, tr("Increase brush size")},
        {PenButtonAction::DecreaseBrushSize, tr("Decrease brush size")},
    }};
    for (const auto& [action, label] : entries) {
      combo->addItem(label, static_cast<int>(action));
    }
    const auto index = combo->findData(static_cast<int>(current));
    combo->setCurrentIndex(index >= 0 ? index : 0);
  };
  auto* pen_primary_button_combo = new QComboBox(pen_group);
  pen_primary_button_combo->setObjectName(QStringLiteral("preferencesPenPrimaryButtonCombo"));
  populate_pen_button_combo(pen_primary_button_combo, pen_input_settings_.primary_button_action);
  auto* pen_secondary_button_combo = new QComboBox(pen_group);
  pen_secondary_button_combo->setObjectName(QStringLiteral("preferencesPenSecondaryButtonCombo"));
  populate_pen_button_combo(pen_secondary_button_combo, pen_input_settings_.secondary_button_action);
  auto* pen_tilt_shape_check = new QCheckBox(tr("Tilt shapes brush dabs"), pen_group);
  pen_tilt_shape_check->setObjectName(QStringLiteral("preferencesPenTiltShapeCheck"));
  pen_tilt_shape_check->setChecked(pen_input_settings_.tilt_shape);
  auto* pen_tilt_roundness_spin = new QSpinBox(pen_group);
  pen_tilt_roundness_spin->setObjectName(QStringLiteral("preferencesPenTiltMinRoundnessSpin"));
  pen_tilt_roundness_spin->setRange(1, 100);
  pen_tilt_roundness_spin->setSuffix(percent_suffix());
  pen_tilt_roundness_spin->setValue(pen_input_settings_.tilt_min_roundness_percent);

  const auto refresh_pen_controls = [=] {
    const auto pen_enabled = pen_enabled_check->isChecked();
    pen_pressure_size_check->setEnabled(pen_enabled);
    pen_pressure_size_min_spin->setEnabled(pen_enabled && pen_pressure_size_check->isChecked());
    pen_pressure_opacity_check->setEnabled(pen_enabled);
    pen_pressure_opacity_min_spin->setEnabled(pen_enabled && pen_pressure_opacity_check->isChecked());
    pen_eraser_check->setEnabled(pen_enabled);
    pen_primary_button_combo->setEnabled(pen_enabled);
    pen_secondary_button_combo->setEnabled(pen_enabled);
    pen_tilt_shape_check->setEnabled(pen_enabled);
    pen_tilt_roundness_spin->setEnabled(pen_enabled && pen_tilt_shape_check->isChecked());
  };
  connect(pen_enabled_check, &QCheckBox::toggled, &dialog,
          [refresh_pen_controls](bool) { refresh_pen_controls(); });
  connect(pen_pressure_size_check, &QCheckBox::toggled, &dialog,
          [refresh_pen_controls](bool) { refresh_pen_controls(); });
  connect(pen_pressure_opacity_check, &QCheckBox::toggled, &dialog,
          [refresh_pen_controls](bool) { refresh_pen_controls(); });
  connect(pen_tilt_shape_check, &QCheckBox::toggled, &dialog,
          [refresh_pen_controls](bool) { refresh_pen_controls(); });
  refresh_pen_controls();

  pen_form->addRow(pen_enabled_check);
  pen_form->addRow(pen_pressure_size_check);
  pen_form->addRow(tr("Minimum size:"), pen_pressure_size_min_spin);
  pen_form->addRow(pen_pressure_opacity_check);
  pen_form->addRow(tr("Minimum opacity:"), pen_pressure_opacity_min_spin);
  auto* pen_pad_hint_label = new QLabel(
      tr("Set the pen buttons to Right Mouse Click and Middle Mouse Click in your tablet driver: while the "
         "pen is over the canvas, a right click triggers the Upper action and a middle click the Lower one. "
         "Buttons set to Scroll or Pan are handled by the driver and cannot trigger these actions. Tablet pad "
         "buttons (express keys) are also driver-only: map them to keyboard shortcuts such as Undo, Redo, "
         "[ and ] for brush size, and E for the eraser."),
      pen_group);
  pen_pad_hint_label->setObjectName(QStringLiteral("preferencesPenPadButtonsHint"));
  pen_pad_hint_label->setWordWrap(true);
  pen_pad_hint_label->setEnabled(false);

  pen_form->addRow(pen_eraser_check);
  pen_form->addRow(tr("Upper pen button:"), pen_primary_button_combo);
  pen_form->addRow(tr("Lower pen button:"), pen_secondary_button_combo);
  pen_form->addRow(pen_pad_hint_label);
  pen_form->addRow(pen_tilt_shape_check);
  pen_form->addRow(tr("Minimum tilt roundness:"), pen_tilt_roundness_spin);
  pen_layout->addWidget(pen_group);
  pen_layout->addStretch(1);
  add_page(pen_page, tr("Pen"), QStringLiteral("pen"));

  auto [view_page, view_layout] = make_tab_page(page_stack);
  auto* view_group = new QFrame(view_page);
  view_group->setObjectName(QStringLiteral("preferencesCanvasAidsGroup"));
  configure_panel(view_group);
  auto* view_form = new QFormLayout(view_group);
  configure_form(view_form);

  auto* ruler_units_combo = new QComboBox(view_group);
  ruler_units_combo->setObjectName(QStringLiteral("preferencesRulerUnitsCombo"));
  for (const auto ruler_unit_option :
       {MeasurementUnit::Pixels, MeasurementUnit::Inches, MeasurementUnit::Centimeters,
        MeasurementUnit::Millimeters, MeasurementUnit::Points, MeasurementUnit::Percent}) {
    ruler_units_combo->addItem(measurement_unit_name(ruler_unit_option),
                               measurement_unit_settings_token(ruler_unit_option));
  }
  const auto ruler_units_index =
      ruler_units_combo->findData(measurement_unit_settings_token(ruler_unit_));
  ruler_units_combo->setCurrentIndex(ruler_units_index >= 0 ? ruler_units_index : 0);

  auto* default_rulers_check = new QCheckBox(tr("Show rulers"), view_group);
  default_rulers_check->setObjectName(QStringLiteral("preferencesShowRulersCheck"));
  default_rulers_check->setChecked(view_rulers_visible_);
  auto* default_grid_check = new QCheckBox(tr("Show grid"), view_group);
  default_grid_check->setObjectName(QStringLiteral("preferencesShowGridCheck"));
  default_grid_check->setChecked(view_grid_visible_);
  auto* default_guides_check = new QCheckBox(tr("Show guides"), view_group);
  default_guides_check->setObjectName(QStringLiteral("preferencesShowGuidesCheck"));
  default_guides_check->setChecked(view_guides_visible_);
  auto* lock_guides_check = new QCheckBox(tr("Lock guides"), view_group);
  lock_guides_check->setObjectName(QStringLiteral("preferencesLockGuidesCheck"));
  lock_guides_check->setChecked(view_guides_locked_);
  auto* snap_check = new QCheckBox(tr("Enable snapping"), view_group);
  snap_check->setObjectName(QStringLiteral("preferencesSnapCheck"));
  snap_check->setChecked(view_snap_enabled_);

  auto* grid_spacing_spin = new UnitSpinBox(SpinUnit::Pixels, view_group);
  grid_spacing_spin->setObjectName(QStringLiteral("preferencesGridSpacingSpin"));
  grid_spacing_spin->set_context_provider(document_unit_context_provider(true));
  grid_spacing_spin->setRange(0.03125, 10000.0);
  grid_spacing_spin->setDecimals(3);
  grid_spacing_spin->setValue(static_cast<double>(view_grid_spacing_32_) / 32.0);
  auto* grid_subdivisions_spin = new QSpinBox(view_group);
  grid_subdivisions_spin->setObjectName(QStringLiteral("preferencesGridSubdivisionsSpin"));
  grid_subdivisions_spin->setRange(1, 64);
  grid_subdivisions_spin->setValue(view_grid_subdivisions_);
  auto* grid_style_combo = new QComboBox(view_group);
  grid_style_combo->setObjectName(QStringLiteral("preferencesGridStyleCombo"));
  grid_style_combo->addItem(tr("Lines"), 0);
  grid_style_combo->addItem(tr("Dots"), 1);
  grid_style_combo->setCurrentIndex(std::clamp(view_grid_style_, 0, 1));

  auto selected_grid_color = view_grid_color_;
  auto selected_guide_color = view_guide_color_;
  auto* grid_color_button = new QPushButton(view_group);
  grid_color_button->setObjectName(QStringLiteral("preferencesGridColorButton"));
  auto* guide_color_button = new QPushButton(view_group);
  guide_color_button->setObjectName(QStringLiteral("preferencesGuideColorButton"));
  // The picker chooses the opaque color; these own the alpha the grid and guide renderers read.
  const auto make_opacity_spin = [view_group](const QString& object_name, QColor color) {
    auto* spin = new QSpinBox(view_group);
    spin->setObjectName(object_name);
    spin->setRange(0, 100);
    spin->setSuffix(percent_suffix());
    spin->setValue(color_alpha_percent(color));
    return spin;
  };
  auto* grid_opacity_spin = make_opacity_spin(QStringLiteral("preferencesGridOpacitySpin"), selected_grid_color);
  auto* guide_opacity_spin = make_opacity_spin(QStringLiteral("preferencesGuideOpacitySpin"), selected_guide_color);
  const auto refresh_color_choice_button = [](QPushButton* button, QColor color) {
    button->setText(overlay_color_summary_text(color));
    button->setIcon(overlay_color_swatch_icon(color));
    button->setIconSize(QSize(48, 24));
    button->setToolTip(overlay_color_summary_text(color));
    button->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
  };
  auto* overlay_preview = new QLabel(view_group);
  overlay_preview->setObjectName(QStringLiteral("preferencesGridOverlayPreview"));
  overlay_preview->setAlignment(Qt::AlignCenter);
  overlay_preview->setFixedSize(218, 86);
  const auto refresh_overlay_preview = [&] {
    overlay_preview->setPixmap(grid_overlay_preview_pixmap(selected_grid_color, selected_guide_color,
                                                           grid_style_combo->currentData().toInt(),
                                                           grid_subdivisions_spin->value()));
  };
  refresh_color_choice_button(grid_color_button, selected_grid_color);
  refresh_color_choice_button(guide_color_button, selected_guide_color);
  refresh_overlay_preview();
  // Patchy's own picker (palettes, names, hex) chooses the opaque color and previews it
  // live; the opacity spin beside each button owns the alpha, so a picked color keeps the
  // current opacity and a cancel puts the previous color back.
  const auto choose_overlay_color = [&](QPushButton* button, QColor& selected, const QString& title) {
    const auto original = selected;
    const auto apply = [&, button](QColor color) {
      color.setAlpha(original.alpha());
      selected = color;
      refresh_color_choice_button(button, selected);
      refresh_overlay_preview();
    };
    const auto chosen = request_patchy_color(&dialog, original, title, apply);
    apply(chosen.value_or(original));
  };
  connect(grid_color_button, &QPushButton::clicked, &dialog,
          [&] { choose_overlay_color(grid_color_button, selected_grid_color, tr("Grid Color")); });
  connect(guide_color_button, &QPushButton::clicked, &dialog,
          [&] { choose_overlay_color(guide_color_button, selected_guide_color, tr("Guide Color")); });
  const auto apply_opacity = [&](QColor& selected, QPushButton* button, int percent) {
    selected.setAlphaF(std::clamp(percent, 0, 100) / 100.0);
    refresh_color_choice_button(button, selected);
    refresh_overlay_preview();
  };
  connect(grid_opacity_spin, &QSpinBox::valueChanged, &dialog,
          [&](int percent) { apply_opacity(selected_grid_color, grid_color_button, percent); });
  connect(guide_opacity_spin, &QSpinBox::valueChanged, &dialog,
          [&](int percent) { apply_opacity(selected_guide_color, guide_color_button, percent); });
  connect(grid_subdivisions_spin, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
          [&](int) { refresh_overlay_preview(); });
  connect(grid_style_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
          [&](int) { refresh_overlay_preview(); });

  auto* snap_guides_check = new QCheckBox(tr("Guides"), &dialog);
  snap_guides_check->setObjectName(QStringLiteral("preferencesSnapGuidesCheck"));
  snap_guides_check->setChecked(view_snap_to_guides_);
  auto* snap_grid_check = new QCheckBox(tr("Grid"), &dialog);
  snap_grid_check->setObjectName(QStringLiteral("preferencesSnapGridCheck"));
  snap_grid_check->setChecked(view_snap_to_grid_);
  auto* snap_document_check = new QCheckBox(tr("Document bounds and center"), &dialog);
  snap_document_check->setObjectName(QStringLiteral("preferencesSnapDocumentCheck"));
  snap_document_check->setChecked(view_snap_to_document_);
  auto* snap_layers_check = new QCheckBox(tr("Layer bounds and centers"), &dialog);
  snap_layers_check->setObjectName(QStringLiteral("preferencesSnapLayersCheck"));
  snap_layers_check->setChecked(view_snap_to_layers_);
  auto* snap_selection_check = new QCheckBox(tr("Selection bounds and center"), &dialog);
  snap_selection_check->setObjectName(QStringLiteral("preferencesSnapSelectionCheck"));
  snap_selection_check->setChecked(view_snap_to_selection_);

  auto* visibility_row = new QWidget(view_group);
  visibility_row->setObjectName(QStringLiteral("preferencesVisibilityRow"));
  auto* visibility_layout = new QGridLayout(visibility_row);
  visibility_layout->setContentsMargins(0, 0, 0, 0);
  visibility_layout->setHorizontalSpacing(18);
  visibility_layout->setVerticalSpacing(6);
  visibility_layout->addWidget(default_rulers_check, 0, 0);
  visibility_layout->addWidget(default_grid_check, 0, 1);
  visibility_layout->addWidget(default_guides_check, 1, 0);
  visibility_layout->addWidget(lock_guides_check, 1, 1);
  auto* snap_targets_row = new QWidget(&dialog);
  snap_targets_row->setObjectName(QStringLiteral("preferencesSnapTargetsRow"));
  auto* snap_targets_layout = new QGridLayout(snap_targets_row);
  snap_targets_layout->setContentsMargins(0, 0, 0, 0);
  snap_targets_layout->setHorizontalSpacing(18);
  snap_targets_layout->setVerticalSpacing(6);
  snap_targets_layout->addWidget(snap_guides_check, 0, 0);
  snap_targets_layout->addWidget(snap_grid_check, 0, 1);
  snap_targets_layout->addWidget(snap_document_check, 1, 0, 1, 2);
  snap_targets_layout->addWidget(snap_layers_check, 2, 0, 1, 2);
  snap_targets_layout->addWidget(snap_selection_check, 3, 0, 1, 2);

  view_form->addRow(tr("Default units:"), ruler_units_combo);
  view_form->addRow(tr("Default visibility:"), visibility_row);
  view_form->addRow(tr("Grid spacing:"), grid_spacing_spin);
  view_form->addRow(tr("Grid subdivisions:"), grid_subdivisions_spin);
  view_form->addRow(tr("Grid style:"), grid_style_combo);
  const auto overlay_color_row = [view_group](QPushButton* button, QSpinBox* opacity) {
    auto* row = new QWidget(view_group);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(button, 1);
    layout->addWidget(opacity);
    return row;
  };
  view_form->addRow(tr("Grid color:"), overlay_color_row(grid_color_button, grid_opacity_spin));
  view_form->addRow(tr("Guide color:"), overlay_color_row(guide_color_button, guide_opacity_spin));
  view_form->addRow(tr("Overlay preview:"), overlay_preview);
  view_layout->addWidget(view_group);
  view_layout->addStretch(1);
  add_page(view_page, tr("Units && Grids"), QStringLiteral("unitsAndGrids"));

  auto [snapping_page, snapping_layout] = make_tab_page(page_stack);
  auto* snapping_group = new QFrame(snapping_page);
  snapping_group->setObjectName(QStringLiteral("preferencesSnappingGroup"));
  configure_panel(snapping_group);
  auto* snapping_form = new QFormLayout(snapping_group);
  configure_form(snapping_form);
  snapping_form->addRow(tr("Snap:"), snap_check);
  snapping_form->addRow(tr("Snap targets:"), snap_targets_row);
  snapping_layout->addWidget(snapping_group);
  snapping_layout->addStretch(1);
  add_page(snapping_page, tr("Snapping"), QStringLiteral("snapping"));

  auto [hotkeys_page, hotkeys_layout] = make_tab_page(page_stack);
  hotkeys_layout->addStretch(1);
  const int hotkeys_tab_index = add_page(hotkeys_page, tr("Hotkeys"), QStringLiteral("hotkeys"));

  // Building ~150 hotkey rows is the single most expensive part of opening this
  // dialog, so the panel is built on the first visit to its page; most
  // Preferences opens never switch to it. Built on demand, it is also created
  // after the dialog stylesheet below is set, so Qt never repolishes its
  // several-hundred-widget subtree (the September 2026 Preferences-open
  // slowdown). The page itself stays at this position: tests and the Windows
  // Plug-ins page after it depend on the order.
  HotkeyEditorPanel* hotkey_editor = nullptr;
  connect(page_stack, &QStackedWidget::currentChanged, &dialog,
          [this, hotkeys_page, hotkeys_layout, hotkeys_tab_index, &hotkey_editor](int index) {
            if (index != hotkeys_tab_index || hotkey_editor != nullptr) {
              return;
            }
            // A script added since startup gets its row too.
            refresh_script_commands(scan_scripts(bundled_scripts_directory(), user_scripts_directory()));
            hotkey_editor = new HotkeyEditorPanel(hotkey_registry_, menuBar(), hotkeys_page);
            hotkeys_layout->insertWidget(0, hotkey_editor);
          });
#ifdef Q_OS_WIN
  // Plug-ins: the folders scanned for legacy Photoshop .8bf filters (Windows
  // only, where they can run). The two automatic folders are fixed; the list
  // holds the user-added ones (docs/plugins.md). Scanning never runs a plug-in.
  auto [plugins_page, plugins_layout] = make_tab_page(page_stack);
  auto* plugins_group = new QFrame(plugins_page);
  plugins_group->setObjectName(QStringLiteral("preferencesPluginsGroup"));
  configure_panel(plugins_group);
  auto* plugins_form = new QFormLayout(plugins_group);
  configure_form(plugins_form);
  auto* plugins_intro = new QLabel(
      tr("Photoshop filter plug-ins (.8bf, 32-bit or 64-bit) are found in these folders and their "
         "subfolders and listed under Plugins > Legacy Photoshop Plug-ins. Only run plug-ins you trust: "
         "they execute with your permissions."),
      plugins_group);
  plugins_intro->setWordWrap(true);
  plugins_intro->setObjectName(QStringLiteral("preferencesPluginsIntro"));
  plugins_form->addRow(plugins_intro);
  auto* plugins_auto_label = new QLabel(plugins_group);
  plugins_auto_label->setObjectName(QStringLiteral("preferencesPluginsAutomaticFolders"));
  plugins_auto_label->setWordWrap(true);
  plugins_auto_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  {
    // The scan roots minus the user list (and the developer fixture folder).
    QStringList fixed;
    const auto user_folders = stored_legacy_plugin_folders();
    for (const auto& scan_root : legacy_plugin_scan_roots()) {
      if (!user_folders.contains(scan_root) &&
          !scan_root.endsWith(QStringLiteral("test-fixtures/photoshop-plugins"))) {
        fixed << QDir::toNativeSeparators(scan_root);
      }
    }
    plugins_auto_label->setText(fixed.join(QLatin1Char('\n')));
  }
  plugins_form->addRow(tr("Always scanned:"), plugins_auto_label);
  auto* plugin_folders_list = new QListWidget(plugins_group);
  plugin_folders_list->setObjectName(QStringLiteral("preferencesPluginFoldersList"));
  plugin_folders_list->setSelectionMode(QAbstractItemView::SingleSelection);
  const auto entry_plugin_folders = stored_legacy_plugin_folders();
  for (const auto& folder : entry_plugin_folders) {
    plugin_folders_list->addItem(QDir::toNativeSeparators(folder));
  }
  auto* plugin_folder_buttons = new QWidget(plugins_group);
  auto* plugin_folder_buttons_layout = new QHBoxLayout(plugin_folder_buttons);
  plugin_folder_buttons_layout->setContentsMargins(0, 0, 0, 0);
  auto* add_plugin_folder = new QPushButton(tr("Add Folder..."), plugin_folder_buttons);
  add_plugin_folder->setObjectName(QStringLiteral("preferencesAddPluginFolderButton"));
  auto* remove_plugin_folder = new QPushButton(tr("Remove"), plugin_folder_buttons);
  remove_plugin_folder->setObjectName(QStringLiteral("preferencesRemovePluginFolderButton"));
  remove_plugin_folder->setEnabled(false);
  plugin_folder_buttons_layout->addWidget(add_plugin_folder);
  plugin_folder_buttons_layout->addWidget(remove_plugin_folder);
  plugin_folder_buttons_layout->addStretch(1);
  connect(plugin_folders_list, &QListWidget::itemSelectionChanged, &dialog,
          [plugin_folders_list, remove_plugin_folder] {
            remove_plugin_folder->setEnabled(!plugin_folders_list->selectedItems().isEmpty());
          });
  connect(add_plugin_folder, &QPushButton::clicked, &dialog, [&dialog, plugin_folders_list] {
    const auto chosen = QFileDialog::getExistingDirectory(&dialog, tr("Add Plug-in Folder"), QString());
    if (chosen.isEmpty()) {
      return;
    }
    const auto native = QDir::toNativeSeparators(chosen);
    for (int row = 0; row < plugin_folders_list->count(); ++row) {
      if (plugin_folders_list->item(row)->text() == native) {
        return;
      }
    }
    plugin_folders_list->addItem(native);
  });
  connect(remove_plugin_folder, &QPushButton::clicked, &dialog, [plugin_folders_list] {
    qDeleteAll(plugin_folders_list->selectedItems());
  });
  plugins_form->addRow(tr("Added folders:"), plugin_folders_list);
  plugins_form->addRow(QString(), plugin_folder_buttons);
  // The virtual screen (docs/plugins.md): plug-in windows open on the monitor
  // showing Patchy; full-screen plug-in interfaces size themselves to this.
  auto* plugin_screen_combo = new QComboBox(plugins_group);
  plugin_screen_combo->setObjectName(QStringLiteral("preferencesPluginScreenSizeCombo"));
  for (const auto& [screen_width, screen_height] : kLegacyPluginScreenSizes) {
    plugin_screen_combo->addItem(screen_width == 0 ? tr("Whole monitor")
                                                   : QStringLiteral("%1 x %2").arg(screen_width).arg(screen_height),
                                 QSize(screen_width, screen_height));
  }
  const auto entry_plugin_screen = stored_legacy_plugin_screen_size();
  plugin_screen_combo->setCurrentIndex(
      plugin_screen_combo->findData(QSize(entry_plugin_screen.first, entry_plugin_screen.second)));
  plugin_screen_combo->setToolTip(
      tr("Plug-in windows open on the monitor showing Patchy. Plug-ins with full-screen interfaces size "
         "themselves to this screen size, so a smaller size keeps them usable on large monitors."));
  plugins_form->addRow(tr("Screen size for plug-in windows:"), plugin_screen_combo);
  plugins_layout->addWidget(plugins_group);
  plugins_layout->addStretch(1);
  add_page(plugins_page, tr("Plug-ins"), QStringLiteral("plugins"));
#endif

  auto* pages_row = new QHBoxLayout();
  pages_row->setContentsMargins(0, 0, 0, 0);
  pages_row->setSpacing(8);
  pages_row->addWidget(page_list);
  pages_row->addWidget(page_stack, 1);
  content->addLayout(pages_row, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
  buttons->setObjectName(QStringLiteral("preferencesButtonBox"));
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  // Frameless chrome has no native resize border; the corner grip is the resize handle
  // (GitHub issue 92).
  auto* button_row = new QHBoxLayout();
  button_row->setContentsMargins(0, 0, 0, 0);
  button_row->addWidget(buttons, 1);
  auto* size_grip = new VisibleSizeGrip(&dialog);
  size_grip->setObjectName(QStringLiteral("preferencesSizeGrip"));
  button_row->addWidget(size_grip, 0, Qt::AlignBottom);
  content->addLayout(button_row);

  // Applied after every child widget exists: Qt does not reliably pick up
  // sub-control rules (QSpinBox::up-button) for widgets created on hidden
  // pages after the stylesheet was set. The Hotkeys panel is the one
  // exception, built on demand above: it has no spin box and none of the IDs
  // below, and creating it after this sheet is what keeps its rows from being
  // repolished.
  append_themed_style(dialog, QStringLiteral(R"(
    QDialog#patchyPreferencesDialog QStackedWidget#preferencesPageStack {
      border: 1px solid @dialog_tab_border;
      background: @dialog_tab_bg;
    }
    QDialog#patchyPreferencesDialog QListWidget#preferencesPageList {
      background: @dialog_tab_bg;
      border: 1px solid @dialog_tab_border;
      color: @dialog_tab_text;
      outline: 0;
    }
    QDialog#patchyPreferencesDialog QListWidget#preferencesPageList::item {
      min-height: 0;
      padding: 6px 16px 6px 10px;
      border: 0;
      border-left: 3px solid transparent;
      color: @dialog_tab_text;
    }
    QDialog#patchyPreferencesDialog QListWidget#preferencesPageList::item:hover:!selected {
      background: @dialog_tab_hover_bg;
    }
    QDialog#patchyPreferencesDialog QListWidget#preferencesPageList::item:selected {
      background: @dialog_tab_selected_bg;
      color: @text_on_raised;
      border: 0;
      border-left: 3px solid @accent;
    }
    QDialog#patchyPreferencesDialog QFrame[preferencesPanel="true"] {
      background: @panel_card_bg;
      border: 1px solid @panel_card_border;
      border-radius: 4px;
    }
    QDialog#patchyPreferencesDialog QPushButton#preferencesGridColorButton,
    QDialog#patchyPreferencesDialog QPushButton#preferencesGuideColorButton {
      background: @button_bg;
      border: 1px solid @color_button_border;
      border-radius: 3px;
      color: @text_bright;
      min-height: 30px;
      min-width: 158px;
      padding: 3px 9px;
      text-align: left;
    }
    QDialog#patchyPreferencesDialog QPushButton#preferencesGridColorButton:hover,
    QDialog#patchyPreferencesDialog QPushButton#preferencesGuideColorButton:hover {
      border-color: @color_button_hover_border;
      background: @color_button_hover_bg;
    }
    QDialog#patchyPreferencesDialog QLabel#preferencesGridOverlayPreview {
      background: @grid_preview_bg;
      border: 1px solid @grid_preview_border;
      padding: 0;
    }
    QDialog#patchyPreferencesDialog QWidget#preferencesTabHost,
    QDialog#patchyPreferencesDialog QScrollArea#preferencesTabScroll,
    QDialog#patchyPreferencesDialog QWidget#preferencesTabPage {
      background: transparent;
    }
    /* Plain-QWidget field containers inherit the global QWidget background, which
       shows as a mismatched band against the panel_card_bg panels. */
    QDialog#patchyPreferencesDialog QWidget#preferencesInterfaceScaleRow,
    QDialog#patchyPreferencesDialog QWidget#preferencesCustomizeToolPaletteRow,
    QDialog#patchyPreferencesDialog QWidget#preferencesUpdateCheckRow,
    QDialog#patchyPreferencesDialog QWidget#preferencesRecoveryRow,
    QDialog#patchyPreferencesDialog QWidget#preferencesVisibilityRow,
    QDialog#patchyPreferencesDialog QWidget#preferencesSnapTargetsRow,
    QDialog#patchyPreferencesDialog QWidget#preferencesWatermarkTextRow,
    QDialog#patchyPreferencesDialog QWidget#preferencesSavedMetadataModesRow,
    QDialog#patchyPreferencesDialog QWidget#preferencesSavedMetadataCopyrightRow {
      background: transparent;
    }
    QDialog#patchyPreferencesDialog QWidget#preferencesSavedMetadataModesRow QRadioButton {
      background: transparent;
      spacing: 7px;
    }
    QDialog#patchyPreferencesDialog QWidget#preferencesSavedMetadataModesRow QRadioButton::indicator {
      width: 11px;
      height: 11px;
      border-radius: 6px;
      background: @dlg_grid_bg;
      border: 1px solid @dlg_grid_border;
    }
    QDialog#patchyPreferencesDialog QWidget#preferencesSavedMetadataModesRow QRadioButton::indicator:checked {
      background: @accent_pressed_bg;
      border-color: @dlg_focus_border;
    }
  )"));
  // Appended last, after the block above: the spin sub-control rules must be the
  // final word (see docs/ui-conventions.md).
  append_themed_style(dialog, dialog_spinbox_button_style());

  // The list is as wide as its longest title in the current language, measured with
  // the sheet above applied (row padding included), and no wider.
  page_list->ensurePolished();
  page_list->setFixedWidth(page_list->sizeHintForColumn(0) + 2 * page_list->frameWidth() + 2);

  // The page to open on: Hotkeys when asked for, else the page the dialog was last
  // closed on. Selected after the sheet so a Hotkeys panel built here is never repolished.
  int start_row = 0;
  if (page == PreferencesPage::Hotkeys) {
    start_row = hotkeys_tab_index;
  } else if (page == PreferencesPage::Last) {
    const auto last_page = stored_preferences_last_page();
    for (int row = 0; row < page_list->count(); ++row) {
      if (page_list->item(row)->data(Qt::UserRole).toString() == last_page) {
        start_row = row;
      }
    }
  }
  page_list->setCurrentRow(start_row);  // builds the Hotkeys panel through the connection above
  if (page == PreferencesPage::Hotkeys && hotkey_editor != nullptr) {
    hotkey_editor->set_search_text(hotkey_search);
  }
  page_list->setFocus();

  // The pages scroll only vertically, so the page area is as wide as the widest page's
  // content plus a scroll bar; the scroll areas themselves report almost no minimum.
  int page_content_width = 0;
  for (const auto* scroll : page_stack->findChildren<QScrollArea*>(QStringLiteral("preferencesTabScroll"))) {
    if (const auto* content_widget = scroll->widget(); content_widget != nullptr) {
      content_widget->ensurePolished();  // hidden pages: measure with the sheet's paddings
      page_content_width = std::max(page_content_width, content_widget->minimumSizeHint().width() +
                                                            scroll->verticalScrollBar()->sizeHint().width());
    }
  }
  page_stack->setMinimumWidth(page_content_width + 2 * page_stack->frameWidth());

  // Wide enough for the list and every page's content: the explicit minimum size above
  // keeps the layout from raising the dialog's minimum itself, so take the layout's
  // minimum width by hand.
  root->activate();
  const int needed_width = root->totalMinimumSize().width();
  if (needed_width > dialog.minimumWidth()) {
    dialog.setMinimumWidth(needed_width);
  }
  if (dialog.width() < needed_width) {
    dialog.resize(needed_width, dialog.height());
  }
  // The size the user last dragged the dialog to, kept inside the screen it opens on.
  if (const auto saved_size = stored_preferences_dialog_size(); saved_size.isValid()) {
    auto restored_size = saved_size;
    if (const auto* owner_screen = screen(); owner_screen != nullptr) {
      restored_size = restored_size.boundedTo(owner_screen->availableGeometry().size());
    }
    dialog.resize(restored_size.expandedTo(dialog.minimumSize()));
  }
  const auto entry_size = dialog.size();

  install_wheel_scroll_guard(dialog);

  const int dialog_result = exec_dialog(dialog);
  if (dialog.size() != entry_size) {
    set_stored_preferences_dialog_size(dialog.size());
  }
  if (const auto* current_page = page_list->currentItem(); current_page != nullptr) {
    set_stored_preferences_last_page(current_page->data(Qt::UserRole).toString());
  }
  if (dialog_result == QDialog::Accepted) {
    if (const auto code = language_combo->currentData().toString(); !code.isEmpty()) {
      LocalizationManager::instance().set_language(code);
    }
    if (hotkey_editor != nullptr) {
      hotkey_editor->commit();
    }
    themes_page.commit();
    const auto new_grid_spacing_32 =
        std::clamp(static_cast<int>(std::lround(grid_spacing_spin->value() * 32.0)), 1, 320000);
#ifndef Q_OS_WASM
    if (update_check != nullptr) {
      settings.setValue(QStringLiteral("updates/checkOnStartup"), update_check->isChecked());
      set_stored_update_check_interval_days(update_interval_combo->currentData().toInt());
    }
    set_stored_recovery_enabled(recovery_check->isChecked());
    set_stored_recovery_interval_minutes(recovery_combo->currentData().toInt());
    apply_recovery_preferences();
#endif
#ifdef Q_OS_WIN
    {
      QStringList plugin_folders;
      for (int row = 0; row < plugin_folders_list->count(); ++row) {
        plugin_folders << QDir::fromNativeSeparators(plugin_folders_list->item(row)->text());
      }
      if (plugin_folders != entry_plugin_folders) {
        set_stored_legacy_plugin_folders(plugin_folders);
        start_legacy_plugin_scan(true);
      }
      const auto chosen_screen = plugin_screen_combo->currentData().toSize();
      set_stored_legacy_plugin_screen_size({chosen_screen.width(), chosen_screen.height()});
    }
#endif
    if (const CmykWorkingSettings chosen_cmyk{cmyk_profile_combo->currentData().toString(),
                                              static_cast<RenderingIntent>(cmyk_intent_combo->currentData().toInt()),
                                              cmyk_black_point_check->isChecked()};
        chosen_cmyk != entry_cmyk_settings) {
      set_stored_cmyk_working_settings(chosen_cmyk);
      set_working_cmyk_space(cmyk_working_space_for(chosen_cmyk));
      refresh_color_picker_cmyk_fields();
      refresh_proof_display();
    }
    set_stored_watermark_text(watermark_text_edit->text().trimmed());
    set_stored_watermark_save_mode(
        watermark_save_mode_from_token(watermark_save_combo->currentData().toString()).value_or(WatermarkSaveMode::Never));
    set_stored_watermark_check_on_open(watermark_check_open->isChecked());
    set_stored_save_metadata_mode(metadata_none_radio->isChecked()       ? SaveMetadataMode::None
                                  : metadata_personal_radio->isChecked() ? SaveMetadataMode::Personal
                                                                         : SaveMetadataMode::Basic);
    set_stored_personal_save_metadata(PersonalSaveMetadata{metadata_name_edit->text(), metadata_copyright_edit->text(),
                                                           metadata_email_edit->text(), metadata_website_edit->text(),
                                                           metadata_ask_check->isChecked()});
    set_stored_show_file_info_privacy_notice(privacy_notice_check->isChecked());
    settings.setValue(QStringLiteral("imports/showPsdWarningsAndInfo"), psd_import_warnings_check->isChecked());
    settings.setValue(QStringLiteral("imports/showRawDevelopDialog"), raw_develop_check->isChecked());
    settings.setValue(QStringLiteral("imports/convertRgbProfilesToSrgb"), convert_rgb_profiles_check->isChecked());
    settings.setValue(QStringLiteral("newDocument/addEmptyLayer"), new_document_layer_check->isChecked());
    settings.setValue(QStringLiteral("imports/showMissingFontsDialog"), missing_fonts_check->isChecked());
    settings.setValue(QStringLiteral("imports/showMissingLinksDialog"), missing_links_check->isChecked());
    settings.setValue(QStringLiteral("imports/adoptIndexedPalette"), indexed_open_combo->currentData().toString());
    settings.setValue(QStringLiteral("imports/afImageLayers"), af_image_layers_combo->currentData().toString());
    settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), pdf_layers_combo->currentData().toString());
#if defined(Q_OS_LINUX) && !defined(Q_OS_WASM)
    if (system_title_bar_check->isChecked() != entry_system_title_bar) {
      set_stored_system_title_bar(system_title_bar_check->isChecked());
      show_information_message(this, tr("System Title Bar"),
                               tr("Restart Patchy for the title bar change to take effect."),
                               QStringLiteral("preferencesSystemTitleBarMessageBox"));
    }
#endif
    // Text the validator left short of the range clamps up, as its fixup would on Enter.
    const auto typed_gui_scale = parse_gui_scale_text(gui_scale_combo->currentText());
    const int selected_gui_scale =
        typed_gui_scale ? std::clamp(*typed_gui_scale, kMinGuiScalePercent, kMaxGuiScalePercent)
                        : entry_gui_scale;
    if (selected_gui_scale != entry_gui_scale) {
      set_stored_gui_scale_percent(selected_gui_scale);
      show_information_message(this, tr("Interface Scale"),
#ifdef Q_OS_WASM
                               tr("Reload the page for the new interface scale to take effect."),
#else
                               tr("Restart Patchy for the new interface scale to take effect."),
#endif
                               QStringLiteral("preferencesInterfaceScaleMessageBox"));
    }
    // Compared with what is stored now, not at entry: the customize dialog, opened from here,
    // may have changed both meanwhile (and moved these combos to match).
    apply_tool_palette_preferences(toolbar_icon_size_from_token(toolbar_icon_size_combo->currentData().toString()),
                                   tool_flyout_open_mode_from_token(tool_flyout_open_combo->currentData().toString()));
    set_ruler_unit_preference(measurement_unit_from_settings_token(
        ruler_units_combo->currentData().toString(), MeasurementUnit::Pixels));
    settings.setValue(QStringLiteral("view/rulerUnits"), ruler_units_combo->currentData().toString());
    pen_input_settings_.enabled = pen_enabled_check->isChecked();
    pen_input_settings_.pressure_size = pen_pressure_size_check->isChecked();
    pen_input_settings_.pressure_size_min_percent = pen_pressure_size_min_spin->value();
    pen_input_settings_.pressure_opacity = pen_pressure_opacity_check->isChecked();
    pen_input_settings_.pressure_opacity_min_percent = pen_pressure_opacity_min_spin->value();
    pen_input_settings_.use_eraser_tip = pen_eraser_check->isChecked();
    pen_input_settings_.primary_button_action =
        static_cast<PenButtonAction>(pen_primary_button_combo->currentData().toInt());
    pen_input_settings_.secondary_button_action =
        static_cast<PenButtonAction>(pen_secondary_button_combo->currentData().toInt());
    pen_input_settings_.tilt_shape = pen_tilt_shape_check->isChecked();
    pen_input_settings_.tilt_min_roundness_percent = pen_tilt_roundness_spin->value();
    wheel_zooms_ = wheel_zoom_check->isChecked();
    shift_keeps_transform_aspect_ = transform_shift_aspect_check->isChecked();
    show_transform_drag_values_ = transform_values_check->isChecked();
    snap_transforms_to_pixel_grid_ = transform_snap_check->isChecked();
    escape_commits_text_ = escape_commits_text_check->isChecked();
    if (zoom_layer_thumbnails_to_content_ != zoom_thumbnails_check->isChecked()) {
      zoom_layer_thumbnails_to_content_ = zoom_thumbnails_check->isChecked();
      // The mode is a shape input the revision-keyed cache does not track;
      // drop the pixmaps and rebuild (the color-scheme precedent).
      layer_thumbnail_cache_.clear();
      refresh_layer_list();
    }
    view_rulers_visible_ = default_rulers_check->isChecked();
    view_vector_preview_action_->setChecked(vector_preview_check->isChecked());
    view_grid_visible_ = default_grid_check->isChecked();
    view_guides_visible_ = default_guides_check->isChecked();
    view_guides_locked_ = lock_guides_check->isChecked();
    view_snap_enabled_ = snap_check->isChecked();
    view_snap_to_guides_ = snap_guides_check->isChecked();
    view_snap_to_grid_ = snap_grid_check->isChecked();
    view_snap_to_document_ = snap_document_check->isChecked();
    view_snap_to_layers_ = snap_layers_check->isChecked();
    view_snap_to_selection_ = snap_selection_check->isChecked();
    view_grid_spacing_32_ = new_grid_spacing_32;
    view_grid_subdivisions_ = grid_subdivisions_spin->value();
    view_grid_style_ = grid_style_combo->currentData().toInt();
    view_grid_color_ = selected_grid_color;
    view_guide_color_ = selected_guide_color;
    if (canvas_ != nullptr && (document().grid_settings().horizontal_cycle_32 != new_grid_spacing_32 ||
                               document().grid_settings().vertical_cycle_32 != new_grid_spacing_32)) {
      push_undo_snapshot(tr("Grid Preferences"));
      document().grid_settings().horizontal_cycle_32 = new_grid_spacing_32;
      document().grid_settings().vertical_cycle_32 = new_grid_spacing_32;
      canvas_->document_changed();
    }
    if (view_rulers_action_ != nullptr) {
      view_rulers_action_->setChecked(view_rulers_visible_);
    }
    if (view_grid_action_ != nullptr) {
      view_grid_action_->setChecked(view_grid_visible_);
    }
    if (view_guides_action_ != nullptr) {
      view_guides_action_->setChecked(view_guides_visible_);
    }
    if (view_snap_action_ != nullptr) {
      view_snap_action_->setChecked(view_snap_enabled_);
    }
    if (view_lock_guides_action_ != nullptr) {
      view_lock_guides_action_->setChecked(view_guides_locked_);
    }
    if (view_snap_guides_action_ != nullptr) {
      view_snap_guides_action_->setChecked(view_snap_to_guides_);
    }
    if (view_snap_grid_action_ != nullptr) {
      view_snap_grid_action_->setChecked(view_snap_to_grid_);
    }
    if (view_snap_document_action_ != nullptr) {
      view_snap_document_action_->setChecked(view_snap_to_document_);
    }
    if (view_snap_layers_action_ != nullptr) {
      view_snap_layers_action_->setChecked(view_snap_to_layers_);
    }
    if (view_snap_selection_action_ != nullptr) {
      view_snap_selection_action_->setChecked(view_snap_to_selection_);
    }
    for (const auto& active_session : sessions_) {
      apply_canvas_aid_settings(active_session->canvas);
      apply_pen_input_settings(active_session->canvas);
    }
    save_pen_input_settings();
    save_view_settings();
  }
  if (pending_stress_preset.has_value()) {
    run_stress_test_interactive(*pending_stress_preset);
  }
}

void MainWindow::view_extras_options_dialog() {
  const auto chosen = request_view_extras_options(this, view_extras_covered_);
  if (!chosen.has_value()) {
    return;
  }
  view_extras_covered_ = *chosen;
  save_view_extras_covered(view_extras_covered_);
  for (const auto& active_session : sessions_) {
    apply_canvas_aid_settings(active_session->canvas);
  }
}

void MainWindow::new_guide_dialog() {
  if (canvas_ == nullptr) {
    return;
  }

  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("newGuideDialog"));
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, tr("New Guide"));
  auto* form = new QFormLayout();
  form->setContentsMargins(0, 0, 0, 0);
  form->setSpacing(8);

  auto* orientation_combo = new QComboBox(&dialog);
  orientation_combo->setObjectName(QStringLiteral("newGuideOrientationCombo"));
  orientation_combo->addItem(tr("Vertical"), static_cast<int>(GuideOrientation::Vertical));
  orientation_combo->addItem(tr("Horizontal"), static_cast<int>(GuideOrientation::Horizontal));
  auto* position_spin = new UnitSpinBox(SpinUnit::Pixels, &dialog);
  // Percent is relative to the document extent the guide runs across.
  position_spin->set_context_provider([this, orientation_combo] {
    const bool horizontal =
        orientation_combo->currentData().toInt() == static_cast<int>(GuideOrientation::Horizontal);
    return UnitConversionContext{sanitized_document_ppi(document().print_settings().horizontal_ppi),
                                 static_cast<double>(horizontal ? document().height() : document().width())};
  });
  position_spin->setObjectName(QStringLiteral("newGuidePositionSpin"));
  // Negative or past the canvas puts the guide on the pasteboard (GitHub issue 231).
  position_spin->setRange(-static_cast<double>(kMaxGuideOffsetPixels), static_cast<double>(kMaxGuideOffsetPixels));
  position_spin->setDecimals(3);
  position_spin->setValue(0.0);
  // Shown in the ruler unit, like the guide readout; enrolled for the dialog's
  // lifetime so its unit menu changes the preference like every other field.
  register_ruler_unit_field(position_spin);
  form->addRow(tr("Orientation:"), orientation_combo);
  form->addRow(tr("Position:"), position_spin);
  content->addLayout(form);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("newGuideButtonBox"));
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  content->addWidget(buttons);
  append_themed_style(dialog, dialog_spinbox_button_style());

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return;
  }

  const auto orientation = static_cast<GuideOrientation>(orientation_combo->currentData().toInt());
  canvas_->add_guide(orientation, static_cast<std::int32_t>(std::lround(position_spin->value() * 32.0)));
}

namespace {

// Saved New Guide Layout presets (GitHub issue 403): a JSON array, oldest first. The key,
// the field names and the unit tokens are compatibility contracts; never rename them.
// Lengths keep the unit they were saved in (measurement_unit_settings_token), so a preset
// saved in inches or percent means the same on any document.
constexpr const char* kGuideLayoutUserPresetsKey = "view/guideLayoutUserPresets";
constexpr int kGuideLayoutMaxCount = 64;

struct GuideLayoutUserPreset {
  QString name;
  MeasurementUnit unit{MeasurementUnit::Pixels};
  int columns{0};
  int rows{0};
  double column_gutter{0.0};
  double row_gutter{0.0};
  double margin_top{0.0};
  double margin_left{0.0};
  double margin_bottom{0.0};
  double margin_right{0.0};
  bool clear_existing{false};
};

std::vector<GuideLayoutUserPreset> load_guide_layout_user_presets() {
  std::vector<GuideLayoutUserPreset> presets;
  QJsonParseError error;
  const auto json = QJsonDocument::fromJson(
      app_settings().value(QLatin1String(kGuideLayoutUserPresetsKey)).toByteArray(), &error);
  if (error.error != QJsonParseError::NoError || !json.isArray()) {
    return presets;
  }
  for (const auto& value : json.array()) {
    if (!value.isObject()) {
      continue;
    }
    const auto object = value.toObject();
    GuideLayoutUserPreset preset;
    preset.name = object.value(QStringLiteral("name")).toString().simplified();
    const bool duplicate = std::any_of(presets.begin(), presets.end(), [&preset](const GuideLayoutUserPreset& other) {
      return other.name.compare(preset.name, Qt::CaseInsensitive) == 0;
    });
    if (preset.name.isEmpty() || duplicate) {
      continue;
    }
    const auto count = [&object](const char* key) {
      return std::clamp(object.value(QLatin1String(key)).toInt(0), 0, kGuideLayoutMaxCount);
    };
    const auto length = [&object](const char* key) {
      const auto number = object.value(QLatin1String(key)).toDouble(0.0);
      return std::isfinite(number) ? std::max(0.0, number) : 0.0;
    };
    preset.unit = measurement_unit_from_settings_token(object.value(QStringLiteral("unit")).toString(),
                                                       MeasurementUnit::Pixels);
    preset.columns = count("columns");
    preset.rows = count("rows");
    preset.column_gutter = length("columnGutter");
    preset.row_gutter = length("rowGutter");
    preset.margin_top = length("marginTop");
    preset.margin_left = length("marginLeft");
    preset.margin_bottom = length("marginBottom");
    preset.margin_right = length("marginRight");
    preset.clear_existing = object.value(QStringLiteral("clearExisting")).toBool(false);
    presets.push_back(preset);
  }
  return presets;
}

void store_guide_layout_user_presets(const std::vector<GuideLayoutUserPreset>& presets) {
  auto settings = app_settings();
  if (presets.empty()) {
    settings.remove(QLatin1String(kGuideLayoutUserPresetsKey));
    return;
  }
  QJsonArray array;
  for (const auto& preset : presets) {
    QJsonObject object;
    object.insert(QStringLiteral("name"), preset.name);
    object.insert(QStringLiteral("unit"), measurement_unit_settings_token(preset.unit));
    object.insert(QStringLiteral("columns"), preset.columns);
    object.insert(QStringLiteral("rows"), preset.rows);
    object.insert(QStringLiteral("columnGutter"), preset.column_gutter);
    object.insert(QStringLiteral("rowGutter"), preset.row_gutter);
    object.insert(QStringLiteral("marginTop"), preset.margin_top);
    object.insert(QStringLiteral("marginLeft"), preset.margin_left);
    object.insert(QStringLiteral("marginBottom"), preset.margin_bottom);
    object.insert(QStringLiteral("marginRight"), preset.margin_right);
    object.insert(QStringLiteral("clearExisting"), preset.clear_existing);
    array.push_back(object);
  }
  settings.setValue(QLatin1String(kGuideLayoutUserPresetsKey), QJsonDocument(array).toJson(QJsonDocument::Compact));
}

}  // namespace

void MainWindow::new_guide_layout_dialog() {
  if (canvas_ == nullptr) {
    return;
  }

  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("newGuideLayoutDialog"));
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, tr("New Guide Layout"));
  auto* form = new QFormLayout();
  form->setContentsMargins(0, 0, 0, 0);
  form->setSpacing(8);

  // The last layout used comes back (GitHub issue 403); lengths are stored in pixels.
  auto settings = app_settings();
  const auto stored_pixels = [&settings](const char* key) {
    return std::max(0.0, settings.value(QLatin1String(key), 0.0).toDouble());
  };

  // Item data: "" for Custom, a built-in preset's id, or "user:<index>" for a saved one.
  const auto user_item_prefix = QStringLiteral("user:");
  auto* preset_combo = new QComboBox(&dialog);
  preset_combo->setObjectName(QStringLiteral("newGuideLayoutPresetCombo"));
  const auto& presets = guide_layout_presets();
  auto user_presets = load_guide_layout_user_presets();
  const auto rebuild_preset_combo = [&] {
    const QSignalBlocker block(preset_combo);
    preset_combo->clear();
    preset_combo->addItem(tr("Custom"), QString());
    for (const auto& preset : presets) {
      preset_combo->addItem(translate_data_text(preset.name),
                            QString::fromLatin1(preset.id.data(), static_cast<qsizetype>(preset.id.size())));
    }
    if (!user_presets.empty()) {
      preset_combo->insertSeparator(preset_combo->count());
      for (std::size_t index = 0; index < user_presets.size(); ++index) {
        preset_combo->addItem(user_presets[index].name, user_item_prefix + QString::number(index));
      }
    }
  };
  rebuild_preset_combo();
  const auto user_index_at = [&](int row) -> std::optional<std::size_t> {
    const auto data = preset_combo->itemData(row).toString();
    if (!data.startsWith(user_item_prefix)) {
      return std::nullopt;
    }
    bool ok = false;
    const auto index = data.mid(user_item_prefix.size()).toInt(&ok);
    if (!ok || index < 0 || static_cast<std::size_t>(index) >= user_presets.size()) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(index);
  };
  // Saved presets are managed beside the box; built-in presets cannot be replaced or deleted.
  auto* save_preset = new QToolButton(&dialog);
  save_preset->setObjectName(QStringLiteral("newGuideLayoutSavePresetButton"));
  save_preset->setText(tr("Save Preset..."));
  save_preset->setToolTip(tr("Save the current layout as a preset"));
  save_preset->setAutoRaise(true);
  auto* delete_preset = new QToolButton(&dialog);
  delete_preset->setObjectName(QStringLiteral("newGuideLayoutDeletePresetButton"));
  delete_preset->setText(tr("Delete Preset"));
  delete_preset->setToolTip(tr("Delete the selected saved preset"));
  delete_preset->setAutoRaise(true);
  delete_preset->setEnabled(false);
  auto* preset_row = new QHBoxLayout();
  preset_row->setContentsMargins(0, 0, 0, 0);
  preset_row->setSpacing(4);
  preset_row->addWidget(preset_combo, 1);
  preset_row->addWidget(save_preset);
  preset_row->addWidget(delete_preset);

  const auto make_count_spin = [&dialog](const QString& name, int value) {
    auto* spin = new QSpinBox(&dialog);
    spin->setObjectName(name);
    spin->setRange(0, kGuideLayoutMaxCount);
    spin->setValue(std::clamp(value, 0, kGuideLayoutMaxCount));
    return spin;
  };
  // Percent is relative to the extent the length runs along.
  const auto length_context = [this](bool horizontal) {
    return UnitConversionContext{sanitized_document_ppi(document().print_settings().horizontal_ppi),
                                 static_cast<double>(horizontal ? document().width() : document().height())};
  };
  const auto make_length_spin = [this, &dialog, length_context](const QString& name, double pixels, bool horizontal) {
    auto* spin = new UnitSpinBox(SpinUnit::Pixels, &dialog);
    spin->set_context_provider([length_context, horizontal] { return length_context(horizontal); });
    spin->setObjectName(name);
    spin->setRange(0.0, static_cast<double>(kMaxGuideOffsetPixels));
    spin->setDecimals(3);
    spin->set_trim_trailing_zeros(true);
    spin->setValue(pixels);
    register_ruler_unit_field(spin);
    return spin;
  };
  auto* columns_spin = make_count_spin(QStringLiteral("newGuideLayoutColumnsSpin"),
                                       settings.value(QStringLiteral("view/guideLayoutColumns"), 2).toInt());
  auto* column_gutter_spin = make_length_spin(QStringLiteral("newGuideLayoutColumnGutterSpin"),
                                              stored_pixels("view/guideLayoutColumnGutter"), true);
  auto* rows_spin = make_count_spin(QStringLiteral("newGuideLayoutRowsSpin"),
                                    settings.value(QStringLiteral("view/guideLayoutRows"), 2).toInt());
  auto* row_gutter_spin = make_length_spin(QStringLiteral("newGuideLayoutRowGutterSpin"),
                                           stored_pixels("view/guideLayoutRowGutter"), false);
  auto* top_spin =
      make_length_spin(QStringLiteral("newGuideLayoutMarginTopSpin"), stored_pixels("view/guideLayoutMarginTop"), false);
  auto* left_spin =
      make_length_spin(QStringLiteral("newGuideLayoutMarginLeftSpin"), stored_pixels("view/guideLayoutMarginLeft"), true);
  auto* bottom_spin = make_length_spin(QStringLiteral("newGuideLayoutMarginBottomSpin"),
                                       stored_pixels("view/guideLayoutMarginBottom"), false);
  auto* right_spin = make_length_spin(QStringLiteral("newGuideLayoutMarginRightSpin"),
                                      stored_pixels("view/guideLayoutMarginRight"), true);
  auto* clear_existing = new QCheckBox(tr("Clear existing guides"), &dialog);
  clear_existing->setObjectName(QStringLiteral("newGuideLayoutClearExistingCheck"));
  clear_existing->setChecked(settings.value(QStringLiteral("view/guideLayoutClearExisting"), false).toBool());

  // "Columns: [n]  Gutter: [px]" on one row; each label scrubs the field after it.
  std::vector<QLabel*> second_labels;
  const auto pair_row = [&dialog, &second_labels](QWidget* first, const QString& second_label, QWidget* second) {
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    row->addWidget(first, 1);
    auto* label = new QLabel(second_label, &dialog);
    second_labels.push_back(label);
    row->addWidget(label);
    row->addWidget(second, 1);
    return row;
  };
  form->addRow(tr("Preset:"), preset_row);
  form->addRow(tr("Columns:"), pair_row(columns_spin, tr("Gutter:"), column_gutter_spin));
  form->addRow(tr("Rows:"), pair_row(rows_spin, tr("Gutter:"), row_gutter_spin));
  form->addRow(tr("Top margin:"), pair_row(top_spin, tr("Bottom:"), bottom_spin));
  form->addRow(tr("Left margin:"), pair_row(left_spin, tr("Right:"), right_spin));
  form->addRow(QString(), clear_existing);
  content->addLayout(form);
  // One width for the inner labels lines the second fields up in a column.
  int second_label_width = 0;
  for (const auto* label : second_labels) {
    second_label_width = std::max(second_label_width, label->sizeHint().width());
  }
  for (auto* label : second_labels) {
    label->setMinimumWidth(second_label_width);
  }

  const auto current_layout = [=] {
    return GuideLayout{
        GuideLayoutAxis{columns_spin->value(), column_gutter_spin->value(), left_spin->value(), right_spin->value()},
        GuideLayoutAxis{rows_spin->value(), row_gutter_spin->value(), top_spin->value(), bottom_spin->value()}};
  };
  // A saved preset's lengths in pixels on this document.
  const auto user_preset_layout = [length_context](const GuideLayoutUserPreset& preset) {
    const auto pixels = [&preset, length_context](double value, bool horizontal) {
      const auto converted = convert_unit_entry(UnitEntry{value, spin_unit_for(preset.unit)}, SpinUnit::Pixels,
                                                length_context(horizontal));
      return std::clamp(converted.value_or(0.0), 0.0, static_cast<double>(kMaxGuideOffsetPixels));
    };
    return GuideLayout{GuideLayoutAxis{preset.columns, pixels(preset.column_gutter, true),
                                       pixels(preset.margin_left, true), pixels(preset.margin_right, true)},
                       GuideLayoutAxis{preset.rows, pixels(preset.row_gutter, false), pixels(preset.margin_top, false),
                                       pixels(preset.margin_bottom, false)}};
  };
  // The fields keep three decimals, so a converted saved length matches within that.
  const auto same_layout = [](const GuideLayout& a, const GuideLayout& b) {
    const auto close = [](double x, double y) { return std::abs(x - y) < 0.0006; };
    const auto same_axis = [&close](const GuideLayoutAxis& p, const GuideLayoutAxis& q) {
      return p.count == q.count && close(p.gutter, q.gutter) && close(p.margin_start, q.margin_start) &&
             close(p.margin_end, q.margin_end);
    };
    return same_axis(a.columns, b.columns) && same_axis(a.rows, b.rows);
  };
  // The preset box names the preset the fields match (built-ins first), or Custom.
  const auto sync_preset = [&] {
    const auto layout = current_layout();
    int row = 0;
    for (std::size_t preset = 0; preset < presets.size() && row == 0; ++preset) {
      if (presets[preset].layout == layout) {
        row = preset_combo->findData(
            QString::fromLatin1(presets[preset].id.data(), static_cast<qsizetype>(presets[preset].id.size())));
      }
    }
    for (std::size_t index = 0; index < user_presets.size() && row <= 0; ++index) {
      if (same_layout(user_preset_layout(user_presets[index]), layout)) {
        row = preset_combo->findData(user_item_prefix + QString::number(index));
      }
    }
    const QSignalBlocker block(preset_combo);
    preset_combo->setCurrentIndex(std::max(row, 0));
    delete_preset->setEnabled(user_index_at(preset_combo->currentIndex()).has_value());
  };
  const auto apply_layout = [=](const GuideLayout& layout) {
    columns_spin->setValue(layout.columns.count);
    column_gutter_spin->setValue(layout.columns.gutter);
    left_spin->setValue(layout.columns.margin_start);
    right_spin->setValue(layout.columns.margin_end);
    rows_spin->setValue(layout.rows.count);
    row_gutter_spin->setValue(layout.rows.gutter);
    top_spin->setValue(layout.rows.margin_start);
    bottom_spin->setValue(layout.rows.margin_end);
  };
  connect(preset_combo, &QComboBox::currentIndexChanged, &dialog, [&](int row) {
    if (const auto user = user_index_at(row); user.has_value()) {
      const auto preset = user_presets[*user];
      apply_layout(user_preset_layout(preset));
      clear_existing->setChecked(preset.clear_existing);
    } else if (row > 0 && row <= static_cast<int>(presets.size())) {
      apply_layout(presets[static_cast<std::size_t>(row - 1)].layout);
    }
    sync_preset();
  });
  for (auto* spin : {columns_spin, rows_spin}) {
    connect(spin, &QSpinBox::valueChanged, &dialog, [&] { sync_preset(); });
  }
  for (auto* spin : {column_gutter_spin, row_gutter_spin, top_spin, left_spin, bottom_spin, right_spin}) {
    connect(spin, &QDoubleSpinBox::valueChanged, &dialog, [&] { sync_preset(); });
  }

  connect(save_preset, &QToolButton::clicked, &dialog, [&] {
    const auto title = tr("Save Guide Layout Preset");
    QString initial_name;
    if (const auto user = user_index_at(preset_combo->currentIndex()); user.has_value()) {
      initial_name = user_presets[*user].name;
    }
    QInputDialog input(&dialog);
    input.setObjectName(QStringLiteral("newGuideLayoutSavePresetNameDialog"));
    input.setWindowTitle(title);
    input.setLabelText(tr("Preset name:"));
    input.setInputMode(QInputDialog::TextInput);
    input.setTextValue(initial_name);
    input.setOkButtonText(tr("Save"));
    input.resize(420, input.sizeHint().height());
    if (exec_dialog(input) != QDialog::Accepted) {
      return;
    }
    const auto name = input.textValue().simplified();
    if (name.isEmpty()) {
      (void)show_warning_message(&dialog, title, tr("Enter a name for the preset."), QMessageBox::Ok, QMessageBox::Ok,
                                 QStringLiteral("newGuideLayoutPresetNameMessageBox"));
      return;
    }
    bool builtin = name.compare(tr("Custom"), Qt::CaseInsensitive) == 0;
    for (const auto& preset : presets) {
      builtin = builtin || name.compare(translate_data_text(preset.name), Qt::CaseInsensitive) == 0 ||
                name.compare(QLatin1String(preset.name), Qt::CaseInsensitive) == 0;
    }
    if (builtin) {
      (void)show_warning_message(&dialog, title, tr("\"%1\" is a built-in preset. Choose another name.").arg(name),
                                 QMessageBox::Ok, QMessageBox::Ok, QStringLiteral("newGuideLayoutPresetNameMessageBox"));
      return;
    }
    // Lengths are saved in the unit the fields show.
    const auto unit = measurement_unit_for(column_gutter_spin->display_unit()).value_or(MeasurementUnit::Pixels);
    const auto in_unit = [&](double pixels, bool horizontal) {
      return convert_unit_entry(UnitEntry{pixels, SpinUnit::Pixels}, spin_unit_for(unit), length_context(horizontal))
          .value_or(0.0);
    };
    GuideLayoutUserPreset preset;
    preset.name = name;
    preset.unit = unit;
    preset.columns = columns_spin->value();
    preset.rows = rows_spin->value();
    preset.column_gutter = in_unit(column_gutter_spin->value(), true);
    preset.row_gutter = in_unit(row_gutter_spin->value(), false);
    preset.margin_top = in_unit(top_spin->value(), false);
    preset.margin_left = in_unit(left_spin->value(), true);
    preset.margin_bottom = in_unit(bottom_spin->value(), false);
    preset.margin_right = in_unit(right_spin->value(), true);
    preset.clear_existing = clear_existing->isChecked();
    auto existing = std::find_if(user_presets.begin(), user_presets.end(), [&name](const GuideLayoutUserPreset& other) {
      return other.name.compare(name, Qt::CaseInsensitive) == 0;
    });
    if (existing != user_presets.end()) {
      const auto answer = show_warning_message(&dialog, title, tr("Replace the preset \"%1\"?").arg(existing->name),
                                               QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
                                               QStringLiteral("newGuideLayoutReplacePresetMessageBox"));
      if (answer != QMessageBox::Yes) {
        return;
      }
      *existing = preset;
    } else {
      user_presets.push_back(preset);
    }
    store_guide_layout_user_presets(user_presets);
    rebuild_preset_combo();
    sync_preset();
  });
  connect(delete_preset, &QToolButton::clicked, &dialog, [&] {
    const auto user = user_index_at(preset_combo->currentIndex());
    if (!user.has_value()) {
      return;
    }
    const auto answer = show_warning_message(
        &dialog, tr("Delete Guide Layout Preset"), tr("Delete the preset \"%1\"?").arg(user_presets[*user].name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No, QStringLiteral("newGuideLayoutDeletePresetMessageBox"));
    if (answer != QMessageBox::Yes) {
      return;
    }
    user_presets.erase(user_presets.begin() + static_cast<std::ptrdiff_t>(*user));
    store_guide_layout_user_presets(user_presets);
    rebuild_preset_combo();
    sync_preset();
  });
  sync_preset();

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("newGuideLayoutButtonBox"));
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  content->addWidget(buttons);
  // Last, once every field exists: the fields' -/+ buttons (docs/ui-conventions.md).
  append_themed_style(dialog, dialog_spinbox_button_style());

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return;
  }

  const auto layout = current_layout();
  settings.setValue(QStringLiteral("view/guideLayoutColumns"), layout.columns.count);
  settings.setValue(QStringLiteral("view/guideLayoutRows"), layout.rows.count);
  settings.setValue(QStringLiteral("view/guideLayoutColumnGutter"), layout.columns.gutter);
  settings.setValue(QStringLiteral("view/guideLayoutRowGutter"), layout.rows.gutter);
  settings.setValue(QStringLiteral("view/guideLayoutMarginTop"), layout.rows.margin_start);
  settings.setValue(QStringLiteral("view/guideLayoutMarginLeft"), layout.columns.margin_start);
  settings.setValue(QStringLiteral("view/guideLayoutMarginBottom"), layout.rows.margin_end);
  settings.setValue(QStringLiteral("view/guideLayoutMarginRight"), layout.columns.margin_end);
  settings.setValue(QStringLiteral("view/guideLayoutClearExisting"), clear_existing->isChecked());

  push_undo_snapshot(tr("New Guide Layout"));
  if (clear_existing->isChecked()) {
    document().guides().clear();
  }
  const auto add_guides = [this](GuideOrientation orientation, const GuideLayoutAxis& axis, int extent) {
    for (const auto position : guide_layout_positions_32(axis, extent)) {
      document().guides().push_back(DocumentGuide{orientation, position});
    }
  };
  add_guides(GuideOrientation::Vertical, layout.columns, document().width());
  add_guides(GuideOrientation::Horizontal, layout.rows, document().height());
  canvas_->document_changed();
}

void MainWindow::clear_guides() {
  if (canvas_ == nullptr) {
    return;
  }
  canvas_->clear_guides();
}

void MainWindow::clear_selected_guides() {
  if (canvas_ == nullptr) {
    return;
  }
  canvas_->clear_selected_guides();
}

void MainWindow::set_ruler_unit_preference(MeasurementUnit unit) {
  if (ruler_unit_ == unit) {
    return;
  }
  ruler_unit_ = unit;
  for (const auto& active_session : sessions_) {
    apply_canvas_aid_settings(active_session->canvas);
  }
  save_view_settings();
  apply_ruler_unit_to_fields();
  refresh_document_info();
}

void MainWindow::register_ruler_unit_field(UnitSpinBox* spin) {
  if (spin == nullptr) {
    return;
  }
  ruler_unit_fields_.emplace_back(spin);
  set_field_display_unit(spin, ruler_unit_);
  // Photoshop: a unit picked from a field's menu changes Units & Rulers for every
  // field and the rulers (set_ruler_unit_preference re-applies it to every enrolled
  // field; this one already shows it). A typed unit token stays the field's own.
  connect(spin, &UnitSpinBox::display_unit_picked, this, [this](SpinUnit unit) {
    if (const auto measurement = measurement_unit_for(unit); measurement.has_value()) {
      set_ruler_unit_preference(*measurement);
    }
  });
}

void MainWindow::apply_ruler_unit_to_fields() {
  // Dialog fields (New Guide) enroll for their lifetime; drop the dead pointers.
  std::erase_if(ruler_unit_fields_, [](const QPointer<UnitSpinBox>& spin) { return spin.isNull(); });
  for (const auto& spin : ruler_unit_fields_) {
    set_field_display_unit(spin, ruler_unit_);
  }
}

void MainWindow::refresh_ruler_unit_field_metrics() {
  for (const auto& spin : ruler_unit_fields_) {
    if (spin != nullptr) {
      spin->refresh_display_metrics();
    }
  }
}

UnitConversionContext MainWindow::document_unit_context(bool horizontal) const {
  return document_field_context(document_field_units(), horizontal);
}

UnitSpinBox::ContextProvider MainWindow::document_unit_context_provider(bool horizontal) const {
  return [this, horizontal] { return document_unit_context(horizontal); };
}

DocumentFieldUnits MainWindow::document_field_units() const {
  DocumentFieldUnits units;
  units.display_unit = ruler_unit_;
  if (has_active_document()) {
    units.ppi = text_size_ppi(document());
    units.document_width = static_cast<double>(document().width());
    units.document_height = static_cast<double>(document().height());
  }
  return units;
}

DocumentFieldUnits MainWindow::dialog_field_units() {
  auto units = document_field_units();
  // A unit picked in a modal dialog is the same gesture as on a live field.
  units.on_unit_picked = [this](MeasurementUnit unit) { set_ruler_unit_preference(unit); };
  return units;
}

void MainWindow::set_canvas_backdrop_color_preference(std::optional<QColor> color) {
  if (color.has_value() && !color->isValid()) {
    color.reset();
  }
  if (color.has_value()) {
    color->setAlpha(255);
  }
  view_canvas_backdrop_color_ = color;
  for (const auto& active_session : sessions_) {
    apply_canvas_aid_settings(active_session->canvas);
  }
  save_view_settings();
}

// Patchy's own picker with a live preview on every window; Cancel restores the
// previous choice (a preset or Default), so the preference only changes on OK.
void MainWindow::choose_custom_canvas_backdrop_color() {
  const auto previous = view_canvas_backdrop_color_;
  const auto initial = canvas_ != nullptr ? canvas_->backdrop_color() : theme().canvas_backdrop;
  const auto preview = [this](QColor color) {
    for (const auto& active_session : sessions_) {
      if (active_session->canvas != nullptr) {
        active_session->canvas->set_backdrop_color_override(color);
      }
    }
  };
  const auto chosen = request_patchy_color(this, initial, tr("Canvas Background Color"), preview);
  set_canvas_backdrop_color_preference(chosen.has_value() ? std::optional<QColor>(*chosen) : previous);
}

void MainWindow::apply_canvas_aid_settings(CanvasWidget* canvas) const {
  if (canvas == nullptr) {
    return;
  }
  canvas->set_rulers_visible(view_rulers_visible_);
  canvas->set_ruler_unit(ruler_unit_);
  canvas->set_grid_visible(view_grid_visible_);
  canvas->set_pixel_grid_visible(view_pixel_grid_visible_);
  canvas->set_guides_visible(view_guides_visible_);
  canvas->set_guides_locked(view_guides_locked_);
  canvas->set_snap_enabled(view_snap_enabled_);
  canvas->set_snap_to_guides(view_snap_to_guides_);
  canvas->set_snap_to_grid(view_snap_to_grid_);
  canvas->set_snap_to_document(view_snap_to_document_);
  canvas->set_snap_to_layers(view_snap_to_layers_);
  canvas->set_snap_to_selection(view_snap_to_selection_);
  canvas->set_grid_subdivisions(view_grid_subdivisions_);
  canvas->set_grid_style(view_grid_style_);
  canvas->set_grid_color(view_grid_color_);
  canvas->set_backdrop_color_override(view_canvas_backdrop_color_);
  canvas->set_guide_color(view_guide_color_);
  canvas->set_target_path_visible(view_target_path_visible_);
  canvas->set_show_selection_edges(view_selection_edges_shown_);
  canvas->set_extras_covered(view_extras_covered_);
  canvas->set_vector_preview_enabled(view_vector_preview_enabled_);
  canvas->set_proof_display_transform(proof_display_transform_);
}

void MainWindow::refresh_proof_display(bool announce_view) {
  const bool proof = view_proof_colors_action_ != nullptr && view_proof_colors_action_->isChecked();
  const bool gamut = view_gamut_warning_action_ != nullptr && view_gamut_warning_action_->isChecked();
  const auto space = working_cmyk_space();
  if (!space->has_profile()) {
    // The formula cannot proof: a CMYK Proof document goes back to plain RGB.
    bool dropped = false;
    for (const auto& session : sessions_) {
      dropped = dropped || session->cmyk_proof;
      session->cmyk_proof = false;
    }
    if (dropped) {
      show_status_error(tr("CMYK Proof needs a CMYK profile and was turned off. Choose a profile in Preferences."));
    }
  }
  std::shared_ptr<const CmykProofTransform> transform;
  if (proof || gamut) {
    transform = CmykProofTransform::create(*space, proof, gamut);
    if (transform == nullptr) {
      // The formula has no gamut and nothing to simulate.
      for (auto* action : {view_proof_colors_action_, view_gamut_warning_action_}) {
        const QSignalBlocker blocker(action);
        action->setChecked(false);
      }
      show_status_error(tr("Proof Colors and Gamut Warning need a CMYK profile. Choose one in Preferences."));
    } else if (announce_view) {
      statusBar()->showMessage(
          tr("Canvas view uses the CMYK profile %1. Documents and exports are unchanged.")
              .arg(QString::fromStdString(space->profile_description())),
          6000);
    }
  }
  proof_display_transform_ = std::move(transform);
  // CMYK Proof documents always simulate the print, with the gamut warning when it is on.
  const bool any_document_proof =
      std::any_of(sessions_.begin(), sessions_.end(), [](const auto& session) { return session->cmyk_proof; });
  const bool document_gamut = gamut && proof_display_transform_ != nullptr;
  if (!any_document_proof) {
    document_proof_transform_.reset();
    document_proof_space_.reset();
  } else if (proof && proof_display_transform_ != nullptr) {
    document_proof_transform_ = proof_display_transform_;
    document_proof_space_.reset();
  } else if (document_proof_transform_ == nullptr || document_proof_space_ != space ||
             document_proof_gamut_ != document_gamut) {
    document_proof_transform_ = CmykProofTransform::create(*space, true, document_gamut);
    document_proof_space_ = space;
    document_proof_gamut_ = document_gamut;
  }
  for (const auto& session : sessions_) {
    const auto& session_transform = session->cmyk_proof ? document_proof_transform_ : proof_display_transform_;
    session->canvas->set_proof_display_transform(session_transform);
    // The ink plates exist only under the proof (docs/cmyk.md).
    const bool plates_listed = session_transform != nullptr && session_transform->simulates_colors();
    if (!plates_listed && CanvasWidget::is_cmyk_plate(session->canvas->layer_edit_target())) {
      session->canvas->set_component_channel_preview(CanvasWidget::LayerEditTarget::Content);
    }
  }
  refresh_channel_panel();
  refresh_image_mode_actions();
  update_document_action_state();
}

QString MainWindow::set_session_cmyk_proof(DocumentSession& session, bool enabled) {
  if (enabled) {
    if (std::as_const(session.document).palette_editing().has_value()) {
      return tr("Convert the document to RGB Color before turning on CMYK Proof.");
    }
    if (!working_cmyk_space()->has_profile()) {
      return tr("CMYK Proof needs a CMYK profile. Choose one in Preferences.");
    }
  }
  if (session.cmyk_proof == enabled) {
    refresh_image_mode_actions();
    return {};
  }
  session.cmyk_proof = enabled;
  session.cmyk_proof_rgb_save_accepted = false;
  refresh_proof_display(/*announce_view*/ false);
  return {};
}

void MainWindow::set_document_cmyk_proof(bool enabled) {
  if (!has_active_document()) {
    return;
  }
  if (enabled && std::as_const(document()).palette_editing().has_value()) {
    convert_document_to_rgb();
    if (!has_active_document() || std::as_const(document()).palette_editing().has_value()) {
      refresh_image_mode_actions();  // cancelled: stay Indexed
      return;
    }
  }
  const bool was_enabled = session().cmyk_proof;
  if (const auto refusal = set_session_cmyk_proof(session(), enabled); !refusal.isEmpty()) {
    refresh_image_mode_actions();
    show_status_error(refusal);
    return;
  }
  if (enabled && !was_enabled) {
    statusBar()->showMessage(tr("CMYK Proof: the canvas shows the CMYK profile %1. Saving as JPEG, TIFF or PDF "
                                "writes CMYK.")
                                 .arg(QString::fromStdString(working_cmyk_space()->profile_description())),
                             6000);
  } else if (!enabled && was_enabled) {
    statusBar()->showMessage(tr("RGB Color: saves are written in RGB again."), 4000);
  }
}

void MainWindow::refresh_vector_preview_action() {
  if (view_vector_preview_action_ == nullptr) {
    return;
  }
  const QSignalBlocker blocker(view_vector_preview_action_);
  view_vector_preview_action_->setChecked(view_vector_preview_enabled_);
  refresh_action_tooltip(view_vector_preview_action_);
  auto tooltip = view_vector_preview_action_->toolTip() + QLatin1Char('\n') +
      tr("Keep vector artwork sharp when zooming, including in documents with pixel layers. Saved files and exports keep their pixel resolution.");
  if (canvas_ != nullptr && view_vector_preview_enabled_ && !canvas_->vector_preview_status().isEmpty()) {
    tooltip += QLatin1Char('\n') + canvas_->vector_preview_status();
  }
  view_vector_preview_action_->setToolTip(tooltip);
}

// The load/save/apply trio named for the pen carries the general canvas input
// preferences too (wheel zoom, the transform aspect modifier); they share the
// "input/" key namespace and the same per-canvas push.
void MainWindow::apply_pen_input_settings(CanvasWidget* canvas) const {
  if (canvas == nullptr) {
    return;
  }
  canvas->set_pen_input_settings(pen_input_settings_);
  canvas->set_wheel_zooms(wheel_zooms_);
  canvas->set_shift_keeps_transform_aspect(shift_keeps_transform_aspect_);
  canvas->set_show_transform_drag_values(show_transform_drag_values_);
  canvas->set_snap_transforms_to_pixel_grid(snap_transforms_to_pixel_grid_);
}

void MainWindow::handle_pen_button_action(PenButtonAction action) {
  switch (action) {
    case PenButtonAction::Undo:
      undo();
      break;
    case PenButtonAction::Redo:
      redo();
      break;
    case PenButtonAction::ToggleEraser:
      if (current_tool_ == CanvasTool::Eraser) {
        activate_tool(tool_before_eraser_toggle_);
      } else {
        tool_before_eraser_toggle_ = current_tool_;
        activate_tool(CanvasTool::Eraser);
      }
      break;
    case PenButtonAction::IncreaseBrushSize:
    case PenButtonAction::DecreaseBrushSize: {
      if (auto* brush_size = findChild<QSpinBox*>(QStringLiteral("brushSizeSpin")); brush_size != nullptr) {
        const int direction = action == PenButtonAction::IncreaseBrushSize ? 1 : -1;
        const int value = brush_size->value();
        brush_size->setValue(value + direction * proportional_brush_step(value, direction, false));
      }
      break;
    }
    case PenButtonAction::SwapColors:
      swap_colors();
      break;
    case PenButtonAction::PanCanvas:
    case PenButtonAction::ZoomCanvas:
    case PenButtonAction::PickColor:
    case PenButtonAction::SetCloneSource:
    case PenButtonAction::None:
      break;
  }
}

void MainWindow::load_pen_input_settings() {
  auto settings = app_settings();
  pen_input_settings_.enabled = settings.value(QStringLiteral("input/pen/enabled"), true).toBool();
  pen_input_settings_.pressure_size = settings.value(QStringLiteral("input/pen/pressureSize"), true).toBool();
  pen_input_settings_.pressure_size_min_percent =
      std::clamp(settings.value(QStringLiteral("input/pen/pressureSizeMinPercent"), 20).toInt(), 1, 100);
  pen_input_settings_.pressure_opacity =
      settings.value(QStringLiteral("input/pen/pressureOpacity"), true).toBool();
  pen_input_settings_.pressure_opacity_min_percent =
      std::clamp(settings.value(QStringLiteral("input/pen/pressureOpacityMinPercent"), 15).toInt(), 1, 100);
  pen_input_settings_.use_eraser_tip = settings.value(QStringLiteral("input/pen/useEraserTip"), true).toBool();
  auto primary_token = settings.value(QStringLiteral("input/pen/primaryButtonAction")).toString();
  auto secondary_token = settings.value(QStringLiteral("input/pen/secondaryButtonAction")).toString();
  if (primary_token.isEmpty()) {
    // Migrate from the legacy "barrel button pans canvas" toggle.
    const auto legacy_pans = settings.value(QStringLiteral("input/pen/barrelButtonPans"), true).toBool();
    primary_token =
        pen_button_action_to_token(legacy_pans ? PenButtonAction::PanCanvas : PenButtonAction::None);
  }
  if (secondary_token.isEmpty()) {
    secondary_token = pen_button_action_to_token(PenButtonAction::PickColor);
  }
  pen_input_settings_.primary_button_action = pen_button_action_from_token(primary_token);
  pen_input_settings_.secondary_button_action = pen_button_action_from_token(secondary_token);
  pen_input_settings_.tilt_shape = settings.value(QStringLiteral("input/pen/tiltShape"), false).toBool();
  pen_input_settings_.tilt_min_roundness_percent =
      std::clamp(settings.value(QStringLiteral("input/pen/tiltMinRoundnessPercent"), 35).toInt(), 1, 100);
  wheel_zooms_ = settings.value(QStringLiteral("input/wheelZooms"), kWheelZoomsDefault).toBool();
  shift_keeps_transform_aspect_ =
      settings.value(QStringLiteral("input/shiftKeepsTransformAspect"), false).toBool();
  show_transform_drag_values_ = settings.value(QStringLiteral("view/showTransformValues"), true).toBool();
  snap_transforms_to_pixel_grid_ =
      settings.value(QStringLiteral("input/snapTransformsToPixelGrid"), true).toBool();
  escape_commits_text_ = settings.value(QStringLiteral("input/escapeCommitsText"), true).toBool();
  apply_pen_input_settings(canvas_);
}

void MainWindow::save_pen_input_settings() const {
  auto settings = app_settings();
  settings.setValue(QStringLiteral("input/pen/enabled"), pen_input_settings_.enabled);
  settings.setValue(QStringLiteral("input/pen/pressureSize"), pen_input_settings_.pressure_size);
  settings.setValue(QStringLiteral("input/pen/pressureSizeMinPercent"),
                    pen_input_settings_.pressure_size_min_percent);
  settings.setValue(QStringLiteral("input/pen/pressureOpacity"), pen_input_settings_.pressure_opacity);
  settings.setValue(QStringLiteral("input/pen/pressureOpacityMinPercent"),
                    pen_input_settings_.pressure_opacity_min_percent);
  settings.setValue(QStringLiteral("input/pen/useEraserTip"), pen_input_settings_.use_eraser_tip);
  settings.setValue(QStringLiteral("input/pen/primaryButtonAction"),
                    pen_button_action_to_token(pen_input_settings_.primary_button_action));
  settings.setValue(QStringLiteral("input/pen/secondaryButtonAction"),
                    pen_button_action_to_token(pen_input_settings_.secondary_button_action));
  settings.setValue(QStringLiteral("input/pen/tiltShape"), pen_input_settings_.tilt_shape);
  settings.setValue(QStringLiteral("input/pen/tiltMinRoundnessPercent"),
                    pen_input_settings_.tilt_min_roundness_percent);
  settings.setValue(QStringLiteral("input/wheelZooms"), wheel_zooms_);
  settings.setValue(QStringLiteral("input/shiftKeepsTransformAspect"), shift_keeps_transform_aspect_);
  settings.setValue(QStringLiteral("view/showTransformValues"), show_transform_drag_values_);
  settings.setValue(QStringLiteral("input/snapTransformsToPixelGrid"), snap_transforms_to_pixel_grid_);
  settings.setValue(QStringLiteral("input/escapeCommitsText"), escape_commits_text_);
}

void MainWindow::load_view_settings() {
  auto settings = app_settings();
  view_vector_preview_enabled_ = settings.value(QStringLiteral("view/vectorPreview"), false).toBool();
  refresh_vector_preview_action();
  view_rulers_visible_ = settings.value(QStringLiteral("view/rulersVisible"), view_rulers_visible_).toBool();
  ruler_unit_ = measurement_unit_from_settings_token(
      settings.value(QStringLiteral("view/rulerUnits"), QStringLiteral("px")).toString(),
      MeasurementUnit::Pixels);
  apply_ruler_unit_to_fields();  // the options-bar fields exist by now; the docks enroll themselves
  view_grid_visible_ = settings.value(QStringLiteral("view/gridVisible"), view_grid_visible_).toBool();
  view_pixel_grid_visible_ = settings.value(QStringLiteral("view/pixelGrid"), view_pixel_grid_visible_).toBool();
  view_guides_visible_ = settings.value(QStringLiteral("view/guidesVisible"), view_guides_visible_).toBool();
  view_extras_covered_ = load_view_extras_covered();
  view_guides_locked_ = settings.value(QStringLiteral("view/guidesLocked"), view_guides_locked_).toBool();
  view_snap_enabled_ = settings.value(QStringLiteral("view/snapEnabled"), view_snap_enabled_).toBool();
  view_snap_to_guides_ = settings.value(QStringLiteral("view/snapToGuides"), view_snap_to_guides_).toBool();
  view_snap_to_grid_ = settings.value(QStringLiteral("view/snapToGrid"), view_snap_to_grid_).toBool();
  view_snap_to_document_ = settings.value(QStringLiteral("view/snapToDocument"), view_snap_to_document_).toBool();
  view_snap_to_layers_ = settings.value(QStringLiteral("view/snapToLayers"), view_snap_to_layers_).toBool();
  view_snap_to_selection_ = settings.value(QStringLiteral("view/snapToSelection"), view_snap_to_selection_).toBool();
  view_grid_spacing_32_ = std::clamp(settings.value(QStringLiteral("view/gridSpacing32"), view_grid_spacing_32_).toInt(),
                                     1, 320000);
  view_grid_subdivisions_ =
      std::clamp(settings.value(QStringLiteral("view/gridSubdivisions"), view_grid_subdivisions_).toInt(), 1, 64);
  view_grid_style_ = std::clamp(settings.value(QStringLiteral("view/gridStyle"), view_grid_style_).toInt(), 0, 1);
  view_grid_color_ =
      settings.value(QStringLiteral("view/gridColor"), view_grid_color_).value<QColor>();
  view_guide_color_ =
      settings.value(QStringLiteral("view/guideColor"), view_guide_color_).value<QColor>();
  // Absent or invalid means Default (the theme role); the key is only written for a user color.
  if (const auto backdrop = settings.value(QStringLiteral("view/canvasBackdropColor")).value<QColor>();
      backdrop.isValid()) {
    view_canvas_backdrop_color_ = backdrop;
    view_canvas_backdrop_color_->setAlpha(255);
  } else {
    view_canvas_backdrop_color_.reset();
  }
  zoom_layer_thumbnails_to_content_ =
      settings.value(QStringLiteral("view/zoomLayerThumbnailsToContent"), zoom_layer_thumbnails_to_content_)
          .toBool();
  layer_thumbnail_size_ = layer_thumbnail_size_from_token(
      settings.value(QLatin1String(kLayerThumbnailSizeSettingsKey)).toString());
  const bool migrate_legacy_guide_default =
      !settings.value(QStringLiteral("view/guideColorDefaultMigrated"), false).toBool();
  if (!view_grid_color_.isValid()) {
    view_grid_color_ = QColor(78, 154, 255, 105);
  }
  if (!view_guide_color_.isValid() ||
      (migrate_legacy_guide_default && view_guide_color_ == QColor(72, 186, 255, 210))) {
    view_guide_color_ = QColor(255, 70, 180, 230);
  }
  if (migrate_legacy_guide_default) {
    settings.setValue(QStringLiteral("view/guideColorDefaultMigrated"), true);
  }

  if (view_rulers_action_ != nullptr) {
    view_rulers_action_->setChecked(view_rulers_visible_);
  }
  if (view_grid_action_ != nullptr) {
    view_grid_action_->setChecked(view_grid_visible_);
  }
  if (view_pixel_grid_action_ != nullptr) {
    view_pixel_grid_action_->setChecked(view_pixel_grid_visible_);
  }
  if (view_guides_action_ != nullptr) {
    view_guides_action_->setChecked(view_guides_visible_);
  }
  if (view_snap_action_ != nullptr) {
    view_snap_action_->setChecked(view_snap_enabled_);
  }
  if (view_lock_guides_action_ != nullptr) {
    view_lock_guides_action_->setChecked(view_guides_locked_);
  }
  if (view_snap_guides_action_ != nullptr) {
    view_snap_guides_action_->setChecked(view_snap_to_guides_);
  }
  if (view_snap_grid_action_ != nullptr) {
    view_snap_grid_action_->setChecked(view_snap_to_grid_);
  }
  if (view_snap_document_action_ != nullptr) {
    view_snap_document_action_->setChecked(view_snap_to_document_);
  }
  if (view_snap_layers_action_ != nullptr) {
    view_snap_layers_action_->setChecked(view_snap_to_layers_);
  }
  if (view_snap_selection_action_ != nullptr) {
    view_snap_selection_action_->setChecked(view_snap_to_selection_);
  }
  apply_canvas_aid_settings(canvas_);
}

void MainWindow::save_view_settings() const {
  auto settings = app_settings();
  settings.setValue(QStringLiteral("view/vectorPreview"), view_vector_preview_enabled_);
  settings.setValue(QStringLiteral("view/rulersVisible"), view_rulers_visible_);
  settings.setValue(QStringLiteral("view/rulerUnits"), measurement_unit_settings_token(ruler_unit_));
  settings.setValue(QStringLiteral("view/gridVisible"), view_grid_visible_);
  settings.setValue(QStringLiteral("view/pixelGrid"), view_pixel_grid_visible_);
  settings.setValue(QStringLiteral("view/guidesVisible"), view_guides_visible_);
  settings.setValue(QStringLiteral("view/guidesLocked"), view_guides_locked_);
  settings.setValue(QStringLiteral("view/snapEnabled"), view_snap_enabled_);
  settings.setValue(QStringLiteral("view/snapToGuides"), view_snap_to_guides_);
  settings.setValue(QStringLiteral("view/snapToGrid"), view_snap_to_grid_);
  settings.setValue(QStringLiteral("view/snapToDocument"), view_snap_to_document_);
  settings.setValue(QStringLiteral("view/snapToLayers"), view_snap_to_layers_);
  settings.setValue(QStringLiteral("view/snapToSelection"), view_snap_to_selection_);
  settings.setValue(QStringLiteral("view/gridSpacing32"), view_grid_spacing_32_);
  settings.setValue(QStringLiteral("view/gridSubdivisions"), view_grid_subdivisions_);
  settings.setValue(QStringLiteral("view/gridStyle"), view_grid_style_);
  settings.setValue(QStringLiteral("view/gridColor"), view_grid_color_);
  settings.setValue(QStringLiteral("view/guideColor"), view_guide_color_);
  if (view_canvas_backdrop_color_.has_value()) {
    settings.setValue(QStringLiteral("view/canvasBackdropColor"), *view_canvas_backdrop_color_);
  } else {
    settings.remove(QStringLiteral("view/canvasBackdropColor"));
  }
  settings.setValue(QStringLiteral("view/zoomLayerThumbnailsToContent"), zoom_layer_thumbnails_to_content_);
  settings.setValue(QLatin1String(kLayerThumbnailSizeSettingsKey), layer_thumbnail_size_token(layer_thumbnail_size_));
  settings.setValue(QStringLiteral("view/guideColorDefaultMigrated"), true);
}

}  // namespace patchy::ui
