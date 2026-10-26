#include "formats/metadata/gimp_image_metadata.hpp"

#include "formats/metadata/exif_tags.hpp"
#include "formats/metadata/iptc_iim.hpp"
#include "formats/metadata/metadata_view.hpp"
#include "formats/metadata/tiff_ifd.hpp"
#include "formats/svg_xml.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <exception>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace patchy::metadata {
namespace {

constexpr std::size_t kMaxTags = 20000;
constexpr std::size_t kMaxArrayIndex = 4096;
constexpr std::size_t kMaxPathSegments = 16;
constexpr std::size_t kMaxIimValue = 32000;

// --- XMP ------------------------------------------------------------------------------

// exiv2's prefixes for namespaces a file may use without listing a <namespace> for them.
constexpr std::pair<std::string_view, std::string_view> kExiv2Prefixes[] = {
    {"dc", xmp_ns::kDc},
    {"xmp", xmp_ns::kXmp},
    {"xmpRights", xmp_ns::kXmpRights},
    {"xmpMM", xmp_ns::kXmpMM},
    {"stEvt", xmp_ns::kStEvt},
    {"stRef", xmp_ns::kStRef},
    {"photoshop", xmp_ns::kPhotoshop},
    {"tiff", xmp_ns::kTiff},
    {"exif", xmp_ns::kExif},
    {"exifEX", xmp_ns::kExifEx},
    {"aux", xmp_ns::kAux},
    {"crs", xmp_ns::kCrs},
    {"iptc", xmp_ns::kIptcCore},
    {"Iptc4xmpCore", xmp_ns::kIptcCore},
    {"iptcExt", xmp_ns::kIptcExt},
    {"Iptc4xmpExt", xmp_ns::kIptcExt},
    {"plus", xmp_ns::kPlus},
    {"pdf", xmp_ns::kPdf},
    {"xmpNote", xmp_ns::kXmpNote},
    {"xmpGImg", xmp_ns::kXmpGImg},
    {"lr", xmp_ns::kLightroom},
    {"DICOM", "http://ns.adobe.com/DICOM/"},
    {"dcterms", "http://purl.org/dc/terms/"},
};

struct ArrayProperty {
  std::string_view ns;
  std::string_view name;
  XmpValue::Kind kind;
};

// Array properties whose single value GIMP writes as one plain tag; the schema says
// they are arrays even with one item. Lang-alt values identify themselves (lang="...").
constexpr ArrayProperty kArrayProperties[] = {
    {xmp_ns::kDc, "contributor", XmpValue::Kind::Bag},
    {xmp_ns::kDc, "creator", XmpValue::Kind::Seq},
    {xmp_ns::kDc, "date", XmpValue::Kind::Seq},
    {xmp_ns::kDc, "language", XmpValue::Kind::Bag},
    {xmp_ns::kDc, "publisher", XmpValue::Kind::Bag},
    {xmp_ns::kDc, "relation", XmpValue::Kind::Bag},
    {xmp_ns::kDc, "subject", XmpValue::Kind::Bag},
    {xmp_ns::kDc, "type", XmpValue::Kind::Bag},
    {xmp_ns::kPhotoshop, "SupplementalCategories", XmpValue::Kind::Bag},
    {xmp_ns::kPhotoshop, "DocumentAncestors", XmpValue::Kind::Bag},
    {xmp_ns::kXmp, "Identifier", XmpValue::Kind::Bag},
    {xmp_ns::kXmp, "Advisory", XmpValue::Kind::Bag},
    {xmp_ns::kXmpRights, "Owner", XmpValue::Kind::Bag},
    {xmp_ns::kXmpMM, "History", XmpValue::Kind::Seq},
    {xmp_ns::kXmpMM, "Ingredients", XmpValue::Kind::Bag},
    {xmp_ns::kXmpMM, "Pantry", XmpValue::Kind::Bag},
    {xmp_ns::kIptcCore, "SubjectCode", XmpValue::Kind::Bag},
    {xmp_ns::kIptcCore, "Scene", XmpValue::Kind::Bag},
    {xmp_ns::kIptcExt, "PersonInImage", XmpValue::Kind::Bag},
    {xmp_ns::kIptcExt, "OrganisationInImageName", XmpValue::Kind::Bag},
    {xmp_ns::kIptcExt, "OrganisationInImageCode", XmpValue::Kind::Bag},
    {xmp_ns::kIptcExt, "ModelAge", XmpValue::Kind::Bag},
    {xmp_ns::kIptcExt, "LocationShown", XmpValue::Kind::Bag},
    {xmp_ns::kIptcExt, "LocationCreated", XmpValue::Kind::Bag},
    {xmp_ns::kIptcExt, "ArtworkOrObject", XmpValue::Kind::Bag},
    {xmp_ns::kIptcExt, "RegistryId", XmpValue::Kind::Bag},
    {xmp_ns::kPlus, "ImageCreator", XmpValue::Kind::Seq},
    {xmp_ns::kPlus, "CopyrightOwner", XmpValue::Kind::Seq},
    {xmp_ns::kPlus, "Licensor", XmpValue::Kind::Bag},
    {xmp_ns::kPlus, "PropertyReleaseID", XmpValue::Kind::Bag},
    {xmp_ns::kPlus, "ModelReleaseID", XmpValue::Kind::Bag},
    {xmp_ns::kLightroom, "hierarchicalSubject", XmpValue::Kind::Bag},
    {xmp_ns::kExif, "ISOSpeedRatings", XmpValue::Kind::Seq},
};

[[nodiscard]] std::optional<XmpValue::Kind> array_kind(std::string_view ns, std::string_view name) {
  for (const auto& entry : kArrayProperties) {
    if (entry.ns == ns && entry.name == name) {
      return entry.kind;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool valid_name(std::string_view name) {
  if (name.empty() || name.size() > 256) {
    return false;
  }
  const auto c0 = static_cast<unsigned char>(name.front());
  if (!(std::isalpha(c0) || c0 == '_')) {
    return false;
  }
  for (const char c : name) {
    const auto u = static_cast<unsigned char>(c);
    if (!(std::isalnum(u) || u == '_' || u == '-' || u == '.')) {
      return false;
    }
  }
  return true;
}

class XmpBuilder {
public:
  void add_namespace(const std::string& prefix, const std::string& url) {
    if (valid_name(prefix) && !url.empty()) {
      namespaces_[prefix] = url;
    }
  }

  // `key` is the exiv2 key without its "Xmp." prefix: "dc.title",
  // "xmpMM.History[1]/stEvt:action", "iptc.CreatorContactInfo/Iptc4xmpCore:CiAdrCity".
  void add(std::string_view key, const std::string& value) {
    const auto dot = key.find('.');
    if (dot == std::string_view::npos || key.find('?') != std::string_view::npos) {
      return;  // not a property path, or a qualifier
    }
    const auto ns = ns_for(key.substr(0, dot));
    const auto rest = key.substr(dot + 1);
    auto pos = std::min(rest.find_first_of("[/"), rest.size());
    const auto name = rest.substr(0, pos);
    if (ns.empty() || !valid_name(name)) {
      return;
    }
    // GIMP also writes CreatorContactInfo's fields (Xmp.iptc.CiAdrCity) at the top level,
    // where the schema has no such properties; the struct copy is the real one.
    if (ns == xmp_ns::kIptcCore && name.starts_with("Ci")) {
      return;
    }
    auto* property = packet_.find(ns, name);
    if (property == nullptr) {
      packet_.properties.push_back(XmpProperty{ns, std::string(name), {}});
      property = &packet_.properties.back();
      if (const auto kind = array_kind(ns, name)) {
        property->value.kind = *kind;
      }
      remember_prefix(ns, key.substr(0, dot));
    }
    XmpValue* node = &property->value;
    std::size_t segments = 0;
    while (pos < rest.size()) {
      if (++segments > kMaxPathSegments) {
        return;
      }
      if (rest[pos] == '[') {
        const auto close = rest.find(']', pos);
        if (close == std::string_view::npos) {
          return;
        }
        std::size_t index = 0;
        const auto digits = rest.substr(pos + 1, close - pos - 1);
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), index);
        if (error != std::errc{} || end != digits.data() + digits.size() || index == 0 || index > kMaxArrayIndex) {
          return;
        }
        if (!node->is_array()) {
          if (node->kind != XmpValue::Kind::Simple || !node->text.empty()) {
            return;
          }
          node->kind = XmpValue::Kind::Bag;
        }
        if (node->items.size() < index) {
          node->items.resize(index);
        }
        node = &node->items[index - 1];
        pos = close + 1;
      } else if (rest[pos] == '/') {
        const auto colon = rest.find(':', pos);
        if (colon == std::string_view::npos) {
          return;
        }
        const auto field_ns = ns_for(rest.substr(pos + 1, colon - pos - 1));
        const auto field_end = std::min(rest.find_first_of("[/", colon + 1), rest.size());
        const auto field_name = rest.substr(colon + 1, field_end - colon - 1);
        if (field_ns.empty() || !valid_name(field_name)) {
          return;
        }
        if (node->kind != XmpValue::Kind::Struct) {
          if (node->kind != XmpValue::Kind::Simple || !node->text.empty()) {
            return;
          }
          node->kind = XmpValue::Kind::Struct;
        }
        XmpProperty* field = nullptr;
        for (auto& candidate : node->fields) {
          if (candidate.ns == field_ns && candidate.name == field_name) {
            field = &candidate;
          }
        }
        if (field == nullptr) {
          node->fields.push_back(XmpProperty{field_ns, std::string(field_name), {}});
          field = &node->fields.back();
          remember_prefix(field_ns, rest.substr(pos + 1, colon - pos - 1));
        }
        node = &field->value;
        pos = field_end;
      } else {
        return;
      }
    }
    assign(*node, value);
  }

  [[nodiscard]] std::optional<XmpPacket> finish() {
    std::erase_if(packet_.properties, [](XmpProperty& property) { return prune(property.value); });
    if (packet_.properties.empty()) {
      return std::nullopt;
    }
    return std::move(packet_);
  }

private:
  [[nodiscard]] std::string ns_for(std::string_view prefix) const {
    if (const auto it = namespaces_.find(std::string(prefix)); it != namespaces_.end()) {
      return it->second;
    }
    for (const auto& [known, url] : kExiv2Prefixes) {
      if (known == prefix) {
        return std::string(url);
      }
    }
    return {};
  }

  // The serializer picks the well-known prefix; others keep the one the file used.
  void remember_prefix(const std::string& ns, std::string_view prefix) {
    if (well_known_xmp_prefix(ns).empty() && !packet_.prefixes.contains(ns)) {
      packet_.prefixes.emplace(ns, std::string(prefix));
    }
  }

  // exiv2's text forms: type="Bag" declares an (often empty) array, lang="x-default" T is
  // one lang-alt item, and a repeated tag is one more array item.
  static void assign(XmpValue& node, const std::string& value) {
    constexpr std::string_view kType = "type=\"";
    constexpr std::string_view kLang = "lang=\"";
    if (value.starts_with(kType)) {
      const auto type = std::string_view(value).substr(kType.size());
      if (node.kind == XmpValue::Kind::Simple && node.text.empty() && node.items.empty()) {
        if (type.starts_with("Bag\"")) {
          node.kind = XmpValue::Kind::Bag;
        } else if (type.starts_with("Seq\"")) {
          node.kind = XmpValue::Kind::Seq;
        } else if (type.starts_with("Alt\"")) {
          node.kind = XmpValue::Kind::Alt;
        } else if (type.starts_with("Struct\"")) {
          node.kind = XmpValue::Kind::Struct;
        }
      }
      return;
    }
    if (value.starts_with(kLang)) {
      const auto close = value.find('"', kLang.size());
      if (close == std::string::npos) {
        return;
      }
      XmpValue item;
      item.lang = value.substr(kLang.size(), close - kLang.size());
      auto start = close + 1;
      if (start < value.size() && value[start] == ' ') {
        ++start;
      }
      item.text = value.substr(start);
      if (node.kind == XmpValue::Kind::Simple && node.text.empty()) {
        node.kind = XmpValue::Kind::Alt;
      }
      if (node.kind != XmpValue::Kind::Alt) {
        return;
      }
      for (auto& existing : node.items) {
        if (existing.lang == item.lang) {
          existing.text = std::move(item.text);
          return;
        }
      }
      node.items.push_back(std::move(item));
      return;
    }
    if (node.is_array()) {
      XmpValue item;
      item.text = value;
      node.items.push_back(std::move(item));
      return;
    }
    if (node.kind != XmpValue::Kind::Simple) {
      return;
    }
    if (!node.text.empty()) {
      XmpValue first;
      first.text = std::move(node.text);
      XmpValue second;
      second.text = value;
      node = XmpValue{};
      node.kind = XmpValue::Kind::Bag;
      node.items.push_back(std::move(first));
      node.items.push_back(std::move(second));
      return;
    }
    node.text = value;
  }

  // Drops what holds nothing (GIMP lists empty Bags for every struct array); true when
  // `value` itself is empty.
  static bool prune(XmpValue& value) {
    switch (value.kind) {
      case XmpValue::Kind::Simple:
        return value.text.empty();
      case XmpValue::Kind::Struct:
        std::erase_if(value.fields, [](XmpProperty& field) { return prune(field.value); });
        return value.fields.empty();
      default:
        std::erase_if(value.items, [](XmpValue& item) { return prune(item); });
        return value.items.empty();
    }
  }

  XmpPacket packet_;
  std::map<std::string, std::string> namespaces_;
};

// --- Exif -----------------------------------------------------------------------------

[[nodiscard]] std::optional<IfdKind> exif_group(std::string_view group) {
  if (group == "Image") {
    return IfdKind::Ifd0;
  }
  if (group == "Photo") {
    return IfdKind::Exif;
  }
  if (group == "GPSInfo") {
    return IfdKind::Gps;
  }
  if (group == "Iop") {
    return IfdKind::Interop;
  }
  return std::nullopt;  // Exif.Thumbnail, maker notes and the like
}

[[nodiscard]] bool is_ascii_tag(IfdKind kind, std::uint16_t tag) {
  switch (kind) {
    case IfdKind::Ifd0:
      return tag == 269 || tag == 270 || tag == 271 || tag == 272 || tag == 285 || tag == 305 || tag == 306 ||
             tag == 315 || tag == 316 || tag == 32781 || tag == 33432;
    case IfdKind::Exif:
      return tag == 34852 || tag == 36867 || tag == 36868 || (tag >= 36880 && tag <= 36882) ||
             (tag >= 37520 && tag <= 37522) || tag == 40964 || tag == 42016 || (tag >= 42032 && tag <= 42033) ||
             (tag >= 42035 && tag <= 42044);
    case IfdKind::Gps:
      return tag == 1 || tag == 3 || tag == 9 || tag == 10 || tag == 12 || tag == 14 || tag == 16 || tag == 18 ||
             tag == 19 || tag == 21 || tag == 23 || tag == 25 || tag == 29;
    case IfdKind::Interop:
      return tag == 1;
    default:
      return false;
  }
}

// Comment-style UNDEFINED tags: an 8-byte character code, then the text.
[[nodiscard]] bool is_comment_tag(IfdKind kind, std::uint16_t tag) {
  return (kind == IfdKind::Exif && tag == exif_tag::kUserComment) ||
         (kind == IfdKind::Gps && (tag == 27 || tag == 28));
}

[[nodiscard]] bool is_undefined_tag(IfdKind kind, std::uint16_t tag) {
  if (kind == IfdKind::Exif) {
    return tag == 34856 || tag == 36864 || tag == 37121 || tag == 40960 || tag == 41484 || tag == 41728 ||
           tag == 41729 || tag == 41730 || tag == 41995;
  }
  return kind == IfdKind::Interop && tag == 2;
}

[[nodiscard]] bool is_byte_tag(IfdKind kind, std::uint16_t tag) {
  return (kind == IfdKind::Gps && (tag == 0 || tag == 5)) || (kind == IfdKind::Ifd0 && tag >= 40091 && tag <= 40095);
}

[[nodiscard]] bool is_long_tag(IfdKind kind, std::uint16_t tag) {
  return (kind == IfdKind::Exif && (tag == 34865 || tag == 34866 || tag == 34867 || tag == 40962 || tag == 40963));
}

[[nodiscard]] bool is_signed_rational_tag(IfdKind kind, std::uint16_t tag) {
  return kind == IfdKind::Exif &&
         (tag == 37377 || tag == 37379 || tag == 37380 || tag == 37888 || tag == 37891 || tag == 37893);
}

// Tags that point into the original file or only make sense with its bytes.
[[nodiscard]] bool skipped_exif_tag(std::uint16_t tag) {
  return is_tiff_structure_tag(tag) || tag == kTagExifIfd || tag == kTagGpsIfd || tag == kTagInteropIfd ||
         tag == kTagJpegInterchangeFormat || tag == kTagJpegInterchangeFormatLength || tag == exif_tag::kMakerNote;
}

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 8));
  out.push_back(static_cast<std::uint8_t>(value));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    out.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

