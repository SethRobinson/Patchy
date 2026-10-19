#include "synthetic_dng.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstring>
#include <exception>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>
// G'MIC filter engine (docs/gmic.md): the '#@gui' catalog parser over the
// vendored standard library and over hand-written declarations, the allowlist,
// and the engine wrapper, including what the Patchy build leaves out (external
// programs, the patch-based algorithms). The catalog and allowlist tests run in
// every build; the engine tests only where the engine is built (PATCHY_ENABLE_GMIC).
#include "core/environment.hpp"
#include "core_test_support.hpp"
#include "local_psd_fixtures.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"
#include "filters/gmic/gmic_catalog.hpp"
#include "filters/gmic/gmic_engine.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace gmic = patchy::gmic;

const std::string& standard_library() {
  static const std::string text = [] {
    std::ifstream in(patchy::test::source_root_path() / "src" / "filters" / "gmic" / "gmic_stdlib.gmic",
                     std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
  }();
  return text;
}

const gmic::Catalog& standard_catalog() {
  static const gmic::Catalog catalog = gmic::parse_catalog(standard_library());
  return catalog;
}

// Four flat opaque quadrants.
gmic::Image quadrants(int width, int height) {
  const float colors[4][4] = {
      {0xc0, 0x30, 0x20, 255}, {0x20, 0xa0, 0x40, 255}, {0x20, 0x40, 0xc0, 255}, {0xe0, 0xd0, 0x20, 255}};
  gmic::Image image = gmic::make_image(width, height, 4);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int quadrant = (y >= height / 2 ? 2 : 0) + (x >= width / 2 ? 1 : 0);
      for (int channel = 0; channel < 4; ++channel) {
        image.at(x, y, channel) = colors[quadrant][channel];
      }
    }
  }
  return image;
}

bool close_to(float actual, float expected, float tolerance = 0.5F) {
  return std::fabs(actual - expected) <= tolerance;
}

void gmic_catalog_reads_the_standard_library() {
  CHECK(standard_library().size() > 2'000'000U);
  const auto& catalog = standard_catalog();
  for (const auto& diagnostic : catalog.diagnostics) {
    std::cerr << "  catalog: " << diagnostic << '\n';
  }
  CHECK(catalog.diagnostics.empty());
  CHECK(catalog.filters.size() > 400U);
  for (const auto& filter : catalog.filters) {
    CHECK(!filter.id.empty());
    CHECK(!filter.name.empty());
    CHECK(filter.command != "_none_");
    CHECK(gmic::default_values(filter).size() == filter.value_count());
  }

  const auto* sepia = catalog.find("fx_sepia");
  CHECK(sepia != nullptr);
  if (sepia == nullptr) {
    return;
  }
  CHECK(sepia->name == "Sepia");
  CHECK(sepia->folder_path == std::vector<std::string>{"Colors"});
  CHECK(sepia->preview_command.empty());
  CHECK(sepia->input_mode == '.');
  CHECK(sepia->value_count() == 3U);
  CHECK(sepia->parameters.size() == 7U);  // note, separator, three sliders, separator, author note
  CHECK(sepia->parameters[0].type == gmic::ParameterType::Note);
  CHECK(sepia->parameters[0].text.find("classic sepia tone") != std::string::npos);
  CHECK(sepia->parameters[1].type == gmic::ParameterType::Separator);
  const auto& brightness = sepia->parameters[2];
  CHECK(brightness.name == "Brightness (%)");
  CHECK(brightness.type == gmic::ParameterType::Float);
  CHECK(brightness.randomizable);
  CHECK(brightness.minimum == -100.0);
  CHECK(brightness.maximum == 100.0);
  CHECK(gmic::command_line(*sepia, {}, false) == "fx_sepia 0,0,0");
  CHECK(gmic::command_line(*sepia, {"10", "-5"}, true) == "fx_sepia 10,-5,0");
}

