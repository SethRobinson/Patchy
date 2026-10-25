#include "formats/metadata/metadata_edit.hpp"

#include "formats/metadata/exif_tags.hpp"
#include "formats/metadata/iptc_iim.hpp"
#include "formats/metadata/tiff_ifd.hpp"
#include "formats/metadata/xmp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <tuple>

namespace patchy::metadata {

std::string xmp_date(std::int64_t unix_seconds, int utc_offset_minutes) {
  // Civil date from days since 1970-01-01 (Howard Hinnant's algorithm): no time zone
  // database and no locale, the same on every platform.
  const auto local = unix_seconds + static_cast<std::int64_t>(utc_offset_minutes) * 60;
  auto days = local / 86400;
  auto seconds = local % 86400;
  if (seconds < 0) {
    seconds += 86400;
    --days;
  }
  days += 719468;
  const auto era = (days >= 0 ? days : days - 146096) / 146097;
  const auto day_of_era = days - era * 146097;
  const auto year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
  const auto day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  const auto month_index = (5 * day_of_year + 2) / 153;
  const auto day = day_of_year - (153 * month_index + 2) / 5 + 1;
  const auto month = month_index < 10 ? month_index + 3 : month_index - 9;
  const auto year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);
  const auto offset = std::abs(static_cast<std::int64_t>(utc_offset_minutes));
  // Zero-padded fields appended by hand: snprintf into a fixed buffer draws GCC's
  // -Wformat-truncation for the 64-bit fields however large the buffer is.
  std::string text;
  const auto field = [&text](std::int64_t value, int width) {
    auto digits = std::to_string(value < 0 ? -value : value);
    if (value < 0) {
      text.push_back('-');
    }
    text.append(digits.size() < static_cast<std::size_t>(width) ? static_cast<std::size_t>(width) - digits.size() : 0U,
                '0');
    text += digits;
  };
  field(year, 4);
  text.push_back('-');
  field(month, 2);
  text.push_back('-');
  field(day, 2);
  text.push_back('T');
  field(seconds / 3600, 2);
  text.push_back(':');
  field((seconds / 60) % 60, 2);
  text.push_back(':');
  field(seconds % 60, 2);
  text.push_back(utc_offset_minutes < 0 ? '-' : '+');
  field(offset / 60, 2);
  text.push_back(':');
  field(offset % 60, 2);
  return text;
}

