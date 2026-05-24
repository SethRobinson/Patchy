#pragma once

#include "core/document.hpp"

#include <functional>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace photoslop {

using FormatReadFn = std::function<Document(std::span<const std::uint8_t>)>;
using FormatWriteFn = std::function<std::vector<std::uint8_t>(const Document&)>;

struct FormatHandler {
  std::string identifier;
  std::string display_name;
  std::vector<std::string> extensions;
  FormatReadFn read;
  FormatWriteFn write;
};

class FormatRegistry {
public:
  void register_handler(FormatHandler handler);
  [[nodiscard]] const FormatHandler* find_by_extension(std::string_view extension) const noexcept;
  [[nodiscard]] const std::vector<FormatHandler>& handlers() const noexcept;

private:
  std::vector<FormatHandler> handlers_;
};

void register_builtin_formats(FormatRegistry& registry);

}  // namespace photoslop
