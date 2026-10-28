#include "formats/metadata/exif_tags.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <span>

namespace patchy::metadata {

std::string_view privacy_class_id(PrivacyClass value) noexcept {
  switch (value) {
    case PrivacyClass::None:
      return "none";
    case PrivacyClass::Location:
      return "location";
    case PrivacyClass::DeviceIdentity:
      return "deviceIdentity";
    case PrivacyClass::Personal:
      return "personal";
    case PrivacyClass::HiddenImage:
      return "hiddenImage";
    case PrivacyClass::EditHistory:
      return "editHistory";
    case PrivacyClass::MachineInfo:
      return "machineInfo";
    case PrivacyClass::Timestamp:
      return "timestamp";
    case PrivacyClass::AiPrompt:
      return "aiPrompt";
    case PrivacyClass::Provenance:
      return "provenance";
    case PrivacyClass::Watermark:
      return "watermark";
  }
  return "none";
}

namespace {

struct TagName {
  std::uint16_t tag;
  std::string_view name;
  PrivacyClass privacy;
};

using P = PrivacyClass;

// TIFF baseline/extension tags as they appear in IFD0/IFD1 (and TIFF files).
constexpr std::array kImageTags{
    TagName{254, "NewSubfileType", P::None},
    TagName{255, "SubfileType", P::None},
    TagName{256, "ImageWidth", P::None},
    TagName{257, "ImageLength", P::None},
    TagName{258, "BitsPerSample", P::None},
    TagName{259, "Compression", P::None},
    TagName{262, "PhotometricInterpretation", P::None},
    TagName{263, "Thresholding", P::None},
    TagName{266, "FillOrder", P::None},
    TagName{269, "DocumentName", P::MachineInfo},
    TagName{270, "ImageDescription", P::None},
    TagName{271, "Make", P::None},
    TagName{272, "Model", P::None},
    TagName{273, "StripOffsets", P::None},
    TagName{274, "Orientation", P::None},
    TagName{277, "SamplesPerPixel", P::None},
    TagName{278, "RowsPerStrip", P::None},
    TagName{279, "StripByteCounts", P::None},
    TagName{282, "XResolution", P::None},
    TagName{283, "YResolution", P::None},
    TagName{284, "PlanarConfiguration", P::None},
    TagName{285, "PageName", P::None},
    TagName{286, "XPosition", P::None},
    TagName{287, "YPosition", P::None},
    TagName{296, "ResolutionUnit", P::None},
    TagName{297, "PageNumber", P::None},
    TagName{301, "TransferFunction", P::None},
    TagName{305, "Software", P::MachineInfo},
    TagName{306, "DateTime", P::Timestamp},
    TagName{315, "Artist", P::Personal},
    TagName{316, "HostComputer", P::MachineInfo},
    TagName{317, "Predictor", P::None},
    TagName{318, "WhitePoint", P::None},
    TagName{319, "PrimaryChromaticities", P::None},
    TagName{320, "ColorMap", P::None},
    TagName{322, "TileWidth", P::None},
    TagName{323, "TileLength", P::None},
    TagName{324, "TileOffsets", P::None},
    TagName{325, "TileByteCounts", P::None},
    TagName{330, "SubIFDs", P::None},
    TagName{338, "ExtraSamples", P::None},
    TagName{339, "SampleFormat", P::None},
    TagName{513, "JPEGInterchangeFormat", P::HiddenImage},
    TagName{514, "JPEGInterchangeFormatLength", P::HiddenImage},
    TagName{529, "YCbCrCoefficients", P::None},
    TagName{530, "YCbCrSubSampling", P::None},
    TagName{531, "YCbCrPositioning", P::None},
    TagName{532, "ReferenceBlackWhite", P::None},
    TagName{700, "XMLPacket", P::None},
    TagName{18246, "Rating", P::None},
    TagName{18249, "RatingPercent", P::None},
    TagName{32781, "ImageID", P::MachineInfo},
    TagName{33421, "CFARepeatPatternDim", P::None},
    TagName{33422, "CFAPattern", P::None},
    TagName{33432, "Copyright", P::Personal},
    TagName{33723, "IPTC-NAA", P::None},
    TagName{34377, "PhotoshopSettings", P::None},
    TagName{34665, "ExifIFDPointer", P::None},
    TagName{34675, "ICCProfile", P::None},
    TagName{34853, "GPSInfoIFDPointer", P::Location},
    TagName{37724, "ImageSourceData", P::HiddenImage},
    TagName{40091, "XPTitle", P::None},
    TagName{40092, "XPComment", P::None},
    TagName{40093, "XPAuthor", P::Personal},
    TagName{40094, "XPKeywords", P::None},
    TagName{40095, "XPSubject", P::None},
    TagName{50341, "PrintIM", P::None},
    TagName{50706, "DNGVersion", P::None},
    TagName{50707, "DNGBackwardVersion", P::None},
    TagName{50708, "UniqueCameraModel", P::None},
    TagName{50709, "LocalizedCameraModel", P::None},
    TagName{50735, "CameraSerialNumber", P::DeviceIdentity},
    TagName{50740, "DNGPrivateData", P::DeviceIdentity},
    TagName{50827, "OriginalRawFileName", P::MachineInfo},
    TagName{51041, "NoiseProfile", P::None},
    TagName{52545, "C2PA", P::Provenance},
};

constexpr std::array kExifTags{
    TagName{33434, "ExposureTime", P::None},
    TagName{33437, "FNumber", P::None},
    TagName{34850, "ExposureProgram", P::None},
    TagName{34852, "SpectralSensitivity", P::None},
    TagName{34855, "ISOSpeedRatings", P::None},
    TagName{34856, "OECF", P::None},
    TagName{34864, "SensitivityType", P::None},
    TagName{34865, "StandardOutputSensitivity", P::None},
    TagName{34866, "RecommendedExposureIndex", P::None},
    TagName{34867, "ISOSpeed", P::None},
    TagName{36864, "ExifVersion", P::None},
    TagName{36867, "DateTimeOriginal", P::Timestamp},
    TagName{36868, "DateTimeDigitized", P::Timestamp},
    TagName{36880, "OffsetTime", P::Timestamp},
    TagName{36881, "OffsetTimeOriginal", P::Timestamp},
    TagName{36882, "OffsetTimeDigitized", P::Timestamp},
    TagName{37121, "ComponentsConfiguration", P::None},
    TagName{37122, "CompressedBitsPerPixel", P::None},
    TagName{37377, "ShutterSpeedValue", P::None},
    TagName{37378, "ApertureValue", P::None},
    TagName{37379, "BrightnessValue", P::None},
    TagName{37380, "ExposureBiasValue", P::None},
    TagName{37381, "MaxApertureValue", P::None},
    TagName{37382, "SubjectDistance", P::None},
    TagName{37383, "MeteringMode", P::None},
    TagName{37384, "LightSource", P::None},
    TagName{37385, "Flash", P::None},
    TagName{37386, "FocalLength", P::None},
    TagName{37396, "SubjectArea", P::None},
    TagName{37500, "MakerNote", P::DeviceIdentity},
    TagName{37510, "UserComment", P::None},
    TagName{37520, "SubSecTime", P::Timestamp},
    TagName{37521, "SubSecTimeOriginal", P::Timestamp},
    TagName{37522, "SubSecTimeDigitized", P::Timestamp},
    TagName{37888, "Temperature", P::None},
    TagName{37889, "Humidity", P::None},
    TagName{37890, "Pressure", P::None},
    TagName{37891, "WaterDepth", P::None},
    TagName{37892, "Acceleration", P::None},
    TagName{37893, "CameraElevationAngle", P::None},
    TagName{40960, "FlashpixVersion", P::None},
    TagName{40961, "ColorSpace", P::None},
    TagName{40962, "PixelXDimension", P::None},
    TagName{40963, "PixelYDimension", P::None},
    TagName{40964, "RelatedSoundFile", P::None},
    TagName{40965, "InteroperabilityIFDPointer", P::None},
    TagName{41483, "FlashEnergy", P::None},
    TagName{41484, "SpatialFrequencyResponse", P::None},
    TagName{41486, "FocalPlaneXResolution", P::None},
    TagName{41487, "FocalPlaneYResolution", P::None},
    TagName{41488, "FocalPlaneResolutionUnit", P::None},
    TagName{41492, "SubjectLocation", P::None},
    TagName{41493, "ExposureIndex", P::None},
    TagName{41495, "SensingMethod", P::None},
    TagName{41728, "FileSource", P::None},
    TagName{41729, "SceneType", P::None},
    TagName{41730, "CFAPattern", P::None},
    TagName{41985, "CustomRendered", P::None},
    TagName{41986, "ExposureMode", P::None},
    TagName{41987, "WhiteBalance", P::None},
    TagName{41988, "DigitalZoomRatio", P::None},
    TagName{41989, "FocalLengthIn35mmFilm", P::None},
    TagName{41990, "SceneCaptureType", P::None},
    TagName{41991, "GainControl", P::None},
    TagName{41992, "Contrast", P::None},
    TagName{41993, "Saturation", P::None},
    TagName{41994, "Sharpness", P::None},
    TagName{41995, "DeviceSettingDescription", P::None},
    TagName{41996, "SubjectDistanceRange", P::None},
    TagName{42016, "ImageUniqueID", P::DeviceIdentity},
    TagName{42032, "CameraOwnerName", P::DeviceIdentity},
    TagName{42033, "BodySerialNumber", P::DeviceIdentity},
    TagName{42034, "LensSpecification", P::None},
    TagName{42035, "LensMake", P::None},
    TagName{42036, "LensModel", P::None},
    TagName{42037, "LensSerialNumber", P::DeviceIdentity},
    TagName{42038, "ImageTitle", P::None},
    TagName{42039, "Photographer", P::Personal},
    TagName{42040, "ImageEditor", P::Personal},
    TagName{42041, "CameraFirmware", P::None},
    TagName{42042, "RAWDevelopingSoftware", P::MachineInfo},
    TagName{42043, "ImageEditingSoftware", P::MachineInfo},
    TagName{42044, "MetadataEditingSoftware", P::MachineInfo},
    TagName{42080, "CompositeImage", P::None},
    TagName{42081, "SourceImageNumberOfCompositeImage", P::None},
    TagName{42082, "SourceExposureTimesOfCompositeImage", P::None},
    TagName{42240, "Gamma", P::None},
    TagName{59932, "Padding", P::None},
    TagName{59933, "OffsetSchema", P::None},
};

constexpr std::array kGpsTags{
    TagName{0, "GPSVersionID", P::Location},
    TagName{1, "GPSLatitudeRef", P::Location},
    TagName{2, "GPSLatitude", P::Location},
    TagName{3, "GPSLongitudeRef", P::Location},
    TagName{4, "GPSLongitude", P::Location},
    TagName{5, "GPSAltitudeRef", P::Location},
    TagName{6, "GPSAltitude", P::Location},
    TagName{7, "GPSTimeStamp", P::Location},
    TagName{8, "GPSSatellites", P::Location},
    TagName{9, "GPSStatus", P::Location},
    TagName{10, "GPSMeasureMode", P::Location},
    TagName{11, "GPSDOP", P::Location},
    TagName{12, "GPSSpeedRef", P::Location},
    TagName{13, "GPSSpeed", P::Location},
    TagName{14, "GPSTrackRef", P::Location},
    TagName{15, "GPSTrack", P::Location},
    TagName{16, "GPSImgDirectionRef", P::Location},
    TagName{17, "GPSImgDirection", P::Location},
    TagName{18, "GPSMapDatum", P::Location},
    TagName{19, "GPSDestLatitudeRef", P::Location},
    TagName{20, "GPSDestLatitude", P::Location},
    TagName{21, "GPSDestLongitudeRef", P::Location},
    TagName{22, "GPSDestLongitude", P::Location},
    TagName{23, "GPSDestBearingRef", P::Location},
    TagName{24, "GPSDestBearing", P::Location},
    TagName{25, "GPSDestDistanceRef", P::Location},
    TagName{26, "GPSDestDistance", P::Location},
    TagName{27, "GPSProcessingMethod", P::Location},
    TagName{28, "GPSAreaInformation", P::Location},
    TagName{29, "GPSDateStamp", P::Location},
    TagName{30, "GPSDifferential", P::Location},
    TagName{31, "GPSHPositioningError", P::Location},
};

constexpr std::array kInteropTags{
    TagName{1, "InteroperabilityIndex", P::None},
    TagName{2, "InteroperabilityVersion", P::None},
    TagName{4096, "RelatedImageFileFormat", P::None},
    TagName{4097, "RelatedImageWidth", P::None},
    TagName{4098, "RelatedImageLength", P::None},
};

const TagName* lookup(IfdKind kind, std::uint16_t tag) noexcept {
  const auto find_in = [tag](std::span<const TagName> table) -> const TagName* {
    for (const auto& entry : table) {
      if (entry.tag == tag) {
        return &entry;
      }
    }
    return nullptr;
  };
  switch (kind) {
    case IfdKind::Gps:
      return find_in(kGpsTags);
    case IfdKind::Interop:
      return find_in(kInteropTags);
    case IfdKind::Exif:
      if (const auto* found = find_in(kExifTags)) {
        return found;
      }
      return find_in(kImageTags);
    default:
      if (const auto* found = find_in(kImageTags)) {
        return found;
      }
      return find_in(kExifTags);  // raw files put Exif tags straight into IFD0
  }
}

struct EnumText {
  int value;
  std::string_view text;
};

std::string enum_text(std::span<const EnumText> table, std::optional<std::uint32_t> value) {
  if (!value) {
    return {};
  }
  for (const auto& entry : table) {
    if (entry.value == static_cast<int>(*value)) {
      return std::string(entry.text);
    }
  }
  return std::to_string(*value);
}

constexpr std::array kOrientation{
    EnumText{1, "Horizontal (normal)"},          EnumText{2, "Mirror horizontal"},
    EnumText{3, "Rotate 180"},                   EnumText{4, "Mirror vertical"},
    EnumText{5, "Mirror horizontal and rotate 270 CW"}, EnumText{6, "Rotate 90 CW"},
    EnumText{7, "Mirror horizontal and rotate 90 CW"},  EnumText{8, "Rotate 270 CW"},
};
constexpr std::array kResolutionUnit{EnumText{1, "None"}, EnumText{2, "inches"}, EnumText{3, "cm"}};
constexpr std::array kCompression{EnumText{1, "Uncompressed"}, EnumText{5, "LZW"}, EnumText{6, "JPEG (old-style)"},
                                  EnumText{7, "JPEG"},         EnumText{8, "Adobe Deflate"},
                                  EnumText{32773, "PackBits"}};
constexpr std::array kPhotometric{EnumText{0, "WhiteIsZero"}, EnumText{1, "BlackIsZero"}, EnumText{2, "RGB"},
                                  EnumText{3, "RGB Palette"}, EnumText{4, "Transparency Mask"},
                                  EnumText{5, "CMYK"},        EnumText{6, "YCbCr"},
                                  EnumText{8, "CIELab"},      EnumText{32803, "Color Filter Array"},
                                  EnumText{34892, "Linear Raw"}};
constexpr std::array kYCbCrPositioning{EnumText{1, "Centered"}, EnumText{2, "Co-sited"}};
constexpr std::array kExposureProgram{
    EnumText{0, "Not Defined"},       EnumText{1, "Manual"},           EnumText{2, "Program AE"},
    EnumText{3, "Aperture-priority AE"}, EnumText{4, "Shutter speed priority AE"}, EnumText{5, "Creative (Slow speed)"},
    EnumText{6, "Action (High speed)"}, EnumText{7, "Portrait"},       EnumText{8, "Landscape"},
};
constexpr std::array kMeteringMode{
    EnumText{0, "Unknown"},     EnumText{1, "Average"},   EnumText{2, "Center-weighted average"},
    EnumText{3, "Spot"},        EnumText{4, "Multi-spot"}, EnumText{5, "Multi-segment"},
    EnumText{6, "Partial"},     EnumText{255, "Other"},
};
constexpr std::array kLightSource{
    EnumText{0, "Unknown"},       EnumText{1, "Daylight"},        EnumText{2, "Fluorescent"},
    EnumText{3, "Tungsten"},      EnumText{4, "Flash"},           EnumText{9, "Fine Weather"},
    EnumText{10, "Cloudy"},       EnumText{11, "Shade"},          EnumText{12, "Daylight Fluorescent"},
    EnumText{13, "Day White Fluorescent"}, EnumText{14, "Cool White Fluorescent"}, EnumText{15, "White Fluorescent"},
    EnumText{17, "Standard Light A"}, EnumText{18, "Standard Light B"}, EnumText{19, "Standard Light C"},
    EnumText{20, "D55"},          EnumText{21, "D65"},            EnumText{22, "D75"},
    EnumText{23, "D50"},          EnumText{24, "ISO Studio Tungsten"}, EnumText{255, "Other"},
};
constexpr std::array kColorSpace{EnumText{1, "sRGB"}, EnumText{2, "Adobe RGB"}, EnumText{0xFFFF, "Uncalibrated"}};
constexpr std::array kSensingMethod{
    EnumText{1, "Not defined"}, EnumText{2, "One-chip color area"}, EnumText{3, "Two-chip color area"},
    EnumText{4, "Three-chip color area"}, EnumText{5, "Color sequential area"}, EnumText{7, "Trilinear"},
    EnumText{8, "Color sequential linear"},
};
constexpr std::array kCustomRendered{EnumText{0, "Normal"}, EnumText{1, "Custom"}};
constexpr std::array kExposureMode{EnumText{0, "Auto"}, EnumText{1, "Manual"}, EnumText{2, "Auto bracket"}};
constexpr std::array kWhiteBalance{EnumText{0, "Auto"}, EnumText{1, "Manual"}};
constexpr std::array kSceneCaptureType{EnumText{0, "Standard"}, EnumText{1, "Landscape"}, EnumText{2, "Portrait"},
                                       EnumText{3, "Night"}};
constexpr std::array kGainControl{EnumText{0, "None"}, EnumText{1, "Low gain up"}, EnumText{2, "High gain up"},
                                  EnumText{3, "Low gain down"}, EnumText{4, "High gain down"}};
constexpr std::array kContrast{EnumText{0, "Normal"}, EnumText{1, "Low"}, EnumText{2, "High"}};
constexpr std::array kSharpness{EnumText{0, "Normal"}, EnumText{1, "Soft"}, EnumText{2, "Hard"}};
constexpr std::array kSubjectDistanceRange{EnumText{0, "Unknown"}, EnumText{1, "Macro"}, EnumText{2, "Close"},
                                           EnumText{3, "Distant"}};
constexpr std::array kSensitivityType{
    EnumText{0, "Unknown"}, EnumText{1, "Standard Output Sensitivity"}, EnumText{2, "Recommended Exposure Index"},
    EnumText{3, "ISO Speed"}, EnumText{4, "SOS and REI"}, EnumText{5, "SOS and ISO Speed"},
    EnumText{6, "REI and ISO Speed"}, EnumText{7, "SOS, REI and ISO Speed"},
};
constexpr std::array kAltitudeRef{EnumText{0, "Above sea level"}, EnumText{1, "Below sea level"}};

std::string format_number(double value, int max_decimals) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", max_decimals, value);
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

std::string flash_text(std::uint32_t value) {
  std::string text = (value & 0x1U) != 0 ? "Fired" : "No flash";
  const auto mode = (value >> 3) & 0x3U;
  if (mode == 1) {
    text += ", compulsory";
  } else if (mode == 2) {
    text += ", off (suppressed)";
  } else if (mode == 3) {
    text += ", auto";
  }
  const auto ret = (value >> 1) & 0x3U;
  if (ret == 2) {
    text += ", return not detected";
  } else if (ret == 3) {
    text += ", return detected";
  }
  if ((value & 0x40U) != 0) {
    text += ", red-eye reduction";
  }
  if ((value & 0x20U) != 0) {
    text = "No flash function";
  }
  return text;
}

std::string exposure_time_text(double seconds) {
  if (seconds <= 0.0) {
    return format_number(seconds, 6) + " s";
  }
  if (seconds < 0.25) {
    return "1/" + format_number(std::round(1.0 / seconds), 0) + " s";
  }
  return format_number(seconds, 2) + " s";
}

std::string version_text(const TiffEntry& entry) {
  // "0232" -> "2.32"; "0100" -> "1.0".
  if (entry.data.size() != 4) {
    return {};
  }
  std::string raw(entry.data.begin(), entry.data.end());
  for (const char c : raw) {
    if (c < '0' || c > '9') {
      return {};
    }
  }
  std::string major = raw.substr(0, 2);
  if (major.front() == '0') {
    major.erase(0, 1);
  }
  std::string minor = raw.substr(2, 2);
  if (minor.size() == 2 && minor.back() == '0') {
    minor.pop_back();
  }
  return major + "." + minor;
}

// UCS-2 little-endian (Windows XP* tags are BYTE arrays in UTF-16LE).
std::string utf16le_text(std::span<const std::uint8_t> data) {
  std::string out;
  for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
    std::uint32_t unit = data[i] | (static_cast<std::uint32_t>(data[i + 1]) << 8);
    if (unit == 0) {
      break;
    }
    if (unit >= 0xD800U && unit <= 0xDBFFU && i + 3 < data.size()) {
      const std::uint32_t low = data[i + 2] | (static_cast<std::uint32_t>(data[i + 3]) << 8);
      if (low >= 0xDC00U && low <= 0xDFFFU) {
        unit = 0x10000U + ((unit - 0xD800U) << 10) + (low - 0xDC00U);
        i += 2;
      }
    }
    if (unit < 0x80U) {
      out.push_back(static_cast<char>(unit));
    } else if (unit < 0x800U) {
      out.push_back(static_cast<char>(0xC0U | (unit >> 6)));
      out.push_back(static_cast<char>(0x80U | (unit & 0x3FU)));
    } else if (unit < 0x10000U) {
      out.push_back(static_cast<char>(0xE0U | (unit >> 12)));
      out.push_back(static_cast<char>(0x80U | ((unit >> 6) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (unit & 0x3FU)));
    } else {
      out.push_back(static_cast<char>(0xF0U | (unit >> 18)));
      out.push_back(static_cast<char>(0x80U | ((unit >> 12) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | ((unit >> 6) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (unit & 0x3FU)));
    }
  }
  return out;
}

// UserComment / GPSProcessingMethod: 8-byte character-code prefix, then text.
std::string prefixed_comment_text(std::span<const std::uint8_t> data) {
  if (data.size() < 8) {
    return {};
  }
  const std::string code(data.begin(), data.begin() + 8);
  const auto body = data.subspan(8);
  if (code.starts_with("UNICODE")) {
    // Byte order follows the TIFF stream in theory; real files are mostly LE or BE
    // without a BOM. Pick the order whose high bytes look like ASCII text.
    std::size_t zeros_even = 0;
    std::size_t zeros_odd = 0;
    for (std::size_t i = 0; i < body.size(); ++i) {
      if (body[i] == 0) {
        (i % 2 == 0 ? zeros_even : zeros_odd)++;
      }
    }
    if (zeros_even > zeros_odd) {
      std::vector<std::uint8_t> swapped(body.begin(), body.end());
      for (std::size_t i = 0; i + 1 < swapped.size(); i += 2) {
        std::swap(swapped[i], swapped[i + 1]);
      }
      return utf16le_text(swapped);
    }
    return utf16le_text(body);
  }
  std::string text;
  for (const auto byte : body) {
    if (byte == 0) {
      break;
    }
    text.push_back(static_cast<char>(byte));
  }
  while (!text.empty() && text.back() == ' ') {
    text.pop_back();
  }
  return text;
}

std::string dms_text(const TiffStream& stream, const TiffEntry& entry) {
  const auto degrees = stream.real_value(entry, 0);
  const auto minutes = stream.real_value(entry, 1);
  const auto seconds = stream.real_value(entry, 2);
  if (!degrees || !minutes || !seconds) {
    return {};
  }
  return format_number(*degrees, 0) + " deg " + format_number(*minutes, 0) + "' " + format_number(*seconds, 2) +
         "\"";
}

std::optional<double> dms_decimal(const TiffStream& stream, std::uint16_t value_tag, std::uint16_t ref_tag,
                                  char negative_ref) {
  const auto* gps = stream.find(IfdKind::Gps);
  if (gps == nullptr) {
    return std::nullopt;
  }
  const auto* value = gps->find(value_tag);
  if (value == nullptr) {
    return std::nullopt;
  }
  const auto degrees = stream.real_value(*value, 0);
  const auto minutes = stream.real_value(*value, 1);
  const auto seconds = stream.real_value(*value, 2);
  if (!degrees) {
    return std::nullopt;
  }
  double result = *degrees + minutes.value_or(0.0) / 60.0 + seconds.value_or(0.0) / 3600.0;
  if (const auto* ref = gps->find(ref_tag); ref != nullptr && !ref->data.empty() &&
                                              static_cast<char>(ref->data.front()) == negative_ref) {
    result = -result;
  }
  return result;
}

std::string generic_value(const TiffStream& stream, const TiffEntry& entry) {
  const auto type = static_cast<TiffType>(entry.type);
  if (type == TiffType::Ascii) {
    return stream.string_value(entry);
  }
  if (type == TiffType::Undefined || ((type == TiffType::Byte || type == TiffType::SByte) && entry.count > 16)) {
    bool printable = !entry.data.empty();
    for (const auto byte : entry.data) {
      if (byte != 0 && (byte < 0x20 || byte >= 0x7F) && byte != '\n' && byte != '\r' && byte != '\t') {
        printable = false;
        break;
      }
    }
    if (printable && entry.data.size() <= 256) {
      return stream.string_value(entry);
    }
    return "(" + std::to_string(entry.data.size()) + " bytes of binary data)";
  }
  constexpr std::uint32_t kMaxShown = 16;
  std::string out;
  const auto shown = std::min(entry.count, kMaxShown);
  for (std::uint32_t i = 0; i < shown; ++i) {
    if (i > 0) {
      out += ' ';
    }
    if (type == TiffType::Rational || type == TiffType::SRational) {
      const auto value = stream.rational_value(entry, i);
      if (value && value->second != 0) {
        out += format_number(static_cast<double>(value->first) / static_cast<double>(value->second), 4);
      } else if (value) {
        out += std::to_string(value->first) + "/0";
      }
    } else if (type == TiffType::Float || type == TiffType::Double) {
      if (const auto value = stream.real_value(entry, i)) {
        out += format_number(*value, 6);
      }
    } else if (const auto value = stream.signed_value(entry, i)) {
      out += std::to_string(*value);
    }
  }
  if (entry.count > shown) {
    out += " ... (" + std::to_string(entry.count) + " values)";
  }
  return out;
}

}  // namespace

std::string_view exif_tag_name(IfdKind kind, std::uint16_t tag) noexcept {
  const auto* found = lookup(kind, tag);
  return found != nullptr ? found->name : std::string_view{};
}

std::optional<std::uint16_t> exif_tag_by_name(IfdKind kind, std::string_view name) noexcept {
  std::span<const TagName> table;
  switch (kind) {
    case IfdKind::Ifd0:
      table = kImageTags;
      break;
    case IfdKind::Exif:
      table = kExifTags;
      break;
    case IfdKind::Gps:
      table = kGpsTags;
      break;
    case IfdKind::Interop:
      table = kInteropTags;
      break;
    default:
      return std::nullopt;
  }
  for (const auto& entry : table) {
    if (entry.name == name) {
      return entry.tag;
    }
  }
  return std::nullopt;
}

std::string exif_tag_display_name(IfdKind kind, std::uint16_t tag) {
  const auto name = exif_tag_name(kind, tag);
  if (!name.empty()) {
    return std::string(name);
  }
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "Tag 0x%04X", static_cast<unsigned>(tag));
  return buffer;
}

PrivacyClass exif_tag_privacy(IfdKind kind, std::uint16_t tag) noexcept {
  if (kind == IfdKind::Gps) {
    return PrivacyClass::Location;
  }
  if (kind == IfdKind::Ifd1 && (tag == kTagJpegInterchangeFormat || tag == kTagJpegInterchangeFormatLength)) {
    return PrivacyClass::HiddenImage;
  }
  const auto* found = lookup(kind, tag);
  return found != nullptr ? found->privacy : PrivacyClass::None;
}

std::string_view ifd_kind_label(IfdKind kind) noexcept {
  switch (kind) {
    case IfdKind::Ifd0:
      return "IFD0";
    case IfdKind::Ifd1:
      return "IFD1 (thumbnail)";
    case IfdKind::Exif:
      return "Exif";
    case IfdKind::Gps:
      return "GPS";
    case IfdKind::Interop:
      return "Interop";
    case IfdKind::SubIfd:
      return "SubIFD";
    case IfdKind::Chained:
      return "Page IFD";
  }
  return "IFD";
}

std::string exif_date_display(std::string text) {
  const auto digits = [&text](std::size_t from, std::size_t count) {
    for (std::size_t i = from; i < from + count; ++i) {
      if (text[i] < '0' || text[i] > '9') {
        return false;
      }
    }
    return true;
  };
  const bool date = text.size() >= 10 && digits(0, 4) && text[4] == ':' && digits(5, 2) && text[7] == ':' &&
                    digits(8, 2);
  const bool date_only = date && text.size() == 10;
  const bool date_time = date && text.size() == 19 && text[10] == ' ' && digits(11, 2) && text[13] == ':' &&
                         digits(14, 2) && text[16] == ':' && digits(17, 2);
  if (date_only || date_time) {
    text[4] = '-';
    text[7] = '-';
  }
  return text;
}

std::string format_exif_value(const TiffStream& stream, IfdKind kind, const TiffEntry& entry) {
  const auto u = [&](std::uint32_t index = 0) { return stream.unsigned_value(entry, index); };
  const auto real = [&](std::uint32_t index = 0) { return stream.real_value(entry, index); };
  if (kind == IfdKind::Gps) {
    switch (entry.tag) {
      case 0:
        if (entry.data.size() == 4) {
          return std::to_string(entry.data[0]) + "." + std::to_string(entry.data[1]) + "." +
                 std::to_string(entry.data[2]) + "." + std::to_string(entry.data[3]);
        }
        break;
      case 2:
      case 4:
      case 20:
      case 22:
        if (const auto text = dms_text(stream, entry); !text.empty()) {
          return text;
        }
        break;
      case 5:
        return enum_text(kAltitudeRef, u());
      case 6:
        if (const auto value = real()) {
          return format_number(*value, 2) + " m";
        }
        break;
      case 7:
        if (const auto h = real(0), m = real(1), s = real(2); h && m && s) {
          char buffer[32];
          std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%05.2f UTC", static_cast<int>(*h), static_cast<int>(*m),
                        *s);
          return buffer;
        }
        break;
      case 27:
      case 28:
        return prefixed_comment_text(entry.data);
      case 29:
        return exif_date_display(generic_value(stream, entry));
      default:
        break;
    }
    return generic_value(stream, entry);
  }
  if (kind == IfdKind::Interop) {
    return generic_value(stream, entry);
  }
  switch (entry.tag) {
    case 259:
      return enum_text(kCompression, u());
    case 262:
      return enum_text(kPhotometric, u());
    case 274:
      return enum_text(kOrientation, u());
    case 282:
    case 283:
      if (const auto value = real()) {
        return format_number(*value, 2);
      }
      break;
    case 296:
    case 41488:
      return enum_text(kResolutionUnit, u());
    case 531:
      return enum_text(kYCbCrPositioning, u());
    case 306:
    case 36867:
    case 36868:
      return exif_date_display(generic_value(stream, entry));
    case 700:
    case 33723:
    case 34377:
    case 34675:
    case 37724:
    case 50341:
    case 50740:
    case 52545:
      return "(" + std::to_string(entry.data.size()) + " bytes)";
    case 40091:
    case 40092:
    case 40093:
    case 40094:
    case 40095:
      return utf16le_text(entry.data);
    case 33434:
      if (const auto value = real()) {
        return exposure_time_text(*value);
      }
      break;
    case 33437:
      if (const auto value = real()) {
        return "f/" + format_number(*value, 1);
      }
      break;
    case 34850:
      return enum_text(kExposureProgram, u());
    case 34864:
      return enum_text(kSensitivityType, u());
    case 36864:
    case 40960:
      if (auto text = version_text(entry); !text.empty()) {
        return text;
      }
      break;
    case 37121:
      if (entry.data.size() == 4) {
        static constexpr std::array<std::string_view, 7> kComponents{"-", "Y", "Cb", "Cr", "R", "G", "B"};
        std::string text;
        for (const auto component : entry.data) {
          if (component < kComponents.size()) {
            text += kComponents[component];
          }
        }
        return text;
      }
      break;
    case 37377:
      if (const auto apex = real()) {
        return exposure_time_text(std::pow(2.0, -*apex));
      }
      break;
    case 37378:
    case 37381:
      if (const auto apex = real()) {
        return "f/" + format_number(std::pow(std::sqrt(2.0), *apex), 1);
      }
      break;
    case 37380:
      if (const auto value = real()) {
        return (*value > 0 ? "+" : "") + format_number(*value, 2) + " EV";
      }
      break;
    case 37382:
      if (const auto value = real()) {
        return format_number(*value, 2) + " m";
      }
      break;
    case 37383:
      return enum_text(kMeteringMode, u());
    case 37384:
      return enum_text(kLightSource, u());
    case 37385:
      if (const auto value = u()) {
        return flash_text(*value);
      }
      break;
    case 37386:
      if (const auto value = real()) {
        return format_number(*value, 1) + " mm";
      }
      break;
    case 37500:
      return "(" + std::to_string(entry.data.size()) + " bytes, maker-specific)";
    case 37510:
      return prefixed_comment_text(entry.data);
    case 40961:
      return enum_text(kColorSpace, u());
    case 41495:
      return enum_text(kSensingMethod, u());
    case 41728:
      if (!entry.data.empty() && entry.data[0] == 3) {
        return "Digital Camera";
      }
      break;
    case 41729:
      if (!entry.data.empty() && entry.data[0] == 1) {
        return "Directly photographed";
      }
      break;
    case 41985:
      return enum_text(kCustomRendered, u());
    case 41986:
      return enum_text(kExposureMode, u());
    case 41987:
      return enum_text(kWhiteBalance, u());
    case 41989:
      if (const auto value = u()) {
        return std::to_string(*value) + " mm";
      }
      break;
    case 41990:
      return enum_text(kSceneCaptureType, u());
    case 41991:
      return enum_text(kGainControl, u());
    case 41992:
    case 41993:
      return enum_text(kContrast, u());
    case 41994:
      return enum_text(kSharpness, u());
    case 41996:
      return enum_text(kSubjectDistanceRange, u());
    case 42034:
      if (const auto a = real(0), b = real(1), c = real(2), d = real(3); a && b) {
        std::string text = format_number(*a, 1);
        if (*b != *a) {
          text += "-" + format_number(*b, 1);
        }
        text += " mm";
        if (c && d && std::isfinite(*c) && *c > 0.0) {
          text += " f/" + format_number(*c, 1);
          if (*d != *c) {
            text += "-" + format_number(*d, 1);
          }
        }
        return text;
      }
      break;
    default:
      break;
  }
  return generic_value(stream, entry);
}

std::optional<double> gps_latitude(const TiffStream& stream) {
  return dms_decimal(stream, exif_tag::kGpsLatitude, exif_tag::kGpsLatitudeRef, 'S');
}

std::optional<double> gps_longitude(const TiffStream& stream) {
  return dms_decimal(stream, exif_tag::kGpsLongitude, exif_tag::kGpsLongitudeRef, 'W');
}

std::optional<double> gps_altitude(const TiffStream& stream) {
  const auto* gps = stream.find(IfdKind::Gps);
  if (gps == nullptr) {
    return std::nullopt;
  }
  const auto* value = gps->find(exif_tag::kGpsAltitude);
  if (value == nullptr) {
    return std::nullopt;
  }
  auto altitude = stream.real_value(*value);
  if (!altitude) {
    return std::nullopt;
  }
  if (const auto* ref = gps->find(exif_tag::kGpsAltitudeRef); ref != nullptr) {
    if (const auto below = stream.unsigned_value(*ref); below && *below == 1) {
      *altitude = -*altitude;
    }
  }
  return altitude;
}

}  // namespace patchy::metadata
