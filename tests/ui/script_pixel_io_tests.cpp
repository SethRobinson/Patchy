// Pixel exchange for scripts and companion apps (src/ui/script_pixel_io.cpp,
// docs/companions.md): selection masks, raster layer masks, the composite read and
// patchy.io image files.
#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/pixel_depth.hpp"
#include "formats/document_flatten.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "test_scratch_remove.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QStringList>

#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace {
using namespace patchy::test::ui;
using namespace patchy::test::ui::scripting_support;

// doc.selection.getMask reads hard and soft coverage, setMask writes it back and combines
// with the current selection, and bad input throws.
void ui_script_selection_mask_reads_and_combines() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var sel = app.activeDocument.selection;
    console.log('none=' + (sel.getMask() === null));
    sel.selectRect(10, 10, 20, 10);
    var m = sel.getMask();
    var v = new Uint8Array(m.data);
    var full = v.every(function (b) { return b === 255; });
    console.log('bounds=' + m.x + ',' + m.y + ',' + m.width + 'x' + m.height + ' full=' + full);
    var area = sel.getMask({x: 0, y: 0, width: 40, height: 40});
    var a = new Uint8Array(area.data);
    console.log('area=' + area.width + 'x' + area.height + ' out=' + a[5 * 40 + 5] + ' in=' + a[15 * 40 + 15]);

    var ramp = new Uint8Array([0, 85, 170, 255]);
    sel.setMask({x: 50, y: 50, width: 4, height: 1, data: ramp.buffer});
    var back = new Uint8Array(sel.getMask({x: 50, y: 50, width: 4, height: 1}).data);
    console.log('ramp=' + Array.prototype.join.call(back, ','));
    var b = sel.bounds;
    console.log('replaced=' + b.x + ',' + b.y + ',' + b.width + 'x' + b.height);

    sel.selectRect(0, 0, 10, 10);
    var square = new Uint8Array(100).fill(255);
    sel.setMask({x: 20, y: 0, width: 10, height: 10, data: square.buffer}, {operation: 'add'});
    b = sel.bounds;
    console.log('added=' + b.x + ',' + b.y + ',' + b.width + 'x' + b.height);
    sel.setMask({x: 0, y: 0, width: 10, height: 10, data: square.buffer}, {operation: 'subtract'});
    b = sel.bounds;
    console.log('subtracted=' + b.x + ',' + b.y + ',' + b.width + 'x' + b.height);
    var half = new Uint8Array(100).fill(128);
    sel.setMask({x: 20, y: 0, width: 10, height: 10, data: half.buffer}, {operation: 'intersect'});
    console.log('intersected=' + new Uint8Array(sel.getMask({x: 25, y: 5, width: 1, height: 1}).data)[0]);
    sel.setMask({x: 0, y: 0, width: 10, height: 10, data: new Uint8Array(100).buffer});
    console.log('cleared=' + sel.exists);

    var errors = [];
    [function () { sel.setMask({width: 2, height: 2, data: new Uint8Array(3).buffer}); },
     function () { sel.setMask({width: 1, height: 1, data: new Uint8Array(1).buffer}, {operation: 'xor'}); },
     function () { sel.setMask({width: 1, height: 1, data: new Uint8Array(1).buffer, z: 1}); },
     function () { sel.getMask({x: 5000, y: 5000, width: 4, height: 4}); }].forEach(function (f) {
      try { f(); errors.push('no-throw'); } catch (e) { errors.push(e.message.length > 0 ? 'thrown' : 'empty'); }
    });
    console.log('errors=' + errors.join(','));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("none=true")));
  CHECK(backlog_contains(window, QStringLiteral("bounds=10,10,20x10 full=true")));
  CHECK(backlog_contains(window, QStringLiteral("area=40x40 out=0 in=255")));
  CHECK(backlog_contains(window, QStringLiteral("ramp=0,85,170,255")));
  CHECK(backlog_contains(window, QStringLiteral("replaced=51,50,3x1")));
  CHECK(backlog_contains(window, QStringLiteral("added=0,0,30x10")));
  CHECK(backlog_contains(window, QStringLiteral("subtracted=20,0,10x10")));
  CHECK(backlog_contains(window, QStringLiteral("intersected=128")));
  CHECK(backlog_contains(window, QStringLiteral("cleared=false")));
  CHECK(backlog_contains(window, QStringLiteral("errors=thrown,thrown,thrown,thrown")));
}

