#include "ui/view_extras.hpp"

#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QSettings>
#include <QVBoxLayout>

#include <array>
#include <utility>
#include <vector>

namespace patchy::ui {
namespace {

struct ViewExtraEntry {
  ViewExtra extra;
  const char* settings_key;  // under view/extras/; persisted, never renamed
  const char* object_name;
  const char* label;
};

// In the dialog's order: selection and path marks first, then the layout aids.
constexpr std::array<ViewExtraEntry, 8> kViewExtraEntries{{
    {ViewExtra::SelectionEdges, "selectionEdges", "viewExtrasSelectionEdgesCheck",
     QT_TRANSLATE_NOOP("QObject", "Selection edges")},
    {ViewExtra::TargetPath, "targetPath", "viewExtrasTargetPathCheck",
     QT_TRANSLATE_NOOP("QObject", "Target path (anchor points and outline)")},
    {ViewExtra::LayerOutlines, "layerOutlines", "viewExtrasLayerOutlinesCheck",
     QT_TRANSLATE_NOOP("QObject", "Layer outlines under the Move tool")},
    {ViewExtra::Guides, "guides", "viewExtrasGuidesCheck", QT_TRANSLATE_NOOP("QObject", "Guides")},
    {ViewExtra::SmartGuides, "smartGuides", "viewExtrasSmartGuidesCheck",
     QT_TRANSLATE_NOOP("QObject", "Alignment lines while moving")},
    {ViewExtra::Grid, "grid", "viewExtrasGridCheck", QT_TRANSLATE_NOOP("QObject", "Grid")},
    {ViewExtra::PixelGrid, "pixelGrid", "viewExtrasPixelGridCheck", QT_TRANSLATE_NOOP("QObject", "Pixel grid")},
    {ViewExtra::ArtboardNames, "artboardNames", "viewExtrasArtboardNamesCheck",
     QT_TRANSLATE_NOOP("QObject", "Artboard names")},
}};

QString settings_key(const ViewExtraEntry& entry) {
  return QStringLiteral("view/extras/") + QLatin1String(entry.settings_key);
}

}  // namespace

std::uint32_t load_view_extras_covered() {
  const auto settings = app_settings();
  std::uint32_t covered = 0;
  for (const auto& entry : kViewExtraEntries) {
    if (settings.value(settings_key(entry), true).toBool()) {
      covered |= view_extra_bit(entry.extra);
    }
  }
  return covered;
}

void save_view_extras_covered(std::uint32_t covered) {
  auto settings = app_settings();
  for (const auto& entry : kViewExtraEntries) {
    settings.setValue(settings_key(entry), (covered & view_extra_bit(entry.extra)) != 0);
  }
}

std::optional<std::uint32_t> request_view_extras_options(QWidget* parent, std::uint32_t covered) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("viewExtrasOptionsDialog"));
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, QObject::tr("Show Extras Options"));

  auto* intro = new QLabel(QObject::tr("View > Extras shows and hides the items ticked here.\n"
                                       "Each item's own View setting still applies."),
                           &dialog);
  intro->setObjectName(QStringLiteral("viewExtrasOptionsIntro"));
  content->addWidget(intro);

  std::vector<std::pair<ViewExtra, QCheckBox*>> boxes;
  for (const auto& entry : kViewExtraEntries) {
    auto* box = new QCheckBox(QObject::tr(entry.label), &dialog);
    box->setObjectName(QLatin1String(entry.object_name));
    box->setChecked((covered & view_extra_bit(entry.extra)) != 0);
    content->addWidget(box);
    boxes.emplace_back(entry.extra, box);
  }

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("viewExtrasOptionsButtonBox"));
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  content->addSpacing(4);
  content->addWidget(buttons);

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  std::uint32_t result = 0;
  for (const auto& [extra, box] : boxes) {
    if (box->isChecked()) {
      result |= view_extra_bit(extra);
    }
  }
  return result;
}

}  // namespace patchy::ui
