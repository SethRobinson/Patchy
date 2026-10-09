#include "ui_test_support.hpp"
#include "ui/script_engine.hpp"
#include "ui/text_area_layout.hpp"
#include "core/text_area.hpp"
#include "core/pixel_depth.hpp"
#include "core/document_depth.hpp"
#include "ui/qt_paths.hpp"
#include "psd/psd_document_io.hpp"
#include "local_psd_fixtures.hpp"
#include <QElapsedTimer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QScopeGuard>
#include <QDir>
#include <QFontMetricsF>
#include <cstdio>

namespace {
using namespace patchy;
using namespace patchy::ui;
using namespace patchy::test::ui;

VectorPath polygon(std::initializer_list<QPointF> points) {
  VectorPath path;
  auto& sub = path.subpaths.emplace_back();
  for (auto p : points) sub.anchors.push_back({p.x(), p.y(), p.x(), p.y(), p.x(), p.y(), false});
  return path;
}

const Layer* layer_named(const Document& document, std::string_view name) {
  for(const auto& layer:document.layers()) if(layer.name()==name)return &layer;
  return nullptr;
}

void ui_text_area_full_band_spans() {
  const auto concave = polygon({{0,0},{500,0},{500,400},{320,400},{320,150},{180,150},{180,400},{0,400}});
  const TextAreaGeometry geometry(concave);
  const auto upper = geometry.spans(100, 120);
  CHECK(upper.size() == 1 && upper[0].first == 0 && upper[0].second == 500);
  const auto crossing = geometry.spans(140, 160);
  CHECK(crossing.size() == 2);
  if (crossing.size() == 2) {
    CHECK(crossing[0] == std::make_pair(0.0, 180.0));
    CHECK(crossing[1] == std::make_pair(320.0, 500.0));
  }
  const TextAreaGeometry bowtie(polygon({{0,0},{100,100},{100,0},{0,100}}));
  const auto split = bowtie.spans(40,60);
  CHECK(split.size() == 2);
  if (split.size() == 2) {
    CHECK(std::abs(split[0].second - 40.0) < .001);
    CHECK(std::abs(split[1].first - 60.0) < .001);
  }
  CHECK(geometry.spans(400,410).empty());
  CHECK(geometry.spans(20,20).empty());
  CHECK(TextAreaGeometry(polygon({{0,0},{0,0}})).spans(0,10).empty());
  VectorPath large;auto& outline=large.subpaths.emplace_back();
  for(int i=0;i<10000;++i) {
    const auto angle=static_cast<double>(i)*6.283185307179586/10000.0;
    const auto x=10000.0+9000.0*std::cos(angle),y=10000.0+9000.0*std::sin(angle);
    outline.anchors.push_back({x,y,x,y,x,y,false});
  }
  const TextAreaGeometry indexed(large);
  for(int y=2000;y<18000;y+=32) CHECK(!indexed.spans(y,y+20).empty());
}

void ui_text_area_concave_flow_alignment_and_vertical() {
  QTextDocument document;
  QFont font(QStringLiteral("Arial"));font.setPixelSize(24);font.setStretch(QFont::AnyStretch);
  document.setDefaultFont(font);document.setDocumentMargin(0);document.setTextWidth(500);
  document.setPlainText(QStringLiteral("One two three four five six seven eight nine ten. ").repeated(8).trimmed());
  QTextCursor cursor(&document);cursor.select(QTextCursor::Document);
  QTextCharFormat format;format.setFont(font);format.setProperty(kTextExactSizeFormatProperty,24.0);cursor.setCharFormat(format);
  const auto concave=polygon({{0,0},{500,0},{500,400},{320,400},{320,150},{180,150},{180,400},{0,400}});
  // Photoshop includes its terminal paragraph character in the last count;
  // QTextLine excludes that character (the story itself has 399 characters).
  const std::vector<int> lengths{45,45,44,44,46,16,14,15,15,16,14,14,17,13,16,16,9};
  for(const auto alignment:{Qt::AlignLeft,Qt::AlignHCenter,Qt::AlignRight,Qt::AlignJustify}) {
    QTextBlockFormat paragraph;paragraph.setAlignment(alignment);cursor.setBlockFormat(paragraph);
    const auto plan=area_text_layout_plan(document,concave);
    const auto hit_geometry = TextLineGeometry::from_lines(document, plan.lines);
    CHECK(plan.lines.size()==lengths.size());
    for(std::size_t i=0;i<std::min(plan.lines.size(),lengths.size());++i) {
      CHECK(plan.lines[i].line.textLength()==lengths[i]);
      const auto position = plan.lines[i].line.textStart() + 1;
      CHECK(hit_geometry.position_at(hit_geometry.caret_rect(position).center()) == position);
      if(i>=5) {
        CHECK(std::abs(plan.lines[i].line.x()-((i-5)%2?320.0:0.0))<.01);
        if((i-5)%2) CHECK(plan.lines[i].line.y()==plan.lines[i-1].line.y());
      }
    }
    CHECK(hit_geometry.selection_rects(224,254).size() == 2);
  }
  QTextBlockFormat paragraph;paragraph.setAlignment(Qt::AlignLeft);cursor.setBlockFormat(paragraph);
  const auto vertical=vertical_area_text_layout_plan(document,polygon({{250,0},{500,400},{0,400}}));
  // The final native columns split words; that paragraph-composer behavior is
  // outside the existing vertical typography baseline. Pin the full-word flow.
  const std::vector<int> columns{4,4,6,10,10,11,13,16,10,11,9,4};
  CHECK(vertical.columns.size()>=columns.size());
  for(std::size_t i=0;i<std::min(columns.size(),vertical.columns.size());++i) {
    CHECK(vertical.columns[i].end-vertical.columns[i].start==columns[i]);
    const double first_axis = 500.0 - QFontMetricsF(font).capHeight() / 2.0 - 57.6;
    CHECK(std::abs(vertical.columns[i].axis-(first_axis-28.8*static_cast<double>(i)))<.05);
  }
}

void ui_text_area_photoshop_triangle_line_breaks_and_caret() {
  const auto boundary = polygon({{250,0},{500,400},{0,400}});
  QTextDocument document;
  QFont font(QStringLiteral("Arial")); font.setPixelSize(24); font.setStretch(QFont::AnyStretch);
  document.setDefaultFont(font);
  document.setDocumentMargin(0);
  document.setPlainText(QStringLiteral("One two three four five six seven eight nine ten. ").repeated(8).trimmed());
  QTextCursor cursor(&document); cursor.select(QTextCursor::Document);
  QTextCharFormat format; format.setFont(font); format.setProperty(kTextExactSizeFormatProperty,24.0);
  cursor.setCharFormat(format);
  document.setTextWidth(500);
  const auto plan = area_text_layout_plan(document, boundary);
  // The Linux test host uses metrically compatible Liberation Sans. Its cap
  // height differs from Arial, so keep native line breaks and use its face's
  // measured cap for baseline comparisons. Arial's captured cap is 17.18372.
  const double cap = QFontMetricsF(font).capHeight();
  CHECK(plan.valid);
  const std::vector<int> expected{4,10,14,17,19,26,24,31,33,36,40,46};
  CHECK(plan.lines.size() == expected.size());
  int position = 0;
  for (std::size_t i = 0; i < std::min(plan.lines.size(),expected.size()); ++i) {
    const auto& line = plan.lines[i].line;
    if (line.textLength() != expected[i]) {
      std::fprintf(stderr,"area line %zu: start=%d length=%d expected=%d\n",i,line.textStart(),line.textLength(),expected[i]);
    }
    CHECK(line.textStart() == position);
    CHECK(line.textLength() == expected[i]);
    CHECK(std::abs(line.y()+line.ascent()-(cap+57.6+28.8*static_cast<double>(i))) < .05);
    position += expected[i];
  }
  const auto geometry = TextLineGeometry::from_lines(document,plan.lines);
  for (const auto& item : plan.lines) {
    const int index = item.line.textStart()+1;
    const auto caret = geometry.caret_rect(index);
    CHECK(geometry.position_at(caret.center()) == index);
  }
  CHECK(position < document.characterCount()-1); // overflow stays in the model

  cursor.setPosition(12);cursor.setPosition(40,QTextCursor::KeepAnchor);
  font.setPixelSize(48);format.setFont(font);format.setProperty(kTextExactSizeFormatProperty,48.0);
  cursor.setCharFormat(format);
  const auto mixed=area_text_layout_plan(document,boundary);
  const std::vector<int> mixed_lengths{4,10,10,10,24,37,39,44};
  const std::vector<double> mixed_baselines{74.78372,132.38373,189.98373,247.58374,305.18375,333.98373,362.78372,391.58371};
  CHECK(mixed.lines.size()==mixed_lengths.size());
  for(std::size_t i=0;i<std::min(mixed.lines.size(),mixed_lengths.size());++i) {
    CHECK(mixed.lines[i].line.textLength()==mixed_lengths[i]);
    CHECK(std::abs(mixed.lines[i].line.y()+mixed.lines[i].line.ascent()-(mixed_baselines[i]+cap-17.18372))<.05);
  }
}

QJsonValue run(MainWindow& window, const QString& code) {
  auto& host = window.script_engine_host();
  ScriptEngineHost::RunOptions options; options.name="area-text-test"; options.unattended=true;
  const bool started = host.run_source(QStringLiteral("function check(v,m){if(!v)throw Error(m||'check failed');}function reject(f){var bad=false;try{f();}catch(e){bad=true;}check(bad,'expected refusal');}\n")+code,options);
  if (!started) for (const auto& message : host.message_backlog()) std::fprintf(stderr,"%s\n",message.toUtf8().constData());
  CHECK(started);
  QElapsedTimer timer; timer.start();
  while (host.run_active() && timer.elapsed()<30000) QApplication::processEvents(QEventLoop::AllEvents,10);
  CHECK(!host.run_active());
  if (host.last_run_had_error()) for (const auto& message : host.message_backlog()) std::fprintf(stderr,"%s\n",message.toUtf8().constData());
  CHECK(!host.last_run_had_error());
  return host.last_result();
}

void ui_text_area_script_creation_boundary_edit_and_undo() {
  MainWindow window; show_window_empty(window);
  run(window,R"JS(
    var d=app.newDocument(660,520);
    var shape=d.addShape('Source',{type:'polygon',cx:300,cy:250,radius:200,sides:3});
    var p=shape.getShape().path;
    var t=d.addTextLayer('One two three four five six seven eight nine ten. '.repeat(8),{font:'Arial',size:24,area:p});
    t.name='Area';
    check(t.textArea.subpaths.length===1);
    check(t.textArea.subpaths[0].anchors.length===3);
    var original=JSON.stringify(t.textArea);
    p.subpaths[0].anchors[0].x+=30;
    check(JSON.stringify(t.textArea)===original,'area is a snapshot');
    reject(function(){d.addTextLayer('bad',{area:p,box:{width:100,height:100}});});
    reject(function(){t.textArea={subpaths:[]};});
    reject(function(){t.textArea={subpaths:[p.subpaths[0],p.subpaths[0]]};});
    patchy.setResult({id:t.id,area:t.textArea,text:t.text});
  )JS");
  const auto original=window.script_engine_host().last_result().toObject();
  run(window,R"JS(
    var t=app.activeDocument.findLayer('Area'), p=t.textArea;
    p.subpaths[0].anchors[0].x+=20;
    t.textArea=p;
    patchy.setResult(t.textArea);
  )JS");
  CHECK(window.script_engine_host().last_result()!=original["area"]);
  run(window,"check(app.activeDocument.undo());patchy.setResult(app.activeDocument.findLayer('Area').textArea);");
  CHECK(window.script_engine_host().last_result()==original["area"]);
  run(window,"check(app.activeDocument.redo());");
  run(window,R"JS(
    var source=app.activeDocument,t=source.findLayer('Area'),area=JSON.stringify(t.textArea);
    var duplicate=t.duplicate();check(JSON.stringify(duplicate.textArea)===area);
    var target=app.newDocument(660,520),copy=t.duplicate(target);
    check(copy.text===t.text && JSON.stringify(copy.textArea)===area,'cross-document copy');
    var before=copy.textArea.subpaths[0].anchors;
    target.resizeImage(330,260);
    var after=copy.textArea.subpaths[0].anchors;
    for(var i=0;i<before.length;i++) {
      check(Math.abs(after[i].x-before[i].x/2)<1.01 && Math.abs(after[i].y-before[i].y/2)<1.01,'document resize');
    }
    source.activate();source.activeLayer=t;
  )JS");
  const auto& doc=std::as_const(MainWindowTestAccess::document(window));
  const auto* layer=doc.find_layer(original["id"].toString().toULongLong());
  CHECK(layer && text_area_for_layer(*layer));
  CHECK(layer && !layer->pixels().empty());
  if (layer) CHECK(QString::fromStdString(layer->metadata().at(kLayerMetadataText))==original["text"].toString());
  const auto* canvas=require_canvas(window);
  CHECK(canvas->text_area_edit_target_path()!=nullptr);
}

void ui_text_area_depth_edit_transform_and_roundtrip() {
  set_deep_editing_override(true);
  const auto restore = qScopeGuard([] { set_deep_editing_override(std::nullopt); });
  ensure_artifact_dir();
  for (const int bits : {8,16,32}) {
    MainWindow window; show_window_empty(window);
    run(window,QString(R"JS(
      var d=app.newDocument(660,520);d.convertBitDepth(%1);
      var preserved=d.addLayer('Preserved');preserved.fillRect(0,0,3,1,'#3579ab');
      patchy.setResult(preserved.id);
    )JS").arg(bits));
    const auto preserved_id=window.script_engine_host().last_result().toString().toULongLong();
    auto& doc=MainWindowTestAccess::document(window);
    auto* preserved=doc.find_layer(preserved_id);
    CHECK(preserved!=nullptr); if(!preserved)continue;
    if(bits!=8) {
      const std::array<float,12> samples{123.456F,42.125F,35.875F,255.F,500.125F,68.2F,8.3F,255.F,0.125F,50.625F,128.125F,255.F};
      store_rgba_row(preserved->pixels(),0,0,3,deep_domain_for(document_bit_depth(doc)),samples);
    }
    const auto before=std::as_const(*preserved).pixels();
    run(window,R"JS(
      var d=app.activeDocument;
      var p={subpaths:[{closed:true,anchors:[{x:330,y:60},{x:580,y:460},{x:80,y:460}]}]};
      var t=d.addTextLayer('One two three four five six seven eight nine ten. '.repeat(8),{area:p,font:'Arial',size:24});
      t.name='Area';t.text+=' More hidden overflow.';
      var a=t.textArea;a.subpaths[0].anchors[0].x+=10;t.textArea=a;
      t.moveTo(t.x+12,t.y+9);t.textAlign='center';
      patchy.setResult(t.id);
    )JS");
    const auto id=window.script_engine_host().last_result().toString().toULongLong();
    run(window,"check(app.activeDocument.undo());check(app.activeDocument.redo());");
    const auto* layer=std::as_const(doc).find_layer(id);
    CHECK(layer && text_area_for_layer(*layer)); if(!layer)continue;
    CHECK(layer->pixels().format().bit_depth==document_bit_depth(doc));
    auto* canvas=require_canvas(window);
    CHECK(canvas->begin_free_transform());
    const auto state=canvas->transform_controls_state();
    CHECK(state.has_value());
    if(state) CHECK(canvas->set_transform_controls_state(state->reference_position,-115.0,90.0,17.0));
    canvas->finish_free_transform();
    run(window,"var t=app.activeDocument.findLayer('Area'), before=JSON.stringify(t.textArea);t.text+=' Transform edit.';check(JSON.stringify(t.textArea)===before,'re-entry moved the boundary');");
    CHECK(document_depth_problems(doc).empty());
    const auto* after=std::as_const(doc).find_layer(preserved_id);
    CHECK(after && after->pixels().data().size()==before.data().size());
    if(after) CHECK(std::equal(before.data().begin(),before.data().end(),after->pixels().data().begin()));
    const auto path=to_filesystem_path(QDir::current().absoluteFilePath(QStringLiteral("test-artifacts/area-%1.psd").arg(bits)));
    psd::DocumentIo::write_layered_rgb8_file(doc,path);
    const auto reopened=psd::DocumentIo::read_file(path,{.keep_bit_depth=true});
    CHECK(document_bit_depth(reopened)==document_bit_depth(doc));
    const auto* original=std::as_const(doc).find_layer(id);
    const auto* saved=layer_named(reopened,"Area");
    CHECK(saved && original && text_area_for_layer(*saved)==text_area_for_layer(*original));
    CHECK(saved && original && saved->metadata().at(kLayerMetadataText)==original->metadata().at(kLayerMetadataText));
    const auto psb=psd::DocumentIo::write_layered_rgb8(doc,{.large_document=true});
    const auto large=psd::DocumentIo::read(psb,{.keep_bit_depth=true});
    CHECK(document_bit_depth(large)==document_bit_depth(doc));
    CHECK(layer_named(large,"Area") && text_area_for_layer(*layer_named(large,"Area"))==text_area_for_layer(*original));
  }
}

void ui_text_area_empty_and_large_frames() {
  MainWindow window; show_window_empty(window);
  window.set_cli_automation_mode(true);  // Match --run-script's missing-font substitution on Linux.
  const auto fixture = to_qstring(patchy::test::committed_psd_fixture_path("photoshop-area-empty.psd"));
  const auto quoted = QString::fromUtf8(QJsonDocument(QJsonArray{fixture}).toJson(QJsonDocument::Compact));
  run(window, QStringLiteral("var d=app.open(%1[0]);var t=d.layers.filter(function(l){return l.isText;})[0];"
      "var story=t.text, area=JSON.stringify(t.textArea);check(story.length>300);"
      "t.text+=' More overflow.';check(t.text===story+' More overflow.');"
      "check(JSON.stringify(t.textArea)===area);t.text='';check(t.text==='');"
      "t.text=story;check(t.text===story);d.close();").arg(quoted));
  run(window,R"JS(
    var d=app.newDocument(660,520);
    var p={subpaths:[{closed:true,anchors:[{x:20,y:20},{x:100000,y:20},{x:100000,y:100000},{x:20,y:100000}]}]};
    var t=d.addTextLayer('Small story',{area:p,font:'Arial',size:24});
    check(t.bounds.width*t.bounds.height<100000,'sparse area allocated the whole frame');
    t.textOrientation='vertical';
    check(t.bounds.width*t.bounds.height<100000,'sparse vertical area allocated the whole frame');
    t.textOrientation='horizontal';
    t.textArea={subpaths:[{closed:true,anchors:[{x:20,y:20},{x:500,y:20},{x:500,y:400},{x:20,y:400}]}]};
    var area=JSON.stringify(t.textArea);t.text='';
    check(t.text==='' && JSON.stringify(t.textArea)===area,'clear lost the frame');
    t.text='Restored';
    check(t.text==='Restored' && JSON.stringify(t.textArea)===area,'empty frame re-entry');
    t.textArea=null;check(t.textArea===null && t.text==='Restored','convert to box');
  )JS");
}

void ui_text_area_psd_uses_postscript_font_names() {
  MainWindow window; show_window_empty(window);
  const bool arial = QFontDatabase::families().contains(QStringLiteral("Arial"));
  const auto family = arial ? QStringLiteral("Arial") : QStringLiteral("Liberation Sans");
  CHECK(QFontDatabase::families().contains(family));
  run(window,QStringLiteral(R"JS(
    var d=app.newDocument(400,300);
    var p={subpaths:[{closed:true,anchors:[{x:20,y:20},{x:380,y:20},{x:200,y:280}]}]};
    for(var i=0;i<4;i++) {
      var t=d.addTextLayer('Font',{area:p,font:'%1',size:24,bold:!!(i&1),italic:!!(i&2)});
      t.name='Font '+i;
    }
  )JS").arg(family));
  const auto bytes = psd::DocumentIo::write_layered_rgb8(MainWindowTestAccess::document(window));
  const auto reopened = psd::DocumentIo::read(bytes);
  const std::array<std::string,4> names = arial
      ? std::array<std::string,4>{"ArialMT","Arial-BoldMT","Arial-ItalicMT","Arial-BoldItalicMT"}
      : std::array<std::string,4>{"LiberationSans","LiberationSans-Bold","LiberationSans-Italic","LiberationSans-BoldItalic"};
  for(std::size_t i=0;i<names.size();++i) {
    std::vector<std::uint8_t> needle{0xfe,0xff};
    for(const auto c:names[i]) {needle.push_back(0);needle.push_back(static_cast<std::uint8_t>(c));}
    const auto contains = [&](const UnknownPsdBlock& block) {
      return std::search(block.payload.begin(),block.payload.end(),needle.begin(),needle.end())!=block.payload.end();
    };
    const auto* layer=layer_named(reopened,"Font "+std::to_string(i));
    CHECK(layer!=nullptr);if(!layer)continue;
    CHECK(std::any_of(layer->unknown_psd_blocks().begin(),layer->unknown_psd_blocks().end(),
                     [&](const auto& b){return b.key=="TySh" && contains(b);}));
    const auto& globals=reopened.metadata().unknown_psd_resources;
    CHECK(std::any_of(globals.begin(),globals.end(),[&](const auto& b){return b.key=="Txt2" && contains(b);}));
  }
}

void ui_text_area_type_tool_creation_cancel_and_boundary_edit() {
  MainWindow window; show_window_empty(window);
  run(window,R"JS(
    var d=app.newDocument(660,520);
    var s=d.addShape('Source',{type:'polygon',cx:330,cy:260,radius:210,sides:3});
    patchy.setResult(s.id);
  )JS");
  auto* canvas=require_canvas(window);canvas->set_zoom(1.0);
  canvas->set_primary_color(Qt::white);
  const auto source_id=window.script_engine_host().last_result().toString().toULongLong();
  const auto& doc=std::as_const(MainWindowTestAccess::document(window));
  const auto source_path=doc.find_layer(source_id)->vector_shape()->path;
  require_action_by_text(window,QStringLiteral("Type"))->trigger();
  const QPoint center(330,260);
  CHECK(canvas->text_area_at(center).has_value());
  const auto click=canvas->widget_position_for_document_point(center);
  send_mouse(*canvas,QEvent::MouseButtonPress,click,Qt::LeftButton,Qt::LeftButton);
  send_mouse(*canvas,QEvent::MouseButtonRelease,click,Qt::LeftButton,Qt::NoButton);
  QApplication::processEvents();
  auto* editor=canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor!=nullptr);if(!editor)return;
  editor->setPlainText(QStringLiteral("Editable area text with hidden overflow. ").repeated(20));
  process_events_for(150);
  auto* overlay = canvas->findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay"));
  CHECK(overlay != nullptr && overlay->isVisible()); if (!overlay) return;
  const auto original_area = parse_vector_path(editor->property("patchy.documentTextArea").toString().toStdString());
  CHECK(original_area.has_value()); if (!original_area) return;
  const auto handles = overlay->property("patchy.transformedTextResizeHandleCenters").toList();
  CHECK(handles.size() == 4); if (handles.size() != 4) return;
  const auto corner = handles.back().toPointF().toPoint() - overlay->pos();
  send_mouse(*overlay,QEvent::MouseButtonPress,corner,Qt::LeftButton,Qt::LeftButton);
  send_mouse(*overlay,QEvent::MouseMove,corner+QPoint(12,8),Qt::NoButton,Qt::LeftButton);
  send_mouse(*overlay,QEvent::MouseMove,corner+QPoint(24,16),Qt::NoButton,Qt::LeftButton);
  send_mouse(*overlay,QEvent::MouseButtonRelease,corner+QPoint(24,16),Qt::LeftButton,Qt::NoButton);
  const auto resized_area = parse_vector_path(editor->property("patchy.documentTextArea").toString().toStdString());
  CHECK(resized_area.has_value()); if (!resized_area) return;
  const auto first_bounds=original_area->bounds(),last_bounds=resized_area->bounds();
  CHECK(std::abs((last_bounds->right-last_bounds->left)-(first_bounds->right-first_bounds->left)-24)<1.5);
  CHECK(std::abs((last_bounds->bottom-last_bounds->top)-(first_bounds->bottom-first_bounds->top)-16)<1.5);
  process_events_for(150);
  save_widget_artifact("ui_text_area_editing", *canvas);
  require_action_by_text(window,QStringLiteral("Move"))->trigger();QApplication::processEvents();
  const auto id=doc.active_layer_id();CHECK(id.has_value());if(!id)return;
  const auto* area_layer=doc.find_layer(*id);CHECK(area_layer && text_area_for_layer(*area_layer));if(!area_layer)return;
  const auto original=*area_layer;
  require_action_by_text(window,QStringLiteral("Type"))->trigger();
  send_mouse(*canvas,QEvent::MouseButtonPress,click,Qt::LeftButton,Qt::LeftButton);
  send_mouse(*canvas,QEvent::MouseButtonRelease,click,Qt::LeftButton,Qt::NoButton);
  QApplication::processEvents();
  editor=canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor!=nullptr);if(!editor)return;
  editor->selectAll();editor->insertPlainText(QStringLiteral("Cancelled edit"));
  send_key(*editor,Qt::Key_Escape);QApplication::processEvents();
  CHECK(doc.find_layer(*id)->metadata()==original.metadata());
  CHECK(text_area_for_layer(*doc.find_layer(*id))==text_area_for_layer(original));
  auto changed=*canvas->text_area_edit_target_path();
  changed.subpaths.front().anchors.front().anchor_x=source_path.bounds()->left-30;
  canvas->replace_path_edit_target(changed,{});
  CHECK(text_area_for_layer(*doc.find_layer(*id))!=text_area_for_layer(original));
  CHECK(doc.find_layer(source_id)->vector_shape()->path==source_path);
  require_action_by_text(window,QStringLiteral("Type"))->trigger();
  send_mouse(*canvas,QEvent::MouseButtonPress,click,Qt::LeftButton,Qt::LeftButton);
  send_mouse(*canvas,QEvent::MouseButtonRelease,click,Qt::LeftButton,Qt::NoButton);
  QApplication::processEvents();
  editor=canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  overlay=canvas->findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay"));
  CHECK(editor && overlay);if(!editor||!overlay)return;
  const auto polygon=overlay->property("patchy.transformedTextEditorPolygon").toList();
  CHECK(!polygon.empty());
  const auto boundary_left=canvas->widget_position_for_document_point(QPoint(static_cast<int>(source_path.bounds()->left-30),0)).x();
  if(!polygon.empty()) CHECK(polygon.front().toPointF().x()<=boundary_left);
  send_key(*editor,Qt::Key_Escape);QApplication::processEvents();
  const auto before_drag=*text_area_for_layer(*doc.find_layer(*id));
  const auto* target=canvas->text_area_edit_target_path();CHECK(target!=nullptr);if(!target)return;
  const auto anchor=target->subpaths.front().anchors.front();
  canvas->set_tool(CanvasTool::DirectSelect);
  const auto start=canvas->widget_position_for_document_point(QPoint(static_cast<int>(std::lround(anchor.anchor_x)),
                                                                   static_cast<int>(std::lround(anchor.anchor_y))));
  drag(*canvas,start,start+QPoint(18,12));
  CHECK(text_area_for_layer(*doc.find_layer(*id))!=before_drag);
  MainWindowTestAccess::undo(window);
  CHECK(text_area_for_layer(*doc.find_layer(*id))==before_drag);
}
}  // namespace

std::vector<patchy::test::TestCase> text_area_tests() {
  return {{"ui_text_area_full_band_spans",ui_text_area_full_band_spans},
          {"ui_text_area_concave_flow_alignment_and_vertical",ui_text_area_concave_flow_alignment_and_vertical},
          {"ui_text_area_photoshop_triangle_line_breaks_and_caret",ui_text_area_photoshop_triangle_line_breaks_and_caret},
          {"ui_text_area_script_creation_boundary_edit_and_undo",ui_text_area_script_creation_boundary_edit_and_undo},
          {"ui_text_area_depth_edit_transform_and_roundtrip",ui_text_area_depth_edit_transform_and_roundtrip},
          {"ui_text_area_empty_and_large_frames",ui_text_area_empty_and_large_frames},
          {"ui_text_area_psd_uses_postscript_font_names",ui_text_area_psd_uses_postscript_font_names},
          {"ui_text_area_type_tool_creation_cancel_and_boundary_edit",ui_text_area_type_tool_creation_cancel_and_boundary_edit}};
}
