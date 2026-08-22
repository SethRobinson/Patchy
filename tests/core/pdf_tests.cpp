#include "formats/pdf_file.hpp"
#include "formats/pdf_filters.hpp"
#include "formats/pdf_syntax.hpp"

#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"

#include "formats/miniz/miniz.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// PDF syntax, stream filters, and document structure. The vector importer that sits
// on top of these is exercised separately; everything here is the plumbing.

namespace {

std::span<const std::uint8_t> as_bytes(std::string_view text) {
  return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

std::vector<std::uint8_t> bytes_of(std::string_view text) {
  const auto span = as_bytes(text);
  return {span.begin(), span.end()};
}

std::string as_text(const std::vector<std::uint8_t>& data) {
  return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

std::vector<std::uint8_t> zlib_compress(std::string_view text) {
  mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(text.size()));
  std::vector<std::uint8_t> out(bound);
  const auto status = mz_compress(out.data(), &bound, reinterpret_cast<const unsigned char*>(text.data()),
                                  static_cast<mz_ulong>(text.size()));
  CHECK(status == MZ_OK);
  out.resize(bound);
  return out;
}

// Builds a syntactically complete PDF with a correct classic xref table, so the
// structure tests exercise the real offset path rather than the rebuild fallback.
// `objects` are the bodies of objects 1..N, in order.
std::vector<std::uint8_t> build_pdf(const std::vector<std::string>& objects, std::string_view trailer_extra = {}) {
  std::string pdf = "%PDF-1.7\n";
  std::vector<std::size_t> offsets;
  offsets.reserve(objects.size());
  for (std::size_t index = 0; index < objects.size(); ++index) {
    offsets.push_back(pdf.size());
    pdf += std::to_string(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
  }
  const std::size_t xref_offset = pdf.size();
  pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
  for (const auto offset : offsets) {
    char entry[24];
    std::snprintf(entry, sizeof(entry), "%010zu 00000 n \n", offset);
    pdf += entry;
  }
  pdf += "trailer\n<</Size " + std::to_string(objects.size() + 1) + "/Root 1 0 R" + std::string(trailer_extra) +
         ">>\nstartxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";
  return bytes_of(pdf);
}

// A two-page document: page 1 is 144x72 pt, page 2 is 72x144 pt and rotated 90.
std::vector<std::uint8_t> two_page_pdf() {
  return build_pdf({
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R 5 0 R]/Count 2/Resources<</Font<</F1 7 0 R>>>>>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 144 72]/Contents 4 0 R>>",
      "<</Length 25>>\nstream\n1 0 0 rg 0 0 144 72 re f\nendstream",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 72 144]/Rotate 90/Contents 6 0 R>>",
      "<</Length 25>>\nstream\n0 0 1 rg 0 0 72 144 re f\nendstream",
      "<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>",
  });
}

void pdf_lexer_reads_every_object_type() {
  const std::string source =
      "42 -7 3.5 -.25 4. true false null "
      "/Name /With#20Space "
      "(simple) (nested (parens) ok) (esc\\(\\)\\\\\\n\\t\\101) "
      "<48656C6C6F> <48656C6C6F7> "
      "[1 2 /Three] <</Key 1/Ref 9 0 R>> 12 0 R";
  patchy::pdf::Lexer lexer(as_bytes(source));

  CHECK(lexer.next_object().integer() == 42);
  CHECK(lexer.next_object().integer() == -7);
  CHECK(std::abs(lexer.next_object().number() - 3.5) < 1e-9);
  CHECK(std::abs(lexer.next_object().number() + 0.25) < 1e-9);
  CHECK(std::abs(lexer.next_object().number() - 4.0) < 1e-9);
  CHECK(lexer.next_object().boolean());
  CHECK(!lexer.next_object().boolean(true));
  CHECK(lexer.next_object().is_null());

  CHECK(lexer.next_object().name() == "Name");
  // #-escapes decode in names.
  CHECK(lexer.next_object().name() == "With Space");

  CHECK(lexer.next_object().string() == "simple");
  // Balanced inner parentheses stay in the string without escaping.
  CHECK(lexer.next_object().string() == "nested (parens) ok");
  // \( \) \\ \n \t and the octal \101 = 'A'.
  CHECK(lexer.next_object().string() == "esc()\\\n\tA");

  CHECK(lexer.next_object().string() == "Hello");
  // An odd trailing hex digit is padded with zero: 0x70 = 'p'.
  CHECK(lexer.next_object().string() == "Hellop");

  const auto array = lexer.next_object();
  CHECK(array.array() != nullptr);
  CHECK(array.array()->size() == 3);
  CHECK((*array.array())[2].name() == "Three");

  const auto dict = lexer.next_object();
  CHECK(dict.dictionary() != nullptr);
  CHECK(dict.get("Key").integer() == 1);
  // "9 0 R" is three tokens in the grammar; the lexer folds it into one reference.
  CHECK(dict.get("Ref").reference().has_value());
  CHECK(dict.get("Ref").reference()->number == 9);

  const auto reference = lexer.next_object();
  CHECK(reference.reference().has_value());
  CHECK(reference.reference()->number == 12);
}

