#include "ui/theme_effects.hpp"

#include "ui/app_settings.hpp"
#include "ui/theme_package.hpp"

#include <QEvent>
#include <QPainter>
#include <QPen>
#include <QPaintEvent>
#include <QPointer>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace patchy::ui {

namespace {

// Persisted identifier.
const QString& play_theme_effects_key() {
  static const QString key = QStringLiteral("preferences/playThemeEffects");
  return key;
}

constexpr double kPi = 3.14159265358979323846;
constexpr double kGravity = 260.0;  // px/s^2, fireworks sparks and confetti
constexpr int kFrameMs = 33;

QPointer<ThemeEffectsOverlay>& overlay_slot() {
  static QPointer<ThemeEffectsOverlay> overlay;
  return overlay;
}

[[nodiscard]] double option(const QVariantMap& spec, const char* key, double fallback) {
  const auto value = spec.value(QLatin1String(key));
  bool ok = false;
  const double number = value.toDouble(&ok);
  return value.isValid() && ok && std::isfinite(number) ? number : fallback;
}

// Effects draw content, not chrome: their colors are generated hues (the exemption
// docs/theme-files.md records), varied per burst.
[[nodiscard]] QColor effect_hue(double unit, double saturation = 0.85, double value = 1.0) {
  return QColor::fromHsvF(static_cast<float>(unit - std::floor(unit)), static_cast<float>(saturation),
                          static_cast<float>(value));
}

}  // namespace

bool theme_effects_enabled() { return app_settings().value(play_theme_effects_key(), true).toBool(); }

void set_theme_effects_enabled(bool enabled) { app_settings().setValue(play_theme_effects_key(), enabled); }

QStringList theme_effect_names() {
  return {QStringLiteral("fireworks"), QStringLiteral("sparkles"), QStringLiteral("confetti"),
          QStringLiteral("sprite")};
}

ThemeEffectsOverlay::ThemeEffectsOverlay(QWidget* host) : QWidget(host), host_(host) {
  setObjectName(QStringLiteral("themeEffectsOverlay"));
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setFocusPolicy(Qt::NoFocus);
  setAutoFillBackground(false);
  timer_.setInterval(kFrameMs);
  connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
  if (host_ != nullptr) {
    host_->installEventFilter(this);
  }
  follow_host();
  hide();
}

ThemeEffectsOverlay::~ThemeEffectsOverlay() {
  if (host_ != nullptr) {
    host_->removeEventFilter(this);
  }
}

