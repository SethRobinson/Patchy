// Krita filter, fill and vector layers as Patchy layers (docs/kra.md "Filters", "Fill
// layers", "Vector layers"). The settings formats were read from files Krita 5.3 writes.

#include "formats/kra_internal.hpp"

#include "core/pixel_depth.hpp"
#include "formats/svg_document_io.hpp"
#include "formats/svg_xml.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace patchy::kra::detail {

namespace {

[[nodiscard]] std::optional<double> to_number(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\n')) {
    text.remove_prefix(1);
  }
  if (text.empty()) {
    return std::nullopt;
  }
  std::istringstream stream{std::string(text)};
  stream.imbue(std::locale::classic());
  double value = 0.0;
  stream >> value;
  if (stream.fail() || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::vector<double> numbers_separated(std::string_view text, char separator) {
  std::vector<double> values;
  std::size_t start = 0;
  while (start <= text.size()) {
    const auto end = text.find(separator, start);
    const auto piece = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (const auto value = to_number(piece); value.has_value()) {
      values.push_back(*value);
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return values;
}

[[nodiscard]] int clamp_int(double value, int low, int high) {
  return std::clamp(static_cast<int>(std::lround(value)), low, high);
}

// Krita's curve text: "x,y;x,y;..." with both in 0..1.
[[nodiscard]] CurveControlPoints curve_points(std::string_view text) {
  CurveControlPoints points;
  std::size_t start = 0;
  while (start < text.size()) {
    const auto end = text.find(';', start);
    const auto piece = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    const auto pair = numbers_separated(piece, ',');
    if (pair.size() == 2) {
      points.push_back(CurveControlPoint{clamp_int(pair[0] * 255.0, 0, 255), clamp_int(pair[1] * 255.0, 0, 255)});
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.input < b.input; });
  points.erase(std::unique(points.begin(), points.end(),
                           [](const auto& a, const auto& b) { return a.input == b.input; }),
               points.end());
  if (points.size() < 2) {
    return CurveControlPoints{{0, 0}, {255, 255}};
  }
  return points;
}

[[nodiscard]] bool is_identity_curve(const CurveControlPoints& points) {
  return std::all_of(points.begin(), points.end(), [](const auto& point) { return point.input == point.output; });
}

// One Levels record from Krita's "black;white;gamma;outblack;outwhite" text (0..1 values).
[[nodiscard]] std::optional<LevelsRecord> levels_record(std::string_view text) {
  const auto values = numbers_separated(text, ';');
  if (values.size() < 5) {
    return std::nullopt;
  }
  LevelsRecord record;
  record.black_input = clamp_int(values[0] * 255.0, 0, 253);
  record.white_input = clamp_int(values[1] * 255.0, record.black_input + 2, 255);
  record.gamma_percent = clamp_int(values[2] * 100.0, 10, 999);
  record.black_output = clamp_int(values[3] * 255.0, 0, 255);
  record.white_output = clamp_int(values[4] * 255.0, 0, 255);
  return record;
}

}  // namespace

const std::string* FilterConfig::find(const std::string& key) const {
  const auto found = params.find(key);
  return found == params.end() ? nullptr : &found->second;
}

double FilterConfig::number(const std::string& key, double fallback) const {
  const auto* value = find(key);
  if (value == nullptr) {
    return fallback;
  }
  return to_number(*value).value_or(fallback);
}

bool FilterConfig::flag(const std::string& key, bool fallback) const {
  const auto* value = find(key);
  if (value == nullptr) {
    return fallback;
  }
  return *value == "true" || *value == "1";
}

FilterConfig parse_filter_config(std::string name, std::span<const std::uint8_t> xml) {
  FilterConfig config;
  config.name = std::move(name);
  svg::XmlNode root;
  try {
    root = svg::parse_xml(xml);
  } catch (const std::exception&) {
    return config;
  }
  if (const auto* version = root.attribute("version"); version != nullptr) {
    config.version = static_cast<int>(to_number(*version).value_or(0.0));
  }
  for (const auto& child : root.children) {
    if (child.is_text() || child.name != "param") {
      continue;
    }
    if (const auto* key = child.attribute("name"); key != nullptr) {
      config.params[*key] = child.all_text();
    }
  }
  return config;
}

std::optional<AdjustmentSettings> adjustment_from_filter(const FilterConfig& config, bool& approximate,
                                                       BlendMode& blend) {
  approximate = false;
  blend = BlendMode::Normal;
  AdjustmentSettings settings;
  const auto& name = config.name;
  if (name == "invert") {
    settings.kind = AdjustmentKind::Invert;
    return settings;
  }
  if (name == "levels") {
    settings.kind = AdjustmentKind::Levels;
    // Version 2 stores 0..1 records ("lightness" for the composite, "channel_N" per channel);
    // version 1 stored 0..255 numbers.
    const auto* mode = config.find("mode");
    const bool lightness_mode = mode == nullptr || *mode == "lightness";
    if (const auto* lightness = config.find("lightness"); lightness != nullptr && !lightness_mode) {
      // The per-channel mode ignores the lightness record (measured: Krita draws nothing
      // for it); only the channel records below apply.
    } else if (lightness != nullptr) {
      if (const auto record = levels_record(*lightness); record.has_value()) {
        settings.levels.black_input = record->black_input;
        settings.levels.white_input = record->white_input;
        settings.levels.gamma_percent = record->gamma_percent;
        settings.levels.black_output = record->black_output;
        settings.levels.white_output = record->white_output;
      }
    } else {
      settings.levels.black_input = clamp_int(config.number("blackvalue", 0.0), 0, 253);
      settings.levels.white_input = clamp_int(config.number("whitevalue", 255.0), settings.levels.black_input + 2, 255);
      settings.levels.gamma_percent = clamp_int(config.number("gammavalue", 1.0) * 100.0, 10, 999);
      settings.levels.black_output = clamp_int(config.number("outblackvalue", 0.0), 0, 255);
      settings.levels.white_output = clamp_int(config.number("outwhitevalue", 255.0), 0, 255);
    }
    const auto channels = clamp_int(config.number("number_of_channels", 0.0), 0, 8);
    // Integer RGB lists blue, green, red (then alpha); the records apply per channel.
    std::array<LevelsRecord*, 3> targets{&settings.levels.blue, &settings.levels.green, &settings.levels.red};
    for (int channel = 0; channel < std::min(channels, 3); ++channel) {
      if (const auto* text = config.find("channel_" + std::to_string(channel)); text != nullptr) {
        if (const auto record = levels_record(*text); record.has_value()) {
          *targets[static_cast<std::size_t>(channel)] = *record;
        }
      }
    }
    // Krita's lightness mode (and the version 1 settings, which Krita reads the same way)
    // levels CIE L* and keeps a and b (measured within 1 level). Patchy's Levels runs on
    // RGB; at Luminosity it keeps the colors and comes closest (docs/kra.md).
    approximate = lightness_mode;
    if (lightness_mode) {
      blend = BlendMode::Luminosity;
    }
    return settings;
  }
  if (name == "perchannel") {
    settings.kind = AdjustmentKind::Curves;
    const auto transfers = clamp_int(config.number("nTransfers", 0.0), 0, 16);
    const auto curve = [&](int index) {
      const auto* text = config.find("curve" + std::to_string(index));
      return text == nullptr ? CurveControlPoints{{0, 0}, {255, 255}} : curve_points(*text);
    };
    if (transfers == 8) {
      // Krita 5 for RGB: the composite, red, green, blue, alpha, then hue, saturation and
      // lightness curves (measured one curve at a time on Krita's renders).
      settings.curves.rgb = curve(0);
      settings.curves.red = curve(1);
      settings.curves.green = curve(2);
      settings.curves.blue = curve(3);
      for (int index = 4; index < 8; ++index) {
        if (!is_identity_curve(curve(index))) {
          approximate = true;  // alpha, hue, saturation or lightness: Curves has none of these
        }
      }
      return settings;
    }
    // Older files store fewer curves; their first curve is applied to the composite.
    settings.curves.rgb = curve(0);
    for (int index = 1; index < transfers; ++index) {
      if (!is_identity_curve(curve(index))) {
        approximate = true;
      }
    }
    return settings;
  }
  if (name == "hsvadjustment") {
    approximate = true;  // Krita's HSV/HSL/HSI/HSY models are not Photoshop's Hue/Saturation
    settings.kind = AdjustmentKind::HueSaturation;
    auto& hs = settings.hue_saturation;
    if (config.flag("colorize", false)) {
      hs.colorize = true;
      hs.colorize_hue = clamp_int(config.number("h", 0.0), 0, 360);
      hs.colorize_saturation = clamp_int(config.number("s", 25.0), 0, 100);
      hs.colorize_lightness = clamp_int(config.number("v", 0.0), -100, 100);
    } else {
      hs.hue_shift = clamp_int(config.number("h", 0.0), -180, 180);
      hs.saturation_delta = clamp_int(config.number("s", 0.0), -100, 100);
      hs.lightness_delta = clamp_int(config.number("v", 0.0), -100, 100);
    }
    return settings;
  }
  if (name == "desaturate") {
    const auto type = clamp_int(config.number("type", 0.0), 0, 6);
    if (type == 1 || type == 2 || type == 3) {
      // Luminosity (BT.709 or BT.601) or the average: a monochrome channel mix.
      settings.kind = AdjustmentKind::ChannelMixer;
      settings.channel_mixer.monochrome = true;
      const std::array<int, 3> weights = type == 1 ? std::array<int, 3>{21, 72, 7}
                                         : type == 2 ? std::array<int, 3>{30, 59, 11}
                                                     : std::array<int, 3>{33, 34, 33};
      settings.channel_mixer.gray.red = weights[0];
      settings.channel_mixer.gray.green = weights[1];
      settings.channel_mixer.gray.blue = weights[2];
      approximate = true;  // whole-percent weights
      return settings;
    }
    // Lightness ((max + min) / 2) is Hue/Saturation's full desaturation; min and max are not.
    settings.kind = AdjustmentKind::HueSaturation;
    settings.hue_saturation.saturation_delta = -100;
    approximate = type != 0;
    return settings;
  }
  if (name == "colorbalance") {
    approximate = true;  // Krita's tone ranges and strengths are not Photoshop's
    settings.kind = AdjustmentKind::ColorBalance;
    auto& balance = settings.color_balance;
    // Krita 5.3 names its sliders cyan_red, magenta_blue and yellow_green (Krita 4 wrote
    // magenta_green and yellow_blue for the same sliders). Measured on Krita's renders, the
    // second moves green (positive toward green) and the third moves blue with the opposite
    // sign (positive toward yellow).
    const auto tone = [&](const std::string& range) {
      ColorBalanceTone value;
      value.cyan_red = clamp_int(config.number("cyan_red_" + range, 0.0), -100, 100);
      value.magenta_green = clamp_int(
          config.number("magenta_blue_" + range, config.number("magenta_green_" + range, 0.0)), -100, 100);
      value.yellow_blue = clamp_int(
          -config.number("yellow_green_" + range, config.number("yellow_blue_" + range, 0.0)), -100, 100);
      return value;
    };
    balance.set_tone(ColorBalanceToneRange::Shadows, tone("shadows"));
    balance.set_tone(ColorBalanceToneRange::Midtones, tone("midtones"));
    balance.set_tone(ColorBalanceToneRange::Highlights, tone("highlights"));
    balance.preserve_luminosity = config.flag("preserve_luminosity", true);
    return settings;
  }
  if (name == "threshold") {
    settings.kind = AdjustmentKind::Threshold;
    settings.threshold.level = clamp_int(config.number("threshold", 128.0), 1, 255);
    approximate = true;  // Krita thresholds each channel; Photoshop thresholds luminance
    return settings;
  }
  if (name == "posterize") {
    settings.kind = AdjustmentKind::Posterize;
    // Krita's steps count the intervals: 4 steps give five values (0, 64, 128, 191, 255).
    settings.posterize.levels = clamp_int(config.number("steps", 16.0) + 1.0, 2, 255);
    approximate = true;
    return settings;
  }
  return std::nullopt;
}

namespace {

// Premultiplied RGBA planes (deep scale) of a whole buffer, and back.
struct Planes {
  std::int32_t width{0};
  std::int32_t height{0};
  std::vector<float> values;  // 4 per pixel, premultiplied
};

[[nodiscard]] DeepDomain domain_of(const PixelBuffer& pixels) {
  return pixels.format().bit_depth == BitDepth::Float32 ? DeepDomain::Linear : DeepDomain::Encoded;
}

[[nodiscard]] Planes load_planes(const PixelBuffer& pixels) {
  Planes planes{pixels.width(), pixels.height(), {}};
  planes.values.resize(static_cast<std::size_t>(pixels.width()) * static_cast<std::size_t>(pixels.height()) * 4U);
  const auto domain = domain_of(pixels);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    auto row = std::span<float>(planes.values).subspan(static_cast<std::size_t>(y) * pixels.width() * 4U,
                                                       static_cast<std::size_t>(pixels.width()) * 4U);
    load_rgba_row(pixels, y, 0, pixels.width(), domain, row);
    for (std::size_t i = 0; i < row.size(); i += 4) {
      const float alpha = row[i + 3] / kDeepScale;
      row[i] *= alpha;
      row[i + 1] *= alpha;
      row[i + 2] *= alpha;
    }
  }
  return planes;
}

void store_planes(const Planes& planes, PixelBuffer& pixels) {
  const auto domain = domain_of(pixels);
  const bool linear = domain == DeepDomain::Linear;
  std::vector<float> row(static_cast<std::size_t>(planes.width) * 4U);
  for (std::int32_t y = 0; y < planes.height; ++y) {
    const auto* source = planes.values.data() + static_cast<std::size_t>(y) * planes.width * 4U;
    for (std::size_t i = 0; i < row.size(); i += 4) {
      const float alpha = std::clamp(source[i + 3], 0.0F, kDeepScale);
      for (std::size_t c = 0; c < 3; ++c) {
        float value = alpha > 0.0F ? source[i + c] * kDeepScale / alpha : 0.0F;
        row[i + c] = linear ? std::max(0.0F, value) : std::clamp(value, 0.0F, kDeepScale);
      }
      row[i + 3] = alpha;
    }
    store_rgba_row(pixels, y, 0, planes.width, domain, row);
  }
}

// One separable pass of `kernel` (odd length) along x or y, edges clamped.
[[nodiscard]] Planes convolve_1d(const Planes& in, const std::vector<float>& kernel, bool horizontal) {
  Planes out{in.width, in.height, std::vector<float>(in.values.size())};
  const auto half = static_cast<std::int32_t>(kernel.size() / 2);
  for (std::int32_t y = 0; y < in.height; ++y) {
    for (std::int32_t x = 0; x < in.width; ++x) {
      float sum[4] = {0.0F, 0.0F, 0.0F, 0.0F};
      for (std::int32_t k = -half; k <= half; ++k) {
        const auto sx = horizontal ? std::clamp(x + k, 0, in.width - 1) : x;
        const auto sy = horizontal ? y : std::clamp(y + k, 0, in.height - 1);
        const float weight = kernel[static_cast<std::size_t>(k + half)];
        const auto* px = in.values.data() + (static_cast<std::size_t>(sy) * in.width + sx) * 4U;
        for (int c = 0; c < 4; ++c) {
          sum[c] += weight * px[c];
        }
      }
      auto* target = out.values.data() + (static_cast<std::size_t>(y) * in.width + x) * 4U;
      for (int c = 0; c < 4; ++c) {
        target[c] = sum[c];
      }
    }
  }
  return out;
}

[[nodiscard]] Planes gaussian(const Planes& in, double radius) {
  const double sigma = 0.3 * radius + 0.3;
  const auto half = std::max(1, static_cast<int>(std::ceil(radius)));
  std::vector<float> kernel(static_cast<std::size_t>(half * 2 + 1));
  double total = 0.0;
  for (int k = -half; k <= half; ++k) {
    const double weight = std::exp(-(k * k) / (2.0 * sigma * sigma));
    kernel[static_cast<std::size_t>(k + half)] = static_cast<float>(weight);
    total += weight;
  }
  for (auto& weight : kernel) {
    weight = static_cast<float>(weight / total);
  }
  return convolve_1d(convolve_1d(in, kernel, true), kernel, false);
}

}  // namespace

bool krita_filter_is_applied(const FilterConfig& config) {
  return config.name == "sharpen" || config.name == "unsharp" || config.name == "gaussian blur";
}

bool apply_krita_filter(const FilterConfig& config, PixelBuffer& pixels) {
  if (!krita_filter_is_applied(config) || pixels.empty() || pixels.format().channels != 4) {
    return false;
  }
  const auto planes = load_planes(pixels);
  Planes result{planes.width, planes.height, std::vector<float>(planes.values.size())};
  if (config.name == "gaussian blur") {
    const double horizontal = std::clamp(config.number("horizRadius", 5.0), 0.0, 500.0);
    const double vertical = std::clamp(config.number("vertRadius", horizontal), 0.0, 500.0);
    result = gaussian(planes, (horizontal + vertical) / 2.0);
  } else if (config.name == "sharpen") {
    const bool linear = domain_of(pixels) == DeepDomain::Linear;
    const float edge = -2.0F / 3.0F;
    const float centre = 11.0F / 3.0F;
    for (std::int32_t y = 0; y < planes.height; ++y) {
      for (std::int32_t x = 0; x < planes.width; ++x) {
        const auto at = [&](std::int32_t sx, std::int32_t sy) {
          return planes.values.data() + (static_cast<std::size_t>(std::clamp(sy, 0, planes.height - 1)) * planes.width +
                                         static_cast<std::size_t>(std::clamp(sx, 0, planes.width - 1))) *
                                            4U;
        };
        auto* target = result.values.data() + (static_cast<std::size_t>(y) * planes.width + x) * 4U;
        for (int c = 0; c < 4; ++c) {
          target[c] = centre * at(x, y)[c] + edge * (at(x - 1, y)[c] + at(x + 1, y)[c] + at(x, y - 1)[c] + at(x, y + 1)[c]);
        }
        // Alpha keeps the pixel's own coverage; colors stay within it.
        target[3] = at(x, y)[3];
        const float upper = linear ? std::numeric_limits<float>::max() : std::max(target[3], 0.0F);
        for (int c = 0; c < 3; ++c) {
          target[c] = std::clamp(target[c], 0.0F, upper);
        }
      }
    }
  } else {
    const double half_size = std::clamp(config.number("halfSize", 1.0), 0.0, 500.0);
    const double amount = std::clamp(config.number("amount", 0.5), 0.0, 100.0);
    const double threshold = std::clamp(config.number("threshold", 0.0), 0.0, 255.0);
    const bool lightness_only = config.flag("lightnessOnly", true);
    const auto blurred = gaussian(planes, half_size);
    for (std::size_t i = 0; i < planes.values.size(); i += 4) {
      const auto* px = planes.values.data() + i;
      const auto* blur = blurred.values.data() + i;
      auto* target = result.values.data() + i;
      target[3] = px[3];
      if (lightness_only) {
        // The difference of the pixel's lightness, added to every channel.
        const float light = 0.299F * px[0] + 0.587F * px[1] + 0.114F * px[2];
        const float blur_light = 0.299F * blur[0] + 0.587F * blur[1] + 0.114F * blur[2];
        const float difference = light - blur_light;
        for (int c = 0; c < 3; ++c) {
          target[c] = std::abs(difference) * (px[3] > 0.0F ? kDeepScale / px[3] : 0.0F) > threshold
                          ? px[c] + static_cast<float>(amount) * difference
                          : px[c];
        }
      } else {
        for (int c = 0; c < 3; ++c) {
          const float difference = px[c] - blur[c];
          target[c] = std::abs(difference) * (px[3] > 0.0F ? kDeepScale / px[3] : 0.0F) > threshold
                          ? px[c] + static_cast<float>(amount) * difference
                          : px[c];
        }
      }
    }
  }
  store_planes(result, pixels);
  return true;
}

std::optional<VectorFill> fill_from_generator(const FilterConfig& config) {
  if (config.name != "color") {
    return std::nullopt;
  }
  const auto* text = config.find("color");
  if (text == nullptr) {
    return std::nullopt;
  }
  // A KoColor: <color><RGB r= g= b= space=/></color> (or <Gray g=/>), values 0..1.
  svg::XmlNode root;
  try {
    root = svg::parse_xml(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text->data()),
                                                        text->size()));
  } catch (const std::exception&) {
    return std::nullopt;
  }
  for (const auto& child : root.children) {
    if (child.is_text()) {
      continue;
    }
    const auto read = [&](std::string_view key) {
      const auto* value = child.attribute(key);
      return value == nullptr ? 0.0 : to_number(*value).value_or(0.0);
    };
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    if (child.name == "RGB") {
      r = read("r");
      g = read("g");
      b = read("b");
    } else if (child.name == "Gray") {
      r = g = b = read("g");
    } else {
      continue;
    }
    // A linear-light space ("...-g10...") stores light; Patchy's colors are encoded.
    const auto* space = child.attribute("space");
    const bool linear = space != nullptr && (space->find("g10") != std::string::npos ||
                                             space->find("linear") != std::string::npos);
    const auto channel = [&](double value) {
      value = std::clamp(value, 0.0, 1.0);
      return static_cast<std::uint8_t>(std::lround((linear ? srgb_encode(value) : value) * 255.0));
    };
    VectorFill fill;
    fill.kind = VectorFillKind::Solid;
    fill.color = RgbColor{channel(r), channel(g), channel(b)};
    return fill;
  }
  return std::nullopt;
}

