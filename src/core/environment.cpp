#include "core/environment.hpp"

#include <cstdlib>

namespace patchy {

std::optional<std::string> environment_variable(const char* name) {
  if (name == nullptr) {
    return std::nullopt;
  }
#if defined(_WIN32)
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
    std::free(value);
    return std::nullopt;
  }
  std::string result(value);
  std::free(value);
  return result;
#else
  const char* value = std::getenv(name);
  return value != nullptr ? std::optional<std::string>(value) : std::nullopt;
#endif
}

bool environment_variable_is_set(const char* name) noexcept {
  if (name == nullptr) {
    return false;
  }
#if defined(_WIN32)
  char* value = nullptr;
  std::size_t size = 0;
  const auto error = _dupenv_s(&value, &size, name);
  const bool found = error == 0 && value != nullptr;
  std::free(value);
  return found;
#else
  return std::getenv(name) != nullptr;
#endif
}

}  // namespace patchy
