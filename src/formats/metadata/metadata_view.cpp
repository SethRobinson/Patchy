#include "formats/metadata/metadata_view.hpp"

#include "formats/metadata/exif_tags.hpp"
#include "formats/metadata/iptc_iim.hpp"
#include "formats/metadata/jumbf.hpp"
#include "formats/metadata/tiff_ifd.hpp"
#include "formats/metadata/xmp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace patchy::metadata {
namespace {

constexpr std::size_t kMaxValueChars = 65536;

using Span = std::span<const std::uint8_t>;

std::uint16_t be16(Span data, std::size_t at) {
  return static_cast<std::uint16_t>((data[at] << 8) | data[at + 1]);
}
std::uint32_t be32(Span data, std::size_t at) {
  return (static_cast<std::uint32_t>(data[at]) << 24) | (static_cast<std::uint32_t>(data[at + 1]) << 16) |
         (static_cast<std::uint32_t>(data[at + 2]) << 8) | data[at + 3];
}

std::string clip(std::string value) {
  if (value.size() > kMaxValueChars) {
    value.resize(kMaxValueChars);
    value += "...";
  }
  return value;
}

std::string bytes_text(std::size_t size) {
  return "(" + std::to_string(size) + " bytes)";
}

std::string number_text(double value, int decimals) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  std::string text = buffer;
  if (text.find('.') != std::string::npos) {
    while (!text.empty() && text.back() == '0') {
      text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
      text.pop_back();
    }
  }
  return text;
}

std::string signature_text(Span data, std::size_t at) {
  std::string text;
  for (std::size_t i = 0; i < 4; ++i) {
    const auto c = data[at + i];
    if (c >= 0x20 && c < 0x7F) {
      text.push_back(static_cast<char>(c));
    }
  }
  while (!text.empty() && text.back() == ' ') {
    text.pop_back();
  }
  return text;
}

std::string xmp_prefix(const XmpPacket& packet, const std::string& ns) {
  if (const auto it = packet.prefixes.find(ns); it != packet.prefixes.end() && !it->second.empty()) {
    return it->second;
  }
  const auto known = well_known_xmp_prefix(ns);
  return known.empty() ? ns : std::string(known);
}

