#pragma once

#include "core/document.hpp"

#include <QPixmap>
#include <QWidget>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

class QAction;
class QEvent;
class QListWidget;
class QListWidgetItem;
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
    Spot
  };

  struct Row {
    RowKind kind{RowKind::Composite};
    ChannelId id{0};
    QString name;
    QPixmap thumbnail;
    bool overlay{false};
  };

  using TargetCallback = std::function<void(RowKind, ChannelId, bool overlay)>;
  using ReorderCallback = std::function<void(std::vector<ChannelId>)>;

  explicit ChannelPanel(QWidget* parent = nullptr);

  void set_rows(std::vector<Row> rows, std::optional<Row> selected = std::nullopt);
  void set_document_available(bool available);
  void set_channel_creation_available(bool available);
  void set_actions(QAction* create, QAction* save_selection, QAction* load_selection,
                   QAction* rename, QAction* invert, QAction* remove);
  void set_target_callback(TargetCallback callback);
  void set_reorder_callback(ReorderCallback callback);

  [[nodiscard]] std::optional<Row> selected_row() const;
  [[nodiscard]] std::vector<ChannelId> saved_channel_order() const;

protected:
  void changeEvent(QEvent* event) override;

private:
  static constexpr int kKindRole = Qt::UserRole + 1;
  static constexpr int kChannelIdRole = Qt::UserRole + 2;

  [[nodiscard]] Row row_from_item(const QListWidgetItem* item) const;
  void handle_current_item_changed(QListWidgetItem* current);
  void handle_item_changed(QListWidgetItem* item);
  void handle_rows_moved();
  void refresh_action_states();
  void retranslate_ui();

  QListWidget* list_{nullptr};
  QToolButton* create_button_{nullptr};
  QToolButton* save_selection_button_{nullptr};
  QToolButton* load_selection_button_{nullptr};
  QToolButton* rename_button_{nullptr};
  QToolButton* invert_button_{nullptr};
  QToolButton* remove_button_{nullptr};
  TargetCallback target_callback_;
  ReorderCallback reorder_callback_;
  bool updating_{false};
  bool document_available_{false};
  bool channel_creation_available_{false};
  std::vector<Row> committed_rows_;
  std::vector<std::pair<std::size_t, ChannelId>> fixed_spot_positions_;
};

}  // namespace patchy::ui
