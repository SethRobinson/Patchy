// GitHub issue 755: a Pattern Overlay, a Bevel & Emboss Texture or a shape's pattern fill
// whose tile the document does not hold, while the pattern library or the bundled presets
// do, drew nothing on the canvas. Only the Layer Style dialog's preview copied the tile in,
// and OK with no edits put it back out. A document now takes such a tile when it opens,
// after an undo, and when the library gains the pattern, and a save embeds it.
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_presets.hpp"
#include "core/pattern_resource.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/pattern_library.hpp"
#include "ui/photo_pattern_presets.hpp"
#include "ui/qt_paths.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QScopeGuard>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;

// Ids no shipped preset uses.
constexpr std::string_view kStripesId = "75500000-aaaa-4bbb-8ccc-000000000001";
constexpr std::string_view kGrainId = "75500000-aaaa-4bbb-8ccc-000000000002";
constexpr std::string_view kNowhereId = "75500000-aaaa-4bbb-8ccc-00000000dead";

constexpr int kWidth = 240;
constexpr int kHeight = 150;
const QRect kBlock(40, 30, 160, 90);

// Diagonal yellow and blue stripes: easy to see through any blend.
patchy::PixelBuffer stripes_tile() {
  auto tile = solid_pixels(16, 16, patchy::PixelFormat::rgba8(), QColor(30, 60, 200));
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      if (((x + y) / 4) % 2 == 0) {
        auto* px = tile.pixel(x, y);
        px[0] = 250;
        px[1] = 220;
        px[2] = 40;
      }
    }
  }
  return tile;
}

// One round bump per tile for the bevel texture: a grid of dimples.
patchy::PixelBuffer grain_tile() {
  constexpr int kSize = 12;
  auto tile = solid_pixels(kSize, kSize, patchy::PixelFormat::rgba8(), QColor(60, 60, 60));
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      const int dx = 2 * x + 1 - kSize;
      const int dy = 2 * y + 1 - kSize;
      const int distance_squared = dx * dx + dy * dy;  // 2 at the center, 242 in a corner
      const auto level = static_cast<std::uint8_t>(std::clamp(250 - distance_squared * 2, 60, 250));
      auto* px = tile.pixel(x, y);
      px[0] = level;
      px[1] = level;
      px[2] = level;
    }
  }
  return tile;
}

patchy::PatternResource resource(std::string_view id, const char* name, patchy::PixelBuffer tile) {
  patchy::PatternResource result;
  result.id = std::string(id);
  result.name = name;
  result.tile = std::move(tile);
  result.provenance = patchy::PatternProvenance::Authored;
  return result;
}

// A white document with a pink block whose style is a Pattern Overlay and a Bevel & Emboss
// with a Texture. The document holds no pattern tiles.
patchy::Document styled_document(std::string_view overlay_id, std::string_view texture_id) {
  patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background",
                           solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  auto pixels = solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
  fill_pixel_rect(pixels, kBlock, QColor(232, 60, 200));
  auto& block = document.add_pixel_layer("Styled", std::move(pixels));
  patchy::LayerPatternOverlay overlay;
  overlay.enabled = true;
  overlay.opacity = 0.6F;
  overlay.pattern_id = std::string(overlay_id);
  overlay.pattern_name = "Stripes";
  block.layer_style().pattern_overlays = {overlay};
  patchy::LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.texture.enabled = true;
  bevel.texture.scale = 2.0F;
  bevel.texture.pattern_id = std::string(texture_id);
  bevel.texture.pattern_name = "Grain";
  block.layer_style().bevels = {bevel};
  document.set_active_layer(block.id());
  return document;
}

patchy::Document styled_document() { return styled_document(kStripesId, kGrainId); }

patchy::PixelBuffer flatten(const patchy::Document& document) {
  return patchy::Compositor().flatten_rgb8(document);
}

bool same_pixels(const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) {
  return a.width() == b.width() && a.height() == b.height() && a.data().size() == b.data().size() &&
         std::equal(a.data().begin(), a.data().end(), b.data().begin());
}

// What the block looks like with both tiles and with neither.
struct ReferenceRenders {
  patchy::PixelBuffer full;
  patchy::PixelBuffer plain;
};

enum class Through { Memory, Psd };