namespace {

using Bytes = std::vector<std::uint8_t>;

std::string join(const std::vector<std::string>& values) {
  std::string out;
  for (const auto& value : values) {
    if (!out.empty()) {
      out += "; ";
    }
    out += value;
  }
  return out;
}

Bytes text_bytes(const std::string& text) {
  return {text.begin(), text.end()};
}

XmpPacket packet_of(const DocumentFileInfo& info) {
  if (info.xmp) {
    if (auto packet = parse_xmp(*info.xmp)) {
      return std::move(*packet);
    }
  }
  return {};
}

void store_packet(DocumentFileInfo& info, const XmpPacket& packet) {
  if (packet.empty()) {
    info.xmp.reset();
    return;
  }
  info.xmp = make_metadata_bytes(text_bytes(serialize_xmp(packet)));
}

std::optional<TiffStream> exif_of(const DocumentFileInfo& info) {
  if (info.exif) {
    return parse_tiff(*info.exif);
  }
  return std::nullopt;
}

std::string exif_text(const std::optional<TiffStream>& stream, IfdKind kind, std::uint16_t tag) {
  if (!stream) {
    return {};
  }
  const auto* ifd = stream->find(kind);
  const auto* entry = ifd != nullptr ? ifd->find(tag) : nullptr;
  return entry != nullptr ? stream->string_value(*entry) : std::string{};
}

// "YYYY:MM:DD HH:MM:SS" (EXIF) to "YYYY-MM-DDTHH:MM:SS" (XMP).
std::string exif_date_to_xmp(std::string text) {
  if (text.size() >= 19 && text[4] == ':' && text[7] == ':') {
    text[4] = '-';
    text[7] = '-';
    text[10] = 'T';
  }
  return text;
}

// "YYYY-MM-DDTHH:MM:SS..." (XMP, local time) to "YYYY:MM:DD HH:MM:SS" (EXIF), or empty.
std::string xmp_date_to_exif(const std::string& text) {
  if (text.size() < 19 || text[4] != '-' || text[7] != '-' || text[10] != 'T') {
    return {};
  }
  auto exif = text.substr(0, 19);
  exif[4] = ':';
  exif[7] = ':';
  exif[10] = ' ';
  return exif;
}

// IIM date "CCYYMMDD" to "CCYY-MM-DD" and back.
std::string iim_date_to_xmp(const std::string& text) {
  if (text.size() == 8 && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) {
    return text.substr(0, 4) + "-" + text.substr(4, 2) + "-" + text.substr(6, 2);
  }
  return text;
}

std::string xmp_date_to_iim(const std::string& text) {
  std::string digits;
  for (const char c : text) {
    if (c == 'T') {
      break;
    }
    if (c >= '0' && c <= '9') {
      digits.push_back(c);
    }
  }
  return digits.size() >= 8 ? digits.substr(0, 8) : std::string{};
}

struct IimField {
  std::uint8_t dataset;
  std::string FileInfoFields::*single;
  std::vector<std::string> FileInfoFields::*list;
};

// IPTC-IIM datasets that mirror File Info fields (MWG / IPTC Photo Metadata mapping).
const std::vector<IimField>& iim_fields() {
  static const std::vector<IimField> fields{
      {iim::kObjectName, &FileInfoFields::title, nullptr},
      {iim::kByline, nullptr, &FileInfoFields::authors},
      {iim::kBylineTitle, &FileInfoFields::author_title, nullptr},
      {iim::kCaption, &FileInfoFields::description, nullptr},
      {iim::kWriterEditor, &FileInfoFields::description_writer, nullptr},
      {iim::kKeywords, nullptr, &FileInfoFields::keywords},
      {iim::kCopyrightNotice, &FileInfoFields::copyright_notice, nullptr},
      {iim::kHeadline, &FileInfoFields::headline, nullptr},
      {iim::kCity, &FileInfoFields::city, nullptr},
      {iim::kSublocation, &FileInfoFields::sublocation, nullptr},
      {iim::kProvinceState, &FileInfoFields::state, nullptr},
      {iim::kCountryCode, &FileInfoFields::country_code, nullptr},
      {iim::kCountryName, &FileInfoFields::country, nullptr},
      {iim::kCredit, &FileInfoFields::credit, nullptr},
      {iim::kSource, &FileInfoFields::source, nullptr},
      {iim::kSpecialInstructions, &FileInfoFields::instructions, nullptr},
      {iim::kTransmissionReference, &FileInfoFields::transmission_reference, nullptr},
  };
  return fields;
}

struct XmpTextField {
  std::string_view ns;
  std::string_view name;
  std::string FileInfoFields::*member;
  bool lang_alt;
};

const std::vector<XmpTextField>& xmp_text_fields() {
  static const std::vector<XmpTextField> fields{
      {xmp_ns::kDc, "title", &FileInfoFields::title, true},
      {xmp_ns::kPhotoshop, "AuthorsPosition", &FileInfoFields::author_title, false},
      {xmp_ns::kDc, "description", &FileInfoFields::description, true},
      {xmp_ns::kPhotoshop, "CaptionWriter", &FileInfoFields::description_writer, false},
      {xmp_ns::kDc, "rights", &FileInfoFields::copyright_notice, true},
      {xmp_ns::kXmpRights, "WebStatement", &FileInfoFields::copyright_url, false},
      {xmp_ns::kPhotoshop, "Headline", &FileInfoFields::headline, false},
      {xmp_ns::kPhotoshop, "DateCreated", &FileInfoFields::date_created, false},
      {xmp_ns::kPhotoshop, "City", &FileInfoFields::city, false},
      {xmp_ns::kPhotoshop, "State", &FileInfoFields::state, false},
      {xmp_ns::kPhotoshop, "Country", &FileInfoFields::country, false},
      {xmp_ns::kIptcCore, "Location", &FileInfoFields::sublocation, false},
      {xmp_ns::kIptcCore, "CountryCode", &FileInfoFields::country_code, false},
      {xmp_ns::kPhotoshop, "Credit", &FileInfoFields::credit, false},
      {xmp_ns::kPhotoshop, "Source", &FileInfoFields::source, false},
      {xmp_ns::kPhotoshop, "Instructions", &FileInfoFields::instructions, false},
      {xmp_ns::kPhotoshop, "TransmissionReference", &FileInfoFields::transmission_reference, false},
      {xmp_ns::kXmpRights, "UsageTerms", &FileInfoFields::usage_terms, true},
      {xmp_ns::kIptcCore, "IntellectualGenre", &FileInfoFields::intellectual_genre, false},
  };
  return fields;
}

struct ContactField {
  std::string_view name;
  std::string FileInfoFields::*member;
};

const std::vector<ContactField>& contact_fields() {
  static const std::vector<ContactField> fields{
      {"CiAdrExtadr", &FileInfoFields::contact_address}, {"CiAdrCity", &FileInfoFields::contact_city},
      {"CiAdrRegion", &FileInfoFields::contact_region},  {"CiAdrPcode", &FileInfoFields::contact_postal_code},
      {"CiAdrCtry", &FileInfoFields::contact_country},   {"CiTelWork", &FileInfoFields::contact_phone},
      {"CiEmailWork", &FileInfoFields::contact_email},   {"CiUrlWork", &FileInfoFields::contact_url},
  };
  return fields;
}

}  // namespace

