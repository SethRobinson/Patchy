#include "ocr/ocr_engine.hpp"

#include "ocr/ocr_fake_engine.hpp"
#include "ocr/ocr_platform.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

namespace patchy::ocr {

namespace {

struct Registry {
  std::mutex mutex;
  std::vector<EngineProvider> providers;
  std::function<std::vector<std::shared_ptr<Engine>>()> test_factory;
  // The operating system's engines are listed once: an engine object is cheap, and its
  // languages are asked fresh on every call anyway.
  bool platform_listed{false};
  std::vector<std::shared_ptr<Engine>> platform;
  // The PATCHY_FAKE_OCR engine, made once so a run sees one object.
  bool environment_read{false};
  std::shared_ptr<Engine> environment_fake;
};

Registry& registry() {
  static Registry instance;
  return instance;
}

}  // namespace

int channel_count(PixelLayout layout) noexcept {
  switch (layout) {
    case PixelLayout::Gray8:
      return 1;
    case PixelLayout::Rgb8:
      return 3;
    case PixelLayout::Rgba8:
    case PixelLayout::Bgra8:
      return 4;
  }
  return 4;
}

Quad quad_from_box(const Box& box) noexcept {
  return {Point{box.x, box.y}, Point{box.x + box.width, box.y}, Point{box.x + box.width, box.y + box.height},
          Point{box.x, box.y + box.height}};
}

Box box_from_quad(const Quad& quad) noexcept {
  double left = quad[0].x;
  double right = quad[0].x;
  double top = quad[0].y;
  double bottom = quad[0].y;
  for (const auto& point : quad) {
    left = std::min(left, point.x);
    right = std::max(right, point.x);
    top = std::min(top, point.y);
    bottom = std::max(bottom, point.y);
  }
  return Box{left, top, right - left, bottom - top};
}

std::string Result::text() const {
  std::string joined;
  for (const auto& line : lines) {
    if (!joined.empty()) {
      joined += '\n';
    }
    joined += line.text;
  }
  return joined;
}

RecognitionCancelled::RecognitionCancelled() : std::runtime_error("text recognition was cancelled") {}

std::vector<std::shared_ptr<Engine>> engines() {
  auto& state = registry();
  std::function<std::vector<std::shared_ptr<Engine>>()> test_factory;
  std::vector<EngineProvider> providers;
  std::vector<std::shared_ptr<Engine>> listed;
  {
    const std::lock_guard lock(state.mutex);
    if (state.test_factory) {
      test_factory = state.test_factory;
    } else {
      if (!state.environment_read) {
        state.environment_read = true;
        if (auto config = fake_engine_config_from_environment()) {
          state.environment_fake = make_fake_engine(std::move(*config));
        }
      }
      if (state.environment_fake != nullptr) {
        return {state.environment_fake};
      }
      if (!state.platform_listed) {
        state.platform_listed = true;
        state.platform = detail::platform_engines();
      }
      listed = state.platform;
      providers = state.providers;
    }
  }
  // Factories and providers run outside the lock: they may list engines themselves.
  if (test_factory) {
    return test_factory();
  }
  for (const auto& provider : providers) {
    auto provided = provider();
    listed.insert(listed.end(), std::make_move_iterator(provided.begin()), std::make_move_iterator(provided.end()));
  }
  return listed;
}

std::shared_ptr<Engine> find_engine(std::string_view id) {
  for (auto& engine : engines()) {
    if (engine != nullptr && engine->id() == id) {
      return engine;
    }
  }
  return nullptr;
}

void add_engine_provider(EngineProvider provider) {
  if (!provider) {
    return;
  }
  auto& state = registry();
  const std::lock_guard lock(state.mutex);
  state.providers.push_back(std::move(provider));
}

void set_engines_for_testing(std::function<std::vector<std::shared_ptr<Engine>>()> factory) {
  auto& state = registry();
  const std::lock_guard lock(state.mutex);
  state.test_factory = std::move(factory);
}

}  // namespace patchy::ocr