// `through` Psd gives the picture of a document that was saved and read back, which is
// what a test that opens a file compares with.
ReferenceRenders reference_renders(Through through, std::optional<patchy::PatternResource> stripes = std::nullopt,
                                   std::optional<patchy::PatternResource> grain = std::nullopt,
                                   std::string_view overlay_id = kStripesId,
                                   std::string_view texture_id = kGrainId) {
  const auto picture = [through](const patchy::Document& document) {
    if (through == Through::Memory) {
      return flatten(document);
    }
    return flatten(patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(document), {}));
  };
  auto document = styled_document(overlay_id, texture_id);
  ReferenceRenders renders;
  renders.plain = picture(document);
  auto with_stripes = document;
  with_stripes.metadata().patterns.adopt(stripes.value_or(resource(overlay_id, "Stripes", stripes_tile())));
  const auto stripes_only = picture(with_stripes);
  auto with_both = with_stripes;
  with_both.metadata().patterns.adopt(grain.value_or(resource(texture_id, "Grain", grain_tile())));
  renders.full = picture(with_both);
  // The fixture is only worth anything when each tile changes the picture.
  CHECK(!same_pixels(renders.plain, stripes_only));
  CHECK(!same_pixels(stripes_only, renders.full));
  return renders;
}

// The canvas picture against a flatten, a level or two apart at most.
bool canvas_shows(patchy::ui::CanvasWidget& canvas, const patchy::PixelBuffer& expected) {
  canvas.force_refresh();
  QApplication::processEvents();
  if (!process_events_until([&] { return !canvas.current_composite().isNull(); })) {
    return false;
  }
  const auto shown = canvas.current_composite().convertToFormat(QImage::Format_RGB888);
  if (shown.width() != expected.width() || shown.height() != expected.height()) {
    return false;
  }
  for (int y = 0; y < shown.height(); ++y) {
    const auto* row = shown.constScanLine(y);
    for (int x = 0; x < shown.width(); ++x) {
      const auto* want = expected.pixel(x, y);
      for (int channel = 0; channel < 3; ++channel) {
        if (std::abs(static_cast<int>(row[x * 3 + channel]) - static_cast<int>(want[channel])) > 2) {
          return false;
        }
      }
    }
  }
  return true;
}

const patchy::Layer& styled_layer(const patchy::Document& document) {
  for (const auto& layer : document.layers()) {
    if (layer.name() == "Styled") {
      return layer;
    }
  }
  throw std::runtime_error("the Styled layer is missing");
}

QString scratch_path(const QString& name) {
  ensure_artifact_dir();
  return QDir(QStringLiteral("test-artifacts")).absoluteFilePath(name);
}

QString write_file(const QString& name, const std::vector<std::uint8_t>& bytes) {
  const auto path = scratch_path(name);
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  CHECK(file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) ==
        static_cast<qint64>(bytes.size()));
  file.close();
  return path;
}

// A PSD of the styled document that holds both tiles, as Photoshop writes one.
QString write_psd_with_tiles(const QString& name, const patchy::PatternResource& overlay,
                             const patchy::PatternResource& texture) {
  auto document = styled_document(overlay.id, texture.id);
  document.metadata().patterns.adopt(overlay);
  document.metadata().patterns.adopt(texture);
  return write_file(name, patchy::psd::DocumentIo::write_layered_rgb8(document));
}

// The same PSD as an editor that embeds no tile for the two ids would write it: the tiles
// are saved under the two ids, then the pattern block's copies of the ids get another last
// character. A pattern block stores its id as plain bytes and a style descriptor as UTF-16,
// so only the block changes, and the style asks for two ids nothing in the file answers.
QString write_psd_without_tiles(const QString& name, const patchy::PatternResource& overlay,
                                const patchy::PatternResource& texture) {
  auto document = styled_document(overlay.id, texture.id);
  document.metadata().patterns.adopt(overlay);
  document.metadata().patterns.adopt(texture);
  auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  const auto hide = [&bytes](const std::string& id) {
    auto hidden = id;
    hidden.back() = hidden.back() == 'f' ? 'e' : 'f';
    int renamed = 0;
    auto at = bytes.begin();
    while ((at = std::search(at, bytes.end(), id.begin(), id.end())) != bytes.end()) {
      at = std::copy(hidden.begin(), hidden.end(), at);
      ++renamed;
    }
    CHECK(renamed == 1);
  };
  hide(overlay.id);
  hide(texture.id);
  const auto path = write_file(name, bytes);
  const auto reread = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
  CHECK(reread.metadata().patterns.find(overlay.id) == nullptr);
  CHECK(reread.metadata().patterns.find(texture.id) == nullptr);
  const auto& styled = styled_layer(reread);
  CHECK(styled.layer_style().pattern_overlays.size() == 1U && styled.layer_style().bevels.size() == 1U);
  CHECK(styled.layer_style().pattern_overlays.front().pattern_id == overlay.id);
  CHECK(styled.layer_style().bevels.front().texture.pattern_id == texture.id);
  return path;
}

