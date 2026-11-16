// Invisible text in the PDF image writer (docs/pdf-searchable-text.md): the font built for
// it, the objects and operators a page with text gets, the text read back through Patchy's
// own PDF reader (which shares no code with the writer), and the bytes of pages without any.

#include "formats/pdf_content.hpp"
#include "formats/pdf_document_io.hpp"
#include "formats/pdf_file.hpp"
#include "formats/pdf_crypt.hpp"
#include "formats/pdf_hidden_text_font.hpp"
#include "formats/pdf_image_writer.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> read_whole(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string text_of(const std::vector<std::uint8_t>& data) {
  return std::string(data.begin(), data.end());
}

std::string hex_of(std::span<const std::uint8_t> bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  for (const auto byte : bytes) {
    out.push_back(kDigits[byte >> 4U]);
    out.push_back(kDigits[byte & 0xFU]);
  }
  return out;
}

std::uint32_t be32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
  return (static_cast<std::uint32_t>(bytes[at]) << 24U) | (static_cast<std::uint32_t>(bytes[at + 1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[at + 2]) << 8U) | bytes[at + 3];
}

std::uint32_t be16(const std::vector<std::uint8_t>& bytes, std::size_t at) {
  return (static_cast<std::uint32_t>(bytes[at]) << 8U) | bytes[at + 1];
}

std::uint32_t word_sum(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::size_t length) {
  std::uint32_t sum = 0;
  for (std::size_t index = 0; index < length; index += 4) {
    std::uint32_t word = 0;
    for (std::size_t byte = 0; byte < 4; ++byte) {
      word = (word << 8U) | (index + byte < length ? bytes[offset + index + byte] : 0U);
    }
    sum += word;
  }
  return sum;
}

patchy::pdf::ImageStream fake_jpeg(int width, int height, std::uint8_t seed) {
  patchy::pdf::ImageStream image;
  image.width = width;
  image.height = height;
  image.filter = "DCTDecode";
  image.color_space = "/DeviceGray";
  image.bytes = {0xFF, 0xD8, seed, 0x0A, 0x0D, 0x00, 0x25, 0xFF, 0xD9};
  return image;
}

patchy::pdf::ImagePage scan_page(std::uint8_t seed) {
  patchy::pdf::ImagePage page;
  page.width_points = 300.0;
  page.height_points = 200.0;
  page.image = fake_jpeg(600, 400, seed);
  return page;
}

// Records what a reader would find on a page: the invisible runs, and anything drawn.
struct TextSink final : patchy::pdf::ContentSink {
  std::vector<patchy::pdf::TextRun> hidden;
  std::vector<patchy::pdf::TextRun> visible;
  int images{0};
  std::vector<std::string> notices;

  void on_path(const patchy::pdf::PaintedPath&) override {}
  void on_text(const patchy::pdf::TextRun& run) override { visible.push_back(run); }
  void on_hidden_text(const patchy::pdf::TextRun& run) override { hidden.push_back(run); }
  void on_image(const patchy::pdf::PlacedImage&) override { ++images; }
  void on_shading(std::shared_ptr<const patchy::pdf::ResolvedShading>, const patchy::VectorPath&) override {}
  void on_notice(const std::string& text) override { notices.push_back(text); }
};

void pdf_hidden_text_font_is_a_well_formed_truetype_file() {
  const auto font = patchy::pdf::hidden_text_font();
  CHECK(font.size() == 276);
  CHECK(font == patchy::pdf::hidden_text_font());  // the same bytes every time
  if (font.size() != 276) {
    return;
  }
  CHECK(be32(font, 0) == 0x00010000U);
  CHECK(be16(font, 4) == 6);
  // The directory: six tables in tag order, each inside the file, on a 4-byte boundary, with
  // the checksum of its own bytes.
  const std::vector<std::string> expected{"glyf", "head", "hhea", "hmtx", "loca", "maxp"};
  std::size_t head = 0;
  std::size_t maxp = 0;
  std::size_t glyf = 0;
  std::size_t glyf_length = 0;
  std::size_t loca = 0;
  for (std::size_t index = 0; index < 6; ++index) {
    const std::size_t entry = 12 + index * 16;
    CHECK(std::string(font.begin() + static_cast<std::ptrdiff_t>(entry),
                      font.begin() + static_cast<std::ptrdiff_t>(entry + 4)) == expected[index]);
    const std::size_t offset = be32(font, entry + 8);
    const std::size_t length = be32(font, entry + 12);
    CHECK(offset % 4 == 0 && offset + length <= font.size());
    if (offset + length > font.size()) {
      return;
    }
    if (expected[index] == "head") {
      head = offset;
      // head's checksum is taken with its adjustment field as zero.
      CHECK(be32(font, entry + 4) == word_sum(font, offset, length) - be32(font, offset + 8));
    } else {
      CHECK(be32(font, entry + 4) == word_sum(font, offset, length));
    }
    if (expected[index] == "maxp") {
      maxp = offset;
    } else if (expected[index] == "glyf") {
      glyf = offset;
      glyf_length = length;
    } else if (expected[index] == "loca") {
      loca = offset;
    }
  }
  // The whole file sums to the magic value once the adjustment is in.
  CHECK(word_sum(font, 0, font.size()) == 0xB1B0AFBAU);
  CHECK(be32(font, head + 12) == 0x5F0F3CF5U);
  CHECK(be16(font, head + 18) == 1000);  // units per em
  CHECK(be16(font, head + 50) == 0);     // short loca
  CHECK(be16(font, maxp + 4) == 1);      // one glyph
  // That glyph: one contour, a box of one em, and loca points at all of it.
  CHECK(be16(font, glyf) == 1);
  CHECK(be16(font, glyf + 6) == 1000 && be16(font, glyf + 8) == 1000);
  CHECK(be16(font, loca) == 0 && be16(font, loca + 2) * 2 == glyf_length);
}

