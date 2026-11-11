#include "ui/font_picker.hpp"

#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/font_display_names.hpp"
#include "ui/font_face_name_index.hpp"
#include "ui/font_family_filters.hpp"
#include "ui/theme_palette.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QAbstractListModel>
#include <QApplication>
#include <QCloseEvent>
#include <QCollator>
#include <QColor>
#include <QComboBox>
#include <QCursor>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QIcon>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QStringListModel>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace patchy::ui {

namespace {

const QString kPopupSizeSettingsKey = QStringLiteral("ui/textFontPickerPopupSize");

// Sample-line order: the scripts a user most likely hunts a font for come first; Symbol sits
// last among these so dingbat coverage never crowds out a real language.
constexpr QFontDatabase::WritingSystem kWritingSystemPriority[] = {
    QFontDatabase::Latin,   QFontDatabase::Japanese,   QFontDatabase::Korean,
    QFontDatabase::SimplifiedChinese, QFontDatabase::TraditionalChinese,
    QFontDatabase::Cyrillic, QFontDatabase::Greek,     QFontDatabase::Arabic,
    QFontDatabase::Hebrew,  QFontDatabase::Thai,       QFontDatabase::Devanagari,
    QFontDatabase::Vietnamese, QFontDatabase::Symbol,
};

constexpr int kMaxScriptPreviewLines = 3;  // script sample lines below the specimen + pangram
constexpr int kPreviewPaneHeight = 136;
constexpr int kMaxRowSampleChars = 9;

// Copies `source`'s size onto `font`, which carries its own family. The preview
// fonts are built from a family name and then sized from the UI font, and a
// QFont answers only for the unit it was defined in (see dialog_utils.hpp), so
// the setter has to follow the source rather than always being setPointSizeF.
void apply_font_size(QFont& font, const QFont& source) {
  if (source.pixelSize() > 0) {
    font.setPixelSize(source.pixelSize());
  } else if (source.pointSizeF() > 0.0) {
    font.setPointSizeF(source.pointSizeF());
  }
}

// Readable per-script samples: the language name written in itself plus a short real
// phrase. Qt's writingSystemSample strings are 3-4 characters chosen to probe font
// engines ("Джя"), useless as human specimens. Deliberately untranslated: native-script
// sample text is the same in every UI language. Uncurated scripts fall back to Qt's sample.
[[nodiscard]] QString curated_writing_system_sample(QFontDatabase::WritingSystem ws) {
  switch (ws) {
    case QFontDatabase::Japanese:
      return QStringLiteral("日本語 かなカナ漢字のサンプル");
    case QFontDatabase::Korean:
      return QStringLiteral("한국어 다람쥐 헌 쳇바퀴에 타고파");
    case QFontDatabase::SimplifiedChinese:
      return QStringLiteral("简体中文 字体样本");
    case QFontDatabase::TraditionalChinese:
      return QStringLiteral("繁體中文 字體樣本");
    case QFontDatabase::Arabic:
      return QStringLiteral("العربية أبجد هوز حطي");
    case QFontDatabase::Hebrew:
      return QStringLiteral("עברית דג סקרן שט בים");
    case QFontDatabase::Thai:
      return QStringLiteral("ไทย สวัสดีครับ");
    case QFontDatabase::Devanagari:
      return QStringLiteral("हिन्दी नमस्ते संसार");
    case QFontDatabase::Bengali:
      return QStringLiteral("বাংলা নমস্কার");
    case QFontDatabase::Tamil:
      return QStringLiteral("தமிழ் வணக்கம்");
    case QFontDatabase::Cyrillic:
      return QStringLiteral("Русский язык");
    case QFontDatabase::Greek:
      return QStringLiteral("Ελληνικά αλφάβητο");
    case QFontDatabase::Armenian:
      return QStringLiteral("Հայերեն");
    case QFontDatabase::Georgian:
      return QStringLiteral("ქართული");
    case QFontDatabase::Vietnamese:
      return QStringLiteral("Tiếng Việt");
    default:
      return {};
  }
}

[[nodiscard]] QString writing_system_sample(QFontDatabase::WritingSystem ws) {
  const auto curated = curated_writing_system_sample(ws);
  return curated.isEmpty() ? QFontDatabase::writingSystemSample(ws) : curated;
}

// Scripts worth a full sample line even on a Latin font (what people hunt fonts for).
// The Latin-adjacent European set only clutters: a wide Latin font almost always covers
// Greek/Cyrillic/Vietnamese, so those collapse into the "Also supports" footer instead.
[[nodiscard]] bool is_specimen_script(QFontDatabase::WritingSystem ws) {
  switch (ws) {
    case QFontDatabase::Latin:
    case QFontDatabase::Greek:
    case QFontDatabase::Cyrillic:
    case QFontDatabase::Armenian:
    case QFontDatabase::Georgian:
    case QFontDatabase::Vietnamese:
    case QFontDatabase::Runic:
    case QFontDatabase::Ogham:
      return false;
    default:
      return true;
  }
}

// Keeps only the characters the font itself can render. The font must carry NoFontMerging:
// with merging on, Qt answers for the whole fallback chain and every font looks omniscient.
[[nodiscard]] QString renderable_subset(const QFont& font, const QString& text) {
  const QFontMetrics metrics(font);
  QString out;
  for (const auto& ch : text) {
    if (ch.isSpace()) {
      out.append(ch);
      continue;
    }
    if (!ch.isSurrogate() && metrics.inFontUcs4(ch.unicode())) {
      out.append(ch);
    }
  }
  return out;
}

[[nodiscard]] int glyph_count(const QString& text) {
  return static_cast<int>(
      std::count_if(text.begin(), text.end(), [](QChar ch) { return !ch.isSpace(); }));
}

// Symbol fonts (Wingdings, Webdings) map ASCII codes onto their glyphs, so plain letters
// render as the font's actual dingbats; fall back to Qt's Symbol sample characters.
[[nodiscard]] QString symbol_font_sample(const QFont& sample_font) {
  const auto ascii = renderable_subset(sample_font, QStringLiteral("ABCDEFGH abcdefgh 123"));
  if (glyph_count(ascii) >= 6) {
    return ascii;
  }
  const auto sample =
      renderable_subset(sample_font, QFontDatabase::writingSystemSample(QFontDatabase::Symbol));
  if (glyph_count(sample) >= 3) {
    return sample;
  }
  return {};
}

// The picker popup is a frameless Qt::Popup, so resizing happens through an embedded
// QSizeGrip; the chosen size persists across openings (and launches) via app settings.
class SizePersistingPopupFrame : public QFrame {
public:
  using QFrame::QFrame;

protected:
  void closeEvent(QCloseEvent* event) override {
    auto settings = app_settings();
    settings.setValue(kPopupSizeSettingsKey, size());
    QFrame::closeEvent(event);
  }
};

[[nodiscard]] bool is_family_row(const QModelIndex& index) {
  return index.isValid() &&
         index.data(kFontPickerRowKindRole).toInt() == static_cast<int>(FontPickerRowKind::family);
}

// Moves the list's current row to the next family row in `direction` (+1 down, -1 up), skipping
// the section headers. Reaching the first family scrolls the Recent header back into view.
bool step_to_family_row(QAbstractItemView& list, int start_row, int direction) {
  auto* model = list.model();
  for (int row = start_row + direction; row >= 0 && row < model->rowCount(); row += direction) {
    const auto index = model->index(row, 0);
    if (is_family_row(index)) {
      list.setCurrentIndex(index);
      list.scrollTo(index);
      if (direction < 0 && row <= 1) {
        list.scrollToTop();
      }
      return true;
    }
  }
  return false;
}

// Forwards list-navigation keys from the search edit to the list view (the list itself is
// Qt::NoFocus so the user can type and arrow through results without moving focus). Up and Down
// step over the section headers; a page jump that lands on one moves on to the next family.
class SearchKeyForwarder : public QObject {
public:
  SearchKeyForwarder(QAbstractItemView* list, QObject* parent) : QObject(parent), list_(list) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::KeyPress) {
      const auto* key = static_cast<const QKeyEvent*>(event);
      const auto current = list_->currentIndex();
      switch (key->key()) {
        case Qt::Key_Up:
          step_to_family_row(*list_, current.isValid() ? current.row() : list_->model()->rowCount(), -1);
          return true;
        case Qt::Key_Down:
          step_to_family_row(*list_, current.isValid() ? current.row() : -1, 1);
          return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
          // A filter combo's list open above the font popup owns Enter: hand it over rather
          // than pick a font and close everything (GitHub issue 570 follow-up).
          if (auto* active = QApplication::activePopupWidget(); active != nullptr && active != list_->window()) {
            QApplication::sendEvent(active->focusWidget() != nullptr ? active->focusWidget() : active, event);
            return true;
          }
          break;
        case Qt::Key_PageUp:
        case Qt::Key_PageDown: {
          QApplication::sendEvent(list_, event);
          const auto landed = list_->currentIndex();
          if (landed.isValid() && !is_family_row(landed)) {
            const int direction = key->key() == Qt::Key_PageUp ? -1 : 1;
            if (!step_to_family_row(*list_, landed.row(), direction)) {
              step_to_family_row(*list_, landed.row(), -direction);
            }
          }
          return true;
        }
        default:
          break;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QAbstractItemView* list_;
};

// Enter or Return in a filter combo's open list picks the row and closes only that list. The
// stock container's handling let the key reach the font popup, which then closed as if a font
// had been picked (GitHub issue 570 follow-up).
class ComboListEnterGuard : public QObject {
public:
  explicit ComboListEnterGuard(QComboBox& combo) : QObject(&combo), combo_(combo) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
      const auto key = static_cast<const QKeyEvent*>(event)->key();
      if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (event->type() == QEvent::KeyPress) {
          const auto current = combo_.view()->currentIndex();
          if (current.isValid() && (current.flags() & Qt::ItemIsEnabled) != 0) {
            combo_.setCurrentIndex(current.row());
          }
          combo_.hidePopup();
        }
        event->accept();
        return true;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QComboBox& combo_;
};

struct FontListRow {
  FontPickerRowKind kind{FontPickerRowKind::family};
  QString text;  // the family, or the header's or note's text
  bool recent{false};
  int extra_section{-1};  // a family row of FontPickerCombo::extra_sections()[n]
  QString note{};         // that row's dim note beside the name
  bool removable{false};  // an extra section's row with the remove button (Recent rows always have it)
};

// Where a family row sits, so a rebuild can put the selection back in the same place: the full
// list, the Recent section, or extra section n (kFirstExtraSectionZone + n).
constexpr int kFullListZone = -1;
constexpr int kRecentZone = 0;
constexpr int kFirstExtraSectionZone = 1;

[[nodiscard]] int row_zone(const QModelIndex& index) {
  if (!index.isValid()) {
    return kFullListZone;
  }
  if (const int section = index.data(kFontPickerExtraSectionRole).toInt(); section >= 0) {
    return kFirstExtraSectionZone + section;
  }
  return index.data(kFontPickerRecentRowRole).toBool() ? kRecentZone : kFullListZone;
}

// A five-pointed star centered on `center`, its points on a circle of `radius`.
[[nodiscard]] QPainterPath star_path(const QPointF& center, qreal radius) {
  QPainterPath path;
  constexpr double kPi = 3.14159265358979323846;
  const qreal inner = radius * 0.42;
  for (int point = 0; point < 10; ++point) {
    const qreal r = point % 2 == 0 ? radius : inner;
    const double angle = -kPi / 2.0 + point * kPi / 5.0;
    const QPointF at(center.x() + r * std::cos(angle), center.y() + r * std::sin(angle) + radius * 0.08);
    if (point == 0) {
      path.moveTo(at);
    } else {
      path.lineTo(at);
    }
  }
  path.closeSubpath();
  return path;
}

// Paints the favorite star: filled in font_favorite_star for a favorite, else an outline in `ink`.
void paint_star(QPainter& painter, const QRectF& box, bool filled, const QColor& ink) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto path = star_path(box.center(), std::min(box.width(), box.height()) * 0.42);
  if (filled) {
    painter.setPen(QPen(theme().font_favorite_star, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(theme().font_favorite_star);
  } else {
    painter.setPen(QPen(ink, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
  }
  painter.drawPath(path);
  painter.restore();
}

// The row button under the mouse, shared by the list's mouse handling and the delegate that
// draws the buttons (the hovered one in brighter ink).
struct RowButtonHover {
  int row{-1};
  FontPickerRowButton button{FontPickerRowButton::none};
};

// The popup's rows, rebuilt whenever the search text or a filter changes.
class FontListModel : public QAbstractListModel {
public:
  using QAbstractListModel::QAbstractListModel;

  void set_rows(std::vector<FontListRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
  }

  void set_favorites(const QSet<QString>* favorites) noexcept { favorites_ = favorites; }
  void set_display_names(const FontDisplayNames* names) noexcept { names_ = names; }

  // Repaints `family`'s rows after its favorite state changed.
  void favorite_changed(const QString& family) {
    for (int row = 0; row < static_cast<int>(rows_.size()); ++row) {
      const auto& entry = rows_[static_cast<std::size_t>(row)];
      if (entry.kind == FontPickerRowKind::family && entry.text == family) {
        const auto changed = index(row, 0);
        emit dataChanged(changed, changed, {kFontPickerFavoriteRole});
      }
    }
  }

  int rowCount(const QModelIndex& parent = QModelIndex()) const override {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
  }

  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid() || index.row() >= static_cast<int>(rows_.size())) {
      return {};
    }
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
      case Qt::DisplayRole:
        return row.text;
      case kFontPickerShownNameRole:
        return row.kind == FontPickerRowKind::family && names_ != nullptr ? names_->display_name(row.text) : row.text;
      case kFontPickerRowKindRole:
        return static_cast<int>(row.kind);
      case kFontPickerRecentRowRole:
        return row.recent;
      case kFontPickerExtraSectionRole:
        return row.kind == FontPickerRowKind::family ? row.extra_section : -1;
      case kFontPickerRowNoteRole:
        return row.note;
      case kFontPickerRowRemovableRole:
        return row.kind == FontPickerRowKind::family && (row.recent || row.removable);
      case kFontPickerFavoriteRole:
        return row.kind == FontPickerRowKind::family && favorites_ != nullptr && favorites_->contains(row.text);
      default:
        return {};
    }
  }

  Qt::ItemFlags flags(const QModelIndex& index) const override {
    if (!index.isValid() || index.row() >= static_cast<int>(rows_.size())) {
      return Qt::NoItemFlags;
    }
    return rows_[static_cast<std::size_t>(index.row())].kind == FontPickerRowKind::family
               ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren
               : Qt::ItemNeverHasChildren;
  }

private:
  std::vector<FontListRow> rows_;
  const QSet<QString>* favorites_{nullptr};
  const FontDisplayNames* names_{nullptr};
};

class FontListDelegate : public QStyledItemDelegate {
public:
  FontListDelegate(FontPickerCombo& combo, const RowButtonHover& hover, QObject* parent)
      : QStyledItemDelegate(parent), combo_(combo), hover_(hover) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    if (!is_family_row(index)) {
      paint_header(painter, option, index);
      return;
    }
    const auto family = index.data(Qt::DisplayRole).toString();
    // Copy: the cache reference is only stable until the next lookup, and QFont/QString
    // copies are shared-data cheap.
    const auto info = combo_.family_render_info(family);
    painter->save();
    const auto rect = option.rect;
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    if (selected) {
      painter->fillRect(rect, theme().list_selection_bg);
      painter->setPen(theme().list_selection_border);
      painter->drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
    } else if ((option.state & QStyle::State_MouseOver) != 0) {
      painter->fillRect(rect, theme().list_row_hover_bg);
    }
    auto text_rect = rect.adjusted(8, 0, -8, 0);
    // The row buttons (GitHub issues 630 and 569). Their space is kept on every row, so a name
    // never re-elides when the mouse arrives; a favorite's star shows always, the rest on hover.
    const bool removable = index.data(kFontPickerRowRemovableRole).toBool();
    const auto star_rect = font_picker_row_button_rect(rect, FontPickerRowButton::favorite, removable);
    text_rect.setRight(star_rect.left() - 4);
    const bool hovered = (option.state & QStyle::State_MouseOver) != 0;
    const bool favorite = index.data(kFontPickerFavoriteRole).toBool();
    const auto button_ink = [&](FontPickerRowButton button) {
      if (selected) {
        return theme().list_selection_text;
      }
      return hover_.row == index.row() && hover_.button == button ? theme().text_primary : theme().text_secondary;
    };
    if (favorite || hovered) {
      paint_star(*painter, QRectF(star_rect), favorite, button_ink(FontPickerRowButton::favorite));
    }
    if (removable && hovered) {
      const QRectF glyph = QRectF(font_picker_row_button_rect(rect, FontPickerRowButton::remove_recent, true))
                               .adjusted(6.0, 6.0, -6.0, -6.0);
      painter->setRenderHint(QPainter::Antialiasing, true);
      painter->setPen(QPen(button_ink(FontPickerRowButton::remove_recent), 1.5, Qt::SolidLine, Qt::RoundCap));
      painter->drawLine(glyph.topLeft(), glyph.bottomRight());
      painter->drawLine(glyph.topRight(), glyph.bottomLeft());
      painter->setRenderHint(QPainter::Antialiasing, false);
    }
    if (const auto note = index.data(kFontPickerRowNoteRole).toString(); !note.isEmpty()) {
      // An extra section's note (GitHub issue 692: why a font is suggested), dim in the UI font at
      // the right of the row; the name keeps at least two fifths of the width.
      const QFont note_font = scaled_font(combo_.font(), 0.9);
      const QFontMetrics note_metrics(note_font);
      const auto shown = note_metrics.elidedText(note, Qt::ElideRight, text_rect.width() * 3 / 5);
      const auto note_width = note_metrics.horizontalAdvance(shown);
      painter->setFont(note_font);
      painter->setPen(selected ? theme().list_selection_text : theme().text_secondary);
      painter->drawText(QRect(text_rect.right() - note_width, text_rect.top(), note_width, text_rect.height()),
                        Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, shown);
      text_rect.setWidth(text_rect.width() - note_width - 12);
    } else if (!info.row_sample.isEmpty()) {
      // Symbol-class family: readable name in the UI font, its actual glyphs beside it.
      const QFontMetrics sample_metrics(info.sample_font);
      const auto sample =
          sample_metrics.elidedText(info.row_sample, Qt::ElideRight, text_rect.width() / 2);
      const auto sample_width = sample_metrics.horizontalAdvance(sample);
      painter->setFont(info.sample_font);
      painter->setPen(theme().script_detail_text);
      painter->drawText(
          QRect(text_rect.right() - sample_width, text_rect.top(), sample_width, text_rect.height()),
          Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, sample);
      text_rect.setWidth(text_rect.width() - sample_width - 12);
    }
    painter->setFont(info.display_font);
    painter->setPen(selected ? theme().list_selection_text : theme().text_primary);
    const QFontMetrics metrics(info.display_font);
    painter->drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                      metrics.elidedText(index.data(kFontPickerShownNameRole).toString(), Qt::ElideRight,
                                         text_rect.width()));
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    Q_UNUSED(option);
    Q_UNUSED(index);
    // The list uses uniformItemSizes, so the first row's hint sizes every row: return a fixed
    // height instead of per-family metrics (a family with a huge line height would otherwise
    // inflate every row).
    const QFont probe = scaled_font(combo_.font(), 4.0 / 3.0);
    const auto height = std::clamp(QFontMetrics(probe).height() + 6, 22, 34);
    return QSize(200, height);
  }

private:
  // A section header: a small dim label; "All Fonts", and every other header below the first row,
  // also draws the rule that separates it from the section above. The Favorites view's empty note
  // is a dim line in the UI font.
  void paint_header(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    const auto rect = option.rect;
    if (index.data(kFontPickerRowKindRole).toInt() == static_cast<int>(FontPickerRowKind::empty_note)) {
      painter->setFont(combo_.font());
      painter->setPen(theme().text_secondary);
      painter->drawText(rect.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                        QFontMetrics(combo_.font()).elidedText(index.data(Qt::DisplayRole).toString(),
                                                               Qt::ElideRight, rect.width() - 16));
      painter->restore();
      return;
    }
    if (index.data(kFontPickerRowKindRole).toInt() == static_cast<int>(FontPickerRowKind::all_header) ||
        index.row() > 0) {
      // field_border (the gray around fields and combos), not the list frame's
      // field_inset_border: in Dark that one sits a shade off the list background and the rule
      // vanished (GitHub issue 569 follow-up). menu_separator is too faint in Light.
      painter->setPen(theme().field_border);
      painter->drawLine(rect.left() + 4, rect.top() + 2, rect.right() - 4, rect.top() + 2);
    }
    const QFont label_font = scaled_font(combo_.font(), 0.9);
    painter->setFont(label_font);
    painter->setPen(theme().text_secondary);
    painter->drawText(rect.adjusted(8, 2, -8, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                      QFontMetrics(label_font).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                                          rect.width() - 16));
    painter->restore();
  }

