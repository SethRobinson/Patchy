#include "ui/file_info_dialog.hpp"
#include "core/invisible_watermark.hpp"
#include "ai/trustmark_watermark.hpp"

#include "formats/metadata/iptc_iim.hpp"
#include "formats/metadata/metadata_edit.hpp"
#include "formats/metadata/metadata_view.hpp"
#include "render/compositor.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/object_selection_model_download.hpp"
#include "ui/save_metadata_settings.hpp"
#include "ui/watermark_model_download.hpp"
#include "ui/watermark_scan_report.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>

namespace patchy::ui {
namespace {

namespace md = patchy::metadata;

QString utf8(const std::string& text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string to_utf8(const QString& text) {
  const auto bytes = text.toUtf8();
  return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

// "a; b ; c" -> {"a", "b", "c"}. Keywords also split on commas (Photoshop accepts
// both); author names keep their commas ("Smith, Ann").
std::vector<std::string> split_list(const QString& text, bool commas) {
  QString normalized = text;
  if (commas) {
    normalized.replace(QLatin1Char(','), QLatin1Char(';'));
  }
  std::vector<std::string> values;
  for (const auto& part : normalized.split(QLatin1Char(';'))) {
    const auto trimmed = part.trimmed();
    if (!trimmed.isEmpty()) {
      values.push_back(to_utf8(trimmed));
    }
  }
  return values;
}

QString join_list(const std::vector<std::string>& values) {
  QStringList parts;
  for (const auto& value : values) {
    parts.push_back(utf8(value));
  }
  return parts.join(QStringLiteral("; "));
}

QString segment_display_name(const std::string& name) {
  // PSD resources are recorded as "8BIM <id>"; add the resource's name.
  if (name.starts_with("8BIM ")) {
    const auto id = static_cast<std::uint16_t>(std::strtoul(name.c_str() + 5, nullptr, 10));
    return utf8(name + "  " + md::psd_resource_name(id));
  }
  return utf8(name);
}

QLabel* value_label(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return label;
}

QPlainTextEdit* read_only_text(const QString& text, QWidget* parent, int max_height = 120) {
  auto* edit = new QPlainTextEdit(text, parent);
  edit->setReadOnly(true);
  edit->setMaximumHeight(max_height);
  return edit;
}

// The privacy bar's insets: the text sits kPrivacyBannerTextInset from the left edge, and the
// Don't Show Again button, centered vertically, sits as far from the right edge as from the
// top and bottom edges (never less than kPrivacyBannerMinimumInset, never more than the text
// inset plus a few pixels, so a wrap change cannot oscillate). Without the button the right
// inset matches the left.
constexpr int kPrivacyBannerTextInset = 9;
constexpr int kPrivacyBannerMinimumInset = 7;
constexpr int kPrivacyBannerMaximumInset = 14;

class PrivacyBannerInset : public QObject {
public:
  PrivacyBannerInset(QObject* parent, QHBoxLayout* layout, QWidget* button)
      : QObject(parent), layout_(layout), button_(button) {}

  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::LayoutRequest) {
      apply(static_cast<QWidget*>(watched));
    }
    return QObject::eventFilter(watched, event);
  }

private:
  void apply(const QWidget* bar) {
    int right = kPrivacyBannerTextInset;
    if (!button_->isHidden()) {
      const int vertical = (bar->height() - button_->sizeHint().height()) / 2;
      right = std::clamp(vertical, kPrivacyBannerMinimumInset, kPrivacyBannerMaximumInset);
    }
    auto margins = layout_->contentsMargins();
    if (margins.right() != right) {
      margins.setRight(right);
      layout_->setContentsMargins(margins);
    }
  }

  QHBoxLayout* layout_;
  QWidget* button_;
};

// A titled group of read-only rows; rows with empty values are skipped.
class RowsBuilder {
public:
  explicit RowsBuilder(QVBoxLayout* layout, QWidget* parent) : layout_(layout), parent_(parent) {}

  // Shown only once something follows it, so empty sections leave no stray title.
  void heading(const QString& text) {
    pending_heading_ = text;
    form_ = nullptr;
  }

  void row(const QString& label, const QString& value) {
    if (value.isEmpty()) {
      return;
    }
    flush_heading();
    ensure_form();
    form_->addRow(label, value_label(value, parent_));
    ++rows_;
  }

  void row(const QString& label, const std::string& value) { row(label, utf8(value)); }

  void widget(QWidget* widget) {
    flush_heading();
    // Buttons keep their natural width; views fill the page.
    layout_->addWidget(widget, 0, qobject_cast<QPushButton*>(widget) != nullptr ? Qt::AlignLeft : Qt::Alignment{});
    form_ = nullptr;
  }

  void note(const QString& text) {
    flush_heading();
    auto* label = value_label(text, parent_);
    label->setObjectName(QStringLiteral("fileInfoNote"));
    layout_->addWidget(label);
    form_ = nullptr;
  }

  [[nodiscard]] int rows() const noexcept { return rows_; }

private:
  void flush_heading() {
    if (pending_heading_.isEmpty()) {
      return;
    }
    auto* label = new QLabel(pending_heading_, parent_);
    label->setObjectName(QStringLiteral("fileInfoSectionHeading"));
    layout_->addWidget(label);
    pending_heading_.clear();
    form_ = nullptr;
  }

  void ensure_form() {
    if (form_ == nullptr) {
      form_ = new QFormLayout();
      form_->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
      form_->setHorizontalSpacing(14);
      form_->setVerticalSpacing(6);
      layout_->addLayout(form_);
    }
  }

  QVBoxLayout* layout_;
  QWidget* parent_;
  QFormLayout* form_{nullptr};
  QString pending_heading_;
  int rows_{0};
};

QTreeWidget* make_tree(QWidget* parent, const QStringList& headers, const QString& object_name) {
  auto* tree = new QTreeWidget(parent);
  tree->setObjectName(object_name);
  tree->setHeaderLabels(headers);
  tree->setRootIsDecorated(false);
  tree->setUniformRowHeights(true);
  tree->setAlternatingRowColors(false);
  tree->header()->setStretchLastSection(true);
  tree->header()->setSectionResizeMode(QHeaderView::Interactive);
  return tree;
}

struct Editors {
  QLineEdit* title{nullptr};
  QLineEdit* authors{nullptr};
  QLineEdit* author_title{nullptr};
  QPlainTextEdit* description{nullptr};
  QComboBox* rating{nullptr};
  QLineEdit* description_writer{nullptr};
  QLineEdit* keywords{nullptr};
  QComboBox* copyright_status{nullptr};
  QPlainTextEdit* copyright_notice{nullptr};
  QLineEdit* copyright_url{nullptr};
  QLabel* created{nullptr};
  QLabel* modified{nullptr};
  QLabel* creator_tool{nullptr};
  QLabel* format{nullptr};
  QLineEdit* headline{nullptr};
  QLineEdit* date_created{nullptr};
  QLineEdit* sublocation{nullptr};
  QLineEdit* city{nullptr};
  QLineEdit* state{nullptr};
  QLineEdit* country{nullptr};
  QLineEdit* country_code{nullptr};
  QLineEdit* credit{nullptr};
  QLineEdit* source{nullptr};
  QPlainTextEdit* instructions{nullptr};
  QLineEdit* transmission_reference{nullptr};
  QLineEdit* usage_terms{nullptr};
  QLineEdit* intellectual_genre{nullptr};
  QLineEdit* scene{nullptr};
  QLineEdit* subject_code{nullptr};
  QLineEdit* contact_address{nullptr};
  QLineEdit* contact_city{nullptr};
  QLineEdit* contact_region{nullptr};
  QLineEdit* contact_postal_code{nullptr};
  QLineEdit* contact_country{nullptr};
  QLineEdit* contact_phone{nullptr};
  QLineEdit* contact_email{nullptr};
  QLineEdit* contact_url{nullptr};

