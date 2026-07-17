#pragma once

#include "core/document_path.hpp"

#include <QPixmap>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QAction;
class QEvent;
class QListWidget;
class QListWidgetItem;
class QObject;
class QPoint;

namespace patchy::ui {

// The Paths dock (tabified with Channels): saved paths, the work path (italic,
// sorted last), and a transient row for the active layer's shape or vector
// mask path. Presentation-only like ChannelPanel - MainWindow owns every
// document mutation and supplies the outline thumbnails.
class PathsPanel final : public QWidget {
  Q_OBJECT

public:
  enum class RowKind {
    LayerPath,  // transient: the active layer's shape / vector-mask path
    SavedPath,
    WorkPath
  };

  struct Row {
    RowKind kind{RowKind::SavedPath};
    DocumentPathId id{0};  // 0 for LayerPath
    QString name;
    QPixmap thumbnail;
    bool clipping{false};  // the document's clipping path (underlined name)
  };

  using TargetCallback = std::function<void(RowKind, DocumentPathId)>;
  using DeselectCallback = std::function<void()>;
  using RenameCallback = std::function<void(DocumentPathId, QString)>;
  using SaveWorkPathCallback = std::function<void()>;
  using LoadSelectionCallback = std::function<void(RowKind, DocumentPathId)>;
  using ReorderCallback = std::function<void(std::vector<DocumentPathId>)>;

  explicit PathsPanel(QWidget* parent = nullptr);

  void set_rows(std::vector<Row> rows, std::optional<Row> selected = std::nullopt);
  void set_document_available(bool available);
  void set_actions(QAction* new_path, QAction* fill_path, QAction* stroke_path,
                   QAction* make_selection, QAction* from_selection, QAction* duplicate_path,
                   QAction* clipping_path, QAction* delete_path);
  void set_target_callback(TargetCallback callback);
  void set_deselect_callback(DeselectCallback callback);
  void set_rename_callback(RenameCallback callback);
  void set_save_work_path_callback(SaveWorkPathCallback callback);
  void set_load_selection_callback(LoadSelectionCallback callback);
  void set_reorder_callback(ReorderCallback callback);

  [[nodiscard]] std::optional<Row> selected_row() const;
  // Starts an inline rename of the saved path's row (used right after the work
  // path is saved so the user can name it immediately).
  void begin_rename(DocumentPathId id);

protected:
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  static constexpr int kKindRole = Qt::UserRole + 1;
  static constexpr int kPathIdRole = Qt::UserRole + 2;
  static constexpr int kClippingRole = Qt::UserRole + 3;

  [[nodiscard]] Row row_from_item(const QListWidgetItem* item) const;
  void handle_current_item_changed(QListWidgetItem* current);
  void handle_item_double_clicked(QListWidgetItem* item);
  void handle_item_changed(QListWidgetItem* item);
  void handle_rows_moved();
  [[nodiscard]] std::vector<DocumentPathId> saved_path_order() const;
  void show_context_menu(const QPoint& position);
  void refresh_action_states();

  QListWidget* list_{nullptr};
  QAction* new_path_action_{nullptr};
  QAction* fill_path_action_{nullptr};
  QAction* stroke_path_action_{nullptr};
  QAction* make_selection_action_{nullptr};
  QAction* from_selection_action_{nullptr};
  QAction* duplicate_path_action_{nullptr};
  QAction* clipping_path_action_{nullptr};
  QAction* delete_path_action_{nullptr};
  TargetCallback target_callback_;
  DeselectCallback deselect_callback_;
  RenameCallback rename_callback_;
  SaveWorkPathCallback save_work_path_callback_;
  LoadSelectionCallback load_selection_callback_;
  ReorderCallback reorder_callback_;
  std::vector<Row> committed_rows_;
  bool updating_{false};
  bool document_available_{false};
  bool ctrl_load_mouse_press_{false};
};

}  // namespace patchy::ui
