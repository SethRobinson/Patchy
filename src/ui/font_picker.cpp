#include "ui/font_picker.hpp"

#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QCursor>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QScreen>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

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

// Forwards list-navigation keys from the search edit to the list view (the list itself is
// Qt::NoFocus so the user can type and arrow through results without moving focus).
class SearchKeyForwarder : public QObject {
public:
  SearchKeyForwarder(QAbstractItemView* list, QObject* parent) : QObject(parent), list_(list) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::KeyPress) {
      const auto* key = static_cast<const QKeyEvent*>(event);
      switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
          QApplication::sendEvent(list_, event);
          return true;
        default:
          break;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QAbstractItemView* list_;
};

class FontListDelegate : public QStyledItemDelegate {
public:
  FontListDelegate(FontPickerCombo& combo, QObject* parent)
      : QStyledItemDelegate(parent), combo_(combo) {}

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    const auto family = index.data(Qt::DisplayRole).toString();
    // Copy: the cache reference is only stable until the next lookup, and QFont/QString
    // copies are shared-data cheap.
    const auto info = combo_.family_render_info(family);
    painter->save();
    const auto rect = option.rect;
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    if (selected) {
      painter->fillRect(rect, QColor(0x3a, 0x41, 0x4a));
      painter->setPen(QColor(0x67, 0x71, 0x7d));
      painter->drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
    } else if ((option.state & QStyle::State_MouseOver) != 0) {
      painter->fillRect(rect, QColor(0x33, 0x37, 0x3d));
    }
    auto text_rect = rect.adjusted(8, 0, -8, 0);
    if (!info.row_sample.isEmpty()) {
      // Symbol-class family: readable name in the UI font, its actual glyphs beside it.
      const QFontMetrics sample_metrics(info.sample_font);
      const auto sample =
          sample_metrics.elidedText(info.row_sample, Qt::ElideRight, text_rect.width() / 2);
      const auto sample_width = sample_metrics.horizontalAdvance(sample);
      painter->setFont(info.sample_font);
      painter->setPen(QColor(0x8a, 0x93, 0x9f));
      painter->drawText(
          QRect(text_rect.right() - sample_width, text_rect.top(), sample_width, text_rect.height()),
          Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, sample);
      text_rect.setWidth(text_rect.width() - sample_width - 12);
    }
    painter->setFont(info.display_font);
    painter->setPen(selected ? QColor(0xf4, 0xf6, 0xf8) : QColor(0xe6, 0xe6, 0xe6));
    const QFontMetrics metrics(info.display_font);
    painter->drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                      metrics.elidedText(family, Qt::ElideRight, text_rect.width()));
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    Q_UNUSED(option);
    Q_UNUSED(index);
    // The list uses uniformItemSizes, so the first row's hint sizes every row: return a fixed
    // height instead of per-family metrics (a family with a huge line height would otherwise
    // inflate every row).
    QFont probe = combo_.font();
    probe.setPointSizeF(probe.pointSizeF() * 4.0 / 3.0);
    const auto height = std::clamp(QFontMetrics(probe).height() + 6, 22, 34);
    return QSize(200, height);
  }

private:
  FontPickerCombo& combo_;
};

}  // namespace

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
    // stays complete even when the face misses the odd character).
    QFont name_font(family_);
    name_font.setPointSizeF(combo_.font().pointSizeF() * 1.9);
    lines_.push_back({name_font, family_, QString()});
  }
  int script_lines = 0;
  QStringList footer_names;
  for (const auto ws : info.systems) {
    QFont line_font = info.sample_font;  // the family with NoFontMerging (honest coverage)
    line_font.setPointSizeF(combo_.font().pointSizeF() * 1.4);
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
    lines_.push_back({font(), family_, QString()});
  }
}

void FontPreviewPane::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(0x25, 0x25, 0x25));
  painter.setPen(QColor(0x17, 0x17, 0x17));
  painter.drawLine(0, 0, width(), 0);

  QFont label_font = font();
  label_font.setPointSizeF(std::max(7.0, font().pointSizeF() * 0.85));
  const QFontMetrics label_metrics(label_font);

  // The footer is pinned to the pane bottom so overflowing sample lines can never push
  // it out; the flowing lines get whatever height remains above it.
  auto flow_bottom = height() - 4;
  if (!footer_.isEmpty()) {
    const auto footer_height = label_metrics.height();
    const auto footer_y = height() - footer_height - 5;
    painter.setFont(label_font);
    painter.setPen(QColor(0x9a, 0x9a, 0x9a));
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
      painter.setPen(QColor(0x9a, 0x9a, 0x9a));
      painter.drawText(QRect(width() - label_width - 10, y, label_width, line_height),
                       Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, line.label);
      reserved = label_width + 16;
    }
    painter.setFont(line.font);
    painter.setPen(QColor(0xe6, 0xe6, 0xe6));
    const QRect text_rect(10, y, width() - 20 - reserved, line_height);
    painter.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                     metrics.elidedText(line.text, Qt::ElideRight, text_rect.width()));
    y += line_height + 5;
  }
}

