#include "formats/metadata/jumbf.hpp"

#include <cstring>
#include <optional>
#include <string_view>
#include <utility>

namespace patchy::metadata {
namespace {

constexpr int kMaxDepth = 16;
constexpr std::size_t kMaxItems = 20000;

struct CborItem {
  enum class Type : std::uint8_t { Unsigned, Negative, Bytes, Text, Array, Map, Tag, Simple, Float, Invalid };
  Type type{Type::Invalid};
  std::uint64_t number{0};
  double real{0.0};
  std::string text;
  std::size_t byte_size{0};
  std::vector<CborItem> items;                            // Array, or [tagged item] for Tag
  std::vector<std::pair<CborItem, CborItem>> entries;     // Map
};

struct CborReader {
  std::span<const std::uint8_t> bytes;
  std::size_t pos{0};
  std::size_t item_count{0};
  bool failed{false};

  std::optional<std::uint64_t> argument(std::uint8_t info) {
    if (info < 24) {
      return info;
    }
    std::size_t size = 0;
    if (info == 24) {
      size = 1;
    } else if (info == 25) {
      size = 2;
    } else if (info == 26) {
      size = 4;
    } else if (info == 27) {
      size = 8;
    } else {
      return std::nullopt;
    }
    if (pos + size > bytes.size()) {
      failed = true;
      return std::nullopt;
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i) {
      value = (value << 8) | bytes[pos + i];
    }
    pos += size;
    return value;
  }

