// Preferences > Colors and Themes (docs/theme-files.md, "Preferences UI"): the theme
// list with previews, the shown theme's details and per-theme script answer, the theme
// file buttons, and the theme sound and effect switches. Selecting a theme previews it
// live; OK keeps it and any other way out puts the entry theme back.

#include "ui/main_window.hpp"
#include "ui/preferences_themes_page.hpp"

#include "support/atomic_file_write.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/options_flow_layout.hpp"
#include "ui/qt_paths.hpp"
#include "ui/theme_apply_trace.hpp"
#include "ui/theme_effects.hpp"
#include "ui/theme_file.hpp"
#include "ui/theme_manager.hpp"
#include "ui/theme_package.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/theme_script_host.hpp"
#include "ui/theme_sounds.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Row data: Qt::UserRole holds the token ("system", "dark", "light" or "custom:" + id).
constexpr int kSubtitleRole = Qt::UserRole + 1;
constexpr int kPreviewRole = Qt::UserRole + 2;
constexpr int kHeaderRole = Qt::UserRole + 3;

constexpr int kSwatchWidth = 64;
constexpr int kSwatchHeight = 40;
// Small enough that the list and the details fit the dialog's minimum width (issue 451).
constexpr int kPreviewWidth = 240;
constexpr int kPreviewHeight = 144;

const QString kCustomPrefix = QStringLiteral("custom:");

// A small picture of the chrome in `palette`: title bar, options bar, tool palette,
// canvas (with the package backdrop when there is one), a docked panel and the status
// bar. These are the previewed theme's own colors, content rather than chrome.
void paint_theme_preview(QPainter& painter, const QRectF& rect, const ThemePalette& palette, const QImage& backdrop) {
  const auto w = rect.width();
  const auto h = rect.height();
  const auto part = [&rect, w, h](double x, double y, double width, double height) {
    return QRectF(rect.left() + x * w, rect.top() + y * h, width * w, height * h);
  };
  painter.fillRect(rect, palette.window_bg);
  painter.fillRect(part(0, 0, 1, 0.12), palette.title_bar_bg);
  painter.fillRect(part(0, 0.12, 1, 0.1), palette.options_bar_bg);
  painter.fillRect(part(0.78, 0.15, 0.08, 0.04), palette.accent);
  painter.fillRect(part(0, 0.22, 0.09, 0.7), palette.tool_palette_bg);
  for (int i = 0; i < 5; ++i) {
    painter.fillRect(part(0.025, 0.26 + i * 0.09, 0.04, 0.05),
                     i == 1 ? palette.list_selection_bg : palette.text_secondary);
  }
  const auto canvas = part(0.09, 0.22, 0.63, 0.7);
  painter.fillRect(canvas, palette.canvas_backdrop);
  if (!backdrop.isNull()) {
    painter.save();
    painter.setClipRect(canvas);
    const auto scale = std::min(1.0, canvas.height() / std::max(1, backdrop.height()));
    painter.setBrush(QBrush(QPixmap::fromImage(backdrop.scaled(std::max(1, int(backdrop.width() * scale)),
                                                               std::max(1, int(backdrop.height() * scale)),
                                                               Qt::KeepAspectRatio, Qt::SmoothTransformation))));
    painter.setPen(Qt::NoPen);
    painter.drawRect(canvas);
    painter.restore();
  }
  painter.fillRect(part(0.2, 0.34, 0.41, 0.46), palette.button_bg);
  painter.fillRect(part(0.72, 0.22, 0.28, 0.7), palette.panel_bg);
  painter.fillRect(part(0.72, 0.22, 0.28, 0.08), palette.dock_title_bg);
  painter.fillRect(part(0.74, 0.24, 0.12, 0.04), palette.text_primary);
  painter.fillRect(part(0.72, 0.43, 0.28, 0.08), palette.list_selection_bg);
  for (int i = 0; i < 4; ++i) {
    painter.fillRect(part(0.75, 0.33 + i * 0.1 + (i >= 1 ? 0.02 : 0), 0.16 - i * 0.02, 0.03),
                     i == 1 ? palette.list_selection_text : palette.text_secondary);
  }
  painter.fillRect(part(0, 0.92, 1, 0.08), palette.status_bar_bg);
  painter.fillRect(part(0.02, 0.945, 0.2, 0.03), palette.status_text);
  painter.setPen(QPen(palette.window_border, 1));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
}

// "Follow system": Dark on the upper left, Light on the lower right.
QPixmap theme_preview_pixmap(const ThemePalette* palette, const QImage& backdrop, QSize size, qreal dpr) {
  QPixmap pixmap(size * dpr);
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  const QRectF rect(QPointF(0, 0), QSizeF(size));
  if (palette != nullptr) {
    paint_theme_preview(painter, rect, *palette, backdrop);
    return pixmap;
  }
  paint_theme_preview(painter, rect, theme(ColorScheme::Dark), {});
  QPainterPath lower_right;
  lower_right.moveTo(rect.topRight());
  lower_right.lineTo(rect.bottomRight());
  lower_right.lineTo(rect.bottomLeft());
  lower_right.closeSubpath();
  painter.setClipPath(lower_right);
  paint_theme_preview(painter, rect, theme(ColorScheme::Light), {});
  return pixmap;
}

