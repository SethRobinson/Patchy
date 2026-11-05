#include "ui/adjustment_presets.hpp"

#include "support/atomic_file_write.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/qt_paths.hpp"

#include <QBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QInputDialog>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSizePolicy>
#include <QStandardPaths>

#include <algorithm>
#include <array>
#include <exception>
#include <span>
#include <utility>

namespace patchy::ui {

namespace {

// A preset file is a few hundred bytes; Import refuses anything far larger.
constexpr qint64 kMaxImportedPresetBytes = 64 * 1024;

QString preset_kind_folder(AdjustmentPresetKind kind) {
  switch (kind) {
    case AdjustmentPresetKind::HueSaturation:
      return QStringLiteral("hue-saturation");
    case AdjustmentPresetKind::Levels:
      return QStringLiteral("levels");
    case AdjustmentPresetKind::Curves:
      return QStringLiteral("curves");
    case AdjustmentPresetKind::BlackWhite:
      return QStringLiteral("black-white");
  }
  return QStringLiteral("other");
}

HueSaturationAdjustment colorize_preset(int hue, int saturation, int lightness) {
  HueSaturationAdjustment settings;
  settings.colorize = true;
  settings.colorize_hue = hue;
  settings.colorize_saturation = saturation;
  settings.colorize_lightness = lightness;
  return settings;
}

HueSaturationAdjustment master_preset(int hue, int saturation, int lightness) {
  HueSaturationAdjustment settings;
  settings.hue_shift = hue;
  settings.saturation_delta = saturation;
  settings.lightness_delta = lightness;
  return settings;
}

// `band`: 0 reds, 1 yellows, 2 greens, 3 cyans, 4 blues, 5 magentas.
HueSaturationAdjustment range_preset(std::size_t band, int hue, int saturation, int lightness) {
  HueSaturationAdjustment settings;
  settings.bands[band].hue_shift = hue;
  settings.bands[band].saturation_delta = saturation;
  settings.bands[band].lightness_delta = lightness;
  return settings;
}

LevelsAdjustment levels_preset(int black, int gamma_percent, int white, int black_output = 0,
                               int white_output = 255) {
  LevelsAdjustment settings;
  settings.black_input = black;
  settings.gamma_percent = gamma_percent;
  settings.white_input = white;
  settings.black_output = black_output;
  settings.white_output = white_output;
  return settings;
}

QString unique_user_preset_path(AdjustmentPresetKind kind, const QString& name) {
  auto path = user_adjustment_preset_path(kind, name);
  for (int number = 2; !path.isEmpty() && QFileInfo::exists(path) && number < 1000; ++number) {
    path = user_adjustment_preset_path(kind, name + QStringLiteral(" ") + QString::number(number));
  }
  return path;
}

}  // namespace

QString user_adjustment_presets_directory(AdjustmentPresetKind kind) {
  auto root = qEnvironmentVariable("PATCHY_ADJUSTMENT_PRESETS_DIR");
  if (root.isEmpty()) {
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
      return {};
    }
    root = base + QStringLiteral("/adjustment-presets");
  }
  return QDir(root).absoluteFilePath(preset_kind_folder(kind));
}

QString adjustment_preset_suffix(AdjustmentPresetKind kind) {
  switch (kind) {
    case AdjustmentPresetKind::HueSaturation:
      return QStringLiteral("ahu");
    case AdjustmentPresetKind::Levels:
      return QStringLiteral("alv");
    case AdjustmentPresetKind::Curves:
      return QStringLiteral("acv");
    case AdjustmentPresetKind::BlackWhite:
      return QStringLiteral("blw");
  }
  return {};
}

QString adjustment_preset_file_filter(AdjustmentPresetKind kind) {
  switch (kind) {
    case AdjustmentPresetKind::HueSaturation:
      return QObject::tr("Photoshop Hue/Saturation Preset (*.ahu)");
    case AdjustmentPresetKind::Levels:
      return QObject::tr("Photoshop Levels Preset (*.alv)");
    case AdjustmentPresetKind::Curves:
      return QObject::tr("Photoshop Curves Preset (*.acv)");
    case AdjustmentPresetKind::BlackWhite:
      return QObject::tr("Photoshop Black & White Preset (*.blw)");
  }
  return {};
}