namespace {

void reassign_ids(Layer& layer, Document& target) {
  layer = layer.clone_with_id(target.allocate_layer_id());
  for (auto& child : layer.children()) {
    reassign_ids(child, target);
  }
}

[[nodiscard]] std::string format_number(double value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(10) << value;
  return out.str();
}

// Replaces (or adds) one attribute inside the root <svg ...> start tag.
void set_root_attribute(std::string& text, std::size_t tag_start, std::size_t& tag_end, const std::string& name,
                        const std::string& value) {
  const auto tag = text.substr(tag_start, tag_end - tag_start);
  // Find " name=" with either quote, outside other attribute names (preceded by whitespace).
  std::size_t at = std::string::npos;
  for (std::size_t i = 0; i + name.size() + 1 < tag.size(); ++i) {
    if ((tag[i] == ' ' || tag[i] == '\n' || tag[i] == '\t' || tag[i] == '\r') &&
        tag.compare(i + 1, name.size(), name) == 0) {
      auto j = i + 1 + name.size();
      while (j < tag.size() && tag[j] == ' ') {
        ++j;
      }
      if (j < tag.size() && tag[j] == '=') {
        at = i + 1;
        break;
      }
    }
  }
  const std::string replacement = name + "=\"" + value + "\"";
  if (at == std::string::npos) {
    text.insert(tag_start + 4, " " + replacement);
    tag_end += replacement.size() + 1;
    return;
  }
  auto j = at + name.size();
  while (j < tag.size() && tag[j] != '=') {
    ++j;
  }
  ++j;
  while (j < tag.size() && tag[j] == ' ') {
    ++j;
  }
  if (j >= tag.size() || (tag[j] != '"' && tag[j] != '\'')) {
    return;
  }
  const auto quote = tag[j];
  const auto close = tag.find(quote, j + 1);
  if (close == std::string::npos) {
    return;
  }
  const auto old_length = close + 1 - at;
  text.replace(tag_start + at, old_length, replacement);
  tag_end = tag_end - old_length + replacement.size();
}

}  // namespace

