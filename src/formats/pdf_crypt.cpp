#include "formats/pdf_crypt.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace patchy::pdf {
namespace {

// --- MD5 (RFC 1321) ------------------------------------------------------------

constexpr std::array<std::uint32_t, 64> kMd5K = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};
constexpr std::array<int, 64> kMd5Shift = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20,
    5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

void md5_block(std::array<std::uint32_t, 4>& state, const std::uint8_t* block) {
  std::uint32_t words[16];
  for (int index = 0; index < 16; ++index) {
    words[index] = static_cast<std::uint32_t>(block[index * 4]) | (block[index * 4 + 1] << 8) |
                   (block[index * 4 + 2] << 16) | (static_cast<std::uint32_t>(block[index * 4 + 3]) << 24);
  }
  std::uint32_t a = state[0];
  std::uint32_t b = state[1];
  std::uint32_t c = state[2];
  std::uint32_t d = state[3];
  for (int round = 0; round < 64; ++round) {
    std::uint32_t mix = 0;
    int source = 0;
    if (round < 16) {
      mix = (b & c) | (~b & d);
      source = round;
    } else if (round < 32) {
      mix = (d & b) | (~d & c);
      source = (5 * round + 1) % 16;
    } else if (round < 48) {
      mix = b ^ c ^ d;
      source = (3 * round + 5) % 16;
    } else {
      mix = c ^ (b | ~d);
      source = (7 * round) % 16;
    }
    const std::uint32_t rotated = a + mix + kMd5K[static_cast<std::size_t>(round)] + words[source];
    a = d;
    d = c;
    c = b;
    b += std::rotl(rotated, kMd5Shift[static_cast<std::size_t>(round)]);
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

// --- SHA-2 (FIPS 180-4) --------------------------------------------------------

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

void sha256_block(std::array<std::uint32_t, 8>& state, const std::uint8_t* block) {
  std::uint32_t schedule[64];
  for (int index = 0; index < 16; ++index) {
    schedule[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24) | (block[index * 4 + 1] << 16) |
                      (block[index * 4 + 2] << 8) | block[index * 4 + 3];
  }
  for (int index = 16; index < 64; ++index) {
    const std::uint32_t s0 = std::rotr(schedule[index - 15], 7) ^ std::rotr(schedule[index - 15], 18) ^
                             (schedule[index - 15] >> 3);
    const std::uint32_t s1 = std::rotr(schedule[index - 2], 17) ^ std::rotr(schedule[index - 2], 19) ^
                             (schedule[index - 2] >> 10);
    schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
  }
  auto working = state;
  for (int round = 0; round < 64; ++round) {
    const std::uint32_t s1 = std::rotr(working[4], 6) ^ std::rotr(working[4], 11) ^ std::rotr(working[4], 25);
    const std::uint32_t choose = (working[4] & working[5]) ^ (~working[4] & working[6]);
    const std::uint32_t temp1 =
        working[7] + s1 + choose + kSha256K[static_cast<std::size_t>(round)] + schedule[round];
    const std::uint32_t s0 = std::rotr(working[0], 2) ^ std::rotr(working[0], 13) ^ std::rotr(working[0], 22);
    const std::uint32_t majority = (working[0] & working[1]) ^ (working[0] & working[2]) ^ (working[1] & working[2]);
    const std::uint32_t temp2 = s0 + majority;
    working[7] = working[6];
    working[6] = working[5];
    working[5] = working[4];
    working[4] = working[3] + temp1;
    working[3] = working[2];
    working[2] = working[1];
    working[1] = working[0];
    working[0] = temp1 + temp2;
  }
  for (int index = 0; index < 8; ++index) {
    state[static_cast<std::size_t>(index)] += working[static_cast<std::size_t>(index)];
  }
}

constexpr std::array<std::uint64_t, 80> kSha512K = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

void sha512_block(std::array<std::uint64_t, 8>& state, const std::uint8_t* block) {
  std::uint64_t schedule[80];
  for (int index = 0; index < 16; ++index) {
    std::uint64_t word = 0;
    for (int byte = 0; byte < 8; ++byte) {
      word = (word << 8) | block[index * 8 + byte];
    }
    schedule[index] = word;
  }
  for (int index = 16; index < 80; ++index) {
    const std::uint64_t s0 = std::rotr(schedule[index - 15], 1) ^ std::rotr(schedule[index - 15], 8) ^
                             (schedule[index - 15] >> 7);
    const std::uint64_t s1 = std::rotr(schedule[index - 2], 19) ^ std::rotr(schedule[index - 2], 61) ^
                             (schedule[index - 2] >> 6);
    schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
  }
  auto working = state;
  for (int round = 0; round < 80; ++round) {
    const std::uint64_t s1 = std::rotr(working[4], 14) ^ std::rotr(working[4], 18) ^ std::rotr(working[4], 41);
    const std::uint64_t choose = (working[4] & working[5]) ^ (~working[4] & working[6]);
    const std::uint64_t temp1 =
        working[7] + s1 + choose + kSha512K[static_cast<std::size_t>(round)] + schedule[round];
    const std::uint64_t s0 = std::rotr(working[0], 28) ^ std::rotr(working[0], 34) ^ std::rotr(working[0], 39);
    const std::uint64_t majority = (working[0] & working[1]) ^ (working[0] & working[2]) ^ (working[1] & working[2]);
    const std::uint64_t temp2 = s0 + majority;
    working[7] = working[6];
    working[6] = working[5];
    working[5] = working[4];
    working[4] = working[3] + temp1;
    working[3] = working[2];
    working[2] = working[1];
    working[1] = working[0];
    working[0] = temp1 + temp2;
  }
  for (int index = 0; index < 8; ++index) {
    state[static_cast<std::size_t>(index)] += working[static_cast<std::size_t>(index)];
  }
}

// Generic Merkle-Damgard driver: pads, runs blocks, serializes the state.
template <typename State, std::size_t BlockSize, typename BlockFn>
std::vector<std::uint8_t> run_hash(State state, std::span<const std::uint8_t> data, BlockFn block_fn,
                                   bool little_endian_length, std::size_t word_bytes) {
  std::vector<std::uint8_t> padded(data.begin(), data.end());
  padded.push_back(0x80);
  const std::size_t length_bytes = BlockSize == 128 ? 16 : 8;
  while (padded.size() % BlockSize != BlockSize - length_bytes) {
    padded.push_back(0);
  }
  const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8;
  if (little_endian_length) {
    for (int byte = 0; byte < 8; ++byte) {
      padded.push_back(static_cast<std::uint8_t>((bit_length >> (byte * 8)) & 0xFF));
    }
  } else {
    for (std::size_t byte = 0; byte < length_bytes; ++byte) {
      const int shift = static_cast<int>((length_bytes - 1 - byte) * 8);
      padded.push_back(shift < 64 ? static_cast<std::uint8_t>((bit_length >> shift) & 0xFF) : 0);
    }
  }
  for (std::size_t offset = 0; offset < padded.size(); offset += BlockSize) {
    block_fn(state, padded.data() + offset);
  }
  std::vector<std::uint8_t> digest;
  for (const auto word : state) {
    for (std::size_t byte = 0; byte < word_bytes; ++byte) {
      const int shift = little_endian_length ? static_cast<int>(byte * 8)
                                             : static_cast<int>((word_bytes - 1 - byte) * 8);
      digest.push_back(static_cast<std::uint8_t>((word >> shift) & 0xFF));
    }
  }
  return digest;
}

// --- AES decryption (FIPS 197) -------------------------------------------------

constexpr std::array<std::uint8_t, 256> kSbox = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

std::array<std::uint8_t, 256> make_inverse_sbox() {
  std::array<std::uint8_t, 256> inverse{};
  for (int index = 0; index < 256; ++index) {
    inverse[kSbox[static_cast<std::size_t>(index)]] = static_cast<std::uint8_t>(index);
  }
  return inverse;
}

std::uint8_t gf_multiply(std::uint8_t a, std::uint8_t b) {
  std::uint8_t product = 0;
  while (b != 0) {
    if ((b & 1) != 0) {
      product ^= a;
    }
    const bool high = (a & 0x80) != 0;
    a = static_cast<std::uint8_t>(a << 1);
    if (high) {
      a ^= 0x1B;
    }
    b >>= 1;
  }
  return product;
}

struct AesSchedule {
  // Up to 15 round keys of 16 bytes (AES-256 has 14 rounds + initial).
  std::array<std::uint8_t, 240> round_keys{};
  int rounds{0};
};

std::optional<AesSchedule> aes_key_schedule(std::span<const std::uint8_t> key) {
  const std::size_t key_words = key.size() / 4;
  if (key.size() != 16 && key.size() != 24 && key.size() != 32) {
    return std::nullopt;
  }
  AesSchedule schedule;
  schedule.rounds = static_cast<int>(key_words) + 6;
  const std::size_t total_words = 4u * (static_cast<std::size_t>(schedule.rounds) + 1);
  std::memcpy(schedule.round_keys.data(), key.data(), key.size());
  std::uint8_t rcon = 1;
  for (std::size_t word = key_words; word < total_words; ++word) {
    std::uint8_t temp[4];
    std::memcpy(temp, schedule.round_keys.data() + (word - 1) * 4, 4);
    if (word % key_words == 0) {
      const std::uint8_t first = temp[0];
      temp[0] = static_cast<std::uint8_t>(kSbox[temp[1]] ^ rcon);
      temp[1] = kSbox[temp[2]];
      temp[2] = kSbox[temp[3]];
      temp[3] = kSbox[first];
      rcon = gf_multiply(rcon, 2);
    } else if (key_words > 6 && word % key_words == 4) {
      for (auto& byte : temp) {
        byte = kSbox[byte];
      }
    }
    for (int byte = 0; byte < 4; ++byte) {
      schedule.round_keys[word * 4 + static_cast<std::size_t>(byte)] =
          schedule.round_keys[(word - key_words) * 4 + static_cast<std::size_t>(byte)] ^ temp[byte];
    }
  }
  return schedule;
}

void aes_decrypt_block(const AesSchedule& schedule, std::uint8_t* block) {
  static const auto kInverseSbox = make_inverse_sbox();
  const auto add_round_key = [&](int round) {
    for (int byte = 0; byte < 16; ++byte) {
      block[byte] ^= schedule.round_keys[static_cast<std::size_t>(round) * 16 + static_cast<std::size_t>(byte)];
    }
  };
  const auto inverse_shift_rows = [&] {
    std::uint8_t copy[16];
    std::memcpy(copy, block, 16);
    for (int column = 0; column < 4; ++column) {
      for (int row = 0; row < 4; ++row) {
        block[((column + row) % 4) * 4 + row] = copy[column * 4 + row];
      }
    }
  };
  const auto inverse_sub_bytes = [&] {
    for (int byte = 0; byte < 16; ++byte) {
      block[byte] = kInverseSbox[block[byte]];
    }
  };
  const auto inverse_mix_columns = [&] {
    for (int column = 0; column < 4; ++column) {
      std::uint8_t* base = block + column * 4;
      const std::uint8_t a = base[0];
      const std::uint8_t b = base[1];
      const std::uint8_t c = base[2];
      const std::uint8_t d = base[3];
      base[0] = gf_multiply(a, 14) ^ gf_multiply(b, 11) ^ gf_multiply(c, 13) ^ gf_multiply(d, 9);
      base[1] = gf_multiply(a, 9) ^ gf_multiply(b, 14) ^ gf_multiply(c, 11) ^ gf_multiply(d, 13);
      base[2] = gf_multiply(a, 13) ^ gf_multiply(b, 9) ^ gf_multiply(c, 14) ^ gf_multiply(d, 11);
      base[3] = gf_multiply(a, 11) ^ gf_multiply(b, 13) ^ gf_multiply(c, 9) ^ gf_multiply(d, 14);
    }
  };

  add_round_key(schedule.rounds);
  for (int round = schedule.rounds - 1; round > 0; --round) {
    inverse_shift_rows();
    inverse_sub_bytes();
    add_round_key(round);
    inverse_mix_columns();
  }
  inverse_shift_rows();
  inverse_sub_bytes();
  add_round_key(0);
}

// AES-CBC ENCRYPTION of exactly one whole-block message, needed only by the
// revision 6 key derivation (algorithm 2.B encrypts with the evolving key).
std::vector<std::uint8_t> aes_cbc_encrypt_no_pad(std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv,
                                                 std::span<const std::uint8_t> data) {
  const auto schedule = aes_key_schedule(key);
  if (!schedule.has_value() || data.size() % 16 != 0 || iv.size() != 16) {
    return {};
  }
  static const auto encrypt_block = [](const AesSchedule& keys, std::uint8_t* block) {
    const auto add_round_key = [&](int round) {
      for (int byte = 0; byte < 16; ++byte) {
        block[byte] ^= keys.round_keys[static_cast<std::size_t>(round) * 16 + static_cast<std::size_t>(byte)];
      }
    };
    const auto sub_bytes = [&] {
      for (int byte = 0; byte < 16; ++byte) {
        block[byte] = kSbox[block[byte]];
      }
    };
    const auto shift_rows = [&] {
      std::uint8_t copy[16];
      std::memcpy(copy, block, 16);
      for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
          block[column * 4 + row] = copy[((column + row) % 4) * 4 + row];
        }
      }
    };
    const auto mix_columns = [&] {
      for (int column = 0; column < 4; ++column) {
        std::uint8_t* base = block + column * 4;
        const std::uint8_t a = base[0];
        const std::uint8_t b = base[1];
        const std::uint8_t c = base[2];
        const std::uint8_t d = base[3];
        base[0] = gf_multiply(a, 2) ^ gf_multiply(b, 3) ^ c ^ d;
        base[1] = a ^ gf_multiply(b, 2) ^ gf_multiply(c, 3) ^ d;
        base[2] = a ^ b ^ gf_multiply(c, 2) ^ gf_multiply(d, 3);
        base[3] = gf_multiply(a, 3) ^ b ^ c ^ gf_multiply(d, 2);
      }
    };
    add_round_key(0);
    for (int round = 1; round < keys.rounds; ++round) {
      sub_bytes();
      shift_rows();
      mix_columns();
      add_round_key(round);
    }
    sub_bytes();
    shift_rows();
    add_round_key(keys.rounds);
  };

  std::vector<std::uint8_t> out(data.begin(), data.end());
  std::uint8_t chain[16];
  std::memcpy(chain, iv.data(), 16);
  for (std::size_t offset = 0; offset < out.size(); offset += 16) {
    for (int byte = 0; byte < 16; ++byte) {
      out[offset + static_cast<std::size_t>(byte)] ^= chain[byte];
    }
    encrypt_block(*schedule, out.data() + offset);
    std::memcpy(chain, out.data() + offset, 16);
  }
  return out;
}

