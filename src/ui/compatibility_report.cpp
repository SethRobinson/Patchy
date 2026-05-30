#include "ui/compatibility_report.hpp"

#include "core/adjustment_layer.hpp"
#include "core/layer_metadata.hpp"
#include "ui/dialog_utils.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <string>

namespace patchy::ui {

namespace {

QString psd_text_source_block(const Layer& layer) {
  if (const auto found = layer.metadata().find(kLayerMetadataTextSourceBlock); found != layer.metadata().end()) {
    return QString::fromStdString(found->second);
  }
  for (const auto& block : layer.unknown_psd_blocks()) {
    if (block.key == "TySh" || block.key == "tySh") {
      return QString::fromStdString(block.key);
    }
  }
  return {};
}

bool known_round_trip_layer_block(const UnknownPsdBlock& block) {
  return block.key == "luni" || block.key == "plFX" || block.key == "plAD" || block.key == "lfx2" ||
         block.key == "lrFX" || block.key == "lsct" || block.key == "lsdk";
}

int unknown_layer_block_count(const Layer& layer) {
  return static_cast<int>(
      std::count_if(layer.unknown_psd_blocks().begin(), layer.unknown_psd_blocks().end(),
                    [](const UnknownPsdBlock& block) { return !known_round_trip_layer_block(block); }));
}

void append_layer_warnings(const Layer& layer, QStringList& warnings) {
  if (layer.kind() == LayerKind::Group) {
    const auto unknown_blocks = unknown_layer_block_count(layer);
    if (unknown_blocks > 0) {
      warnings << QObject::tr("%1 preserves %2 unknown PSD layer block(s).")
                       .arg(QString::fromStdString(layer.name()))
                       .arg(unknown_blocks);
    }
    for (const auto& child : layer.children()) {
      append_layer_warnings(child, warnings);
    }
    return;
  }

  if (layer_is_text(layer)) {
    const auto source_block = psd_text_source_block(layer);
    const auto raster_status = [&layer] {
      const auto found = layer.metadata().find(kLayerMetadataTextRasterStatus);
      return found == layer.metadata().end() ? std::string{} : found->second;
    }();
    if (raster_status == "placeholder") {
      warnings << QObject::tr("%1: extracted editable PSD text from %2, but Patchy generated a placeholder raster "
                              "preview because the PSD text pixels were not visible.")
                       .arg(QString::fromStdString(layer.name()), source_block.isEmpty() ? QObject::tr("text data")
                                                                                         : source_block);
    } else if (!source_block.isEmpty()) {
      warnings << QObject::tr("%1: extracted editable PSD text from %2 and preserved the original PSD text block; "
                              "the current pixels use the PSD raster preview until the text is edited.")
                       .arg(QString::fromStdString(layer.name()), source_block);
    }
  }
  if (layer.kind() == LayerKind::Adjustment) {
    warnings << QObject::tr("%1 is a Patchy-native adjustment layer; it round-trips in Patchy PSDs but may "
                            "appear as an unsupported adjustment in other editors.")
                     .arg(QString::fromStdString(layer.name()));
  } else if (layer.kind() != LayerKind::Pixel && layer.kind() != LayerKind::Text) {
    warnings << QObject::tr("%1 uses an unsupported layer kind and may not export as editable PSD data.")
                     .arg(QString::fromStdString(layer.name()));
  }

  if (layer.kind() == LayerKind::Pixel && !layer.pixels().empty() &&
      (layer.pixels().format().bit_depth != BitDepth::UInt8 || layer.pixels().format().channels < 3)) {
    warnings << QObject::tr("%1 uses a pixel format that can render but is not fully editable in this build.")
                     .arg(QString::fromStdString(layer.name()));
  }
  const auto unknown_blocks = unknown_layer_block_count(layer);
  if (unknown_blocks > 0) {
    warnings << QObject::tr("%1 preserves %2 unknown PSD layer block(s).")
                     .arg(QString::fromStdString(layer.name()))
                     .arg(unknown_blocks);
  }
}

QString report_text(const QStringList& warnings) {
  QString text;
  for (const auto& warning : warnings) {
    text += QStringLiteral("- ");
    text += warning;
    text += QLatin1Char('\n');
  }
  return text.trimmed();
}

}  // namespace

QStringList compatibility_warnings_for_document(const Document& document) {
  QStringList warnings;
  const auto version = document.metadata().values.find("psd.version");
  if (version != document.metadata().values.end() && version->second == "PSB") {
    warnings << QObject::tr("The file is PSB. This build can read it, but layered PSB writing is not implemented.");
  }
  const auto color_mode = document.metadata().values.find("psd.color_mode");
  if (color_mode != document.metadata().values.end() && color_mode->second != "RGB") {
    if (color_mode->second == "CMYK") {
      warnings << QObject::tr("The source color mode is CMYK; Patchy converted the pixels to RGB/RGBA for editing "
                              "and will export RGB PSD data from this document.");
    } else {
      warnings << QObject::tr("The source color mode is %1; Patchy currently edits through RGB/RGBA workflows.")
                       .arg(QString::fromStdString(color_mode->second));
    }
  }
  if (!document.metadata().unknown_psd_resources.empty()) {
    warnings << QObject::tr("The document preserves %1 unknown PSD image resource(s).")
                     .arg(document.metadata().unknown_psd_resources.size());
  }
  for (const auto& layer : document.layers()) {
    append_layer_warnings(layer, warnings);
  }

  warnings.removeDuplicates();
  warnings.erase(std::remove_if(warnings.begin(), warnings.end(), [](const QString& warning) {
                   return warning.trimmed().isEmpty();
                 }),
                 warnings.end());
  return warnings;
}

void show_compatibility_report(QWidget* parent, const Document& document, const QString& source_name) {
  const auto warnings = compatibility_warnings_for_document(document);
  if (warnings.isEmpty()) {
    return;
  }

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyCompatibilityReportDialog"));
  dialog.setWindowTitle(QObject::tr("PSD Compatibility Report"));
  auto* root = new QVBoxLayout(&dialog);
  root->setContentsMargins(0, 0, 0, 0);
  auto* content = install_dark_dialog_chrome(
      dialog, root, QObject::tr("Compatibility: %1").arg(source_name.isEmpty() ? QObject::tr("document") : source_name));

  auto* summary = new QLabel(QObject::tr("Patchy preserved the editable data it understands and flagged areas that "
                                         "may differ from Photoshop or other PSD editors."),
                             &dialog);
  summary->setWordWrap(true);
  content->addWidget(summary);

  auto* details = new QPlainTextEdit(report_text(warnings), &dialog);
  details->setObjectName(QStringLiteral("compatibilityReportText"));
  details->setReadOnly(true);
  details->setMinimumSize(520, 220);
  content->addWidget(details, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  content->addWidget(buttons);
  exec_dialog(dialog);
}

}  // namespace patchy::ui
