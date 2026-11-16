#include "formats/pdf_image_writer.hpp"

#include "formats/pdf_hidden_text_font.hpp"
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

constexpr char kHexDigits[] = "0123456789ABCDEF";

void append_hex16(std::string& out, std::uint32_t value) {
  for (int shift = 12; shift >= 0; shift -= 4) {
    out.push_back(kHexDigits[(value >> shift) & 0xFU]);
  }
}

// The code points of UTF-8 text; a malformed byte reads as U+FFFD.
std::vector<char32_t> code_points(std::string_view utf8) {
  std::vector<char32_t> out;
  for (std::size_t i = 0; i < utf8.size();) {
    const auto c = static_cast<unsigned char>(utf8[i]);
    char32_t code = 0xFFFD;
    std::size_t length = 1;
    const auto tail = [&](std::size_t offset) {
      return static_cast<char32_t>(static_cast<unsigned char>(utf8[i + offset]) & 0x3FU);
    };
    if (c < 0x80) {
      code = c;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
      code = (static_cast<char32_t>(c & 0x1FU) << 6) | tail(1);
      length = 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
      code = (static_cast<char32_t>(c & 0x0FU) << 12) | (tail(1) << 6) | tail(2);
      length = 3;
    } else if ((c & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
      code = (static_cast<char32_t>(c & 0x07U) << 18) | (tail(1) << 12) | (tail(2) << 6) | tail(3);
      length = 4;
    }
    i += length;
    out.push_back(code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF) ? char32_t{0xFFFD} : code);
  }
  return out;
}

// The hidden-text font's codes are two bytes: at most this many distinct characters.
constexpr std::size_t kMaximumHiddenCharacters = 0xFFFEU;

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
  // A CMYK export names DeviceCMYK for readers without color management; gray and RGB
  // profiles keep the dictionary they always had.
  write("<< /N " + std::to_string(image.icc_components) +
        (image.icc_components == 4 ? " /Alternate /DeviceCMYK" : "") + " /Length " +
        std::to_string(image.icc_profile->size()) + " >>\nstream\n");
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
      (has_profile && image.icc_components != 1 && image.icc_components != 3 && image.icc_components != 4) ||
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
  const auto text_content = hidden_text_content(page.text);
  content += text_content;
  const auto content_object = begin_object();
  write("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream\nendobj\n");

  const auto page_object = begin_object();
  write("<< /Type /Page /Parent " + std::to_string(kPagesObject) + " 0 R /MediaBox [0 0 " + format_pdf_number(width) +
        " " + format_pdf_number(height) + "]" + (page.rotate != 0 ? " /Rotate " + std::to_string(page.rotate) : "") +
        " /Resources << /XObject << /Im0 " + std::to_string(image_object) + " 0 R >>" +
        (text_content.empty() ? std::string() : " /Font << /F0 " + std::to_string(hidden_font_object_) + " 0 R >>") +
        " >> /Contents " + std::to_string(content_object) + " 0 R >>\nendobj\n");
  page_objects_.push_back(page_object);

  if (!file_) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The PDF file could not be written."));
  }
  if (position_ > kMaximumOffset) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The PDF would be too large to write (over 9 GB)."));
  }
}

