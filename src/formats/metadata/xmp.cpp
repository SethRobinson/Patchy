#include "formats/metadata/xmp.hpp"

#include "formats/svg_xml.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace patchy::metadata {
namespace {

constexpr int kMaxDepth = 32;

struct WellKnownPrefix {
  std::string_view ns;
  std::string_view prefix;
};

constexpr std::array kWellKnownPrefixes{
    WellKnownPrefix{xmp_ns::kRdf, "rdf"},
    WellKnownPrefix{xmp_ns::kXml, "xml"},
    WellKnownPrefix{xmp_ns::kDc, "dc"},
    WellKnownPrefix{xmp_ns::kXmp, "xmp"},
    WellKnownPrefix{xmp_ns::kXmpRights, "xmpRights"},
    WellKnownPrefix{xmp_ns::kXmpMM, "xmpMM"},
    WellKnownPrefix{xmp_ns::kStEvt, "stEvt"},
    WellKnownPrefix{xmp_ns::kStRef, "stRef"},
    WellKnownPrefix{xmp_ns::kPhotoshop, "photoshop"},
    WellKnownPrefix{xmp_ns::kTiff, "tiff"},
    WellKnownPrefix{xmp_ns::kExif, "exif"},
    WellKnownPrefix{xmp_ns::kExifEx, "exifEX"},
    WellKnownPrefix{xmp_ns::kAux, "aux"},
    WellKnownPrefix{xmp_ns::kCrs, "crs"},
    WellKnownPrefix{xmp_ns::kIptcCore, "Iptc4xmpCore"},
    WellKnownPrefix{xmp_ns::kIptcExt, "Iptc4xmpExt"},
    WellKnownPrefix{xmp_ns::kPlus, "plus"},
    WellKnownPrefix{xmp_ns::kPdf, "pdf"},
    WellKnownPrefix{xmp_ns::kXmpNote, "xmpNote"},
    WellKnownPrefix{xmp_ns::kXmpGImg, "xmpGImg"},
    WellKnownPrefix{xmp_ns::kLightroom, "lr"},
    WellKnownPrefix{"http://ns.adobe.com/xap/1.0/t/pg/", "xmpTPg"},
    WellKnownPrefix{"http://ns.adobe.com/xap/1.0/sType/Dimensions#", "stDim"},
    WellKnownPrefix{"http://ns.adobe.com/xap/1.0/bj/", "xmpBJ"},
    WellKnownPrefix{"http://ns.adobe.com/xmp/1.0/DynamicMedia/", "xmpDM"},
    WellKnownPrefix{"http://ns.adobe.com/hdr-gain-map/1.0/", "hdrgm"},
    WellKnownPrefix{"http://ns.google.com/photos/1.0/panorama/", "GPano"},
    WellKnownPrefix{"http://ns.google.com/photos/1.0/container/", "GContainer"},
    WellKnownPrefix{"http://ns.google.com/photos/1.0/container/item/", "Item"},
    WellKnownPrefix{"http://ns.adobe.com/photoshop/1.0/camera-profile", "stCamera"},
    WellKnownPrefix{"http://ns.microsoft.com/photo/1.0/", "MicrosoftPhoto"},
    WellKnownPrefix{"http://creativecommons.org/ns#", "cc"},
};

struct Scope {
  std::vector<std::pair<std::string, std::string>> bindings;  // prefix -> URI
  const Scope* parent{nullptr};