void stamp_file_info_for_save(DocumentFileInfo& info, const SaveStamp& stamp, const SaveStampDocument* document) {
  // EXIF IFD0 names the application and time of the latest save, as Photoshop writes it
  // even into a file another application created (xmp:CreatorTool keeps the creator).
  // Without the document, only an EXIF block the document already has; with it, a
  // document that has none gets Photoshop's minimal block unless it was stripped.
  auto stream = exif_of(info);
  const bool new_exif = (!info.exif || info.exif->empty()) && document != nullptr && !info.stripped;
  if (new_exif) {
    stream = TiffStream{};
    stream->big_endian = true;  // Photoshop writes its EXIF big-endian
    stream->set_short(IfdKind::Ifd0, exif_tag::kOrientation, 1);
    static constexpr std::array<std::uint8_t, 4> kExifVersion{'0', '2', '3', '1'};
    stream->set_undefined(IfdKind::Exif, exif_tag::kExifVersion, kExifVersion);
    stream->set_short(IfdKind::Exif, exif_tag::kColorSpace, document->srgb ? 1 : 0xFFFF);
  }
  if (stream.has_value()) {
    stream->set_ascii(IfdKind::Ifd0, exif_tag::kSoftware, stamp.software);
    if (const auto when = xmp_date_to_exif(stamp.when); !when.empty()) {
      stream->set_ascii(IfdKind::Ifd0, exif_tag::kDateTime, when);
    }
    if (document != nullptr) {
      // The saved file's size and resolution; an IFD1 thumbnail shows an older image.
      stream->thumbnail.clear();
      std::erase_if(stream->ifds, [](const TiffIfd& ifd) { return ifd.kind == IfdKind::Ifd1; });
      if (auto* exif = stream->find(IfdKind::Exif); exif != nullptr) {
        if (new_exif || exif->find(exif_tag::kPixelXDimension) != nullptr ||
            exif->find(exif_tag::kPixelYDimension) != nullptr) {
          stream->set_long(IfdKind::Exif, exif_tag::kPixelXDimension, static_cast<std::uint32_t>(document->width));
          stream->set_long(IfdKind::Exif, exif_tag::kPixelYDimension, static_cast<std::uint32_t>(document->height));
        }
      }
      if (const auto* ifd0 = stream->find(IfdKind::Ifd0);
          new_exif || (ifd0 != nullptr && (ifd0->find(exif_tag::kXResolution) != nullptr ||
                                           ifd0->find(exif_tag::kYResolution) != nullptr))) {
        const auto ppi = [](double value) {
          return static_cast<std::uint32_t>(std::clamp(std::lround(value * 100.0), 1L, 100000000L));
        };
        stream->set_rational(IfdKind::Ifd0, exif_tag::kXResolution, ppi(document->horizontal_ppi), 100);
        stream->set_rational(IfdKind::Ifd0, exif_tag::kYResolution, ppi(document->vertical_ppi), 100);
        stream->set_short(IfdKind::Ifd0, exif_tag::kResolutionUnit, 2);
      }
    }
    info.exif = make_metadata_bytes(write_tiff(*stream));
  }

  XmpPacket packet;
  if (info.xmp && !info.xmp->empty()) {
    auto parsed = parse_xmp(*info.xmp);
    if (!parsed.has_value()) {
      return;
    }
    packet = std::move(*parsed);
  }
  const bool first_save = packet.text(xmp_ns::kXmp, "CreateDate").empty();
  if (packet.text(xmp_ns::kXmp, "CreatorTool").empty()) {
    packet.set_simple(xmp_ns::kXmp, "CreatorTool", stamp.software);
  }
  if (first_save) {
    packet.set_simple(xmp_ns::kXmp, "CreateDate", stamp.when);
  }
  packet.set_simple(xmp_ns::kXmp, "ModifyDate", stamp.when);
  packet.set_simple(xmp_ns::kXmp, "MetadataDate", stamp.when);
  if (document != nullptr) {
    packet.set_simple(xmp_ns::kDc, "format", document->format);
    packet.set_simple(xmp_ns::kPhotoshop, "ColorMode", std::to_string(document->color_mode));
  }

  XmpValue event;
  event.kind = XmpValue::Kind::Struct;
  const auto field = [&event](std::string_view name, const std::string& value) {
    XmpProperty property;
    property.ns = std::string(xmp_ns::kStEvt);
    property.name = std::string(name);
    property.value.text = value;
    event.fields.push_back(std::move(property));
  };
  // "created" only for a packet with no creation date and no history: a document's
  // first save, not a file another application wrote without a CreateDate.
  const bool created = first_save && packet.find(xmp_ns::kXmpMM, "History") == nullptr;
  // Each save is a new instance: xmpMM:InstanceID names the entry this save adds. The
  // first save also names the document, from the same GUID as Photoshop does.
  const auto instance_id = "xmp.iid:" + stamp.instance_guid;
  if (created) {
    for (const auto* name : {"DocumentID", "OriginalDocumentID"}) {
      if (packet.text(xmp_ns::kXmpMM, name).empty()) {
        packet.set_simple(xmp_ns::kXmpMM, name, "xmp.did:" + stamp.instance_guid);
      }
    }
  }
  packet.set_simple(xmp_ns::kXmpMM, "InstanceID", instance_id);
  field("action", created ? "created" : "saved");
  field("instanceID", instance_id);
  field("when", stamp.when);
  field("softwareAgent", stamp.software);
  if (!created) {
    field("changed", "/");
  }
  auto* history = packet.find(xmp_ns::kXmpMM, "History");
  if (history == nullptr || history->value.kind != XmpValue::Kind::Seq) {
    if (history != nullptr) {
      packet.erase(xmp_ns::kXmpMM, "History");
    }
    XmpProperty property;
    property.ns = std::string(xmp_ns::kXmpMM);
    property.name = "History";
    property.value.kind = XmpValue::Kind::Seq;
    packet.properties.push_back(std::move(property));
    history = &packet.properties.back();
  }
  history->value.items.push_back(std::move(event));

  const auto text = serialize_xmp(packet);
  info.xmp = make_metadata_bytes(std::vector<std::uint8_t>(text.begin(), text.end()));
}

