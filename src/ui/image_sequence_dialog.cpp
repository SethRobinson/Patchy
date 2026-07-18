#include "ui/image_sequence_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/image_document_io.hpp"

#include <QButtonGroup>
#include <QCollator>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QRadioButton>
#include <QSet>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace patchy::ui {

namespace {

QVBoxLayout* create_sequence_dialog_chrome(QDialog& dialog, const QString& title) {
  auto* root = new QVBoxLayout(&dialog);
  auto* content = install_dark_dialog_chrome(dialog, root, title);
  content->setSpacing(10);
  return content;
}

QDialogButtonBox* add_sequence_dialog_buttons(QVBoxLayout* content, QDialog& dialog) {
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  content->addWidget(buttons);
  return buttons;
}

// Length of the digit run ending the string (0 when it does not end in a digit).
int trailing_digit_count(const QString& text) {
  int index = text.size();
  while (index > 0 && text[index - 1].isDigit()) {
    --index;
  }
  return text.size() - index;
}

// Windows is the strictest filesystem: strip its illegal characters plus control
// codes, and the trailing dots/spaces it rejects.
QString sanitized_file_name(const QString& name) {
  static const QString illegal = QStringLiteral("\\/:*?\"<>|");
  QString cleaned;
  cleaned.reserve(name.size());
  for (const auto ch : name) {
    cleaned.append(illegal.contains(ch) || ch.unicode() < 0x20 ? QChar(QLatin1Char('_')) : ch);
  }
  while (!cleaned.isEmpty() && (cleaned.back() == QLatin1Char('.') || cleaned.back() == QLatin1Char(' '))) {
    cleaned.chop(1);
  }
  return cleaned.trimmed();
}

}  // namespace

QStringList sorted_sequence_paths(QStringList paths) {
  QCollator collator;
  collator.setNumericMode(true);
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  std::sort(paths.begin(), paths.end(), [&collator](const QString& a, const QString& b) {
    const auto compared = collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName());
    if (compared != 0) {
      return compared < 0;
    }
    return a < b;
  });
  return paths;
}

QStringList expand_numbered_sequence(const QString& path) {
  const QFileInfo info(path);
  const auto base = info.completeBaseName();
  const auto digits = trailing_digit_count(base);
  if (digits == 0) {
    return {path};
  }
  const auto prefix = base.left(base.size() - digits);
  const auto extension = info.suffix();
  QStringList run;
  const auto entries = info.dir().entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
  for (const auto& entry : entries) {
    const auto entry_base = entry.completeBaseName();
    if (entry.suffix().compare(extension, Qt::CaseInsensitive) != 0) {
      continue;
    }
    if (!entry_base.startsWith(prefix, Qt::CaseInsensitive)) {
      continue;
    }
    const auto remainder = entry_base.mid(prefix.size());
    if (remainder.isEmpty() || trailing_digit_count(remainder) != remainder.size()) {
      continue;
    }
    run.push_back(entry.absoluteFilePath());
  }
  if (run.isEmpty()) {
    return {path};  // the directory listing failed to see even the selected file
  }
  return sorted_sequence_paths(std::move(run));
}

std::optional<Document> document_from_image_sequence(const QStringList& paths, QString* error) {
  if (paths.isEmpty()) {
    return std::nullopt;
  }
  std::vector<QImage> frames;
  frames.reserve(static_cast<std::size_t>(paths.size()));
  int canvas_width = 0;
  int canvas_height = 0;
  for (const auto& path : paths) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    auto frame = reader.read().convertToFormat(QImage::Format_RGBA8888);
    if (frame.isNull()) {
      if (error != nullptr) {
        *error = QObject::tr("%1: %2").arg(QFileInfo(path).fileName(), reader.errorString());
      }
      return std::nullopt;
    }
    canvas_width = std::max(canvas_width, frame.width());
    canvas_height = std::max(canvas_height, frame.height());
    frames.push_back(std::move(frame));
  }
  Document document(canvas_width, canvas_height, PixelFormat::rgba8());
  for (std::size_t index = 0; index < frames.size(); ++index) {
    auto frame = std::move(frames[index]);
    if (frame.size() != QSize(canvas_width, canvas_height)) {
      // Smaller frames top-left align on the shared canvas (registration stays at the
      // (0,0) origin, matching Photoshop's Load Files into Stack).
      QImage padded(canvas_width, canvas_height, QImage::Format_RGBA8888);
      padded.fill(Qt::transparent);
      QPainter painter(&padded);
      painter.drawImage(0, 0, frame);
      painter.end();
      frame = std::move(padded);
    }
    Layer layer(document.allocate_layer_id(),
                QFileInfo(paths[static_cast<int>(index)]).completeBaseName().toStdString(),
                pixels_from_image_rgba(frame));
    layer.set_visible(index == 0);
    document.add_layer(std::move(layer));
  }
  return document;
}