  FontPickerCombo& combo_;
  const RowButtonHover& hover_;
};

[[nodiscard]] QString style_filter_label(FontStyleFilter filter) {
  switch (filter) {
    case FontStyleFilter::all:
      return FontPickerCombo::tr("All Styles");
    case FontStyleFilter::serif:
      return FontPickerCombo::tr("Serif");
    case FontStyleFilter::sans_serif:
      return FontPickerCombo::tr("Sans Serif");
    case FontStyleFilter::slab_serif:
      return FontPickerCombo::tr("Slab Serif");
    case FontStyleFilter::script:
      return FontPickerCombo::tr("Script");
    case FontStyleFilter::decorative:
      return FontPickerCombo::tr("Decorative");
    case FontStyleFilter::symbol:
      return FontPickerCombo::tr("Symbol");
    case FontStyleFilter::monospace:
      return FontPickerCombo::tr("Monospace");
    case FontStyleFilter::variable:
      //: Variable fonts (one font file with adjustable weight or width axes).
      return FontPickerCombo::tr("Variable");
    case FontStyleFilter::other:
      //: Fonts whose files state no style (none of the entries above).
      return FontPickerCombo::tr("Other");
  }
  return {};
}

// Owns the popup's row model and the Style/Language filtering. Families a table-reading Style
// filter has not classified yet are read in short timer slices (FontStyleIndex caches them on
// disk), so opening the popup never waits for every installed font to load.
class FontListFilter : public QObject {
public:
  FontListFilter(FontPickerCombo& combo, QListView& list, QLineEdit& search, QComboBox& style,
                 QComboBox& language, QLabel& status, QToolButton& hidden_matches, QObject* parent)
      : QObject(parent),
        combo_(combo),
        list_(list),
        search_(search),
        style_(style),
        language_(language),
        status_(status),
        hidden_matches_(hidden_matches),
        names_(font_display_names()) {
    model_ = new FontListModel(this);
    auto* source = combo.model();
    // One row per family: the combo's model holds a row per foundry copy (GitHub issue 646).
    QStringList listed;
    listed.reserve(source->rowCount());
    for (int row = 0; row < source->rowCount(); ++row) {
      listed.append(source->index(row, 0).data(Qt::DisplayRole).toString());
    }
    // Ordered by the names the rows show (GitHub issue 691); Qt's order when none differs.
    families_ = names_.sorted(font_families_without_foundries(listed));
    const QSet<QString> installed(families_.cbegin(), families_.cend());
    for (const auto& family : stored_recent_font_families()) {
      if (installed.contains(family)) {
        recent_.append(family);
      }
    }
    for (const auto& family : stored_favorite_font_families()) {
      if (installed.contains(family)) {
        favorites_.insert(family);
      }
    }
    // The caller's extra sections (GitHub issue 692), installed families only, each once a section.
    for (const auto& section : combo.extra_sections()) {
      FontPickerSection kept{section.title, {}, section.remove, section.remove_tooltip};
      for (const auto& row : section.rows) {
        if (installed.contains(row.family) &&
            std::ranges::none_of(kept.rows,
                                 [&row](const FontPickerSectionRow& other) { return other.family == row.family; })) {
          kept.rows.push_back(row);
        }
      }
      extra_sections_.push_back(std::move(kept));
    }
    model_->set_favorites(&favorites_);
    model_->set_display_names(&names_);
    slice_timer_.setInterval(0);
    connect(&slice_timer_, &QTimer::timeout, this, [this] { classify_slice(); });
  }