// ICC text from a 'desc' (v2 textDescriptionType), 'mluc' or 'text' tag element.
std::string icc_text_tag(Span profile, std::uint32_t offset, std::uint32_t size) {
  if (offset > profile.size() || size > profile.size() - offset || size < 12) {
    return {};
  }
  const auto tag = profile.subspan(offset, size);
  const auto type = signature_text(tag, 0);
  if (type == "desc") {
    const auto count = be32(tag, 8);
    if (count == 0 || 12 + static_cast<std::uint64_t>(count) > tag.size()) {
      return {};
    }
    std::string text(reinterpret_cast<const char*>(tag.data() + 12), count);
    while (!text.empty() && text.back() == '\0') {
      text.pop_back();
    }
    return legacy_text_to_utf8(Span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
  }
  if (type == "text") {
    std::string text(reinterpret_cast<const char*>(tag.data() + 8), tag.size() - 8);
    while (!text.empty() && text.back() == '\0') {
      text.pop_back();
    }
    return legacy_text_to_utf8(Span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
  }
  if (type == "mluc" && tag.size() >= 16) {
    const auto records = be32(tag, 8);
    const auto record_size = be32(tag, 12);
    std::size_t chosen = 0;
    bool found = false;
    for (std::uint32_t i = 0; i < records && i < 256; ++i) {
      const std::size_t at = 16 + static_cast<std::size_t>(i) * record_size;
      if (record_size < 12 || at + 12 > tag.size()) {
        break;
      }
      if (!found || (tag[at] == 'e' && tag[at + 1] == 'n')) {
        chosen = at;
        found = true;
        if (tag[at] == 'e' && tag[at + 1] == 'n') {
          break;
        }
      }
    }
    if (!found) {
      return {};
    }
    const auto length = be32(tag, chosen + 4);
    const auto string_offset = be32(tag, chosen + 8);
    if (string_offset > tag.size() || length > tag.size() - string_offset) {
      return {};
    }
    std::string out;
    for (std::size_t i = 0; i + 1 < length; i += 2) {
      const std::uint32_t unit = be16(tag, string_offset + i);
      if (unit == 0) {
        break;
      }
      if (unit < 0x80U) {
        out.push_back(static_cast<char>(unit));
      } else if (unit < 0x800U) {
        out.push_back(static_cast<char>(0xC0U | (unit >> 6)));
        out.push_back(static_cast<char>(0x80U | (unit & 0x3FU)));
      } else {
        out.push_back(static_cast<char>(0xE0U | (unit >> 12)));
        out.push_back(static_cast<char>(0x80U | ((unit >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (unit & 0x3FU)));
      }
    }
    return out;
  }
  return {};
}

std::string png_chunk_value(const FileInfoChunk& chunk) {
  const std::string type(chunk.type.begin(), chunk.type.end());
  const Span data = chunk.data;
  if (type == "sRGB" && data.size() == 1) {
    static constexpr std::array<std::string_view, 4> kIntents{"Perceptual", "Relative colorimetric", "Saturation",
                                                              "Absolute colorimetric"};
    return data[0] < kIntents.size() ? std::string(kIntents[data[0]]) : std::to_string(data[0]);
  }
  if (type == "gAMA" && data.size() == 4) {
    return number_text(be32(data, 0) / 100000.0, 5);
  }
  if (type == "cHRM" && data.size() == 32) {
    std::string text;
    static constexpr std::array<std::string_view, 4> kNames{"white", "red", "green", "blue"};
    for (std::size_t i = 0; i < 4; ++i) {
      text += (i > 0 ? ", " : "") + std::string(kNames[i]) + " " + number_text(be32(data, i * 8) / 100000.0, 4) +
              "," + number_text(be32(data, i * 8 + 4) / 100000.0, 4);
    }
    return text;
  }
  if (type == "cICP" && data.size() == 4) {
    return "primaries " + std::to_string(data[0]) + ", transfer " + std::to_string(data[1]) + ", matrix " +
           std::to_string(data[2]) + (data[3] != 0 ? ", full range" : ", limited range");
  }
  if (type == "cLLI" && data.size() == 8) {
    return "MaxCLL " + number_text(be32(data, 0) / 10000.0, 4) + " cd/m2, MaxFALL " +
           number_text(be32(data, 4) / 10000.0, 4) + " cd/m2";
  }
  if (type == "sBIT") {
    std::string text;
    for (const auto bits : data) {
      text += (text.empty() ? "" : " ") + std::to_string(bits);
    }
    return text + " significant bits";
  }
  return bytes_text(data.size());
}

std::string text_group(FileInfoTextKind kind) {
  switch (kind) {
    case FileInfoTextKind::PngText:
    case FileInfoTextKind::PngCompressedText:
    case FileInfoTextKind::PngUnicodeText:
      return "PNG text";
    case FileInfoTextKind::Comment:
      return "Comment";
    case FileInfoTextKind::PdfInfo:
      return "PDF Info";
    case FileInfoTextKind::SvgText:
      return "SVG";
  }
  return "Text";
}

std::string text_source(FileInfoTextKind kind) {
  switch (kind) {
    case FileInfoTextKind::PngText:
      return "tEXt";
    case FileInfoTextKind::PngCompressedText:
      return "zTXt";
    case FileInfoTextKind::PngUnicodeText:
      return "iTXt";
    case FileInfoTextKind::Comment:
      return "Comment";
    case FileInfoTextKind::PdfInfo:
      return "/Info";
    case FileInfoTextKind::SvgText:
      return "SVG element";
  }
  return {};
}

PrivacyClass text_privacy(const FileInfoText& text) {
  if (is_ai_generation_text_key(text.key)) {
    return PrivacyClass::AiPrompt;
  }
  const auto& key = text.key;
  if (key == "Author" || key == "Copyright") {
    return PrivacyClass::Personal;
  }
  if (key == "Software" || key == "Creator" || key == "Producer" || key == "Source") {
    return PrivacyClass::MachineInfo;
  }
  if (key == "Creation Time" || key == "CreationDate" || key == "ModDate" || key == "date:create" ||
      key == "date:modify" || key == "date:timestamp") {
    return PrivacyClass::Timestamp;
  }
  return PrivacyClass::None;
}

// XMP rational "28/10" -> 2.8.
std::optional<double> xmp_rational(const std::string& text) {
  if (text.empty()) {
    return std::nullopt;
  }
  const auto slash = text.find('/');
  char* end = nullptr;
  if (slash == std::string::npos) {
    const double value = std::strtod(text.c_str(), &end);
    return end != text.c_str() ? std::optional<double>(value) : std::nullopt;
  }
  const double numerator = std::strtod(text.substr(0, slash).c_str(), nullptr);
  const double denominator = std::strtod(text.substr(slash + 1).c_str(), nullptr);
  if (denominator == 0.0) {
    return std::nullopt;
  }
  return numerator / denominator;
}

// XMP GPS coordinate: "DDD,MM.mmmK" or "DDD,MM,SSK" (K = N/S/E/W).
std::optional<double> xmp_gps(const std::string& text) {
  if (text.size() < 3) {
    return std::nullopt;
  }
  const char direction = text.back();
  std::string body = text.substr(0, text.size() - 1);
  double parts[3] = {0.0, 0.0, 0.0};
  int index = 0;
  std::size_t start = 0;
  while (index < 3) {
    const auto comma = body.find(',', start);
    parts[index++] = std::strtod(body.substr(start, comma - start).c_str(), nullptr);
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  double value = parts[0] + parts[1] / 60.0 + parts[2] / 3600.0;
  if (direction == 'S' || direction == 'W') {
    value = -value;
  }
  return value;
}

std::string exposure_from_seconds(double seconds) {
  if (seconds > 0.0 && seconds < 0.25) {
    return "1/" + number_text(std::round(1.0 / seconds), 0) + " s";
  }
  return number_text(seconds, 2) + " s";
}

// Value of `"field": "..."` in JSON text (first occurrence), with escapes decoded.
std::string json_string_field(std::string_view json, std::string_view field) {
  const std::string needle = "\"" + std::string(field) + "\"";
  auto at = json.find(needle);
  if (at == std::string_view::npos) {
    return {};
  }
  at += needle.size();
  while (at < json.size() && (json[at] == ' ' || json[at] == ':' || json[at] == '\n' || json[at] == '\t')) {
    ++at;
  }
  if (at >= json.size() || json[at] != '"') {
    return {};
  }
  std::string out;
  for (++at; at < json.size() && json[at] != '"'; ++at) {
    if (json[at] == '\\' && at + 1 < json.size()) {
      ++at;
      switch (json[at]) {
        case 'n':
          out.push_back('\n');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u':
          if (at + 4 < json.size()) {
            const auto code = std::strtoul(std::string(json.substr(at + 1, 4)).c_str(), nullptr, 16);
            if (code < 0x80) {
              out.push_back(static_cast<char>(code));
            } else if (code < 0x800) {
              out.push_back(static_cast<char>(0xC0 | (code >> 6)));
              out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
              out.push_back(static_cast<char>(0xE0 | (code >> 12)));
              out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            at += 4;
          }
          break;
        default:
          out.push_back(json[at]);
          break;
      }
    } else {
      out.push_back(json[at]);
    }
  }
  return out;
}

// Automatic1111 / Forge "parameters": prompt, optional "Negative prompt:" block,
// then a final "Steps: ..." settings line.
void split_a1111_parameters(const std::string& text, AiGenerationSummary& summary) {
  const auto settings_at = text.rfind("\nSteps: ");
  std::string head = settings_at == std::string::npos ? text : text.substr(0, settings_at);
  if (settings_at != std::string::npos) {
    summary.settings = text.substr(settings_at + 1);
  } else if (text.starts_with("Steps: ")) {
    summary.settings = text;
    head.clear();
  }
  const auto negative_at = head.find("Negative prompt:");
  if (negative_at != std::string::npos) {
    summary.negative_prompt = head.substr(negative_at + 16);
    while (!summary.negative_prompt.empty() && summary.negative_prompt.front() == ' ') {
      summary.negative_prompt.erase(0, 1);
    }
    head = head.substr(0, negative_at);
  }
  while (!head.empty() && (head.back() == '\n' || head.back() == ' ')) {
    head.pop_back();
  }
  summary.prompt = head;
}

}  // namespace

std::vector<std::uint8_t> decode_base64(std::string_view text) {
  std::vector<std::uint8_t> out;
  std::uint32_t buffer = 0;
  int bits = 0;
  for (const char c : text) {
    int value = -1;
    if (c >= 'A' && c <= 'Z') {
      value = c - 'A';
    } else if (c >= 'a' && c <= 'z') {
      value = c - 'a' + 26;
    } else if (c >= '0' && c <= '9') {
      value = c - '0' + 52;
    } else if (c == '+' || c == '-') {
      value = 62;
    } else if (c == '/' || c == '_') {
      value = 63;
    } else if (c == '=') {
      break;
    }
    if (value < 0) {
      continue;
    }
    buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFFU));
    }
  }
  return out;
}

std::string xmp_display_text(std::span<const std::uint8_t> packet) {
  std::string text(packet.begin(), packet.end());
  const auto trim_end = [](std::string& value) {
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' ||
                              value.back() == '\n' || value.back() == '\0')) {
      value.pop_back();
    }
  };
  trim_end(text);
  const auto trailer_at = text.rfind("<?xpacket");
  if (trailer_at == std::string::npos || trailer_at == 0U) {
    return text;
  }
  auto trailer = text.substr(trailer_at);
  if (trailer.find("end=") == std::string::npos || trailer.find('>') != trailer.size() - 1U ||
      trailer.compare(trailer.size() - 2U, 2U, "?>") != 0) {
    return text;
  }
  text.erase(trailer_at);
  trim_end(text);
  return text.empty() ? trailer : text + "\n" + trailer;
}