  [[nodiscard]] const std::string* lookup(std::string_view prefix) const {
    for (const auto* scope = this; scope != nullptr; scope = scope->parent) {
      for (const auto& [bound_prefix, uri] : scope->bindings) {
        if (bound_prefix == prefix) {
          return &uri;
        }
      }
    }
    return nullptr;
  }
};

struct QName {
  std::string ns;
  std::string local;
};

QName resolve(std::string_view name, const Scope& scope) {
  const auto colon = name.find(':');
  const std::string_view prefix = colon == std::string_view::npos ? std::string_view{} : name.substr(0, colon);
  const std::string_view local = colon == std::string_view::npos ? name : name.substr(colon + 1);
  if (prefix == "xml") {
    return {std::string(xmp_ns::kXml), std::string(local)};
  }
  if (const auto* uri = scope.lookup(prefix)) {
    return {*uri, std::string(local)};
  }
  // Unbound prefix: a sloppy writer. Fall back to the well-known namespace with that
  // prefix so the property is not lost.
  for (const auto& known : kWellKnownPrefixes) {
    if (known.prefix == prefix) {
      return {std::string(known.ns), std::string(local)};
    }
  }
  return {std::string(prefix), std::string(local)};
}

bool is_namespace_declaration(std::string_view name) {
  return name == "xmlns" || name.starts_with("xmlns:");
}

struct Reader {
  XmpPacket& packet;

  Scope scope_for(const svg::XmlNode& node, const Scope* parent) {
    Scope scope;
    scope.parent = parent;
    for (const auto& [name, value] : node.attributes) {
      if (name == "xmlns") {
        scope.bindings.emplace_back(std::string(), value);
      } else if (name.starts_with("xmlns:")) {
        auto prefix = name.substr(6);
        packet.prefixes.emplace(value, prefix);
        scope.bindings.emplace_back(std::move(prefix), value);
      }
    }
    return scope;
  }

  static bool is_rdf(const QName& name, std::string_view local) {
    return name.ns == xmp_ns::kRdf && name.local == local;
  }

  // Attributes that are not RDF/XML syntax become struct fields or simple properties.
  void attribute_properties(const svg::XmlNode& node, const Scope& scope, std::vector<XmpProperty>& out) {
    for (const auto& [name, value] : node.attributes) {
      if (is_namespace_declaration(name)) {
        continue;
      }
      const auto qname = resolve(name, scope);
      if (qname.ns == xmp_ns::kRdf || qname.ns == xmp_ns::kXml) {
        continue;
      }
      XmpProperty property;
      property.ns = qname.ns;
      property.name = qname.local;
      property.value.text = value;
      out.push_back(std::move(property));
    }
  }

  void element_properties(const svg::XmlNode& node, const Scope& scope, std::vector<XmpProperty>& out, int depth) {
    for (const auto& child : node.children) {
      if (child.is_text()) {
        continue;
      }
      const auto child_scope = scope_for(child, &scope);
      const auto qname = resolve(child.name, child_scope);
      XmpProperty property;
      property.ns = qname.ns;
      property.name = qname.local;
      property.value = value_of(child, child_scope, depth + 1);
      out.push_back(std::move(property));
    }
  }

  XmpValue value_of(const svg::XmlNode& node, const Scope& scope, int depth) {
    XmpValue value;
    if (depth > kMaxDepth) {
      return value;
    }
    bool has_field_attributes = false;
    for (const auto& [name, attribute_value] : node.attributes) {
      if (is_namespace_declaration(name)) {
        continue;
      }
      const auto qname = resolve(name, scope);
      if (qname.ns == xmp_ns::kXml && qname.local == "lang") {
        value.lang = attribute_value;
      } else if (is_rdf(qname, "resource")) {
        value.text = attribute_value;
        return value;
      } else if (is_rdf(qname, "parseType")) {
        if (attribute_value == "Resource") {
          value.kind = XmpValue::Kind::Struct;
          element_properties(node, scope, value.fields, depth);
          return value;
        }
      } else if (qname.ns != xmp_ns::kRdf) {
        has_field_attributes = true;
      }
    }
    const svg::XmlNode* element_child = nullptr;
    for (const auto& child : node.children) {
      if (!child.is_text()) {
        element_child = &child;
        break;
      }
    }
    if (element_child != nullptr) {
      const auto child_scope = scope_for(*element_child, &scope);
      const auto qname = resolve(element_child->name, child_scope);
      if (is_rdf(qname, "Seq") || is_rdf(qname, "Bag") || is_rdf(qname, "Alt")) {
        value.kind = is_rdf(qname, "Seq")   ? XmpValue::Kind::Seq
                     : is_rdf(qname, "Bag") ? XmpValue::Kind::Bag
                                            : XmpValue::Kind::Alt;
        for (const auto& item : element_child->children) {
          if (item.is_text()) {
            continue;
          }
          const auto item_scope = scope_for(item, &child_scope);
          if (!is_rdf(resolve(item.name, item_scope), "li")) {
            continue;
          }
          value.items.push_back(value_of(item, item_scope, depth + 1));
        }
        return value;
      }
      value.kind = XmpValue::Kind::Struct;
      if (is_rdf(qname, "Description")) {
        attribute_properties(*element_child, child_scope, value.fields);
        element_properties(*element_child, child_scope, value.fields, depth);
      } else {
        element_properties(node, scope, value.fields, depth);
      }
      // A qualified simple value (rdf:value plus qualifiers) reads as its value.
      if (value.fields.size() >= 1) {
        for (const auto& field : value.fields) {
          if (field.ns == xmp_ns::kRdf && field.name == "value") {
            XmpValue simple = field.value;
            simple.lang = value.lang;
            return simple;
          }
        }
      }
      return value;
    }
    if (has_field_attributes) {
      value.kind = XmpValue::Kind::Struct;
      attribute_properties(node, scope, value.fields);
      return value;
    }
    value.text = node.all_text();
    return value;
  }

