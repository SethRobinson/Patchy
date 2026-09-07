#include <QCoreApplication>
#include "ui/brush_automation.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_presets.hpp"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>
#include <cmath>
#include <stdexcept>

namespace patchy::ui {
namespace brush_input {
[[noreturn]] void invalid(const QString& field) { throw std::invalid_argument(field.toStdString()); }
void keys(const QJsonObject& object, const QStringList& allowed) {
  for (auto it = object.begin(); it != object.end(); ++it) if (!allowed.contains(it.key())) invalid(it.key());
}
double number(const QJsonObject& o, const QString& k, double fallback, double lo, double hi, bool integral) {
  if (!o.contains(k)) return fallback;
  const auto v = o[k]; const auto n = v.toDouble();
  if (!v.isDouble() || !std::isfinite(n) || n < lo || n > hi || (integral && n != std::floor(n))) invalid(k);
  return n;
}
bool boolean(const QJsonObject& o, const QString& k, bool fallback) {
  if (!o.contains(k)) return fallback;
  if (!o[k].isBool()) invalid(k);
  return o[k].toBool();
}
QJsonObject object(const QJsonObject& o, const QString& k) {
  if (!o.contains(k)) return {};
  if (!o[k].isObject()) invalid(k);
  return o[k].toObject();
}
}
namespace {
using namespace brush_input;
QString string(const QJsonObject& o, const QString& k, const QString& fallback = {}) {
  if (!o.contains(k)) return fallback;
  if (!o[k].isString()) invalid(k);
  return o[k].toString();
}
QColor color(const QJsonObject& o, const QString& k, const QColor& fallback) {
  if (!o.contains(k)) return fallback;
  const QColor c(string(o, k)); if (!c.isValid()) invalid(k); return c;
}
QJsonObject dynamics(const QJsonObject& overrides, const BrushDynamics& base) {
  auto out = brush_dynamics_to_json(base);
  keys(overrides, out.keys());
  for (auto it = overrides.begin(); it != overrides.end(); ++it) {
    const auto& k = it.key(); const auto expected = out[k];
    if (expected.isBool()) { (void)boolean(overrides, k, false); }
    else if (expected.isString()) {
      const auto token = string(overrides, k);
      if (k == "textureStyle") {
        if (!QStringList{"fineGrain", "canvas", "speckle"}.contains(token)) invalid(k);
      } else {
        QStringList allowed{"off", "fade", "penPressure", "penTilt", "penRotation", "stylusWheel"};
        if (k == "angleControl") allowed << "direction" << "initialDirection";
        if (k == "sizeControl" || k == "roundnessControl" || k == "opacityControl") allowed << "global";
        if (!allowed.contains(token)) invalid(k);
      }
    } else {
      double lo = 0, hi = 1; bool integral = false;
      if (k.endsWith("FadeSteps")) { lo = 1; hi = 9999; integral = true; }
      else if (k == "count") { lo = 1; hi = 16; integral = true; }
      else if (k == "textureSeed") { hi = 4294967295.0; integral = true; }
      else if (k == "scatter") hi = 10;
      else if (k == "textureScale") { lo = .01; hi = 10; }
      else if (k == "dualBrushSize") { lo = .05; hi = 4; }
      else if (k == "dualBrushSpacing") { lo = .1; hi = 10; }
      else if (k == "purity") lo = -1;
      (void)number(overrides, k, 0, lo, hi, integral);
    }
    out[k] = it.value();
  }
  return out;
}
QJsonObject builtin(const BrushPreset& p) {
  return {{"id", p.id}, {"name", brush_preset_display_name(p)}, {"source", "builtin"},
          {"settings", QJsonObject{{"tool", "brush"}, {"size", p.size}, {"opacity", p.opacity},
          {"flow", p.flow}, {"softness", p.softness}, {"airbrush", p.build_up}}}};
}
}
BrushAutomationLibrary::BrushAutomationLibrary(BrushTipLibrary& tips, QObject* parent, QString directory)
    : QObject(parent), tips_(tips), directory_(std::move(directory)) {
  if (directory_.isEmpty()) directory_ = QDir(tips.storage_dir()).absoluteFilePath("../brush-presets");
  refresh();
}
void BrushAutomationLibrary::refresh() {
  QByteArray signature;
  QByteArray tip_signature;
  for (const auto& directory : {directory_, tips_.storage_dir()}) {
    for (const auto& file : QDir(directory).entryInfoList({"*.json", "*.png"}, QDir::Files, QDir::Name)) {
      signature += file.absoluteFilePath().toUtf8() + QByteArray::number(file.size()) +
                   QByteArray::number(file.lastModified().toMSecsSinceEpoch());
      // Same-size edits can commit within one millisecond. Include JSON content so
      // those updates cannot evade cross-process refresh or attached state tokens.
      QByteArray json_digest;
      if (file.suffix() == "json" && file.size() <= 32 * 1024 * 1024) {
        QFile json(file.absoluteFilePath());
        if (json.open(QIODevice::ReadOnly)) json_digest = QCryptographicHash::hash(json.readAll(), QCryptographicHash::Sha256);
        signature += json_digest;
      }
      if (directory == tips_.storage_dir()) tip_signature += file.absoluteFilePath().toUtf8() + QByteArray::number(file.size()) +
                   QByteArray::number(file.lastModified().toMSecsSinceEpoch()) + json_digest;
    }
  }
  const auto fingerprint = QString::fromLatin1(QCryptographicHash::hash(signature, QCryptographicHash::Sha256).toHex());
  if (fingerprint == fingerprint_) return;
  fingerprint_ = fingerprint; presets_ = {}; preset_tips_.clear();
  const auto tip_fingerprint = QString::fromLatin1(QCryptographicHash::hash(tip_signature, QCryptographicHash::Sha256).toHex());
  if (tip_fingerprint != tip_fingerprint_) { tip_fingerprint_ = tip_fingerprint; tips_.refresh_from_disk(); }
  for (const auto& f : QDir(directory_).entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
    QFile file(f.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024) continue;
    const auto obj = QJsonDocument::fromJson(file.readAll()).object();
    if (obj["version"].toInt() == 1 && obj["id"].toString() == f.completeBaseName() && obj["settings"].isObject())
      presets_.append(obj);
  }
  revision_ = fingerprint;
  emit changed();
}
QString BrushAutomationLibrary::revision() const { return revision_; }
QJsonObject BrushAutomationLibrary::tip_info(const QString& id) const {
  if (id == builtin_round_brush_tip_id()) return {{"id", id}, {"name", tr("Round")}, {"source", "builtin"}};
  if (const auto found = captured_tips_.find(id); found != captured_tips_.end())
    return {{"id", id}, {"name", tr("Working brush")}, {"source", "session"},
            {"width", found->second->width}, {"height", found->second->height}};
  const auto* t = tips_.find_entry(id); if (!t) invalid("tipId");
  return {{"id", t->id}, {"name", t->name}, {"folder", t->folder}, {"source", "library"},
          {"width", t->size.width()}, {"height", t->size.height()}, {"spacing", t->spacing},
          {"angle", t->base_angle_degrees}, {"roundness", t->base_roundness},
          {"dynamics", brush_dynamics_to_json(t->dynamics)}};
}
QJsonArray BrushAutomationLibrary::tips() const {
  QJsonArray out{tip_info(builtin_round_brush_tip_id())};
  for (const auto& t : tips_.entries()) out.append(tip_info(t.id));
  return out;
}
QJsonArray BrushAutomationLibrary::presets() const {
  QJsonArray out;
  for (const auto& p : builtin_brush_presets()) out.append(builtin(p));
  for (const auto& value : presets_) {
    auto p = value.toObject(); p.remove("tipPng");
    auto config = p["settings"].toObject(); config["presetId"] = p["id"]; p["settings"] = config;
    out.append(p);
  }
  return out;
}
QJsonObject BrushAutomationLibrary::preset(const QString& id) const {
  if (const auto* p = find_brush_preset(id)) return builtin(*p);
  for (const auto& p : presets_) if (p.toObject()["id"].toString() == id) return p.toObject();
  invalid("presetId");
}
QStringList BrushAutomationLibrary::setting_keys() {
  return {"tool", "color", "backgroundColor", "size", "opacity", "flow", "softness", "seed", "sizeJitter", "scatter",
          "tipId", "presetId", "spacing", "angle", "roundness", "dynamics", "mixer", "pen", "smoothing", "airbrush"};
}
ScriptStroke BrushAutomationLibrary::resolve(const QJsonObject& input) const {
  keys(input, setting_keys());
  ScriptStroke s;
  auto args = input;
  const auto preset_id = string(input, "presetId");
  if (!preset_id.isEmpty()) {
    const auto p = preset(preset_id); args = p["settings"].toObject();
    for (auto it = input.begin(); it != input.end(); ++it) {
      if (it.value().isObject()) {
        auto merged = args[it.key()].toObject();
        const auto fields = it.value().toObject();
        for (auto f = fields.begin(); f != fields.end(); ++f) merged[f.key()] = f.value();
        args[it.key()] = merged;
      } else args[it.key()] = it.value();
    }
    s.preset_id = preset_id; s.label = p["name"].toString();
    if (p.contains("tipPng") && !input.contains("tipId")) {
      auto& cached = preset_tips_[preset_id];
      if (!cached) {
        const auto image = QImage::fromData(QByteArray::fromBase64(p["tipPng"].toString().toLatin1()), "PNG");
        if (image.isNull() || image.width() > 4096 || image.height() > 4096) invalid("preset.tip");
        cached = std::make_shared<BrushTip>(brush_tip_from_coverage_image(image));
      }
      s.tip = cached;
    }
  }
  const auto tool = string(args, "tool", "brush");
  if (!QStringList{"brush", "eraser", "mixer"}.contains(tool)) invalid("tool");
  s.erase = tool == "eraser"; s.mixer = tool == "mixer";
  const auto tip_id = string(args, "tipId");
  if (!tip_id.isEmpty()) {
    s.tip_id = tip_id;
    if (tip_id != builtin_round_brush_tip_id()) {
      const auto* t = tips_.find_entry(tip_id);
      const auto captured = captured_tips_.find(tip_id);
      if (captured != captured_tips_.end()) {
        s.tip = captured->second;
        s.spacing = s.tip->default_spacing;
      } else {
      s.tip = tips_.tip(tip_id);
      if (!t || !s.tip) invalid("tipId");
      s.label = t->name; s.spacing = t->spacing; s.angle = t->base_angle_degrees;
      s.roundness = static_cast<int>(std::lround(t->base_roundness));
      if (!s.erase && !s.mixer) s.dynamics = t->dynamics;
      if (t->tool_flow_percent) s.flow = *t->tool_flow_percent;
      if (t->tool_airbrush && !s.erase && !s.mixer) s.airbrush = *t->tool_airbrush;
      }
    } else s.tip.reset();
    // A replacement tip supplies its defaults, not the saved preset's tip configuration.
    if (input.contains("tipId") && !preset_id.isEmpty()) {
      for (const auto& k : {"spacing", "angle", "roundness", "dynamics"}) {
        if (input.contains(k)) args[k] = input[k]; else args.remove(k);
      }
      if (const auto* entry = tips_.find_entry(tip_id)) {
        if (entry->tool_flow_percent && !input.contains("flow")) args.remove("flow");
        if (entry->tool_airbrush && !input.contains("airbrush")) args.remove("airbrush");
      }
    }
  }
  s.color = color(args, "color", s.color); s.background = color(args, "backgroundColor", s.background);
  const auto integer = [&](const QString& k, int fallback, int lo, int hi) {
    return static_cast<int>(number(args, k, fallback, lo, hi, true));
  };
  s.size = integer("size", s.size, 1, 1024); s.opacity = integer("opacity", s.opacity, 1, 100);
  s.flow = integer("flow", s.flow, 1, 100); s.softness = integer("softness", s.softness, 0, 100);
  if (s.mixer && s.opacity != 100) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "opacity: Mixer uses Flow"));
  s.angle = number(args, "angle", s.angle, -180, 360); s.roundness = integer("roundness", s.roundness, 1, 100);
  if (args.contains("spacing")) s.spacing = number(args, "spacing", .25, .01, 10);
  s.airbrush = boolean(args, "airbrush", s.airbrush);
  auto dyn = object(args, "dynamics");
  for (const auto& k : {"sizeJitter", "scatter"}) if (args.contains(k)) {
    if (dyn.contains(k) && dyn[k] != args[k]) invalid(k);
    dyn[k] = args[k];
  }
  if ((s.erase || s.mixer) && input.contains("dynamics") && !object(input, "dynamics").isEmpty()) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "dynamics: brush only"));
  if (!s.erase && !s.mixer) s.dynamics = brush_dynamics_from_json(dynamics(dyn, s.dynamics));
  else (void)dynamics(dyn, {}); // Keep legacy jitter/scatter acceptance on Eraser.
  s.dynamics.seed = static_cast<std::uint32_t>(number(args, "seed", 0, 0, 4294967295.0, true));
  if (s.airbrush && (s.erase || s.mixer)) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "airbrush: brush only"));
  const auto mix = object(args, "mixer"); keys(mix, {"wet", "load", "mix", "sampleAllLayers"});
  if (!mix.isEmpty() && !s.mixer) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "mixer: mixer tool only"));
  s.wet = static_cast<int>(number(mix, "wet", 50, 0, 100, true));
  s.load = static_cast<int>(number(mix, "load", 50, 1, 100, true));
  s.mix = static_cast<int>(number(mix, "mix", 50, 0, 100, true));
  s.sample_all_layers = boolean(mix, "sampleAllLayers", false);
  const auto pen = object(args, "pen");
  keys(pen, {"pressureSize", "pressureOpacity", "sizeMinimum", "opacityMinimum", "tiltShape", "tiltMinimumRoundness"});
  s.pressure_size = boolean(pen, "pressureSize", true); s.pressure_opacity = boolean(pen, "pressureOpacity", true);
  s.pressure_size_min = static_cast<int>(number(pen, "sizeMinimum", 20, 1, 100, true));
  s.pressure_opacity_min = static_cast<int>(number(pen, "opacityMinimum", 15, 1, 100, true));
  s.tilt_shape = boolean(pen, "tiltShape", false);
  s.tilt_min_roundness = static_cast<int>(number(pen, "tiltMinimumRoundness", 35, 1, 100, true));
  const auto sm = object(args, "smoothing");
  keys(sm, {"amount", "pulledString", "catchUp", "catchUpOnEnd", "adjustForZoom", "referenceZoom"});
  s.smoothing = static_cast<int>(number(sm, "amount", 0, 0, 100, true));
  s.pulled_string = boolean(sm, "pulledString", false); s.catch_up = boolean(sm, "catchUp", true);
  s.catch_up_end = boolean(sm, "catchUpOnEnd", true); s.zoom_adjust = boolean(sm, "adjustForZoom", true);
  s.reference_zoom = number(sm, "referenceZoom", 100, .01, 100000);
  return s;
}
QJsonObject BrushAutomationLibrary::settings(const ScriptStroke& s) {
  QJsonObject o{{"tool", s.mixer ? "mixer" : s.erase ? "eraser" : "brush"}, {"size", s.size},
    {"opacity", s.opacity}, {"flow", s.flow}, {"softness", s.softness}, {"color", s.color.name(QColor::HexArgb)},
    {"backgroundColor", s.background.name(QColor::HexArgb)}, {"angle", s.angle}, {"roundness", s.roundness},
    {"airbrush", s.airbrush}, {"seed", static_cast<double>(s.dynamics.seed)},
    {"pen", QJsonObject{{"pressureSize", s.pressure_size}, {"pressureOpacity", s.pressure_opacity},
      {"sizeMinimum", s.pressure_size_min}, {"opacityMinimum", s.pressure_opacity_min},
      {"tiltShape", s.tilt_shape}, {"tiltMinimumRoundness", s.tilt_min_roundness}}},
    {"smoothing", QJsonObject{{"amount", s.smoothing}, {"pulledString", s.pulled_string}, {"catchUp", s.catch_up},
      {"catchUpOnEnd", s.catch_up_end}, {"adjustForZoom", s.zoom_adjust}, {"referenceZoom", s.reference_zoom}}}};
  if (!s.tip_id.isEmpty()) o["tipId"] = s.tip_id;
  if (!s.preset_id.isEmpty()) o["presetId"] = s.preset_id;
  if (s.spacing) o["spacing"] = *s.spacing;
  if (s.mixer) o["mixer"] = QJsonObject{{"wet", s.wet}, {"load", s.load}, {"mix", s.mix}, {"sampleAllLayers", s.sample_all_layers}};
  if (!s.mixer && !s.erase) o["dynamics"] = brush_dynamics_to_json(s.dynamics);
  return o;
}
QJsonObject BrushAutomationLibrary::capture(const ScriptStroke& s) {
  auto result = settings(s); result.remove("presetId");
  if (!s.tip) { result["tipId"] = builtin_round_brush_tip_id(); return result; }
  QString id;
  for (const auto& entry : captured_tips_) if (entry.second == s.tip) { id = entry.first; break; }
  if (id.isEmpty()) {
    id = "snapshot:" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    captured_tips_.emplace(id, s.tip);
  }
  result["tipId"] = id;
  return result;
}
QString BrushAutomationLibrary::save(const QString& name, const ScriptStroke& s, bool colors,
                                    const QString& existing, const QString& folder) {
  if (name.trimmed().isEmpty()) invalid("name");
  if (!QDir().mkpath(directory_)) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "preset directory"));
  QLockFile lock(QDir(directory_).filePath("write.lock"));
  if (!lock.tryLock(0)) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "preset library busy"));
  refresh();
  if (!existing.isEmpty() && preset(existing)["source"].toString() != "user") invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "preset is read-only"));
  const auto id = existing.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : existing;
  auto config = settings(s); config.remove("presetId"); config.remove("tipId"); config.remove("seed");
  if (!colors) { config.remove("color"); config.remove("backgroundColor"); }
  QJsonObject entry{{"version", 1}, {"id", id}, {"name", name.trimmed()}, {"folder", folder}, {"source", "user"},
                    {"settings", config}, {"includeColors", colors}};
  if (s.tip) {
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    if (!coverage_image_from_brush_tip(*s.tip).save(&buffer, "PNG")) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "preset tip"));
    entry["tipPng"] = QString::fromLatin1(bytes.toBase64());
    config["spacing"] = s.spacing.value_or(s.tip->default_spacing); entry["settings"] = config;
  }
  QSaveFile file(QDir(directory_).filePath(id + ".json")); const auto bytes = QJsonDocument(entry).toJson();
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "preset write"));
  refresh(); return id;
}
void BrushAutomationLibrary::remove(const QString& id) {
  QLockFile lock(QDir(directory_).filePath("write.lock")); if (!lock.tryLock(0)) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "preset library busy"));
  refresh(); if (preset(id)["source"].toString() != "user") invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "preset is read-only"));
  if (!QFile::remove(QDir(directory_).filePath(id + ".json"))) invalid(QCoreApplication::translate("patchy::ui::BrushAutomationLibrary", "preset remove"));
  refresh();
}
}  // namespace patchy::ui
