#include "ui/missing_fonts_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/font_picker.hpp"
#include "ui/font_replacement_memory.hpp"
#include "ui/text_font_service.hpp"
#include "ui/theme_qss.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include <algorithm>

namespace patchy::ui {
namespace {

// Why a built-in candidate is offered, for its row in the picker's "Suggested" section. The same
// reasons as the "Suggested: %1 (...)" lines.
QString candidate_reason(const MissingFontCandidate& candidate) {
  switch (candidate.kind) {
    case MissingFontSuggestion::metric:
      //: Beside a suggested font in the Missing Fonts dialog's font list: why it is suggested.
      return QObject::tr("same character widths");
    case MissingFontSuggestion::lookalike:
      //: Beside a suggested font in the Missing Fonts dialog's font list: why it is suggested.
      return QObject::tr("similar design");
    case MissingFontSuggestion::same_type:
      if (candidate.font_type == FontClass::serif) {
        //: Beside a suggested font in the Missing Fonts dialog's font list: why it is suggested.
        return QObject::tr("also a serif font");
      }
      if (candidate.font_type == FontClass::sans) {
        //: Beside a suggested font in the Missing Fonts dialog's font list: why it is suggested.
        return QObject::tr("also a sans-serif font");
      }
      if (candidate.font_type == FontClass::mono) {
        //: Beside a suggested font in the Missing Fonts dialog's font list: why it is suggested.
        return QObject::tr("also a monospaced font");
      }
      break;
    case MissingFontSuggestion::no_symbol_match:
    case MissingFontSuggestion::fallback:
      break;
  }
  return {};
}

// The two short sections on top of a row's font list (GitHub issue 692): the fonts chosen for this
// missing font before, most often chosen first, then every built-in candidate with its reason,
// leaving out the ones already listed. The picker drops a section with nothing to show.
std::vector<FontPickerSection> picker_sections(const MissingFontEntry& entry) {
  //: Heading in the Missing Fonts dialog's font list: fonts the user picked for this missing font earlier.
  FontPickerSection chosen{QObject::tr("Chosen before"), {}};
  for (const auto& family : entry.chosen_before_families) {
    chosen.rows.push_back(FontPickerSectionRow{family, QString()});
  }
  // The row's remove button forgets that pick for this missing font (issue 692 follow-up). The
  // row lists the installed family a remembered name matched, so every remembered spelling of it
  // goes.
  chosen.remove = [missing = entry.family](const QString& family) {
    for (const auto& pick : remembered_font_replacements(missing)) {
      if (pick.family.compare(family, Qt::CaseInsensitive) == 0 ||
          available_text_family_match(pick.family) == std::optional<QString>(family)) {
        forget_font_replacement(missing, pick.family);
      }
    }
  };
  //: Tooltip of the x on a row of the Missing Fonts dialog's Chosen before font list section.
  chosen.remove_tooltip = QObject::tr("Remove from Chosen before");
  //: Heading in the Missing Fonts dialog's font list: fonts Patchy suggests for this missing font.
  FontPickerSection suggested{QObject::tr("Suggested"), {}};
  for (const auto& candidate : entry.candidates) {
    if (!entry.chosen_before_families.contains(candidate.family, Qt::CaseInsensitive)) {
      suggested.rows.push_back(FontPickerSectionRow{candidate.family, candidate_reason(candidate)});
    }
  }
  return {std::move(chosen), std::move(suggested)};
}

}  // namespace

std::optional<MissingFontsDialogResult> run_missing_fonts_dialog(QWidget* parent,
                                                                 const std::vector<MissingFontEntry>& fonts,
                                                                 bool show_on_open, bool* show_on_open_out) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("missingFontsDialog"));
  dialog.setWindowTitle(QObject::tr("Missing Fonts"));
  auto* layout = new QVBoxLayout(&dialog);

  auto* intro = new QLabel(QObject::tr("Some fonts used in this document are missing on this computer. Choose an "
                                       "installed font to replace each one in every text layer that uses it, or "
                                       "leave it unresolved."),
                           &dialog);
  intro->setObjectName(QStringLiteral("missingFontsIntroLabel"));
  intro->setWordWrap(true);
  layout->addWidget(intro);

  auto* rows_widget = new QWidget;
  rows_widget->setObjectName(QStringLiteral("missingFontsRows"));
  auto* grid = new QGridLayout(rows_widget);
  grid->setColumnStretch(0, 1);
  struct Row {
    const MissingFontEntry* entry;
    QCheckBox* replace;
    FontPickerCombo* picker;
    bool picked_by_hand{false};  // the picker's activated signal: a pick in its list or a typed name
  };
  std::vector<Row> rows;
  // Enabled while at least one row is ticked (issue 463 follow-up). Cancel is the way to keep
  // every font; it still saves the "Don't show" box (issue 169 follow-up).
  auto* resolve_button = new QPushButton(QObject::tr("Resolve Fonts"), &dialog);
  resolve_button->setObjectName(QStringLiteral("missingFontsResolveButton"));
  const auto sync_resolve_button = [&rows, resolve_button] {
    resolve_button->setEnabled(std::ranges::any_of(rows, [](const Row& row) { return row.replace->isChecked(); }));
  };
  for (const auto& entry : fonts) {
    const auto index = static_cast<int>(rows.size());
    // Rows sit on even grid rows; a divider line fills the odd row between two of them.
    const auto grid_row = index * 2;
    if (index > 0) {
      auto* divider = new QFrame(rows_widget);
      divider->setObjectName(QStringLiteral("missingFontsRowDivider"));
      divider->setFrameShape(QFrame::HLine);
      divider->setFrameShadow(QFrame::Plain);
      grid->addWidget(divider, grid_row - 1, 0, 1, 3);
    }
    // The name in bold, a style in brackets after its family, split as the options bar splits a
    // missing font (GitHub issue 669): "Museo Sans Rounded (500)". One label, so a right-to-left
    // layout keeps the name in reading order. Display only: the entry, its suggestion and the
    // replacement keep the whole name, which the label carries as kMissingFontFullNameProperty.
    const auto parts = split_font_family_and_style(entry.family.trimmed().toStdString());
    const auto style_text = QString::fromStdString(parts.style);
    auto* name = new QLabel(
        style_text.isEmpty() ? entry.family
                             : QStringLiteral("%1 (%2)").arg(QString::fromStdString(parts.family), style_text),
        rows_widget);
    name->setObjectName(QStringLiteral("missingFontName%1").arg(index));
    name->setProperty(kMissingFontFullNameProperty, entry.family);
    name->setTextFormat(Qt::PlainText);
    name->setFont(offset_font(name->font(), 0, true));
    auto* name_line = new QHBoxLayout;
    name_line->setSpacing(6);
    name_line->addWidget(name);
    name_line->addStretch(1);
    auto* detail = new QLabel(entry.no_glyphs
                                  ? QObject::tr("Installed, but has no glyphs for this text. Used in %n text layer(s).",
                                                nullptr, entry.layer_count)
                                  : QObject::tr("Used in %n text layer(s).", nullptr, entry.layer_count),
                              rows_widget);
    detail->setObjectName(QStringLiteral("missingFontDetail"));
    // One line: wrapped, the name column shrank until the count broke across lines.
    auto* name_column = new QVBoxLayout;
    name_column->setSpacing(1);
    name_column->addLayout(name_line);
    name_column->addWidget(detail);
    // A substitute from the built-in table or a same-type default (issue 463) says how close it
    // is; the picker starts on it.
    QString suggestion_text;
    switch (entry.suggestion) {
      case MissingFontSuggestion::metric:
        suggestion_text = QObject::tr("Suggested: %1 (same character widths)");
        break;
      case MissingFontSuggestion::lookalike:
        suggestion_text = QObject::tr("Suggested: %1 (similar design)");
        break;
      case MissingFontSuggestion::same_type:
        if (entry.font_type == FontClass::serif) {
          suggestion_text = QObject::tr("Suggested: %1 (also a serif font)");
        } else if (entry.font_type == FontClass::sans) {
          suggestion_text = QObject::tr("Suggested: %1 (also a sans-serif font)");
        } else if (entry.font_type == FontClass::mono) {
          suggestion_text = QObject::tr("Suggested: %1 (also a monospaced font)");
        }
        break;
      case MissingFontSuggestion::no_symbol_match:
      case MissingFontSuggestion::fallback:
        break;
    }
    // A font chosen for this missing font before (GitHub issue 692) is where the picker starts, and
    // the line says so in place of the suggestion. That holds for a symbol font too: the pick was
    // made by hand, so the row starts ticked on it.
    const bool no_symbol_match =
        entry.suggestion == MissingFontSuggestion::no_symbol_match && !entry.chosen_before;
    if (entry.chosen_before && !entry.suggested.isEmpty()) {
      auto* chosen = new QLabel(QObject::tr("Chosen before: %1").arg(entry.suggested), rows_widget);
      chosen->setObjectName(QStringLiteral("missingFontChosenBefore%1").arg(index));
      chosen->setProperty("patchyMissingFontSuggestion", true);
      chosen->setTextFormat(Qt::PlainText);
      name_column->addWidget(chosen);
    } else if (no_symbol_match) {
      auto* note = new QLabel(QObject::tr("Symbol font: no installed font has the same symbols"), rows_widget);
      note->setObjectName(QStringLiteral("missingFontNoSymbolMatch%1").arg(index));
      note->setProperty("patchyMissingFontSuggestion", true);
      note->setTextFormat(Qt::PlainText);
      name_column->addWidget(note);
    } else if (!suggestion_text.isEmpty() && !entry.suggested.isEmpty()) {
      auto* suggestion = new QLabel(suggestion_text.arg(entry.suggested), rows_widget);
      suggestion->setObjectName(QStringLiteral("missingFontSuggestion%1").arg(index));
      suggestion->setProperty("patchyMissingFontSuggestion", true);
      suggestion->setTextFormat(Qt::PlainText);
      name_column->addWidget(suggestion);
    }
    grid->addLayout(name_column, grid_row, 0);

    // Ticked by default (issue 463 follow-up, Seth): Resolve Fonts replaces every listed font with
    // the picker's choice (the font chosen before, else the suggested stand-in, else the face
    // drawing the text now). Unticking
    // a row, or Cancel, keeps the file naming the font, so a machine that has it shows the original.
    // A symbol font with no copy installed starts unticked: the user may still pick one by hand.
    auto* replace = new QCheckBox(QObject::tr("Replace with:"), rows_widget);
    replace->setObjectName(QStringLiteral("missingFontReplaceCheck%1").arg(index));
    replace->setChecked(!no_symbol_match);
    grid->addWidget(replace, grid_row, 1, Qt::AlignVCenter);
    auto* picker = new FontPickerCombo(rows_widget);
    picker->setObjectName(QStringLiteral("missingFontReplacementCombo%1").arg(index));
    picker->setMinimumWidth(220);
    picker->set_extra_sections(picker_sections(entry));
    if (!entry.suggested.isEmpty()) {
      picker->setCurrentFont(QFont(entry.suggested));
    }
    picker->setEnabled(replace->isChecked());
    grid->addWidget(picker, grid_row, 2, Qt::AlignVCenter);
    QObject::connect(replace, &QCheckBox::toggled, picker, &QWidget::setEnabled);
    QObject::connect(replace, &QCheckBox::toggled, &dialog, [&sync_resolve_button](bool) { sync_resolve_button(); });
    // Every pick by hand counts, the font the row started on included (issue 692 follow-up), so
    // Resolve Fonts remembers it; setting the font from code does not emit this.
    QObject::connect(picker, &QComboBox::activated, &dialog, [&rows, index](int) {
      rows[static_cast<std::size_t>(index)].picked_by_hand = true;
    });
    rows.push_back(Row{&entry, replace, picker});
  }

  auto* scroll = new QScrollArea(&dialog);
  scroll->setObjectName(QStringLiteral("missingFontsScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidget(rows_widget);
  // Wide enough for the widest row. The list never scrolls sideways, so a narrower one cut the
  // pickers off at the right when a row's "Suggested:" line was long.
  scroll->setMinimumWidth(rows_widget->minimumSizeHint().width() +
                          (rows.size() > 5U ? scroll->verticalScrollBar()->sizeHint().width() : 0));
  // Up to five rows show without scrolling; a longer list scrolls inside the dialog.
  const auto row_height = std::max(1, rows_widget->sizeHint().height() / std::max<int>(1, static_cast<int>(rows.size())));
  scroll->setMinimumHeight(std::min(rows_widget->sizeHint().height(), row_height * 5) + 4);
  layout->addWidget(scroll, 1);

  // The label names the way back itself: a tooltip alone went unseen (issue 169 follow-up).
  auto* dont_show = new QCheckBox(
      QObject::tr("Don't show when opening documents (Preferences > Application turns it back on)"), &dialog);
  dont_show->setObjectName(QStringLiteral("missingFontsDontShowCheck"));
  dont_show->setToolTip(QObject::tr("Type > Resolve Missing Fonts opens this dialog at any time. Preferences can "
                                    "turn it back on for opened documents."));
  dont_show->setChecked(!show_on_open);
  layout->addWidget(dont_show);

  auto* buttons = new QDialogButtonBox(&dialog);
  buttons->addButton(resolve_button, QDialogButtonBox::AcceptRole);
  auto* cancel_button = buttons->addButton(QDialogButtonBox::Cancel);
  cancel_button->setObjectName(QStringLiteral("missingFontsCancelButton"));
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  resolve_button->setDefault(true);
  sync_resolve_button();

  append_themed_style(dialog, QStringLiteral("QLabel#missingFontDetail, QLabel[patchyMissingFontSuggestion=\"true\"] { "
                                             "color: @text_secondary; }"
                                             "QFrame#missingFontsRowDivider { background: @dlg_divider; color: "
                                             "@dlg_divider; min-height: 1px; max-height: 1px; border: 0; }"));
  dialog.resize(std::max(560, dialog.sizeHint().width()), dialog.sizeHint().height());
  remember_dialog_position(dialog);
  const auto accepted = exec_dialog(dialog) == QDialog::Accepted;
  if (show_on_open_out != nullptr) {
    *show_on_open_out = !dont_show->isChecked();
  }
  if (!accepted) {
    return std::nullopt;
  }
  MissingFontsDialogResult result;
  result.show_on_open = !dont_show->isChecked();
  for (const auto& row : rows) {
    if (!row.replace->isChecked()) {
      continue;
    }
    const auto chosen = row.picker->currentFont().family();
    if (!chosen.trimmed().isEmpty()) {
      const auto key = text_font_replacement_key(row.entry->family);
      result.replacements.insert(key, chosen);
      if (row.picked_by_hand) {
        result.picked_by_hand.insert(key);
      }
    }
  }
  return result;
}

}  // namespace patchy::ui