void pdf_lexer_survives_damaged_input() {
  // Unterminated string, unbalanced dictionary, a stray closer, and a keyword where
  // a value belongs. None of these may hang or throw: real files carry all of them.
  patchy::pdf::Lexer lexer(as_bytes("<</A 1 /B >> ] (unterminated"));
  const auto dict = lexer.next_object();
  CHECK(dict.dictionary() != nullptr);
  CHECK(dict.get("A").integer() == 1);
  // /B had no value, so it is absent or null; either way it must not corrupt /A.
  CHECK(dict.get("B").is_null());

  // The lexer must always make progress; a stall would hang the importer.
  std::size_t guard = 0;
  while (!lexer.at_end() && guard < 100) {
    const auto before = lexer.position();
    if (!lexer.next().has_value()) {
      break;
    }
    CHECK(lexer.position() > before);
    ++guard;
  }
  CHECK(guard < 100);
}

void pdf_filters_decode_every_supported_codec() {
  // ASCII85: "Hello" per the standard worked example.
  const auto ascii85 = patchy::pdf::decode_ascii85(as_bytes("87cURD]~>"));
  CHECK(ascii85.ok());
  CHECK(as_text(ascii85.data) == "Hello");
  // 'z' is shorthand for four zero bytes.
  const auto ascii85_zero = patchy::pdf::decode_ascii85(as_bytes("z~>"));
  CHECK(ascii85_zero.data.size() == 4);
  CHECK(ascii85_zero.data[0] == 0 && ascii85_zero.data[3] == 0);

  const auto hex = patchy::pdf::decode_ascii_hex(as_bytes("48 65 6C 6C 6F>"));
  CHECK(hex.ok());
  CHECK(as_text(hex.data) == "Hello");

  // RunLength: literal run of 3, then 4 copies of 'x', then the 128 terminator.
  const std::vector<std::uint8_t> run_length_input{2, 'a', 'b', 'c', 253, 'x', 128};
  const auto run_length = patchy::pdf::decode_run_length(run_length_input);
  CHECK(run_length.ok());
  CHECK(as_text(run_length.data) == "abcxxxx");

  const std::string original = "Patchy PDF filter round trip, padded out so Flate has something to chew on.";
  const auto flate = patchy::pdf::inflate_bytes(zlib_compress(original));
  CHECK(flate.ok());
  CHECK(as_text(flate.data) == original);

  // A truncated Flate stream keeps what decoded and reports the damage instead of
  // losing the whole page.
  auto truncated = zlib_compress(original);
  truncated.resize(truncated.size() / 2);
  const auto partial = patchy::pdf::inflate_bytes(truncated);
  CHECK(!partial.ok());

  // Image codecs are recognized but never decoded here: the bytes come back
  // untouched and flagged so the caller hands them to Qt.
  const auto dct = patchy::pdf::apply_filter(patchy::pdf::FilterKind::Dct, as_bytes("\xFF\xD8\xFF"), {});
  CHECK(dct.image_codec == patchy::pdf::FilterKind::Dct);
  CHECK(dct.data.size() == 3);
  CHECK(patchy::pdf::image_codec_extension(patchy::pdf::FilterKind::Dct) == "jpg");
}