// The fixed 32-byte password pad of algorithm 2 (clause 7.6.4.3.2).
constexpr std::array<std::uint8_t, 32> kPasswordPad = {
    0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
    0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A,
};

std::vector<std::uint8_t> pad_password(std::string_view password) {
  std::vector<std::uint8_t> padded(password.begin(),
                                   password.begin() + std::min<std::size_t>(password.size(), 32));
  padded.insert(padded.end(), kPasswordPad.begin(), kPasswordPad.begin() + (32 - padded.size()));
  return padded;
}

void append_bytes(std::vector<std::uint8_t>& out, std::string_view text) {
  out.insert(out.end(), text.begin(), text.end());
}

void append_le32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (int byte = 0; byte < 4; ++byte) {
    out.push_back(static_cast<std::uint8_t>((value >> (byte * 8)) & 0xFF));
  }
}

// Algorithm 2: the revision 2-4 file key.
std::vector<std::uint8_t> legacy_file_key(const Decryptor::Inputs& inputs, std::span<const std::uint8_t> padded) {
  std::vector<std::uint8_t> material(padded.begin(), padded.end());
  append_bytes(material, inputs.owner_hash);
  append_le32(material, static_cast<std::uint32_t>(inputs.permissions));
  append_bytes(material, inputs.first_file_id);
  if (inputs.revision >= 4 && !inputs.encrypt_metadata) {
    append_le32(material, 0xFFFFFFFFu);
  }
  auto digest_array = md5(material);
  std::vector<std::uint8_t> digest(digest_array.begin(), digest_array.end());
  const std::size_t key_length = std::clamp(inputs.length_bits / 8, 5, 16);
  if (inputs.revision >= 3) {
    for (int iteration = 0; iteration < 50; ++iteration) {
      const auto again = md5(std::span(digest.data(), key_length));
      std::copy(again.begin(), again.end(), digest.begin());
    }
  }
  digest.resize(key_length);
  return digest;
}

