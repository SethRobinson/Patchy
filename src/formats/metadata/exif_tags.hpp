#pragma once

#include "formats/metadata/metadata_privacy.hpp"
#include "formats/metadata/tiff_ifd.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// EXIF / TIFF tag names and value formatting (docs/file-info.md). Names are the
// specification identifiers (ExposureTime, GPSLatitude), shown untranslated like
// ExifTool does; values are formatted the way photographers read them
// ("1/250 s", "f/2.8", "35 mm", decimal GPS degrees).
namespace patchy::metadata {

// The tag's specification name, or empty when unknown.
[[nodiscard]] std::string_view exif_tag_name(IfdKind kind, std::uint16_t tag) noexcept;

// The tag with this specification name in the directory's own table (IFD0, Exif,
// GPS or Interop), or nullopt.
[[nodiscard]] std::optional<std::uint16_t> exif_tag_by_name(IfdKind kind, std::string_view name) noexcept;
// Display name: the specification name, or "Tag 0x1234" when unknown.
[[nodiscard]] std::string exif_tag_display_name(IfdKind kind, std::uint16_t tag);

[[nodiscard]] PrivacyClass exif_tag_privacy(IfdKind kind, std::uint16_t tag) noexcept;

// Group label for a directory ("IFD0", "Exif", "GPS", "Interop", "IFD1").
[[nodiscard]] std::string_view ifd_kind_label(IfdKind kind) noexcept;

// Human-readable value. Enumerations decode to their specification text,
// rationals to the usual photographic units, and long binary blobs to a size.
[[nodiscard]] std::string format_exif_value(const TiffStream& stream, IfdKind kind, const TiffEntry& entry);

// GPS latitude/longitude in signed decimal degrees (south/west negative).
[[nodiscard]] std::optional<double> gps_latitude(const TiffStream& stream);
[[nodiscard]] std::optional<double> gps_longitude(const TiffStream& stream);
// Altitude in meters (negative below sea level).
[[nodiscard]] std::optional<double> gps_altitude(const TiffStream& stream);

// Well-known tags File Info reads or rewrites.
namespace exif_tag {
constexpr std::uint16_t kImageDescription = 270;
constexpr std::uint16_t kMake = 271;
constexpr std::uint16_t kModel = 272;
constexpr std::uint16_t kOrientation = 274;
constexpr std::uint16_t kXResolution = 282;
constexpr std::uint16_t kYResolution = 283;
constexpr std::uint16_t kResolutionUnit = 296;
constexpr std::uint16_t kSoftware = 305;
constexpr std::uint16_t kDateTime = 306;
constexpr std::uint16_t kArtist = 315;
constexpr std::uint16_t kHostComputer = 316;
constexpr std::uint16_t kXmp = 700;
constexpr std::uint16_t kCopyright = 33432;
constexpr std::uint16_t kIptc = 33723;
constexpr std::uint16_t kPhotoshopIrb = 34377;
constexpr std::uint16_t kIccProfile = 34675;
constexpr std::uint16_t kExposureTime = 33434;
constexpr std::uint16_t kFNumber = 33437;
constexpr std::uint16_t kExposureProgram = 34850;
constexpr std::uint16_t kIsoSpeed = 34855;
constexpr std::uint16_t kExifVersion = 36864;
constexpr std::uint16_t kDateTimeOriginal = 36867;
constexpr std::uint16_t kDateTimeDigitized = 36868;
constexpr std::uint16_t kOffsetTime = 36880;
constexpr std::uint16_t kOffsetTimeOriginal = 36881;
constexpr std::uint16_t kExposureBias = 37380;
constexpr std::uint16_t kMeteringMode = 37383;
constexpr std::uint16_t kFlash = 37385;
constexpr std::uint16_t kFocalLength = 37386;
constexpr std::uint16_t kMakerNote = 37500;
constexpr std::uint16_t kUserComment = 37510;
constexpr std::uint16_t kColorSpace = 40961;
constexpr std::uint16_t kPixelXDimension = 40962;
constexpr std::uint16_t kPixelYDimension = 40963;
constexpr std::uint16_t kWhiteBalance = 41987;
constexpr std::uint16_t kFocalLength35mm = 41989;
constexpr std::uint16_t kCameraOwnerName = 42032;
constexpr std::uint16_t kBodySerialNumber = 42033;
constexpr std::uint16_t kLensSpecification = 42034;
constexpr std::uint16_t kLensMake = 42035;
constexpr std::uint16_t kLensModel = 42036;
constexpr std::uint16_t kLensSerialNumber = 42037;
constexpr std::uint16_t kGpsLatitudeRef = 1;
constexpr std::uint16_t kGpsLatitude = 2;
constexpr std::uint16_t kGpsLongitudeRef = 3;
constexpr std::uint16_t kGpsLongitude = 4;
constexpr std::uint16_t kGpsAltitudeRef = 5;
constexpr std::uint16_t kGpsAltitude = 6;
constexpr std::uint16_t kGpsImgDirection = 17;
}  // namespace exif_tag

}  // namespace patchy::metadata