  [[nodiscard]] FontListModel* model() const noexcept { return model_; }

  // The Favorites view (GitHub issue 630): only the favorites that pass the search and filters.
  void set_favorites_only(bool favorites_only) {
    if (favorites_only_ != favorites_only) {
      favorites_only_ = favorites_only;
      rebuild();
    }
  }

  // The row star: flips `family`'s favorite state, saves it and updates the list in place.
  void toggle_favorite(const QString& family) {
    const bool favorite = !favorites_.contains(family);
    set_favorite_font_family(family, favorite);
    if (favorite) {
      favorites_.insert(family);
    } else {
      favorites_.remove(family);
    }
    if (favorites_only_) {
      rebuild(true);  // an unstarred favorite leaves the Favorites view at once
    } else {
      model_->favorite_changed(family);
    }
  }

  // The Recent row's remove button (GitHub issue 569 follow-up).
  void remove_recent(const QString& family) {
    remove_recent_font_family(family);
    if (recent_.removeAll(family) > 0) {
      rebuild(true);
    }
  }

  // The remove button of an extra section's row (issue 692 follow-up: "Chosen before").
  void remove_from_section(int section, const QString& family) {
    if (section < 0 || section >= static_cast<int>(extra_sections_.size())) {
      return;
    }
    auto& rows = extra_sections_[static_cast<std::size_t>(section)].rows;
    const auto removed =
        std::erase_if(rows, [&family](const FontPickerSectionRow& row) { return row.family == family; });
    combo_.remove_extra_section_row(section, family);
    if (removed > 0) {
      rebuild(true);
    }
  }

  // The remove button's tooltip on a row of extra section `section`.
  [[nodiscard]] QString section_remove_tooltip(int section) const {
    return section >= 0 && section < static_cast<int>(extra_sections_.size())
               ? extra_sections_[static_cast<std::size_t>(section)].remove_tooltip
               : QString();
  }

