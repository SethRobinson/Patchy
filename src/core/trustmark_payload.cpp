#include "core/trustmark_payload.hpp"

#include <algorithm>

namespace patchy::trustmark {
namespace {

// The packet: payload, BCH parity filling the first 96 bits, then 0, 0 and the schema code.
constexpr int kCodedBits = 96;

// GF(2^7) with TrustMark's primitive polynomial 137 (x^7 + x^3 + 1).
constexpr int kFieldOrder = 127;
constexpr int kPrimitivePolynomial = 137;

struct Field {
  std::array<int, kFieldOrder + 1> exp{};
  std::array<int, kFieldOrder + 1> log{};

  Field() {
    int value = 1;
    for (int i = 0; i < kFieldOrder; ++i) {
      exp[static_cast<std::size_t>(i)] = value;
      log[static_cast<std::size_t>(value)] = i;
      value <<= 1;
      if ((value & 128) != 0) {
        value ^= kPrimitivePolynomial;
      }
    }
    exp[kFieldOrder] = 1;
  }

  [[nodiscard]] int power(int exponent) const {
    const int reduced = ((exponent % kFieldOrder) + kFieldOrder) % kFieldOrder;
    return exp[static_cast<std::size_t>(reduced)];
  }
  [[nodiscard]] int multiply(int a, int b) const {
    if (a == 0 || b == 0) {
      return 0;
    }
    return power(log[static_cast<std::size_t>(a)] + log[static_cast<std::size_t>(b)]);
  }
  [[nodiscard]] int divide(int a, int b) const {
    if (a == 0) {
      return 0;
    }
    return power(log[static_cast<std::size_t>(a)] - log[static_cast<std::size_t>(b)]);
  }
};

const Field& field() {
  static const Field instance;
  return instance;
}

// Narrow-sense binary BCH code correcting `t` errors: the generator is the product of
// (x - a^i) over the cyclotomic cosets of 1, 3, ..., 2t - 1.
class Bch {
public:
  explicit Bch(int t) : t_(t) {
    const auto& gf = field();
    std::array<bool, kFieldOrder> root{};
    for (int i = 0; i < t_; ++i) {
      int r = 2 * i + 1;
      for (int j = 0; j < 7; ++j) {
        root[static_cast<std::size_t>(r)] = true;
        r = (2 * r) % kFieldOrder;
      }
    }
    std::vector<int> generator{1};  // coefficient of x^j at index j
    for (int i = 0; i < kFieldOrder; ++i) {
      if (!root[static_cast<std::size_t>(i)]) {
        continue;
      }
      std::vector<int> next(generator.size() + 1, 0);
      for (std::size_t j = 0; j < generator.size(); ++j) {
        next[j] ^= gf.multiply(generator[j], gf.power(i));
        next[j + 1] ^= generator[j];
      }
      generator = std::move(next);
    }
    // Every coefficient of a product of minimal polynomials is 0 or 1.
    for (auto it = generator.rbegin(); it != generator.rend(); ++it) {
      generator_msb_first_.push_back(static_cast<std::uint8_t>(*it & 1));
    }
  }

  [[nodiscard]] int degree() const { return static_cast<int>(generator_msb_first_.size()) - 1; }

  // Systematic parity of `data` zero-padded to whole bytes, as bchlib computes it.
  [[nodiscard]] std::vector<std::uint8_t> parity(const std::vector<std::uint8_t>& data) const {
    std::vector<std::uint8_t> dividend = padded(data);
    const auto data_length = dividend.size();
    dividend.resize(data_length + static_cast<std::size_t>(degree()), 0);
    for (std::size_t i = 0; i < data_length; ++i) {
      if (dividend[i] != 0) {
        for (std::size_t j = 0; j < generator_msb_first_.size(); ++j) {
          dividend[i + j] = static_cast<std::uint8_t>(dividend[i + j] ^ generator_msb_first_[j]);
        }
      }
    }
    return {dividend.begin() + static_cast<std::ptrdiff_t>(data_length), dividend.end()};
  }

