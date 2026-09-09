#include "ui/raw_develop_settings.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QObject>
#include <QSaveFile>

#include <array>
#include <cmath>

namespace patchy::ui {
namespace {

template <typename Enum, std::size_t N>
bool enum_field(QJsonObject& json, const char* key, Enum& value,
                const std::array<const char*, N>& tokens, bool reading) {
  const auto name = QString::fromLatin1(key);
  if (!reading) {
    const auto index = static_cast<std::size_t>(value);
    if (index >= N) return false;
    json.insert(name, QString::fromLatin1(tokens[index]));
    return true;
  }
  const auto field = json.value(name);
  if (!field.isString()) return false;
  for (std::size_t i = 0; i < N; ++i) {
    if (field.toString() == QLatin1String(tokens[i])) {
      value = static_cast<Enum>(i);
      return true;
    }
  }
  return false;
}

bool parameters(QJsonObject& json, raw::DevelopParams& params, bool reading) {
  const auto number = [&](const char* key, double& value, double low, double high) {
    const auto name = QString::fromLatin1(key);
    if (!reading) { json.insert(name, value); return true; }
    const auto field = json.value(name);
    if (!field.isDouble()) return false;
    const auto parsed = field.toDouble();
    if (!std::isfinite(parsed) || parsed < low || parsed > high) return false;
    value = parsed;
    return true;
  };
  const auto boolean = [&](const char* key, bool& value) {
    const auto name = QString::fromLatin1(key);
    if (!reading) { json.insert(name, value); return true; }
    const auto field = json.value(name);
    if (!field.isBool()) return false;
    value = field.toBool();
    return true;
  };
  double wavelet = params.wavelet_denoise_threshold;
  const bool valid =
      enum_field(json, "whiteBalance", params.white_balance, std::array{"asShot", "auto", "custom"}, reading) &&
      enum_field(json, "highlightRecovery", params.highlight_recovery, std::array{"clip", "unclip", "blend", "rebuild"}, reading) &&
      enum_field(json, "demosaic", params.demosaic, std::array{"linear", "vng", "ppg", "ahd", "dcb", "dht", "aahd"}, reading) &&
      enum_field(json, "noiseReduction", params.noise_reduction, std::array{"auto", "manual", "off"}, reading) &&
      enum_field(json, "fbdd", params.fbdd, std::array{"off", "light", "full"}, reading) &&
      number("temperature", params.custom_white_balance.temperature_k, 2000, 25000) &&
      number("tint", params.custom_white_balance.tint, -150, 150) &&
      number("exposure", params.exposure_ev, -2, 3) &&
      number("brightness", params.brightness, 0.25, 4) &&
      number("contrast", params.contrast, -100, 100) &&
      number("highlights", params.highlights, -100, 100) &&
      number("shadows", params.shadows, -100, 100) &&
      number("saturation", params.saturation, -100, 100) &&
      number("vibrance", params.vibrance, -100, 100) &&
      number("waveletDenoise", wavelet, 0, 1000) && std::floor(wavelet) == wavelet &&
      boolean("autoBrighten", params.auto_brighten) && boolean("halfSize", params.half_size);
  params.wavelet_denoise_threshold = static_cast<int>(wavelet);
  if (!valid) return false;
  if (params.processing_version == 1) {
    params.profile = raw::RenderingProfile::Neutral;
    params.color_denoise_passes = 0;
    return true;
  }
  double color_passes = params.color_denoise_passes;
  const bool current_valid =
      enum_field(json, "profile", params.profile, std::array{"neutral", "natural"}, reading) &&
      number("colorDenoisePasses", color_passes, 0, 4) && std::floor(color_passes) == color_passes;
  params.color_denoise_passes = static_cast<int>(color_passes);
  return current_valid;
}

QString settings_error(const QString& path) {
  return QObject::tr("Could not save RAW settings next to %1.").arg(QFileInfo(path).fileName());
}

}  // namespace

QString raw_develop_settings_path(const QString& source_path) {
  return source_path + QStringLiteral(".rawprefs");
}

RawDevelopSettings load_raw_develop_settings(const QString& source_path) {
  RawDevelopSettings result;
  QFile file(raw_develop_settings_path(source_path));
  result.exists = QFileInfo::exists(file.fileName());
  if (!result.exists) return result;
  result.notice = QObject::tr("Saved RAW settings could not be read. Patchy defaults are being used.");
  // Settings should be small; reject oversized files without allocating their contents.
  if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return result;
  result.original_bytes = file.readAll();
  if (file.error() != QFileDevice::NoError) return result;
  const auto doc = QJsonDocument::fromJson(result.original_bytes);
  if (!doc.isObject()) return result;
  const auto root = doc.object();
  if (root.value(QStringLiteral("format")) != QJsonValue(QStringLiteral("patchy.rawprefs")) ||
      root.value(QStringLiteral("version")) != QJsonValue(1) ||
      !root.value(QStringLiteral("parameters")).isObject()) return result;
  const auto version = root.value(QStringLiteral("processingVersion"));
  if (!version.isDouble() || version.toDouble() != version.toInt() ||
      version.toInt() < 1 || version.toInt() > raw::kProcessingVersion) return result;
  auto fields = root.value(QStringLiteral("parameters")).toObject();
  raw::DevelopParams parsed;
  parsed.processing_version = version.toInt();
  if (!parameters(fields, parsed, true)) return result;
  result.params = parsed;
  result.preserved = root;
  result.recognized = true;
  result.notice.clear();
  return result;
}

QString save_raw_develop_settings(const QString& source_path, const raw::DevelopParams& requested,
                                 const RawDevelopSettings& loaded, bool replace_unrecognized) {
  const auto current = load_raw_develop_settings(source_path);
  if (current.exists != loaded.exists || current.original_bytes != loaded.original_bytes)
    return QObject::tr("The RAW settings file changed while this photo was open. Reopen the photo before saving settings.");
  if (current.exists && !current.recognized && !replace_unrecognized)
    return QObject::tr("The existing RAW settings file is unreadable or unsupported. Replace it to save these adjustments.");
  auto params = raw::normalize_develop_params(requested);
  if (params.processing_version < 1 || params.processing_version > raw::kProcessingVersion)
    return settings_error(source_path);
  const auto path = raw_develop_settings_path(source_path);
  if (params == raw::DevelopParams{}) {
    if (current.exists && !QFile::remove(path)) return settings_error(source_path);
    return {};
  }
  auto root = current.recognized ? current.preserved : QJsonObject{};
  auto fields = root.value(QStringLiteral("parameters")).toObject();
  if (!parameters(fields, params, false)) return settings_error(source_path);
  root.insert(QStringLiteral("format"), QStringLiteral("patchy.rawprefs"));
  root.insert(QStringLiteral("version"), 1);
  root.insert(QStringLiteral("processingVersion"), params.processing_version);
  root.insert(QStringLiteral("parameters"), fields);
  const auto bytes = QJsonDocument(root).toJson();
  if (bytes == current.original_bytes) return {};
  QSaveFile file(path);
  // No direct-write fallback: failure must preserve the previous sidecar.
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    return settings_error(source_path);
  return {};
}

}  // namespace patchy::ui
