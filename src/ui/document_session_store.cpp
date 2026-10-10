#include "ui/document_session_store.hpp"

#include <algorithm>
#include <utility>

namespace patchy::ui {

DocumentSession& DocumentSessionStore::add(Entry session) {
  sessions_.push_back(std::move(session));
  return *sessions_.back();
}

DocumentSessionStore::Entry DocumentSessionStore::remove(const DocumentSession& session) noexcept {
  const auto found = std::find_if(sessions_.begin(), sessions_.end(),
                                  [&session](const Entry& candidate) { return candidate.get() == &session; });
  if (found == sessions_.end()) {
    return nullptr;
  }
  Entry removed = std::move(*found);
  sessions_.erase(found);
  return removed;
}

DocumentSession* DocumentSessionStore::find_by_id(std::int64_t session_id) noexcept {
  const auto found = std::find_if(sessions_.begin(), sessions_.end(), [session_id](const Entry& candidate) {
    return candidate->session_id == session_id;
  });
  return found == sessions_.end() ? nullptr : found->get();
}

const DocumentSession* DocumentSessionStore::find_by_id(std::int64_t session_id) const noexcept {
  const auto found = std::find_if(sessions_.begin(), sessions_.end(), [session_id](const Entry& candidate) {
    return candidate->session_id == session_id;
  });
  return found == sessions_.end() ? nullptr : found->get();
}

DocumentSession* DocumentSessionStore::find_by_canvas(const CanvasWidget* canvas) noexcept {
  if (canvas == nullptr) {
    return nullptr;
  }
  const auto found = std::find_if(sessions_.begin(), sessions_.end(),
                                  [canvas](const Entry& candidate) { return candidate->canvas == canvas; });
  return found == sessions_.end() ? nullptr : found->get();
}

const DocumentSession* DocumentSessionStore::find_by_canvas(const CanvasWidget* canvas) const noexcept {
  if (canvas == nullptr) {
    return nullptr;
  }
  const auto found = std::find_if(sessions_.begin(), sessions_.end(),
                                  [canvas](const Entry& candidate) { return candidate->canvas == canvas; });
  return found == sessions_.end() ? nullptr : found->get();
}

bool DocumentSessionStore::contains(const DocumentSession& session) const noexcept {
  return std::any_of(sessions_.begin(), sessions_.end(),
                     [&session](const Entry& candidate) { return candidate.get() == &session; });
}

std::vector<DocumentSession*> DocumentSessionStore::smart_object_children(std::int64_t parent_session_id) {
  std::vector<DocumentSession*> children;
  for (const auto& candidate : sessions_) {
    if (candidate->smart_object_link.has_value() &&
        candidate->smart_object_link->parent_session_id == parent_session_id) {
      children.push_back(candidate.get());
    }
  }
  return children;
}

std::vector<std::int64_t> DocumentSessionStore::ids() const {
  std::vector<std::int64_t> ids;
  ids.reserve(sessions_.size());
  for (const auto& candidate : sessions_) {
    ids.push_back(candidate->session_id);
  }
  return ids;
}

}  // namespace patchy::ui