// layer.setLayerMask from the selection and from a coverage block, getLayerMask's fields,
// removeLayerMask, and the masks landing in the document model.
void ui_script_layer_mask_set_get_and_remove() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Masked');
    layer.fillRect(0, 0, 64, 64, '#ff0000');
    console.log('initial=' + (layer.getLayerMask() === null) + ',' + layer.removeLayerMask());
    doc.selection.selectRect(8, 8, 16, 16);
    layer.setLayerMask({fromSelection: true, feather: 3, density: 50});
    var m = layer.getLayerMask();
    var v = new Uint8Array(m.data);
    console.log('fromSelection=' + m.x + ',' + m.y + ',' + m.width + 'x' + m.height + ' ' + v[0] +
                ' default=' + m.defaultColor + ' feather=' + m.feather + ' density=' + Math.round(m.density) +
                ' disabled=' + m.disabled);
    layer.setLayerMask({fromSelection: true, invert: true});
    m = layer.getLayerMask();
    console.log('inverted=' + new Uint8Array(m.data)[0] + ' default=' + m.defaultColor);

    var block = new Uint8Array([0, 64, 128, 255, 255, 128, 64, 0]);
    layer.setLayerMask({x: 30, y: 40, width: 4, height: 2, data: block.buffer});
    m = layer.getLayerMask();
    console.log('block=' + m.x + ',' + m.y + ',' + m.width + 'x' + m.height + ' ' +
                Array.prototype.join.call(new Uint8Array(m.data), ',') + ' default=' + m.defaultColor);

    var errors = [];
    doc.selection.deselect();
    [function () { layer.setLayerMask({fromSelection: true}); },
     function () { layer.setLayerMask({width: 1, height: 1, data: new Uint8Array(1).buffer, defaultColor: 7}); },
     function () { layer.setLayerMask({width: 1, height: 1, data: new Uint8Array(1).buffer, density: 101}); },
     function () { layer.setLayerMask({width: 1, height: 1, data: new Uint8Array(1).buffer, blur: 1}); },
     function () { layer.setLayerMask('mask'); }].forEach(function (f) {
      try { f(); errors.push('no-throw'); } catch (e) { errors.push(e.message.length > 0 ? 'thrown' : 'empty'); }
    });
    console.log('errors=' + errors.join(','));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("initial=true,false")));
  CHECK(backlog_contains(window,
                         QStringLiteral("fromSelection=8,8,16x16 255 default=0 feather=3 density=50 disabled=false")));
  CHECK(backlog_contains(window, QStringLiteral("inverted=0 default=255")));
  CHECK(backlog_contains(window, QStringLiteral("block=30,40,4x2 0,64,128,255,255,128,64,0 default=0")));
  CHECK(backlog_contains(window, QStringLiteral("errors=thrown,thrown,thrown,thrown,thrown")));
  const auto* layer = layer_named(patchy::ui::MainWindowTestAccess::document(window), "Masked");
  CHECK(layer != nullptr && layer->mask().has_value());
  CHECK(layer->mask()->bounds.x == 30 && layer->mask()->bounds.y == 40);
  CHECK(layer->mask()->pixels.pixel(3, 0)[0] == 255);

  CHECK(run_script(window, QStringLiteral(R"JS(
    var layer = app.activeDocument.findLayer('Masked');
    console.log('removed=' + layer.removeLayerMask() + ',' + (layer.getLayerMask() === null));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("removed=true,true")));
  layer = layer_named(patchy::ui::MainWindowTestAccess::document(window), "Masked");
  CHECK(layer != nullptr && !layer->mask().has_value());
}

