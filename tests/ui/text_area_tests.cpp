#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "core/layer_metadata.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/font_face_name_index.hpp"
#include "test_harness.hpp"
#include <QApplication>
#include <QEventLoop>
#include <QFont>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDoubleSpinBox>
#include <QTextEdit>
#include <QPainter>
#include <QToolTip>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "ui/script_engine.hpp"
#include "ui/text_area_layout.hpp"
#include "ui/text_document.hpp"
#include "ui/text_render.hpp"
#include "core/text_area.hpp"
#include "core/pixel_depth.hpp"
#include "core/layer_render_utils.hpp"
#include "core/document_depth.hpp"
#include "ui/qt_paths.hpp"
#include "ui/theme_palette.hpp"
#include "psd/psd_document_io.hpp"
#include "local_psd_fixtures.hpp"
#include <QElapsedTimer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
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

// Ink columns [left, right) of the bottom band of rows that hold ink (the last line), in the
// render's local units. Empty when nothing is inked.
std::optional<std::pair<double, double>> last_line_ink_extent(const RenderedTextPixels& rendered) {
  const auto& pixels = rendered.pixels;
  const auto channels = pixels.format().channels;
  if (pixels.empty() || channels < 4U) return std::nullopt;
  const auto bytes = pixels.data();
  const auto stride = pixels.stride_bytes();
  const auto alpha = [&](int x, int y) {
    return bytes[static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * channels + 3U];
  };
  const auto row_inked = [&](int y) {
    for (int x = 0; x < pixels.width(); ++x) if (alpha(x, y) > 0) return true;
    return false;
  };
  int bottom = pixels.height() - 1;
  while (bottom >= 0 && !row_inked(bottom)) --bottom;
  if (bottom < 0) return std::nullopt;
  int top = bottom;
  while (top > 0 && row_inked(top - 1)) --top;
  int left = pixels.width();
  int right = -1;
  for (int y = top; y <= bottom; ++y) {
    for (int x = 0; x < pixels.width(); ++x) {
      if (alpha(x, y) > 0) { left = std::min(left, x); right = std::max(right, x); }
    }
  }
  return std::make_pair(rendered.local_rect.left() + left, rendered.local_rect.left() + right + 1);
}

// Photoshop's justify modes (#672), calibrated with Photoshop 2026 COM probes: Arial 20 px in a
// 300 px box at x = 50 ("Hello world", "Word"; layer bounds less 50). In a box a lone line is
// the last line: Justify Last Left, Centered and Right place it like Left, Center and Right, and
// Justify All spreads it over the box, a single word letter by letter. Point text sets every
// justify mode flush left at its anchor.
void ui_text_justify_last_line_modes_match_photoshop_if_available() {
  if (!QFontDatabase::hasFamily(QStringLiteral("Arial"))) {
    std::printf("  [SKIP] Arial is not installed\n");
    return;
  }
  const auto render = [](const QString& text, const QString& align, bool boxed) {
    TextToolSettings settings;
    settings.text = text;
    settings.family = QStringLiteral("Arial");
    settings.size = 20;
    settings.photoshop_layout = true;
    settings.boxed = boxed;
    settings.box_width = 300;
    settings.box_height = 100;
    const auto runs = QStringLiteral("v1\n0\t%1\t%2").arg(text.size()).arg(align);
    return last_line_ink_extent(render_text_pixels_with_local_rect(settings, QColor(Qt::black), 300, runs));
  };
  struct Expected {
    const char* text;
    const char* align;
    double left;
    double right;
  };
  const std::array<Expected, 7> box{{
      {"Hello world", "justify", 1, 98},
      {"Hello world", "justifyCenter", 102, 198},
      {"Hello world", "justifyRight", 202, 299},
      {"Hello world", "justifyAll", 1, 299},
      {"Word", "justify", 0, 46},
      {"Word", "justifyCenter", 126, 173},
      {"Word", "justifyAll", 0, 299},
  }};
  for (const auto& expected : box) {
    const auto ink = render(QString::fromLatin1(expected.text), QString::fromLatin1(expected.align), true);
    CHECK(ink.has_value());
    if (!ink.has_value()) continue;
    std::printf("  box %-11s %-13s ink %.0f..%.0f (photoshop %.0f..%.0f)\n", expected.text, expected.align,
                ink->first, ink->second, expected.left, expected.right);
    CHECK(std::abs(ink->first - expected.left) <= 1.0);
    CHECK(std::abs(ink->second - expected.right) <= 1.0);
  }
  const auto point_left = render(QStringLiteral("Hello world"), QStringLiteral("left"), false);
  CHECK(point_left.has_value());
  for (const auto* align : {"justify", "justifyCenter", "justifyRight", "justifyAll"}) {
    const auto ink = render(QStringLiteral("Hello world"), QString::fromLatin1(align), false);
    CHECK(ink.has_value() && point_left.has_value() && ink == point_left);
  }

  // Two lines: every mode justifies the first; the last line follows the mode.
  const auto two_lines = QStringLiteral("One two three four five six seven eight nine ten eleven");
  const auto first_line_right = [&](const QString& align) {
    TextToolSettings settings;
    settings.text = two_lines;
    settings.family = QStringLiteral("Arial");
    settings.size = 20;
    settings.photoshop_layout = true;
    settings.boxed = true;
    settings.box_width = 300;
    settings.box_height = 100;
    const auto built = build_text_render_document(settings, QColor(Qt::black), 300,
                                                  QStringLiteral("v1\n0\t%1\t%2").arg(two_lines.size()).arg(align),
                                                  QString(), 1.0);
    const auto plan = photoshop_text_layout_plan(*built.document, true);
    CHECK(plan.lines.size() == 2U);
    if (plan.lines.size() != 2U) return std::make_pair(0.0, 0.0);
    const auto block_text = built.document->begin().text();
    const auto line_right = [&block_text](const BoxTextLineRenderItem& item) {
      auto end = item.line.textStart() + item.line.textLength();
      while (end > item.line.textStart() && block_text.at(end - 1).isSpace()) --end;
      return item.block_origin.x() + item.line.cursorToX(end);
    };
    const auto& last = plan.lines.back();
    const auto last_left = last.block_origin.x() + last.line.cursorToX(last.line.textStart());
    return std::make_pair(line_right(plan.lines.front()), last_left);
  };
  for (const auto* align : {"justify", "justifyCenter", "justifyRight", "justifyAll"}) {
    const auto [first_right, last_left] = first_line_right(QString::fromLatin1(align));
    std::printf("  two lines %-13s first line ends %.2f, last line starts %.2f\n", align, first_right, last_left);
    CHECK(std::abs(first_right - 300.0) <= 0.5);
    if (std::string_view(align) == "justify" || std::string_view(align) == "justifyAll") {
      CHECK(std::abs(last_left) <= 0.5);
    } else {
      CHECK(last_left > 20.0);
    }
  }

  // An area flows the same way: the last line sits in its own span's slack.
  QTextDocument document;
  QFont font(QStringLiteral("Arial")); font.setPixelSize(20);
  document.setDefaultFont(font); document.setDocumentMargin(0);
  document.setPlainText(QStringLiteral("Hello world"));
  QTextCursor cursor(&document); cursor.select(QTextCursor::Document);
  QTextCharFormat format; format.setFont(font); format.setProperty(kTextExactSizeFormatProperty, 20.0);
  cursor.setCharFormat(format);
  const auto area = polygon({{0, 0}, {300, 0}, {300, 100}, {0, 100}});
  const auto area_right_edge = [&](const QString& align) {
    QTextBlockFormat paragraph;
    set_paragraph_alignment(paragraph, align);
    cursor.setBlockFormat(paragraph);
    const auto plan = area_text_layout_plan(document, area);
    CHECK(plan.lines.size() == 1U);
    if (plan.lines.empty()) return 0.0;
    const auto& item = plan.lines.front();
    return item.block_origin.x() + item.line.cursorToX(item.line.textStart() + item.line.textLength());
  };
  const auto natural = area_right_edge(QStringLiteral("justify"));
  CHECK(natural < 120.0);
  CHECK(std::abs(area_right_edge(QStringLiteral("justifyRight")) - 300.0) <= 0.5);
  CHECK(std::abs(area_right_edge(QStringLiteral("justifyCenter")) - (150.0 + natural / 2.0)) <= 0.5);
}

