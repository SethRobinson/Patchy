// Pixel exchange for scripts and companion apps (src/ui/script_pixel_io.cpp,
// docs/companions.md): selection masks, raster layer masks, the composite read and
// patchy.io image files.
#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
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
#include <iostream>
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
    var plane = patchy.io.readImage(maskPath, {grayscale: true});
    console.log('plane=' + plane.width + 'x' + plane.height + ' ' + Array.prototype.join.call(new Uint8Array(plane.data), ','));
    var errors = [];
    [function () { patchy.io.writeImage(dir + '/x.notaformat', {width: 1, height: 1, data: new Uint8Array(4).buffer}); },
     function () { patchy.io.writeImage(dir + '/y.png', {width: 2, height: 2, data: new Uint8Array(5).buffer}); },
     function () { patchy.io.readImage(dir + '/missing.png'); },
     function () { patchy.io.readImage(maskPath, {gray: true}); }].forEach(function (f) {
      try { f(); errors.push('no-throw'); } catch (e) { errors.push(e.message.length > 0 ? 'thrown' : 'empty'); }
    });
    console.log('errors=' + errors.join(','));
  )JS");
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("rgba=0,0,2x2 255,0,0,255,0,255,0,128,0,0,255,255,10,20,30,40")));
  CHECK(backlog_contains(window, QStringLiteral("gray=0,128,128,255,255,255")));
  CHECK(backlog_contains(window, QStringLiteral("plane=3x1 0,128,255")));
  CHECK(backlog_contains(window, QStringLiteral("errors=thrown,thrown,thrown,thrown")));
  remove_test_scratch_dir(dir);
}

// doc.resizeCanvas with an offset or an anchor moves the content and the selection and fills
// the Background; without options only the size changes; bad options throw.
void ui_script_resize_canvas_anchor_and_offset() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.newDocument(40, 30);
    var layer = doc.addLayer('Content');
    layer.fillRect(0, 0, 10, 10, '#ff0000');
    doc.selection.selectRect(2, 3, 4, 5);
    doc.resizeCanvas(70, 50, {x: 30, y: 20, fill: '#00ff00'});
    var b = doc.selection.bounds;
    var corner = new Uint8Array(doc.getCompositePixels({x: 0, y: 0, width: 1, height: 1}).data);
    var moved = new Uint8Array(doc.getCompositePixels({x: 30, y: 20, width: 1, height: 1}).data);
    console.log('offset=' + doc.width + 'x' + doc.height + ' layer=' + layer.x + ',' + layer.y +
                ' sel=' + b.x + ',' + b.y + ',' + b.width + 'x' + b.height +
                ' corner=' + corner[0] + ',' + corner[1] + ',' + corner[2] + ' moved=' + moved[0] + ',' + moved[1]);
    doc.resizeCanvas(80, 60, {anchor: 'center'});
    console.log('center=' + doc.width + 'x' + doc.height + ' layer=' + layer.x + ',' + layer.y);
    doc.resizeCanvas(100, 60, {anchor: 'bottomRight'});
    console.log('bottomRight=' + layer.x + ',' + layer.y);
    doc.resizeCanvas(120, 70);
    console.log('plain=' + doc.width + 'x' + doc.height + ' layer=' + layer.x + ',' + layer.y);

    var errors = [];
    [function () { doc.resizeCanvas(10, 10, {anchor: 'middle'}); },
     function () { doc.resizeCanvas(10, 10, {anchor: 'top', x: 1}); },
     function () { doc.resizeCanvas(10, 10, {x: 'left'}); },
     function () { doc.resizeCanvas(10, 10, {fill: 'nope'}); },
     function () { doc.resizeCanvas(10, 10, {offset: 1}); }].forEach(function (f) {
      try { f(); errors.push('no-throw'); } catch (e) { errors.push(e.message.length > 0 ? 'thrown' : 'empty'); }
    });
    console.log('errors=' + errors.join(',') + ' size=' + doc.width + 'x' + doc.height);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("offset=70x50 layer=30,20 sel=32,23,4x5 corner=0,255,0 moved=255,0")));
  CHECK(backlog_contains(window, QStringLiteral("center=80x60 layer=35,25")));
  CHECK(backlog_contains(window, QStringLiteral("bottomRight=55,25")));
  CHECK(backlog_contains(window, QStringLiteral("plain=120x70 layer=55,25")));
  CHECK(backlog_contains(window, QStringLiteral("errors=thrown,thrown,thrown,thrown,thrown size=120x70")));
}