[[nodiscard]] std::vector<std::string_view> tokens(std::string_view text) {
  std::vector<std::string_view> out;
  std::size_t pos = 0;
  while (pos < text.size()) {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) {
      ++pos;
    }
    const auto start = pos;
    while (pos < text.size() && text[pos] != ' ' && text[pos] != '\t' && text[pos] != '\n' && text[pos] != '\r') {
      ++pos;
    }
    if (pos > start) {
      out.push_back(text.substr(start, pos - start));
    }
  }
  return out;
}

[[nodiscard]] std::optional<std::int64_t> integer(std::string_view text) {
  std::int64_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// UTF-8 to UTF-16BE; invalid sequences become U+FFFD.
[[nodiscard]] std::vector<std::uint8_t> utf16be(std::string_view text) {
  std::vector<std::uint8_t> out;
  std::size_t i = 0;
  while (i < text.size()) {
    const auto c = static_cast<unsigned char>(text[i]);
    std::uint32_t code = 0xFFFD;
    std::size_t length = 1;
    if (c < 0x80) {
      code = c;
    } else if ((c & 0xE0) == 0xC0) {
      length = 2;
    } else if ((c & 0xF0) == 0xE0) {
      length = 3;
    } else if ((c & 0xF8) == 0xF0) {
      length = 4;
    }
    if (length > 1) {
      if (i + length > text.size()) {
        length = 1;
      } else {
        code = c & (0x7F >> length);
        for (std::size_t k = 1; k < length; ++k) {
          const auto next = static_cast<unsigned char>(text[i + k]);
          if ((next & 0xC0) != 0x80) {
            code = 0xFFFD;
            length = k;
            break;
          }
          code = (code << 6) | (next & 0x3F);
        }
      }
    }
    i += length;
    if (code > 0x10FFFF) {
      code = 0xFFFD;
    }
    if (code >= 0x10000) {
      code -= 0x10000;
      put_u16(out, static_cast<std::uint16_t>(0xD800 + (code >> 10)));
      put_u16(out, static_cast<std::uint16_t>(0xDC00 + (code & 0x3FF)));
    } else {
      put_u16(out, static_cast<std::uint16_t>(code));
    }
  }
  return out;
}

// exiv2's CommentValue text: an optional charset="Ascii" (or charset=Unicode) prefix, then
// the comment.
[[nodiscard]] TiffEntry comment_entry(std::uint16_t tag, std::string_view value) {
  std::string_view charset;
  if (value.starts_with("charset=")) {
    const auto space = value.find(' ');
    charset = value.substr(8, (space == std::string_view::npos ? value.size() : space) - 8);
    if (charset.size() >= 2 && charset.front() == '"' && charset.back() == '"') {
      charset = charset.substr(1, charset.size() - 2);
    }
    value = space == std::string_view::npos ? std::string_view{} : value.substr(space + 1);
  }
  bool ascii = true;
  for (const char c : value) {
    ascii = ascii && static_cast<unsigned char>(c) < 0x80;
  }
  TiffEntry entry;
  entry.tag = tag;
  entry.type = static_cast<std::uint16_t>(TiffType::Undefined);
  if (charset == "Unicode" || (charset.empty() && !ascii)) {
    constexpr std::array<std::uint8_t, 8> kUnicode{'U', 'N', 'I', 'C', 'O', 'D', 'E', 0};
    entry.data.assign(kUnicode.begin(), kUnicode.end());
    const auto text = utf16be(value);
    entry.data.insert(entry.data.end(), text.begin(), text.end());
  } else {
    constexpr std::array<std::uint8_t, 8> kAscii{'A', 'S', 'C', 'I', 'I', 0, 0, 0};
    constexpr std::array<std::uint8_t, 8> kUndefined{};
    const auto& code = charset == "Undefined" ? kUndefined : kAscii;
    entry.data.assign(code.begin(), code.end());
    entry.data.insert(entry.data.end(), value.begin(), value.end());
  }
  entry.count = static_cast<std::uint32_t>(entry.data.size());
  return entry;
}

// exiv2's toString of a value: ASCII as is, numbers and rationals ("300/1") separated by
// spaces, UNDEFINED bytes as decimal numbers.
[[nodiscard]] std::optional<TiffEntry> exif_entry(IfdKind kind, std::uint16_t tag, const std::string& value) {
  if (is_comment_tag(kind, tag)) {
    return comment_entry(tag, value);
  }
  TiffEntry entry;
  entry.tag = tag;
  if (is_ascii_tag(kind, tag)) {
    entry.type = static_cast<std::uint16_t>(TiffType::Ascii);
    entry.data.assign(value.begin(), value.end());
    entry.data.push_back(0);
    entry.count = static_cast<std::uint32_t>(entry.data.size());
    return entry;
  }
  const auto parts = tokens(value);
  if (parts.empty() || parts.size() > 4096) {
    return std::nullopt;
  }
  if (is_undefined_tag(kind, tag)) {
    entry.type = static_cast<std::uint16_t>(TiffType::Undefined);
    for (const auto part : parts) {
      const auto number = integer(part);
      if (!number || *number < 0 || *number > 255) {
        entry.data.assign(value.begin(), value.end());  // already the raw text ("0231")
        break;
      }
      entry.data.push_back(static_cast<std::uint8_t>(*number));
    }
    entry.count = static_cast<std::uint32_t>(entry.data.size());
    return entry;
  }
  if (value.find('/') != std::string::npos) {
    const bool want_signed = is_signed_rational_tag(kind, tag);
    entry.type = static_cast<std::uint16_t>(want_signed ? TiffType::SRational : TiffType::Rational);
    for (const auto part : parts) {
      const auto slash = part.find('/');
      if (slash == std::string_view::npos) {
        return std::nullopt;
      }
      const auto numerator = integer(part.substr(0, slash));
      const auto denominator = integer(part.substr(slash + 1));
      if (!numerator || !denominator) {
        return std::nullopt;
      }
      const auto low = want_signed ? std::int64_t{INT32_MIN} : std::int64_t{0};
      const auto high = want_signed ? std::int64_t{INT32_MAX} : std::int64_t{UINT32_MAX};
      if (*numerator < low || *numerator > high || *denominator < low || *denominator > high) {
        return std::nullopt;
      }
      put_u32(entry.data, static_cast<std::uint32_t>(*numerator));
      put_u32(entry.data, static_cast<std::uint32_t>(*denominator));
    }
    entry.count = static_cast<std::uint32_t>(parts.size());
    return entry;
  }
  std::vector<std::int64_t> numbers;
  for (const auto part : parts) {
    const auto number = integer(part);
    if (!number) {
      return std::nullopt;  // text where a number belongs: the type is unknown
    }
    numbers.push_back(*number);
  }
  const auto [lowest, highest] = std::minmax_element(numbers.begin(), numbers.end());
  if (is_byte_tag(kind, tag)) {
    if (*lowest < 0 || *highest > 255) {
      return std::nullopt;
    }
    entry.type = static_cast<std::uint16_t>(TiffType::Byte);
    for (const auto number : numbers) {
      entry.data.push_back(static_cast<std::uint8_t>(number));
    }
  } else if (*lowest < 0) {
    if (*lowest < INT32_MIN || *highest > INT32_MAX) {
      return std::nullopt;
    }
    entry.type = static_cast<std::uint16_t>(TiffType::SLong);
    for (const auto number : numbers) {
      put_u32(entry.data, static_cast<std::uint32_t>(number));
    }
  } else if (is_long_tag(kind, tag) || *highest > 0xFFFF) {
    if (*highest > std::int64_t{UINT32_MAX}) {
      return std::nullopt;
    }
    entry.type = static_cast<std::uint16_t>(TiffType::Long);
    for (const auto number : numbers) {
      put_u32(entry.data, static_cast<std::uint32_t>(number));
    }
  } else {
    entry.type = static_cast<std::uint16_t>(TiffType::Short);
    for (const auto number : numbers) {
      put_u16(entry.data, static_cast<std::uint16_t>(number));
    }
  }
  entry.count = static_cast<std::uint32_t>(numbers.size());
  return entry;
}

class ExifBuilder {
public:
  // `key` without its "Exif." prefix: "Image.Artist", "GPSInfo.GPSLatitude".
  void add(std::string_view key, const std::string& value) {
    const auto dot = key.find('.');
    if (dot == std::string_view::npos) {
      return;
    }
    const auto kind = exif_group(key.substr(0, dot));
    if (!kind) {
      return;
    }
    const auto tag = exif_tag_by_name(*kind, key.substr(dot + 1));
    if (!tag || skipped_exif_tag(*tag)) {
      return;
    }
    auto entry = exif_entry(*kind, *tag, value);
    if (!entry) {
      return;
    }
    auto& ifd = stream_.find_or_add(*kind);
    ifd.erase(*tag);
    ifd.entries.push_back(std::move(*entry));
    any_ = true;
  }

  [[nodiscard]] std::vector<std::uint8_t> finish() {
    if (!any_) {
      return {};
    }
    (void)stream_.find_or_add(IfdKind::Ifd0);
    return write_tiff(stream_);
  }

private:
  TiffStream stream_ = [] {
    TiffStream stream;
    stream.big_endian = true;
    return stream;
  }();
  bool any_{false};
};

// --- IPTC -----------------------------------------------------------------------------

struct IimName {
  std::string_view name;
  std::uint8_t dataset;
};

// exiv2's Iptc.Application2 names (record 2). RecordVersion is written by write_iim, and
// the binary datasets (rasterized caption, previews) are left out.
constexpr IimName kIimNames[] = {
    {"ObjectType", 3},          {"ObjectAttribute", 4},   {"ObjectName", 5},
    {"EditStatus", 7},          {"EditorialUpdate", 8},   {"Urgency", 10},
    {"Subject", 12},            {"Category", 15},         {"SuppCategory", 20},
    {"FixtureId", 22},          {"Keywords", 25},         {"LocationCode", 26},
    {"LocationName", 27},       {"ReleaseDate", 30},      {"ReleaseTime", 35},
    {"ExpirationDate", 37},     {"ExpirationTime", 38},   {"SpecialInstructions", 40},
    {"ActionAdvised", 42},      {"ReferenceService", 45}, {"ReferenceDate", 47},
    {"ReferenceNumber", 50},    {"DateCreated", 55},      {"TimeCreated", 60},
    {"DigitizationDate", 62},   {"DigitizationTime", 63}, {"Program", 65},
    {"ProgramVersion", 70},     {"ObjectCycle", 75},      {"Byline", 80},
    {"BylineTitle", 85},        {"City", 90},             {"SubLocation", 92},
    {"ProvinceState", 95},      {"CountryCode", 100},     {"CountryName", 101},
    {"TransmissionReference", 103}, {"Headline", 105},    {"Credit", 110},
    {"Source", 115},            {"Copyright", 116},       {"Contact", 118},
    {"Caption", 120},           {"Writer", 122},          {"ImageType", 130},
    {"ImageOrientation", 131},  {"Language", 135},        {"AudioType", 150},
    {"AudioRate", 151},         {"AudioResolution", 152}, {"AudioDuration", 153},
    {"AudioOutcue", 154},
};

[[nodiscard]] bool is_iim_date(std::uint8_t dataset) {
  return dataset == 30 || dataset == 37 || dataset == 47 || dataset == 55 || dataset == 62;
}

[[nodiscard]] bool is_iim_time(std::uint8_t dataset) {
  return dataset == 35 || dataset == 38 || dataset == 60 || dataset == 63;
}

class IptcBuilder {
public:
  // `key` without its "Iptc." prefix: "Application2.City".
  void add(std::string_view key, std::string value) {
    constexpr std::string_view kRecord2 = "Application2.";
    if (!key.starts_with(kRecord2) || value.empty() || value.size() > kMaxIimValue) {
      return;
    }
    const auto name = key.substr(kRecord2.size());
    for (const auto& entry : kIimNames) {
      if (entry.name != name) {
        continue;
      }
      // exiv2 shows dates as 2026-10-26 and times as 13:12:12+03:00; IIM stores
      // CCYYMMDD and HHMMSS+HHMM.
      if (is_iim_date(entry.dataset)) {
        std::erase(value, '-');
      } else if (is_iim_time(entry.dataset)) {
        std::erase(value, ':');
      }
      for (auto& [dataset, values] : datasets_) {
        if (dataset == entry.dataset) {
          values.push_back(std::move(value));
          return;
        }
      }
      datasets_.push_back({entry.dataset, {std::move(value)}});
      return;
    }
  }

  [[nodiscard]] std::vector<std::uint8_t> finish() const {
    if (datasets_.empty()) {
      return {};
    }
    IimBlock block;
    for (const auto& [dataset, values] : datasets_) {
      block.set_values(2, dataset, values);
    }
    return write_iim(block);
  }

private:
  std::vector<std::pair<std::uint8_t, std::vector<std::string>>> datasets_;
};

}  // namespace

