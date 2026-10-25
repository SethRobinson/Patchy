#include "ui/gmic_filter_library_dialog.hpp"

#include "ui/coalesced_preview_emitter.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/gmic_parameter_panel.hpp"
#include "ui/theme_qss.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QComboBox>
#include <QSignalBlocker>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <map>

namespace patchy::ui {

namespace {

constexpr int kFilterIdRole = Qt::UserRole + 1;

[[nodiscard]] QString from_gmic(const std::string& text) {
  return QString::fromUtf8(text.c_str(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] bool is_filter_item(const QTreeWidgetItem* item) {
  return item != nullptr && item->data(0, kFilterIdRole).isValid();
}

}  // namespace

GmicFilterLibraryDialog::GmicFilterLibraryDialog(const gmic::Catalog& catalog, const QString& engine_version,
                                                 QWidget* parent)
    : QDialog(parent), catalog_(catalog), engine_version_(engine_version) {
  setObjectName(QStringLiteral("gmicFilterLibraryDialog"));
  setWindowTitle(tr("G'MIC Filter Library"));
  resize(880, 580);

  auto* root = new QVBoxLayout(this);
  auto* splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setChildrenCollapsible(false);
  root->addWidget(splitter, 1);

  auto* browser = new QWidget(splitter);
  auto* browser_layout = new QVBoxLayout(browser);
  browser_layout->setContentsMargins(0, 0, 0, 0);
  search_ = new QLineEdit(browser);
  search_->setObjectName(QStringLiteral("gmicFilterSearchEdit"));
  search_->setPlaceholderText(tr("Search filters..."));
  search_->setClearButtonEnabled(true);
  browser_layout->addWidget(search_);
  tree_ = new QTreeWidget(browser);
  tree_->setObjectName(QStringLiteral("gmicFilterTree"));
  tree_->setHeaderHidden(true);
  tree_->setUniformRowHeights(true);
  browser_layout->addWidget(tree_, 1);
  splitter->addWidget(browser);

  auto* details = new QWidget(splitter);
  auto* details_layout = new QVBoxLayout(details);
  details_layout->setContentsMargins(8, 0, 0, 0);
  title_ = new QLabel(details);
  title_->setObjectName(QStringLiteral("gmicFilterTitle"));
  title_->setFont(offset_font(title_->font(), 3, true));
  details_layout->addWidget(title_);
  folder_ = new QLabel(details);
  folder_->setObjectName(QStringLiteral("gmicFilterFolder"));
  set_themed_style(*folder_, QStringLiteral("color: @text_secondary;"));
  details_layout->addWidget(folder_);
  message_ = new QLabel(details);
  message_->setObjectName(QStringLiteral("gmicPreviewMessage"));
  message_->setWordWrap(true);
  set_themed_style(*message_, QStringLiteral("color: @text_secondary;"));
  message_->hide();
  details_layout->addWidget(message_);
  notes_ = new QLabel(details);
  notes_->setObjectName(QStringLiteral("gmicResultNotes"));
  notes_->setWordWrap(true);
  set_themed_style(*notes_, QStringLiteral("QLabel#gmicResultNotes { background: @info_banner_bg; border: 1px solid "
                                           "@info_banner_border; border-radius: 3px; color: @info_banner_text; "
                                           "padding: 6px 8px; }"));
  notes_->hide();
  details_layout->addWidget(notes_);
  auto* input_row = new QHBoxLayout;
  auto* input_label = new QLabel(tr("Input layers:"), details);
  input_ = new QComboBox(details);
  input_->setObjectName(QStringLiteral("gmicInputLayersCombo"));
  input_->addItem(tr("Active layer"), QChar(QLatin1Char('.')));
  input_->addItem(tr("Active layer and the one below"), QChar(QLatin1Char('+')));
  input_->addItem(tr("Active layer and the one above"), QChar(QLatin1Char('-')));
  input_->addItem(tr("All layers"), QChar(QLatin1Char('*')));
  input_->addItem(tr("All visible layers"), QChar(QLatin1Char('v')));
  input_->addItem(tr("All hidden layers"), QChar(QLatin1Char('i')));
  input_->setToolTip(tr("The layers the filter works on. A filter made for one image is applied to each chosen "
                        "layer in turn; a filter made for several layers, such as the Blend filters, combines them. "
                        "Locked, text, shape and Smart Object layers are read but never changed."));
  input_label->setBuddy(input_);
  input_row->addWidget(input_label);
  input_row->addWidget(input_);
  input_row->addStretch(1);
  details_layout->addLayout(input_row);
  auto* scroll = new QScrollArea(details);
  scroll->setObjectName(QStringLiteral("gmicParameterScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* page = new QWidget(scroll);
  auto* page_layout = new QVBoxLayout(page);
  page_layout->setContentsMargins(0, 6, 8, 0);
  panel_ = new GmicParameterPanel(page);
  panel_->setObjectName(QStringLiteral("gmicParameterPanel"));
  page_layout->addWidget(panel_);
  page_layout->addStretch(1);
  scroll->setWidget(page);
  details_layout->addWidget(scroll, 1);
  splitter->addWidget(details);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({280, 600});

  auto* footer = new QHBoxLayout;
  reset_ = new QPushButton(tr("Reset"), this);
  reset_->setObjectName(QStringLiteral("gmicFilterResetButton"));
  footer->addWidget(reset_);
  preview_ = new QCheckBox(tr("Preview"), this);
  preview_->setObjectName(QStringLiteral("gmicPreviewCheck"));
  preview_->setChecked(true);
  footer->addWidget(preview_);
  auto* about = new QPushButton(tr("About G'MIC..."), this);
  about->setObjectName(QStringLiteral("gmicAboutButton"));
  about->setAutoDefault(false);
  footer->addWidget(about);
  auto* credit = new QLabel(tr("Filters from G'MIC %1 (gmic.eu)").arg(engine_version), this);
  credit->setObjectName(QStringLiteral("gmicCreditLabel"));
  set_themed_style(*credit, QStringLiteral("color: @text_secondary;"));
  footer->addWidget(credit);
  footer->addStretch(1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  ok_ = buttons->button(QDialogButtonBox::Ok);
  footer->addWidget(buttons);
  root->addLayout(footer);

  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(reset_, &QPushButton::clicked, this, [this] {
    panel_->reset_to_defaults();
    request_preview(true);
  });
  connect(about, &QPushButton::clicked, this, &GmicFilterLibraryDialog::show_about_gmic);
  connect(preview_, &QCheckBox::toggled, this, [this](bool) { request_preview(true); });
  connect(input_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
    set_result_notes({});
    request_preview(true);
    report_points();  // the image the percentages refer to changed
  });
  panel_->set_points_changed_callback([this] { report_points(); });
  panel_->set_changed_callback([this](bool updates_preview) {
    if (updates_preview) {
      request_preview(false);
    }
  });
  preview_emitter_ = std::make_unique<CoalescedPreviewEmitter<GmicPreviewRequest>>(
      *this, [this](const GmicPreviewRequest& request) {
        if (preview_callback_) {
          preview_callback_(request);
        }
      });
  connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { apply_search(); });
  // Enter in the search field picks the first match instead of accepting the dialog
  // (keyPressEvent keeps the key from the default button).
  connect(search_, &QLineEdit::returnPressed, this, [this] {
    for (QTreeWidgetItemIterator it(tree_); *it != nullptr; ++it) {
      if (is_filter_item(*it) && !(*it)->isHidden()) {
        tree_->setCurrentItem(*it);
        break;
      }
    }
  });
  connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item, QTreeWidgetItem*) {
    if (is_filter_item(item)) {
      show_filter(catalog_.find(item->data(0, kFilterIdRole).toByteArray().toStdString()), {});
    }
  });
  connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
    if (is_filter_item(item) && current_ != nullptr) {
      accept();
    }
  });

