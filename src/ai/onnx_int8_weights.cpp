#include "ai/onnx_int8_weights.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_M_X64) || defined(__x86_64__)
#if defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace patchy::ai {

namespace {

// The few protobuf wire-format pieces an ONNX model needs: a message is a run of fields, each
// a varint key (number << 3 | wire type) and its payload. Unknown fields are copied as they are.
struct Field {
  std::uint32_t number{0};
  int wire{0};  // 0 varint, 1 fixed64, 2 length-delimited, 5 fixed32
  std::uint64_t varint{0};
  std::string_view payload;  // wire 2 only
  std::string_view raw;      // the whole field, key included
};

bool read_varint(std::string_view data, std::size_t& at, std::uint64_t& value) {
  value = 0;
  for (int shift = 0; shift < 64; shift += 7) {
    if (at >= data.size()) {
      return false;
    }
    const auto byte = static_cast<std::uint8_t>(data[at++]);
    value |= static_cast<std::uint64_t>(byte & 0x7fU) << shift;
    if ((byte & 0x80U) == 0) {
      return true;
    }
  }
  return false;
}

bool parse(std::string_view message, std::vector<Field>& fields) {
  fields.clear();
  std::size_t at = 0;
  while (at < message.size()) {
    const auto start = at;
    std::uint64_t key = 0;
    if (!read_varint(message, at, key)) {
      return false;
    }
    Field field;
    field.number = static_cast<std::uint32_t>(key >> 3U);
    field.wire = static_cast<int>(key & 7U);
    if (field.number == 0) {
      return false;
    }
    switch (field.wire) {
      case 0:
        if (!read_varint(message, at, field.varint)) {
          return false;
        }
        break;
      case 1:
        if (message.size() - at < 8) {
          return false;
        }
        at += 8;
        break;
      case 2: {
        std::uint64_t length = 0;
        if (!read_varint(message, at, length) || length > message.size() - at) {
          return false;
        }
        field.payload = message.substr(at, static_cast<std::size_t>(length));
        at += static_cast<std::size_t>(length);
        break;
      }
      case 5:
        if (message.size() - at < 4) {
          return false;
        }
        at += 4;
        break;
      default:
        return false;  // groups (wire types 3 and 4) are not used by ONNX
    }
    field.raw = message.substr(start, at - start);
    fields.push_back(field);
  }
  return true;
}

void write_varint(std::string& out, std::uint64_t value) {
  while (value >= 0x80U) {
    out.push_back(static_cast<char>((value & 0x7fU) | 0x80U));
    value >>= 7U;
  }
  out.push_back(static_cast<char>(value));
}

void write_key(std::string& out, std::uint32_t number, int wire) {
  write_varint(out, (static_cast<std::uint64_t>(number) << 3U) | static_cast<std::uint64_t>(wire));
}

void write_bytes(std::string& out, std::uint32_t number, std::string_view payload) {
  write_key(out, number, 2);
  write_varint(out, payload.size());
  out.append(payload);
}

// ONNX field numbers (onnx.proto3) and tensor element types.
constexpr std::uint32_t kModelGraph = 7;
constexpr std::uint32_t kGraphNode = 1;
constexpr std::uint32_t kGraphInitializer = 5;
constexpr std::uint32_t kGraphSparseInitializer = 15;
constexpr std::uint32_t kGraphInput = 11;
constexpr std::uint32_t kGraphOutput = 12;
constexpr std::uint32_t kGraphValueInfo = 13;
constexpr std::uint32_t kNodeInput = 1;
constexpr std::uint32_t kNodeOpType = 4;
constexpr std::uint32_t kNodeAttribute = 5;
constexpr std::uint32_t kNodeDomain = 7;
constexpr std::uint32_t kAttributeGraph = 6;
constexpr std::uint32_t kAttributeGraphs = 11;
constexpr std::uint32_t kValueInfoName = 1;
constexpr std::uint32_t kTensorDataType = 2;
constexpr std::uint32_t kTensorInt32Data = 5;
constexpr std::uint32_t kTensorName = 8;
constexpr std::uint32_t kTensorRawData = 9;
constexpr std::uint32_t kTensorExternalData = 13;
constexpr std::uint32_t kTensorDataLocation = 14;
constexpr std::uint64_t kTypeUint8 = 2;
constexpr std::uint64_t kTypeInt8 = 3;

struct Node {
  std::string op_type;
  std::string domain;
  std::vector<std::string> inputs;
};

struct Tensor {
  std::string name;
  std::uint64_t data_type{0};
  bool raw{false};
  bool int32{false};
  bool external{false};
};

bool read_node(std::string_view message, Node& node) {
  std::vector<Field> fields;
  if (!parse(message, fields)) {
    return false;
  }
  for (const auto& field : fields) {
    if (field.wire != 2) {
      continue;
    }
    if (field.number == kNodeInput) {
      node.inputs.emplace_back(field.payload);
    } else if (field.number == kNodeOpType) {
      node.op_type = std::string(field.payload);
    } else if (field.number == kNodeDomain) {
      node.domain = std::string(field.payload);
    } else if (field.number == kNodeAttribute) {
      std::vector<Field> attribute;
      if (!parse(field.payload, attribute)) {
        return false;
      }
      for (const auto& part : attribute) {
        if (part.number == kAttributeGraph || part.number == kAttributeGraphs) {
          return false;  // a subgraph could use an initializer this pass cannot see
        }
      }
    }
  }
  return true;
}

bool read_tensor(std::string_view message, Tensor& tensor) {
  std::vector<Field> fields;
  if (!parse(message, fields)) {
    return false;
  }
  for (const auto& field : fields) {
    if (field.number == kTensorName && field.wire == 2) {
      tensor.name = std::string(field.payload);
    } else if (field.number == kTensorDataType && field.wire == 0) {
      tensor.data_type = field.varint;
    } else if (field.number == kTensorRawData) {
      tensor.raw = true;
    } else if (field.number == kTensorInt32Data) {
      tensor.int32 = true;
    } else if (field.number == kTensorExternalData || (field.number == kTensorDataLocation && field.varint != 0)) {
      tensor.external = true;
    }
  }
  return true;
}

// An int8 value stored as an int32 varint (a negative one sign-extended to 64 bits), moved up
// by 128. False when it is not an int8 value.
bool shifted_int8(std::uint64_t stored, std::uint64_t& shifted) {
  const auto value = static_cast<std::int64_t>(stored);
  if (value < -128 || value > 127) {
    return false;
  }
  shifted = static_cast<std::uint64_t>(value + 128);
  return true;
}

// The tensor as uint8, every value + 128. Empty on anything unexpected.
std::optional<std::string> tensor_as_uint8(std::string_view message) {
  std::vector<Field> fields;
  if (!parse(message, fields)) {
    return std::nullopt;
  }
  std::string out;
  out.reserve(message.size() + 64);
  for (const auto& field : fields) {
    if (field.number == kTensorDataType && field.wire == 0) {
      write_key(out, kTensorDataType, 0);
      write_varint(out, kTypeUint8);
    } else if (field.number == kTensorRawData && field.wire == 2) {
      // One byte per int8 value: x + 128 as uint8 is the same byte with its top bit flipped.
      std::string bytes(field.payload);
      for (auto& byte : bytes) {
        byte = static_cast<char>(static_cast<std::uint8_t>(byte) ^ 0x80U);
      }
      write_bytes(out, kTensorRawData, bytes);
    } else if (field.number == kTensorInt32Data && field.wire == 2) {
      std::string packed;
      std::size_t at = 0;
      while (at < field.payload.size()) {
        std::uint64_t stored = 0;
        std::uint64_t shifted = 0;
        if (!read_varint(field.payload, at, stored) || !shifted_int8(stored, shifted)) {
          return std::nullopt;
        }
        write_varint(packed, shifted);
      }
      write_bytes(out, kTensorInt32Data, packed);
    } else if (field.number == kTensorInt32Data && field.wire == 0) {
      std::uint64_t shifted = 0;
      if (!shifted_int8(field.varint, shifted)) {
        return std::nullopt;
      }
      write_key(out, kTensorInt32Data, 0);
      write_varint(out, shifted);
    } else if (field.number == kTensorInt32Data) {
      return std::nullopt;
    } else {
      out.append(field.raw);
    }
  }
  return out;
}

bool is_onnx_domain(const std::string& domain) { return domain.empty() || domain == "ai.onnx"; }

}  // namespace