namespace {

// The fields exactly as the XMP packet states them (no legacy fallbacks).
FileInfoFields read_xmp_fields(const XmpPacket& packet) {
  FileInfoFields fields;
  for (const auto& field : xmp_text_fields()) {
    fields.*field.member = packet.text(field.ns, field.name);
  }
  fields.authors = packet.items(xmp_ns::kDc, "creator");
  fields.keywords = packet.items(xmp_ns::kDc, "subject");
  fields.scene = packet.items(xmp_ns::kIptcCore, "Scene");
  fields.subject_code = packet.items(xmp_ns::kIptcCore, "SubjectCode");
  for (const auto& field : contact_fields()) {
    fields.*field.member = packet.struct_field(xmp_ns::kIptcCore, "CreatorContactInfo", xmp_ns::kIptcCore, field.name);
  }
  if (const auto rating = packet.text(xmp_ns::kXmp, "Rating"); !rating.empty()) {
    fields.rating = std::clamp(static_cast<int>(std::lround(std::strtod(rating.c_str(), nullptr))), -1, 5);
  }
  const auto marked = packet.text(xmp_ns::kXmpRights, "Marked");
  if (marked == "True" || marked == "true") {
    fields.copyright_status = CopyrightStatus::Copyrighted;
  } else if (marked == "False" || marked == "false") {
    fields.copyright_status = CopyrightStatus::PublicDomain;
  }
  fields.created = packet.text(xmp_ns::kXmp, "CreateDate");
  fields.modified = packet.text(xmp_ns::kXmp, "ModifyDate");
  fields.creator_tool = packet.text(xmp_ns::kXmp, "CreatorTool");
  return fields;
}

}  // namespace

