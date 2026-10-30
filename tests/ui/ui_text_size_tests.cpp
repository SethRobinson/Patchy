// The interface text size (GitHub issue 495; docs/ui-text-size.md):
// the setting and its Preferences field, the stylesheet resolution, and a sweep that opens
// the main window with every tool, every panel tab, the Preferences pages and every "..."
// menu command's dialog at the smallest and largest text size in several languages, and
// fails on text that no longer fits where it fitted at 100%.
//
// One test per language (each stays inside the per-test time limit). The sizes default to
// both ends of the range; PATCHY_TEXT_SIZE_SIZES ("150") narrows a run, and
// PATCHY_TEXT_SIZE_CAPTURE=1 saves every state it checks as a test artifact
// (ui_text_size_<size>_<language>_<state>.png) for a person to look over.

#include "app_shell_test_support.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_qss.hpp"
#include "ui/ui_test_access.hpp"
#include "ui/ui_text_size.hpp"
#include "ui/update_checker.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontInfo>
#include <QFontMetrics>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QScopeGuard>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::test::ui::app_shell_support::run_preferences_dialog;

// Sets the process text size and the application font for one test and restores both.
// The suite runs at 100%, so the font it starts with is the 100% font.
class TextSizeScope {
 public:
  TextSizeScope() : previous_percent_(patchy::ui::ui_text_size_percent()), base_font_(QApplication::font()) {}
  explicit TextSizeScope(int percent) : TextSizeScope() { apply(percent); }
  ~TextSizeScope() {
    patchy::ui::set_ui_text_size_percent(previous_percent_);
    QApplication::setFont(base_font_);
  }
  TextSizeScope(const TextSizeScope&) = delete;
  TextSizeScope& operator=(const TextSizeScope&) = delete;

  void apply(int percent) const {
    patchy::ui::set_ui_text_size_percent(percent);
    QApplication::setFont(patchy::ui::ui_text_scaled_font(base_font_));
  }

 private:
  int previous_percent_;
  QFont base_font_;
};

void ui_text_size_setting_round_trips_and_clamps() {
  SettingsValueRestorer restore(QStringLiteral("preferences/uiTextSizePercent"));
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("preferences/uiTextSizePercent"));
  CHECK(patchy::ui::stored_ui_text_size_percent() == 100);
  patchy::ui::set_stored_ui_text_size_percent(115);
  CHECK(patchy::ui::stored_ui_text_size_percent() == 115);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("preferences/uiTextSizePercent")).toInt() == 115);
  patchy::ui::set_stored_ui_text_size_percent(400);
  CHECK(patchy::ui::stored_ui_text_size_percent() == patchy::ui::kMaxUiTextSizePercent);
  patchy::ui::set_stored_ui_text_size_percent(10);
  CHECK(patchy::ui::stored_ui_text_size_percent() == patchy::ui::kMinUiTextSizePercent);
  // The default is stored as no key, so a later default change reaches everyone on it.
  patchy::ui::set_stored_ui_text_size_percent(100);
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("preferences/uiTextSizePercent")));
  // A hand-edited value that is not a number reads as the default.
  settings.setValue(QStringLiteral("preferences/uiTextSizePercent"), QStringLiteral("large"));
  CHECK(patchy::ui::stored_ui_text_size_percent() == 100);
  CHECK(patchy::ui::normalize_ui_text_size_percent(0) == 100);
  CHECK(patchy::ui::normalize_ui_text_size_percent(-5) == 100);
}

void ui_text_size_resolves_stylesheet_font_sizes() {
  TextSizeScope scope;
  const QString sheet = QStringLiteral(
      "QWidget { font-size: 12px; } QLabel#a { font-size:11px; min-height: @tx(20)px; } "
      "QLabel#b { font-size: 14px /* scaled */; } QLabel#c { width: 12px; }");
  scope.apply(100);
  // At the default the sheet is unchanged apart from the resolved @tx lengths.
  CHECK(patchy::ui::resolve_ui_text_sizes(sheet) ==
        QStringLiteral("QWidget { font-size: 12px; } QLabel#a { font-size:11px; min-height: 20px; } "
                       "QLabel#b { font-size: 14px; } QLabel#c { width: 12px; }"));
  CHECK(patchy::ui::ui_text_px(12) == 12);
  scope.apply(patchy::ui::kMaxUiTextSizePercent);
  CHECK(patchy::ui::ui_text_size_percent() == 125);
  CHECK(patchy::ui::resolve_ui_text_sizes(sheet) ==
        QStringLiteral("QWidget { font-size: 15px; } QLabel#a { font-size:14px; min-height: 25px; } "
                       "QLabel#b { font-size: 14px; } QLabel#c { width: 12px; }"));
  CHECK(patchy::ui::ui_text_px(12) == 15);
  CHECK(patchy::ui::ui_text_length(88) == 110);
  // Out of range clamps.
  scope.apply(400);
  CHECK(patchy::ui::ui_text_size_percent() == patchy::ui::kMaxUiTextSizePercent);
  scope.apply(80);
  CHECK(patchy::ui::ui_text_px(12) == 10);
  CHECK(patchy::ui::ui_text_px(11) == 9);
  // Lengths that hold text never shrink below their default.
  CHECK(patchy::ui::ui_text_length(88) == 88);
  CHECK(patchy::ui::resolve_ui_text_sizes(QStringLiteral("QLabel { min-height: @tx(20)px; }")) ==
        QStringLiteral("QLabel { min-height: 20px; }"));
  // Through the theme resolver, as every themed sheet goes.
  CHECK(patchy::ui::apply_theme_tokens(QStringLiteral("QLabel { font-size: 10px; }")) ==
        QStringLiteral("QLabel { font-size: 8px; }"));
  // The application font follows too.
  // A point size becomes the pixel size the sheets round to.
  QFont points;
  points.setPointSizeF(9.0);
  const int point_pixels = QFontInfo(points).pixelSize();
  CHECK(patchy::ui::ui_text_scaled_font(points).pixelSize() == patchy::ui::ui_text_px(point_pixels));
  QFont pixels;
  pixels.setPixelSize(20);
  CHECK(patchy::ui::ui_text_scaled_font(pixels).pixelSize() == 16);
}

