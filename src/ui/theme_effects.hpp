#pragma once

// Theme effects (docs/theme-files.md, "Events and effects"): short animations a theme
// names for an event or plays from its scripts. They draw in a transparent overlay
// above the document area that takes no input and never touches the document. The
// overlay is hidden and its timer stopped whenever nothing plays, so an idle theme
// costs nothing; while one plays, each frame repaints only the area its particles and
// sprites cover, and the particle count is capped.

#include <QColor>
#include <QElapsedTimer>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <vector>

namespace patchy::ui {

struct ThemeAssets;

// "preferences/playThemeEffects" (a persisted key), on by default.
[[nodiscard]] bool theme_effects_enabled();
void set_theme_effects_enabled(bool enabled);

// The effect names a theme may use: "fireworks", "sparkles", "confetti", "sprite".
[[nodiscard]] QStringList theme_effect_names();

class ThemeEffectsOverlay final : public QWidget {
 public:
  // Covers `host` (the document tab area) and follows its size.
  explicit ThemeEffectsOverlay(QWidget* host);
  ~ThemeEffectsOverlay() override;

  // Starts one effect from a spec ({"name": ..., options}); a sprite reads its image
  // from `assets`. False when refused: an unknown name, a missing sprite image, a
  // start-screen effect while no start screen shows, or the concurrency cap.
  bool play(const QVariantMap& spec, const ThemeAssets* assets);
  void clear();

  [[nodiscard]] int active_effect_count() const { return static_cast<int>(effects_.size()); }
  [[nodiscard]] bool animating() const { return timer_.isActive(); }
  // Paint events received; the test seam for "nothing repaints while idle".
  [[nodiscard]] int paint_count() const { return paint_count_; }
  // Advances the animation by `milliseconds` at once (tests).
  void advance_for_testing(int milliseconds);

  // Answers whether the empty start screen is showing; "where": "start-screen"
  // effects play only while it does and end when it hides.
  void set_start_screen_probe(std::function<bool()> probe) { start_screen_probe_ = std::move(probe); }

  static constexpr int kMaxEffects = 8;
  static constexpr int kMaxParticles = 1500;
  static constexpr int kMaxDurationMs = 10000;

 protected:
  void paintEvent(QPaintEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  struct Particle {
    QPointF position;
    QPointF velocity;
    QPointF previous;
    double age = 0.0;    // seconds
    double life = 1.0;   // seconds
    double size = 2.0;
    double angle = 0.0;
    double spin = 0.0;
    QColor color;
  };
  enum class Kind { Fireworks, Sparkles, Confetti, Sprite };
  struct Burst {
    double at = 0.0;  // seconds after the start
    QPointF center;
    QColor color;
  };
  struct Effect {
    Kind kind = Kind::Fireworks;
    double age = 0.0;
    double duration = 2.0;
    std::vector<Particle> particles;
    std::vector<Burst> bursts;  // fireworks still to explode
    bool start_screen_only = false;
    // Sprite
    QPixmap sheet;
    int frame_width = 0;
    int frame_height = 0;
    int frames = 1;
    double fps = 10.0;
    bool loop = true;
    double scale = 1.0;
    bool flip = false;
    QPointF from;
    QPointF to;
  };

  void step(double seconds);
  void tick();
  void spawn_burst(Effect& effect, const Burst& burst);
  [[nodiscard]] QRectF effect_bounds(const Effect& effect) const;
  [[nodiscard]] QRectF sprite_rect(const Effect& effect) const;
  [[nodiscard]] int particle_total() const;
  [[nodiscard]] double random_unit();
  void follow_host();

  QWidget* host_ = nullptr;
  std::vector<Effect> effects_;
  QTimer timer_;
  QElapsedTimer clock_;
  QRectF last_bounds_;
  std::uint64_t random_state_ = 0x5eed7e3a11ce5ULL;
  int paint_count_ = 0;
  std::function<bool()> start_screen_probe_;
};

// The overlay of the most recently activated main window; null without one.
void register_theme_effects_overlay(ThemeEffectsOverlay* overlay);
[[nodiscard]] ThemeEffectsOverlay* active_theme_effects_overlay();
// Plays `spec` on the active overlay with the active theme's assets, when theme
// effects are on. False when nothing played.
bool play_theme_effect(const QVariantMap& spec);

}  // namespace patchy::ui
