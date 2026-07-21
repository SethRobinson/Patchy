#pragma once

// Shared smart-filter test helpers moved verbatim from the retired
// tests/ui/smart_filter_tests.cpp monolith (used by more than one
// smart_filter_tests part TU). Moved, never copied.

#include <cstddef>

#include <QString>

#include "core/document.hpp"
#include "ui/main_window.hpp"

namespace patchy::test::ui {

constexpr auto kSmartFilterInstanceAName = "Instance A Gaussian 1.5";

constexpr auto kSmartFilterInstanceBName = "Instance B Gaussian 4.5 transformed masked";

std::size_t smart_filter_effect_record_count(const patchy::Document& document);

patchy::LayerId select_named_layer(patchy::ui::MainWindow& window, const QString& name);

void open_smart_filter_instances_fixture(patchy::ui::MainWindow& window);

}  // namespace patchy::test::ui