QString write_psd_without_tiles(const QString& name) {
  return write_psd_without_tiles(name, resource(kStripesId, "Stripes", stripes_tile()),
                                 resource(kGrainId, "Grain", grain_tile()));
}

void add_library_patterns(patchy::ui::MainWindow& window) {
  auto& library = window.pattern_library();
  CHECK(!library.add_pattern(QStringLiteral("Stripes"), stripes_tile(), QStringLiteral("755"),
                             QString::fromLatin1(kStripesId.data(), static_cast<qsizetype>(kStripesId.size())))
             .isEmpty());
  CHECK(!library.add_pattern(QStringLiteral("Grain"), grain_tile(), QStringLiteral("755"),
                             QString::fromLatin1(kGrainId.data(), static_cast<qsizetype>(kGrainId.size())))
             .isEmpty());
}

bool has_tile(const patchy::Document& document, std::string_view id) {
  const auto* found = document.metadata().patterns.find(id);
  return found != nullptr && !patchy::pattern_tile_is_unrenderable(found->tile);
}

// (a) A PSD whose Pattern Overlay and Bevel Texture ids only the library has: the canvas
// shows both as soon as the file is open, and opening changed nothing the user must save.
void ui_layer_style_library_patterns_show_when_a_document_opens() {
  clear_pattern_test_state();
  const auto cleanup = qScopeGuard([] { clear_pattern_test_state(); });
  const auto renders = reference_renders(Through::Psd);
  const auto path = write_psd_without_tiles(QStringLiteral("issue755_library_patterns.psd"));

  patchy::ui::MainWindow window;
  show_window(window);
  add_library_patterns(window);
  MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  CHECK(document.width() == kWidth);
  save_image_artifact("issue755_canvas_after_open", [&] {
    canvas->force_refresh();
    QApplication::processEvents();
    return canvas->current_composite();
  }());
  CHECK(has_tile(document, kStripesId));
  CHECK(has_tile(document, kGrainId));
  CHECK(same_pixels(flatten(document), renders.full));
  CHECK(canvas_shows(*canvas, renders.full));
  CHECK(!MainWindowTestAccess::active_session_is_modified(window));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 0U);
  // The tiles count as Patchy's own, so a save writes them.
  CHECK(document.metadata().patterns.find(kStripesId)->provenance == patchy::PatternProvenance::Authored);

  // A file that holds its own tiles keeps them, whatever the library has under those ids.
  const auto own_stripes =
      resource(kStripesId, "Stripes", solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(20, 160, 90)));
  const auto own_grain = resource(kGrainId, "Grain", grain_tile());
  const auto own = reference_renders(Through::Psd, own_stripes, own_grain);
  MainWindowTestAccess::open_document_path(
      window, write_psd_with_tiles(QStringLiteral("issue755_own_patterns.psd"), own_stripes, own_grain));
  QApplication::processEvents();
  const auto& with_own = std::as_const(MainWindowTestAccess::document(window));
  CHECK(with_own.metadata().patterns.find(kStripesId)->provenance == patchy::PatternProvenance::ImportedRaw);
  CHECK(same_pixels(with_own.metadata().patterns.find(kStripesId)->tile, own_stripes.tile));
  CHECK(same_pixels(flatten(with_own), own.full));
  CHECK(!same_pixels(own.full, renders.full));
  // The pair the review steps use: this file with the tiles, and the one above without.
  (void)write_psd_with_tiles(QStringLiteral("issue755_review_stripes_source.psd"),
                             resource(kStripesId, "755 Stripes", stripes_tile()),
                             resource(kGrainId, "755 Grain", grain_tile()));
}