// Two-line rows: the swatch, the name and a dimmer subtitle; section headers in bold.
class ThemeListDelegate final : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    const QFontMetrics metrics(option.font);
    if (index.data(kHeaderRole).toBool()) {
      return {option.rect.width(), metrics.height() + 14};
    }
    return {option.rect.width(), std::max(kSwatchHeight, metrics.height() * 2 + 4) + 10};
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    const auto& colors = theme();
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    painter->save();
    if (index.data(kHeaderRole).toBool()) {
      auto font = opt.font;
      font.setBold(true);
      painter->setFont(font);
      painter->setPen(colors.text_secondary);
      painter->drawText(opt.rect.adjusted(6, 0, -6, -3), Qt::AlignLeft | Qt::AlignBottom, opt.text);
      painter->restore();
      return;
    }
    const auto text = opt.text;
    opt.text.clear();
    opt.icon = QIcon();
    const auto* widget = opt.widget;
    auto* style = widget != nullptr ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
    const bool selected = (opt.state & QStyle::State_Selected) != 0;
    const bool enabled = (opt.state & QStyle::State_Enabled) != 0;
    const auto swatch = index.data(kPreviewRole).value<QPixmap>();
    const QRect swatch_rect(opt.rect.left() + 6, opt.rect.top() + (opt.rect.height() - kSwatchHeight) / 2,
                            kSwatchWidth, kSwatchHeight);
    if (!swatch.isNull()) {
      painter->setOpacity(enabled ? 1.0 : 0.4);
      painter->drawPixmap(swatch_rect, swatch);
      painter->setOpacity(1.0);
    }
    const auto text_rect = opt.rect.adjusted(kSwatchWidth + 16, 5, -6, -5);
    const auto subtitle = index.data(kSubtitleRole).toString();
    const QFontMetrics metrics(opt.font);
    const auto primary = !enabled ? colors.text_disabled : selected ? colors.list_selection_text : colors.text_primary;
    const auto secondary = !enabled ? colors.text_disabled : selected ? colors.list_selection_text : colors.text_secondary;
    painter->setPen(primary);
    const auto name_rect = subtitle.isEmpty() ? text_rect : text_rect.adjusted(0, 0, 0, -text_rect.height() / 2);
    painter->drawText(name_rect, Qt::AlignLeft | (subtitle.isEmpty() ? Qt::AlignVCenter : Qt::AlignBottom),
                      metrics.elidedText(text, Qt::ElideRight, name_rect.width()));
    if (!subtitle.isEmpty()) {
      auto small = opt.font;
      if (small.pointSizeF() > 0) {
        small.setPointSizeF(std::max(6.0, small.pointSizeF() * 0.9));
      } else if (small.pixelSize() > 0) {
        small.setPixelSize(std::max(8, small.pixelSize() * 9 / 10));
      }
      painter->setFont(small);
      painter->setPen(secondary);
      const auto subtitle_rect = text_rect.adjusted(0, text_rect.height() / 2 + 1, 0, 0);
      painter->drawText(subtitle_rect, Qt::AlignLeft | Qt::AlignTop,
                        QFontMetrics(small).elidedText(subtitle, Qt::ElideRight, subtitle_rect.width()));
    }
    painter->restore();
  }
};

// A message box that shows theme-supplied text (a theme's name) literally: rich text
// from a downloaded theme must never reach the box's HTML renderer.
QMessageBox::StandardButton theme_message(QWidget* parent, QMessageBox::Icon icon, const QString& title,
                                          const QString& text, QMessageBox::StandardButtons buttons,
                                          const QString& object_name, const QString& accept_text = {}) {
  QMessageBox box(icon, title, text, buttons, parent);
  box.setTextFormat(Qt::PlainText);
  box.setObjectName(object_name);
  QAbstractButton* accept = nullptr;
  if (!accept_text.isEmpty()) {
    accept = box.addButton(accept_text, QMessageBox::AcceptRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(accept));
  }
  exec_dialog(box);
  if (accept != nullptr) {
    return box.clickedButton() == accept ? QMessageBox::Ok : QMessageBox::Cancel;
  }
  return box.standardButton(box.clickedButton());
}

QString newer_patchy_text(const QString& name, const ThemeInfo& info) {
  return MainWindow::tr("\"%1\" is made for Patchy %2 or later. This is Patchy %3, so parts of the theme may not "
                        "show or work as intended.")
      .arg(name, info.min_patchy_version, running_patchy_version());
}

class ThemesPage final {
 public:
  using ConfirmScripts = std::function<bool(QWidget*, const QString&)>;
  ThemesPage(MainWindow& window, QDialog& dialog, ConfirmScripts confirm_scripts)
      : window_(window), dialog_(dialog), confirm_scripts_(std::move(confirm_scripts)) {
    entry_preference_ = ThemeManager::instance().preference();
    entry_custom_id_ = ThemeManager::instance().active_custom_theme_id();
  }

  void build(QVBoxLayout* layout, const std::function<void(QFrame*)>& configure_panel);
  void populate();
  void commit();
  void revert();

 private:
  [[nodiscard]] QString current_token() const;
  [[nodiscard]] QString current_custom_id() const;
  [[nodiscard]] const CustomTheme* current_custom_theme() const;
  [[nodiscard]] QString display_name(const QString& token) const;
  // Lists every theme again (the bundled set, then the user's folder) and selects
  // `token` (the first entry when it is gone), signals blocked.
  void rebuild_list(const QString& token);
  void add_header(const QString& text);
  void add_theme_row(const QString& token, const QString& name, const QString& subtitle, const QPixmap& swatch,
                     const QString& tooltip, bool enabled);
  void add_custom_theme_row(const QString& id, const ThemeLoadResult& result);
  // The list holds package summaries (theme.json and backdrop); this loads the package
  // `id` in full before it is shown in detail or applied. A full load that fails turns
  // its row into a "Could not be loaded" one and returns null.
  const CustomTheme* ensure_full_theme(const QString& id);
  void select_token(const QString& token);
  // The combo-era "apply what is shown": a live preview, never persisted here.
  void apply_selection();
  void refresh_details();
  void refresh_scripts_check();
  void warn_if_needs_newer_patchy();

  void import_theme();
  void export_theme();
  void export_theme_package();
  void delete_theme();
  void reload_themes();

