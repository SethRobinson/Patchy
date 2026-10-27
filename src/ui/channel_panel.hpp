#pragma once

#include "core/document.hpp"

#include <QIcon>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QWidget>

#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

class QAction;
class QEvent;
class QListWidget;
class QListWidgetItem;
class QObject;
class QPoint;
class QToolButton;

namespace patchy::ui {

// The Channels dock is deliberately a presentation-only widget. MainWindow
// owns all document/history mutations and supplies revision-cached thumbnails;
// this keeps list rebuilds from accidentally taking mutable Document access.
class ChannelPanel final : public QWidget {
  Q_OBJECT

public:
  enum class RowKind {
    Composite,
    Red,
    Green,
    Blue,
    Alpha,
    Spot,
    QuickMask,
    // Ink plates of the CMYK proof (docs/cmyk.md); listed only while Proof Colors is on.
    Cyan,
    Magenta,
    Yellow,
    Black
  };

  struct Row {
    RowKind kind{RowKind::Composite};
    ChannelId id{0};
    QString name;
    QPixmap thumbnail;
    bool overlay{false};
    // The row's eye (docs/channels.md, "Channel visibility"): on, off, or none (Quick Mask).
    // Saved channels show their overlay state.
    std::optional<bool> eye;
  };

  using TargetCallback = std::function<void(RowKind, ChannelId, bool overlay)>;
  using LoadSelectionCallback = std::function<void(RowKind, ChannelId)>;
  using ReorderCallback = std::function<void(std::vector<ChannelId>)>;
  // The eye of a Composite, component or ink plate row was clicked.
  using EyeCallback = std::function<void(RowKind)>;

  // Width of the eye column at the left of each row's icon.
  static constexpr int kEyeColumnWidth = 22;
  static constexpr QSize kThumbnailSize{42, 30};

  explicit ChannelPanel(QWidget* parent = nullptr);

  void set_rows(std::vector<Row> rows, std::optional<Row> selected = std::nullopt);
  void set_document_available(bool available);
  void set_channel_creation_available(bool available);
  void set_actions(QAction* create, QAction* save_selection, QAction* load_selection,
                   QAction* rename, QAction* invert, QAction* remove);
  // Context-menu-only actions: a new layer from the selected row, as grayscale values or
  // in the channel's own color (component and ink plate rows only).
  void set_layer_actions(QAction* gray_layer, QAction* color_layer);
  void set_target_callback(TargetCallback callback);
  void set_load_selection_callback(LoadSelectionCallback callback);
  void set_reorder_callback(ReorderCallback callback);
  void set_eye_callback(EyeCallback callback);
  // The key each row's channel shortcut shows on its right (docs/channels.md), by row
  // kind; a kind left out shows none. Kept across list rebuilds.
  void set_row_shortcuts(std::map<RowKind, QString> shortcuts);
  // Replaces the thumbnail of a derived row (Composite, a component, an ink plate) in
  // place, without rebuilding the list.
  void set_row_thumbnail(RowKind kind, const QPixmap& thumbnail);
  // The eye hit area of a row in list viewport coordinates (empty without an eye).
  [[nodiscard]] QRect eye_rect(int row) const;
  // A row's eye state (none for Quick Mask) and thumbnail, as shown.
  [[nodiscard]] std::optional<bool> row_eye(int row) const;
  [[nodiscard]] QPixmap row_thumbnail(int row) const;
  [[nodiscard]] QString row_shortcut(int row) const;

  [[nodiscard]] std::optional<Row> selected_row() const;
  [[nodiscard]] std::vector<ChannelId> saved_channel_order() const;

protected:
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  static constexpr int kKindRole = Qt::UserRole + 1;
  static constexpr int kChannelIdRole = Qt::UserRole + 2;
  static constexpr int kThumbnailRole = Qt::UserRole + 3;
  // -1: no eye, 0: off, 1: on.
  static constexpr int kEyeRole = Qt::UserRole + 4;
  // The shortcut text drawn right-aligned on the row (empty: none).
  static constexpr int kShortcutRole = Qt::UserRole + 5;

  [[nodiscard]] QIcon row_icon(const QPixmap& thumbnail, int eye) const;
  void refresh_row_icons();
  // A press in a row's eye column: true when it was handled.
  bool handle_eye_press(QPoint viewport_position);
  void toggle_saved_channel_overlay(QListWidgetItem* item);

  [[nodiscard]] Row row_from_item(const QListWidgetItem* item) const;
  void handle_current_item_changed(QListWidgetItem* current);
  void handle_rows_moved();
  void show_context_menu(const QPoint& position);
  void refresh_action_states();
  void retranslate_ui();

  QListWidget* list_{nullptr};
  QToolButton* create_button_{nullptr};
  QToolButton* save_selection_button_{nullptr};
  QToolButton* load_selection_button_{nullptr};
  QToolButton* rename_button_{nullptr};
  QToolButton* invert_button_{nullptr};
  QToolButton* remove_button_{nullptr};
  QAction* gray_layer_action_{nullptr};
  QAction* color_layer_action_{nullptr};
  TargetCallback target_callback_;
  LoadSelectionCallback load_selection_callback_;
  ReorderCallback reorder_callback_;
  EyeCallback eye_callback_;
  bool eye_mouse_press_{false};
  QListWidgetItem* reselect_item_{nullptr};
  bool updating_{false};
  bool ctrl_load_mouse_press_{false};
  bool document_available_{false};
  bool channel_creation_available_{false};
  std::vector<Row> committed_rows_;
  std::map<RowKind, QString> row_shortcuts_;
  std::vector<std::pair<std::size_t, ChannelId>> fixed_spot_positions_;
};

}  // namespace patchy::ui