void ui_text_size_preferences_field_stores_and_notifies() {
  SettingsValueRestorer restore(QStringLiteral("preferences/uiTextSizePercent"));
  patchy::ui::set_stored_ui_text_size_percent(100);
  patchy::ui::MainWindow window;
  show_window_empty(window);
  bool saw_notice = false;
  run_preferences_dialog(window, true, [&](QDialog& dialog) {
    auto* spin = dialog.findChild<QSpinBox*>(QStringLiteral("preferencesUiTextSizeSpin"));
    CHECK(spin != nullptr);
    if (spin == nullptr) {
      return;
    }
    CHECK(spin->value() == 100);
    CHECK(spin->minimum() == patchy::ui::kMinUiTextSizePercent);
    CHECK(spin->maximum() == patchy::ui::kMaxUiTextSizePercent);
    CHECK(spin->singleStep() == patchy::ui::kUiTextSizeStepPercent);
    spin->setValue(120);
    // The restart notice comes up on OK.
    QTimer::singleShot(0, [&] {
      for (auto* top : QApplication::topLevelWidgets()) {
        if (top->objectName() == QStringLiteral("preferencesUiTextSizeMessageBox") && top->isVisible()) {
          saw_notice = true;
          if (auto* box = qobject_cast<QDialog*>(top)) {
            box->accept();
          }
        }
      }
    });
  });
  process_events_until([&] { return saw_notice; }, 2000);
  CHECK(saw_notice);
  CHECK(patchy::ui::stored_ui_text_size_percent() == 120);
  // Nothing changes for this process: the size applies on the next start.
  CHECK(patchy::ui::ui_text_size_percent() == 100);
  // Reopened, the field shows the stored value.
  run_preferences_dialog(window, false, [&](QDialog& dialog) {
    auto* spin = dialog.findChild<QSpinBox*>(QStringLiteral("preferencesUiTextSizeSpin"));
    CHECK(spin != nullptr && spin->value() == 120);
  });
}

// ---------------------------------------------------------------------------------------
// The scale itself (card i495-fu): every text widget's font grows by the text size once. A
// font scaled through both the application font and a stylesheet, or a size derived from an
// already scaled font and scaled again, would show here as more than 125%.

bool shows_text(const QWidget& widget) {
  if (const auto* label = qobject_cast<const QLabel*>(&widget)) {
    return !label->text().trimmed().isEmpty();
  }
  if (const auto* button = qobject_cast<const QAbstractButton*>(&widget)) {
    return !button->text().trimmed().isEmpty();
  }
  return qobject_cast<const QLineEdit*>(&widget) != nullptr ||
         qobject_cast<const QComboBox*>(&widget) != nullptr ||
         qobject_cast<const QTabBar*>(&widget) != nullptr ||
         qobject_cast<const QMenuBar*>(&widget) != nullptr ||
         qobject_cast<const QAbstractItemView*>(&widget) != nullptr;
}

// The font pixel size of every visible text widget under `root`, keyed by its place in the
// widget tree (object names where there are some), so two runs line up widget by widget.
void collect_text_font_sizes(QWidget& widget, const QString& path, std::map<QString, int>& out) {
  if (!widget.isVisible()) {
    return;
  }
  widget.ensurePolished();
  if (shows_text(widget)) {
    out[path] = QFontInfo(widget.font()).pixelSize();
  }
  std::map<QString, int> seen;
  for (auto* child : widget.findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
    if (child == nullptr || child->isWindow()) {
      continue;
    }
    const auto name = child->objectName().isEmpty() ? QString::fromLatin1(child->metaObject()->className())
                                                    : child->objectName();
    const int index = seen[name]++;
    collect_text_font_sizes(*child, path + QLatin1Char('/') + name + QLatin1Char('#') + QString::number(index), out);
  }
}

std::map<QString, int> text_font_sizes(QWidget& root) {
  std::map<QString, int> sizes;
  collect_text_font_sizes(root, root.objectName(), sizes);
  return sizes;
}

// Each widget's size at `percent` is its 100% size times the factor, give or take the
// whole-pixel rounding: never a second step.
int count_off_scale_fonts(const QString& state, const std::map<QString, int>& base, const std::map<QString, int>& scaled,
                          int percent) {
  int compared = 0;
  int off = 0;
  for (const auto& [path, base_px] : base) {
    const auto found = scaled.find(path);
    if (found == scaled.end() || base_px <= 0) {
      continue;
    }
    ++compared;
    const double expected = base_px * percent / 100.0;
    if (std::abs(found->second - expected) > 1.0) {
      ++off;
      std::cerr << state.toStdString() << ": " << path.toStdString() << " is " << found->second << " px at "
                << percent << "%, " << base_px << " px at 100% (expected about " << expected << ")\n";
    }
  }
  std::cerr << state.toStdString() << ": " << compared << " text widgets compared at " << percent << "%\n";
  CHECK(compared >= 20);
  return off;
}