  MainWindow& window_;
  QDialog& dialog_;
  ConfirmScripts confirm_scripts_;  // MainWindow::confirm_theme_scripts
  ColorSchemePreference entry_preference_ = ColorSchemePreference::FollowSystem;
  std::optional<QString> entry_custom_id_;
  // Summaries until ensure_full_theme loads one in full.
  QHash<QString, CustomTheme> custom_themes_;
  // Run-scripts edits per theme id, stored on OK.
  QHash<QString, bool> script_choices_;
  // Themes whose "needs a newer Patchy" warning this dialog already showed.
  QSet<QString> warned_ids_;
  bool populated_ = false;
  bool committed_ = false;

  QListWidget* list_ = nullptr;
  QLabel* preview_ = nullptr;
  QLabel* name_ = nullptr;
  QLabel* meta_ = nullptr;
  QLabel* compatibility_ = nullptr;
  QCheckBox* scripts_check_ = nullptr;
  QLabel* scripts_note_ = nullptr;
  QCheckBox* sounds_check_ = nullptr;
  QCheckBox* effects_check_ = nullptr;
  QPushButton* delete_button_ = nullptr;
};

void ThemesPage::build(QVBoxLayout* layout, const std::function<void(QFrame*)>& configure_panel) {
  auto* page = layout->parentWidget();
  auto* group = new QFrame(page);
  group->setObjectName(QStringLiteral("preferencesThemesGroup"));
  configure_panel(group);
  auto* group_layout = new QVBoxLayout(group);
  group_layout->setContentsMargins(12, 12, 12, 12);
  group_layout->setSpacing(10);

  auto* columns = new QHBoxLayout();
  columns->setSpacing(14);
  list_ = new QListWidget(group);
  list_->setObjectName(QStringLiteral("preferencesThemeList"));
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list_->setItemDelegate(new ThemeListDelegate(list_));
  list_->setMinimumSize(180, 260);
  list_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  columns->addWidget(list_, 5);

  auto* details = new QWidget(group);
  details->setObjectName(QStringLiteral("preferencesThemeDetails"));
  auto* details_layout = new QVBoxLayout(details);
  details_layout->setContentsMargins(0, 0, 0, 0);
  details_layout->setSpacing(6);
  preview_ = new QLabel(details);
  preview_->setObjectName(QStringLiteral("preferencesThemePreview"));
  preview_->setFixedSize(kPreviewWidth, kPreviewHeight);
  details_layout->addWidget(preview_);
  // Theme files supply these texts: plain text only, never rich text.
  name_ = new QLabel(details);
  name_->setObjectName(QStringLiteral("preferencesThemeName"));
  name_->setTextFormat(Qt::PlainText);
  name_->setWordWrap(true);
  auto name_font = name_->font();
  name_font.setBold(true);
  if (name_font.pointSizeF() > 0) {
    name_font.setPointSizeF(name_font.pointSizeF() * 1.2);
  }
  name_->setFont(name_font);
  details_layout->addWidget(name_);
  meta_ = new QLabel(details);
  meta_->setObjectName(QStringLiteral("preferencesThemeMeta"));
  meta_->setTextFormat(Qt::PlainText);
  meta_->setWordWrap(true);
  set_themed_style(*meta_, QStringLiteral("color: @text_secondary;"));
  details_layout->addWidget(meta_);
  compatibility_ = new QLabel(details);
  compatibility_->setObjectName(QStringLiteral("preferencesThemeCompatibility"));
  compatibility_->setTextFormat(Qt::PlainText);
  compatibility_->setWordWrap(true);
  set_themed_style(*compatibility_, QStringLiteral("color: @console_warning_text;"));
  compatibility_->hide();
  details_layout->addWidget(compatibility_);
  // Per theme: the box shows the stored answer of the theme the list shows (a theme
  // downloaded from anywhere must never inherit another theme's yes). Edits wait in
  // script_choices_ and are stored on OK, before the theme commit restarts the host.
  scripts_check_ = new WrappingCheckBox(MainWindow::tr("Run this theme's scripts"), details);
  scripts_check_->setObjectName(QStringLiteral("preferencesRunThemeScriptsCheck"));
  scripts_check_->setEnabled(false);
  details_layout->addWidget(scripts_check_);
  // Shown while the selected theme has scripts: what they cannot do, and whom to trust.
  scripts_note_ = new QLabel(MainWindow::tr("Theme scripts cannot open your files or documents or use the "
                                            "internet. But to be safe, only allow them in themes from people "
                                            "you trust."),
                             details);
  scripts_note_->setObjectName(QStringLiteral("preferencesThemeScriptsNote"));
  scripts_note_->setTextFormat(Qt::PlainText);
  scripts_note_->setWordWrap(true);
  set_themed_style(*scripts_note_, QStringLiteral("color: @text_secondary;"));
  scripts_note_->hide();
  details_layout->addWidget(scripts_note_);
  details_layout->addStretch(1);
  columns->addWidget(details, 4);
  group_layout->addLayout(columns, 1);

#ifndef Q_OS_WASM
  // The theme buttons are desktop-only: wasm has no AppData store to hold the
  // imported files (user_themes_directory() is empty there).
  const auto button = [group](const QString& text, const char* name, const QString& tooltip = {}) {
    auto* push = new QPushButton(text, group);
    push->setObjectName(QLatin1String(name));
    push->setAutoDefault(false);
    if (!tooltip.isEmpty()) {
      push->setToolTip(tooltip);
    }
    return push;
  };
  auto* import_button = button(MainWindow::tr("Import Theme..."), "preferencesImportThemeButton");
  auto* export_button = button(MainWindow::tr("Export Theme..."), "preferencesExportThemeButton");
  auto* reload_button =
      button(MainWindow::tr("Reload Themes"), "preferencesReloadThemesButton",
             MainWindow::tr("Re-read the theme files in the themes folder and apply the selected one."));
  delete_button_ = button(MainWindow::tr("Delete Theme..."), "preferencesDeleteThemeButton");
  auto* folder_button = button(MainWindow::tr("Open Themes Folder"), "preferencesOpenThemesFolderButton");
  auto* package_button =
      button(MainWindow::tr("Export Theme Package..."), "preferencesExportThemePackageButton",
             MainWindow::tr("Save the selected theme as a .zip package, with its icons, images and sounds."));
  auto* icons_button = button(
      MainWindow::tr("Export Icon Template..."), "preferencesExportIconTemplateButton",
      MainWindow::tr("Write every built-in icon as an SVG file into a folder, to start a theme's icons/ folder from."));
  // The buttons flow onto as many rows as the page width needs: fixed rows outgrew the
  // page in the longer languages (the scroll area clips rather than scrolls sideways) and
  // set the dialog's minimum width (GitHub issue 451).
  auto* buttons = new QWidget(group);
  buttons->setObjectName(QStringLiteral("preferencesThemeButtons"));
  auto* buttons_layout = new FlowLayout(buttons, 6, 6);
  buttons_layout->setContentsMargins(0, 0, 0, 0);
  for (auto* push : {import_button, export_button, reload_button, delete_button_, folder_button, package_button,
                     icons_button}) {
    buttons_layout->addWidget(push);
  }
  group_layout->addWidget(buttons);
  delete_button_->setEnabled(false);
  QObject::connect(import_button, &QPushButton::clicked, &dialog_, [this] { import_theme(); });
  QObject::connect(export_button, &QPushButton::clicked, &dialog_, [this] { export_theme(); });
  QObject::connect(reload_button, &QPushButton::clicked, &dialog_, [this] { reload_themes(); });
  QObject::connect(delete_button_, &QPushButton::clicked, &dialog_, [this] { delete_theme(); });
  QObject::connect(package_button, &QPushButton::clicked, &dialog_, [this] { export_theme_package(); });
  QObject::connect(folder_button, &QPushButton::clicked, &dialog_, [this] {
    const auto themes_dir = user_themes_directory();
    if (themes_dir.isEmpty() || !QDir().mkpath(themes_dir) ||
        !QDesktopServices::openUrl(QUrl::fromLocalFile(themes_dir))) {
      show_critical_message(&dialog_, MainWindow::tr("Open Themes Folder"),
                            MainWindow::tr("Could not open the themes folder."),
                            QStringLiteral("openThemesFolderFailedMessageBox"));
    }
  });
  QObject::connect(icons_button, &QPushButton::clicked, &dialog_, [this] {
    const auto folder = QFileDialog::getExistingDirectory(&dialog_, MainWindow::tr("Export Icon Template"),
                                                          user_themes_directory(), QFileDialog::ShowDirsOnly);
    if (folder.isEmpty()) {
      return;
    }
    QString error;
    const int written = export_icon_template(folder, &error);
    if (written < 0) {
      show_critical_message(&dialog_, MainWindow::tr("Export failed"), error,
                            QStringLiteral("exportIconTemplateFailedMessageBox"));
      return;
    }
    show_information_message(
        &dialog_, MainWindow::tr("Export Icon Template"),
        MainWindow::tr("Wrote %n icon(s) to %1.", nullptr, written).arg(QDir::toNativeSeparators(folder)),
        QStringLiteral("exportIconTemplateDoneMessageBox"));
  });
#endif
  layout->addWidget(group, 1);

  auto* extras = new QFrame(page);
  extras->setObjectName(QStringLiteral("preferencesThemeExtrasGroup"));
  configure_panel(extras);
  auto* extras_layout = new QHBoxLayout(extras);
  extras_layout->setContentsMargins(12, 12, 12, 12);
  extras_layout->setSpacing(18);
  sounds_check_ = new WrappingCheckBox(MainWindow::tr("Play theme sounds"), extras);
  sounds_check_->setObjectName(QStringLiteral("preferencesPlayThemeSoundsCheck"));
  sounds_check_->setToolTip(MainWindow::tr("Play the sounds a theme package includes, for example when a save "
                                           "finishes. Themes without sounds stay silent."));
  sounds_check_->setChecked(theme_sounds_enabled());
  effects_check_ = new WrappingCheckBox(MainWindow::tr("Play theme effects"), extras);
  effects_check_->setObjectName(QStringLiteral("preferencesPlayThemeEffectsCheck"));
  effects_check_->setToolTip(MainWindow::tr(
      "Play the short animations a theme shows for events, such as fireworks when a document opens."));
  effects_check_->setChecked(theme_effects_enabled());
  extras_layout->addWidget(sounds_check_);
  extras_layout->addWidget(effects_check_);
  extras_layout->addStretch(1);
  layout->addWidget(extras);

  QObject::connect(list_, &QListWidget::currentRowChanged, &dialog_, [this] {
    apply_selection();
    // Picking a theme previews how it sounds too: its startup sound, under the dialog's
    // own Play theme sounds box (not yet stored).
    if (const auto* custom = current_custom_theme(); custom != nullptr) {
      play_theme_startup_preview(custom->assets.get(), sounds_check_->isChecked());
    }
  });
  QObject::connect(scripts_check_, &QCheckBox::toggled, &dialog_, [this](bool checked) {
    if (const auto* custom = current_custom_theme(); custom != nullptr && custom->assets != nullptr &&
                                                     custom->assets->has_scripts()) {
      script_choices_.insert(current_custom_id(), checked);
    }
  });
  // A trust prompt answered while a theme is previewed here.
  QObject::connect(&ThemeScriptHost::instance(), &ThemeScriptHost::trust_changed, &dialog_,
                   [this] { refresh_scripts_check(); });
}

