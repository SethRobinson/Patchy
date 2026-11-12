#include "ui/compatibility_report.hpp"

#include "core/adjustment_layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"

#include <QCheckBox>
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
  // The reader's own list of understood keys (psd::is_understood_layer_block_key), so the
  // report agrees with what the reader models or keeps as routine Photoshop data.
  if (block.key == "iOpa") {
    return block.payload.size() == 4U;
  }
  return psd::is_understood_layer_block_key(block.key);
}

QString smart_object_layer_warning(const Layer& layer) {
  const auto lock = smart_object_lock_reason(layer);
  const auto name = QString::fromStdString(layer.name());
  if (lock.empty()) {
    return {};
  }
  if (lock == "external") {
    return QObject::tr("Layer '%1' is a smart object linked to an external file; Patchy preserves it and can update it "
                       "from disk when the source file is available.")
        .arg(name);
  }
  if (lock == "filters") {
    return QObject::tr("Layer '%1' is a smart object with Smart Filters; Patchy preserves them and shows Photoshop's "
                       "preview (rasterize the layer to edit it here).")
        .arg(name);
  }
  if (lock == "warp" || lock == "non_affine") {
    return QObject::tr("Layer '%1' is a smart object with a warp or perspective transform; Patchy preserves it and "
                       "shows Photoshop's preview (rasterize the layer to edit it here).")
        .arg(name);
  }
  return QObject::tr("Layer '%1' is a smart object Patchy can only preserve, not edit (%2).")
      .arg(name, QString::fromStdString(lock));
}

int unknown_layer_block_count(const Layer& layer) {
  return static_cast<int>(
      std::count_if(layer.unknown_psd_blocks().begin(), layer.unknown_psd_blocks().end(),
                    [](const UnknownPsdBlock& block) { return !known_round_trip_layer_block(block); }));
}

void append_unrendered_style_warnings(const Layer& layer, QStringList& warnings) {
  const auto& style = layer.layer_style();
  const auto has_unsupported_satin_contour =
      std::any_of(style.satins.begin(), style.satins.end(), [](const LayerSatin& satin) {
        return satin.unsupported_contour_options;
      });
  if (has_unsupported_satin_contour) {
    warnings << QObject::tr("Layer '%1' contains Photoshop Satin contour settings that Patchy cannot render or edit "
                            "(a custom curve or anti-aliasing). Patchy preserves them until layer styles are edited, "
                            "then uses the non-anti-aliased Linear contour.")
                    .arg(QString::fromStdString(layer.name()));
  }

}

// What the layer walk needs from the document, and what it counts for the document-level lines.
struct LayerWarningContext {
  bool native_color_candidate{false};
  BitDepth document_depth{BitDepth::UInt8};
  int uneditable_pixel_layers{0};
};

