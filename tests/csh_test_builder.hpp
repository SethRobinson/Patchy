#pragma once

// Builds small Photoshop .csh custom-shape files for the reader tests (core and
// UI suites), following the layout recorded in docs/vector-tools.md
// ("Photoshop .csh files"). Self-authored geometry only.

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace patchy::test {

struct CshTestContour {
  // Corner points as fractions of the shape's bounds box (x, y).
  std::vector<std::array<double, 2>> points;
  // Combine op of a group's lead contour; a continuation contour of the
  // same group (Photoshop's compound encoding, filled even-odd) writes 0xFFFF.
  std::uint16_t op{1};
  bool continuation{false};
  std::uint32_t group{0};
};

struct CshTestShape {
  std::u16string name;
  std::string id{"00000000-0000-0000-0000-000000000000"};
  std::int32_t top{0};
  std::int32_t left{0};
  std::int32_t bottom{100};
  std::int32_t right{100};
  std::vector<CshTestContour> contours;
};

namespace csh_detail {
inline void put_u16(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}
inline void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  put_u16(out, value >> 16U);
  put_u16(out, value & 0xFFFFU);
}
inline void put_fixed(std::vector<std::uint8_t>& out, double fraction) {
  put_u32(out, static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(fraction * 16777216.0))));
}
inline void pad_record(std::vector<std::uint8_t>& out, std::size_t start) {
  while (out.size() < start + 26U) out.push_back(0);
}
inline void pad4(std::vector<std::uint8_t>& out) {
  while (out.size() % 4U != 0U) out.push_back(0);
}
}  // namespace csh_detail

inline std::vector<std::uint8_t> make_test_csh(const std::vector<CshTestShape>& shapes, std::uint32_t version = 2) {
  using namespace csh_detail;
  std::vector<std::uint8_t> out{'c', 'u', 's', 'h'};
  put_u32(out, version);
  put_u32(out, static_cast<std::uint32_t>(shapes.size()));
  for (const auto& shape : shapes) {
    put_u32(out, static_cast<std::uint32_t>(shape.name.size() + 1U));
    for (const auto unit : shape.name) put_u16(out, unit);
    put_u16(out, 0);
    pad4(out);
    put_u32(out, 1);
    std::vector<std::uint8_t> block;
    block.push_back(static_cast<std::uint8_t>(shape.id.size()));
    block.insert(block.end(), shape.id.begin(), shape.id.end());
    put_u32(block, static_cast<std::uint32_t>(shape.top));
    put_u32(block, static_cast<std::uint32_t>(shape.left));
    put_u32(block, static_cast<std::uint32_t>(shape.bottom));
    put_u32(block, static_cast<std::uint32_t>(shape.right));
    auto start = block.size();
    put_u16(block, 6);  // fill rule record
    pad_record(block, start);
    start = block.size();
    put_u16(block, 8);  // initial fill record
    pad_record(block, start);
    for (const auto& contour : shape.contours) {
      start = block.size();
      put_u16(block, 0);  // closed subpath length record
      put_u16(block, static_cast<std::uint32_t>(contour.points.size()));
      put_u16(block, contour.continuation ? 0xFFFFU : contour.op);
      put_u16(block, contour.continuation ? 0U : 1U);
      put_u16(block, 0);
      put_u16(block, 0);
      put_u32(block, contour.group);
      pad_record(block, start);
      for (const auto& point : contour.points) {
        start = block.size();
        put_u16(block, 2);  // closed subpath, unlinked knot
        for (int i = 0; i < 3; ++i) {
          put_fixed(block, point[1]);
          put_fixed(block, point[0]);
        }
        pad_record(block, start);
      }
    }
    // The block runs to the next 4-byte file offset.
    const auto block_start = out.size() + 4U;
    while ((block_start + block.size()) % 4U != 0U) block.push_back(0);
    put_u32(out, static_cast<std::uint32_t>(block.size()));
    out.insert(out.end(), block.begin(), block.end());
  }
  return out;
}

// A 200 x 100 frame (outer rectangle with a rectangular hole, one compound
// group) and a 100 x 100 triangle; the frame's name carries an astral-plane
// character (U+1F3A8) to exercise surrogate pairs.
inline std::vector<CshTestShape> sample_test_csh_shapes() {
  CshTestShape frame;
  frame.name = u"Frame \U0001F3A8";
  frame.top = 10;
  frame.left = 20;
  frame.bottom = 110;
  frame.right = 220;
  frame.contours.push_back({{{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}}, 1, false, 0});
  frame.contours.push_back({{{0.25, 0.25}, {0.75, 0.25}, {0.75, 0.75}, {0.25, 0.75}}, 1, true, 0});
  CshTestShape triangle;
  triangle.name = u"Triangle";
  triangle.id = "8a1d4c52-0d4e-4f6b-9a1e-1b2c3d4e5f60";
  triangle.contours.push_back({{{0.0, 0.0}, {1.0, 0.25}, {0.25, 1.0}}, 1, false, 0});
  return {frame, triangle};
}

}  // namespace patchy::test