QString ThemesPage::current_token() const {
  const auto* item = list_->currentItem();
  return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
}

QString ThemesPage::current_custom_id() const {
  const auto token = current_token();
  return token.startsWith(kCustomPrefix) ? token.mid(kCustomPrefix.size()) : QString();
}

const CustomTheme* ThemesPage::current_custom_theme() const {
  const auto found = custom_themes_.constFind(current_custom_id());
  return found != custom_themes_.constEnd() ? &found.value() : nullptr;
}

QString ThemesPage::display_name(const QString& token) const {
  if (token.startsWith(kCustomPrefix)) {
    const auto id = token.mid(kCustomPrefix.size());
    const auto found = custom_themes_.constFind(id);
    return found != custom_themes_.constEnd() && !found.value().name.isEmpty() ? found.value().name : id;
  }
  switch (color_scheme_preference_from_token(token)) {
    case ColorSchemePreference::FollowSystem:
      return MainWindow::tr("Follow system");
    case ColorSchemePreference::Dark:
      return MainWindow::tr("Dark");
    case ColorSchemePreference::Light:
      return MainWindow::tr("Light");
  }
  return token;
}

void ThemesPage::add_header(const QString& text) {
  auto* item = new QListWidgetItem(text, list_);
  item->setData(kHeaderRole, true);
  item->setFlags(Qt::NoItemFlags);
}