  rebuild_tree();
  show_filter(nullptr, {});
  append_themed_style(*this, dialog_spinbox_button_style());
}

GmicFilterLibraryDialog::~GmicFilterLibraryDialog() = default;

void GmicFilterLibraryDialog::keyPressEvent(QKeyEvent* event) {
  // The search field leaves Enter unhandled, and QDialog would click OK with it.
  if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && focusWidget() == search_) {
    event->accept();
    return;
  }
  QDialog::keyPressEvent(event);
}

void GmicFilterLibraryDialog::set_preview_callback(std::function<void(const GmicPreviewRequest&)> callback) {
  preview_callback_ = std::move(callback);
}

void GmicFilterLibraryDialog::set_points_callback(std::function<void(const std::vector<QPointF>&)> callback) {
  points_callback_ = std::move(callback);
  report_points();
}

void GmicFilterLibraryDialog::report_points() {
  if (!points_callback_) {
    return;
  }
  // Plain position markers only. Blur [Depth-of-Field] and Blur [Motion] are not offered
  // at all (docs/legal-constraints.md, "G'MIC engine"); should one ever reach this dialog
  // without that review, it still gets no on-canvas control.
  const bool handles =
      current_ != nullptr && current_->id != "fx_blur_dof" && current_->id != "fx_blur_motion";
  points_callback_(handles ? panel_->points() : std::vector<QPointF>{});
}

