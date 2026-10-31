#include "ui/command_search.hpp"

#include "ui/hotkey_registry.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHash>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <memory>
#include <utility>

namespace patchy::ui {

namespace {

constexpr int kPopupWidth = 680;
constexpr int kVisibleRows = 12;
constexpr int kRowPadding = 6;
constexpr int kCheckColumn = 22;
constexpr int kButtonWidth = 30;
constexpr int kButtonGap = 2;

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kShortcutRole = Qt::UserRole + 2;
constexpr int kCheckRole = Qt::UserRole + 3;  // -1 not checkable, 0 off, 1 on
constexpr int kEntryRole = Qt::UserRole + 4;
constexpr int kDescriptionRole = Qt::UserRole + 5;

const QString kPathSeparator = QStringLiteral(" › ");

struct CommandDescription {
  const char* id;  // a hotkey command id (persisted, never renamed)
  const char* text;
};

// Short "what it does" lines for the commonly searched commands (GitHub issue 554
// follow-up): the search matches them, so a user who types "resize" or "add new layer" finds
// Image Size or New Layer. Commands with a status tip that says more than the name use it
// instead; the rest show no description.
constexpr CommandDescription kCommandDescriptions[] = {
    // File
    {"file.new", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Create a new blank image")},
    {"file.open", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Open an image file")},
    {"file.open_clipboard", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "New image from the clipboard")},
    {"file.open_folder", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Open every image in a folder")},
    {"file.save", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save the document")},
    {"file.save_as", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save to a new file or format")},
    {"file.export_flat", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save a flattened copy")},
    {"file.export_for_web", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Optimize and save for the web")},
    {"file.export_animated_gif", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save the layers as a GIF animation")},
    {"file.export_sprite_sheet", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Combine the layers into one sheet")},
    {"file.place_embedded", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Insert an image as a Smart Object")},
    {"file.place_linked", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Insert a linked image file")},
    {"file.import_files_as_layers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Open several images as layers")},
    {"file.import_scanner", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Scan an image")},
    {"file.file_info", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Author, copyright and other metadata")},
    {"file.strip_metadata", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove EXIF and other metadata")},
    {"file.print", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Print the image")},
    {"file.close", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Close the document")},
    {"file.preferences", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Settings and options")},
    {"file.quit", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Exit Patchy")},
    // Edit
    {"edit.undo", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Take back the last change")},
    {"edit.redo", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Redo the change you took back")},
    {"edit.cut", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move the selection to the clipboard")},
    {"edit.copy", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Copy the selection to the clipboard")},
    {"edit.copy_merged", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Copy what all visible layers show")},
    {"edit.paste", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Paste from the clipboard")},
    {"edit.paste_in_place", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Paste at the same position")},
    {"edit.fill", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fill with a color or pattern")},
    {"edit.stroke_selection", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw an outline along the selection")},
    {"edit.free_transform", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Scale, rotate or move freely")},
    {"edit.transform_scale", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Resize the layer or selection")},
    {"edit.transform_rotate", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Turn the layer or selection")},
    {"edit.warp_transform", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Bend the layer with a grid")},
    {"edit.remove_object", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Erase an object and fill the gap")},
    {"edit.define_brush_tip", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make a brush from the selection")},
    {"edit.define_custom_shape", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make a custom shape from a path")},
    {"edit.convert_to_profile", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Change the color profile")},
    {"edit.presets_gradients", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Manage gradient presets")},
    {"edit.presets_patterns", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Manage pattern presets")},
    // Image
    {"image.size", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Resize or scale the image")},
    {"image.canvas_size", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add or remove space around the image")},
    {"image.crop_to_selection", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Crop the image to the selection")},
    {"image.trim", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove empty borders")},
    {"image.levels", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Set the black point, white point and midtones")},
    {"image.curves", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Adjust the tones with a curve")},
    {"image.hue_saturation", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Change colors, saturation and lightness")},
    {"image.color_balance", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Shift the colors of shadows and highlights")},
    {"image.black_white", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Turn the image gray")},
    {"image.vibrance", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make muted colors stronger")},
    {"image.exposure", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Brighten or darken the image")},
    {"image.auto_all", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fix tone and color automatically")},
    {"image.mode_indexed", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Reduce the image to a color palette")},
    {"image.mode_16_bit", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Use 16 bits per channel")},
    // Layer
    {"layer.new_with_dialog", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add New Layer")},
    {"layer.new", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add an empty layer at once")},
    {"layer.duplicate", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Copy the layer")},
    {"layer.delete", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove the layer")},
    {"layer.rename", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Change the layer name")},
    {"layer.new_folder", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Put the selected layers in a folder")},
    {"layer.via_copy", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Copy the selection to a new layer")},
    {"layer.via_cut", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move the selection to a new layer")},
    {"layer.merge_down", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Combine with the layer below")},
    {"layer.merge_visible", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Combine all visible layers")},
    {"layer.add_mask", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add a layer mask")},
    {"layer.apply_mask", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Bake the mask into the pixels")},
    {"layer.styles", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Drop shadow, stroke, glow and other effects")},
    {"layer.toggle_clipping_mask", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Clip to the layer below")},
    {"layer.toggle_visibility", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show or hide layers")},
    {"layer.lock_layers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Protect layers from changes")},
    {"layer.move_up", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move the layer up the stack")},
    {"layer.move_down", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move the layer down the stack")},
    {"layer.fill", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fill with the foreground color")},
    {"layer.fill_background", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fill with the background color")},
    {"layer.clear", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Erase the layer or selection")},
    // Select
    {"select.all", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select the whole image")},
    {"select.deselect", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Clear the selection")},
    {"select.reselect", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Bring back the last selection")},
    {"select.inverse", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Swap selected and unselected areas")},
    {"select.all_layers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select every layer")},
    {"select.expand", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make the selection larger")},
    {"select.contract", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make the selection smaller")},
    {"select.border", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select a band along the edge")},
    {"select.grow", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add neighboring similar colors")},
    {"select.similar", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add similar colors anywhere")},
    {"select.quick_mask", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Paint a selection as a mask")},
    // View
    {"view.zoom_in", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Magnify the view")},
    {"view.zoom_out", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show more of the image")},
    {"view.fit_on_screen", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the whole image")},
    {"view.actual_pixels", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Zoom to 100%")},
    {"view.rulers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show or hide the rulers")},
    {"view.grid", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show or hide the grid")},
    {"view.guides", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show or hide the guides")},
    {"view.snap", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Line up with guides and edges while dragging")},
    {"view.pixel_grid", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the pixel grid when zoomed in")},
    {"view.new_guide", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add a guide line")},
    {"view.clear_guides", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove all guides")},
    // Tools
    {"tools.move", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move layers and selections")},
    {"tools.marquee", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select a rectangle")},
    {"tools.elliptical_marquee", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select an ellipse or circle")},
    {"tools.lasso", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw a freehand selection")},
    {"tools.polygonal_lasso", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select with straight edges")},
    {"tools.magnetic_lasso", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select along edges it finds")},
    {"tools.magic_wand", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select similar colors with a click")},
    {"tools.quick_select", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Paint a selection that finds edges")},
    {"tools.object_select", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select an object automatically")},
    {"tools.brush", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Paint with a brush")},
    {"tools.mixer_brush", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Mix paint with the colors under it")},
    {"tools.clone", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Copy pixels from one place to another")},
    {"tools.pattern_stamp", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Paint with a pattern")},
    {"tools.healing", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Repair blemishes with sampled pixels")},
    {"tools.spot_healing", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove spots with one click")},
    {"tools.patch", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Replace an area with pixels from elsewhere")},
    {"tools.red_eye", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fix red eyes in photos")},
    {"tools.smudge", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Smear paint")},
    {"tools.dodge", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Lighten areas")},
    {"tools.burn", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Darken areas")},
    {"tools.sponge", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Change the color saturation")},
    {"tools.blur", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Soften areas")},
    {"tools.sharpen", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Sharpen areas")},
    {"tools.eraser", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Erase pixels")},
    {"tools.gradient", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw a color gradient")},
    {"tools.fill", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fill an area with color")},
    {"tools.line", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw a straight line")},
    {"tools.rect", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw a rectangle")},
    {"tools.ellipse", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw an ellipse or circle")},
    {"tools.polygon", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw a polygon or star")},
    {"tools.custom_shape", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw a custom shape")},
    {"tools.eyedropper", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Pick a color from the image")},
    {"tools.ruler", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Measure distances and angles")},
    {"tools.type", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add text")},
    {"tools.hand", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Scroll the view")},
    {"tools.zoom", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Zoom in and out")},
    {"tools.pen", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw paths and curves")},
    {"tools.path_select", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select and move whole shapes")},
    {"tools.direct_select", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Edit points and handles")},
    {"tools.add_anchor", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add a point to a path")},
    {"tools.delete_anchor", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove a point from a path")},
    {"tools.convert_point", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch points between corner and smooth")},
    {"tools.crop", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Crop or straighten the image")},
    {"tools.perspective_crop", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Crop and fix the perspective")},
    {"tools.artboard", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add and arrange artboards")},
};

// The non-menu hotkey categories the search lists, with their row path. Panel commands
// (channels, paths), theme and script commands are left out: they act on a panel's target
// or are listed in their menu.
QString search_category_path(const QString& category) {
  if (category == QStringLiteral("tools")) {
    return QCoreApplication::translate("patchy::ui::HotkeyEditorPanel", "Tools");
  }
  if (category == QStringLiteral("color")) {
    return QCoreApplication::translate("patchy::ui::HotkeyEditorPanel", "Color");
  }
  if (category == QStringLiteral("brush")) {
    return QCoreApplication::translate("patchy::ui::HotkeyEditorPanel", "Brush");
  }
  return {};
}

bool search_lists_category(const QString& category) {
  return category.isEmpty() || category == QStringLiteral("tools") || category == QStringLiteral("color") ||
         category == QStringLiteral("brush");
}

void walk_menu(QMenu* menu, bool visible_only, const QString& top, QStringList& submenus,
               const std::function<void(QAction*, const QString&, const QStringList&)>& visit) {
  for (auto* action : menu->actions()) {
    if (action == nullptr || action->isSeparator() || qobject_cast<QWidgetAction*>(action) != nullptr) {
      continue;
    }
    if (visible_only && !action->isVisible()) {
      continue;
    }
    if (auto* submenu = action->menu(); submenu != nullptr) {
      if (visible_only && !action->isEnabled()) {
        continue;
      }
      submenus.push_back(clean_action_text(action));
      walk_menu(submenu, visible_only, top, submenus, visit);
      submenus.pop_back();
      continue;
    }
    visit(action, top, submenus);
  }
}

// The menu path in the muted color, then the name ("Filter › Blur › Gaussian Blur"), then the
// description in muted italics, the shortcut at the right edge; a check mark in the left
// column for a checked command. The name keeps its full width first; the path elides from
// the left, the description from the right.
class CommandRowDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
    return {kPopupWidth, option.fontMetrics.height() + 2 * kRowPadding};
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    const auto rect = option.rect;
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    if (selected) {
      painter->fillRect(rect, theme().menu_item_selected_bg);
    } else if ((option.state & QStyle::State_MouseOver) != 0) {
      painter->fillRect(rect, theme().list_row_hover_bg);
    }
    const QColor primary = selected ? theme().text_on_accent : theme().text_primary;
    const QColor muted = selected ? theme().text_on_accent : theme().text_secondary;
    const auto& metrics = option.fontMetrics;
    const int baseline = rect.top() + (rect.height() - metrics.height()) / 2 + metrics.ascent();

    if (index.data(kCheckRole).toInt() == 1) {
      painter->setRenderHint(QPainter::Antialiasing);
      painter->setPen(QPen(primary, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      const QPointF center(rect.left() + kCheckColumn / 2.0 + 1, rect.center().y() + 0.5);
      QPainterPath check;
      check.moveTo(center + QPointF(-4.0, 0.0));
      check.lineTo(center + QPointF(-1.2, 3.0));
      check.lineTo(center + QPointF(4.0, -3.5));
      painter->drawPath(check);
    }

    int right = rect.right() - 2 * kRowPadding;
    const auto shortcut = index.data(kShortcutRole).toString();
    if (!shortcut.isEmpty()) {
      const int width = metrics.horizontalAdvance(shortcut);
      painter->setPen(muted);
      painter->drawText(right - width, baseline, shortcut);
      right -= width + 3 * kRowPadding;
    }
    int x = rect.left() + kCheckColumn;
    const auto full_name = index.data(Qt::DisplayRole).toString();
    const auto path = index.data(kPathRole).toString();
    if (!path.isEmpty()) {
      const int path_room = right - x - metrics.horizontalAdvance(full_name);
      // Left out when only its ellipsis would fit: the name needs the whole row.
      if (path_room > metrics.horizontalAdvance(QStringLiteral("…") + kPathSeparator)) {
        const auto shown = metrics.elidedText(path + kPathSeparator, Qt::ElideLeft, path_room);
        painter->setPen(muted);
        painter->drawText(x, baseline, shown);
        x += metrics.horizontalAdvance(shown);
      }
    }
    const auto name = metrics.elidedText(full_name, Qt::ElideRight, right - x);
    painter->setPen(primary);
    painter->drawText(x, baseline, name);
    x += metrics.horizontalAdvance(name) + 3 * kRowPadding;
    const auto description = index.data(kDescriptionRole).toString();
    if (!description.isEmpty() && right - x > 4 * metrics.averageCharWidth()) {
      auto italic = option.font;
      italic.setItalic(true);
      const QFontMetrics italic_metrics(italic);
      painter->setFont(italic);
      painter->setPen(muted);
      painter->drawText(x, baseline, italic_metrics.elidedText(description, Qt::ElideRight, right - x));
    }
    painter->restore();
  }
};

// Keeps typing in the field: Up/Down/PageUp/PageDown move the list, Enter runs the
// chosen command, Esc closes.
class SearchKeyFilter final : public QObject {
public:
  SearchKeyFilter(QListWidget* list, std::function<void()> run, QObject* parent)
      : QObject(parent), list_(list), run_(std::move(run)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::ShortcutOverride) {
      // Plain typing belongs to the field, never an application shortcut.
      const auto* key = static_cast<const QKeyEvent*>(event);
      if (key->key() == Qt::Key_Escape || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        event->accept();
        return true;
      }
    }
    if (event->type() == QEvent::KeyPress) {
      const auto* key = static_cast<const QKeyEvent*>(event);
      switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
          QApplication::sendEvent(list_, event);
          return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
          run_();
          return true;
        case Qt::Key_Escape:
          if (auto* popup = list_->window(); popup != nullptr) {
            popup->close();
          }
          return true;
        default:
          break;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QListWidget* list_;
  std::function<void()> run_;
};

// Places the menu bar's search button right after the last visible menu title.
class SearchButtonPlacer final : public QObject {
public:
  SearchButtonPlacer(QMenuBar* bar, QToolButton* button) : QObject(button), bar_(bar), button_(button) {
    bar_->installEventFilter(this);
  }

  void place() {
    if (bar_ == nullptr || button_ == nullptr) {
      return;
    }
    QAction* last = nullptr;
    for (auto* action : bar_->actions()) {
      if (action->isVisible() && action->menu() != nullptr) {
        last = action;
      }
    }
    const auto title = last != nullptr ? bar_->actionGeometry(last) : QRect();
    // A title the bar moved into its overflow (">>") menu is not at its own rectangle.
    const bool title_shown = title.isValid() && bar_->actionAt(title.center()) == last;
    int right_limit = bar_->width();
    if (const auto* controls =
            bar_->findChild<QWidget*>(QStringLiteral("windowChromeControls"), Qt::FindDirectChildrenOnly);
        controls != nullptr && controls->isVisible()) {
      right_limit = controls->x();
    }
    const int x = title.right() + 1 + kButtonGap;
    if (bar_->isNativeMenuBar() || !title_shown || x + kButtonWidth > right_limit) {
      button_->hide();
      return;
    }
    button_->setGeometry(x, 0, kButtonWidth, bar_->height());
    button_->show();
    button_->raise();
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == bar_) {
      switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::LayoutRequest:
        case QEvent::LanguageChange:
        case QEvent::FontChange:
        case QEvent::StyleChange:
        case QEvent::ActionAdded:
        case QEvent::ActionChanged:
        case QEvent::ActionRemoved:
          // The bar lays its titles out when it next handles the change; place after it.
          if (!pending_) {
            pending_ = true;
            QTimer::singleShot(0, this, [this] {
              pending_ = false;
              place();
            });
          }
          break;
        default:
          break;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QPointer<QMenuBar> bar_;
  QPointer<QToolButton> button_;
  bool pending_{false};
};

// The written description, else the status tip when it is more than the name restated
// ("Apply Gaussian Blur to the active layer" is not).
QString entry_description(const QAction* action, const QString& id, const QString& name) {
  if (!id.isEmpty()) {
    if (auto written = command_search_description(id); !written.isEmpty()) {
      return written;
    }
  }
  const auto tip = action != nullptr ? action->statusTip().simplified() : QString();
  if (tip.isEmpty() || tip.contains(name, Qt::CaseInsensitive)) {
    return {};
  }
  return tip;
}

// A description matches a word only at a word start: "move" must not find "Remove EXIF".
bool contains_word_start(const QString& text, const QString& word) {
  for (qsizetype at = text.indexOf(word); at >= 0; at = text.indexOf(word, at + 1)) {
    if (at == 0 || !text.at(at - 1).isLetterOrNumber()) {
      return true;
    }
  }
  return false;
}

}  // namespace

QString strip_menu_ellipsis(QString label) {
  while (label.endsWith(QStringLiteral("..."))) {
    label.chop(3);
  }
  while (label.endsWith(QChar(0x2026))) {
    label.chop(1);
  }
  return label.trimmed();
}

void for_each_menu_bar_command(
    const QMenuBar* menu_bar, bool visible_only,
    const std::function<void(QAction* action, const QString& top, const QStringList& submenus)>& visit) {
  if (menu_bar == nullptr) {
    return;
  }
  for (auto* top_action : menu_bar->actions()) {
    auto* menu = top_action->menu();
    if (menu == nullptr || (visible_only && !top_action->isVisible())) {
      continue;
    }
    QStringList submenus;
    walk_menu(menu, visible_only, clean_action_text(top_action), submenus, visit);
  }
}

std::vector<MenuCommandEntry> collect_menu_commands(const QMenuBar* menu_bar) {
  std::vector<MenuCommandEntry> entries;
  QSet<const QAction*> seen;
  for_each_menu_bar_command(menu_bar, true, [&](QAction* action, const QString& top, const QStringList& submenus) {
    if (!action->isEnabled() || seen.contains(action)) {
      return;
    }
    const auto name = strip_menu_ellipsis(clean_action_text(action));
    if (name.isEmpty()) {
      return;
    }
    seen.insert(action);
    auto path = top;
    for (const auto& submenu : submenus) {
      path += kPathSeparator + strip_menu_ellipsis(submenu);
    }
    entries.push_back({action, name, path});
  });
  return entries;
}

QString command_search_description(const QString& id) {
  for (const auto& entry : kCommandDescriptions) {
    if (id == QLatin1String(entry.id)) {
      return QCoreApplication::translate("patchy::ui::CommandSearch", entry.text);
    }
  }
  return {};
}

QStringList command_search_described_ids() {
  QStringList ids;
  for (const auto& entry : kCommandDescriptions) {
    ids << QLatin1String(entry.id);
  }
  return ids;
}

std::vector<MenuCommandEntry> collect_search_commands(const QMenuBar* menu_bar, const HotkeyRegistry& registry) {
  QHash<const QAction*, QString> id_by_action;
  for (const auto& command : registry.commands()) {
    if (command.action != nullptr && !id_by_action.contains(command.action.data())) {
      id_by_action.insert(command.action.data(), command.id);
    }
  }
  auto entries = collect_menu_commands(menu_bar);
  for (auto& entry : entries) {
    entry.description = entry_description(entry.action.data(), id_by_action.value(entry.action.data()), entry.name);
  }
  // Everything the menus hold, hidden places included, so a command inside a disabled
  // submenu is not listed again as a key outside the menus.
  QSet<const QAction*> in_menus;
  for_each_menu_bar_command(menu_bar, false, [&in_menus](QAction* action, const QString&, const QStringList&) {
    in_menus.insert(action);
  });
  QSet<const QAction*> listed;
  // The tools first, in palette order, then the other keys outside the menus.
  const auto add_registered = [&](bool tools) {
    for (const auto& command : registry.commands()) {
      const auto* action = command.action.data();
      if (action == nullptr || (command.category == QStringLiteral("tools")) != tools ||
          !search_lists_category(command.category) || in_menus.contains(action) || listed.contains(action) ||
          !action->isEnabled() || action->menu() != nullptr) {
        continue;
      }
      const auto name = strip_menu_ellipsis(clean_action_text(action));
      if (name.isEmpty()) {
        continue;
      }
      listed.insert(action);
      entries.push_back({command.action, name, search_category_path(command.category),
                         entry_description(action, command.id, name)});
    }
  };
  add_registered(true);
  add_registered(false);
  return entries;
}

std::vector<int> match_menu_commands(const std::vector<MenuCommandEntry>& entries, const QString& query) {
  const auto words = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  const auto phrase = words.join(QLatin1Char(' '));
  std::vector<std::pair<int, int>> ranked;  // (tier, index)
  for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
    const auto& entry = entries[static_cast<std::size_t>(index)];
    const auto name = entry.name.toLower();
    const auto path = entry.path.toLower();
    const auto description = entry.description.toLower();
    bool all = true;
    for (const auto& word : words) {
      if (!name.contains(word) && !path.contains(word) && !contains_word_start(description, word)) {
        all = false;
        break;
      }
    }
    if (!all) {
      continue;
    }
    // Only the exact and starts-with matches move up; everything else keeps menu bar order.
    int tier = 2;
    if (phrase.isEmpty() || name == phrase || description == phrase) {
      tier = 0;
    } else if (name.startsWith(phrase) || description.startsWith(phrase)) {
      tier = 1;
    }
    ranked.emplace_back(tier, index);
  }
  std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<int> result;
  result.reserve(ranked.size());
  for (const auto& [tier, index] : ranked) {
    result.push_back(index);
  }
  return result;
}

QFrame* show_command_search_popup(QWidget* parent, std::vector<MenuCommandEntry> entries, QPoint top_left) {
  auto* popup = new QFrame(parent, Qt::Popup);
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setObjectName(QStringLiteral("commandSearchPopup"));
  popup->setFrameShape(QFrame::StyledPanel);
  set_themed_style(*popup, QStringLiteral(R"(
    QFrame#commandSearchPopup {
      background: @menu_bg;
      border: 1px solid @menu_border;
    }
    QFrame#commandSearchPopup QListWidget {
      background: @menu_bg;
      color: @text_primary;
      border: 0;
      outline: none;
    }
  )"));

  auto* layout = new QVBoxLayout(popup);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);

  auto* search = new QLineEdit(popup);
  search->setObjectName(QStringLiteral("commandSearchEdit"));
  search->setPlaceholderText(QCoreApplication::translate("patchy::ui::CommandSearch", "Search commands and tools"));
  search->setClearButtonEnabled(true);
  layout->addWidget(search);

  auto* list = new QListWidget(popup);
  list->setObjectName(QStringLiteral("commandSearchList"));
  list->setItemDelegate(new CommandRowDelegate(list));
  list->setUniformItemSizes(true);
  list->setSelectionMode(QAbstractItemView::SingleSelection);
  list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list->setFocusPolicy(Qt::NoFocus);
  list->setMouseTracking(true);
  list->viewport()->setAttribute(Qt::WA_Hover, true);
  // Polished first: the application sheet's QListWidget min-height would otherwise replace
  // this minimum when the popup shows, and its font sets the row height.
  list->ensurePolished();
  const int row_height = list->fontMetrics().height() + 2 * kRowPadding;
  list->setFixedHeight(row_height * kVisibleRows + 2 * list->frameWidth());
  layout->addWidget(list);

  auto shared_entries = std::make_shared<std::vector<MenuCommandEntry>>(std::move(entries));
  const auto refill = [list, shared_entries](const QString& query) {
    list->clear();
    for (const int index : match_menu_commands(*shared_entries, query)) {
      const auto& entry = (*shared_entries)[static_cast<std::size_t>(index)];
      if (entry.action == nullptr) {
        continue;
      }
      auto* item = new QListWidgetItem(entry.name, list);
      item->setData(kPathRole, entry.path);
      item->setData(kShortcutRole, action_shortcut_text(entry.action.data()));
      item->setData(kCheckRole, entry.action->isCheckable() ? (entry.action->isChecked() ? 1 : 0) : -1);
      item->setData(kEntryRole, index);
      if (!entry.description.isEmpty()) {
        item->setData(kDescriptionRole, entry.description);
        item->setToolTip(entry.description);
      }
    }
    if (list->count() > 0) {
      list->setCurrentRow(0);
    }
  };
  const auto run = [popup, list, shared_entries] {
    auto* item = list->currentItem();
    if (item == nullptr) {
      return;
    }
    const QPointer<QAction> action = (*shared_entries)[static_cast<std::size_t>(item->data(kEntryRole).toInt())].action;
    popup->close();
    if (action == nullptr) {
      return;
    }
    // After the popup is gone, so a command that opens a dialog or a menu never fights
    // the popup's grab.
    QTimer::singleShot(0, action.data(), [action] {
      if (action != nullptr && action->isEnabled()) {
        action->trigger();
      }
    });
  };
  QObject::connect(search, &QLineEdit::textChanged, popup, refill);
  QObject::connect(list, &QListWidget::itemClicked, popup, [run] { run(); });
  search->installEventFilter(new SearchKeyFilter(list, run, search));
  refill(QString());

  popup->resize(kPopupWidth, popup->sizeHint().height());
  auto position = top_left;
  const auto* screen = parent != nullptr ? parent->screen() : QGuiApplication::screenAt(top_left);
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  if (screen != nullptr) {
    const auto available = screen->availableGeometry();
    position.setX(std::clamp(position.x(), available.left(),
                             std::max(available.left(), available.right() - popup->width() + 1)));
    position.setY(std::clamp(position.y(), available.top(),
                             std::max(available.top(), available.bottom() - popup->height() + 1)));
  }
  popup->move(position);
  popup->show();
  search->setFocus(Qt::PopupFocusReason);
  return popup;
}

QToolButton* add_menu_bar_search_button(QMenuBar* menu_bar, QAction* action) {
  auto* button = new QToolButton(menu_bar);
  button->setObjectName(QStringLiteral("menuBarCommandSearchButton"));
  button->setDefaultAction(action);
  button->setFocusPolicy(Qt::NoFocus);
  button->setAutoRaise(true);
  button->setIconSize(QSize(16, 16));
  set_themed_style(*button, QStringLiteral(R"(
    QToolButton#menuBarCommandSearchButton {
      background: transparent;
      border: 0;
      padding: 0;
      min-width: 0;
      min-height: 0;
    }
    QToolButton#menuBarCommandSearchButton:hover,
    QToolButton#menuBarCommandSearchButton:pressed {
      background: @menu_bar_item_hover_bg;
    }
  )"));
  auto* placer = new SearchButtonPlacer(menu_bar, button);
  placer->place();
  return button;
}

}  // namespace patchy::ui