bool is_ai_generation_text_key(std::string_view key) noexcept {
  return key == "parameters" || key == "prompt" || key == "workflow" || key == "invokeai_metadata" ||
         key == "invokeai_graph" || key == "sd-metadata" || key == "Dream" || key == "generation_data" ||
         key == "fooocus_scheme" || key == "SDXL" || key == "aesthetic_score" || key == "negative_prompt";
}

std::optional<IccSummary> summarize_icc(std::span<const std::uint8_t> profile) {
  if (profile.size() < 132 || signature_text(profile, 36) != "acsp") {
    return std::nullopt;
  }
  IccSummary summary;
  summary.size = profile.size();
  summary.version = std::to_string(profile[8]) + "." + std::to_string(profile[9] >> 4);
  if ((profile[9] & 0x0F) != 0) {
    summary.version += "." + std::to_string(profile[9] & 0x0F);
  }
  const auto device_class = signature_text(profile, 12);
  if (device_class == "mntr") {
    summary.device_class = "Display";
  } else if (device_class == "prtr") {
    summary.device_class = "Output";
  } else if (device_class == "scnr") {
    summary.device_class = "Input";
  } else if (device_class == "spac") {
    summary.device_class = "Color space";
  } else if (device_class == "link") {
    summary.device_class = "Device link";
  } else if (device_class == "abst") {
    summary.device_class = "Abstract";
  } else if (device_class == "nmcl") {
    summary.device_class = "Named color";
  } else {
    summary.device_class = device_class;
  }
  summary.color_space = signature_text(profile, 16);
  summary.connection_space = signature_text(profile, 20);
  char date[48];
  std::snprintf(date, sizeof(date), "%04u-%02u-%02u %02u:%02u:%02u", static_cast<unsigned>(be16(profile, 24)),
                static_cast<unsigned>(be16(profile, 26)), static_cast<unsigned>(be16(profile, 28)),
                static_cast<unsigned>(be16(profile, 30)), static_cast<unsigned>(be16(profile, 32)),
                static_cast<unsigned>(be16(profile, 34)));
  summary.created = date;
  summary.manufacturer = signature_text(profile, 48);
  summary.model = signature_text(profile, 52);
  summary.creator = signature_text(profile, 80);
  static constexpr std::array<std::string_view, 4> kIntents{"Perceptual", "Relative colorimetric", "Saturation",
                                                            "Absolute colorimetric"};
  const auto intent = be32(profile, 64);
  summary.rendering_intent = intent < kIntents.size() ? std::string(kIntents[intent]) : std::to_string(intent);
  const auto count = be32(profile, 128);
  for (std::uint32_t i = 0; i < count && i < 512; ++i) {
    const std::size_t at = 132 + static_cast<std::size_t>(i) * 12;
    if (at + 12 > profile.size()) {
      break;
    }
    const auto signature = signature_text(profile, at);
    const auto offset = be32(profile, at + 4);
    const auto size = be32(profile, at + 8);
    if (signature == "desc") {
      summary.description = icc_text_tag(profile, offset, size);
    } else if (signature == "cprt") {
      summary.copyright = icc_text_tag(profile, offset, size);
    } else if (signature == "dmnd") {
      if (auto text = icc_text_tag(profile, offset, size); !text.empty()) {
        summary.manufacturer = std::move(text);
      }
    } else if (signature == "dmdd") {
      if (auto text = icc_text_tag(profile, offset, size); !text.empty()) {
        summary.model = std::move(text);
      }
    }
  }
  return summary;
}

