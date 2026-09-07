#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"
#include "ui/script_engine.hpp"
#include "ui/script_vector.hpp"
#include "ui/pattern_library.hpp"
#include "ui/gradient_library.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/layer_tree.hpp"
#include "core/layer_render_utils.hpp"
#include "core/layer_metadata.hpp"
#include "ui/qt_paths.hpp"
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QDir>
#include <QFileInfo>
#include <cstdio>

namespace {
using namespace patchy::test::ui;
using patchy::ui::MainWindow;
using patchy::ui::ScriptEngineHost;

QJsonValue run(MainWindow& window, const QString& code) {
  auto& host = window.script_engine_host();
  ScriptEngineHost::RunOptions options;
  options.name = "vector-test"; options.unattended = true;
  const QString helpers = R"JS(
    function check(ok, message) { if (!ok) throw Error(message || 'check failed'); }
    function reject(fn) { var failed=false; try { fn(); } catch(e) { failed=true; } check(failed, 'expected refusal'); }
  )JS";
  const bool started = host.run_source(helpers + code, options);
  if (!started) {
    for (const auto& text : host.message_backlog()) { std::fprintf(stderr, "%s\n", text.toUtf8().constData()); }
  }
  CHECK(started);
  QElapsedTimer deadline; deadline.start();
  while (host.run_active() && deadline.elapsed() < 30000) { QApplication::processEvents(QEventLoop::AllEvents, 10); }
  QApplication::processEvents();
  CHECK(!host.run_active());
  if (host.last_run_had_error()) {
    for (const auto& text : host.message_backlog()) { std::fprintf(stderr, "%s\n", text.toUtf8().constData()); }
  }
  CHECK(!host.last_run_had_error());
  return host.last_result();
}
QString literal(const QString& text) {
  const auto json = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
  return QString::fromUtf8(json.mid(1, json.size() - 2));
}
void ui_script_vector_native_shapes_and_point_edits() {
  MainWindow window; show_window_empty(window);
  run(window, R"JS(
    var d=app.newDocument(128,128);
    var e=d.addShape('Eye',{type:'ellipse',x:10,y:20,width:60,height:40},{fill:'#e0a060',stroke:{width:3,paint:'#543322'}});
    d.addShape('Box',{type:'roundedRectangle',x:80,y:20,width:30,height:50,radii:[2,4,6,8]});
    d.addShape('Arrow',{type:'line',x1:10,y1:90,x2:90,y2:100,weight:3,arrowEnd:true});
    d.addShape('Star',{type:'polygon',cx:90,cy:80,radius:15,sides:5,starInset:45});
    var s=e.getShape(); check(s.liveShapes[0].geometry.type==='ellipse');
    check(Array.isArray(s.path.subpaths) && Array.isArray(s.path.subpaths[0].anchors),'plain detached arrays');
    check(s.stroke.alignment==='inside' && s.stroke.enabled);
    s.path.subpaths[0].anchors[0].x=999;
    check(e.getShape().path.subpaths[0].anchors[0].x!==999,'snapshot must be detached');
    patchy.setResult({eye:e.id, box:d.findLayer('Box').id});
  )JS");
  const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto result = window.script_engine_host().last_result().toObject();
  const auto* eye = doc.find_layer(result["eye"].toString().toULongLong());
  CHECK(eye && eye->vector_shape());
  patchy::LiveShapeParams expected;
  expected.kind = patchy::LiveShapeKind::Ellipse; expected.left = 10; expected.top = 20; expected.right = 70; expected.bottom = 60;
  CHECK(eye->vector_shape()->path.subpaths == patchy::generate_live_shape_subpaths(expected));
  const auto before = eye->vector_shape()->path;
  run(window, R"JS(
    var d=app.activeDocument, e=d.findLayer('Eye');
    e.updateShape({fill:'#ffcc88',stroke:{width:5}});
    check(e.getShape().liveShapes.length===1);
    var p=e.getShape().path;
    p.subpaths[0].anchors[0].x+=4; p.subpaths[0].anchors[0].outY-=6;
    e.updateShape({path:p});
    check(e.getShape().liveShapes.length===0);
    d.findLayer('Box').updateShape({geometry:{type:'roundedRectangle',x:78,y:22,width:32,height:48,radius:7},group:0});
  )JS");
  run(window, "check(app.activeDocument.undo());");
  CHECK(doc.find_layer(result["eye"].toString().toULongLong())->vector_shape()->path == before);
  run(window, "check(app.activeDocument.redo());");
  CHECK(doc.find_layer(result["eye"].toString().toULongLong())->vector_shape()->origination.empty());
  // Preserve imported bookkeeping when the requested edit only changes appearance.
  auto* imported = patchy::ui::MainWindowTestAccess::document(window).find_layer(result["eye"].toString().toULongLong());
  auto content = *std::as_const(*imported).vector_shape();
  content.path.fill_rule_value = 7; content.path.initial_fill_value = 9;
  content.fill.gradient_noise_pre_seed = 123;
  imported->set_vector_shape(content);
  imported->unknown_psd_blocks().push_back({"test", {1,2,3,4}});
  run(window, "app.activeDocument.findLayer('Eye').updateShape({stroke:{width:2}});");
  const auto* preserved = doc.find_layer(result["eye"].toString().toULongLong());
  CHECK(preserved->vector_shape()->path.fill_rule_value == 7);
  CHECK(preserved->vector_shape()->path.initial_fill_value == 9);
  CHECK(preserved->vector_shape()->fill.gradient_noise_pre_seed == 123);
  CHECK(std::any_of(preserved->unknown_psd_blocks().begin(), preserved->unknown_psd_blocks().end(),
      [](const auto& block) { return block.key == "test" && block.payload == std::vector<std::uint8_t>{1,2,3,4}; }));
}
void ui_script_vector_refusals_and_reads_preserve_state() {
  MainWindow window; show_window_empty(window);
  run(window, R"JS(
    var d=app.newDocument(64,64);
    d.addShape('Shape',{type:'rectangle',x:4,y:4,width:30,height:30});
    d.addPath('Saved',d.findLayer('Shape').getShape().path);
  )JS");
  auto& host = window.script_engine_host(); const auto before = host.automation_fingerprint();
  run(window, R"JS(
    var d=app.activeDocument,s=d.findLayer('Shape');
    d.listVectorResources(); s.getShape(); s.getVectorMask(); d.paths[0].getPath();
    reject(function(){d.addShape('Bad',{type:'ellipse',x:NaN,y:1,width:4,height:4});});
    reject(function(){d.addShape('Bad',{type:'ellipse',x:1,y:1,width:-4,height:4});});
    reject(function(){s.updateShape({fill:'#ff0000',stroke:{width:Infinity}});});
    reject(function(){s.updateShape({fill:{type:'pattern',source:'library',resourceId:'missing'}});});
    reject(function(){s.updateShape({unknown:1});});
    reject(function(){s.updateShape({path:{subpaths:[]}});});
    reject(function(){s.setVectorMask({path:s.getShape().path});});
    reject(function(){s.updateShape({geometry:{type:'ellipse',x:1,y:1,width:2,height:2},path:s.getShape().path});});
    reject(function(){s.transformShape([0,0,0,0,0,0]);});
    reject(function(){d.moveLayers([s],{parentId:s.id});});
    reject(function(){d.paths[0].moveTo(999);});
    reject(function(){d.paths[0].moveTo(NaN);});
    reject(function(){d.paths[0].moveTo(0.5);});
    reject(function(){d.getPath('999999');});
    s.updateShape({fill:s.getShape().fill});
  )JS");
  CHECK(host.automation_fingerprint() == before);
  run(window, "app.activeDocument.findLayer('Shape').locked=true;");
  const auto locked = host.automation_fingerprint();
  run(window, "var s=app.activeDocument.findLayer('Shape'); check(!s.getShape().editable); reject(function(){s.updateShape({fill:'#ff0000'});});");
  CHECK(host.automation_fingerprint() == locked);
  run(window, "app.activeDocument.findLayer('Shape').locked=false;");
  auto& imported = *patchy::ui::MainWindowTestAccess::document(window).find_layer(
      std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers().back().id());
  imported.metadata()[patchy::kLayerMetadataVectorLock] = "preserved-import";
  const auto unsupported = host.automation_fingerprint();
  run(window, "var s=app.activeDocument.findLayer('Shape'); check(!s.getShape().editable); reject(function(){s.updateShape({fill:'#ff0000'});});");
  CHECK(host.automation_fingerprint() == unsupported);
}
void ui_script_vector_boolean_groups_and_transforms() {
  MainWindow window; show_window_empty(window);
  run(window, R"JS(
    var d=app.newDocument(128,128);
    var a=d.addShape('Outer',{type:'rectangle',x:10,y:10,width:60,height:60});
    var b=d.addShape('Hole',{type:'ellipse',x:25,y:25,width:30,height:30});
    var cut=d.combineShapes([a,b],'subtract');
    check(cut.getShape().path.subpaths.length===2);
    var p=cut.getShape().path; p.subpaths[1].anchors[0].x+=2;
    cut.updateShape({path:p});
    check(cut.getShape().liveShapes.length===1,'untouched live group retained');
    var eye=d.addShape('Eye',{type:'ellipse',x:80,y:20,width:10,height:10});
    var group=d.groupLayers([eye,cut],'Cat');
    check(group.children[0].id===cut.id && group.children[1].id===eye.id);
    group.transformShape([1,0,0,1,5,7]);
    check(eye.getShape().liveShapes[0].geometry.x===85);
    eye.transformShape([0,1,-1,0,128,0]);
    check(eye.getShape().liveShapes.length===0);
    var folder=d.addGroup('Other');
    d.moveLayers([eye],{parentId:folder.id,index:0});
    check(folder.children[0].id===eye.id && group.children.length===1);
    reject(function(){d.moveLayers([folder],{parentId:eye.id});});
    var pixel=d.addLayer('Raster'); d.moveLayers([pixel],{parentId:folder.id});
    var prior=JSON.stringify(eye.getShape());
    reject(function(){folder.transformShape([2,0,0,2,0,0]);});
    check(prior===JSON.stringify(eye.getShape()),'mixed group prevalidation');
  )JS");
  auto& doc = patchy::ui::MainWindowTestAccess::document(window);
  const auto& root = std::as_const(doc).layers();
  CHECK(patchy::layer_tree_count(root) >= 5);
}
void ui_script_vector_masks_paths_selection_and_history() {
  MainWindow window; show_window_empty(window);
  run(window, R"JS(
    var d=app.newDocument(64,64);
    var face=d.addShape('Artwork',{type:'ellipse',x:8,y:8,width:48,height:48},{fill:'#ffaa66'});
    var p=face.getShape().path, s=d.groupLayers([face],'Face');
    var work=d.setWorkPath(p), id=work.id;
    work.save('Outline'); check(work.kind==='saved' && work.id===id && d.workPath===null);
    var copy=work.duplicate('Copy'); copy.moveTo(0); check(d.paths[0].id===copy.id);
    d.clippingPath=work; check(d.clippingPath.id===work.id);
    copy.transform([1,0,0,1,2,0]); copy.activate();
    s.setVectorMask({path:copy.getPath(),density:70,feather:1});
    check(s.getVectorMask().linked && s.getVectorMask().enabled);
    s.transformVectorMask([1,0,0,1,-2,0]);
    d.selection.fromPath(p,{feather:1}); check(d.selection.exists);
    var traced=d.selection.toPath({tolerance:1}); check(traced.subpaths.length>0);
    d.setWorkPath(traced);
    patchy.setResult({shape:s.id, saved:work.id, copy:copy.id});
  )JS");
  const auto state = window.script_engine_host().last_result().toObject();
  const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(doc.paths().size() == 3);
  const auto shape_id = state["shape"].toString().toULongLong();
  CHECK(doc.find_layer(shape_id)->vector_mask());
  run(window, "var s=app.activeDocument.findLayer('Face'); s.rasterizeVectorMask(); check(s.getVectorMask()===null);");
  CHECK(doc.find_layer(shape_id)->mask().has_value());
  run(window, "check(app.activeDocument.undo());");
  CHECK(doc.find_layer(shape_id)->vector_mask());
  CHECK(!doc.find_layer(shape_id)->mask());
  run(window, R"JS(
    var d=app.activeDocument, p=d.paths[0]; p.remove(); reject(function(){p.getPath();});
    var s=d.findLayer('Face'); s.removeVectorMask();
    s.setVectorMask({path:{subpaths:[]},inverted:true}); check(s.getVectorMask().inverted);
    s.setVectorMask({enabled:false}); check(!s.getVectorMask().enabled);
  )JS");
  CHECK(patchy::vector_mask_alpha_at(*doc.find_layer(shape_id), 30, 30) == 1.0F);
  run(window, "app.activeDocument.findLayer('Face').rasterizeVectorMask();");
  CHECK(!doc.find_layer(shape_id)->vector_mask() && !doc.find_layer(shape_id)->mask());
}
void ui_script_vector_all_paints_and_pattern_collision() {
  MainWindow window; show_window_empty(window);
  run(window, "app.newDocument(96,96);");
  auto& host = window.script_engine_host();
  host.vector_pattern_library().restore_default_patterns();
  host.vector_gradient_library().restore_default_gradients();
  CHECK(!host.vector_pattern_library().entries().empty());
  const auto entry = host.vector_pattern_library().entries().front();
  const auto resource = host.vector_pattern_library().resource_for_entry(entry.storage_id);
  CHECK(resource.has_value());
  auto collision = *resource;
  collision.tile = patchy::PixelBuffer(1, 1, patchy::PixelFormat::rgba8());
  collision.tile.pixel(0, 0)[0] = 255; collision.tile.pixel(0, 0)[3] = 255;
  patchy::ui::MainWindowTestAccess::document(window).metadata().patterns.adopt(collision);
  run(window, QString(R"JS(
    var d=app.activeDocument;
    var p=d.addShape('Pattern',{type:'rectangle',x:5,y:5,width:30,height:30},
      {fill:{type:'pattern',source:'library',resourceId:%1,scale:0.5},
       stroke:{width:4,paint:{type:'pattern',source:'library',resourceId:%1}}});
    check(p.getShape().fill.resourceId!==%2,'collision must adopt different id');
    check(p.getShape().fill.resourceId===p.getShape().stroke.paint.resourceId);
    var types=['linear','radial','angle','reflected','diamond'];
    types.forEach(function(type,i){d.addShape(type,{type:'ellipse',x:8+i*14,y:44,width:18,height:18},
      {fill:{type:'gradient',gradient:{type:type,colorStops:[{position:0,color:'#ff0000'},{position:1,color:'#0000ff'}],
      alphaStops:[{position:0,opacity:1},{position:1,opacity:0.5}],smoothness:0}}});});
    d.addFillLayer('Noise',{type:'gradient',gradient:{form:'noise',noise:{seed:42,roughness:2200}}});
    var resources=d.listVectorResources(); check(resources.gradients.length>0 && resources.customShapes.length>0);
    p.updateShape({stroke:{paint:{type:'gradient',gradient:{presetId:resources.gradients[0].presetId}},cap:'round',join:'round',dashes:[2,1]}});
    d.addShape('Custom',{type:'custom',resourceId:resources.customShapes[0].resourceId,x:30,y:30,width:20,height:20});
    patchy.setResult(p.getShape().fill.resourceId);
  )JS").arg(literal(entry.storage_id), literal(QString::fromStdString(entry.id.toStdString()))));
  const auto id = host.last_result().toString().toStdString();
  const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(doc.metadata().patterns.find(id));
  CHECK(patchy::ui::presets::pattern_tiles_equal(doc.metadata().patterns.find(id)->tile, resource->tile));
  CHECK(doc.metadata().patterns.find(collision.id)->tile.width() == 1);
}
void ui_script_vector_path_raster_painting() {
  MainWindow window; show_window_empty(window);
  run(window, R"JS(
    var d=app.newDocument(64,64), paint=d.addLayer('Paint');
    var p={subpaths:[{closed:false,anchors:[{x:8,y:8},{x:40,y:8},{x:40,y:40}]}]};
    paint.strokePath(p,{color:'#ff0000',size:3,seed:1});
    var data=paint.getPixels(), a=new Uint8Array(data.data);
    function alpha(x,y){return a[((y-data.y)*data.width+x-data.x)*4+3]||0;}
    check(alpha(25,8)>0); check(alpha(20,20)===0,'no implied closing stroke');
    d.selection.selectRect(20,10,15,15);
    paint.fillPath(p,{paint:'#00ff00'});
    patchy.setResult(paint.id);
  )JS");
  const auto id = window.script_engine_host().last_result().toString().toULongLong();
  const auto& doc = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* layer = doc.find_layer(id); CHECK(layer);
  const auto& pixels = layer->pixels(); const auto bounds = layer->bounds();
  CHECK(pixels.pixel(30 - bounds.x, 15 - bounds.y)[1] == 255);
  CHECK(pixels.pixel(38 - bounds.x, 20 - bounds.y)[3] == 0);
}
void ui_script_vector_psd_svg_unicode_round_trip() {
  MainWindow window; show_window_empty(window); window.set_cli_automation_mode(true);
  auto& patterns = window.script_engine_host().vector_pattern_library();
  patchy::PixelBuffer tile(2, 2, patchy::PixelFormat::rgba8()); tile.clear(255);
  tile.pixel(0, 0)[0] = 0; tile.pixel(1, 1)[1] = 0;
  const auto pattern_id = patterns.add_pattern("Vector round trip", tile);
  CHECK(!pattern_id.isEmpty());
  ensure_artifact_dir();
  const auto folder = QFileInfo("test-artifacts/vector-scripting").absoluteFilePath(); CHECK(QDir().mkpath(folder));
  const auto name = QString::fromUtf8(patchy::test::utf8_string(patchy::test::kUnicodePathStems[0]).c_str());
  run(window, QString(R"JS(
    var d=app.newDocument(80,80);
    var s=d.addShape('Cat face',{type:'ellipse',x:10,y:10,width:60,height:55},{fill:'#ffaa77'});
    var p=s.getShape().path; d.clippingPath=d.addPath('Silhouette',p); d.setWorkPath(p);
    var masked=d.groupLayers([s],'Masked face'); masked.setVectorMask({path:p,density:90,feather:1.5,linked:false});
    d.addShape('Gradient',{type:'rectangle',x:5,y:60,width:20,height:12},
      {fill:{type:'gradient',gradient:{type:'linear',angle:30,colorStops:[{position:0,color:'#ffcc66'},{position:1,color:'#994433'}]}}});
    d.addShape('Pattern',{type:'rectangle',x:45,y:60,width:20,height:12},
      {fill:{type:'pattern',source:'library',resourceId:%2,scale:1}});
    d.addShape('Unfilled smile',{type:'path',path:{subpaths:[{closed:false,anchors:[
      {x:30,y:35,outX:30,outY:50},{x:50,y:35,inX:50,inY:50}]}]}},
      {fill:'none',stroke:{paint:'#774422',width:3}});
    d.addShape('No outline',{type:'ellipse',x:25,y:20,width:5,height:5},
      {fill:'#ffffff',stroke:{paint:'none',width:10}});
    var stem=%1;
    check(d.saveAs(stem+'.psd'),'PSD save'); d.close();
    var re=app.open(stem+'.psd'), rs=re.findLayer('Cat face');
    check(rs.isShape && rs.getShape().liveShapes.length===1,'PSD editable shape');
    var rm=re.findLayer('Masked face').getVectorMask();
    check(rm!==null && Math.abs(rm.density-90)<0.3 && rm.feather===1.5 && !rm.linked,'PSD editable group mask');
    check(re.paths.length===2 && re.clippingPath.name==='Silhouette','PSD saved/work/clipping paths');
    check(re.findLayer('Gradient').getShape().fill.gradient.colorStops.length===2,'PSD gradient');
    check(re.findLayer('Pattern').getShape().fill.source==='document','PSD pattern');
    check(!re.findLayer('Unfilled smile').getShape().stroke.fillEnabled,'PSD None fill');
    check(!re.findLayer('No outline').getShape().stroke.enabled,'PSD None stroke');
    check(re.listVectorResources().patterns.some(function(p){return p.source==='document';}),'PSD adopted resource');
    re.findLayer('Masked face').removeVectorMask(); check(re.saveAs(stem+'.svg'),'SVG save'); re.close();
    var svg=app.open(stem+'.svg');
    function hasShape(layers){return layers.some(function(l){return l.isShape || (l.isGroup && hasShape(l.children));});}
    check(hasShape(svg.layers),'SVG native shape');
    check(svg.renderPreview(stem+'.png',{maxWidth:80,maxHeight:80}).width===80,'SVG preview size');
  )JS").arg(literal(folder + '/' + name), literal(pattern_id)));
  CHECK(QFileInfo::exists(folder + '/' + name + ".psd"));
  CHECK(QFileInfo::exists(folder + '/' + name + ".svg"));
  CHECK(patterns.remove_pattern(pattern_id));
}
}  // namespace
std::vector<patchy::test::TestCase> vector_scripting_tests() {
  return {{"ui_script_vector_native_shapes_and_point_edits", ui_script_vector_native_shapes_and_point_edits},
    {"ui_script_vector_refusals_and_reads_preserve_state", ui_script_vector_refusals_and_reads_preserve_state},
    {"ui_script_vector_boolean_groups_and_transforms", ui_script_vector_boolean_groups_and_transforms},
    {"ui_script_vector_masks_paths_selection_and_history", ui_script_vector_masks_paths_selection_and_history},
    {"ui_script_vector_all_paints_and_pattern_collision", ui_script_vector_all_paints_and_pattern_collision},
    {"ui_script_vector_path_raster_painting", ui_script_vector_path_raster_painting},
    {"ui_script_vector_psd_svg_unicode_round_trip", ui_script_vector_psd_svg_unicode_round_trip}};
}
