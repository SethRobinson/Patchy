#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// TIFF image-file-directory reader and writer for metadata (docs/file-info.md).
// EXIF is a TIFF stream wherever it lives (JPEG APP1, PNG eXIf, WebP EXIF, HEIF Exif
// item, PSD resource 1058), and TIFF files and TIFF-based camera raws carry their
// descriptive tags the same way. Qt-free; hardened against hostile offsets (cycles,
// overlapping or out-of-range data, absurd counts): a malformed directory ends the
// walk, never the read.
namespace patchy::metadata {

enum class TiffType : std::uint16_t {
  Byte = 1,
  Ascii = 2,
  Short = 3,
  Long = 4,
  Rational = 5,
  SByte = 6,
  Undefined = 7,
  SShort = 8,
  SLong = 9,
  SRational = 10,
  Float = 11,
  Double = 12,
  Ifd = 13,
};

// Byte size of one value of `type`, 0 for an unknown type.
[[nodiscard]] std::uint32_t tiff_type_size(std::uint16_t type) noexcept;

// Where a directory sits in the EXIF tree. Each kind has its own tag namespace
// (tag 1 is InteropIndex in Interop, GPSLatitudeRef in GPS).
enum class IfdKind : std::uint8_t {
  Ifd0,      // primary image (TIFF baseline + pointers)
  Ifd1,      // EXIF thumbnail directory
  Exif,      // tag 34665
  Gps,       // tag 34853
  Interop,   // tag 40965 inside Exif
  SubIfd,    // tag 330 (raw previews); read only
  Chained,   // IFD2+ in a multi-page TIFF; read only
};

struct TiffEntry {
  std::uint16_t tag{0};
  std::uint16_t type{0};
  std::uint32_t count{0};
  // Raw value bytes, in the stream's byte order (copied verbatim on write, which
  // keeps opaque blobs such as the MakerNote intact).
  std::vector<std::uint8_t> data;
};

struct TiffIfd {
  IfdKind kind{IfdKind::Ifd0};
  std::vector<TiffEntry> entries;

  [[nodiscard]] const TiffEntry* find(std::uint16_t tag) const noexcept;
  [[nodiscard]] TiffEntry* find(std::uint16_t tag) noexcept;
  void erase(std::uint16_t tag);
};

struct TiffStream {
  bool big_endian{false};
  std::vector<TiffIfd> ifds;
  // The IFD1 thumbnail (JPEGInterchangeFormat/Length), copied out.
  std::vector<std::uint8_t> thumbnail;
  // True when the walk stopped early on a malformed directory or offset.
  bool truncated{false};

  [[nodiscard]] const TiffIfd* find(IfdKind kind) const noexcept;
  [[nodiscard]] TiffIfd* find(IfdKind kind) noexcept;
  TiffIfd& find_or_add(IfdKind kind);

  // Typed reads that honor the stream's byte order. Out-of-range indexes and
  // mismatched types return nullopt.
  [[nodiscard]] std::optional<std::uint32_t> unsigned_value(const TiffEntry& entry, std::uint32_t index = 0) const;
  [[nodiscard]] std::optional<std::int64_t> signed_value(const TiffEntry& entry, std::uint32_t index = 0) const;
  // Rationals as {numerator, denominator}; also accepts signed rationals.
  [[nodiscard]] std::optional<std::pair<std::int64_t, std::int64_t>> rational_value(const TiffEntry& entry,
                                                                                  std::uint32_t index = 0) const;
  [[nodiscard]] std::optional<double> real_value(const TiffEntry& entry, std::uint32_t index = 0) const;
  // ASCII (trailing NULs and padding removed; embedded NULs separate values, joined
  // with "; "). UNDEFINED/BYTE data is treated as text too.
  [[nodiscard]] std::string string_value(const TiffEntry& entry) const;

  // Writers that encode in the stream's byte order and replace any existing entry.
  void set_ascii(IfdKind kind, std::uint16_t tag, const std::string& value);
  void set_short(IfdKind kind, std::uint16_t tag, std::uint16_t value);
  void set_long(IfdKind kind, std::uint16_t tag, std::uint32_t value);
  void set_rational(IfdKind kind, std::uint16_t tag, std::uint32_t numerator, std::uint32_t denominator);
  void set_undefined(IfdKind kind, std::uint16_t tag, std::span<const std::uint8_t> value);
};

// Well-known pointer and structure tags.
constexpr std::uint16_t kTagSubIfds = 330;
constexpr std::uint16_t kTagJpegInterchangeFormat = 513;
constexpr std::uint16_t kTagJpegInterchangeFormatLength = 514;
constexpr std::uint16_t kTagExifIfd = 34665;
constexpr std::uint16_t kTagGpsIfd = 34853;
constexpr std::uint16_t kTagInteropIfd = 40965;

// Tags that describe a TIFF file's image data rather than the image (dimensions,
// strips, tiles, embedded XMP/IPTC/ICC blocks). They stay out of the EXIF slot,
// which other formats write back as their EXIF block.
[[nodiscard]] bool is_tiff_structure_tag(std::uint16_t tag) noexcept;

struct TiffParseOptions {
  // Follow IFD0's next-IFD chain past IFD1 (multi-page TIFF files).
  bool follow_page_chain{false};
  // Follow tag 330 sub-IFDs (camera raw previews).
  bool follow_sub_ifds{false};
};

// Parses a TIFF stream beginning with "II*\0" / "MM\0*" (also the raw variants with
// magic 0x55 (RW2) and "RO"/"RS" (ORF)). nullopt when the header is not TIFF.
[[nodiscard]] std::optional<TiffStream> parse_tiff(std::span<const std::uint8_t> bytes,
                                                   const TiffParseOptions& options = {});

// Rebuilds an EXIF-style stream: IFD0 (+ Exif + GPS + Interop) and IFD1 with the
// thumbnail when present. Pointer tags are regenerated from the directories that
// exist; SubIfd/Chained directories and strip/tile data are not written. Entries are
// sorted by tag, as TIFF requires.
[[nodiscard]] std::vector<std::uint8_t> write_tiff(const TiffStream& stream);

}  // namespace patchy::metadata