void ThemesPage::add_theme_row(const QString& token, const QString& name, const QString& subtitle,
                               const QPixmap& swatch, const QString& tooltip, bool enabled) {
  auto* item = new QListWidgetItem(name, list_);
  item->setData(Qt::UserRole, token);
  item->setData(kSubtitleRole, subtitle);
  item->setData(kPreviewRole, swatch);
  item->setToolTip(tooltip);
  item->setFlags(enabled ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags);
}

void ThemesPage::add_custom_theme_row(const QString& id, const ThemeLoadResult& result) {
  const auto dpr = list_->devicePixelRatioF();
  const auto swatch_size = QSize(kSwatchWidth, kSwatchHeight);
  if (!result.theme) {
    // Listed so its author sees why it does not load, but never selectable.
    add_theme_row(kCustomPrefix + id, id, MainWindow::tr("Could not be loaded"), {}, result.error, false);
    return;
  }
  const auto& custom = *result.theme;
  custom_themes_.insert(id, custom);
  QStringList parts;
  if (!custom.info.version.isEmpty()) {
    parts << MainWindow::tr("Version %1").arg(custom.info.version);
  }
  if (!custom.info.author.isEmpty()) {
    parts << MainWindow::tr("by %1").arg(custom.info.author);
  }
  if (parts.isEmpty()) {
    parts << (is_bundled_theme_id(id) ? MainWindow::tr("Built into Patchy")
              : custom.assets != nullptr ? MainWindow::tr("Theme package")
                                         : MainWindow::tr("Theme file"));
  }
  if (theme_needs_newer_patchy(custom.info)) {
    parts << MainWindow::tr("Needs Patchy %1").arg(custom.info.min_patchy_version);
  }
  const auto backdrop = custom.assets != nullptr ? custom.assets->backdrop : QImage();
  add_theme_row(kCustomPrefix + id, custom.name.isEmpty() ? id : custom.name,
                parts.join(QStringLiteral(" · ")), theme_preview_pixmap(&custom.palette, backdrop, swatch_size, dpr),
                is_bundled_theme_id(id) ? QString() : id, true);
}

void ThemesPage::rebuild_list(const QString& token) {
  const QSignalBlocker blocker(list_);
  list_->clear();
  custom_themes_.clear();
  const auto dpr = list_->devicePixelRatioF();
  const auto swatch_size = QSize(kSwatchWidth, kSwatchHeight);
  add_header(MainWindow::tr("Dark and Light"));
  add_theme_row(color_scheme_preference_to_token(ColorSchemePreference::FollowSystem),
                display_name(QStringLiteral("system")), MainWindow::tr("Matches the system's light or dark setting"),
                theme_preview_pixmap(nullptr, {}, swatch_size, dpr), {}, true);
  for (const auto preference : {ColorSchemePreference::Dark, ColorSchemePreference::Light}) {
    const auto scheme_token = color_scheme_preference_to_token(preference);
    add_theme_row(scheme_token, display_name(scheme_token), MainWindow::tr("Built into Patchy"),
                  theme_preview_pixmap(&theme(preference == ColorSchemePreference::Dark ? ColorScheme::Dark
                                                                                       : ColorScheme::Light),
                                       {}, swatch_size, dpr),
                  {}, true);
  }
  add_header(MainWindow::tr("Included themes"));
  for (const auto& file_name : bundled_theme_file_names()) {
    const auto id = kBundledThemeIdPrefix + file_name;
    add_custom_theme_row(id, load_theme_summary_by_id(id));
  }
  // .patchytheme files, .zip packages and package folders (theme_file.hpp).
  if (const auto ids = user_theme_ids(); !ids.isEmpty()) {
    add_header(MainWindow::tr("Your themes"));
    for (const auto& id : ids) {
      add_custom_theme_row(id, load_theme_summary_by_id(id));
    }
  }
  select_token(token);
}

const CustomTheme* ThemesPage::ensure_full_theme(const QString& id) {
  const auto found = custom_themes_.find(id);
  if (found == custom_themes_.end()) {
    return nullptr;
  }
  if (found.value().assets == nullptr || !found.value().assets->summary) {
    return &found.value();
  }
  auto result = load_theme_by_id(id);
  if (result.theme) {
    found.value() = std::move(*result.theme);
    return &found.value();
  }
  custom_themes_.erase(found);
  for (int row = 0; row < list_->count(); ++row) {
    auto* item = list_->item(row);
    if (item->data(Qt::UserRole).toString() == kCustomPrefix + id) {
      item->setData(kSubtitleRole, MainWindow::tr("Could not be loaded"));
      item->setToolTip(result.error);
      item->setFlags(Qt::NoItemFlags);
    }
  }
  return nullptr;
}

void ThemesPage::select_token(const QString& token) {
  int first = -1;
  for (int row = 0; row < list_->count(); ++row) {
    const auto* item = list_->item(row);
    if ((item->flags() & Qt::ItemIsSelectable) == 0) {
      continue;
    }
    if (first < 0) {
      first = row;
    }
    if (item->data(Qt::UserRole).toString() == token) {
      list_->setCurrentRow(row);
      return;
    }
  }
  list_->setCurrentRow(first);
}