// A few named widgets, printed so a person can read the numbers.
void print_named_font_sizes(const QString& state, const std::map<QString, int>& base,
                            const std::map<QString, int>& scaled, const QStringList& names) {
  for (const auto& name : names) {
    for (const auto& [path, base_px] : base) {
      if (!path.contains(name)) {
        continue;
      }
      const auto found = scaled.find(path);
      std::cerr << state.toStdString() << " " << name.toStdString() << ": " << base_px << " px -> "
                << (found == scaled.end() ? -1 : found->second) << " px\n";
      break;
    }
  }
}

void ui_text_size_scales_every_text_widget_once() {
  TextSizeScope scope;
  constexpr int kPercents[] = {patchy::ui::kMinUiTextSizePercent, 125};
  static_assert(patchy::ui::kMaxUiTextSizePercent >= 125);
  struct Sizes {
    std::map<QString, int> start;
    std::map<QString, int> main;
    std::map<QString, int> preferences;
  };
  const auto measure = [](int percent) {
    SettingsValueRestorer restore_text_size(QStringLiteral("preferences/uiTextSizePercent"));
    patchy::ui::set_stored_ui_text_size_percent(percent);
    Sizes sizes;
    {
      patchy::ui::MainWindow empty;
      show_window_empty(empty);
      empty.resize(1280, 800);
      QApplication::processEvents();
      sizes.start = text_font_sizes(empty);
    }
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1280, 800);
    QApplication::processEvents();
    sizes.main = text_font_sizes(window);
    run_preferences_dialog(window, false, [&](QDialog& dialog) { sizes.preferences = text_font_sizes(dialog); });
    return sizes;
  };
  scope.apply(100);
  const auto base = measure(100);
  const QStringList named{QStringLiteral("QMenuBar"), QStringLiteral("QTabBar"), QStringLiteral("layerList"),
                          QStringLiteral("toolOptions"), QStringLiteral("QStatusBar")};
  int off = 0;
  for (const int percent : kPercents) {
    scope.apply(percent);
    const auto scaled = measure(percent);
    off += count_off_scale_fonts(QStringLiteral("start %1%").arg(percent), base.start, scaled.start, percent);
    off += count_off_scale_fonts(QStringLiteral("main %1%").arg(percent), base.main, scaled.main, percent);
    off += count_off_scale_fonts(QStringLiteral("preferences %1%").arg(percent), base.preferences,
                                 scaled.preferences, percent);
    print_named_font_sizes(QStringLiteral("main %1%").arg(percent), base.main, scaled.main, named);
    print_named_font_sizes(QStringLiteral("preferences %1%").arg(percent), base.preferences, scaled.preferences,
                           {QStringLiteral("QLabel")});
  }
  CHECK(off == 0);
}

// ---------------------------------------------------------------------------------------
// The sweep.

QString plain_text(QString text) {
  text.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
  text.remove(QLatin1Char('&'));
  text.replace(QStringLiteral("\x01"), QStringLiteral("&"));
  // A tab separates a menu shortcut; only the first line of multi-line text is measured.
  return text.section(QLatin1Char('\t'), 0, 0);
}

QString widget_key(const QWidget& widget, const QString& text) {
  const auto name = widget.objectName();
  return QString::fromLatin1(widget.metaObject()->className()) + QLatin1Char('#') +
         (name.isEmpty() ? QLatin1Char('"') + text.left(32) + QLatin1Char('"') : name);
}

// The nearest scroll area up to `root` that holds `widget` in its viewport, or null: its
// children may extend past their parent, since the area scrolls.
QAbstractScrollArea* enclosing_scroll_area(const QWidget& widget, const QWidget& root) {
  for (auto* parent = widget.parentWidget(); parent != nullptr && parent != &root; parent = parent->parentWidget()) {
    if (auto* area = qobject_cast<QAbstractScrollArea*>(parent)) {
      return area;
    }
  }
  return nullptr;
}

bool inside_scrolling_content(const QWidget& widget, const QWidget& root) {
  return enclosing_scroll_area(widget, root) != nullptr;
}