FileInfoFields read_fields(const DocumentFileInfo& info) {
  auto fields = read_xmp_fields(packet_of(info));
  if (fields.copyright_status == CopyrightStatus::Unknown && info.copyright_flag == true) {
    fields.copyright_status = CopyrightStatus::Copyrighted;
  }
  if (fields.copyright_url.empty()) {
    fields.copyright_url = info.copyright_url;
  }

  // Legacy IPTC: fills gaps, and wins when the digest shows another program edited it.
  if (info.iptc) {
    if (const auto block = parse_iim(*info.iptc)) {
      const bool iim_wins = info.iptc_digest && *info.iptc_digest != iim_digest(*info.iptc);
      for (const auto& field : iim_fields()) {
        const auto values = block->values(2, field.dataset);
        if (values.empty()) {
          continue;
        }
        if (field.single != nullptr) {
          auto& target = fields.*field.single;
          if (target.empty() || iim_wins) {
            target = values.front();
          }
        } else {
          auto& target = fields.*field.list;
          if (target.empty() || iim_wins) {
            target = values;
          }
        }
      }
      if (fields.date_created.empty() || iim_wins) {
        const auto date = block->value(2, iim::kDateCreated);
        if (!date.empty()) {
          fields.date_created = iim_date_to_xmp(date);
        }
      }
    }
  }

  // EXIF: Exif is authoritative for dates and fills description/author/copyright gaps.
  const auto stream = exif_of(info);
  if (fields.description.empty()) {
    fields.description = exif_text(stream, IfdKind::Ifd0, exif_tag::kImageDescription);
  }
  if (fields.authors.empty()) {
    if (auto artist = exif_text(stream, IfdKind::Ifd0, exif_tag::kArtist); !artist.empty()) {
      fields.authors.push_back(std::move(artist));
    }
  }
  if (fields.copyright_notice.empty()) {
    fields.copyright_notice = exif_text(stream, IfdKind::Ifd0, exif_tag::kCopyright);
  }
  if (const auto original = exif_text(stream, IfdKind::Exif, exif_tag::kDateTimeOriginal); !original.empty()) {
    fields.created = exif_date_to_xmp(original);
  }
  if (fields.modified.empty()) {
    fields.modified = exif_date_to_xmp(exif_text(stream, IfdKind::Ifd0, exif_tag::kDateTime));
  }
  if (fields.creator_tool.empty()) {
    fields.creator_tool = exif_text(stream, IfdKind::Ifd0, exif_tag::kSoftware);
  }
  return fields;
}

