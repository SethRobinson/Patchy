// The New Document dialog: a two-pane layout with category chips over a clickable
// preset card grid on the left and the width/height/resolution/background details
// pane on the right. Pixels are the stored truth; the dimension spins convert
// through the chosen unit at the chosen resolution, like Image Size. Split out of
// main_window_document_dialogs.cpp when the dialog gained the card grid.

#include "ui/new_document_dialog.hpp"
#include "ui/icon_theme.hpp"
#include "ui/theme_palette.hpp"

#include "color/color_management.hpp"
#include "color/rgb_profiles.hpp"
#include "ui/app_settings.hpp"
#include "core/pixel_depth.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/measurement_units.hpp"
#include "ui/modifier_names.hpp"
#include "ui/options_flow_layout.hpp"
#include "ui/theme_qss.hpp"

#include <QApplication>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QClipboard>
#include <QEvent>
#include <QFrame>
#include <QMimeData>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QAction>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QInputDialog>
#include <QGridLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QVariant>

#include <algorithm>
#include <functional>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Resolution conventions follow Photoshop: screen/web/video presets (and the
// Clipboard) are 72 PPI; physical print presets are their paper size at 300 PPI.
constexpr double kScreenPpi = 72.0;
constexpr double kPrintPpi = 300.0;

constexpr int kPresetIdRole = kNewDocumentPresetIdRole;
constexpr int kPresetSizeRole = kNewDocumentPresetSizeRole;
constexpr int kPresetPpiRole = kNewDocumentPresetPpiRole;
constexpr int kPresetClipboardRole = Qt::UserRole + 4;
constexpr int kPresetThumbnailRole = Qt::UserRole + 5;
// A Recent card's index into the recent documents (newest first).
constexpr int kPresetRecentIndexRole = Qt::UserRole + 6;
constexpr int kPresetSizeTextRole = kNewDocumentPresetSizeTextRole;
// The unit a card shows its size in and a template switches the Width/Height unit to.
constexpr int kPresetUnitRole = Qt::UserRole + 8;

constexpr QSize kPresetCardCell(116, 100);
// The group grid's smallest size: three rows of five cards (every group fits unscrolled;
// GitHub issue 342), with room for the scroll bar search results bring so they keep five
// columns. The Recent row is one card tall.
constexpr QSize kPresetGridMinimum(5 * kPresetCardCell.width() + 22, 3 * kPresetCardCell.height() + 4);
constexpr int kRecentRowHeight = kPresetCardCell.height() + 2;

// The Last Used card's id (item data only, never written to settings) - never rename.
// The Recent row's newest card carries it; the older ones are "recent-1" and up.
constexpr QLatin1StringView kLastUsedPresetId("last-used");
constexpr QLatin1StringView kClipboardPresetId("clipboard");
constexpr int kMaxRecentDocuments = 8;

// The template groups, in chip order (GitHub issue 222). Organized by what people make
// rather than after another editor's list; every group holds at most 12 templates, so
// it fits the five-column, three-row card grid without a scroll bar. Saved holds the
// user's own presets (GitHub issue 374); its chip shows only while there are some.
enum class PresetCategory { Screen, Mobile, Social, Web, Print, Photo, Icons, Saved };

constexpr std::array kPresetCategories = {PresetCategory::Screen, PresetCategory::Mobile, PresetCategory::Social,
                                          PresetCategory::Web,    PresetCategory::Print,  PresetCategory::Photo,
                                          PresetCategory::Icons,  PresetCategory::Saved};

QString preset_category_name(PresetCategory category) {
  switch (category) {
    case PresetCategory::Screen:
      return QObject::tr("Screen", "new document template group");
    case PresetCategory::Mobile:
      return QObject::tr("Mobile", "new document template group");
    case PresetCategory::Social:
      return QObject::tr("Social", "new document template group");
    case PresetCategory::Web:
      return QObject::tr("Web", "new document template group");
    case PresetCategory::Print:
      return QObject::tr("Print", "new document template group");
    case PresetCategory::Photo:
      return QObject::tr("Photo", "new document template group");
    case PresetCategory::Icons:
      return QObject::tr("Icons & Games", "new document template group");
    case PresetCategory::Saved:
      return QObject::tr("Saved", "new document template group of the user's own presets");
  }
  return {};
}

QString preset_category_chip_name(PresetCategory category) {
  switch (category) {
    case PresetCategory::Screen:
      return QStringLiteral("newDocumentScreenChip");
    case PresetCategory::Mobile:
      return QStringLiteral("newDocumentMobileChip");
    case PresetCategory::Social:
      return QStringLiteral("newDocumentSocialChip");
    case PresetCategory::Web:
      return QStringLiteral("newDocumentWebChip");
    case PresetCategory::Print:
      return QStringLiteral("newDocumentPrintChip");
    case PresetCategory::Photo:
      return QStringLiteral("newDocumentPhotoChip");
    case PresetCategory::Icons:
      return QStringLiteral("newDocumentIconsChip");
    case PresetCategory::Saved:
      return QStringLiteral("newDocumentSavedChip");
  }
  return {};
}

// The template group tokens stored in newDocument/lastCategory - never rename.
QString preset_category_token(PresetCategory category) {
  switch (category) {
    case PresetCategory::Screen:
      return QStringLiteral("screen");
    case PresetCategory::Mobile:
      return QStringLiteral("mobile");
    case PresetCategory::Social:
      return QStringLiteral("social");
    case PresetCategory::Web:
      return QStringLiteral("web");
    case PresetCategory::Print:
      return QStringLiteral("print");
    case PresetCategory::Photo:
      return QStringLiteral("photo");
    case PresetCategory::Icons:
      return QStringLiteral("icons");
    case PresetCategory::Saved:
      return QStringLiteral("saved");
  }
  return {};
}

std::optional<PresetCategory> preset_category_from_token(const QString& token) {
  for (const auto category : kPresetCategories) {
    if (preset_category_token(category) == token) {
      return category;
    }
  }
  return std::nullopt;
}

struct NewDocumentPreset {
  QString id;  // persisted as newDocument/lastPresetId in user settings - never rename
  QString name;
  QSize size;
  double ppi{kScreenPpi};
  // The unit the card shows and picking it switches the fields to: the unit the paper or
  // print size is specified in (ISO paper in mm, ANSI paper and photo prints in inches),
  // pixels for screens. A saved preset's is the unit it was saved with.
  MeasurementUnit unit{MeasurementUnit::Pixels};
  bool clipboard{false};
  bool tablet{false};  // a Mobile template for a tablet: the Artboard picker's Tablet group
};

// One document's settings: what a Recent card or a saved preset restores.
struct RecentDocumentSettings {
  QSize size;
  double ppi{kScreenPpi};
  MeasurementUnit unit{MeasurementUnit::Pixels};
  int resolution_unit_index{0};
  QColor background{Qt::white};
  int bits{8};
  bool artboards{false};
  bool cmyk_proof{false};

  // The same document whatever units the fields show: what Save Preset compares.
  [[nodiscard]] bool same_document(const RecentDocumentSettings& other) const {
    return size == other.size && std::abs(ppi - other.ppi) < 1e-6 && background.rgba() == other.background.rgba() &&
           bits == other.bits && artboards == other.artboards && cmyk_proof == other.cmyk_proof;
  }
  bool operator==(const RecentDocumentSettings& other) const {
    return same_document(other) && unit == other.unit && resolution_unit_index == other.resolution_unit_index;
  }
};

// The colorMode tokens of newDocument/recent and newDocument/userPresets - never rename. A
// missing key reads as RGB, so entries written before GitHub issue 360 load unchanged.
constexpr QLatin1StringView kColorModeRgbToken("rgb");
constexpr QLatin1StringView kColorModeCmykProofToken("cmykProof");

// One entry of newDocument/recent or newDocument/userPresets: {w, h, ppi, unit (a
// measurement_unit_settings_token), resolutionUnit, background (#AARRGGBB), bits,
// artboards, colorMode}. Nullopt for an entry without a usable size.
std::optional<RecentDocumentSettings> document_settings_from_json(const QJsonObject& entry) {
  const auto width = entry.value(QStringLiteral("w")).toInt();
  const auto height = entry.value(QStringLiteral("h")).toInt();
  if (width < 1 || height < 1) {
    return std::nullopt;
  }
  RecentDocumentSettings recent;
  recent.size = QSize(std::clamp(width, 1, 30000), std::clamp(height, 1, 30000));
  recent.ppi = std::clamp(entry.value(QStringLiteral("ppi")).toDouble(kScreenPpi), 0.01, 9999.0);
  recent.unit =
      measurement_unit_from_settings_token(entry.value(QStringLiteral("unit")).toString(), MeasurementUnit::Pixels);
  if (recent.unit != MeasurementUnit::Pixels && recent.unit != MeasurementUnit::Inches &&
      recent.unit != MeasurementUnit::Centimeters && recent.unit != MeasurementUnit::Millimeters) {
    recent.unit = MeasurementUnit::Pixels;
  }
  recent.resolution_unit_index = std::clamp(entry.value(QStringLiteral("resolutionUnit")).toInt(), 0, 1);
  if (const QColor background(entry.value(QStringLiteral("background")).toString()); background.isValid()) {
    recent.background = background;
  }
  recent.bits = entry.value(QStringLiteral("bits")).toInt(8);
  recent.artboards = entry.value(QStringLiteral("artboards")).toBool();
  recent.cmyk_proof = entry.value(QStringLiteral("colorMode")).toString() == kColorModeCmykProofToken;
  return recent;
}

QJsonObject document_settings_to_json(const RecentDocumentSettings& recent) {
  QJsonObject entry;
  entry.insert(QStringLiteral("w"), recent.size.width());
  entry.insert(QStringLiteral("h"), recent.size.height());
  entry.insert(QStringLiteral("ppi"), recent.ppi);
  entry.insert(QStringLiteral("unit"), measurement_unit_settings_token(recent.unit));
  entry.insert(QStringLiteral("resolutionUnit"), recent.resolution_unit_index);
  entry.insert(QStringLiteral("background"), recent.background.name(QColor::HexArgb));
  entry.insert(QStringLiteral("bits"), recent.bits);
  entry.insert(QStringLiteral("artboards"), recent.artboards);
  entry.insert(QStringLiteral("colorMode"),
               QString(recent.cmyk_proof ? kColorModeCmykProofToken : kColorModeRgbToken));
  return entry;
}

// A preset made with Save Preset (GitHub issue 374).
struct UserPreset {
  QString id;  // "user-N", persisted; a new preset takes one above every existing N
  QString name;
  RecentDocumentSettings settings;
};

bool is_user_preset_id(const QString& id) { return id.startsWith(QStringLiteral("user-")); }

// newDocument/userPresets (permanent): a JSON array, oldest first, of the recent-entry
// fields plus `id` and `name`.
std::vector<UserPreset> load_user_presets() {
  std::vector<UserPreset> presets;
  const auto array =
      QJsonDocument::fromJson(app_settings().value(QStringLiteral("newDocument/userPresets")).toString().toUtf8())
          .array();
  for (const auto& value : array) {
    const auto entry = value.toObject();
    const auto id = entry.value(QStringLiteral("id")).toString();
    const auto name = entry.value(QStringLiteral("name")).toString().trimmed();
    auto settings = document_settings_from_json(entry);
    if (!is_user_preset_id(id) || name.isEmpty() || !settings.has_value()) {
      continue;
    }
    presets.push_back({id, name, *settings});
  }
  return presets;
}