// Text that does not fit the space it was given, under `root`: a label narrower or
// shorter than its text, a button narrower than its label, a field shorter than a line of
// text or narrower than its value, a list row or tab shorter than its text, or a text
// widget pushed past its parent's edge by a layout that ran out of room. Each entry is
// keyed so the same widget can be matched across text sizes.
std::map<QString, QString> text_fit_problems(QWidget& root) {
  std::map<QString, QString> problems;
  const auto add = [&](const QWidget& widget, const QString& text, const QString& detail) {
    problems.emplace(widget_key(widget, text), detail);
  };
  for (auto* widget : root.findChildren<QWidget*>()) {
    if (!widget->isVisible() || widget->width() <= 0 || widget->height() <= 0) {
      continue;
    }
    const QFontMetrics metrics(widget->font());
    const int line_height = metrics.height();
    QString text;
    if (auto* label = qobject_cast<QLabel*>(widget)) {
      text = label->text();
      if (text.isEmpty() || !label->pixmap().isNull() ||
          label->textFormat() == Qt::RichText ||
          (label->textFormat() == Qt::AutoText && Qt::mightBeRichText(text)) || text.contains(QChar(0x2026))) {
        continue;
      }
      const auto contents = label->contentsRect();
      if (label->wordWrap()) {
        const int needed = label->heightForWidth(label->width());
        if (needed > label->height() + 1) {
          add(*label, text, QStringLiteral("wrapped label needs %1px height, has %2").arg(needed).arg(label->height()));
        }
      } else {
        const auto first_line = text.section(QLatin1Char('\n'), 0, 0);
        const int needed = metrics.horizontalAdvance(plain_text(first_line)) + label->margin() * 2;
        if (needed > contents.width() + 1) {
          add(*label, text, QStringLiteral("label needs %1px width, has %2").arg(needed).arg(contents.width()));
        } else if (line_height > label->height() + 1) {
          // The whole label, not its contents rect: a sheet's padding narrows the rect, but
          // the text paints over it.
          add(*label, text,
              QStringLiteral("label needs %1px height, has %2").arg(line_height).arg(label->height()));
        }
      }
    } else if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
      text = plain_text(button->text());
      // Swatches paint a color and keep their name as text for accessibility only.
      if (text.isEmpty() || button->inherits("patchy::ui::ColorWellSwatch")) {
        continue;
      }
      if (auto* tool = qobject_cast<QToolButton*>(button);
          tool != nullptr && tool->toolButtonStyle() == Qt::ToolButtonIconOnly && !tool->icon().isNull()) {
        continue;
      }
      if (button->hasHeightForWidth()) {
        const int needed = button->heightForWidth(button->width());
        if (needed > button->height() + 1) {
          add(*button, text,
              QStringLiteral("wrapping button needs %1px height, has %2").arg(needed).arg(button->height()));
        }
        continue;
      }
      int needed = metrics.horizontalAdvance(text);
      if (qobject_cast<QCheckBox*>(button) != nullptr || qobject_cast<QRadioButton*>(button) != nullptr) {
        needed += 12;
      } else if (!button->icon().isNull()) {
        needed += button->iconSize().width();
      }
      if (needed > button->width()) {
        add(*button, text, QStringLiteral("button needs %1px width, has %2").arg(needed).arg(button->width()));
      } else if (line_height > button->height()) {
        add(*button, text,
            QStringLiteral("button needs %1px height, has %2").arg(line_height).arg(button->height()));
      }
    } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
      text = combo->currentText();
      QStyleOptionComboBox option;
      option.initFrom(combo);
      option.editable = combo->isEditable();
      option.frame = combo->hasFrame();
      option.currentText = text;
      const auto field = combo->style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                        QStyle::SC_ComboBoxEditField, combo);
      const int needed = metrics.horizontalAdvance(text);
      if (line_height > combo->height()) {
        add(*combo, text, QStringLiteral("combo needs %1px height, has %2").arg(line_height).arg(combo->height()));
      } else if (!text.isEmpty() && !combo->isEditable() && needed > field.width() + 2) {
        add(*combo, text,
            QStringLiteral("combo text needs %1px width, has %2").arg(needed).arg(field.width()));
      }
    } else if (auto* spin = qobject_cast<QAbstractSpinBox*>(widget)) {
      auto* edit = spin->findChild<QLineEdit*>();
      text = edit != nullptr ? edit->text() : QString();
      if (line_height > spin->height()) {
        add(*spin, text, QStringLiteral("field needs %1px height, has %2").arg(line_height).arg(spin->height()));
      } else if (edit != nullptr && !text.isEmpty() && metrics.horizontalAdvance(text) > edit->width()) {
        add(*spin, text,
            QStringLiteral("value needs %1px width, has %2").arg(metrics.horizontalAdvance(text)).arg(edit->width()));
      }
      continue;
    } else if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
      if (qobject_cast<QAbstractSpinBox*>(edit->parentWidget()) != nullptr ||
          qobject_cast<QComboBox*>(edit->parentWidget()) != nullptr) {
        continue;
      }
      text = edit->text().isEmpty() ? edit->placeholderText() : edit->text();
      if (line_height > edit->height()) {
        add(*edit, text, QStringLiteral("field needs %1px height, has %2").arg(line_height).arg(edit->height()));
      }
    } else if (auto* tabs = qobject_cast<QTabBar*>(widget)) {
      for (int index = 0; index < tabs->count(); ++index) {
        const auto tab_text = plain_text(tabs->tabText(index));
        const auto rect = tabs->tabRect(index);
        if (tab_text.isEmpty() || !rect.isValid()) {
          continue;
        }
        const QFontMetrics tab_metrics(tabs->font());
        if (tab_metrics.height() > rect.height()) {
          add(*tabs, tab_text,
              QStringLiteral("tab needs %1px height, has %2").arg(tab_metrics.height()).arg(rect.height()));
        }
      }
      continue;
    } else if (auto* view = qobject_cast<QAbstractItemView*>(widget)) {
      auto* model = view->model();
      if (model == nullptr) {
        continue;
      }
      const QFontMetrics view_metrics(view->font());
      for (int row = 0; row < std::min(model->rowCount(view->rootIndex()), 60); ++row) {
        const auto index = model->index(row, 0, view->rootIndex());
        const auto item_text = index.data(Qt::DisplayRole).toString();
        if (item_text.isEmpty() || view->indexWidget(index) != nullptr) {
          continue;
        }
        const auto rect = view->visualRect(index);
        if (!rect.isValid() || !view->viewport()->rect().intersects(rect)) {
          continue;
        }
        if (view_metrics.height() > rect.height() + 1) {
          add(*view, item_text,
              QStringLiteral("row \"%1\" needs %2px height, has %3")
                  .arg(item_text.left(24))
                  .arg(view_metrics.height())
                  .arg(rect.height()));
          break;
        }
      }
      continue;
    } else {
      continue;
    }
    // A text widget past the right edge of a scroll area that does not scroll sideways: the
    // area cuts it off with nothing to bring it into view.
    if (auto* area = enclosing_scroll_area(*widget, root);
        area != nullptr && widget->isVisibleTo(area->viewport()) && !area->horizontalScrollBar()->isVisible() &&
        area->viewport()->isAncestorOf(widget)) {
      const QRect in_viewport(widget->mapTo(area->viewport(), QPoint(0, 0)), widget->size());
      if (in_viewport.right() > area->viewport()->width() + 1) {
        add(*widget, text,
            QStringLiteral("cut off by the right edge of %1 (%2 px past it)")
                .arg(area->objectName().isEmpty() ? QString::fromLatin1(area->metaObject()->className())
                                                  : area->objectName())
                .arg(in_viewport.right() - area->viewport()->width()));
      }
    }
    // A text widget the layout pushed past its parent's edge (outside scrolling content).
    if (auto* parent = widget->parentWidget(); parent != nullptr && widget != &root &&
                                               !inside_scrolling_content(*widget, root) &&
                                               qobject_cast<QToolBar*>(parent) == nullptr) {
      const auto geometry = widget->geometry();
      const auto inside = parent->rect();
      if (geometry.right() > inside.right() + 1 || geometry.bottom() > inside.bottom() + 1 ||
          geometry.left() < inside.left() - 1 || geometry.top() < inside.top() - 1) {
        add(*widget, text,
            QStringLiteral("cut off by %1 (%2x%3 at %4,%5 in %6x%7)")
                .arg(parent->objectName().isEmpty() ? QString::fromLatin1(parent->metaObject()->className())
                                                    : parent->objectName())
                .arg(geometry.width())
                .arg(geometry.height())
                .arg(geometry.x())
                .arg(geometry.y())
                .arg(inside.width())
                .arg(inside.height()));
      }
    }
  }
  return problems;
}

