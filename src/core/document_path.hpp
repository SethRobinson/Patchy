#pragma once

#include "core/vector_shape.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace patchy {

using DocumentPathId = std::uint64_t;

// PSD image-resource homes (docs/vector-tools.md): saved paths occupy
// resources 2000..2997 (name = resource name), the work path is resource 1025
// (no name), and resource 2999 names the clipping path.
inline constexpr std::uint16_t kPsdSavedPathResourceFirst = 2000;
inline constexpr std::uint16_t kPsdSavedPathResourceLast = 2997;
inline constexpr std::uint16_t kPsdWorkPathResourceId = 1025;
inline constexpr std::uint16_t kPsdClippingPathNameResourceId = 2999;

enum class DocumentPathKind : std::uint8_t {
  Saved,
  Work
};

// One Paths-panel entry (a saved path or the work path). Follows the
// DocumentChannel design: MainWindow owns every mutation; content_revision
// keys thumbnail/overlay caches. The original resource payload is kept for
// verbatim re-emit while the path is untouched (the vector dirty-or-verbatim
// rule), shared_ptr-held so whole-Document undo snapshots share one copy.
class DocumentPath {
public:
  DocumentPath() = default;
  DocumentPath(DocumentPathId id, std::string name, DocumentPathKind kind, VectorPath path);

  [[nodiscard]] DocumentPathId id() const noexcept;
  [[nodiscard]] const std::string& name() const noexcept;
  [[nodiscard]] DocumentPathKind kind() const noexcept;
  [[nodiscard]] const VectorPath& path() const noexcept;
  [[nodiscard]] bool is_clipping_path() const noexcept;
  [[nodiscard]] const std::optional<std::uint16_t>& resource_id() const noexcept;
  [[nodiscard]] const std::shared_ptr<const std::vector<std::uint8_t>>& raw_payload() const noexcept;
  [[nodiscard]] bool dirty() const noexcept;
  [[nodiscard]] std::uint64_t content_revision() const noexcept;

  void set_name(std::string name);
  void set_kind(DocumentPathKind kind) noexcept;
  void set_path(VectorPath path);
  void set_clipping_path(bool clipping) noexcept;
  // Import plumbing: records the source resource id and original bytes
  // without marking the path dirty.
  void set_resource_source(std::uint16_t resource_id,
                           std::shared_ptr<const std::vector<std::uint8_t>> payload) noexcept;
  void mark_dirty() noexcept;

private:
  void mark_changed() noexcept;

  DocumentPathId id_{0};
  std::string name_;
  DocumentPathKind kind_{DocumentPathKind::Saved};
  VectorPath path_;
  bool clipping_path_{false};
  std::optional<std::uint16_t> resource_id_;
  std::shared_ptr<const std::vector<std::uint8_t>> raw_payload_;
  bool dirty_{false};
  std::uint64_t content_revision_{0};
};

}  // namespace patchy