void gmic_catalog_reads_every_declaration_form() {
  const std::string text =
      "#@gui ____<b>Arrays &amp; Tiles</b>\n"
      "#@gui First : fx_first, fx_first_preview(0)+ : *\n"
      "#@gui : note = note{\"Line one,\n"
      "#@gui : line two (still the note)\"}, sep = separator()\n"
      "#@gui : Amount = _float(1.5,0,10)_1+\n"
      "#@gui : Mode = ~choice(2,\"Off\",\"Soft, wide\",\"Hard\")\n"
      "#@gui : Tint = color(#ff8000) Edge = color(1,2,3,4)\n"
      "#@gui : Label = text{1,\"Say \\\"hi\\\", twice\"}\n"
      "#@gui <i>Inner</i>\n"
      "#@gui Second : fx_second, fx_second_preview(2)*\n"
      "#@gui : Center = point(25,75,0,1) , Count = int(3,1,9), On = bool(true), Go = button(),\\\n"
      "#   Hidden = value(7:8)_0, More = link(\"Docs\",\"https://example.invalid/x?a=1&b=2\")\n"
      "#@gui _<i>Sibling</i>\n"
      "#@gui Again : fx_first\n"
      "#@gui _\n"
      "#@gui About : _none_, _none_\n"
      "#@gui : note = note(\"skipped\")\n"
      "#@gui Plain : fx_plain, _none_\n"
      "#@gui_fr Seulement : fx_fr\n"
      "fx_first : skip\n";
  const auto catalog = gmic::parse_catalog(text);
  for (const auto& diagnostic : catalog.diagnostics) {
    std::cerr << "  catalog: " << diagnostic << '\n';
  }
  CHECK(catalog.diagnostics.empty());
  CHECK(catalog.filters.size() == 4U);
  if (catalog.filters.size() != 4U) {
    return;
  }

  const auto& first = catalog.filters[0];
  CHECK(first.id == "fx_first");
  CHECK(first.folder_path == std::vector<std::string>{"Arrays & Tiles"});
  CHECK(first.preview_command == "fx_first_preview");
  CHECK(first.preview_factor == 0.0);
  CHECK(first.preview_accurate_at_any_zoom);
  CHECK(!first.preview_needs_whole_image);
  CHECK(first.input_mode == '*');
  CHECK(first.parameters.size() == 7U);
  if (first.parameters.size() == 7U) {
    CHECK(first.parameters[0].text == "Line one, line two (still the note)");
    const auto& amount = first.parameters[2];
    CHECK(!amount.updates_preview);
    CHECK(amount.visibility == gmic::ParameterVisibility::Disabled);
    CHECK(amount.visibility_propagation == '+');
    CHECK(amount.default_value == "1.5");
    const auto& mode = first.parameters[3];
    CHECK(mode.randomizable);
    CHECK(mode.choices == (std::vector<std::string>{"Off", "Soft, wide", "Hard"}));
    CHECK(mode.default_value == "2");
    CHECK(first.parameters[4].default_value == "255,128,0");
    CHECK(!first.parameters[4].has_alpha);
    CHECK(first.parameters[5].name == "Edge");
    CHECK(first.parameters[5].default_value == "1,2,3,4");
    CHECK(first.parameters[5].has_alpha);
    CHECK(first.parameters[6].multiline);
    CHECK(first.parameters[6].text == "Say \"hi\", twice");
    CHECK(first.parameters[6].default_value == "\"Say \\\"hi\\\", twice\"");
  }
  CHECK(gmic::command_line(first, {}, false) == "fx_first 1.5,2,255,128,0,1,2,3,4,\"Say \\\"hi\\\", twice\"");

  const auto& second = catalog.filters[1];
  CHECK(second.folder_path == (std::vector<std::string>{"Arrays & Tiles", "Inner"}));
  CHECK(second.preview_factor == 2.0);
  CHECK(second.preview_needs_whole_image);
  CHECK(second.parameters.size() == 6U);
  CHECK(second.value_count() == 5U);
  if (second.parameters.size() == 6U) {
    CHECK(second.parameters[4].visibility == gmic::ParameterVisibility::Hidden);
    CHECK(second.parameters[5].type == gmic::ParameterType::Link);
    CHECK(second.parameters[5].text == "Docs");
    CHECK(second.parameters[5].url == "https://example.invalid/x?a=1&b=2");
  }
  CHECK(gmic::command_line(second, {}, false) == "fx_second 25,75,3,1,0,\"7:8\"");
  CHECK(gmic::command_line(second, {}, true) == "fx_second_preview 25,75,3,1,0,\"7:8\"");

  // A second filter on the same command gets its own id; folders are siblings.
  CHECK(catalog.filters[2].id == "fx_first#2");
  CHECK(catalog.filters[2].folder_path == (std::vector<std::string>{"Arrays & Tiles", "Sibling"}));
  CHECK(gmic::command_line(catalog.filters[2], {}, false) == "fx_first");
  // The '_none_' page is skipped with its parameters; a '_none_' preview means none.
  CHECK(catalog.filters[3].id == "fx_plain");
  CHECK(catalog.filters[3].folder_path == std::vector<std::string>{"Arrays & Tiles"});
  CHECK(catalog.filters[3].preview_command.empty());
  CHECK(catalog.filters[3].parameters.empty());
}

