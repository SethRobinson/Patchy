#include "ui/multipage_pdf_export_dialog.hpp"

#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_save_options_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {
namespace {

constexpr auto kSourceKey = "exportOptions/multiPagePdfSource";
constexpr auto kUngroupedKey = "exportOptions/multiPagePdfUngroupedLayers";
constexpr auto kHiddenGroupsKey = "exportOptions/multiPagePdfHiddenGroups";
constexpr auto kEditableKey = "exportOptions/multiPagePdfEditableLayers";
constexpr auto kMissingFontsKey = "saveOptions/pdfMissingFontsAsImages";
constexpr auto kKeepOriginalKey = "saveOptions/pdfKeepOriginalImages";
constexpr auto kSearchableTextKey = "saveOptions/pdfSearchableText";

}  // namespace

std::optional<MultiPagePdfExportChoice> run_pdf_options_dialog(QWidget* parent,
                                                              const PdfOptionsDialogContext& context) {
  auto settings = app_settings();
  const int top_level_group_count = context.top_level_group_count;
  const int hidden_group_count = context.hidden_group_count;
  const int artboard_count = context.artboard_count;
  const bool groups_available = top_level_group_count > 0;
  const bool artboards_available = artboard_count > 0;
  const bool documents_available = !context.documents.empty();
  const auto stored_source = settings.value(QLatin1String(kSourceKey), QStringLiteral("documents")).toString();

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("multiPagePdfExportDialog"));
  dialog.setWindowTitle(context.title.isEmpty() ? QObject::tr("Export Multi-Page PDF") : context.title);
  dialog.resize(520, documents_available ? 520 : 380);
  auto* layout = new QVBoxLayout(&dialog);
  if (!context.file_name.isEmpty()) {
    auto* file_label = new QLabel(context.file_name, &dialog);
    file_label->setObjectName(QStringLiteral("multiPagePdfFileLabel"));
    file_label->setTextFormat(Qt::PlainText);
    layout->addWidget(file_label);
  }

  auto* source_group = new QGroupBox(QObject::tr("Pages"), &dialog);
  auto* source_layout = new QVBoxLayout(source_group);

  auto* canvas_radio = new QRadioButton(QObject::tr("One page of the whole canvas"), source_group);
  canvas_radio->setObjectName(QStringLiteral("multiPagePdfCanvasRadio"));
  source_layout->addWidget(canvas_radio);
  auto* artboards_radio = new QRadioButton(
      artboards_available ? QObject::tr("One page per artboard of the current document (%n artboard(s))", nullptr,
                                        artboard_count)
                          : QObject::tr("One page per artboard (the current document has none)"),
      source_group);
  artboards_radio->setObjectName(QStringLiteral("multiPagePdfArtboardsRadio"));
  artboards_radio->setEnabled(artboards_available);
  source_layout->addWidget(artboards_radio);
  auto* groups_radio = new QRadioButton(
      groups_available ? QObject::tr("One page per top-level layer group of the current document (%n group(s))",
                                     nullptr, top_level_group_count)
                       : QObject::tr("One page per top-level layer group (the current document has none)"),
      source_group);
  groups_radio->setObjectName(QStringLiteral("multiPagePdfGroupsRadio"));
  groups_radio->setEnabled(groups_available);
  source_layout->addWidget(groups_radio);
  auto* ungrouped = new QCheckBox(QObject::tr("Draw ungrouped layers on every page"), source_group);
  ungrouped->setObjectName(QStringLiteral("multiPagePdfUngroupedCheck"));
  ungrouped->setChecked(settings.value(QLatin1String(kUngroupedKey), true).toBool());
  ungrouped->setVisible(groups_available);
  source_layout->addWidget(ungrouped);
  auto* hidden_groups = new QCheckBox(QObject::tr("Include hidden groups"), source_group);
  hidden_groups->setObjectName(QStringLiteral("multiPagePdfHiddenGroupsCheck"));
  hidden_groups->setChecked(settings.value(QLatin1String(kHiddenGroupsKey), true).toBool());
  hidden_groups->setToolTip(QObject::tr("A PDF imported as page folders shows only its first page; its hidden pages "
                                        "are still pages."));
  hidden_groups->setVisible(hidden_group_count > 0);
  source_layout->addWidget(hidden_groups);

  auto* documents_radio = new QRadioButton(QObject::tr("One page per open document"), source_group);
  documents_radio->setObjectName(QStringLiteral("multiPagePdfDocumentsRadio"));
  documents_radio->setVisible(documents_available);
  source_layout->addWidget(documents_radio);
  // The shared ordered checklist: Move Up/Down, Auto Sort (numbering-aware), Reverse.
  const auto order = build_document_order_controls(source_group, QStringLiteral("multiPagePdf"), context.documents,
                                                   context.active_session_id);
  auto* list = order.list;
  order.row->setVisible(documents_available);
  source_layout->addWidget(order.row);
  layout->addWidget(source_group);

  auto* options_group = new QGroupBox(QObject::tr("PDF Options"), &dialog);
  auto* options_layout = new QVBoxLayout(options_group);
  auto* editable = new QCheckBox(QObject::tr("Keep layers as editable objects"), options_group);
  editable->setObjectName(QStringLiteral("multiPagePdfEditableCheck"));
  // On by default: a PDF is a print or handoff document, and text and shapes that stay
  // editable are worth more there than pixel-exact blends.
  editable->setChecked(context.editable_default.value_or(settings.value(QLatin1String(kEditableKey), true).toBool()));
  editable->setVisible(context.layered);
  options_layout->addWidget(editable);
  auto* editable_note = new QLabel(
      QObject::tr("Shapes and text stay editable; blend modes, adjustment layers, and layer styles flatten to "
                  "images on each page."),
      options_group);
  editable_note->setObjectName(QStringLiteral("multiPagePdfEditableNote"));
  editable_note->setWordWrap(true);
  editable_note->setVisible(context.layered);
  options_layout->addWidget(editable_note);
  auto* missing_fonts = new QCheckBox(
      QObject::tr("When a font is missing, export that text as an image instead of substituting a font"),
      options_group);
  missing_fonts->setObjectName(QStringLiteral("multiPagePdfMissingFontsCheck"));
  missing_fonts->setToolTip(
      QObject::tr("Unchecked, text in a font that is not installed is written as editable text in a "
                  "substitute font, so it can look different from the canvas. Checked, that text is "
                  "written as an image of the layer's pixels instead."));
  missing_fonts->setChecked(settings.value(QLatin1String(kMissingFontsKey), false).toBool());
  missing_fonts->setVisible(context.layered);
  options_layout->addWidget(missing_fonts);
  // One image-quality preference for every PDF save.
  auto* quality_row = new QHBoxLayout();
  auto* quality_label = new QLabel(QObject::tr("Image quality:"), options_group);
  auto* quality = new QComboBox(options_group);
  quality->setObjectName(QStringLiteral("multiPagePdfImageQualityCombo"));
  populate_pdf_image_quality_combo(*quality, stored_pdf_image_quality_id());
  quality_label->setBuddy(quality);
  quality_row->addWidget(quality_label);
  quality_row->addWidget(quality, 1);
  options_layout->addLayout(quality_row);
  auto* keep_original = new QCheckBox(pdf_keep_original_images_label(), options_group);
  keep_original->setObjectName(QStringLiteral("multiPagePdfKeepOriginalCheck"));
  keep_original->setChecked(settings.value(QLatin1String(kKeepOriginalKey), true).toBool());
  keep_original->setToolTip(pdf_keep_original_images_tooltip());
  keep_original->setVisible(context.original_image_data_available);
  options_layout->addWidget(keep_original);
  auto* searchable_text = new QCheckBox(pdf_searchable_text_label(), options_group);
  searchable_text->setObjectName(QStringLiteral("multiPagePdfSearchableTextCheck"));
  searchable_text->setChecked(settings.value(QLatin1String(kSearchableTextKey), true).toBool());
  searchable_text->setToolTip(pdf_searchable_text_tooltip());
  searchable_text->setVisible(context.recognized_text_available);
  options_layout->addWidget(searchable_text);
  layout->addWidget(options_group);

  auto* summary = new QLabel(&dialog);
  summary->setObjectName(QStringLiteral("multiPagePdfSummaryLabel"));
  summary->setWordWrap(true);
  layout->addWidget(summary);
  layout->addStretch(1);

  auto* buttons = new QDialogButtonBox(&dialog);
  auto* export_button = buttons->addButton(
      context.accept_text.isEmpty() ? QObject::tr("Export") : context.accept_text, QDialogButtonBox::AcceptRole);
  export_button->setObjectName(QStringLiteral("multiPagePdfExportButton"));
  export_button->setDefault(true);
  buttons->addButton(QDialogButtonBox::Cancel);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);

  const auto sync = [&] {
    const bool by_groups = groups_radio->isChecked();
    const bool by_artboards = artboards_radio->isChecked();
    const bool by_documents = documents_radio->isChecked();
    sync_document_order_controls(order, by_documents);
    ungrouped->setEnabled(by_groups);
    hidden_groups->setEnabled(by_groups);
    editable_note->setEnabled(editable->isChecked());
    missing_fonts->setEnabled(editable->isChecked());
    const int page_count = by_groups      ? top_level_group_count - (hidden_groups->isChecked() ? 0 : hidden_group_count)
                           : by_artboards ? artboard_count
                           : by_documents ? static_cast<int>(selected_session_ids(*list).size())
                                          : 1;
    export_button->setEnabled(page_count > 0);
    summary->setText(page_count > 0 ? QObject::tr("%n page(s) will be written.", nullptr, page_count)
                                    : QObject::tr("Select at least one document."));
  };
  // Where to start: the caller's pick, else the remembered source when it is offered;
  // an artboard document opens on its artboards (that is what its pages are).
  auto start = context.default_source;
  if (!start.has_value()) {
    if (artboards_available && stored_source != QStringLiteral("groups") && stored_source != QStringLiteral("canvas")) {
      start = MultiPagePdfSource::Artboards;
    } else if (groups_available && stored_source == QStringLiteral("groups")) {
      start = MultiPagePdfSource::TopLevelGroups;
    } else if (documents_available && stored_source == QStringLiteral("documents")) {
      start = MultiPagePdfSource::OpenDocuments;
    } else {
      start = MultiPagePdfSource::Canvas;
    }
  }
  if ((*start == MultiPagePdfSource::Artboards && !artboards_available) ||
      (*start == MultiPagePdfSource::TopLevelGroups && !groups_available) ||
      (*start == MultiPagePdfSource::OpenDocuments && !documents_available)) {
    start = MultiPagePdfSource::Canvas;
  }
  (*start == MultiPagePdfSource::Artboards        ? artboards_radio
   : *start == MultiPagePdfSource::TopLevelGroups ? groups_radio
   : *start == MultiPagePdfSource::OpenDocuments  ? documents_radio
                                                  : canvas_radio)
      ->setChecked(true);
  sync();
  QObject::connect(canvas_radio, &QRadioButton::toggled, &dialog, sync);
  QObject::connect(documents_radio, &QRadioButton::toggled, &dialog, sync);
  QObject::connect(groups_radio, &QRadioButton::toggled, &dialog, sync);
  QObject::connect(artboards_radio, &QRadioButton::toggled, &dialog, sync);
  QObject::connect(list, &QListWidget::itemSelectionChanged, &dialog, sync);
  QObject::connect(list, &QListWidget::currentRowChanged, &dialog, sync);
  QObject::connect(order.select_all, &QPushButton::clicked, &dialog, sync);
  QObject::connect(editable, &QCheckBox::toggled, &dialog, sync);
  QObject::connect(hidden_groups, &QCheckBox::toggled, &dialog, sync);

  remember_dialog_position(dialog);
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }

  MultiPagePdfExportChoice choice;
  choice.source = groups_radio->isChecked()      ? MultiPagePdfSource::TopLevelGroups
                  : artboards_radio->isChecked() ? MultiPagePdfSource::Artboards
                  : documents_radio->isChecked() ? MultiPagePdfSource::OpenDocuments
                                                 : MultiPagePdfSource::Canvas;
  choice.session_ids = selected_session_ids(*list);
  choice.include_ungrouped_layers = ungrouped->isChecked();
  choice.include_hidden_groups = hidden_groups->isChecked();
  apply_pdf_image_quality(quality->currentData().toString(), choice.options);
  choice.options.editable_layers = context.layered && editable->isChecked();
  choice.options.missing_fonts_as_images = missing_fonts->isChecked();
  choice.options.pages = choice.source == MultiPagePdfSource::TopLevelGroups ? PdfPageSource::Groups
                         : choice.source == MultiPagePdfSource::Canvas       ? PdfPageSource::Canvas
                                                                             : PdfPageSource::Artboards;
  choice.options.include_ungrouped_layers = choice.include_ungrouped_layers;
  choice.options.include_hidden_groups = choice.include_hidden_groups;
  settings.setValue(QLatin1String(kSourceKey), choice.source == MultiPagePdfSource::TopLevelGroups ? QStringLiteral("groups")
                                                : choice.source == MultiPagePdfSource::Artboards
                                                    ? QStringLiteral("artboards")
                                                : choice.source == MultiPagePdfSource::Canvas
                                                    ? QStringLiteral("canvas")
                                                    : QStringLiteral("documents"));
  settings.setValue(QLatin1String(kUngroupedKey), choice.include_ungrouped_layers);
  settings.setValue(QLatin1String(kHiddenGroupsKey), choice.include_hidden_groups);
  if (context.layered) {
    settings.setValue(QLatin1String(kEditableKey), editable->isChecked());
    settings.setValue(QLatin1String(kMissingFontsKey), choice.options.missing_fonts_as_images);
  }
  store_pdf_image_quality_id(quality->currentData().toString());
  choice.options.keep_original_image_data = settings.value(QLatin1String(kKeepOriginalKey), true).toBool();
  if (context.original_image_data_available) {
    choice.options.keep_original_image_data = keep_original->isChecked();
    settings.setValue(QLatin1String(kKeepOriginalKey), choice.options.keep_original_image_data);
  }
  choice.options.searchable_text = settings.value(QLatin1String(kSearchableTextKey), true).toBool();
  if (context.recognized_text_available) {
    choice.options.searchable_text = searchable_text->isChecked();
    settings.setValue(QLatin1String(kSearchableTextKey), choice.options.searchable_text);
  }
  return choice;
}

void apply_pdf_choice_to_save_options(const MultiPagePdfExportChoice& choice, ImageSaveOptions& options) {
  options.pdf_lossless = choice.options.lossless;
  options.pdf_jpeg_quality = choice.options.jpeg_quality;
  options.pdf_editable_layers = choice.options.editable_layers;
  options.pdf_missing_fonts_as_images = choice.options.missing_fonts_as_images;
  options.pdf_keep_original_images = choice.options.keep_original_image_data;
  options.pdf_searchable_text = choice.options.searchable_text;
  options.pdf_pages = pdf_page_source_token(choice.options.pages);
  options.pdf_include_ungrouped_layers = choice.include_ungrouped_layers;
  options.pdf_include_hidden_groups = choice.include_hidden_groups;
  // PDF Options offers no pixel export transforms: a scale or trim remembered from another
  // format's export must not reach the page.
  options.export_scale = 1;
  options.export_trim_transparent = false;
  options.export_width = 0;
  options.export_height = 0;
  options.export_fill_transparent = false;
}

}  // namespace patchy::ui