void store_user_presets(const std::vector<UserPreset>& presets) {
  QJsonArray array;
  for (const auto& preset : presets) {
    auto entry = document_settings_to_json(preset.settings);
    entry.insert(QStringLiteral("id"), preset.id);
    entry.insert(QStringLiteral("name"), preset.name);
    array.append(entry);
  }
  app_settings().setValue(QStringLiteral("newDocument/userPresets"),
                          QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

// Sizes come from the published specs: display and video standards, the device makers'
// native screen resolutions, each platform's own upload guidance, the IAB ad units, ISO
// 216 and ANSI paper, and photo-print sizes, printed sizes at 300 PPI. Ids are permanent;
// a template that moves to another group keeps its id (the first ones predate the groups).
std::vector<NewDocumentPreset> presets_for_category(PresetCategory category) {
  constexpr auto kPx = MeasurementUnit::Pixels;
  constexpr auto kIn = MeasurementUnit::Inches;
  constexpr auto kCm = MeasurementUnit::Centimeters;
  constexpr auto kMm = MeasurementUnit::Millimeters;
  switch (category) {
    case PresetCategory::Screen:
      return {
          {QStringLiteral("screen-1024x768"), QObject::tr("Default"), QSize(1024, 768), kScreenPpi},
          {QStringLiteral("screen-720p"), QObject::tr("720p"), QSize(1280, 720), kScreenPpi},
          {QStringLiteral("screen-1080p"), QObject::tr("1080p"), QSize(1920, 1080), kScreenPpi},
          {QStringLiteral("screen-1440p"), QObject::tr("1440p"), QSize(2560, 1440), kScreenPpi},
          {QStringLiteral("screen-4k"), QObject::tr("4K"), QSize(3840, 2160), kScreenPpi},
          {QStringLiteral("screen-8k"), QObject::tr("8K"), QSize(7680, 4320), kScreenPpi},
          {QStringLiteral("screen-ultrawide-3440"), QObject::tr("Ultrawide"), QSize(3440, 1440), kScreenPpi},
          {QStringLiteral("screen-dci-4k"), QObject::tr("Cinema 4K"), QSize(4096, 2160), kScreenPpi},
          {QStringLiteral("screen-laptop-1366"), QObject::tr("Laptop"), QSize(1366, 768), kScreenPpi},
          {QStringLiteral("screen-square-2048"), QObject::tr("Square", "new document preset"), QSize(2048, 2048),
           kScreenPpi},
      };
    case PresetCategory::Mobile:
      return {
          // iPhone 16 Pro and the three Android screen classes joined for GitHub issue 281.
          {QStringLiteral("mobile-iphone-16"), QObject::tr("iPhone 16"), QSize(1179, 2556), kScreenPpi},
          {QStringLiteral("mobile-iphone-16-pro"), QObject::tr("iPhone 16 Pro"), QSize(1206, 2622), kScreenPpi},
          {QStringLiteral("mobile-iphone-16-pro-max"), QObject::tr("iPhone 16 Pro Max"), QSize(1320, 2868),
           kScreenPpi},
          {QStringLiteral("mobile-iphone-se"), QObject::tr("iPhone SE"), QSize(750, 1334), kScreenPpi},
          {QStringLiteral("mobile-android-hd-720x1280"), QObject::tr("Android HD"), QSize(720, 1280), kScreenPpi},
          {QStringLiteral("mobile-android-fhd-1080x1920"), QObject::tr("Android FHD"), QSize(1080, 1920),
           kScreenPpi},
          {QStringLiteral("mobile-android-1080x2400"), QObject::tr("Android Phone"), QSize(1080, 2400), kScreenPpi},
          {QStringLiteral("mobile-android-wqhd-1440x2560"), QObject::tr("Android WQHD"), QSize(1440, 2560),
           kScreenPpi},
          {QStringLiteral("mobile-android-1440x3120"), QObject::tr("Android Phone QHD+"), QSize(1440, 3120),
           kScreenPpi},
          {QStringLiteral("mobile-ipad-11"), QObject::tr("iPad 11-inch"), QSize(1640, 2360), kScreenPpi, kPx, false,
           true},
          {QStringLiteral("mobile-ipad-pro-13"), QObject::tr("iPad Pro 13-inch"), QSize(2064, 2752), kScreenPpi, kPx,
           false, true},
          {QStringLiteral("mobile-android-tablet-1600x2560"), QObject::tr("Android Tablet"), QSize(1600, 2560),
           kScreenPpi, kPx, false, true},
      };
    case PresetCategory::Social:
      return {
          {QStringLiteral("social-square-1080"), QObject::tr("Social Post"), QSize(1080, 1080), kScreenPpi},
          {QStringLiteral("social-portrait-1080x1350"), QObject::tr("Portrait Post"), QSize(1080, 1350),
           kScreenPpi},
          {QStringLiteral("phone-story-1080x1920"), QObject::tr("Social Story"), QSize(1080, 1920), kScreenPpi},
          {QStringLiteral("social-youtube-thumbnail"), QObject::tr("YouTube Thumbnail"), QSize(1280, 720),
           kScreenPpi},
          {QStringLiteral("social-youtube-banner"), QObject::tr("YouTube Banner"), QSize(2560, 1440), kScreenPpi},
          {QStringLiteral("social-facebook-cover"), QObject::tr("Facebook Cover"), QSize(851, 315), kScreenPpi},
          {QStringLiteral("social-x-header"), QObject::tr("X Header"), QSize(1500, 500), kScreenPpi},
          {QStringLiteral("social-x-post"), QObject::tr("X Post"), QSize(1600, 900), kScreenPpi},
          {QStringLiteral("social-linkedin-banner"), QObject::tr("LinkedIn Banner"), QSize(1584, 396), kScreenPpi},
          {QStringLiteral("social-pinterest-pin"), QObject::tr("Pinterest Pin"), QSize(1000, 1500), kScreenPpi},
          {QStringLiteral("social-profile-picture"), QObject::tr("Profile Picture"), QSize(400, 400), kScreenPpi},
      };
    case PresetCategory::Web:
      return {
          {QStringLiteral("web-page-1440x900"), QObject::tr("Web Page"), QSize(1440, 900), kScreenPpi},
          {QStringLiteral("web-link-preview"), QObject::tr("Link Preview"), QSize(1200, 630), kScreenPpi},
          {QStringLiteral("web-email-header"), QObject::tr("Email Header"), QSize(600, 200), kScreenPpi},
          {QStringLiteral("web-ad-medium-rectangle"), QObject::tr("Medium Rectangle Ad"), QSize(300, 250),
           kScreenPpi},
          {QStringLiteral("web-ad-large-rectangle"), QObject::tr("Large Rectangle Ad"), QSize(336, 280),
           kScreenPpi},
          {QStringLiteral("web-ad-leaderboard"), QObject::tr("Leaderboard Ad"), QSize(728, 90), kScreenPpi},
          {QStringLiteral("web-ad-billboard"), QObject::tr("Billboard Ad"), QSize(970, 250), kScreenPpi},
          {QStringLiteral("web-ad-half-page"), QObject::tr("Half Page Ad"), QSize(300, 600), kScreenPpi},
          {QStringLiteral("web-ad-wide-skyscraper"), QObject::tr("Skyscraper Ad"), QSize(160, 600), kScreenPpi},
          {QStringLiteral("web-ad-mobile-banner"), QObject::tr("Mobile Banner Ad"), QSize(320, 50), kScreenPpi},
      };
    case PresetCategory::Print:
      return {
          {QStringLiteral("print-a6"), QObject::tr("A6"), QSize(1240, 1748), kPrintPpi, kMm},
          {QStringLiteral("print-a5"), QObject::tr("A5"), QSize(1748, 2480), kPrintPpi, kMm},
          {QStringLiteral("print-a4"), QObject::tr("A4"), QSize(2480, 3508), kPrintPpi, kMm},
          {QStringLiteral("print-a3"), QObject::tr("A3"), QSize(3508, 4961), kPrintPpi, kMm},
          // Super A3 / Super B, 329 x 483 mm: A3 with a bleed margin on photo printers.
          {QStringLiteral("print-a3-plus"), QObject::tr("A3+"), QSize(3886, 5705), kPrintPpi, kMm},
          {QStringLiteral("print-a2"), QObject::tr("A2"), QSize(4961, 7016), kPrintPpi, kMm},
          {QStringLiteral("print-us-letter"), QObject::tr("US Letter"), QSize(2550, 3300), kPrintPpi, kIn},
          {QStringLiteral("print-us-legal"), QObject::tr("US Legal"), QSize(2550, 4200), kPrintPpi, kIn},
          {QStringLiteral("print-tabloid"), QObject::tr("Tabloid"), QSize(3300, 5100), kPrintPpi, kIn},
          {QStringLiteral("print-business-card-us"), QObject::tr("Business Card (US)"), QSize(1050, 600),
           kPrintPpi, kIn},
          {QStringLiteral("print-business-card-eu"), QObject::tr("Business Card (EU)"), QSize(1004, 650),
           kPrintPpi, kMm},
          {QStringLiteral("print-poster-18x24"), QObject::tr("Poster 18 x 24 in"), QSize(5400, 7200), kPrintPpi,
           kIn},
      };
    case PresetCategory::Photo:
      return {
          {QStringLiteral("photo-print-4x6"), QObject::tr("4 x 6 in"), QSize(1200, 1800), kPrintPpi, kIn},
          {QStringLiteral("print-5x7"), QObject::tr("5 x 7 in"), QSize(1500, 2100), kPrintPpi, kIn},
          {QStringLiteral("print-8x10"), QObject::tr("8 x 10 in"), QSize(2400, 3000), kPrintPpi, kIn},
          {QStringLiteral("photo-print-11x14"), QObject::tr("11 x 14 in"), QSize(3300, 4200), kPrintPpi, kIn},
          {QStringLiteral("photo-print-8x8"), QObject::tr("8 x 8 in"), QSize(2400, 2400), kPrintPpi, kIn},
          {QStringLiteral("photo-print-10x15cm"), QObject::tr("10 x 15 cm"), QSize(1181, 1772), kPrintPpi, kCm},
          {QStringLiteral("photo-print-13x18cm"), QObject::tr("13 x 18 cm"), QSize(1535, 2126), kPrintPpi, kCm},
          {QStringLiteral("photo-3x2-3000"), QObject::tr("Photo 3:2"), QSize(3000, 2000), kScreenPpi},
      };
    case PresetCategory::Icons:
      return {
          {QStringLiteral("icon-app-1024"), QObject::tr("App Icon"), QSize(1024, 1024), kScreenPpi},
          {QStringLiteral("icon-store-512"), QObject::tr("Store Icon"), QSize(512, 512), kScreenPpi},
          {QStringLiteral("icon-desktop-256"), QObject::tr("Desktop Icon"), QSize(256, 256), kScreenPpi},
          {QStringLiteral("icon-favicon-32"), QObject::tr("Favicon"), QSize(32, 32), kScreenPpi},
          {QStringLiteral("art-pixel-16"), QObject::tr("Pixel Art 16"), QSize(16, 16), kScreenPpi},
          {QStringLiteral("art-pixel-32"), QObject::tr("Pixel Art 32"), QSize(32, 32), kScreenPpi},
          {QStringLiteral("art-pixel-64"), QObject::tr("Pixel Art 64"), QSize(64, 64), kScreenPpi},
          {QStringLiteral("art-texture-1024"), QObject::tr("Texture 1K"), QSize(1024, 1024), kScreenPpi},
          {QStringLiteral("art-texture-2048"), QObject::tr("Texture 2K"), QSize(2048, 2048), kScreenPpi},
          {QStringLiteral("art-texture-4096"), QObject::tr("Texture 4K"), QSize(4096, 4096), kScreenPpi},
          {QStringLiteral("game-store-header"), QObject::tr("Game Store Header"), QSize(920, 430), kScreenPpi},
      };
    case PresetCategory::Saved: {
      std::vector<NewDocumentPreset> presets;
      for (const auto& preset : load_user_presets()) {
        presets.push_back(
            {preset.id, preset.name, preset.settings.size, preset.settings.ppi, preset.settings.unit});
      }
      return presets;
    }
  }
  return {};
}

}  // namespace

// The search field's rule (GitHub issue 223): every whitespace-separated word must
// appear, ignoring case, in the template's name or its group's name. A word holding a
// digit may also match the size ("1080 x 1920", "1080x1920"); a bare "x" matching every
// size made the field show nearly every template.
bool size_preset_matches_search(const QString& name, const QString& group, QSize size, const QString& text,
                                const QString& shown_size) {
  const auto names = name + QLatin1Char(' ') + group;
  auto sizes = QStringLiteral("%1 x %2 %1x%2").arg(size.width()).arg(size.height());
  if (!shown_size.isEmpty()) {
    sizes += QLatin1Char(' ') + shown_size + QLatin1Char(' ') + QString(shown_size).remove(QLatin1Char(' '));
  }
  const auto words = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  return !words.isEmpty() && std::all_of(words.begin(), words.end(), [&names, &sizes](const QString& word) {
    if (names.contains(word, Qt::CaseInsensitive)) {
      return true;
    }
    const bool has_digit = std::any_of(word.begin(), word.end(), [](QChar character) { return character.isDigit(); });
    return has_digit && sizes.contains(word, Qt::CaseInsensitive);
  });
}

std::vector<ArtboardSizePreset> artboard_size_presets() {
  std::vector<ArtboardSizePreset> presets;
  for (const auto category : kPresetCategories) {
    if (category == PresetCategory::Saved) {
      continue;  // document presets, not artboard sizes
    }
    for (const auto& preset : presets_for_category(category)) {
      if (preset.clipboard) {
        continue;
      }
      // The picker splits Mobile into phones and tablets (GitHub issue 281); the dialog keeps
      // one Mobile group.
      QString group = preset_category_name(category);
      if (category == PresetCategory::Mobile) {
        group = preset.tablet ? QObject::tr("Tablet", "artboard size group") : QObject::tr("Phone", "artboard size group");
      }
      presets.push_back({preset.id, preset.name, preset.size, group});
    }
  }
  return presets;
}

namespace {

PresetCategory category_of_preset_id(const QString& id) {
  for (const auto category : kPresetCategories) {
    for (const auto& preset : presets_for_category(category)) {
      if (preset.id == id) {
        return category;
      }
    }
  }
  return PresetCategory::Screen;
}

// newDocument/recent (permanent): a JSON array, newest first, of document_settings_to_json
// entries. Before the key exists, the old Last Used keys seed one entry.
std::vector<RecentDocumentSettings> load_recent_documents(MeasurementUnit seed_unit, int seed_resolution_unit,
                                                          bool seed_artboards) {
  std::vector<RecentDocumentSettings> recents;
  auto settings = app_settings();
  if (settings.contains(QStringLiteral("newDocument/recent"))) {
    const auto array = QJsonDocument::fromJson(settings.value(QStringLiteral("newDocument/recent")).toString().toUtf8())
                           .array();
    for (const auto& value : array) {
      const auto recent = document_settings_from_json(value.toObject());
      if (!recent.has_value()) {
        continue;
      }
      if (std::find(recents.begin(), recents.end(), *recent) == recents.end()) {
        recents.push_back(*recent);
      }
      if (static_cast<int>(recents.size()) == kMaxRecentDocuments) {
        break;
      }
    }
    return recents;
  }
  settings.beginGroup(QStringLiteral("newDocument"));
  const auto last_width = settings.value(QStringLiteral("lastWidth")).toInt();
  const auto last_height = settings.value(QStringLiteral("lastHeight")).toInt();
  if (last_width >= 1 && last_height >= 1) {
    RecentDocumentSettings recent;
    recent.size = QSize(std::clamp(last_width, 1, 30000), std::clamp(last_height, 1, 30000));
    const auto last_ppi = settings.value(QStringLiteral("lastPpi")).toDouble();
    recent.ppi = std::clamp(last_ppi > 0.0 ? last_ppi : kScreenPpi, 0.01, 9999.0);
    recent.unit = seed_unit;
    recent.resolution_unit_index = seed_resolution_unit;
    if (const auto background = settings.value(QStringLiteral("lastBackground")).value<QColor>(); background.isValid()) {
      recent.background = background;
    }
    recent.bits = settings.value(QStringLiteral("lastBitsPerChannel"), 8).toInt();
    recent.artboards = seed_artboards;
    recents.push_back(recent);
  }
  settings.endGroup();
  return recents;
}

void store_recent_documents(const std::vector<RecentDocumentSettings>& recents) {
  QJsonArray array;
  for (const auto& recent : recents) {
    if (array.size() == kMaxRecentDocuments) {
      break;
    }
    array.append(document_settings_to_json(recent));
  }
  app_settings().setValue(QStringLiteral("newDocument/recent"),
                          QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

// A Recent card's title: the template its size and resolution match, else "Custom".
QString recent_document_name(const RecentDocumentSettings& recent) {
  for (const auto category : kPresetCategories) {
    for (const auto& preset : presets_for_category(category)) {
      if (preset.size == recent.size && std::abs(preset.ppi - recent.ppi) < 1e-6) {
        return preset.name;
      }
    }
  }
  return QObject::tr("Custom");
}

bool is_recent_card_id(const QString& id) {
  return id == kLastUsedPresetId || id.startsWith(QStringLiteral("recent-"));
}

// Calls `on_resize` whenever the watched widget resizes (the Recent row refits its cards).
class ResizeWatcher final : public QObject {
 public:
  ResizeWatcher(QObject* parent, std::function<void()> on_resize) : QObject(parent), on_resize_(std::move(on_resize)) {}

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::Resize && on_resize_) {
      on_resize_();
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  std::function<void()> on_resize_;
};

// The characters a file name cannot hold on Windows, the strictest file system.
bool is_file_name_illegal(QChar character) {
  return character.unicode() < 0x20 || QStringLiteral("\\/:*?\"<>|").contains(character);
}

QString without_illegal_file_name_characters(const QString& text) {
  QString cleaned;
  cleaned.reserve(text.size());
  for (const auto character : text) {
    if (!is_file_name_illegal(character)) {
      cleaned.append(character);
    }
  }
  return cleaned;
}

// A length in `unit` with that unit's decimals, trailing zeros dropped: 2480 px at 300 PPI
// reads "210" mm, 2550 px "8.5" in. Paper and print templates are whole pixels of their
// nominal size, so the unit's rounding gives the nominal figures back.
QString trimmed_unit_number(double pixels, MeasurementUnit unit, double ppi) {
  auto text = format_readout_number(pixels_to_measurement_unit(pixels, unit, ppi, pixels),
                                    measurement_unit_decimals(unit));
  const auto point = QLocale().decimalPoint();
  if (text.contains(point)) {
    while (text.endsWith(QLatin1Char('0'))) {
      text.chop(1);
    }
    if (text.endsWith(point)) {
      text.chop(point.size());
    }
  }
  return text;
}

// The size a card shows: pixels for screen templates, the paper's own unit for print ones.
QString preset_size_text(QSize size, double ppi, MeasurementUnit unit) {
  if (!size.isValid() || size.width() <= 0) {
    return QObject::tr("No image");
  }
  if (!measurement_unit_is_physical(unit)) {
    return QObject::tr("%1 x %2 px").arg(size.width()).arg(size.height());
  }
  return QObject::tr("%1 x %2 %3", "new document card size: width, height, unit suffix")
      .arg(trimmed_unit_number(size.width(), unit, ppi), trimmed_unit_number(size.height(), unit, ppi),
           measurement_unit_suffix(unit));
}

bool preset_matches_search(const NewDocumentPreset& preset, PresetCategory category, const QString& text) {
  return size_preset_matches_search(preset.name, preset_category_name(category), preset.size, text,
                                    preset_size_text(preset.size, preset.ppi, preset.unit));
}

// Stores what a card shows and restores: its size, resolution and unit.
void set_card_size_data(QListWidgetItem* item, QSize size, double ppi, MeasurementUnit unit) {
  item->setData(kPresetSizeRole, size);
  item->setData(kPresetPpiRole, ppi);
  item->setData(kPresetUnitRole, static_cast<int>(unit));
  item->setData(kPresetSizeTextRole, preset_size_text(size, ppi, unit));
}

// Paints one preset card: rounded background, an aspect-ratio miniature (the real
// clipboard thumbnail for the Clipboard card), the preset name, the size in the card's
// unit, and the preset resolution.
class PresetCardDelegate final : public QStyledItemDelegate {
 public:
  explicit PresetCardDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

  QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return kPresetCardCell; }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRect card = option.rect.adjusted(4, 4, -4, -4);
    const bool enabled = (index.flags() & Qt::ItemIsEnabled) != 0;
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;

    QColor background = theme().panel_card_bg;
    QColor border = theme().panel_card_border;
    if (!enabled) {
      background = theme().nd_card_disabled_bg;
      border = theme().nd_card_disabled_border;
    } else if (selected) {
      background = theme().nd_card_selected_bg;
      border = theme().primary_border;
    } else if (hovered) {
      background = theme().button_bg;
      border = theme().nd_card_hover_border;
    }
    QPainterPath card_path;
    card_path.addRoundedRect(QRectF(card).adjusted(0.5, 0.5, -0.5, -0.5), 5.0, 5.0);
    painter->fillPath(card_path, background);
    painter->setPen(QPen(border, selected ? 2.0 : 1.0));
    painter->drawPath(card_path);

    const QRect thumb_area(card.left() + 8, card.top() + 7, card.width() - 16, 32);
    const auto size = index.data(kPresetSizeRole).toSize();
    const auto thumbnail = index.data(kPresetThumbnailRole).value<QPixmap>();
    painter->setRenderHint(QPainter::Antialiasing, false);
    if (!thumbnail.isNull()) {
      const auto scaled =
          thumbnail.scaled(thumb_area.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
      const QRect target(thumb_area.x() + (thumb_area.width() - scaled.width()) / 2,
                         thumb_area.y() + (thumb_area.height() - scaled.height()) / 2, scaled.width(),
                         scaled.height());
      painter->drawPixmap(target, scaled);
      painter->setPen(QPen(theme().nd_thumb_outline, 1.0));
      painter->drawRect(target.adjusted(0, 0, -1, -1));
    } else {
      QSizeF box = size.isValid() && size.width() > 0 && size.height() > 0 ? QSizeF(size) : QSizeF(3.0, 2.0);
      box.scale(QSizeF(thumb_area.size()), Qt::KeepAspectRatio);
      const QRectF outline(thumb_area.x() + (thumb_area.width() - box.width()) / 2.0,
                           thumb_area.y() + (thumb_area.height() - box.height()) / 2.0, box.width(),
                           box.height());
      painter->fillRect(outline, enabled ? theme().nd_thumb_fill : theme().nd_thumb_fill_disabled);
      QPen outline_pen(enabled ? theme().nd_thumb_border : theme().nd_thumb_border_disabled, 1.0);
      if (!size.isValid() || size.width() <= 0) {
        outline_pen.setStyle(Qt::DashLine);  // Clipboard card with nothing on the clipboard.
      }
      painter->setPen(outline_pen);
      painter->drawRect(outline);
    }

    const auto name_font = offset_font(option.font, 0, true);
    painter->setFont(name_font);
    painter->setPen(enabled ? theme().nd_card_title_text : theme().nd_card_title_disabled_text);
    const QRect name_rect(card.left() + 4, card.top() + 41, card.width() - 8, 16);
    painter->drawText(name_rect, Qt::AlignHCenter | Qt::AlignVCenter,
                      QFontMetrics(name_font).elidedText(index.data(Qt::DisplayRole).toString(),
                                                         Qt::ElideRight, name_rect.width()));

    const auto detail_font = offset_font(option.font, -1, false);
    painter->setFont(detail_font);
    painter->setPen(enabled ? theme().nd_card_detail_text : theme().nd_card_detail_disabled_text);
    const QRect dims_rect(card.left() + 4, card.top() + 57, card.width() - 8, 14);
    painter->drawText(dims_rect, Qt::AlignHCenter | Qt::AlignVCenter,
                      QFontMetrics(detail_font)
                          .elidedText(index.data(kPresetSizeTextRole).toString(), Qt::ElideRight, dims_rect.width()));

    painter->setPen(enabled ? theme().nd_card_meta_text : theme().nd_card_meta_disabled_text);
    const QRect ppi_rect(card.left() + 4, card.top() + 71, card.width() - 8, 13);
    painter->drawText(ppi_rect, Qt::AlignHCenter | Qt::AlignVCenter,
                      QObject::tr("%1 ppi").arg(QString::number(index.data(kPresetPpiRole).toDouble())));
    painter->restore();
  }
};

ThemedQss new_document_dialog_style() {
  return ThemedQss(QStringLiteral(R"(
    QDialog#patchyNewDocumentDialog {
      background: @window_bg;
    }
    QDialog#patchyNewDocumentDialog QLabel {
      background: transparent;
      color: @text_primary;
    }
    QDialog#patchyNewDocumentDialog QLabel#newDocumentSummaryLabel,
    QDialog#patchyNewDocumentDialog QLabel#newDocumentNoMatchesLabel,
    QDialog#patchyNewDocumentDialog QLabel#newDocumentRecentLabel,
    QDialog#patchyNewDocumentDialog QLabel#newDocumentGroupLabel {
      color: @nd_section_text;
    }
    QDialog#patchyNewDocumentDialog QLineEdit#newDocumentSearchEdit,
    QDialog#patchyNewDocumentDialog QLineEdit#newDocumentNameEdit {
      background: @panel_card_bg;
      border: 1px solid @nd_field_border;
      border-radius: 3px;
      color: @text_bright;
      min-height: 22px;
      padding: 0 6px;
    }
    QDialog#patchyNewDocumentDialog QLineEdit#newDocumentSearchEdit:focus,
    QDialog#patchyNewDocumentDialog QLineEdit#newDocumentNameEdit:focus {
      border-color: @primary_border;
    }
    QDialog#patchyNewDocumentDialog QWidget#newDocumentChipRow {
      background: transparent;
    }
    QDialog#patchyNewDocumentDialog QFrame#newDocumentListFrame {
      background: @list_surface_bg;
      border: 1px solid @list_surface_border;
      border-radius: 5px;
    }
    QDialog#patchyNewDocumentDialog QFrame#newDocumentListDivider {
      background: @list_surface_border;
      border: none;
    }
    QDialog#patchyNewDocumentDialog QListWidget#newDocumentPresetList,
    QDialog#patchyNewDocumentDialog QListWidget#newDocumentRecentList {
      background: transparent;
      border: none;
      padding: 0;
    }
    QToolButton[newDocumentChip="true"] {
      background: @panel_card_bg;
      border: 1px solid @nd_chip_border;
      border-radius: 12px;
      color: @nd_chip_text;
      padding: 3px 14px;
      min-height: 17px;
    }
    QToolButton[newDocumentChip="true"]:hover {
      background: @button_bg;
      border-color: @nd_card_hover_border;
    }
    QToolButton[newDocumentChip="true"]:checked {
      background: @selection_soft_bg;
      border-color: @primary_border;
      color: @text_on_raised;
    }
    QDialog#patchyNewDocumentDialog QDoubleSpinBox,
    QDialog#patchyNewDocumentDialog QComboBox {
      background: @panel_card_bg;
      border: 1px solid @nd_field_border;
      border-radius: 3px;
      color: @text_bright;
      min-height: 22px;
      padding: 0 6px;
    }
    QDialog#patchyNewDocumentDialog QDoubleSpinBox:focus,
    QDialog#patchyNewDocumentDialog QComboBox:focus {
      border-color: @primary_border;
    }
    QDialog#patchyNewDocumentDialog QDoubleSpinBox:disabled,
    QDialog#patchyNewDocumentDialog QComboBox:disabled {
      background: @nd_field_disabled_bg;
      border-color: @nd_field_disabled_border;
      color: @nd_field_disabled_text;
    }
    QDialog#patchyNewDocumentDialog QToolButton#newDocumentSwapDimensionsButton {
      background: @panel_card_bg;
      border: 1px solid @nd_field_border;
      border-radius: 3px;
      min-height: 24px;
      max-height: 24px;
    }
    QDialog#patchyNewDocumentDialog QToolButton#newDocumentSwapDimensionsButton:hover {
      border-color: @primary_border;
    }
    QDialog#patchyNewDocumentDialog QToolButton#newDocumentSwapDimensionsButton:disabled {
      background: @nd_field_disabled_bg;
      border-color: @nd_field_disabled_border;
    }
    QDialog#patchyNewDocumentDialog QPushButton {
      background: @button_bg;
      border: 1px solid @field_border;
      border-radius: 13px;
      color: @text_bright;
      min-width: 84px;
      min-height: 26px;
      padding: 0 16px;
    }
    QDialog#patchyNewDocumentDialog QPushButton:hover {
      background: @neutral_button_hover_bg;
      border-color: @neutral_button_hover_border;
    }
    QDialog#patchyNewDocumentDialog QPushButton:disabled {
      color: @nd_field_disabled_text;
      border-color: @nd_field_disabled_border;
    }
    QDialog#patchyNewDocumentDialog QPushButton#newDocumentCreateButton {
      background: @primary_bg;
      border: 1px solid @primary_border;
      font-weight: 700;
    }
    QDialog#patchyNewDocumentDialog QPushButton#newDocumentCreateButton:hover {
      background: @primary_hover_bg;
    }
    QDialog#patchyNewDocumentDialog QPushButton#newDocumentBackgroundSwatch {
      background: @panel_card_bg;
      border: 1px solid @nd_field_border;
      border-radius: 3px;
      min-width: 40px;
      max-width: 40px;
      min-height: 24px;
      max-height: 24px;
      padding: 0;
    }
    QDialog#patchyNewDocumentDialog QPushButton#newDocumentBackgroundSwatch:hover {
      border-color: @primary_border;
    }
    QDialog#patchyNewDocumentDialog QPushButton#newDocumentBackgroundSwatch:disabled {
      background: @nd_field_disabled_bg;
      border-color: @nd_field_disabled_border;
    }
  )")) +
         // The application sheet gives every QListWidget a 20 px min-height, which replaces a
         // setMinimumSize/setFixedHeight at polish time, so the card lists' sizes live here.
         ThemedQss(QStringLiteral(R"(
    QDialog#patchyNewDocumentDialog QListWidget#newDocumentPresetList {
      min-width: %1px;
      min-height: %2px;
    }
    QDialog#patchyNewDocumentDialog QListWidget#newDocumentRecentList {
      min-height: %3px;
      max-height: %3px;
    }
  )")
                       .arg(kPresetGridMinimum.width())
                       .arg(kPresetGridMinimum.height())
                       .arg(kRecentRowHeight));
}

}  // namespace

