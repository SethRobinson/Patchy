// Legacy Photoshop plug-in support (docs/plugins.md): the PiPL property-list
// reader (a pure file read, no plug-in is loaded), the probe's names and
// bitness, and, on Windows, the out-of-process host executables driven through
// their --self-test mode over the committed Filter Foundry fixtures.

#include "plugins/legacy_photoshop_adapter.hpp"
#include "plugins/pipl.hpp"

#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using patchy::pipl::fourcc;

std::filesystem::path plugin_fixture(const char* name) {
  return patchy::test::source_root_path() / "test-fixtures" / "photoshop-plugins" / name;
}

std::vector<std::uint8_t> pascal_bytes(const std::string& text) {
  std::vector<std::uint8_t> out;
  out.push_back(static_cast<std::uint8_t>(text.size()));
  out.insert(out.end(), text.begin(), text.end());
  return out;
}

std::vector<std::uint8_t> c_string(const std::string& text) {
  std::vector<std::uint8_t> out(text.begin(), text.end());
  out.push_back(0);
  return out;
}

void pipl_blob_round_trips_properties() {
  std::vector<std::uint8_t> case_info;
  for (int c = 0; c < 7; ++c) {
    // Cases 1..5 filter plainly, 6 and 7 cannot be filtered.
    case_info.push_back(c < 5 ? 1 : 0);
    case_info.push_back(1);
    case_info.push_back(3);
    case_info.push_back(0);
  }
  const auto blob = patchy::pipl::build_pipl_blob({
      {fourcc('k', 'i', 'n', 'd'), {'M', 'F', 'B', '8'}},  // '8BFM' stored least-significant first
      {fourcc('n', 'a', 'm', 'e'), pascal_bytes("Test & Co")},   // odd length: exercises the padding
      {fourcc('c', 'a', 't', 'g'), pascal_bytes("Category")},
      {fourcc('8', '6', '6', '4'), c_string("PluginMain")},
      {fourcc('w', 'x', '8', '6'), c_string("Entry32")},
      {fourcc('v', 'e', 'r', 's'), {0, 0, 4, 0}},
      {fourcc('f', 'i', 'c', 'i'), case_info},
  });
  CHECK((blob.size() - 10) % 4 == 0);  // 10-byte header, then 4-byte padded properties
  const auto info = patchy::pipl::parse_pipl_blob(blob);
  CHECK(info.found);
  CHECK(info.is_filter());
  CHECK(info.name == "Test & Co");
  CHECK(info.category == "Category");
  CHECK(info.entry_point_64 == "PluginMain");
  CHECK(info.entry_point_32 == "Entry32");
  CHECK(info.version == 0x00040000U);
  CHECK(info.has_case_info);
  CHECK(info.supports_filter_case(patchy::pipl::kFilterCaseFlatImageNoSelection));
  CHECK(info.supports_filter_case(patchy::pipl::kFilterCaseEditableTransparencyWithSelection));
  CHECK(!info.supports_filter_case(patchy::pipl::kFilterCaseProtectedTransparencyNoSelection));
  CHECK(info.case_info_for(1).flags1 == 3);

  // Garbage in: nothing found, no crash.
  CHECK(!patchy::pipl::parse_pipl_blob({}).found);
  const std::vector<std::uint8_t> truncated(blob.begin(), blob.begin() + 20);
  (void)patchy::pipl::parse_pipl_blob(truncated);
  CHECK(!patchy::pipl::read_pipl_from_pe_bytes(blob).found);

  // Without the property, only the flat cases are offered.
  patchy::pipl::PiplInfo plain;
  CHECK(plain.supports_filter_case(patchy::pipl::kFilterCaseFlatImageWithSelection));
  CHECK(!plain.supports_filter_case(patchy::pipl::kFilterCaseEditableTransparencyNoSelection));
}