// (b) The same for ids of the bundled presets, with an empty pattern library: one of the
// generated patterns and one of the photo textures.
void ui_layer_style_bundled_patterns_show_when_a_document_opens() {
  clear_pattern_test_state();
  const auto cleanup = qScopeGuard([] { clear_pattern_test_state(); });
  const std::string overlay_id = patchy::builtin_pattern_presets().front().id;
  const std::string texture_id = patchy::photo_pattern_presets().front().id;
  const auto overlay = patchy::ui::bundled_pattern_resource(overlay_id);
  const auto texture = patchy::ui::bundled_pattern_resource(texture_id);
  CHECK(overlay.has_value() && texture.has_value());
  const auto renders = reference_renders(Through::Psd, overlay, texture, overlay_id, texture_id);
  const auto path = write_psd_without_tiles(QStringLiteral("issue755_bundled_patterns.psd"), *overlay, *texture);

  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(window.pattern_library().entries().empty());
  MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  CHECK(has_tile(document, overlay_id));
  CHECK(has_tile(document, texture_id));
  CHECK(same_pixels(flatten(document), renders.full));
  CHECK(canvas_shows(*canvas, renders.full));
  CHECK(!MainWindowTestAccess::active_session_is_modified(window));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 0U);
}

// (c) Layer Style closed with OK and with Cancel, nothing edited: the canvas keeps the
// pattern and the texture, and neither adds a history step.
void ui_layer_style_ok_without_edits_keeps_library_patterns() {
  clear_pattern_test_state();
  const auto cleanup = qScopeGuard([] { clear_pattern_test_state(); });
  const auto renders = reference_renders(Through::Psd);
  const auto path = write_psd_without_tiles(QStringLiteral("issue755_ok_without_edits.psd"));

  patchy::ui::MainWindow window;
  show_window(window);
  add_library_patterns(window);
  MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  const auto& document = std::as_const(MainWindowTestAccess::document(window));

  for (const bool accept : {true, false}) {
    bool drove = false;
    when_dialog_visible(QStringLiteral("patchyLayerStyleDialog"), [&](QDialog& dialog) {
      // Both effects are presented as found: no warning, Texture ticked.
      CHECK(dialog.findChild<QLabel*>(QStringLiteral("layerStyleMissingPatternWarning")) == nullptr);
      auto* texture_check = dialog.findChild<QCheckBox*>(QStringLiteral("layerStyleBevelTextureCategoryCheck"));
      CHECK(texture_check != nullptr && texture_check->isChecked());
      QApplication::processEvents();
      CHECK(canvas_shows(*canvas, renders.full));
      drove = true;
      if (accept) {
        dialog.accept();
      } else {
        dialog.reject();
      }
    });
    require_action(window, "layerBlendingOptionsAction")->trigger();
    QApplication::processEvents();
    CHECK(drove);
    CHECK(has_tile(document, kStripesId));
    CHECK(has_tile(document, kGrainId));
    CHECK(same_pixels(flatten(document), renders.full));
    CHECK(canvas_shows(*canvas, renders.full));
    CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 0U);
    CHECK(!MainWindowTestAccess::active_session_is_modified(window));
  }
  save_widget_artifact("issue755_after_ok_without_edits", window);
}

// A pattern that reaches the library while the document is open shows at once, and an undo
// back to a state from before it arrived keeps it.
void ui_layer_style_pattern_added_to_the_library_shows_in_open_documents() {
  clear_pattern_test_state();
  const auto cleanup = qScopeGuard([] { clear_pattern_test_state(); });
  const auto renders = reference_renders(Through::Memory);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(styled_document(), QStringLiteral("Library Arrives Later"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  auto& document = MainWindowTestAccess::document(window);
  CHECK(same_pixels(flatten(std::as_const(document)), renders.plain));
  CHECK(canvas_shows(*canvas, renders.plain));

  // A history state from before the patterns exist anywhere.
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  layer_list->setCurrentItem(require_layer_item(*layer_list, QStringLiteral("Styled")));
  QApplication::processEvents();
  require_action(window, "layerDuplicateAction")->trigger();
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 1U);

  add_library_patterns(window);
  QApplication::processEvents();
  CHECK(has_tile(std::as_const(document), kStripesId));
  CHECK(has_tile(std::as_const(document), kGrainId));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 1U);

  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(has_tile(std::as_const(document), kStripesId));
  CHECK(same_pixels(flatten(std::as_const(document)), renders.full));
  CHECK(canvas_shows(*canvas, renders.full));
}

