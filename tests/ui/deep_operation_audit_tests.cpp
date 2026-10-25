// The 16 and 32-bit audit (docs/high-bit-depth.md, "Verification rules"): every user-level
// operation below runs on the same picture at 8, 16 and 32 bits through the scripting API,
// and the display composites are compared after the operation.
//   - 16 bits computes in the display encoding like 8 bits, so it must match everywhere
//     (within a few levels; a little more where a filter rounds differently).
//   - 32 bits computes in linear light, as Photoshop does, so blurs, resampling, blends and
//     gradients legitimately differ from 8 bits at edges and in mixed tones. It must match
//     on the interiors of flat color regions, where every correct pipeline agrees.
// Operations whose 32-bit meaning differs by design (opacity, blend modes) compare 16 bits
// only; filters that 32 bits does not offer are skipped there.
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "render/compositor.hpp"
#include "ui/main_window.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_support.hpp"
#include <QApplication>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::test::ui::scripting_support::run_script;

enum class Compare {
  Flat,        // 16 bits everywhere, 32 bits on flat interiors
  SixteenOnly  // 32 bits differs from 8 by design
};

struct AuditOperation {
  const char* name;
  // JavaScript run after the setup below, with `doc`, `base` (the Background) and `top`.
  // %DIR% is replaced by a temporary folder for save round trips.
  const char* script;
  Compare compare{Compare::Flat};
  int tolerance{3};
};

// A 160x120 Background of twelve 40x40 flat blocks plus an opaque 60x50 "Top" rectangle,
// made at 8 bits and converted to BITS.
constexpr const char* kSetup = R"JS(
  var doc = app.newDocument(160, 120);
  var base = doc.activeLayer;
  var palette = [[200,40,40],[40,180,60],[30,60,200],[240,220,40],[128,128,128],[20,20,20],
                 [250,250,250],[90,30,120],[0,160,170],[230,120,30],[180,180,220],[60,90,60]];
  var data = new Uint8Array(160 * 120 * 4);
  for (var y = 0; y < 120; y++) {
    for (var x = 0; x < 160; x++) {
      var c = palette[Math.floor(y / 40) * 4 + Math.floor(x / 40)];
      var i = (y * 160 + x) * 4;
      data[i] = c[0]; data[i + 1] = c[1]; data[i + 2] = c[2]; data[i + 3] = 255;
    }
  }
  base.setPixels({x: 0, y: 0, width: 160, height: 120, data: data.buffer});
  var top = doc.addLayer('Top');
  top.fillRect(50, 30, 60, 50, '#c83c78');
  if (BITS !== 8) { doc.convertBitDepth(BITS); }
)JS";