void pipl_reads_bundled_fixture_property_lists() {
  struct Expected {
    const char* file;
    const char* name;
    bool sixty_four;
  };
  const Expected expected[] = {
      {"Greyscale.8bf", "Greyscale", false},
      {"Greyscale64.8bf", "Greyscale", true},
      {"White to Transparent.8bf", "White to Transparent", false},
      {"White to Transparent64.8bf", "White to Transparent", true},
  };
  for (const auto& item : expected) {
    const auto path = plugin_fixture(item.file);
    CHECK(std::filesystem::exists(path));
    const auto info = patchy::pipl::read_pipl_from_pe_file(path);
    CHECK(info.found);
    CHECK(info.is_filter());
    CHECK(info.name == item.name);
    CHECK(info.category == "ViaThinkSoft");
    CHECK((item.sixty_four ? info.entry_point_64 : info.entry_point_32) == "PluginMain");
    CHECK(info.has_case_info);
    for (int c = 1; c <= 7; ++c) {
      CHECK(info.supports_filter_case(c));
    }
  }
}

// The file reader loads only the headers and the resource section; the result
// must match a parse of the whole image.
void pipl_partial_read_matches_full_read() {
  for (const char* name : {"Greyscale.8bf", "Greyscale64.8bf", "White to Transparent.8bf",
                           "White to Transparent64.8bf"}) {
    const auto path = plugin_fixture(name);
    std::ifstream input(path, std::ios::binary);
    std::vector<std::uint8_t> whole((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto full = patchy::pipl::read_pipl_from_pe_bytes(whole);
    const auto partial = patchy::pipl::read_pipl_from_pe_file(path);
    CHECK(full.found && partial.found);
    CHECK(full.name == partial.name);
    CHECK(full.category == partial.category);
    CHECK(full.kind == partial.kind);
    CHECK(full.entry_point_32 == partial.entry_point_32);
    CHECK(full.entry_point_64 == partial.entry_point_64);
    CHECK(full.has_case_info == partial.has_case_info);
    for (int c = 1; c <= 7; ++c) {
      CHECK(full.case_info_for(c).input_handling == partial.case_info_for(c).input_handling);
    }
  }
}

void legacy_probe_reports_names_and_bitness() {
  patchy::LegacyPhotoshopAdapter adapter;
  const auto x86 = adapter.probe(plugin_fixture("Greyscale.8bf"));
  CHECK(x86.kind == patchy::LegacyPhotoshopPluginKind::Filter8bf);
  CHECK(x86.architecture == "x86");
  CHECK(x86.display_name == "Greyscale");
  CHECK(x86.category == "ViaThinkSoft");
  const auto x64 = adapter.probe(plugin_fixture("White to Transparent64.8bf"));
  CHECK(x64.architecture == "x64");
  CHECK(x64.display_name == "White to Transparent");
#ifdef _WIN32
  CHECK(x86.supported);
  CHECK(x86.entry_point == "PluginMain");
  CHECK(x64.supported);
  CHECK(x64.entry_point == "PluginMain");
  CHECK(x86.reason.find("32-bit") != std::string::npos);
  CHECK(x64.reason.find("64-bit") != std::string::npos);
#else
  CHECK(!x86.supported);
  CHECK(!x64.supported);
#endif

  // Only filters run: a renamed copy with a format extension is reported, not offered.
  const auto out_dir = std::filesystem::path("test-artifacts") / "legacy-plugins";
  std::filesystem::create_directories(out_dir);
  const auto format_copy = out_dir / "Not A Filter.8bi";
  std::filesystem::copy_file(plugin_fixture("Greyscale64.8bf"), format_copy,
                             std::filesystem::copy_options::overwrite_existing);
  const auto format = adapter.probe(format_copy);
  CHECK(format.kind == patchy::LegacyPhotoshopPluginKind::Format8bi);
  CHECK(!format.supported);
  CHECK(format.display_name == "Greyscale");
}

#ifdef _WIN32
std::filesystem::path test_binary_directory() {
  wchar_t buffer[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  return std::filesystem::path(buffer).parent_path();
}

// Runs the helper synchronously; returns its exit code (a negative crash code
// for a fault) or a very negative sentinel when it could not start.
long run_helper(const std::wstring& command_line) {
  std::wstring mutable_line = command_line;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, mutable_line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                      &startup, &process)) {
    return -999999L;
  }
  WaitForSingleObject(process.hProcess, 60000);
  DWORD code = static_cast<DWORD>(-999998L);
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return static_cast<long>(code);
}

std::wstring quoted(const std::filesystem::path& path) { return L"\"" + path.wstring() + L"\""; }

void legacy_host_self_test_filters_fixtures_in_both_bitnesses() {
  const auto bin = test_binary_directory();
  const auto out_dir = std::filesystem::path("test-artifacts") / "legacy-plugins";
  std::filesystem::create_directories(out_dir);
  constexpr int width = 16;
  constexpr int height = 8;
  std::vector<std::uint8_t> input;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const bool left = x < width / 2;
      input.push_back(left ? 220 : 255);
      input.push_back(left ? 30 : 255);
      input.push_back(left ? 30 : 255);
      input.push_back(255);
    }
  }
  const auto input_path = std::filesystem::absolute(out_dir / "self-test-in.raw");
  {
    std::ofstream file(input_path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(input.data()), static_cast<std::streamsize>(input.size()));
  }
  struct Case {
    const wchar_t* helper;
    const char* plugin;
    const wchar_t* output;
    bool greyscale;
  };
  const Case cases[] = {
      {L"patchy-8bf-host64.exe", "Greyscale64.8bf", L"self-test-grey64.raw", true},
      {L"patchy-8bf-host32.exe", "Greyscale.8bf", L"self-test-grey32.raw", true},
      {L"patchy-8bf-host64.exe", "White to Transparent64.8bf", L"self-test-wtt64.raw", false},
      {L"patchy-8bf-host32.exe", "White to Transparent.8bf", L"self-test-wtt32.raw", false},
  };
  for (const auto& item : cases) {
    const auto helper = bin / item.helper;
    CHECK(std::filesystem::exists(helper));
    const auto output_path = std::filesystem::absolute(out_dir / item.output);
    const auto command = quoted(helper) + L" --self-test " + quoted(plugin_fixture(item.plugin)) + L" PluginMain " +
                         quoted(input_path) + L" " + quoted(output_path) + L" 16 8 4";
    const auto code = run_helper(command);
    CHECK(code == 0);
    std::ifstream file(output_path, std::ios::binary);
    std::vector<std::uint8_t> output((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(output.size() == input.size());
    if (output.size() != input.size()) {
      continue;
    }
    const auto* red_pixel = output.data() + (2 * width + 2) * 4;
    const auto* white_pixel = output.data() + (2 * width + 12) * 4;
    if (item.greyscale) {
      CHECK(red_pixel[0] == red_pixel[1] && red_pixel[1] == red_pixel[2]);
      CHECK(red_pixel[0] > 60 && red_pixel[0] < 120);
      CHECK(red_pixel[3] == 255);
      CHECK(white_pixel[0] == 255 && white_pixel[3] == 255);
    } else {
      CHECK(white_pixel[3] == 0);   // white becomes fully transparent
      CHECK(red_pixel[3] > 200);    // red keeps most of its opacity
    }
  }

  // A fault inside the helper ends only the helper, with the exception code as
  // its exit status (Patchy reports that as "the plug-in crashed").
  const auto crash_code = run_helper(quoted(bin / L"patchy-8bf-host64.exe") + L" --self-test-crash");
  CHECK(crash_code == static_cast<long>(0xC0000005U));
}

// The virtual screen (screen_shim.cpp): a plug-in asking for the screen size,
// work area or desktop window sees the capped rectangle, a window it creates
// or moves at coordinates meant for that rectangle lands on it, and a window
// already placed there (real coordinates) stays put. Every test window is
// hidden. Both helpers, since each patches its own bitness of import table.
void legacy_host_virtual_screen_places_windows() {
  const auto bin = test_binary_directory();
  const auto out_dir = std::filesystem::path("test-artifacts") / "legacy-plugins";
  std::filesystem::create_directories(out_dir);
  for (const wchar_t* helper : {L"patchy-8bf-host64.exe", L"patchy-8bf-host32.exe"}) {
    const auto report_path = std::filesystem::absolute(out_dir / (std::wstring(helper) + L".screen.txt"));
    const auto code =
        run_helper(quoted(bin / helper) + L" --self-test-screen 640 480 " + quoted(report_path));
    CHECK(code == 0);
    std::ifstream report(report_path);
    std::map<std::string, std::vector<long>> lines;
    std::string line;
    while (std::getline(report, line)) {
      std::istringstream fields(line);
      std::string key;
      fields >> key;
      long value = 0;
      while (fields >> value) {
        lines[key].push_back(value);
      }
    }
    const auto& screen = lines["virtual"];
    CHECK(screen.size() == 4);
    if (screen.size() != 4) {
      continue;
    }
    const long left = screen[0];
    const long top = screen[1];
    const long width = screen[2];
    const long height = screen[3];
    CHECK(width <= 640 && height <= 480);
    CHECK(width > 0 && height > 0);
    CHECK(lines["metrics"] == (std::vector<long>{width, height}));
    CHECK(lines["workarea"] == (std::vector<long>{0, 0, width, height}));
    CHECK(lines["desktop"] == (std::vector<long>{0, 0, width, height}));
    CHECK(lines["caps"] == (std::vector<long>{width, height}));
    // Only meaningful when the virtual screen sits away from the origin, which a
    // 640 x 480 cap on any larger monitor guarantees.
    if (left != 0 || top != 0) {
      CHECK(lines["window0"] == (std::vector<long>{left, top}));
      CHECK(lines["moved"] == (std::vector<long>{left + 20, top + 30}));
    }
    CHECK(lines["window1"] == (std::vector<long>{left + 10, top + 10}));
    CHECK(lines["kept"] == (std::vector<long>{left + 40, top + 50}));
    // A stub that grows to the screen size is adopted into the frame.
    CHECK(lines["grown"] == (std::vector<long>{1}));
    // A full-screen canvas goes into the titled frame (class PatchyPluginFrame,
    // "<name> via Patchy"), fills its client area, and nothing was shown.
    CHECK(lines["framed"] == (std::vector<long>{1, 1, 1, 1, 0}));
    // Owned by the anchor (Patchy's window), so it always stays above it.
    CHECK(lines["owner"] == (std::vector<long>{1}));
    const auto& frame_client = lines["frameclient"];
    CHECK(frame_client.size() == 4);
    if (frame_client.size() == 4) {
      // The plug-in's space now has the canvas at (0,0): its rectangle reads
      // as (0,0,w,h) while it really sits at the frame's client origin, client
      // conversions round-trip unchanged, and a popup asked for at (5,5)
      // really lands at origin + (5,5).
      CHECK(lines["canvas"] == (std::vector<long>{0, 0, width, height}));
      CHECK(lines["canvasreal"] == (std::vector<long>{frame_client[0], frame_client[1]}));
      CHECK(frame_client[2] == width && frame_client[3] == height);
      CHECK(lines["vspace"] ==
            (std::vector<long>{0, 0, 10, 10, 5, 5, frame_client[0] + 5, frame_client[1] + 5}));
    }
  }
}
#endif

}  // namespace

std::vector<patchy::test::TestCase> pipl_tests() {
  return {
      {"pipl_blob_round_trips_properties", pipl_blob_round_trips_properties},
      {"pipl_reads_bundled_fixture_property_lists", pipl_reads_bundled_fixture_property_lists},
      {"pipl_partial_read_matches_full_read", pipl_partial_read_matches_full_read},
      {"legacy_probe_reports_names_and_bitness", legacy_probe_reports_names_and_bitness},
#ifdef _WIN32
      {"legacy_host_self_test_filters_fixtures_in_both_bitnesses",
       legacy_host_self_test_filters_fixtures_in_both_bitnesses},
      {"legacy_host_virtual_screen_places_windows", legacy_host_virtual_screen_places_windows},
#endif
  };
}
