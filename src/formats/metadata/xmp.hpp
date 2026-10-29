#pragma once

#include "formats/metadata/metadata_privacy.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// XMP (ISO 16684-1) packet model, reader and serializer (docs/file-info.md).
// Properties are keyed by namespace URI, never by prefix. The model keeps every
// property it reads, so editing a few File Info fields and writing the packet back
// does not lose unknown namespaces (Camera Raw settings, Lightroom, gain maps).
// Supported RDF: rdf:Description with property attributes and property elements,
// rdf:resource, rdf:parseType="Resource", nested rdf:Description structs,
// rdf:Seq/Bag/Alt arrays and xml:lang qualifiers. Other qualifiers are dropped.
namespace patchy::metadata {

namespace xmp_ns {
constexpr std::string_view kRdf = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
constexpr std::string_view kXml = "http://www.w3.org/XML/1998/namespace";
constexpr std::string_view kDc = "http://purl.org/dc/elements/1.1/";
constexpr std::string_view kXmp = "http://ns.adobe.com/xap/1.0/";
constexpr std::string_view kXmpRights = "http://ns.adobe.com/xap/1.0/rights/";
constexpr std::string_view kXmpMM = "http://ns.adobe.com/xap/1.0/mm/";
constexpr std::string_view kStEvt = "http://ns.adobe.com/xap/1.0/sType/ResourceEvent#";
constexpr std::string_view kStRef = "http://ns.adobe.com/xap/1.0/sType/ResourceRef#";
constexpr std::string_view kPhotoshop = "http://ns.adobe.com/photoshop/1.0/";
constexpr std::string_view kTiff = "http://ns.adobe.com/tiff/1.0/";
constexpr std::string_view kExif = "http://ns.adobe.com/exif/1.0/";
constexpr std::string_view kExifEx = "http://cipa.jp/exif/1.0/";
constexpr std::string_view kAux = "http://ns.adobe.com/exif/1.0/aux/";
constexpr std::string_view kCrs = "http://ns.adobe.com/camera-raw-settings/1.0/";
constexpr std::string_view kIptcCore = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
constexpr std::string_view kIptcExt = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
constexpr std::string_view kPlus = "http://ns.useplus.org/ldf/xmp/1.0/";
constexpr std::string_view kPdf = "http://ns.adobe.com/pdf/1.3/";
constexpr std::string_view kXmpNote = "http://ns.adobe.com/xmp/note/";
constexpr std::string_view kXmpGImg = "http://ns.adobe.com/xap/1.0/g/img/";
constexpr std::string_view kLightroom = "http://ns.adobe.com/lightroom/1.0/";
}  // namespace xmp_ns

struct XmpProperty;

struct XmpValue {
  enum class Kind : std::uint8_t { Simple, Struct, Seq, Bag, Alt };
  Kind kind{Kind::Simple};
  std::string text;                  // Simple
  std::string lang;                  // xml:lang qualifier, if any
  std::vector<XmpProperty> fields;   // Struct
  std::vector<XmpValue> items;       // Seq/Bag/Alt

  [[nodiscard]] bool is_array() const noexcept { return kind == Kind::Seq || kind == Kind::Bag || kind == Kind::Alt; }
};

struct XmpProperty {
  std::string ns;    // namespace URI
  std::string name;  // local name
  XmpValue value;
};

struct XmpPacket {
  std::vector<XmpProperty> properties;  // document order
  // Prefixes the source used, by namespace URI; the serializer reuses them.
  std::map<std::string, std::string> prefixes;

  [[nodiscard]] bool empty() const noexcept { return properties.empty(); }
  [[nodiscard]] const XmpProperty* find(std::string_view ns, std::string_view name) const noexcept;
  [[nodiscard]] XmpProperty* find(std::string_view ns, std::string_view name) noexcept;

  // Text of a simple property; for an Alt the x-default (or first) item; for a
  // Seq/Bag the items joined with "; ". Empty when absent.
  [[nodiscard]] std::string text(std::string_view ns, std::string_view name) const;
  // Array items (a simple property reads as one item).
  [[nodiscard]] std::vector<std::string> items(std::string_view ns, std::string_view name) const;

  // Setters erase the property when the value is empty.
  void set_simple(std::string_view ns, std::string_view name, const std::string& value);
  void set_lang_alt(std::string_view ns, std::string_view name, const std::string& value);
  void set_array(std::string_view ns, std::string_view name, XmpValue::Kind kind,
                 const std::vector<std::string>& values);
  void set_struct_field(std::string_view ns, std::string_view name, std::string_view field_ns,
                        std::string_view field_name, const std::string& value);
  [[nodiscard]] std::string struct_field(std::string_view ns, std::string_view name, std::string_view field_ns,
                                         std::string_view field_name) const;
  bool erase(std::string_view ns, std::string_view name);
  // Erases every property whose local name starts with `name_prefix` in `ns`.
  std::size_t erase_matching(std::string_view ns, std::string_view name_prefix);
};

// Parses an XMP packet (with or without the xpacket wrapper and x:xmpmeta).
// nullopt when the bytes are not XML or hold no rdf:RDF.
[[nodiscard]] std::optional<XmpPacket> parse_xmp(std::span<const std::uint8_t> bytes);

// Serializes to UTF-8 RDF/XML inside an xpacket wrapper, followed by
// `padding_bytes` of whitespace so in-place editors can grow it.
[[nodiscard]] std::string serialize_xmp(const XmpPacket& packet, std::size_t padding_bytes = 2048);

// Preferred prefix for a namespace (the well-known one, else empty).
[[nodiscard]] std::string_view well_known_xmp_prefix(std::string_view ns) noexcept;

// Flattened view for File Info's All Metadata list: one row per leaf value, with a
// path such as "dc:title[x-default]" or "xmpMM:History[2]/stEvt:action".
struct XmpRow {
  std::string path;
  std::string value;
  std::string ns;    // namespace of the top-level property
  std::string name;  // local name of the top-level property
  PrivacyClass privacy{PrivacyClass::None};
};
[[nodiscard]] std::vector<XmpRow> flatten_xmp(const XmpPacket& packet);

[[nodiscard]] PrivacyClass xmp_property_privacy(std::string_view ns, std::string_view name) noexcept;

// An ISO 8601 date-time whose time zone is hour-only ("+03") or basic ("+0300"), as GIMP and
// some exporters write, with the offset shown as "+03:00" (#500). Display only: anything
// else, "Z" and "+03:00" included, comes back unchanged.
[[nodiscard]] std::string xmp_date_display(std::string text);

// True for IPTC DigitalSourceType values that name a trained model (generative AI):
// trainedAlgorithmicMedia and compositeWithTrainedAlgorithmicMedia, as a full newscodes URI
// or a bare code. digitalCapture, digitalArt and the other values describe ordinary sources.
[[nodiscard]] bool is_ai_digital_source_type(std::string_view value) noexcept;

}  // namespace patchy::metadata
