#include "support/random_uuid.hpp"

#include <mutex>
#include <random>

namespace patchy {

std::string generate_random_uuid(RandomUuidShape shape) {
  // One engine behind one mutex: a function-local static engine per caller
  // raced when two imports ran at once. Randomness here never feeds pixel
  // output, so the cross-toolchain determinism rule for render code does not
  // apply.
  static std::mutex mutex;
  static std::mt19937_64 engine{std::random_device{}()};
  const auto hex_digits = "0123456789abcdef";
  std::string uuid;
  uuid.reserve(36);
  const std::scoped_lock lock(mutex);
  for (const int group : {8, 4, 4, 4, 12}) {
    if (!uuid.empty()) {
      uuid.push_back('-');
    }
    for (int i = 0; i < group; ++i) {
      uuid.push_back(hex_digits[engine() & 0xFU]);
    }
  }
  if (shape == RandomUuidShape::Rfc4122Variant) {
    uuid[19] = hex_digits[8U + (engine() & 0x3U)];
  }
  return uuid;
}

}  // namespace patchy