// The alignment names are persisted (paragraph runs, script API): every one round-trips through
// a block format, and an unknown name leaves the format alone.
void ui_text_paragraph_alignment_names_round_trip() {
  for (const auto* name : {"left", "center", "right", "justify", "justifyCenter", "justifyRight", "justifyAll"}) {
    QTextBlockFormat format;
    set_paragraph_alignment(format, QString::fromLatin1(name).toUpper());
    CHECK(paragraph_alignment_name(format) == QLatin1String(name));
    CHECK(canonical_paragraph_alignment_name(QString::fromLatin1(name).toLower()) == QLatin1String(name));
  }
  QTextBlockFormat format;
  set_paragraph_alignment(format, QStringLiteral("justifyAll"));
  set_paragraph_alignment(format, QStringLiteral("sideways"));
  CHECK(paragraph_alignment_name(format) == QStringLiteral("justifyAll"));
  set_paragraph_alignment(format, QStringLiteral("center"));
  CHECK(paragraph_alignment_name(format) == QStringLiteral("center"));
  CHECK(text_justify_last_line(format) == TextJustifyLastLine::Left);
  CHECK(canonical_paragraph_alignment_name(QStringLiteral("justified")).isEmpty());
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

// Point and box text name their faces by PostScript name too, on every platform (GitHub issue
// 646: Linux wrote Qt family names, "Roboto Mono [GOOG]", which Photoshop could not find). A
// family with no installed face keeps its own name instead of a substitute's.
void ui_text_point_psd_uses_postscript_font_names() {
  MainWindow window; show_window_empty(window);
  const bool arial = QFontDatabase::families().contains(QStringLiteral("Arial"));
  const auto family = arial ? QStringLiteral("Arial") : QStringLiteral("Liberation Sans");
  CHECK(QFontDatabase::families().contains(family));
  run(window,QStringLiteral(R"JS(
    var d=app.newDocument(400,300);
    for(var i=0;i<4;i++) {
      var t=d.addTextLayer('Font',{x:20,y:40+50*i,font:'%1',size:24,bold:!!(i&1),italic:!!(i&2)});
      t.name='Font '+i;
    }
    var m=d.addTextLayer('Missing',{x:200,y:40,font:'Patchy Issue Six Four Six',size:24});
    m.name='Missing';
  )JS").arg(family));
  const auto bytes = psd::DocumentIo::write_layered_rgb8(MainWindowTestAccess::document(window));
  const auto reopened = psd::DocumentIo::read(bytes);
  const std::array<std::string,4> names = arial
      ? std::array<std::string,4>{"ArialMT","Arial-BoldMT","Arial-ItalicMT","Arial-BoldItalicMT"}
      : std::array<std::string,4>{"LiberationSans","LiberationSans-Bold","LiberationSans-Italic","LiberationSans-BoldItalic"};
  const auto check_name = [&](const std::string& layer_name, const std::string& font_name) {
    std::vector<std::uint8_t> needle{0xfe,0xff};
    for(const auto c:font_name) {needle.push_back(0);needle.push_back(static_cast<std::uint8_t>(c));}
    const auto contains = [&](const UnknownPsdBlock& block) {
      return std::search(block.payload.begin(),block.payload.end(),needle.begin(),needle.end())!=block.payload.end();
    };
    const auto* layer=layer_named(reopened,layer_name);
    CHECK(layer!=nullptr);if(!layer)return;
    const bool in_tysh = std::any_of(layer->unknown_psd_blocks().begin(),layer->unknown_psd_blocks().end(),
                                     [&](const auto& b){return b.key=="TySh" && contains(b);});
    if(!in_tysh) std::fprintf(stderr,"  %s: TySh lacks %s\n",layer_name.c_str(),font_name.c_str());
    CHECK(in_tysh);
    const auto& globals=reopened.metadata().unknown_psd_resources;
    CHECK(std::any_of(globals.begin(),globals.end(),[&](const auto& b){return b.key=="Txt2" && contains(b);}));
  };
  for(std::size_t i=0;i<names.size();++i) check_name("Font "+std::to_string(i),names[i]);
  check_name("Missing","Patchy Issue Six Four Six");
}

// A variable font's name id 6 names only its default instance; a run on another instance is
// written under that instance's PostScript name (GitHub issue 646: Google's Roboto Mono and
// Roboto Slab ship as variable fonts).
void ui_text_variable_font_instances_get_their_own_postscript_names() {
  const auto put_u16 = [](QByteArray& bytes, quint16 value) {
    bytes.append(static_cast<char>(value >> 8U));
    bytes.append(static_cast<char>(value & 0xFFU));
  };
  const auto name_table = [&](const std::vector<std::pair<quint16, QString>>& records) {
    QByteArray table;
    QByteArray strings;
    put_u16(table, 0);
    put_u16(table, static_cast<quint16>(records.size()));
    put_u16(table, static_cast<quint16>(6 + 12 * records.size()));
    for (const auto& [id, text] : records) {
      QByteArray encoded;
      for (const auto ch : text) put_u16(encoded, ch.unicode());
      for (const quint16 value : {quint16{3}, quint16{1}, quint16{0x0409}, id, static_cast<quint16>(encoded.size()),
                                  static_cast<quint16>(strings.size())}) {
        put_u16(table, value);
      }
      strings.append(encoded);
    }
    return table + strings;
  };
  // One wght axis; instances (subfamily name id, optional PostScript name id).
  const auto fvar_table = [&](const std::vector<std::pair<quint16, quint16>>& instances, bool with_postscript_ids) {
    QByteArray table;
    const quint16 instance_size = with_postscript_ids ? 10 : 8;
    for (const quint16 value : {quint16{1}, quint16{0}, quint16{16}, quint16{2}, quint16{1}, quint16{20},
                                static_cast<quint16>(instances.size()), instance_size}) {
      put_u16(table, value);
    }
    table.append("wght", 4);
    for (int i = 0; i < 6; ++i) put_u16(table, 0);  // min, default, max (Fixed)
    put_u16(table, 0);                              // flags
    put_u16(table, 256);                            // axis name id
    for (const auto& [subfamily, postscript] : instances) {
      put_u16(table, subfamily);
      put_u16(table, 0);
      put_u16(table, 0);
      put_u16(table, 0);  // wght coordinate
      if (with_postscript_ids) put_u16(table, postscript);
    }
    return table;
  };
  std::vector<std::pair<quint16, QString>> records{
      {1, QStringLiteral("Roboto Mono")},     {2, QStringLiteral("Thin")},
      {6, QStringLiteral("RobotoMono-Thin")}, {16, QStringLiteral("Roboto Mono")},
      {256, QStringLiteral("Weight")},        {257, QStringLiteral("Thin")},
      {258, QStringLiteral("Regular")},       {259, QStringLiteral("Bold")},
      {260, QStringLiteral("Semi Bold")},     {261, QStringLiteral("RobotoMono-BoldCustom")}};
  const auto names = name_table(records);
  const auto plain = fvar_table({{257, 0}, {258, 0}, {259, 0}, {260, 0}}, false);
  using patchy::ui::variable_instance_postscript_name;
  CHECK(variable_instance_postscript_name(plain, names, QStringLiteral("Regular")) == QStringLiteral("RobotoMono-Regular"));
  CHECK(variable_instance_postscript_name(plain, names, QStringLiteral("bold")) == QStringLiteral("RobotoMono-Bold"));
  CHECK(variable_instance_postscript_name(plain, names, QStringLiteral("SemiBold")) == QStringLiteral("RobotoMono-SemiBold"));
  CHECK(!variable_instance_postscript_name(plain, names, QStringLiteral("Medium")).has_value());
  CHECK(!variable_instance_postscript_name(QByteArray(), names, QStringLiteral("Regular")).has_value());
  // The instance's own PostScript name record wins; 0xFFFF means it has none.
  const auto with_ids = fvar_table({{258, 0xFFFF}, {259, 261}}, true);
  CHECK(variable_instance_postscript_name(with_ids, names, QStringLiteral("Bold")) == QStringLiteral("RobotoMono-BoldCustom"));
  CHECK(variable_instance_postscript_name(with_ids, names, QStringLiteral("Regular")) == QStringLiteral("RobotoMono-Regular"));
  // The variations PostScript name prefix (name id 25) replaces the family.
  records.push_back({25, QStringLiteral("RobotoMonoVF")});
  CHECK(variable_instance_postscript_name(plain, name_table(records), QStringLiteral("Bold")) ==
        QStringLiteral("RobotoMonoVF-Bold"));
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
  CHECK(editor->property("patchy.textHiddenLines").toBool());  // issue 631: the overlay's plus
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
// Document row just below a layer's lowest opaque pixel, or nullopt for an empty raster.
std::optional<int> layer_ink_bottom(const Layer& layer) {
  const auto ink = visible_alpha_local_bounds(layer.pixels());
  if (!ink) return std::nullopt;
  return layer.bounds().y + ink->y + ink->height;
}

std::string layer_story(const Layer& layer) {
  const auto found = layer.metadata().find(kLayerMetadataText);
  return found == layer.metadata().end() ? std::string() : found->second;
}

// GitHub issue 621: a box one line tall holding two lines drew the second line whole below the
// box, because a line was kept while its TOP was above the frame bottom. Photoshop keeps a line
// only while its baseline fits (docs/area-text.md, "Box text overflow"). Arial 40 px:
// the second line starts about 46 px down (inside the 70 px frame) with its baseline near 82.
void ui_box_text_overflow_hides_lines_below_the_frame() {
  MainWindow window; show_window_empty(window);
  window.set_cli_automation_mode(true);  // Match --run-script's missing-font substitution on Linux.
  run(window,R"JS(
    var d=app.newDocument(1100,300);
    var t=d.addTextLayer("Let's give this a try\nMy name is Yousuf",{font:'Arial',size:40,x:20,y:20,box:{width:500,height:70}});
    t.name='Short Box';
    var tall=d.addTextLayer("Let's give this a try\nMy name is Yousuf",{font:'Arial',size:40,x:20,y:160,box:{width:500,height:120}});
    tall.name='Tall Box';
    var mid=d.addTextLayer("Let's give this a try\nMy name is Yousuf",{font:'Arial',size:40,x:540,y:20,box:{width:500,height:86}});
    mid.name='Mid Box';
  )JS");
  auto& doc = MainWindowTestAccess::document(window);
  const auto* short_box = layer_named(doc, "Short Box");
  const auto* tall_box = layer_named(doc, "Tall Box");
  CHECK(short_box && tall_box); if (!short_box || !tall_box) return;
  const auto short_bottom = layer_ink_bottom(*short_box);
  const auto tall_bottom = layer_ink_bottom(*tall_box);
  CHECK(short_bottom.has_value() && tall_bottom.has_value()); if (!short_bottom || !tall_bottom) return;
  std::fprintf(stderr, "i621 ink bottoms: short %d (frame 90), tall %d (frame 280)\n", *short_bottom, *tall_bottom);
  CHECK(*short_bottom <= 20 + 70);                // only the first line draws
  CHECK(*tall_bottom - 160 > *short_bottom - 20 + 30);  // both lines fit the taller box
  CHECK(layer_story(*short_box).find("My name is Yousuf") != std::string::npos);  // hiding is render-only
  // The 86 px frame holds the second baseline (about 82) but not the "y" below it: that line
  // keeps its place and draws whole, descender past the frame included.
  const auto* mid_box = layer_named(doc, "Mid Box");
  CHECK(mid_box != nullptr);
  if (mid_box) {
    const auto mid_bottom = layer_ink_bottom(*mid_box);
    CHECK(mid_bottom.has_value() && *mid_bottom > 20 + 86 && *mid_bottom - 20 == *tall_bottom - 160);
  }
  save_widget_artifact("ui_box_text_overflow_hides_lines_below_the_frame", *require_canvas(window));

  // An older raster that baked the overflow line in must not keep it on the next edit: the
  // re-edit renders into the old ink's rect, but only the frame decides which lines show.
  auto* stale = doc.find_layer(tall_box->id());
  CHECK(stale != nullptr); if (!stale) return;
  stale->metadata()[kLayerMetadataTextBoxHeight] = "70";
  run(window,"var t=app.activeDocument.findLayer('Tall Box');t.text=t.text+'.';");
  const auto* reedited = layer_named(doc, "Tall Box");
  CHECK(reedited != nullptr); if (!reedited) return;
  const auto reedited_bottom = layer_ink_bottom(*reedited);
  CHECK(reedited_bottom.has_value() && *reedited_bottom <= 160 + 70);
  CHECK(layer_story(*reedited).find("My name is Yousuf.") != std::string::npos);
}

// Photoshop 2026 box text, Arial 24 px in a 300 x 40 box at (10, 10): the second line's
// baseline (55.98) is past the frame, so Photoshop drew only the first line (ink rows 9..31).
// Patchy keeps the hidden line in the story through an edit and a PSD round trip, and its own
// render hides it the same way.
void ui_box_text_overflow_photoshop_story_survives_edit_and_round_trip() {
  const auto fixture = patchy::test::committed_psd_fixture_path("photoshop-box-text-overflow.psd");
  const auto imported = psd::DocumentIo::read_file(fixture);
  const auto* original = layer_named(imported, "Overflow Box");
  CHECK(original != nullptr); if (!original) return;
  CHECK(layer_story(*original).find("My name is Yousuf") != std::string::npos);
  const auto photoshop_bottom = layer_ink_bottom(*original);
  CHECK(photoshop_bottom.has_value() && *photoshop_bottom <= 10 + 40);

  MainWindow window; show_window_empty(window);
  window.set_cli_automation_mode(true);
  const auto quoted = QString::fromUtf8(QJsonDocument(QJsonArray{to_qstring(fixture)}).toJson(QJsonDocument::Compact));
  run(window, QStringLiteral("var d=app.open(%1[0]);var t=d.findLayer('Overflow Box');"
                             "t.text=t.text.replace('try','try now');check(t.text.indexOf('My name is Yousuf')>0);")
                  .arg(quoted));
  const auto& doc = MainWindowTestAccess::document(window);
  const auto* edited = layer_named(doc, "Overflow Box");
  CHECK(edited != nullptr); if (!edited) return;
  const auto edited_bottom = layer_ink_bottom(*edited);
  CHECK(edited_bottom.has_value() && *edited_bottom <= 10 + 40);
  const auto bytes = psd::DocumentIo::write_layered_rgb8(doc);
  const auto reopened = psd::DocumentIo::read(bytes);
  const auto* round_trip = layer_named(reopened, "Overflow Box");
  CHECK(round_trip != nullptr); if (!round_trip) return;
  CHECK(layer_story(*round_trip).find("try now") != std::string::npos);
  CHECK(layer_story(*round_trip).find("My name is Yousuf") != std::string::npos);
  const auto round_trip_bottom = layer_ink_bottom(*round_trip);
  CHECK(round_trip_bottom.has_value() && *round_trip_bottom <= 10 + 40);
}

// GitHub issue 631: while the Type tool edits a box that hides lines of its story, its bottom
// right handle carries a plus. It follows the layout live: dragging the box tall enough or
// deleting the hidden text clears it, typing past the bottom brings it back, and a box whose
// story fits never shows it. The handle still resizes the box.
void ui_box_text_overflow_marker_on_bottom_right_handle() {
  MainWindow window; show_window_empty(window);
  window.set_cli_automation_mode(true);  // Match --run-script's missing-font substitution on Linux.
  run(window, "app.newDocument(640,420);");
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  auto* text_size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  CHECK(text_size != nullptr); if (!text_size) return;
  const auto bottom_right = [canvas] {
    return canvas->findChild<QWidget*>(QStringLiteral("textBoxResizeHandleBottomRight"), Qt::FindDirectChildrenOnly);
  };
  // The plus is the border role's color through the handle's centre; a plain handle is its fill.
  const auto centre_shows_plus = [](QWidget& handle) {
    const auto image = handle.grab().toImage();
    const auto centre = image.pixelColor(image.width() / 2, image.height() / 2);
    const auto border = theme().resize_handle_border;
    return std::abs(centre.red() - border.red()) + std::abs(centre.green() - border.green()) +
               std::abs(centre.blue() - border.blue()) < 40;
  };
  const auto edit_at = [&](QPoint document_point) -> QTextEdit* {
    require_action_by_text(window, QStringLiteral("Type"))->trigger();
    const auto click = canvas->widget_position_for_document_point(document_point);
    send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
    process_events_for(150);
    return canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  };

  // A box dragged out with the Type tool, then typed past its bottom.
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  text_size->setValue(12.0);  // no text layer yet, so this only sets the new box's size
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 40)),
       canvas->widget_position_for_document_point(QPoint(280, 90)));
  process_events_for(150);
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr); if (!editor) return;
  CHECK(editor->property("patchy.documentTextFlow").toString() == QStringLiteral("box"));
  editor->setPlainText(QStringLiteral("One"));
  process_events_for(150);
  auto* handle = bottom_right();
  CHECK(handle != nullptr); if (!handle) return;
  CHECK(!editor->property("patchy.textHiddenLines").toBool());
  CHECK(!handle->property("patchy.textOverflowMarker").toBool());
  CHECK(!centre_shows_plus(*handle));
  editor->moveCursor(QTextCursor::End);
  editor->insertPlainText(QStringLiteral("\nTwo\nThree\nFour"));
  process_events_for(150);
  handle = bottom_right();
  CHECK(handle != nullptr); if (!handle) return;
  CHECK(editor->property("patchy.textHiddenLines").toBool());
  CHECK(handle->property("patchy.textOverflowMarker").toBool());
  CHECK(centre_shows_plus(*handle));
  auto* top_left = canvas->findChild<QWidget*>(QStringLiteral("textBoxResizeHandleTopLeft"), Qt::FindDirectChildrenOnly);
  CHECK(top_left != nullptr && !top_left->property("patchy.textOverflowMarker").toBool());
  if (top_left) CHECK(!centre_shows_plus(*top_left));
  save_widget_artifact("ui_box_text_overflow_marker_shown", *canvas);

  // Dragging the marked handle still resizes the box; once every line fits, the plus goes.
  const auto height_before = editor->property("patchy.documentTextHeight").toInt();
  const auto grow = std::max(160, editor->property("patchy.documentTextSize").toInt() * 6);
  editor->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  const auto handle_center = handle->geometry().center();
  drag(*canvas, handle_center, handle_center + QPoint(0, grow));
  process_events_for(150);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == editor);
  CHECK(editor->property("patchy.documentTextHeight").toInt() >= height_before + grow - 10);
  CHECK(!editor->property("patchy.textHiddenLines").toBool());
  handle = bottom_right();
  CHECK(handle != nullptr); if (!handle) return;
  CHECK(!handle->property("patchy.textOverflowMarker").toBool());
  CHECK(!centre_shows_plus(*handle));
  save_widget_artifact("ui_box_text_overflow_marker_cleared", *canvas);

  // Typing past the new bottom brings it back; deleting that text clears it again.
  editor->moveCursor(QTextCursor::End);
  editor->insertPlainText(QStringLiteral("\nFive\nSix\nSeven\nEight\nNine\nTen\nEleven\nTwelve"));
  process_events_for(150);
  CHECK(editor->property("patchy.textHiddenLines").toBool());
  handle = bottom_right();
  CHECK(handle != nullptr && handle->property("patchy.textOverflowMarker").toBool());
  editor->setPlainText(QStringLiteral("Short"));
  process_events_for(150);
  CHECK(!editor->property("patchy.textHiddenLines").toBool());
  handle = bottom_right();
  CHECK(handle != nullptr && !handle->property("patchy.textOverflowMarker").toBool());
  send_key(*editor, Qt::Key_Escape);
  process_events_for(100);

  // Re-editing a scripted box whose second line is hidden marks it from the first frame; one
  // whose story fits never does. No event loop runs between these sessions, so each finds the
  // previous session's handles still pending deletion (remove_text_editor_handles).
  run(window,R"JS(
    var d=app.activeDocument;
    d.addTextLayer("Let's give this a try\nMy name is Yousuf",{font:'Arial',size:40,x:330,y:40,box:{width:280,height:70}});
    d.addTextLayer("This fits",{font:'Arial',size:40,x:330,y:260,box:{width:280,height:120}});
  )JS");
  QApplication::processEvents();
  editor = edit_at(QPoint(350, 60));
  CHECK(editor != nullptr); if (!editor) return;
  CHECK(editor->property("patchy.documentTextFlow").toString() == QStringLiteral("box"));
  CHECK(editor->property("patchy.textHiddenLines").toBool());
  handle = bottom_right();
  CHECK(handle != nullptr && handle->property("patchy.textOverflowMarker").toBool());
  if (handle) CHECK(centre_shows_plus(*handle));
  save_widget_artifact("ui_box_text_overflow_marker_reedit", *canvas);
  send_key(*editor, Qt::Key_Escape);
  process_events_for(100);
  editor = edit_at(QPoint(350, 280));
  CHECK(editor != nullptr); if (!editor) return;
  CHECK(!editor->property("patchy.textHiddenLines").toBool());
  handle = bottom_right();
  CHECK(handle != nullptr && !handle->property("patchy.textOverflowMarker").toBool());
  send_key(*editor, Qt::Key_Escape);
  process_events_for(100);
}
// GitHub issue 631 follow-up: hovering the plus explains it. A tooltip event on the marked
// bottom right handle shows the hint; a handle without the plus, or a box whose text fits, shows
// none, and the hint goes when the plus clears. Covers the flat handle widget and the area
// frame's painted handles (transformed overlay).
void ui_box_text_overflow_marker_tooltip() {
  MainWindow window; show_window_empty(window);
  window.set_cli_automation_mode(true);
  run(window, "app.newDocument(640,420);");
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  auto* text_size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  CHECK(text_size != nullptr); if (!text_size) return;
  const auto hint = QStringLiteral("Some text does not fit. Drag to enlarge the box.");
  const auto shows_hint = [&] { return QToolTip::isVisible() && QToolTip::text() == hint; };
  // QToolTip::hideText() lets the label linger for a short timer, so absence is waited for.
  const auto hint_gone = [&] {
    for (int i = 0; i < 20 && shows_hint(); ++i) {
      process_events_for(50);
    }
    return !shows_hint();
  };
  const auto hover = [](QWidget& widget, QPoint local) {
    QHelpEvent help(QEvent::ToolTip, local, widget.mapToGlobal(local));
    QApplication::sendEvent(&widget, &help);
    QApplication::processEvents();
  };
  const auto handle_named = [canvas](const char* name) {
    return canvas->findChild<QWidget*>(QString::fromLatin1(name), Qt::FindDirectChildrenOnly);
  };

  // A flat box: no hint while the text fits, the hint once lines hide, none on another handle.
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  text_size->setValue(12.0);
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(20, 40)),
       canvas->widget_position_for_document_point(QPoint(280, 90)));
  process_events_for(150);
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr); if (!editor) return;
  editor->setPlainText(QStringLiteral("One"));
  process_events_for(150);
  auto* handle = handle_named("textBoxResizeHandleBottomRight");
  CHECK(handle != nullptr); if (!handle) return;
  CHECK(handle->toolTip().isEmpty());
  hover(*handle, handle->rect().center());
  CHECK(hint_gone());
  editor->moveCursor(QTextCursor::End);
  editor->insertPlainText(QStringLiteral("\nTwo\nThree\nFour"));
  process_events_for(150);
  handle = handle_named("textBoxResizeHandleBottomRight");
  CHECK(handle != nullptr && handle->property("patchy.textOverflowMarker").toBool()); if (!handle) return;
  CHECK(handle->toolTip() == hint);
  hover(*handle, handle->rect().center());
  CHECK(shows_hint());
  {
    // The tip is a child of the handle: the handle's own sheet must not reach it, so it
    // paints in the tooltip palette's base. The artifact paints the tip (its own window)
    // over the canvas grab, cropped to the box.
    auto shot = canvas->grab().toImage();
    QRect area = editor->geometry();
    for (auto* top : QApplication::topLevelWidgets()) {
      if (top->inherits("QTipLabel") && top->isVisible()) {
        const auto tip_image = top->grab().toImage();
        const auto base = QToolTip::palette().color(QPalette::ToolTipBase);
        const auto inside = tip_image.pixelColor(tip_image.width() / 2, 2);
        CHECK(std::abs(inside.red() - base.red()) + std::abs(inside.green() - base.green()) +
                  std::abs(inside.blue() - base.blue()) < 24);
        const QRect tip(canvas->mapFromGlobal(top->pos()), top->size());
        QPainter painter(&shot);
        painter.drawImage(tip.topLeft(), tip_image);
        area = area.united(tip);
      }
    }
    save_image_artifact("ui_box_text_overflow_marker_tooltip",
                        shot.copy(area.adjusted(-24, -24, 24, 24).intersected(shot.rect())));
  }
  auto* top_left = handle_named("textBoxResizeHandleTopLeft");
  CHECK(top_left != nullptr && top_left->toolTip().isEmpty());
  // Removing the hidden text clears the plus and the hint showing for it.
  editor->setPlainText(QStringLiteral("Short"));
  process_events_for(150);
  CHECK(!editor->property("patchy.textHiddenLines").toBool());
  CHECK(hint_gone());
  handle = handle_named("textBoxResizeHandleBottomRight");
  CHECK(handle != nullptr && handle->toolTip().isEmpty());
  send_key(*editor, Qt::Key_Escape);
  process_events_for(100);
  QToolTip::hideText();

  // An area frame (painted handles on the transformed overlay).
  run(window, R"JS(
    var d=app.activeDocument;
    d.addShape('Source',{type:'polygon',cx:330,cy:260,radius:150,sides:3});
  )JS");
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const QPoint center(330, 270);
  CHECK(canvas->text_area_at(center).has_value());
  const auto click = canvas->widget_position_for_document_point(center);
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  process_events_for(150);
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr); if (!editor) return;
  editor->setPlainText(QStringLiteral("Fits"));
  process_events_for(150);
  auto* overlay = canvas->findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay"));
  CHECK(overlay != nullptr && overlay->isVisible()); if (!overlay) return;
  auto handles = overlay->property("patchy.transformedTextResizeHandleCenters").toList();
  CHECK(handles.size() == 4); if (handles.size() != 4) return;
  CHECK(!editor->property("patchy.textHiddenLines").toBool());
  hover(*overlay, handles.back().toPointF().toPoint() - overlay->pos());
  CHECK(hint_gone());
  editor->setPlainText(QStringLiteral("Area text with hidden overflow. ").repeated(30));
  process_events_for(150);
  CHECK(editor->property("patchy.textHiddenLines").toBool());
  handles = overlay->property("patchy.transformedTextResizeHandleCenters").toList();
  CHECK(handles.size() == 4); if (handles.size() != 4) return;
  const auto corner = handles.back().toPointF().toPoint() - overlay->pos();
  hover(*overlay, corner);
  CHECK(shows_hint());
  hover(*overlay, handles.front().toPointF().toPoint() - overlay->pos());
  CHECK(hint_gone());
  send_key(*editor, Qt::Key_Escape);
  process_events_for(100);
  QToolTip::hideText();
}
// GitHub issue 684: a box session has a handle on each side as well as the corners. A side
// handle moves only its own edge (the opposite edge stays put) and the text rewraps to the new
// width; its cursor is the horizontal or vertical size cursor. A box too small on screen keeps
// only its corners, so a press near a corner resizes from the corner.
void ui_box_text_side_handles_move_one_edge() {
  MainWindow window; show_window_empty(window);
  window.set_cli_automation_mode(true);
  run(window, "app.newDocument(640,420);");
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  auto* text_size = window.findChild<QDoubleSpinBox*>(QStringLiteral("textSizeSpin"));
  CHECK(text_size != nullptr); if (!text_size) return;
  const auto handle_named = [canvas](const char* name) {
    return canvas->findChild<QWidget*>(QString::fromLatin1(name), Qt::FindDirectChildrenOnly);
  };
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  text_size->setValue(12.0);
  QApplication::processEvents();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(40, 60)),
       canvas->widget_position_for_document_point(QPoint(340, 260)));
  process_events_for(150);
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr); if (!editor) return;
  CHECK(editor->property("patchy.documentTextFlow").toString() == QStringLiteral("box"));
  editor->setPlainText(QStringLiteral("Side handles move a single edge of the text box. ").repeated(4));
  process_events_for(150);

  struct Box {
    int x, y, width, height;
  };
  const auto box = [&] {
    return Box{editor->property("patchy.documentTextX").toInt(), editor->property("patchy.documentTextY").toInt(),
               editor->property("patchy.documentTextWidth").toInt(),
               editor->property("patchy.documentTextHeight").toInt()};
  };
  const auto layout_height = [&] { return editor->document()->size().height(); };

  // All eight handles show; the sides sit at the edge midpoints with straight size cursors.
  for (const auto* name : {"textBoxResizeHandleTopLeft", "textBoxResizeHandleTopRight", "textBoxResizeHandleBottomLeft",
                           "textBoxResizeHandleBottomRight", "textBoxResizeHandleTop", "textBoxResizeHandleBottom",
                           "textBoxResizeHandleLeft", "textBoxResizeHandleRight"}) {
    auto* handle = handle_named(name);
    CHECK(handle != nullptr && handle->isVisible());
  }
  {
    const auto frame = editor->geometry();
    auto* top = handle_named("textBoxResizeHandleTop");
    auto* bottom = handle_named("textBoxResizeHandleBottom");
    auto* left = handle_named("textBoxResizeHandleLeft");
    auto* right = handle_named("textBoxResizeHandleRight");
    if (!top || !bottom || !left || !right) return;
    CHECK(std::abs(top->geometry().center().x() - frame.center().x()) <= 1);
    CHECK(std::abs(top->geometry().center().y() - frame.top()) <= 1);
    CHECK(std::abs(bottom->geometry().center().y() - (frame.bottom() + 1)) <= 1);
    CHECK(std::abs(left->geometry().center().y() - frame.center().y()) <= 1);
    CHECK(std::abs(left->geometry().center().x() - frame.left()) <= 1);
    CHECK(std::abs(right->geometry().center().x() - (frame.right() + 1)) <= 1);
    CHECK(top->cursor().shape() == Qt::SizeVerCursor && bottom->cursor().shape() == Qt::SizeVerCursor);
    CHECK(left->cursor().shape() == Qt::SizeHorCursor && right->cursor().shape() == Qt::SizeHorCursor);
    CHECK(!bottom->property("patchy.textOverflowMarker").toBool());
  }
  {
    const auto shot = canvas->grab().toImage();
    save_image_artifact("ui_box_text_side_handles",
                        shot.copy(editor->geometry().adjusted(-40, -30, 40, 30).intersected(shot.rect())));
  }

  const auto drag_handle = [&](const char* name, QPoint delta) {
    auto* handle = handle_named(name);
    CHECK(handle != nullptr && handle->isVisible()); if (!handle) return;
    editor->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    const auto center = handle->geometry().center();
    drag(*canvas, center, center + delta);
    process_events_for(150);
    CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == editor);
  };

  // Right edge in: only the width changes, the left edge stays, the text wraps into more lines.
  auto before = box();
  auto lines_before = layout_height();
  drag_handle("textBoxResizeHandleRight", QPoint(-120, 17));
  auto after = box();
  CHECK(after.x == before.x && after.y == before.y && after.height == before.height);
  CHECK(std::abs(after.width - (before.width - 120)) <= 1);
  CHECK(layout_height() > lines_before + 4.0);

  // Left edge in: the box origin moves, the right edge stays.
  before = box();
  lines_before = layout_height();
  drag_handle("textBoxResizeHandleLeft", QPoint(30, -11));
  after = box();
  CHECK(std::abs(after.x - (before.x + 30)) <= 1);
  CHECK(std::abs((after.x + after.width) - (before.x + before.width)) <= 1);
  CHECK(after.y == before.y && after.height == before.height);
  CHECK(layout_height() > lines_before);

  // Bottom edge down: only the height changes.
  before = box();
  drag_handle("textBoxResizeHandleBottom", QPoint(23, 40));
  after = box();
  CHECK(after.x == before.x && after.y == before.y && after.width == before.width);
  CHECK(std::abs(after.height - (before.height + 40)) <= 1);

  // Top edge up: the origin moves up, the bottom edge stays.
  before = box();
  drag_handle("textBoxResizeHandleTop", QPoint(-9, -25));
  after = box();
  CHECK(std::abs(after.y - (before.y - 25)) <= 1);
  CHECK(std::abs((after.y + after.height) - (before.y + before.height)) <= 1);
  CHECK(after.x == before.x && after.width == before.width);

  // The same minimum as the corners: a side dragged past the opposite edge stops there.
  before = box();
  drag_handle("textBoxResizeHandleRight", QPoint(-before.width - 200, 0));
  after = box();
  CHECK(after.x == before.x && after.width == 16);

  // A box small on screen hides its side handles; a press just inside a corner's reach, toward
  // where the side handle would be, resizes from the corner (both edges move).
  drag_handle("textBoxResizeHandleRight", QPoint(160, 0));
  canvas->set_zoom(0.15);
  process_events_for(150);
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr); if (!editor) return;
  CHECK(editor->geometry().width() < 50);
  for (const auto* name : {"textBoxResizeHandleTop", "textBoxResizeHandleBottom"}) {
    auto* handle = handle_named(name);
    CHECK(handle == nullptr || !handle->isVisible());
  }
  for (const auto* name : {"textBoxResizeHandleLeft", "textBoxResizeHandleRight"}) {
    auto* handle = handle_named(name);
    CHECK((handle != nullptr && handle->isVisible()) == (editor->geometry().height() >= 50));
  }
  auto* corner = handle_named("textBoxResizeHandleBottomRight");
  CHECK(corner != nullptr && corner->isVisible()); if (!corner) return;
  before = box();
  editor->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  const auto press = corner->geometry().center() + QPoint(-8, 0);
  drag(*canvas, press, press + QPoint(10, 10));
  process_events_for(150);
  CHECK(canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor")) == editor);
  after = box();
  CHECK(after.x == before.x && after.y == before.y);
  CHECK(after.width >= before.width + 30 && after.height >= before.height + 30);
  send_key(*editor, Qt::Key_Escape);
  process_events_for(100);
}