void GmicFilterLibraryDialog::move_point(int index, QPointF percent) {
  panel_->set_point(index, percent);
}

bool GmicFilterLibraryDialog::preview_enabled() const {
  return preview_->isChecked();
}

void GmicFilterLibraryDialog::request_preview(bool immediate) {
  GmicPreviewRequest request{choice(), preview_->isChecked()};
  if (immediate) {
    preview_emitter_->flush(std::move(request));
  } else {
    preview_emitter_->schedule(std::move(request));
  }
}

void GmicFilterLibraryDialog::apply_status(const std::string& filter_id, const std::string& status) {
  if (current_ != nullptr && current_->id == filter_id) {
    panel_->apply_status(status);
  }
}

void GmicFilterLibraryDialog::release_buttons() {
  panel_->release_buttons();
}

void GmicFilterLibraryDialog::set_preview_message(const QString& text) {
  message_->setText(text);
  message_->setVisible(!text.isEmpty());
}

void GmicFilterLibraryDialog::set_result_notes(const QStringList& notes) {
  notes_->setText(notes.join(QLatin1Char('\n')));
  notes_->setVisible(!notes.isEmpty());
}

QStringList GmicFilterLibraryDialog::result_notes() const {
  return notes_->text().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

void GmicFilterLibraryDialog::show_about_gmic() {
  QDialog about(this);
  about.setObjectName(QStringLiteral("gmicAboutDialog"));
  about.setWindowTitle(tr("About G'MIC"));
  auto* layout = new QVBoxLayout(&about);
  auto* heading = new QLabel(tr("G'MIC %1").arg(engine_version_), &about);
  heading->setObjectName(QStringLiteral("gmicAboutHeading"));
  heading->setFont(offset_font(heading->font(), 3, true));
  layout->addWidget(heading);
  const QStringList paragraphs{
      tr("G'MIC (GREYC's Magic for Image Computing) is a free, open-source framework for image processing. "
         "Patchy includes its interpreter and %n of the filters from its standard library, and runs them in "
         "this Patchy dialog.",
         nullptr, static_cast<int>(catalog_.filters.size())),
      tr("G'MIC is made by David Tschumperlé and the Image Team of the GREYC laboratory in Caen, France, "
         "with filters written by many contributors. Most filters name their authors at the end of their "
         "settings."),
      tr("How to say it: its author pronounces it like the English word \"gimmick\", a nod to the magic in its "
         "name, and says any pronunciation is fine."),
      tr("G'MIC is distributed under the CeCILL-C license, and its license text is included with Patchy. "
         "Questions about how Patchy runs a filter belong with Patchy, not the G'MIC team."),
  };
  auto* body = new QLabel(&about);
  body->setObjectName(QStringLiteral("gmicAboutText"));
  body->setTextFormat(Qt::RichText);
  body->setWordWrap(true);
  QString html;
  for (const auto& paragraph : paragraphs) {
    html += QStringLiteral("<p>%1</p>").arg(paragraph.toHtmlEscaped());
  }
  body->setText(html);
  body->setMinimumWidth(420);
  layout->addWidget(body);
  auto* link = new QLabel(&about);
  link->setObjectName(QStringLiteral("gmicAboutLink"));
  link->setTextFormat(Qt::RichText);
  link->setTextInteractionFlags(Qt::TextBrowserInteraction);
  link->setOpenExternalLinks(true);
  set_themed_label_text(*link, QStringLiteral("<a style=\"color:@accent\" href=\"https://gmic.eu\">%1</a>")
                                   .arg(tr("Visit the G'MIC website (gmic.eu)").toHtmlEscaped()));
  layout->addWidget(link);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &about);
  connect(buttons, &QDialogButtonBox::accepted, &about, &QDialog::accept);
  layout->addWidget(buttons);
  exec_dialog(about);
}

void GmicFilterLibraryDialog::rebuild_tree() {
  tree_->clear();
  // Folder path -> item, created on first use so the tree keeps the library's order.
  std::map<std::vector<std::string>, QTreeWidgetItem*> folders;
  const auto folder_item = [&](const std::vector<std::string>& path) {
    QTreeWidgetItem* parent = nullptr;
    std::vector<std::string> prefix;
    for (const auto& name : path) {
      prefix.push_back(name);
      auto found = folders.find(prefix);
      if (found == folders.end()) {
        auto* item = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
        item->setText(0, from_gmic(name));
        item->setFlags(Qt::ItemIsEnabled);
        found = folders.emplace(prefix, item).first;
      }
      parent = found->second;
    }
    return parent;
  };
  for (const auto& filter : catalog_.filters) {
    auto* parent = folder_item(filter.folder_path);
    auto* item = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
    item->setText(0, from_gmic(filter.name));
    item->setData(0, kFilterIdRole, QByteArray::fromStdString(filter.id));
  }
}