// The library gains the patterns while Layer Style is open (its Pattern Manager can import
// them). The dialog owns the document's pattern store until it closes, so the tiles arrive
// when it does, here after Cancel, and without a history step.
void ui_layer_style_pattern_added_under_the_dialog_shows_when_it_closes() {
  clear_pattern_test_state();
  const auto cleanup = qScopeGuard([] { clear_pattern_test_state(); });
  const auto renders = reference_renders(Through::Memory);

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(styled_document(), QStringLiteral("Library Changes Under The Dialog"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  CHECK(canvas_shows(*canvas, renders.plain));

  bool drove = false;
  when_dialog_visible(QStringLiteral("patchyLayerStyleDialog"), [&](QDialog& dialog) {
    add_library_patterns(window);
    QApplication::processEvents();
    CHECK(!has_tile(document, kStripesId));  // not while the dialog holds the document
    drove = true;
    dialog.reject();
  });
  require_action(window, "layerBlendingOptionsAction")->trigger();
  CHECK(drove);
  CHECK(has_tile(document, kStripesId));
  CHECK(has_tile(document, kGrainId));
  QApplication::processEvents();
  CHECK(same_pixels(flatten(document), renders.full));
  CHECK(canvas_shows(*canvas, renders.full));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 0U);
}

// (d) A save embeds the tiles: the file renders the same where no library has them. Also a
// file saved while the tiles were missing, which carries Patchy's one pixel placeholder under
// each id: it shows the library's tiles when opened, and its next save replaces the placeholders.
void ui_layer_style_library_patterns_are_embedded_on_save() {
  clear_pattern_test_state();
  const auto cleanup = qScopeGuard([] { clear_pattern_test_state(); });
  const auto renders = reference_renders(Through::Psd);
  const auto without_tiles = write_psd_without_tiles(QStringLiteral("issue755_save_input.psd"));
  const auto with_placeholders = scratch_path(QStringLiteral("issue755_placeholder_input.psd"));
  patchy::psd::DocumentIo::write_layered_rgb8_file(styled_document(),
                                                   patchy::ui::to_filesystem_path(with_placeholders));
  {
    const auto reread = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(with_placeholders));
    const auto* placeholder = reread.metadata().patterns.find(kStripesId);
    CHECK(placeholder != nullptr && patchy::pattern_tile_is_unrenderable(placeholder->tile));
  }

  patchy::ui::MainWindow window;
  show_window(window);
  add_library_patterns(window);
  int index = 0;
  for (const auto& input : {without_tiles, with_placeholders}) {
    MainWindowTestAccess::open_document_path(window, input);
    QApplication::processEvents();
    const auto& document = std::as_const(MainWindowTestAccess::document(window));
    CHECK(same_pixels(flatten(document), renders.full));
    CHECK(canvas_shows(*require_canvas(window), renders.full));
    CHECK(!MainWindowTestAccess::active_session_is_modified(window));
    const auto saved = scratch_path(QStringLiteral("issue755_saved_%1.psd").arg(index++));
    CHECK(MainWindowTestAccess::save_document_to_path(window, saved));
    // Read with no library at all: what Photoshop and a machine without the patterns get.
    const auto reread = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(saved));
    CHECK(has_tile(reread, kStripesId));
    CHECK(has_tile(reread, kGrainId));
    CHECK(same_pixels(flatten(reread), renders.full));
  }
}

// (e) An id that is in no document, library or bundle draws nothing, as before, and the
// dialog still names it.
void ui_layer_style_pattern_found_nowhere_stays_missing() {
  clear_pattern_test_state();
  const auto cleanup = qScopeGuard([] { clear_pattern_test_state(); });
  auto source = styled_document(kNowhereId, kNowhereId);
  const auto plain = flatten(source);

  patchy::ui::MainWindow window;
  show_window(window);
  add_library_patterns(window);  // other patterns: none answers the id
  window.add_document_session(std::move(source), QStringLiteral("Pattern Found Nowhere"));
  QApplication::processEvents();
  auto* canvas = require_canvas(window);
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  CHECK(document.metadata().patterns.find(kNowhereId) == nullptr);
  CHECK(same_pixels(flatten(document), plain));
  CHECK(canvas_shows(*canvas, plain));
  CHECK(!MainWindowTestAccess::active_session_is_modified(window));

  bool drove = false;
  when_dialog_visible(QStringLiteral("patchyLayerStyleDialog"), [&](QDialog& dialog) {
    auto* warning = dialog.findChild<QLabel*>(QStringLiteral("layerStyleMissingPatternWarning"));
    CHECK(warning != nullptr && warning->isVisible());
    CHECK(warning != nullptr && warning->text().contains(QStringLiteral("Stripes")));
    // Texture with a pattern nobody has is presented unticked.
    auto* texture_check = dialog.findChild<QCheckBox*>(QStringLiteral("layerStyleBevelTextureCategoryCheck"));
    CHECK(texture_check != nullptr && !texture_check->isChecked());
    drove = true;
    dialog.reject();
  });
  require_action(window, "layerBlendingOptionsAction")->trigger();
  QApplication::processEvents();
  CHECK(drove);
  CHECK(document.metadata().patterns.find(kNowhereId) == nullptr);
  CHECK(same_pixels(flatten(document), plain));
}