// GitHub issue 684 on the transformed overlay: a box turned 90 degrees by Free Transform edits
// through the overlay, whose side handles follow the box's axes. The right side then points down
// the screen (vertical size cursor) and the bottom side points sideways (horizontal); dragging
// either outward grows only its own dimension and leaves the opposite side where it was. An area
// frame (also an overlay session) gets side handles too.
void ui_transformed_text_side_handles_follow_rotation() {
  MainWindow window; show_window_empty(window);
  window.set_cli_automation_mode(true);
  run(window, "app.newDocument(640,480);");
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  drag(*canvas, canvas->widget_position_for_document_point(QPoint(200, 180)),
       canvas->widget_position_for_document_point(QPoint(440, 300)));
  process_events_for(150);
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr); if (!editor) return;
  editor->setPlainText(QStringLiteral("Turned box text with side handles"));
  QApplication::processEvents();
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  require_action(window, "editFreeTransformAction")->trigger();
  QApplication::processEvents();
  CHECK(canvas->free_transform_active());
  const auto state = canvas->transform_controls_state();
  CHECK(state.has_value()); if (!state) return;
  CHECK(canvas->set_transform_controls_state(state->reference_position, 100.0, 100.0, 90.0));
  QApplication::processEvents();
  send_key(*canvas, Qt::Key_Return);
  QApplication::processEvents();
  const auto turned = canvas->active_layer_document_rect();
  CHECK(turned.has_value()); if (!turned) return;
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto edit_point = canvas->widget_position_for_document_point(turned->center());
  send_mouse(*canvas, QEvent::MouseButtonPress, edit_point, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, edit_point, Qt::LeftButton, Qt::NoButton);
  process_events_for(150);
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr); if (!editor) return;
  auto* overlay = canvas->findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay"), Qt::FindDirectChildrenOnly);
  CHECK(overlay != nullptr && overlay->isVisible()); if (!overlay) return;

  // Top, Bottom, Left, Right; every side shows on this box.
  const auto sides = [&] { return overlay->property("patchy.transformedTextSideHandleCenters").toList(); };
  const auto corners = [&] { return overlay->property("patchy.transformedTextResizeHandleCenters").toList(); };
  auto side_list = sides();
  CHECK(side_list.size() == 4 && corners().size() == 4); if (side_list.size() != 4) return;
  for (const auto& side : side_list) {
    CHECK(side.isValid());
  }
  const auto box_center = [&] {
    QPointF sum;
    for (const auto& point : corners()) sum += point.toPointF();
    return sum / 4.0;
  };
  const auto hover_cursor = [&](QPointF canvas_point) {
    send_mouse(*overlay, QEvent::MouseMove, (canvas_point - QPointF(overlay->pos())).toPoint(), Qt::NoButton,
               Qt::NoButton);
    return overlay->cursor().shape();
  };
  // Turned a quarter: the right side's outward direction is vertical on screen, the bottom's
  // horizontal, and the corners keep diagonal cursors.
  const auto right_outward = side_list[3].toPointF() - box_center();
  const auto bottom_outward = side_list[1].toPointF() - box_center();
  CHECK(std::abs(right_outward.y()) > 4.0 * std::abs(right_outward.x()));
  CHECK(std::abs(bottom_outward.x()) > 4.0 * std::abs(bottom_outward.y()));
  CHECK(hover_cursor(side_list[3].toPointF()) == Qt::SizeVerCursor);
  CHECK(hover_cursor(side_list[0].toPointF()) == Qt::SizeHorCursor);
  CHECK(hover_cursor(side_list[1].toPointF()) == Qt::SizeHorCursor);
  const auto corner_cursor = hover_cursor(corners().back().toPointF());
  CHECK(corner_cursor == Qt::SizeFDiagCursor || corner_cursor == Qt::SizeBDiagCursor);
  {
    const auto shot = canvas->grab().toImage();
    save_image_artifact("ui_transformed_text_side_handles",
                        shot.copy(overlay->geometry().adjusted(-30, -30, 30, 30).intersected(shot.rect())));
  }

  const auto drag_side = [&](int index, double distance) {
    const auto list = sides();
    CHECK(list.size() == 4 && list[index].isValid()); if (list.size() != 4) return;
    const auto from = list[index].toPointF();
    auto outward = from - box_center();
    outward /= std::max(1e-6, std::hypot(outward.x(), outward.y()));
    const auto start = from.toPoint();
    const auto end = (from + outward * distance).toPoint();
    send_mouse(*canvas, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, (from + outward * distance * 0.5).toPoint(), Qt::NoButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
    process_events_for(120);
    editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    overlay = canvas->findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay"), Qt::FindDirectChildrenOnly);
  };

  // Right side outward (down the screen): only the width grows; the left side does not move.
  auto width_before = editor->property("patchy.documentTextWidth").toInt();
  auto height_before = editor->property("patchy.documentTextHeight").toInt();
  auto left_before = sides()[2].toPointF();
  drag_side(3, 40.0);
  CHECK(editor != nullptr && overlay != nullptr); if (!editor || !overlay) return;
  CHECK(std::abs(editor->property("patchy.documentTextWidth").toInt() - (width_before + 40)) <= 2);
  CHECK(editor->property("patchy.documentTextHeight").toInt() == height_before);
  CHECK((sides()[2].toPointF() - left_before).manhattanLength() < 2.5);

  // Top side outward (sideways): only the height grows; the bottom side does not move.
  width_before = editor->property("patchy.documentTextWidth").toInt();
  height_before = editor->property("patchy.documentTextHeight").toInt();
  const auto bottom_before = sides()[1].toPointF();
  drag_side(0, 30.0);
  CHECK(editor != nullptr && overlay != nullptr); if (!editor || !overlay) return;
  CHECK(std::abs(editor->property("patchy.documentTextHeight").toInt() - (height_before + 30)) <= 2);
  CHECK(editor->property("patchy.documentTextWidth").toInt() == width_before);
  CHECK((sides()[1].toPointF() - bottom_before).manhattanLength() < 2.5);
  send_key(*editor, Qt::Key_Escape);
  process_events_for(100);

  // An area frame scales along one axis from a side handle.
  run(window, R"JS(
    var d=app.activeDocument;
    d.addShape('Frame',{type:'polygon',cx:130,cy:240,radius:100,sides:6});
  )JS");
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  // Clear of the turned box, which would take the click as a text edit.
  const QPoint area_center(130, 240);
  CHECK(canvas->text_area_at(area_center).has_value());
  const auto click = canvas->widget_position_for_document_point(area_center);
  send_mouse(*canvas, QEvent::MouseButtonPress, click, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*canvas, QEvent::MouseButtonRelease, click, Qt::LeftButton, Qt::NoButton);
  process_events_for(150);
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr); if (!editor) return;
  editor->setPlainText(QStringLiteral("Area text"));
  process_events_for(150);
  overlay = canvas->findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay"), Qt::FindDirectChildrenOnly);
  CHECK(overlay != nullptr && overlay->isVisible()); if (!overlay) return;
  const auto original_area = parse_vector_path(editor->property("patchy.documentTextArea").toString().toStdString());
  CHECK(original_area.has_value()); if (!original_area) return;
  CHECK(sides().size() == 4 && sides()[1].isValid());
  drag_side(1, 36.0);
  CHECK(editor != nullptr); if (!editor) return;
  const auto resized_area = parse_vector_path(editor->property("patchy.documentTextArea").toString().toStdString());
  CHECK(resized_area.has_value()); if (!resized_area) return;
  const auto first = original_area->bounds();
  const auto last = resized_area->bounds();
  CHECK(std::abs((last->bottom - last->top) - (first->bottom - first->top) - 36.0) < 2.0);
  CHECK(std::abs((last->right - last->left) - (first->right - first->left)) < 1.0);
  CHECK(std::abs(last->top - first->top) < 1.0);
  send_key(*editor, Qt::Key_Escape);
  process_events_for(100);
}
}  // namespace