// Algorithm 2.B: the revision 6 iterated hash.
std::vector<std::uint8_t> revision6_hash(std::string_view password, std::span<const std::uint8_t> salt,
                                         std::span<const std::uint8_t> user_hash_extra) {
  std::vector<std::uint8_t> input;
  append_bytes(input, password);
  input.insert(input.end(), salt.begin(), salt.end());
  input.insert(input.end(), user_hash_extra.begin(), user_hash_extra.end());
  auto k_array = sha256(input);
  std::vector<std::uint8_t> k(k_array.begin(), k_array.end());

  for (int round = 0;; ++round) {
    std::vector<std::uint8_t> k1;
    for (int repeat = 0; repeat < 64; ++repeat) {
      append_bytes(k1, password);
      k1.insert(k1.end(), k.begin(), k.end());
      k1.insert(k1.end(), user_hash_extra.begin(), user_hash_extra.end());
    }
    const auto e = aes_cbc_encrypt_no_pad(std::span(k.data(), 16), std::span(k.data() + 16, 16), k1);
    if (e.empty()) {
      return {};
    }
    int modulo = 0;
    for (int byte = 0; byte < 16; ++byte) {
      modulo += e[static_cast<std::size_t>(byte)];
    }
    modulo %= 3;
    if (modulo == 0) {
      const auto digest = sha256(e);
      k.assign(digest.begin(), digest.end());
    } else if (modulo == 1) {
      const auto digest = sha384(e);
      k.assign(digest.begin(), digest.end());
    } else {
      const auto digest = sha512(e);
      k.assign(digest.begin(), digest.end());
    }
    // At least 64 rounds; stop when the last byte of E is at most round - 32.
    if (round >= 63 && e.back() <= static_cast<std::uint8_t>(round - 31)) {
      break;
    }
    if (round > 1000) {
      return {};  // a damaged file must not spin forever
    }
  }
  k.resize(32);
  return k;
}

}  // namespace

