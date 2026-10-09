#pragma once

#include "support/atomic_file_write.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A streaming PDF writer for pages that are one image each. Qt-free.
//
// Qt's PDF engine can only write an image as Flate RGB or as JPEG at a fixed quality,
// never as grayscale JPEG, and it cannot carry an already-encoded stream at all. This
// writer takes image streams that are ALREADY encoded (the caller picks the codec, or
// hands over the bytes an imported PDF page carried) and lays each one over its whole
// page, so a scanned multi-page document can be written at the size it came in at.
//
// Pages are written to disk as they arrive; only the object offsets are kept, so a
// file of any page count costs one page of memory. They go to a sibling temporary file
// (support/atomic_file_write.hpp) that replaces the destination only in finish(), so a
// crash, a full disk, or a cancel mid-export leaves an existing file untouched. Output
// is deterministic: no dates, no ids, numbers formatted without the C locale.

namespace patchy::pdf {

// One image XObject's stream and the dictionary entries that describe it.
struct ImageStream {
  // The encoded bytes exactly as they go between `stream` and `endstream`.
  std::vector<std::uint8_t> bytes;
  // Filter name without the slash: "DCTDecode", "FlateDecode", "JPXDecode". Empty
  // means the bytes are raw samples.
  std::string filter;
  // Verbatim PDF text for the optional entries, empty to omit each:
  // "<< /Predictor 15 /Columns 100 >>", "/DeviceGray" or "[/ICCBased ...]", "[1 0]".
  std::string decode_parms;
  std::string color_space;  // may be empty only for JPXDecode, which carries its own
  std::string decode;
  // An ICC profile (decoded bytes) and its component count. When set it IS the colour
  // space: the writer emits the profile stream once per distinct profile, shares it
  // across pages, and ignores `color_space`.
  std::shared_ptr<const std::vector<std::uint8_t>> icc_profile;
  int icc_components{0};
  int bits_per_component{8};
  int width{0};
  int height{0};
};

struct ImagePage {
  // Page size in points (1/72 inch), before `rotate`.
  double width_points{0.0};
  double height_points{0.0};
  // /Rotate: 0, 90, 180, or 270 degrees clockwise when displayed.
  int rotate{0};
  // Where the image's unit square lands on the page (a b c d e f). Unset draws the
  // image over the whole page; a page carried over from another PDF repeats the
  // placement it had there, which may be a little larger than the page.
  std::optional<std::array<double, 6>> image_matrix;
  ImageStream image;
  // An 8-bit DeviceGray alpha channel the size of the image, for pages with
  // transparency. Its color_space and bits_per_component are forced.
  std::optional<ImageStream> soft_mask;
};

class ImageWriter {
public:
  // Creates the temporary file beside `path` and emits the header; `path` itself is not
  // touched until finish(). Throws std::runtime_error when the file cannot be created.
  explicit ImageWriter(const std::filesystem::path& path);
  // An unfinished file (finish() never ran, or abort() did) is removed and `path` keeps
  // whatever it held before.
  ~ImageWriter();
  ImageWriter(const ImageWriter&) = delete;
  ImageWriter& operator=(const ImageWriter&) = delete;

  // Throws std::runtime_error on a bad page, a write failure, or a file that has
  // outgrown the classic cross-reference table's ten-digit offsets.
  void add_page(const ImagePage& page);
  // Writes the page tree, catalog, cross-reference table, and trailer, then renames the
  // finished file over `path`. At least one page must have been added.
  void finish();
  // Closes and removes the temporary file. Safe to call more than once.
  void abort() noexcept;

  [[nodiscard]] int page_count() const noexcept { return static_cast<int>(page_objects_.size()); }

private:
  std::uint32_t begin_object();
  void begin_reserved_object(std::uint32_t number);
  void write_image_object(const ImageStream& image, std::uint32_t soft_mask_object, bool is_soft_mask,
                          std::uint32_t icc_object);
  std::uint32_t icc_profile_object(const ImageStream& image);
  void write(std::string_view text);
  void write(const std::vector<std::uint8_t>& bytes);

  // Declared before file_ so the stream closes before the temporary is removed.
  AtomicFileReplacement replacement_;
  std::ofstream file_;
  std::uint64_t position_{0};
  // Byte offset of object N at index N; index 0 is the free-list head.
  std::vector<std::uint64_t> offsets_;
  std::vector<std::uint32_t> page_objects_;
  // Profiles already written, matched by content, so 85 pages of one scanner share one
  // object. A short list, not a map keyed on the bytes: GCC 13 reports a false
  // -Wstringop-overread through vector's operator<=>, and a file holds a handful at most.
  std::vector<std::pair<std::shared_ptr<const std::vector<std::uint8_t>>, std::uint32_t>> icc_objects_;
  bool finished_{false};
  bool open_{false};
};

// A PDF number: up to four decimals, no exponent, no trailing zeros, and a '.' decimal
// point whatever the process locale says.
[[nodiscard]] std::string format_pdf_number(double value);

}  // namespace patchy::pdf