std::vector<UserAdjustmentPreset> user_adjustment_presets(AdjustmentPresetKind kind) {
  std::vector<UserAdjustmentPreset> presets;
  const auto folder = user_adjustment_presets_directory(kind);
  if (folder.isEmpty()) {
    return presets;
  }
  const QDir directory(folder);
  const auto files = directory.entryInfoList({QStringLiteral("*.") + adjustment_preset_suffix(kind)},
                                             QDir::Files | QDir::Readable);
  for (const auto& file : files) {
    presets.push_back(UserAdjustmentPreset{file.completeBaseName(), file.absoluteFilePath()});
  }
  std::sort(presets.begin(), presets.end(), [](const UserAdjustmentPreset& left, const UserAdjustmentPreset& right) {
    const auto order = QString::compare(left.name, right.name, Qt::CaseInsensitive);
    return order != 0 ? order < 0 : left.name < right.name;
  });
  return presets;
}

QString user_adjustment_preset_path(AdjustmentPresetKind kind, const QString& name) {
  QString stem;
  stem.reserve(name.size());
  for (const auto character : name) {
    const auto code = character.unicode();
    const bool forbidden = code < 0x20 || code == 0x7F || QStringLiteral("<>:\"/\\|?*").contains(character);
    stem.append(forbidden ? QChar(u'_') : character);
  }
  // Windows drops trailing dots and spaces from file names, and a leading dot hides a file.
  while (!stem.isEmpty() && (stem.front() == u' ' || stem.front() == u'.')) {
    stem.remove(0, 1);
  }
  while (!stem.isEmpty() && (stem.back() == u' ' || stem.back() == u'.')) {
    stem.chop(1);
  }
  stem.truncate(120);
  const auto folder = user_adjustment_presets_directory(kind);
  if (stem.isEmpty() || folder.isEmpty()) {
    return {};
  }
  // Device names Windows reserves, with or without an extension.
  static const QStringList kReserved{QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),
                                     QStringLiteral("NUL"),  QStringLiteral("COM1"), QStringLiteral("COM2"),
                                     QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("LPT1"),
                                     QStringLiteral("LPT2"), QStringLiteral("LPT3")};
  if (kReserved.contains(stem.section(u'.', 0, 0), Qt::CaseInsensitive)) {
    stem.prepend(u'_');
  }
  return QDir(folder).absoluteFilePath(stem + QStringLiteral(".") + adjustment_preset_suffix(kind));
}

std::span<const HueSaturationPreset> builtin_hue_saturation_presets() {
  static const std::array<HueSaturationPreset, 15> presets{{
      {"hue_saturation.default", QT_TRANSLATE_NOOP("QObject", "Default"), HueSaturationAdjustment{}},
      {"hue_saturation.sepia_wash", QT_TRANSLATE_NOOP("QObject", "Sepia Wash"), colorize_preset(32, 28, 0)},
      {"hue_saturation.blueprint", QT_TRANSLATE_NOOP("QObject", "Blueprint"), colorize_preset(200, 40, 0)},
      {"hue_saturation.golden_mono", QT_TRANSLATE_NOOP("QObject", "Golden Monochrome"), colorize_preset(42, 45, 0)},
      {"hue_saturation.moonlight", QT_TRANSLATE_NOOP("QObject", "Moonlight"), colorize_preset(222, 30, -5)},
      {"hue_saturation.faded_film", QT_TRANSLATE_NOOP("QObject", "Faded Film"), master_preset(0, -35, 5)},
      {"hue_saturation.warm_vintage", QT_TRANSLATE_NOOP("QObject", "Warm Vintage"), master_preset(-6, -15, 3)},
      {"hue_saturation.vivid_film", QT_TRANSLATE_NOOP("QObject", "Vivid Film"), master_preset(6, 40, 0)},
      {"hue_saturation.soft_pastel", QT_TRANSLATE_NOOP("QObject", "Soft Pastel"), master_preset(0, -40, 18)},
      {"hue_saturation.cool_shift", QT_TRANSLATE_NOOP("QObject", "Cool Shift"), master_preset(8, -10, 0)},
      {"hue_saturation.warm_shift", QT_TRANSLATE_NOOP("QObject", "Warm Shift"), master_preset(-8, 10, 3)},
      {"hue_saturation.warm_skin", QT_TRANSLATE_NOOP("QObject", "Warm Skin Tones"), range_preset(0, -4, 14, 0)},
      {"hue_saturation.autumn_leaves", QT_TRANSLATE_NOOP("QObject", "Autumn Leaves"), range_preset(2, -55, 35, 0)},
      {"hue_saturation.aqua_skies", QT_TRANSLATE_NOOP("QObject", "Aqua Skies"), range_preset(4, -25, 30, 0)},
      {"hue_saturation.deep_ocean", QT_TRANSLATE_NOOP("QObject", "Deep Ocean"), range_preset(3, 12, 35, -8)},
  }};
  return presets;
}

