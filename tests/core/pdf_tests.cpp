#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "formats/pdf_content.hpp"
#include "formats/pdf_document_io.hpp"
#include "formats/pdf_file.hpp"
#include "formats/pdf_fonts.hpp"
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

void pdf_font_names_split_subset_tags_and_styles() {
  // A subset tag is exactly six uppercase letters and a '+'.
  const auto subset = patchy::pdf::parse_base_font_name("AAAAAA+SegoeUI-Bold");
  CHECK(subset.family == "SegoeUI");
  CHECK(subset.style == "Bold");
  CHECK(subset.bold);
  CHECK(!subset.italic);

  // Six letters but no '+', so nothing is stripped.
  CHECK(patchy::pdf::parse_base_font_name("ABCDEFGlyphic").family == "ABCDEFGlyphic");
  // Lowercase in the tag position is not a subset tag either.
  CHECK(patchy::pdf::parse_base_font_name("aaaaaa+Times").family == "aaaaaa+Times");

  const auto both = patchy::pdf::parse_base_font_name("BCDEFG+TimesNewRomanPS-BoldItalicMT");
  CHECK(both.family == "TimesNewRomanPS");
  CHECK(both.bold);
  CHECK(both.italic);

  // The comma form, and a name with no separator at all.
  CHECK(patchy::pdf::parse_base_font_name("Arial,BoldItalic").bold);
  CHECK(patchy::pdf::parse_base_font_name("Arial,BoldItalic").italic);
  CHECK(patchy::pdf::parse_base_font_name("ArialBold").bold);

  const auto plain = patchy::pdf::parse_base_font_name("Helvetica");
  CHECK(plain.family == "Helvetica");
  CHECK(!plain.bold);
  CHECK(!plain.italic);
  // Oblique counts as italic; Black and Heavy count as bold.
  CHECK(patchy::pdf::parse_base_font_name("Helvetica-Oblique").italic);
  CHECK(patchy::pdf::parse_base_font_name("Futura-Black").bold);
}

void pdf_glyph_names_map_to_unicode() {
  CHECK(patchy::pdf::unicode_for_glyph_name("A") == U'A');
  CHECK(patchy::pdf::unicode_for_glyph_name("space") == U' ');
  CHECK(patchy::pdf::unicode_for_glyph_name("bullet") == 0x2022);
  CHECK(patchy::pdf::unicode_for_glyph_name("emdash") == 0x2014);
  CHECK(patchy::pdf::unicode_for_glyph_name("quoteright") == 0x2019);
  CHECK(patchy::pdf::unicode_for_glyph_name("fi") == 0xFB01);
  // Composed accented names rather than a table entry each.
  CHECK(patchy::pdf::unicode_for_glyph_name("eacute") == 0x00E9);
  CHECK(patchy::pdf::unicode_for_glyph_name("Adieresis") == 0x00C4);
  CHECK(patchy::pdf::unicode_for_glyph_name("ntilde") == 0x00F1);
  CHECK(patchy::pdf::unicode_for_glyph_name("scaron") == 0x0161);
  // The algorithmic forms.
  CHECK(patchy::pdf::unicode_for_glyph_name("uni20AC") == 0x20AC);
  CHECK(patchy::pdf::unicode_for_glyph_name("u1F600") == 0x1F600);
  // A variant suffix falls back to the base name.
  CHECK(patchy::pdf::unicode_for_glyph_name("one.oldstyle") == U'1');
  // Names that carry no Unicode meaning must say so rather than guess.
  CHECK(patchy::pdf::unicode_for_glyph_name("g42") == 0);
  CHECK(patchy::pdf::unicode_for_glyph_name("") == 0);
}

void pdf_to_unicode_cmap_reads_chars_and_ranges() {
  const std::string cmap =
      "/CIDInit /ProcSet findresource begin\n"
      "1 begincodespacerange <0000> <FFFF> endcodespacerange\n"
      "2 beginbfchar\n<0003> <0020>\n<0024> <0041>\n endbfchar\n"
      "2 beginbfrange\n<0030> <0039> <0030>\n<0041> <0043> [<0058> <0059> <005A>]\n endbfrange\n"
      "endcmap\n";
  const auto mapping = patchy::pdf::parse_to_unicode_cmap(as_bytes(cmap));

  CHECK(mapping.at(0x0003) == U' ');
  CHECK(mapping.at(0x0024) == U'A');
  // A bfrange with a scalar destination increments.
  CHECK(mapping.at(0x0030) == U'0');
  CHECK(mapping.at(0x0039) == U'9');
  // A bfrange with an array destination takes one entry per code.
  CHECK(mapping.at(0x0041) == U'X');
  CHECK(mapping.at(0x0043) == U'Z');
  // The codespacerange numbers must not leak in as entries.
  CHECK(!mapping.contains(1));
  CHECK(!mapping.contains(2));
}

void pdf_simple_font_encodings_decode_text() {
  // WinAnsi with a /Differences override, /Widths, and no /ToUnicode: the path a
  // standard-14 font takes.
  auto file = patchy::pdf::File::open(
      build_pdf({
          "<</Type/Catalog/Pages 2 0 R>>",
          "<</Type/Pages/Kids[3 0 R]/Count 1>>",
          "<</Type/Page/Parent 2 0 R/MediaBox[0 0 100 100]/Resources<</Font<</F1 4 0 R>>>>>>",
          "<</Type/Font/Subtype/Type1/BaseFont/Helvetica/FirstChar 65/LastChar 67"
          "/Widths[600 700 800]/Encoding<</BaseEncoding/WinAnsiEncoding/Differences[67 /bullet]>>>>",
      }),
      nullptr);
  CHECK(file.has_value());
  if (!file.has_value()) {
    return;
  }
  const auto& page = file->pages().front();
  const auto& font_dict = file->get(file->get(page.resources, "Font"), "F1");
  const auto font = patchy::pdf::load_font(*file, font_dict);

  CHECK(font.kind == patchy::pdf::FontKind::Type1);
  CHECK(font.family == "Helvetica");
  CHECK(!font.two_byte);
  CHECK(font.has_widths);

  const auto glyphs = font.decode("ABC");
  CHECK(glyphs.size() == 3);
  CHECK(glyphs[0].unicode == U'A');
  CHECK(glyphs[1].unicode == U'B');
  // /Differences remapped code 67 from 'C' to a bullet.
  CHECK(glyphs[2].unicode == 0x2022);
  CHECK(std::abs(glyphs[0].width - 0.6) < 1e-9);
  CHECK(std::abs(glyphs[2].width - 0.8) < 1e-9);
  CHECK(std::abs(font.string_width("ABC") - 2.1) < 1e-9);

  // The WinAnsi high block, which is CP1252 and not Latin-1.
  const std::string high_bytes(1, static_cast<char>(0x93));
  CHECK(font.decode(high_bytes)[0].unicode == 0x201C);  // left double quote
  const std::string euro(1, static_cast<char>(0x80));
  CHECK(font.decode(euro)[0].unicode == 0x20AC);
  // Above 0x9F WinAnsi is plain Latin-1.
  const std::string latin(1, static_cast<char>(0xE9));
  CHECK(font.decode(latin)[0].unicode == 0x00E9);

  // Only the single-byte code 32 takes word spacing.
  CHECK(font.decode(" ")[0].is_word_space);
  CHECK(!font.decode("A")[0].is_word_space);
}