FontPickerCombo::FontPickerCombo(QWidget* parent) : QFontComboBox(parent) {
  popup_clock_.start();
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
  const auto row_point_size = font().pointSizeF() * 4.0 / 3.0;
  info.sample_font = QFont(family);
  info.sample_font.setPointSizeF(row_point_size);
  info.sample_font.setStyleStrategy(QFont::NoFontMerging);
  if (info.latin_capable) {
    info.display_font = QFont(family);
  } else {
    // The family cannot draw its own Latin name; keep the row readable in the UI font and
    // show a short run of the family's actual glyphs beside it.
    info.display_font = font();
    if (info.systems.contains(QFontDatabase::Symbol)) {
      info.row_sample = symbol_font_sample(info.sample_font).left(kMaxRowSampleChars).trimmed();
    } else if (!info.systems.isEmpty()) {
      info.row_sample = renderable_subset(info.sample_font, writing_system_sample(info.systems.front()))
                            .left(kMaxRowSampleChars)
                            .trimmed();
    }
  }
  info.display_font.setPointSizeF(row_point_size);
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
  popup->setStyleSheet(QStringLiteral(R"(
    QFrame#textFontPickerPopup {
      background: #2b2b2b;
      border: 1px solid #171717;
    }
    QFrame#textFontPickerPopup QListView {
      background: #232323;
      color: #e6e6e6;
      border: 1px solid #171717;
      outline: none;
    }
  )"));

  auto* layout = new QVBoxLayout(popup);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);

  auto* search = new QLineEdit(popup);
  search->setObjectName(QStringLiteral("textFontPickerSearchEdit"));
  search->setPlaceholderText(tr("Search fonts..."));
  search->setClearButtonEnabled(true);
  layout->addWidget(search);

  auto* proxy = new QSortFilterProxyModel(popup);
  proxy->setSourceModel(model());
  proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

  auto* list = new QListView(popup);
  list->setObjectName(QStringLiteral("textFontPickerList"));
  list->setModel(proxy);
  list->setItemDelegate(new FontListDelegate(*this, list));
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
    if (index.isValid()) {
      preview->set_family(index.data(Qt::DisplayRole).toString());
    }
  });
  connect(list->selectionModel(), &QItemSelectionModel::currentChanged, popup,
          [preview](const QModelIndex& current, const QModelIndex&) {
            if (current.isValid()) {
              preview->set_family(current.data(Qt::DisplayRole).toString());
            }
          });
  connect(search, &QLineEdit::textChanged, popup, [list, proxy](const QString& text) {
    proxy->setFilterFixedString(text);
    if (!list->currentIndex().isValid() && proxy->rowCount() > 0) {
      list->setCurrentIndex(proxy->index(0, 0));
    }
    if (list->currentIndex().isValid()) {
      list->scrollTo(list->currentIndex());
    }
  });

  const auto commit = [this, popup, proxy](const QModelIndex& proxy_index) {
    if (!proxy_index.isValid()) {
      return;
    }
    const auto source_row = proxy->mapToSource(proxy_index).row();
    popup->close();
    setCurrentIndex(source_row);  // fires currentFontChanged through the stock combo path
    setFocus(Qt::OtherFocusReason);  // land focus on a text-option widget, not a dying popup
  };
  connect(list, &QListView::clicked, popup, commit);
  connect(search, &QLineEdit::returnPressed, popup, [list, commit] { commit(list->currentIndex()); });

  // Start on the current family, centered and previewed.
  const auto current_proxy = proxy->mapFromSource(model()->index(currentIndex(), 0));
  if (current_proxy.isValid()) {
    list->setCurrentIndex(current_proxy);
    list->scrollTo(current_proxy, QAbstractItemView::PositionAtCenter);
  }
  preview->set_family(currentText());

  {
    // Restore the user's popup size; first open defaults to a comfortable browse size.
    const auto saved = app_settings().value(kPopupSizeSettingsKey).toSize();
    if (saved.isValid()) {
      popup->resize(saved.expandedTo(popup->minimumSizeHint()));
    } else {
      popup->resize(QSize(std::max(360, width()), 520).expandedTo(popup->minimumSizeHint()));
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