// GitHub issue 701: doc.resizeCanvas gives the result Image > Canvas Size does. A canvas that
// only grows keeps the Background (filled with `fill`); one that leaves the Background's
// pixels beyond the canvas makes it an ordinary "Layer 0" that keeps them; and the plain form
// (no options, layers stay put) releases a Background that no longer matches the canvas.
void ui_script_resize_canvas_keeping_pixels_turns_background_into_layer_0() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto background = [&document] {
    return patchy::background_layer(std::as_const(document).layers(), document.width(), document.height());
  };
  const auto background_locks = patchy::kLayerLockPosition | patchy::kLayerLockTransparentPixels |
                                patchy::kLayerLockArtboardAutonest;
  CHECK(background() != nullptr);
  if (background() == nullptr) {
    return;
  }
  const auto background_id = background()->id();
  const auto width = document.width();
  const auto height = document.height();

  CHECK(run_script(window, QStringLiteral(
      "var d = app.activeDocument; d.resizeCanvas(d.width + 40, d.height + 20, {anchor: 'center', fill: '#00ff00'});")));
  CHECK(document.width() == width + 40 && document.height() == height + 20);
  CHECK(background() != nullptr && background()->id() == background_id);
  CHECK(background() != nullptr && background()->pixels().width() == width + 40);
  CHECK(background() != nullptr && background()->pixels().pixel(0, 0)[1] == 255 &&
        background()->pixels().pixel(0, 0)[0] == 0);

  CHECK(run_script(window, QStringLiteral("app.activeDocument.resizeCanvas(400, 300, {x: -50, y: -30});")));
  CHECK(document.width() == 400 && document.height() == 300);
  CHECK(background() == nullptr);
  {
    const auto& bottom = std::as_const(document).layers().front();
    CHECK(bottom.id() == background_id);
    CHECK(bottom.name() == "Layer 0");
    CHECK((patchy::layer_lock_flags(bottom) & background_locks) == 0U);
    CHECK(bottom.bounds().x == -50 && bottom.bounds().y == -30);
    CHECK(bottom.bounds().width == width + 40 && bottom.bounds().height == height + 20);
    CHECK(bottom.pixels().width() == width + 40 && bottom.pixels().height() == height + 20);
  }

  // The plain form on a fresh window: only the size changes, and the Background, which no
  // longer covers exactly the canvas, is released the same way.
  patchy::ui::MainWindow plain_window;
  show_window(plain_window);
  auto& plain = patchy::ui::MainWindowTestAccess::document(plain_window);
  const auto* plain_background =
      patchy::background_layer(std::as_const(plain).layers(), plain.width(), plain.height());
  CHECK(plain_background != nullptr);
  if (plain_background == nullptr) {
    return;
  }
  const auto plain_id = plain_background->id();
  CHECK(run_script(plain_window, QStringLiteral("app.activeDocument.resizeCanvas(500, 400);")));
  CHECK(plain.width() == 500 && plain.height() == 400);
  CHECK(patchy::background_layer(std::as_const(plain).layers(), plain.width(), plain.height()) == nullptr);
  {
    const auto& bottom = std::as_const(plain).layers().front();
    CHECK(bottom.id() == plain_id);
    CHECK(bottom.name() == "Layer 0");
    CHECK((patchy::layer_lock_flags(bottom) & background_locks) == 0U);
    CHECK(bottom.bounds().x == 0 && bottom.bounds().y == 0);
    CHECK(bottom.bounds().width == width && bottom.bounds().height == height);
  }
}