void pdf_png_predictor_undoes_row_filters() {
  // Two rows of three 8-bit RGB pixels. Row 0 uses filter 0 (None), row 1 uses
  // filter 2 (Up), so row 1 decodes to row 0 plus its own deltas.
  std::vector<std::uint8_t> data{
      0, 10, 20, 30, 40, 50, 60, 70, 80, 90,  //
      2, 1,  1,  1,  1,  1,  1,  1,  1,  1,   //
  };
  const auto result = patchy::pdf::undo_predictor(std::move(data), 12, 3, 8, 3);
  CHECK(result.ok());
  CHECK(result.data.size() == 18);
  CHECK(result.data[0] == 10);
  CHECK(result.data[8] == 90);
  CHECK(result.data[9] == 11);   // 10 + 1
  CHECK(result.data[17] == 91);  // 90 + 1

  // Filter 1 (Sub) walks left by one whole pixel, not one byte.
  std::vector<std::uint8_t> sub{1, 10, 20, 30, 5, 5, 5};
  const auto sub_result = patchy::pdf::undo_predictor(std::move(sub), 15, 3, 8, 2);
  CHECK(sub_result.data.size() == 6);
  CHECK(sub_result.data[3] == 15);  // 10 + 5
  CHECK(sub_result.data[5] == 35);  // 30 + 5
}

void pdf_file_reads_pages_and_inherited_attributes() {
  auto file = patchy::pdf::File::open(two_page_pdf(), nullptr);
  CHECK(file.has_value());
  if (!file.has_value()) {
    return;
  }
  CHECK(file->version() == "1.7");
  CHECK(!file->is_encrypted());
  CHECK(file->pages().size() == 2);

  const auto& first = file->pages()[0];
  CHECK(std::abs(first.media_box[2] - 144.0) < 1e-9);
  CHECK(std::abs(first.media_box[3] - 72.0) < 1e-9);
  // No /CropBox anywhere, so it defaults to /MediaBox.
  CHECK(std::abs(first.crop_box[2] - 144.0) < 1e-9);
  CHECK(first.rotate == 0);
  // /Resources sits on the Pages node and must be inherited by both leaves.
  CHECK(file->get(first.resources, "Font").is_dictionary());

  const auto& second = file->pages()[1];
  CHECK(std::abs(second.media_box[3] - 144.0) < 1e-9);
  CHECK(second.rotate == 90);

  // Content streams decode, and an indirect /Length resolves.
  const auto content = file->stream_data(file->get(first.dict, "Contents"));
  CHECK(content.error.empty());
  CHECK(as_text(content.data).find("144 72 re") != std::string::npos);
}

void pdf_file_rebuilds_a_damaged_cross_reference_table() {
  auto bytes = two_page_pdf();
  // Corrupt every xref offset the way an editor that appends without fixing the
  // table does. Acrobat rebuilds by scanning; so must we.
  const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  const auto xref = text.find("xref\n0 ");
  CHECK(xref != std::string_view::npos);
  for (std::size_t index = xref; index + 10 < bytes.size(); ++index) {
    if (bytes[index] >= '1' && bytes[index] <= '9') {
      bytes[index] = '9';
    }
  }

  std::vector<std::string> notices;
  auto file = patchy::pdf::File::open(std::move(bytes), &notices);
  CHECK(file.has_value());
  if (!file.has_value()) {
    return;
  }
  CHECK(file->pages().size() == 2);
  CHECK(!notices.empty());
  const auto content = file->stream_data(file->get(file->pages()[0].dict, "Contents"));
  CHECK(as_text(content.data).find("144 72 re") != std::string::npos);
}