std::array<std::uint8_t, 16> md5(std::span<const std::uint8_t> data) {
  std::array<std::uint32_t, 4> state = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  const auto digest = run_hash<decltype(state), 64>(state, data, md5_block, true, 4);
  std::array<std::uint8_t, 16> out{};
  std::copy_n(digest.begin(), 16, out.begin());
  return out;
}

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data) {
  std::array<std::uint32_t, 8> state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const auto digest = run_hash<decltype(state), 64>(state, data, sha256_block, false, 4);
  std::array<std::uint8_t, 32> out{};
  std::copy_n(digest.begin(), 32, out.begin());
  return out;
}

std::array<std::uint8_t, 48> sha384(std::span<const std::uint8_t> data) {
  std::array<std::uint64_t, 8> state = {0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL,
                                        0x152fecd8f70e5939ULL, 0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL,
                                        0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL};
  const auto digest = run_hash<decltype(state), 128>(state, data, sha512_block, false, 8);
  std::array<std::uint8_t, 48> out{};
  std::copy_n(digest.begin(), 48, out.begin());
  return out;
}

std::array<std::uint8_t, 64> sha512(std::span<const std::uint8_t> data) {
  std::array<std::uint64_t, 8> state = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
                                        0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
                                        0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
  const auto digest = run_hash<decltype(state), 128>(state, data, sha512_block, false, 8);
  std::array<std::uint8_t, 64> out{};
  std::copy_n(digest.begin(), 64, out.begin());
  return out;
}