void pdf_composite_font_reads_two_byte_codes_and_w_widths() {
  auto file = patchy::pdf::File::open(
      build_pdf({
          "<</Type/Catalog/Pages 2 0 R>>",
          "<</Type/Pages/Kids[3 0 R]/Count 1>>",
          "<</Type/Page/Parent 2 0 R/MediaBox[0 0 100 100]/Resources<</Font<</F1 4 0 R>>>>>>",
          "<</Type/Font/Subtype/Type0/BaseFont/ABCDEF+NotoSansJP/Encoding/Identity-H"
          "/DescendantFonts[5 0 R]>>",
          "<</Type/Font/Subtype/CIDFontType2/BaseFont/ABCDEF+NotoSansJP/DW 1000"
          "/W[1 [500 600] 10 12 750]>>",
      }),
      nullptr);
  CHECK(file.has_value());
  if (!file.has_value()) {
    return;
  }
  const auto& page = file->pages().front();
  const auto font = patchy::pdf::load_font(*file, file->get(file->get(page.resources, "Font"), "F1"));

  CHECK(font.kind == patchy::pdf::FontKind::Type0);
  CHECK(font.two_byte);
  CHECK(font.family == "NotoSansJP");
  CHECK(font.has_widths);

  // Identity-H: each pair of bytes is one code, big-endian.
  const std::string codes{0x00, 0x01, 0x00, 0x0B};
  const auto glyphs = font.decode(codes);
  CHECK(glyphs.size() == 2);
  CHECK(glyphs[0].code == 1);
  CHECK(glyphs[1].code == 11);
  // /W's "c [w w]" form covers codes 1 and 2; its "cFirst cLast w" form covers 10-12.
  CHECK(std::abs(glyphs[0].width - 0.5) < 1e-9);
  CHECK(std::abs(glyphs[1].width - 0.75) < 1e-9);
  // An unlisted code falls back to /DW, not to the simple-font default.
  const std::string unlisted{0x01, 0x00};
  CHECK(std::abs(font.decode(unlisted)[0].width - 1.0) < 1e-9);
  // Two-byte codes never take word spacing, even when a byte happens to be 32.
  CHECK(!glyphs[0].is_word_space);
}

void pdf_local_real_document_fonts_decode_if_available() {
  const auto path = patchy::test::local_format_fixture_path("pdf", "HoloVCS_C2_A4_Brochure.pdf");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local pdf fixture missing: " << path.string() << '\n';
    return;
  }
  std::ifstream stream(path, std::ios::binary);
  CHECK(stream.good());
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                        std::istreambuf_iterator<char>());
  auto file = patchy::pdf::File::open(bytes, nullptr);
  CHECK(file.has_value());
  if (!file.has_value() || file->pages().empty()) {
    return;
  }

  const auto& fonts = file->get(file->pages().front().resources, "Font");
  const auto* dict = fonts.dictionary();
  CHECK(dict != nullptr);
  if (dict == nullptr) {
    return;
  }

  // The document names six fonts: Helvetica plus four embedded TrueType subsets
  // and one more standard face.
  CHECK(dict->size() >= 5);
  bool saw_subset = false;
  for (const auto& [name, value] : *dict) {
    const auto font = patchy::pdf::load_font(*file, file->resolve(value));
    CHECK(!font.family.empty());
    // Every subset tag must be stripped: "AAAAAA+Consolas" -> "Consolas".
    CHECK(font.family.find('+') == std::string::npos);
    if (font.base_font.find('+') == std::string::npos) {
      continue;
    }
    saw_subset = true;
    // The subsets carry /ToUnicode, which is what makes their text recoverable.
    CHECK(!font.to_unicode.empty());
    CHECK(font.has_widths);
    // Real text from the page: this string appears in the brochure verbatim.
    const auto glyphs = font.decode("HoloVCS");
    CHECK(glyphs.size() == 7);
  }
  CHECK(saw_subset);

  // Decode a real text run and confirm it comes back as the authored words. The
  // brochure writes them as plain literal strings under WinAnsi-compatible subsets.
  const auto& consolas_or_segoe = file->resolve(dict->begin()->second);
  const auto font = patchy::pdf::load_font(*file, consolas_or_segoe);
  std::string decoded;
  for (const auto& glyph : font.decode("HoloVCS")) {
    if (glyph.unicode != 0xFFFD && glyph.unicode < 0x80) {
      decoded.push_back(static_cast<char>(glyph.unicode));
    }
  }
  CHECK(decoded == "HoloVCS");
}

// Records everything the interpreter emits, in page order.
struct RecordingSink final : patchy::pdf::ContentSink {
  std::vector<patchy::pdf::PaintedPath> paths;
  std::vector<patchy::pdf::TextRun> texts;
  std::vector<patchy::pdf::PlacedImage> images;
  int shadings{0};
  std::vector<std::string> notices;

  void on_path(const patchy::pdf::PaintedPath& path) override { paths.push_back(path); }
  void on_text(const patchy::pdf::TextRun& run) override { texts.push_back(run); }
  void on_image(const patchy::pdf::PlacedImage& image) override { images.push_back(image); }
  void on_shading(const patchy::pdf::Object&, const patchy::pdf::Affine&, const patchy::VectorPath&) override {
    ++shadings;
  }
  void on_notice(const std::string& text) override { notices.push_back(text); }

  [[nodiscard]] std::string all_text() const {
    std::string joined;
    for (const auto& run : texts) {
      joined += run.utf8;
      joined.push_back('\n');
    }
    return joined;
  }
};