  // Rebuilds the rows, keeping the current family (and its section) when it survives. With
  // `keep_scroll` (a row button removed a row) the list stays where it was instead of following
  // the current row.
  void rebuild(bool keep_scroll = false) {
    const int scroll = list_.verticalScrollBar()->value();
    const auto previous = list_.currentIndex();
    const auto previous_family = is_family_row(previous) ? previous.data(Qt::DisplayRole).toString() : QString();
    const int previous_zone = row_zone(previous);

    const auto filter = font_style_filter_from_token(style_.currentData().toString());
    const auto system = writing_system_from_token(language_.currentData().toString());
    const auto text = search_.text().trimmed();
    if (system.has_value() && system != language_families_system_) {
      const auto families = font_families_without_foundries(QFontDatabase::families(*system));
      language_families_ = QSet<QString>(families.cbegin(), families.cend());
      language_families_system_ = system;
    }
    auto& index = FontStyleIndex::instance();
    pending_.clear();
    const auto passes_filters = [&](const QString& family) {
      if (system.has_value() && !language_families_.contains(family)) {
        return false;
      }
      if (!font_style_filter_reads_tables(filter)) {
        return index.matches(filter, family, {});
      }
      const auto traits = index.cached(family);
      if (!traits.has_value()) {
        pending_.append(family);
        return false;
      }
      return index.matches(filter, family, *traits);
    };
    // The search reads the listed name and the shown one (GitHub issue 691).
    const auto accepts = [&](const QString& family) {
      return (text.isEmpty() || names_.matches_search(family, text)) && passes_filters(family);
    };

    std::vector<FontListRow> rows;
    if (favorites_only_) {
      // The Favorites view replaces the Recent and All Fonts sections; the search and the filters
      // still apply inside it.
      rows.push_back({FontPickerRowKind::favorites_header, FontPickerCombo::tr("Favorites"), false});
      for (const auto& family : families_) {
        if (favorites_.contains(family) && accepts(family)) {
          rows.push_back({FontPickerRowKind::family, family, false});
        }
      }
      if (rows.size() == 1 && pending_.isEmpty()) {
        rows.push_back({FontPickerRowKind::empty_note,
                        favorites_.isEmpty()
                            ? FontPickerCombo::tr("No favorites yet. Click the star beside a font to add it.")
                            : FontPickerCombo::tr("No favorite fonts match the search or filters."),
                        false});
      }
    } else if (text.isEmpty()) {
      // While searching the top sections hide: the matches are the list. The caller's extra
      // sections come first (GitHub issue 692), then Recent; a section with no row that passes the
      // filters has no header, and "All Fonts" heads the full list only below a section.
      for (int section = 0; section < static_cast<int>(extra_sections_.size()); ++section) {
        const auto& extra = extra_sections_[static_cast<std::size_t>(section)];
        std::vector<FontListRow> section_rows;
        for (const auto& row : extra.rows) {
          if (accepts(row.family)) {
            section_rows.push_back(
                {FontPickerRowKind::family, row.family, false, section, row.note, static_cast<bool>(extra.remove)});
          }
        }
        if (!section_rows.empty()) {
          rows.push_back({FontPickerRowKind::extra_section_header, extra.title, false});
          rows.insert(rows.end(), section_rows.begin(), section_rows.end());
        }
      }
      std::vector<FontListRow> recent_rows;
      for (const auto& family : recent_) {
        if (accepts(family)) {
          recent_rows.push_back({FontPickerRowKind::family, family, true});
        }
      }
      if (!recent_rows.empty()) {
        rows.push_back({FontPickerRowKind::recent_header, FontPickerCombo::tr("Recent"), true});
        rows.insert(rows.end(), recent_rows.begin(), recent_rows.end());
      }
      if (!rows.empty()) {
        rows.push_back({FontPickerRowKind::all_header, FontPickerCombo::tr("All Fonts"), false});
      }
    }
    // Search matches the Style or Language filter hides (GitHub issue 647): counted in the same
    // pass over the family names, so the count costs no font loading. Families still waiting to be
    // classified are not counted; they join the list or the count once they are read.
    int hidden_matches = 0;
    if (!text.isEmpty()) {
      for (const auto& family : families_) {
        if (favorites_only_ && !favorites_.contains(family)) {
          continue;
        }
        if (!names_.matches_search(family, text)) {
          continue;
        }
        const qsizetype pending_before = pending_.size();
        if (passes_filters(family)) {
          if (!favorites_only_) {
            rows.push_back({FontPickerRowKind::family, family, false});
          }
        } else if (pending_.size() == pending_before) {
          ++hidden_matches;
        }
      }
    } else if (!favorites_only_) {
      for (const auto& family : families_) {
        if (accepts(family)) {
          rows.push_back({FontPickerRowKind::family, family, false});
        }
      }
    }
    pending_.removeDuplicates();
    model_->set_rows(std::move(rows));
    hidden_matches_.setText(
        //: Under the font list while searching: fonts that match the search but are hidden by the
        //: Style or Language filter next to the search box. Clicking it sets both filters to All.
        FontPickerCombo::tr("More matches with All Styles and All Languages: %n", nullptr, hidden_matches));
    hidden_matches_.setVisible(hidden_matches > 0);

    if (!previous_family.isEmpty() && select_family(previous_family, previous_zone)) {
      if (!keep_scroll) {
        list_.scrollTo(list_.currentIndex());
      }
    } else {
      step_to_family_row(list_, -1, 1);
    }
    if (keep_scroll) {
      list_.verticalScrollBar()->setValue(scroll);
    }
    update_classification();
  }

  // Selects `family`'s row in `zone` (the full list, Recent or an extra section) when it has one
  // there, else its topmost row.
  bool select_family(const QString& family, int zone) {
    QModelIndex fallback;
    for (int row = 0; row < model_->rowCount(); ++row) {
      const auto index = model_->index(row, 0);
      if (!is_family_row(index) || index.data(Qt::DisplayRole).toString() != family) {
        continue;
      }
      if (row_zone(index) == zone) {
        list_.setCurrentIndex(index);
        return true;
      }
      if (!fallback.isValid()) {
        fallback = index;
      }
    }
    if (fallback.isValid()) {
      list_.setCurrentIndex(fallback);
      return true;
    }
    return false;
  }

  // Selects `family`'s topmost row: in an extra section, else in Recent, else in the full list.
  bool select_topmost_family(const QString& family) {
    for (int row = 0; row < model_->rowCount(); ++row) {
      const auto index = model_->index(row, 0);
      if (is_family_row(index) && index.data(Qt::DisplayRole).toString() == family) {
        list_.setCurrentIndex(index);
        return true;
      }
    }
    return false;
  }

  // Whether an extra section has a row in the list right now.
  [[nodiscard]] bool shows_extra_section() const {
    for (int row = 0; row < model_->rowCount(); ++row) {
      if (model_->index(row, 0).data(kFontPickerRowKindRole).toInt() ==
          static_cast<int>(FontPickerRowKind::extra_section_header)) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] bool classifying() const noexcept { return slice_timer_.isActive(); }

private:
  void update_classification() {
    pending_total_ = std::max(pending_total_, static_cast<int>(pending_.size()));
    if (pending_.isEmpty()) {
      slice_timer_.stop();
      status_.hide();
      pending_total_ = 0;
      return;
    }
    show_progress();
    if (!slice_timer_.isActive()) {
      since_rebuild_.start();
      slice_timer_.start();
    }
  }

  void show_progress() {
    const int total = std::max(1, pending_total_);
    const int done = total - static_cast<int>(pending_.size());
    status_.setText(FontPickerCombo::tr("Reading font styles... %1%").arg(done * 100 / total));
    status_.show();
  }

  void classify_slice() {
    QElapsedTimer slice;
    slice.start();
    auto& index = FontStyleIndex::instance();
    while (!pending_.isEmpty() && slice.elapsed() < 12) {
      index.classify(pending_.takeFirst());
    }
    if (pending_.isEmpty()) {
      slice_timer_.stop();
      index.save();
      rebuild();
      return;
    }
    show_progress();
    if (since_rebuild_.elapsed() > 400) {  // let matches appear while the rest is read
      since_rebuild_.restart();
      rebuild();  // gathers what is still unread
    }
  }

  FontPickerCombo& combo_;
  QListView& list_;
  QLineEdit& search_;
  QComboBox& style_;
  QComboBox& language_;
  QLabel& status_;
  QToolButton& hidden_matches_;
  // A copy for the popup's life: the shared table is rebuilt when the font database changes.
  const FontDisplayNames names_;
  FontListModel* model_{nullptr};
  QStringList families_;
  QStringList recent_;
  std::vector<FontPickerSection> extra_sections_;  // installed families only
  QSet<QString> favorites_;  // installed favorites only
  bool favorites_only_{false};
  QSet<QString> language_families_;
  std::optional<QFontDatabase::WritingSystem> language_families_system_;
  QStringList pending_;
  int pending_total_{0};
  QTimer slice_timer_;
  QElapsedTimer since_rebuild_;
};

// The row buttons' mouse handling on the list's viewport: a press on a button belongs to the
// button (the row is neither picked nor selected), the action runs on release over the same
// button, and hovering a button gives it its tooltip and brighter ink. Everywhere else the list
// behaves as before, so the row's own pick and the keyboard are untouched.
class RowButtonController : public QObject {
public:
  RowButtonController(QListView& list, FontListFilter& rows, QObject* parent)
      : QObject(parent), list_(list), rows_(rows) {}