void pdf_file_recovers_a_wrong_stream_length() {
  // /Length says 5 but the data is longer. Producers get this wrong constantly, so
  // the reader verifies against "endstream" rather than trusting the number.
  auto file = patchy::pdf::File::open(build_pdf({
                                          "<</Type/Catalog/Pages 2 0 R>>",
                                          "<</Type/Pages/Kids[3 0 R]/Count 1>>",
                                          "<</Type/Page/Parent 2 0 R/MediaBox[0 0 10 10]/Contents 4 0 R>>",
                                          "<</Length 5>>\nstream\n0 0 1 rg 0 0 10 10 re f\nendstream",
                                      }),
                                      nullptr);
  CHECK(file.has_value());
  if (!file.has_value()) {
    return;
  }
  const auto content = file->stream_data(file->get(file->pages()[0].dict, "Contents"));
  CHECK(as_text(content.data).find("10 10 re f") != std::string::npos);
}

void pdf_file_resolves_object_streams() {
  // Objects 1-3 live inside a compressed object stream, reached through a
  // cross-reference stream. This is how every PDF 1.5+ producer writes files, so it
  // has to work without the classic table.
  const std::string bodies[] = {
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]>>",
  };
  // Offsets are computed, never hand-counted: the header is what tells the reader
  // where each packed object starts, so a miscount would test the wrong thing.
  std::string packed;
  std::string header;
  for (std::size_t index = 0; index < std::size(bodies); ++index) {
    header += std::to_string(index + 1) + " " + std::to_string(packed.size()) + " ";
    packed += bodies[index];
    packed.push_back(' ');
  }
  const std::string payload = header + packed;
  const auto compressed = zlib_compress(payload);

  std::string pdf = "%PDF-1.5\n";
  const std::size_t objstm_offset = pdf.size();
  pdf += "4 0 obj\n<</Type/ObjStm/N 3/First " + std::to_string(header.size()) + "/Length " +
         std::to_string(compressed.size()) + "/Filter/FlateDecode>>\nstream\n";
  pdf.append(reinterpret_cast<const char*>(compressed.data()), compressed.size());
  pdf += "\nendstream\nendobj\n";

  // A cross-reference stream with W [1 2 1]: type, then a 2-byte field, then 1 byte.
  std::vector<std::uint8_t> xref_rows;
  const auto push_row = [&xref_rows](std::uint8_t type, std::uint16_t second, std::uint8_t third) {
    xref_rows.push_back(type);
    xref_rows.push_back(static_cast<std::uint8_t>(second >> 8));
    xref_rows.push_back(static_cast<std::uint8_t>(second & 0xFF));
    xref_rows.push_back(third);
  };
  push_row(0, 0, 0);                                            // object 0, free
  push_row(2, 4, 0);                                            // object 1 in stream 4, index 0
  push_row(2, 4, 1);                                            // object 2 in stream 4, index 1
  push_row(2, 4, 2);                                            // object 3 in stream 4, index 2
  push_row(1, static_cast<std::uint16_t>(objstm_offset), 0);    // object 4 at its offset
  const std::size_t xref_offset = pdf.size();
  push_row(1, static_cast<std::uint16_t>(xref_offset), 0);      // object 5, the xref stream itself

  pdf += "5 0 obj\n<</Type/XRef/Size 6/W[1 2 1]/Root 1 0 R/Length " + std::to_string(xref_rows.size()) +
         ">>\nstream\n";
  pdf.append(reinterpret_cast<const char*>(xref_rows.data()), xref_rows.size());
  pdf += "\nendstream\nendobj\nstartxref\n" + std::to_string(xref_offset) + "\n%%EOF\n";

  auto file = patchy::pdf::File::open(bytes_of(pdf), nullptr);
  CHECK(file.has_value());
  if (!file.has_value()) {
    return;
  }
  CHECK(file->pages().size() == 1);
  CHECK(std::abs(file->pages()[0].media_box[2] - 200.0) < 1e-9);
  CHECK(file->get(file->catalog(), "Pages").is_dictionary());
}