  void load(const md::FileInfoFields& fields) const {
    title->setText(utf8(fields.title));
    authors->setText(join_list(fields.authors));
    author_title->setText(utf8(fields.author_title));
    description->setPlainText(utf8(fields.description));
    rating->setCurrentIndex(fields.rating < 0 ? 6 : std::clamp(fields.rating, 0, 5));
    description_writer->setText(utf8(fields.description_writer));
    keywords->setText(join_list(fields.keywords));
    copyright_status->setCurrentIndex(static_cast<int>(fields.copyright_status));
    copyright_notice->setPlainText(utf8(fields.copyright_notice));
    copyright_url->setText(utf8(fields.copyright_url));
    created->setText(utf8(fields.created));
    modified->setText(utf8(fields.modified));
    creator_tool->setText(utf8(fields.creator_tool));
    headline->setText(utf8(fields.headline));
    date_created->setText(utf8(fields.date_created));
    sublocation->setText(utf8(fields.sublocation));
    city->setText(utf8(fields.city));
    state->setText(utf8(fields.state));
    country->setText(utf8(fields.country));
    country_code->setText(utf8(fields.country_code));
    credit->setText(utf8(fields.credit));
    source->setText(utf8(fields.source));
    instructions->setPlainText(utf8(fields.instructions));
    transmission_reference->setText(utf8(fields.transmission_reference));
    usage_terms->setText(utf8(fields.usage_terms));
    intellectual_genre->setText(utf8(fields.intellectual_genre));
    scene->setText(join_list(fields.scene));
    subject_code->setText(join_list(fields.subject_code));
    contact_address->setText(utf8(fields.contact_address));
    contact_city->setText(utf8(fields.contact_city));
    contact_region->setText(utf8(fields.contact_region));
    contact_postal_code->setText(utf8(fields.contact_postal_code));
    contact_country->setText(utf8(fields.contact_country));
    contact_phone->setText(utf8(fields.contact_phone));
    contact_email->setText(utf8(fields.contact_email));
    contact_url->setText(utf8(fields.contact_url));
  }

