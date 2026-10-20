#include "ui/missing_links_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/theme_qss.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {

MissingLinksDialogResult run_missing_links_dialog(
    QWidget* parent, const std::vector<MissingLinkEntry>& links, bool show_on_open,
    const std::function<std::optional<QString>(const MissingLinkEntry&)>& relink) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("missingLinksDialog"));
  dialog.setWindowTitle(QObject::tr("Missing Linked Files"));
  auto* layout = new QVBoxLayout(&dialog);

  auto* intro = new QLabel(QObject::tr("These linked smart objects point to files Patchy cannot find. Relink each "
                                       "one to a file now, or continue with the stored previews and use Layer > "
                                       "Smart Objects > Relink to File... later."),
                           &dialog);
  intro->setObjectName(QStringLiteral("missingLinksIntroLabel"));
  intro->setWordWrap(true);
  layout->addWidget(intro);

  MissingLinksDialogResult result;
  auto* rows_widget = new QWidget;
  rows_widget->setObjectName(QStringLiteral("missingLinksRows"));
  auto* grid = new QGridLayout(rows_widget);
  grid->setColumnStretch(1, 1);
  int row_count = 0;
  for (const auto& entry : links) {
    const auto index = row_count++;
    auto* relink_button = new QPushButton(QObject::tr("Relink..."), rows_widget);
    relink_button->setObjectName(QStringLiteral("missingLinkRelinkButton%1").arg(index));
    relink_button->setEnabled(static_cast<bool>(relink));
    grid->addWidget(relink_button, index, 0, Qt::AlignTop);

    auto* name = new QLabel(entry.file_name, rows_widget);
    name->setObjectName(QStringLiteral("missingLinkName%1").arg(index));
    name->setTextFormat(Qt::PlainText);
    name->setFont(offset_font(name->font(), 0, true));
    auto* detail = new QLabel(
        entry.layer_names.size() == 1
            ? QObject::tr("Used by layer \"%1\"").arg(entry.layer_names.front())
            : QObject::tr("Used by %n layers: %1", nullptr, static_cast<int>(entry.layer_names.size()))
                  .arg(entry.layer_names.join(QStringLiteral(", "))),
        rows_widget);
    detail->setObjectName(QStringLiteral("missingLinkDetail%1").arg(index));
    detail->setProperty("missingLinkSecondary", true);
    detail->setTextFormat(Qt::PlainText);
    detail->setWordWrap(true);
    auto* path = new QLabel(entry.stored_path, rows_widget);
    path->setObjectName(QStringLiteral("missingLinkPath%1").arg(index));
    path->setProperty("missingLinkSecondary", true);
    path->setTextFormat(Qt::PlainText);
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* column = new QVBoxLayout;
    column->setSpacing(1);
    column->addWidget(name);
    column->addWidget(detail);
    column->addWidget(path);
    grid->addLayout(column, index, 1, Qt::AlignTop);

    QObject::connect(relink_button, &QPushButton::clicked, &dialog,
                     [&relink, &entry, &result, relink_button, detail, path] {
      if (!relink) {
        return;
      }
      const auto relinked = relink(entry);
      if (!relinked.has_value()) {
        return;
      }
      ++result.relinked;
      relink_button->setEnabled(false);
      detail->setText(QObject::tr("Relinked to %1").arg(*relinked));
      path->hide();
    });
  }

  // Spare height goes below the rows, not between a row's lines.
  grid->setRowStretch(row_count, 1);

  auto* scroll = new QScrollArea(&dialog);
  scroll->setObjectName(QStringLiteral("missingLinksScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidget(rows_widget);
  // Up to four rows show without scrolling; a longer list scrolls inside the dialog.
  const auto row_height = std::max(1, rows_widget->sizeHint().height() / std::max(1, row_count));
  scroll->setMinimumHeight(std::min(rows_widget->sizeHint().height(), row_height * 4) + 4);
  layout->addWidget(scroll, 1);

  auto* dont_show = new QCheckBox(QObject::tr("Don't show when opening documents"), &dialog);
  dont_show->setObjectName(QStringLiteral("missingLinksDontShowCheck"));
  dont_show->setToolTip(QObject::tr("Missing files still show on the status bar. Preferences can turn this "
                                    "dialog back on for opened documents."));
  dont_show->setChecked(!show_on_open);
  layout->addWidget(dont_show);

  auto* buttons = new QDialogButtonBox(&dialog);
  auto* continue_button = buttons->addButton(QObject::tr("Continue"), QDialogButtonBox::AcceptRole);
  continue_button->setObjectName(QStringLiteral("missingLinksContinueButton"));
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  continue_button->setDefault(true);

  append_themed_style(dialog, QStringLiteral("QLabel[missingLinkSecondary=\"true\"] { color: @text_secondary; }"));
  dialog.resize(std::max(560, dialog.sizeHint().width()), dialog.sizeHint().height());
  remember_dialog_position(dialog);
  exec_dialog(dialog);
  result.show_on_open = !dont_show->isChecked();
  return result;
}

}  // namespace patchy::ui
