#pragma once

// Ownership of the open document sessions, apart from MainWindow: the list itself,
// the lookups by session id and by canvas, and the id snapshots callers take before a
// loop that can close documents. Sessions are referred to by id, never by stored
// pointer: remove() frees the DocumentSession, so a pointer taken from a lookup is
// good only until the next close. Iteration order is creation order (tab order is
// the tab widget's business), which several callers rely on
// (docs/code-organization.md, "Session lifetime").

#include "ui/document_session.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace patchy::ui {

class CanvasWidget;

class DocumentSessionStore {
public:
  using Entry = std::unique_ptr<DocumentSession>;
  using iterator = std::vector<Entry>::iterator;
  using const_iterator = std::vector<Entry>::const_iterator;
  using reverse_iterator = std::vector<Entry>::reverse_iterator;
  using const_reverse_iterator = std::vector<Entry>::const_reverse_iterator;

  // Appends and publishes `session` (callers finish installing its canvas first:
  // once stored, event handlers may resolve it).
  DocumentSession& add(Entry session);
  // Removes the entry owning `session` and hands it back, so the caller decides
  // when the DocumentSession dies (the end of the statement, usually). Null when
  // `session` is not stored, which a teardown handler that already closed it can
  // cause.
  [[nodiscard]] Entry remove(const DocumentSession& session) noexcept;

  [[nodiscard]] DocumentSession* find_by_id(std::int64_t session_id) noexcept;
  [[nodiscard]] const DocumentSession* find_by_id(std::int64_t session_id) const noexcept;
  // The session whose canvas is `canvas`; null for a null canvas or a canvas no
  // session owns (one being torn down, or a script canvas window's).
  [[nodiscard]] DocumentSession* find_by_canvas(const CanvasWidget* canvas) noexcept;
  [[nodiscard]] const DocumentSession* find_by_canvas(const CanvasWidget* canvas) const noexcept;
  [[nodiscard]] bool contains(const DocumentSession& session) const noexcept;
  // Edit Smart Object Contents child sessions of `parent_session_id`, in creation order.
  [[nodiscard]] std::vector<DocumentSession*> smart_object_children(std::int64_t parent_session_id);
  // Every session id in creation order: the snapshot to iterate over when the loop
  // body may close documents.
  [[nodiscard]] std::vector<std::int64_t> ids() const;

  [[nodiscard]] bool empty() const noexcept { return sessions_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return sessions_.size(); }
  [[nodiscard]] const Entry& at(std::size_t index) const { return sessions_.at(index); }
  [[nodiscard]] iterator begin() noexcept { return sessions_.begin(); }
  [[nodiscard]] iterator end() noexcept { return sessions_.end(); }
  [[nodiscard]] const_iterator begin() const noexcept { return sessions_.begin(); }
  [[nodiscard]] const_iterator end() const noexcept { return sessions_.end(); }
  [[nodiscard]] reverse_iterator rbegin() noexcept { return sessions_.rbegin(); }
  [[nodiscard]] reverse_iterator rend() noexcept { return sessions_.rend(); }
  [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return sessions_.rbegin(); }
  [[nodiscard]] const_reverse_iterator rend() const noexcept { return sessions_.rend(); }

private:
  std::vector<Entry> sessions_;
};

}  // namespace patchy::ui