std::optional<std::string> matmul_integer_weights_as_uint8(std::string_view model) {
  try {
    std::vector<Field> model_fields;
    if (!parse(model, model_fields)) {
      return std::nullopt;
    }
    const Field* graph_field = nullptr;
    for (const auto& field : model_fields) {
      if (field.number == kModelGraph) {
        if (graph_field != nullptr || field.wire != 2) {
          return std::nullopt;
        }
        graph_field = &field;
      }
    }
    if (graph_field == nullptr) {
      return std::nullopt;
    }
    std::vector<Field> graph;
    if (!parse(graph_field->payload, graph)) {
      return std::nullopt;
    }

    std::vector<Node> nodes;
    std::unordered_map<std::string, Tensor> initializers;
    std::unordered_set<std::string> listed;  // names the graph also declares a type for
    for (const auto& field : graph) {
      if (field.wire != 2) {
        continue;
      }
      if (field.number == kGraphNode) {
        Node node;
        if (!read_node(field.payload, node)) {
          return std::nullopt;
        }
        nodes.push_back(std::move(node));
      } else if (field.number == kGraphInitializer) {
        Tensor tensor;
        if (!read_tensor(field.payload, tensor) || tensor.name.empty()) {
          return std::nullopt;
        }
        initializers[tensor.name] = tensor;
      } else if (field.number == kGraphSparseInitializer) {
        return std::nullopt;
      } else if (field.number == kGraphInput || field.number == kGraphOutput || field.number == kGraphValueInfo) {
        std::vector<Field> info;
        if (!parse(field.payload, info)) {
          return std::nullopt;
        }
        for (const auto& part : info) {
          if (part.number == kValueInfoName && part.wire == 2) {
            listed.emplace(part.payload);
          }
        }
      }
    }

    // Every use of every name: (node, input index).
    std::unordered_map<std::string, std::vector<std::pair<std::size_t, std::size_t>>> uses;
    for (std::size_t n = 0; n < nodes.size(); ++n) {
      for (std::size_t i = 0; i < nodes[n].inputs.size(); ++i) {
        if (!nodes[n].inputs[i].empty()) {
          uses[nodes[n].inputs[i]].emplace_back(n, i);
        }
      }
    }
    const auto convertible_tensor = [&](const std::string& name) {
      const auto found = initializers.find(name);
      return found != initializers.end() && found->second.data_type == kTypeInt8 && !found->second.external &&
             (found->second.raw || found->second.int32) && listed.count(name) == 0;
    };
    std::set<std::size_t> candidates;
    for (std::size_t n = 0; n < nodes.size(); ++n) {
      const auto& node = nodes[n];
      // A missing zero point means 0 of the weight's type, which a shift cannot move with it.
      if (node.op_type == "MatMulInteger" && is_onnx_domain(node.domain) && node.inputs.size() >= 4 &&
          convertible_tensor(node.inputs[1]) && convertible_tensor(node.inputs[3])) {
        candidates.insert(n);
      }
    }
    // A tensor is converted only when every use of it is a candidate's weight or zero point;
    // a candidate stays only when both of its tensors are converted. Repeat until stable.
    for (bool changed = true; changed;) {
      changed = false;
      for (auto it = candidates.begin(); it != candidates.end();) {
        bool keep = true;
        for (const auto index : {std::size_t{1}, std::size_t{3}}) {
          for (const auto& [node, input] : uses[nodes[*it].inputs[index]]) {
            if (candidates.count(node) == 0 || (input != 1 && input != 3)) {
              keep = false;
            }
          }
        }
        if (keep) {
          ++it;
        } else {
          it = candidates.erase(it);
          changed = true;
        }
      }
    }
    if (candidates.empty()) {
      return std::nullopt;
    }
    std::unordered_set<std::string> convert;
    for (const auto n : candidates) {
      convert.insert(nodes[n].inputs[1]);
      convert.insert(nodes[n].inputs[3]);
    }

    std::string new_graph;
    new_graph.reserve(graph_field->payload.size() + 4096);
    for (const auto& field : graph) {
      if (field.number == kGraphInitializer && field.wire == 2) {
        Tensor tensor;
        (void)read_tensor(field.payload, tensor);
        if (convert.count(tensor.name) != 0) {
          const auto converted = tensor_as_uint8(field.payload);
          if (!converted.has_value()) {
            return std::nullopt;
          }
          write_bytes(new_graph, kGraphInitializer, *converted);
          continue;
        }
      }
      new_graph.append(field.raw);
    }
    std::string out;
    out.reserve(model.size() + 4096);
    for (const auto& field : model_fields) {
      if (&field == graph_field) {
        write_bytes(out, kModelGraph, new_graph);
      } else {
        out.append(field.raw);
      }
    }
    return out;
  } catch (...) {
    return std::nullopt;
  }
}