  const svg::XmlNode* find_rdf(const svg::XmlNode& node, const Scope& scope, Scope& found_scope, int depth) {
    if (depth > kMaxDepth || node.is_text()) {
      return nullptr;
    }
    auto node_scope = scope_for(node, &scope);
    if (is_rdf(resolve(node.name, node_scope), "RDF")) {
      found_scope = std::move(node_scope);
      found_scope.parent = &scope;
      return &node;
    }
    for (const auto& child : node.children) {
      if (const auto* found = find_rdf(child, node_scope, found_scope, depth + 1)) {
        // found_scope.parent points into this frame's node_scope; flatten it so it
        // survives the return.
        Scope flattened;
        for (const auto* s = &found_scope; s != nullptr; s = s->parent) {
          flattened.bindings.insert(flattened.bindings.end(), s->bindings.begin(), s->bindings.end());
        }
        found_scope = std::move(flattened);
        return found;
      }
    }
    return nullptr;
  }
};

std::string escape_xml(std::string_view text, bool attribute) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += attribute ? "&quot;" : "\"";
        break;
      default:
        // XML 1.0 forbids most C0 controls; drop them rather than write an
        // unreadable packet.
        if (static_cast<unsigned char>(c) < 0x20 && c != '\n' && c != '\r' && c != '\t') {
          break;
        }
        out.push_back(c);
        break;
    }
  }
  return out;
}

struct Writer {
  std::map<std::string, std::string> prefix_of;  // URI -> prefix
  std::string out;

  void collect(const XmpProperty& property, std::set<std::string>& namespaces) {
    namespaces.insert(property.ns);
    collect_value(property.value, namespaces);
  }
  void collect_value(const XmpValue& value, std::set<std::string>& namespaces) {
    for (const auto& field : value.fields) {
      collect(field, namespaces);
    }
    for (const auto& item : value.items) {
      collect_value(item, namespaces);
    }
  }

  void assign_prefixes(const XmpPacket& packet) {
    std::set<std::string> namespaces;
    for (const auto& property : packet.properties) {
      collect(property, namespaces);
    }
    std::set<std::string> used{"rdf", "x", "xml", "xmlns"};
    int generated = 0;
    for (const auto& ns : namespaces) {
      std::string prefix;
      if (const auto it = packet.prefixes.find(ns); it != packet.prefixes.end()) {
        prefix = it->second;
      }
      if (prefix.empty() || used.contains(prefix)) {
        prefix = std::string(well_known_xmp_prefix(ns));
      }
      while (prefix.empty() || used.contains(prefix)) {
        prefix = "ns" + std::to_string(++generated);
      }
      used.insert(prefix);
      prefix_of[ns] = prefix;
    }
  }

