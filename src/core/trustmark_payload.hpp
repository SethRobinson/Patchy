#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The 100-bit packet of Adobe's TrustMark watermark (docs/watermarks.md): payload bits, a
// BCH code over GF(2^7) that protects them, two reserved zero bits and a two-bit schema
// code. Patchy-owned implementation of the published layout (TrustMark's datalayer.py and
// bchecc.py, MIT; docs/legal-constraints.md, "Watermark detection"). Qt-free and exact:
// integer math only.
namespace patchy::trustmark {

constexpr int kPacketBits = 100;
using Packet = std::array<std::uint8_t, kPacketBits>;  // 0/1 per bit, in model order

// Error-correction schemas, by the code TrustMark stores in the packet's last two bits.
// The values are TrustMark's own and appear in scripts ("BCH_5" and so on): permanent.
enum class Schema : std::uint8_t { BchSuper = 0, Bch5 = 1, Bch4 = 2, Bch3 = 3 };

[[nodiscard]] std::string_view schema_name(Schema schema) noexcept;  // "BCH_SUPER", "BCH_5", ...
[[nodiscard]] std::optional<Schema> schema_from_name(std::string_view name) noexcept;
// Payload bits the schema protects: 40, 61, 68, 75.
[[nodiscard]] int schema_data_bits(Schema schema) noexcept;
// Bit errors its BCH code corrects: 8, 5, 4, 3.
[[nodiscard]] int schema_correctable_bits(Schema schema) noexcept;
// Text characters that fit (7 bits each): 5, 8, 9, 10.
[[nodiscard]] int schema_text_capacity(Schema schema) noexcept;

// TrustMark's text form: 7-bit ASCII per character (code & 127), most significant bit
// first. Characters past the schema's capacity are dropped by encode_packet.
[[nodiscard]] std::vector<std::uint8_t> text_to_bits(std::string_view ascii);
// The text in `data_bits` (whole 7-bit groups), trailing NULs and surrounding spaces
// removed. Empty when any character is outside printable ASCII (the payload is then an ID).
[[nodiscard]] std::string bits_to_text(const std::vector<std::uint8_t>& data_bits);
// "0x" and the bits as hexadecimal, most significant first, the last digit padded with
// zero bits (61 bits read as 16 digits).
[[nodiscard]] std::string bits_to_hex(const std::vector<std::uint8_t>& data_bits);

// The full packet for `data_bits` (shorter payloads are padded with zeros, longer ones
// truncated to the schema's capacity), as TrustMark's DataLayer.process_encode builds it.
[[nodiscard]] Packet encode_packet(std::vector<std::uint8_t> data_bits, Schema schema);

struct DecodedPacket {
  Schema schema{Schema::Bch5};
  std::vector<std::uint8_t> data_bits;  // schema_data_bits(schema) bits, corrected
  int corrected_bits{0};
};

// Reads the schema from the packet's last two bits and corrects the payload with that
// schema's BCH code, as TrustMark's decoder does (no other schema is tried, which keeps
// chance matches on unmarked images rare). Empty when the code cannot correct it.
[[nodiscard]] std::optional<DecodedPacket> decode_packet(const Packet& packet);

}  // namespace patchy::trustmark