// Every line about one layer starts "Layer 'Name'", so the name cannot read as part of the
// sentence (GitHub issue 712).
void append_layer_warnings(const Layer& layer, QStringList& warnings, LayerWarningContext& context) {
  append_unrendered_style_warnings(layer, warnings);
  if (!layer.raw_psd_blending_ranges().empty() &&
      layer.blend_if_payload_status() == BlendIfPayloadStatus::Unsupported) {
    warnings << QObject::tr("Layer '%1' contains Photoshop Blend If data for an unsupported color mode or payload "
                            "shape. Patchy preserves it for PSD round-trip but does not render or edit it.")
                    .arg(QString::fromStdString(layer.name()));
  }
  if (blend_if_payload_has_non_identity_or_unsupported(layer.raw_psd_group_boundary_blending_ranges())) {
    warnings << QObject::tr("Layer '%1' contains Blend If data on a Photoshop group-boundary record. Patchy preserves "
                            "that boundary data but does not render or edit it.")
                    .arg(QString::fromStdString(layer.name()));
  }
  if (!layer.channel_restriction_supported()) {
    warnings << QObject::tr("Layer '%1' contains Photoshop channel blending restrictions for an unsupported color "
                            "mode or payload shape. Patchy preserves them for PSD round-trip but does not "
                            "render or edit them.")
                    .arg(QString::fromStdString(layer.name()));
  }

  if (layer.kind() == LayerKind::Group) {
    const auto unknown_blocks = unknown_layer_block_count(layer);
    if (unknown_blocks > 0) {
      warnings << QObject::tr("Layer '%1' preserves %2 unknown PSD layer block(s).")
                       .arg(QString::fromStdString(layer.name()))
                       .arg(unknown_blocks);
    }
    for (const auto& child : layer.children()) {
      append_layer_warnings(child, warnings, context);
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
      warnings << QObject::tr("Layer '%1': extracted editable PSD text from %2, but Patchy generated a placeholder "
                              "raster preview because the PSD text pixels were not visible.")
                       .arg(QString::fromStdString(layer.name()), source_block.isEmpty() ? QObject::tr("text data")
                                                                                         : source_block);
    }
    // Text Patchy reads and lays out (its own PSDs and ordinary Photoshop type) adds no line:
    // showing Photoshop's raster until an edit is how every editor opens PSD type (issue 646).
  }
  if (layer_is_smart_object(layer)) {
    if (const auto warning = smart_object_layer_warning(layer); !warning.isEmpty()) {
      warnings << warning;
    }
  }
  if (layer.kind() == LayerKind::Adjustment) {
    const auto settings = adjustment_settings_from_layer(layer);
    // Every modeled adjustment kind writes a native Photoshop block (levl /
    // curv / hue2 / blnc / nvrt / post / thrs / brit); only an adjustment
    // layer whose settings cannot be parsed stays Patchy-opaque.
    if (!settings.has_value()) {
      warnings << QObject::tr("Layer '%1' is a Patchy-native adjustment layer; it round-trips in Patchy PSDs but may "
                              "appear as an unsupported adjustment in other editors.")
                       .arg(QString::fromStdString(layer.name()));
    } else if (!context.native_color_candidate && adjustment_depends_on_source_color_mode(*settings)) {
      // The layer came from a CMYK document and is evaluated on its inks (InkSpace).
      // An RGB save cannot carry that meaning.
      warnings << (settings->ink_space->is_gray()
                       ? QObject::tr("Layer '%1' came from a grayscale document and adjusts its gray channel here. "
                                     "Patchy saves RGB files, so in the saved file Photoshop and Patchy apply "
                                     "it to RGB and its tones will look different.")
                       : QObject::tr("Layer '%1' came from a CMYK document and adjusts its CMYK inks here. Patchy "
                                     "saves RGB files, so in the saved file Photoshop and Patchy apply it to "
                                     "RGB and its colors will look different."))
                      .arg(QString::fromStdString(layer.name()));
    }
  } else if (layer.kind() != LayerKind::Pixel && layer.kind() != LayerKind::Text) {
    warnings << QObject::tr("Layer '%1' uses an unsupported layer kind and may not export as editable PSD data.")
                     .arg(QString::fromStdString(layer.name()));
  }

  // A pixel layer is editable at the document's own depth, 16 and 32 bits included, so a deep
  // file's layers add nothing here (GitHub issue 712). Only a layer that breaks the document's
  // format (another depth, or fewer than three channels) counts, and those share one line.
  if (layer.kind() == LayerKind::Pixel && !layer.pixels().empty() &&
      (layer.pixels().format().bit_depth != context.document_depth || layer.pixels().format().channels < 3)) {
    ++context.uneditable_pixel_layers;
  }
  const auto unknown_blocks = unknown_layer_block_count(layer);
  if (unknown_blocks > 0) {
    warnings << QObject::tr("Layer '%1' preserves %2 unknown PSD layer block(s).")
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
  const bool native_color_candidate = document.metadata().psd_native_color_space != nullptr;
  const auto color_mode = document.metadata().values.find("psd.color_mode");
  if (native_color_candidate) {
    warnings << QObject::tr("Patchy edits this file in RGB. Unchanged supported layers can be saved in the original "
                            "color mode. Content edits or unsupported layers require RGB saving, which can change gradients and adjustments.");
  } else if (color_mode != document.metadata().values.end() && color_mode->second != "RGB") {
    if (color_mode->second == "CMYK") {
      warnings << QObject::tr("The source color mode is CMYK; Patchy converted the pixels to RGB/RGBA for editing "
                              "and will export RGB PSD data from this document.");
    } else if (color_mode->second == "Grayscale") {
      warnings << QObject::tr("The source color mode is Grayscale; Patchy converted the gray values to RGB/RGBA "
                              "for editing and will export RGB PSD data from this document.");
    } else {
      warnings << QObject::tr("The source color mode is %1; Patchy currently edits through RGB/RGBA workflows.")
                       .arg(QString::fromStdString(color_mode->second));
    }
  }
  if (const auto depth = document.metadata().values.find("psd.depth");
      depth != document.metadata().values.end() && depth->second != "8" &&
      document.color_state().bit_depth == BitDepth::UInt8) {
    warnings << QObject::tr("The source is %1 bits per channel; Patchy converted it to 8-bit for editing and "
                            "saves an 8-bit file. Keep the original if you need the deeper data.")
                    .arg(QString::fromStdString(depth->second));
  }
  // Unknown image resources and embedded smart object sources round-trip unchanged and ask
  // nothing of the user, so the report does not list them (GitHub issue 669).
  LayerWarningContext context;
  context.native_color_candidate = native_color_candidate;
  context.document_depth = document.color_state().bit_depth;
  const auto first_layer_line = warnings.size();
  for (const auto& layer : document.layers()) {
    append_layer_warnings(layer, warnings, context);
  }
  if (context.uneditable_pixel_layers > 0) {
    warnings.insert(first_layer_line,
                    QObject::tr("%1 layer(s) use a pixel format that can render but is not fully editable in "
                                "this build.")
                        .arg(context.uneditable_pixel_layers));
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

  // Turns off the Preferences option that opens this report and the Import Notes popup
  // (GitHub issue 559: the report gave no way to stop it).
  auto* dont_show = new QCheckBox(QObject::tr("Don't show this again"), &dialog);
  dont_show->setObjectName(QStringLiteral("compatibilityReportDontShowCheck"));
  dont_show->setToolTip(QObject::tr("Import warnings and notes go to the status bar from now on. Turn the popup "
                                    "back on in Preferences > Application."));
  content->addWidget(dont_show);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  content->addWidget(buttons);
  exec_dialog(dialog);
  if (dont_show->isChecked()) {
    // Persisted settings key shared with Preferences (never rename).
    app_settings().setValue(QStringLiteral("imports/showPsdWarningsAndInfo"), false);
  }
}

}  // namespace patchy::ui