  [[nodiscard]] std::string qname(const std::string& ns, const std::string& name) const {
    return prefix_of.at(ns) + ":" + name;
  }

  void indent(int level) { out.append(static_cast<std::size_t>(level) * 1, ' '); }

  static std::string lang_attribute(const XmpValue& value) {
    return value.lang.empty() ? std::string{} : " xml:lang=\"" + escape_xml(value.lang, true) + "\"";
  }

  void write_value_body(const XmpValue& value, int level) {
    switch (value.kind) {
      case XmpValue::Kind::Simple:
        break;
      case XmpValue::Kind::Struct:
        for (const auto& field : value.fields) {
          write_property(field, level);
        }
        break;
      case XmpValue::Kind::Seq:
      case XmpValue::Kind::Bag:
      case XmpValue::Kind::Alt: {
        const char* tag = value.kind == XmpValue::Kind::Seq ? "rdf:Seq"
                          : value.kind == XmpValue::Kind::Bag ? "rdf:Bag"
                                                              : "rdf:Alt";
        indent(level);
        out += "<";
        out += tag;
        out += ">\n";
        for (const auto& item : value.items) {
          write_element("rdf:li", item, level + 1);
        }
        indent(level);
        out += "</";
        out += tag;
        out += ">\n";
        break;
      }
    }
  }

  void write_element(const std::string& tag, const XmpValue& value, int level) {
    indent(level);
    out += "<" + tag + lang_attribute(value);
    if (value.kind == XmpValue::Kind::Simple) {
      out += ">" + escape_xml(value.text, false) + "</" + tag + ">\n";
      return;
    }
    if (value.kind == XmpValue::Kind::Struct) {
      out += " rdf:parseType=\"Resource\">\n";
    } else {
      out += ">\n";
    }
    write_value_body(value, level + 1);
    indent(level);
    out += "</" + tag + ">\n";
  }

  void write_property(const XmpProperty& property, int level) {
    write_element(qname(property.ns, property.name), property.value, level);
  }
};

const XmpValue* alt_default(const XmpValue& value) {
  if (value.items.empty()) {
    return nullptr;
  }
  for (const auto& item : value.items) {
    if (item.lang == "x-default") {
      return &item;
    }
  }
  return &value.items.front();
}

std::string value_text(const XmpValue& value) {
  switch (value.kind) {
    case XmpValue::Kind::Simple:
      return value.text;
    case XmpValue::Kind::Alt:
      if (const auto* item = alt_default(value)) {
        return value_text(*item);
      }
      return {};
    case XmpValue::Kind::Seq:
    case XmpValue::Kind::Bag: {
      std::string joined;
      for (const auto& item : value.items) {
        const auto text = value_text(item);
        if (text.empty()) {
          continue;
        }
        if (!joined.empty()) {
          joined += "; ";
        }
        joined += text;
      }
      return joined;
    }
    case XmpValue::Kind::Struct:
      return {};
  }
  return {};
}

}  // namespace

std::string_view well_known_xmp_prefix(std::string_view ns) noexcept {
  for (const auto& known : kWellKnownPrefixes) {
    if (known.ns == ns) {
      return known.prefix;
    }
  }
  return {};
}

const XmpProperty* XmpPacket::find(std::string_view ns, std::string_view name) const noexcept {
  for (const auto& property : properties) {
    if (property.ns == ns && property.name == name) {
      return &property;
    }
  }
  return nullptr;
}

XmpProperty* XmpPacket::find(std::string_view ns, std::string_view name) noexcept {
  for (auto& property : properties) {
    if (property.ns == ns && property.name == name) {
      return &property;
    }
  }
  return nullptr;
}

std::string XmpPacket::text(std::string_view ns, std::string_view name) const {
  const auto* property = find(ns, name);
  return property != nullptr ? value_text(property->value) : std::string{};
}