// Runs one content stream against a document built around it.
RecordingSink interpret(const std::string& content, const std::string& extra_objects = {},
                        const std::string& page_extra = {}) {
  std::vector<std::string> objects = {
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]/Contents 4 0 R" + page_extra + ">>",
      "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream",
  };
  if (!extra_objects.empty()) {
    // The caller supplies whole objects separated by a form feed.
    std::size_t start = 0;
    while (start < extra_objects.size()) {
      const auto end = extra_objects.find('\f', start);
      objects.push_back(extra_objects.substr(start, end == std::string::npos ? end : end - start));
      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    }
  }
  RecordingSink sink;
  auto file = patchy::pdf::File::open(build_pdf(objects), nullptr);
  CHECK(file.has_value());
  if (!file.has_value() || file->pages().empty()) {
    return sink;
  }
  // One pixel per point keeps the arithmetic in the assertions readable.
  patchy::pdf::execute_page(*file, file->pages().front(), 1.0, sink);
  return sink;
}

void pdf_page_transform_flips_and_rotates() {
  patchy::pdf::Page page;
  page.media_box[0] = 0.0;
  page.media_box[1] = 0.0;
  page.media_box[2] = 200.0;
  page.media_box[3] = 100.0;
  std::copy(std::begin(page.media_box), std::end(page.media_box), std::begin(page.crop_box));

  // PDF user space is y-up from the bottom-left; document space is y-down from the
  // top-left, so the page's top-left corner (0, 100) must land on (0, 0).
  const auto upright = patchy::pdf::page_base_transform(page, 1.0);
  auto point = patchy::formats::map_point(upright, 0.0, 100.0);
  CHECK(std::abs(point[0]) < 1e-9);
  CHECK(std::abs(point[1]) < 1e-9);
  point = patchy::formats::map_point(upright, 200.0, 0.0);
  CHECK(std::abs(point[0] - 200.0) < 1e-9);
  CHECK(std::abs(point[1] - 100.0) < 1e-9);
  auto size = patchy::pdf::page_pixel_size(page, 1.0);
  CHECK(size[0] == 200 && size[1] == 100);

  // Scale carries through.
  size = patchy::pdf::page_pixel_size(page, 2.0);
  CHECK(size[0] == 400 && size[1] == 200);

  // A rotated page swaps the document's dimensions.
  page.rotate = 90;
  size = patchy::pdf::page_pixel_size(page, 1.0);
  CHECK(size[0] == 100 && size[1] == 200);
  const auto rotated = patchy::pdf::page_base_transform(page, 1.0);
  // Every corner must still land inside the rotated page.
  for (const auto& corner : {std::pair{0.0, 0.0}, std::pair{200.0, 0.0}, std::pair{0.0, 100.0},
                             std::pair{200.0, 100.0}}) {
    const auto mapped = patchy::formats::map_point(rotated, corner.first, corner.second);
    CHECK(mapped[0] >= -1e-6 && mapped[0] <= 100.0 + 1e-6);
    CHECK(mapped[1] >= -1e-6 && mapped[1] <= 200.0 + 1e-6);
  }

  // A crop box smaller than the media box sets the origin and the size.
  page.rotate = 0;
  page.crop_box[0] = 50.0;
  page.crop_box[1] = 20.0;
  page.crop_box[2] = 150.0;
  page.crop_box[3] = 80.0;
  size = patchy::pdf::page_pixel_size(page, 1.0);
  CHECK(size[0] == 100 && size[1] == 60);
  point = patchy::formats::map_point(patchy::pdf::page_base_transform(page, 1.0), 50.0, 80.0);
  CHECK(std::abs(point[0]) < 1e-9);
  CHECK(std::abs(point[1]) < 1e-9);
}

void pdf_interpreter_builds_paths_with_colors_and_winding() {
  const auto sink = interpret(
      "1 0 0 rg 10 20 30 40 re f\n"                       // a filled rectangle
      "0 0 1 RG 4 w 1 J 1 j [3 2] 0 d 5 5 m 60 70 l S\n"  // a stroked line
      "0 1 0 rg 0 0 10 10 re 20 20 10 10 re f*\n");       // two subpaths, even-odd

  CHECK(sink.paths.size() == 3);
  if (sink.paths.size() != 3) {
    return;
  }

  // Fill: red, one closed subpath of four anchors, y flipped into document space.
  const auto& rectangle = sink.paths[0];
  CHECK(rectangle.has_fill);
  CHECK(!rectangle.has_stroke);
  CHECK(rectangle.fill.color.red == 255);
  CHECK(rectangle.fill.color.green == 0);
  CHECK(rectangle.path.subpaths.size() == 1);
  CHECK(rectangle.path.subpaths[0].anchors.size() == 4);
  CHECK(rectangle.path.subpaths[0].closed);
  const auto bounds = rectangle.path.bounds();
  CHECK(bounds.has_value());
  if (bounds.has_value()) {
    CHECK(std::abs(bounds->left - 10.0) < 1e-9);
    CHECK(std::abs(bounds->right - 40.0) < 1e-9);
    // The rectangle spans y 20..60 in PDF space, so 40..80 down from the top of a
    // 100-point page.
    CHECK(std::abs(bounds->top - 40.0) < 1e-9);
    CHECK(std::abs(bounds->bottom - 80.0) < 1e-9);
  }

  // Stroke: blue, width and joins carried, dashes preserved.
  const auto& line = sink.paths[1];
  CHECK(!line.has_fill);
  CHECK(line.has_stroke);
  CHECK(line.stroke.color.blue == 255);
  CHECK(std::abs(line.stroke_style.width - 4.0) < 1e-9);
  CHECK(line.stroke_style.cap == patchy::VectorStrokeCap::Round);
  CHECK(line.stroke_style.join == patchy::VectorStrokeJoin::Round);
  CHECK(line.stroke_style.dashes.size() == 2);
  CHECK(!line.path.subpaths.empty());
  CHECK(!line.path.subpaths[0].closed);

  // Even-odd fill: both subpaths in one shape group, which is core's even-odd rule.
  const auto& holes = sink.paths[2];
  CHECK(holes.fill_even_odd);
  CHECK(holes.path.subpaths.size() == 2);
  CHECK(holes.path.subpaths[0].shape_group == holes.path.subpaths[1].shape_group);
}

