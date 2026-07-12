#pragma once

#include "core/layer.hpp"

#include <QString>

#include <optional>

class QWidget;

namespace patchy::ui {
class GradientLibrary;

[[nodiscard]] QString request_gradient_manager(
    QWidget *parent, GradientLibrary &library,
    const QString &initial_storage_id = {},
    std::optional<GradientDefinition> current = std::nullopt);
} // namespace patchy::ui