QStringList sweep_list_from_env(const char* name, const QStringList& fallback) {
  const auto value = qEnvironmentVariable(name);
  if (value.trimmed().isEmpty()) {
    return fallback;
  }
  return value.split(QLatin1Char(','), Qt::SkipEmptyParts);
}

// ASCII only: the names travel through scp and shells.
std::string artifact_safe(QString text) {
  for (auto& ch : text) {
    if (ch.unicode() > 127 || !ch.isLetterOrNumber()) {
      ch = QLatin1Char('_');
    }
  }
  return text.left(60).toStdString();
}

// Records the states checked at one size and language, keyed "state | widget".
struct SweepPass {
  int percent{100};
  QString language;
  bool capture{false};
  std::map<QString, QString> problems;
  // Dialogs that need more than a 1366 x 768 screen's room at this size (informational).
  QStringList large_dialogs;
  int states{0};

  void check(const QString& state, QWidget& root) {
    ++states;
    for (auto& [key, detail] : text_fit_problems(root)) {
      problems.emplace(state + QStringLiteral(" | ") + key, detail);
    }
    if (capture) {
      const auto pixmap = root.grab();
      ensure_artifact_dir();
      const auto name = "ui_text_size_" + std::to_string(percent) + "_" + language.toStdString() + "_" +
                        artifact_safe(state) + ".png";
      (void)pixmap.save(QString::fromStdString("test-artifacts/" + name));
    }
  }
};

struct MenuCommand {
  QPointer<QAction> action;
  QString path;
};

void collect_menu_commands(QMenu& menu, const QString& path, std::vector<MenuCommand>& out, int depth = 0) {
  if (depth > 4) {
    return;
  }
  // Menus that fill themselves when shown (recent files, the Window menu).
  Q_EMIT menu.aboutToShow();
  for (auto* action : menu.actions()) {
    if (action->isSeparator() || !action->isVisible()) {
      continue;
    }
    if (auto* sub = action->menu()) {
      collect_menu_commands(*sub, path + QStringLiteral(" > ") + plain_text(sub->title()), out, depth + 1);
      continue;
    }
    const auto text = plain_text(action->text());
    const auto command_path = path + QStringLiteral(" > ") + text;
    // Every "..." command, every filter (their dialogs carry no ellipsis), New and About.
    if (text.endsWith(QStringLiteral("...")) || text.endsWith(QChar(0x2026)) ||
        (path.startsWith(QStringLiteral("Filter > ")) && depth == 1) ||
        command_path == QStringLiteral("File > New") || command_path == QStringLiteral("Help > About Patchy")) {
      out.push_back({action, command_path});
    }
  }
  Q_EMIT menu.aboutToHide();
}