  CborItem read(int depth) {
    CborItem item;
    if (failed || depth > kMaxDepth || pos >= bytes.size() || ++item_count > kMaxItems) {
      failed = true;
      return item;
    }
    const auto initial = bytes[pos++];
    const auto major = static_cast<std::uint8_t>(initial >> 5);
    const auto info = static_cast<std::uint8_t>(initial & 0x1F);
    const bool indefinite = info == 31;
    std::optional<std::uint64_t> length;
    if (!indefinite && major != 7) {
      length = argument(info);
      if (!length) {
        failed = true;
        return item;
      }
    }
    switch (major) {
      case 0:
        item.type = CborItem::Type::Unsigned;
        item.number = *length;
        break;
      case 1:
        item.type = CborItem::Type::Negative;
        item.number = *length;
        break;
      case 2:
      case 3: {
        item.type = major == 2 ? CborItem::Type::Bytes : CborItem::Type::Text;
        const auto append_chunk = [&](std::uint64_t size) {
          if (size > bytes.size() - pos) {
            failed = true;
            return;
          }
          if (major == 3) {
            item.text.append(reinterpret_cast<const char*>(bytes.data() + pos), static_cast<std::size_t>(size));
          }
          item.byte_size += static_cast<std::size_t>(size);
          pos += static_cast<std::size_t>(size);
        };
        if (!indefinite) {
          append_chunk(*length);
        } else {
          while (!failed && pos < bytes.size() && bytes[pos] != 0xFF) {
            const auto chunk_initial = bytes[pos++];
            const auto chunk_length = argument(static_cast<std::uint8_t>(chunk_initial & 0x1F));
            if (!chunk_length) {
              failed = true;
              break;
            }
            append_chunk(*chunk_length);
          }
          ++pos;  // break byte
        }
        break;
      }
      case 4:
        item.type = CborItem::Type::Array;
        if (!indefinite) {
          for (std::uint64_t i = 0; i < *length && !failed; ++i) {
            item.items.push_back(read(depth + 1));
          }
        } else {
          while (!failed && pos < bytes.size() && bytes[pos] != 0xFF) {
            item.items.push_back(read(depth + 1));
          }
          ++pos;
        }
        break;
      case 5:
        item.type = CborItem::Type::Map;
        if (!indefinite) {
          for (std::uint64_t i = 0; i < *length && !failed; ++i) {
            auto key = read(depth + 1);
            auto value = read(depth + 1);
            item.entries.emplace_back(std::move(key), std::move(value));
          }
        } else {
          while (!failed && pos < bytes.size() && bytes[pos] != 0xFF) {
            auto key = read(depth + 1);
            auto value = read(depth + 1);
            item.entries.emplace_back(std::move(key), std::move(value));
          }
          ++pos;
        }
        break;
      case 6:
        item.type = CborItem::Type::Tag;
        item.number = *length;
        item.items.push_back(read(depth + 1));
        break;
      case 7:
        if (info == 25 || info == 26 || info == 27) {
          const auto raw = argument(info);
          if (!raw) {
            break;
          }
          item.type = CborItem::Type::Float;
          if (info == 27) {
            std::memcpy(&item.real, &*raw, sizeof(double));
          } else if (info == 26) {
            const auto bits = static_cast<std::uint32_t>(*raw);
            float value = 0.0F;
            std::memcpy(&value, &bits, sizeof(value));
            item.real = value;
          } else {
            item.real = 0.0;  // half floats: shown as 0; C2PA never uses them
          }
        } else {
          item.type = CborItem::Type::Simple;
          item.number = info < 24 ? info : argument(info).value_or(0);
        }
        break;
      default:
        failed = true;
        break;
    }
    return item;
  }
};

void render(const CborItem& item, std::string& out, std::size_t max_chars) {
  if (out.size() >= max_chars) {
    return;
  }
  switch (item.type) {
    case CborItem::Type::Unsigned:
      out += std::to_string(item.number);
      break;
    case CborItem::Type::Negative:
      out += "-" + std::to_string(item.number + 1);
      break;
    case CborItem::Type::Bytes:
      out += "<" + std::to_string(item.byte_size) + " bytes>";
      break;
    case CborItem::Type::Text:
      out += "\"" + item.text + "\"";
      break;
    case CborItem::Type::Array:
      out += "[";
      for (std::size_t i = 0; i < item.items.size(); ++i) {
        if (i > 0) {
          out += ", ";
        }
        render(item.items[i], out, max_chars);
      }
      out += "]";
      break;
    case CborItem::Type::Map:
      out += "{";
      for (std::size_t i = 0; i < item.entries.size(); ++i) {
        if (i > 0) {
          out += ", ";
        }
        render(item.entries[i].first, out, max_chars);
        out += ": ";
        render(item.entries[i].second, out, max_chars);
      }
      out += "}";
      break;
    case CborItem::Type::Tag:
      if (!item.items.empty()) {
        render(item.items.front(), out, max_chars);
      }
      break;
    case CborItem::Type::Simple:
      out += item.number == 20 ? "false" : item.number == 21 ? "true" : item.number == 22 ? "null" : "undefined";
      break;
    case CborItem::Type::Float:
      out += std::to_string(item.real);
      break;
    case CborItem::Type::Invalid:
      out += "?";
      break;
  }
  if (out.size() > max_chars) {
    out.resize(max_chars);
    out += "...";
  }
}

const CborItem* map_value(const CborItem& map, std::string_view key) {
  if (map.type != CborItem::Type::Map) {
    return nullptr;
  }
  for (const auto& [entry_key, value] : map.entries) {
    if (entry_key.type == CborItem::Type::Text && entry_key.text == key) {
      return &value;
    }
  }
  return nullptr;
}

std::string text_of(const CborItem* item) {
  if (item == nullptr) {
    return {};
  }
  if (item->type == CborItem::Type::Text) {
    return item->text;
  }
  if (item->type == CborItem::Type::Map) {
    // softwareAgent / claim_generator_info: {name, version}
    std::string text = text_of(map_value(*item, "name"));
    const auto version = text_of(map_value(*item, "version"));
    if (!version.empty()) {
      text += " " + version;
    }
    return text;
  }
  return {};
}

std::string short_source_type(const std::string& uri) {
  const auto slash = uri.find_last_of('/');
  return slash == std::string::npos ? uri : uri.substr(slash + 1);
}

struct BoxWalker {
  JumbfSummary& summary;
  int boxes{0};