// Real-world samples, skipped when the untracked fixtures are absent (they are on
// the remote builders). These are ordinary ReportLab documents: a classic xref
// table, Flate and ASCII85 content, embedded TrueType subsets.
void pdf_local_real_documents_parse_if_available() {
  struct Sample {
    const char* file_name;
    std::size_t expected_pages;
    double expected_width;
  };
  // Both are A4: 595.2756 x 841.8898 pt.
  const Sample samples[] = {
      {"HoloVCS_C2_A4_Brochure.pdf", 1, 595.2756},
      {"Arduino_vs_ESP32_Spider_Bot_A4_Poster.pdf", 1, 595.2756},
  };

  for (const auto& sample : samples) {
    const auto path = patchy::test::local_format_fixture_path("pdf", sample.file_name);
    if (!std::filesystem::exists(path)) {
      std::cout << "[SKIP] local pdf fixture missing: " << path.string() << '\n';
      continue;
    }
    std::ifstream stream(path, std::ios::binary);
    CHECK(stream.good());
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                          std::istreambuf_iterator<char>());
    CHECK(!bytes.empty());

    std::vector<std::string> notices;
    auto file = patchy::pdf::File::open(bytes, &notices);
    CHECK(file.has_value());
    if (!file.has_value()) {
      continue;
    }
    // A well-formed file must parse through the real xref, never the rebuild path.
    CHECK(notices.empty());
    CHECK(!file->is_encrypted());
    CHECK(file->pages().size() == sample.expected_pages);
    if (file->pages().empty()) {
      continue;
    }

    const auto& page = file->pages().front();
    CHECK(std::abs(page.media_box[2] - sample.expected_width) < 0.01);
    CHECK(std::abs(page.media_box[3] - 841.8898) < 0.01);

    // The content stream decodes and holds real operators.
    const auto content = file->stream_data(file->get(page.dict, "Contents"));
    CHECK(content.error.empty());
    CHECK(content.data.size() > 1000);
    const auto text = as_text(content.data);
    CHECK(text.find(" re") != std::string::npos);
    CHECK(text.find("BT") != std::string::npos);
    CHECK(text.find("Tf") != std::string::npos);

    // Fonts resolve through the inherited /Resources.
    const auto& fonts = file->get(file->get(page.resources, "Font"), "F1");
    CHECK(fonts.is_dictionary());
    CHECK(file->get(fonts, "BaseFont").is_name());

    // Every XObject the page names must decode or be an image codec handed onward.
    const auto& xobjects = file->get(page.resources, "XObject");
    if (const auto* dict = xobjects.dictionary(); dict != nullptr) {
      for (const auto& [name, value] : *dict) {
        const auto& xobject = file->resolve(value);
        if (xobject.stream() == nullptr) {
          continue;
        }
        const auto data = file->stream_data(xobject);
        CHECK(!data.data.empty());
      }
    }
  }
}

}  // namespace

std::vector<patchy::test::TestCase> pdf_tests() {
  return {
      {"pdf_lexer_reads_every_object_type", pdf_lexer_reads_every_object_type},
      {"pdf_lexer_survives_damaged_input", pdf_lexer_survives_damaged_input},
      {"pdf_filters_decode_every_supported_codec", pdf_filters_decode_every_supported_codec},
      {"pdf_png_predictor_undoes_row_filters", pdf_png_predictor_undoes_row_filters},
      {"pdf_file_reads_pages_and_inherited_attributes", pdf_file_reads_pages_and_inherited_attributes},
      {"pdf_file_rebuilds_a_damaged_cross_reference_table", pdf_file_rebuilds_a_damaged_cross_reference_table},
      {"pdf_file_recovers_a_wrong_stream_length", pdf_file_recovers_a_wrong_stream_length},
      {"pdf_file_resolves_object_streams", pdf_file_resolves_object_streams},
      {"pdf_local_real_documents_parse_if_available", pdf_local_real_documents_parse_if_available},
  };
}