  [[nodiscard]] const RowButtonHover& hover() const noexcept { return hover_; }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    switch (event->type()) {
      case QEvent::MouseMove:
        set_hover(button_at(static_cast<const QMouseEvent*>(event)->position().toPoint()));
        break;
      case QEvent::Leave:
        set_hover({});
        break;
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonDblClick: {
        const auto* mouse = static_cast<const QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
          const auto hit = button_at(mouse->position().toPoint());
          if (hit.button != FontPickerRowButton::none) {
            pressed_ = hit;
            event->accept();
            return true;
          }
        }
        break;
      }
      case QEvent::MouseButtonRelease: {
        const auto* mouse = static_cast<const QMouseEvent*>(event);
        if (pressed_.button != FontPickerRowButton::none && mouse->button() == Qt::LeftButton) {
          const auto pressed = std::exchange(pressed_, RowButtonHover{});
          const auto hit = button_at(mouse->position().toPoint());
          if (hit.row == pressed.row && hit.button == pressed.button) {
            const auto index = list_.model()->index(hit.row, 0);
            const auto family = index.data(Qt::DisplayRole).toString();
            const int section = index.data(kFontPickerExtraSectionRole).toInt();
            // Queued: the action rebuilds the list's model, which must not happen inside its
            // own mouse event.
            QMetaObject::invokeMethod(
                this, [this, family, section, button = hit.button] { activate(family, section, button); },
                Qt::QueuedConnection);
          }
          event->accept();
          return true;
        }
        break;
      }
      case QEvent::ToolTip: {
        const auto* help = static_cast<const QHelpEvent*>(event);
        const auto hit = button_at(help->pos());
        if (hit.button == FontPickerRowButton::remove_recent) {
          const int section = list_.model()->index(hit.row, 0).data(kFontPickerExtraSectionRole).toInt();
          const auto tip =
              section >= 0 ? rows_.section_remove_tooltip(section) : FontPickerCombo::tr("Remove from Recent");
          if (!tip.isEmpty()) {
            QToolTip::showText(help->globalPos(), tip, list_.viewport());
          }
          return true;
        }
        if (hit.button == FontPickerRowButton::favorite) {
          const bool favorite = list_.model()->index(hit.row, 0).data(kFontPickerFavoriteRole).toBool();
          QToolTip::showText(help->globalPos(),
                             favorite ? FontPickerCombo::tr("Remove from favorites")
                                      : FontPickerCombo::tr("Add to favorites"),
                             list_.viewport());
          return true;
        }
        // A row shown under its English name tells the name the font database lists (GitHub
        // issue 691).
        if (const auto index = list_.indexAt(help->pos()); is_family_row(index)) {
          const auto family = index.data(Qt::DisplayRole).toString();
          if (index.data(kFontPickerShownNameRole).toString() != family) {
            QToolTip::showText(help->globalPos(), family, list_.viewport());
            return true;
          }
        }
        break;
      }
      default:
        break;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  [[nodiscard]] RowButtonHover button_at(const QPoint& position) const {
    const auto index = list_.indexAt(position);
    if (!is_family_row(index)) {
      return {};
    }
    const auto rect = list_.visualRect(index);
    const bool removable = index.data(kFontPickerRowRemovableRole).toBool();
    for (const auto button : {FontPickerRowButton::remove_recent, FontPickerRowButton::favorite}) {
      if (font_picker_row_button_rect(rect, button, removable).contains(position)) {
        return {index.row(), button};
      }
    }
    return {};
  }

  void set_hover(const RowButtonHover& hover) {
    if (hover.row != hover_.row || hover.button != hover_.button) {
      hover_ = hover;
      list_.viewport()->update();
    }
  }

  void activate(const QString& family, int section, FontPickerRowButton button) {
    if (button == FontPickerRowButton::favorite) {
      rows_.toggle_favorite(family);
    } else if (button == FontPickerRowButton::remove_recent && section >= 0) {
      rows_.remove_from_section(section, family);
    } else if (button == FontPickerRowButton::remove_recent) {
      rows_.remove_recent(family);
    }
    // Rows may have moved under the mouse.
    set_hover(button_at(list_.viewport()->mapFromGlobal(QCursor::pos())));
  }