std::vector<MetadataEntry> describe_file_info(const DocumentFileInfo& info,
                                              std::span<const std::uint8_t> kept_icc_profile) {
  std::vector<MetadataEntry> rows;
  const auto add = [&](std::string group, std::string name, std::string value, std::string source,
                       PrivacyClass privacy) {
    rows.push_back(MetadataEntry{std::move(group), std::move(name), clip(std::move(value)), std::move(source), privacy});
  };

  if (info.exif) {
    if (const auto stream = parse_tiff(*info.exif)) {
      for (const auto kind : {IfdKind::Ifd0, IfdKind::Exif, IfdKind::Gps, IfdKind::Interop, IfdKind::Ifd1}) {
        const auto* ifd = stream->find(kind);
        if (ifd == nullptr) {
          continue;
        }
        const auto group = "EXIF " + std::string(ifd_kind_label(kind));
        for (const auto& entry : ifd->entries) {
          if ((kind == IfdKind::Ifd0 && (entry.tag == kTagExifIfd || entry.tag == kTagGpsIfd)) ||
              (kind == IfdKind::Exif && entry.tag == kTagInteropIfd)) {
            continue;
          }
          add(group, exif_tag_display_name(kind, entry.tag), format_exif_value(*stream, kind, entry), "EXIF",
              exif_tag_privacy(kind, entry.tag));
        }
      }
      if (!stream->thumbnail.empty()) {
        add("EXIF IFD1 (thumbnail)", "ThumbnailImage", bytes_text(stream->thumbnail.size()), "EXIF",
            PrivacyClass::HiddenImage);
      }
    } else {
      add("EXIF", "Unreadable EXIF block", bytes_text(info.exif->size()), "EXIF", PrivacyClass::None);
    }
  }

  if (info.xmp) {
    if (const auto packet = parse_xmp(*info.xmp)) {
      for (const auto& row : flatten_xmp(*packet)) {
        add("XMP " + xmp_prefix(*packet, row.ns), row.path, row.value, "XMP", row.privacy);
      }
    } else {
      add("XMP", "Unreadable XMP packet", bytes_text(info.xmp->size()), "XMP", PrivacyClass::None);
    }
  }

  if (info.iptc) {
    if (const auto block = parse_iim(*info.iptc)) {
      for (const auto& dataset : block->datasets) {
        std::string value;
        if (dataset.record == 1 && dataset.dataset == 90) {
          value = block->utf8_marker ? "UTF-8" : bytes_text(dataset.data.size());
        } else if (dataset.record == 2 && dataset.dataset == 0 && dataset.data.size() == 2) {
          value = std::to_string(be16(dataset.data, 0));
        } else if (dataset.record == 2 && dataset.dataset == 202) {
          value = bytes_text(dataset.data.size());
        } else {
          value = iim_display_text(dataset.record, dataset.dataset, iim_text(*block, dataset));
        }
        add("IPTC-IIM", iim_dataset_name(dataset.record, dataset.dataset), std::move(value), "IPTC",
            iim_dataset_privacy(dataset.record, dataset.dataset));
      }
      if (info.iptc_digest) {
        const auto digest = iim_digest(*info.iptc);
        add("IPTC-IIM", "CaptionDigest",
            digest == *info.iptc_digest ? "matches (XMP is authoritative)"
                                        : "does not match (the IPTC block was edited by another program)",
            "8BIM 1061", PrivacyClass::None);
      }
    }
  }

  if (info.copyright_flag) {
    add("Photoshop", "CopyrightFlag", *info.copyright_flag ? "Copyrighted" : "Not marked", "8BIM 1034",
        PrivacyClass::None);
  }
  if (!info.copyright_url.empty()) {
    add("Photoshop", "CopyrightURL", info.copyright_url, "8BIM 1035", PrivacyClass::None);
  }
  for (const auto& [id, bytes] : info.psd_resources) {
    std::string value = bytes ? bytes_text(bytes->size()) : std::string{};
    if (id == psd_resource::kAutoSaveFilePath && bytes) {
      // Unicode string: u32 length, then UTF-16BE.
      if (bytes->size() >= 4) {
        const Span data = *bytes;
        const auto length = be32(data, 0);
        std::string path;
        for (std::size_t i = 0; i < length && 4 + i * 2 + 1 < data.size(); ++i) {
          const auto unit = be16(data, 4 + i * 2);
          if (unit < 0x80) {
            path.push_back(static_cast<char>(unit));
          } else {
            path.push_back('?');
          }
        }
        value = path;
      }
    }
    add("Photoshop", std::to_string(id) + " " + psd_resource_name(id), std::move(value),
        "8BIM " + std::to_string(id), psd_resource_privacy(id));
  }

  const Span icc = !kept_icc_profile.empty() ? kept_icc_profile
                                             : (info.source_icc ? Span(*info.source_icc) : Span{});
  if (const auto summary = summarize_icc(icc)) {
    const std::string group = "ICC profile";
    const std::string source = !kept_icc_profile.empty() ? "embedded" : "source file";
    add(group, "Description", summary->description, source, PrivacyClass::None);
    if (!summary->copyright.empty()) {
      add(group, "Copyright", summary->copyright, source, PrivacyClass::None);
    }
    add(group, "Manufacturer", summary->manufacturer, source, PrivacyClass::None);
    add(group, "Model", summary->model, source, PrivacyClass::None);
    add(group, "Version", summary->version, source, PrivacyClass::None);
    add(group, "Class", summary->device_class, source, PrivacyClass::None);
    add(group, "ColorSpace", summary->color_space, source, PrivacyClass::None);
    add(group, "ConnectionSpace", summary->connection_space, source, PrivacyClass::None);
    add(group, "Creator", summary->creator, source, PrivacyClass::None);
    add(group, "Created", summary->created, source, PrivacyClass::None);
    add(group, "RenderingIntent", summary->rendering_intent, source, PrivacyClass::None);
    add(group, "Size", bytes_text(summary->size), source, PrivacyClass::None);
  }
  if (!info.source_icc_note.empty()) {
    add("ICC profile", "Note", info.source_icc_note, "source file", PrivacyClass::None);
  }

  for (const auto& chunk : info.png_color_chunks) {
    add("PNG color", std::string(chunk.type.begin(), chunk.type.end()), png_chunk_value(chunk), "PNG chunk",
        PrivacyClass::None);
  }

  for (const auto& text : info.text) {
    add(text_group(text.kind), text.key.empty() ? std::string("Comment") : text.key, text.value,
        text_source(text.kind) + (text.language.empty() ? "" : " [" + text.language + "]"), text_privacy(text));
  }

  for (const auto& item : info.observed) {
    std::string value = item.summary;
    if (value.empty() && item.size > 0) {
      value = bytes_text(static_cast<std::size_t>(item.size));
    }
    add(item.container, item.label, std::move(value), item.container, item.privacy);
  }
  return rows;
}