void ThemesPage::populate() {
  if (populated_) {
    return;
  }
  populated_ = true;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  rebuild_list(entry_custom_id_ ? kCustomPrefix + *entry_custom_id_
                                : color_scheme_preference_to_token(entry_preference_));
  QApplication::restoreOverrideCursor();
  refresh_details();
}

void ThemesPage::apply_selection() {
  const auto token = current_token();
  if (token.isEmpty()) {
    return;
  }
  // One busy cursor from the click to the redrawn details, the package load included.
  std::optional<ThemeApplyScope> apply;
  apply.emplace("Preferences theme choice");
  if (token.startsWith(kCustomPrefix)) {
    const auto* custom = ensure_full_theme(current_custom_id());
    theme_apply_phase("load theme");
    if (custom != nullptr) {
      ThemeManager::instance().set_custom_theme(current_custom_id(), *custom, /*persist=*/false);
    }
  } else {
    ThemeManager::instance().set_preference(color_scheme_preference_from_token(token), /*persist=*/false);
  }
  refresh_details();
  theme_apply_phase("theme details");
  // Before the warning, which can open a message box.
  apply.reset();
  warn_if_needs_newer_patchy();
}

void ThemesPage::warn_if_needs_newer_patchy() {
  const auto* custom = current_custom_theme();
  const auto id = current_custom_id();
  if (custom == nullptr || !theme_needs_newer_patchy(custom->info) || warned_ids_.contains(id) ||
      window_.unattended_automation()) {
    return;
  }
  warned_ids_.insert(id);
  theme_message(&dialog_, QMessageBox::Warning, MainWindow::tr("Newer Patchy Needed"),
                newer_patchy_text(display_name(current_token()), custom->info), QMessageBox::Ok,
                QStringLiteral("themeNeedsNewerPatchyMessageBox"));
}

void ThemesPage::refresh_details() {
  const auto token = current_token();
  const auto* custom = ensure_full_theme(current_custom_id());
  const auto dpr = preview_->devicePixelRatioF();
  const auto preview_size = QSize(kPreviewWidth, kPreviewHeight);
  name_->setText(display_name(token));
  QStringList lines;
  if (custom != nullptr) {
    preview_->setPixmap(theme_preview_pixmap(&custom->palette,
                                             custom->assets != nullptr ? custom->assets->backdrop : QImage(),
                                             preview_size, dpr));
    if (!custom->info.version.isEmpty()) {
      lines << MainWindow::tr("Version %1").arg(custom->info.version);
    }
    if (!custom->info.author.isEmpty()) {
      lines << MainWindow::tr("by %1").arg(custom->info.author);
    }
    lines << (custom->base == ColorScheme::Light ? MainWindow::tr("Based on Light") : MainWindow::tr("Based on Dark"));
    if (const auto* assets = custom->assets.get(); assets != nullptr) {
      QStringList parts;
      if (!assets->icons.isEmpty()) {
        parts << MainWindow::tr("icons");
      }
      if (!assets->backdrop.isNull()) {
        parts << MainWindow::tr("backdrop");
      }
      if (!assets->sounds.isEmpty() ||
          std::any_of(assets->events.cbegin(), assets->events.cend(),
                      [](const ThemeEventAction& action) { return !action.sound.isEmpty(); })) {
        parts << MainWindow::tr("sounds");
      }
      if (std::any_of(assets->events.cbegin(), assets->events.cend(),
                      [](const ThemeEventAction& action) { return !action.effect.isEmpty(); })) {
        parts << MainWindow::tr("effects");
      }
      if (assets->has_scripts()) {
        parts << MainWindow::tr("scripts");
      }
      if (!parts.isEmpty()) {
        lines << MainWindow::tr("Includes: %1").arg(parts.join(QStringLiteral(", ")));
      }
    }
    const bool too_old = theme_needs_newer_patchy(custom->info);
    compatibility_->setVisible(too_old);
    if (too_old) {
      compatibility_->setText(
          MainWindow::tr("Made for Patchy %1 or later. This is Patchy %2, so parts of it may not work.")
              .arg(custom->info.min_patchy_version, running_patchy_version()));
    }
  } else if (token.startsWith(kCustomPrefix)) {
    // A package whose full load failed: its error, as its row's tooltip has it.
    preview_->clear();
    lines << list_->currentItem()->toolTip();
    compatibility_->hide();
  } else {
    const auto preference = color_scheme_preference_from_token(token);
    preview_->setPixmap(theme_preview_pixmap(
        preference == ColorSchemePreference::FollowSystem
            ? nullptr
            : &theme(preference == ColorSchemePreference::Dark ? ColorScheme::Dark : ColorScheme::Light),
        {}, preview_size, dpr));
    lines << (preference == ColorSchemePreference::FollowSystem
                  ? MainWindow::tr("Matches the system's light or dark setting")
                  : MainWindow::tr("Built into Patchy"));
    compatibility_->hide();
  }
  meta_->setText(lines.join(QLatin1Char('\n')));
  if (delete_button_ != nullptr) {
    // A built-in scheme and a bundled theme cannot be deleted.
    delete_button_->setEnabled(custom != nullptr && !is_bundled_theme_id(current_custom_id()));
  }
  refresh_scripts_check();
}