std::vector<std::string> XmpPacket::items(std::string_view ns, std::string_view name) const {
  std::vector<std::string> result;
  const auto* property = find(ns, name);
  if (property == nullptr) {
    return result;
  }
  if (property->value.is_array()) {
    for (const auto& item : property->value.items) {
      auto text = value_text(item);
      if (!text.empty()) {
        result.push_back(std::move(text));
      }
    }
  } else if (!property->value.text.empty()) {
    result.push_back(property->value.text);
  }
  return result;
}

void XmpPacket::set_simple(std::string_view ns, std::string_view name, const std::string& value) {
  if (value.empty()) {
    erase(ns, name);
    return;
  }
  auto* property = find(ns, name);
  if (property == nullptr) {
    properties.push_back(XmpProperty{std::string(ns), std::string(name), {}});
    property = &properties.back();
  }
  property->value = XmpValue{};
  property->value.text = value;
}

void XmpPacket::set_lang_alt(std::string_view ns, std::string_view name, const std::string& value) {
  if (value.empty()) {
    erase(ns, name);
    return;
  }
  auto* property = find(ns, name);
  if (property != nullptr && property->value.kind == XmpValue::Kind::Alt && !property->value.items.empty()) {
    // Keep the other languages; replace the default.
    for (auto& item : property->value.items) {
      if (item.lang == "x-default") {
        item.text = value;
        return;
      }
    }
    XmpValue item;
    item.text = value;
    item.lang = "x-default";
    property->value.items.insert(property->value.items.begin(), std::move(item));
    return;
  }
  if (property == nullptr) {
    properties.push_back(XmpProperty{std::string(ns), std::string(name), {}});
    property = &properties.back();
  }
  property->value = XmpValue{};
  property->value.kind = XmpValue::Kind::Alt;
  XmpValue item;
  item.text = value;
  item.lang = "x-default";
  property->value.items.push_back(std::move(item));
}

void XmpPacket::set_array(std::string_view ns, std::string_view name, XmpValue::Kind kind,
                          const std::vector<std::string>& values) {
  std::vector<std::string> kept;
  for (const auto& value : values) {
    if (!value.empty()) {
      kept.push_back(value);
    }
  }
  if (kept.empty()) {
    erase(ns, name);
    return;
  }
  auto* property = find(ns, name);
  if (property == nullptr) {
    properties.push_back(XmpProperty{std::string(ns), std::string(name), {}});
    property = &properties.back();
  }
  property->value = XmpValue{};
  property->value.kind = kind;
  for (const auto& value : kept) {
    XmpValue item;
    item.text = value;
    property->value.items.push_back(std::move(item));
  }
}

void XmpPacket::set_struct_field(std::string_view ns, std::string_view name, std::string_view field_ns,
                                 std::string_view field_name, const std::string& value) {
  auto* property = find(ns, name);
  if (property == nullptr) {
    if (value.empty()) {
      return;
    }
    properties.push_back(XmpProperty{std::string(ns), std::string(name), {}});
    property = &properties.back();
    property->value.kind = XmpValue::Kind::Struct;
  }
  if (property->value.kind != XmpValue::Kind::Struct) {
    property->value = XmpValue{};
    property->value.kind = XmpValue::Kind::Struct;
  }
  auto& fields = property->value.fields;
  const auto it = std::find_if(fields.begin(), fields.end(), [&](const XmpProperty& field) {
    return field.ns == field_ns && field.name == field_name;
  });
  if (value.empty()) {
    if (it != fields.end()) {
      fields.erase(it);
    }
    if (fields.empty()) {
      erase(ns, name);
    }
    return;
  }
  if (it != fields.end()) {
    it->value = XmpValue{};
    it->value.text = value;
  } else {
    XmpProperty field{std::string(field_ns), std::string(field_name), {}};
    field.value.text = value;
    fields.push_back(std::move(field));
  }
}