CameraSummary summarize_camera(const DocumentFileInfo& info) {
  CameraSummary summary;
  std::optional<TiffStream> stream;
  if (info.exif) {
    stream = parse_tiff(*info.exif);
  }
  const auto exif_value = [&](IfdKind kind, std::uint16_t tag) -> std::string {
    if (!stream) {
      return {};
    }
    const auto* ifd = stream->find(kind);
    if (ifd == nullptr && kind == IfdKind::Exif) {
      ifd = stream->find(IfdKind::Ifd0);  // raw files keep Exif tags in IFD0
    }
    if (ifd == nullptr) {
      return {};
    }
    const auto* entry = ifd->find(tag);
    return entry != nullptr ? format_exif_value(*stream, kind, *entry) : std::string{};
  };
  std::optional<XmpPacket> packet;
  if (info.xmp) {
    packet = parse_xmp(*info.xmp);
  }
  const auto xmp_text = [&](std::string_view ns, std::string_view name) -> std::string {
    return packet ? packet->text(ns, name) : std::string{};
  };
  const auto pick = [](std::string first, std::string second) { return first.empty() ? second : first; };

  summary.make = pick(exif_value(IfdKind::Ifd0, exif_tag::kMake), xmp_text(xmp_ns::kTiff, "Make"));
  summary.model = pick(exif_value(IfdKind::Ifd0, exif_tag::kModel), xmp_text(xmp_ns::kTiff, "Model"));
  summary.lens = pick(exif_value(IfdKind::Exif, exif_tag::kLensModel),
                      pick(xmp_text(xmp_ns::kExifEx, "LensModel"), xmp_text(xmp_ns::kAux, "Lens")));
  if (summary.lens.empty()) {
    summary.lens = exif_value(IfdKind::Exif, exif_tag::kLensSpecification);
  }
  summary.body_serial = pick(exif_value(IfdKind::Exif, exif_tag::kBodySerialNumber),
                             pick(xmp_text(xmp_ns::kExifEx, "BodySerialNumber"), xmp_text(xmp_ns::kAux, "SerialNumber")));
  summary.lens_serial = pick(exif_value(IfdKind::Exif, exif_tag::kLensSerialNumber),
                             xmp_text(xmp_ns::kExifEx, "LensSerialNumber"));
  summary.owner = pick(exif_value(IfdKind::Exif, exif_tag::kCameraOwnerName),
                       pick(xmp_text(xmp_ns::kExifEx, "CameraOwnerName"), xmp_text(xmp_ns::kAux, "OwnerName")));
  summary.exposure_time = exif_value(IfdKind::Exif, exif_tag::kExposureTime);
  if (summary.exposure_time.empty()) {
    if (const auto value = xmp_rational(xmp_text(xmp_ns::kExif, "ExposureTime"))) {
      summary.exposure_time = exposure_from_seconds(*value);
    }
  }
  summary.f_number = exif_value(IfdKind::Exif, exif_tag::kFNumber);
  if (summary.f_number.empty()) {
    if (const auto value = xmp_rational(xmp_text(xmp_ns::kExif, "FNumber"))) {
      summary.f_number = "f/" + number_text(*value, 1);
    }
  }
  summary.iso = pick(exif_value(IfdKind::Exif, exif_tag::kIsoSpeed),
                     pick(xmp_text(xmp_ns::kExif, "ISOSpeedRatings"), xmp_text(xmp_ns::kExifEx, "PhotographicSensitivity")));
  summary.focal_length = exif_value(IfdKind::Exif, exif_tag::kFocalLength);
  if (summary.focal_length.empty()) {
    if (const auto value = xmp_rational(xmp_text(xmp_ns::kExif, "FocalLength"))) {
      summary.focal_length = number_text(*value, 1) + " mm";
    }
  }
  summary.focal_length_35mm = exif_value(IfdKind::Exif, exif_tag::kFocalLength35mm);
  summary.exposure_bias = exif_value(IfdKind::Exif, exif_tag::kExposureBias);
  summary.exposure_program = exif_value(IfdKind::Exif, exif_tag::kExposureProgram);
  summary.metering_mode = exif_value(IfdKind::Exif, exif_tag::kMeteringMode);
  summary.flash = exif_value(IfdKind::Exif, exif_tag::kFlash);
  summary.white_balance = exif_value(IfdKind::Exif, exif_tag::kWhiteBalance);
  summary.date_original = pick(exif_value(IfdKind::Exif, exif_tag::kDateTimeOriginal),
                               xmp_text(xmp_ns::kExif, "DateTimeOriginal"));
  summary.date_digitized = exif_value(IfdKind::Exif, exif_tag::kDateTimeDigitized);
  summary.offset_time = exif_value(IfdKind::Exif, exif_tag::kOffsetTimeOriginal);
  summary.orientation = exif_value(IfdKind::Ifd0, exif_tag::kOrientation);
  summary.software = pick(exif_value(IfdKind::Ifd0, exif_tag::kSoftware), xmp_text(xmp_ns::kXmp, "CreatorTool"));
  if (stream) {
    summary.latitude = gps_latitude(*stream);
    summary.longitude = gps_longitude(*stream);
    summary.altitude = gps_altitude(*stream);
    summary.gps_direction = exif_value(IfdKind::Gps, exif_tag::kGpsImgDirection);
    summary.gps_timestamp = exif_value(IfdKind::Gps, 7);
    const auto date = exif_value(IfdKind::Gps, 29);
    if (!date.empty()) {
      summary.gps_timestamp = date + " " + summary.gps_timestamp;
    }
    if (const auto* exif_ifd = stream->find(IfdKind::Exif)) {
      summary.has_maker_note = exif_ifd->find(exif_tag::kMakerNote) != nullptr;
    }
  }
  if (!summary.latitude && packet) {
    summary.latitude = xmp_gps(xmp_text(xmp_ns::kExif, "GPSLatitude"));
    summary.longitude = xmp_gps(xmp_text(xmp_ns::kExif, "GPSLongitude"));
    if (const auto altitude = xmp_rational(xmp_text(xmp_ns::kExif, "GPSAltitude"))) {
      summary.altitude = xmp_text(xmp_ns::kExif, "GPSAltitudeRef") == "1" ? -*altitude : *altitude;
    }
  }
  return summary;
}