// Commands that leave Patchy (printing, scanning, a file browser, a desktop entry), run
// user code (scripts, plug-ins) or start a canvas session instead of opening a dialog.
bool skip_menu_command(const QString& path) {
  static const QStringList skipped{
      QStringLiteral("Print"),          QStringLiteral("Page Setup"),         QStringLiteral("Scanner"),
      QStringLiteral("Photocopy"),      QStringLiteral("Scan and Divide"),    QStringLiteral("Browse Scripts"),
      QStringLiteral("Applications Menu"), QStringLiteral("Free Transform"),  QStringLiteral("Plugins >"),
      QStringLiteral("Rename Layer"),   QStringLiteral("Open Recent"),
  };
  for (const auto& word : skipped) {
    if (path.contains(word)) {
      return true;
    }
  }
  return path.contains(QStringLiteral("Scripts >")) && !path.contains(QStringLiteral("Script Manager"));
}

QSet<QWidget*> visible_top_levels() {
  QSet<QWidget*> windows;
  for (auto* top : QApplication::topLevelWidgets()) {
    if (top->isVisible()) {
      windows.insert(top);
    }
  }
  return windows;
}

bool is_dialog_window(QWidget* top) {
  return top != nullptr && top->isVisible() && qobject_cast<QMenu*>(top) == nullptr &&
         !top->inherits("QTipLabel") && !top->testAttribute(Qt::WA_TranslucentBackground) &&
         (top->windowFlags() & Qt::WindowType_Mask) != Qt::ToolTip;
}

void close_window(QWidget* top) {
  if (auto* box = qobject_cast<QMessageBox*>(top)) {
    if (box->escapeButton() != nullptr) {
      box->escapeButton()->click();
      return;
    }
    box->done(QMessageBox::Cancel);
    return;
  }
  if (auto* dialog = qobject_cast<QDialog*>(top)) {
    dialog->reject();
    if (dialog->isVisible()) {
      dialog->done(QDialog::Rejected);
    }
    return;
  }
  top->close();
}

// Checks one dialog; Preferences page by page.
void check_dialog(SweepPass& pass, const QString& state, QWidget& top) {
  // Qt's own file dialog: the desktop builds show the platform's.
  if (qobject_cast<QFileDialog*>(&top) != nullptr) {
    return;
  }
  // The offscreen screen is 800 x 800, so the window caps a dialog's growth there
  // (grow_dialog_for_text_size). Check it at the size a 1366 x 768 laptop gives it instead
  // (1366 x 728 past the taskbar): its own size grown to what its content asks for, up to that
  // screen and never under the dialog's minimum. A dialog whose minimum is larger than that
  // screen is listed apart.
  // The size is put back afterwards: dialogs that remember their size would keep the grown
  // one for the rest of the run.
  QSize restore_size;
  const auto restore = qScopeGuard([&] {
    if (restore_size.isValid()) {
      top.resize(restore_size);
    }
  });
  if (auto* dialog = qobject_cast<QDialog*>(&top);
      dialog != nullptr && qobject_cast<QMessageBox*>(dialog) == nullptr && dialog->layout() != nullptr &&
      dialog->layout()->sizeConstraint() != QLayout::SetFixedSize) {
    restore_size = dialog->size();
    auto wanted = dialog->size().expandedTo(dialog->sizeHint()).boundedTo(dialog->maximumSize());
    if (dialog->layout()->hasHeightForWidth()) {
      wanted.setHeight(std::max(wanted.height(), dialog->layout()->totalHeightForWidth(wanted.width())));
    }
    wanted = wanted.boundedTo(QSize(1366, 728)).expandedTo(dialog->minimumSize());
    if (wanted != dialog->size()) {
      dialog->resize(wanted);
      QApplication::processEvents();
    }
    if (dialog->width() > 1366 || dialog->height() > 728) {
      pass.large_dialogs.append(QStringLiteral("%1: %2 x %3").arg(state).arg(dialog->width()).arg(dialog->height()));
    }
  }
  // A message box sizes itself against the screen: never wider than the screen less 480 px
  // (QMessageBox's limit). Given the room a 1366 px screen allows it, its content must fit.
  if (auto* box = qobject_cast<QMessageBox*>(&top); box != nullptr && box->layout() != nullptr) {
    constexpr int kLaptopMessageBoxWidth = 1366 - 480;
    const auto minimum = box->layout()->totalMinimumSize();
    if (minimum.width() > box->width() && minimum.width() <= kLaptopMessageBoxWidth) {
      const int height = box->layout()->hasHeightForWidth()
                             ? std::max(box->height(), box->layout()->totalHeightForWidth(minimum.width()))
                             : std::max(box->height(), minimum.height());
      box->setFixedSize(minimum.width(), height);
      QApplication::processEvents();
    }
    if (box->width() > 1366 || box->height() > 728) {
      pass.large_dialogs.append(QStringLiteral("%1: %2 x %3").arg(state).arg(box->width()).arg(box->height()));
    }
  }
  if (top.objectName() == QStringLiteral("patchyPreferencesDialog")) {
    wait_for_preferences_start_page(top);
    auto* pages = preferences_page_list(top);
    if (pages != nullptr) {
      for (int row = 0; row < pages->count(); ++row) {
        const auto title = pages->item(row)->text();
        (void)select_preferences_page(top, title);
        QApplication::processEvents();
        pass.check(state + QStringLiteral(" page ") + QString::number(row), top);
      }
      return;
    }
  }
  pass.check(state, top);
}