void ThemesPage::refresh_scripts_check() {
  const auto* custom = current_custom_theme();
  const bool scripted = custom != nullptr && custom->assets != nullptr && custom->assets->has_scripts();
  const QSignalBlocker blocker(scripts_check_);
  scripts_check_->setEnabled(scripted);
  scripts_note_->setVisible(scripted);
  if (!scripted) {
    scripts_check_->setChecked(false);
    scripts_check_->setToolTip(MainWindow::tr("The selected theme has no scripts."));
    return;
  }
  // The trust prompt's two sentences (MainWindow::confirm_theme_scripts).
  scripts_check_->setToolTip(
      MainWindow::tr("Theme scripts run in a sandbox: they can play sounds and effects, show pictures and add menu "
                     "commands, but they cannot open or change your files or documents, and they cannot use the "
                     "internet.") +
      QLatin1Char(' ') + MainWindow::tr("Even so, only allow scripts in themes from people you trust."));
  const auto id = current_custom_id();
  const auto choice = script_choices_.constFind(id);
  scripts_check_->setChecked(choice != script_choices_.constEnd()
                                 ? choice.value()
                                 : stored_theme_script_trust(id, theme_scripts_digest(*custom->assets)) ==
                                       ThemeScriptTrust::Allowed);
}

void ThemesPage::reload_themes() {
  rebuild_list(current_token());
  // A re-read file may hold new colors under the same name, so apply even when the
  // selection did not move.
  apply_selection();
}

void ThemesPage::delete_theme() {
  const auto file_name = current_custom_id();
  if (file_name.isEmpty() || is_bundled_theme_id(file_name)) {
    return;
  }
  const bool package_folder = is_theme_package_directory_id(file_name);
  const auto name = display_name(current_token());
  if (theme_message(&dialog_, QMessageBox::Question, MainWindow::tr("Delete Theme"),
                    (package_folder ? MainWindow::tr("Delete the theme \"%1\"? Its folder is moved to the trash.")
                                    : MainWindow::tr("Delete the theme \"%1\"? Its file is removed from the themes "
                                                     "folder."))
                        .arg(name),
                    QMessageBox::Cancel, QStringLiteral("preferencesDeleteThemeConfirm"),
                    MainWindow::tr("Delete")) != QMessageBox::Ok) {
    return;
  }
  const auto themes_dir = user_themes_directory();
  // The live preview watches a folder package; stop watching before it goes.
  if (ThemeManager::instance().active_custom_theme_id() == file_name) {
    ThemeManager::instance().clear_custom_theme(/*persist=*/false);
  }
  // A folder is never deleted outright: it goes to the trash, so an author's work can
  // come back.
  const auto target = themes_dir.isEmpty() ? QString() : QDir(themes_dir).filePath(file_name);
  if (target.isEmpty() || !(package_folder ? QFile::moveToTrash(target) : QFile::remove(target))) {
    show_critical_message(&dialog_, MainWindow::tr("Delete Theme"),
                          MainWindow::tr("Could not delete \"%1\".").arg(file_name),
                          QStringLiteral("deleteThemeFailedMessageBox"));
    return;
  }
  rebuild_list({});
  apply_selection();
}

