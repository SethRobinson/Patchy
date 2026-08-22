#include "formats/pdf_file.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>

namespace patchy::pdf {
namespace {

// The header may sit up to 1024 bytes into the file (clause 7.5.2 note), and real
// files do carry leading junk.
constexpr std::size_t kHeaderSearchWindow = 1024;
constexpr int kMaximumResolveDepth = 64;
// A malformed /Prev chain can point back at itself; cap the walk regardless.
constexpr std::size_t kMaximumXrefSections = 1024;
constexpr std::size_t kMaximumPages = 20000;

std::string_view text_at(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t length) {
  if (offset >= bytes.size()) {
    return {};
  }
  return std::string_view(reinterpret_cast<const char*>(bytes.data()) + offset,
                          std::min(length, bytes.size() - offset));
}

std::size_t find_backwards(std::span<const std::uint8_t> bytes, std::string_view needle, std::size_t from) {
  if (needle.empty() || needle.size() > bytes.size()) {
    return std::string_view::npos;
  }
  std::size_t position = std::min(from, bytes.size() - needle.size());
  while (true) {
    if (std::memcmp(bytes.data() + position, needle.data(), needle.size()) == 0) {
      return position;
    }
    if (position == 0) {
      return std::string_view::npos;
    }
    --position;
  }
}

void normalize_box(const std::vector<double>& values, double (&box)[4]) {
  if (values.size() < 4) {
    return;
  }
  box[0] = std::min(values[0], values[2]);
  box[1] = std::min(values[1], values[3]);
  box[2] = std::max(values[0], values[2]);
  box[3] = std::max(values[1], values[3]);
}

}  // namespace

std::optional<File> File::open(std::vector<std::uint8_t> bytes, std::vector<std::string>* notices) {
  const auto window = text_at(bytes, 0, kHeaderSearchWindow + 8);
  const auto header = window.find("%PDF-");
  if (header == std::string_view::npos) {
    return std::nullopt;
  }

  File file;
  file.bytes_ = std::move(bytes);
  const auto version_start = header + 5;
  const auto version_text = text_at(file.bytes_, version_start, 8);
  for (const char character : version_text) {
    if ((character >= '0' && character <= '9') || character == '.') {
      file.version_.push_back(character);
      continue;
    }
    break;
  }

  file.parse_xref_chain(notices);
  if (file.locations_.empty()) {
    file.reconstruct_by_scanning(notices);
  }

  const auto& encrypt = file.get(file.trailer_, "Encrypt");
  file.encrypted_ = !encrypt.is_null();

  file.collect_pages(notices);
  if (file.pages_.empty() && !file.scanned_) {
    // The xref parsed but produced no pages: almost always stale offsets from an
    // editor that appended without updating them. Rebuild and try once more.
    file.reconstruct_by_scanning(notices);
    file.cache_.clear();
    file.loaded_object_streams_.clear();
    file.collect_pages(notices);
  }
  return file;
}

void File::parse_xref_chain(std::vector<std::string>* notices) {
  const auto marker = find_backwards(bytes_, "startxref", bytes_.size());
  if (marker == std::string_view::npos) {
    return;
  }
  Lexer lexer(bytes_, marker + 9);
  const auto offset_object = lexer.next_object();
  if (!offset_object.is_number()) {
    return;
  }
  const auto offset = offset_object.integer(-1);
  if (offset < 0 || static_cast<std::size_t>(offset) >= bytes_.size()) {
    return;
  }
  std::vector<std::size_t> visited;
  parse_xref_section(static_cast<std::size_t>(offset), visited, notices);
}

bool File::parse_xref_section(std::size_t offset, std::vector<std::size_t>& visited,
                              std::vector<std::string>* notices) {
  if (visited.size() >= kMaximumXrefSections || offset >= bytes_.size()) {
    return false;
  }
  if (std::find(visited.begin(), visited.end(), offset) != visited.end()) {
    return false;  // a /Prev loop
  }
  visited.push_back(offset);

  Lexer lexer(bytes_, offset);
  lexer.skip_whitespace_and_comments();
  if (text_at(bytes_, lexer.position(), 4) == "xref") {
    lexer.seek(lexer.position() + 4);
    if (!parse_xref_table(lexer)) {
      return false;
    }
  } else {
    // A cross-reference stream: "N G obj << ... >> stream" (clause 7.5.8).
    auto token = lexer.next();
    if (!token.has_value() || token->keyword != "obj") {
      return false;
    }
    const auto stream_object = lexer.next_object();
    if (!parse_xref_stream(stream_object)) {
      return false;
    }
  }

  // Both forms may chain: /Prev to the previous section, and a hybrid file's
  // /XRefStm to a cross-reference stream holding the same objects for newer readers.
  const auto& xref_stm = trailer_.get("XRefStm");
  if (xref_stm.is_number()) {
    parse_xref_section(static_cast<std::size_t>(xref_stm.integer(0)), visited, notices);
  }
  const auto& previous = trailer_.get("Prev");
  if (previous.is_number()) {
    const auto previous_offset = previous.integer(-1);
    if (previous_offset >= 0) {
      parse_xref_section(static_cast<std::size_t>(previous_offset), visited, notices);
    }
  }
  return true;
}

bool File::parse_xref_table(Lexer& lexer) {
  while (true) {
    lexer.skip_whitespace_and_comments();
    if (text_at(bytes_, lexer.position(), 7) == "trailer") {
      lexer.seek(lexer.position() + 7);
      auto trailer = lexer.next_object();
      if (trailer.is_dictionary()) {
        // Earlier sections in the chain must not overwrite the newest values.
        if (!trailer_.is_dictionary()) {
          trailer_ = trailer;
        } else {
          Dictionary merged = *trailer_.dictionary();
          for (const auto& [key, value] : *trailer.dictionary()) {
            merged.try_emplace(key, value);
          }
          // Keep /Prev from the section just parsed so the chain walks correctly.
          const auto previous = trailer.dictionary()->find("Prev");
          if (previous != trailer.dictionary()->end()) {
            merged["Prev"] = previous->second;
          } else {
            merged.erase("Prev");
          }
          const auto xref_stm = trailer.dictionary()->find("XRefStm");
          if (xref_stm != trailer.dictionary()->end()) {
            merged["XRefStm"] = xref_stm->second;
          } else {
            merged.erase("XRefStm");
          }
          trailer_ = Object(std::move(merged));
        }
      }
      return true;
    }

    const auto start_object = lexer.next_object();
    const auto count_object = lexer.next_object();
    if (!start_object.is_number() || !count_object.is_number()) {
      return !locations_.empty();
    }
    const auto start = start_object.integer(0);
    const auto count = count_object.integer(0);
    if (count < 0 || count > 10'000'000) {
      return false;
    }
    for (std::int64_t index = 0; index < count; ++index) {
      lexer.skip_whitespace_and_comments();
      const auto entry = text_at(bytes_, lexer.position(), 20);
      if (entry.size() < 18) {
        return !locations_.empty();
      }
      std::int64_t entry_offset = 0;
      const auto parsed = std::from_chars(entry.data(), entry.data() + 10, entry_offset);
      const char type = entry[17];
      lexer.seek(lexer.position() + (entry.size() >= 20 && (entry[19] == '\n' || entry[19] == '\r') ? 20 : 18));
      if (parsed.ec != std::errc{} || type != 'n') {
        continue;  // a free entry, or an unreadable one
      }
      const auto number = static_cast<std::uint32_t>(start + index);
      if (entry_offset <= 0 || static_cast<std::size_t>(entry_offset) >= bytes_.size()) {
        continue;
      }
      // First writer wins: the chain is walked newest section first.
      locations_.try_emplace(number, Location{static_cast<std::size_t>(entry_offset), 0, 0, false});
    }
  }
}

bool File::parse_xref_stream(const Object& stream_object) {
  const auto* stream = stream_object.stream();
  if (stream == nullptr) {
    return false;
  }
  const auto data = stream_data(stream_object);
  if (data.data.empty()) {
    return false;
  }

  const auto widths = numbers(stream_object.get("W"));
  if (widths.size() < 3) {
    return false;
  }
  const auto field0 = static_cast<std::size_t>(std::max(0.0, widths[0]));
  const auto field1 = static_cast<std::size_t>(std::max(0.0, widths[1]));
  const auto field2 = static_cast<std::size_t>(std::max(0.0, widths[2]));
  const std::size_t entry_size = field0 + field1 + field2;
  if (entry_size == 0 || entry_size > 32) {
    return false;
  }

  // /Index defaults to [0 Size] (clause 7.5.8.2).
  std::vector<double> index = numbers(stream_object.get("Index"));
  if (index.size() < 2) {
    index = {0.0, stream_object.get("Size").number(0.0)};
  }

  const auto read_field = [&data](std::size_t offset, std::size_t width) -> std::uint64_t {
    std::uint64_t value = 0;
    for (std::size_t byte = 0; byte < width; ++byte) {
      value = (value << 8) | data.data[offset + byte];
    }
    return value;
  };

  std::size_t position = 0;
  for (std::size_t pair = 0; pair + 1 < index.size(); pair += 2) {
    const auto start = static_cast<std::int64_t>(index[pair]);
    const auto count = static_cast<std::int64_t>(index[pair + 1]);
    for (std::int64_t item = 0; item < count; ++item) {
      if (position + entry_size > data.data.size()) {
        break;
      }
      // A zero-width type field means type 1 (clause 7.5.8.3).
      const std::uint64_t type = field0 == 0 ? 1 : read_field(position, field0);
      const std::uint64_t second = field1 == 0 ? 0 : read_field(position + field0, field1);
      const std::uint64_t third = field2 == 0 ? 0 : read_field(position + field0 + field1, field2);
      position += entry_size;

      const auto number = static_cast<std::uint32_t>(start + item);
      if (type == 1) {
        if (second > 0 && second < bytes_.size()) {
          locations_.try_emplace(number, Location{static_cast<std::size_t>(second), 0, 0, false});
        }
        continue;
      }
      if (type == 2) {
        locations_.try_emplace(number, Location{0, static_cast<std::uint32_t>(second),
                                                static_cast<std::uint32_t>(third), true});
      }
      // type 0 is a free object.
    }
  }

  // A cross-reference stream's own dictionary is the trailer.
  if (!trailer_.is_dictionary()) {
    Dictionary merged;
    if (const auto* dict = stream_object.dictionary(); dict != nullptr) {
      merged = *dict;
    }
    trailer_ = Object(std::move(merged));
  } else if (const auto* dict = stream_object.dictionary(); dict != nullptr) {
    Dictionary merged = *trailer_.dictionary();
    for (const auto& [key, value] : *dict) {
      merged.try_emplace(key, value);
    }
    const auto previous = dict->find("Prev");
    if (previous != dict->end()) {
      merged["Prev"] = previous->second;
    } else {
      merged.erase("Prev");
    }
    merged.erase("XRefStm");
    trailer_ = Object(std::move(merged));
  }
  return true;
}

void File::reconstruct_by_scanning(std::vector<std::string>* notices) {
  // Every viewer does this when the xref is unusable: sweep the whole file for
  // "N G obj" headers and believe the last definition of each object number.
  scanned_ = true;
  locations_.clear();
  cache_.clear();
  loaded_object_streams_.clear();

  std::size_t found = 0;
  for (std::size_t position = 0; position + 3 < bytes_.size(); ++position) {
    if (bytes_[position] != 'o' || bytes_[position + 1] != 'b' || bytes_[position + 2] != 'j') {
      continue;
    }
    if (position + 3 < bytes_.size() && is_regular(bytes_[position + 3])) {
      continue;
    }
    // Walk back over "  G  N" to the start of the object number.
    std::size_t cursor = position;
    const auto skip_back_whitespace = [&] {
      while (cursor > 0 && is_whitespace(bytes_[cursor - 1])) {
        --cursor;
      }
    };
    const auto skip_back_digits = [&]() -> bool {
      const std::size_t end = cursor;
      while (cursor > 0 && bytes_[cursor - 1] >= '0' && bytes_[cursor - 1] <= '9') {
        --cursor;
      }
      return cursor < end;
    };
    skip_back_whitespace();
    if (!skip_back_digits()) {
      continue;
    }
    skip_back_whitespace();
    const std::size_t number_end = cursor;
    if (!skip_back_digits()) {
      continue;
    }
    std::uint32_t number = 0;
    const auto* first = reinterpret_cast<const char*>(bytes_.data()) + cursor;
    const auto* last = reinterpret_cast<const char*>(bytes_.data()) + number_end;
    if (std::from_chars(first, last, number).ec != std::errc{}) {
      continue;
    }
    locations_[number] = Location{cursor, 0, 0, false};
    ++found;
  }

  // The trailer may itself be gone; recover /Root by finding a catalog object.
  if (!get(trailer_, "Root").is_dictionary()) {
    const auto trailer_position = find_backwards(bytes_, "trailer", bytes_.size());
    if (trailer_position != std::string_view::npos) {
      Lexer lexer(bytes_, trailer_position + 7);
      auto recovered = lexer.next_object();
      if (recovered.is_dictionary()) {
        trailer_ = std::move(recovered);
      }
    }
  }
  if (!get(trailer_, "Root").is_dictionary()) {
    for (const auto& [number, location] : locations_) {
      const auto& candidate = object(Reference{number, 0});
      if (candidate.get("Type").name() == "Catalog") {
        Dictionary recovered;
        if (trailer_.is_dictionary()) {
          recovered = *trailer_.dictionary();
        }
        recovered["Root"] = Object(Reference{number, 0});
        trailer_ = Object(std::move(recovered));
        break;
      }
    }
  }

  if (notices != nullptr && found > 0) {
    notices->push_back("The PDF cross-reference table was damaged; the file was rebuilt by scanning it.");
  }
}

void File::load_object_stream(std::uint32_t stream_number) const {
  if (loaded_object_streams_[stream_number]) {
    return;
  }
  loaded_object_streams_[stream_number] = true;

  const auto& container = object(Reference{stream_number, 0});
  if (container.stream() == nullptr) {
    return;
  }
  const auto data = stream_data(container);
  if (data.data.empty()) {
    return;
  }
  const auto count = get(container, "N").integer(0);
  const auto first = get(container, "First").integer(0);
  if (count <= 0 || first < 0 || static_cast<std::size_t>(first) > data.data.size()) {
    return;
  }

  // The header is N pairs of "objectNumber offset", offsets relative to /First.
  Lexer header(data.data, 0);
  std::vector<std::pair<std::uint32_t, std::size_t>> entries;
  entries.reserve(static_cast<std::size_t>(count));
  for (std::int64_t index = 0; index < count; ++index) {
    const auto number = header.next_object();
    const auto offset = header.next_object();
    if (!number.is_number() || !offset.is_number()) {
      break;
    }
    const auto absolute = static_cast<std::size_t>(first) + static_cast<std::size_t>(offset.integer(0));
    if (absolute >= data.data.size()) {
      continue;
    }
    entries.emplace_back(static_cast<std::uint32_t>(number.integer(0)), absolute);
  }

  for (const auto& [number, offset] : entries) {
    // Only fill objects the xref actually assigned to this stream, and never
    // overwrite one already parsed from a newer section.
    const auto location = locations_.find(number);
    if (location == locations_.end() || !location->second.in_object_stream ||
        location->second.container_stream != stream_number) {
      continue;
    }
    if (cache_.contains(number)) {
      continue;
    }
    Lexer lexer(data.data, offset);
    cache_.emplace(number, lexer.next_object());
  }
}

std::size_t File::resolve_stream_length(const RawStream& stream) const {
  const auto it = stream.dict.find("Length");
  std::int64_t declared = -1;
  if (it != stream.dict.end()) {
    const auto& length = resolve(it->second);
    if (length.is_number()) {
      declared = length.integer(-1);
    }
  }

  const std::size_t available = bytes_.size() - std::min(stream.data_offset, bytes_.size());
  if (declared >= 0 && static_cast<std::size_t>(declared) <= available) {
    // Trust the declared length only if "endstream" really follows it; producers
    // get this wrong often enough that checking is worth the scan.
    std::size_t after = stream.data_offset + static_cast<std::size_t>(declared);
    std::size_t probe = after;
    while (probe < bytes_.size() && probe < after + 4 && is_whitespace(bytes_[probe])) {
      ++probe;
    }
    if (text_at(bytes_, probe, 9) == "endstream") {
      return static_cast<std::size_t>(declared);
    }
  }

  // Fall back to the first "endstream" after the data starts.
  const auto haystack = text_at(bytes_, stream.data_offset, available);
  const auto found = haystack.find("endstream");
  if (found == std::string_view::npos) {
    return declared >= 0 ? std::min(static_cast<std::size_t>(declared), available) : available;
  }
  std::size_t length = found;
  // "endstream" is preceded by an EOL that is not part of the data.
  while (length > 0 && (haystack[length - 1] == '\n' || haystack[length - 1] == '\r')) {
    --length;
  }
  return length;
}

const Object& File::object(Reference reference) const {
  const auto cached = cache_.find(reference.number);
  if (cached != cache_.end()) {
    return cached->second;
  }
  const auto location = locations_.find(reference.number);
  if (location == locations_.end()) {
    return null_object();
  }

  if (location->second.in_object_stream) {
    load_object_stream(location->second.container_stream);
    const auto loaded = cache_.find(reference.number);
    return loaded != cache_.end() ? loaded->second : null_object();
  }

  // Reserve the slot before parsing so a self-referential object cannot recurse.
  auto [slot, inserted] = cache_.emplace(reference.number, Object());
  Lexer lexer(bytes_, location->second.offset);
  auto token = lexer.next();
  if (!token.has_value() || token->keyword != "obj") {
    return slot->second;
  }
  const auto header = token->object.reference();
  if (header.has_value() && header->number != reference.number) {
    // The offset points at the wrong object: the xref is stale. One full rescan is
    // allowed, then the lookup is retried against the rebuilt table.
    if (!scanned_) {
      auto* mutable_self = const_cast<File*>(this);
      mutable_self->reconstruct_by_scanning(nullptr);
      return object(reference);
    }
    return slot->second;
  }
  slot->second = lexer.next_object();
  return slot->second;
}

const Object& File::resolve(const Object& value) const {
  if (!value.is_reference()) {
    return value;
  }
  if (resolve_depth_ >= kMaximumResolveDepth) {
    return null_object();
  }
  ++resolve_depth_;
  const auto& resolved = resolve(object(*value.reference()));
  --resolve_depth_;
  return resolved;
}

const Object& File::get(const Object& container, std::string_view key) const {
  return resolve(container.get(key));
}

const Object& File::get_any(const Object& container, std::string_view key, std::string_view alternate) const {
  const auto& primary = get(container, key);
  return primary.is_null() ? get(container, alternate) : primary;
}

std::vector<double> File::numbers(const Object& array_object) const {
  std::vector<double> values;
  const auto& resolved = resolve(array_object);
  const auto* array = resolved.array();
  if (array == nullptr) {
    return values;
  }
  values.reserve(array->size());
  for (const auto& item : *array) {
    values.push_back(resolve(item).number(0.0));
  }
  return values;
}

std::vector<FilterStep> File::filter_chain(const Object& stream_object) const {
  std::vector<FilterStep> steps;
  const auto& filters = get_any(stream_object, "Filter", "F");
  const auto& parms = get_any(stream_object, "DecodeParms", "DP");

  const auto parms_at = [&](std::size_t index) -> Dictionary {
    const auto* array = parms.array();
    if (array != nullptr) {
      if (index < array->size()) {
        const auto& entry = resolve((*array)[index]);
        if (const auto* dict = entry.dictionary(); dict != nullptr) {
          return *dict;
        }
      }
      return {};
    }
    if (index == 0) {
      if (const auto* dict = parms.dictionary(); dict != nullptr) {
        return *dict;
      }
    }
    return {};
  };

  if (filters.is_name()) {
    steps.push_back(FilterStep{filter_kind_from_name(filters.name()), std::string(filters.name()), parms_at(0)});
    return steps;
  }
  if (const auto* array = filters.array(); array != nullptr) {
    steps.reserve(array->size());
    for (std::size_t index = 0; index < array->size(); ++index) {
      const auto& entry = resolve((*array)[index]);
      if (!entry.is_name()) {
        continue;
      }
      steps.push_back(FilterStep{filter_kind_from_name(entry.name()), std::string(entry.name()), parms_at(index)});
    }
  }
  return steps;
}

std::span<const std::uint8_t> File::raw_stream_bytes(const Object& stream_object) const {
  const auto* stream = stream_object.stream();
  if (stream == nullptr || stream->data_offset >= bytes_.size()) {
    return {};
  }
  const auto length = resolve_stream_length(*stream);
  return std::span<const std::uint8_t>(bytes_.data() + stream->data_offset,
                                       std::min(length, bytes_.size() - stream->data_offset));
}

File::StreamData File::stream_data(const Object& stream_object) const {
  StreamData result;
  const auto raw = raw_stream_bytes(stream_object);
  if (raw.empty()) {
    return result;
  }
  auto decoded = apply_filter_chain(raw, filter_chain(stream_object));
  result.data = std::move(decoded.data);
  result.image_codec = decoded.image_codec;
  result.error = std::move(decoded.error);
  return result;
}

const Object& File::catalog() const {
  return get(trailer_, "Root");
}

void File::collect_pages(std::vector<std::string>* notices) {
  pages_.clear();
  const auto& root = catalog();
  const auto& page_tree = get(root, "Pages");

  struct Inherited {
    Object resources;
    std::vector<double> media_box;
    std::vector<double> crop_box;
    int rotate{0};
    bool has_rotate{false};
  };

  std::vector<Reference> guard;
  const auto walk = [&](auto&& self, const Object& node, Inherited inherited, int depth) -> void {
    if (depth > 64 || pages_.size() >= kMaximumPages) {
      return;
    }
    if (const auto& resources = get(node, "Resources"); !resources.is_null()) {
      inherited.resources = resources;
    }
    if (auto box = numbers(node.get("MediaBox")); box.size() >= 4) {
      inherited.media_box = std::move(box);
    }
    if (auto box = numbers(node.get("CropBox")); box.size() >= 4) {
      inherited.crop_box = std::move(box);
    }
    if (const auto& rotate = get(node, "Rotate"); rotate.is_number()) {
      inherited.rotate = static_cast<int>(rotate.integer(0));
      inherited.has_rotate = true;
    }

    const auto& kids = get(node, "Kids");
    const auto* kid_array = kids.array();
    const bool is_leaf = kid_array == nullptr || get(node, "Type").name() == "Page";
    if (is_leaf) {
      Page page;
      page.dict = node;
      page.resources = inherited.resources;
      if (!inherited.media_box.empty()) {
        normalize_box(inherited.media_box, page.media_box);
      }
      // /CropBox defaults to /MediaBox, and PDFium clips to their intersection.
      if (!inherited.crop_box.empty()) {
        normalize_box(inherited.crop_box, page.crop_box);
        page.crop_box[0] = std::max(page.crop_box[0], page.media_box[0]);
        page.crop_box[1] = std::max(page.crop_box[1], page.media_box[1]);
        page.crop_box[2] = std::min(page.crop_box[2], page.media_box[2]);
        page.crop_box[3] = std::min(page.crop_box[3], page.media_box[3]);
        if (page.crop_box[2] <= page.crop_box[0] || page.crop_box[3] <= page.crop_box[1]) {
          std::copy(std::begin(page.media_box), std::end(page.media_box), std::begin(page.crop_box));
        }
      } else {
        std::copy(std::begin(page.media_box), std::end(page.media_box), std::begin(page.crop_box));
      }
      page.rotate = ((inherited.rotate % 360) + 360) % 360;
      page.rotate = (page.rotate / 90) * 90;
      pages_.push_back(std::move(page));
      return;
    }

    for (const auto& kid : *kid_array) {
      if (const auto reference = kid.reference(); reference.has_value()) {
        if (std::find(guard.begin(), guard.end(), *reference) != guard.end()) {
          continue;  // a cycle in the page tree
        }
        guard.push_back(*reference);
        self(self, resolve(kid), inherited, depth + 1);
        guard.pop_back();
        continue;
      }
      self(self, resolve(kid), inherited, depth + 1);
    }
  };

  if (page_tree.is_dictionary()) {
    walk(walk, page_tree, Inherited{}, 0);
  }

  if (pages_.empty()) {
    // No usable page tree: gather anything that calls itself a Page, in object order.
    for (const auto& [number, location] : locations_) {
      const auto& candidate = object(Reference{number, 0});
      if (candidate.get("Type").name() != "Page") {
        continue;
      }
      Page page;
      page.dict = candidate;
      page.resources = get(candidate, "Resources");
      if (auto box = numbers(candidate.get("MediaBox")); box.size() >= 4) {
        normalize_box(box, page.media_box);
      }
      std::copy(std::begin(page.media_box), std::end(page.media_box), std::begin(page.crop_box));
      pages_.push_back(std::move(page));
      if (pages_.size() >= kMaximumPages) {
        break;
      }
    }
    if (!pages_.empty() && notices != nullptr) {
      notices->push_back("The PDF page tree was unreadable; pages were recovered individually.");
    }
  }
}

}  // namespace patchy::pdf
