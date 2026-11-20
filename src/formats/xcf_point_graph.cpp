// A finite subset of the graph strings stored by GIMP, calibrated against its saved renders.
// Only operations on the incoming pixel are representable. Nothing here invokes GEGL.
#include "formats/xcf_internal.hpp"
#include "core/pixel_depth.hpp"

#include <array>
#include <map>

namespace patchy::xcf::detail {
namespace {

enum class Operation { InvertLinear, InvertGamma, Over };
struct Node {
  Operation operation;
  int input;
  int aux;
};
constexpr std::size_t kMaxSamples = 32;

class GraphReader {
public:
  explicit GraphReader(std::string_view text) : rest_(text) {}

  std::optional<PointOperation> read() {
    const int last = chain(0, 0);
    if (last < 0 || !rest_.empty() || nodes_.empty()) return std::nullopt;
    return [nodes = std::move(nodes_), last](float* rgba) {
      // Keep the intermediate samples in straight linear RGB and coverage alpha.
      std::array<std::array<float, 4>, kMaxSamples> pixels;
      for (int c = 0; c < 3; ++c) pixels[0][c] = static_cast<float>(srgb_decode(rgba[c]));
      pixels[0][3] = rgba[3];
      for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        const auto& input = pixels[node.input];
        auto& out = pixels[i + 1];
        if (node.operation == Operation::Over) {
          const auto& source = pixels[node.aux];
          const float background = input[3] * (1.0F - source[3]);
          out[3] = background + source[3];
          for (int c = 0; c < 3; ++c) {
            out[c] = out[3] > 0.0F ? (source[c] * source[3] + input[c] * background) / out[3] : 0.0F;
          }
        } else {
          for (int c = 0; c < 3; ++c) {
            out[c] = node.operation == Operation::InvertLinear ? 1.0F - input[c]
                : static_cast<float>(srgb_decode(1.0 - srgb_encode(input[c])));
          }
          out[3] = input[3];
        }
      }
      for (int c = 0; c < 3; ++c) rgba[c] = static_cast<float>(srgb_encode(pixels[last][c]));
      rgba[3] = pixels[last][3];
    };
  }

private:
  std::string_view token() {
    const auto first = rest_.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
      rest_ = {};
      return {};
    }
    rest_.remove_prefix(first);
    const auto boundary = rest_.find_first_of(" \t\r\n=[]");
    const auto word = rest_.substr(0, boundary == 0 ? 1 : boundary);
    rest_.remove_prefix(word.size());
    return word;
  }

  int chain(int current, int depth) {
    if (depth > 8) return -1;
    for (;;) {
      auto op = token();
      if (op.empty()) return depth == 0 ? current : -1;
      if (op == "]") return depth > 0 ? current : -1;
      if (op == "id" || op == "ref") {
        if (token() != "=") return -1;
        const auto label = token();
        if (label.empty() || label.find_first_of("=[]") != std::string_view::npos) return -1;
        if (op == "id") {
          if (current < 0 || !named_.emplace(label, current).second) return -1;
        } else {
          const auto it = named_.find(label);
          if (it == named_.end()) return -1;
          current = it->second;
        }
        continue;
      }
      if (current < 0) return -1;
      if (op.starts_with("gegl:")) op.remove_prefix(5);
      Node node{Operation::InvertLinear, current, 0};
      if (op == "over" || op == "svg:src-over") {
        if (token() != "aux" || token() != "=" || token() != "[") return -1;
        node.operation = Operation::Over;
        node.aux = chain(-1, depth + 1);
        if (node.aux < 0) return -1;
      } else if (op == "invert-gamma") {
        node.operation = Operation::InvertGamma;
      } else if (op != "invert-linear") {
        return -1;
      }
      if (nodes_.size() + 1 >= kMaxSamples) return -1;
      nodes_.push_back(node);
      current = static_cast<int>(nodes_.size());
    }
  }

  std::string_view rest_;
  std::map<std::string_view, int> named_;
  std::vector<Node> nodes_;
};

}  // namespace

std::optional<PointOperation> parse_point_graph(std::string_view text) {
  if (text.empty() || text.size() > 4096) return std::nullopt;
  return GraphReader(text).read();
}

}  // namespace patchy::xcf::detail
