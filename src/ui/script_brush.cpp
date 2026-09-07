#include <QCoreApplication>
#include "ui/script_engine.hpp"
#include "ui/brush_automation.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/main_window.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/image_document_io.hpp"
#include "formats/document_flatten.hpp"
#include <QJSEngine>
#include <QJSValueIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QImageReader>
#include <QSaveFile>
#include <QScopeGuard>
#include <QLockFile>
#include <QDir>
#include <cmath>

namespace patchy::ui {
namespace {
using namespace brush_input;
QJsonObject json_object(const QJSValue& v, bool optional = false) {
  if (optional && v.isUndefined()) return {};
  if (!v.isObject() || v.isArray() || v.isNull()) invalid("object");
  // Reject nonfinite values before QVariant/JSON conversion can turn them into null.
  QJSValueIterator it(v);
  while (it.hasNext()) {
    it.next(); if (it.value().isNumber() && !std::isfinite(it.value().toNumber())) invalid(it.name());
  }
  return QJsonObject::fromVariantMap(v.toVariant().toMap());
}
QString text_arg(const QJSValue& v, const QString& field) {
  if (!v.isString() || v.toString().trimmed().isEmpty()) invalid(field);
  return v.toString();
}
QImage read_tip_image(const QString& path) {
  QImageReader reader(path);
  const auto size = reader.size();
  if (!size.isValid() || size.width() > 4096 || size.height() > 4096) invalid("image dimensions (1..4096)");
  const auto image = reader.read(); if (image.isNull()) invalid("image"); return image;
}
QJsonObject public_preset(QJsonObject p) {
  p.remove("tipPng");
  auto settings = p["settings"].toObject(); settings["presetId"] = p["id"]; p["settings"] = settings;
  return p;
}
}
std::vector<ScriptStroke> ScriptEngineHost::parse_brush_strokes(const QJSValue& input) {
  if (!input.isArray()) invalid("strokes");
  const auto length = input.property("length").toUInt();
  if (!length || length > 1000) invalid("strokes.length (1..1000)");
  auto& library = window_.brush_automation_library(); library.refresh();
  std::vector<ScriptStroke> strokes;
  std::size_t total_points = 0, total_ticks = 0;
  for (quint32 i = 0; i < length; ++i) {
    const auto value = input.property(i); auto options = json_object(value); options.remove("points");
    auto stroke = library.resolve(options);
    const auto points = value.property("points"); if (!points.isArray()) invalid("points");
    const auto count = points.property("length").toUInt(); total_points += count;
    if (!count || total_points > 100000) invalid("points.length (1..100000 total)");
    const bool timed = !points.property(0).property("timeMs").isUndefined();
    if (stroke.airbrush && !timed) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "airbrush requires timeMs"));
    int previous_time = 0;
    for (quint32 p = 0; p < count; ++p) {
      const auto point = json_object(points.property(p));
      keys(point, {"x", "y", "pressure", "xTilt", "yTilt", "rotation", "tangentialPressure", "timeMs"});
      if (!point.contains("x") || !point.contains("y")) invalid("x/y");
      ScriptStrokePoint sample;
      sample.position = {number(point, "x", 0, -100000, 100000), number(point, "y", 0, -100000, 100000)};
      if (point.contains("pressure")) sample.pressure = static_cast<float>(number(point, "pressure", 1, 0, 1));
      if (point.contains("xTilt") != point.contains("yTilt")) invalid("xTilt/yTilt");
      if (point.contains("xTilt")) {
        sample.x_tilt = static_cast<float>(number(point, "xTilt", 0, -90, 90));
        sample.y_tilt = static_cast<float>(number(point, "yTilt", 0, -90, 90));
      }
      if (point.contains("rotation")) sample.rotation = number(point, "rotation", 0, -360, 360);
      if (point.contains("tangentialPressure")) sample.tangential_pressure = static_cast<float>(number(point, "tangentialPressure", 0, -1, 1));
      if (point.contains("timeMs") != timed) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "timeMs: complete timeline required"));
      if (timed) {
        const auto ms = static_cast<int>(number(point, "timeMs", 0, 0, 3600000, true));
        if ((!p && ms != 0) || ms < previous_time) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "timeMs order"));
        sample.time_ms = ms; previous_time = ms;
      }
      stroke.points.push_back(sample);
    }
    if (stroke.airbrush) total_ticks += static_cast<std::size_t>(previous_time / 50);
    if (stroke.smoothing && stroke.catch_up) total_ticks += static_cast<std::size_t>(previous_time / 16);
    if (total_ticks > 1000000) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "timeline exceeds 1000000 ticks per batch"));
    strokes.push_back(std::move(stroke));
  }
  return strokes;
}
QJSValue ScriptEngineHost::scriptBrushCall(const QString& method, const QJSValue& args) {
  try {
    pump_progress_indicator();
    auto& library = window_.brush_automation_library(); library.refresh();
    const auto a = [&](quint32 i) { return args.property(i); };
    const auto js = [&](const QJsonValue& v) { return engine_->toScriptValue(v.toVariant()); };
    if (method == "listTips") return js(library.tips());
    if (method == "getTip") return js(library.tip_info(text_arg(a(0), "tipId")));
    if (method == "listPresets") return js(library.presets());
    if (method == "getPreset") return js(public_preset(library.preset(text_arg(a(0), "presetId"))));
    if (method == "getCurrent") {
      if (!window_.canvas_) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "active document"));
      return js(library.capture(window_.canvas_->current_script_brush()));
    }
    if (method == "resolve" || method == "activate") {
      const auto s = library.resolve(json_object(a(0), true));
      if (method == "activate") window_.activate_automation_brush(s);
      return js(QJsonObject{{"settings", BrushAutomationLibrary::settings(s)},
        {"capabilities", QJsonObject{{"dynamics", !s.erase && !s.mixer}, {"airbrush", !s.erase && !s.mixer},
          {"mixer", s.mixer}, {"timingRequired", s.airbrush}}}});
    }
    if (method == "savePreset" || method == "updatePreset" || method == "duplicatePreset") {
      const bool update = method == "updatePreset", duplicate = method == "duplicatePreset";
      const auto id = update || duplicate ? text_arg(a(0), "presetId") : QString();
      const auto options = json_object(a(2), true); keys(options, {"includeColors", "name", "folder"});
      if (options.contains("name")) (void)text_arg(a(2).property("name"), "name");
      if (options.contains("folder") && !options["folder"].isString()) invalid("folder");
      auto settings = duplicate ? library.resolve(QJsonObject{{"presetId", id}}) : library.resolve(json_object(a(1)));
      const auto old = id.isEmpty() ? QJsonObject() : library.preset(id);
      const auto name = update ? options.value("name").toString(old["name"].toString()) : text_arg(a(duplicate ? 1 : 0), "name");
      const auto saved = library.save(name, settings, boolean(options, "includeColors", old["includeColors"].toBool()),
                                     update ? id : QString(), options.value("folder").toString(old["folder"].toString()));
      return js(public_preset(library.preset(saved)));
    }
    if (method == "removePreset") { library.remove(text_arg(a(0), "presetId")); return QJSValue(true); }
    if (method == "importAbr") {
      const auto path = text_arg(a(0), "path"); const auto options = json_object(a(1), true); keys(options, {});
      auto& tips = window_.brush_tip_library(); QDir().mkpath(tips.storage_dir());
      QLockFile lock(QDir(tips.storage_dir()).filePath("automation.lock"));
      if (!lock.tryLock(0)) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "brush library busy"));
      QStringList before; for (const auto& t : tips.entries()) before << t.id;
      QString error; QStringList warnings; (void)tips.import_abr(path, error, warnings);
      QJsonArray ids, notices; for (const auto& t : tips.entries()) if (!before.contains(t.id)) ids.append(t.id);
      for (const auto& w : warnings) notices.append(QJsonObject{{"message", w}});
      library.refresh();
      if (ids.isEmpty()) invalid(error);
      return js(QJsonObject{{"ids", ids}, {"warnings", notices}});
    }
    if (method == "createTip") {
      const auto name = text_arg(a(0), "name"); const auto source = a(1);
      const auto options = json_object(a(2), true); keys(options, {"spacing", "folder"});
      if (options.contains("folder") && !options["folder"].isString()) invalid("folder");
      const auto spacing = number(options, "spacing", .25, .01, 10);
      QImage coverage;
      if (source.isString()) {
        coverage = brush_coverage_from_image(read_tip_image(source.toString()));
      } else {
        auto src = json_object(source); src.remove("data");
        if (src.contains("documentId")) {
          keys(src, {"documentId", "rect", "useSelection"});
          bool ok = false; const auto id = src["documentId"].toString().toLongLong(&ok);
          const auto* session = ok ? window_.session_with_id(id) : nullptr; if (!session) invalid("documentId");
          const auto& doc = session->document; QRect rect(0, 0, doc.width(), doc.height());
          if (src.contains("rect")) {
            const auto r = object(src, "rect"); keys(r, {"x", "y", "width", "height"});
            rect = QRect(static_cast<int>(number(r,"x",0,-100000,100000,true)), static_cast<int>(number(r,"y",0,-100000,100000,true)),
                         static_cast<int>(number(r,"width",0,1,4096,true)), static_cast<int>(number(r,"height",0,1,4096,true))).intersected(rect);
          }
          const auto use_selection = boolean(src, "useSelection", true);
          if (use_selection && session->canvas->selected_document_rect()) rect = rect.intersected(*session->canvas->selected_document_rect());
          if (rect.isEmpty() || rect.width()>4096 || rect.height()>4096) invalid("tip dimensions (1..4096)");
          const auto image = qimage_from_document_rect(doc, rect, true).convertToFormat(QImage::Format_ARGB32);
          coverage = brush_coverage_from_image(image, use_selection && session->canvas->selected_document_rect()
            ? std::function<int(int,int)>([&](int x,int y) { return session->canvas->selection_alpha_at(rect.topLeft()+QPoint(x,y)); })
            : std::function<int(int,int)>());
        } else {
          keys(src, {"width", "height"});
          const auto w=static_cast<int>(number(src,"width",0,1,4096,true)), h=static_cast<int>(number(src,"height",0,1,4096,true));
          const auto bytes=source.property("data").toVariant().toByteArray();
          if (w<1 || h<1 || bytes.size()!=static_cast<qsizetype>(w)*h) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "coverage data length"));
          coverage=QImage(w,h,QImage::Format_Grayscale8);
          for(int y=0;y<h;++y) std::copy_n(reinterpret_cast<const uchar*>(bytes.constData())+static_cast<qsizetype>(y)*w,w,coverage.scanLine(y));
        }
      }
      auto& tips=window_.brush_tip_library(); QDir().mkpath(tips.storage_dir());
      QLockFile lock(QDir(tips.storage_dir()).filePath("automation.lock")); if(!lock.tryLock(0)) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "brush library busy"));
      const auto id=tips.add_tip(name,coverage,spacing,options["folder"].toString());
      if(id.isEmpty()) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "empty or unsaveable tip"));
      library.refresh(); return js(library.tip_info(id));
    }
    if (method == "renderPreview") {
      const auto path=text_arg(a(0),"path"); auto s=library.resolve(json_object(a(1),true));
      const auto options=json_object(a(2),true); keys(options,{"width","height","backgroundColor"});
      if (options.contains("backgroundColor") && !options["backgroundColor"].isString()) invalid("backgroundColor");
      const auto w=static_cast<int>(number(options,"width",320,32,1024,true)), h=static_cast<int>(number(options,"height",160,32,1024,true));
      const QColor bg(options["backgroundColor"].toString("#e8ded0")); if(!bg.isValid()) invalid("backgroundColor");
      Document doc(w,h,PixelFormat::rgba8()); PixelBuffer pixels(w,h,PixelFormat::rgba8());
      for(int y=0;y<h;++y) for(int x=0;x<w;++x) { auto* p=pixels.pixel(x,y); p[0]=static_cast<std::uint8_t>(bg.red());p[1]=static_cast<std::uint8_t>(bg.green());p[2]=static_cast<std::uint8_t>(bg.blue());p[3]=255; }
      auto& layer=doc.add_pixel_layer("Swatch",std::move(pixels)); doc.set_active_layer(layer.id());
      CanvasWidget canvas; canvas.set_document(&doc); const auto detach=qScopeGuard([&]{canvas.set_document(nullptr);});
      for(int i=0;i<=32;++i) { ScriptStrokePoint p; p.position={w*(.1+.8*i/32),h*(.5+.2*std::sin(i*6.283185307179586/32))};
        p.pressure=static_cast<float>(.15+.85*std::sin(i*3.141592653589793/32));p.time_ms=i*25;s.points.push_back(p); }
      canvas.paint_script_stroke(s,[&](const QRect&){pump_progress_indicator();return engine_->isInterrupted();});
      if(engine_->isInterrupted()) return {};
      const auto image=qimage_from_document_rect(doc,QRect(0,0,w,h),true); QSaveFile file(path);
      if(!file.open(QIODevice::WriteOnly)||!image.save(&file,"PNG")||!file.commit()) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "preview write"));
      return js(QJsonObject{{"path",path},{"width",w},{"height",h}});
    }
    invalid("brush method");
  } catch(const std::exception& e) { throw_js_error(tr("Invalid brush argument: %1").arg(QString::fromUtf8(e.what()))); }
  return {};
}
}  // namespace patchy::ui