std::string XmpPacket::struct_field(std::string_view ns, std::string_view name, std::string_view field_ns,
                                    std::string_view field_name) const {
  const auto* property = find(ns, name);
  if (property == nullptr || property->value.kind != XmpValue::Kind::Struct) {
    return {};
  }
  for (const auto& field : property->value.fields) {
    if (field.ns == field_ns && field.name == field_name) {
      return value_text(field.value);
    }
  }
  return {};
}

bool XmpPacket::erase(std::string_view ns, std::string_view name) {
  const auto before = properties.size();
  std::erase_if(properties, [&](const XmpProperty& property) { return property.ns == ns && property.name == name; });
  return properties.size() != before;
}

std::size_t XmpPacket::erase_matching(std::string_view ns, std::string_view name_prefix) {
  const auto before = properties.size();
  std::erase_if(properties, [&](const XmpProperty& property) {
    return property.ns == ns && std::string_view(property.name).starts_with(name_prefix);
  });
  return before - properties.size();
}

std::optional<XmpPacket> parse_xmp(std::span<const std::uint8_t> bytes) {
  // Skip anything before the first '<' (stray padding, a BOM is handled by the parser).
  std::size_t start = 0;
  while (start < bytes.size() && bytes[start] != '<' && bytes[start] != 0xEF && bytes[start] != 0xFE &&
         bytes[start] != 0xFF) {
    ++start;
  }
  if (start >= bytes.size()) {
    return std::nullopt;
  }
  // Trailing NULs (fixed-size containers) are not XML.
  std::size_t end = bytes.size();
  while (end > start && bytes[end - 1] == 0) {
    --end;
  }
  svg::XmlNode root;
  try {
    svg::XmlParseOptions options;
    options.keep_namespace_declarations = true;
    root = svg::parse_xml(bytes.subspan(start, end - start), options);
  } catch (const std::exception&) {
    return std::nullopt;
  }
  XmpPacket packet;
  Reader reader{packet};
  Scope empty;
  Scope rdf_scope;
  const auto* rdf = reader.find_rdf(root, empty, rdf_scope, 0);
  if (rdf == nullptr) {
    return std::nullopt;
  }
  for (const auto& description : rdf->children) {
    if (description.is_text()) {
      continue;
    }
    const auto scope = reader.scope_for(description, &rdf_scope);
    // rdf:Description, or a typed node such as Inkscape's cc:Work (RDF/XML 2.13):
    // both carry the properties the same way.
    const auto node_name = resolve(description.name, scope);
    if (node_name.ns == xmp_ns::kRdf && node_name.local != "Description") {
      continue;
    }
    std::vector<XmpProperty> found;
    reader.attribute_properties(description, scope, found);
    reader.element_properties(description, scope, found, 0);
    for (auto& property : found) {
      // Duplicates across rdf:Description blocks: the first one wins.
      if (packet.find(property.ns, property.name) == nullptr) {
        packet.properties.push_back(std::move(property));
      }
    }
  }
  return packet;
}

std::string serialize_xmp(const XmpPacket& packet, std::size_t padding_bytes) {
  Writer writer;
  writer.assign_prefixes(packet);
  auto& out = writer.out;
  out += "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n";
  out += "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"Patchy\">\n";
  out += " <rdf:RDF xmlns:rdf=\"" + std::string(xmp_ns::kRdf) + "\">\n";
  out += "  <rdf:Description rdf:about=\"\"";
  for (const auto& [ns, prefix] : writer.prefix_of) {
    if (prefix == "xml") {
      continue;
    }
    out += "\n    xmlns:" + prefix + "=\"" + escape_xml(ns, true) + "\"";
  }
  out += ">\n";
  for (const auto& property : packet.properties) {
    writer.write_property(property, 3);
  }
  out += "  </rdf:Description>\n";
  out += " </rdf:RDF>\n";
  out += "</x:xmpmeta>\n";
  for (std::size_t written = 0; written < padding_bytes; written += 100) {
    out.append(99, ' ');
    out += '\n';
  }
  out += "<?xpacket end=\"w\"?>";
  return out;
}