std::string ImageWriter::hidden_text_content(const std::vector<TextLine>& lines) {
  std::string content;
  for (const auto& line : lines) {
    if (!(line.width > 0.0) || !(line.size > 0.0) || !std::isfinite(line.x) || !std::isfinite(line.y) ||
        !std::isfinite(line.angle_degrees)) {
      continue;
    }
    std::string codes;
    std::size_t count = 0;
    for (const auto character : code_points(line.utf8)) {
      if (character < 0x20U || character == 0x7FU) {
        continue;  // nothing a reader could find
      }
      auto found = std::find(hidden_characters_.begin(), hidden_characters_.end(), character);
      if (found == hidden_characters_.end()) {
        if (hidden_characters_.size() >= kMaximumHiddenCharacters) {
          continue;
        }
        hidden_characters_.push_back(character);
        found = hidden_characters_.end() - 1;
      }
      append_hex16(codes, static_cast<std::uint32_t>(found - hidden_characters_.begin()) + 1U);
      ++count;
    }
    if (count == 0) {
      continue;
    }
    if (hidden_font_object_ == 0U) {
      // Reserved now so this page can name it; written by finish().
      hidden_font_object_ = static_cast<std::uint32_t>(offsets_.size());
      offsets_.push_back(0U);
    }
    // Every glyph of the font is one em wide, so the run is `count` sizes long before the
    // horizontal scale stretches it to the line. One Tj for the whole line.
    const double radians = line.angle_degrees * 3.14159265358979323846 / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    content += "/F0 " + format_pdf_number(line.size) + " Tf " +
               format_pdf_number(100.0 * line.width / (static_cast<double>(count) * line.size)) + " Tz " +
               format_pdf_number(cosine) + " " + format_pdf_number(sine) + " " + format_pdf_number(-sine) + " " +
               format_pdf_number(cosine) + " " + format_pdf_number(line.x) + " " + format_pdf_number(line.y) +
               " Tm <" + codes + "> Tj\n";
  }
  // Render mode 3: neither filled nor stroked.
  return content.empty() ? content : "BT 3 Tr\n" + content + "ET\n";
}

void ImageWriter::write_hidden_text_font() {
  const auto font_file = hidden_text_font();
  const auto file_object = begin_object();
  write("<< /Length " + std::to_string(font_file.size()) + " /Length1 " + std::to_string(font_file.size()) +
        " >>\nstream\n");
  write(font_file);
  write("\nendstream\nendobj\n");

  const auto descriptor_object = begin_object();
  write("<< /Type /FontDescriptor /FontName /PatchyHiddenText /Flags 4 /FontBBox [0 0 1000 1000] /ItalicAngle 0 "
        "/Ascent 1000 /Descent 0 /CapHeight 1000 /StemV 80 /FontFile2 " +
        std::to_string(file_object) + " 0 R >>\nendobj\n");

  // Every code draws the font's one glyph.
  const std::vector<std::uint8_t> glyph_map((hidden_characters_.size() + 1U) * 2U, 0U);
  const auto map_object = begin_object();
  write("<< /Length " + std::to_string(glyph_map.size()) + " >>\nstream\n");
  write(glyph_map);
  write("\nendstream\nendobj\n");

  const auto descendant_object = begin_object();
  write("<< /Type /Font /Subtype /CIDFontType2 /BaseFont /PatchyHiddenText /CIDSystemInfo << /Registry (Adobe) "
        "/Ordering (Identity) /Supplement 0 >> /FontDescriptor " +
        std::to_string(descriptor_object) + " 0 R /DW 1000 /W [1 " + std::to_string(hidden_characters_.size()) +
        " 1000] /CIDToGIDMap " + std::to_string(map_object) + " 0 R >>\nendobj\n");

  // What each code means, so a reader can search, select and copy the text.
  std::string cmap = "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
                     "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
                     "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
                     "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
  for (std::size_t start = 0; start < hidden_characters_.size(); start += 100U) {
    const auto end = std::min(hidden_characters_.size(), start + 100U);
    cmap += std::to_string(end - start) + " beginbfchar\n";
    for (std::size_t index = start; index < end; ++index) {
      cmap += "<";
      append_hex16(cmap, static_cast<std::uint32_t>(index) + 1U);
      cmap += "> <";
      auto character = static_cast<std::uint32_t>(hidden_characters_[index]);
      if (character >= 0x10000U) {
        character -= 0x10000U;
        append_hex16(cmap, 0xD800U + (character >> 10U));
        append_hex16(cmap, 0xDC00U + (character & 0x3FFU));
      } else {
        append_hex16(cmap, character);
      }
      cmap += ">\n";
    }
    cmap += "endbfchar\n";
  }
  cmap += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
  const auto unicode_object = begin_object();
  write("<< /Length " + std::to_string(cmap.size()) + " >>\nstream\n" + cmap + "endstream\nendobj\n");

  begin_reserved_object(hidden_font_object_);
  write("<< /Type /Font /Subtype /Type0 /BaseFont /PatchyHiddenText /Encoding /Identity-H /DescendantFonts [" +
        std::to_string(descendant_object) + " 0 R] /ToUnicode " + std::to_string(unicode_object) +
        " 0 R >>\nendobj\n");
}