void pdf_image_writer_writes_invisible_text_a_reader_finds() {
  std::filesystem::create_directories("test-artifacts");
  const std::filesystem::path path = "test-artifacts/pdf_hidden_text.pdf";
  std::filesystem::remove(path);
  // Latin, Cyrillic, kana and kanji, and a character outside the basic plane.
  const std::string latin = "Hello, hidden text";
  const std::string cyrillic = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
  const std::string japanese = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x81\xAE\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88";
  const std::string astral = "A\xF0\x9F\x98\x80" "B";
  {
    patchy::pdf::ImageWriter writer(path);
    auto first = scan_page(0x11);
    first.text.push_back({latin, 20.0, 150.0, 180.0, 12.0, 0.0});
    first.text.push_back({cyrillic, 20.0, 120.0, 90.0, 12.0, 0.0});
    first.text.push_back({japanese, 250.0, 180.0, 140.0, 20.0, -90.0});  // a column, read downward
    // Lines nobody could find are left out: empty, control characters only, no length.
    first.text.push_back({"", 0.0, 0.0, 10.0, 10.0, 0.0});
    first.text.push_back({"\t\n", 0.0, 0.0, 10.0, 10.0, 0.0});
    first.text.push_back({"lost", 0.0, 0.0, 0.0, 10.0, 0.0});
    writer.add_page(first);
    writer.add_page(scan_page(0x22));  // a page without text between two with
    auto third = scan_page(0x33);
    third.text.push_back({astral, 10.0, 10.0, 60.0, 10.0, 0.0});
    third.text.push_back({latin, 10.0, 40.0, 180.0, 12.0, 0.0});  // shares the first page's characters
    writer.add_page(third);
    writer.finish();
  }
  const auto bytes = read_whole(path);
  std::vector<std::string> notices;
  auto file = patchy::pdf::File::open(bytes, &notices);
  CHECK(file.has_value());
  if (!file.has_value() || file->pages().size() != 3) {
    CHECK(false);
    return;
  }
  CHECK(notices.empty());  // a clean file: nothing repaired

  // The page's operators: the image as always, then one text object, one run a line.
  const auto& first = file->pages()[0];
  const auto content = text_of(file->stream_data(file->get(first.dict, "Contents")).data);
  CHECK(content.starts_with("q 300 0 0 200 0 0 cm /Im0 Do Q\nBT 3 Tr\n"));
  CHECK(content.ends_with("ET\n"));
  // 18 characters of 12 pt over 180 pt: 180 / (18 x 12) = 83.3333%.
  CHECK(content.find("/F0 12 Tf 83.3333 Tz 1 0 0 1 20 150 Tm <") != std::string::npos);
  // The column: a quarter turn clockwise, so the run reads down the page.
  CHECK(content.find("/F0 20 Tf 100 Tz 0 -1 1 0 250 180 Tm <") != std::string::npos);
  std::size_t runs = 0;
  for (std::size_t at = content.find(" Tj"); at != std::string::npos; at = content.find(" Tj", at + 1)) {
    ++runs;
  }
  CHECK(runs == 3);
  CHECK(content.find("TJ") == std::string::npos);  // never an array of placed pieces
  CHECK(content.find("lost") == std::string::npos);

  // The font: one object shared by the pages with text, absent from the page without.
  const auto& font = file->get(file->get(first.resources, "Font"), "F0");
  CHECK(file->get(font, "Subtype").name() == "Type0");
  CHECK(file->get(font, "Encoding").name() == "Identity-H");
  CHECK(file->get(font, "ToUnicode").is_stream());
  const auto& descendants = file->get(font, "DescendantFonts");
  CHECK(descendants.is_array());
  if (descendants.is_array() && descendants.array() != nullptr && !descendants.array()->empty()) {
    const auto& descendant = file->resolve(descendants.array()->front());
    CHECK(file->get(descendant, "Subtype").name() == "CIDFontType2");
    CHECK(file->get(descendant, "DW").integer() == 1000);
    CHECK(file->get(descendant, "CIDToGIDMap").is_stream());
    const auto& descriptor = file->get(descendant, "FontDescriptor");
    CHECK(file->get(descriptor, "FontName").name() == "PatchyHiddenText");
    CHECK(file->stream_data(file->get(descriptor, "FontFile2")).data == patchy::pdf::hidden_text_font());
  }
  CHECK(file->get(file->pages()[1].resources, "Font").is_null());
  CHECK(text_of(file->stream_data(file->get(file->pages()[1].dict, "Contents")).data) ==
        "q 300 0 0 200 0 0 cm /Im0 Do Q\n");
  CHECK(!file->get(file->get(file->pages()[2].resources, "Font"), "F0").is_null());

  // Read back by the reader: the text of every line, in order, invisible, and where the
  // writer was told to put it (one pixel per point, y down from the page's top).
  TextSink first_sink;
  patchy::pdf::execute_page(*file, first, 1.0, first_sink);
  CHECK(first_sink.images == 1);
  CHECK(first_sink.visible.empty());
  CHECK(first_sink.hidden.size() == 3);
  if (first_sink.hidden.size() == 3) {
    CHECK(first_sink.hidden[0].utf8 == latin);
    CHECK(first_sink.hidden[1].utf8 == cyrillic);
    CHECK(first_sink.hidden[2].utf8 == japanese);
    for (const auto& run : first_sink.hidden) {
      CHECK(run.render_mode == 3);
    }
    const auto& level = first_sink.hidden[0];
    CHECK(std::abs(level.transform.e - 20.0) < 1e-6);
    CHECK(std::abs(level.transform.f - (200.0 - 150.0)) < 1e-6);
    // The run is as long as the line it stands for: 180 pt, whatever the characters.
    CHECK(level.width_is_known);
    CHECK(std::abs(level.intended_width - 180.0) < 0.01);
    CHECK(std::abs(first_sink.hidden[1].intended_width - 90.0) < 0.01);
    CHECK(std::abs(first_sink.hidden[2].intended_width - 140.0) < 0.01);
    // The column's reading direction points down the page.
    const auto& column = first_sink.hidden[2];
    CHECK(std::abs(column.transform.a) < 1e-6 && column.transform.b > 0.0);
  }
  TextSink third_sink;
  patchy::pdf::execute_page(*file, file->pages()[2], 1.0, third_sink);
  CHECK(third_sink.hidden.size() == 2);
  if (third_sink.hidden.size() == 2) {
    CHECK(third_sink.hidden[0].utf8 == astral);
    CHECK(third_sink.hidden[1].utf8 == latin);
  }

  // The text paints nothing, so the page is still one image an export can carry verbatim.
  const patchy::pdf::PageReader reader(bytes);
  const auto probe = reader.probe_page(0);
  CHECK(probe.images == 1);
  CHECK(probe.text_runs == 0);
  CHECK(probe.source_page != nullptr);
}