  // Corrects `data` and `ecc` in place; the number of corrected bits, or -1.
  [[nodiscard]] int correct(std::vector<std::uint8_t>& data, std::vector<std::uint8_t>& ecc) const {
    const auto& gf = field();
    std::vector<std::uint8_t> received = padded(data);
    const auto data_length = received.size();
    received.insert(received.end(), ecc.begin(), ecc.end());
    const int length = static_cast<int>(received.size());
    // Syndromes S_1 .. S_2t: the received word evaluated at a^j, bit k standing for
    // x^(length - 1 - k).
    std::vector<int> syndrome(static_cast<std::size_t>(2 * t_ + 1), 0);
    bool clean = true;
    for (int j = 1; j <= 2 * t_; ++j) {
      int value = 0;
      for (int k = 0; k < length; ++k) {
        if (received[static_cast<std::size_t>(k)] != 0) {
          value ^= gf.power(j * (length - 1 - k));
        }
      }
      syndrome[static_cast<std::size_t>(j)] = value;
      clean = clean && value == 0;
    }
    if (clean) {
      return 0;
    }
    // Berlekamp-Massey: the error locator polynomial.
    std::vector<int> locator(static_cast<std::size_t>(2 * t_ + 1), 0);
    std::vector<int> previous(static_cast<std::size_t>(2 * t_ + 1), 0);
    locator[0] = 1;
    previous[0] = 1;
    int errors = 0;
    int shift = 1;
    int last_discrepancy = 1;
    for (int n = 0; n < 2 * t_; ++n) {
      int discrepancy = syndrome[static_cast<std::size_t>(n + 1)];
      for (int i = 1; i <= errors; ++i) {
        discrepancy ^= gf.multiply(locator[static_cast<std::size_t>(i)], syndrome[static_cast<std::size_t>(n + 1 - i)]);
      }
      if (discrepancy == 0) {
        ++shift;
        continue;
      }
      const auto saved = locator;
      const int factor = gf.divide(discrepancy, last_discrepancy);
      for (int i = 0; i + shift <= 2 * t_; ++i) {
        locator[static_cast<std::size_t>(i + shift)] ^= gf.multiply(factor, previous[static_cast<std::size_t>(i)]);
      }
      if (2 * errors <= n) {
        errors = n + 1 - errors;
        previous = saved;
        last_discrepancy = discrepancy;
        shift = 1;
      } else {
        ++shift;
      }
    }
    if (errors > t_) {
      return -1;
    }
    // Chien search: position k is wrong when the locator vanishes at a^-(length - 1 - k).
    int found = 0;
    for (int k = 0; k < length; ++k) {
      const int position = length - 1 - k;
      int value = 1;
      for (int j = 1; j <= errors; ++j) {
        value ^= gf.multiply(locator[static_cast<std::size_t>(j)], gf.power(-j * position));
      }
      if (value == 0) {
        received[static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(received[static_cast<std::size_t>(k)] ^ 1U);
        ++found;
      }
    }
    if (found != errors) {
      return -1;  // roots outside the shortened code: more errors than it can fix
    }
    std::vector<std::uint8_t> corrected_data(received.begin(), received.begin() + static_cast<std::ptrdiff_t>(data.size()));
    // A correction may not touch the zero padding.
    for (std::size_t i = data.size(); i < data_length; ++i) {
      if (received[i] != 0) {
        return -1;
      }
    }
    std::vector<std::uint8_t> corrected_ecc(received.begin() + static_cast<std::ptrdiff_t>(data_length), received.end());
    if (parity(corrected_data) != corrected_ecc) {
      return -1;
    }
    data = std::move(corrected_data);
    ecc = std::move(corrected_ecc);
    return found;
  }

private:
  static std::vector<std::uint8_t> padded(const std::vector<std::uint8_t>& bits) {
    std::vector<std::uint8_t> out = bits;
    out.resize((bits.size() + 7) / 8 * 8, 0);
    return out;
  }

  int t_;
  std::vector<std::uint8_t> generator_msb_first_;
};

const Bch& bch_for(Schema schema) {
  static const Bch super_code(8);
  static const Bch bch5(5);
  static const Bch bch4(4);
  static const Bch bch3(3);
  switch (schema) {
    case Schema::BchSuper:
      return super_code;
    case Schema::Bch5:
      return bch5;
    case Schema::Bch4:
      return bch4;
    case Schema::Bch3:
      return bch3;
  }
  return bch5;
}

}  // namespace

std::string_view schema_name(Schema schema) noexcept {
  switch (schema) {
    case Schema::BchSuper:
      return "BCH_SUPER";
    case Schema::Bch5:
      return "BCH_5";
    case Schema::Bch4:
      return "BCH_4";
    case Schema::Bch3:
      return "BCH_3";
  }
  return "BCH_5";
}

std::optional<Schema> schema_from_name(std::string_view name) noexcept {
  for (const auto schema : {Schema::BchSuper, Schema::Bch5, Schema::Bch4, Schema::Bch3}) {
    if (schema_name(schema) == name) {
      return schema;
    }
  }
  return std::nullopt;
}

int schema_data_bits(Schema schema) noexcept {
  switch (schema) {
    case Schema::BchSuper:
      return 40;
    case Schema::Bch5:
      return 61;
    case Schema::Bch4:
      return 68;
    case Schema::Bch3:
      return 75;
  }
  return 61;
}

int schema_correctable_bits(Schema schema) noexcept {
  switch (schema) {
    case Schema::BchSuper:
      return 8;
    case Schema::Bch5:
      return 5;
    case Schema::Bch4:
      return 4;
    case Schema::Bch3:
      return 3;
  }
  return 5;
}

int schema_text_capacity(Schema schema) noexcept {
  return schema_data_bits(schema) / 7;
}

std::vector<std::uint8_t> text_to_bits(std::string_view ascii) {
  std::vector<std::uint8_t> bits;
  bits.reserve(ascii.size() * 7);
  for (const char ch : ascii) {
    const auto code = static_cast<unsigned>(static_cast<unsigned char>(ch)) & 127U;
    for (int bit = 6; bit >= 0; --bit) {
      bits.push_back(static_cast<std::uint8_t>((code >> static_cast<unsigned>(bit)) & 1U));
    }
  }
  return bits;
}

std::string bits_to_text(const std::vector<std::uint8_t>& data_bits) {
  std::string text;
  for (std::size_t start = 0; start + 7 <= data_bits.size(); start += 7) {
    unsigned code = 0;
    for (std::size_t i = 0; i < 7; ++i) {
      code = (code << 1U) | (data_bits[start + i] & 1U);
    }
    text.push_back(static_cast<char>(code));
  }
  while (!text.empty() && text.back() == '\0') {
    text.pop_back();
  }
  const auto first = text.find_first_not_of(' ');
  if (first == std::string::npos) {
    return {};
  }
  text = text.substr(first, text.find_last_not_of(' ') - first + 1);
  for (const char ch : text) {
    if (ch < 32 || ch > 126) {
      return {};
    }
  }
  return text;
}

std::string bits_to_hex(const std::vector<std::uint8_t>& data_bits) {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  std::string hex = "0x";
  for (std::size_t start = 0; start < data_bits.size(); start += 4) {
    unsigned nibble = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      const auto index = start + i;
      nibble = (nibble << 1U) | (index < data_bits.size() ? (data_bits[index] & 1U) : 0U);
    }
    hex.push_back(kDigits[nibble]);
  }
  return hex;
}

Packet encode_packet(std::vector<std::uint8_t> data_bits, Schema schema) {
  const auto data_length = static_cast<std::size_t>(schema_data_bits(schema));
  data_bits.resize(data_length, 0);
  for (auto& bit : data_bits) {
    bit = static_cast<std::uint8_t>(bit != 0 ? 1 : 0);
  }
  auto ecc = bch_for(schema).parity(data_bits);
  ecc.resize(static_cast<std::size_t>(kCodedBits) - data_length, 0);
  Packet packet{};
  std::copy(data_bits.begin(), data_bits.end(), packet.begin());
  std::copy(ecc.begin(), ecc.end(), packet.begin() + static_cast<std::ptrdiff_t>(data_length));
  const auto code = static_cast<unsigned>(schema);
  packet[96] = 0;
  packet[97] = 0;
  packet[98] = static_cast<std::uint8_t>((code >> 1U) & 1U);
  packet[99] = static_cast<std::uint8_t>(code & 1U);
  return packet;
}

std::optional<DecodedPacket> decode_packet(const Packet& packet) {
  const auto schema = static_cast<Schema>(((packet[98] & 1U) << 1U) | (packet[99] & 1U));
  const auto data_length = static_cast<std::size_t>(schema_data_bits(schema));
  std::vector<std::uint8_t> data(packet.begin(), packet.begin() + static_cast<std::ptrdiff_t>(data_length));
  std::vector<std::uint8_t> ecc(packet.begin() + static_cast<std::ptrdiff_t>(data_length),
                                packet.begin() + kCodedBits);
  for (auto& bit : data) {
    bit = static_cast<std::uint8_t>(bit & 1U);
  }
  for (auto& bit : ecc) {
    bit = static_cast<std::uint8_t>(bit & 1U);
  }
  const int corrected = bch_for(schema).correct(data, ecc);
  if (corrected < 0) {
    return std::nullopt;
  }
  return DecodedPacket{schema, std::move(data), corrected};
}

}  // namespace patchy::trustmark