// Runs `trigger` and checks every window it opens, closing each one.
void sweep_command(SweepPass& pass, const std::function<void()>& trigger, const QString& state,
                   const QSet<QWidget*>& before) {
  QSet<QWidget*> handled;
  int dialogs = 0;
  QTimer watcher;
  watcher.setInterval(60);
  std::map<QWidget*, int> seen_ticks;
  QObject::connect(&watcher, &QTimer::timeout, [&] {
    for (auto* top : QApplication::topLevelWidgets()) {
      if (before.contains(top) || !is_dialog_window(top)) {
        continue;
      }
      // Let a dialog finish laying out (and fill lazily built parts) before measuring.
      if (++seen_ticks[top] < 4) {
        continue;
      }
      if (!handled.contains(top)) {
        handled.insert(top);
        const auto name = top->objectName().isEmpty() ? QString::fromLatin1(top->metaObject()->className())
                                                       : top->objectName();
        check_dialog(pass, state + QStringLiteral(" #") + QString::number(dialogs++) + QLatin1Char(' ') + name,
                     *top);
      }
      close_window(top);
      return;
    }
  });
  watcher.start();
  trigger();
  for (int i = 0; i < 20; ++i) {
    QApplication::processEvents();
  }
  // Non-modal windows stay open after the trigger returns.
  const bool closed = process_events_until(
      [&] {
        for (auto* top : QApplication::topLevelWidgets()) {
          if (!before.contains(top) && is_dialog_window(top)) {
            return false;
          }
        }
        return true;
      },
      3000);
  watcher.stop();
  for (auto* top : QApplication::topLevelWidgets()) {
    if (!before.contains(top) && is_dialog_window(top)) {
      if (!closed) {
        std::cerr << "text size sweep: " << state.toStdString() << " left "
                  << top->metaObject()->className() << " '" << top->objectName().toStdString() << "' open\n";
      }
      top->close();
    }
  }
  QApplication::processEvents();
}

const QStringList kSweepTools{
    QStringLiteral("tools.move"),       QStringLiteral("tools.marquee"),     QStringLiteral("tools.lasso"),
    QStringLiteral("tools.magic_wand"), QStringLiteral("tools.quick_select"), QStringLiteral("tools.crop"),
    QStringLiteral("tools.eyedropper"), QStringLiteral("tools.healing"),     QStringLiteral("tools.brush"),
    QStringLiteral("tools.mixer_brush"), QStringLiteral("tools.clone"),      QStringLiteral("tools.eraser"),
    QStringLiteral("tools.gradient"),   QStringLiteral("tools.fill"),        QStringLiteral("tools.blur"),
    QStringLiteral("tools.dodge"),      QStringLiteral("tools.sponge"),      QStringLiteral("tools.pen"),
    QStringLiteral("tools.type"),       QStringLiteral("tools.path_select"), QStringLiteral("tools.rect"),
    QStringLiteral("tools.custom_shape"), QStringLiteral("tools.line"),      QStringLiteral("tools.hand"),
    QStringLiteral("tools.zoom"),       QStringLiteral("tools.ruler"),       QStringLiteral("tools.artboard"),
};

void run_sweep_pass(SweepPass& pass, bool include_menus) {
  // Preferences shows the size the pass runs at, as after a restart with it chosen.
  SettingsValueRestorer restore_text_size(QStringLiteral("preferences/uiTextSizePercent"));
  patchy::ui::set_stored_ui_text_size_percent(pass.percent);
  auto& localization = patchy::ui::LocalizationManager::instance();
  CHECK(localization.set_language(pass.language, false));
  QApplication::processEvents();
  {
    // The home screen, with no document open.
    patchy::ui::MainWindow empty;
    show_window_empty(empty);
    empty.resize(1280, 800);
    QApplication::processEvents();
    pass.check(QStringLiteral("start"), empty);
  }
  patchy::ui::MainWindow window;
  show_window(window);
  window.resize(1280, 800);
  QApplication::processEvents();
  pass.check(QStringLiteral("main"), window);
  for (const auto& tool : kSweepTools) {
    require_hotkey_action(window, tool)->trigger();
    QApplication::processEvents();
    pass.check(QStringLiteral("tool ") + tool, window);
  }
  require_hotkey_action(window, QStringLiteral("tools.move"))->trigger();
  // Every panel tab.
  for (auto* tabs : window.findChildren<QTabBar*>()) {
    if (!tabs->isVisible() || tabs->parentWidget() == nullptr ||
        tabs->parentWidget()->objectName() == QStringLiteral("documentTabs")) {
      continue;
    }
    const int original = tabs->currentIndex();
    for (int index = 0; index < tabs->count(); ++index) {
      tabs->setCurrentIndex(index);
      QApplication::processEvents();
      pass.check(QStringLiteral("panel ") + plain_text(tabs->tabText(index)), window);
    }
    tabs->setCurrentIndex(original);
  }
  // The panels that start collapsed, each opened in turn.
  for (const auto* id : {"propertiesDock", "infoDock", "historyDock"}) {
    patchy::ui::MainWindowTestAccess::show_panel(window, QString::fromLatin1(id));
    QApplication::processEvents();
    pass.check(QStringLiteral("panel ") + QString::fromLatin1(id), window);
  }
  if (!include_menus) {
    return;
  }
  // Every "..." menu command, with the whole canvas selected so selection commands open.
  require_hotkey_action(window, QStringLiteral("select.all"))->trigger();
  QApplication::processEvents();
  std::vector<MenuCommand> commands;
  // Collected in English so the skip list and state names read the same in every language.
  CHECK(localization.set_language(QStringLiteral("en"), false));
  QApplication::processEvents();
  for (auto* action : window.menuBar()->actions()) {
    if (auto* menu = action->menu()) {
      collect_menu_commands(*menu, plain_text(menu->title()), commands);
    }
  }
  CHECK(localization.set_language(pass.language, false));
  QApplication::processEvents();
  const auto before = visible_top_levels();
  for (const auto& command : commands) {
    if (command.action == nullptr || !command.action->isEnabled() || skip_menu_command(command.path)) {
      continue;
    }
    sweep_command(pass, [action = command.action] { if (action != nullptr) action->trigger(); }, command.path, before);
  }
  // The update prompt, which only a startup check opens.
  patchy::ui::UpdateInfo update;
  update.platform = QStringLiteral("windows");
  update.version = QStringLiteral("9.9");
  update.download_url = QUrl(QStringLiteral("https://example.invalid/patchy-setup.exe"));
  sweep_command(pass, [&] { window.show_update_available(update); }, QStringLiteral("Update prompt"), before);
}

