#include "formats/pdf_image_writer.hpp"

#include "support/translate_noop.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace patchy::pdf {
namespace {

// Objects 1 and 2 are reserved so every page can name its parent before the page tree
// exists; both are written by finish().
constexpr std::uint32_t kCatalogObject = 1;
constexpr std::uint32_t kPagesObject = 2;
// A classic cross-reference entry holds a ten-digit byte offset (ISO 32000-1 7.5.4).
constexpr std::uint64_t kMaximumOffset = 9'999'999'999ULL;
// ISO 32000-1 Annex C: a page is at most 14400 units on a side.
constexpr double kMaximumPagePoints = 14400.0;

std::string padded_offset(std::uint64_t offset) {
  std::string digits = std::to_string(offset);
  return std::string(10 - digits.size(), '0') + digits;
}

}  // namespace

std::string format_pdf_number(double value) {
  if (!std::isfinite(value)) {
    return "0";
  }
  const bool negative = value < 0.0;
  const auto scaled = static_cast<std::uint64_t>(std::llround(std::abs(value) * 10000.0));
  std::string text = std::to_string(scaled / 10000U);
  auto fraction = static_cast<unsigned>(scaled % 10000U);
  if (fraction != 0U) {
    std::string digits = std::to_string(fraction);
    digits.insert(0, 4 - digits.size(), '0');
    while (digits.back() == '0') {
      digits.pop_back();
    }
    text += '.';
    text += digits;
  }
  if (negative && scaled != 0U) {
    text.insert(0, 1, '-');
  }
  return text;
}

ImageWriter::ImageWriter(const std::filesystem::path& path) : replacement_(path) {
  file_.open(replacement_.temporary_path(), std::ios::binary | std::ios::trunc);
  if (!file_) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The PDF file could not be opened for writing."));
  }
  open_ = true;
  offsets_.assign(kPagesObject + 1U, 0U);
  // 1.7 covers JPXDecode (1.5) and soft masks (1.4). The comment line of high bytes
  // tells transfer programs the file is binary (clause 7.5.2).
  write("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
}

ImageWriter::~ImageWriter() {
  if (!finished_) {
    abort();
  }
}

void ImageWriter::abort() noexcept {
  if (!open_) {
    return;
  }
  open_ = false;
  file_.close();
  replacement_.discard();
}

void ImageWriter::write(std::string_view text) {
  file_.write(text.data(), static_cast<std::streamsize>(text.size()));
  position_ += text.size();
}

void ImageWriter::write(const std::vector<std::uint8_t>& bytes) {
  file_.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  position_ += bytes.size();
}

std::uint32_t ImageWriter::begin_object() {
  const auto number = static_cast<std::uint32_t>(offsets_.size());
  offsets_.push_back(position_);
  write(std::to_string(number) + " 0 obj\n");
  return number;
}

void ImageWriter::begin_reserved_object(std::uint32_t number) {
  offsets_[number] = position_;
  write(std::to_string(number) + " 0 obj\n");
}

std::uint32_t ImageWriter::icc_profile_object(const ImageStream& image) {
  if (image.icc_profile == nullptr || image.icc_profile->empty()) {
    return 0U;
  }
  const auto& profile = *image.icc_profile;
  for (const auto& [written, number] : icc_objects_) {
    if (written == image.icc_profile ||
        (written->size() == profile.size() && std::memcmp(written->data(), profile.data(), profile.size()) == 0)) {
      return number;
    }
  }
  const auto number = begin_object();
  write("<< /N " + std::to_string(image.icc_components) + " /Length " + std::to_string(image.icc_profile->size()) +
        " >>\nstream\n");
  write(*image.icc_profile);
  write("\nendstream\nendobj\n");
  icc_objects_.emplace_back(image.icc_profile, number);
  return number;
}

void ImageWriter::write_image_object(const ImageStream& image, std::uint32_t soft_mask_object, bool is_soft_mask,
                                     std::uint32_t icc_object) {
  std::string dict = "<< /Type /XObject /Subtype /Image /Width " + std::to_string(image.width) + " /Height " +
                     std::to_string(image.height);
  if (is_soft_mask) {
    dict += " /ColorSpace /DeviceGray /BitsPerComponent 8";
  } else {
    if (icc_object != 0U) {
      dict += " /ColorSpace [/ICCBased " + std::to_string(icc_object) + " 0 R]";
    } else if (!image.color_space.empty()) {
      dict += " /ColorSpace " + image.color_space;
    }
    // JPXDecode ignores the entry (the codestream carries its own depth), but writing
    // it is allowed and keeps simple readers happy.
    dict += " /BitsPerComponent " + std::to_string(image.bits_per_component);
  }
  if (!image.decode.empty()) {
    dict += " /Decode " + image.decode;
  }
  if (!image.filter.empty()) {
    dict += " /Filter /" + image.filter;
  }
  if (!image.decode_parms.empty()) {
    dict += " /DecodeParms " + image.decode_parms;
  }
  if (soft_mask_object != 0U) {
    dict += " /SMask " + std::to_string(soft_mask_object) + " 0 R";
  }
  dict += " /Length " + std::to_string(image.bytes.size()) + " >>\nstream\n";
  write(dict);
  write(image.bytes);
  write("\nendstream\nendobj\n");
}