void pdf_interpreter_tracks_graphics_state_and_clipping() {
  const auto sink = interpret(
      "q 1 0 0 rg 0 0 5 5 re f Q\n"          // red inside the save
      "0 0 10 10 re f\n"                     // black again after the restore
      "q 0 0 50 50 re W n 0 0 100 100 re f Q\n"  // clipped fill
      "0 0 20 20 re f\n");                   // clip is gone after Q

  CHECK(sink.paths.size() == 4);
  if (sink.paths.size() != 4) {
    return;
  }
  CHECK(sink.paths[0].fill.color.red == 255);
  // q/Q restored the colour.
  CHECK(sink.paths[1].fill.color.red == 0);
  // W n set a clip that the following fill carries.
  CHECK(!sink.paths[2].clip.subpaths.empty());
  // Q popped it.
  CHECK(sink.paths[3].clip.subpaths.empty());

  // A second clip intersects rather than replacing, which core expresses as a
  // separate shape group combined with Intersect.
  const auto nested = interpret("q 0 0 50 50 re W n 10 10 30 30 re W n 0 0 100 100 re f Q\n");
  CHECK(nested.paths.size() == 1);
  if (!nested.paths.empty()) {
    const auto& clip = nested.paths[0].clip;
    CHECK(clip.subpaths.size() == 2);
    if (clip.subpaths.size() == 2) {
      CHECK(clip.subpaths[0].shape_group != clip.subpaths[1].shape_group);
      CHECK(clip.subpaths[1].op == patchy::PathCombineOp::Intersect);
    }
  }
}

void pdf_interpreter_converts_color_spaces() {
  // DeviceCMYK through the naive ink mix, and DeviceGray.
  const auto sink = interpret(
      "0 0 0 1 k 0 0 5 5 re f\n"        // pure black
      "0 1 1 0 k 0 0 5 5 re f\n"        // magenta + yellow = red
      "0.5 g 0 0 5 5 re f\n");
  CHECK(sink.paths.size() == 3);
  if (sink.paths.size() != 3) {
    return;
  }
  CHECK(sink.paths[0].fill.color.red == 0 && sink.paths[0].fill.color.blue == 0);
  CHECK(sink.paths[1].fill.color.red == 255);
  CHECK(sink.paths[1].fill.color.green == 0);
  CHECK(sink.paths[1].fill.color.blue == 0);
  CHECK(std::abs(static_cast<int>(sink.paths[2].fill.color.red) - 128) <= 1);

  // An Indexed palette resolves through its base space.
  const auto indexed = interpret(
      "/CS0 cs 1 sc 0 0 5 5 re f\n",
      {},
      "/Resources<</ColorSpace<</CS0[/Indexed /DeviceRGB 1 <FF000000FF00>]>>>>");
  CHECK(indexed.paths.size() == 1);
  if (!indexed.paths.empty()) {
    // Index 1 is the second RGB triple in the table: 00 FF 00.
    CHECK(indexed.paths[0].fill.color.green == 255);
    CHECK(indexed.paths[0].fill.color.red == 0);
  }

  // ICCBased takes its component count from /N without applying the profile.
  const auto icc = interpret("/CS1 cs 0 0 1 sc 0 0 5 5 re f\n",
                             "<</N 3/Length 0>>\nstream\n\nendstream",
                             "/Resources<</ColorSpace<</CS1[/ICCBased 5 0 R]>>>>");
  CHECK(icc.paths.size() == 1);
  if (!icc.paths.empty()) {
    CHECK(icc.paths[0].fill.color.blue == 255);
  }
}

void pdf_interpreter_extracts_text_runs_with_position() {
  const auto sink = interpret(
      "BT /F1 12 Tf 1 0 0 1 20 80 Tm (Hello) Tj ET\n"
      "BT /F1 12 Tf 14 TL 1 0 0 1 20 80 Tm (One) ' (Two) ' ET\n",
      "<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding"
      "/FirstChar 32/LastChar 122/Widths[500]>>",
      "/Resources<</Font<</F1 5 0 R>>>>");

  CHECK(sink.texts.size() == 3);
  if (sink.texts.size() != 3) {
    return;
  }
  CHECK(sink.texts[0].utf8 == "Hello");
  CHECK(std::abs(sink.texts[0].font_size - 12.0) < 1e-9);
  CHECK(sink.texts[0].family == "Helvetica");
  // Tm places the baseline at (20, 80) in PDF space, i.e. 20 down from the top of a
  // 100-point page.
  CHECK(std::abs(sink.texts[0].transform.e - 20.0) < 1e-9);
  CHECK(std::abs(sink.texts[0].transform.f - 20.0) < 1e-9);
  // The y axis is flipped, so the text matrix's vertical scale is negative.
  CHECK(sink.texts[0].transform.d < 0.0);

  // The quote operator advances by the leading each time.
  CHECK(sink.texts[1].utf8 == "One");
  CHECK(sink.texts[2].utf8 == "Two");
  CHECK(std::abs(sink.texts[2].transform.f - sink.texts[1].transform.f - 14.0) < 1e-6);
}

void pdf_interpreter_applies_kerning_and_spacing_to_text() {
  // A TJ array with a kerning adjustment, and Tc/Tw/Tz spacing.
  const auto sink = interpret(
      "BT /F1 10 Tf 1 0 0 1 0 90 Tm [(A) -1000 (B)] TJ ET\n"
      "BT /F1 10 Tf 2 Tc 1 0 0 1 0 50 Tm (AB) Tj ET\n"
      "BT /F1 10 Tf 200 Tz 1 0 0 1 0 20 Tm (AB) Tj ET\n",
      "<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding"
      "/FirstChar 65/LastChar 66/Widths[500 500]>>",
      "/Resources<</Font<</F1 5 0 R>>>>");

  CHECK(sink.texts.size() == 4);
  if (sink.texts.size() != 4) {
    return;
  }
  // The two halves of the TJ array become separate runs, and the -1000 pushes the
  // second one a full em to the right of where "A" left off.
  CHECK(sink.texts[0].utf8 == "A");
  CHECK(sink.texts[1].utf8 == "B");
  const double gap = sink.texts[1].transform.e - sink.texts[0].transform.e;
  // 'A' advances 0.5 em = 5 units, plus the 1000/1000 em = 10 unit kern.
  CHECK(std::abs(gap - 15.0) < 1e-6);

  // Character spacing widens the run's intended width: 2 glyphs x (5 + 2).
  CHECK(sink.texts[2].width_is_known);
  CHECK(std::abs(sink.texts[2].intended_width - 14.0) < 1e-6);
  // Horizontal scale doubles both the advance and the transform's x scale. The Tc of
  // 2 set in the previous block is STILL in effect: the text state parameters live
  // in the graphics state and are reset by q/Q, never by BT/ET (clause 9.3), so the
  // width here is 2 x (5 + 2) x 2, not 2 x 5 x 2.
  CHECK(std::abs(sink.texts[3].intended_width - 28.0) < 1e-6);
  CHECK(std::abs(sink.texts[3].transform.a - 20.0) < 1e-6);
}