  [[nodiscard]] md::FileInfoFields read(md::FileInfoFields fields) const {
    const auto text = [](const QLineEdit* edit) { return to_utf8(edit->text().trimmed()); };
    const auto plain = [](const QPlainTextEdit* edit) { return to_utf8(edit->toPlainText().trimmed()); };
    fields.title = text(title);
    fields.authors = split_list(authors->text(), false);
    fields.author_title = text(author_title);
    fields.description = plain(description);
    fields.rating = rating->currentIndex() == 6 ? -1 : rating->currentIndex();
    fields.description_writer = text(description_writer);
    fields.keywords = split_list(keywords->text(), true);
    fields.copyright_status = static_cast<md::CopyrightStatus>(copyright_status->currentIndex());
    fields.copyright_notice = plain(copyright_notice);
    fields.copyright_url = text(copyright_url);
    fields.headline = text(headline);
    fields.date_created = text(date_created);
    fields.sublocation = text(sublocation);
    fields.city = text(city);
    fields.state = text(state);
    fields.country = text(country);
    fields.country_code = text(country_code);
    fields.credit = text(credit);
    fields.source = text(source);
    fields.instructions = plain(instructions);
    fields.transmission_reference = text(transmission_reference);
    fields.usage_terms = text(usage_terms);
    fields.intellectual_genre = text(intellectual_genre);
    fields.scene = split_list(scene->text(), true);
    fields.subject_code = split_list(subject_code->text(), true);
    fields.contact_address = text(contact_address);
    fields.contact_city = text(contact_city);
    fields.contact_region = text(contact_region);
    fields.contact_postal_code = text(contact_postal_code);
    fields.contact_country = text(contact_country);
    fields.contact_phone = text(contact_phone);
    fields.contact_email = text(contact_email);
    fields.contact_url = text(contact_url);
    return fields;
  }
};

QLineEdit* add_line(QFormLayout* form, const QString& label, const char* object_name, QWidget* parent,
                    const QString& placeholder = QString()) {
  auto* edit = new QLineEdit(parent);
  edit->setObjectName(QString::fromLatin1(object_name));
  if (!placeholder.isEmpty()) {
    edit->setPlaceholderText(placeholder);
  }
  form->addRow(label, edit);
  return edit;
}

QPlainTextEdit* add_text(QFormLayout* form, const QString& label, const char* object_name, QWidget* parent,
                         int height) {
  auto* edit = new QPlainTextEdit(parent);
  edit->setObjectName(QString::fromLatin1(object_name));
  edit->setTabChangesFocus(true);
  edit->setFixedHeight(height);
  form->addRow(label, edit);
  return edit;
}

QScrollArea* scroll_page(QWidget* content) {
  auto* scroll = new QScrollArea();
  scroll->setProperty("fileInfoPage", true);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  if (content != nullptr) {
    scroll->setWidget(content);
  }
  return scroll;
}

// Replaces a rebuilt page. The old content goes through deleteLater: the rebuild can
// run from a click on a button inside it (Remove Location), which must outlive its
// own signal.
void replace_page_content(QScrollArea* page, QWidget* content) {
  if (auto* old = page->takeWidget()) {
    old->hide();
    old->deleteLater();
  }
  page->setWidget(content);
}

// `pixel_watermarks` names what Scan for Watermarks found; stripping cannot remove those.
QString privacy_summary_text(const std::map<md::PrivacyClass, std::size_t>& counts,
                             const QString& pixel_watermarks) {
  QStringList parts;
  for (const auto& [privacy, count] : counts) {
    if (privacy == md::PrivacyClass::Timestamp || privacy == md::PrivacyClass::None) {
      continue;  // nearly every file has dates; listing them would hide the real findings
    }
    parts.push_back(privacy_class_label(privacy));
  }
  QString text;
  if (!parts.isEmpty()) {
    text = QObject::tr("This document carries information you may not want to share: %1. Strip All Metadata "
                       "removes it.")
               .arg(parts.join(QStringLiteral(", ")));
  }
  if (!pixel_watermarks.isEmpty()) {
    if (!text.isEmpty()) {
      text += QLatin1Char(' ');
    }
    text += QObject::tr("The pixels carry an invisible watermark (%1). Stripping metadata does not remove it.")
                .arg(pixel_watermarks);
  }
  return text;
}

// All Metadata's untranslated value for a TrustMark hit.
std::string trustmark_mark_identifiers(const patchy::ai::TrustMarkMark& mark) {
  std::string value = "variant " + std::string(patchy::ai::trustmark_variant_name(mark.variant)) + ", " +
                      std::string(patchy::trustmark::schema_name(mark.schema));
  if (!mark.text.empty()) {
    value += ", text \"" + mark.text + "\"";
  }
  value += ", " + mark.hex;
  return value;
}

QString file_size_text(std::uint64_t size) {
  return QLocale().formattedDataSize(static_cast<qint64>(size));
}

}  // namespace

QString privacy_class_label(MetadataPrivacy privacy) {
  switch (privacy) {
    case MetadataPrivacy::None:
      return {};
    case MetadataPrivacy::Location:
      return QObject::tr("GPS location");
    case MetadataPrivacy::DeviceIdentity:
      return QObject::tr("camera serial numbers or owner");
    case MetadataPrivacy::Personal:
      return QObject::tr("names and contact details");
    case MetadataPrivacy::HiddenImage:
      return QObject::tr("embedded thumbnails or extra images");
    case MetadataPrivacy::EditHistory:
      return QObject::tr("edit history");
    case MetadataPrivacy::MachineInfo:
      return QObject::tr("software and file paths");
    case MetadataPrivacy::Timestamp:
      return QObject::tr("dates and times");
    case MetadataPrivacy::AiPrompt:
      return QObject::tr("AI generation prompts");
    case MetadataPrivacy::Provenance:
      return QObject::tr("Content Credentials");
    case MetadataPrivacy::Watermark:
      return QObject::tr("watermarks");
  }
  return {};
}

std::optional<bool> confirm_strip_metadata(QWidget* parent) {
  QMessageBox box(parent);
  box.setObjectName(QStringLiteral("fileInfoStripConfirm"));
  box.setIcon(QMessageBox::Warning);
  box.setWindowTitle(QObject::tr("Strip All Metadata"));
  box.setText(QObject::tr("Remove all metadata from this document?"));
  box.setInformativeText(QObject::tr(
      "This removes EXIF (including GPS and camera serial numbers), XMP, IPTC, comments, text such as AI "
      "prompts, embedded thumbnails and Content Credentials. The resolution is kept. You can undo it, and the "
      "file changes the next time you save it."));
  auto* strip_button = box.addButton(QObject::tr("Strip Metadata"), QMessageBox::AcceptRole);
  box.addButton(QMessageBox::Cancel);
  box.setDefaultButton(strip_button);
  box.setEscapeButton(QMessageBox::Cancel);
  auto* drop_profile = new QCheckBox(
      QObject::tr("Also remove the color profile (colors may look different in color-managed apps)"), &box);
  drop_profile->setObjectName(QStringLiteral("fileInfoStripDropProfileCheck"));
  box.setCheckBox(drop_profile);
  exec_dialog(box);
  if (box.clickedButton() != strip_button) {
    return std::nullopt;
  }
  return !drop_profile->isChecked();
}

std::optional<FileInfoEdit> request_file_info_edit(QWidget* parent, const Document& document,
                                                   const QString& file_path,
                                                   const FileInfoWatermarkSupport& watermarks) {
  DocumentFileInfo working = document.file_info();
  std::vector<std::uint8_t> working_icc = document.color_state().embedded_icc_profile;
  bool drop_color_profile = false;
  bool changed = false;

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyFileInfoDialog"));
  const auto name = file_path.isEmpty() ? QObject::tr("Untitled") : QFileInfo(file_path).fileName();
  dialog.setWindowTitle(QObject::tr("File Info: %1").arg(name));
  append_themed_style(dialog, QStringLiteral(R"(
    QDialog#patchyFileInfoDialog {
      background: @dlg_raised_bg;
      color: @dlg_raised_text;
    }
    QDialog#patchyFileInfoDialog QLabel {
      background: transparent;
      color: @dlg_raised_text;
    }
    QLabel#fileInfoSectionHeading {
      font-weight: bold;
      padding-top: 8px;
    }
    QLabel#fileInfoNote {
      color: @hint_text;
    }
    QLabel#fileInfoWatermarkCheckStatus[watermarkStatus="found"] {
      background: @warning_banner_bg;
      border: 1px solid @warning_banner_border;
      border-radius: 3px;
      color: @warning_banner_text;
      font-weight: bold;
      padding: 1px 6px;
    }
    QLabel#fileInfoWatermarkCheckStatus[watermarkStatus="notChecked"],
    QLabel#fileInfoWatermarkCheckDetail[watermarkStatus="notChecked"] {
      color: @hint_text;
    }
    QFrame#fileInfoPrivacyBannerRow {
      background: @warning_banner_bg;
      border: 1px solid @warning_banner_border;
      border-radius: 3px;
    }
    QLabel#fileInfoPrivacyBanner {
      background: transparent;
      border: none;
      color: @warning_banner_text;
    }
    QScrollArea[fileInfoPage="true"], QScrollArea[fileInfoPage="true"] > QWidget > QWidget {
      background: transparent;
    }
    QLineEdit, QPlainTextEdit, QComboBox {
      background: @dlg_field_bg;
      border: 1px solid @dlg_field_border;
      color: @dlg_field_text;
      min-height: 22px;
      padding: 1px 4px;
    }
    QLineEdit:focus, QPlainTextEdit:focus {
      border: 1px solid @accent;
    }
    QHeaderView::section {
      background: @dlg_button_bg;
      border: none;
      border-right: 1px solid @dlg_field_border;
      border-bottom: 1px solid @dlg_field_border;
      color: @text_on_raised;
      padding: 3px 6px;
    }
    QPushButton {
      background: @dlg_raised_bg;
      border: 1px solid @dlg_raised_border;
      border-radius: 13px;
      color: @text_on_raised;
      min-height: 24px;
      padding: 0 14px;
    }
    QPushButton:hover {
      border-color: @dlg_raised_hover_border;
      background: @dlg_raised_hover_bg;
    }
    QDialogButtonBox QPushButton {
      min-width: 110px;
    }
  )"));
  dialog.resize(940, 640);

  auto* root = new QVBoxLayout(&dialog);
  root->setContentsMargins(10, 10, 10, 10);
  root->setSpacing(8);

  // The privacy notice, with a way to turn it off (Preferences > Metadata and Watermarks
  // turns it back on). A pixel watermark found by Scan for Watermarks always shows.
  // The bar is the frame; its button sits inside it, as far from the right edge as from the
  // bar's top and bottom (PrivacyBannerInset).
  auto* banner_row = new QFrame(&dialog);
  banner_row->setObjectName(QStringLiteral("fileInfoPrivacyBannerRow"));
  auto* banner_layout = new QHBoxLayout(banner_row);
  banner_layout->setContentsMargins(kPrivacyBannerTextInset, kPrivacyBannerMinimumInset, kPrivacyBannerTextInset,
                                    kPrivacyBannerMinimumInset);
  banner_layout->setSpacing(kPrivacyBannerTextInset);
  auto* banner = new QLabel(banner_row);
  banner->setObjectName(QStringLiteral("fileInfoPrivacyBanner"));
  banner->setWordWrap(true);
  banner_layout->addWidget(banner, 1);
  auto* hide_notice = new QPushButton(QObject::tr("Don't Show Again"), banner_row);
  hide_notice->setObjectName(QStringLiteral("fileInfoHidePrivacyNoticeButton"));
  hide_notice->setToolTip(QObject::tr("Preferences > Metadata and Watermarks can show this notice again."));
  banner_layout->addWidget(hide_notice, 0, Qt::AlignVCenter);
  banner_row->installEventFilter(new PrivacyBannerInset(banner_row, banner_layout, hide_notice));
  root->addWidget(banner_row);
  QString banner_watermarks;  // what the latest scan found, for the banner
  const auto show_banner = [banner, banner_row, hide_notice, &banner_watermarks](
                               const std::map<md::PrivacyClass, std::size_t>& counts) {
    const bool notice = stored_show_file_info_privacy_notice();
    const auto summary = privacy_summary_text(notice ? counts : std::map<md::PrivacyClass, std::size_t>{},
                                              banner_watermarks);
    banner->setText(summary);
    banner->setVisible(!summary.isEmpty());
    hide_notice->setVisible(notice && !privacy_summary_text(counts, {}).isEmpty());
    banner_row->setVisible(!summary.isEmpty());
  };
  QObject::connect(hide_notice, &QPushButton::clicked, &dialog, [show_banner] {
    set_stored_show_file_info_privacy_notice(false);
    show_banner({});
  });

  auto* body = new QHBoxLayout();
  body->setSpacing(10);
  root->addLayout(body, 1);
  auto* categories = new QListWidget(&dialog);
  categories->setObjectName(QStringLiteral("fileInfoCategoryList"));
  categories->setMinimumWidth(170);
  categories->setMaximumWidth(210);
  body->addWidget(categories);
  auto* pages = new QStackedWidget(&dialog);
  pages->setObjectName(QStringLiteral("fileInfoPages"));
  body->addWidget(pages, 1);
  QObject::connect(categories, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);

  const auto add_page = [&](const QString& title, const QString& object_name, QWidget* page) {
    auto* item = new QListWidgetItem(title, categories);
    item->setData(Qt::UserRole, object_name);
    page->setObjectName(object_name);
    pages->addWidget(page);
    return item;
  };

  // Description (editable).
  Editors editors;
  {
    auto* content = new QWidget();
    auto* form = new QFormLayout(content);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(14);
    editors.title = add_line(form, QObject::tr("Document Title:"), "fileInfoTitleEdit", content);
    editors.authors = add_line(form, QObject::tr("Author:"), "fileInfoAuthorEdit", content,
                               QObject::tr("Separate several names with semicolons"));
    editors.author_title = add_line(form, QObject::tr("Author Title:"), "fileInfoAuthorTitleEdit", content);
    editors.description = add_text(form, QObject::tr("Description:"), "fileInfoDescriptionEdit", content, 84);
    editors.rating = new QComboBox(content);
    editors.rating->setObjectName(QStringLiteral("fileInfoRatingCombo"));
    editors.rating->addItem(QObject::tr("None"));
    for (int stars = 1; stars <= 5; ++stars) {
      editors.rating->addItem(QString::number(stars));
    }
    editors.rating->addItem(QObject::tr("Rejected"));
    form->addRow(QObject::tr("Rating:"), editors.rating);
    editors.description_writer =
        add_line(form, QObject::tr("Description Writer:"), "fileInfoDescriptionWriterEdit", content);
    editors.keywords = add_line(form, QObject::tr("Keywords:"), "fileInfoKeywordsEdit", content,
                                QObject::tr("Separate keywords with semicolons or commas"));
    editors.copyright_status = new QComboBox(content);
    editors.copyright_status->setObjectName(QStringLiteral("fileInfoCopyrightStatusCombo"));
    editors.copyright_status->addItems({QObject::tr("Unknown"), QObject::tr("Copyrighted"),
                                        QObject::tr("Public Domain")});
    form->addRow(QObject::tr("Copyright Status:"), editors.copyright_status);
    editors.copyright_notice =
        add_text(form, QObject::tr("Copyright Notice:"), "fileInfoCopyrightNoticeEdit", content, 56);
    editors.copyright_url = add_line(form, QObject::tr("Copyright Info URL:"), "fileInfoCopyrightUrlEdit", content);
    // Short read-only values stay on one line: a wrapped QLabel in the page's
    // scroll area could fold "Adobe Photoshop CC 2017 (Macintosh)" into a
    // second line that the one-line row then clipped, until the dialog was
    // stretched tall enough to lose the scroll bar (GitHub issue 103).
    editors.created = value_label({}, content);
    editors.created->setObjectName(QStringLiteral("fileInfoCreatedLabel"));
    editors.created->setWordWrap(false);
    form->addRow(QObject::tr("Created:"), editors.created);
    editors.modified = value_label({}, content);
    editors.modified->setWordWrap(false);
    form->addRow(QObject::tr("Modified:"), editors.modified);
    editors.creator_tool = value_label({}, content);
    editors.creator_tool->setObjectName(QStringLiteral("fileInfoApplicationLabel"));
    editors.creator_tool->setWordWrap(false);
    form->addRow(QObject::tr("Application:"), editors.creator_tool);
    editors.format = value_label({}, content);
    editors.format->setWordWrap(false);
    form->addRow(QObject::tr("Format:"), editors.format);
    add_page(QObject::tr("Description"), QStringLiteral("fileInfoDescriptionPage"), scroll_page(content));
  }

  // IPTC (editable).
  {
    auto* content = new QWidget();
    auto* form = new QFormLayout(content);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(14);
    editors.headline = add_line(form, QObject::tr("Headline:"), "fileInfoHeadlineEdit", content);
    editors.date_created = add_line(form, QObject::tr("Date Created:"), "fileInfoDateCreatedEdit", content,
                                    QStringLiteral("YYYY-MM-DD"));
    editors.sublocation = add_line(form, QObject::tr("Sublocation:"), "fileInfoSublocationEdit", content);
    editors.city = add_line(form, QObject::tr("City:"), "fileInfoCityEdit", content);
    editors.state = add_line(form, QObject::tr("State/Province:"), "fileInfoStateEdit", content);
    editors.country = add_line(form, QObject::tr("Country:"), "fileInfoCountryEdit", content);
    editors.country_code = add_line(form, QObject::tr("ISO Country Code:"), "fileInfoCountryCodeEdit", content);
    editors.credit = add_line(form, QObject::tr("Credit Line:"), "fileInfoCreditEdit", content);
    editors.source = add_line(form, QObject::tr("Source:"), "fileInfoSourceEdit", content);
    editors.instructions = add_text(form, QObject::tr("Instructions:"), "fileInfoInstructionsEdit", content, 56);
    editors.transmission_reference =
        add_line(form, QObject::tr("Job Identifier:"), "fileInfoJobIdentifierEdit", content);
    editors.usage_terms = add_line(form, QObject::tr("Rights Usage Terms:"), "fileInfoUsageTermsEdit", content);
    editors.intellectual_genre =
        add_line(form, QObject::tr("Intellectual Genre:"), "fileInfoIntellectualGenreEdit", content);
    editors.scene = add_line(form, QObject::tr("IPTC Scene Codes:"), "fileInfoSceneEdit", content);
    editors.subject_code = add_line(form, QObject::tr("IPTC Subject Codes:"), "fileInfoSubjectCodeEdit", content);
    auto* contact = new QLabel(QObject::tr("Creator Contact Info"), content);
    contact->setObjectName(QStringLiteral("fileInfoSectionHeading"));
    form->addRow(contact);
    editors.contact_address = add_line(form, QObject::tr("Address:"), "fileInfoContactAddressEdit", content);
    editors.contact_city = add_line(form, QObject::tr("City:"), "fileInfoContactCityEdit", content);
    editors.contact_region = add_line(form, QObject::tr("State/Province:"), "fileInfoContactRegionEdit", content);
    editors.contact_postal_code =
        add_line(form, QObject::tr("Postal Code:"), "fileInfoContactPostalCodeEdit", content);
    editors.contact_country = add_line(form, QObject::tr("Country:"), "fileInfoContactCountryEdit", content);
    editors.contact_phone = add_line(form, QObject::tr("Phone:"), "fileInfoContactPhoneEdit", content);
    editors.contact_email = add_line(form, QObject::tr("Email:"), "fileInfoContactEmailEdit", content);
    editors.contact_url = add_line(form, QObject::tr("Website:"), "fileInfoContactUrlEdit", content);
    add_page(QObject::tr("IPTC"), QStringLiteral("fileInfoIptcPage"), scroll_page(content));
  }
  for (auto* edit : dialog.findChildren<QLineEdit*>()) {
    select_all_on_focus(*edit);
  }

  // Read-only pages are rebuilt whenever the working copy changes.
  auto* camera_page = scroll_page(nullptr);
  auto* color_page = scroll_page(nullptr);
  auto* history_page = scroll_page(nullptr);
  auto* ai_page = scroll_page(nullptr);
  auto* thumbnails_page = scroll_page(nullptr);
  auto* file_page = scroll_page(nullptr);
  add_page(QObject::tr("Camera Data"), QStringLiteral("fileInfoCameraPage"), camera_page);
  add_page(QObject::tr("Color and Image"), QStringLiteral("fileInfoColorPage"), color_page);
  add_page(QObject::tr("History and Provenance"), QStringLiteral("fileInfoHistoryPage"), history_page);
  auto* ai_item = add_page(QObject::tr("AI Generation"), QStringLiteral("fileInfoAiPage"), ai_page);
  auto* watermarks_page = scroll_page(nullptr);
  add_page(QObject::tr("Watermarks"), QStringLiteral("fileInfoWatermarksPage"), watermarks_page);
  auto* thumbnails_item =
      add_page(QObject::tr("Embedded Thumbnails"), QStringLiteral("fileInfoThumbnailsPage"), thumbnails_page);
  add_page(QObject::tr("File"), QStringLiteral("fileInfoFilePage"), file_page);

  // All Metadata.
  auto* all_page = new QWidget();
  auto* all_layout = new QVBoxLayout(all_page);
  all_layout->setContentsMargins(0, 0, 0, 0);
  auto* filter = new QLineEdit(all_page);
  filter->setObjectName(QStringLiteral("fileInfoAllMetadataFilter"));
  filter->setPlaceholderText(QObject::tr("Filter"));
  filter->setClearButtonEnabled(true);
  all_layout->addWidget(filter);
  auto* all_tree = make_tree(all_page,
                             {QObject::tr("Name"), QObject::tr("Value"), QObject::tr("Source"), QObject::tr("Privacy")},
                             QStringLiteral("fileInfoAllMetadataTree"));
  all_tree->setRootIsDecorated(true);
  all_layout->addWidget(all_tree, 1);
  auto* copy_all = new QPushButton(QObject::tr("Copy All"), all_page);
  copy_all->setObjectName(QStringLiteral("fileInfoCopyAllButton"));
  all_layout->addWidget(copy_all, 0, Qt::AlignRight);
  add_page(QObject::tr("All Metadata"), QStringLiteral("fileInfoAllMetadataPage"), all_page);

  // Raw XMP.
  auto* xmp_page = new QWidget();
  auto* xmp_layout = new QVBoxLayout(xmp_page);
  xmp_layout->setContentsMargins(0, 0, 0, 0);
  auto* xmp_view = new QPlainTextEdit(xmp_page);
  xmp_view->setObjectName(QStringLiteral("fileInfoRawXmpView"));
  xmp_view->setReadOnly(true);
  xmp_view->setLineWrapMode(QPlainTextEdit::NoWrap);
  xmp_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  xmp_layout->addWidget(xmp_view, 1);
  auto* copy_xmp = new QPushButton(QObject::tr("Copy XMP"), xmp_page);
  copy_xmp->setObjectName(QStringLiteral("fileInfoCopyXmpButton"));
  xmp_layout->addWidget(copy_xmp, 0, Qt::AlignRight);
  add_page(QObject::tr("Raw XMP"), QStringLiteral("fileInfoRawXmpPage"), xmp_page);

  // Bottom row.
  auto* bottom = new QHBoxLayout();
  auto* strip_button = new QPushButton(QObject::tr("Strip All Metadata..."), &dialog);
  strip_button->setObjectName(QStringLiteral("fileInfoStripButton"));
  bottom->addWidget(strip_button);
  bottom->addStretch(1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("fileInfoButtonBox"));
  bottom->addWidget(buttons);
  root->addLayout(bottom);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  md::FileInfoFields baseline;
  std::vector<md::MetadataEntry> entries;
  std::function<void()> populate;
  // Scan for Watermarks reads the pixels only when asked (never at open or per repaint).
  std::optional<WatermarkScanReport> watermark_report;
  configure_downloaded_object_selection_model();  // points the engine at the download folder

  // Folds pending field edits into the working copy before a staged operation.
  const auto commit_fields = [&] {
    const auto edited = editors.read(baseline);
    if (md::apply_fields(working, baseline, edited)) {
      changed = true;
    }
    baseline = md::read_fields(working);
  };

  const auto build_camera = [&] {
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    RowsBuilder rows(layout, content);
    const auto camera = md::summarize_camera(working);
    rows.heading(QObject::tr("Camera"));
    rows.row(QObject::tr("Make:"), camera.make);
    rows.row(QObject::tr("Model:"), camera.model);
    rows.row(QObject::tr("Lens:"), camera.lens);
    rows.row(QObject::tr("Body serial number:"), camera.body_serial);
    rows.row(QObject::tr("Lens serial number:"), camera.lens_serial);
    rows.row(QObject::tr("Camera owner:"), camera.owner);
    rows.heading(QObject::tr("Exposure"));
    rows.row(QObject::tr("Exposure time:"), camera.exposure_time);
    rows.row(QObject::tr("F-number:"), camera.f_number);
    rows.row(QObject::tr("ISO:"), camera.iso);
    rows.row(QObject::tr("Focal length:"), camera.focal_length);
    rows.row(QObject::tr("35 mm equivalent:"), camera.focal_length_35mm);
    rows.row(QObject::tr("Exposure bias:"), camera.exposure_bias);
    rows.row(QObject::tr("Exposure program:"), camera.exposure_program);
    rows.row(QObject::tr("Metering:"), camera.metering_mode);
    rows.row(QObject::tr("Flash:"), camera.flash);
    rows.row(QObject::tr("White balance:"), camera.white_balance);
    rows.heading(QObject::tr("Dates"));
    rows.row(QObject::tr("Taken:"), camera.date_original);
    rows.row(QObject::tr("Digitized:"), camera.date_digitized);
    rows.row(QObject::tr("Time zone offset:"), camera.offset_time);
    rows.row(QObject::tr("Orientation:"), camera.orientation);
    rows.row(QObject::tr("Software:"), camera.software);
    if (camera.has_maker_note) {
      rows.note(QObject::tr("The file also carries a maker note: the camera maker's private data, which often "
                            "includes serial numbers."));
    }
    rows.heading(QObject::tr("Location"));
    if (camera.has_location()) {
      rows.row(QObject::tr("Latitude:"), QString::number(*camera.latitude, 'f', 6));
      rows.row(QObject::tr("Longitude:"), QString::number(*camera.longitude, 'f', 6));
      if (camera.altitude) {
        rows.row(QObject::tr("Altitude:"), QObject::tr("%1 m").arg(QString::number(*camera.altitude, 'f', 1)));
      }
      rows.row(QObject::tr("Direction:"), camera.gps_direction);
      rows.row(QObject::tr("GPS time:"), camera.gps_timestamp);
    } else {
      rows.note(QObject::tr("No GPS location."));
    }
    auto* remove = new QPushButton(QObject::tr("Remove Location"), content);
    remove->setObjectName(QStringLiteral("fileInfoRemoveLocationButton"));
    remove->setEnabled(camera.has_location());
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
      commit_fields();
      if (md::remove_location(working)) {
        changed = true;
      }
      populate();
    });
    rows.widget(remove);
    layout->addStretch(1);
    replace_page_content(camera_page, content);
  };

  const auto build_color = [&] {
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    RowsBuilder rows(layout, content);
    rows.heading(QObject::tr("Image"));
    rows.row(QObject::tr("Source format:"), working.source_format);
    rows.row(QObject::tr("Dimensions:"),
             QObject::tr("%1 x %2 px").arg(document.width()).arg(document.height()));
    const auto depth = document.color_state().bit_depth;
    rows.row(QObject::tr("Bit depth:"), depth == BitDepth::Float32   ? QObject::tr("32 bits per channel")
                                        : depth == BitDepth::UInt16 ? QObject::tr("16 bits per channel")
                                                                     : QObject::tr("8 bits per channel"));
    rows.row(QObject::tr("Resolution:"),
             QObject::tr("%1 pixels/inch").arg(QString::number(document.print_settings().horizontal_ppi, 'f', 2)));
    const bool kept = !working_icc.empty();
    const std::span<const std::uint8_t> icc =
        kept ? std::span<const std::uint8_t>(working_icc)
             : (working.source_icc ? std::span<const std::uint8_t>(*working.source_icc)
                                   : std::span<const std::uint8_t>{});
    rows.heading(kept ? QObject::tr("Embedded color profile") : QObject::tr("Color profile"));
    if (const auto summary = md::summarize_icc(icc)) {
      rows.row(QObject::tr("Description:"), summary->description);
      rows.row(QObject::tr("Copyright:"), summary->copyright);
      rows.row(QObject::tr("Device class:"), summary->device_class);
      rows.row(QObject::tr("Color space:"), summary->color_space);
      rows.row(QObject::tr("Connection space:"), summary->connection_space);
      rows.row(QObject::tr("Version:"), summary->version);
      rows.row(QObject::tr("Created:"), summary->created);
      rows.row(QObject::tr("Size:"), file_size_text(summary->size));
      if (!kept && !working.source_icc_note.empty()) {
        rows.note(QCoreApplication::translate("QObject", working.source_icc_note.c_str()));
      }
    } else {
      rows.note(QObject::tr("No embedded color profile. Applications assume sRGB."));
    }
    bool chunk_heading = false;
    for (const auto& entry : entries) {
      if (entry.group != "PNG color") {
        continue;
      }
      if (!chunk_heading) {
        rows.heading(QObject::tr("PNG color chunks"));
        chunk_heading = true;
      }
      rows.row(utf8(entry.name) + QLatin1Char(':'), entry.value);
    }
    layout->addStretch(1);
    replace_page_content(color_page, content);
  };

  const auto build_history = [&] {
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    RowsBuilder rows(layout, content);
    const auto history = md::summarize_edit_history(working);
    rows.heading(QObject::tr("Document identity"));
    rows.row(QObject::tr("Document ID:"), history.document_id);
    rows.row(QObject::tr("Instance ID:"), history.instance_id);
    rows.row(QObject::tr("Original document ID:"), history.original_document_id);
    rows.row(QObject::tr("Derived from:"), history.derived_from);
    if (!history.events.empty()) {
      rows.heading(QObject::tr("History"));
      auto* tree = make_tree(content,
                             {QObject::tr("Action"), QObject::tr("When"), QObject::tr("Software"),
                              QObject::tr("Changed")},
                             QStringLiteral("fileInfoHistoryTree"));
      for (const auto& event : history.events) {
        new QTreeWidgetItem(tree, {utf8(event.action), utf8(event.when), utf8(event.software_agent),
                                   utf8(event.changed)});
      }
      tree->setMinimumHeight(140);
      rows.widget(tree);
    }
    if (history.ancestor_count > 0) {
      rows.row(QObject::tr("Document ancestors:"),
               QStringLiteral("%1 (%2)")
                   .arg(static_cast<qulonglong>(history.ancestor_count))
                   .arg(file_size_text(history.ancestor_bytes)));
      rows.note(QObject::tr("Photoshop records every document pasted or placed into this one. The list only grows "
                            "and links this file to others."));
    }
    if (!history.text_layers.empty()) {
      rows.heading(QObject::tr("Cached text layer contents"));
      auto* tree = make_tree(content, {QObject::tr("Layer"), QObject::tr("Text")},
                             QStringLiteral("fileInfoTextLayersTree"));
      for (const auto& [layer, text] : history.text_layers) {
        new QTreeWidgetItem(tree, {utf8(layer), utf8(text)});
      }
      tree->setMinimumHeight(100);
      rows.widget(tree);
      rows.note(QObject::tr("This copy of the text can include layers that were later hidden or deleted."));
    }
    if (history.has_camera_raw_settings) {
      rows.row(QObject::tr("Camera Raw:"), QObject::tr("Development settings are stored."));
    }
    for (const auto& item : working.observed) {
      if (item.privacy != md::PrivacyClass::Provenance) {
        continue;
      }
      rows.heading(QObject::tr("Content Credentials (C2PA)"));
      rows.row(QObject::tr("Manifest:"), item.summary);
      rows.note(QObject::tr("Content Credentials are not written when Patchy saves: edits invalidate their "
                            "signature."));
    }
    if (rows.rows() == 0 && history.events.empty() && history.text_layers.empty()) {
      rows.note(QObject::tr("No edit history or provenance records."));
    }
    auto* remove = new QPushButton(QObject::tr("Remove Edit History"), content);
    remove->setObjectName(QStringLiteral("fileInfoRemoveHistoryButton"));
    remove->setEnabled(!history.empty());
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
      commit_fields();
      if (md::remove_edit_history(working)) {
        changed = true;
      }
      populate();
    });
    rows.widget(remove);
    layout->addStretch(1);
    replace_page_content(history_page, content);
  };

  const auto build_ai = [&] {
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    RowsBuilder rows(layout, content);
    const auto ai = md::summarize_ai_generation(working);
    rows.row(QObject::tr("Generator:"), ai.tool);
    rows.row(QObject::tr("Digital source type:"), ai.digital_source_type);
    const auto add_text_block = [&](const QString& heading, const std::string& text, const char* object_name) {
      if (text.empty()) {
        return;
      }
      rows.heading(heading);
      auto* view = read_only_text(utf8(text), content, 160);
      view->setObjectName(QString::fromLatin1(object_name));
      rows.widget(view);
    };
    add_text_block(QObject::tr("Prompt"), ai.prompt, "fileInfoAiPromptView");
    add_text_block(QObject::tr("Negative prompt"), ai.negative_prompt, "fileInfoAiNegativePromptView");
    add_text_block(QObject::tr("Settings"), ai.settings, "fileInfoAiSettingsView");
    if (!ai.workflow.empty()) {
      const auto json = QJsonDocument::fromJson(QByteArray::fromStdString(ai.workflow));
      const auto pretty = json.isNull() ? QByteArray::fromStdString(ai.workflow) : json.toJson(QJsonDocument::Indented);
      add_text_block(QObject::tr("Workflow"), pretty.toStdString(), "fileInfoAiWorkflowView");
    }
    layout->addStretch(1);
    replace_page_content(ai_page, content);
    ai_item->setHidden(!ai.present());
  };

  const auto build_watermarks = [&] {
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    RowsBuilder rows(layout, content);
    rows.heading(QObject::tr("Declared in metadata"));
    bool declared = false;
    for (const auto& item : working.observed) {
      if (item.privacy == md::PrivacyClass::Watermark) {
        rows.row(QObject::tr("Content Credentials:"), item.summary);
        declared = true;
      }
    }
    if (!declared) {
      rows.note(QObject::tr("The file's metadata declares no watermark."));
    }
    rows.heading(QObject::tr("Invisible watermarks in the pixels"));
    rows.note(QObject::tr("Scan for Watermarks checks the image as it looks now for the open invisible watermarks "
                          "that Stable Diffusion 1.x, 2.x and SDXL add to the images they generate, and for Adobe "
                          "TrustMark once the watermark models are downloaded. Cropping, resizing or repainting the "
                          "image usually removes them."));
    auto* scan_button = new QPushButton(QObject::tr("Scan for Watermarks"), content);
    scan_button->setObjectName(QStringLiteral("fileInfoScanWatermarksButton"));
    const auto run_scan = [&] {
      QGuiApplication::setOverrideCursor(Qt::WaitCursor);
      watermark_report = scan_watermarks(Compositor{}.flatten_rgb8(document), working.observed);
      QGuiApplication::restoreOverrideCursor();
    };
    QObject::connect(scan_button, &QPushButton::clicked, &dialog, [&, run_scan] {
      commit_fields();
      run_scan();
      populate();
    });
    rows.widget(scan_button);
    if (watermark_report) {
      // Rebuilt on every populate so the metadata row follows a staged Strip All Metadata.
      watermark_report->checks = watermark_checks(watermark_report->stable_diffusion, watermark_report->trustmark,
                                                  watermark_report->trustmark_error, working.observed);
      QStringList found;
      bool unchecked = false;
      for (const auto& check : watermark_report->checks) {
        if (check.status == WatermarkCheckStatus::Found) {
          found.push_back(check.name);
        }
        unchecked = unchecked || check.status == WatermarkCheckStatus::NotChecked;
      }
      QString summary;
      if (!found.isEmpty()) {
        summary = QObject::tr("Found: %1.").arg(found.join(QStringLiteral(", ")));
      } else if (unchecked) {
        summary = QObject::tr("None of the watermarks that could be checked was found. Some kinds were not checked.");
      } else {
        summary = QObject::tr("None of the known watermarks was found.");
      }
      auto* label = value_label(summary, content);
      label->setObjectName(QStringLiteral("fileInfoWatermarkScanResult"));
      rows.widget(label);

      auto* table = new QWidget(content);
      table->setObjectName(QStringLiteral("fileInfoWatermarkScanResults"));
      auto* grid = new QGridLayout(table);
      grid->setContentsMargins(0, 2, 0, 2);
      grid->setHorizontalSpacing(14);
      grid->setVerticalSpacing(5);
      grid->setColumnStretch(2, 1);
      int row = 0;
      for (const auto& check : watermark_report->checks) {
        const auto token = watermark_check_status_token(check.status);
        auto* name_label = value_label(check.name, table);
        name_label->setObjectName(QStringLiteral("fileInfoWatermarkCheckName"));
        name_label->setWordWrap(false);
        auto* status_label = value_label(watermark_check_status_text(check.status), table);
        status_label->setObjectName(QStringLiteral("fileInfoWatermarkCheckStatus"));
        status_label->setWordWrap(false);
        auto* detail_label = value_label(check.detail, table);
        detail_label->setObjectName(QStringLiteral("fileInfoWatermarkCheckDetail"));
        for (auto* cell : {name_label, status_label, detail_label}) {
          cell->setProperty("watermarkCheck", check.id);
          cell->setProperty("watermarkStatus", token);
        }
        grid->addWidget(name_label, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        grid->addWidget(status_label, row, 1, Qt::AlignLeft | Qt::AlignVCenter);
        grid->addWidget(detail_label, row, 2, Qt::AlignVCenter);
        ++row;
      }
      rows.widget(table);
    }
    if (!patchy::ai::trustmark_models_available()) {
      if (patchy::ai::trustmark_runtime_available() && watermarks.download_models) {
        rows.note(QObject::tr("Adobe TrustMark, the open watermark behind durable Content Credentials, needs the "
                              "watermark models (Adobe's, MIT License): a %1 MB download from %2, made only when "
                              "you ask.")
                      .arg(watermark_model_download_megabytes())
                      .arg(model_download_source_text(watermark_model_files())));
        auto* download_button = new QPushButton(QObject::tr("Download Watermark Models"), content);
        download_button->setObjectName(QStringLiteral("fileInfoDownloadWatermarkModelsButton"));
        QObject::connect(download_button, &QPushButton::clicked, &dialog, [&, run_scan] {
          commit_fields();
          if (watermarks.download_models(&dialog)) {
            if (watermark_report) {
              run_scan();  // the TrustMark rows were Not checked; fill them in
            }
            populate();
          }
        });
        rows.widget(download_button);
      } else if (!watermark_report) {
        rows.note(QObject::tr("This version of Patchy cannot read TrustMark watermarks."));
      }
    }
    rows.note(watermark_scan_limits_note());
    layout->addStretch(1);
    replace_page_content(watermarks_page, content);
  };

  const auto build_thumbnails = [&] {
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    const auto thumbnails = md::embedded_thumbnails(working);
    auto* note = value_label(QObject::tr("Thumbnails are written by the camera or editor that saved the file. "
                                         "They are not updated by every edit, so they can show content that was "
                                         "cropped out or painted over."),
                             content);
    note->setObjectName(QStringLiteral("fileInfoNote"));
    layout->addWidget(note);
    auto* grid = new QGridLayout();
    int index = 0;
    for (const auto& thumbnail : thumbnails) {
      QImage image;
      image.loadFromData(thumbnail.bytes.data(), static_cast<int>(thumbnail.bytes.size()));
      auto* picture = new QLabel(content);
      picture->setObjectName(QStringLiteral("fileInfoThumbnailImage"));
      if (!image.isNull()) {
        picture->setPixmap(QPixmap::fromImage(image.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
      } else {
        picture->setText(QObject::tr("(cannot display)"));
      }
      auto* caption = value_label(utf8(thumbnail.source) + QLatin1Char('\n') +
                                      (image.isNull() ? file_size_text(thumbnail.bytes.size())
                                                      : QObject::tr("%1 x %2 px").arg(image.width()).arg(image.height())),
                                  content);
      grid->addWidget(picture, (index / 2) * 2, index % 2, Qt::AlignLeft | Qt::AlignBottom);
      grid->addWidget(caption, (index / 2) * 2 + 1, index % 2, Qt::AlignLeft | Qt::AlignTop);
      ++index;
    }
    layout->addLayout(grid);
    layout->addStretch(1);
    replace_page_content(thumbnails_page, content);
    thumbnails_item->setHidden(thumbnails.empty());
  };

  const auto build_file = [&] {
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    RowsBuilder rows(layout, content);
    const QFileInfo file(file_path);
    rows.row(QObject::tr("Location:"), file_path.isEmpty() ? QObject::tr("Not saved yet")
                                                           : QDir::toNativeSeparators(file.absoluteFilePath()));
    if (working.source_file_size > 0) {
      rows.row(QObject::tr("Size on disk:"), file_size_text(working.source_file_size));
    }
    if (!file_path.isEmpty() && file.exists()) {
      rows.row(QObject::tr("Modified:"), QLocale().toString(file.lastModified(), QLocale::LongFormat));
    }
    rows.row(QObject::tr("Format:"), working.source_format);
    if (!working.structure.empty()) {
      rows.heading(QObject::tr("Layout of the file as it was opened"));
      auto* tree = make_tree(content, {QObject::tr("Block"), QObject::tr("Offset"), QObject::tr("Size")},
                             QStringLiteral("fileInfoStructureTree"));
      for (const auto& segment : working.structure) {
        new QTreeWidgetItem(tree, {segment_display_name(segment.name),
                                   segment.offset > 0 ? QString::number(segment.offset) : QString(),
                                   QString::number(segment.size)});
      }
      tree->setMinimumHeight(260);
      rows.widget(tree);
    }
    layout->addStretch(1);
    replace_page_content(file_page, content);
  };

  const auto build_all = [&] {
    all_tree->clear();
    std::map<std::string, QTreeWidgetItem*> groups;
    const auto warning = theme().icon_warning;
    for (const auto& entry : entries) {
      auto*& group = groups[entry.group];
      if (group == nullptr) {
        group = new QTreeWidgetItem(all_tree, {utf8(entry.group)});
        group->setFirstColumnSpanned(true);
        group->setExpanded(true);
      }
      auto value = utf8(entry.value);
      if (value.size() > 500) {
        value = value.left(500) + QStringLiteral("...");
      }
      value.replace(QLatin1Char('\n'), QLatin1Char(' '));
      auto* item = new QTreeWidgetItem(group, {utf8(entry.name), value, utf8(entry.source),
                                               privacy_class_label(entry.privacy)});
      item->setToolTip(1, utf8(entry.value).left(4000));
      if (entry.privacy != md::PrivacyClass::None && entry.privacy != md::PrivacyClass::Timestamp) {
        item->setForeground(3, warning);
      }
    }
    all_tree->resizeColumnToContents(0);
    all_tree->setColumnWidth(0, std::min(all_tree->columnWidth(0), 260));
    all_tree->setColumnWidth(1, 280);
    all_tree->setColumnWidth(2, 90);
  };

  populate = [&] {
    baseline = md::read_fields(working);
    editors.load(baseline);
    editors.format->setText(utf8(working.source_format));
    entries = md::describe_file_info(working, working_icc);
    QStringList pixel_watermarks;
    const auto counts = md::privacy_summary(entries);
    if (watermark_report) {
      for (const auto& match : watermark_report->stable_diffusion.matches) {
        const auto name = QString::fromLatin1(match.name.data(), static_cast<qsizetype>(match.name.size()));
        pixel_watermarks.push_back(name);
        // All Metadata values are untranslated identifiers (docs/file-info.md).
        entries.push_back(md::MetadataEntry{
            "Pixel scan", "Invisible watermark",
            name.toStdString() + " (" + std::to_string(match.bit_count - match.differing_bits) + " of " +
                std::to_string(match.bit_count) + " bits match" +
                (match.red_blue_swapped ? ", red and blue exchanged)" : ")"),
            "pixels", md::PrivacyClass::Watermark});
      }
    }
    if (watermark_report && watermark_report->trustmark.has_value() && !watermark_report->trustmark->marks.empty()) {
      pixel_watermarks.push_back(QStringLiteral("TrustMark"));
      for (const auto& mark : watermark_report->trustmark->marks) {
        entries.push_back(md::MetadataEntry{"Pixel scan", "TrustMark watermark", trustmark_mark_identifiers(mark),
                                            "pixels", md::PrivacyClass::Watermark});
      }
    }
    banner_watermarks = pixel_watermarks.join(QStringLiteral(", "));
    show_banner(counts);
    build_camera();
    build_color();
    build_history();
    build_ai();
    build_watermarks();
    build_thumbnails();
    build_file();
    build_all();
    xmp_view->setPlainText(working.xmp ? QString::fromStdString(md::xmp_display_text(*working.xmp))
                                       : QObject::tr("No XMP metadata."));
    filter->clear();
  };

  QObject::connect(filter, &QLineEdit::textChanged, all_tree, [all_tree](const QString& text) {
    for (int g = 0; g < all_tree->topLevelItemCount(); ++g) {
      auto* group = all_tree->topLevelItem(g);
      int visible = 0;
      for (int i = 0; i < group->childCount(); ++i) {
        auto* item = group->child(i);
        const bool match = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive) ||
                           item->text(1).contains(text, Qt::CaseInsensitive) ||
                           group->text(0).contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
        visible += match ? 1 : 0;
      }
      group->setHidden(visible == 0);
    }
  });
  QObject::connect(copy_all, &QPushButton::clicked, &dialog, [&] {
    QString text;
    for (const auto& entry : entries) {
      text += utf8(entry.group) + QLatin1Char('\t') + utf8(entry.name) + QLatin1Char('\t') + utf8(entry.value) +
              QLatin1Char('\n');
    }
    QGuiApplication::clipboard()->setText(text);
  });
  QObject::connect(copy_xmp, &QPushButton::clicked, &dialog,
                   [xmp_view] { QGuiApplication::clipboard()->setText(xmp_view->toPlainText()); });
  QObject::connect(strip_button, &QPushButton::clicked, &dialog, [&] {
    const auto keep_profile = confirm_strip_metadata(&dialog);
    if (!keep_profile) {
      return;
    }
    md::strip_file_info(working, *keep_profile);
    if (!*keep_profile) {
      working_icc.clear();
      drop_color_profile = true;
    }
    changed = true;
    populate();
  });

  populate();
  categories->setCurrentRow(0);
  if (watermarks.open_watermarks_and_scan) {
    for (int row = 0; row < categories->count(); ++row) {
      if (categories->item(row)->data(Qt::UserRole).toString() == QStringLiteral("fileInfoWatermarksPage")) {
        categories->setCurrentRow(row);
      }
    }
    QTimer::singleShot(0, &dialog, [watermarks_page] {
      for (auto* button : watermarks_page->findChildren<QPushButton*>(QStringLiteral("fileInfoScanWatermarksButton"))) {
        button->click();
        return;
      }
    });
  }
  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  commit_fields();
  if (!changed) {
    return std::nullopt;
  }
  return FileInfoEdit{std::move(working), drop_color_profile};
}

}  // namespace patchy::ui