bool hue_saturation_presets_match(const HueSaturationAdjustment& left, const HueSaturationAdjustment& right) {
  if (left.colorize != right.colorize) {
    return false;
  }
  if (left.colorize) {
    return left.colorize_hue % 360 == right.colorize_hue % 360 &&
           left.colorize_saturation == right.colorize_saturation &&
           left.colorize_lightness == right.colorize_lightness;
  }
  return left.hue_shift == right.hue_shift && left.saturation_delta == right.saturation_delta &&
         left.lightness_delta == right.lightness_delta && left.bands == right.bands;
}

std::span<const LevelsPreset> builtin_levels_presets() {
  static const std::array<LevelsPreset, 20> presets{{
      {"levels.default", QT_TRANSLATE_NOOP("QObject", "Default"), levels_preset(0, 100, 255)},
      {"levels.brighten_slightly", QT_TRANSLATE_NOOP("QObject", "Brighten Slightly"), levels_preset(0, 110, 255)},
      {"levels.brighten_medium", QT_TRANSLATE_NOOP("QObject", "Brighten Medium"), levels_preset(0, 130, 255)},
      {"levels.brighten_strong", QT_TRANSLATE_NOOP("QObject", "Brighten Strong"), levels_preset(0, 155, 255)},
      {"levels.darken_slightly", QT_TRANSLATE_NOOP("QObject", "Darken Slightly"), levels_preset(0, 90, 255)},
      {"levels.darken_medium", QT_TRANSLATE_NOOP("QObject", "Darken Medium"), levels_preset(0, 80, 255)},
      {"levels.darken_strong", QT_TRANSLATE_NOOP("QObject", "Darken Strong"), levels_preset(0, 65, 255)},
      {"levels.slight_contrast", QT_TRANSLATE_NOOP("QObject", "Slight Contrast"), levels_preset(12, 100, 243)},
      {"levels.medium_contrast", QT_TRANSLATE_NOOP("QObject", "Medium Contrast"), levels_preset(28, 100, 228)},
      {"levels.strong_contrast", QT_TRANSLATE_NOOP("QObject", "Strong Contrast"), levels_preset(45, 100, 210)},
      {"levels.dramatic_contrast", QT_TRANSLATE_NOOP("QObject", "Dramatic Contrast"), levels_preset(38, 90, 222)},
      {"levels.deep_blacks", QT_TRANSLATE_NOOP("QObject", "Deep Blacks"), levels_preset(25, 100, 255)},
      {"levels.crushed_blacks", QT_TRANSLATE_NOOP("QObject", "Crushed Blacks"), levels_preset(45, 100, 255)},
      {"levels.brilliant_whites", QT_TRANSLATE_NOOP("QObject", "Brilliant Whites"), levels_preset(0, 100, 222)},
      {"levels.silhouette", QT_TRANSLATE_NOOP("QObject", "Silhouette"), levels_preset(20, 70, 220)},
      {"levels.faded_light", QT_TRANSLATE_NOOP("QObject", "Faded Light"), levels_preset(0, 100, 255, 14, 244)},
      {"levels.faded_medium", QT_TRANSLATE_NOOP("QObject", "Faded Medium"), levels_preset(0, 100, 255, 26, 234)},
      {"levels.faded_strong", QT_TRANSLATE_NOOP("QObject", "Faded Strong"), levels_preset(0, 100, 255, 40, 220)},
      {"levels.matte", QT_TRANSLATE_NOOP("QObject", "Matte"), levels_preset(0, 110, 255, 22, 240)},
      {"levels.washed_out", QT_TRANSLATE_NOOP("QObject", "Washed Out"), levels_preset(0, 115, 255, 36, 226)},
  }};
  return presets;
}

bool levels_presets_match(const LevelsAdjustment& left, const LevelsAdjustment& right) {
  auto left_any = left;
  auto right_any = right;
  left_any.channel = LevelsChannel::Rgb;
  right_any.channel = LevelsChannel::Rgb;
  return left_any == right_any;
}

