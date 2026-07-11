#pragma once

#include <QElapsedTimer>
#include <QFont>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <vector>

class QFrame;

namespace patchy::ui {

class FontPickerCombo;

// objectName of the search/preview popup. MainWindow::is_text_option_widget matches it BY NAME
// (a Qt::Popup is a window, so isAncestorOf-based ownership checks stop at its boundary);
// renaming it would make focusing the search box auto-commit an open inline text editor.
inline constexpr char kFontPickerPopupObjectName[] = "textFontPickerPopup";

// Per-family info for list rows and previews, computed lazily the first time a family is
// painted or hovered (never precomputed across the whole database -- hundreds of families).
struct FontFamilyRenderInfo {
  QFont display_font;   // the family's own font for its list row; the UI font when the family
                        // cannot render its own Latin name (symbol fonts)
  QFont sample_font;    // the family's own font with NoFontMerging (honest glyph coverage)
  QString row_sample;   // short glyph run drawn beside the name when !latin_capable
  bool latin_capable{false};
  QList<QFontDatabase::WritingSystem> systems;  // the full claimed list, priority-ordered
};

// The hover preview pane at the bottom of the picker popup: the family name as a large
// specimen, a Latin pangram, then curated sample lines for the notable scripts the font
// covers (CJK, Arabic, Hebrew, Thai, Indic, symbol glyphs); minor European scripts collapse
// into a dim "Also supports" footer. Sample lines render with NoFontMerging so Qt's font
// fallback cannot fake script coverage.
class FontPreviewPane : public QWidget {
  Q_OBJECT

public:
  FontPreviewPane(FontPickerCombo& combo, QWidget* parent);

  void set_family(const QString& family);
  [[nodiscard]] const QString& family() const noexcept { return family_; }
  [[nodiscard]] QStringList line_texts() const;  // sample text per line (test accessor)
  [[nodiscard]] const QString& footer_text() const noexcept { return footer_; }

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  struct PreviewLine {
    QFont font;
    QString text;
    QString label;  // writing-system name, drawn small and dim
  };
  void rebuild_lines();

  FontPickerCombo& combo_;
  QString family_;
  std::vector<PreviewLine> lines_;
  QString footer_;  // "Also supports: ..." pinned to the pane bottom; empty when none
};

// Drop-in QFontComboBox for the Options bar: the closed control is unchanged, but opening it
// shows a searchable family list (live substring filter) with a hover/keyboard preview pane
// instead of the stock dropdown. Selection commits through setCurrentIndex, so
// currentFontChanged fires exactly like the stock combo and existing wiring keeps working.
class FontPickerCombo : public QFontComboBox {
  Q_OBJECT

public:
  explicit FontPickerCombo(QWidget* parent = nullptr);

  void showPopup() override;
  void hidePopup() override;

  // Lazy per-family cache shared by the row delegate and the preview pane. The returned
  // reference is only valid until the next call (QHash rehash); copy it to keep it.
  const FontFamilyRenderInfo& family_render_info(const QString& family);

private:
  QPointer<QFrame> popup_;
  // Toggle guard: the click that dismisses the Qt::Popup is replayed onto the combo (or lands
  // while the popup is still closing), which would instantly reopen it. Same machinery as
  // BrushTipPicker::show_popup().
  QElapsedTimer popup_clock_;
  qint64 popup_dismissed_ms_{-1};
  QHash<QString, FontFamilyRenderInfo> render_info_;  // persists across popup opens
};

}  // namespace patchy::ui