std::vector<std::uint8_t> rc4(std::span<const std::uint8_t> key, std::span<const std::uint8_t> data) {
  std::array<std::uint8_t, 256> state;
  for (int index = 0; index < 256; ++index) {
    state[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(index);
  }
  if (!key.empty()) {
    int j = 0;
    for (int i = 0; i < 256; ++i) {
      j = (j + state[static_cast<std::size_t>(i)] + key[static_cast<std::size_t>(i) % key.size()]) & 0xFF;
      std::swap(state[static_cast<std::size_t>(i)], state[static_cast<std::size_t>(j)]);
    }
  }
  std::vector<std::uint8_t> out;
  out.reserve(data.size());
  int i = 0;
  int j = 0;
  for (const auto byte : data) {
    i = (i + 1) & 0xFF;
    j = (j + state[static_cast<std::size_t>(i)]) & 0xFF;
    std::swap(state[static_cast<std::size_t>(i)], state[static_cast<std::size_t>(j)]);
    out.push_back(byte ^ state[static_cast<std::size_t>(
                             (state[static_cast<std::size_t>(i)] + state[static_cast<std::size_t>(j)]) & 0xFF)]);
  }
  return out;
}

std::vector<std::uint8_t> aes_cbc_decrypt(std::span<const std::uint8_t> key, std::span<const std::uint8_t> data,
                                          bool iv_in_data, bool strip_padding) {
  const auto schedule = aes_key_schedule(key);
  if (!schedule.has_value()) {
    return {};
  }
  std::uint8_t chain[16] = {};
  std::span<const std::uint8_t> body = data;
  if (iv_in_data) {
    if (data.size() < 16) {
      return {};
    }
    std::memcpy(chain, data.data(), 16);
    body = data.subspan(16);
  }
  if (body.empty() || body.size() % 16 != 0) {
    return {};
  }
  std::vector<std::uint8_t> out(body.begin(), body.end());
  std::uint8_t previous[16];
  for (std::size_t offset = 0; offset < out.size(); offset += 16) {
    std::memcpy(previous, out.data() + offset, 16);
    aes_decrypt_block(*schedule, out.data() + offset);
    for (int byte = 0; byte < 16; ++byte) {
      out[offset + static_cast<std::size_t>(byte)] ^= chain[byte];
    }
    std::memcpy(chain, previous, 16);
  }
  if (strip_padding && !out.empty()) {
    const std::uint8_t pad = out.back();
    if (pad < 1 || pad > 16 || pad > out.size()) {
      return {};
    }
    for (std::size_t index = out.size() - pad; index < out.size(); ++index) {
      if (out[index] != pad) {
        return {};
      }
    }
    out.resize(out.size() - pad);
  }
  return out;
}

std::optional<Decryptor> Decryptor::create(const Inputs& inputs, std::string_view password) {
  const auto method_from_name = [](const std::string& name) -> std::optional<Method> {
    if (name == "Identity" || name == "None") {
      return Method::Identity;
    }
    if (name == "V2") {
      return Method::Rc4;
    }
    if (name == "AESV2" || name == "AESV3") {
      return Method::Aes;
    }
    return std::nullopt;
  };

  Decryptor decryptor;
  decryptor.revision_ = inputs.revision;
  if (inputs.v <= 3) {
    // V1/V2 files have no crypt filters; everything is RC4.
    decryptor.stream_method_ = Method::Rc4;
    decryptor.string_method_ = Method::Rc4;
  } else {
    const auto stream_method = method_from_name(inputs.stream_method);
    const auto string_method = method_from_name(inputs.string_method);
    if (!stream_method.has_value() || !string_method.has_value()) {
      return std::nullopt;
    }
    if ((inputs.revision <= 4 && (inputs.stream_method == "AESV3" || inputs.string_method == "AESV3")) ||
        (inputs.revision >= 5 &&
         ((inputs.stream_method != "Identity" && inputs.stream_method != "None" && inputs.stream_method != "AESV3") ||
          (inputs.string_method != "Identity" && inputs.string_method != "None" && inputs.string_method != "AESV3")))) {
      return std::nullopt;
    }
    decryptor.stream_method_ = *stream_method;
    decryptor.string_method_ = *string_method;
  }

  if (inputs.revision >= 2 && inputs.revision <= 4) {
    const std::size_t required_user_hash = inputs.revision == 2 ? 32 : 16;
    if (inputs.owner_hash.size() < 32 || inputs.user_hash.size() < required_user_hash) {
      return std::nullopt;
    }
    // Try the password as the USER password (algorithm 4/5 check against /U).
    const auto try_user = [&](std::span<const std::uint8_t> padded) -> bool {
      decryptor.file_key_ = legacy_file_key(inputs, padded);
      if (inputs.revision == 2) {
        const auto expected = rc4(decryptor.file_key_,
                                  std::span(kPasswordPad.data(), kPasswordPad.size()));
        return std::memcmp(expected.data(), inputs.user_hash.data(), 32) == 0;
      }
      // Revision 3/4: MD5(pad + file id), RC4 through 20 derived keys; compare 16.
      std::vector<std::uint8_t> seed(kPasswordPad.begin(), kPasswordPad.end());
      append_bytes(seed, inputs.first_file_id);
      auto digest_array = md5(seed);
      std::vector<std::uint8_t> value(digest_array.begin(), digest_array.end());
      for (int iteration = 0; iteration <= 19; ++iteration) {
        std::vector<std::uint8_t> round_key = decryptor.file_key_;
        for (auto& byte : round_key) {
          byte = static_cast<std::uint8_t>(byte ^ iteration);
        }
        value = rc4(round_key, value);
      }
      return std::memcmp(value.data(), inputs.user_hash.data(), 16) == 0;
    };

    const auto padded_user = pad_password(password);
    if (try_user(padded_user)) {
      return decryptor;
    }
    // Try it as the OWNER password (algorithm 7): decrypt /O into the user
    // password, then run the user check with that.
    {
      const auto padded_owner = pad_password(password);
      auto digest = md5(padded_owner);
      std::vector<std::uint8_t> owner_key(digest.begin(), digest.end());
      const std::size_t key_length = std::clamp(inputs.length_bits / 8, 5, 16);
      if (inputs.revision >= 3) {
        for (int iteration = 0; iteration < 50; ++iteration) {
          digest = md5(std::span(owner_key.data(), key_length));
          owner_key.assign(digest.begin(), digest.end());
        }
      }
      owner_key.resize(key_length);
      std::vector<std::uint8_t> user_password(inputs.owner_hash.begin(), inputs.owner_hash.begin() + 32);
      if (inputs.revision == 2) {
        user_password = rc4(owner_key, user_password);
      } else {
        for (int iteration = 19; iteration >= 0; --iteration) {
          std::vector<std::uint8_t> round_key = owner_key;
          for (auto& byte : round_key) {
            byte = static_cast<std::uint8_t>(byte ^ iteration);
          }
          user_password = rc4(round_key, user_password);
        }
      }
      if (try_user(user_password)) {
        return decryptor;
      }
    }
    return std::nullopt;
  }

  if (inputs.revision == 5 || inputs.revision == 6) {
    // AES-256. /U and /O are 48 bytes: 32 hash + 8 validation salt + 8 key salt.
    if (inputs.user_hash.size() < 48) {
      return std::nullopt;
    }
    const auto user = std::span(reinterpret_cast<const std::uint8_t*>(inputs.user_hash.data()), 48);
    password = password.substr(0, std::min<std::size_t>(password.size(), 127));
    const auto hash_with = [&](std::span<const std::uint8_t> salt, std::span<const std::uint8_t> extra) {
      // Revision 5 (the deprecated Adobe interim scheme) is a single SHA-256;
      // revision 6 runs the hardened iterated hash.
      if (inputs.revision == 5) {
        std::vector<std::uint8_t> input;
        append_bytes(input, password);
        input.insert(input.end(), salt.begin(), salt.end());
        input.insert(input.end(), extra.begin(), extra.end());
        const auto digest = sha256(input);
        return std::vector<std::uint8_t>(digest.begin(), digest.end());
      }
      return revision6_hash(password, salt, extra);
    };

    // As the user password: hash(password + validation salt) == /U[0..32].
    const auto user_check = hash_with(user.subspan(32, 8), {});
    if (!user_check.empty() && std::memcmp(user_check.data(), user.data(), 32) == 0) {
      const auto intermediate = hash_with(user.subspan(40, 8), {});
      if (inputs.user_key.size() >= 32) {
        const auto key_bytes = std::span(reinterpret_cast<const std::uint8_t*>(inputs.user_key.data()), 32);
        decryptor.file_key_ = aes_cbc_decrypt(intermediate, key_bytes, false, false);
        if (decryptor.file_key_.size() == 32) {
          return decryptor;
        }
      }
    }
    // As the owner password: the hash also covers the full 48-byte /U.
    if (inputs.owner_hash.size() >= 48 && inputs.owner_key.size() >= 32) {
      const auto owner = std::span(reinterpret_cast<const std::uint8_t*>(inputs.owner_hash.data()), 48);
      const auto owner_check = hash_with(owner.subspan(32, 8), user);
      if (!owner_check.empty() && std::memcmp(owner_check.data(), owner.data(), 32) == 0) {
        const auto intermediate = hash_with(owner.subspan(40, 8), user);
        const auto key_bytes = std::span(reinterpret_cast<const std::uint8_t*>(inputs.owner_key.data()), 32);
        decryptor.file_key_ = aes_cbc_decrypt(intermediate, key_bytes, false, false);
        if (decryptor.file_key_.size() == 32) {
          return decryptor;
        }
      }
    }
    return std::nullopt;
  }

  return std::nullopt;
}

std::vector<std::uint8_t> Decryptor::object_key(std::uint32_t object_number, std::uint16_t generation,
                                                Method method) const {
  if (revision_ >= 5) {
    return file_key_;  // AES-256 uses the file key directly
  }
  std::vector<std::uint8_t> material = file_key_;
  material.push_back(static_cast<std::uint8_t>(object_number & 0xFF));
  material.push_back(static_cast<std::uint8_t>((object_number >> 8) & 0xFF));
  material.push_back(static_cast<std::uint8_t>((object_number >> 16) & 0xFF));
  material.push_back(static_cast<std::uint8_t>(generation & 0xFF));
  material.push_back(static_cast<std::uint8_t>((generation >> 8) & 0xFF));
  if (method == Method::Aes) {
    // The AESV2 "sAlT" extension bytes (clause 7.6.3.1).
    material.push_back(0x73);
    material.push_back(0x41);
    material.push_back(0x6C);
    material.push_back(0x54);
  }
  const auto digest = md5(material);
  std::vector<std::uint8_t> key(digest.begin(), digest.end());
  key.resize(std::min<std::size_t>(file_key_.size() + 5, 16));
  return key;
}

std::vector<std::uint8_t> Decryptor::apply(Method method, std::span<const std::uint8_t> key,
                                           std::span<const std::uint8_t> data) const {
  switch (method) {
    case Method::Identity: return {data.begin(), data.end()};
    case Method::Rc4: return rc4(key, data);
    case Method::Aes: return aes_cbc_decrypt(key, data);
  }
  return {};
}

std::vector<std::uint8_t> Decryptor::decrypt_stream(std::uint32_t object_number, std::uint16_t generation,
                                                    std::span<const std::uint8_t> data) const {
  return apply(stream_method_, object_key(object_number, generation, stream_method_), data);
}

std::string Decryptor::decrypt_string(std::uint32_t object_number, std::uint16_t generation,
                                      std::string_view data) const {
  const auto bytes = apply(string_method_,
                           object_key(object_number, generation, string_method_),
                           std::span(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
  return std::string(bytes.begin(), bytes.end());
}

}  // namespace patchy::pdf
