#pragma once

#include "formats/pdf_filters.hpp"
#include "formats/pdf_syntax.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// PDF document structure (ISO 32000-1 clause 7.5): the cross-reference chain,
// object and cross-reference streams, indirect-reference resolution, stream
// decoding, and the page tree. Qt-free.
//
// Everything here is lenient by design. A PDF whose xref is wrong is not rare, it
// is Tuesday: producers truncate files, editors append without fixing offsets, and
// transfers corrupt bytes. Acrobat silently rebuilds and so does this, because the
// alternative is refusing files every other viewer opens.

namespace patchy::pdf {

// One page's dictionary plus the attributes it inherits from its ancestors
// (clause 7.7.3.4), already resolved so the content interpreter never walks up.
struct Page {
  Object dict;
  Object resources;
  // Media/crop boxes in default user space, normalized so lower <= upper.
  double media_box[4]{0.0, 0.0, 612.0, 792.0};
  double crop_box[4]{0.0, 0.0, 612.0, 792.0};
  int rotate{0};  // 0/90/180/270, already normalized
};

class File {
public:
  // Takes ownership of the file bytes; every Object refers into them. Returns
  // nullopt only when the data is not a PDF at all. Recoverable damage is reported
  // through `notices` and the file still opens.
  [[nodiscard]] static std::optional<File> open(std::vector<std::uint8_t> bytes, std::vector<std::string>* notices);

  [[nodiscard]] const Object& trailer() const noexcept { return trailer_; }
  [[nodiscard]] const Object& catalog() const;
  [[nodiscard]] const std::vector<Page>& pages() const noexcept { return pages_; }
  // True when the file declared /Encrypt. Decryption is not implemented yet, so
  // strings and streams from such a file are still ciphertext; the importer refuses
  // rather than emitting garbage.
  [[nodiscard]] bool is_encrypted() const noexcept { return encrypted_; }
  [[nodiscard]] const std::string& version() const noexcept { return version_; }

  // Follows an indirect reference (and a chain of them) to a direct object.
  // A missing or circular reference resolves to null.
  [[nodiscard]] const Object& resolve(const Object& object) const;
  [[nodiscard]] const Object& object(Reference reference) const;
  // Dictionary lookup that resolves the result: the form nearly every caller wants,
  // because any value in a PDF may be an indirect reference.
  [[nodiscard]] const Object& get(const Object& container, std::string_view key) const;
  // First present key, for the /Filter vs /F abbreviation pairs inline images use.
  [[nodiscard]] const Object& get_any(const Object& container, std::string_view key,
                                      std::string_view alternate) const;

  // Fully decoded stream contents. `image_codec` reports when the chain stopped at
  // an image codec, in which case the bytes are that codec's own encoding.
  struct StreamData {
    std::vector<std::uint8_t> data;
    FilterKind image_codec{FilterKind::None};
    std::string error;
  };
  [[nodiscard]] StreamData stream_data(const Object& stream_object) const;
  // The still-encoded bytes, with only decryption applied.
  [[nodiscard]] std::span<const std::uint8_t> raw_stream_bytes(const Object& stream_object) const;

  // Resolves the /Filter and /DecodeParms pair (either may be a single item or an
  // array, and either may be indirect) into an ordered chain.
  [[nodiscard]] std::vector<FilterStep> filter_chain(const Object& stream_object) const;

  // Numbers from a resolved array, for /MediaBox and friends. Missing entries are 0.
  [[nodiscard]] std::vector<double> numbers(const Object& array_object) const;

private:
  File() = default;

  void parse_xref_chain(std::vector<std::string>* notices);
  bool parse_xref_section(std::size_t offset, std::vector<std::size_t>& visited, std::vector<std::string>* notices);
  bool parse_xref_table(Lexer& lexer);
  bool parse_xref_stream(const Object& stream_object);
  void reconstruct_by_scanning(std::vector<std::string>* notices);
  void load_object_stream(std::uint32_t stream_number) const;
  [[nodiscard]] std::size_t resolve_stream_length(const RawStream& stream) const;
  void collect_pages(std::vector<std::string>* notices);

  std::vector<std::uint8_t> bytes_;
  std::string version_;
  Object trailer_;
  bool encrypted_{false};

  // Where each object lives: either at a byte offset, or inside an object stream.
  struct Location {
    std::size_t offset{0};
    std::uint32_t container_stream{0};  // 0 = a plain offset
    std::uint32_t index_in_stream{0};
    bool in_object_stream{false};
  };
  std::map<std::uint32_t, Location> locations_;

  mutable std::unordered_map<std::uint32_t, Object> cache_;
  mutable std::unordered_map<std::uint32_t, bool> loaded_object_streams_;
  mutable int resolve_depth_{0};
  mutable bool scanned_{false};

  std::vector<Page> pages_;
};

}  // namespace patchy::pdf