void pdf_interpreter_recurses_into_form_xobjects() {
  const auto sink = interpret(
      "q 1 0 0 1 100 0 cm /Fm0 Do Q\n0 0 5 5 re f\n",
      "<</Type/XObject/Subtype/Form/BBox[0 0 50 50]/Matrix[1 0 0 1 0 0]/Length 24>>\n"
      "stream\n1 0 0 rg 0 0 20 20 re f\nendstream",
      "/Resources<</XObject<</Fm0 5 0 R>>>>");

  CHECK(sink.paths.size() == 2);
  if (sink.paths.size() != 2) {
    return;
  }
  // The form's rectangle is drawn through the outer cm, so it sits 100 to the right.
  const auto inner = sink.paths[0].path.bounds();
  CHECK(inner.has_value());
  if (inner.has_value()) {
    CHECK(std::abs(inner->left - 100.0) < 1e-9);
    CHECK(std::abs(inner->right - 120.0) < 1e-9);
  }
  // The form's BBox clips its contents.
  CHECK(!sink.paths[0].clip.subpaths.empty());
  // State is restored afterwards: the outer fill is black and unclipped.
  CHECK(sink.paths[1].fill.color.red == 0);
  CHECK(sink.paths[1].clip.subpaths.empty());
}

void pdf_interpreter_reads_images_and_ext_gstate() {
  // A 2x2 RGB image placed by a cm that maps the unit square, plus alpha and a
  // blend mode from the graphics state.
  // Built from an initializer list, not a literal: a string literal would stop dead
  // at the first embedded NUL and the image would silently be one byte long.
  const std::string samples({'\xFF', '\x00', '\x00', '\x00', '\xFF', '\x00', '\x00', '\x00', '\xFF', '\xFF',
                             '\xFF', '\xFF'});
  const auto sink = interpret(
      "q /GS0 gs 40 0 0 20 10 30 cm /Im0 Do Q\n",
      "<</Type/XObject/Subtype/Image/Width 2/Height 2/ColorSpace/DeviceRGB"
      "/BitsPerComponent 8/Length 12>>\nstream\n" + samples + "\nendstream",
      "/Resources<</XObject<</Im0 5 0 R>>/ExtGState<</GS0<</ca 0.5/BM/Multiply>>>>>>");

  CHECK(sink.images.size() == 1);
  if (sink.images.empty()) {
    return;
  }
  const auto& image = sink.images[0];
  CHECK(image.width == 2 && image.height == 2);
  CHECK(image.codec == patchy::pdf::FilterKind::None);
  CHECK(image.rgba.size() == 16);
  // Row 0 is red then green; samples are read top-down as PDF stores them.
  CHECK(image.rgba[0] == 255 && image.rgba[1] == 0 && image.rgba[2] == 0 && image.rgba[3] == 255);
  CHECK(image.rgba[4] == 0 && image.rgba[5] == 255);
  // The CTM maps the unit square onto the placement, so it carries the size.
  CHECK(std::abs(image.transform.a - 40.0) < 1e-9);
  CHECK(std::abs(std::abs(image.transform.d) - 20.0) < 1e-9);
  // ExtGState reached the placement.
  CHECK(std::abs(image.alpha - 0.5) < 1e-9);
  CHECK(image.blend == patchy::BlendMode::Multiply);
}

void pdf_interpreter_survives_damaged_content() {
  // Operators with missing operands, an unknown operator, unbalanced Q, a huge
  // operand count, and an unterminated string. None may hang, throw, or lose the
  // operators that follow.
  const auto sink = interpret(
      "Q Q Q\n"
      "re f\n"
      "1 2 3 4 5 6 7 8 9 bogusoperator\n"
      "0 0 1 rg 10 10 20 20 re f\n"
      "BT /Missing 12 Tf (text) Tj ET\n");
  // The one well-formed fill still arrives.
  bool found_blue = false;
  for (const auto& path : sink.paths) {
    if (path.fill.color.blue == 255) {
      found_blue = true;
    }
  }
  CHECK(found_blue);
}

void pdf_local_brochure_interprets_to_shapes_and_text_if_available() {
  const auto path = patchy::test::local_format_fixture_path("pdf", "HoloVCS_C2_A4_Brochure.pdf");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local pdf fixture missing: " << path.string() << '\n';
    return;
  }
  std::ifstream stream(path, std::ios::binary);
  CHECK(stream.good());
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                        std::istreambuf_iterator<char>());
  auto file = patchy::pdf::File::open(bytes, nullptr);
  CHECK(file.has_value());
  if (!file.has_value() || file->pages().empty()) {
    return;
  }

  RecordingSink sink;
  patchy::pdf::execute_page(*file, file->pages().front(), 1.0, sink);

  // The page is 525 rectangles, 44 text runs, a few strokes, and 2 images.
  CHECK(sink.paths.size() > 400);
  CHECK(sink.texts.size() >= 40);
  CHECK(sink.images.size() == 2);

  // Real authored strings come back as text, which is the whole point.
  const auto joined = sink.all_text();
  CHECK(joined.find("HoloVCS") != std::string::npos);
  CHECK(joined.find("OPEN SOURCE / LIVE DEMO") != std::string::npos);
  CHECK(joined.find("CLASSIC GAMES. REAL DEPTH.") != std::string::npos);

  // Every run carries a resolvable family with the subset tag stripped, a real
  // size, and a position inside the A4 page.
  for (const auto& run : sink.texts) {
    CHECK(!run.family.empty());
    CHECK(run.family.find('+') == std::string::npos);
    CHECK(run.font_size > 0.0 && run.font_size < 200.0);
    CHECK(run.transform.e >= -50.0 && run.transform.e <= 650.0);
    CHECK(run.transform.f >= -50.0 && run.transform.f <= 900.0);
  }

  // The images decode: one is a JPEG handed on still encoded, and the ones we
  // expand carry the SMask alpha the file supplies.
  for (const auto& image : sink.images) {
    CHECK(image.width > 0 && image.height > 0);
    CHECK(!image.rgba.empty() || !image.encoded.empty());
  }

  // Fills are real colours, not all black.
  bool saw_non_black = false;
  for (const auto& painted : sink.paths) {
    if (painted.has_fill && (painted.fill.color.red != 0 || painted.fill.color.green != 0 ||
                             painted.fill.color.blue != 0)) {
      saw_non_black = true;
      break;
    }
  }
  CHECK(saw_non_black);
}