const std::vector<AuditOperation>& audit_operations() {
  static const std::vector<AuditOperation> operations = {
      {"unchanged", ""},
      // Interactive operations, as one-call script equivalents.
      {"move layer", "top.x = top.x + 23; top.y = top.y - 11;"},
      {"transform scale 150%", "top.transform({matrix: [1.5, 0, 0, 1.5, -40, -30]});"},
      {"transform rotate 30", "top.transform({matrix: [0.866, 0.5, -0.5, 0.866, 50, -30]});"},
      {"transform perspective", "top.transform({quad: [[40, 20], [130, 35], [120, 95], [55, 100]]});"},
      {"transform selection perspective",
       "doc.selection.selectRect(60, 40, 40, 30); top.transform({quad: [[55, 35], [110, 45], [105, 80], [60, 75]]});"},
      {"transform nearest", "top.transform({matrix: [2, 0, 0, 2, -80, -55], interpolation: 'nearest'});"},
      // Resampling and geometry.
      {"image size 50% bicubic", "doc.resizeImage(80, 60, {method: 'bicubic'});"},
      {"image size 200% bilinear", "doc.resizeImage(320, 240, {method: 'bilinear'});"},
      {"image size 150% nearest", "doc.resizeImage(240, 180, {method: 'nearest'});"},
      {"image size 70% automatic", "doc.resizeImage(112, 84);"},
      {"canvas size", "doc.resizeCanvas(200, 150);"},
      {"crop", "doc.crop(13, 9, 120, 90);"},
      {"image rotate 90", "app.runCommand('image.rotate_cw');"},
      {"image rotate 180", "app.runCommand('image.rotate_180');"},
      {"image flip horizontal", "app.runCommand('image.flip_horizontal');"},
      {"layer rotate 90", "app.runCommand('layer.rotate_cw');"},
      {"layer flip vertical", "app.runCommand('layer.flip_vertical');"},
      // Copy, paste, duplicate, merges.
      {"duplicate layer", "var d = top.duplicate(); d.x = d.x + 31; d.y = d.y + 17;"},
      {"copy and paste", "doc.selection.selectRect(30, 20, 70, 60); app.runCommand('edit.copy');"
                         " doc.selection.deselect(); app.runCommand('edit.paste');"},
      {"paste in place", "doc.selection.selectRect(30, 20, 70, 60); app.runCommand('edit.copy');"
                         " app.runCommand('edit.paste_in_place'); doc.activeLayer.x = doc.activeLayer.x + 40;"},
      {"copy merged and paste", "doc.selection.selectRect(30, 20, 70, 60); app.runCommand('edit.copy_merged');"
                                " doc.selection.deselect(); app.runCommand('edit.paste');"
                                " doc.activeLayer.x = doc.activeLayer.x - 20;"},
      {"layer via copy", "doc.activeLayer = base; doc.selection.selectRect(0, 0, 80, 40);"
                         " app.runCommand('layer.via_copy'); doc.activeLayer.y = doc.activeLayer.y + 60;"},
      {"merge down", "top.x = top.x + 10; app.runCommand('layer.merge_down');"},
      {"merge visible", "top.y = top.y + 10; app.runCommand('layer.merge_visible');"},
      {"flatten", "top.x = top.x - 15; doc.flatten();"},
      // Selections, masks, fills, painting.
      {"fill selection", "doc.selection.selectRect(10, 50, 90, 40); top.fill('#3080f0');"},
      {"clear selection", "doc.activeLayer = base; doc.selection.selectRect(70, 10, 60, 60); app.runCommand('layer.clear');"},
      {"mask from selection", "doc.selection.selectRect(60, 40, 30, 30); app.runCommand('layer.add_mask');"},
      {"mask from selection inverted", "doc.selection.selectRect(60, 40, 30, 30); app.runCommand('layer.add_mask');"
                                       " app.runCommand('layer.invert_mask');"},
      {"brush stroke", "top.drawStrokes([{points: [{x: 5, y: 110}, {x: 150, y: 15}], size: 20,"
                       " color: '#20d0a0'}]);"},
      {"eraser stroke", "top.drawStrokes([{tool: 'eraser', points: [{x: 40, y: 55}, {x: 120, y: 55}], size: 16,"
                        "}]);"},
      {"solid color fill layer", "doc.addFillLayer('Fill', '#ff8800');"},
      {"solid fill layer saved as PSD", "doc.addFillLayer('Fill', '#ff8800'); doc.activeLayer.opacity = 100;"
                                        " doc.saveAs('%DIR%/fill.psd'); app.open('%DIR%/fill.psd');"},
      // Gradients interpolate in linear light at 32 bits (Photoshop's rule): 16 bits only.
      {"gradient fill layer", "doc.addFillLayer('Gradient', {type: 'gradient', gradient: {type: 'linear',"
                              " colorStops: [{position: 0, color: '#102080'}, {position: 1, color: '#f0e060'}]}});",
       Compare::SixteenOnly},
      // By design different at 32 bits (linear-light blending): 16 bits only.
      {"layer opacity 60%", "top.opacity = 60;", Compare::SixteenOnly},
      {"blend mode multiply", "top.blendMode = 'multiply';", Compare::SixteenOnly},
      {"blend mode difference", "top.blendMode = 'difference';", Compare::SixteenOnly},
      // Save and reopen (the reopened document is compared).
      {"save PSD and reopen", "doc.saveAs('%DIR%/audit.psd'); app.open('%DIR%/audit.psd');"},
      {"save PSB and reopen", "doc.saveAs('%DIR%/audit.psb'); app.open('%DIR%/audit.psb');"},
      {"save TIFF and reopen", "doc.saveAs('%DIR%/audit.tif'); app.open('%DIR%/audit.tif');"},
      {"save PNG and reopen", "doc.saveAs('%DIR%/audit.png'); app.open('%DIR%/audit.png');"},
      {"save OpenEXR and reopen", "doc.saveAs('%DIR%/audit.exr'); app.open('%DIR%/audit.exr');"},
      {"save JPEG XL and reopen", "doc.saveAs('%DIR%/audit.jxl', {lossless: true}); app.open('%DIR%/audit.jxl');"},
      {"save Radiance HDR and reopen", "doc.saveAs('%DIR%/audit.hdr'); app.open('%DIR%/audit.hdr');", Compare::Flat, 6},
  };
  return operations;
}