  QListView& list_;
  FontListFilter& rows_;
  RowButtonHover hover_;
  RowButtonHover pressed_;
};

// The Favorites toggle's star: an outline in icon ink while off, filled in font_favorite_star on.
[[nodiscard]] QIcon favorites_toggle_icon(bool on) {
  return themed_glyph_icon(on ? QStringLiteral("font-favorites-on") : QStringLiteral("font-favorites-off"), 16.0,
                           on ? &ThemePalette::font_favorite_star : &ThemePalette::text_primary,
                           [on](QPainter& painter, const QColor& ink) {
                             painter.setRenderHint(QPainter::Antialiasing, true);
                             const auto path = star_path(QPointF(8.0, 8.0), 7.0);
                             if (on) {
                               painter.setPen(QPen(ink, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                               painter.setBrush(ink);
                             } else {
                               painter.setPen(QPen(ink, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                               painter.setBrush(Qt::NoBrush);
                             }
                             painter.drawPath(path);
                           });
}

}  // namespace

QRect font_picker_row_button_rect(const QRect& row_rect, FontPickerRowButton button, bool removable_row) {
  const int side = std::clamp(row_rect.height() - 4, 14, 22);
  const QRect rightmost(row_rect.right() - 4 - side + 1, row_rect.top() + (row_rect.height() - side) / 2, side, side);
  switch (button) {
    case FontPickerRowButton::remove_recent:
      return removable_row ? rightmost : QRect();
    case FontPickerRowButton::favorite:
      return removable_row ? rightmost.translated(-(side + 2), 0) : rightmost;
    case FontPickerRowButton::none:
      break;
  }
  return {};
}

FontPreviewPane::FontPreviewPane(FontPickerCombo& combo, QWidget* parent)
    : QWidget(parent), combo_(combo) {}

void FontPreviewPane::set_family(const QString& family) {
  if (family_ == family) {
    return;
  }
  family_ = family;
  rebuild_lines();
  update();
}

QStringList FontPreviewPane::line_texts() const {
  QStringList out;
  out.reserve(static_cast<qsizetype>(lines_.size()));
  for (const auto& line : lines_) {
    out.append(line.text);
  }
  return out;
}

void FontPreviewPane::rebuild_lines() {
  lines_.clear();
  footer_.clear();
  if (family_.isEmpty()) {
    return;
  }
  const auto info = combo_.family_render_info(family_);  // copy; see family_render_info
  if (info.latin_capable) {
    // Type-specimen lead: the family name, large, in its own face (merging ON so the name
    // stays complete even when the face misses the odd character); in the UI font for a symbol
    // face, whose letters are not letters.
    QFont name_font = info.symbol_glyphs ? combo_.font() : QFont(family_);
    apply_font_size(name_font, scaled_font(combo_.font(), 1.9));
    lines_.push_back({name_font, font_display_names().display_name(family_), QString()});
  }
  int script_lines = 0;
  QStringList footer_names;
  for (const auto ws : info.systems) {
    QFont line_font = info.sample_font;  // the family with NoFontMerging (honest coverage)
    apply_font_size(line_font, scaled_font(combo_.font(), 1.4));
    if (ws == QFontDatabase::Latin && info.symbol_glyphs) {
      // Its "Latin" is the font's symbol set: a glyph sample, under the Symbol label.
      if (const auto text = symbol_font_sample(line_font); glyph_count(text) > 0) {
        lines_.push_back({line_font, text, QFontDatabase::writingSystemName(QFontDatabase::Symbol)});
      }
      continue;
    }
    if (ws == QFontDatabase::Latin) {
      //: Latin sample text in the font preview; keep it Latin in every language (it
      //: demonstrates the font's Latin glyph coverage).
      const auto sample =
          FontPickerCombo::tr("The quick brown fox jumps over the lazy dog. 0123456789");
      const auto subset = renderable_subset(line_font, sample);
      if (glyph_count(subset) * 2 >= glyph_count(sample)) {
        lines_.push_back({line_font, subset, QFontDatabase::writingSystemName(ws)});
      }
      continue;
    }
    // Minor European scripts on a Latin font, and anything past the line budget, collapse
    // into the "Also supports" footer instead of rendering a sample line.
    if ((info.latin_capable && !is_specimen_script(ws)) || script_lines >= kMaxScriptPreviewLines) {
      footer_names.append(QFontDatabase::writingSystemName(ws));
      continue;
    }
    QString text;
    if (ws == QFontDatabase::Symbol) {
      text = symbol_font_sample(line_font);
    } else {
      const auto sample = writing_system_sample(ws);
      const auto subset = renderable_subset(line_font, sample);
      if (glyph_count(subset) * 2 < glyph_count(sample)) {
        continue;  // the font over-claims this writing system; omit it entirely
      }
      text = subset;
    }
    if (glyph_count(text) == 0) {
      continue;
    }
    lines_.push_back({line_font, text, QFontDatabase::writingSystemName(ws)});
    ++script_lines;
  }
  if (!footer_names.isEmpty()) {
    footer_ = FontPickerCombo::tr("Also supports: %1").arg(footer_names.join(QStringLiteral(", ")));
  }
  if (lines_.empty()) {
    // Nothing verifiable to sample: at least show the family name in the UI font.
    lines_.push_back({font(), font_display_names().display_name(family_), QString()});
  }
}

void FontPreviewPane::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.fillRect(rect(), theme().preview_pane_bg);
  painter.setPen(theme().field_inset_border);
  painter.drawLine(0, 0, width(), 0);

  const QFont label_font = scaled_font(font(), 0.85);
  const QFontMetrics label_metrics(label_font);

  // The footer is pinned to the pane bottom so overflowing sample lines can never push
  // it out; the flowing lines get whatever height remains above it.
  auto flow_bottom = height() - 4;
  if (!footer_.isEmpty()) {
    const auto footer_height = label_metrics.height();
    const auto footer_y = height() - footer_height - 5;
    painter.setFont(label_font);
    painter.setPen(theme().preview_pane_muted_text);
    painter.drawText(QRect(10, footer_y, width() - 20, footer_height),
                     Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                     label_metrics.elidedText(footer_, Qt::ElideRight, width() - 20));
    flow_bottom = footer_y - 3;
  }

  auto y = 8;
  for (const auto& line : lines_) {
    const QFontMetrics metrics(line.font);
    const auto line_height = std::max(metrics.height(), label_metrics.height());
    if (y + line_height > flow_bottom) {
      break;
    }
    auto reserved = 0;
    if (!line.label.isEmpty()) {
      const auto label_width = label_metrics.horizontalAdvance(line.label);
      painter.setFont(label_font);
      painter.setPen(theme().preview_pane_muted_text);
      painter.drawText(QRect(width() - label_width - 10, y, label_width, line_height),
                       Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, line.label);
      reserved = label_width + 16;
    }
    painter.setFont(line.font);
    painter.setPen(theme().text_primary);
    const QRect text_rect(10, y, width() - 20 - reserved, line_height);
    painter.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                     metrics.elidedText(line.text, Qt::ElideRight, text_rect.width()));
    y += line_height + 5;
  }
}

namespace {

// The missing-font warning badge the Layers panel draws on a type layer's thumbnail
// (draw_missing_font_thumbnail_badge): a theme-colored triangle with the exclamation punched
// through, so it reads on the field in Dark and Light alike.
QIcon missing_font_badge_icon(const QWidget& widget) {
  constexpr int kSize = 16;
  const qreal ratio = std::max<qreal>(1.0, widget.devicePixelRatioF());
  QPixmap pixmap(static_cast<int>(std::ceil(kSize * ratio)), static_cast<int>(std::ceil(kSize * ratio)));
  pixmap.setDevicePixelRatio(ratio);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  const qreal left = 1.0;
  const qreal top = 1.5;
  const qreal badge = 14.0;
  const qreal centre = left + badge / 2.0;
  QPainterPath triangle;
  triangle.moveTo(centre, top);
  triangle.lineTo(left + badge, top + badge);
  triangle.lineTo(left, top + badge);
  triangle.closeSubpath();
  painter.setPen(QPen(theme().icon_warning, 1.0, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
  painter.setBrush(theme().icon_warning);
  painter.drawPath(triangle);
  painter.setCompositionMode(QPainter::CompositionMode_Clear);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::black);  // color-literal-ok: Clear mode only uses the alpha
  painter.drawRect(QRectF(centre - 1.0, top + 4.5, 2.0, 5.0));
  painter.drawRect(QRectF(centre - 1.0, top + 10.5, 2.0, 2.0));
  painter.end();
  return QIcon(pixmap);
}

// The combo's family model: QFontComboBox fills it from QFontDatabase::families() and picks rows
// by those names, but every name reads back without Qt's " [Foundry]" suffix (GitHub issue 646),
// so the closed control, currentText(), findText() and the font the combo reports all name the
// plain family ("Arial", never "Arial [TMC ]"). A family several foundries install still has one
// row per copy here, because Qt selects rows by their place in its own list; every copy after the
// first is disabled, so the closed control's arrow keys and wheel step over it and findText()
// lands on the first. The popup lists the family once.
class FoundryFreeFamilyModel final : public QStringListModel {
public:
  using QStringListModel::QStringListModel;

  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override {
    auto value = QStringListModel::data(index, role);
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
      return font_family_without_foundry(value.toString());
    }
    return value;
  }

  [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override {
    auto flags = QStringListModel::flags(index);
    if (index.isValid() && index.row() > 0 &&
        data(index, Qt::DisplayRole)
                .toString()
                .compare(data(sibling(index.row() - 1, 0, index), Qt::DisplayRole).toString(), Qt::CaseInsensitive) ==
            0) {
      flags &= ~Qt::ItemFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
    return flags;
  }
};

}  // namespace

FontPickerCombo::FontPickerCombo(QWidget* parent) : QFontComboBox(parent) {
  // Before any connection below: QFontComboBox repopulates a QStringListModel subclass as its own
  // (on setCurrentFont, writing-system changes and fontDatabaseChanged). Refilling it lets the
  // current font take the plain name of its row.
  setModel(new FoundryFreeFamilyModel(this));
  setWritingSystem(writingSystem());
  popup_clock_.start();
  // Text set by code (a pick, a layer switch, setCurrentFont under a QSignalBlocker on the combo)
  // leaves the line edit unmodified; text the user is typing stays where the cursor is.
  if (auto* edit = lineEdit(); edit != nullptr) {
    connect(edit, &QLineEdit::textChanged, this, [this, edit] {
      if (!edit->isModified()) {
        show_row_display_name(false);
        show_name_from_start();
      }
    });
    edit->installEventFilter(this);
    // Typing over a missing family's name starts a new choice: drop the missing styling.
    connect(edit, &QLineEdit::textEdited, this, [this] {
      if (!missing_family_.isEmpty()) {
        missing_family_.clear();
        refresh_missing_family_decoration();
      }
    });
  }
  show_row_display_name(false);
  show_name_from_start();
  connect(&FontDisplayNamesNotifier::instance(), &FontDisplayNamesNotifier::changed, this,
          [this] { show_row_display_name(true); });
  // Any pick replaces a missing family (GitHub issue 652). Connected before the Recent recorder
  // so row_family() there is the picked row, not the missing name.
  connect(this, &QFontComboBox::currentFontChanged, this, [this] { set_missing_family(QString()); });
  connect(this, &QComboBox::activated, this, [this] {
    set_missing_family(QString());
    show_row_display_name(false);  // a family name typed in full leaves the edit modified
  });
  // Every user pick (the popup's and a name typed into the closed control) feeds the popup's
  // Recent section (GitHub issue 569).
  connect(this, &QComboBox::activated, this, [this] {
    if (const auto family = row_family(); findText(family, Qt::MatchExactly | Qt::MatchCaseSensitive) >= 0) {
      record_recent_font_family(family);
    }
  });
}

QString FontPickerCombo::row_family() const {
  const auto row = itemText(currentIndex());
  if (!missing_family_.isEmpty()) {
    return row;
  }
  const auto text = currentText();
  return !row.isEmpty() && text == font_display_names().display_name(row) ? row : text;
}

void FontPickerCombo::show_row_display_name(bool replace_shown_name) {
  auto* edit = lineEdit();
  if (edit == nullptr || !missing_family_.isEmpty()) {
    return;
  }
  const auto family = itemText(currentIndex());
  if (family.isEmpty()) {
    return;
  }
  const auto shown = font_display_names().display_name(family);
  const auto text = edit->text();
  if (text == shown || (text != family && (!replace_shown_name || edit->isModified()))) {
    return;
  }
  // Blocked like a missing family's name: the text would reach QFontComboBox through
  // currentTextChanged and become currentFont()'s family.
  const QSignalBlocker blocker(this);
  edit->setText(shown);
  edit->setModified(false);
  show_name_from_start();
}

bool FontPickerCombo::eventFilter(QObject* watched, QEvent* event) {
  if (watched == lineEdit() && event->type() == QEvent::KeyPress && missing_family_.isEmpty()) {
    const auto key = static_cast<const QKeyEvent*>(event)->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
      // The stock handling looks the text up among the rows, which hold the listed names: an
      // English name would be added as a row of its own and become the font's family.
      const auto typed = lineEdit()->text();
      const auto family = font_display_names().family_for_display_name(typed);
      if (family != typed) {
        if (const int row = findText(family, Qt::MatchExactly | Qt::MatchCaseSensitive); row >= 0) {
          setCurrentIndex(row);
          show_row_display_name(true);
          emit activated(row);
          return true;
        }
      }
    }
  }
  return QFontComboBox::eventFilter(watched, event);
}

void FontPickerCombo::set_missing_family(const QString& family, const QString& shown) {
  const auto trimmed = family.trimmed();
  const auto shown_text = trimmed.isEmpty() || shown.trimmed().isEmpty() ? trimmed : shown.trimmed();
  if (trimmed == missing_family_ && (trimmed.isEmpty() || lineEdit() == nullptr || lineEdit()->text() == shown_text)) {
    return;
  }
  missing_family_ = trimmed;
  if (auto* edit = lineEdit(); edit != nullptr) {
    // Unmodified, so the name scrolls to its start like any code-set text. Shown text that
    // differs from the missing name (its family without the face, issue 669) would reach
    // QFontComboBox through currentTextChanged and replace currentFont(), which applies that
    // family to the selected layer: the combo's signals stay blocked while it is set.
    const QSignalBlocker blocker(missing_family_.isEmpty() ? nullptr : this);
    edit->setText(missing_family_.isEmpty() ? itemText(currentIndex()) : shown_text);
    edit->setModified(false);
    show_name_from_start();
  }
  refresh_missing_family_decoration();
}

void FontPickerCombo::refresh_missing_family_decoration() {
  auto* edit = lineEdit();
  if (edit == nullptr) {
    return;
  }
  if (missing_family_.isEmpty()) {
    if (missing_badge_ != nullptr) {
      edit->removeAction(missing_badge_);
      delete missing_badge_;
    }
    edit->setFont(QFont());  // nothing set explicitly: inherit the combo's font again
    edit->setToolTip(QString());
    return;
  }
  QFont italic;
  italic.setItalic(true);  // only the italic attribute is explicit; the rest still inherits
  edit->setFont(italic);
  const auto tip = tr("Font is missing on this computer: %1").arg(missing_family_);
  if (missing_badge_ == nullptr) {
    missing_badge_ = edit->addAction(missing_font_badge_icon(*this), QLineEdit::LeadingPosition);
    missing_badge_->setObjectName(QStringLiteral("textFontMissingBadge"));
  } else {
    missing_badge_->setIcon(missing_font_badge_icon(*this));
  }
  missing_badge_->setToolTip(tip);
  edit->setToolTip(tip);
}

void FontPickerCombo::changeEvent(QEvent* event) {
  QFontComboBox::changeEvent(event);
  if (missing_family_.isEmpty()) {
    return;
  }
  // A theme switch restyles the badge color; a language switch retranslates the tooltip.
  switch (event->type()) {
    case QEvent::LanguageChange:
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
      refresh_missing_family_decoration();
      break;
    default:
      break;
  }
}

void FontPickerCombo::set_extra_sections(std::vector<FontPickerSection> sections) {
  extra_sections_ = std::move(sections);
}

void FontPickerCombo::remove_extra_section_row(int section, const QString& family) {
  if (section < 0 || section >= static_cast<int>(extra_sections_.size())) {
    return;
  }
  auto& extra = extra_sections_[static_cast<std::size_t>(section)];
  std::erase_if(extra.rows, [&family](const FontPickerSectionRow& row) { return row.family == family; });
  // A copy: the action may hand the picker new sections.
  if (const auto remove = extra.remove; remove) {
    remove(family);
  }
}

void FontPickerCombo::show_name_from_start() {
  if (auto* edit = lineEdit(); edit != nullptr) {
    edit->setCursorPosition(0);
  }
}

void FontPickerCombo::focusOutEvent(QFocusEvent* event) {
  QFontComboBox::focusOutEvent(event);
  show_name_from_start();
}

const FontFamilyRenderInfo& FontPickerCombo::family_render_info(const QString& family) {
  const auto existing = render_info_.constFind(family);
  if (existing != render_info_.constEnd()) {
    return existing.value();
  }
  FontFamilyRenderInfo info;
  const auto systems = QFontDatabase::writingSystems(family);
  info.latin_capable = systems.contains(QFontDatabase::Latin);
  for (const auto ws : kWritingSystemPriority) {
    if (systems.contains(ws)) {
      info.systems.append(ws);
    }
  }
  for (const auto ws : systems) {
    if (!info.systems.contains(ws)) {
      info.systems.append(ws);
    }
  }
  const QFont row_font = scaled_font(font(), 4.0 / 3.0);
  info.sample_font = QFont(family);
  apply_font_size(info.sample_font, row_font);
  info.sample_font.setStyleStrategy(QFont::NoFontMerging);
  if (info.latin_capable) {
    // GitHub issue 691 follow-up: a Symbol-class face that claims Latin maps its letters onto
    // symbols. One OS/2 read per family, kept by the style index (its Style filter answer).
    auto& styles = FontStyleIndex::instance();
    const auto traits = styles.cached(family);
    info.symbol_glyphs =
        (traits.has_value() ? *traits : styles.classify(family)).category == FontStyleCategory::symbol;
  }
  if (info.latin_capable && !info.symbol_glyphs) {
    info.display_font = QFont(family);
  } else {
    // The family cannot draw its own Latin name; keep the row readable in the UI font and
    // show a short run of the family's actual glyphs beside it.
    info.display_font = font();
    if (info.symbol_glyphs || info.systems.contains(QFontDatabase::Symbol)) {
      info.row_sample = symbol_font_sample(info.sample_font).left(kMaxRowSampleChars).trimmed();
    } else if (!info.systems.isEmpty()) {
      info.row_sample = renderable_subset(info.sample_font, writing_system_sample(info.systems.front()))
                            .left(kMaxRowSampleChars)
                            .trimmed();
    }
  }
  apply_font_size(info.display_font, row_font);
  return *render_info_.insert(family, info);
}

void FontPickerCombo::showPopup() {
  // Clicking the combo while its popup is open must DISMISS it, not reopen it. The press that
  // closes the Qt::Popup is replayed onto the combo, so by the time showPopup runs again the
  // popup is either still closing (pointer alive) or was just destroyed (timestamp) -- both
  // mean this click was the dismissal. Same machinery as BrushTipPicker::show_popup.
  if (popup_ != nullptr) {
    popup_->close();
    return;
  }
  if (popup_dismissed_ms_ >= 0 && popup_clock_.elapsed() - popup_dismissed_ms_ < 300) {
    popup_dismissed_ms_ = -1;
    return;
  }
  auto* popup = new SizePersistingPopupFrame(this, Qt::Popup);
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setObjectName(QString::fromLatin1(kFontPickerPopupObjectName));
  popup->setFrameShape(QFrame::StyledPanel);
  popup_ = popup;
  connect(popup, &QObject::destroyed, this, [this] {
    // Arm the swallow window only when the popup died under a click on the combo itself;
    // closing it by choosing a font must not eat a quick legitimate reopen.
    if (rect().contains(mapFromGlobal(QCursor::pos()))) {
      popup_dismissed_ms_ = popup_clock_.elapsed();
    }
  });
  // The app stylesheet reaches the popup through the parent chain, but has no QListView
  // rules (only QListWidget), so the popup carries its own scoped additions.
  set_themed_style(*popup, QStringLiteral(R"(
    QFrame#textFontPickerPopup {
      background: @field_bg_large;
      border: 1px solid @field_inset_border;
    }
    QFrame#textFontPickerPopup QListView {
      background: @popup_list_bg;
      color: @text_primary;
      border: 1px solid @field_inset_border;
      outline: none;
    }
    QToolButton#textFontPickerHiddenMatchesButton {
      color: @text_secondary;
      border: none;
      background: transparent;
      padding: 0px 2px;
    }
    QToolButton#textFontPickerHiddenMatchesButton:hover {
      color: @text_primary;
      text-decoration: underline;
    }
  )"));

