#pragma once

#include "formats/pdf_syntax.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// PDF standard security handler (ISO 32000-1/-2 clause 7.6): RC4 and AES
// decryption with the revision 2-6 key derivations. Qt-free, so the
// hashes and ciphers are implemented here; they are small, fully specified, and a
// dependency would cost more than the ~400 lines they take.
//
// Encryption is NOT implemented: Patchy never writes encrypted PDF.

namespace patchy::pdf {

// Digest primitives, exposed for tests.
[[nodiscard]] std::array<std::uint8_t, 16> md5(std::span<const std::uint8_t> data);
[[nodiscard]] std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data);
[[nodiscard]] std::array<std::uint8_t, 48> sha384(std::span<const std::uint8_t> data);
[[nodiscard]] std::array<std::uint8_t, 64> sha512(std::span<const std::uint8_t> data);

// RC4 with an arbitrary-length key; output size equals input size.
[[nodiscard]] std::vector<std::uint8_t> rc4(std::span<const std::uint8_t> key, std::span<const std::uint8_t> data);

// AES-CBC decryption for 128/192/256-bit keys. `data` starts with the 16-byte IV
// unless `iv_in_data` is false (revision 6 uses a zero IV for the key unwrap).
// Strips PKCS#7 padding when `strip_padding`. Returns empty on malformed input.
[[nodiscard]] std::vector<std::uint8_t> aes_cbc_decrypt(std::span<const std::uint8_t> key,
                                                        std::span<const std::uint8_t> data, bool iv_in_data = true,
                                                        bool strip_padding = true);

// The decryption state for one open file.
class Decryptor {
public:
  struct Inputs {
    int v{0};
    int revision{0};
    int length_bits{40};
    std::string owner_hash;      // /O
    std::string user_hash;       // /U
    std::string owner_key;       // /OE (revisions 5 and 6)
    std::string user_key;        // /UE (revisions 5 and 6)
    std::int64_t permissions{0};  // /P, sign-extended
    bool encrypt_metadata{true};
    std::string first_file_id;   // the first element of the trailer /ID
    // The stream/string crypt filter methods from /CF + /StmF + /StrF:
    // "Identity", "V2" (RC4), "AESV2", "AESV3".
    std::string stream_method{"V2"};
    std::string string_method{"V2"};
  };

  // Derives the file key from the password (tried as the user password, then as
  // the owner password). Returns nullopt when the password is wrong or the
  // handler revision is unsupported.
  [[nodiscard]] static std::optional<Decryptor> create(const Inputs& inputs, std::string_view password);

  // Decrypts one stream's or string's bytes with the object's own key.
  [[nodiscard]] std::vector<std::uint8_t> decrypt_stream(std::uint32_t object_number, std::uint16_t generation,
                                                         std::span<const std::uint8_t> data) const;
  [[nodiscard]] std::string decrypt_string(std::uint32_t object_number, std::uint16_t generation,
                                           std::string_view data) const;

  [[nodiscard]] bool strings_are_identity() const noexcept { return string_method_ == Method::Identity; }

private:
  enum class Method { Identity, Rc4, Aes };

  [[nodiscard]] std::vector<std::uint8_t> object_key(std::uint32_t object_number, std::uint16_t generation,
                                                     Method method) const;
  [[nodiscard]] std::vector<std::uint8_t> apply(Method method, std::span<const std::uint8_t> key,
                                                std::span<const std::uint8_t> data) const;

  std::vector<std::uint8_t> file_key_;
  int revision_{0};
  Method stream_method_{Method::Rc4};
  Method string_method_{Method::Rc4};
};

}  // namespace patchy::pdf