EditHistorySummary summarize_edit_history(const DocumentFileInfo& info) {
  EditHistorySummary summary;
  if (!info.xmp) {
    return summary;
  }
  const auto packet = parse_xmp(*info.xmp);
  if (!packet) {
    return summary;
  }
  summary.document_id = packet->text(xmp_ns::kXmpMM, "DocumentID");
  summary.instance_id = packet->text(xmp_ns::kXmpMM, "InstanceID");
  summary.original_document_id = packet->text(xmp_ns::kXmpMM, "OriginalDocumentID");
  summary.derived_from = packet->struct_field(xmp_ns::kXmpMM, "DerivedFrom", xmp_ns::kStRef, "documentID");
  if (const auto* history = packet->find(xmp_ns::kXmpMM, "History")) {
    for (const auto& item : history->value.items) {
      HistoryEvent event;
      for (const auto& field : item.fields) {
        const auto& text = field.value.text;
        if (field.name == "action") {
          event.action = text;
        } else if (field.name == "when") {
          event.when = text;
        } else if (field.name == "softwareAgent") {
          event.software_agent = text;
        } else if (field.name == "changed") {
          event.changed = text;
        } else if (field.name == "instanceID") {
          event.instance_id = text;
        }
      }
      summary.events.push_back(std::move(event));
    }
  }
  if (const auto* ancestors = packet->find(xmp_ns::kPhotoshop, "DocumentAncestors")) {
    summary.ancestor_count = ancestors->value.items.size();
    for (const auto& item : ancestors->value.items) {
      summary.ancestor_bytes += item.text.size();
    }
  }
  if (const auto* layers = packet->find(xmp_ns::kPhotoshop, "TextLayers")) {
    for (const auto& item : layers->value.items) {
      std::string name;
      std::string text;
      for (const auto& field : item.fields) {
        if (field.name == "LayerName") {
          name = field.value.text;
        } else if (field.name == "LayerText") {
          text = field.value.text;
        }
      }
      summary.text_layers.emplace_back(std::move(name), std::move(text));
    }
  }
  summary.has_camera_raw_settings =
      std::any_of(packet->properties.begin(), packet->properties.end(),
                  [](const XmpProperty& property) { return property.ns == xmp_ns::kCrs; });
  return summary;
}