double ThemeEffectsOverlay::random_unit() {
  // splitmix64 (AGENTS.md: deterministic randomness), mapped to [0, 1).
  std::uint64_t z = (random_state_ += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  z ^= z >> 31U;
  return static_cast<double>(z >> 11U) * (1.0 / 9007199254740992.0);
}

void ThemeEffectsOverlay::follow_host() {
  if (host_ != nullptr) {
    setGeometry(host_->rect());
  }
}

bool ThemeEffectsOverlay::eventFilter(QObject* watched, QEvent* event) {
  if (watched == host_ && event->type() == QEvent::Resize) {
    follow_host();
  }
  return QWidget::eventFilter(watched, event);
}

int ThemeEffectsOverlay::particle_total() const {
  int total = 0;
  for (const auto& effect : effects_) {
    total += static_cast<int>(effect.particles.size());
  }
  return total;
}

bool ThemeEffectsOverlay::play(const QVariantMap& spec, const ThemeAssets* assets) {
  if (static_cast<int>(effects_.size()) >= kMaxEffects || width() <= 0 || height() <= 0) {
    return false;
  }
  const auto name = spec.value(QStringLiteral("name")).toString();
  Effect effect;
  effect.start_screen_only = spec.value(QStringLiteral("where")).toString() == QStringLiteral("start-screen");
  if (effect.start_screen_only && (!start_screen_probe_ || !start_screen_probe_())) {
    return false;
  }
  const QPointF center(std::clamp(option(spec, "x", 0.5), 0.0, 1.0) * width(),
                       std::clamp(option(spec, "y", 0.4), 0.0, 1.0) * height());
  const double default_duration = name == QStringLiteral("confetti") ? 2.6 : 2.0;
  effect.duration = std::clamp(option(spec, "duration", default_duration * 1000.0), 100.0,
                               static_cast<double>(kMaxDurationMs)) /
                    1000.0;
  if (name == QStringLiteral("fireworks")) {
    effect.kind = Kind::Fireworks;
    const int bursts = std::clamp(static_cast<int>(option(spec, "bursts", 4)), 1, 8);
    const double spread = std::min(width(), height()) * 0.28;
    for (int i = 0; i < bursts; ++i) {
      const QPointF offset((random_unit() * 2.0 - 1.0) * spread * 1.4, (random_unit() * 2.0 - 1.0) * spread * 0.7);
      effect.bursts.push_back({i * 0.28 + random_unit() * 0.12, center + (i == 0 ? QPointF() : offset),
                               effect_hue(random_unit())});
    }
  } else if (name == QStringLiteral("sparkles")) {
    effect.kind = Kind::Sparkles;
    const double radius = option(spec, "radius", std::min(width(), height()) * 0.18);
    const int count = std::clamp(static_cast<int>(option(spec, "count", 48)), 1, 200);
    for (int i = 0; i < count; ++i) {
      Particle p;
      const double angle = random_unit() * 2.0 * kPi;
      const double distance = std::sqrt(random_unit()) * radius;
      p.position = center + QPointF(std::cos(angle) * distance, std::sin(angle) * distance);
      p.previous = p.position;
      p.velocity = QPointF(0.0, -12.0 - random_unit() * 18.0);
      p.age = -random_unit() * effect.duration * 0.5;  // staggered twinkles
      p.life = 0.5 + random_unit() * 0.7;
      p.size = 3.0 + random_unit() * 5.0;
      p.color = effect_hue(0.12 + random_unit() * 0.08, 0.35 + random_unit() * 0.4, 1.0);
      effect.particles.push_back(p);
    }
  } else if (name == QStringLiteral("confetti")) {
    effect.kind = Kind::Confetti;
    const int count = std::clamp(static_cast<int>(option(spec, "count", 140)), 1, 400);
    for (int i = 0; i < count; ++i) {
      Particle p;
      p.position = QPointF(random_unit() * width(), -20.0 - random_unit() * height() * 0.35);
      p.previous = p.position;
      p.velocity = QPointF((random_unit() * 2.0 - 1.0) * 60.0, 60.0 + random_unit() * 90.0);
      p.life = effect.duration;
      p.size = 5.0 + random_unit() * 5.0;
      p.angle = random_unit() * 360.0;
      p.spin = (random_unit() * 2.0 - 1.0) * 540.0;
      p.color = effect_hue(random_unit(), 0.75, 0.95);
      effect.particles.push_back(p);
    }
  } else if (name == QStringLiteral("sprite")) {
    effect.kind = Kind::Sprite;
    if (assets == nullptr) {
      return false;
    }
    const auto image = theme_package_image(*assets, spec.value(QStringLiteral("image")).toString());
    if (image.isNull()) {
      return false;
    }
    effect.sheet = QPixmap::fromImage(image);
    effect.frames = std::clamp(static_cast<int>(option(spec, "frames", 1)), 1, 256);
    effect.frame_width = std::clamp(static_cast<int>(option(spec, "frameWidth", image.width() / effect.frames)), 1,
                                    image.width());
    effect.frame_height = std::clamp(static_cast<int>(option(spec, "frameHeight", image.height())), 1, image.height());
    effect.fps = std::clamp(option(spec, "fps", 10.0), 1.0, 60.0);
    effect.loop = spec.value(QStringLiteral("loop"), true).toBool();
    effect.scale = std::clamp(option(spec, "scale", 1.0), 0.1, 8.0);
    effect.from = center;
    effect.to = QPointF(std::clamp(option(spec, "toX", center.x() / width()), -0.5, 1.5) * width(),
                        std::clamp(option(spec, "toY", center.y() / height()), -0.5, 1.5) * height());
    effect.flip = spec.value(QStringLiteral("flip")).toBool();
    if (!spec.contains(QStringLiteral("duration")) && !effect.loop) {
      effect.duration = std::min(effect.frames / effect.fps, kMaxDurationMs / 1000.0);
    }
  } else {
    return false;
  }
  if (particle_total() + static_cast<int>(effect.particles.size()) > kMaxParticles) {
    return false;
  }
  effects_.push_back(std::move(effect));
  follow_host();
  if (isHidden()) {
    show();
  }
  raise();
  if (!timer_.isActive()) {
    clock_.restart();
    timer_.start();
  }
  return true;
}

void ThemeEffectsOverlay::clear() {
  effects_.clear();
  timer_.stop();
  if (!last_bounds_.isEmpty()) {
    update(last_bounds_.toAlignedRect());
  }
  last_bounds_ = QRectF();
  hide();
}

void ThemeEffectsOverlay::spawn_burst(Effect& effect, const Burst& burst) {
  const int count = 70;
  for (int i = 0; i < count && particle_total() < kMaxParticles; ++i) {
    Particle p;
    const double angle = (i + random_unit() * 0.6) * 2.0 * kPi / count;
    const double speed = 90.0 + random_unit() * 150.0;
    p.position = burst.center;
    p.previous = burst.center;
    p.velocity = QPointF(std::cos(angle) * speed, std::sin(angle) * speed);
    p.life = 0.9 + random_unit() * 0.7;
    p.size = 2.0 + random_unit() * 1.5;
    p.color = random_unit() < 0.15 ? QColor(255, 250, 230) : burst.color;
    effect.particles.push_back(p);
  }
}

void ThemeEffectsOverlay::step(double seconds) {
  const bool on_start_screen = !start_screen_probe_ || start_screen_probe_();
  for (auto& effect : effects_) {
    effect.age += seconds;
    for (auto it = effect.bursts.begin(); it != effect.bursts.end();) {
      if (it->at <= effect.age) {
        spawn_burst(effect, *it);
        it = effect.bursts.erase(it);
      } else {
        ++it;
      }
    }
    for (auto& p : effect.particles) {
      p.previous = p.position;
      p.age += seconds;
      if (p.age < 0.0) {
        continue;
      }
      switch (effect.kind) {
        case Kind::Fireworks:
          p.velocity *= std::pow(0.35, seconds);  // air drag
          p.velocity.ry() += kGravity * 0.45 * seconds;
          break;
        case Kind::Confetti:
          p.velocity.rx() += std::sin(p.age * 3.0 + p.size) * 40.0 * seconds;
          p.velocity.ry() = std::min(p.velocity.y() + kGravity * 0.25 * seconds, 220.0);
          p.angle += p.spin * seconds;
          break;
        case Kind::Sparkles:
        case Kind::Sprite:
          break;
      }
      p.position += p.velocity * seconds;
    }
    const double floor_y = height() + 30.0;
    std::erase_if(effect.particles, [&effect, floor_y](const Particle& p) {
      return p.age > p.life || (effect.kind == Kind::Confetti && p.position.y() > floor_y);
    });
  }
  std::erase_if(effects_, [on_start_screen](const Effect& effect) {
    const bool done = effect.age >= effect.duration && effect.bursts.empty() &&
                      (effect.kind == Kind::Sprite || effect.particles.empty() || effect.age >= effect.duration + 1.5);
    return done || (effect.start_screen_only && !on_start_screen);
  });
}

void ThemeEffectsOverlay::tick() {
  const double seconds = std::clamp(clock_.restart() / 1000.0, 0.0, 0.1);
  step(seconds);
  QRectF bounds;
  for (const auto& effect : effects_) {
    bounds = bounds.united(effect_bounds(effect));
  }
  // Repaint where things were and where they are now, nothing else.
  const auto dirty = bounds.united(last_bounds_).adjusted(-4, -4, 4, 4).toAlignedRect();
  last_bounds_ = bounds;
  if (!dirty.isEmpty()) {
    update(dirty);
  }
  if (effects_.empty()) {
    timer_.stop();
    hide();
  }
}

void ThemeEffectsOverlay::advance_for_testing(int milliseconds) {
  for (int elapsed = 0; elapsed < milliseconds; elapsed += kFrameMs) {
    step(kFrameMs / 1000.0);
  }
  tick();
}

QRectF ThemeEffectsOverlay::sprite_rect(const Effect& effect) const {
  const double t = effect.duration > 0.0 ? std::clamp(effect.age / effect.duration, 0.0, 1.0) : 1.0;
  const QPointF position = effect.from + (effect.to - effect.from) * t;
  const QSizeF size(effect.frame_width * effect.scale, effect.frame_height * effect.scale);
  return QRectF(position - QPointF(size.width() / 2.0, size.height()), size);  // anchored at the feet
}

QRectF ThemeEffectsOverlay::effect_bounds(const Effect& effect) const {
  if (effect.kind == Kind::Sprite) {
    return sprite_rect(effect);
  }
  QRectF bounds;
  for (const auto& p : effect.particles) {
    if (p.age < 0.0) {
      continue;
    }
    const double r = p.size * 1.5 + 2.0;
    const QRectF here(p.position - QPointF(r, r), QSizeF(2 * r, 2 * r));
    const QRectF before(p.previous - QPointF(r, r), QSizeF(2 * r, 2 * r));
    bounds = bounds.united(here).united(before);
  }
  return bounds;
}

void ThemeEffectsOverlay::paintEvent(QPaintEvent* event) {
  ++paint_count_;
  QPainter painter(this);
  painter.setClipRegion(event->region());
  painter.setRenderHint(QPainter::Antialiasing);
  for (const auto& effect : effects_) {
    if (effect.kind == Kind::Sprite) {
      const auto frame_index = effect.loop ? static_cast<int>(effect.age * effect.fps) % effect.frames
                                           : std::min(static_cast<int>(effect.age * effect.fps), effect.frames - 1);
      const int columns = std::max(1, effect.sheet.width() / effect.frame_width);
      const QRect source((frame_index % columns) * effect.frame_width, (frame_index / columns) * effect.frame_height,
                         effect.frame_width, effect.frame_height);
      const auto target = sprite_rect(effect);
      painter.save();
      if (effect.flip) {
        painter.translate(target.center());
        painter.scale(-1.0, 1.0);
        painter.translate(-target.center());
      }
      painter.setRenderHint(QPainter::SmoothPixmapTransform, effect.scale < 1.0);
      painter.drawPixmap(target, effect.sheet, QRectF(source));
      painter.restore();
      continue;
    }
    for (const auto& p : effect.particles) {
      if (p.age < 0.0) {
        continue;
      }
      const double fade = std::clamp(1.0 - p.age / p.life, 0.0, 1.0);
      auto color = p.color;
      switch (effect.kind) {
        case Kind::Fireworks: {
          color.setAlphaF(static_cast<float>(fade));
          painter.setPen(QPen(color, p.size * 0.8, Qt::SolidLine, Qt::RoundCap));
          painter.drawLine(p.previous, p.position);
          painter.setPen(Qt::NoPen);
          painter.setBrush(color);
          painter.drawEllipse(p.position, p.size * 0.6, p.size * 0.6);
          break;
        }
        case Kind::Sparkles: {
          const double pulse = std::sin(std::clamp(p.age / p.life, 0.0, 1.0) * kPi);
          color.setAlphaF(static_cast<float>(pulse));
          const double r = p.size * pulse;
          painter.setPen(QPen(color, 1.4));
          painter.drawLine(p.position - QPointF(r, 0), p.position + QPointF(r, 0));
          painter.drawLine(p.position - QPointF(0, r), p.position + QPointF(0, r));
          painter.setPen(Qt::NoPen);
          painter.setBrush(color);
          painter.drawEllipse(p.position, r * 0.3, r * 0.3);
          break;
        }
        case Kind::Confetti: {
          const double remaining = effect.duration - effect.age;
          color.setAlphaF(static_cast<float>(std::clamp(remaining / 0.5, 0.0, 1.0)));
          painter.save();
          painter.translate(p.position);
          painter.rotate(p.angle);
          painter.setPen(Qt::NoPen);
          painter.setBrush(color);
          const double squash = std::abs(std::cos(p.age * 6.0 + p.size));
          painter.drawRect(QRectF(-p.size / 2.0, -p.size * 0.3 * squash, p.size, p.size * 0.6 * squash + 0.5));
          painter.restore();
          break;
        }
        case Kind::Sprite:
          break;
      }
    }
  }
}

void register_theme_effects_overlay(ThemeEffectsOverlay* overlay) { overlay_slot() = overlay; }

ThemeEffectsOverlay* active_theme_effects_overlay() { return overlay_slot().data(); }

bool play_theme_effect(const QVariantMap& spec) {
  auto* overlay = active_theme_effects_overlay();
  if (overlay == nullptr || spec.isEmpty() || !theme_effects_enabled()) {
    return false;
  }
  return overlay->play(spec, active_theme_assets());
}

}  // namespace patchy::ui