// Import copies the picked file into user_themes_directory() and previews it at once,
// whatever the dialog's answer: the file itself is not a preference, so only the live
// preview is undone by a Cancel.
void ThemesPage::import_theme() {
  const auto path = get_open_file_name(&dialog_, MainWindow::tr("Import Theme"), QString(),
                                       MainWindow::tr("Patchy themes (*.patchytheme *.zip)"));
  if (path.isEmpty()) {
    return;
  }
  QFile source(path);
  if (!source.open(QIODevice::ReadOnly) || source.size() > ThemePackageLimits{}.max_package_bytes) {
    show_critical_message(&dialog_, MainWindow::tr("Import failed"),
                          MainWindow::tr("Could not open \"%1\".").arg(path),
                          QStringLiteral("importThemeFailedMessageBox"));
    return;
  }
  // A zip is a theme package, copied as it is; anything else is theme JSON.
  const auto bytes = source.readAll();
  auto result = is_theme_package_zip_name(path) ? load_theme_package_zip(bytes) : load_theme_from_json(bytes);
  if (!result.theme) {
    show_critical_message(&dialog_, MainWindow::tr("Import failed"), result.error,
                          QStringLiteral("importThemeFailedMessageBox"));
    return;
  }
  const auto name = result.theme->name.isEmpty() ? QFileInfo(path).fileName() : result.theme->name;
  if (theme_needs_newer_patchy(result.theme->info) && !window_.unattended_automation() &&
      theme_message(&dialog_, QMessageBox::Warning, MainWindow::tr("Newer Patchy Needed"),
                    newer_patchy_text(name, result.theme->info) + QStringLiteral("\n\n") +
                        MainWindow::tr("Import it anyway?"),
                    QMessageBox::Cancel, QStringLiteral("importThemeNeedsNewerPatchyMessageBox"),
                    MainWindow::tr("Import Anyway")) != QMessageBox::Ok) {
    return;
  }
  const auto themes_dir = user_themes_directory();
  if (themes_dir.isEmpty() || !QDir().mkpath(themes_dir)) {
    show_critical_message(&dialog_, MainWindow::tr("Import failed"),
                          MainWindow::tr("Could not create the themes folder."),
                          QStringLiteral("importThemeFailedMessageBox"));
    return;
  }
  // Copied under the picked file's own name, de-duplicated on a collision, so it no
  // longer depends on where it came from and load_saved_preference() finds it by name.
  const QDir dir(themes_dir);
  const QFileInfo base_info(path);
  const auto stem = base_info.completeBaseName();
  const auto suffix = base_info.suffix();
  auto file_name = base_info.fileName();
  for (int attempt = 2; QFileInfo::exists(dir.filePath(file_name)); ++attempt) {
    file_name = QStringLiteral("%1-%2.%3").arg(stem).arg(attempt).arg(suffix);
  }
  try {
    write_file_bytes_atomically(to_filesystem_path(dir.filePath(file_name)),
                                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                                              static_cast<std::size_t>(bytes.size())),
                                "Could not create the theme file", "Could not write the theme file");
  } catch (const std::exception& error) {
    show_critical_message(&dialog_, MainWindow::tr("Import failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("importThemeFailedMessageBox"));
    return;
  }
  // A package with scripts asks once here, so selecting it below does not ask again.
  if (result.theme->assets != nullptr && result.theme->assets->has_scripts()) {
    store_theme_script_trust(file_name, theme_scripts_digest(*result.theme->assets),
                             confirm_scripts_(&dialog_, name));
  }
  rebuild_list(kCustomPrefix + file_name);
  apply_selection();
}

// Export writes the palette the list shows. It opens in the themes folder, suggests the
// shown theme's own name, and when the file lands in that folder lists and selects it at
// once, so "export a built-in, then edit it" is one step.
void ThemesPage::export_theme() {
  auto suggested_name = display_name(current_token());
  if (suggested_name.isEmpty()) {
    suggested_name = MainWindow::tr("Theme", "Default file name offered when exporting a theme; the save dialog "
                                             "appends the extension.");
  }
  auto initial_path = suggested_name + QStringLiteral(".patchytheme");
  const auto themes_dir = user_themes_directory();
  if (!themes_dir.isEmpty() && QDir().mkpath(themes_dir)) {
    initial_path = QDir(themes_dir).filePath(initial_path);
  }
  const auto path = get_save_file_name(&dialog_, MainWindow::tr("Export Theme"), initial_path,
                                       MainWindow::tr("Patchy theme (*.patchytheme)"), nullptr,
                                       QStringLiteral("exportThemeFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  const auto json = serialize_theme_to_json(theme(), active_color_scheme(), QFileInfo(path).completeBaseName());
  try {
    write_file_bytes_atomically(to_filesystem_path(path),
                                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(json.constData()),
                                                              static_cast<std::size_t>(json.size())),
                                "Could not create the theme file", "Could not write the theme file");
  } catch (const std::exception& error) {
    show_critical_message(&dialog_, MainWindow::tr("Export failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("exportThemeFailedMessageBox"));
    return;
  }
  const QFileInfo written(path);
  if (themes_dir.isEmpty() ||
      QDir::cleanPath(written.absolutePath()) != QDir::cleanPath(QDir(themes_dir).absolutePath())) {
    return;
  }
  rebuild_list(kCustomPrefix + written.fileName());
  apply_selection();
}

// Export Theme Package writes the theme the list shows as a .zip: theme.json from the
// live palette, plus the icons, images and sounds of a package theme.
void ThemesPage::export_theme_package() {
  auto suggested_name = display_name(current_token());
  const auto* custom = current_custom_theme();
  const ThemeAssets* assets = custom != nullptr ? custom->assets.get() : nullptr;
  if (suggested_name.isEmpty()) {
    suggested_name = MainWindow::tr("Theme", "Default file name offered when exporting a theme; the save dialog "
                                             "appends the extension.");
  }
  const auto path = get_save_file_name(&dialog_, MainWindow::tr("Export Theme Package"),
                                       suggested_name + QStringLiteral(".zip"),
                                       MainWindow::tr("Patchy theme package (*.zip)"), nullptr,
                                       QStringLiteral("exportThemePackageFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  const auto zip =
      serialize_theme_package_zip(theme(), active_color_scheme(), QFileInfo(path).completeBaseName(), assets);
  try {
    write_file_bytes_atomically(to_filesystem_path(path),
                                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(zip.constData()),
                                                              static_cast<std::size_t>(zip.size())),
                                "Could not create the theme file", "Could not write the theme file");
  } catch (const std::exception& error) {
    show_critical_message(&dialog_, MainWindow::tr("Export failed"), QString::fromUtf8(error.what()),
                          QStringLiteral("exportThemeFailedMessageBox"));
  }
}

void ThemesPage::commit() {
  committed_ = true;
  // Before the theme commit below, which restarts the host under the stored answers.
  for (auto it = script_choices_.cbegin(); it != script_choices_.cend(); ++it) {
    if (const auto found = custom_themes_.constFind(it.key());
        found != custom_themes_.constEnd() && found.value().assets != nullptr) {
      store_theme_script_trust(it.key(), theme_scripts_digest(*found.value().assets), it.value());
    }
  }
  // Never visited: the theme did not change.
  if (populated_) {
    // No restart notice: the scheme is already applied, unlike interface scale.
    const auto token = current_token();
    if (token.startsWith(kCustomPrefix)) {
      // The list already previewed it: save the choice rather than restyle again,
      // unless its scripts answer changed here (the apply restarts the script host).
      auto& manager = ThemeManager::instance();
      if (manager.active_custom_theme_id() == current_custom_id() &&
          !script_choices_.contains(current_custom_id())) {
        manager.persist_active_custom_theme();
      } else if (const auto* custom = current_custom_theme(); custom != nullptr) {
        manager.set_custom_theme(current_custom_id(), *custom, /*persist=*/true);
      }
    } else if (!token.isEmpty()) {
      ThemeManager::instance().set_preference(color_scheme_preference_from_token(token), /*persist=*/true);
    }
  }
  set_theme_sounds_enabled(sounds_check_->isChecked());
  set_theme_effects_enabled(effects_check_->isChecked());
}

// The list previews live, so every way out of the dialog that is not OK puts the entry
// theme back.
void ThemesPage::revert() {
  if (committed_ || !populated_) {
    return;
  }
  if (entry_custom_id_) {
    if (const auto* entry = ensure_full_theme(*entry_custom_id_); entry != nullptr) {
      ThemeManager::instance().set_custom_theme(*entry_custom_id_, *entry, /*persist=*/false);
      return;
    }
  }
  ThemeManager::instance().set_preference(entry_preference_, /*persist=*/false);
}

}  // namespace

PreferencesThemesPage MainWindow::build_preferences_themes_page(QDialog& dialog, QVBoxLayout* layout,
                                                                const std::function<void(QFrame*)>& configure_panel) {
  auto page = std::make_shared<ThemesPage>(*this, dialog, &MainWindow::confirm_theme_scripts);
  page->build(layout, configure_panel);
  return {[page] { page->populate(); }, [page] { page->commit(); }, [page] { page->revert(); }};
}

}  // namespace patchy::ui