AiGenerationSummary summarize_ai_generation(const DocumentFileInfo& info) {
  AiGenerationSummary summary;
  bool novelai = false;
  for (const auto& text : info.text) {
    if (text.key == "Software" && text.value.find("NovelAI") != std::string::npos) {
      novelai = true;
    }
  }
  for (const auto& text : info.text) {
    const auto& key = text.key;
    const auto& value = text.value;
    if (key == "parameters") {
      summary.raw.emplace_back(key, value);
      if (!value.empty() && value.front() == '{') {
        summary.tool = "Fooocus";
        summary.prompt = json_string_field(value, "prompt");
        summary.negative_prompt = json_string_field(value, "negative_prompt");
        summary.workflow = value;
      } else {
        summary.tool = "Automatic1111 / Forge";
        split_a1111_parameters(value, summary);
      }
    } else if (key == "prompt" || key == "workflow") {
      summary.raw.emplace_back(key, value);
      summary.tool = "ComfyUI";
      if (key == "workflow" || summary.workflow.empty()) {
        summary.workflow = value;
      }
    } else if (key == "invokeai_metadata" || key == "sd-metadata" || key == "invokeai_graph") {
      summary.raw.emplace_back(key, value);
      summary.tool = "InvokeAI";
      if (summary.prompt.empty()) {
        summary.prompt = json_string_field(value, "positive_prompt");
        if (summary.prompt.empty()) {
          summary.prompt = json_string_field(value, "prompt");
        }
        summary.negative_prompt = json_string_field(value, "negative_prompt");
      }
      if (key != "invokeai_graph") {
        summary.workflow = value;
      }
    } else if (key == "Dream" || key == "generation_data" || key == "fooocus_scheme") {
      summary.raw.emplace_back(key, value);
      if (summary.prompt.empty()) {
        summary.prompt = value;
      }
    } else if (novelai && (key == "Description" || key == "Comment")) {
      summary.raw.emplace_back(key, value);
      summary.tool = "NovelAI";
      if (key == "Description") {
        summary.prompt = value;
      } else {
        summary.settings = value;
        summary.negative_prompt = json_string_field(value, "uc");
      }
    }
  }
  // Automatic1111 writes its parameters into EXIF UserComment for JPEG and WebP.
  if (summary.prompt.empty() && info.exif) {
    if (const auto stream = parse_tiff(*info.exif)) {
      if (const auto* exif = stream->find(IfdKind::Exif)) {
        if (const auto* comment = exif->find(exif_tag::kUserComment)) {
          const auto text = format_exif_value(*stream, IfdKind::Exif, *comment);
          if (text.find("Steps: ") != std::string::npos && text.find("Sampler") != std::string::npos) {
            summary.tool = "Automatic1111 / Forge";
            summary.raw.emplace_back("UserComment", text);
            split_a1111_parameters(text, summary);
          }
        }
      }
    }
  }
  if (info.xmp) {
    if (const auto packet = parse_xmp(*info.xmp)) {
      summary.digital_source_type = packet->text(xmp_ns::kIptcExt, "DigitalSourceType");
    }
  }
  for (const auto& item : info.observed) {
    if (item.privacy == PrivacyClass::Provenance && item.bytes) {
      const auto jumbf = summarize_jumbf(*item.bytes);
      if (summary.digital_source_type.empty()) {
        summary.digital_source_type = jumbf.digital_source_type;
      }
      if (summary.tool.empty() && !jumbf.digital_source_type.empty() &&
          jumbf.digital_source_type.find("trainedAlgorithmicMedia") != std::string::npos) {
        summary.tool = jumbf.claim_generator;
      }
    }
  }
  return summary;
}