bool cpu_int8_matmul_saturates() {
#if defined(_M_X64) || defined(__x86_64__)
  static const bool saturates = [] {
    unsigned int regs[4]{};
    const auto cpuid = [&regs](unsigned int leaf, unsigned int subleaf) {
#if defined(_MSC_VER)
      int out[4]{};
      __cpuidex(out, static_cast<int>(leaf), static_cast<int>(subleaf));
      for (int k = 0; k < 4; ++k) {
        regs[k] = static_cast<unsigned int>(out[k]);
      }
#else
      __cpuid_count(leaf, subleaf, regs[0], regs[1], regs[2], regs[3]);
#endif
    };
    cpuid(0, 0);
    if (regs[0] < 7) {
      return true;
    }
    cpuid(1, 0);
    const bool osxsave = (regs[2] & (1U << 27U)) != 0;
    std::uint64_t xcr0 = 0;
    if (osxsave) {
#if defined(_MSC_VER)
      xcr0 = _xgetbv(0);
#else
      unsigned int eax = 0;
      unsigned int edx = 0;
      __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
      xcr0 = (static_cast<std::uint64_t>(edx) << 32U) | eax;
#endif
    }
    const bool avx_state = (xcr0 & 0x6U) == 0x6U;
    const bool avx512_state = avx_state && (xcr0 & 0xe0U) == 0xe0U;
    cpuid(7, 0);
    const bool avx512f = (regs[1] & (1U << 16U)) != 0;
    const bool avx512_vnni = (regs[2] & (1U << 11U)) != 0;
    cpuid(7, 1);
    const bool avx_vnni = (regs[0] & (1U << 4U)) != 0;
    const bool vnni = (avx_state && avx_vnni) || (avx512_state && avx512f && avx512_vnni);
    return !vnni;
  }();
  return saturates;
#else
  return false;
#endif
}

namespace {

// -1: the CPU decides; 0 and 1: a test's choice.
std::atomic<int> rewrite_override{-1};

}  // namespace

bool int8_weight_rewrite_enabled() {
  const int forced = rewrite_override.load(std::memory_order_relaxed);
  return forced < 0 ? cpu_int8_matmul_saturates() : forced != 0;
}

void set_int8_weight_rewrite_for_testing(std::optional<bool> enabled) {
  rewrite_override.store(enabled.has_value() ? (*enabled ? 1 : 0) : -1, std::memory_order_relaxed);
}

}  // namespace patchy::ai