// doc.selection.smooth and feather (GitHub issue 614) on a 16-bit document, and bad radii throw.
void ui_script_selection_smooth_and_feather() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::set_deep_editing_override(true);
  const auto restore_deep_editing = qScopeGuard([] { patchy::set_deep_editing_override(std::nullopt); });
  const bool ran = run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    doc.convertBitDepth(16);
    var sel = doc.selection;
    sel.selectRect(10, 10, 30, 30);
    sel.setMask({x: 60, y: 10, width: 2, height: 2, data: new Uint8Array(4).fill(255).buffer}, {operation: 'add'});
    sel.smooth(3);
    var m = new Uint8Array(sel.getMask({x: 0, y: 0, width: 70, height: 50}).data);
    console.log('smooth depth=' + doc.bitDepth + ' corner=' + m[10 * 70 + 10] + ' edge=' + m[25 * 70 + 10] +
                ' speck=' + m[10 * 70 + 60] + ' bounds=' + JSON.stringify(sel.bounds));
    sel.feather(2.5);
    m = new Uint8Array(sel.getMask({x: 0, y: 0, width: 70, height: 50}).data);
    console.log('feather inside=' + m[25 * 70 + 25] + ' soft=' + (m[25 * 70 + 10] > 0 && m[25 * 70 + 10] < 255) +
                ' outside=' + m[25 * 70 + 2]);
    var errors = [];
    [function () { sel.smooth(0); }, function () { sel.smooth(1.5); }, function () { sel.smooth(501); },
     function () { sel.feather(0); }, function () { sel.feather(1000.5); }].forEach(function (f) {
      try { f(); errors.push('no-throw'); } catch (e) { errors.push(e.message.length > 0 ? 'thrown' : 'empty'); }
    });
    sel.deselect();
    sel.smooth(4);
    sel.feather(4);
    console.log('errors=' + errors.join(',') + ' empty=' + sel.exists);
  )JS"));
  CHECK(ran);
  if (!ran) {
    for (const auto& line : window.script_engine_host().message_backlog()) {
      std::cout << "  script: " << line.toStdString() << std::endl;
    }
  }
  CHECK(backlog_contains(window, QStringLiteral(
      R"(smooth depth=16 corner=0 edge=255 speck=0 bounds={"x":10,"y":10,"width":30,"height":30})")));
  CHECK(backlog_contains(window, QStringLiteral("feather inside=255 soft=true outside=0")));
  CHECK(backlog_contains(window, QStringLiteral("errors=thrown,thrown,thrown,thrown,thrown empty=false")));
}

}  // namespace

std::vector<patchy::test::TestCase> script_pixel_io_tests() {
  return {
      {"ui_script_selection_mask_reads_and_combines", ui_script_selection_mask_reads_and_combines},
      {"ui_script_selection_smooth_and_feather", ui_script_selection_smooth_and_feather},
      {"ui_script_layer_mask_set_get_and_remove", ui_script_layer_mask_set_get_and_remove},
      {"ui_script_layer_mask_companion_insert_is_one_undo_step",
       ui_script_layer_mask_companion_insert_is_one_undo_step},
      {"ui_script_composite_pixels_match_flatten", ui_script_composite_pixels_match_flatten},
      {"ui_script_io_image_round_trips_unicode_path", ui_script_io_image_round_trips_unicode_path},
      {"ui_script_resize_canvas_anchor_and_offset", ui_script_resize_canvas_anchor_and_offset},
      {"ui_script_resize_canvas_keeping_pixels_turns_background_into_layer_0",
       ui_script_resize_canvas_keeping_pixels_turns_background_into_layer_0},
  };
}