void ImageWriter::add_page(const ImagePage& page) {
  if (!open_ || finished_) {
    // A caller bug (the writer was finished or aborted), reported like any write failure.
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The PDF file could not be written."));
  }
  const auto& image = page.image;
  const bool has_profile = image.icc_profile != nullptr && !image.icc_profile->empty();
  if (image.width <= 0 || image.height <= 0 || image.bytes.empty() || !(page.width_points > 0.0) ||
      !(page.height_points > 0.0) ||
      (page.rotate != 0 && page.rotate != 90 && page.rotate != 180 && page.rotate != 270) ||
      (has_profile && image.icc_components != 1 && image.icc_components != 3) ||
      (image.color_space.empty() && !has_profile && image.filter != "JPXDecode")) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The document could not be rendered for PDF export."));
  }
  if (page.soft_mask.has_value() &&
      (page.soft_mask->width <= 0 || page.soft_mask->height <= 0 || page.soft_mask->bytes.empty())) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The document could not be rendered for PDF export."));
  }
  const double width = std::min(page.width_points, kMaximumPagePoints);
  const double height = std::min(page.height_points, kMaximumPagePoints);

  std::uint32_t soft_mask_object = 0;
  if (page.soft_mask.has_value()) {
    soft_mask_object = begin_object();
    write_image_object(*page.soft_mask, 0U, true, 0U);
  }
  const auto icc_object = icc_profile_object(image);
  const auto image_object = begin_object();
  write_image_object(image, soft_mask_object, false, icc_object);

  // The unit square of image space, scaled to the page (the image covers it exactly),
  // or the placement the caller carried over.
  std::string content = "q ";
  if (page.image_matrix.has_value()) {
    for (const double value : *page.image_matrix) {
      content += format_pdf_number(value) + " ";
    }
  } else {
    content += format_pdf_number(width) + " 0 0 " + format_pdf_number(height) + " 0 0 ";
  }
  content += "cm /Im0 Do Q\n";
  const auto content_object = begin_object();
  write("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream\nendobj\n");

  const auto page_object = begin_object();
  write("<< /Type /Page /Parent " + std::to_string(kPagesObject) + " 0 R /MediaBox [0 0 " + format_pdf_number(width) +
        " " + format_pdf_number(height) + "]" + (page.rotate != 0 ? " /Rotate " + std::to_string(page.rotate) : "") +
        " /Resources << /XObject << /Im0 " + std::to_string(image_object) + " 0 R >> >> /Contents " +
        std::to_string(content_object) + " 0 R >>\nendobj\n");
  page_objects_.push_back(page_object);

  if (!file_) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The PDF file could not be written."));
  }
  if (position_ > kMaximumOffset) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The PDF would be too large to write (over 9 GB)."));
  }
}

void ImageWriter::finish() {
  if (!open_ || finished_) {
    // A caller bug (the writer was finished or aborted), reported like any write failure.
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The PDF file could not be written."));
  }
  if (page_objects_.empty()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "There are no pages to export."));
  }
  begin_reserved_object(kPagesObject);
  std::string kids;
  for (const auto page_object : page_objects_) {
    kids += std::to_string(page_object) + " 0 R ";
  }
  write("<< /Type /Pages /Count " + std::to_string(page_objects_.size()) + " /Kids [ " + kids + "] >>\nendobj\n");
  begin_reserved_object(kCatalogObject);
  write("<< /Type /Catalog /Pages " + std::to_string(kPagesObject) + " 0 R >>\nendobj\n");
  const auto info_object = begin_object();
  write("<< /Creator (Patchy) /Producer (Patchy) >>\nendobj\n");

  if (position_ > kMaximumOffset) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The PDF would be too large to write (over 9 GB)."));
  }
  const auto xref_position = position_;
  // Every entry is exactly 20 bytes: offset, generation, keyword, and a two-byte EOL.
  std::string xref = "xref\n0 " + std::to_string(offsets_.size()) + "\n0000000000 65535 f \n";
  for (std::size_t number = 1; number < offsets_.size(); ++number) {
    xref += padded_offset(offsets_[number]) + " 00000 n \n";
  }
  write(xref);
  write("trailer\n<< /Size " + std::to_string(offsets_.size()) + " /Root " + std::to_string(kCatalogObject) +
        " 0 R /Info " + std::to_string(info_object) + " 0 R >>\nstartxref\n" + std::to_string(xref_position) +
        "\n%%EOF\n");
  file_.flush();
  file_.close();
  if (file_.fail()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The PDF file could not be written."));
  }
  open_ = false;
  replacement_.commit(PATCHY_TRANSLATE_NOOP("QObject", "The PDF file could not be written."));
  finished_ = true;
}

}  // namespace patchy::pdf
