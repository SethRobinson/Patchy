#include "formats/pdn_document_io.hpp"

#include "core/environment.hpp"
#include "core/layer_metadata.hpp"
#include "core/worker_budget.hpp"
#include "formats/format_file_io.hpp"
#include "formats/miniz/miniz.h"
#include "formats/nrbf_reader.hpp"
#include "formats/stb/stb_image.h"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

// Wire layout (docs/pdn-format.md has the full record and where each fact comes from):
//
//   "PDN3"  u24le xml_length  xml (UTF-8 <pdnImage width= height= layers= savedWithVersion=>
//                                   <custom><thumb png="base64"/></custom></pdnImage>)
//   then either 00 01 + an NRBF stream, or 1F 8B + a gzip member holding [00 01] + the stream;
//   Paint.NET 1.x/2.x files are only that gzip member, with no header at all.
//
// The graph's root is PaintDotNet.Document {width, height, layers -> LayerList
// {ArrayList+_items, ArrayList+_size}, savedWith -> System.Version, userMetadataItems
// (KeyValuePair[] in 3.5+) and/or userMetaData (NameValueCollection, 3.x)}. Each layer is a
// PaintDotNet.BitmapLayer {properties -> BitmapLayerProperties {blendOp: an empty
// UserBlendOps+<Name>BlendOp class}, surface -> Surface {width, height, stride, scan0 ->
// MemoryBlock {length64, hasParent, deferred}}, Layer+properties -> LayerProperties {name,
// visible, isBackground, opacity (byte), blendMode -> LayerBlendMode {value__} in 4.0+}}.
//
// After the graph's MessageEnd come the deferred MemoryBlocks, in the order their records
// appear in the graph: per block a format byte (0 = gzip chunks, 1 = raw), a big-endian u32
// chunk size, then ceil(length64 / chunk size) chunks of {u32be index, u32be stored size,
// bytes}, in ANY order (diskBad-250.pdn from Paint.NET 4.5 starts with chunk 1). Pixels are
// BGRA, straight alpha, rows `stride` bytes apart.