patchy::pdf::VectorReadResult read_vectors(const std::vector<std::uint8_t>& bytes, int page = 0,
                                           double pixels_per_point = 1.0) {
  patchy::pdf::VectorReadOptions options;
  options.page = page;
  options.pixels_per_point = pixels_per_point;
  return patchy::pdf::read_page_as_vectors(bytes, options);
}

void pdf_vector_import_builds_shape_layers() {
  const std::string content =
      "1 0 0 rg 10 20 30 40 re f\n"
      "0 0 1 RG 3 w 5 5 m 60 70 l S\n";
  const auto bytes = build_pdf({
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]/Contents 4 0 R>>",
      "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream",
  });

  const auto result = read_vectors(bytes);
  CHECK(result.shape_layers == 2);
  CHECK(result.text_layers == 0);
  CHECK(result.document.width() == 200);
  CHECK(result.document.height() == 100);
  CHECK(result.document.layers().size() == 2);
  if (result.document.layers().size() != 2) {
    return;
  }

  // Both are real vector shape layers, not baked pixels.
  const auto& filled = result.document.layers()[0];
  CHECK(patchy::layer_is_vector_shape(filled));
  CHECK(filled.vector_shape() != nullptr);
  if (filled.vector_shape() != nullptr) {
    CHECK(filled.vector_shape()->fill.kind == patchy::VectorFillKind::Solid);
    CHECK(filled.vector_shape()->fill.color.red == 255);
    CHECK(!filled.vector_shape()->stroke.enabled);
  }
  // The raster cache is baked so the layer composites without a re-render.
  CHECK(!filled.pixels().empty());

  const auto& stroked = result.document.layers()[1];
  CHECK(stroked.vector_shape() != nullptr);
  if (stroked.vector_shape() != nullptr) {
    CHECK(stroked.vector_shape()->stroke.enabled);
    CHECK(std::abs(stroked.vector_shape()->stroke.width - 3.0) < 1e-9);
    CHECK(stroked.vector_shape()->stroke.content.color.blue == 255);
    CHECK(stroked.vector_shape()->stroke.alignment == patchy::VectorStrokeAlignment::Center);
  }

  // The page's resolution rides on the document, so it prints at its authored size.
  CHECK(std::abs(result.document.print_settings().horizontal_ppi - 72.0) < 1e-9);

  // Doubling the scale doubles the canvas and the resolution together.
  const auto scaled = read_vectors(bytes, 0, 2.0);
  CHECK(scaled.document.width() == 400);
  CHECK(std::abs(scaled.document.print_settings().horizontal_ppi - 144.0) < 1e-9);
}

void pdf_vector_import_applies_nonzero_holes() {
  // A square with a reverse-wound square inside it: nonzero makes the inner one a
  // hole, and core expresses that as a Subtract shape group.
  const std::string content =
      "0 g 0 0 100 100 re 20 80 m 80 80 l 80 20 l 20 20 l h f\n";
  const auto bytes = build_pdf({
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 100 100]/Contents 4 0 R>>",
      "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream",
  });
  const auto result = read_vectors(bytes);
  CHECK(result.document.layers().size() == 1);
  if (result.document.layers().empty()) {
    return;
  }
  const auto* shape = result.document.layers()[0].vector_shape();
  CHECK(shape != nullptr);
  if (shape == nullptr) {
    return;
  }
  CHECK(shape->path.subpaths.size() == 2);
  if (shape->path.subpaths.size() == 2) {
    // Separate groups, and the contained one subtracts.
    CHECK(shape->path.subpaths[0].shape_group != shape->path.subpaths[1].shape_group);
    CHECK(shape->path.subpaths[1].op == patchy::PathCombineOp::Subtract);
  }

  // The same geometry with f* keeps one group, which is core's even-odd rule.
  const std::string even_odd_content =
      "0 g 0 0 100 100 re 20 80 m 80 80 l 80 20 l 20 20 l h f*\n";
  const auto even_odd = read_vectors(build_pdf({
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 100 100]/Contents 4 0 R>>",
      "<</Length " + std::to_string(even_odd_content.size()) + ">>\nstream\n" + even_odd_content + "\nendstream",
  }));
  const auto* even_odd_shape = even_odd.document.layers()[0].vector_shape();
  CHECK(even_odd_shape != nullptr);
  if (even_odd_shape != nullptr && even_odd_shape->path.subpaths.size() == 2) {
    CHECK(even_odd_shape->path.subpaths[0].shape_group == even_odd_shape->path.subpaths[1].shape_group);
  }
}

void pdf_vector_import_builds_editable_text_layers() {
  const std::string content = "BT /F1 18 Tf 1 0 0 1 20 60 Tm 0 0 1 rg (Hello PDF) Tj ET\n";
  const auto bytes = build_pdf({
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]/Contents 4 0 R"
      "/Resources<</Font<</F1 5 0 R>>>>>>",
      "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream",
      "<</Type/Font/Subtype/TrueType/BaseFont/ABCDEF+SegoeUI-Bold/Encoding/WinAnsiEncoding"
      "/FirstChar 32/LastChar 122/Widths[500]>>",
  });

  const auto result = read_vectors(bytes);
  CHECK(result.text_layers == 1);
  CHECK(result.document.layers().size() == 1);
  if (result.document.layers().empty()) {
    return;
  }

  const auto& layer = result.document.layers()[0];
  // A real, editable text layer: the predicate the whole text pipeline keys off.
  CHECK(patchy::layer_is_text(layer));
  const auto& metadata = layer.metadata();
  CHECK(metadata.at(patchy::kLayerMetadataText) == "Hello PDF");
  // The layer is named after its own words, not "Layer 1".
  CHECK(layer.name() == "Hello PDF");
  // Subset tag stripped, style split, bold detected from the name.
  CHECK(metadata.at(patchy::kLayerMetadataTextFont) == "SegoeUI");
  CHECK(metadata.at(patchy::kLayerMetadataTextBold) == "1");
  CHECK(metadata.at(patchy::kLayerMetadataTextColor) == "#0000ff");
  CHECK(metadata.at(patchy::kLayerMetadataTextSize) == "18");
  // Style runs carry the fractional size and the family the missing-font warning reads.
  CHECK(metadata.contains(patchy::kLayerMetadataTextRuns));
  // The Qt side is told to finish the job, and given the matrix and target width.
  CHECK(metadata.at(patchy::kLayerMetadataPdfPendingText) == "1");
  CHECK(metadata.contains(patchy::kLayerMetadataPdfTextXfrm));
  CHECK(metadata.contains(patchy::kLayerMetadataPdfTextIntendedWidth));
  // The matrix must place the baseline 40 pixels down a 100-point page (100 - 60).
  const auto transform = metadata.at(patchy::kLayerMetadataPdfTextXfrm);
  CHECK(transform.find("20 ") != std::string::npos);
  CHECK(transform.find(" 40") != std::string::npos);
}