// A shape whose pattern fill names a tile only the library has is drawn with it too.
void ui_shape_pattern_fill_takes_the_library_tile_when_a_document_opens() {
  clear_pattern_test_state();
  const auto cleanup = qScopeGuard([] { clear_pattern_test_state(); });
  const auto shape_document = [] {
    patchy::Document document(kWidth, kHeight, patchy::PixelFormat::rgb8());
    document.add_pixel_layer("Background",
                             solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
    patchy::LiveShapeParams params;
    params.kind = patchy::LiveShapeKind::Rectangle;
    params.left = kBlock.left();
    params.top = kBlock.top();
    params.right = kBlock.left() + kBlock.width();
    params.bottom = kBlock.top() + kBlock.height();
    params.index = 1;
    patchy::populate_live_shape_box_corners(params);
    patchy::VectorShapeContent content;
    content.path.subpaths = patchy::generate_live_shape_subpaths(params);
    content.origination.push_back(params);
    content.fill.kind = patchy::VectorFillKind::Pattern;
    content.fill.pattern_id = std::string(kStripesId);
    content.fill.pattern_name = "Stripes";
    patchy::Layer layer(document.allocate_layer_id(), "Shape", patchy::PixelBuffer{});
    layer.set_vector_shape(std::move(content));
    layer.metadata()[patchy::kLayerMetadataVectorShape] = "1";
    auto& added = document.add_layer(std::move(layer));
    patchy::update_vector_shape_raster(added, patchy::Rect::from_size(kWidth, kHeight),
                                       &document.metadata().patterns);
    return document;
  };
  auto reference = shape_document();
  const auto plain = flatten(reference);
  reference.metadata().patterns.adopt(resource(kStripesId, "Stripes", stripes_tile()));
  for (auto& layer : reference.layers()) {
    if (std::as_const(layer).vector_shape() != nullptr) {
      patchy::update_vector_shape_raster(layer, patchy::Rect::from_size(kWidth, kHeight),
                                         &reference.metadata().patterns);
    }
  }
  const auto full = flatten(reference);
  CHECK(!same_pixels(plain, full));

  patchy::ui::MainWindow window;
  show_window(window);
  add_library_patterns(window);
  window.add_document_session(shape_document(), QStringLiteral("Shape Pattern Fill"));
  QApplication::processEvents();
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  CHECK(has_tile(document, kStripesId));
  CHECK(same_pixels(flatten(document), full));
  CHECK(canvas_shows(*require_canvas(window), full));
  CHECK(!MainWindowTestAccess::active_session_is_modified(window));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == 0U);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_style_library_pattern_tests() {
  return {
      {"ui_layer_style_library_patterns_show_when_a_document_opens",
       ui_layer_style_library_patterns_show_when_a_document_opens},
      {"ui_layer_style_bundled_patterns_show_when_a_document_opens",
       ui_layer_style_bundled_patterns_show_when_a_document_opens},
      {"ui_layer_style_ok_without_edits_keeps_library_patterns",
       ui_layer_style_ok_without_edits_keeps_library_patterns},
      {"ui_layer_style_pattern_added_to_the_library_shows_in_open_documents",
       ui_layer_style_pattern_added_to_the_library_shows_in_open_documents},
      {"ui_layer_style_pattern_added_under_the_dialog_shows_when_it_closes",
       ui_layer_style_pattern_added_under_the_dialog_shows_when_it_closes},
      {"ui_layer_style_library_patterns_are_embedded_on_save",
       ui_layer_style_library_patterns_are_embedded_on_save},
      {"ui_layer_style_pattern_found_nowhere_stays_missing", ui_layer_style_pattern_found_nowhere_stays_missing},
      {"ui_shape_pattern_fill_takes_the_library_tile_when_a_document_opens",
       ui_shape_pattern_fill_takes_the_library_tile_when_a_document_opens},
  };
}