// The companion insert recipe: read the composite and the selection, write a result
// layer with a feathered mask from the selection. One run is one undo step, and undo
// leaves the document as it was.
void ui_script_layer_mask_companion_insert_is_one_undo_step() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto layers_before = patchy::ui::MainWindowTestAccess::document(window).layers().size();
  CHECK(run_script(window, QStringLiteral(R"JS(
    app.activeDocument.selection.selectEllipse(100, 100, 80, 60);
  )JS")));
  const auto depth_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var rect = doc.selection.bounds;
    var image = doc.getCompositePixels(rect);
    var px = new Uint8Array(image.data);
    for (var i = 0; i < px.length; i += 4) { px[i] = 255 - px[i]; px[i + 1] = 255 - px[i + 1]; px[i + 2] = 255 - px[i + 2]; }
    var result = doc.addLayer('Generative Fill: test');
    result.setPixels(image);
    result.setLayerMask({fromSelection: true, feather: 2});
    console.log('inserted=' + (result.bounds.width === rect.width && result.bounds.height === rect.height));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("inserted=true")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth_before + 1);
  CHECK(patchy::ui::MainWindowTestAccess::document(window).layers().size() == layers_before + 1);
  patchy::ui::MainWindowTestAccess::undo(window);
  CHECK(patchy::ui::MainWindowTestAccess::document(window).layers().size() == layers_before);
  CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window), "Generative Fill: test") == nullptr);
}

// doc.getCompositePixels returns the flattened document's pixels for a rect, clipped to
// the canvas, and works on a 16-bit document.
void ui_script_composite_pixels_match_flatten() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Top');
    layer.fillRect(0, 0, 40, 40, '#3366cc');
    layer.opacity = 50;
    var c = doc.getCompositePixels({x: 30, y: 30, width: 20, height: 10});
    var v = new Uint8Array(c.data);
    console.log('rect=' + c.x + ',' + c.y + ',' + c.width + 'x' + c.height + ' bytes=' + v.length);
    console.log('px=' + v[0] + ',' + v[1] + ',' + v[2] + ',' + v[3] + ' far=' + v[15 * 4] + ',' + v[15 * 4 + 1]);
    var clipped = doc.getCompositePixels({x: doc.width - 5, y: doc.height - 5, width: 50, height: 50});
    console.log('clipped=' + clipped.width + 'x' + clipped.height);
    var whole = doc.getCompositePixels();
    console.log('whole=' + (whole.width === doc.width && whole.height === doc.height));
    try { doc.getCompositePixels({x: -100, y: 0, width: 10, height: 10}); console.log('outside-no-throw'); }
    catch (e) { console.log('outside=thrown'); }
  )JS")));
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto expected = patchy::flatten_document_rgba8_rect(document, patchy::Rect{30, 30, 20, 10});
  const auto* first = expected.pixel(0, 0);
  const auto* far = expected.pixel(15, 0);
  CHECK(backlog_contains(window, QStringLiteral("rect=30,30,20x10 bytes=800")));
  CHECK(backlog_contains(window, QStringLiteral("px=%1,%2,%3,%4 far=%5,%6")
                                     .arg(int{first[0]})
                                     .arg(int{first[1]})
                                     .arg(int{first[2]})
                                     .arg(int{first[3]})
                                     .arg(int{far[0]})
                                     .arg(int{far[1]})));
  CHECK(backlog_contains(window, QStringLiteral("clipped=5x5")));
  CHECK(backlog_contains(window, QStringLiteral("whole=true")));
  CHECK(backlog_contains(window, QStringLiteral("outside=thrown")));

  patchy::set_deep_editing_override(true);
  const auto restore_deep_editing = qScopeGuard([] { patchy::set_deep_editing_override(std::nullopt); });
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    doc.convertBitDepth(16);
    var c = doc.getCompositePixels({x: 0, y: 0, width: 2, height: 2});
    console.log('deep=' + doc.bitDepth + ' ' + new Uint8Array(c.data).length);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("deep=16 16")));
}