void pdf_vector_import_places_images_as_smart_objects() {
  const std::string samples({'\xFF', '\x00', '\x00', '\x00', '\xFF', '\x00', '\x00', '\x00', '\xFF', '\xFF',
                             '\xFF', '\xFF'});
  // A rotated placement, which is exactly what a plain pixel layer would lose.
  const std::string content = "q 0 40 -40 0 60 20 cm /Im0 Do Q\n";
  const auto bytes = build_pdf({
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]/Contents 4 0 R"
      "/Resources<</XObject<</Im0 5 0 R>>>>>>",
      "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream",
      "<</Type/XObject/Subtype/Image/Width 2/Height 2/ColorSpace/DeviceRGB"
      "/BitsPerComponent 8/Length 12>>\nstream\n" + samples + "\nendstream",
  });

  const auto result = read_vectors(bytes);
  CHECK(result.image_layers == 1);
  CHECK(result.document.layers().size() == 1);
  if (result.document.layers().empty()) {
    return;
  }

  const auto& layer = result.document.layers()[0];
  CHECK(patchy::layer_is_smart_object(layer));
  CHECK(layer.metadata().at(patchy::kLayerMetadataPdfPendingImage) == "1");

  const auto placement = patchy::smart_object_placement_from_layer(layer);
  CHECK(placement.has_value());
  if (placement.has_value()) {
    CHECK(std::abs(placement->width - 2.0) < 1e-9);
    CHECK(std::abs(placement->height - 2.0) < 1e-9);
    // A rotated quad: the four corners are NOT an axis-aligned rectangle, which is
    // the whole reason images go in as smart objects.
    const bool axis_aligned = std::abs(placement->transform[0] - placement->transform[6]) < 1e-6 &&
                              std::abs(placement->transform[1] - placement->transform[3]) < 1e-6;
    CHECK(!axis_aligned);
  }

  // The bytes really are in the store, as a decodable PNG.
  const auto uuid = patchy::smart_object_source_uuid(layer);
  CHECK(!uuid.empty());
  const auto* source = result.document.metadata().smart_objects.find(uuid);
  CHECK(source != nullptr);
  if (source != nullptr && source->file_bytes != nullptr) {
    CHECK(source->filetype == "png ");
    const auto& png = *source->file_bytes;
    CHECK(png.size() > 8);
    // The PNG signature, so this is a real file and not a raw sample dump.
    CHECK(png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
  }
}

void pdf_vector_import_keeps_jpeg_bytes_untranscoded() {
  // A DCTDecode image must reach the smart object as the file's own JPEG bytes.
  const std::string jpeg({'\xFF', '\xD8', '\xFF', '\xE0', '\x00', '\x10', 'J', 'F', 'I', 'F', '\x00', '\xFF',
                          '\xD9'});
  const std::string content = "q 40 0 0 20 10 30 cm /Im0 Do Q\n";
  const auto bytes = build_pdf({
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]/Contents 4 0 R"
      "/Resources<</XObject<</Im0 5 0 R>>>>>>",
      "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream",
      "<</Type/XObject/Subtype/Image/Width 8/Height 8/ColorSpace/DeviceRGB/Filter/DCTDecode"
      "/BitsPerComponent 8/Length " + std::to_string(jpeg.size()) + ">>\nstream\n" + jpeg + "\nendstream",
  });

  const auto result = read_vectors(bytes);
  CHECK(result.image_layers == 1);
  if (result.document.layers().empty()) {
    return;
  }
  const auto uuid = patchy::smart_object_source_uuid(result.document.layers()[0]);
  const auto* source = result.document.metadata().smart_objects.find(uuid);
  CHECK(source != nullptr);
  if (source != nullptr && source->file_bytes != nullptr) {
    CHECK(source->filetype == "JPEG");
    // Byte-for-byte the PDF's own stream: nothing was decoded and re-encoded. The
    // comparison casts because char is signed here, so '\xFF' would otherwise never
    // equal the stored 255.
    const auto& stored = *source->file_bytes;
    CHECK(stored.size() == jpeg.size());
    CHECK(std::equal(stored.begin(), stored.end(), jpeg.begin(),
                     [](std::uint8_t left, char right) { return left == static_cast<std::uint8_t>(right); }));
  }
}

void pdf_vector_import_reports_what_it_cannot_model() {
  // A shading pattern fill: imported as a flat colour, and said so.
  const std::string content = "/Pattern cs /P0 scn 0 0 100 100 re f\n";
  const auto bytes = build_pdf({
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R]/Count 1>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 100 100]/Contents 4 0 R"
      "/Resources<</Pattern<</P0 5 0 R>>>>>>",
      "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream",
      "<</PatternType 2/Shading<</ShadingType 2/ColorSpace/DeviceRGB>>>>",
  });
  const auto result = read_vectors(bytes);
  CHECK(result.has_unmodelled_content);
  CHECK(!result.notices.empty());
  bool mentioned = false;
  for (const auto& text : result.notices) {
    if (text.find("gradient") != std::string::npos) {
      mentioned = true;
    }
  }
  CHECK(mentioned);
}