std::vector<EmbeddedThumbnail> embedded_thumbnails(const DocumentFileInfo& info) {
  std::vector<EmbeddedThumbnail> thumbnails;
  if (info.exif) {
    if (const auto stream = parse_tiff(*info.exif); stream && !stream->thumbnail.empty()) {
      thumbnails.push_back(EmbeddedThumbnail{"EXIF IFD1", stream->thumbnail});
    }
  }
  for (const auto& [id, bytes] : info.psd_resources) {
    if ((id == psd_resource::kThumbnail || id == psd_resource::kThumbnailPs4) && bytes) {
      auto jpeg = psd_thumbnail_jpeg(*bytes);
      // Photoshop 4 thumbnails (1033) store BGR JPEG data; they decode with swapped
      // channels, which is still enough to see what the image showed.
      if (!jpeg.empty()) {
        thumbnails.push_back(EmbeddedThumbnail{"Photoshop resource " + std::to_string(id), std::move(jpeg)});
      }
    }
  }
  for (const auto& item : info.observed) {
    if (item.is_thumbnail && item.bytes && !item.bytes->empty()) {
      thumbnails.push_back(EmbeddedThumbnail{item.container + " " + item.label, *item.bytes});
    }
  }
  if (info.xmp) {
    if (const auto packet = parse_xmp(*info.xmp)) {
      if (const auto* property = packet->find(xmp_ns::kXmp, "Thumbnails")) {
        for (const auto& item : property->value.items) {
          for (const auto& field : item.fields) {
            if (field.ns == xmp_ns::kXmpGImg && field.name == "image") {
              auto bytes = decode_base64(field.value.text);
              if (!bytes.empty()) {
                thumbnails.push_back(EmbeddedThumbnail{"XMP xmp:Thumbnails", std::move(bytes)});
              }
            }
          }
        }
      }
    }
  }
  return thumbnails;
}

std::map<PrivacyClass, std::size_t> privacy_summary(const std::vector<MetadataEntry>& entries) {
  std::map<PrivacyClass, std::size_t> counts;
  for (const auto& entry : entries) {
    if (entry.privacy != PrivacyClass::None) {
      ++counts[entry.privacy];
    }
  }
  return counts;
}

}  // namespace patchy::metadata