bool apply_fields(DocumentFileInfo& info, const FileInfoFields& before, const FileInfoFields& after) {
  if (before == after) {
    return false;
  }
  // MWG: XMP is always written. Every field whose XMP value differs from the result is set,
  // including values that were only displayed from EXIF or IPTC; the legacy mirrors below
  // change only for the fields the user edited.
  auto packet = packet_of(info);
  const auto in_xmp = read_xmp_fields(packet);
  for (const auto& field : xmp_text_fields()) {
    const auto& value = after.*field.member;
    if (in_xmp.*field.member == value) {
      continue;
    }
    if (field.lang_alt) {
      packet.set_lang_alt(field.ns, field.name, value);
    } else {
      packet.set_simple(field.ns, field.name, value);
    }
  }
  if (in_xmp.authors != after.authors) {
    packet.set_array(xmp_ns::kDc, "creator", XmpValue::Kind::Seq, after.authors);
  }
  if (in_xmp.keywords != after.keywords) {
    packet.set_array(xmp_ns::kDc, "subject", XmpValue::Kind::Bag, after.keywords);
  }
  if (in_xmp.scene != after.scene) {
    packet.set_array(xmp_ns::kIptcCore, "Scene", XmpValue::Kind::Bag, after.scene);
  }
  if (in_xmp.subject_code != after.subject_code) {
    packet.set_array(xmp_ns::kIptcCore, "SubjectCode", XmpValue::Kind::Bag, after.subject_code);
  }
  for (const auto& field : contact_fields()) {
    if (in_xmp.*field.member != after.*field.member) {
      packet.set_struct_field(xmp_ns::kIptcCore, "CreatorContactInfo", xmp_ns::kIptcCore, field.name,
                              after.*field.member);
    }
  }
  if (in_xmp.rating != after.rating) {
    packet.set_simple(xmp_ns::kXmp, "Rating", after.rating == 0 ? std::string{} : std::to_string(after.rating));
  }
  if (in_xmp.copyright_status != after.copyright_status) {
    packet.set_simple(xmp_ns::kXmpRights, "Marked",
                      after.copyright_status == CopyrightStatus::Copyrighted    ? "True"
                      : after.copyright_status == CopyrightStatus::PublicDomain ? "False"
                                                                                : "");
  }
  if (before.copyright_status != after.copyright_status) {
    if (after.copyright_status == CopyrightStatus::Copyrighted) {
      info.copyright_flag = true;
    } else if (info.copyright_flag) {
      info.copyright_flag = false;
    }
  }
  if (before.copyright_url != after.copyright_url && (!info.copyright_url.empty() || !after.copyright_url.empty())) {
    info.copyright_url = after.copyright_url;
  }
  store_packet(info, packet);

  // EXIF mirrors, only in an EXIF block the file already had.
  if (auto stream = exif_of(info)) {
    const auto mirror = [&](std::uint16_t tag, const std::string& old_value, const std::string& new_value) {
      if (old_value == new_value) {
        return;
      }
      if (new_value.empty()) {
        if (auto* ifd = stream->find(IfdKind::Ifd0)) {
          ifd->erase(tag);
        }
      } else {
        stream->set_ascii(IfdKind::Ifd0, tag, new_value);
      }
    };
    mirror(exif_tag::kImageDescription, before.description, after.description);
    mirror(exif_tag::kArtist, join(before.authors), join(after.authors));
    mirror(exif_tag::kCopyright, before.copyright_notice, after.copyright_notice);
    info.exif = make_metadata_bytes(write_tiff(*stream));
  }

  // IPTC-IIM mirrors, only in an IIM block the file already had.
  if (info.iptc) {
    if (auto block = parse_iim(*info.iptc)) {
      for (const auto& field : iim_fields()) {
        if (field.single != nullptr && before.*field.single != after.*field.single) {
          block->set_values(2, field.dataset, {after.*field.single});
        } else if (field.list != nullptr && before.*field.list != after.*field.list) {
          block->set_values(2, field.dataset, after.*field.list);
        }
      }
      if (before.date_created != after.date_created) {
        block->set_values(2, iim::kDateCreated, {xmp_date_to_iim(after.date_created)});
      }
      auto bytes = write_iim(*block);
      info.iptc_digest = iim_digest(bytes);
      info.iptc = make_metadata_bytes(std::move(bytes));
    }
  }
  return true;
}

void strip_metadata(Document& document, bool keep_color_profile) {
  strip_file_info(document.file_info(), keep_color_profile);
  if (!keep_color_profile) {
    document.color_state().embedded_icc_profile.clear();
  }
}

void strip_file_info(DocumentFileInfo& info, bool keep_color_profile) {
  info.xmp.reset();
  info.exif.reset();
  info.iptc.reset();
  info.iptc_digest.reset();
  info.copyright_flag.reset();
  info.copyright_url.clear();
  info.text.clear();
  info.psd_resources.clear();
  info.observed.clear();
  info.stripped = true;
  if (!keep_color_profile) {
    info.png_color_chunks.clear();
    info.source_icc.reset();
    info.source_icc_note.clear();
  }
}