std::vector<Layer> read_vector_layer(std::span<const std::uint8_t> svg, Document& target,
                                     std::vector<std::string>& notices) {
  std::string text(svg.begin(), svg.end());
  // The root element: the first "<svg" that starts a tag.
  auto tag_start = text.find("<svg");
  while (tag_start != std::string::npos && tag_start + 4 < text.size() &&
         !(text[tag_start + 4] == ' ' || text[tag_start + 4] == '\n' || text[tag_start + 4] == '\r' ||
           text[tag_start + 4] == '\t' || text[tag_start + 4] == '>')) {
    tag_start = text.find("<svg", tag_start + 4);
  }
  if (tag_start == std::string::npos) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "the vector data is not SVG"));
  }
  auto tag_end = text.find('>', tag_start);
  if (tag_end == std::string::npos) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "the vector data is not SVG"));
  }
  // Krita writes the canvas size in points with a matching viewBox: user units are points.
  // Sizing the root in pixels maps them onto the canvas at the document's resolution.
  const auto& print = target.print_settings();
  const auto view_box = format_number(0) + " " + format_number(0) + " " +
                        format_number(target.width() * 72.0 / print.horizontal_ppi) + " " +
                        format_number(target.height() * 72.0 / print.vertical_ppi);
  if (text.substr(tag_start, tag_end - tag_start).find("viewBox") == std::string::npos) {
    set_root_attribute(text, tag_start, tag_end, "viewBox", view_box);
  }
  set_root_attribute(text, tag_start, tag_end, "preserveAspectRatio", "none");
  set_root_attribute(text, tag_start, tag_end, "width", std::to_string(target.width()));
  set_root_attribute(text, tag_start, tag_end, "height", std::to_string(target.height()));

  std::vector<std::string> svg_notices;
  Document shapes = svg::DocumentIo::read(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()), &svg_notices);
  for (auto& notice : svg_notices) {
    notices.push_back(std::move(notice));
  }
  if (shapes.width() != target.width() || shapes.height() != target.height()) {
    notices.emplace_back(PATCHY_TRANSLATE_NOOP("QObject", "the shapes were scaled to fit the canvas"));
  }
  for (const auto& pattern : shapes.metadata().patterns.patterns) {
    target.metadata().patterns.adopt(pattern);
  }
  std::vector<Layer> layers = std::move(shapes.layers());
  for (auto& layer : layers) {
    reassign_ids(layer, target);
  }
  return layers;
}

}  // namespace patchy::kra::detail