void pdf_image_writer_without_text_writes_the_bytes_it_always_did() {
  std::filesystem::create_directories("test-artifacts");
  const auto write = [](const std::filesystem::path& path, bool unusable_lines) {
    std::filesystem::remove(path);
    patchy::pdf::ImageWriter writer(path);
    for (const std::uint8_t seed : {std::uint8_t{0x11}, std::uint8_t{0x22}}) {
      auto page = scan_page(seed);
      if (unusable_lines) {
        page.text.push_back({"", 1.0, 1.0, 10.0, 10.0, 0.0});
        page.text.push_back({"no size", 1.0, 1.0, 10.0, 0.0, 0.0});
      }
      writer.add_page(page);
    }
    writer.set_document_info({{"Title", "Pinned"}});
    writer.finish();
    return read_whole(path);
  };
  const auto plain = write("test-artifacts/pdf_hidden_text_none.pdf", false);
  // Lines that cannot be written add nothing: no font, no text object, the same bytes.
  CHECK(write("test-artifacts/pdf_hidden_text_unusable.pdf", true) == plain);
  CHECK(text_of(plain).find("/Font") == std::string::npos);
  CHECK(text_of(plain).find("BT") == std::string::npos);
  // The pin: this file is what the writer produced before it knew about text. A change
  // here changes every image PDF Patchy writes; re-pin only on purpose.
  CHECK(plain.size() == 1272);
  CHECK(hex_of(patchy::pdf::sha256(plain)) == "8adfbc50b807bcb6ea8cebd3ac937def24357d8a4d7464ce572faa103e36a121");
}

}  // namespace

std::vector<patchy::test::TestCase> pdf_hidden_text_tests() {
  return {
      {"pdf_hidden_text_font_is_a_well_formed_truetype_file", pdf_hidden_text_font_is_a_well_formed_truetype_file},
      {"pdf_image_writer_writes_invisible_text_a_reader_finds", pdf_image_writer_writes_invisible_text_a_reader_finds},
      {"pdf_image_writer_without_text_writes_the_bytes_it_always_did",
       pdf_image_writer_without_text_writes_the_bytes_it_always_did},
  };
}
