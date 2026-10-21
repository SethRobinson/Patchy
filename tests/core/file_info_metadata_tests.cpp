#include "formats/pdf_crypt.hpp"
// File Info metadata (docs/file-info.md): the Qt-free parsers, container scanners and
// views in src/formats/metadata. Every byte stream here is built by the test itself.

#include "core/document_file_info.hpp"
#include "formats/metadata/exif_tags.hpp"
#include "formats/metadata/iptc_iim.hpp"
#include "formats/metadata/jumbf.hpp"
#include "formats/metadata/metadata_containers.hpp"
#include "formats/metadata/metadata_edit.hpp"
#include "formats/metadata/metadata_inject.hpp"
#include "formats/metadata/metadata_view.hpp"
#include "formats/metadata/tiff_ifd.hpp"
#include "formats/metadata/xmp.hpp"
#include "formats/miniz/miniz.h"
#include "formats/stb/stb_image.h"
#include "psd/psd_document_io.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"
#include "local_psd_fixtures.hpp"
#include "unicode_path_names.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace md = patchy::metadata;
using Bytes = std::vector<std::uint8_t>;

Bytes bytes_of(std::string_view text) {
  return {text.begin(), text.end()};
}

void append(Bytes& out, std::string_view text) {
  out.insert(out.end(), text.begin(), text.end());
}

void append(Bytes& out, const Bytes& more) {
  out.insert(out.end(), more.begin(), more.end());
}

void put_be16(Bytes& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void put_be32(Bytes& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void put_le32(Bytes& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

std::uint64_t splitmix64(std::uint64_t& state) {
  std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

// A rational entry with several values, encoded in the stream's byte order.
md::TiffEntry rationals(const md::TiffStream& stream, std::uint16_t tag,
                        std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> values) {
  md::TiffEntry entry{tag, static_cast<std::uint16_t>(md::TiffType::Rational),
                      static_cast<std::uint32_t>(values.size()), {}};
  for (const auto& [numerator, denominator] : values) {
    for (const auto value : {numerator, denominator}) {
      for (int i = 0; i < 4; ++i) {
        const int shift = stream.big_endian ? 8 * (3 - i) : 8 * i;
        entry.data.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
      }
    }
  }
  return entry;
}

const Bytes kFakeThumbnail = {0xFF, 0xD8, 0xFF, 0xDB, 0x00, 0x02, 0xFF, 0xD9};

md::TiffStream sample_exif_stream(bool big_endian) {
  md::TiffStream stream;
  stream.big_endian = big_endian;
  stream.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kMake, "PatchyCam");
  stream.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kModel, "Model X");
  stream.set_short(md::IfdKind::Ifd0, md::exif_tag::kOrientation, 6);
  stream.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kArtist, "Ann Example");
  stream.set_rational(md::IfdKind::Exif, md::exif_tag::kExposureTime, 1, 250);
  stream.set_rational(md::IfdKind::Exif, md::exif_tag::kFNumber, 28, 10);
  stream.set_ascii(md::IfdKind::Exif, md::exif_tag::kBodySerialNumber, "SN123");
  const Bytes maker_note = {'M', 'a', 'k', 'e', 'r', 0, 1, 2, 3, 4};
  stream.set_undefined(md::IfdKind::Exif, md::exif_tag::kMakerNote, maker_note);
  stream.set_ascii(md::IfdKind::Gps, md::exif_tag::kGpsLatitudeRef, "N");
  stream.find_or_add(md::IfdKind::Gps).entries.push_back(rationals(stream, md::exif_tag::kGpsLatitude, {{47, 1}, {36, 1}, {0, 1}}));
  stream.set_ascii(md::IfdKind::Gps, md::exif_tag::kGpsLongitudeRef, "W");
  stream.find_or_add(md::IfdKind::Gps).entries.push_back(
      rationals(stream, md::exif_tag::kGpsLongitude, {{122, 1}, {19, 1}, {3000, 100}}));
  stream.set_rational(md::IfdKind::Gps, md::exif_tag::kGpsAltitude, 100, 1);
  stream.find_or_add(md::IfdKind::Ifd1);
  stream.set_short(md::IfdKind::Ifd1, 259, 6);
  stream.thumbnail = kFakeThumbnail;
  return stream;
}

const std::string kSampleXmp = R"(<?xpacket begin="" id="W5M0MpCehiHzreSzNTczkc9d"?>
<x:xmpmeta xmlns:x="adobe:ns:meta/" xmlns:dc="http://purl.org/dc/elements/1.1/">
 <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
  <rdf:Description rdf:about=""
    xmlns:xmp="http://ns.adobe.com/xap/1.0/"
    xmlns:ps="http://ns.adobe.com/photoshop/1.0/"
    xmlns:xmpMM="http://ns.adobe.com/xap/1.0/mm/"
    xmlns:stEvt="http://ns.adobe.com/xap/1.0/sType/ResourceEvent#"
    xmlns:foo="http://example.com/foo/"
    xmp:CreatorTool="Example Editor 1.0"
    ps:AuthorsPosition="Photographer">
   <dc:title><rdf:Alt><rdf:li xml:lang="x-default">Sunset &amp; Sea</rdf:li><rdf:li xml:lang="fr">Coucher</rdf:li></rdf:Alt></dc:title>
   <dc:creator><rdf:Seq><rdf:li>Ann Example</rdf:li></rdf:Seq></dc:creator>
   <dc:subject><rdf:Bag><rdf:li>beach</rdf:li><rdf:li>sunset</rdf:li></rdf:Bag></dc:subject>
   <xmpMM:History><rdf:Seq>
     <rdf:li rdf:parseType="Resource"><stEvt:action>created</stEvt:action><stEvt:when>2026-01-02T03:04:05-07:00</stEvt:when></rdf:li>
     <rdf:li stEvt:action="saved" stEvt:softwareAgent="Example Editor 1.0"/>
   </rdf:Seq></xmpMM:History>
   <ps:DocumentAncestors><rdf:Bag><rdf:li>xmp.did:1</rdf:li><rdf:li>xmp.did:2</rdf:li><rdf:li>xmp.did:3</rdf:li></rdf:Bag></ps:DocumentAncestors>
   <ps:TextLayers><rdf:Seq><rdf:li ps:LayerName="Caption" ps:LayerText="Secret text"/></rdf:Seq></ps:TextLayers>
   <foo:Custom><rdf:Description foo:inner="kept"/></foo:Custom>
  </rdf:Description>
 </rdf:RDF>
</x:xmpmeta>
<?xpacket end="w"?>)";

Bytes fake_icc(std::string_view description, std::string_view space = "RGB ") {
  Bytes profile(128, 0);
  profile[8] = 4;
  profile[9] = 0x30;
  std::memcpy(profile.data() + 12, "mntr", 4);
  std::memcpy(profile.data() + 16, space.data(), 4);
  std::memcpy(profile.data() + 20, "XYZ ", 4);
  std::memcpy(profile.data() + 36, "acsp", 4);
  put_be32(profile, 1);  // tag count
  Bytes desc;
  append(desc, "desc");
  put_be32(desc, 0);
  put_be32(desc, static_cast<std::uint32_t>(description.size() + 1));
  append(desc, description);
  desc.push_back(0);
  put_be32(profile, 0x64657363);  // 'desc'
  put_be32(profile, 144);
  put_be32(profile, static_cast<std::uint32_t>(desc.size()));
  append(profile, desc);
  const auto size = static_cast<std::uint32_t>(profile.size());
  profile[0] = static_cast<std::uint8_t>(size >> 24);
  profile[1] = static_cast<std::uint8_t>(size >> 16);
  profile[2] = static_cast<std::uint8_t>(size >> 8);
  profile[3] = static_cast<std::uint8_t>(size);
  return profile;
}

Bytes jpeg_segment(std::uint8_t marker, const Bytes& payload) {
  Bytes out{0xFF, marker};
  put_be16(out, static_cast<std::uint32_t>(payload.size() + 2));
  append(out, payload);
  return out;
}

Bytes sample_iim() {
  md::IimBlock block;
  block.set_values(2, md::iim::kByline, {"Ann Example"});
  block.set_values(2, md::iim::kKeywords, {"beach", "sunset"});
  block.set_values(2, md::iim::kCity, {"Seattle"});
  return md::write_iim(block);
}

Bytes jumbf_box(std::string_view type, const Bytes& payload) {
  Bytes out;
  put_be32(out, static_cast<std::uint32_t>(payload.size() + 8));
  append(out, type);
  append(out, payload);
  return out;
}

Bytes jumbf_superbox(std::string_view label, const Bytes& content) {
  // Reserved up front: growing in place makes GCC 15 report a false -Warray-bounds.
  Bytes description;
  description.reserve(18 + label.size());
  description.assign(16, 0x11);
  description.push_back(0x03);  // requestable + label present
  append(description, label);
  description.push_back(0);
  Bytes payload = jumbf_box("jumd", description);
  append(payload, content);
  return jumbf_box("jumb", payload);
}

Bytes cbor_text(std::string_view text) {
  Bytes out;
  if (text.size() < 24) {
    out.push_back(static_cast<std::uint8_t>(0x60 | text.size()));
  } else {
    out.push_back(0x78);
    out.push_back(static_cast<std::uint8_t>(text.size()));
  }
  append(out, text);
  return out;
}

Bytes sample_c2pa() {
  Bytes claim{0xA1};
  append(claim, cbor_text("claim_generator"));
  append(claim, cbor_text("Example Generator 2.0"));
  Bytes actions{0xA1};
  append(actions, cbor_text("actions"));
  actions.push_back(0x81);
  actions.push_back(0xA2);
  append(actions, cbor_text("action"));
  append(actions, cbor_text("c2pa.created"));
  append(actions, cbor_text("digitalSourceType"));
  append(actions,
         cbor_text("http://cv.iptc.org/newscodes/digitalsourcetype/trainedAlgorithmicMedia"));
  Bytes inner = jumbf_superbox("c2pa.claim", jumbf_box("cbor", claim));
  append(inner, jumbf_superbox("c2pa.actions", jumbf_box("cbor", actions)));
  return jumbf_superbox("c2pa", inner);
}