// The text-fit sweep at the smallest and largest text size against 100%, per language.
// One language per test, so each stays inside the per-test time limit.
void sweep_language(const QString& language) {
  for (const auto role : {patchy::test::TestFontRole::JapaneseGothic}) {
    patchy::test::register_test_fonts(role);
  }
  TextSizeScope scope;
  // Qt's static pickers (Open Folder's getExistingDirectory) go to the desktop portal inside
  // the Linux Flatpak SDK, where nothing answers a headless run and the call never returns.
  // The sweep skips file dialogs anyway, so keep them on Qt's own widgets, which it closes.
  const bool native_dialogs_were_off = QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
  QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
  const auto restore_native_dialogs = qScopeGuard(
      [native_dialogs_were_off] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native_dialogs_were_off); });
  QList<int> sizes;
  for (const auto& size : sweep_list_from_env(
           "PATCHY_TEXT_SIZE_SIZES", {QString::number(patchy::ui::kMinUiTextSizePercent),
                                      QString::number(patchy::ui::kMaxUiTextSizePercent)})) {
    sizes.append(size.toInt());
  }
  const bool capture = qEnvironmentVariable("PATCHY_TEXT_SIZE_CAPTURE") == QStringLiteral("1");
  const bool menus = qEnvironmentVariable("PATCHY_TEXT_SIZE_MENUS") != QStringLiteral("0");
  QStringList report;
  int new_problems = 0;
  // Written after every pass, so a run cut short still leaves what it found.
  const auto write_report = [&report, &language] {
    ensure_artifact_dir();
    QFile file(QStringLiteral("test-artifacts/ui_text_size_report_%1.txt").arg(language));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
      QTextStream stream(&file);
      for (const auto& line : report) {
        stream << line << '\n';
      }
    }
  };
  SweepPass baseline;
  baseline.language = language;
  baseline.capture = capture && language == QStringLiteral("en");
  scope.apply(100);
  run_sweep_pass(baseline, menus);
  for (const int percent : sizes) {
    SweepPass pass;
    pass.percent = percent;
    pass.language = language;
    pass.capture = capture;
    scope.apply(percent);
    run_sweep_pass(pass, menus);
    for (const auto& [key, detail] : pass.problems) {
      if (baseline.problems.count(key) != 0) {
        continue;
      }
      ++new_problems;
      report.append(QStringLiteral("%1% %2: %3: %4").arg(percent).arg(language, key, detail));
    }
    std::cerr << "text size " << percent << "% " << language.toStdString() << ": " << pass.states
              << " states checked\n";
    for (const auto& large : pass.large_dialogs) {
      report.append(QStringLiteral("note %1% %2: minimum larger than 1366 x 728: %3").arg(percent).arg(language, large));
    }
    write_report();
  }
  CHECK(patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false));
  for (const auto& line : report.mid(0, 400)) {
    std::cerr << line.toStdString() << "\n";
  }
  CHECK(new_problems == 0);
}

}  // namespace

std::vector<patchy::test::TestCase> ui_text_size_tests() {
  return {
      {"ui_text_size_setting_round_trips_and_clamps", ui_text_size_setting_round_trips_and_clamps},
      {"ui_text_size_resolves_stylesheet_font_sizes", ui_text_size_resolves_stylesheet_font_sizes},
      {"ui_text_size_preferences_field_stores_and_notifies", ui_text_size_preferences_field_stores_and_notifies},
      {"ui_text_size_scales_every_text_widget_once", ui_text_size_scales_every_text_widget_once},
      {"ui_text_size_sweep_keeps_text_fitting_en", [] { sweep_language(QStringLiteral("en")); }},
      {"ui_text_size_sweep_keeps_text_fitting_de", [] { sweep_language(QStringLiteral("de")); }},
      {"ui_text_size_sweep_keeps_text_fitting_fr", [] { sweep_language(QStringLiteral("fr")); }},
      {"ui_text_size_sweep_keeps_text_fitting_ru", [] { sweep_language(QStringLiteral("ru")); }},
      {"ui_text_size_sweep_keeps_text_fitting_ja", [] { sweep_language(QStringLiteral("ja")); }},
  };
}