bool remove_location(DocumentFileInfo& info) {
  bool removed = false;
  if (auto stream = exif_of(info); stream && stream->find(IfdKind::Gps) != nullptr) {
    std::erase_if(stream->ifds, [](const TiffIfd& ifd) { return ifd.kind == IfdKind::Gps; });
    info.exif = make_metadata_bytes(write_tiff(*stream));
    removed = true;
  }
  auto packet = packet_of(info);
  if (packet.erase_matching(xmp_ns::kExif, "GPS") > 0) {
    store_packet(info, packet);
    removed = true;
  }
  return removed;
}

bool remove_edit_history(DocumentFileInfo& info) {
  bool removed = false;
  auto packet = packet_of(info);
  for (const auto& [ns, name] : {std::pair{xmp_ns::kXmpMM, "History"}, std::pair{xmp_ns::kXmpMM, "DerivedFrom"},
                                 std::pair{xmp_ns::kXmpMM, "OriginalDocumentID"},
                                 std::pair{xmp_ns::kXmpMM, "Ingredients"}, std::pair{xmp_ns::kXmpMM, "Pantry"},
                                 std::pair{xmp_ns::kXmpMM, "Manifest"},
                                 std::pair{xmp_ns::kPhotoshop, "DocumentAncestors"},
                                 std::pair{xmp_ns::kPhotoshop, "TextLayers"},
                                 std::pair{xmp_ns::kPhotoshop, "History"}}) {
    removed = packet.erase(ns, name) || removed;
  }
  removed = packet.erase_matching(xmp_ns::kCrs, "") > 0 || removed;
  if (removed) {
    store_packet(info, packet);
  }
  const auto before = info.psd_resources.size();
  std::erase_if(info.psd_resources, [](const auto& resource) {
    return resource.first == psd_resource::kLightroomWorkflow || resource.first == psd_resource::kAutoSaveFilePath ||
           resource.first == psd_resource::kAutoSaveFormat;
  });
  return removed || info.psd_resources.size() != before;
}

std::string bake_orientation(DocumentFileInfo& info) {
  std::string original;
  if (auto stream = exif_of(info)) {
    if (auto* ifd0 = stream->find(IfdKind::Ifd0)) {
      if (auto* entry = ifd0->find(exif_tag::kOrientation)) {
        const auto value = stream->unsigned_value(*entry);
        if (value && *value > 1 && *value <= 8) {
          original = format_exif_value(*stream, IfdKind::Ifd0, *entry);
          stream->set_short(IfdKind::Ifd0, exif_tag::kOrientation, 1);
          info.exif = make_metadata_bytes(write_tiff(*stream));
        }
      }
    }
  }
  auto packet = packet_of(info);
  if (const auto value = packet.text(xmp_ns::kTiff, "Orientation"); !value.empty() && value != "1") {
    packet.set_simple(xmp_ns::kTiff, "Orientation", "1");
    store_packet(info, packet);
  }
  return original;
}