  void walk(std::span<const std::uint8_t> data, int depth, const std::string& parent_label) {
    std::size_t pos = 0;
    std::string label = parent_label;
    while (pos + 8 <= data.size() && depth <= kMaxDepth && ++boxes < 4096) {
      std::uint64_t size = (static_cast<std::uint32_t>(data[pos]) << 24) |
                           (static_cast<std::uint32_t>(data[pos + 1]) << 16) |
                           (static_cast<std::uint32_t>(data[pos + 2]) << 8) | data[pos + 3];
      const std::string type(reinterpret_cast<const char*>(data.data() + pos + 4), 4);
      std::size_t header = 8;
      if (size == 1) {
        if (pos + 16 > data.size()) {
          return;
        }
        size = 0;
        for (int i = 0; i < 8; ++i) {
          size = (size << 8) | data[pos + 8 + static_cast<std::size_t>(i)];
        }
        header = 16;
      } else if (size == 0) {
        size = data.size() - pos;
      }
      if (size < header || size > data.size() - pos) {
        return;
      }
      const auto payload = data.subspan(pos + header, static_cast<std::size_t>(size) - header);
      if (type == "jumb") {
        walk(payload, depth + 1, label);
      } else if (type == "jumd" && payload.size() >= 17) {
        const auto toggles = payload[16];
        if ((toggles & 0x02) != 0) {
          std::size_t end = 17;
          while (end < payload.size() && payload[end] != 0) {
            ++end;
          }
          label.assign(reinterpret_cast<const char*>(payload.data() + 17), end - 17);
          summary.labels.push_back(label);
        }
      } else if (type == "cbor") {
        CborReader reader{payload};
        const auto root = reader.read(0);
        if (label.starts_with("c2pa.claim")) {
          auto generator = text_of(map_value(root, "claim_generator"));
          if (generator.empty()) {
            if (const auto* info = map_value(root, "claim_generator_info")) {
              generator = info->type == CborItem::Type::Array && !info->items.empty() ? text_of(&info->items.front())
                                                                                     : text_of(info);
            }
          }
          if (!generator.empty()) {
            summary.claim_generator = generator;
          }
        } else if (label.starts_with("c2pa.soft-binding")) {
          // C2PA 2.x soft-binding assertion: {alg, blocks: [...], ...}.
          auto algorithm = text_of(map_value(root, "alg"));
          if (algorithm.empty()) {
            algorithm = "unnamed algorithm";
          }
          if (summary.soft_bindings.size() < 16) {
            summary.soft_bindings.push_back(std::move(algorithm));
          }
        } else if (label.starts_with("c2pa.actions")) {
          if (const auto* actions = map_value(root, "actions"); actions != nullptr &&
                                                                actions->type == CborItem::Type::Array) {
            for (const auto& action : actions->items) {
              std::string text = text_of(map_value(action, "action"));
              const auto source_type = text_of(map_value(action, "digitalSourceType"));
              if (!source_type.empty()) {
                text += " (" + short_source_type(source_type) + ")";
                if (summary.digital_source_type.empty()) {
                  summary.digital_source_type = source_type;
                }
              }
              const auto agent = text_of(map_value(action, "softwareAgent"));
              if (!agent.empty()) {
                text += " by " + agent;
              }
              const auto name = text_of(map_value(action, "action"));
              if (name.starts_with("c2pa.watermarked") && summary.watermark_actions.size() < 16) {
                summary.watermark_actions.push_back(name);
              }
              if (!text.empty() && summary.actions.size() < 64) {
                summary.actions.push_back(std::move(text));
              }
            }
          }
        }
      }
      pos += static_cast<std::size_t>(size);
    }
  }
};

}  // namespace

std::string cbor_to_text(std::span<const std::uint8_t> bytes, std::size_t max_chars) {
  CborReader reader{bytes};
  const auto item = reader.read(0);
  std::string out;
  render(item, out, max_chars);
  return out;
}

JumbfSummary summarize_jumbf(std::span<const std::uint8_t> bytes) {
  JumbfSummary summary;
  BoxWalker walker{summary};
  walker.walk(bytes, 0, {});
  return summary;
}

}  // namespace patchy::metadata