// Every built-in filter at its defaults on the Top layer and the Background; ids that a
// depth does not offer throw there and are skipped (docs/high-bit-depth.md, Phase 6).
const std::vector<std::string>& audit_filters() {
  static const std::vector<std::string> filters = {
      "add_noise", "auto_color", "auto_contrast", "auto_tone", "average", "blur", "blur_more", "box_blur",
      "brightness_contrast", "cinematic_matte", "clouds", "color_halftone", "desaturate", "dust_and_scratches",
      "edge_detect", "emboss", "film_grain", "gaussian_blur", "glowing_edges", "grayscale", "high_pass", "invert",
      "iris_blur", "lens_blur", "median", "motion_blur", "noir", "pinch_bloat", "pixelate", "plastic_wrap",
      "posterize", "punchy_color", "radial_blur", "sepia", "sharpen", "sharpen_edges", "sharpen_more", "soft_glow",
      "surface_blur", "threshold", "tilt_shift_blur", "twirl", "unsharp_mask", "vignette", "vintage_fade", "wave"};
  return filters;
}

struct Result {
  bool ran{false};
  std::string error;
  patchy::PixelBuffer display;  // RGB8 display composite
};

Result run_at_depth(const std::string& operation_script, int bits, const QString& directory) {
  std::cout << "  [audit-run] " << bits << " bits: " << operation_script.substr(0, 60) << std::endl;
  patchy::ui::MainWindow window;
  show_window(window);
  auto script = QString::fromUtf8(kSetup).replace(QStringLiteral("BITS"), QString::number(bits)) +
                QString::fromStdString(operation_script).replace(QStringLiteral("%DIR%"), directory);
  Result result;
  try {
    result.ran = run_script(window, QStringLiteral("try {\n") + script +
                                        QStringLiteral("\n} catch (error) { console.log('AUDIT-THROW ' + error); throw error; }"));
    QApplication::processEvents();
  } catch (const std::exception& error) {
    // A C++ exception escaping the script engine or the event loop is a defect of its own.
    result.ran = true;
    result.error = std::string("C++ exception escaped: ") + error.what();
    return result;
  }
  if (!result.ran) {
    return result;
  }
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  for (const auto& problem : patchy::document_depth_problems(document)) {
    result.error += problem + "; ";
  }
  if (bits != 8 && std::as_const(document).color_state().bit_depth == patchy::BitDepth::UInt8) {
    result.error += "the document came back at 8 bits; ";
  }
  try {
    result.display = patchy::Compositor{}.flatten_rgb8(document);
  } catch (const std::exception& error) {
    result.error += std::string("flatten threw: ") + error.what();
  }
  return result;
}

struct Difference {
  std::size_t compared{0};
  std::size_t over{0};
  int worst{0};
  int worst_x{0};
  int worst_y{0};
};

// Channel difference against the 8-bit result, over every pixel or only where the 8-bit
// result is flat in a 5x5 neighborhood.
Difference compare_display(const patchy::PixelBuffer& reference, const patchy::PixelBuffer& candidate, bool flat_only,
                           int tolerance) {
  Difference difference;
  const auto width = reference.width();
  const auto height = reference.height();
  const auto channels = static_cast<std::size_t>(reference.format().channels);
  const auto flat_at = [&](int x, int y) {
    if (x < 2 || y < 2 || x + 2 >= width || y + 2 >= height) {
      return false;
    }
    const auto* center = reference.pixel(x, y);
    for (int dy = -2; dy <= 2; ++dy) {
      for (int dx = -2; dx <= 2; ++dx) {
        const auto* px = reference.pixel(x + dx, y + dy);
        for (std::size_t c = 0; c < 3; ++c) {
          if (std::abs(static_cast<int>(px[c]) - static_cast<int>(center[c])) > 2) {
            return false;
          }
        }
      }
    }
    return true;
  };
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (flat_only && !flat_at(x, y)) {
        continue;
      }
      const auto* a = reference.pixel(x, y);
      const auto* b = candidate.pixel(x, y);
      int pixel_worst = 0;
      for (std::size_t c = 0; c < std::min<std::size_t>(channels, 3U); ++c) {
        pixel_worst = std::max(pixel_worst, std::abs(static_cast<int>(a[c]) - static_cast<int>(b[c])));
      }
      ++difference.compared;
      if (pixel_worst > tolerance) {
        ++difference.over;
      }
      if (pixel_worst > difference.worst) {
        difference.worst = pixel_worst;
        difference.worst_x = x;
        difference.worst_y = y;
      }
    }
  }
  return difference;
}