GimpImageMetadata parse_gimp_image_metadata(std::span<const std::uint8_t> bytes) {
  GimpImageMetadata out;
  while (!bytes.empty() && bytes.back() == 0) {
    bytes = bytes.first(bytes.size() - 1);
  }
  if (bytes.empty()) {
    return out;
  }
  svg::XmlNode root;
  try {
    root = svg::parse_xml(bytes);
  } catch (const std::exception&) {
    return out;
  }
  if (root.name != "metadata") {
    return out;
  }
  XmpBuilder xmp;
  ExifBuilder exif;
  IptcBuilder iptc;
  // Namespaces first: a file may list one after a tag that uses it.
  for (const auto& child : root.children) {
    if (child.name == "namespace") {
      const auto* prefix = child.attribute("prefix");
      const auto* url = child.attribute("url");
      if (prefix != nullptr && url != nullptr) {
        xmp.add_namespace(*prefix, *url);
      }
    }
  }
  std::size_t count = 0;
  for (const auto& child : root.children) {
    if (child.name != "tag") {
      continue;
    }
    if (++count > kMaxTags) {
      break;
    }
    const auto* name_attribute = child.attribute("name");
    if (name_attribute == nullptr) {
      continue;
    }
    const std::string_view name = *name_attribute;
    auto value = child.all_text();
    if (const auto* encoding = child.attribute("encoding"); encoding != nullptr && *encoding == "base64") {
      const auto decoded = decode_base64(value);
      value.assign(decoded.begin(), decoded.end());
    }
    if (name.starts_with("Xmp.")) {
      xmp.add(name.substr(4), value);
    } else if (name.starts_with("Exif.")) {
      exif.add(name.substr(5), value);
    } else if (name.starts_with("Iptc.")) {
      iptc.add(name.substr(5), std::move(value));
    }
  }
  out.xmp = xmp.finish();
  out.exif = exif.finish();
  out.iptc = iptc.finish();
  return out;
}

std::size_t add_missing_xmp_properties(XmpPacket& into, const XmpPacket& from) {
  std::size_t added = 0;
  for (const auto& property : from.properties) {
    if (into.find(property.ns, property.name) != nullptr) {
      continue;
    }
    into.properties.push_back(property);
    ++added;
  }
  for (const auto& [ns, prefix] : from.prefixes) {
    into.prefixes.emplace(ns, prefix);
  }
  return added;
}

}  // namespace patchy::metadata