Bytes sample_jpeg(bool with_extended_xmp = false) {
  Bytes out{0xFF, 0xD8};
  {
    Bytes jfif;
    append(jfif, std::string_view("JFIF\0", 5));
    jfif.insert(jfif.end(), {1, 2, 1, 0, 72, 0, 72, 0, 0});
    append(out, jpeg_segment(0xE0, jfif));
  }
  {
    Bytes exif;
    append(exif, std::string_view("Exif\0\0", 6));
    append(exif, md::write_tiff(sample_exif_stream(false)));
    append(out, jpeg_segment(0xE1, exif));
  }
  std::string standard = kSampleXmp;
  std::string extension;
  std::string guid;
  if (with_extended_xmp) {
    extension = R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"><rdf:Description xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:description><rdf:Alt><rdf:li xml:lang="x-default">From the extension</rdf:li></rdf:Alt></dc:description></rdf:Description></rdf:RDF></x:xmpmeta>)";
    const auto digest = patchy::pdf::md5(bytes_of(extension));
    for (const auto byte : digest) {
      char hex[3];
      std::snprintf(hex, sizeof(hex), "%02X", byte);
      guid += hex;
    }
    const auto at = standard.find("ps:AuthorsPosition=");
    standard.insert(at, "xmlns:xmpNote=\"http://ns.adobe.com/xmp/note/\" xmpNote:HasExtendedXMP=\"" + guid + "\" ");
  }
  {
    Bytes xmp;
    append(xmp, std::string_view("http://ns.adobe.com/xap/1.0/\0", 29));
    append(xmp, standard);
    append(out, jpeg_segment(0xE1, xmp));
  }
  if (with_extended_xmp) {
    const auto half = extension.size() / 2;
    for (std::size_t offset : {half, std::size_t{0}}) {  // out of order on purpose
      Bytes chunk;
      append(chunk, std::string_view("http://ns.adobe.com/xmp/extension/\0", 35));
      append(chunk, guid);
      put_be32(chunk, static_cast<std::uint32_t>(extension.size()));
      put_be32(chunk, static_cast<std::uint32_t>(offset));
      const auto end = offset == 0 ? half : extension.size();
      append(chunk, std::string_view(extension).substr(offset, end - offset));
      append(out, jpeg_segment(0xE1, chunk));
    }
  }
  const auto icc = fake_icc("Patchy Test RGB");
  const auto half = icc.size() / 2;
  for (std::uint8_t sequence : {std::uint8_t{2}, std::uint8_t{1}}) {
    Bytes chunk;
    append(chunk, std::string_view("ICC_PROFILE\0", 12));
    chunk.push_back(sequence);
    chunk.push_back(2);
    if (sequence == 1) {
      chunk.insert(chunk.end(), icc.begin(), icc.begin() + static_cast<std::ptrdiff_t>(half));
    } else {
      chunk.insert(chunk.end(), icc.begin() + static_cast<std::ptrdiff_t>(half), icc.end());
    }
    append(out, jpeg_segment(0xE2, chunk));
  }
  {
    const auto iim = sample_iim();
    std::vector<md::IrbResource> resources;
    resources.push_back(md::IrbResource{0x3842494DU, md::psd_resource::kIptc, {}, iim});
    const auto digest = md::iim_digest(iim);
    resources.push_back(md::IrbResource{0x3842494DU, md::psd_resource::kCaptionDigest, {}, Bytes(digest.begin(), digest.end())});
    resources.push_back(md::IrbResource{0x3842494DU, md::psd_resource::kCopyrightFlag, {}, {1}});
    Bytes app13;
    append(app13, std::string_view("Photoshop 3.0\0", 14));
    append(app13, md::write_irb(resources));
    append(out, jpeg_segment(0xED, app13));
  }
  {
    Bytes app11{'J', 'P', 0, 1};
    put_be32(app11, 1);
    append(app11, sample_c2pa());
    append(out, jpeg_segment(0xEB, app11));
  }
  append(out, jpeg_segment(0xFE, bytes_of("hello comment")));
  append(out, jpeg_segment(0xDA, Bytes{1, 1, 0, 0, 63, 0}));
  out.insert(out.end(), {0x12, 0xFF, 0x00, 0x34, 0xFF, 0xD0, 0x56});  // stuffed byte + restart marker
  out.insert(out.end(), {0xFF, 0xD9});
  append(out, bytes_of("TRAILER!!"));
  return out;
}

Bytes png_chunk(std::string_view type, const Bytes& payload) {
  Bytes out;
  put_be32(out, static_cast<std::uint32_t>(payload.size()));
  Bytes typed;
  append(typed, type);
  append(typed, payload);
  append(out, typed);
  put_be32(out, static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, typed.data(), typed.size())));
  return out;
}

Bytes zlib(const Bytes& data) {
  mz_ulong size = mz_compressBound(static_cast<mz_ulong>(data.size()));
  Bytes out(size);
  CHECK(mz_compress(out.data(), &size, data.data(), static_cast<mz_ulong>(data.size())) == MZ_OK);
  out.resize(size);
  return out;
}

const std::string kA1111Parameters =
    "a lighthouse at dusk, oil painting\nNegative prompt: blurry, lowres\nSteps: 30, Sampler: Euler a, Seed: 42";

Bytes sample_png() {
  Bytes out;
  append(out, std::string_view("\x89PNG\r\n\x1A\n", 8));
  Bytes ihdr;
  put_be32(ihdr, 1);
  put_be32(ihdr, 1);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  append(out, png_chunk("IHDR", ihdr));
  {
    Bytes iccp;
    append(iccp, std::string_view("test\0", 5));
    iccp.push_back(0);
    append(iccp, zlib(fake_icc("PNG Profile")));
    append(out, png_chunk("iCCP", iccp));
  }
  {
    Bytes gama;
    put_be32(gama, 45455);
    append(out, png_chunk("gAMA", gama));
  }
  {
    Bytes text;
    append(text, std::string_view("parameters\0", 11));
    append(text, kA1111Parameters);
    append(out, png_chunk("tEXt", text));
  }
  {
    Bytes ztxt;
    append(ztxt, std::string_view("Comment\0", 8));
    ztxt.push_back(0);
    append(ztxt, zlib(bytes_of("compressed comment")));
    append(out, png_chunk("zTXt", ztxt));
  }
  {
    Bytes itxt;
    append(itxt, std::string_view("XML:com.adobe.xmp\0\0\0\0\0", 22));
    append(itxt, kSampleXmp);
    append(out, png_chunk("iTXt", itxt));
  }
  {
    Bytes itxt;
    append(itxt, std::string_view("Title\0\x01\0en\0Title\0", 17));
    append(itxt, zlib(bytes_of("Caf\xC3\xA9")));
    append(out, png_chunk("iTXt", itxt));
  }
  append(out, png_chunk("eXIf", md::write_tiff(sample_exif_stream(true))));
  append(out, png_chunk("tIME", Bytes{0x07, 0xEA, 10, 11, 12, 13, 14}));
  append(out, png_chunk("caBX", sample_c2pa()));
  append(out, png_chunk("IDAT", Bytes{0x78, 0x01}));
  append(out, png_chunk("IEND", {}));
  return out;
}

Bytes riff_chunk(std::string_view type, const Bytes& payload) {
  Bytes out;
  append(out, type);
  put_le32(out, static_cast<std::uint32_t>(payload.size()));
  append(out, payload);
  if (payload.size() % 2 != 0) {
    out.push_back(0);
  }
  return out;
}