PrivacyClass xmp_property_privacy(std::string_view ns, std::string_view name) noexcept {
  if (ns == xmp_ns::kExif && name.starts_with("GPS")) {
    return PrivacyClass::Location;
  }
  if (ns == xmp_ns::kPhotoshop) {
    if (name == "City" || name == "State" || name == "Country") {
      return PrivacyClass::Location;
    }
    if (name == "AuthorsPosition" || name == "CaptionWriter") {
      return PrivacyClass::Personal;
    }
    if (name == "DocumentAncestors" || name == "TextLayers" || name == "History") {
      return PrivacyClass::EditHistory;
    }
    if (name == "DateCreated") {
      return PrivacyClass::Timestamp;
    }
  }
  if (ns == xmp_ns::kIptcCore) {
    if (name == "Location" || name == "CountryCode") {
      return PrivacyClass::Location;
    }
    if (name == "CreatorContactInfo") {
      return PrivacyClass::Personal;
    }
  }
  if (ns == xmp_ns::kIptcExt) {
    if (name == "LocationCreated" || name == "LocationShown") {
      return PrivacyClass::Location;
    }
    if (name == "PersonInImage" || name == "PersonInImageWDetails") {
      return PrivacyClass::Personal;
    }
    if (name == "DigitalSourceType") {
      return PrivacyClass::AiPrompt;
    }
  }
  if (ns == xmp_ns::kDc && name == "creator") {
    return PrivacyClass::Personal;
  }
  if (ns == xmp_ns::kXmpRights && name == "Owner") {
    return PrivacyClass::Personal;
  }
  if ((ns == xmp_ns::kAux && (name == "SerialNumber" || name == "OwnerName" || name == "LensSerialNumber")) ||
      (ns == xmp_ns::kExifEx &&
       (name == "BodySerialNumber" || name == "LensSerialNumber" || name == "CameraOwnerName"))) {
    return PrivacyClass::DeviceIdentity;
  }
  if (ns == xmp_ns::kXmpMM) {
    return PrivacyClass::EditHistory;
  }
  if (ns == xmp_ns::kCrs) {
    return PrivacyClass::EditHistory;
  }
  if (ns == xmp_ns::kXmp) {
    if (name == "CreatorTool") {
      return PrivacyClass::MachineInfo;
    }
    if (name == "CreateDate" || name == "ModifyDate" || name == "MetadataDate") {
      return PrivacyClass::Timestamp;
    }
    if (name == "Thumbnails") {
      return PrivacyClass::HiddenImage;
    }
  }
  if (ns == xmp_ns::kExif && (name == "DateTimeOriginal" || name == "DateTimeDigitized")) {
    return PrivacyClass::Timestamp;
  }
  if (ns == xmp_ns::kPdf && name == "Producer") {
    return PrivacyClass::MachineInfo;
  }
  return PrivacyClass::None;
}

namespace {

void flatten_value(const XmpPacket& packet, const XmpValue& value, const std::string& path, const XmpProperty& top,
                   std::vector<XmpRow>& rows, const std::map<std::string, std::string>& prefixes, int depth) {
  if (depth > kMaxDepth) {
    return;
  }
  const auto prefix_for = [&](const std::string& ns) {
    if (const auto it = prefixes.find(ns); it != prefixes.end()) {
      return it->second;
    }
    const auto known = well_known_xmp_prefix(ns);
    return known.empty() ? ns : std::string(known);
  };
  const auto privacy = xmp_property_privacy(top.ns, top.name);
  switch (value.kind) {
    case XmpValue::Kind::Simple:
      rows.push_back(XmpRow{path + (value.lang.empty() || value.lang == "x-default" ? "" : "[" + value.lang + "]"),
                            value.text, top.ns, top.name, privacy});
      break;
    case XmpValue::Kind::Struct:
      if (value.fields.empty()) {
        rows.push_back(XmpRow{path, {}, top.ns, top.name, privacy});
      }
      for (const auto& field : value.fields) {
        flatten_value(packet, field.value, path + "/" + prefix_for(field.ns) + ":" + field.name, top, rows, prefixes,
                      depth + 1);
      }
      break;
    case XmpValue::Kind::Seq:
    case XmpValue::Kind::Bag:
    case XmpValue::Kind::Alt:
      if (value.items.empty()) {
        rows.push_back(XmpRow{path, {}, top.ns, top.name, privacy});
      }
      for (std::size_t i = 0; i < value.items.size(); ++i) {
        const auto& item = value.items[i];
        if (value.kind == XmpValue::Kind::Alt && item.kind == XmpValue::Kind::Simple) {
          // The default language reads as the property itself; others carry their tag.
          const std::string suffix = item.lang == "x-default" ? std::string{}
                                     : item.lang.empty()      ? "[" + std::to_string(i + 1) + "]"
                                                              : "[" + item.lang + "]";
          rows.push_back(XmpRow{path + suffix, item.text, top.ns, top.name, privacy});
          continue;
        }
        flatten_value(packet, item, path + "[" + std::to_string(i + 1) + "]", top, rows, prefixes, depth + 1);
      }
      break;
  }
}

}  // namespace