void GmicFilterLibraryDialog::set_search_text(const QString& text) {
  search_->setText(text);
}

void GmicFilterLibraryDialog::apply_search() {
  const QStringList words = search_->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  // Filters first, then each folder shows when any descendant does.
  for (QTreeWidgetItemIterator it(tree_); *it != nullptr; ++it) {
    auto* item = *it;
    if (!is_filter_item(item)) {
      continue;
    }
    QString haystack = item->text(0);
    for (auto* parent = item->parent(); parent != nullptr; parent = parent->parent()) {
      haystack += QLatin1Char(' ') + parent->text(0);
    }
    bool matches = true;
    for (const auto& word : words) {
      matches = matches && haystack.contains(word, Qt::CaseInsensitive);
    }
    item->setHidden(!matches);
  }
  const auto update_folder = [](auto&& self, QTreeWidgetItem* folder) -> bool {
    bool any = false;
    for (int index = 0; index < folder->childCount(); ++index) {
      auto* child = folder->child(index);
      any = (is_filter_item(child) ? !child->isHidden() : self(self, child)) || any;
    }
    folder->setHidden(!any);
    return any;
  };
  for (int index = 0; index < tree_->topLevelItemCount(); ++index) {
    auto* item = tree_->topLevelItem(index);
    if (!is_filter_item(item)) {
      update_folder(update_folder, item);
    }
  }
  if (words.isEmpty()) {
    tree_->collapseAll();
    if (auto* current = tree_->currentItem(); current != nullptr) {
      tree_->scrollToItem(current);
    }
  } else {
    tree_->expandAll();
  }
}

int GmicFilterLibraryDialog::visible_filter_count() const {
  int count = 0;
  for (QTreeWidgetItemIterator it(tree_); *it != nullptr; ++it) {
    if (is_filter_item(*it) && !(*it)->isHidden()) {
      ++count;
    }
  }
  return count;
}

void GmicFilterLibraryDialog::show_filter(const gmic::Filter* filter, const std::vector<std::string>& values) {
  current_ = filter;
  ok_->setEnabled(filter != nullptr);
  reset_->setEnabled(filter != nullptr);
  input_->setEnabled(filter != nullptr);
  {
    // Each filter starts from the input layers it declares.
    const QSignalBlocker blocker(input_);
    const int declared = input_->findData(QChar(QLatin1Char(filter != nullptr ? filter->input_mode : '.')));
    input_->setCurrentIndex(declared >= 0 ? declared : 0);
  }
  if (filter == nullptr) {
    title_->setText(tr("Select a filter"));
    folder_->clear();
    panel_->set_filter(gmic::Filter{});
    set_preview_message(QString());
    set_result_notes({});
    request_preview(true);
    return;
  }
  title_->setText(from_gmic(filter->name));
  QStringList path;
  for (const auto& name : filter->folder_path) {
    path << from_gmic(name);
  }
  folder_->setText(path.join(QStringLiteral(" / ")));
  panel_->set_filter(*filter, values);
  set_preview_message(QString());
  set_result_notes({});
  request_preview(true);
}

bool GmicFilterLibraryDialog::select_filter(const std::string& filter_id, const std::vector<std::string>& values,
                                            char input_mode) {
  const auto wanted = QByteArray::fromStdString(filter_id);
  for (QTreeWidgetItemIterator it(tree_); *it != nullptr; ++it) {
    if (is_filter_item(*it) && (*it)->data(0, kFilterIdRole).toByteArray() == wanted) {
      tree_->setCurrentItem(*it);
      tree_->scrollToItem(*it);
      bool restored = false;
      if (!values.empty()) {
        panel_->set_values(values);
        restored = true;
      }
      if (const int mode_index = input_mode != 0 ? input_->findData(QChar(QLatin1Char(input_mode))) : -1;
          mode_index >= 0 && mode_index != input_->currentIndex()) {
        const QSignalBlocker blocker(input_);
        input_->setCurrentIndex(mode_index);
        restored = true;
      }
      if (restored) {
        request_preview(true);
      }
      return current_ != nullptr && current_->id == filter_id;
    }
  }
  return false;
}

std::optional<GmicFilterChoice> GmicFilterLibraryDialog::choice() const {
  if (current_ == nullptr) {
    return std::nullopt;
  }
  return GmicFilterChoice{current_->id, panel_->values(), input_->currentData().toChar().toLatin1()};
}

}  // namespace patchy::ui