AdjustmentPresetButtons::AdjustmentPresetButtons(AdjustmentPresetKind kind, const QString& object_prefix,
                                                 Arrangement arrangement, Hooks hooks, QWidget* parent)
    : QWidget(parent),
      kind_(kind),
      arrangement_(arrangement),
      object_prefix_(object_prefix),
      hooks_(std::move(hooks)) {
  setObjectName(object_prefix + QStringLiteral("PresetButtons"));
  const auto make_button = [this](const QString& text, const QString& role, const QString& tip) {
    auto* button = new QPushButton(text, this);
    button->setObjectName(object_prefix_ + role + QStringLiteral("PresetButton"));
    button->setToolTip(tip);
    button->setAutoDefault(false);
    return button;
  };
  save_ = make_button(QObject::tr("Save..."), QStringLiteral("Save"), QObject::tr("Save the current settings as a preset"));
  delete_ = make_button(QObject::tr("Delete"), QStringLiteral("Delete"), QObject::tr("Delete the selected preset"));
  import_ = make_button(QObject::tr("Import..."), QStringLiteral("Import"),
                        QObject::tr("Add a preset file to your presets"));
  export_ = make_button(QObject::tr("Export..."), QStringLiteral("Export"),
                        QObject::tr("Write the current settings to a preset file"));
  const std::array<QPushButton*, 4> buttons{save_, delete_, import_, export_};
  if (arrangement == Arrangement::Grid) {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(4);
    for (int index = 0; index < 4; ++index) {
      grid->addWidget(buttons[static_cast<std::size_t>(index)], index / 2, index % 2);
    }
  } else {
    auto* box = new QBoxLayout(arrangement == Arrangement::Row ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom,
                               this);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(4);
    // A row splits the width it is given evenly (#588): under a preset combo it spans the
    // combo, edge to edge. Its buttons ignore their own widths; sizeHint() below keeps each
    // at least as wide as the widest needs.
    const bool row = arrangement == Arrangement::Row;
    for (auto* button : buttons) {
      if (row) {
        button->setSizePolicy(QSizePolicy::Ignored, button->sizePolicy().verticalPolicy());
      }
      box->addWidget(button, row ? 1 : 0);
    }
    if (arrangement == Arrangement::Column) {
      box->addStretch(1);
    }
  }
  if (arrangement != Arrangement::Row) {
    // One width for the stacked buttons.
    int width = 0;
    for (auto* button : buttons) {
      width = std::max(width, button->sizeHint().width());
    }
    for (auto* button : buttons) {
      button->setMinimumWidth(width);
    }
  }
  QObject::connect(save_, &QPushButton::clicked, this, [this] { save_preset(); });
  QObject::connect(delete_, &QPushButton::clicked, this, [this] { delete_preset(); });
  QObject::connect(import_, &QPushButton::clicked, this, [this] { import_preset(); });
  QObject::connect(export_, &QPushButton::clicked, this, [this] { export_preset(); });
  const bool has_folder = !user_adjustment_presets_directory(kind_).isEmpty();
  save_->setEnabled(has_folder);
  import_->setEnabled(has_folder);
  refresh();
}

QSize AdjustmentPresetButtons::row_size() const {
  // Read when the layout asks, after the style sheet has polished the buttons.
  int width = 0;
  int height = 0;
  for (const auto* button : {save_, delete_, import_, export_}) {
    const auto hint = button->sizeHint();
    width = std::max(width, hint.width());
    height = std::max(height, hint.height());
  }
  const auto spacing = layout() != nullptr ? layout()->spacing() : 0;
  return {4 * width + 3 * spacing, height};
}

QSize AdjustmentPresetButtons::sizeHint() const {
  return arrangement_ == Arrangement::Row ? row_size() : QWidget::sizeHint();
}

QSize AdjustmentPresetButtons::minimumSizeHint() const {
  return arrangement_ == Arrangement::Row ? row_size() : QWidget::minimumSizeHint();
}

void AdjustmentPresetButtons::refresh() {
  delete_->setEnabled(hooks_.selected_user_preset && hooks_.selected_user_preset().has_value());
}