Bytes sample_webp() {
  Bytes body;
  append(body, "WEBP");
  append(body, riff_chunk("VP8X", Bytes{0x2C, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
  append(body, riff_chunk("ICCP", fake_icc("WebP Profile")));
  Bytes exif;
  append(exif, std::string_view("Exif\0\0", 6));  // a common writer mistake; readers accept it
  append(exif, md::write_tiff(sample_exif_stream(false)));
  append(body, riff_chunk("EXIF", exif));
  append(body, riff_chunk("XMP ", bytes_of(kSampleXmp)));
  append(body, riff_chunk("VP8L", Bytes{0x2F, 0, 0, 0, 0}));
  Bytes out;
  append(out, "RIFF");
  put_le32(out, static_cast<std::uint32_t>(body.size()));
  append(out, body);
  return out;
}

Bytes sample_gif() {
  Bytes out;
  append(out, "GIF89a");
  out.insert(out.end(), {1, 0, 1, 0, 0, 0, 0});  // 1x1, no global table
  out.insert(out.end(), {0x21, 0xFE, 5});
  append(out, "hello");
  out.push_back(0);
  out.insert(out.end(), {0x21, 0xFF, 11});
  append(out, "XMP DataXMP");
  append(out, kSampleXmp);
  out.push_back(1);
  for (int value = 255; value >= 0; --value) {
    out.push_back(static_cast<std::uint8_t>(value));
  }
  out.push_back(0);
  out.insert(out.end(), {0x2C, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x4C, 0x01, 0});
  out.push_back(0x3B);
  return out;
}

Bytes sample_heif() {
  // ftyp + meta { pitm, iinf (hvc1 + Exif), iloc v1 (Exif from idat), idat }.
  Bytes tiff_payload;
  put_be32(tiff_payload, 6);
  append(tiff_payload, std::string_view("Exif\0\0", 6));
  append(tiff_payload, md::write_tiff(sample_exif_stream(true)));

  Bytes ftyp;
  append(ftyp, "heic");
  put_be32(ftyp, 0);
  append(ftyp, "mif1heic");
  Bytes meta{0, 0, 0, 0};
  {
    Bytes pitm{0, 0, 0, 0};
    put_be16(pitm, 1);
    append(meta, jumbf_box("pitm", pitm));
  }
  {
    Bytes iinf{0, 0, 0, 0};
    put_be16(iinf, 2);
    for (const auto& [id, type] : {std::pair<int, std::string_view>{1, "hvc1"}, {2, "Exif"}}) {
      Bytes infe{2, 0, 0, 0};
      put_be16(infe, static_cast<std::uint32_t>(id));
      put_be16(infe, 0);
      append(infe, type);
      infe.push_back(0);
      append(iinf, jumbf_box("infe", infe));
    }
    append(meta, jumbf_box("iinf", iinf));
  }
  {
    Bytes iloc{1, 0, 0, 0, 0x44, 0x00};
    put_be16(iloc, 1);
    put_be16(iloc, 2);  // item id
    put_be16(iloc, 1);  // construction method 1 (idat)
    put_be16(iloc, 0);  // data reference index
    put_be16(iloc, 1);  // extent count
    put_be32(iloc, 0);
    put_be32(iloc, static_cast<std::uint32_t>(tiff_payload.size()));
    append(meta, jumbf_box("iloc", iloc));
  }
  append(meta, jumbf_box("idat", tiff_payload));
  Bytes out = jumbf_box("ftyp", ftyp);
  append(out, jumbf_box("meta", meta));
  return out;
}

Bytes sample_bmp_linked_profile() {
  Bytes out;
  append(out, "BM");
  const std::string path = "C:\\Users\\someone\\profiles\\monitor.icc";
  const std::uint32_t total = 14 + 124 + static_cast<std::uint32_t>(path.size()) + 1;
  put_le32(out, total);
  put_le32(out, 0);
  put_le32(out, 14 + 124);
  Bytes header(124, 0);
  header[0] = 124;
  const auto set_le32 = [&](std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
      header[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
    }
  };
  set_le32(56, 0x4C494E4BU);  // 'LINK'
  set_le32(112, 124);         // profile data offset, from the info header
  set_le32(116, static_cast<std::uint32_t>(path.size() + 1));
  append(out, header);
  append(out, path);
  out.push_back(0);
  return out;
}

Bytes sample_pdf() {
  std::string text = "%PDF-1.4\n";
  std::vector<std::size_t> offsets;
  const auto object = [&](const std::string& body) {
    offsets.push_back(text.size());
    text += std::to_string(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
  };
  object("<< /Type /Catalog /Pages 2 0 R >>");
  object("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
  object("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 10] >>");
  object("<< /Title (Hello PDF) /Author <FEFF00410062> /Producer (Some Producer) >>");
  const auto xref = text.size();
  text += "xref\n0 5\n0000000000 65535 f \n";
  for (const auto offset : offsets) {
    char line[24];
    std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
    text += line;
  }
  text += "trailer\n<< /Size 5 /Root 1 0 R /Info 4 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
  return bytes_of(text);
}

const std::string kSampleSvg = R"(<?xml version="1.0"?>
<svg xmlns="http://www.w3.org/2000/svg" xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"
     xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" sodipodi:docname="drawing.svg"
     inkscape:export-filename="C:\Users\someone\Desktop\export.png" width="10" height="10">
  <title>My Drawing</title>
  <desc>A description</desc>
  <metadata>
    <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#" xmlns:cc="http://creativecommons.org/ns#"
             xmlns:dc="http://purl.org/dc/elements/1.1/">
      <cc:Work rdf:about=""><dc:title>RDF Title</dc:title></cc:Work>
    </rdf:RDF>
  </metadata>
  <rect width="10" height="10"/>
</svg>)";

const md::MetadataEntry* find_entry(const std::vector<md::MetadataEntry>& entries, std::string_view group,
                                    std::string_view name) {
  for (const auto& entry : entries) {
    if (entry.group == group && entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

// ------------------------------------------------------------------------ tests

void tiff_ifd_round_trips_both_byte_orders() {
  for (const bool big_endian : {false, true}) {
    const auto stream = sample_exif_stream(big_endian);
    const auto bytes = md::write_tiff(stream);
    CHECK(bytes[0] == (big_endian ? 'M' : 'I'));
    const auto parsed = md::parse_tiff(bytes);
    CHECK(parsed.has_value());
    CHECK(parsed->big_endian == big_endian);
    CHECK(!parsed->truncated);
    const auto* ifd0 = parsed->find(md::IfdKind::Ifd0);
    CHECK(ifd0 != nullptr);
    CHECK(parsed->string_value(*ifd0->find(md::exif_tag::kMake)) == "PatchyCam");
    CHECK(parsed->unsigned_value(*ifd0->find(md::exif_tag::kOrientation)) == 6U);
    const auto* exif = parsed->find(md::IfdKind::Exif);
    CHECK(exif != nullptr);
    CHECK(md::format_exif_value(*parsed, md::IfdKind::Exif, *exif->find(md::exif_tag::kExposureTime)) == "1/250 s");
    CHECK(md::format_exif_value(*parsed, md::IfdKind::Exif, *exif->find(md::exif_tag::kFNumber)) == "f/2.8");
    CHECK(exif->find(md::exif_tag::kMakerNote)->data.size() == 10);
    CHECK(parsed->thumbnail == kFakeThumbnail);
    const auto latitude = md::gps_latitude(*parsed);
    const auto longitude = md::gps_longitude(*parsed);
    CHECK(latitude && std::abs(*latitude - 47.6) < 1e-9);
    CHECK(longitude && std::abs(*longitude - (-(122.0 + 19.0 / 60.0 + 30.0 / 3600.0))) < 1e-9);
    CHECK(md::gps_altitude(*parsed) == 100.0);
    // Re-writing the parsed stream reproduces the same bytes.
    CHECK(md::write_tiff(*parsed) == bytes);
  }
}

void tiff_ifd_survives_hostile_offsets() {
  // IFD0 whose next-IFD pointer points back at itself, plus one entry with a
  // gigantic count: the walk must end, keep the valid entry, and flag truncation.
  Bytes data{'I', 'I', 42, 0, 8, 0, 0, 0};
  data.insert(data.end(), {2, 0});
  data.insert(data.end(), {0x0F, 0x01, 2, 0, 4, 0, 0, 0, 'A', 'B', 'C', 0});        // Make "ABC"
  data.insert(data.end(), {0x10, 0x01, 2, 0, 0xFF, 0xFF, 0xFF, 0x7F, 8, 0, 0, 0});  // Model, absurd count
  data.insert(data.end(), {8, 0, 0, 0});                                              // next IFD = itself
  const auto parsed = md::parse_tiff(data);
  CHECK(parsed.has_value());
  CHECK(parsed->truncated);
  CHECK(parsed->ifds.size() == 1);
  CHECK(parsed->string_value(*parsed->ifds[0].find(md::exif_tag::kMake)) == "ABC");
  CHECK(parsed->ifds[0].find(md::exif_tag::kModel) == nullptr);
  CHECK(!md::parse_tiff(Bytes{'I', 'I', 42, 0}).has_value());
  CHECK(!md::parse_tiff(Bytes{'X', 'X', 42, 0, 8, 0, 0, 0}).has_value());
}

void xmp_reads_every_rdf_form_and_round_trips() {
  const auto packet = md::parse_xmp(bytes_of(kSampleXmp));
  CHECK(packet.has_value());
  CHECK(packet->text(md::xmp_ns::kDc, "title") == "Sunset & Sea");
  CHECK(packet->text(md::xmp_ns::kDc, "creator") == "Ann Example");
  CHECK(packet->items(md::xmp_ns::kDc, "subject") == std::vector<std::string>({"beach", "sunset"}));
  CHECK(packet->text(md::xmp_ns::kXmp, "CreatorTool") == "Example Editor 1.0");
  // A non-standard prefix ("ps") still resolves to the photoshop namespace.
  CHECK(packet->text(md::xmp_ns::kPhotoshop, "AuthorsPosition") == "Photographer");
  const auto* history = packet->find(md::xmp_ns::kXmpMM, "History");
  CHECK(history != nullptr && history->value.items.size() == 2);
  CHECK(history->value.items[1].kind == md::XmpValue::Kind::Struct);
  CHECK(packet->struct_field("http://example.com/foo/", "Custom", "http://example.com/foo/", "inner") == "kept");

  const auto serialized = md::serialize_xmp(*packet);
  CHECK(serialized.starts_with("<?xpacket begin="));
  CHECK(serialized.ends_with("<?xpacket end=\"w\"?>"));
  const auto reparsed = md::parse_xmp(bytes_of(serialized));
  CHECK(reparsed.has_value());
  const auto before = md::flatten_xmp(*packet);
  const auto after = md::flatten_xmp(*reparsed);
  CHECK(before.size() == after.size());
  for (std::size_t i = 0; i < before.size() && i < after.size(); ++i) {
    CHECK(before[i].value == after[i].value);
    CHECK(before[i].ns == after[i].ns);
  }
  // The French alternative survives an edit of the default language.
  auto edited = *reparsed;
  edited.set_lang_alt(md::xmp_ns::kDc, "title", "New title");
  CHECK(edited.text(md::xmp_ns::kDc, "title") == "New title");
  CHECK(edited.find(md::xmp_ns::kDc, "title")->value.items.size() == 2);
  edited.set_lang_alt(md::xmp_ns::kDc, "title", "");
  CHECK(edited.find(md::xmp_ns::kDc, "title") == nullptr);
  CHECK(!md::parse_xmp(bytes_of("not xml at all")).has_value());
}

void xmp_privacy_flags_history_and_text_layers() {
  CHECK(md::xmp_property_privacy(md::xmp_ns::kPhotoshop, "TextLayers") == md::PrivacyClass::EditHistory);
  CHECK(md::xmp_property_privacy(md::xmp_ns::kExif, "GPSLatitude") == md::PrivacyClass::Location);
  CHECK(md::xmp_property_privacy(md::xmp_ns::kXmp, "Thumbnails") == md::PrivacyClass::HiddenImage);
  patchy::DocumentFileInfo info;
  info.xmp = patchy::make_metadata_bytes(bytes_of(kSampleXmp));
  const auto history = md::summarize_edit_history(info);
  CHECK(history.events.size() == 2);
  CHECK(history.events[0].action == "created");
  CHECK(history.events[1].software_agent == "Example Editor 1.0");
  CHECK(history.ancestor_count == 3);
  CHECK(history.text_layers.size() == 1);
  CHECK(history.text_layers[0].second == "Secret text");
}

void iptc_iim_round_trips_and_decodes_latin1() {
  const auto bytes = sample_iim();
  const auto block = md::parse_iim(bytes);
  CHECK(block.has_value());
  CHECK(block->utf8_marker);
  CHECK(block->values(2, md::iim::kKeywords) == std::vector<std::string>({"beach", "sunset"}));
  CHECK(block->value(2, md::iim::kByline) == "Ann Example");
  CHECK(md::write_iim(*block) == bytes);
  // Legacy Latin-1 text without the 1:90 marker decodes to UTF-8.
  const Bytes latin1{0x1C, 2, 0, 0, 2, 0, 4, 0x1C, 2, 120, 0, 4, 'C', 'a', 'f', 0xE9};
  const auto legacy = md::parse_iim(latin1);
  CHECK(legacy.has_value() && !legacy->utf8_marker);
  CHECK(legacy->value(2, md::iim::kCaption) == "Caf\xC3\xA9");
  // The digest is plain MD5 ("abc" is the RFC 1321 test vector).
  const auto digest = md::iim_digest(bytes_of("abc"));
  CHECK(digest[0] == 0x90 && digest[1] == 0x01 && digest[15] == 0x72);
}

void photoshop_irb_round_trips_odd_lengths() {
  std::vector<md::IrbResource> resources{
      md::IrbResource{0x3842494DU, 1028, "a", Bytes{1, 2, 3}},
      md::IrbResource{0x3842494DU, 1060, "", Bytes{4, 5}},
  };
  const auto bytes = md::write_irb(resources);
  CHECK(bytes.size() % 2 == 0);
  const auto parsed = md::parse_irb(bytes);
  CHECK(parsed.size() == 2);
  CHECK(parsed[0].name == "a" && parsed[0].data == Bytes({1, 2, 3}));
  CHECK(parsed[1].id == 1060 && parsed[1].data == Bytes({4, 5}));
}

void jpeg_container_extracts_every_block() {
  const auto info = md::extract_file_info(sample_jpeg());
  CHECK(info.source_format == "JPEG");
  CHECK(info.exif && md::parse_tiff(*info.exif));
  CHECK(info.xmp && md::parse_xmp(*info.xmp)->text(md::xmp_ns::kDc, "title") == "Sunset & Sea");
  CHECK(info.source_icc && *info.source_icc == fake_icc("Patchy Test RGB"));
  CHECK(info.iptc && md::parse_iim(*info.iptc)->value(2, md::iim::kCity) == "Seattle");
  CHECK(info.iptc_digest && *info.iptc_digest == md::iim_digest(*info.iptc));
  CHECK(info.copyright_flag == true);
  CHECK(info.text.size() == 1 && info.text[0].value == "hello comment");
  const auto c2pa = std::find_if(info.observed.begin(), info.observed.end(), [](const auto& item) {
    return item.privacy == md::PrivacyClass::Provenance;
  });
  CHECK(c2pa != info.observed.end());
  CHECK(c2pa->summary.find("Example Generator 2.0") != std::string::npos);
  const auto trailer = std::find_if(info.observed.begin(), info.observed.end(),
                                    [](const auto& item) { return item.label == "Data after end of image"; });
  CHECK(trailer != info.observed.end() && trailer->size == 9);
  CHECK(std::any_of(info.structure.begin(), info.structure.end(),
                    [](const auto& segment) { return segment.name == "APP1 Exif"; }));
  CHECK(std::any_of(info.structure.begin(), info.structure.end(),
                    [](const auto& segment) { return segment.name == "EOI"; }));
}

void jpeg_extended_xmp_merges_after_md5_check() {
  const auto info = md::extract_file_info(sample_jpeg(true));
  const auto packet = md::parse_xmp(*info.xmp);
  CHECK(packet.has_value());
  CHECK(packet->text(md::xmp_ns::kDc, "description") == "From the extension");
  CHECK(packet->find(md::xmp_ns::kXmpNote, "HasExtendedXMP") == nullptr);
  CHECK(packet->text(md::xmp_ns::kDc, "title") == "Sunset & Sea");
}

void png_container_extracts_text_profiles_and_ai_parameters() {
  const auto info = md::extract_file_info(sample_png());
  CHECK(info.source_format == "PNG");
  CHECK(info.exif && md::parse_tiff(*info.exif)->big_endian);
  CHECK(info.xmp && md::parse_xmp(*info.xmp).has_value());
  CHECK(info.source_icc && md::summarize_icc(*info.source_icc)->description == "PNG Profile");
  CHECK(info.png_color_chunks.size() == 1);
  CHECK(info.png_color_chunks[0].type == (std::array<char, 4>{'g', 'A', 'M', 'A'}));
  // XMP goes to the XMP slot, not the text list.
  CHECK(info.text.size() == 3);
  CHECK(info.text[1].kind == patchy::FileInfoTextKind::PngCompressedText && info.text[1].value == "compressed comment");
  CHECK(info.text[2].key == "Title" && info.text[2].value == "Caf\xC3\xA9" && info.text[2].language == "en");
  const auto ai = md::summarize_ai_generation(info);
  CHECK(ai.tool == "Automatic1111 / Forge");
  CHECK(ai.prompt == "a lighthouse at dusk, oil painting");
  CHECK(ai.negative_prompt == "blurry, lowres");
  CHECK(ai.settings == "Steps: 30, Sampler: Euler a, Seed: 42");
  CHECK(ai.digital_source_type.find("trainedAlgorithmicMedia") != std::string::npos);
  CHECK(std::any_of(info.observed.begin(), info.observed.end(),
                    [](const auto& item) { return item.label == "Last modification time"; }));
}

void webp_gif_heif_bmp_containers_extract() {
  {
    const auto info = md::extract_file_info(sample_webp());
    CHECK(info.source_format == "WebP");
    CHECK(info.exif && md::parse_tiff(*info.exif));
    CHECK(info.xmp && info.source_icc);
  }
  {
    const auto info = md::extract_file_info(sample_gif());
    CHECK(info.source_format == "GIF");
    CHECK(info.text.size() == 1 && info.text[0].value == "hello");
    CHECK(info.xmp && md::parse_xmp(*info.xmp)->text(md::xmp_ns::kDc, "title") == "Sunset & Sea");
    CHECK(std::any_of(info.structure.begin(), info.structure.end(),
                      [](const auto& segment) { return segment.name == "Image 1"; }));
  }
  {
    const auto info = md::extract_file_info(sample_heif());
    CHECK(info.source_format == "HEIF");
    CHECK(info.exif && md::parse_tiff(*info.exif));
    const auto camera = md::summarize_camera(info);
    CHECK(camera.make == "PatchyCam");
    CHECK(camera.has_location());
  }
  {
    const auto info = md::extract_file_info(sample_bmp_linked_profile());
    CHECK(info.source_format == "BMP");
    CHECK(info.observed.size() == 1);
    CHECK(info.observed[0].summary == "C:\\Users\\someone\\profiles\\monitor.icc");
    CHECK(info.observed[0].privacy == md::PrivacyClass::MachineInfo);
  }
}

void tiff_pdf_svg_containers_extract() {
  {
    md::TiffStream stream;
    stream.big_endian = true;
    stream.set_long(md::IfdKind::Ifd0, 256, 1);
    stream.set_long(md::IfdKind::Ifd0, 257, 1);
    stream.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kArtist, "Tiff Author");
    stream.set_undefined(md::IfdKind::Ifd0, md::exif_tag::kXmp, bytes_of(kSampleXmp));
    stream.set_undefined(md::IfdKind::Ifd0, md::exif_tag::kIccProfile, fake_icc("TIFF Profile"));
    stream.set_rational(md::IfdKind::Exif, md::exif_tag::kFNumber, 4, 1);
    const auto info = md::extract_file_info(md::write_tiff(stream));
    CHECK(info.source_format == "TIFF");
    CHECK(info.xmp && info.source_icc);
    const auto exif = md::parse_tiff(*info.exif);
    CHECK(exif.has_value());
    // Image-structure tags stay out of the EXIF slot; descriptive tags go in.
    CHECK(exif->find(md::IfdKind::Ifd0)->find(256) == nullptr);
    CHECK(exif->find(md::IfdKind::Ifd0)->find(md::exif_tag::kXmp) == nullptr);
    CHECK(exif->string_value(*exif->find(md::IfdKind::Ifd0)->find(md::exif_tag::kArtist)) == "Tiff Author");
    CHECK(exif->find(md::IfdKind::Exif) != nullptr);
  }
  {
    const auto info = md::extract_file_info(sample_pdf());
    CHECK(info.source_format == "PDF");
    const auto title = std::find_if(info.text.begin(), info.text.end(), [](const auto& t) { return t.key == "Title"; });
    const auto author = std::find_if(info.text.begin(), info.text.end(), [](const auto& t) { return t.key == "Author"; });
    CHECK(title != info.text.end() && title->value == "Hello PDF");
    CHECK(author != info.text.end() && author->value == "Ab");
  }
  {
    const auto info = md::extract_file_info(bytes_of(kSampleSvg));
    CHECK(info.source_format == "SVG");
    CHECK(info.text.size() == 2 && info.text[0].value == "My Drawing");
    CHECK(info.xmp && md::parse_xmp(*info.xmp)->text(md::xmp_ns::kDc, "title") == "RDF Title");
    CHECK(std::any_of(info.observed.begin(), info.observed.end(), [](const auto& item) {
      return item.label == "Export file path" && item.privacy == md::PrivacyClass::MachineInfo;
    }));
  }
}

void describe_file_info_lists_everything_with_privacy() {
  const auto info = md::extract_file_info(sample_jpeg());
  const auto entries = md::describe_file_info(info, {});
  const auto* make = find_entry(entries, "EXIF IFD0", "Make");
  CHECK(make != nullptr && make->value == "PatchyCam");
  const auto* latitude = find_entry(entries, "EXIF GPS", "GPSLatitude");
  CHECK(latitude != nullptr && latitude->privacy == md::PrivacyClass::Location);
  const auto* serial = find_entry(entries, "EXIF Exif", "BodySerialNumber");
  CHECK(serial != nullptr && serial->privacy == md::PrivacyClass::DeviceIdentity);
  CHECK(find_entry(entries, "XMP dc", "dc:title") != nullptr);
  CHECK(find_entry(entries, "XMP dc", "dc:title[fr]") != nullptr);
  CHECK(find_entry(entries, "IPTC-IIM", "By-line") != nullptr);
  const auto* digest = find_entry(entries, "IPTC-IIM", "CaptionDigest");
  CHECK(digest != nullptr && digest->value.starts_with("matches"));
  const auto* description = find_entry(entries, "ICC profile", "Description");
  CHECK(description != nullptr && description->value == "Patchy Test RGB");
  CHECK(find_entry(entries, "EXIF IFD1 (thumbnail)", "ThumbnailImage") != nullptr);
  const auto counts = md::privacy_summary(entries);
  CHECK(counts.contains(md::PrivacyClass::Location));
  CHECK(counts.contains(md::PrivacyClass::HiddenImage));
  CHECK(counts.contains(md::PrivacyClass::Provenance));
  const auto thumbnails = md::embedded_thumbnails(info);
  CHECK(thumbnails.size() == 1 && thumbnails[0].bytes == kFakeThumbnail);
  const auto camera = md::summarize_camera(info);
  CHECK(camera.model == "Model X");
  CHECK(camera.body_serial == "SN123");
  CHECK(camera.orientation == "Rotate 90 CW");
  CHECK(camera.has_maker_note);
}

void cbor_and_jumbf_summaries() {
  const Bytes map{0xA2, 0x61, 'a', 0x01, 0x61, 'b', 0x82, 0xF5, 0x61, 'x'};
  CHECK(md::cbor_to_text(map) == "{\"a\": 1, \"b\": [true, \"x\"]}");
  const auto summary = md::summarize_jumbf(sample_c2pa());
  CHECK(summary.labels == std::vector<std::string>({"c2pa", "c2pa.claim", "c2pa.actions"}));
  CHECK(summary.claim_generator == "Example Generator 2.0");
  CHECK(summary.actions.size() == 1 && summary.actions[0] == "c2pa.created (trainedAlgorithmicMedia)");
  // Truncated CBOR must not read out of bounds.
  CHECK(!md::cbor_to_text(Bytes{0x7A, 0xFF, 0xFF, 0xFF}).empty());
}

void inflate_is_capped() {
  const Bytes zeros(4U * 1024U * 1024U, 0);
  const auto compressed = zlib(zeros);
  CHECK(md::inflate_zlib_capped(compressed, 1024U * 1024U).size() == 1024U * 1024U);
  CHECK(md::inflate_zlib_capped(compressed, 8U * 1024U * 1024U) == zeros);
  CHECK(md::inflate_zlib_capped(Bytes{1, 2, 3}, 100).empty());
}

void container_scanners_survive_mutation() {
  // Deterministic byte flips and truncations over every sample: the scanners must
  // neither crash nor hang (they run on untrusted files at open time).
  const std::vector<Bytes> samples{sample_jpeg(true), sample_png(), sample_webp(), sample_gif(), sample_heif(),
                                   sample_bmp_linked_profile(), md::write_tiff(sample_exif_stream(false))};
  std::uint64_t state = 0x5EEDF11EULL;
  for (const auto& sample : samples) {
    for (int iteration = 0; iteration < 300; ++iteration) {
      Bytes mutated = sample;
      const auto flips = 1 + splitmix64(state) % 8;
      for (std::uint64_t i = 0; i < flips; ++i) {
        mutated[splitmix64(state) % mutated.size()] = static_cast<std::uint8_t>(splitmix64(state));
      }
      if (splitmix64(state) % 4 == 0) {
        mutated.resize(splitmix64(state) % mutated.size());
      }
      const auto info = md::extract_file_info(mutated);
      (void)md::describe_file_info(info, {});
      (void)md::summarize_ai_generation(info);
      (void)md::embedded_thumbnails(info);
    }
  }
}

void extract_file_info_from_unicode_paths() {
  const auto dir = patchy::test::unicode_artifact_dir(u8"file-info");
  const auto png = sample_png();
  std::vector<std::filesystem::path> written;
  for (const auto stem : patchy::test::kUnicodePathStems) {
    const auto path = dir / patchy::test::unicode_path_piece(std::u8string(stem) + u8".png");
    {
      std::ofstream stream(path, std::ios::binary);
      stream.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    }
    written.push_back(path);
    const auto info = md::extract_file_info_from_path(path);
    CHECK(info.source_format == "PNG");
    CHECK(info.source_file_size == png.size());
    CHECK(info.xmp != nullptr);
  }
  CHECK(patchy::test::directory_holds_only(dir, written));
  CHECK(md::extract_file_info_from_path(dir / "missing.png").source_format.empty());
}

void psd_round_trips_file_info_resources() {
  patchy::Document document(4, 3, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", patchy::PixelBuffer(4, 3, patchy::PixelFormat::rgb8()));
  auto& info = document.file_info();
  info.xmp = patchy::make_metadata_bytes(bytes_of(kSampleXmp));
  info.exif = patchy::make_metadata_bytes(md::write_tiff(sample_exif_stream(true)));
  const auto iim = sample_iim();
  info.iptc = patchy::make_metadata_bytes(iim);
  info.iptc_digest = md::iim_digest(iim);
  info.copyright_flag = true;
  info.copyright_url = "https://example.com/license";
  info.text.push_back(patchy::FileInfoText{patchy::FileInfoTextKind::PngText, "parameters", kA1111Parameters, {}});
  info.text.push_back(patchy::FileInfoText{patchy::FileInfoTextKind::Comment, {}, "a comment", {}});
  info.text.push_back(patchy::FileInfoText{patchy::FileInfoTextKind::PdfInfo, "Title", "not carried", {}});
  info.psd_resources.emplace_back(std::uint16_t{1057}, patchy::make_metadata_bytes(Bytes{0, 0, 0, 1, 1}));

  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto read = patchy::psd::DocumentIo::read(bytes);
  const auto& back = read.file_info();
  CHECK(back.source_format == "PSD");
  CHECK(back.xmp && md::parse_xmp(*back.xmp)->text(md::xmp_ns::kDc, "title") == "Sunset & Sea");
  CHECK(back.exif && *back.exif == *info.exif);
  CHECK(back.iptc && *back.iptc == iim);
  CHECK(back.iptc_digest == info.iptc_digest);
  CHECK(back.copyright_flag == true);
  CHECK(back.copyright_url == "https://example.com/license");
  CHECK(back.text.size() == 2);
  CHECK(back.text[0].key == "parameters" && back.text[0].value == kA1111Parameters);
  CHECK(back.text[1].kind == patchy::FileInfoTextKind::Comment && back.text[1].value == "a comment");
  CHECK(back.psd_resources.size() == 1 && back.psd_resources[0].first == 1057);
  CHECK(std::any_of(back.structure.begin(), back.structure.end(),
                    [](const auto& segment) { return segment.name == "8BIM 1060"; }));
  // The lifted resources left the raw blob, so a second save writes them once.
  const auto& raw = read.metadata().raw_psd_image_resources;
  const std::string raw_text(raw.begin(), raw.end());
  CHECK(raw_text.find("xmpmeta") == std::string::npos);
  const auto again = patchy::psd::DocumentIo::write_layered_rgb8(read);
  const auto read_again = patchy::psd::DocumentIo::read(again);
  CHECK(read_again.file_info().text.size() == 2);
  CHECK(*read_again.file_info().iptc == iim);

  // Stripping everything writes none of them.
  auto stripped = read_again;
  stripped.file_info() = patchy::DocumentFileInfo{};
  const auto clean = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(stripped));
  CHECK(!clean.file_info().has_metadata());
}

patchy::DocumentFileInfo sample_info() {
  return md::extract_file_info(sample_jpeg());
}

// A decodable 2x2 RGB PNG (stb_image verifies the injected files still decode).
Bytes minimal_png() {
  Bytes out;
  append(out, std::string_view("\x89PNG\r\n\x1A\n", 8));
  Bytes ihdr;
  put_be32(ihdr, 2);
  put_be32(ihdr, 2);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  append(out, png_chunk("IHDR", ihdr));
  const Bytes raw{0, 255, 0, 0, 0, 255, 0, 0, 0, 0, 255, 255, 255, 255};
  append(out, png_chunk("IDAT", zlib(raw)));
  append(out, png_chunk("IEND", {}));
  return out;
}

void edit_fields_follow_mwg_and_mirror_legacy_blocks() {
  auto info = sample_info();
  const auto before = md::read_fields(info);
  CHECK(before.title == "Sunset & Sea");
  CHECK(before.authors == std::vector<std::string>({"Ann Example"}));
  CHECK(before.keywords == std::vector<std::string>({"beach", "sunset"}));
  CHECK(before.author_title == "Photographer");
  // City only exists in the legacy IPTC block.
  CHECK(before.city == "Seattle");
  CHECK(before.copyright_status == md::CopyrightStatus::Copyrighted);

  auto after = before;
  after.title = "Harbor";
  after.authors = {"Bea Example", "Cy Example"};
  after.keywords = {"harbor"};
  after.city = "Tacoma";
  after.copyright_notice = "(c) 2026 Bea";
  after.contact_email = "bea@example.com";
  after.rating = 4;
  CHECK(md::apply_fields(info, before, after));
  CHECK(!md::apply_fields(info, after, after));
  const auto reread = md::read_fields(info);
  CHECK(reread.title == "Harbor");
  CHECK(reread.authors == after.authors);
  CHECK(reread.keywords == after.keywords);
  CHECK(reread.city == "Tacoma");
  CHECK(reread.contact_email == "bea@example.com");
  CHECK(reread.rating == 4);
  // The unknown namespace survived the rewrite.
  CHECK(md::parse_xmp(*info.xmp)->struct_field("http://example.com/foo/", "Custom", "http://example.com/foo/",
                                               "inner") == "kept");
  // Mirrors: IIM keywords/by-line/city rewritten with a fresh digest, EXIF Artist and Copyright.
  const auto iim = md::parse_iim(*info.iptc);
  CHECK(iim->values(2, md::iim::kKeywords) == std::vector<std::string>({"harbor"}));
  CHECK(iim->values(2, md::iim::kByline) == after.authors);
  CHECK(iim->value(2, md::iim::kCity) == "Tacoma");
  CHECK(info.iptc_digest && *info.iptc_digest == md::iim_digest(*info.iptc));
  const auto exif = md::parse_tiff(*info.exif);
  CHECK(exif->string_value(*exif->find(md::IfdKind::Ifd0)->find(md::exif_tag::kArtist)) == "Bea Example; Cy Example");
  CHECK(exif->string_value(*exif->find(md::IfdKind::Ifd0)->find(md::exif_tag::kCopyright)) == "(c) 2026 Bea");
  // A value shown from EXIF alone reaches XMP on the next edit (MWG: XMP is always written),
  // which is where Photoshop reads a PSD's or PNG's author.
  {
    patchy::DocumentFileInfo exif_only;
    exif_only.exif = patchy::make_metadata_bytes(md::write_tiff(sample_exif_stream(false)));
    auto edit_before = md::read_fields(exif_only);
    CHECK(edit_before.authors == std::vector<std::string>({"Ann Example"}));
    auto edit_after = edit_before;
    edit_after.title = "New";
    CHECK(md::apply_fields(exif_only, edit_before, edit_after));
    CHECK(md::parse_xmp(*exif_only.xmp)->items(md::xmp_ns::kDc, "creator") ==
          std::vector<std::string>({"Ann Example"}));
  }
  // A digest mismatch means another program edited the IIM: its values win.
  info.iptc_digest = std::array<std::uint8_t, 16>{};
  auto stale = md::parse_xmp(*info.xmp);
  stale->set_simple(md::xmp_ns::kPhotoshop, "City", "Elsewhere");
  info.xmp = patchy::make_metadata_bytes(bytes_of(md::serialize_xmp(*stale)));
  CHECK(md::read_fields(info).city == "Tacoma");
}

void strip_and_targeted_removals() {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  document.file_info() = sample_info();
  document.color_state().embedded_icc_profile = fake_icc("Kept");
  document.file_info().png_color_chunks.push_back(patchy::FileInfoChunk{{'g', 'A', 'M', 'A'}, {0, 0, 177, 143}});

  auto located = document.file_info();
  CHECK(md::remove_location(located));
  CHECK(!md::summarize_camera(located).has_location());
  CHECK(md::parse_tiff(*located.exif)->find(md::IfdKind::Gps) == nullptr);
  CHECK(!md::remove_location(located));

  auto history = document.file_info();
  CHECK(md::remove_edit_history(history));
  const auto summary = md::summarize_edit_history(history);
  CHECK(summary.events.empty() && summary.ancestor_count == 0 && summary.text_layers.empty());
  CHECK(md::parse_xmp(*history.xmp)->text(md::xmp_ns::kDc, "title") == "Sunset & Sea");

  auto kept = document;
  md::strip_metadata(kept, true);
  CHECK(!kept.file_info().has_metadata());
  CHECK(kept.color_state().embedded_icc_profile == fake_icc("Kept"));
  CHECK(kept.file_info().png_color_chunks.size() == 1);
  const auto remaining = md::describe_file_info(kept.file_info(), kept.color_state().embedded_icc_profile);
  CHECK(std::all_of(remaining.begin(), remaining.end(), [](const md::MetadataEntry& entry) {
    return entry.group == "ICC profile" || entry.group == "PNG color";
  }));
  auto dropped = document;
  md::strip_metadata(dropped, false);
  CHECK(dropped.color_state().embedded_icc_profile.empty());
  CHECK(dropped.file_info().png_color_chunks.empty());
  const auto save = md::prepare_save_metadata(kept, md::MetadataPolicy::All, 2, 2);
  CHECK(save.exif.empty() && save.xmp.empty() && save.iptc.empty() && save.text.empty());
  CHECK(save.icc == fake_icc("Kept"));
}

void orientation_bakes_and_saves_as_normal() {
  auto info = sample_info();
  CHECK(md::bake_orientation(info) == "Rotate 90 CW");
  CHECK(md::summarize_camera(info).orientation == "Horizontal (normal)");
  CHECK(md::bake_orientation(info).empty());
}

void save_metadata_applies_rules_and_policies() {
  patchy::Document document(40, 30, patchy::PixelFormat::rgb8());
  document.print_settings().horizontal_ppi = 150.0;
  document.print_settings().vertical_ppi = 150.0;
  auto stream = sample_exif_stream(false);
  stream.set_long(md::IfdKind::Exif, md::exif_tag::kPixelXDimension, 4000);
  stream.set_long(md::IfdKind::Exif, md::exif_tag::kPixelYDimension, 3000);
  stream.set_rational(md::IfdKind::Ifd0, md::exif_tag::kXResolution, 72, 1);
  stream.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kCopyright, "(c) Ann");
  document.file_info() = sample_info();
  document.file_info().exif = patchy::make_metadata_bytes(md::write_tiff(stream));
  document.file_info().text.push_back(patchy::FileInfoText{patchy::FileInfoTextKind::PngText, "parameters", "p", {}});
  document.file_info().text.push_back(patchy::FileInfoText{patchy::FileInfoTextKind::PdfInfo, "Title", "t", {}});

  const auto all = md::prepare_save_metadata(document, md::MetadataPolicy::All, 40, 30);
  const auto exif = md::parse_tiff(all.exif);
  CHECK(exif.has_value());
  CHECK(exif->thumbnail.empty() && exif->find(md::IfdKind::Ifd1) == nullptr);
  CHECK(exif->unsigned_value(*exif->find(md::IfdKind::Ifd0)->find(md::exif_tag::kOrientation)) == 1U);
  CHECK(exif->unsigned_value(*exif->find(md::IfdKind::Exif)->find(md::exif_tag::kPixelXDimension)) == 40U);
  CHECK(exif->real_value(*exif->find(md::IfdKind::Ifd0)->find(md::exif_tag::kXResolution)) == 150.0);
  CHECK(exif->find(md::IfdKind::Gps) != nullptr);
  CHECK(!all.xmp.empty() && !all.iptc.empty());
  CHECK(all.text.size() == 2);  // the comment and the PNG text, never the PDF /Info entry

  const auto copyright = md::prepare_save_metadata(document, md::MetadataPolicy::CopyrightAndContact, 40, 30);
  const auto reduced = md::parse_tiff(copyright.exif);
  CHECK(reduced->find(md::IfdKind::Gps) == nullptr && reduced->find(md::IfdKind::Exif) == nullptr);
  CHECK(reduced->find(md::IfdKind::Ifd0)->find(md::exif_tag::kArtist) != nullptr);
  CHECK(reduced->find(md::IfdKind::Ifd0)->find(md::exif_tag::kMake) == nullptr);
  const auto reduced_xmp = md::parse_xmp(bytes_of(copyright.xmp));
  CHECK(reduced_xmp->find(md::xmp_ns::kDc, "creator") != nullptr);
  CHECK(reduced_xmp->find(md::xmp_ns::kDc, "title") == nullptr);
  CHECK(reduced_xmp->find(md::xmp_ns::kXmpMM, "History") == nullptr);
  CHECK(md::parse_iim(copyright.iptc)->values(2, md::iim::kCity).empty());
  CHECK(copyright.text.empty());

  const auto none = md::prepare_save_metadata(document, md::MetadataPolicy::None, 40, 30);
  CHECK(none.exif.empty() && none.xmp.empty() && none.iptc.empty() && none.text.empty());
}

md::SaveMetadata full_save_metadata(std::size_t xmp_padding = 0) {
  patchy::Document document(2, 2, patchy::PixelFormat::rgb8());
  document.file_info() = sample_info();
  if (xmp_padding > 0) {
    // A packet past JPEG's 65502-byte APP1 limit forces Extended XMP.
    auto packet = md::parse_xmp(*document.file_info().xmp);
    std::vector<std::string> ancestors;
    for (std::size_t i = 0; i < xmp_padding; ++i) {
      ancestors.push_back("xmp.did:" + std::to_string(1000000 + i));
    }
    packet->set_array(md::xmp_ns::kPhotoshop, "DocumentAncestors", md::XmpValue::Kind::Bag, ancestors);
    document.file_info().xmp = patchy::make_metadata_bytes(bytes_of(md::serialize_xmp(*packet)));
  }
  document.color_state().embedded_icc_profile = fake_icc("Injected");
  return md::prepare_save_metadata(document, md::MetadataPolicy::All, 2, 2);
}

void inject_round_trips_through_every_container() {
  const auto metadata = full_save_metadata();
  CHECK(md::inject_metadata(minimal_png(), "png", md::SaveMetadata{}) == minimal_png());
  {
    const auto png = md::inject_metadata(minimal_png(), "png", metadata);
    int width = 0;
    int height = 0;
    int channels = 0;
    auto* pixels = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &width, &height, &channels, 3);
    CHECK(pixels != nullptr && width == 2 && pixels[0] == 255 && pixels[1] == 0);
    stbi_image_free(pixels);
    const auto info = md::extract_file_info(png);
    CHECK(info.exif && md::parse_tiff(*info.exif));
    CHECK(md::read_fields(info).title == "Sunset & Sea");
    CHECK(info.source_icc && *info.source_icc == fake_icc("Injected"));
    CHECK(info.text.size() == 1 && info.text[0].key == "Comment" && info.text[0].value == "hello comment");
    // A second injection replaces, never duplicates.
    const auto twice = md::extract_file_info(md::inject_metadata(png, "png", metadata));
    CHECK(twice.text.size() == 1);
  }
  {
    // Strip the source JPEG's own metadata segments, then inject.
    Bytes plain{0xFF, 0xD8};
    append(plain, jpeg_segment(0xE0, Bytes{'J', 'F', 'I', 'F', 0, 1, 2, 1, 0, 72, 0, 72, 0, 0}));
    append(plain, jpeg_segment(0xDA, Bytes{1, 1, 0, 0, 63, 0}));
    plain.insert(plain.end(), {0x12, 0x34, 0xFF, 0xD9});
    const auto jpeg = md::inject_metadata(plain, "jpg", metadata);
    CHECK(jpeg[2] == 0xFF && jpeg[3] == 0xE0);  // JFIF stays first
    const auto info = md::extract_file_info(jpeg);
    CHECK(info.exif && info.xmp && info.iptc && info.source_icc);
    CHECK(info.iptc_digest && *info.iptc_digest == md::iim_digest(*info.iptc));
    CHECK(info.text.size() == 1 && info.text[0].value == "hello comment");
    CHECK(md::read_fields(info).city == "Seattle");
    const auto big = full_save_metadata(4000);
    CHECK(big.xmp.size() > 65502);
    const auto extended = md::extract_file_info(md::inject_metadata(plain, "jpg", big));
    const auto packet = md::parse_xmp(*extended.xmp);
    CHECK(packet->items(md::xmp_ns::kPhotoshop, "DocumentAncestors").size() == 4000);
    CHECK(packet->text(md::xmp_ns::kDc, "title") == "Sunset & Sea");
  }
  {
    md::TiffStream tiff;
    tiff.big_endian = true;  // the EXIF block is little-endian: values must be re-encoded
    tiff.set_long(md::IfdKind::Ifd0, 256, 2);
    tiff.set_long(md::IfdKind::Ifd0, 257, 2);
    const auto injected = md::inject_metadata(md::write_tiff(tiff), "tif", metadata);
    const auto info = md::extract_file_info(injected);
    CHECK(info.xmp && info.source_icc && info.iptc);
    const auto exif = md::parse_tiff(*info.exif);
    CHECK(exif->string_value(*exif->find(md::IfdKind::Ifd0)->find(md::exif_tag::kMake)) == "PatchyCam");
    CHECK(md::format_exif_value(*exif, md::IfdKind::Exif,
                                *exif->find(md::IfdKind::Exif)->find(md::exif_tag::kExposureTime)) == "1/250 s");
    CHECK(md::summarize_camera(info).has_location());
    const auto file = md::parse_tiff(injected);
    CHECK(file->unsigned_value(*file->find(md::IfdKind::Ifd0)->find(256)) == 2U);
  }
  {
    Bytes gif;
    append(gif, "GIF87a");
    gif.insert(gif.end(), {1, 0, 1, 0, 0, 0, 0, 0x2C, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x4C, 0x01, 0, 0x3B});
    const auto injected = md::inject_metadata(gif, "gif", metadata);
    CHECK(injected[4] == '9');
    const auto info = md::extract_file_info(injected);
    CHECK(info.text.size() == 1 && info.text[0].value == "hello comment");
    CHECK(md::read_fields(info).title == "Sunset & Sea");
    CHECK(std::any_of(info.structure.begin(), info.structure.end(),
                      [](const auto& segment) { return segment.name == "Image 1"; }));
  }
}


// Save stamp (GitHub issue 219, docs/file-info.md "Save stamp"): PSD saves record their
// dates and creator in the XMP the way Photoshop does, from a fixed clock here.
md::XmpPacket stamped_packet(const patchy::DocumentFileInfo& info) {
  CHECK(info.xmp != nullptr);
  if (info.xmp == nullptr) {
    return {};
  }
  return md::parse_xmp(*info.xmp).value_or(md::XmpPacket{});
}

std::vector<std::string> history_field(const md::XmpPacket& packet, std::string_view field) {
  std::vector<std::string> values;
  if (const auto* history = packet.find(md::xmp_ns::kXmpMM, "History")) {
    for (const auto& item : history->value.items) {
      for (const auto& entry : item.fields) {
        if (entry.name == field) {
          values.push_back(entry.value.text);
        }
      }
    }
  }
  return values;
}

void metadata_xmp_date_is_iso_8601_with_the_offset() {
  CHECK(md::xmp_date(0, 0) == "1970-01-01T00:00:00+00:00");
  CHECK(md::xmp_date(1792478465, 540) == "2026-10-20T15:41:05+09:00");
  CHECK(md::xmp_date(1709208000, -300) == "2024-02-29T07:00:00-05:00");
  CHECK(md::xmp_date(951782400, 330) == "2000-02-29T05:30:00+05:30");
}

void metadata_save_stamp_records_creation_and_each_save() {
  patchy::DocumentFileInfo info;
  md::stamp_file_info_for_save(info, {"2026-10-20T15:41:05+09:00", "Patchy 9.9", "00000000-0000-4000-8000-000000000000"});
  auto packet = stamped_packet(info);
  CHECK(packet.text(md::xmp_ns::kXmp, "CreatorTool") == "Patchy 9.9");
  CHECK(packet.text(md::xmp_ns::kXmp, "CreateDate") == "2026-10-20T15:41:05+09:00");
  CHECK(packet.text(md::xmp_ns::kXmp, "ModifyDate") == "2026-10-20T15:41:05+09:00");
  CHECK(packet.text(md::xmp_ns::kXmp, "MetadataDate") == "2026-10-20T15:41:05+09:00");
  CHECK(history_field(packet, "action") == std::vector<std::string>{"created"});

  // The next save moves the modify date and adds a history entry; creation stays.
  md::stamp_file_info_for_save(info, {"2026-10-21T08:00:00+09:00", "Patchy 9.9", "00000000-0000-4000-8000-000000000000"});
  packet = stamped_packet(info);
  CHECK(packet.text(md::xmp_ns::kXmp, "CreateDate") == "2026-10-20T15:41:05+09:00");
  CHECK(packet.text(md::xmp_ns::kXmp, "ModifyDate") == "2026-10-21T08:00:00+09:00");
  CHECK((history_field(packet, "action") == std::vector<std::string>{"created", "saved"}));
  CHECK((history_field(packet, "when") ==
         std::vector<std::string>{"2026-10-20T15:41:05+09:00", "2026-10-21T08:00:00+09:00"}));

  // Through the PSD writer and reader: resource 1060 carries it.
  patchy::Document document(8, 8, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", patchy::PixelBuffer(8, 8, patchy::PixelFormat::rgb8()));
  document.file_info() = info;
  const auto read = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  const auto reread = stamped_packet(read.file_info());
  CHECK(reread.text(md::xmp_ns::kXmp, "CreatorTool") == "Patchy 9.9");
  CHECK(reread.text(md::xmp_ns::kXmp, "CreateDate") == "2026-10-20T15:41:05+09:00");
  CHECK(history_field(reread, "softwareAgent").size() == 2U);
}

// A file another application wrote keeps its creator, creation date, history and every
// other field; only the modify dates and a new "saved" entry change, as when Photoshop
// saves a file it did not create (pinball_from_photoshop.psd keeps "Adobe Photoshop 21.1"
// as its creator after a Photoshop 23.4 save, which went to xmpMM:History).
void metadata_save_stamp_keeps_another_applications_record() {
  const std::string source =
      R"x(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">)x"
      R"x(<rdf:Description rdf:about="" xmlns:xmp="http://ns.adobe.com/xap/1.0/" xmlns:dc="http://purl.org/dc/elements/1.1/")x"
      R"x( xmlns:xmpMM="http://ns.adobe.com/xap/1.0/mm/" xmlns:stEvt="http://ns.adobe.com/xap/1.0/sType/ResourceEvent#")x"
      R"x( xmp:CreatorTool="Adobe Photoshop 27.0 (Windows)" xmp:CreateDate="2025-01-02T03:04:05-08:00")x"
      R"x( xmp:ModifyDate="2025-01-02T03:04:05-08:00" xmpMM:DocumentID="xmp.did:1234">)x"
      R"x(<dc:title><rdf:Alt><rdf:li xml:lang="x-default">Poster</rdf:li></rdf:Alt></dc:title>)x"
      R"x(<xmpMM:History><rdf:Seq><rdf:li rdf:parseType="Resource"><stEvt:action>created</stEvt:action>)x"
      R"x(<stEvt:when>2025-01-02T03:04:05-08:00</stEvt:when><stEvt:softwareAgent>Adobe Photoshop 27.0 (Windows)</stEvt:softwareAgent>)x"
      R"x(</rdf:li></rdf:Seq></xmpMM:History></rdf:Description></rdf:RDF></x:xmpmeta>)x";
  patchy::DocumentFileInfo info;
  info.xmp = patchy::make_metadata_bytes(Bytes(source.begin(), source.end()));
  md::stamp_file_info_for_save(info, {"2026-10-20T15:41:05+09:00", "Patchy 9.9", "00000000-0000-4000-8000-000000000000"});
  const auto packet = stamped_packet(info);
  CHECK(packet.text(md::xmp_ns::kXmp, "CreatorTool") == "Adobe Photoshop 27.0 (Windows)");
  CHECK(packet.text(md::xmp_ns::kXmp, "CreateDate") == "2025-01-02T03:04:05-08:00");
  CHECK(packet.text(md::xmp_ns::kXmp, "ModifyDate") == "2026-10-20T15:41:05+09:00");
  CHECK(packet.text(md::xmp_ns::kXmp, "MetadataDate") == "2026-10-20T15:41:05+09:00");
  CHECK(packet.text(md::xmp_ns::kXmpMM, "DocumentID") == "xmp.did:1234");
  CHECK(packet.text(md::xmp_ns::kDc, "title") == "Poster");
  CHECK((history_field(packet, "action") == std::vector<std::string>{"created", "saved"}));
  CHECK((history_field(packet, "softwareAgent") ==
         std::vector<std::string>{"Adobe Photoshop 27.0 (Windows)", "Patchy 9.9"}));

  // A packet that does not parse is left alone rather than replaced.
  patchy::DocumentFileInfo broken;
  broken.xmp = patchy::make_metadata_bytes(Bytes{'n', 'o', 't', ' ', 'x', 'm', 'l'});
  const auto before = broken.xmp;
  md::stamp_file_info_for_save(broken, {"2026-10-20T15:41:05+09:00", "Patchy 9.9", "00000000-0000-4000-8000-000000000000"});
  CHECK(broken.xmp == before);
}

// Each save is a new instance (GitHub issue 219 follow-up): xmpMM:InstanceID matches the
// instanceID of the History entry the save adds, as in a Photoshop 27.11 save. The first
// save also names the document (DocumentID and OriginalDocumentID, from the same GUID as
// the "created" entry); later saves keep those.
void metadata_save_stamp_records_a_new_instance_per_save() {
  patchy::DocumentFileInfo info;
  md::stamp_file_info_for_save(info, {"2026-10-20T15:41:05+09:00", "Patchy 9.9", "6f1a0c2e-0000-4000-8000-000000000001"});
  auto packet = stamped_packet(info);
  CHECK(packet.text(md::xmp_ns::kXmpMM, "InstanceID") == "xmp.iid:6f1a0c2e-0000-4000-8000-000000000001");
  CHECK(packet.text(md::xmp_ns::kXmpMM, "DocumentID") == "xmp.did:6f1a0c2e-0000-4000-8000-000000000001");
  CHECK(packet.text(md::xmp_ns::kXmpMM, "OriginalDocumentID") == "xmp.did:6f1a0c2e-0000-4000-8000-000000000001");
  CHECK(history_field(packet, "instanceID") ==
        std::vector<std::string>{"xmp.iid:6f1a0c2e-0000-4000-8000-000000000001"});

  md::stamp_file_info_for_save(info, {"2026-10-21T08:00:00+09:00", "Patchy 9.9", "6f1a0c2e-0000-4000-8000-000000000002"});
  packet = stamped_packet(info);
  CHECK(packet.text(md::xmp_ns::kXmpMM, "InstanceID") == "xmp.iid:6f1a0c2e-0000-4000-8000-000000000002");
  CHECK(packet.text(md::xmp_ns::kXmpMM, "DocumentID") == "xmp.did:6f1a0c2e-0000-4000-8000-000000000001");
  CHECK(packet.text(md::xmp_ns::kXmpMM, "OriginalDocumentID") == "xmp.did:6f1a0c2e-0000-4000-8000-000000000001");
  CHECK((history_field(packet, "instanceID") ==
         std::vector<std::string>{"xmp.iid:6f1a0c2e-0000-4000-8000-000000000001",
                                  "xmp.iid:6f1a0c2e-0000-4000-8000-000000000002"}));

  // Another application's document keeps its ids; only the instance moves.
  const std::string source =
      R"x(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">)x"
      R"x(<rdf:Description rdf:about="" xmlns:xmp="http://ns.adobe.com/xap/1.0/" xmlns:xmpMM="http://ns.adobe.com/xap/1.0/mm/")x"
      R"x( xmp:CreateDate="2025-01-02T03:04:05-08:00" xmpMM:DocumentID="adobe:docid:photoshop:1234")x"
      R"x( xmpMM:OriginalDocumentID="xmp.did:abcd" xmpMM:InstanceID="xmp.iid:old"/></rdf:RDF></x:xmpmeta>)x";
  patchy::DocumentFileInfo other;
  other.xmp = patchy::make_metadata_bytes(Bytes(source.begin(), source.end()));
  md::stamp_file_info_for_save(other, {"2026-10-20T15:41:05+09:00", "Patchy 9.9", "6f1a0c2e-0000-4000-8000-000000000003"});
  packet = stamped_packet(other);
  CHECK(packet.text(md::xmp_ns::kXmpMM, "InstanceID") == "xmp.iid:6f1a0c2e-0000-4000-8000-000000000003");
  CHECK(packet.text(md::xmp_ns::kXmpMM, "DocumentID") == "adobe:docid:photoshop:1234");
  CHECK(packet.text(md::xmp_ns::kXmpMM, "OriginalDocumentID") == "xmp.did:abcd");
  CHECK(history_field(packet, "instanceID") ==
        std::vector<std::string>{"xmp.iid:6f1a0c2e-0000-4000-8000-000000000003"});
}

// EXIF IFD0 Software and DateTime name the saving application and time on every save, as
// Photoshop writes them (even into a file another application created; xmp:CreatorTool
// keeps the creator). Only an EXIF block the document already has is touched: a document
// without one gets none.
void metadata_save_stamp_updates_exif_software_and_date() {
  auto stream = sample_exif_stream(false);
  stream.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kSoftware, "OtherApp 1.0");
  stream.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kDateTime, "2020:01:02 03:04:05");
  patchy::DocumentFileInfo info;
  info.exif = patchy::make_metadata_bytes(md::write_tiff(stream));
  md::stamp_file_info_for_save(info, {"2026-10-20T15:41:05+09:00", "Patchy 9.9", "6f1a0c2e-0000-4000-8000-000000000001"});
  CHECK(info.exif != nullptr);
  const auto stamped = info.exif ? md::parse_tiff(*info.exif) : std::nullopt;
  CHECK(stamped.has_value());
  if (stamped) {
    const auto text = [&stamped](std::uint16_t tag) {
      const auto* ifd = stamped->find(md::IfdKind::Ifd0);
      const auto* entry = ifd != nullptr ? ifd->find(tag) : nullptr;
      return entry != nullptr ? stamped->string_value(*entry) : std::string{};
    };
    CHECK(text(md::exif_tag::kSoftware) == "Patchy 9.9");
    CHECK(text(md::exif_tag::kDateTime) == "2026:10:20 15:41:05");
    CHECK(text(md::exif_tag::kModel) == "Model X");
    CHECK(stamped->find(md::IfdKind::Gps) != nullptr);
  }
  const auto fields = md::read_fields(info);
  CHECK(fields.creator_tool == "Patchy 9.9");

  // Through the PSD writer and reader: resource 1058 carries it.
  patchy::Document document(8, 8, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", patchy::PixelBuffer(8, 8, patchy::PixelFormat::rgb8()));
  document.file_info() = info;
  const auto read = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document));
  CHECK(read.file_info().exif != nullptr && *read.file_info().exif == *info.exif);

  patchy::DocumentFileInfo fresh;
  md::stamp_file_info_for_save(fresh, {"2026-10-20T15:41:05+09:00", "Patchy 9.9", "6f1a0c2e-0000-4000-8000-000000000001"});
  CHECK(fresh.exif == nullptr);
}