namespace {

// A PDF text string for /Info: UTF-16BE with a byte-order mark, written as a hex string so
// no character needs escaping (ISO 32000-1 7.9.2.2).
std::string utf16_hex_string(const std::string& utf8) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out = "<FEFF";
  const auto unit = [&out](std::uint32_t value) {
    for (int shift = 12; shift >= 0; shift -= 4) {
      out.push_back(kHex[(value >> shift) & 0xF]);
    }
  };
  for (std::size_t i = 0; i < utf8.size();) {
    const auto c = static_cast<unsigned char>(utf8[i]);
    std::uint32_t code = 0xFFFD;
    std::size_t length = 1;
    if (c < 0x80) {
      code = c;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
      code = ((c & 0x1FU) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3FU);
      length = 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
      code = ((c & 0x0FU) << 12) | ((static_cast<unsigned char>(utf8[i + 1]) & 0x3FU) << 6) |
             (static_cast<unsigned char>(utf8[i + 2]) & 0x3FU);
      length = 3;
    } else if ((c & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
      code = ((c & 0x07U) << 18) | ((static_cast<unsigned char>(utf8[i + 1]) & 0x3FU) << 12) |
             ((static_cast<unsigned char>(utf8[i + 2]) & 0x3FU) << 6) |
             (static_cast<unsigned char>(utf8[i + 3]) & 0x3FU);
      length = 4;
    }
    i += length;
    if (code >= 0x10000U) {
      code -= 0x10000U;
      unit(0xD800U + (code >> 10));
      unit(0xDC00U + (code & 0x3FFU));
    } else {
      unit(code);
    }
  }
  return out + ">";
}

}  // namespace

void ImageWriter::set_document_info(std::vector<std::pair<std::string, std::string>> entries) {
  document_info_ = std::move(entries);
}

void ImageWriter::set_output_intent(std::shared_ptr<const std::vector<std::uint8_t>> profile, int components,
                                    std::string condition) {
  output_intent_profile_ = std::move(profile);
  output_intent_components_ = components;
  output_intent_condition_ = std::move(condition);
}

void ImageWriter::finish() {
  if (!open_ || finished_) {
    // A caller bug (the writer was finished or aborted), reported like any write failure.
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "The PDF file could not be written."));
  }
  if (page_objects_.empty()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "There are no pages to export."));
  }
  if (hidden_font_object_ != 0U) {
    write_hidden_text_font();
  }
  begin_reserved_object(kPagesObject);
  std::string kids;
  for (const auto page_object : page_objects_) {
    kids += std::to_string(page_object) + " 0 R ";
  }
  write("<< /Type /Pages /Count " + std::to_string(page_objects_.size()) + " /Kids [ " + kids + "] >>\nendobj\n");
  std::string output_intents;
  if (output_intent_profile_ != nullptr && !output_intent_profile_->empty()) {
    ImageStream profile_holder;
    profile_holder.icc_profile = output_intent_profile_;
    profile_holder.icc_components = output_intent_components_;
    const auto profile_object = icc_profile_object(profile_holder);
    // "Custom": the profile is not one of the registered characterizations by name.
    output_intents = " /OutputIntents [<< /Type /OutputIntent /S /GTS_PDFX /OutputConditionIdentifier (Custom)";
    if (!output_intent_condition_.empty()) {
      output_intents += " /Info " + utf16_hex_string(output_intent_condition_);
    }
    output_intents += " /DestOutputProfile " + std::to_string(profile_object) + " 0 R >>]";
  }
  begin_reserved_object(kCatalogObject);
  write("<< /Type /Catalog /Pages " + std::to_string(kPagesObject) + " 0 R" + output_intents + " >>\nendobj\n");
  const auto info_object = begin_object();
  std::string info = "<< /Creator (Patchy) /Producer (Patchy)";
  for (const auto& [key, value] : document_info_) {
    if (!value.empty() && !key.empty()) {
      info += " /" + key + " " + utf16_hex_string(value);
    }
  }
  write(info + " >>\nendobj\n");

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