  auto* layout = new QVBoxLayout(popup);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);

  // Search, then the Style and Language filters (GitHub issue 570). The combos never take focus,
  // so typing and the arrow keys stay in the search box.
  auto* filter_row = new QHBoxLayout();
  filter_row->setContentsMargins(0, 0, 0, 0);
  filter_row->setSpacing(4);
  auto* search = new QLineEdit(popup);
  search->setObjectName(QStringLiteral("textFontPickerSearchEdit"));
  search->setPlaceholderText(tr("Search fonts..."));
  search->setClearButtonEnabled(true);
  filter_row->addWidget(search, 1);

  auto* style_combo = new QComboBox(popup);
  style_combo->setObjectName(QStringLiteral("textFontPickerStyleCombo"));
  style_combo->setToolTip(tr("Show only fonts of this style"));
  style_combo->setFocusPolicy(Qt::NoFocus);
  style_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  for (const auto filter : font_style_filters()) {
    style_combo->addItem(style_filter_label(filter), font_style_filter_token(filter));
  }
  const auto saved_style = font_style_filter_token(
      font_style_filter_from_token(app_settings().value(QString::fromLatin1(kFontPickerStyleFilterKey)).toString()));
  style_combo->setCurrentIndex(std::max(0, style_combo->findData(saved_style)));
  filter_row->addWidget(style_combo);

  auto* language_combo = new QComboBox(popup);
  language_combo->setObjectName(QStringLiteral("textFontPickerLanguageCombo"));
  language_combo->setToolTip(tr("Show only fonts that support this language"));
  language_combo->setFocusPolicy(Qt::NoFocus);
  language_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  language_combo->setMinimumContentsLength(9);
  language_combo->addItem(tr("All Languages"), writing_system_token(QFontDatabase::Any));
  {
    std::vector<std::pair<QString, QString>> systems;  // name, token
    for (const auto system : QFontDatabase::writingSystems()) {
      if (system != QFontDatabase::Any) {
        systems.emplace_back(QFontDatabase::writingSystemName(system), writing_system_token(system));
      }
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(systems.begin(), systems.end(),
              [&collator](const auto& a, const auto& b) { return collator.compare(a.first, b.first) < 0; });
    for (const auto& [name, token] : systems) {
      language_combo->addItem(name, token);
    }
  }
  const auto saved_language = app_settings().value(QString::fromLatin1(kFontPickerLanguageFilterKey)).toString();
  language_combo->setCurrentIndex(std::max(0, language_combo->findData(saved_language)));
  // The closed combo stays narrow; its list is as wide as the longest language name.
  language_combo->view()->setMinimumWidth(language_combo->view()->sizeHintForColumn(0) + 24);
  filter_row->addWidget(language_combo);
  // The Favorites view (GitHub issue 630): a toggle beside the filters, since favorites are not a
  // style. It persists like the filters.
  auto* favorites_button = new QToolButton(popup);
  favorites_button->setObjectName(QStringLiteral("textFontPickerFavoritesButton"));
  favorites_button->setToolTip(tr("Show only favorite fonts"));
  favorites_button->setFocusPolicy(Qt::NoFocus);
  favorites_button->setCheckable(true);
  favorites_button->setAutoRaise(true);
  favorites_button->setIconSize(QSize(16, 16));
  favorites_button->setChecked(app_settings().value(QString::fromLatin1(kFontPickerFavoritesOnlyKey)).toBool());
  favorites_button->setIcon(favorites_toggle_icon(favorites_button->isChecked()));
  filter_row->addWidget(favorites_button);
  layout->addLayout(filter_row);
  // Both lists scroll at a fixed height instead of running off the screen (the Language list
  // has dozens of entries; GitHub issue 570 follow-up), and keep Enter to themselves.
  for (auto* combo : {style_combo, language_combo}) {
    use_scrolling_list_popup(*combo);
    auto* guard = new ComboListEnterGuard(*combo);
    combo->view()->installEventFilter(guard);
    combo->view()->window()->installEventFilter(guard);  // the list's popup container
  }

  auto* status = new QLabel(popup);
  status->setObjectName(QStringLiteral("textFontPickerStatusLabel"));
  status->setFont(scaled_font(font(), 0.9));
  status->hide();
  layout->addWidget(status);

  // Shown under the list while the filters hide fonts that match the search (GitHub issue 647);
  // clicking it sets both filters to All.
  auto* hidden_matches = new QToolButton(popup);
  hidden_matches->setObjectName(QStringLiteral("textFontPickerHiddenMatchesButton"));
  hidden_matches->setToolTip(tr("Set Style and Language to All"));
  hidden_matches->setFocusPolicy(Qt::NoFocus);
  hidden_matches->setAutoRaise(true);
  hidden_matches->setCursor(Qt::PointingHandCursor);
  hidden_matches->setFont(scaled_font(font(), 0.9));
  hidden_matches->hide();

  auto* list = new QListView(popup);
  list->setObjectName(QStringLiteral("textFontPickerList"));
  auto* rows = new FontListFilter(*this, *list, *search, *style_combo, *language_combo, *status, *hidden_matches,
                                  popup);
  list->setModel(rows->model());
  auto* row_buttons = new RowButtonController(*list, *rows, list);
  list->viewport()->installEventFilter(row_buttons);
  list->setItemDelegate(new FontListDelegate(*this, row_buttons->hover(), list));
  list->setUniformItemSizes(true);
  list->setEditTriggers(QAbstractItemView::NoEditTriggers);
  list->setSelectionMode(QAbstractItemView::SingleSelection);
  list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  list->setFocusPolicy(Qt::NoFocus);
  list->setMouseTracking(true);
  list->viewport()->setMouseTracking(true);
  list->viewport()->setAttribute(Qt::WA_Hover, true);
  list->setMinimumSize(240, 120);
  layout->addWidget(list, 1);
  layout->addWidget(hidden_matches, 0, Qt::AlignLeft);

  auto* preview = new FontPreviewPane(*this, popup);
  preview->setObjectName(QStringLiteral("textFontPickerPreview"));
  preview->setFixedHeight(kPreviewPaneHeight);
  layout->addWidget(preview);

  auto* bottom = new QHBoxLayout();
  bottom->setContentsMargins(0, 0, 0, 0);
  bottom->addStretch(1);
  // A frameless popup has no native resize border; the grip is the resize handle.
  bottom->addWidget(new VisibleSizeGrip(popup), 0, Qt::AlignBottom);
  layout->addLayout(bottom);

  search->installEventFilter(new SearchKeyForwarder(list, search));

  connect(list, &QListView::entered, popup, [preview](const QModelIndex& index) {
    if (is_family_row(index)) {
      preview->set_family(index.data(Qt::DisplayRole).toString());
    }
  });
  connect(list->selectionModel(), &QItemSelectionModel::currentChanged, popup,
          [preview](const QModelIndex& current, const QModelIndex&) {
            if (is_family_row(current)) {
              preview->set_family(current.data(Qt::DisplayRole).toString());
            }
          });
  connect(search, &QLineEdit::textChanged, popup, [rows] { rows->rebuild(); });
  connect(style_combo, &QComboBox::currentIndexChanged, popup, [rows, style_combo, search] {
    auto settings = app_settings();
    settings.setValue(QString::fromLatin1(kFontPickerStyleFilterKey), style_combo->currentData().toString());
    rows->rebuild();
    search->setFocus();
  });
  connect(language_combo, &QComboBox::currentIndexChanged, popup, [rows, language_combo, search] {
    auto settings = app_settings();
    settings.setValue(QString::fromLatin1(kFontPickerLanguageFilterKey), language_combo->currentData().toString());
    rows->rebuild();
    search->setFocus();
  });
  connect(hidden_matches, &QToolButton::clicked, popup, [style_combo, language_combo, search] {
    style_combo->setCurrentIndex(0);  // All Styles
    language_combo->setCurrentIndex(0);  // All Languages
    search->setFocus();
  });
  connect(favorites_button, &QToolButton::toggled, popup, [rows, favorites_button, search](bool checked) {
    auto settings = app_settings();
    settings.setValue(QString::fromLatin1(kFontPickerFavoritesOnlyKey), checked);
    favorites_button->setIcon(favorites_toggle_icon(checked));
    rows->set_favorites_only(checked);
    search->setFocus();
  });

  const auto commit = [this, popup](const QModelIndex& index) {
    if (!is_family_row(index)) {
      return;
    }
    const auto chosen = index.data(Qt::DisplayRole).toString();
    const auto source_row = findText(chosen, Qt::MatchExactly | Qt::MatchCaseSensitive);
    if (source_row < 0) {
      return;
    }
    // Hide now, close (and so delete, WA_DeleteOnClose) only once the pick has gone through. The
    // pick can open a modal prompt (a selected layer's missing font asks first), and on macOS that
    // prompt's event loop runs the popup's deferred delete: the search field or list emitting this
    // signal, and this slot, were freed under it (a crash on Return in the search field).
    const QPointer<QWidget> alive(popup);
    popup->hide();
    setCurrentIndex(source_row);  // fires currentFontChanged through the stock combo path
    // ... unless the control was ALREADY parked on that row while its current font named a
    // different family. A missing font cannot be a row (it is not in the database), so a type
    // layer set in one leaves the closed control showing some other family with the missing name
    // still in currentFont(); the user then picks the family in front of them, the index never
    // moves, the stock path stays silent and the pick is lost -- the layer commits back into the
    // font it was supposed to be replacing. Push the family through so every pick is heard.
    if (!chosen.isEmpty() && currentFont().families().value(0) != chosen) {
      auto font = currentFont();
      font.setFamilies(QStringList{chosen});
      setCurrentFont(font);
    }
    setFocus(Qt::OtherFocusReason);  // land focus on a text-option widget, not a dying popup
    emit activated(currentIndex());  // a user pick, like the stock popup's (records it as recent)
    if (alive != nullptr) {
      alive->close();
    }
  };
  connect(list, &QListView::clicked, popup, commit);
  connect(search, &QLineEdit::returnPressed, popup, [list, commit] { commit(list->currentIndex()); });

  // Start on the current family, previewed: its row in an extra section or in Recent with the list
  // at the top when it has one (so the section is in view), else its place in the full list,
  // centered. A list with extra sections always opens at the top: they are what the caller wants
  // seen first (GitHub issue 692).
  rows->set_favorites_only(favorites_button->isChecked());
  rows->rebuild();
  list->clearSelection();
  list->setCurrentIndex(QModelIndex());
  const auto start_family = row_family();
  if (rows->select_topmost_family(start_family)) {
    if (row_zone(list->currentIndex()) != kFullListZone || rows->shows_extra_section()) {
      list->scrollToTop();
    } else {
      list->scrollTo(list->currentIndex(), QAbstractItemView::PositionAtCenter);
    }
  } else {
    step_to_family_row(*list, -1, 1);
  }
  preview->set_family(start_family);

  {
    // Restore the user's popup size; first open defaults to a comfortable browse size.
    const auto saved = app_settings().value(kPopupSizeSettingsKey).toSize();
    if (saved.isValid()) {
      popup->resize(saved.expandedTo(popup->minimumSizeHint()));
    } else {
      popup->resize(QSize(std::max(440, width()), 520).expandedTo(popup->minimumSizeHint()));
    }
  }
  auto position = mapToGlobal(QPoint(0, height()));
  const auto* screen = this->screen();
  if (screen != nullptr) {
    const auto available = screen->availableGeometry();
    if (position.y() + popup->height() > available.bottom()) {
      position.setY(mapToGlobal(QPoint(0, 0)).y() - popup->height());
    }
    position.setX(std::min(position.x(), available.right() - popup->width()));
  }
  popup->move(position);
  popup->show();
  search->setFocus();
}

void FontPickerCombo::hidePopup() {
  if (popup_ != nullptr) {
    popup_->close();
  }
  QFontComboBox::hidePopup();  // no-op while the stock view container was never shown
}

}  // namespace patchy::ui