std::vector<patchy::test::TestCase> text_area_tests() {
  return {{"ui_text_area_full_band_spans",ui_text_area_full_band_spans},
          {"ui_text_area_concave_flow_alignment_and_vertical",ui_text_area_concave_flow_alignment_and_vertical},
          {"ui_text_area_photoshop_triangle_line_breaks_and_caret",ui_text_area_photoshop_triangle_line_breaks_and_caret},
          {"ui_text_justify_last_line_modes_match_photoshop_if_available",
           ui_text_justify_last_line_modes_match_photoshop_if_available},
          {"ui_text_paragraph_alignment_names_round_trip", ui_text_paragraph_alignment_names_round_trip},
          {"ui_text_area_script_creation_boundary_edit_and_undo",ui_text_area_script_creation_boundary_edit_and_undo},
          {"ui_text_area_depth_edit_transform_and_roundtrip",ui_text_area_depth_edit_transform_and_roundtrip},
          {"ui_text_area_empty_and_large_frames",ui_text_area_empty_and_large_frames},
          {"ui_text_area_psd_uses_postscript_font_names",ui_text_area_psd_uses_postscript_font_names},
          {"ui_text_point_psd_uses_postscript_font_names",ui_text_point_psd_uses_postscript_font_names},
          {"ui_text_variable_font_instances_get_their_own_postscript_names",
           ui_text_variable_font_instances_get_their_own_postscript_names},
          {"ui_text_area_type_tool_creation_cancel_and_boundary_edit",ui_text_area_type_tool_creation_cancel_and_boundary_edit},
          {"ui_box_text_overflow_hides_lines_below_the_frame",ui_box_text_overflow_hides_lines_below_the_frame},
          {"ui_box_text_overflow_photoshop_story_survives_edit_and_round_trip",
           ui_box_text_overflow_photoshop_story_survives_edit_and_round_trip},
          {"ui_box_text_overflow_marker_on_bottom_right_handle", ui_box_text_overflow_marker_on_bottom_right_handle},
          {"ui_box_text_overflow_marker_tooltip", ui_box_text_overflow_marker_tooltip},
          {"ui_box_text_side_handles_move_one_edge", ui_box_text_side_handles_move_one_edge},
          {"ui_transformed_text_side_handles_follow_rotation", ui_transformed_text_side_handles_follow_rotation}};
}