QString sanitized_new_document_name(const QString& name) {
  auto cleaned = without_illegal_file_name_characters(name).trimmed();
  while (!cleaned.isEmpty() && (cleaned.back() == QLatin1Char('.') || cleaned.back() == QLatin1Char(' '))) {
    cleaned.chop(1);
  }
  return cleaned;
}

bool clipboard_offers_image() {
  const auto* clipboard = QApplication::clipboard();
  const auto* mime = clipboard != nullptr ? clipboard->mimeData() : nullptr;
  return mime != nullptr && mime->hasImage();
}

std::optional<NewDocumentSettings> request_new_document_settings(QWidget* parent, const QString& default_name) {
  auto clipboard_image = clipboard_offers_image() ? QApplication::clipboard()->image() : QImage();

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyNewDocumentDialog"));
  dialog.setWindowTitle(QObject::tr("New Document"));

  auto* root = new QVBoxLayout(&dialog);
  root->setContentsMargins(14, 12, 14, 12);
  root->setSpacing(10);

  auto* body = new QHBoxLayout();
  body->setSpacing(18);
  root->addLayout(body, 1);

  // Left pane: the search field and the group chips over the preset card grid.
  auto* left = new QVBoxLayout();
  left->setSpacing(8);
  body->addLayout(left, 1);

  // Searches every group at once (GitHub issue 223); %CTRL%+F focuses it.
  auto* search = new QLineEdit(&dialog);
  search->setObjectName(QStringLiteral("newDocumentSearchEdit"));
  search->setPlaceholderText(QObject::tr("Search all templates"));
  search->setToolTip(resolve_modifier_names(QObject::tr("Search every template group by name or size (%CTRL%+F)")));
  search->setClearButtonEnabled(true);
  left->addWidget(search);

  auto* chips = new QWidget(&dialog);
  chips->setObjectName(QStringLiteral("newDocumentChipRow"));
  auto* chips_row = new FlowLayout(chips, 6, 6);
  chips_row->setContentsMargins(0, 0, 0, 0);
  auto* chip_group = new QButtonGroup(&dialog);
  chip_group->setExclusive(true);
  for (const auto category : kPresetCategories) {
    auto* chip = new QToolButton(chips);
    chip->setObjectName(preset_category_chip_name(category));
    // "&&": a button reads a single & as a mnemonic marker ("Icons & Games").
    chip->setText(preset_category_name(category).replace(QLatin1Char('&'), QStringLiteral("&&")));
    chip->setCheckable(true);
    chip->setCursor(Qt::PointingHandCursor);
    chip->setProperty("newDocumentChip", true);
    chip_group->addButton(chip, static_cast<int>(category));
    chips_row->addWidget(chip);
  }
  left->addWidget(chips);

  // One framed list area (GitHub issue 342): the Recent row on top, a divider, then the
  // group's cards under a caption naming the group, or the search.
  auto* list_frame = new QFrame(&dialog);
  list_frame->setObjectName(QStringLiteral("newDocumentListFrame"));
  auto* list_area = new QVBoxLayout(list_frame);
  list_area->setContentsMargins(6, 4, 4, 4);
  list_area->setSpacing(2);
  left->addWidget(list_frame, 1);

  // The Recent row: Clipboard (with an image on the clipboard) and the last documents
  // created here, newest first, on every group and through a search; as many cards as fit
  // one row, never wrapping or scrolling.
  auto* recent_label = new QLabel(QObject::tr("Recent", "new document recent sizes"), list_frame);
  recent_label->setObjectName(QStringLiteral("newDocumentRecentLabel"));
  list_area->addWidget(recent_label);
  auto* recent_list = new QListWidget(list_frame);
  recent_list->setObjectName(QStringLiteral("newDocumentRecentList"));
  recent_list->setViewMode(QListView::IconMode);
  recent_list->setFlow(QListView::LeftToRight);
  recent_list->setWrapping(false);
  recent_list->setMovement(QListView::Static);
  recent_list->setSelectionMode(QAbstractItemView::SingleSelection);
  recent_list->setUniformItemSizes(true);
  recent_list->setGridSize(kPresetCardCell);
  recent_list->setMouseTracking(true);
  recent_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  recent_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  recent_list->setItemDelegate(new PresetCardDelegate(recent_list));
  recent_list->setFrameShape(QFrame::NoFrame);
  recent_list->setFixedHeight(kRecentRowHeight);
  list_area->addWidget(recent_list);

  auto* divider = new QFrame(list_frame);
  divider->setObjectName(QStringLiteral("newDocumentListDivider"));
  divider->setFixedHeight(1);
  list_area->addSpacing(4);
  list_area->addWidget(divider);
  list_area->addSpacing(2);

  auto* group_label = new QLabel(list_frame);
  group_label->setObjectName(QStringLiteral("newDocumentGroupLabel"));
  group_label->setTextFormat(Qt::PlainText);
  list_area->addWidget(group_label);

  auto* no_matches = new QLabel(QObject::tr("No templates match the search."), list_frame);
  no_matches->setObjectName(QStringLiteral("newDocumentNoMatchesLabel"));
  no_matches->hide();

  auto* preset_list = new QListWidget(list_frame);
  preset_list->setObjectName(QStringLiteral("newDocumentPresetList"));
  preset_list->setViewMode(QListView::IconMode);
  preset_list->setMovement(QListView::Static);
  preset_list->setResizeMode(QListView::Adjust);
  preset_list->setSelectionMode(QAbstractItemView::SingleSelection);
  preset_list->setUniformItemSizes(true);
  preset_list->setGridSize(kPresetCardCell);
  preset_list->setMouseTracking(true);
  preset_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  preset_list->setItemDelegate(new PresetCardDelegate(preset_list));
  preset_list->setFrameShape(QFrame::NoFrame);
  preset_list->setMinimumSize(kPresetGridMinimum);
  list_area->addWidget(no_matches);
  list_area->addWidget(preset_list, 1);

  // Right pane: the details grid. Fixed field/unit column widths keep every row the
  // same size (the old form let the width row stretch wider than the others).
  auto* right = new QVBoxLayout();
  right->setSpacing(10);
  body->addLayout(right, 0);

  constexpr int kFieldWidth = 96;
  constexpr int kUnitWidth = 128;

  auto* grid = new QGridLayout();
  grid->setContentsMargins(0, 2, 0, 0);
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(9);
  right->addLayout(grid);

  auto* width = new QDoubleSpinBox(&dialog);
  width->setObjectName(QStringLiteral("newDocumentWidthSpin"));
  configure_dialog_spinbox(width, kFieldWidth);
  width->setFixedWidth(kFieldWidth);
  auto* height = new QDoubleSpinBox(&dialog);
  height->setObjectName(QStringLiteral("newDocumentHeightSpin"));
  configure_dialog_spinbox(height, kFieldWidth);
  height->setFixedWidth(kFieldWidth);

  auto* unit = new QComboBox(&dialog);
  unit->setObjectName(QStringLiteral("newDocumentUnitCombo"));
  for (const auto dimension_unit : {MeasurementUnit::Pixels, MeasurementUnit::Inches,
                                    MeasurementUnit::Centimeters, MeasurementUnit::Millimeters}) {
    unit->addItem(measurement_unit_name(dimension_unit), static_cast<int>(dimension_unit));
  }
  unit->setFixedWidth(kUnitWidth);

  auto* swap_dimensions = new QToolButton(&dialog);
  swap_dimensions->setObjectName(QStringLiteral("newDocumentSwapDimensionsButton"));
  // The options bars' swap glyph (GitHub issue 286): one icon for every width/height swap.
  swap_dimensions->setIcon(themed_svg_icon(QStringLiteral("swap-dimensions")));
  swap_dimensions->setIconSize(QSize(18, 18));
  swap_dimensions->setToolTip(QObject::tr("Swap width and height"));
  swap_dimensions->setFixedWidth(28);

  auto* resolution = new QDoubleSpinBox(&dialog);
  resolution->setObjectName(QStringLiteral("newDocumentResolutionSpin"));
  resolution->setDecimals(2);
  resolution->setRange(0.01, 9999.0);
  configure_dialog_spinbox(resolution, kFieldWidth);
  resolution->setFixedWidth(kFieldWidth);
  auto* resolution_unit = new QComboBox(&dialog);
  resolution_unit->setObjectName(QStringLiteral("newDocumentResolutionUnitCombo"));
  resolution_unit->addItem(QObject::tr("Pixels/Inch"), 1.0);
  resolution_unit->addItem(QObject::tr("Pixels/Centimeter"), 2.54);
  resolution_unit->setFixedWidth(kUnitWidth);

  auto* background = new QComboBox(&dialog);
  background->setObjectName(QStringLiteral("newDocumentBackgroundCombo"));
  background->addItem(QObject::tr("Other..."), QColor(Qt::white));
  background->addItem(QObject::tr("White"), QColor(Qt::white));
  background->addItem(QObject::tr("Black"), QColor(Qt::black));
  background->addItem(QObject::tr("Transparent"), QColor(0, 0, 0, 0));
  auto* background_swatch = new QPushButton(&dialog);
  background_swatch->setObjectName(QStringLiteral("newDocumentBackgroundSwatch"));
  background_swatch->setAccessibleName(QObject::tr("Background color"));
  background_swatch->setToolTip(QObject::tr("Choose background color"));
  background_swatch->setCursor(Qt::PointingHandCursor);
  background_swatch->setFocusPolicy(Qt::StrongFocus);

  // The document's name (GitHub issue 374): the tab title and the default Save As name.
  // Characters a file name cannot hold are dropped as they are typed.
  const auto fallback_name = default_name.isEmpty() ? QObject::tr("Untitled-1") : default_name;
  auto* name = new QLineEdit(fallback_name, &dialog);
  name->setObjectName(QStringLiteral("newDocumentNameEdit"));
  name->setToolTip(QObject::tr("The document's tab title and the file name Save As suggests"));
  name->setFixedWidth(kFieldWidth + 8 + kUnitWidth);

  // RGB Color or CMYK Proof (GitHub issue 360), the two modes Image > Mode can start from.
  // CMYK Proof needs a CMYK profile in Preferences, so it is greyed under the formula.
  auto* color_mode = new QComboBox(&dialog);
  color_mode->setObjectName(QStringLiteral("newDocumentColorModeCombo"));
  color_mode->addItem(QObject::tr("RGB Color"), QString(kColorModeRgbToken));
  color_mode->addItem(QObject::tr("CMYK Proof"), QString(kColorModeCmykProofToken));
  const auto cmyk_space = working_cmyk_space();
  const bool cmyk_proof_available = cmyk_space->has_profile();
  if (auto* model = qobject_cast<QStandardItemModel*>(color_mode->model()); model != nullptr) {
    if (auto* proof_item = model->item(1); proof_item != nullptr) {
      proof_item->setEnabled(cmyk_proof_available);
      proof_item->setToolTip(cmyk_proof_available
                                 ? QObject::tr("The canvas shows the CMYK profile set in Preferences. Saving as "
                                               "JPEG, TIFF or PDF writes CMYK.")
                                 : QObject::tr("CMYK Proof needs a CMYK profile. Choose one in Preferences."));
    }
  }
  // The profile the document's colors mean: sRGB for RGB (Patchy edits and shows RGB as
  // sRGB; docs/rgb-profiles.md), the Preferences CMYK profile for CMYK Proof.
  auto* color_profile = new QLabel(&dialog);
  color_profile->setObjectName(QStringLiteral("newDocumentColorProfileLabel"));
  color_profile->setTextFormat(Qt::PlainText);
  color_profile->setWordWrap(true);
  color_profile->setFixedWidth(kFieldWidth + 8 + kUnitWidth);

  const auto add_row_label = [&dialog, grid](const QString& text, int row) {
    grid->addWidget(new QLabel(text, &dialog), row, 0, Qt::AlignRight | Qt::AlignVCenter);
  };
  add_row_label(QObject::tr("Name", "new document name field"), 0);
  grid->addWidget(name, 0, 1, 1, 2);
  add_row_label(QObject::tr("Width"), 1);
  grid->addWidget(width, 1, 1);
  grid->addWidget(unit, 1, 2, Qt::AlignLeft);
  add_row_label(QObject::tr("Height"), 2);
  grid->addWidget(height, 2, 1);
  grid->addWidget(swap_dimensions, 2, 2, Qt::AlignLeft);
  add_row_label(QObject::tr("Resolution"), 3);
  grid->addWidget(resolution, 3, 1);
  grid->addWidget(resolution_unit, 3, 2, Qt::AlignLeft);
  add_row_label(QObject::tr("Background"), 4);
  auto* background_row = new QHBoxLayout();
  background_row->setContentsMargins(0, 0, 0, 0);
  background_row->setSpacing(8);
  background_row->addWidget(background, 1);
  background_row->addWidget(background_swatch, 0);
  grid->addLayout(background_row, 4, 1, 1, 2);
  add_row_label(QObject::tr("Color Mode"), 5);
  grid->addWidget(color_mode, 5, 1, 1, 2);
  // Bits per channel, while deep editing is on (docs/high-bit-depth.md).
  auto* bit_depth = new QComboBox(&dialog);
  bit_depth->setObjectName(QStringLiteral("newDocumentBitDepthCombo"));
  bit_depth->addItem(QObject::tr("8 Bits/Channel"), 8);
  bit_depth->addItem(QObject::tr("16 Bits/Channel"), 16);
  if (depth_supported_on_platform(BitDepth::Float32)) {
    bit_depth->addItem(QObject::tr("32 Bits/Channel"), 32);
  }
  if (deep_editing_enabled()) {
    add_row_label(QObject::tr("Bit Depth"), 6);
    grid->addWidget(bit_depth, 6, 1, 1, 2);
    // 8 bits, as in Photoshop's presets; only the Last Used card brings back another depth.
    bit_depth->setCurrentIndex(0);
  } else {
    bit_depth->hide();
  }
  auto* artboards = new QCheckBox(QObject::tr("Artboards"), &dialog);
  artboards->setObjectName(QStringLiteral("newDocumentArtboardsCheck"));
  artboards->setToolTip(QObject::tr("Start the document as an artboard the size of the canvas"));
  artboards->setChecked(app_settings().value(QStringLiteral("newDocument/artboards"), false).toBool());
  add_row_label(QObject::tr("Color Profile"), 7);
  grid->addWidget(color_profile, 7, 1, 1, 2);
  grid->addWidget(artboards, 8, 1, 1, 2);

  auto* summary = new QLabel(&dialog);
  summary->setObjectName(QStringLiteral("newDocumentSummaryLabel"));
  summary->setTextFormat(Qt::PlainText);
  summary->setMinimumHeight(52);
  summary->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  right->addSpacing(4);
  right->addWidget(summary);
  right->addStretch(1);

  auto* buttons_row = new QHBoxLayout();
  buttons_row->setSpacing(8);
  // Saves the settings as a card in the Saved group (GitHub issue 374); enabled only while
  // no template or saved preset already holds them.
  auto* save_preset = new QPushButton(QObject::tr("Save Preset..."), &dialog);
  save_preset->setObjectName(QStringLiteral("newDocumentSavePresetButton"));
  save_preset->setToolTip(QObject::tr("Save these settings as a preset in the Saved group"));
  save_preset->setAutoDefault(false);
  buttons_row->addWidget(save_preset);
  buttons_row->addStretch(1);
  auto* create = new QPushButton(QObject::tr("Create"), &dialog);
  create->setObjectName(QStringLiteral("newDocumentCreateButton"));
  create->setDefault(true);
  auto* cancel = new QPushButton(QObject::tr("Cancel"), &dialog);
  cancel->setObjectName(QStringLiteral("newDocumentCancelButton"));
  buttons_row->addWidget(create);
  buttons_row->addWidget(cancel);
  right->addLayout(buttons_row);

  // Pixels are the stored truth; the width/height spins convert through the chosen
  // unit at the chosen resolution, like Image Size.
  struct NewDocumentState {
    int pixel_width{1024};
    int pixel_height{768};
    double ppi{kScreenPpi};
  };
  NewDocumentState state;
  QString current_preset_id = QStringLiteral("custom");
  QString current_preset_name = QObject::tr("Custom");
  QColor background_color(Qt::white);
  // The documents created here, newest first, which the Recent cards restore; empty
  // until a document has been created from the dialog.
  std::vector<RecentDocumentSettings> recents;
  std::vector<UserPreset> user_presets = load_user_presets();

  const auto current_unit = [unit] { return static_cast<MeasurementUnit>(unit->currentData().toInt()); };
  const auto cmyk_proof_chosen = [color_mode] {
    return color_mode->currentData().toString() == kColorModeCmykProofToken;
  };
  const auto current_document_settings = [&state, &background_color, current_unit, cmyk_proof_chosen, resolution_unit,
                                          bit_depth, artboards] {
    RecentDocumentSettings current;
    current.size = QSize(state.pixel_width, state.pixel_height);
    current.ppi = state.ppi;
    current.unit = current_unit();
    current.resolution_unit_index = resolution_unit->currentIndex();
    current.background = background_color;
    current.bits = deep_editing_enabled() ? bit_depth->currentData().toInt() : 8;
    current.artboards = artboards->isChecked();
    current.cmyk_proof = cmyk_proof_chosen();
    return current;
  };
  // A template holds its size and resolution with the plain settings (white, 8 bits, RGB,
  // no artboards); a saved preset holds everything.
  const auto refresh_save_preset = [&current_preset_id, &user_presets, current_document_settings, save_preset] {
    const auto current = current_document_settings();
    bool held = current_preset_id == kClipboardPresetId;
    for (const auto category : kPresetCategories) {
      if (held || category == PresetCategory::Saved) {
        continue;
      }
      for (const auto& preset : presets_for_category(category)) {
        RecentDocumentSettings plain;
        plain.size = preset.size;
        plain.ppi = preset.ppi;
        if (plain.same_document(current)) {
          held = true;
          break;
        }
      }
    }
    held = held || std::any_of(user_presets.begin(), user_presets.end(),
                               [&current](const UserPreset& preset) { return preset.settings.same_document(current); });
    save_preset->setEnabled(!held);
  };
  const auto refresh_color_profile = [color_profile, cmyk_proof_chosen, cmyk_space] {
    if (cmyk_proof_chosen()) {
      color_profile->setText(QObject::tr("%1, from Preferences", "new document color profile: the CMYK working profile")
                                 .arg(QString::fromStdString(cmyk_space->profile_description())));
      color_profile->setToolTip(QObject::tr("CMYK Proof keeps the pixels in RGB and shows them through this "
                                            "profile. Change it in Preferences."));
    } else {
      color_profile->setText(QString::fromLatin1(kSrgbProfileName));
      color_profile->setToolTip(QObject::tr("Patchy edits and shows RGB documents in sRGB."));
    }
  };
  const auto refresh_dimension_spins = [&state, current_unit, width, height] {
    const auto dimension_unit = current_unit();
    for (auto* spin : {width, height}) {
      const QSignalBlocker blocker(spin);
      spin->setDecimals(measurement_unit_decimals(dimension_unit));
      spin->setSingleStep(measurement_unit_single_step(dimension_unit));
      spin->setRange(dimension_unit == MeasurementUnit::Pixels ? 1.0 : 0.001, 999999.0);
    }
    {
      const QSignalBlocker blocker(width);
      width->setValue(pixels_to_measurement_unit(state.pixel_width, dimension_unit, state.ppi, state.pixel_width));
    }
    {
      const QSignalBlocker blocker(height);
      height->setValue(
          pixels_to_measurement_unit(state.pixel_height, dimension_unit, state.ppi, state.pixel_height));
    }
  };
  const auto refresh_resolution_spin = [&state, resolution, resolution_unit] {
    const QSignalBlocker blocker(resolution);
    resolution->setValue(state.ppi / resolution_unit->currentData().toDouble());
  };

  const auto format_pixel_bytes = [](double bytes) {
    if (bytes >= 1024.0 * 1024.0) {
      return QObject::tr("%1M").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    }
    return QObject::tr("%1K").arg(bytes / 1024.0, 0, 'f', 1);
  };
  const auto update_summary = [&state, &current_preset_name, summary, format_pixel_bytes, bit_depth, current_unit,
                               refresh_save_preset] {
    // Bullet built from its code point: the sources build without /utf-8, so a raw
    // non-ASCII character in a literal would depend on the system codepage.
    const QString separator = QStringLiteral("  ") + QChar(0x2022) + QStringLiteral("  ");
    auto dimensions_line = QObject::tr("%1 x %2 px").arg(state.pixel_width).arg(state.pixel_height);
    const int divisor = std::gcd(std::max(1, state.pixel_width), std::max(1, state.pixel_height));
    const int ratio_width = state.pixel_width / divisor;
    const int ratio_height = state.pixel_height / divisor;
    if (std::max(ratio_width, ratio_height) <= 20) {
      dimensions_line += separator + QStringLiteral("%1:%2").arg(ratio_width).arg(ratio_height);
    }
    // A per-layer memory estimate at RGBA and the chosen depth (2 or 4 bytes a channel
    // at 16 and 32 bits); deliberately simple (documents open with two layers and
    // formats vary), it exists to make huge sizes register as huge.
    const auto bytes_per_channel = std::max(1, bit_depth->currentData().toInt() / 8);
    dimensions_line += separator +
                       format_pixel_bytes(static_cast<double>(state.pixel_width) *
                                          static_cast<double>(state.pixel_height) * 4.0 *
                                          static_cast<double>(bytes_per_channel));
    // The printed size in the fields' unit, inches while they show pixels.
    const auto physical_unit = measurement_unit_is_physical(current_unit()) ? current_unit() : MeasurementUnit::Inches;
    const auto physical_line =
        QObject::tr("%1 at %2 ppi", "new document summary: printed size, resolution")
            .arg(preset_size_text(QSize(state.pixel_width, state.pixel_height), state.ppi, physical_unit),
                 QString::number(state.ppi, 'g', 6));
    summary->setText(current_preset_name + QChar('\n') + dimensions_line + QChar('\n') + physical_line);
    refresh_save_preset();
  };

  const auto set_fields_enabled = [width, height, unit, resolution, resolution_unit, background,
                                   background_swatch, swap_dimensions, color_mode](bool enabled) {
    width->setEnabled(enabled);
    height->setEnabled(enabled);
    unit->setEnabled(enabled);
    resolution->setEnabled(enabled);
    resolution_unit->setEnabled(enabled);
    background->setEnabled(enabled);
    background_swatch->setEnabled(enabled);
    swap_dimensions->setEnabled(enabled);
    color_mode->setEnabled(enabled);  // a Clipboard document opens in RGB
  };

  const auto update_swatch = [background_swatch, &background_color] {
    QPixmap swatch(34, 18);
    QPainter painter(&swatch);
    constexpr int kTile = 6;  // checkerboard base so a transparent background reads as transparent
    for (int y = 0; y < swatch.height(); y += kTile) {
      for (int x = 0; x < swatch.width(); x += kTile) {
        const bool light = ((x / kTile) + (y / kTile)) % 2 == 0;
        painter.fillRect(QRect(x, y, kTile, kTile), light ? QColor(212, 212, 212) : QColor(150, 150, 150));
      }
    }
    painter.fillRect(swatch.rect(), background_color);
    painter.setPen(QPen(QColor(26, 26, 26), 1));
    painter.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
    painter.end();
    background_swatch->setIcon(QIcon(swatch));
    background_swatch->setIconSize(swatch.size());
  };
  const auto select_background_index_for = [background](const QColor& color) {
    const QSignalBlocker blocker(background);
    for (int index = 1; index < background->count(); ++index) {
      if (background->itemData(index).value<QColor>() == color) {
        background->setCurrentIndex(index);
        return;
      }
    }
    background->setItemData(0, color);
    background->setCurrentIndex(0);
  };

  const auto clear_list_selection = [](QListWidget* list) {
    const QSignalBlocker blocker(list);
    list->setCurrentItem(nullptr);
    list->clearSelection();
  };
  const auto mark_custom = [&current_preset_id, &current_preset_name, preset_list, recent_list, update_summary,
                            set_fields_enabled, clear_list_selection] {
    if (current_preset_id != QStringLiteral("custom")) {
      current_preset_id = QStringLiteral("custom");
      current_preset_name = QObject::tr("Custom");
      clear_list_selection(preset_list);
      clear_list_selection(recent_list);
      set_fields_enabled(true);
    }
    update_summary();
  };

  const auto populate_list = [&](PresetCategory category) {
    const QSignalBlocker blocker(preset_list);
    preset_list->clear();
    group_label->setText(preset_category_name(category));
    for (const auto& preset : presets_for_category(category)) {
      auto* item = new QListWidgetItem(preset.name, preset_list);
      item->setData(kPresetIdRole, preset.id);
      set_card_size_data(item, preset.size, preset.ppi, preset.unit);
      item->setData(kPresetClipboardRole, false);
      if (preset.size.isValid() && preset.size.width() > 0) {
        item->setToolTip(QObject::tr("%1: %2 x %3 px at %4 ppi")
                             .arg(preset.name)
                             .arg(preset.size.width())
                             .arg(preset.size.height())
                             .arg(QString::number(preset.ppi)));
      }
      if (is_user_preset_id(preset.id)) {
        item->setToolTip(item->toolTip() + QChar('\n') +
                         QObject::tr("Press Delete or right-click the card to delete this preset."));
      }
      if (preset.id == current_preset_id) {
        preset_list->setCurrentItem(item);
        item->setSelected(true);
      }
    }
  };

  const auto populate_recent_row = [&] {
    const QSignalBlocker blocker(recent_list);
    recent_list->clear();
    // Until the row is laid out its width means nothing: build every card (so the opening
    // selection exists and applies), and the first resize trims the row to what fits.
    const auto fit = recent_list->isVisible() ? std::max(1, recent_list->viewport()->width() / kPresetCardCell.width())
                                              : kMaxRecentDocuments + 1;
    if (!clipboard_image.isNull()) {
      auto* item = new QListWidgetItem(QObject::tr("Clipboard"), recent_list);
      item->setData(kPresetIdRole, QString(kClipboardPresetId));
      set_card_size_data(item, clipboard_image.size(), kScreenPpi, MeasurementUnit::Pixels);
      item->setData(kPresetClipboardRole, true);
      item->setData(kPresetThumbnailRole, QVariant::fromValue(QPixmap::fromImage(clipboard_image.scaled(
                                              112, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
      item->setToolTip(QObject::tr("Create the document from the clipboard image"));
    }
    for (std::size_t index = 0; index < recents.size() && recent_list->count() < fit; ++index) {
      const auto& recent = recents[index];
      const auto id = index == 0 ? QString(kLastUsedPresetId) : QStringLiteral("recent-%1").arg(index);
      auto* item = new QListWidgetItem(index == 0 ? QObject::tr("Last Used") : recent_document_name(recent),
                                       recent_list);
      item->setData(kPresetIdRole, id);
      set_card_size_data(item, recent.size, recent.ppi, recent.unit);
      item->setData(kPresetClipboardRole, false);
      item->setData(kPresetRecentIndexRole, static_cast<int>(index));
      item->setToolTip((index == 0 ? QObject::tr("The settings of the last document created here: %1 x %2 px at %3 ppi")
                                   : QObject::tr("The settings of a recent document: %1 x %2 px at %3 ppi"))
                           .arg(recent.size.width())
                           .arg(recent.size.height())
                           .arg(QString::number(recent.ppi)));
    }
    for (int row = 0; row < recent_list->count(); ++row) {
      if (recent_list->item(row)->data(kPresetIdRole).toString() == current_preset_id) {
        recent_list->setCurrentItem(recent_list->item(row));
        recent_list->item(row)->setSelected(true);
      }
    }
  };

  // The chip shown while not searching; restored when the search clears.
  int browsing_category = static_cast<int>(PresetCategory::Screen);
  const auto populate_search = [&](const QString& text) {
    const QSignalBlocker blocker(preset_list);
    preset_list->clear();
    group_label->setText(QObject::tr("Search results", "new document template list caption"));
    QListWidgetItem* current = nullptr;
    for (const auto category : kPresetCategories) {
      for (const auto& preset : presets_for_category(category)) {
        if (!preset_matches_search(preset, category, text)) {
          continue;
        }
        auto* item = new QListWidgetItem(preset.name, preset_list);
        item->setData(kPresetIdRole, preset.id);
        set_card_size_data(item, preset.size, preset.ppi, preset.unit);
        item->setData(kPresetClipboardRole, false);
        item->setToolTip(QObject::tr("%1 (%2): %3 x %4 px at %5 ppi")
                             .arg(preset.name, preset_category_name(category))
                             .arg(preset.size.width())
                             .arg(preset.size.height())
                             .arg(QString::number(preset.ppi)));
        if (preset.id == current_preset_id) {
          current = item;
        }
      }
    }
    no_matches->setVisible(preset_list->count() == 0);
    // The current template stays selected when it matches; otherwise the first match
    // is, so Enter creates it.
    if (current == nullptr && preset_list->count() > 0) {
      current = preset_list->item(0);
    }
    if (current != nullptr) {
      preset_list->setCurrentItem(current);
      current->setSelected(true);
      preset_list->scrollToItem(current);
    }
    return current;
  };

  // False while the dialog opens: the opening card keeps the remembered unit (or the ruler
  // unit before a first create); every later pick of a template takes the template's unit.
  bool templates_set_unit = false;
  const auto apply_preset_item = [&](QListWidgetItem* item) {
    if (item == nullptr) {
      return;
    }
    current_preset_id = item->data(kPresetIdRole).toString();
    current_preset_name = item->text();
    const auto size = item->data(kPresetSizeRole).toSize();
    if (size.isValid() && size.width() > 0) {
      state.pixel_width = std::clamp(size.width(), 1, 30000);
      state.pixel_height = std::clamp(size.height(), 1, 30000);
    }
    if (const auto preset_ppi = item->data(kPresetPpiRole).toDouble(); preset_ppi > 0.0) {
      state.ppi = preset_ppi;
    }
    const RecentDocumentSettings* restored = nullptr;
    if (const auto recent_index = item->data(kPresetRecentIndexRole);
        recent_index.isValid() && recent_index.toInt() >= 0 && recent_index.toInt() < static_cast<int>(recents.size())) {
      restored = &recents[static_cast<std::size_t>(recent_index.toInt())];
    } else if (is_user_preset_id(current_preset_id)) {
      const auto found = std::find_if(user_presets.begin(), user_presets.end(),
                                      [&current_preset_id](const UserPreset& preset) { return preset.id == current_preset_id; });
      if (found != user_presets.end()) {
        restored = &found->settings;
      }
    }
    if (restored != nullptr) {
      // A Recent card or a saved preset restores every setting, not just the size.
      const auto& recent = *restored;
      {
        const QSignalBlocker blocker(unit);
        unit->setCurrentIndex(std::max(0, unit->findData(static_cast<int>(recent.unit))));
      }
      {
        const QSignalBlocker blocker(resolution_unit);
        resolution_unit->setCurrentIndex(recent.resolution_unit_index);
      }
      background_color = recent.background;
      select_background_index_for(background_color);
      update_swatch();
      if (const auto index = bit_depth->findData(recent.bits); index >= 0 && deep_editing_enabled()) {
        bit_depth->setCurrentIndex(index);
      }
      artboards->setChecked(recent.artboards);
      color_mode->setCurrentIndex(recent.cmyk_proof && cmyk_proof_available ? 1 : 0);
    } else if (templates_set_unit && !item->data(kPresetClipboardRole).toBool()) {
      // A paper or print template shows its size in its own unit (210 x 297 mm), a screen
      // one in pixels; the pixels stay the template's either way.
      const QSignalBlocker blocker(unit);
      unit->setCurrentIndex(std::max(0, unit->findData(item->data(kPresetUnitRole).toInt())));
    }
    refresh_dimension_spins();
    refresh_resolution_spin();
    set_fields_enabled(!item->data(kPresetClipboardRole).toBool());
    update_summary();
  };

  // One card is current across both lists: picking in one clears the other.
  QObject::connect(preset_list, &QListWidget::currentItemChanged, &dialog, [&](QListWidgetItem* item, QListWidgetItem*) {
    if (item != nullptr) {
      clear_list_selection(recent_list);
    }
    apply_preset_item(item);
  });
  QObject::connect(recent_list, &QListWidget::currentItemChanged, &dialog, [&](QListWidgetItem* item, QListWidgetItem*) {
    if (item != nullptr) {
      clear_list_selection(preset_list);
    }
    apply_preset_item(item);
  });
  recent_list->viewport()->installEventFilter(new ResizeWatcher(recent_list, [&] { populate_recent_row(); }));
  // An image copied or cleared while the dialog is open adds or removes the Clipboard card.
  QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged, &dialog, [&] {
    clipboard_image = clipboard_offers_image() ? QApplication::clipboard()->image() : QImage();
    if (current_preset_id == kClipboardPresetId && clipboard_image.isNull()) {
      mark_custom();
    }
    populate_recent_row();
  });
  const auto searching = [search] { return !search->text().trimmed().isEmpty(); };
  auto* saved_chip = chip_group->button(static_cast<int>(PresetCategory::Saved));
  const auto refresh_saved_chip = [saved_chip, &user_presets] {
    if (saved_chip != nullptr) {
      saved_chip->setVisible(!user_presets.empty());
    }
  };
  refresh_saved_chip();
  QObject::connect(chip_group, &QButtonGroup::idToggled, &dialog, [&](int id, bool checked) {
    if (!checked) {
      return;
    }
    browsing_category = id;
    // The group the user last picked opens next time, whether this dialog creates or cancels.
    app_settings().setValue(QStringLiteral("newDocument/lastCategory"),
                            preset_category_token(static_cast<PresetCategory>(id)));
    if (searching()) {
      // A chip picked during a search leaves the search for that group.
      const QSignalBlocker blocker(search);
      search->clear();
      no_matches->hide();
      chip_group->setExclusive(true);
    }
    populate_list(static_cast<PresetCategory>(id));
  });
  QObject::connect(search, &QLineEdit::textChanged, &dialog, [&](const QString& text) {
    if (!text.trimmed().isEmpty()) {
      // Searching: no chip is current, and the list holds only the matches.
      if (auto* chip = chip_group->checkedButton(); chip != nullptr) {
        const QSignalBlocker blocker(chip_group);
        chip_group->setExclusive(false);
        chip->setChecked(false);
      }
      const auto previous = current_preset_id;
      if (auto* item = populate_search(text); item != nullptr && item->data(kPresetIdRole).toString() != previous) {
        clear_list_selection(recent_list);
        apply_preset_item(item);
      }
      return;
    }
    // Cleared: back to the group that was showing.
    no_matches->hide();
    chip_group->setExclusive(true);
    if (auto* chip = chip_group->button(browsing_category); chip != nullptr) {
      const QSignalBlocker blocker(chip_group);
      chip->setChecked(true);
    }
    populate_list(static_cast<PresetCategory>(browsing_category));
  });
  // Up, Down and the page keys move through the cards while typing; Escape clears a
  // search before it can close the dialog. Return reaches the dialog's Create button.
  class SearchKeys final : public QObject {
   public:
    SearchKeys(QLineEdit* edit, QListWidget* list) : QObject(edit), edit_(edit), list_(list) {}

   protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
      if (watched != edit_ || event->type() != QEvent::KeyPress) {
        return false;
      }
      auto* key = static_cast<QKeyEvent*>(event);
      switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
          QApplication::sendEvent(list_, key);
          return true;
        case Qt::Key_Escape:
          if (!edit_->text().isEmpty()) {
            edit_->clear();
            return true;
          }
          return false;
        default:
          return false;
      }
    }

   private:
    QLineEdit* edit_;
    QListWidget* list_;
  };
  search->installEventFilter(new SearchKeys(search, preset_list));
  auto* focus_search = new QShortcut(QKeySequence::Find, &dialog);
  QObject::connect(focus_search, &QShortcut::activated, &dialog, [search] {
    search->setFocus(Qt::ShortcutFocusReason);
    search->selectAll();
  });

  const auto handle_dimension_edit = [&state, current_unit, mark_custom](QDoubleSpinBox* spin,
                                                                         bool editing_width) {
    const auto pixels = std::clamp(
        static_cast<int>(std::lround(measurement_unit_to_pixels(spin->value(), current_unit(), state.ppi, 0.0))),
        1, 30000);
    (editing_width ? state.pixel_width : state.pixel_height) = pixels;
    mark_custom();
  };
  QObject::connect(width, &QDoubleSpinBox::valueChanged, &dialog, [&] { handle_dimension_edit(width, true); });
  QObject::connect(height, &QDoubleSpinBox::valueChanged, &dialog, [&] { handle_dimension_edit(height, false); });
  QObject::connect(unit, &QComboBox::currentIndexChanged, &dialog, [&](int) {
    refresh_dimension_spins();
    update_summary();
  });
  QObject::connect(resolution, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
    const auto new_ppi = std::clamp(value * resolution_unit->currentData().toDouble(), 0.01, 9999.0);
    if (measurement_unit_is_physical(current_unit())) {
      // Physical entry holds its size across a resolution change (Photoshop's
      // new-document behavior): the pixel dimensions re-derive.
      state.pixel_width =
          std::clamp(static_cast<int>(std::lround(state.pixel_width / state.ppi * new_ppi)), 1, 30000);
      state.pixel_height =
          std::clamp(static_cast<int>(std::lround(state.pixel_height / state.ppi * new_ppi)), 1, 30000);
      state.ppi = new_ppi;
      refresh_dimension_spins();
    } else {
      state.ppi = new_ppi;
    }
    mark_custom();
  });
  QObject::connect(resolution_unit, &QComboBox::currentIndexChanged, &dialog,
                   [&](int) { refresh_resolution_spin(); });
  QObject::connect(swap_dimensions, &QToolButton::clicked, &dialog, [&] {
    std::swap(state.pixel_width, state.pixel_height);
    mark_custom();
    refresh_dimension_spins();
  });

  const auto choose_background_color = [&dialog, &background_color, update_swatch,
                                        select_background_index_for, refresh_save_preset] {
    // Patchy's own picker (palettes, names, hex), previewed live on the swatch. It picks
    // opaque colors; a transparent canvas is the combo's own "Transparent" entry.
    const auto original = background_color;
    const auto selected = request_patchy_color(&dialog, original, QObject::tr("Background Color"),
                                               [&background_color, update_swatch](QColor color) {
                                                 background_color = color;
                                                 update_swatch();
                                               });
    background_color = selected.value_or(original);
    select_background_index_for(background_color);
    update_swatch();
    refresh_save_preset();
  };
  // activated covers re-picking "Other..." while it is already current; the fixed
  // colors go through currentIndexChanged so programmatic changes (tests) match
  // user picks.
  QObject::connect(background, &QComboBox::activated, &dialog, [&](int index) {
    if (index == 0) {
      choose_background_color();
    }
  });
  QObject::connect(background, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
    if (index > 0) {
      background_color = background->itemData(index).value<QColor>();
      update_swatch();
      refresh_save_preset();
    }
  });
  QObject::connect(artboards, &QCheckBox::toggled, &dialog, [&](bool) { refresh_save_preset(); });
  QObject::connect(color_mode, &QComboBox::currentIndexChanged, &dialog, [&](int) {
    refresh_color_profile();
    refresh_save_preset();
  });

  // Save Preset: a name, then the card joins the Saved group, selected.
  QObject::connect(save_preset, &QPushButton::clicked, &dialog, [&] {
    QInputDialog prompt(&dialog);
    prompt.setObjectName(QStringLiteral("newDocumentSavePresetDialog"));
    prompt.setWindowTitle(QObject::tr("Save Preset"));
    prompt.setLabelText(QObject::tr("Preset name:"));
    prompt.setInputMode(QInputDialog::TextInput);
    prompt.setTextValue(QObject::tr("%1 x %2 px").arg(state.pixel_width).arg(state.pixel_height));
    if (exec_dialog(prompt) != QDialog::Accepted) {
      return;
    }
    const auto preset_name = prompt.textValue().simplified();
    if (preset_name.isEmpty()) {
      return;
    }
    int next = 1;
    for (const auto& preset : user_presets) {
      bool numbered = false;
      const auto number = preset.id.mid(5).toInt(&numbered);
      if (numbered) {
        next = std::max(next, number + 1);
      }
    }
    user_presets.push_back({QStringLiteral("user-%1").arg(next), preset_name, current_document_settings()});
    store_user_presets(user_presets);
    current_preset_id = user_presets.back().id;
    current_preset_name = preset_name;
    clear_list_selection(recent_list);
    refresh_saved_chip();
    if (saved_chip != nullptr && !saved_chip->isChecked()) {
      saved_chip->setChecked(true);  // leaves a search and fills the grid, the new card selected
    } else {
      populate_list(PresetCategory::Saved);
    }
    set_fields_enabled(true);
    update_summary();
  });
  // Deleting a saved preset: Delete on its card, or its context menu. Templates stay.
  const auto delete_user_preset = [&](const QString& id) {
    const bool was_current = current_preset_id == id;
    std::erase_if(user_presets, [&id](const UserPreset& preset) { return preset.id == id; });
    store_user_presets(user_presets);
    refresh_saved_chip();
    if (user_presets.empty() && !searching() && browsing_category == static_cast<int>(PresetCategory::Saved)) {
      if (auto* screen_chip = chip_group->button(static_cast<int>(PresetCategory::Screen)); screen_chip != nullptr) {
        screen_chip->setChecked(true);
      }
    } else if (searching()) {
      populate_search(search->text());
      if (auto* item = preset_list->currentItem();
          item != nullptr && item->data(kPresetIdRole).toString() != current_preset_id) {
        clear_list_selection(preset_list);
      }
    } else {
      populate_list(static_cast<PresetCategory>(browsing_category));
    }
    if (was_current) {
      mark_custom();
    } else {
      update_summary();
    }
  };
  // Delete or Backspace on the grid deletes the current card when it is a saved preset.
  auto* delete_current_preset = new QAction(QObject::tr("Delete Preset"), preset_list);
  delete_current_preset->setObjectName(QStringLiteral("newDocumentDeleteCurrentPresetAction"));
  delete_current_preset->setShortcuts({QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)});
  delete_current_preset->setShortcutContext(Qt::WidgetShortcut);
  preset_list->addAction(delete_current_preset);
  QObject::connect(delete_current_preset, &QAction::triggered, &dialog, [&] {
    if (auto* item = preset_list->currentItem();
        item != nullptr && is_user_preset_id(item->data(kPresetIdRole).toString())) {
      delete_user_preset(item->data(kPresetIdRole).toString());
    }
  });
  preset_list->setContextMenuPolicy(Qt::CustomContextMenu);
  QObject::connect(preset_list, &QWidget::customContextMenuRequested, &dialog, [&](const QPoint& position) {
    auto* item = preset_list->itemAt(position);
    if (item == nullptr || !is_user_preset_id(item->data(kPresetIdRole).toString())) {
      return;
    }
    const auto id = item->data(kPresetIdRole).toString();
    QMenu menu(&dialog);
    auto* remove = menu.addAction(QObject::tr("Delete Preset"));
    remove->setObjectName(QStringLiteral("newDocumentDeletePresetAction"));
    if (menu.exec(preset_list->viewport()->mapToGlobal(position)) == remove) {
      delete_user_preset(id);
    }
  });
  QObject::connect(background_swatch, &QPushButton::clicked, &dialog, choose_background_color);

  QObject::connect(name, &QLineEdit::textEdited, &dialog, [name](const QString& text) {
    const auto cleaned = without_illegal_file_name_characters(text);
    if (cleaned != text) {
      const auto cursor = name->cursorPosition() - static_cast<int>(text.size() - cleaned.size());
      name->setText(cleaned);
      name->setCursorPosition(std::clamp(cursor, 0, static_cast<int>(cleaned.size())));
    }
  });
  QObject::connect(create, &QPushButton::clicked, &dialog, &QDialog::accept);
  QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
  // Double-clicking a preset card creates the document straight away: the first
  // click already made it current (and applied its values), so the second is
  // the same as pressing Create.
  for (auto* list : {preset_list, recent_list}) {
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, [create](QListWidgetItem*) { create->click(); });
  }

  // Open on the newest Recent card (the Last Used card), or on Default before anything
  // has been created here. A clipboard image never preselects its card: it would hide the
  // remembered size, and File > Open from Clipboard is the one-step clipboard path.
  {
    auto settings = app_settings();
    settings.beginGroup(QStringLiteral("newDocument"));
    const auto last_background = settings.value(QStringLiteral("lastBackground")).value<QColor>();
    settings.endGroup();
    // The W/H unit and the resolution unit (issue 53): the last accepted ones, else
    // the ruler unit on a first run; the combo has no Points or Percent, so those
    // fall back to Pixels (remembered_dialog_unit).
    const auto seed_unit = remembered_dialog_unit(
        QStringLiteral("newDocument/lastUnit"), {MeasurementUnit::Pixels, MeasurementUnit::Inches,
                                                 MeasurementUnit::Centimeters, MeasurementUnit::Millimeters});
    const auto seed_resolution_unit =
        remembered_resolution_unit_index(QStringLiteral("newDocument/lastResolutionUnit"));
    {
      const QSignalBlocker unit_blocker(unit);  // the first refresh below picks it up once
      unit->setCurrentIndex(std::max(0, unit->findData(static_cast<int>(seed_unit))));
      const QSignalBlocker resolution_unit_blocker(resolution_unit);
      resolution_unit->setCurrentIndex(seed_resolution_unit);
    }

    if (last_background.isValid()) {
      background_color = last_background;
    }
    recents = load_recent_documents(seed_unit, seed_resolution_unit, artboards->isChecked());
    if (!recents.empty()) {
      current_preset_id = QString(kLastUsedPresetId);
    } else {
      current_preset_id = QStringLiteral("screen-1024x768");
    }
  }
  select_background_index_for(background_color);
  update_swatch();

  // The group picked last opens again; before any pick (or with an unknown token), the
  // group of the card the dialog selects.
  auto remembered_category =
      preset_category_from_token(app_settings().value(QStringLiteral("newDocument/lastCategory")).toString());
  if (remembered_category == PresetCategory::Saved && user_presets.empty()) {
    remembered_category.reset();
  }
  const auto opening_category = remembered_category.value_or(category_of_preset_id(current_preset_id));
  if (auto* chip = chip_group->button(static_cast<int>(opening_category)); chip != nullptr) {
    chip->setChecked(true);
  }
  refresh_color_profile();
  populate_recent_row();
  if (auto* item = recent_list->currentItem(); item != nullptr) {
    apply_preset_item(item);
  } else if (auto* grid_item = preset_list->currentItem(); grid_item != nullptr) {
    apply_preset_item(grid_item);
  } else {
    refresh_dimension_spins();
    refresh_resolution_spin();
    set_fields_enabled(true);
    update_summary();
  }
  templates_set_unit = true;
  QObject::connect(bit_depth, &QComboBox::currentIndexChanged, &dialog, [update_summary](int) { update_summary(); });

  append_themed_style(dialog, new_document_dialog_style());
  // As small as the layout allows (GitHub issue 342: it filled short screens).
  dialog.resize(dialog.sizeHint());
  // Always over the main window: a remembered spot (a far corner, another monitor) only made
  // the dialog hard to find (Seth, October 2026).
  mark_dialog_always_centered(dialog);
  width->setFocus(Qt::OtherFocusReason);
  width->selectAll();

  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  const bool clipboard_selected = current_preset_id == kClipboardPresetId;
  const auto chosen_bits = deep_editing_enabled() ? bit_depth->currentData().toInt() : 8;
  const bool chosen_cmyk_proof = !clipboard_selected && cmyk_proof_chosen() && cmyk_proof_available;
  {
    // Every create becomes the next Last Used. A clipboard create contributes only its
    // size and resolution (never the image). lastPresetId is no longer read here; older
    // builds restore from it, so it keeps naming a card they know ("custom" for the
    // clipboard size, unchanged when Last Used itself was accepted).
    auto settings = app_settings();
    settings.beginGroup(QStringLiteral("newDocument"));
    if (clipboard_selected) {
      settings.setValue(QStringLiteral("lastPresetId"), QStringLiteral("custom"));
    } else if (!is_recent_card_id(current_preset_id)) {
      settings.setValue(QStringLiteral("lastPresetId"), current_preset_id);
    }
    settings.setValue(QStringLiteral("lastWidth"), state.pixel_width);
    settings.setValue(QStringLiteral("lastHeight"), state.pixel_height);
    settings.setValue(QStringLiteral("lastPpi"), state.ppi);
    if (!clipboard_selected) {
      settings.setValue(QStringLiteral("lastBackground"), background_color);
      settings.setValue(QStringLiteral("lastBitsPerChannel"), chosen_bits);
    }
    settings.endGroup();
    if (!clipboard_selected) {
      remember_dialog_unit(QStringLiteral("newDocument/lastUnit"), current_unit());
      remember_resolution_unit(QStringLiteral("newDocument/lastResolutionUnit"), resolution_unit->currentIndex());
      app_settings().setValue(QStringLiteral("newDocument/artboards"), artboards->isChecked());
    }
    // The create leads the Recent row next time; an identical earlier one moves up. A
    // clipboard create keeps the newest entry's other settings, as Last Used did.
    RecentDocumentSettings created;
    if (clipboard_selected && !recents.empty()) {
      created = recents.front();
    } else if (!clipboard_selected) {
      created.unit = current_unit();
      created.resolution_unit_index = resolution_unit->currentIndex();
      created.background = background_color;
      created.bits = chosen_bits;
      created.artboards = artboards->isChecked();
      created.cmyk_proof = chosen_cmyk_proof;
    }
    created.size = QSize(state.pixel_width, state.pixel_height);
    created.ppi = state.ppi;
    std::erase(recents, created);
    recents.insert(recents.begin(), created);
    store_recent_documents(recents);
  }
  NewDocumentSettings result;
  result.width = state.pixel_width;
  result.height = state.pixel_height;
  result.resolution_ppi = state.ppi;
  result.background = background_color;
  result.bit_depth = chosen_bits == 32   ? BitDepth::Float32
                     : chosen_bits == 16 ? BitDepth::UInt16
                                         : BitDepth::UInt8;
  result.from_clipboard = clipboard_selected;
  result.clipboard_image = clipboard_selected ? QApplication::clipboard()->image() : QImage();
  result.artboards = !clipboard_selected && artboards->isChecked();
  result.cmyk_proof = chosen_cmyk_proof;
  result.name = sanitized_new_document_name(name->text());
  if (result.name.isEmpty()) {
    result.name = fallback_name;
  }
  return result;
}

}  // namespace patchy::ui
