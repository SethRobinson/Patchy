#include "formats/metadata/iptc_iim.hpp"

#include "formats/pdf_crypt.hpp"

#include <algorithm>
#include <cstdio>

namespace patchy::metadata {
namespace {

constexpr std::uint8_t kTagMarker = 0x1C;
constexpr std::size_t kMaxDatasets = 8192;
constexpr std::array<std::uint8_t, 3> kUtf8Escape{0x1B, 0x25, 0x47};  // ESC % G

bool is_valid_utf8(std::span<const std::uint8_t> bytes) {
  std::size_t i = 0;
  while (i < bytes.size()) {
    const auto c = bytes[i];
    std::size_t extra = 0;
    if (c < 0x80) {
      ++i;
      continue;
    }
    if ((c & 0xE0) == 0xC0 && c >= 0xC2) {
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
    } else if ((c & 0xF8) == 0xF0 && c <= 0xF4) {
      extra = 3;
    } else {
      return false;
    }
    if (i + extra >= bytes.size()) {
      return false;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
      if ((bytes[i + k] & 0xC0) != 0x80) {
        return false;
      }
    }
    i += extra + 1;
  }
  return true;
}

struct DatasetName {
  std::uint8_t record;
  std::uint8_t dataset;
  std::string_view name;
};

constexpr std::array kDatasetNames{
    DatasetName{1, 0, "EnvelopeRecordVersion"}, DatasetName{1, 5, "Destination"},
    DatasetName{1, 20, "FileFormat"},           DatasetName{1, 22, "FileVersion"},
    DatasetName{1, 30, "ServiceIdentifier"},    DatasetName{1, 40, "EnvelopeNumber"},
    DatasetName{1, 50, "ProductID"},            DatasetName{1, 60, "EnvelopePriority"},
    DatasetName{1, 70, "DateSent"},             DatasetName{1, 80, "TimeSent"},
    DatasetName{1, 90, "CodedCharacterSet"},    DatasetName{1, 100, "UniqueObjectName"},
    DatasetName{1, 120, "ARMIdentifier"},       DatasetName{1, 122, "ARMVersion"},
    DatasetName{2, 0, "ApplicationRecordVersion"}, DatasetName{2, 3, "ObjectTypeReference"},
    DatasetName{2, 4, "ObjectAttributeReference"}, DatasetName{2, 5, "ObjectName"},
    DatasetName{2, 7, "EditStatus"},            DatasetName{2, 8, "EditorialUpdate"},
    DatasetName{2, 10, "Urgency"},              DatasetName{2, 12, "SubjectReference"},
    DatasetName{2, 15, "Category"},             DatasetName{2, 20, "SupplementalCategories"},
    DatasetName{2, 22, "FixtureIdentifier"},    DatasetName{2, 25, "Keywords"},
    DatasetName{2, 26, "ContentLocationCode"},  DatasetName{2, 27, "ContentLocationName"},
    DatasetName{2, 30, "ReleaseDate"},          DatasetName{2, 35, "ReleaseTime"},
    DatasetName{2, 37, "ExpirationDate"},       DatasetName{2, 38, "ExpirationTime"},
    DatasetName{2, 40, "SpecialInstructions"},  DatasetName{2, 42, "ActionAdvised"},
    DatasetName{2, 45, "ReferenceService"},     DatasetName{2, 47, "ReferenceDate"},
    DatasetName{2, 50, "ReferenceNumber"},      DatasetName{2, 55, "DateCreated"},
    DatasetName{2, 60, "TimeCreated"},          DatasetName{2, 62, "DigitalCreationDate"},
    DatasetName{2, 63, "DigitalCreationTime"},  DatasetName{2, 65, "OriginatingProgram"},
    DatasetName{2, 70, "ProgramVersion"},       DatasetName{2, 75, "ObjectCycle"},
    DatasetName{2, 80, "By-line"},              DatasetName{2, 85, "By-lineTitle"},
    DatasetName{2, 90, "City"},                 DatasetName{2, 92, "Sub-location"},
    DatasetName{2, 95, "Province-State"},       DatasetName{2, 100, "Country-PrimaryLocationCode"},
    DatasetName{2, 101, "Country-PrimaryLocationName"}, DatasetName{2, 103, "OriginalTransmissionReference"},
    DatasetName{2, 105, "Headline"},            DatasetName{2, 110, "Credit"},
    DatasetName{2, 115, "Source"},              DatasetName{2, 116, "CopyrightNotice"},
    DatasetName{2, 118, "Contact"},             DatasetName{2, 120, "Caption-Abstract"},
    DatasetName{2, 121, "LocalCaption"},        DatasetName{2, 122, "Writer-Editor"},
    DatasetName{2, 125, "RasterizedCaption"},   DatasetName{2, 130, "ImageType"},
    DatasetName{2, 131, "ImageOrientation"},    DatasetName{2, 135, "LanguageIdentifier"},
    DatasetName{2, 150, "AudioType"},           DatasetName{2, 184, "JobID"},
    DatasetName{2, 187, "OwnerID"},             DatasetName{2, 200, "ObjectPreviewFileFormat"},
    DatasetName{2, 201, "ObjectPreviewFileVersion"}, DatasetName{2, 202, "ObjectPreviewData"},
    DatasetName{2, 221, "Prefs"},               DatasetName{2, 225, "ClassifyState"},
    DatasetName{2, 228, "SimilarityIndex"},     DatasetName{2, 230, "DocumentNotes"},
    DatasetName{2, 231, "DocumentHistory"},     DatasetName{2, 232, "ExifCameraInfo"},
    DatasetName{2, 255, "CatalogSets"},
};

struct ResourceName {
  std::uint16_t id;
  std::string_view name;
  PrivacyClass privacy;
};

using P = PrivacyClass;

constexpr std::array kResourceNames{
    ResourceName{1000, "Channels, rows, columns, depth, mode (obsolete)", P::None},
    ResourceName{1001, "Macintosh print manager info", P::MachineInfo},
    ResourceName{1003, "Indexed color table (obsolete)", P::None},
    ResourceName{1005, "Resolution", P::None},
    ResourceName{1006, "Alpha channel names", P::None},
    ResourceName{1007, "Display info (obsolete)", P::None},
    ResourceName{1008, "Caption", P::None},
    ResourceName{1009, "Border information", P::None},
    ResourceName{1010, "Background color", P::None},
    ResourceName{1011, "Print flags", P::None},
    ResourceName{1012, "Grayscale halftoning", P::None},
    ResourceName{1013, "Color halftoning", P::None},
    ResourceName{1014, "Duotone halftoning", P::None},
    ResourceName{1015, "Grayscale transfer function", P::None},
    ResourceName{1016, "Color transfer functions", P::None},
    ResourceName{1017, "Duotone transfer functions", P::None},
    ResourceName{1018, "Duotone image information", P::None},
    ResourceName{1019, "Effective black and white values", P::None},
    ResourceName{1021, "EPS options", P::None},
    ResourceName{1022, "Quick Mask information", P::None},
    ResourceName{1024, "Target layer", P::None},
    ResourceName{1025, "Working path", P::None},
    ResourceName{1026, "Layer group information", P::None},
    ResourceName{1028, "IPTC-NAA record", P::None},
    ResourceName{1029, "Raw image mode", P::None},
    ResourceName{1030, "JPEG quality", P::None},
    ResourceName{1032, "Grid and guides", P::None},
    ResourceName{1033, "Thumbnail (Photoshop 4)", P::HiddenImage},
    ResourceName{1034, "Copyright flag", P::None},
    ResourceName{1035, "Copyright URL", P::None},
    ResourceName{1036, "Thumbnail", P::HiddenImage},
    ResourceName{1037, "Global light angle", P::None},
    ResourceName{1039, "ICC profile", P::None},
    ResourceName{1040, "Watermark", P::None},
    ResourceName{1041, "ICC untagged profile", P::None},
    ResourceName{1042, "Effects visible", P::None},
    ResourceName{1043, "Spot halftone", P::None},
    ResourceName{1044, "Document ID seed", P::None},
    ResourceName{1045, "Unicode alpha channel names", P::None},
    ResourceName{1046, "Indexed color table count", P::None},
    ResourceName{1047, "Transparency index", P::None},
    ResourceName{1049, "Global light altitude", P::None},
    ResourceName{1050, "Slices", P::None},
    ResourceName{1051, "Workflow URL", P::MachineInfo},
    ResourceName{1052, "Jump to XPEP", P::None},
    ResourceName{1053, "Alpha channel identifiers", P::None},
    ResourceName{1054, "URL list", P::None},
    ResourceName{1057, "Version info (writer and reader application)", P::MachineInfo},
    ResourceName{1058, "EXIF data", P::None},
    ResourceName{1059, "EXIF data 3", P::None},
    ResourceName{1060, "XMP metadata", P::None},
    ResourceName{1061, "Caption digest", P::None},
    ResourceName{1062, "Print scale", P::None},
    ResourceName{1064, "Pixel aspect ratio", P::None},
    ResourceName{1065, "Layer comps", P::None},
    ResourceName{1066, "Alternate duotone colors", P::None},
    ResourceName{1067, "Alternate spot colors", P::None},
    ResourceName{1069, "Layer selection IDs", P::None},
    ResourceName{1070, "HDR toning information", P::None},
    ResourceName{1071, "Print information", P::None},
    ResourceName{1072, "Layer group(s) enabled ID", P::None},
    ResourceName{1073, "Color samplers", P::None},
    ResourceName{1074, "Measurement scale", P::None},
    ResourceName{1075, "Timeline information", P::None},
    ResourceName{1076, "Sheet disclosure", P::None},
    ResourceName{1077, "Display info", P::None},
    ResourceName{1078, "Onion skins", P::None},
    ResourceName{1080, "Count information", P::None},
    ResourceName{1082, "Print information", P::None},
    ResourceName{1083, "Print style", P::None},
    ResourceName{1084, "Macintosh NSPrintInfo", P::MachineInfo},
    ResourceName{1085, "Windows DEVMODE (printer settings)", P::MachineInfo},
    ResourceName{1086, "Auto save file path", P::MachineInfo},
    ResourceName{1087, "Auto save format", P::None},
    ResourceName{1088, "Path selection state", P::None},
    ResourceName{2999, "Clipping path name", P::None},
    ResourceName{3000, "Origin path info", P::None},
    ResourceName{7000, "ImageReady variables", P::None},
    ResourceName{7001, "ImageReady data sets", P::None},
    ResourceName{7002, "ImageReady default selected state", P::None},
    ResourceName{7003, "ImageReady 7 rollover expanded state", P::None},
    ResourceName{7004, "ImageReady rollover expanded state", P::None},
    ResourceName{7005, "ImageReady save layer settings", P::None},
    ResourceName{7006, "ImageReady version", P::None},
    ResourceName{8000, "Lightroom workflow", P::EditHistory},
    ResourceName{10000, "Print flags information", P::None},
};

void append_u16(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_dataset(std::vector<std::uint8_t>& out, const IimDataset& dataset) {
  out.push_back(kTagMarker);
  out.push_back(dataset.record);
  out.push_back(dataset.dataset);
  const auto size = dataset.data.size();
  if (size < 0x8000U) {
    append_u16(out, static_cast<std::uint32_t>(size));
  } else {
    // Extended dataset: high bit set, then a 4-byte length.
    append_u16(out, 0x8004U);
    append_u32(out, static_cast<std::uint32_t>(size));
  }
  out.insert(out.end(), dataset.data.begin(), dataset.data.end());
}

}  // namespace

std::string legacy_text_to_utf8(std::span<const std::uint8_t> bytes) {
  if (is_valid_utf8(bytes)) {
    return std::string(bytes.begin(), bytes.end());
  }
  std::string out;
  out.reserve(bytes.size() + bytes.size() / 4);
  for (const auto byte : bytes) {
    if (byte < 0x80) {
      out.push_back(static_cast<char>(byte));
    } else {
      out.push_back(static_cast<char>(0xC0U | (byte >> 6)));
      out.push_back(static_cast<char>(0x80U | (byte & 0x3FU)));
    }
  }
  return out;
}

std::vector<std::string> IimBlock::values(std::uint8_t record, std::uint8_t dataset) const {
  std::vector<std::string> result;
  for (const auto& entry : datasets) {
    if (entry.record == record && entry.dataset == dataset) {
      result.push_back(iim_text(*this, entry));
    }
  }
  return result;
}

std::string IimBlock::value(std::uint8_t record, std::uint8_t dataset) const {
  for (const auto& entry : datasets) {
    if (entry.record == record && entry.dataset == dataset) {
      return iim_text(*this, entry);
    }
  }
  return {};
}

void IimBlock::set_values(std::uint8_t record, std::uint8_t dataset, const std::vector<std::string>& new_values) {
  // Keep the position of the first instance so a rewrite stays close to the source order.
  auto position = std::find_if(datasets.begin(), datasets.end(), [&](const IimDataset& entry) {
    return entry.record == record && entry.dataset == dataset;
  });
  std::size_t index = position == datasets.end() ? datasets.size()
                                                  : static_cast<std::size_t>(position - datasets.begin());
  erase(record, dataset);
  index = std::min(index, datasets.size());
  bool any = false;
  for (const auto& text : new_values) {
    if (text.empty()) {
      continue;
    }
    IimDataset entry{record, dataset, std::vector<std::uint8_t>(text.begin(), text.end())};
    datasets.insert(datasets.begin() + static_cast<std::ptrdiff_t>(index++), std::move(entry));
    any = true;
  }
  if (any) {
    // Existing text was decoded with the old charset; rewrite it as UTF-8 so the
    // marker stays truthful.
    if (!utf8_marker) {
      for (auto& entry : datasets) {
        if (entry.record == 2 && !(entry.record == record && entry.dataset == dataset)) {
          const auto text = legacy_text_to_utf8(entry.data);
          entry.data.assign(text.begin(), text.end());
        }
      }
    }
    utf8_marker = true;
  }
}

void IimBlock::erase(std::uint8_t record, std::uint8_t dataset) {
  std::erase_if(datasets,
                [&](const IimDataset& entry) { return entry.record == record && entry.dataset == dataset; });
}

bool IimBlock::empty() const noexcept {
  return std::none_of(datasets.begin(), datasets.end(), [](const IimDataset& entry) {
    return !(entry.record == 2 && entry.dataset == 0) && !(entry.record == 1 && entry.dataset == 90) &&
           !(entry.record == 1 && entry.dataset == 0);
  });
}

std::optional<IimBlock> parse_iim(std::span<const std::uint8_t> bytes) {
  if (bytes.empty() || bytes[0] != kTagMarker) {
    return std::nullopt;
  }
  IimBlock block;
  std::size_t pos = 0;
  while (pos + 5 <= bytes.size() && bytes[pos] == kTagMarker && block.datasets.size() < kMaxDatasets) {
    IimDataset dataset;
    dataset.record = bytes[pos + 1];
    dataset.dataset = bytes[pos + 2];
    std::uint64_t size = (static_cast<std::uint32_t>(bytes[pos + 3]) << 8) | bytes[pos + 4];
    pos += 5;
    if ((size & 0x8000U) != 0) {
      const auto length_bytes = static_cast<std::size_t>(size & 0x7FFFU);
      if (length_bytes == 0 || length_bytes > 8 || pos + length_bytes > bytes.size()) {
        break;
      }
      size = 0;
      for (std::size_t i = 0; i < length_bytes; ++i) {
        size = (size << 8) | bytes[pos + i];
      }
      pos += length_bytes;
    }
    if (size > bytes.size() - pos) {
      break;
    }
    dataset.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                        bytes.begin() + static_cast<std::ptrdiff_t>(pos + size));
    pos += static_cast<std::size_t>(size);
    if (dataset.record == 1 && dataset.dataset == 90 && dataset.data.size() >= 3 &&
        std::equal(kUtf8Escape.begin(), kUtf8Escape.end(), dataset.data.begin())) {
      block.utf8_marker = true;
    }
    block.datasets.push_back(std::move(dataset));
  }
  if (block.datasets.empty()) {
    return std::nullopt;
  }
  return block;
}

std::vector<std::uint8_t> write_iim(const IimBlock& block) {
  std::vector<std::uint8_t> out;
  // Record 1 (minus a stale charset marker) first.
  for (const auto& dataset : block.datasets) {
    if (dataset.record != 1 || dataset.dataset == 90) {
      continue;
    }
    append_dataset(out, dataset);
  }
  if (block.utf8_marker) {
    append_dataset(out, IimDataset{1, 90, {kUtf8Escape.begin(), kUtf8Escape.end()}});
  }
  const bool has_version = std::any_of(block.datasets.begin(), block.datasets.end(), [](const IimDataset& dataset) {
    return dataset.record == 2 && dataset.dataset == 0;
  });
  if (!has_version) {
    append_dataset(out, IimDataset{2, 0, {0x00, 0x04}});  // ApplicationRecordVersion 4
  }
  for (const auto& dataset : block.datasets) {
    if (dataset.record == 1) {
      continue;
    }
    if (dataset.record == 2 && dataset.dataset == 0) {
      // Keep the version first in record 2.
      std::vector<std::uint8_t> head;
      append_dataset(head, dataset);
      out.insert(out.end(), head.begin(), head.end());
    }
  }
  for (const auto& dataset : block.datasets) {
    if (dataset.record == 1 || (dataset.record == 2 && dataset.dataset == 0)) {
      continue;
    }
    append_dataset(out, dataset);
  }
  return out;
}

std::string iim_text(const IimBlock& block, const IimDataset& dataset) {
  std::span<const std::uint8_t> data = dataset.data;
  while (!data.empty() && data.back() == 0) {
    data = data.first(data.size() - 1);
  }
  if (block.utf8_marker) {
    return std::string(data.begin(), data.end());
  }
  return legacy_text_to_utf8(data);
}

std::string iim_dataset_name(std::uint8_t record, std::uint8_t dataset) {
  for (const auto& entry : kDatasetNames) {
    if (entry.record == record && entry.dataset == dataset) {
      return std::string(entry.name);
    }
  }
  return std::to_string(record) + ":" + std::to_string(dataset);
}

PrivacyClass iim_dataset_privacy(std::uint8_t record, std::uint8_t dataset) noexcept {
  if (record != 2) {
    return PrivacyClass::None;
  }
  switch (dataset) {
    case iim::kCity:
    case iim::kSublocation:
    case iim::kProvinceState:
    case iim::kCountryCode:
    case iim::kCountryName:
    case 26:
    case 27:
      return PrivacyClass::Location;
    case iim::kByline:
    case iim::kBylineTitle:
    case iim::kWriterEditor:
    case 118:
      return PrivacyClass::Personal;
    case iim::kDateCreated:
    case iim::kTimeCreated:
    case 62:
    case 63:
      return PrivacyClass::Timestamp;
    case iim::kOriginatingProgram:
    case 70:
      return PrivacyClass::MachineInfo;
    case 202:
      return PrivacyClass::HiddenImage;
    case 231:
      return PrivacyClass::EditHistory;
    default:
      return PrivacyClass::None;
  }
}

std::string iim_display_text(std::uint8_t record, std::uint8_t dataset, std::string text) {
  // 1:70 DateSent, 1:80 TimeSent; 2:30/35 Release, 2:37/38 Expiration, 2:55/60 Created,
  // 2:62/63 DigitalCreation.
  const bool is_date = (record == 1 && dataset == 70) ||
                       (record == 2 && (dataset == 30 || dataset == 37 || dataset == iim::kDateCreated || dataset == 62));
  const bool is_time = (record == 1 && dataset == 80) ||
                       (record == 2 && (dataset == 35 || dataset == 38 || dataset == iim::kTimeCreated || dataset == 63));
  const auto digits = [&text](std::size_t from, std::size_t count) {
    for (std::size_t i = from; i < from + count; ++i) {
      if (text[i] < '0' || text[i] > '9') {
        return false;
      }
    }
    return true;
  };
  if (is_date && text.size() == 8 && digits(0, 8)) {
    return text.substr(0, 4) + "-" + text.substr(4, 2) + "-" + text.substr(6, 2);
  }
  if (is_time && (text.size() == 6 || text.size() == 11) && digits(0, 6)) {
    auto shown = text.substr(0, 2) + ":" + text.substr(2, 2) + ":" + text.substr(4, 2);
    if (text.size() == 6) {
      return shown;
    }
    if ((text[6] == '+' || text[6] == '-') && digits(7, 4)) {
      return shown + text[6] + text.substr(7, 2) + ":" + text.substr(9, 2);
    }
  }
  return text;
}

std::array<std::uint8_t, 16> iim_digest(std::span<const std::uint8_t> iim_bytes) {
  return pdf::md5(iim_bytes);
}

std::vector<IrbResource> parse_irb(std::span<const std::uint8_t> bytes) {
  std::vector<IrbResource> resources;
  std::size_t pos = 0;
  const auto u32 = [&](std::size_t at) {
    return (static_cast<std::uint32_t>(bytes[at]) << 24) | (static_cast<std::uint32_t>(bytes[at + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 8) | bytes[at + 3];
  };
  while (pos + 12 <= bytes.size() && resources.size() < 4096) {
    IrbResource resource;
    resource.signature = u32(pos);
    const bool known_signature = resource.signature == 0x3842494DU || resource.signature == 0x38423634U ||
                                 resource.signature == 0x4D655361U || resource.signature == 0x50485554U ||
                                 resource.signature == 0x41674867U || resource.signature == 0x44435352U;
    if (!known_signature) {
      break;
    }
    resource.id = static_cast<std::uint16_t>((bytes[pos + 4] << 8) | bytes[pos + 5]);
    std::size_t at = pos + 6;
    const std::size_t name_length = bytes[at];
    std::size_t name_field = 1 + name_length;
    if (name_field % 2 != 0) {
      ++name_field;
    }
    if (at + name_field + 4 > bytes.size()) {
      break;
    }
    resource.name.assign(reinterpret_cast<const char*>(bytes.data() + at + 1), name_length);
    at += name_field;
    const std::uint64_t size = u32(at);
    at += 4;
    if (size > bytes.size() - at) {
      break;
    }
    resource.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                         bytes.begin() + static_cast<std::ptrdiff_t>(at + size));
    pos = at + static_cast<std::size_t>(size) + (size % 2);
    resources.push_back(std::move(resource));
  }
  return resources;
}

std::vector<std::uint8_t> write_irb(const std::vector<IrbResource>& resources) {
  std::vector<std::uint8_t> out;
  for (const auto& resource : resources) {
    append_u32(out, resource.signature);
    append_u16(out, resource.id);
    const auto name_length = std::min<std::size_t>(resource.name.size(), 255);
    out.push_back(static_cast<std::uint8_t>(name_length));
    out.insert(out.end(), resource.name.begin(), resource.name.begin() + static_cast<std::ptrdiff_t>(name_length));
    if ((1 + name_length) % 2 != 0) {
      out.push_back(0);
    }
    append_u32(out, static_cast<std::uint32_t>(resource.data.size()));
    out.insert(out.end(), resource.data.begin(), resource.data.end());
    if (resource.data.size() % 2 != 0) {
      out.push_back(0);
    }
  }
  return out;
}

std::string psd_resource_name(std::uint16_t id) {
  for (const auto& entry : kResourceNames) {
    if (entry.id == id) {
      return std::string(entry.name);
    }
  }
  if (id >= 2000 && id <= 2997) {
    return "Path information";
  }
  if (id >= 4000 && id <= 4999) {
    return "Plug-in resource";
  }
  return "Resource " + std::to_string(id);
}

PrivacyClass psd_resource_privacy(std::uint16_t id) noexcept {
  for (const auto& entry : kResourceNames) {
    if (entry.id == id) {
      return entry.privacy;
    }
  }
  return PrivacyClass::None;
}

std::vector<std::uint8_t> psd_thumbnail_jpeg(std::span<const std::uint8_t> resource_data) {
  // Header: u32 format (1 = kJpegRGB), u32 width, u32 height, u32 widthbytes,
  // u32 total size, u32 compressed size, u16 bits per pixel, u16 planes.
  if (resource_data.size() <= 28) {
    return {};
  }
  const auto format = (static_cast<std::uint32_t>(resource_data[0]) << 24) |
                      (static_cast<std::uint32_t>(resource_data[1]) << 16) |
                      (static_cast<std::uint32_t>(resource_data[2]) << 8) | resource_data[3];
  if (format != 1) {
    return {};
  }
  const auto jpeg = resource_data.subspan(28);
  if (jpeg.size() < 2 || jpeg[0] != 0xFF || jpeg[1] != 0xD8) {
    return {};
  }
  return {jpeg.begin(), jpeg.end()};
}

}  // namespace patchy::metadata
