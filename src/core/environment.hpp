#pragma once

#include <optional>
#include <string>

namespace patchy {

[[nodiscard]] std::optional<std::string> environment_variable(const char* name);
[[nodiscard]] bool environment_variable_is_set(const char* name) noexcept;

}  // namespace patchy