void AdjustmentPresetButtons::save_preset() {
  QInputDialog prompt(window());
  prompt.setObjectName(QStringLiteral("adjustmentPresetNameDialog"));
  prompt.setWindowTitle(QObject::tr("Save Preset"));
  prompt.setLabelText(QObject::tr("Preset name:"));
  prompt.setInputMode(QInputDialog::TextInput);
  if (hooks_.selected_user_preset) {
    if (const auto selected = hooks_.selected_user_preset(); selected.has_value()) {
      prompt.setTextValue(*selected);
    }
  }
  if (exec_dialog(prompt) != QDialog::Accepted) {
    return;
  }
  const auto name = prompt.textValue().trimmed();
  const auto path = user_adjustment_preset_path(kind_, name);
  if (path.isEmpty()) {
    show_critical_message(this, QObject::tr("Save Preset"), QObject::tr("Enter a name for the preset."),
                          QStringLiteral("adjustmentPresetNameErrorMessageBox"));
    return;
  }
  const auto stored_name = QFileInfo(path).completeBaseName();
  if (QFileInfo::exists(path) &&
      show_warning_message(this, QObject::tr("Save Preset"),
                           QObject::tr("A preset named \"%1\" already exists. Replace it?").arg(stored_name),
                           QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
                           QStringLiteral("adjustmentPresetReplaceMessageBox")) != QMessageBox::Yes) {
    return;
  }
  try {
    QDir().mkpath(QFileInfo(path).absolutePath());
    hooks_.write_current(path);
  } catch (const std::exception&) {
    show_critical_message(this, QObject::tr("Save Preset"), QObject::tr("The preset could not be saved."),
                          QStringLiteral("adjustmentPresetSaveErrorMessageBox"));
    return;
  }
  if (hooks_.user_presets_changed) {
    hooks_.user_presets_changed(stored_name);
  }
  refresh();
}

void AdjustmentPresetButtons::delete_preset() {
  if (!hooks_.selected_user_preset) {
    return;
  }
  const auto selected = hooks_.selected_user_preset();
  if (!selected.has_value()) {
    return;
  }
  if (show_warning_message(this, QObject::tr("Delete Preset"),
                           QObject::tr("Delete the preset \"%1\"?").arg(*selected),
                           QMessageBox::Yes | QMessageBox::No, QMessageBox::No,
                           QStringLiteral("adjustmentPresetDeleteMessageBox")) != QMessageBox::Yes) {
    return;
  }
  const auto path = user_adjustment_preset_path(kind_, *selected);
  if (path.isEmpty() || !QFile::remove(path)) {
    show_critical_message(this, QObject::tr("Delete Preset"), QObject::tr("The preset could not be deleted."),
                          QStringLiteral("adjustmentPresetDeleteErrorMessageBox"));
    return;
  }
  if (hooks_.user_presets_changed) {
    hooks_.user_presets_changed(QString());
  }
  refresh();
}

void AdjustmentPresetButtons::import_preset() {
  const auto source = get_open_file_name(this, QObject::tr("Import Preset"), QString(),
                                         adjustment_preset_file_filter(kind_), nullptr,
                                         object_prefix_ + QStringLiteral("PresetImportFileDialog"));
  if (source.isEmpty()) {
    return;
  }
  const auto fail = [this] {
    show_critical_message(this, QObject::tr("Import Preset"),
                          QObject::tr("The preset could not be imported. The file may be damaged or unsupported."),
                          QStringLiteral("adjustmentPresetImportErrorMessageBox"));
  };
  QFile file(source);
  if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxImportedPresetBytes) {
    fail();
    return;
  }
  const auto bytes = file.readAll();
  file.close();
  const auto destination = unique_user_preset_path(kind_, QFileInfo(source).completeBaseName());
  if (destination.isEmpty()) {
    fail();
    return;
  }
  try {
    hooks_.check_file(source);
    QDir().mkpath(QFileInfo(destination).absolutePath());
    const std::span<const std::uint8_t> view(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                             static_cast<std::size_t>(bytes.size()));
    write_file_bytes_atomically(to_filesystem_path(destination), view, "Could not open the preset file for writing",
                                "Could not write the preset file");
  } catch (const std::exception&) {
    fail();
    return;
  }
  if (hooks_.user_presets_changed) {
    hooks_.user_presets_changed(QFileInfo(destination).completeBaseName());
  }
  refresh();
}

void AdjustmentPresetButtons::export_preset() {
  auto path = get_save_file_name(this, QObject::tr("Export Preset"), QString(), adjustment_preset_file_filter(kind_),
                                 nullptr, object_prefix_ + QStringLiteral("PresetExportFileDialog"));
  if (path.isEmpty()) {
    return;
  }
  if (QFileInfo(path).suffix().isEmpty()) {
    path += QStringLiteral(".") + adjustment_preset_suffix(kind_);
  }
  try {
    hooks_.write_current(path);
    offer_browser_download_for_saved_file(path);
  } catch (const std::exception&) {
    show_critical_message(this, QObject::tr("Export Preset"), QObject::tr("The preset could not be exported."),
                          QStringLiteral("adjustmentPresetExportErrorMessageBox"));
  }
}

}  // namespace patchy::ui