// patchy.io.writeImage and readImage round-trip RGBA and grayscale blocks through a
// Unicode, special-character path; unsupported formats and missing files throw.
void ui_script_io_image_round_trips_unicode_path() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::test::ui::ensure_artifact_dir();
  const auto dir = QFileInfo(QStringLiteral("test-artifacts")).absoluteFilePath() + QLatin1Char('/') +
                   QString::fromUtf8(reinterpret_cast<const char*>(patchy::test::kUnicodeDirName.data()),
                                     static_cast<qsizetype>(patchy::test::kUnicodeDirName.size())) +
                   QStringLiteral("/script-image-io");
  remove_test_scratch_dir(dir);
  CHECK(QDir().mkpath(dir));
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("image-io");
  options.args = QStringList{QStringLiteral("dir=") + dir};
  const auto source = QStringLiteral(R"JS(
    var dir = patchy.args.dir;
    var rgbaPath = dir + '/せす café 🎨 #1 50% [x] (v2).png';
    var rgba = new Uint8Array([255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 255, 10, 20, 30, 40]);
    patchy.io.writeImage(rgbaPath, {x: 7, y: 9, width: 2, height: 2, data: rgba.buffer});
    var back = patchy.io.readImage(rgbaPath);
    console.log('rgba=' + back.x + ',' + back.y + ',' + back.width + 'x' + back.height + ' ' +
                Array.prototype.join.call(new Uint8Array(back.data), ','));
    var maskPath = dir + '/マスク mask.png';
    patchy.io.writeImage(maskPath, {width: 3, height: 1, data: new Uint8Array([0, 128, 255]).buffer});
    var mask = new Uint8Array(patchy.io.readImage(maskPath).data);
    console.log('gray=' + mask[0] + ',' + mask[4] + ',' + mask[5] + ',' + mask[7] + ',' + mask[8] + ',' + mask[11]);
    var errors = [];
    [function () { patchy.io.writeImage(dir + '/x.notaformat', {width: 1, height: 1, data: new Uint8Array(4).buffer}); },
     function () { patchy.io.writeImage(dir + '/y.png', {width: 2, height: 2, data: new Uint8Array(5).buffer}); },
     function () { patchy.io.readImage(dir + '/missing.png'); }].forEach(function (f) {
      try { f(); errors.push('no-throw'); } catch (e) { errors.push(e.message.length > 0 ? 'thrown' : 'empty'); }
    });
    console.log('errors=' + errors.join(','));
  )JS");
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("rgba=0,0,2x2 255,0,0,255,0,255,0,128,0,0,255,255,10,20,30,40")));
  CHECK(backlog_contains(window, QStringLiteral("gray=0,128,128,255,255,255")));
  CHECK(backlog_contains(window, QStringLiteral("errors=thrown,thrown,thrown")));
  remove_test_scratch_dir(dir);
}

}  // namespace

std::vector<patchy::test::TestCase> script_pixel_io_tests() {
  return {
      {"ui_script_selection_mask_reads_and_combines", ui_script_selection_mask_reads_and_combines},
      {"ui_script_layer_mask_set_get_and_remove", ui_script_layer_mask_set_get_and_remove},
      {"ui_script_layer_mask_companion_insert_is_one_undo_step",
       ui_script_layer_mask_companion_insert_is_one_undo_step},
      {"ui_script_composite_pixels_match_flatten", ui_script_composite_pixels_match_flatten},
      {"ui_script_io_image_round_trips_unicode_path", ui_script_io_image_round_trips_unicode_path},
  };
}