ImageSequenceNaming naming_from_save_base_name(const QString& base_name) {
  ImageSequenceNaming naming;
  const auto digits = trailing_digit_count(base_name);
  if (digits > 0) {
    naming.prefix = base_name.left(base_name.size() - digits);
    naming.padding = digits;
    bool parsed = false;
    naming.start = base_name.right(digits).toInt(&parsed);
    if (!parsed) {  // a digit run too long for int; fall back to 1
      naming.start = 1;
    }
  } else {
    naming.prefix = base_name + QStringLiteral("_");
    naming.start = 1;
    naming.padding = 3;
  }
  return naming;
}

QStringList image_sequence_file_names(const std::vector<QString>& layer_names,
                                      const ImageSequenceNaming& naming, const QString& extension) {
  QStringList names;
  const auto suffix = extension.isEmpty() ? QString() : QStringLiteral(".") + extension;
  if (!naming.use_layer_names) {
    for (std::size_t index = 0; index < layer_names.size(); ++index) {
      const auto number = naming.start + static_cast<int>(index);
      names.push_back(naming.prefix +
                      QStringLiteral("%1").arg(number, std::max(1, naming.padding), 10, QLatin1Char('0')) + suffix);
    }
    return names;
  }
  QSet<QString> used;  // lowercased: Windows file names are case-insensitive
  for (std::size_t index = 0; index < layer_names.size(); ++index) {
    auto base = sanitized_file_name(layer_names[index]);
    if (base.isEmpty()) {
      base = QObject::tr("Frame %1").arg(index + 1);
    }
    auto candidate = base;
    for (int copy = 2; used.contains(candidate.toLower()); ++copy) {
      candidate = base + QStringLiteral(" %1").arg(copy);
    }
    used.insert(candidate.toLower());
    names.push_back(candidate + suffix);
  }
  return names;
}

bool prompt_image_sequence_import_options(QWidget* parent, const QStringList& ordered_paths, QSize canvas_size) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("imageSequenceImportDialog"));
  auto* content = create_sequence_dialog_chrome(dialog, QObject::tr("Image Sequence to Layers"));
  dialog.resize(360, 320);

  auto* count_label = new QLabel(&dialog);
  count_label->setObjectName(QStringLiteral("imageSequenceCountLabel"));
  count_label->setWordWrap(true);
  count_label->setText(canvas_size.isValid()
                           ? QObject::tr("%1 images will import as layers on a %2 x %3 px canvas, in this order:")
                                 .arg(ordered_paths.size())
                                 .arg(canvas_size.width())
                                 .arg(canvas_size.height())
                           : QObject::tr("%1 images will import as layers, in this order:").arg(ordered_paths.size()));
  content->addWidget(count_label);

  auto* file_list = new QListWidget(&dialog);
  file_list->setObjectName(QStringLiteral("imageSequenceFileList"));
  file_list->setSelectionMode(QAbstractItemView::NoSelection);
  file_list->setFocusPolicy(Qt::NoFocus);
  for (const auto& path : ordered_paths) {
    file_list->addItem(QFileInfo(path).fileName());
  }
  content->addWidget(file_list, 1);

  add_sequence_dialog_buttons(content, dialog);
  return exec_dialog(dialog) == QDialog::Accepted;
}