void gmic_allowlist_limits_the_catalog() {
  auto catalog = gmic::parse_catalog("#@gui A : fx_a\n#@gui B : fx_b\n#@gui C : fx_a\n");
  const auto ids = gmic::parse_allowlist("# reviewed filters\n\nfx_a#2   # Folder / C\n  fx_b\r\n#fx_a\n");
  CHECK(ids == (std::vector<std::string>{"fx_a#2", "fx_b"}));
  gmic::retain_allowed(catalog, ids);
  CHECK(catalog.filters.size() == 2U);
  CHECK(catalog.find("fx_a") == nullptr);
  CHECK(catalog.find("fx_a#2") != nullptr);
  CHECK(catalog.find("fx_b") != nullptr);
}

void gmic_output_layer_names_carry_layer_settings() {
  auto info = gmic::parse_output_layer_name("mode(GrainMerge),opacity(50),pos(30,-5),name(Glow (soft), wide)");
  CHECK(info.name == "Glow (soft), wide");
  CHECK(info.blend_mode == "grainmerge");
  CHECK(info.opacity.has_value() && *info.opacity == 50.0);
  CHECK(info.position.has_value() && info.position->first == 30 && info.position->second == -5);

  info = gmic::parse_output_layer_name("name(Layer 1 [red]),mode(add)");
  CHECK(info.name == "Layer 1 [red]");
  CHECK(info.blend_mode == "add");
  CHECK(!info.opacity.has_value() && !info.position.has_value());

  // No field at all: the whole text is the name, even when it holds parentheses.
  info = gmic::parse_output_layer_name("  Photo (copy) ");
  CHECK(info.name == "Photo (copy)");
  CHECK(info.blend_mode.empty());
  // "rename(...)" is not a name field; an out-of-range opacity is clamped.
  info = gmic::parse_output_layer_name("rename(x),opacity(250)");
  CHECK(info.name.empty());
  CHECK(info.opacity.has_value() && *info.opacity == 100.0);
}

