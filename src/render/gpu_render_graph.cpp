#include "render/gpu_render_graph.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace patchy {
namespace {

void set_reason(std::string* reason, std::string value) {
  if (reason != nullptr) {
    *reason = std::move(value);
  }
}

bool valid_id(std::uint32_t id, std::size_t size) {
  return id != 0 && static_cast<std::size_t>(id) <= size;
}

bool has_duplicate_ids(const std::vector<RenderResourceId>& ids) {
  std::unordered_set<RenderResourceId> seen;
  for (const auto id : ids) {
    if (!seen.insert(id).second) {
      return true;
    }
  }
  return false;
}

bool contains_id(const std::vector<RenderResourceId>& ids, RenderResourceId id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

}  // namespace

RenderResourceId RenderGraph::add_resource(std::string name, Rect bounds, RenderPixelFormat format, bool external) {
  resources_.push_back(RenderResourceDescriptor{std::move(name), bounds, format, external});
  return static_cast<RenderResourceId>(resources_.size());
}

RenderPassId RenderGraph::add_pass(std::string name, RenderPassType type, TileKey tile) {
  const auto id = static_cast<RenderPassId>(passes_.size() + 1U);
  passes_.push_back(RenderPassDescriptor{id, std::move(name), type, tile, {}, {}});
  return id;
}

bool RenderGraph::add_read(RenderPassId pass_id, RenderResourceId resource_id) {
  const auto* pass_descriptor = pass(pass_id);
  if (pass_descriptor == nullptr || resource(resource_id) == nullptr) {
    return false;
  }
  auto& target = passes_[static_cast<std::size_t>(pass_id - 1U)].reads;
  if (contains_id(target, resource_id) || contains_id(pass_descriptor->writes, resource_id)) {
    return false;
  }
  target.push_back(resource_id);
  return true;
}

bool RenderGraph::add_write(RenderPassId pass_id, RenderResourceId resource_id) {
  const auto* pass_descriptor = pass(pass_id);
  if (pass_descriptor == nullptr || resource(resource_id) == nullptr) {
    return false;
  }
  auto& target = passes_[static_cast<std::size_t>(pass_id - 1U)].writes;
  if (contains_id(target, resource_id) || contains_id(pass_descriptor->reads, resource_id)) {
    return false;
  }
  target.push_back(resource_id);
  return true;
}

const RenderResourceDescriptor* RenderGraph::resource(RenderResourceId id) const noexcept {
  return valid_id(id, resources_.size()) ? &resources_[static_cast<std::size_t>(id - 1U)] : nullptr;
}

const RenderPassDescriptor* RenderGraph::pass(RenderPassId id) const noexcept {
  return valid_id(id, passes_.size()) ? &passes_[static_cast<std::size_t>(id - 1U)] : nullptr;
}

const std::vector<RenderResourceDescriptor>& RenderGraph::resources() const noexcept {
  return resources_;
}

const std::vector<RenderPassDescriptor>& RenderGraph::passes() const noexcept {
  return passes_;
}

bool RenderGraph::validate(std::string* reason) const {
  for (std::size_t index = 0; index < resources_.size(); ++index) {
    const auto& descriptor = resources_[index];
    if (descriptor.name.empty()) {
      set_reason(reason, "render resource has an empty name");
      return false;
    }
    if (descriptor.bounds.width <= 0 || descriptor.bounds.height <= 0) {
      set_reason(reason, "render resource has empty bounds");
      return false;
    }
  }

  std::unordered_map<RenderResourceId, RenderPassId> writers;
  for (std::size_t pass_index = 0; pass_index < passes_.size(); ++pass_index) {
    const auto& descriptor = passes_[pass_index];
    if (descriptor.id == 0 || descriptor.id != static_cast<RenderPassId>(pass_index + 1U)) {
      set_reason(reason, "render pass ids are not contiguous");
      return false;
    }
    if (descriptor.name.empty()) {
      set_reason(reason, "render pass has an empty name");
      return false;
    }
    if (has_duplicate_ids(descriptor.reads) || has_duplicate_ids(descriptor.writes)) {
      set_reason(reason, "render pass contains a duplicate resource reference");
      return false;
    }
    if (descriptor.type == RenderPassType::Clear && descriptor.reads.size() != 0U) {
      set_reason(reason, "clear pass cannot read a resource");
      return false;
    }
    if (descriptor.type == RenderPassType::Clear && descriptor.writes.empty()) {
      set_reason(reason, "clear pass must write a resource");
      return false;
    }
    if (descriptor.type == RenderPassType::Readback && descriptor.reads.empty()) {
      set_reason(reason, "readback pass must read a resource");
      return false;
    }
    if (descriptor.type == RenderPassType::Readback && !descriptor.writes.empty()) {
      set_reason(reason, "readback pass cannot write a resource");
      return false;
    }
    if (descriptor.tile.mip < 0) {
      set_reason(reason, "render pass has a negative mip level");
      return false;
    }
    if (descriptor.type != RenderPassType::Clear && descriptor.type != RenderPassType::Readback &&
        (descriptor.reads.empty() || descriptor.writes.empty())) {
      set_reason(reason, "render pass needs at least one read and one write");
      return false;
    }
    for (const auto id : descriptor.reads) {
      if (resource(id) == nullptr) {
        set_reason(reason, "render pass reads an unknown resource");
        return false;
      }
    }
    for (const auto id : descriptor.writes) {
      if (resource(id) == nullptr) {
        set_reason(reason, "render pass writes an unknown resource");
        return false;
      }
      const auto [_, inserted] = writers.emplace(id, descriptor.id);
      if (!inserted) {
        set_reason(reason, "render resource has multiple writers");
        return false;
      }
    }
  }
  return true;
}

std::vector<RenderPassId> RenderGraph::execution_order(std::string* reason) const {
  std::vector<RenderPassId> order;
  if (!validate(reason)) {
    return order;
  }

  const auto pass_count = passes_.size();
  std::vector<std::vector<std::size_t>> outgoing(pass_count);
  std::vector<std::size_t> indegree(pass_count, 0U);
  std::unordered_map<RenderResourceId, std::size_t> writer_index;
  std::unordered_map<RenderResourceId, std::vector<std::size_t>> readers;

  for (std::size_t index = 0; index < pass_count; ++index) {
    const auto& descriptor = passes_[index];
    for (const auto resource_id : descriptor.writes) {
      writer_index.emplace(resource_id, index);
    }
    for (const auto resource_id : descriptor.reads) {
      readers[resource_id].push_back(index);
    }
  }

  for (const auto& [resource_id, writer] : writer_index) {
    const auto found = readers.find(resource_id);
    if (found == readers.end()) {
      continue;
    }
    for (const auto reader : found->second) {
      outgoing[writer].push_back(reader);
      ++indegree[reader];
    }
  }

  std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
  for (std::size_t index = 0; index < pass_count; ++index) {
    if (indegree[index] == 0U) {
      ready.push(index);
    }
  }
  while (!ready.empty()) {
    const auto current = ready.top();
    ready.pop();
    order.push_back(passes_[current].id);
    for (const auto next : outgoing[current]) {
      if (--indegree[next] == 0U) {
        ready.push(next);
      }
    }
  }

  if (order.size() != pass_count) {
    order.clear();
    set_reason(reason, "render graph contains a dependency cycle");
  }
  return order;
}

void RenderGraph::clear() noexcept {
  resources_.clear();
  passes_.clear();
}

}  // namespace patchy