std::optional<ImageSequenceExportOptions> prompt_image_sequence_export_options(
    QWidget* parent, const std::vector<QString>& visible_layer_names, const std::vector<QString>& all_layer_names,
    const ImageSequenceNaming& suggested, const QString& extension) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("imageSequenceExportDialog"));
  auto* content = create_sequence_dialog_chrome(dialog, QObject::tr("Export Image Sequence"));
  dialog.resize(360, 260);

  auto* form = new QFormLayout();
  form->setContentsMargins(0, 0, 0, 0);
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(8);

  auto* info = new QLabel(&dialog);
  info->setObjectName(QStringLiteral("imageSequenceInfoLabel"));
  info->setWordWrap(true);
  form->addRow(info);

  // Two independent radio pairs share the dialog, so each needs its own group
  // (same-parent QRadioButtons are otherwise one exclusive set).
  auto* visible_only = new QRadioButton(QObject::tr("Export visible layers only"), &dialog);
  visible_only->setObjectName(QStringLiteral("imageSequenceVisibleLayersRadio"));
  auto* all_layers = new QRadioButton(QObject::tr("Export all layers"), &dialog);
  all_layers->setObjectName(QStringLiteral("imageSequenceAllLayersRadio"));
  auto* scope_group = new QButtonGroup(&dialog);
  scope_group->addButton(visible_only);
  scope_group->addButton(all_layers);
  if (visible_layer_names.empty()) {
    visible_only->setEnabled(false);
    all_layers->setChecked(true);
  } else {
    visible_only->setChecked(true);
  }
  form->addRow(visible_only);
  form->addRow(all_layers);

  auto* numbered = new QRadioButton(QObject::tr("Numbered files"), &dialog);
  numbered->setObjectName(QStringLiteral("imageSequenceNumberedRadio"));
  auto* start = new QSpinBox(&dialog);
  start->setObjectName(QStringLiteral("imageSequenceStartSpin"));
  start->setRange(0, 999999);
  start->setValue(suggested.start);
  configure_dialog_spinbox(start, 88);
  form->addRow(numbered, start);

  auto* layer_named = new QRadioButton(QObject::tr("Layer names"), &dialog);
  layer_named->setObjectName(QStringLiteral("imageSequenceLayerNamesRadio"));
  auto* naming_group = new QButtonGroup(&dialog);
  naming_group->addButton(numbered);
  naming_group->addButton(layer_named);
  numbered->setChecked(true);
  form->addRow(layer_named);

  auto* preview = new QLabel(&dialog);
  preview->setObjectName(QStringLiteral("imageSequencePreviewLabel"));
  preview->setWordWrap(true);
  form->addRow(preview);
  content->addLayout(form);
  add_sequence_dialog_buttons(content, dialog);

  const auto current_options = [&]() {
    ImageSequenceExportOptions options;
    options.naming = suggested;
    options.naming.use_layer_names = layer_named->isChecked();
    options.naming.start = start->value();
    options.visible_layers_only = visible_only->isChecked();
    return options;
  };
  const auto update_preview = [=, &visible_layer_names, &all_layer_names, &extension] {
    start->setEnabled(numbered->isChecked());
    const auto options = current_options();
    const auto& layer_names = options.visible_layers_only ? visible_layer_names : all_layer_names;
    info->setText(options.visible_layers_only
                      ? QObject::tr("%1 images, one per visible top-level layer").arg(layer_names.size())
                      : QObject::tr("%1 images, one per top-level layer").arg(layer_names.size()));
    const auto names = image_sequence_file_names(layer_names, options.naming, extension);
    if (names.isEmpty()) {
      preview->clear();
    } else if (names.size() == 1) {
      preview->setText(names.first());
    } else {
      preview->setText(QObject::tr("%1 ... %2").arg(names.first(), names.last()));
    }
  };
  QObject::connect(visible_only, &QRadioButton::toggled, &dialog, [update_preview](bool) { update_preview(); });
  QObject::connect(numbered, &QRadioButton::toggled, &dialog, [update_preview](bool) { update_preview(); });
  QObject::connect(start, &QSpinBox::valueChanged, &dialog, [update_preview](int) { update_preview(); });
  update_preview();

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return current_options();
}

}  // namespace patchy::ui