namespace patchy::pdn {

namespace {

constexpr std::int32_t kMaxSide = 300000;            // the PSB cap every importer shares
constexpr std::uint64_t kMaxLayerPixels = 1ULL << 28U;
constexpr std::size_t kMaxLayers = 4096;
constexpr std::uint64_t kMaxTotalPixelBytes = 8ULL << 30U;
// Deflate expands at most about 1032:1; a stream claiming more is not deflate.
constexpr std::uint64_t kMaxInflateRatio = 1100;

[[nodiscard]] bool trace_enabled() {
  static const bool enabled = environment_variable_is_set("PATCHY_PDN_TRACE");
  return enabled;
}

void trace(const std::string& message) {
  if (trace_enabled()) {
    std::fprintf(stderr, "[pdn] %s\n", message.c_str());
  }
}

[[nodiscard]] std::uint32_t read_u32_be(std::span<const std::uint8_t> bytes, std::size_t at) {
  return (static_cast<std::uint32_t>(bytes[at]) << 24U) | (static_cast<std::uint32_t>(bytes[at + 1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[at + 2]) << 8U) | static_cast<std::uint32_t>(bytes[at + 3]);
}

[[nodiscard]] std::vector<std::uint8_t> decode_base64(std::string_view text) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(text.size() / 4U * 3U);
  std::uint32_t accumulator = 0;
  int bits = 0;
  for (const char c : text) {
    std::uint32_t value = 0;
    if (c >= 'A' && c <= 'Z') {
      value = static_cast<std::uint32_t>(c - 'A');
    } else if (c >= 'a' && c <= 'z') {
      value = static_cast<std::uint32_t>(c - 'a') + 26U;
    } else if (c >= '0' && c <= '9') {
      value = static_cast<std::uint32_t>(c - '0') + 52U;
    } else if (c == '+') {
      value = 62U;
    } else if (c == '/') {
      value = 63U;
    } else if (c == '=') {
      break;
    } else {
      continue;  // whitespace and line breaks
    }
    accumulator = (accumulator << 6U) | value;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      bytes.push_back(static_cast<std::uint8_t>((accumulator >> static_cast<unsigned>(bits)) & 0xFFU));
    }
  }
  return bytes;
}

// The value of attribute `name` in the first `element` tag of a small XML block. The
// header and the EXIF entries are machine-written with double quotes and no entities
// in the values this reader needs.
[[nodiscard]] std::optional<std::string_view> xml_attribute(std::string_view xml, std::string_view element,
                                                            std::string_view name) {
  const auto open = xml.find("<" + std::string(element));
  if (open == std::string_view::npos) {
    return std::nullopt;
  }
  const auto close = xml.find('>', open);
  const auto tag = xml.substr(open, close == std::string_view::npos ? std::string_view::npos : close - open);
  std::size_t at = 0;
  while ((at = tag.find(name, at)) != std::string_view::npos) {
    const bool starts_word = at > 0 && (tag[at - 1] == ' ' || tag[at - 1] == '\t' || tag[at - 1] == '\n' ||
                                        tag[at - 1] == '\r');
    const auto after = at + name.size();
    if (starts_word && after + 1 < tag.size() && tag[after] == '=' && tag[after + 1] == '"') {
      const auto end = tag.find('"', after + 2);
      if (end == std::string_view::npos) {
        return std::nullopt;
      }
      return tag.substr(after + 2, end - after - 2);
    }
    at = after;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::int64_t> parse_integer(std::optional<std::string_view> text) {
  if (!text.has_value() || text->empty() || text->size() > 18) {
    return std::nullopt;
  }
  std::int64_t value = 0;
  for (const char c : *text) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10 + (c - '0');
  }
  return value;
}

// --- gzip -------------------------------------------------------------------

// The offset of the deflate body inside an RFC 1952 member starting at `bytes[0]`.
[[nodiscard]] std::size_t gzip_body_offset(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 18 || bytes[0] != 0x1F || bytes[1] != 0x8B || bytes[2] != 8) {
    throw std::runtime_error("gzip header is invalid");
  }
  const auto flags = bytes[3];
  std::size_t position = 10;
  if ((flags & 0x04U) != 0) {  // FEXTRA
    if (position + 2 > bytes.size()) {
      throw std::runtime_error("gzip header is truncated");
    }
    position += 2 + (bytes[position] | (static_cast<std::size_t>(bytes[position + 1]) << 8U));
  }
  for (const auto flag : {0x08U, 0x10U}) {  // FNAME, FCOMMENT
    if ((flags & flag) != 0) {
      while (position < bytes.size() && bytes[position] != 0) {
        ++position;
      }
      ++position;
    }
  }
  if ((flags & 0x02U) != 0) {  // FHCRC
    position += 2;
  }
  if (position + 8 > bytes.size()) {
    throw std::runtime_error("gzip member is truncated");
  }
  return position;
}

class Inflater {
public:
  Inflater() {
    std::memset(&stream_, 0, sizeof(stream_));
    if (mz_inflateInit2(&stream_, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
      throw std::runtime_error("inflate could not start");
    }
  }
  ~Inflater() { mz_inflateEnd(&stream_); }
  Inflater(const Inflater&) = delete;
  Inflater& operator=(const Inflater&) = delete;
  mz_stream& stream() { return stream_; }

private:
  mz_stream stream_;
};

// Inflates one gzip member into exactly `out.size()` bytes. False when the stream is
// damaged, short or longer than `out`; `crc_ok` reports the trailer check.
[[nodiscard]] bool inflate_gzip_exact(std::span<const std::uint8_t> member, std::span<std::uint8_t> out,
                                      bool& crc_ok) {
  crc_ok = false;
  try {
    const auto body = gzip_body_offset(member);
    Inflater inflater;
    auto& stream = inflater.stream();
    const auto input = member.subspan(body);
    stream.next_in = input.data();
    stream.avail_in = static_cast<unsigned int>(std::min<std::size_t>(input.size(), UINT32_MAX));
    stream.next_out = out.data();
    stream.avail_out = static_cast<unsigned int>(std::min<std::size_t>(out.size(), UINT32_MAX));
    if (input.size() > UINT32_MAX || out.size() > UINT32_MAX) {
      return false;
    }
    const auto status = mz_inflate(&stream, MZ_FINISH);
    if (status != MZ_STREAM_END || stream.total_out != out.size()) {
      return false;
    }
    const auto trailer = body + stream.total_in;
    if (trailer + 8 <= member.size()) {
      const auto stored_crc = static_cast<std::uint32_t>(member[trailer]) |
                              (static_cast<std::uint32_t>(member[trailer + 1]) << 8U) |
                              (static_cast<std::uint32_t>(member[trailer + 2]) << 16U) |
                              (static_cast<std::uint32_t>(member[trailer + 3]) << 24U);
      crc_ok = static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, out.data(), out.size())) == stored_crc;
    }
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// Inflates consecutive gzip members (the whole-stream compression of old files), up to
// `limit` output bytes.
[[nodiscard]] std::vector<std::uint8_t> inflate_gzip_members(std::span<const std::uint8_t> bytes,
                                                            std::uint64_t limit) {
  std::vector<std::uint8_t> out;
  std::size_t position = 0;
  while (position + 18 <= bytes.size() && bytes[position] == 0x1F && bytes[position + 1] == 0x8B) {
    const auto member = bytes.subspan(position);
    const auto body = gzip_body_offset(member);
    Inflater inflater;
    auto& stream = inflater.stream();
    std::size_t consumed = 0;
    const auto input = member.subspan(body);
    while (true) {
      if (out.size() >= limit) {
        throw std::runtime_error("gzip data decompresses to an unreasonable size");
      }
      const auto grow = static_cast<std::size_t>(std::min<std::uint64_t>(
          limit - out.size(), std::max<std::uint64_t>(1U << 20U, out.size())));
      const auto start = out.size();
      out.resize(start + grow);
      const auto in_left = input.size() - consumed;
      stream.next_in = input.data() + consumed;
      stream.avail_in = static_cast<unsigned int>(std::min<std::size_t>(in_left, 1U << 30U));
      stream.next_out = out.data() + start;
      stream.avail_out = static_cast<unsigned int>(std::min<std::size_t>(grow, 1U << 30U));
      const auto in_before = stream.avail_in;
      const auto out_before = stream.avail_out;
      const auto status = mz_inflate(&stream, MZ_NO_FLUSH);
      consumed += in_before - stream.avail_in;
      out.resize(start + (out_before - stream.avail_out));
      if (status == MZ_STREAM_END) {
        break;
      }
      if (status != MZ_OK && status != MZ_BUF_ERROR) {
        throw std::runtime_error("gzip data is damaged");
      }
      if (in_before == stream.avail_in && out_before == stream.avail_out) {
        throw std::runtime_error("gzip data is truncated");
      }
    }
    position += body + consumed + 8;
  }
  if (out.empty()) {
    throw std::runtime_error("gzip data is empty");
  }
  return out;
}

// --- the graph --------------------------------------------------------------

// Where the NRBF stream and the bytes after it live.
struct Payload {
  std::vector<std::uint8_t> owned;  // the inflated stream when it was gzipped
  std::span<const std::uint8_t> data;
  std::size_t graph_offset{0};
};

[[nodiscard]] std::optional<std::size_t> graph_start(std::span<const std::uint8_t> data, std::size_t at) {
  // 00 01 marks an uncompressed graph; the stream itself also begins with 00 (the
  // SerializationHeader record), so test the header's version fields at both offsets.
  if (at + 2 <= data.size() && data[at] == 0x00 && data[at + 1] == 0x01 && nrbf::looks_like_stream(data, at + 2)) {
    return at + 2;
  }
  if (nrbf::looks_like_stream(data, at)) {
    return at;
  }
  return std::nullopt;
}

[[nodiscard]] Payload locate_payload(std::span<const std::uint8_t> bytes, const std::optional<Header>& header) {
  Payload payload;
  const auto start = header.has_value() ? header->payload_offset : 0;
  if (start + 2 <= bytes.size() && bytes[start] == 0x1F && bytes[start + 1] == 0x8B) {
    const auto compressed = bytes.subspan(start);
    payload.owned = inflate_gzip_members(
        compressed, std::min<std::uint64_t>(kMaxTotalPixelBytes, compressed.size() * kMaxInflateRatio));
    payload.data = payload.owned;
    const auto at = graph_start(payload.data, 0);
    if (!at.has_value()) {
      throw std::runtime_error("the gzipped payload is not a .NET serialization stream");
    }
    payload.graph_offset = *at;
    return payload;
  }
  payload.data = bytes;
  const auto at = graph_start(bytes, start);
  if (!at.has_value()) {
    throw std::runtime_error("no .NET serialization stream follows the header");
  }
  payload.graph_offset = *at;
  return payload;
}

[[nodiscard]] bool class_is(const nrbf::Object* object, std::string_view name) {
  return object != nullptr && object->kind == nrbf::Object::Kind::Class && object->class_name() == name;
}

// "PaintDotNet.UserBlendOps+MultiplyBlendOp" -> "Multiply"
[[nodiscard]] std::string blend_op_name(std::string_view class_name) {
  const auto plus = class_name.rfind('+');
  auto name = class_name.substr(plus == std::string_view::npos ? 0 : plus + 1);
  if (name.ends_with("BlendOp")) {
    name.remove_suffix(7);
  }
  return std::string(name);
}

// Paint.NET's LayerBlendMode numbering (4.0+); 3.x stores the same set as blend-op classes.
[[nodiscard]] std::optional<int> blend_index_from_name(std::string_view name) {
  static constexpr std::string_view kNames[] = {"Normal",   "Multiply",   "Additive", "ColorBurn", "ColorDodge",
                                                "Reflect",  "Glow",       "Overlay",  "Difference", "Negation",
                                                "Lighten",  "Darken",     "Screen",   "Xor"};
  for (int index = 0; index < static_cast<int>(std::size(kNames)); ++index) {
    if (kNames[index] == name) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<BlendMode> blend_mode_from_index(int index) {
  switch (index) {
    case 0: return BlendMode::Normal;
    case 1: return BlendMode::Multiply;
    case 2: return BlendMode::LinearDodge;  // Additive
    case 3: return BlendMode::ColorBurn;
    case 4: return BlendMode::ColorDodge;
    case 7: return BlendMode::Overlay;
    case 8: return BlendMode::Difference;
    case 10: return BlendMode::Lighten;
    case 11: return BlendMode::Darken;
    case 12: return BlendMode::Screen;
    default: return std::nullopt;  // Reflect, Glow, Negation, Xor and unknown values
  }
}

struct LayerRecord {
  std::string name;
  bool visible{true};
  bool is_background{false};
  std::uint8_t opacity{255};
  std::optional<int> blend_index;   // Paint.NET's numbering
  std::int32_t surface_width{0};
  std::int32_t surface_height{0};
  std::int64_t stride{0};
  const nrbf::Object* block{nullptr};  // the MemoryBlock behind scan0
  bool recognized{false};              // a BitmapLayer with a surface
};

struct GraphDocument {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<LayerRecord> layers;
};

[[nodiscard]] const nrbf::Object* document_root(const nrbf::Graph& graph) {
  const auto* root = graph.root();
  if (!class_is(root, "PaintDotNet.Document")) {
    throw std::runtime_error("the graph's root is not a Paint.NET document");
  }
  return root;
}

[[nodiscard]] GraphDocument read_graph_document(const nrbf::Graph& graph) {
  const auto* root = document_root(graph);
  GraphDocument document;
  const auto width = graph.member_integer(*root, "width");
  const auto height = graph.member_integer(*root, "height");
  if (!width || !height || *width <= 0 || *height <= 0 || *width > kMaxSide || *height > kMaxSide ||
      static_cast<std::uint64_t>(*width) * static_cast<std::uint64_t>(*height) > kMaxLayerPixels) {
    throw std::runtime_error("the document size is invalid");
  }
  document.width = static_cast<std::int32_t>(*width);
  document.height = static_cast<std::int32_t>(*height);
  const auto* list = graph.member_object(*root, "layers");
  if (list == nullptr) {
    throw std::runtime_error("the document has no layer list");
  }
  const auto* items = graph.member_object(*list, "ArrayList+_items");
  auto size = graph.member_integer(*list, "ArrayList+_size");
  if (items == nullptr) {  // a plain ArrayList rather than the LayerList subclass
    items = graph.member_object(*list, "_items");
    size = graph.member_integer(*list, "_size");
  }
  if (items == nullptr || items->kind != nrbf::Object::Kind::ObjectArray || !size || *size < 0 ||
      static_cast<std::size_t>(*size) > items->value_count || static_cast<std::size_t>(*size) > kMaxLayers) {
    throw std::runtime_error("the layer list is invalid");
  }
  const auto elements = graph.values(*items);
  for (std::size_t index = 0; index < static_cast<std::size_t>(*size); ++index) {
    LayerRecord record;
    record.name = "Layer " + std::to_string(index + 1);
    const auto* layer = graph.object(&elements[index]);
    if (layer != nullptr && layer->kind == nrbf::Object::Kind::Class) {
      if (const auto* properties = graph.member_object(*layer, "Layer+properties")) {
        if (const auto name = graph.member_string(*properties, "name")) {
          record.name = std::string(*name);
        }
        record.visible = graph.member_bool(*properties, "visible").value_or(true);
        record.is_background = graph.member_bool(*properties, "isBackground").value_or(false);
        record.opacity =
            static_cast<std::uint8_t>(std::clamp<std::int64_t>(graph.member_integer(*properties, "opacity").value_or(255), 0, 255));
        if (const auto* mode = graph.member_object(*properties, "blendMode")) {
          if (const auto value = graph.member_integer(*mode, "value__")) {
            record.blend_index = static_cast<int>(std::clamp<std::int64_t>(*value, -1, 1000));
          }
        }
      }
      if (!record.blend_index.has_value()) {
        if (const auto* bitmap_properties = graph.member_object(*layer, "properties")) {
          if (const auto* op = graph.member_object(*bitmap_properties, "blendOp")) {
            record.blend_index = blend_index_from_name(blend_op_name(op->class_name())).value_or(-1);
          }
        }
      }
      if (const auto* surface = graph.member_object(*layer, "surface"); class_is(surface, "PaintDotNet.Surface")) {
        record.surface_width = static_cast<std::int32_t>(std::clamp<std::int64_t>(
            graph.member_integer(*surface, "width").value_or(0), 0, kMaxSide));
        record.surface_height = static_cast<std::int32_t>(std::clamp<std::int64_t>(
            graph.member_integer(*surface, "height").value_or(0), 0, kMaxSide));
        record.stride = graph.member_integer(*surface, "stride").value_or(0);
        if (const auto* block = graph.member_object(*surface, "scan0"); class_is(block, "PaintDotNet.MemoryBlock")) {
          record.block = block;
          record.recognized = class_is(layer, "PaintDotNet.BitmapLayer");
        }
      }
    }
    document.layers.push_back(std::move(record));
  }
  return document;
}

// --- deferred pixel data ----------------------------------------------------

struct Chunk {
  std::size_t offset{0};  // stored bytes in the payload
  std::size_t size{0};
};

struct BlockData {
  std::uint64_t length{0};
  std::uint8_t format{0};  // 0 gzip, 1 raw
  std::uint64_t chunk_size{0};
  std::vector<Chunk> chunks;  // by chunk index; size 0 + offset 0 means missing
  bool complete{false};
  std::vector<std::uint8_t> inline_bytes;  // a non-deferred block's own byte array
};

// Walks the deferred blocks after the graph. A block whose chunk table runs off the end
// of the file stops the walk: what follows cannot be located.
[[nodiscard]] std::unordered_map<std::int32_t, BlockData> scan_blocks(const nrbf::Graph& graph,
                                                                     std::span<const std::uint8_t> data,
                                                                     std::size_t& damaged) {
  std::unordered_map<std::int32_t, BlockData> blocks;
  std::size_t position = graph.end_offset();
  std::uint64_t total = 0;
  bool lost = false;
  for (const auto& object : graph.objects()) {
    if (!class_is(&object, "PaintDotNet.MemoryBlock")) {
      continue;
    }
    BlockData block;
    const auto length = graph.member_integer(object, "length64").value_or(-1);
    const bool deferred = graph.member_bool(object, "deferred").value_or(false);
    const bool has_parent = graph.member_bool(object, "hasParent").value_or(false);
    if (length < 0 || has_parent) {
      ++damaged;
      continue;
    }
    block.length = static_cast<std::uint64_t>(length);
    total += block.length;
    if (total > kMaxTotalPixelBytes) {
      throw std::runtime_error("the layers are too large");
    }
    if (!deferred) {
      // Inline data: the first byte array member that covers the block.
      for (const auto& value : graph.values(object)) {
        const auto* array = graph.object(&value);
        if (array != nullptr && array->kind == nrbf::Object::Kind::PrimitiveArray &&
            array->element_type == nrbf::PrimitiveType::Byte && array->element_count >= block.length) {
          // The graph indexes into the span it parsed (the payload).
          block.inline_bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(array->data_offset),
                                    data.begin() + static_cast<std::ptrdiff_t>(array->data_offset + block.length));
          block.complete = true;
          break;
        }
      }
      if (!block.complete) {
        ++damaged;
      }
      blocks.emplace(object.id, std::move(block));
      continue;
    }
    if (lost) {
      ++damaged;
      continue;
    }
    if (position + 5 > data.size()) {
      lost = true;
      ++damaged;
      trace("deferred data ends before a block header");
      continue;
    }
    block.format = data[position];
    block.chunk_size = read_u32_be(data, position + 1);
    position += 5;
    if (block.format > 1 || (block.chunk_size == 0 && block.length != 0)) {
      lost = true;
      ++damaged;
      trace("a deferred block header is invalid");
      continue;
    }
    const auto chunk_count = block.length == 0 ? 0 : (block.length + block.chunk_size - 1) / block.chunk_size;
    if (chunk_count > (data.size() - position) / 8U + 1U) {
      lost = true;
      ++damaged;
      trace("a deferred block claims more chunks than the file holds");
      continue;
    }
    block.chunks.resize(static_cast<std::size_t>(chunk_count));
    std::vector<bool> seen(static_cast<std::size_t>(chunk_count), false);
    bool ok = true;
    for (std::uint64_t i = 0; i < chunk_count; ++i) {
      if (position + 8 > data.size()) {
        ok = false;
        break;
      }
      const auto index = read_u32_be(data, position);
      const auto stored = read_u32_be(data, position + 4);
      position += 8;
      if (stored > data.size() - position) {
        ok = false;
        break;
      }
      if (index < chunk_count && !seen[index]) {
        seen[index] = true;
        block.chunks[index] = Chunk{position, stored};
      } else {
        trace("a deferred chunk index is out of range or repeated");
      }
      position += stored;
    }
    block.complete = ok && std::all_of(seen.begin(), seen.end(), [](bool flag) { return flag; });
    if (!ok) {
      lost = true;
    }
    // A block must pay for what it claims: gzip expands at most kMaxInflateRatio times, so
    // a few stored bytes cannot make the reader allocate a huge surface.
    std::uint64_t stored_total = 0;
    for (const auto& chunk : block.chunks) {
      stored_total += chunk.size;
    }
    if (block.length > (block.format == 1 ? stored_total : stored_total * kMaxInflateRatio + 64U)) {
      block.complete = false;
      block.chunks.clear();
      block.length = 0;
      trace("a deferred block claims more pixels than its chunks can hold");
    }
    if (!block.complete) {
      ++damaged;
    }
    blocks.emplace(object.id, std::move(block));
  }
  return blocks;
}

template <typename Work>
void run_in_parallel(std::size_t count, Work work) {
  if (count == 0) {
    return;
  }
  const auto workers = static_cast<std::size_t>(std::max(
      1, max_blocking_fanout_workers(static_cast<int>(std::min<std::size_t>(
             std::min<std::size_t>(static_cast<std::size_t>(hardware_worker_threads()), 16), count)))));
  if (workers <= 1) {
    for (std::size_t i = 0; i < count; ++i) {
      work(i);
    }
    return;
  }
  std::atomic<std::size_t> next{0};
  std::vector<std::future<void>> futures;
  futures.reserve(workers);
  for (std::size_t w = 0; w < workers; ++w) {
    futures.push_back(std::async(std::launch::async, [&] {
      for (std::size_t i = next.fetch_add(1); i < count; i = next.fetch_add(1)) {
        work(i);
      }
    }));
  }
  for (auto& future : futures) {
    future.get();
  }
}

// Decodes one block into its raw BGRA bytes; missing or damaged chunks stay zero
// (transparent). Returns false when anything was lost.
[[nodiscard]] bool decode_block(const BlockData& block, std::span<const std::uint8_t> data,
                                std::vector<std::uint8_t>& out, bool& crc_mismatch) {
  if (!block.inline_bytes.empty() || (block.complete && block.chunks.empty())) {
    out = block.inline_bytes;
    out.resize(static_cast<std::size_t>(block.length), 0);
    return block.complete;
  }
  out.assign(static_cast<std::size_t>(block.length), 0);
  std::atomic<bool> lost{!block.complete};
  std::atomic<bool> mismatch{false};
  run_in_parallel(block.chunks.size(), [&](std::size_t index) {
    const auto& chunk = block.chunks[index];
    const auto begin = static_cast<std::uint64_t>(index) * block.chunk_size;
    const auto expected = static_cast<std::size_t>(std::min<std::uint64_t>(block.chunk_size, block.length - begin));
    if (chunk.offset == 0 && chunk.size == 0) {
      lost = true;
      return;
    }
    const auto stored = data.subspan(chunk.offset, chunk.size);
    auto target = std::span<std::uint8_t>(out).subspan(static_cast<std::size_t>(begin), expected);
    if (block.format == 1) {
      std::memcpy(target.data(), stored.data(), std::min(stored.size(), expected));
      if (stored.size() != expected) {
        lost = true;
      }
      return;
    }
    bool crc_ok = false;
    if (!inflate_gzip_exact(stored, target, crc_ok)) {
      std::fill(target.begin(), target.end(), std::uint8_t{0});
      lost = true;
      return;
    }
    if (!crc_ok) {
      mismatch = true;
    }
  });
  crc_mismatch = mismatch;
  return !lost;
}

// Straight BGRA rows into an RGBA8 buffer the document's size; areas the surface does
// not cover stay transparent. The buffer is fresh, so its storage is unshared: the
// workers write through one pointer taken here (the copy-on-write rule in AGENTS.md).
[[nodiscard]] PixelBuffer convert_surface(std::span<const std::uint8_t> bgra, std::int32_t surface_width,
                                          std::int32_t surface_height, std::size_t stride, std::int32_t width,
                                          std::int32_t height) {
  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  pixels.clear(0);
  const auto copy_width = std::min(width, surface_width);
  const auto copy_height = std::min(height, surface_height);
  if (copy_width <= 0 || copy_height <= 0) {
    return pixels;
  }
  auto storage = pixels.data();
  std::uint8_t* const base = storage.data();
  const auto row_bytes = pixels.stride_bytes();
  constexpr std::int32_t kRowsPerTask = 64;
  const auto tasks = static_cast<std::size_t>((copy_height + kRowsPerTask - 1) / kRowsPerTask);
  run_in_parallel(tasks, [&](std::size_t task) {
    const auto first = static_cast<std::int32_t>(task) * kRowsPerTask;
    const auto last = std::min(copy_height, first + kRowsPerTask);
    for (std::int32_t y = first; y < last; ++y) {
      const auto row_start = static_cast<std::size_t>(y) * stride;
      if (row_start >= bgra.size()) {
        return;
      }
      const auto available = std::min<std::size_t>(static_cast<std::size_t>(copy_width), (bgra.size() - row_start) / 4U);
      const auto* source = bgra.data() + row_start;
      auto* target = base + static_cast<std::size_t>(y) * row_bytes;
      for (std::size_t x = 0; x < available; ++x) {
        target[x * 4U] = source[x * 4U + 2U];
        target[x * 4U + 1U] = source[x * 4U + 1U];
        target[x * 4U + 2U] = source[x * 4U];
        target[x * 4U + 3U] = source[x * 4U + 3U];
      }
    }
  });
  return pixels;
}

[[nodiscard]] bool fully_opaque(const PixelBuffer& pixels) {
  const auto bytes = pixels.data();
  for (std::size_t i = 3; i < bytes.size(); i += 4) {
    if (bytes[i] != 255) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] PixelBuffer drop_alpha(const PixelBuffer& rgba) {
  PixelBuffer rgb(rgba.width(), rgba.height(), PixelFormat::rgb8());
  const auto source = rgba.data();
  auto storage = rgb.data();
  const auto pixel_count = static_cast<std::size_t>(rgba.width()) * static_cast<std::size_t>(rgba.height());
  for (std::size_t i = 0; i < pixel_count; ++i) {
    storage[i * 3U] = source[i * 4U];
    storage[i * 3U + 1U] = source[i * 4U + 1U];
    storage[i * 3U + 2U] = source[i * 4U + 2U];
  }
  return rgb;
}

// --- user metadata ----------------------------------------------------------

[[nodiscard]] std::vector<std::pair<std::string, std::string>> collect_user_metadata(const nrbf::Graph& graph,
                                                                                      const nrbf::Object& owner) {
  std::vector<std::pair<std::string, std::string>> items;
  // 3.5+: KeyValuePair<string, string>[] userMetadataItems.
  if (const auto* array = graph.member_object(owner, "userMetadataItems");
      array != nullptr && array->kind == nrbf::Object::Kind::ObjectArray) {
    for (const auto& value : graph.values(*array)) {
      const auto* pair = graph.object(&value);
      if (pair == nullptr || pair->kind != nrbf::Object::Kind::Class) {
        continue;
      }
      const auto key = graph.member_string(*pair, "key");
      const auto text = graph.member_string(*pair, "value");
      if (key.has_value()) {
        items.emplace_back(std::string(*key), std::string(text.value_or("")));
      }
    }
  }
  if (!items.empty()) {
    return items;
  }
  // 3.x: NameValueCollection {Keys: string[], Values: ArrayList[] of strings}.
  if (const auto* collection = graph.member_object(owner, "userMetaData")) {
    const auto* keys = graph.member_object(*collection, "Keys");
    const auto* values = graph.member_object(*collection, "Values");
    const auto count = graph.member_integer(*collection, "Count").value_or(0);
    if (keys != nullptr && values != nullptr && keys->kind == nrbf::Object::Kind::ObjectArray &&
        values->kind == nrbf::Object::Kind::ObjectArray) {
      const auto key_values = graph.values(*keys);
      const auto value_lists = graph.values(*values);
      const auto limit = std::min<std::size_t>({static_cast<std::size_t>(std::max<std::int64_t>(count, 0)),
                                                key_values.size(), value_lists.size()});
      for (std::size_t i = 0; i < limit; ++i) {
        const auto key = graph.string_value(&key_values[i]);
        if (!key.has_value()) {
          continue;
        }
        const auto* list = graph.object(&value_lists[i]);
        const auto* list_items = list != nullptr ? graph.member_object(*list, "_items") : nullptr;
        const auto list_size = list != nullptr ? graph.member_integer(*list, "_size").value_or(0) : 0;
        if (list_items == nullptr || list_items->kind != nrbf::Object::Kind::ObjectArray) {
          continue;
        }
        const auto entries = graph.values(*list_items);
        for (std::size_t j = 0; j < entries.size() && static_cast<std::int64_t>(j) < list_size; ++j) {
          if (const auto text = graph.string_value(&entries[j])) {
            items.emplace_back(std::string(*key), std::string(*text));
          }
        }
      }
    }
  }
  return items;
}

struct ExifEntry {
  std::uint16_t id{0};
  std::uint16_t type{0};
  std::uint32_t count{0};
  std::vector<std::uint8_t> value;  // little-endian, as stored
};

[[nodiscard]] std::vector<ExifEntry> exif_entries(const std::vector<std::pair<std::string, std::string>>& items) {
  std::vector<ExifEntry> entries;
  for (const auto& [key, text] : items) {
    if (!key.starts_with("$exif.tag")) {
      continue;
    }
    const auto id = parse_integer(xml_attribute(text, "exif", "id"));
    const auto type = parse_integer(xml_attribute(text, "exif", "type"));
    const auto length = parse_integer(xml_attribute(text, "exif", "len"));
    const auto value = xml_attribute(text, "exif", "value");
    if (!id || !type || !length || *id < 0 || *id > 0xFFFF || *type < 1 || *type > 13 || *length < 0 ||
        *length > (1 << 24)) {
      continue;
    }
    static constexpr std::uint32_t kTypeSizes[14] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8, 4};
    ExifEntry entry;
    entry.id = static_cast<std::uint16_t>(*id);
    entry.type = static_cast<std::uint16_t>(*type);
    entry.value = decode_base64(value.value_or(""));
    entry.value.resize(static_cast<std::size_t>(*length), 0);
    entry.count = static_cast<std::uint32_t>(entry.value.size() / kTypeSizes[entry.type]);
    if (entry.count == 0) {
      continue;
    }
    entry.value.resize(static_cast<std::size_t>(entry.count) * kTypeSizes[entry.type]);
    entries.push_back(std::move(entry));
  }
  return entries;
}

[[nodiscard]] std::optional<double> exif_rational(const std::vector<ExifEntry>& entries, std::uint16_t id) {
  for (const auto& entry : entries) {
    if (entry.id == id && entry.type == 5 && entry.value.size() >= 8) {
      const auto le32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(entry.value[at]) | (static_cast<std::uint32_t>(entry.value[at + 1]) << 8U) |
               (static_cast<std::uint32_t>(entry.value[at + 2]) << 16U) |
               (static_cast<std::uint32_t>(entry.value[at + 3]) << 24U);
      };
      const auto numerator = le32(0);
      const auto denominator = le32(4);
      if (denominator == 0 || numerator == 0) {
        return std::nullopt;
      }
      return static_cast<double>(numerator) / static_cast<double>(denominator);
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint16_t> exif_short(const std::vector<ExifEntry>& entries, std::uint16_t id) {
  for (const auto& entry : entries) {
    if (entry.id == id && entry.type == 3 && entry.value.size() >= 2) {
      return static_cast<std::uint16_t>(entry.value[0] | (entry.value[1] << 8U));
    }
  }
  return std::nullopt;
}

// Paint.NET keeps the resolution as EXIF XResolution/YResolution/ResolutionUnit and uses
// 96 pixels per inch when there are none.
void apply_resolution(Document& document, const std::vector<ExifEntry>& entries) {
  auto& print = document.print_settings();
  const auto unit = exif_short(entries, 296).value_or(2);
  const double scale = unit == 3 ? 2.54 : 1.0;
  const auto x = exif_rational(entries, 282);
  const auto y = exif_rational(entries, 283);
  const auto valid = [](double ppi) { return std::isfinite(ppi) && ppi >= 1.0 && ppi <= 100000.0; };
  print.horizontal_ppi = x && valid(*x * scale) ? *x * scale : 96.0;
  print.vertical_ppi = y && valid(*y * scale) ? *y * scale : print.horizontal_ppi;
  const std::uint16_t display = unit == 3 ? 2 : 1;
  print.horizontal_resolution_display_unit = display;
  print.vertical_resolution_display_unit = display;
  if (unit == 3) {
    print.width_display_unit = 2;
    print.height_display_unit = 2;
  }
}

// --- assembly ---------------------------------------------------------------

struct ImportStats {
  std::size_t damaged_blocks{0};
  std::size_t unknown_blend_layers{0};
  std::size_t unread_layers{0};
  bool crc_mismatch{false};
};

[[nodiscard]] Document build_document(const nrbf::Graph& graph, std::span<const std::uint8_t> data,
                                      ImportStats& stats) {
  const auto source = read_graph_document(graph);
  auto blocks = scan_blocks(graph, data, stats.damaged_blocks);
  // Every layer is the document's size, so the document may be no larger than the largest
  // block that the file really holds; a damaged size field cannot allocate more.
  std::uint64_t largest_block = 0;
  for (const auto& [id, block] : blocks) {
    largest_block = std::max(largest_block, block.length);
  }
  if (static_cast<std::uint64_t>(source.width) * static_cast<std::uint64_t>(source.height) * 4U > largest_block) {
    throw std::runtime_error("the document is larger than its pixel data");
  }
  Document document(source.width, source.height, PixelFormat::rgba8());
  // Layers whose pixels cannot be read share one transparent buffer (copy-on-write).
  PixelBuffer empty_layer(source.width, source.height, PixelFormat::rgba8());
  empty_layer.clear(0);
  std::map<std::int32_t, PixelBuffer> decoded;  // shared blocks decode once
  std::size_t opened_layers = 0;
  for (std::size_t index = 0; index < source.layers.size(); ++index) {
    const auto& record = source.layers[index];
    PixelBuffer pixels;
    bool have_pixels = false;
    if (record.recognized && record.block != nullptr) {
      if (const auto cached = decoded.find(record.block->id); cached != decoded.end()) {
        pixels = cached->second;
        have_pixels = true;
      } else if (const auto found = blocks.find(record.block->id); found != blocks.end()) {
        const auto row_bytes = static_cast<std::int64_t>(record.surface_width) * 4;
        if (record.stride >= row_bytes && record.surface_width > 0 && record.surface_height > 0 &&
            static_cast<std::uint64_t>(record.stride) * static_cast<std::uint64_t>(record.surface_height - 1) +
                    static_cast<std::uint64_t>(row_bytes) <=
                found->second.length) {
          std::vector<std::uint8_t> raw;
          bool mismatch = false;
          if (!decode_block(found->second, data, raw, mismatch) && found->second.complete) {
            ++stats.damaged_blocks;
          }
          stats.crc_mismatch = stats.crc_mismatch || mismatch;
          pixels = convert_surface(raw, record.surface_width, record.surface_height,
                                   static_cast<std::size_t>(record.stride), source.width, source.height);
          found->second.inline_bytes.clear();
          have_pixels = true;
          decoded.emplace(record.block->id, pixels);
        } else {
          trace("layer " + std::to_string(index) + " has an invalid surface layout");
        }
      }
    }
    if (!have_pixels) {
      ++stats.unread_layers;
      pixels = empty_layer;
    } else {
      ++opened_layers;
    }
    const bool background = index == 0 && record.is_background && have_pixels && record.opacity == 255 &&
                            record.blend_index.value_or(0) == 0 && fully_opaque(pixels);
    Layer layer(document.allocate_layer_id(), record.name, background ? drop_alpha(pixels) : std::move(pixels));
    layer.set_visible(record.visible);
    layer.set_opacity(static_cast<float>(record.opacity) / 255.0F);
    const auto blend = blend_mode_from_index(record.blend_index.value_or(0));
    if (!blend.has_value()) {
      ++stats.unknown_blend_layers;
      trace("layer " + std::to_string(index) + " blend index " + std::to_string(record.blend_index.value_or(-1)) +
            " maps to Normal");
    }
    layer.set_blend_mode(blend.value_or(BlendMode::Normal));
    if (background) {
      set_layer_lock_flags(layer, layer_lock_flags(layer) | kLayerLockPosition | kLayerLockTransparentPixels);
    }
    document.add_layer(std::move(layer));
  }
  if (opened_layers == 0) {
    throw std::runtime_error("no layer's pixels could be read");
  }
  apply_resolution(document, exif_entries(collect_user_metadata(graph, *document_root(graph))));
  if (!document.layers().empty()) {
    document.set_active_layer(document.layers().back().id());
  }
  return document;
}

[[nodiscard]] Document read_thumbnail(const Header& header) {
  if (header.thumbnail_png.empty() || header.thumbnail_png.size() > static_cast<std::size_t>(INT32_MAX)) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This Paint.NET file is damaged and could not be opened."));
  }
  int width = 0;
  int height = 0;
  int components = 0;
  const auto size = static_cast<int>(header.thumbnail_png.size());
  if (stbi_info_from_memory(header.thumbnail_png.data(), size, &width, &height, &components) == 0 || width <= 0 ||
      height <= 0 || width > 4096 || height > 4096) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This Paint.NET file is damaged and could not be opened."));
  }
  stbi_uc* decoded = stbi_load_from_memory(header.thumbnail_png.data(), size, &width, &height, &components, 4);
  if (decoded == nullptr) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This Paint.NET file is damaged and could not be opened."));
  }
  PixelBuffer pixels(width, height, PixelFormat::rgba8());
  const auto row_bytes = static_cast<std::size_t>(width) * 4U;
  for (std::int32_t y = 0; y < height; ++y) {
    std::memcpy(pixels.row(y).data(), decoded + static_cast<std::size_t>(y) * row_bytes, row_bytes);
  }
  stbi_image_free(decoded);
  Document document(width, height, PixelFormat::rgba8());
  document.add_pixel_layer("Paint.NET preview", std::move(pixels));
  document.print_settings().horizontal_ppi = 96.0;
  document.print_settings().vertical_ppi = 96.0;
  return document;
}

}  // namespace

bool sniff(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= 7 && bytes[0] == 'P' && bytes[1] == 'D' && bytes[2] == 'N' && bytes[3] == '3';
}

const std::vector<std::string>& pdn_extensions() {
  static const std::vector<std::string> extensions{".pdn"};
  return extensions;
}

std::optional<Header> read_header(std::span<const std::uint8_t> bytes) {
  if (!sniff(bytes)) {
    return std::nullopt;
  }
  const std::size_t length = bytes[4] | (static_cast<std::size_t>(bytes[5]) << 8U) |
                             (static_cast<std::size_t>(bytes[6]) << 16U);
  if (7 + length > bytes.size()) {
    throw std::runtime_error("the .pdn header is truncated");
  }
  Header header;
  header.payload_offset = 7 + length;
  const std::string_view xml(reinterpret_cast<const char*>(bytes.data() + 7), length);
  header.width = static_cast<std::int32_t>(
      std::clamp<std::int64_t>(parse_integer(xml_attribute(xml, "pdnImage", "width")).value_or(0), 0, kMaxSide));
  header.height = static_cast<std::int32_t>(
      std::clamp<std::int64_t>(parse_integer(xml_attribute(xml, "pdnImage", "height")).value_or(0), 0, kMaxSide));
  header.layer_count = static_cast<std::int32_t>(
      std::clamp<std::int64_t>(parse_integer(xml_attribute(xml, "pdnImage", "layers")).value_or(0), 0, 1 << 20));
  header.saved_with_version = std::string(xml_attribute(xml, "pdnImage", "savedWithVersion").value_or(""));
  if (const auto thumb = xml_attribute(xml, "thumb", "png")) {
    header.thumbnail_png = decode_base64(*thumb);
  }
  return header;
}

std::vector<std::pair<std::string, std::string>> read_user_metadata(std::span<const std::uint8_t> bytes) {
  const auto header = read_header(bytes);
  const auto payload = locate_payload(bytes, header);
  const auto graph = nrbf::parse(payload.data, payload.graph_offset);
  return collect_user_metadata(graph, *document_root(graph));
}

std::vector<std::uint8_t> exif_from_user_metadata(const std::vector<std::pair<std::string, std::string>>& items) {
  auto entries = exif_entries(items);
  if (entries.empty()) {
    return {};
  }
  // TIFF tags Patchy's EXIF reader expects under the Exif (0x8769) and GPS (0x8825)
  // sub-IFDs: the Exif range starts at ExposureTime (0x829A) except the IFD0 copyright,
  // and GPS tags are 0..31. Paint.NET flattens all of them into one list.
  const auto is_gps = [](const ExifEntry& entry) { return entry.id <= 31; };
  const auto is_exif = [](const ExifEntry& entry) {
    return entry.id >= 0x829A && entry.id != 0x8298 && entry.id != 0x8769 && entry.id != 0x8825;
  };
  std::vector<ExifEntry> ifd0;
  std::vector<ExifEntry> exif;
  std::vector<ExifEntry> gps;
  std::map<std::uint16_t, bool> seen;
  for (auto& entry : entries) {
    if (entry.id == 0x8769 || entry.id == 0x8825 || entry.id == 0xA005 || seen[entry.id]) {
      continue;  // pointers are rebuilt below; the interoperability IFD is not kept
    }
    seen[entry.id] = true;
    (is_gps(entry) ? gps : is_exif(entry) ? exif : ifd0).push_back(std::move(entry));
  }
  const auto by_id = [](const ExifEntry& a, const ExifEntry& b) { return a.id < b.id; };
  std::vector<std::uint8_t> out{'I', 'I', 42, 0, 8, 0, 0, 0};
  const auto put16 = [&](std::size_t at, std::uint32_t value) {
    out[at] = static_cast<std::uint8_t>(value & 0xFFU);
    out[at + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  };
  const auto put32 = [&](std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
      out[at + i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
    }
  };
  // Writes one IFD at the end of `out` with `pointers` (tag, placeholder) entries
  // appended; returns the offsets of those pointer values.
  const auto write_ifd = [&](std::vector<ExifEntry>& list, const std::vector<std::uint16_t>& pointers) {
    std::sort(list.begin(), list.end(), by_id);
    const auto count = list.size() + pointers.size();
    const auto start = out.size();
    out.resize(start + 2 + count * 12 + 4, 0);
    put16(start, static_cast<std::uint32_t>(count));
    std::vector<std::pair<std::uint16_t, std::size_t>> rows;
    for (const auto& entry : list) {
      rows.emplace_back(entry.id, 0);
    }
    for (const auto tag : pointers) {
      rows.emplace_back(tag, 1);
    }
    std::sort(rows.begin(), rows.end());
    std::map<std::uint16_t, std::size_t> pointer_slots;
    std::size_t list_index = 0;
    std::vector<const ExifEntry*> ordered;
    for (const auto& entry : list) {
      ordered.push_back(&entry);
    }
    for (std::size_t row = 0; row < rows.size(); ++row) {
      const auto at = start + 2 + row * 12;
      put16(at, rows[row].first);
      if (rows[row].second == 1) {
        put16(at + 2, 4);  // LONG
        put32(at + 4, 1);
        pointer_slots[rows[row].first] = at + 8;
        continue;
      }
      const auto& entry = *ordered[list_index++];
      put16(at + 2, entry.type);
      put32(at + 4, entry.count);
      if (entry.value.size() <= 4) {
        std::copy(entry.value.begin(), entry.value.end(), out.begin() + static_cast<std::ptrdiff_t>(at + 8));
      } else {
        if (out.size() % 2 != 0) {
          out.push_back(0);
        }
        put32(at + 8, static_cast<std::uint32_t>(out.size()));
        out.insert(out.end(), entry.value.begin(), entry.value.end());
      }
    }
    return pointer_slots;
  };
  std::vector<std::uint16_t> pointers;
  if (!exif.empty()) {
    pointers.push_back(0x8769);
  }
  if (!gps.empty()) {
    pointers.push_back(0x8825);
  }
  const auto slots = write_ifd(ifd0, pointers);
  const auto link = [&](std::uint16_t tag, std::vector<ExifEntry>& list) {
    if (list.empty()) {
      return;
    }
    if (out.size() % 2 != 0) {
      out.push_back(0);
    }
    put32(slots.at(tag), static_cast<std::uint32_t>(out.size()));
    static_cast<void>(write_ifd(list, {}));
  };
  link(0x8769, exif);
  link(0x8825, gps);
  return out;
}

Document DocumentIo::read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices) {
  std::vector<std::string> pending;
  std::optional<Header> header;
  try {
    header = read_header(bytes);
  } catch (const std::exception& error) {
    trace(error.what());
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This Paint.NET file is damaged and could not be opened."));
  }
  if (!header.has_value() && (bytes.size() < 2 || bytes[0] != 0x1F || bytes[1] != 0x8B)) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Not a Paint.NET document"));
  }
  Document document;
  try {
    const auto payload = locate_payload(bytes, header);
    const auto graph = nrbf::parse(payload.data, payload.graph_offset);
    if (graph.dangling_references() != 0) {
      trace(std::to_string(graph.dangling_references()) + " dangling references");
    }
    ImportStats stats;
    document = build_document(graph, payload.data, stats);
    if (stats.damaged_blocks != 0 || stats.unread_layers != 0) {
      pending.emplace_back(PATCHY_TRANSLATE_NOOP(
          "QObject", "Some layer pixels in this Paint.NET file are damaged or missing; those areas opened transparent."));
    } else if (stats.crc_mismatch) {
      pending.emplace_back(PATCHY_TRANSLATE_NOOP(
          "QObject", "Some layer pixels in this Paint.NET file failed their checksum and may be damaged."));
    }
    if (stats.unknown_blend_layers != 0) {
      pending.emplace_back(PATCHY_TRANSLATE_NOOP(
          "QObject", "A layer in this Paint.NET file uses a blend mode Patchy does not have; it opened as Normal."));
    }
  } catch (const std::exception& error) {
    trace(std::string("layer import failed: ") + error.what());
    if (!header.has_value()) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "This Paint.NET file is damaged and could not be opened."));
    }
    document = read_thumbnail(*header);
    pending.clear();
    pending.emplace_back(PATCHY_TRANSLATE_NOOP(
        "QObject",
        "The layers in this Paint.NET file could not be read, so it opened as its small embedded preview image."));
  }
  if (notices != nullptr) {
    for (auto& notice : pending) {
      notices->push_back(std::move(notice));
    }
  }
  return document;
}

Document DocumentIo::read_file(const std::filesystem::path& path, std::vector<std::string>* notices) {
  const auto bytes = formats::read_file_bytes(path, "Paint.NET");
  return read(std::span<const std::uint8_t>(bytes), notices);
}

}  // namespace patchy::pdn
