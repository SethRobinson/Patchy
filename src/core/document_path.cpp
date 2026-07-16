#include "core/document_path.hpp"

#include <utility>

namespace patchy {

DocumentPath::DocumentPath(DocumentPathId id, std::string name, DocumentPathKind kind, VectorPath path)
    : id_(id), name_(std::move(name)), kind_(kind), path_(std::move(path)) {}

DocumentPathId DocumentPath::id() const noexcept {
  return id_;
}

const std::string& DocumentPath::name() const noexcept {
  return name_;
}

DocumentPathKind DocumentPath::kind() const noexcept {
  return kind_;
}

const VectorPath& DocumentPath::path() const noexcept {
  return path_;
}

bool DocumentPath::is_clipping_path() const noexcept {
  return clipping_path_;
}

const std::optional<std::uint16_t>& DocumentPath::resource_id() const noexcept {
  return resource_id_;
}

const std::shared_ptr<const std::vector<std::uint8_t>>& DocumentPath::raw_payload() const noexcept {
  return raw_payload_;
}

bool DocumentPath::dirty() const noexcept {
  return dirty_;
}

std::uint64_t DocumentPath::content_revision() const noexcept {
  return content_revision_;
}

void DocumentPath::set_name(std::string name) {
  if (name_ == name) {
    return;
  }
  name_ = std::move(name);
  mark_dirty();
}

void DocumentPath::set_kind(DocumentPathKind kind) noexcept {
  if (kind_ == kind) {
    return;
  }
  kind_ = kind;
  mark_dirty();
}

void DocumentPath::set_path(VectorPath path) {
  path_ = std::move(path);
  mark_dirty();
}

void DocumentPath::set_clipping_path(bool clipping) noexcept {
  if (clipping_path_ == clipping) {
    return;
  }
  clipping_path_ = clipping;
  mark_dirty();
}

void DocumentPath::set_resource_source(std::uint16_t resource_id,
                                       std::shared_ptr<const std::vector<std::uint8_t>> payload) noexcept {
  resource_id_ = resource_id;
  raw_payload_ = std::move(payload);
}

void DocumentPath::mark_dirty() noexcept {
  dirty_ = true;
  mark_changed();
}

void DocumentPath::reset_dirty() noexcept {
  dirty_ = false;
}

void DocumentPath::mark_changed() noexcept {
  ++content_revision_;
}

}  // namespace patchy