std::vector<XmpRow> flatten_xmp(const XmpPacket& packet) {
  std::vector<XmpRow> rows;
  for (const auto& property : packet.properties) {
    std::string prefix;
    if (const auto it = packet.prefixes.find(property.ns); it != packet.prefixes.end()) {
      prefix = it->second;
    } else {
      prefix = std::string(well_known_xmp_prefix(property.ns));
    }
    if (prefix.empty()) {
      prefix = property.ns;
    }
    flatten_value(packet, property.value, prefix + ":" + property.name, property, rows, packet.prefixes, 0);
  }
  for (auto& row : rows) {
    row.value = xmp_date_display(std::move(row.value));
    if (row.ns == xmp_ns::kIptcExt && row.name == "DigitalSourceType" && !is_ai_digital_source_type(row.value)) {
      row.privacy = PrivacyClass::None;  // a camera, scan or artwork source is not an AI record
    }
  }
  return rows;
}

std::string xmp_date_display(std::string text) {
  const auto digit = [&text](std::size_t at) { return at < text.size() && text[at] >= '0' && text[at] <= '9'; };
  const auto digits = [&digit](std::size_t from, std::size_t count) {
    for (std::size_t i = from; i < from + count; ++i) {
      if (!digit(i)) {
        return false;
      }
    }
    return true;
  };
  // YYYY-MM-DDThh:mm, then optional :ss and .fraction.
  if (!(digits(0, 4) && text.size() > 16 && text[4] == '-' && digits(5, 2) && text[7] == '-' && digits(8, 2) &&
        text[10] == 'T' && digits(11, 2) && text[13] == ':' && digits(14, 2))) {
    return text;
  }
  std::size_t zone = 16;
  if (text[zone] == ':' && digits(zone + 1, 2)) {
    zone += 3;
    if (zone < text.size() && text[zone] == '.' && digit(zone + 1)) {
      ++zone;
      while (digit(zone)) {
        ++zone;
      }
    }
  }
  if (zone >= text.size() || (text[zone] != '+' && text[zone] != '-')) {
    return text;
  }
  const auto rest = text.size() - zone - 1;
  const auto pair = [&text](std::size_t at) { return (text[at] - '0') * 10 + (text[at + 1] - '0'); };
  if (rest == 2 && digits(zone + 1, 2) && pair(zone + 1) <= 23) {
    text += ":00";
  } else if (rest == 4 && digits(zone + 1, 4) && pair(zone + 1) <= 23 && pair(zone + 3) <= 59) {
    text.insert(zone + 3, 1, ':');
  }
  return text;
}

bool is_ai_digital_source_type(std::string_view value) noexcept {
  const auto slash = value.find_last_of('/');
  const auto code = slash == std::string_view::npos ? value : value.substr(slash + 1);
  return code == "trainedAlgorithmicMedia" || code == "compositeWithTrainedAlgorithmicMedia";
}

}  // namespace patchy::metadata