// The committed allowlist names only filters the vendored library declares.
void gmic_committed_allowlist_matches_the_library() {
  std::ifstream in(patchy::test::source_root_path() / "src" / "filters" / "gmic" / "patchy_filters_allowlist.txt",
                   std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  const auto ids = gmic::parse_allowlist(text.str());
  CHECK(ids.size() > 100U);
  for (const auto& id : ids) {
    if (standard_catalog().find(id) == nullptr) {
      std::cerr << "  allowlist names an unknown filter: " << id << '\n';
    }
    CHECK(standard_catalog().find(id) != nullptr);
  }
}

// Every offered filter runs with its default parameters. A filter that fails
// here (most often for data files G'MIC would download, which this build cannot
// do) belongs in patchy_filters_excluded.txt, not in the allowlist.
void gmic_allowlisted_filters_run_with_defaults() {
  std::ifstream in(patchy::test::source_root_path() / "src" / "filters" / "gmic" / "patchy_filters_allowlist.txt",
                   std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  const gmic::Engine engine(standard_library());
  // Scan knobs for reviewing the allowlist (docs/gmic.md): PATCHY_GMIC_SCAN_SIZE=WxH and
  // PATCHY_GMIC_SCAN_CHANNELS=3 rerun every filter on another image shape, such as a
  // background layer without alpha.
  int scan_width = 48;
  int scan_height = 40;
  int scan_channels = 4;
  if (const auto size = patchy::environment_variable("PATCHY_GMIC_SCAN_SIZE"); size.has_value()) {
    const auto cross = size->find('x');
    const int width = cross == std::string::npos ? 0 : std::atoi(size->substr(0, cross).c_str());
    const int height = cross == std::string::npos ? 0 : std::atoi(size->substr(cross + 1).c_str());
    if (width > 0 && height > 0) {
      scan_width = width;
      scan_height = height;
    }
  }
  if (patchy::environment_variable("PATCHY_GMIC_SCAN_CHANNELS").value_or("") == "3") {
    scan_channels = 3;
  }
  const auto scan_image = [&] {
    gmic::Image image = quadrants(scan_width, scan_height);
    if (scan_channels == 3) {
      image.channels = 3;
      image.samples.resize(static_cast<std::size_t>(scan_width) * static_cast<std::size_t>(scan_height) * 3U);
    }
    return image;
  };
  int failures = 0;
  double slowest_ms = 0.0;
  std::string slowest;
  for (const auto& id : gmic::parse_allowlist(text.str())) {
    const auto* filter = standard_catalog().find(id);
    if (filter == nullptr) {
      continue;  // reported by gmic_committed_allowlist_matches_the_library
    }
    // Hosts always name their layers; several filters read the names.
    std::vector<gmic::Image> images{scan_image()};
    images[0].name = "Layer 1";
    if (filter->input_mode != '.') {
      images.push_back(scan_image());
      images[1].name = "Layer 2";
    }
    gmic::RunControl control;
    std::atomic<bool> finished{false};
    // A filter that neither finishes nor fails within the limit is stopped. The limit
    // catches hangs, not slowness: at 10 s, fx_pack_sprites (about 12.5 s on a busy
    // laptop host) raced the abort and failed only when G'MIC polled it first.
    constexpr int kLimitSeconds = 30;
    std::thread limit([&] {
      for (int tick = 0; tick < kLimitSeconds * 20 && !finished.load(); ++tick) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      control.abort.store(true);
    });
    const auto start = std::chrono::steady_clock::now();
    const auto result = engine.run(gmic::command_line(*filter, {}, false), images, &control);
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    finished.store(true);
    limit.join();
    if (elapsed_ms > slowest_ms) {
      slowest_ms = elapsed_ms;
      slowest = id;
    }
    if (!result.ok) {
      ++failures;
      std::string reason =
          result.aborted ? "did not finish in " + std::to_string(kLimitSeconds) + " s" : result.error;
      for (auto& ch : reason) {
        if (ch == '\n' || ch == '\r') {
          ch = ' ';
        }
      }
      std::cerr << "  GMIC-FAIL " << id << " | " << reason.substr(0, 420) << '\n';
    } else if (images.empty()) {
      ++failures;
      std::cerr << "  GMIC-FAIL " << id << " | returned no image\n";
    }
  }
  std::cerr << "  slowest filter: " << slowest << " (" << static_cast<int>(slowest_ms) << " ms)\n";
  CHECK(failures == 0);
}

void gmic_engine_runs_a_library_filter() {
  CHECK(gmic::engine_version() == "4.0.5");
  const gmic::Engine engine(standard_library());
  std::vector<gmic::Image> images{quadrants(64, 48)};
  const auto* sepia = standard_catalog().find("fx_sepia");
  CHECK(sepia != nullptr);
  if (sepia == nullptr) {
    return;
  }
  const auto result = engine.run(gmic::command_line(*sepia, {}, false), images);
  if (!result.ok) {
    std::cerr << "  gmic: " << result.error << '\n';
  }
  CHECK(result.ok);
  CHECK(!result.aborted);
  CHECK(images.size() == 1U);
  if (images.size() != 1U) {
    return;
  }
  CHECK(images[0].width == 64 && images[0].height == 48 && images[0].channels == 4);
  // Pinned from G'MIC-Qt 4.0.4 applying Sepia to the same colors (October 2026).
  const float expected[4][3] = {
      {116.1F, 85.4F, 45.1F}, {135.3F, 109.4F, 65.0F}, {77.4F, 50.1F, 23.4F}, {197.9F, 186.0F, 146.2F}};
  const int probes[4][2] = {{2, 2}, {61, 2}, {2, 45}, {61, 45}};
  for (int quadrant = 0; quadrant < 4; ++quadrant) {
    const int x = probes[quadrant][0];
    const int y = probes[quadrant][1];
    for (int channel = 0; channel < 3; ++channel) {
      CHECK(close_to(images[0].at(x, y, channel), expected[quadrant][channel]));
    }
    CHECK(close_to(images[0].at(x, y, 3), 255.0F));
  }
}

void gmic_engine_reports_errors_and_output_names() {
  const gmic::Engine engine(standard_library());
  std::vector<gmic::Image> images{quadrants(16, 16)};
  auto result = engine.run("patchy_no_such_command 1", images);
  CHECK(!result.ok);
  CHECK(!result.error.empty());
  CHECK(images.size() == 1U);  // untouched on failure

  // A command may return several images, name them, and return a status.
  result = engine.run("+blur 2 name[1] \"mode(screen),opacity(50),name(Glow)\" status patchy_done", images);
  CHECK(result.ok);
  CHECK(images.size() == 2U);
  if (images.size() == 2U) {
    CHECK(images[1].name == "mode(screen),opacity(50),name(Glow)");
    CHECK(images[1].width == 16 && images[1].channels == 4);
  }
  CHECK(result.status == "patchy_done");
}

// The Patchy build of the interpreter starts no programs and holds neither the
// patch correspondence search nor patch-based inpainting (docs/legal-constraints.md).
void gmic_engine_leaves_out_programs_and_patch_algorithms() {
  const gmic::Engine engine(standard_library());

  const auto probe = std::filesystem::current_path() / "patchy-gmic-exec-probe.txt";
  std::error_code ignored;
  std::filesystem::remove(probe, ignored);
  std::vector<gmic::Image> none;
  const auto exec = engine.run("exec \"echo started > " + probe.generic_string() + "\"", none);
  CHECK(exec.ok);  // the command is accepted and skipped
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  CHECK(!std::filesystem::exists(probe));
  std::filesystem::remove(probe, ignored);

  std::vector<gmic::Image> pair{quadrants(32, 32), quadrants(32, 32)};
  const auto match = engine.run("matchpatch[0] [1],5", pair);
  CHECK(!match.ok);
  CHECK(match.error.find("Not available in this build") != std::string::npos);

  gmic::Image mask = gmic::make_image(32, 32, 1);
  mask.at(16, 16, 0) = 255.0F;
  std::vector<gmic::Image> inpaint{quadrants(32, 32), mask};
  const auto patch = engine.run("inpaint[0] [1],7,16", inpaint);
  CHECK(!patch.ok);
  CHECK(patch.error.find("Not available in this build") != std::string::npos);
}

// Catalog hiding alone is insufficient: formulas can call run(), and $$name can
// copy a command into an alias. Exercise the real interpreter and library rather
// than testing the policy's name matching in isolation.
void gmic_engine_restricts_excluded_commands_and_aliases() {
  const gmic::Engine engine(standard_library());
  std::ifstream in(patchy::test::source_root_path() / "src" / "filters" / "gmic" /
                   "patchy_filters_excluded.txt", std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  const auto excluded = gmic::parse_allowlist(text.str());
  CHECK(excluded.size() > 40U);
  const auto original = quadrants(16, 16);
  bool all_refused = true;
  const auto refused = [&](const std::string& command) {
    std::vector<gmic::Image> images{original};
    const auto result = engine.run(command, images);
    const bool rejected = !result.ok && result.error.find("Not available in this build") != std::string::npos;
    const bool unchanged = images.size() == 1U && images.front().samples == original.samples;
    if (!rejected || !unchanged) {
      std::cerr << "  command was not restricted: " << command << " | " << result.error << '\n';
    }
    all_refused = all_refused && rejected && unchanged;
  };
  for (const auto& id : excluded) {
    const auto* filter = standard_catalog().find(id);
    CHECK(filter != nullptr);
    refused(gmic::command_line(*filter, {}, false));
    // gui_no_preview is a shared placeholder, not the excluded filter's code.
    if (!filter->preview_command.empty() && filter->preview_command != "_none_" &&
        filter->preview_command != "gui_no_preview") {
      refused(gmic::command_line(*filter, {}, true));
    }
  }
  for (const auto* command : {
           "_fx_blur_dof", "__fx_blur_motion_preview_args", "+fx_blur_motion[0]",
           "blend_seamless", "seamcarve", "syntexturize", "nn_lib", "inpaint_matchpatch", "denoise 1",
           "m \"patchy_alias : fx_blur_motion\" patchy_alias",
           "m \"patchy_alias : $${fx_blur_motion}\" patchy_alias",
           "m \"fx_blur_motion : fill 0\" fx_blur_motion",
           "eval \"run('fx_blur_', 'motion')\"",
           "parallel \"fx_blur_motion\""}) {
    refused(command);
  }
  CHECK(all_refused);
}

void gmic_engine_custom_formula_cannot_run_excluded_filters() {
  const gmic::Engine engine(standard_library());
  const auto* blend = standard_catalog().find("fx_blend");
  const auto* motion = standard_catalog().find("fx_blur_motion");
  CHECK(blend != nullptr && motion != nullptr);
  auto values = gmic::default_values(*blend);
  CHECK(values.size() == 5U);
  values[0] = "6";  // Custom formula.
  const auto first = quadrants(16, 16);
  auto second = first;
  for (std::size_t i = 0; i < second.samples.size() * 3U / 4U; ++i) second.samples[i] *= 0.5F;
  std::vector<gmic::Image> images{first, second};
  values[4] = gmic::quoted_text_value("run('" + gmic::command_line(*motion, {}, false) + "') + (a+b)/2");
  const auto blocked = engine.run(gmic::command_line(*blend, values, false), images);
  CHECK(!blocked.ok);
  CHECK(blocked.error.find("Not available in this build") != std::string::npos);
  CHECK(images.size() == 2U);
  CHECK(images[0].samples == first.samples && images[1].samples == second.samples);

  // Legitimate formulas and internal command calls still work.
  values[4] = gmic::quoted_text_value("run('status 1')*0 + (a+b)/2");
  const auto allowed = engine.run(gmic::command_line(*blend, values, false), images);
  if (!allowed.ok) std::cerr << "  formula: " << allowed.error << '\n';
  CHECK(allowed.ok && images.size() == 1U);
  for (std::size_t i = 0; i < first.samples.size(); ++i) {
    CHECK(close_to(images[0].samples[i], (first.samples[i] + second.samples[i]) * 0.5F));
  }
}

// The engine's FFT is pocketfft, which has to work at sizes that are not powers of two and keep
// FFTW's scaling (forward unscaled, inverse divided by the transformed lengths).
void gmic_engine_fft_works_at_any_size() {
  const gmic::Engine engine(standard_library());

  gmic::Image impulse = gmic::make_image(12, 10, 1);
  impulse.at(0, 0, 0) = 1.0F;
  std::vector<gmic::Image> spectrum{impulse};
  auto result = engine.run("fft", spectrum);
  if (!result.ok) {
    std::cerr << "  gmic: " << result.error << '\n';
  }
  CHECK(result.ok);
  CHECK(spectrum.size() == 2U);
  if (spectrum.size() == 2U) {
    CHECK(spectrum[0].width == 12 && spectrum[0].height == 10);
    for (int y = 0; y < 10; ++y) {
      for (int x = 0; x < 12; ++x) {
        CHECK(close_to(spectrum[0].at(x, y, 0), 1.0F, 1.0e-4F));
        CHECK(close_to(spectrum[1].at(x, y, 0), 0.0F, 1.0e-4F));
      }
    }
  }

  for (const char* command : {"fft ifft", "fft x ifft x", "fft y ifft y"}) {
    const gmic::Image original = quadrants(21, 15);
    std::vector<gmic::Image> images{original};
    result = engine.run(command, images);
    CHECK(result.ok);
    CHECK(images.size() == 2U);
    if (!result.ok || images.size() != 2U) {
      continue;
    }
    for (int y = 0; y < 15; ++y) {
      for (int x = 0; x < 21; ++x) {
        for (int channel = 0; channel < 4; ++channel) {
          CHECK(close_to(images[0].at(x, y, channel), original.at(x, y, channel), 1.0e-2F));
          CHECK(close_to(images[1].at(x, y, channel), 0.0F, 1.0e-2F));
        }
      }
    }
  }
}

void gmic_engine_stops_on_request() {  const gmic::Engine engine(standard_library());
  std::vector<gmic::Image> images{quadrants(256, 256)};
  gmic::RunControl control;
  control.abort.store(true);
  const auto start = std::chrono::steady_clock::now();
  // Bounded even if the request were ignored.
  const auto result = engine.run("repeat 400 { blur 3 }", images, &control);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  CHECK(result.aborted);
  CHECK(!result.ok);
  CHECK(elapsed < std::chrono::seconds(5));
}

// A command's `progress` reaches the caller as a percentage; a run that reports none
// leaves it negative.
void gmic_engine_reports_progress_as_a_percentage() {
  const gmic::Engine engine(standard_library());
  std::vector<gmic::Image> images{quadrants(16, 16)};
  gmic::RunControl control;
  CHECK(engine.run("progress 42", images, &control).ok);
  CHECK(control.progress.load() == 42.0F);
  gmic::RunControl silent;
  CHECK(engine.run("blur 1", images, &silent).ok);
  CHECK(silent.progress.load() < 0.0F);
}

}  // namespace

std::vector<patchy::test::TestCase> gmic_tests() {
  std::vector<patchy::test::TestCase> tests{
      {"gmic_catalog_reads_the_standard_library", gmic_catalog_reads_the_standard_library},
      {"gmic_catalog_reads_every_declaration_form", gmic_catalog_reads_every_declaration_form},
      {"gmic_allowlist_limits_the_catalog", gmic_allowlist_limits_the_catalog},
      {"gmic_output_layer_names_carry_layer_settings", gmic_output_layer_names_carry_layer_settings},
      {"gmic_committed_allowlist_matches_the_library", gmic_committed_allowlist_matches_the_library},
  };
  if (gmic::engine_available()) {
    tests.insert(tests.end(),
                 {{"gmic_engine_runs_a_library_filter", gmic_engine_runs_a_library_filter},
                  {"gmic_engine_reports_errors_and_output_names", gmic_engine_reports_errors_and_output_names},
                  {"gmic_engine_fft_works_at_any_size", gmic_engine_fft_works_at_any_size},
                  {"gmic_engine_leaves_out_programs_and_patch_algorithms",
                   gmic_engine_leaves_out_programs_and_patch_algorithms},
                  {"gmic_engine_restricts_excluded_commands_and_aliases",
                   gmic_engine_restricts_excluded_commands_and_aliases},
                  {"gmic_engine_custom_formula_cannot_run_excluded_filters",
                   gmic_engine_custom_formula_cannot_run_excluded_filters},
                  {"gmic_engine_stops_on_request", gmic_engine_stops_on_request},
                  {"gmic_engine_reports_progress_as_a_percentage", gmic_engine_reports_progress_as_a_percentage},
                  {"gmic_allowlisted_filters_run_with_defaults", gmic_allowlisted_filters_run_with_defaults}});
  }
  return tests;
}