SaveMetadata prepare_save_metadata(const Document& document, MetadataPolicy policy, std::int32_t output_width,
                                   std::int32_t output_height) {
  SaveMetadata out;
  const auto& info = document.file_info();
  out.icc = document.color_state().embedded_icc_profile;
  out.png_color_chunks = info.png_color_chunks;
  if (policy == MetadataPolicy::None) {
    return out;
  }
  const bool all = policy == MetadataPolicy::All;

  // EXIF
  if (auto stream = exif_of(info)) {
    if (!all) {
      TiffStream reduced;
      reduced.big_endian = stream->big_endian;
      if (const auto* ifd0 = stream->find(IfdKind::Ifd0)) {
        for (const auto tag : {exif_tag::kArtist, exif_tag::kCopyright}) {
          if (const auto* entry = ifd0->find(tag)) {
            reduced.find_or_add(IfdKind::Ifd0).entries.push_back(*entry);
          }
        }
      }
      stream = std::move(reduced);
    }
    // The EXIF thumbnail shows the image as it was when the camera or editor wrote it.
    stream->thumbnail.clear();
    std::erase_if(stream->ifds, [](const TiffIfd& ifd) { return ifd.kind == IfdKind::Ifd1; });
    if (auto* ifd0 = stream->find(IfdKind::Ifd0); ifd0 != nullptr && ifd0->find(exif_tag::kOrientation) != nullptr) {
      stream->set_short(IfdKind::Ifd0, exif_tag::kOrientation, 1);
    }
    if (auto* exif = stream->find(IfdKind::Exif)) {
      if (exif->find(exif_tag::kPixelXDimension) != nullptr || exif->find(exif_tag::kPixelYDimension) != nullptr) {
        stream->set_long(IfdKind::Exif, exif_tag::kPixelXDimension, static_cast<std::uint32_t>(output_width));
        stream->set_long(IfdKind::Exif, exif_tag::kPixelYDimension, static_cast<std::uint32_t>(output_height));
      }
    }
    if (auto* ifd0 = stream->find(IfdKind::Ifd0);
        ifd0 != nullptr && (ifd0->find(exif_tag::kXResolution) != nullptr || ifd0->find(exif_tag::kYResolution))) {
      const auto ppi = [](double value) {
        return static_cast<std::uint32_t>(std::clamp(std::lround(value * 100.0), 1L, 100000000L));
      };
      stream->set_rational(IfdKind::Ifd0, exif_tag::kXResolution, ppi(document.print_settings().horizontal_ppi), 100);
      stream->set_rational(IfdKind::Ifd0, exif_tag::kYResolution, ppi(document.print_settings().vertical_ppi), 100);
      stream->set_short(IfdKind::Ifd0, exif_tag::kResolutionUnit, 2);
    }
    const bool has_entries = std::any_of(stream->ifds.begin(), stream->ifds.end(),
                                         [](const TiffIfd& ifd) { return !ifd.entries.empty(); });
    if (has_entries) {
      out.exif = write_tiff(*stream);
    }
  }

  // XMP
  if (info.xmp) {
    if (auto packet = parse_xmp(*info.xmp)) {
      if (!all) {
        XmpPacket reduced;
        reduced.prefixes = packet->prefixes;
        for (const auto& property : packet->properties) {
          const bool keep = (property.ns == xmp_ns::kDc && (property.name == "creator" || property.name == "rights")) ||
                            property.ns == xmp_ns::kXmpRights ||
                            (property.ns == xmp_ns::kPhotoshop && property.name == "AuthorsPosition") ||
                            (property.ns == xmp_ns::kIptcCore && property.name == "CreatorContactInfo");
          if (keep) {
            reduced.properties.push_back(property);
          }
        }
        packet = std::move(reduced);
      }
      packet->erase(xmp_ns::kXmp, "Thumbnails");
      if (!packet->text(xmp_ns::kTiff, "Orientation").empty()) {
        packet->set_simple(xmp_ns::kTiff, "Orientation", "1");
      }
      for (const auto& [ns, name, value] :
           {std::tuple{xmp_ns::kExif, "PixelXDimension", output_width},
            std::tuple{xmp_ns::kExif, "PixelYDimension", output_height},
            std::tuple{xmp_ns::kTiff, "ImageWidth", output_width}, std::tuple{xmp_ns::kTiff, "ImageLength", output_height}}) {
        if (packet->find(ns, name) != nullptr) {
          packet->set_simple(ns, name, std::to_string(value));
        }
      }
      if (!packet->empty()) {
        out.xmp = serialize_xmp(*packet);
      }
    }
  }

  // IPTC-IIM
  if (info.iptc) {
    if (all) {
      out.iptc = *info.iptc;
      out.iptc_digest = info.iptc_digest;
    } else if (auto block = parse_iim(*info.iptc)) {
      std::erase_if(block->datasets, [](const IimDataset& dataset) {
        return dataset.record == 2 && dataset.dataset != 0 && dataset.dataset != iim::kByline &&
               dataset.dataset != iim::kBylineTitle && dataset.dataset != iim::kCopyrightNotice;
      });
      if (!block->empty()) {
        out.iptc = write_iim(*block);
        out.iptc_digest = iim_digest(out.iptc);
      }
    }
  }

  // Text chunks and comments (never the read-only PDF/SVG entries).
  for (const auto& text : info.text) {
    if (text.kind == FileInfoTextKind::PdfInfo || text.kind == FileInfoTextKind::SvgText) {
      continue;
    }
    if (!all && text.key != "Author" && text.key != "Copyright") {
      continue;
    }
    out.text.push_back(text);
  }
  return out;
}

}  // namespace patchy::metadata