// Photoshop acceptance inputs (File Info should show the dates and the application):
// a new document's first save, a Photoshop file's resave and a resave of a file with
// another application's EXIF, in test-artifacts/psd-xmp-dates for
// scripts/dev/photoshop-open-check.ps1.
void metadata_save_stamp_acceptance_files() {
  const auto out = std::filesystem::path("test-artifacts") / "psd-xmp-dates";
  std::filesystem::create_directories(out);
  const auto write = [](const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  };
  patchy::Document fresh(64, 48, patchy::PixelFormat::rgb8());
  fresh.add_pixel_layer("Background", patchy::PixelBuffer(64, 48, patchy::PixelFormat::rgb8()));
  md::stamp_file_info_for_save(fresh.file_info(), {"2026-10-20T15:41:05+09:00", "Patchy 9.9", "6f1a0c2e-5d3b-4c8a-9e21-7b40d2f8a101"});
  write(out / "new-document.psd", patchy::psd::DocumentIo::write_layered_rgb8(fresh));

  auto exif = sample_exif_stream(true);
  exif.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kSoftware, "OtherApp 1.0");
  exif.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kDateTime, "2020:01:02 03:04:05");
  fresh.file_info().exif = patchy::make_metadata_bytes(md::write_tiff(exif));
  md::stamp_file_info_for_save(fresh.file_info(), {"2026-10-21T08:00:00+09:00", "Patchy 9.9", "6f1a0c2e-5d3b-4c8a-9e21-7b40d2f8a102"});
  write(out / "exif-resave.psd", patchy::psd::DocumentIo::write_layered_rgb8(fresh));

  const auto source = patchy::test::local_psd_fixture_path("checkbox.psd");
  if (std::filesystem::exists(source)) {
    auto resaved = patchy::psd::DocumentIo::read_file(source);
    md::stamp_file_info_for_save(resaved.file_info(), {"2026-10-20T15:41:05+09:00", "Patchy 9.9", "6f1a0c2e-5d3b-4c8a-9e21-7b40d2f8a103"});
    write(out / "photoshop-resave.psd", patchy::psd::DocumentIo::write_layered_rgb8(resaved));
  }
}

}  // namespace