void pdf_vector_import_refuses_what_it_must() {
  // An empty page has nothing to make editable, and saying so lets the caller offer
  // the rasterizing importer instead of producing a blank document.
  bool threw = false;
  try {
    (void)read_vectors(build_pdf({
        "<</Type/Catalog/Pages 2 0 R>>",
        "<</Type/Pages/Kids[3 0 R]/Count 1>>",
        "<</Type/Page/Parent 2 0 R/MediaBox[0 0 100 100]>>",
    }));
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);

  // An out-of-range page index.
  threw = false;
  try {
    (void)read_vectors(two_page_pdf(), 9);
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);

  // Not a PDF at all.
  threw = false;
  try {
    (void)read_vectors(bytes_of("this is not a pdf"));
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);

  CHECK(patchy::pdf::sniff(as_bytes("%PDF-1.7\n")));
  CHECK(!patchy::pdf::sniff(as_bytes("GIF89a")));
  CHECK(patchy::pdf::page_count(two_page_pdf()) == 2);
}

void pdf_local_brochure_imports_as_editable_layers_if_available() {
  const auto path = patchy::test::local_format_fixture_path("pdf", "HoloVCS_C2_A4_Brochure.pdf");
  if (!std::filesystem::exists(path)) {
    std::cout << "[SKIP] local pdf fixture missing: " << path.string() << '\n';
    return;
  }
  std::ifstream stream(path, std::ios::binary);
  CHECK(stream.good());
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                        std::istreambuf_iterator<char>());

  // 150 ppi, a realistic import setting.
  const auto result = read_vectors(bytes, 0, 150.0 / 72.0);

  // A4 at 150 ppi is about 1240 x 1754 pixels.
  CHECK(std::abs(result.document.width() - 1240) <= 2);
  CHECK(std::abs(result.document.height() - 1754) <= 2);
  CHECK(std::abs(result.document.print_settings().horizontal_ppi - 150.0) < 0.01);

  CHECK(result.shape_layers > 400);
  CHECK(result.text_layers >= 40);
  CHECK(result.image_layers == 2);

  // Every layer is a real editable object of the right kind, and the text ones say
  // what they say.
  int vector_shapes = 0;
  int text_layers = 0;
  int smart_objects = 0;
  bool saw_headline = false;
  for (const auto& layer : result.document.layers()) {
    if (patchy::layer_is_text(layer)) {
      ++text_layers;
      const auto& text = layer.metadata().at(patchy::kLayerMetadataText);
      if (text.find("HoloVCS") != std::string::npos) {
        saw_headline = true;
      }
      // Each carries a resolvable family and the pending-render marker.
      CHECK(!layer.metadata().at(patchy::kLayerMetadataTextFont).empty());
      CHECK(layer.metadata().contains(patchy::kLayerMetadataPdfPendingText));
    } else if (patchy::layer_is_smart_object(layer)) {
      ++smart_objects;
    } else if (patchy::layer_is_vector_shape(layer)) {
      ++vector_shapes;
      CHECK(layer.vector_shape() != nullptr);
    }
  }
  CHECK(saw_headline);
  CHECK(vector_shapes == result.shape_layers);
  CHECK(text_layers == result.text_layers);
  CHECK(smart_objects == result.image_layers);

  // Both images made it into the store with real bytes.
  for (const auto& layer : result.document.layers()) {
    if (!patchy::layer_is_smart_object(layer)) {
      continue;
    }
    const auto* source = result.document.metadata().smart_objects.find(patchy::smart_object_source_uuid(layer));
    CHECK(source != nullptr);
    if (source != nullptr) {
      CHECK(source->file_bytes != nullptr && !source->file_bytes->empty());
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
      {"pdf_font_names_split_subset_tags_and_styles", pdf_font_names_split_subset_tags_and_styles},
      {"pdf_glyph_names_map_to_unicode", pdf_glyph_names_map_to_unicode},
      {"pdf_to_unicode_cmap_reads_chars_and_ranges", pdf_to_unicode_cmap_reads_chars_and_ranges},
      {"pdf_simple_font_encodings_decode_text", pdf_simple_font_encodings_decode_text},
      {"pdf_composite_font_reads_two_byte_codes_and_w_widths", pdf_composite_font_reads_two_byte_codes_and_w_widths},
      {"pdf_local_real_documents_parse_if_available", pdf_local_real_documents_parse_if_available},
      {"pdf_local_real_document_fonts_decode_if_available", pdf_local_real_document_fonts_decode_if_available},
      {"pdf_page_transform_flips_and_rotates", pdf_page_transform_flips_and_rotates},
      {"pdf_interpreter_builds_paths_with_colors_and_winding", pdf_interpreter_builds_paths_with_colors_and_winding},
      {"pdf_interpreter_tracks_graphics_state_and_clipping", pdf_interpreter_tracks_graphics_state_and_clipping},
      {"pdf_interpreter_converts_color_spaces", pdf_interpreter_converts_color_spaces},
      {"pdf_interpreter_extracts_text_runs_with_position", pdf_interpreter_extracts_text_runs_with_position},
      {"pdf_interpreter_applies_kerning_and_spacing_to_text", pdf_interpreter_applies_kerning_and_spacing_to_text},
      {"pdf_interpreter_recurses_into_form_xobjects", pdf_interpreter_recurses_into_form_xobjects},
      {"pdf_interpreter_reads_images_and_ext_gstate", pdf_interpreter_reads_images_and_ext_gstate},
      {"pdf_interpreter_survives_damaged_content", pdf_interpreter_survives_damaged_content},
      {"pdf_local_brochure_interprets_to_shapes_and_text_if_available", pdf_local_brochure_interprets_to_shapes_and_text_if_available},
      {"pdf_vector_import_builds_shape_layers", pdf_vector_import_builds_shape_layers},
      {"pdf_vector_import_applies_nonzero_holes", pdf_vector_import_applies_nonzero_holes},
      {"pdf_vector_import_builds_editable_text_layers", pdf_vector_import_builds_editable_text_layers},
      {"pdf_vector_import_places_images_as_smart_objects", pdf_vector_import_places_images_as_smart_objects},
      {"pdf_vector_import_keeps_jpeg_bytes_untranscoded", pdf_vector_import_keeps_jpeg_bytes_untranscoded},
      {"pdf_vector_import_reports_what_it_cannot_model", pdf_vector_import_reports_what_it_cannot_model},
      {"pdf_vector_import_refuses_what_it_must", pdf_vector_import_refuses_what_it_must},
      {"pdf_local_brochure_imports_as_editable_layers_if_available", pdf_local_brochure_imports_as_editable_layers_if_available},
  };
}
