#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Minimal deterministic XML DOM for the SVG reader (Qt-free, like the rest of
// src/formats). Deliberately hand-written rather than vendored: the one XML
// feature real-world SVG needs that mainstream lightweight parsers skip is
// internal-DTD entity expansion (old Illustrator exports declare namespace
// URIs as <!ENTITY ns_svg "..."> and reference them as &ns_svg;). Coverage:
// elements/attributes, comments, CDATA, processing instructions, DOCTYPE with
// internal-subset ENTITY declarations, the predefined + numeric character
// references, namespace-prefix resolution (SVG/xlink namespaces collapse to
// bare local names, foreign prefixes stay verbatim), UTF-8 with or without a
// BOM, UTF-16 (transcoded), and Latin-1 when the XML declaration says so.
// Lenient where real files are sloppy (stray '&', unknown declarations), with
// hard caps against pathological input (node count, depth, entity expansion).
namespace patchy::svg {

struct XmlNode {
  // Element name with the namespace resolved: elements in the SVG namespace
  // (or with no namespace) carry the bare local name ("svg", "path");
  // foreign-namespace elements keep their prefixed form ("sodipodi:namedview").
  // Empty for text nodes.
  std::string name;
  // Decoded attribute values in document order. xmlns declarations are
  // consumed by resolution and do not appear; attributes in the xlink
  // namespace collapse to their local name (xlink:href -> href).
  std::vector<std::pair<std::string, std::string>> attributes;
  // Element and text children in document order (mixed content keeps its
  // ordering, which <text>/<tspan> import needs). Whitespace-only text
  // between elements is dropped.
  std::vector<XmlNode> children;
  // Text-node content (entity-decoded, CDATA included). Empty for elements.
  std::string text;

  [[nodiscard]] bool is_text() const noexcept { return name.empty(); }
  // First attribute with this name, or null.
  [[nodiscard]] const std::string* attribute(std::string_view attribute_name) const noexcept;
  // Concatenated descendant text in document order.
  [[nodiscard]] std::string all_text() const;
};

// Parses the file's root element. Throws std::runtime_error with a
// plain-English message (and 1-based line number) on malformed input.
[[nodiscard]] XmlNode parse_xml(std::span<const std::uint8_t> bytes);

}  // namespace patchy::svg