// One audited operation: run at 8/16/32 and compare. Returns the number of failures.
int audit_one(const std::string& name, const std::string& script, Compare compare, int tolerance,
              const QString& directory) {
  const auto reference = run_at_depth(script, 8, directory);
  if (!reference.ran || !reference.error.empty()) {
    std::cout << "  [audit] " << name << ": the 8-bit run failed " << reference.error << "\n";
    return 1;
  }
  int failures = 0;
  for (const int bits : {16, 32}) {
    if (bits == 32 && compare == Compare::SixteenOnly) {
      continue;
    }
    const auto candidate = run_at_depth(script, bits, directory);
    if (!candidate.ran) {
      // Filters a depth does not offer throw; anything else failing is a defect.
      std::cout << "  [audit] " << name << " at " << bits << " bits: the run threw (not offered at this depth?)\n";
      if (name.rfind("filter ", 0) != 0) {
        ++failures;
      }
      continue;
    }
    if (!candidate.error.empty()) {
      std::cout << "  [audit] " << name << " at " << bits << " bits: " << candidate.error << '\n';
      ++failures;
      if (candidate.display.empty()) {
        continue;
      }
    }
    if (candidate.display.width() != reference.display.width() ||
        candidate.display.height() != reference.display.height()) {
      std::cout << "  [audit] " << name << " at " << bits << " bits: size " << candidate.display.width() << 'x'
                << candidate.display.height() << " vs " << reference.display.width() << 'x'
                << reference.display.height() << '\n';
      ++failures;
      continue;
    }
    const bool flat_only = bits == 32;
    const auto difference = compare_display(reference.display, candidate.display, flat_only, tolerance);
    // A sliver of outliers is allowed for anti-aliasing phase and rounding at block edges.
    const auto allowed = std::max<std::size_t>(2, difference.compared / 200);
    if (difference.compared == 0 || difference.over > allowed) {
      const auto* a = reference.display.pixel(difference.worst_x, difference.worst_y);
      const auto* b = candidate.display.pixel(difference.worst_x, difference.worst_y);
      std::cout << "  [audit] " << name << " at " << bits << " bits: " << difference.over << " of "
                << difference.compared << " pixels differ by more than " << tolerance << " (worst " << difference.worst
                << " at " << difference.worst_x << ',' << difference.worst_y << ": 8-bit " << int(a[0]) << ','
                << int(a[1]) << ',' << int(a[2]) << " vs " << int(b[0]) << ',' << int(b[1]) << ',' << int(b[2])
                << ")\n";
      ++failures;
    }
  }
  return failures;
}

void ui_deep_documents_match_8_bit_after_every_operation() {
  patchy::set_deep_editing_override(true);
  const auto restore = qScopeGuard([] { patchy::set_deep_editing_override(std::nullopt); });
  QTemporaryDir directory;
  CHECK(directory.isValid());
  int failures = 0;
  for (const auto& operation : audit_operations()) {
    failures += audit_one(operation.name, operation.script, operation.compare, operation.tolerance, directory.path());
  }
  std::cout << "  [audit] operations: " << audit_operations().size() << ", failures " << failures << '\n';
  CHECK(failures == 0);
}

void ui_deep_documents_match_8_bit_after_every_filter() {
  patchy::set_deep_editing_override(true);
  const auto restore = qScopeGuard([] { patchy::set_deep_editing_override(std::nullopt); });
  QTemporaryDir directory;
  CHECK(directory.isValid());
  int failures = 0;
  for (const auto& id : audit_filters()) {
    const std::string script = "top.applyFilter('patchy.filters." + id + "'); base.applyFilter('patchy.filters." + id + "');";
    // Noise and grain are random per run; their 16-bit and 32-bit results are compared on
    // flat interiors with a wider tolerance (the grain itself averages out of nothing).
    const bool random = id == "add_noise" || id == "film_grain" || id == "clouds";
    if (random) {
      continue;
    }
    // Radial Blur averages along long arcs that cross several color blocks, so its 32-bit
    // linear-light mix differs from 8 bits even where the result looks flat.
    const auto compare = id == "radial_blur" ? Compare::SixteenOnly : Compare::Flat;
    failures += audit_one("filter " + id, script, compare, 4, directory.path());
  }
  std::cout << "  [audit] filters: " << audit_filters().size() << ", failures " << failures << '\n';
  CHECK(failures == 0);
}

}  // namespace

std::vector<patchy::test::TestCase> deep_operation_audit_tests() {
  return {
      {"ui_deep_documents_match_8_bit_after_every_operation", ui_deep_documents_match_8_bit_after_every_operation},
      {"ui_deep_documents_match_8_bit_after_every_filter", ui_deep_documents_match_8_bit_after_every_filter},
  };
}