std::vector<patchy::test::TestCase> file_info_metadata_tests() {
  return {
      {"metadata_xmp_date_is_iso_8601_with_the_offset", metadata_xmp_date_is_iso_8601_with_the_offset},
      {"metadata_save_stamp_records_creation_and_each_save", metadata_save_stamp_records_creation_and_each_save},
      {"metadata_save_stamp_keeps_another_applications_record", metadata_save_stamp_keeps_another_applications_record},
      {"metadata_save_stamp_records_a_new_instance_per_save", metadata_save_stamp_records_a_new_instance_per_save},
      {"metadata_save_stamp_updates_exif_software_and_date", metadata_save_stamp_updates_exif_software_and_date},
      {"metadata_save_stamp_acceptance_files", metadata_save_stamp_acceptance_files},
      {"metadata_tiff_ifd_round_trips_both_byte_orders", tiff_ifd_round_trips_both_byte_orders},
      {"metadata_tiff_ifd_survives_hostile_offsets", tiff_ifd_survives_hostile_offsets},
      {"metadata_xmp_reads_every_rdf_form_and_round_trips", xmp_reads_every_rdf_form_and_round_trips},
      {"metadata_xmp_privacy_flags_history_and_text_layers", xmp_privacy_flags_history_and_text_layers},
      {"metadata_iptc_iim_round_trips_and_decodes_latin1", iptc_iim_round_trips_and_decodes_latin1},
      {"metadata_photoshop_irb_round_trips_odd_lengths", photoshop_irb_round_trips_odd_lengths},
      {"metadata_jpeg_container_extracts_every_block", jpeg_container_extracts_every_block},
      {"metadata_jpeg_extended_xmp_merges_after_md5_check", jpeg_extended_xmp_merges_after_md5_check},
      {"metadata_png_container_extracts_text_profiles_and_ai_parameters",
       png_container_extracts_text_profiles_and_ai_parameters},
      {"metadata_webp_gif_heif_bmp_containers_extract", webp_gif_heif_bmp_containers_extract},
      {"metadata_tiff_pdf_svg_containers_extract", tiff_pdf_svg_containers_extract},
      {"metadata_describe_file_info_lists_everything_with_privacy", describe_file_info_lists_everything_with_privacy},
      {"metadata_cbor_and_jumbf_summaries", cbor_and_jumbf_summaries},
      {"metadata_inflate_is_capped", inflate_is_capped},
      {"metadata_container_scanners_survive_mutation", container_scanners_survive_mutation},
      {"metadata_extract_file_info_from_unicode_paths", extract_file_info_from_unicode_paths},
      {"metadata_psd_round_trips_file_info_resources", psd_round_trips_file_info_resources},
      {"metadata_edit_fields_follow_mwg_and_mirror_legacy_blocks", edit_fields_follow_mwg_and_mirror_legacy_blocks},
      {"metadata_strip_and_targeted_removals", strip_and_targeted_removals},
      {"metadata_orientation_bakes_and_saves_as_normal", orientation_bakes_and_saves_as_normal},
      {"metadata_save_metadata_applies_rules_and_policies", save_metadata_applies_rules_and_policies},
      {"metadata_inject_round_trips_through_every_container", inject_round_trips_through_every_container},
  };
}
