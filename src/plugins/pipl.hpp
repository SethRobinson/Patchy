#pragma once

// Reader for the plug-in property list ("PiPL") that classic Photoshop plug-ins
// carry as a Windows resource. It walks the PE file on disk (DOS header, COFF
// header, section table, the resource directory) and parses the property list
// out of the raw bytes, so scanning a plug-in folder never loads or executes a
// plug-in. Patchy's own declarations, written from the public plug-in
// documentation; see docs/plugins.md and docs/legal-constraints.md.

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace patchy::pipl {

// A four-character code as the plug-in ABI sees it: '8BIM' is 0x3842494D. In a
// Windows PiPL resource the four bytes are stored least-significant first
// ("MIB8"), which is what read_fourcc_le decodes.
[[nodiscard]] constexpr std::uint32_t fourcc(char a, char b, char c, char d) noexcept {
  return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16U) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8U) |
         static_cast<std::uint32_t>(static_cast<unsigned char>(d));
}

// Plug-in kinds ('kind' property).
inline constexpr std::uint32_t kKindFilter = fourcc('8', 'B', 'F', 'M');
inline constexpr std::uint32_t kKindFormat = fourcc('8', 'B', 'I', 'F');
inline constexpr std::uint32_t kKindExtension = fourcc('8', 'B', 'X', 'M');
inline constexpr std::uint32_t kKindAutomation = fourcc('8', 'L', 'I', 'Z');

// One entry of the FilterCaseInfo property ('fici'): how the plug-in wants each
// of the seven filter cases handled. input_handling 0 means the plug-in cannot
// filter that case at all.
struct FilterCaseInfo {
  std::uint8_t input_handling{0};
  std::uint8_t output_handling{0};
  std::uint8_t flags1{0};
  std::uint8_t flags2{0};
};

// Data-handling values used by FilterCaseInfo.
inline constexpr std::uint8_t kDataHandlingCantFilter = 0;
inline constexpr std::uint8_t kDataHandlingNone = 1;
inline constexpr std::uint8_t kDataHandlingBlackMat = 2;
inline constexpr std::uint8_t kDataHandlingGrayMat = 3;
inline constexpr std::uint8_t kDataHandlingWhiteMat = 4;
inline constexpr std::uint8_t kDataHandlingDefringe = 5;
inline constexpr std::uint8_t kDataHandlingBlackZap = 6;
inline constexpr std::uint8_t kDataHandlingGrayZap = 7;
inline constexpr std::uint8_t kDataHandlingWhiteZap = 8;
inline constexpr std::uint8_t kDataHandlingFillMask = 9;
inline constexpr std::uint8_t kDataHandlingBackgroundZap = 10;
inline constexpr std::uint8_t kDataHandlingForegroundZap = 11;

// The seven filter cases, numbered as the plug-in ABI numbers them.
inline constexpr int kFilterCaseFlatImageNoSelection = 1;
inline constexpr int kFilterCaseFlatImageWithSelection = 2;
inline constexpr int kFilterCaseFloatingSelection = 3;
inline constexpr int kFilterCaseEditableTransparencyNoSelection = 4;
inline constexpr int kFilterCaseEditableTransparencyWithSelection = 5;
inline constexpr int kFilterCaseProtectedTransparencyNoSelection = 6;
inline constexpr int kFilterCaseProtectedTransparencyWithSelection = 7;

struct PiplInfo {
  bool found{false};
  std::uint32_t kind{0};             // 'kind' property, one of the kKind* codes
  std::string name;                  // 'name' (Pascal string), UTF-8
  std::string category;              // 'catg' (Pascal string), UTF-8
  std::string entry_point_32;        // 'wx86': exported entry point of a 32-bit plug-in
  std::string entry_point_64;        // '8664': exported entry point of a 64-bit plug-in
  std::uint32_t version{0};          // 'vers'
  std::uint32_t supported_modes{0};  // 'mode' flags
  bool has_case_info{false};
  std::array<FilterCaseInfo, 7> case_info{};

  [[nodiscard]] bool is_filter() const noexcept { return kind == kKindFilter; }
  // The case info entry for a 1-based filter case; a plug-in without the
  // property is treated as filtering only the flat-image cases.
  [[nodiscard]] FilterCaseInfo case_info_for(int filter_case) const noexcept;
  [[nodiscard]] bool supports_filter_case(int filter_case) const noexcept;
};

// Parses one PIPL resource blob (the bytes of the resource, Windows layout:
// int16 reserved, int32 version, int32 count, then the properties).
[[nodiscard]] PiplInfo parse_pipl_blob(std::span<const std::uint8_t> blob);

// Locates the PIPL resources of a Windows PE image held in memory and parses
// the first one that describes a filter (or the first one at all when none is a
// filter). found is false when the image has no PIPL resource or is not a PE.
[[nodiscard]] PiplInfo read_pipl_from_pe_bytes(std::span<const std::uint8_t> image);

// Reads the file and calls read_pipl_from_pe_bytes. Pure file I/O; the plug-in
// is never loaded.
[[nodiscard]] PiplInfo read_pipl_from_pe_file(const std::filesystem::path& path);

// Builds a PIPL blob from properties (test helper and documentation of the
// layout). Each property is {key, data}; the vendor is '8BIM'.
struct PiplProperty {
  std::uint32_t key{0};
  std::vector<std::uint8_t> data;
};
[[nodiscard]] std::vector<std::uint8_t> build_pipl_blob(const std::vector<PiplProperty>& properties);

}  // namespace patchy::pipl
