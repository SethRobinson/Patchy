#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QStringList>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include "ui_test_access.hpp"
// Unit-entry spin boxes: "200 px", "50%", "2 in" typed into a numeric field convert
// into the field's native unit (Photoshop's numeric-field behavior). The parser and
// converter are exercised directly; the widget tests drive the line edit + Enter.
#include "ui/measurement_units.hpp"
#include "ui/localization.hpp"
#include "ui/unit_spin_box.hpp"
#include "test_harness.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include <QApplication>
#include <QContextMenuEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QString>
#include <QTimer>
#include <cmath>
#include <optional>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::SpinUnit;
using patchy::ui::UnitConversionContext;
using patchy::ui::UnitEntry;
using patchy::ui::UnitIntSpinBox;
using patchy::ui::UnitSpinBox;

bool close_to(double a, double b, double tolerance = 1e-6) {
  return std::abs(a - b) <= tolerance;
}

// validate() is protected on QAbstractSpinBox; expose it for the state probes.
struct ProbeSpin final : UnitSpinBox {
  using UnitSpinBox::UnitSpinBox;
  using UnitSpinBox::validate;
};

void commit_text(QAbstractSpinBox& spin, const QString& text) {
  // lineEdit() is protected; the child lookup reaches the same editor.
  auto* editor = spin.findChild<QLineEdit*>();
  CHECK(editor != nullptr);
  editor->setText(text);
  send_key(spin, Qt::Key_Return);
  QApplication::processEvents();
}

void unit_spin_box_parses_unit_tokens() {
  const QLocale c = QLocale::c();
  struct Case {
    const char* text;
    double value;
    std::optional<SpinUnit> unit;
  };
  const std::vector<Case> cases = {
      {"200", 200.0, std::nullopt},
      {"200px", 200.0, SpinUnit::Pixels},
      {"200 PX", 200.0, SpinUnit::Pixels},
      {"200 pixels", 200.0, SpinUnit::Pixels},
      {"2in", 2.0, SpinUnit::Inches},
      {"2 inches", 2.0, SpinUnit::Inches},
      {"2\"", 2.0, SpinUnit::Inches},
      {"3 cm", 3.0, SpinUnit::Centimeters},
      {"10mm", 10.0, SpinUnit::Millimeters},
      {"36pt", 36.0, SpinUnit::Points},
      {"36 points", 36.0, SpinUnit::Points},
      {"50%", 50.0, SpinUnit::Percent},
      {"50 percent", 50.0, SpinUnit::Percent},
      {"45deg", 45.0, SpinUnit::Degrees},
      {"45°", 45.0, SpinUnit::Degrees},
      {"-12.5 px", -12.5, SpinUnit::Pixels},
      {"+0.5in", 0.5, SpinUnit::Inches},
  };
  for (const auto& test : cases) {
    const auto parsed = patchy::ui::parse_unit_entry(QString::fromUtf8(test.text), c);
    CHECK(parsed.state == QValidator::Acceptable);
    CHECK(close_to(parsed.entry.value, test.value));
    CHECK(parsed.entry.unit == test.unit);
    CHECK(parsed.has_unit_token == test.unit.has_value());
  }

  // The spin's own prefix and suffix are stripped, so the stock display text is a
  // native number.
  const auto native = patchy::ui::parse_unit_entry(QStringLiteral("100.00%"), c, QString(), QStringLiteral("%"));
  CHECK(native.state == QValidator::Acceptable);
  CHECK(!native.has_unit_token);
  CHECK(close_to(native.entry.value, 100.0));

  // Partial tokens and bare signs are still being typed.
  for (const char* partial : {"", "-", ".", "2 i", "2 p", "45 de", "12 c"}) {
    const auto parsed = patchy::ui::parse_unit_entry(QString::fromUtf8(partial), c);
    CHECK(parsed.state == QValidator::Intermediate);
  }
  // Unknown tokens and a unit with no number are invalid.
  for (const char* invalid : {"2 furlongs", "px", "12 xp", "1..2 px"}) {
    const auto parsed = patchy::ui::parse_unit_entry(QString::fromUtf8(invalid), c);
    CHECK(parsed.state == QValidator::Invalid);
  }

  // Locale decimal separators work, and "1.5" still parses under a comma locale
  // because the C locale is the fallback.
  const QLocale german(QLocale::German, QLocale::Germany);
  const auto comma = patchy::ui::parse_unit_entry(QStringLiteral("1,5 cm"), german);
  CHECK(comma.state == QValidator::Acceptable);
  CHECK(close_to(comma.entry.value, 1.5));
  CHECK(comma.entry.unit == SpinUnit::Centimeters);
  const auto point = patchy::ui::parse_unit_entry(QStringLiteral("1.5 cm"), german);
  CHECK(point.state == QValidator::Acceptable);
  CHECK(close_to(point.entry.value, 1.5));
}

void unit_spin_box_converts_between_units() {
  const UnitConversionContext at_300{300.0, 0.0};
  const auto convert = [](double value, SpinUnit unit, SpinUnit native, const UnitConversionContext& context) {
    return patchy::ui::convert_unit_entry(UnitEntry{value, unit}, native, context);
  };
  CHECK(close_to(convert(2.0, SpinUnit::Inches, SpinUnit::Pixels, at_300).value_or(-1.0), 600.0));
  CHECK(close_to(convert(25.4, SpinUnit::Millimeters, SpinUnit::Pixels, at_300).value_or(-1.0), 300.0));
  CHECK(close_to(convert(2.54, SpinUnit::Centimeters, SpinUnit::Pixels, at_300).value_or(-1.0), 300.0));
  CHECK(close_to(convert(72.0, SpinUnit::Points, SpinUnit::Pixels, at_300).value_or(-1.0), 300.0));
  // Native unit and no unit pass through untouched.
  CHECK(close_to(convert(17.0, SpinUnit::Pixels, SpinUnit::Pixels, at_300).value_or(-1.0), 17.0));
  CHECK(close_to(patchy::ui::convert_unit_entry(UnitEntry{17.0, std::nullopt}, SpinUnit::Degrees, at_300).value_or(-1.0),
             17.0));
  // Percent needs a basis: 50% of 800 px is 400 px, and 400 px of 800 is 50%.
  const UnitConversionContext basis_800{300.0, 800.0};
  CHECK(close_to(convert(50.0, SpinUnit::Percent, SpinUnit::Pixels, basis_800).value_or(-1.0), 400.0));
  CHECK(close_to(convert(400.0, SpinUnit::Pixels, SpinUnit::Percent, basis_800).value_or(-1.0), 50.0));
  CHECK(close_to(convert(2.0, SpinUnit::Inches, SpinUnit::Percent, basis_800).value_or(-1.0), 75.0));
  CHECK(!convert(50.0, SpinUnit::Percent, SpinUnit::Pixels, at_300).has_value());
  CHECK(!convert(400.0, SpinUnit::Pixels, SpinUnit::Percent, at_300).has_value());
  // Points-native fields (text size) take pixels through the PPI.
  const UnitConversionContext at_144{144.0, 0.0};
  CHECK(close_to(convert(24.0, SpinUnit::Pixels, SpinUnit::Points, at_144).value_or(-1.0), 12.0));
  // Angles never convert to or from lengths.
  CHECK(!convert(2.0, SpinUnit::Inches, SpinUnit::Degrees, at_300).has_value());
  CHECK(!convert(45.0, SpinUnit::Degrees, SpinUnit::Pixels, at_300).has_value());
  // A bad PPI falls back to the document default instead of producing garbage.
  const UnitConversionContext bad_ppi{0.0, 0.0};
  CHECK(close_to(convert(1.0, SpinUnit::Inches, SpinUnit::Pixels, bad_ppi).value_or(-1.0), 300.0));
}

void unit_spin_box_pixel_field_accepts_percent_and_physical() {
  UnitSpinBox spin(SpinUnit::Pixels);
  spin.setRange(-30000.0, 30000.0);
  spin.setDecimals(2);
  spin.setKeyboardTracking(false);
  spin.set_context_provider([] { return UnitConversionContext{300.0, 1024.0}; });
  spin.setValue(10.0);
  spin.show();
  QApplication::processEvents();

  commit_text(spin, QStringLiteral("2 in"));
  CHECK(close_to(spin.value(), 600.0));
  CHECK(spin.text() == QStringLiteral("600.00") + patchy::ui::pixel_suffix());
  commit_text(spin, QStringLiteral("50%"));
  CHECK(close_to(spin.value(), 512.0));
  commit_text(spin, QStringLiteral("10 mm"));
  CHECK(close_to(spin.value(), 118.11, 0.01));
  commit_text(spin, QStringLiteral("36 pt"));
  CHECK(close_to(spin.value(), 150.0));
  // The display text itself (native suffix included) round-trips.
  commit_text(spin, spin.text());
  CHECK(close_to(spin.value(), 150.0));
  // A converted value beyond the range clamps instead of being refused.
  commit_text(spin, QStringLiteral("200 in"));
  CHECK(close_to(spin.value(), 30000.0));
}

void unit_spin_box_percent_field_accepts_pixels() {
  UnitSpinBox spin(SpinUnit::Percent);
  spin.setRange(-10000.0, 10000.0);
  spin.setDecimals(2);
  spin.setKeyboardTracking(false);
  spin.set_context_provider([] { return UnitConversionContext{300.0, 200.0}; });
  spin.setValue(100.0);
  spin.show();
  QApplication::processEvents();

  commit_text(spin, QStringLiteral("300 px"));
  CHECK(close_to(spin.value(), 150.0));
  commit_text(spin, QStringLiteral("1 in"));
  CHECK(close_to(spin.value(), 150.0));
  commit_text(spin, QStringLiteral("-100 px"));
  CHECK(close_to(spin.value(), -50.0));
  commit_text(spin, QStringLiteral("25%"));
  CHECK(close_to(spin.value(), 25.0));
  CHECK(spin.text() == QStringLiteral("25.00%"));
}

void unit_spin_box_rejects_incompatible_units() {
  ProbeSpin angle(SpinUnit::Degrees);
  angle.setRange(-3600.0, 3600.0);
  angle.setDecimals(2);
  angle.setKeyboardTracking(false);
  angle.setValue(15.0);
  angle.show();
  QApplication::processEvents();
  {
    QString text = QStringLiteral("2 in");
    int pos = static_cast<int>(text.size());
    CHECK(angle.validate(text, pos) == QValidator::Invalid);
  }
  commit_text(angle, QStringLiteral("2 in"));
  CHECK(close_to(angle.value(), 15.0));
  commit_text(angle, QStringLiteral("90 deg"));
  CHECK(close_to(angle.value(), 90.0));
  commit_text(angle, QStringLiteral("45\u00b0"));
  CHECK(close_to(angle.value(), 45.0));
  CHECK(angle.text() == QStringLiteral("45.00") + patchy::ui::degree_suffix());

  // A percent typed into a field without a percent basis is refused too.
  UnitSpinBox pixels(SpinUnit::Pixels);
  pixels.setRange(0.0, 1000.0);
  pixels.setValue(40.0);
  pixels.show();
  QApplication::processEvents();
  commit_text(pixels, QStringLiteral("50%"));
  CHECK(close_to(pixels.value(), 40.0));
}

void unit_spin_box_plain_number_uses_native_unit() {
  ProbeSpin spin(SpinUnit::Pixels);
  spin.setRange(0.0, 100.0);
  spin.setDecimals(0);
  spin.setValue(10.0);
  spin.show();
  QApplication::processEvents();
  commit_text(spin, QStringLiteral("42"));
  CHECK(close_to(spin.value(), 42.0));
  // Stock typing rules survive: a plain number past the maximum is refused
  // keystroke by keystroke, exactly as a QDoubleSpinBox does.
  {
    QString text = QStringLiteral("500");
    int pos = 3;
    CHECK(spin.validate(text, pos) == QValidator::Invalid);
  }
  {
    QString text = QStringLiteral("5 in");
    int pos = 4;
    CHECK(spin.validate(text, pos) == QValidator::Acceptable);
  }
}

// Photoshop's W/H/X/Y fields: a typed unit becomes the field's display unit, the
// stored value stays native, plain numbers are then read in the shown unit, and
// a field that is not switchable keeps its native display.
void unit_spin_box_display_unit_follows_typed_unit() {
  UnitSpinBox spin(SpinUnit::Percent);
  spin.setRange(-10000.0, 10000.0);
  spin.setDecimals(2);
  spin.setKeyboardTracking(false);
  spin.set_context_provider([] { return UnitConversionContext{300.0, 200.0}; });
  spin.set_display_unit_switchable(true);
  spin.setValue(100.0);
  spin.show();
  QApplication::processEvents();
  CHECK(spin.display_unit() == SpinUnit::Percent);

  commit_text(spin, QStringLiteral("300 px"));
  CHECK(close_to(spin.value(), 150.0));
  CHECK(spin.display_unit() == SpinUnit::Pixels);
  CHECK(spin.suffix() == patchy::ui::pixel_suffix());
  CHECK(spin.text() == QStringLiteral("300.00") + patchy::ui::pixel_suffix());
  // A plain number now means pixels.
  commit_text(spin, QStringLiteral("150"));
  CHECK(close_to(spin.value(), 75.0));
  CHECK(spin.text() == QStringLiteral("150.00") + patchy::ui::pixel_suffix());
  // setValue stays native and re-renders in the shown unit.
  spin.setValue(50.0);
  CHECK(spin.text() == QStringLiteral("100.00") + patchy::ui::pixel_suffix());
  commit_text(spin, QStringLiteral("2 in"));
  CHECK(close_to(spin.value(), 300.0));
  CHECK(spin.display_unit() == SpinUnit::Inches);
  CHECK(spin.text() == QStringLiteral("2.000") + patchy::ui::inch_suffix());  // inches widen to 3 decimals
  commit_text(spin, QStringLiteral("25%"));
  CHECK(close_to(spin.value(), 25.0));
  CHECK(spin.display_unit() == SpinUnit::Percent);
  CHECK(spin.text() == QStringLiteral("25.00%"));
  spin.set_display_unit(SpinUnit::Millimeters);
  CHECK(spin.text() == QStringLiteral("4.23") + QStringLiteral(" ") +
                           patchy::ui::measurement_unit_suffix(patchy::ui::MeasurementUnit::Millimeters));

  UnitSpinBox fixed(SpinUnit::Percent);
  fixed.setRange(-10000.0, 10000.0);
  fixed.setDecimals(2);
  fixed.set_context_provider([] { return UnitConversionContext{300.0, 200.0}; });
  fixed.setValue(100.0);
  fixed.show();
  QApplication::processEvents();
  commit_text(fixed, QStringLiteral("300 px"));
  CHECK(close_to(fixed.value(), 150.0));
  CHECK(fixed.display_unit() == SpinUnit::Percent);
  CHECK(fixed.text() == QStringLiteral("150.00%"));
}

void unit_spin_box_keeps_translated_suffix() {
  for (const auto unit : {SpinUnit::Pixels, SpinUnit::Inches, SpinUnit::Centimeters, SpinUnit::Millimeters,
                          SpinUnit::Points, SpinUnit::Percent, SpinUnit::Degrees}) {
    UnitSpinBox spin(unit);
    CHECK(spin.suffix() == patchy::ui::spin_unit_suffix(unit));
    CHECK(!spin.suffix().isEmpty());
  }
  UnitSpinBox pixels(SpinUnit::Pixels);
  CHECK(pixels.suffix() == patchy::ui::pixel_suffix());
  UnitSpinBox percent(SpinUnit::Percent);
  CHECK(percent.suffix() == patchy::ui::percent_suffix());
  UnitSpinBox degrees(SpinUnit::Degrees);
  CHECK(degrees.suffix() == patchy::ui::degree_suffix());
  UnitIntSpinBox whole(SpinUnit::Pixels);
  CHECK(whole.suffix() == patchy::ui::pixel_suffix());
}

void unit_spin_box_language_switch_refreshes_suffix_without_changing_values() {
  auto& manager = patchy::ui::LocalizationManager::instance();
  const auto original = manager.current_language();
  const auto restore = qScopeGuard([&] {
    manager.set_language(original, false);
    QApplication::processEvents();
  });
  CHECK(manager.set_language(QStringLiteral("en"), false));
  QWidget parent;
  UnitSpinBox fractional(SpinUnit::Pixels, &parent);
  UnitSpinBox physical(SpinUnit::Pixels, &parent);
  UnitIntSpinBox whole(SpinUnit::Pixels, &parent);
  fractional.setValue(12.5);
  physical.setValue(72.0);
  physical.set_display_unit(SpinUnit::Millimeters);
  whole.setValue(12);
  int value_changes = 0;
  QObject::connect(&fractional, &QDoubleSpinBox::valueChanged, [&] { ++value_changes; });
  QObject::connect(&physical, &QDoubleSpinBox::valueChanged, [&] { ++value_changes; });
  QObject::connect(&whole, &QSpinBox::valueChanged, [&] { ++value_changes; });
  parent.show();
  QApplication::processEvents();
  for (const auto* language : {"ru", "pl", "pt_BR", "ko", "en"}) {
    CHECK(manager.set_language(QString::fromLatin1(language), false));
    QApplication::processEvents();
    CHECK(fractional.suffix() == patchy::ui::pixel_suffix());
    CHECK(whole.suffix() == fractional.suffix());
    CHECK(physical.suffix() == patchy::ui::spin_unit_suffix(SpinUnit::Millimeters));
    CHECK(fractional.value() == 12.5);
    CHECK(physical.value() == 72.0);
    CHECK(whole.value() == 12);
    CHECK(value_changes == 0);
  }
}

void unit_spin_box_int_rounds_converted_values() {
  UnitIntSpinBox spin(SpinUnit::Pixels);
  spin.setRange(0, 5000);
  spin.set_context_provider([] { return UnitConversionContext{72.0, 400.0}; });
  spin.setValue(7);
  spin.show();
  QApplication::processEvents();
  commit_text(spin, QStringLiteral("1.5 in"));
  CHECK(spin.value() == 108);
  commit_text(spin, QStringLiteral("33.3%"));
  CHECK(spin.value() == 133);
  commit_text(spin, QStringLiteral("12"));
  CHECK(spin.value() == 12);
  commit_text(spin, QStringLiteral("9 deg"));
  CHECK(spin.value() == 12);
}

// Leaving the native unit widens the decimals to the shown unit's and makes one
// arrow press (or one scrubby-drag pixel) move one display unit; returning
// restores the field's own presentation. The decimals change is silent: a shape
// W/H handler must not resize the shape because its readout switched to mm.
void unit_spin_box_display_unit_adjusts_decimals_and_step() {
  UnitSpinBox spin(SpinUnit::Pixels);
  spin.setRange(0.0, 60000.0);
  spin.setDecimals(1);
  spin.setSingleStep(1.0);
  spin.set_context_provider([] { return UnitConversionContext{300.0, 600.0}; });
  spin.set_display_unit_switchable(true);
  spin.setValue(300.0);
  spin.show();
  QApplication::processEvents();
  int value_changes = 0;
  QObject::connect(&spin, &QDoubleSpinBox::valueChanged, &spin, [&value_changes](double) { ++value_changes; });

  const auto suffix_for = [](patchy::ui::MeasurementUnit unit) {
    return QStringLiteral(" ") + patchy::ui::measurement_unit_suffix(unit);
  };
  spin.set_display_unit(SpinUnit::Millimeters);
  CHECK(value_changes == 0);
  CHECK(spin.decimals() == 1);  // max(native 1, mm 1)
  CHECK(close_to(spin.singleStep(), 300.0 / 25.4));
  CHECK(spin.text() == QStringLiteral("25.4") + suffix_for(patchy::ui::MeasurementUnit::Millimeters));
  spin.stepBy(1);
  CHECK(close_to(spin.value(), 300.0 + 300.0 / 25.4, 0.05));
  CHECK(spin.text() == QStringLiteral("26.4") + suffix_for(patchy::ui::MeasurementUnit::Millimeters));
  spin.setValue(300.0);
  value_changes = 0;

  // Inches: three decimals, 0.01 in (3 px) per step.
  spin.set_display_unit(SpinUnit::Inches);
  CHECK(value_changes == 0);
  CHECK(spin.decimals() == 3);
  CHECK(close_to(spin.singleStep(), 3.0));
  CHECK(spin.text() == QStringLiteral("1.000") + patchy::ui::inch_suffix());
  spin.stepBy(-1);
  CHECK(close_to(spin.value(), 297.0, 0.01));
  spin.setValue(300.0);
  value_changes = 0;

  // Percent of the 600 px basis: 1% is 6 px.
  spin.set_display_unit(SpinUnit::Percent);
  CHECK(spin.decimals() == 2);  // max(native 1, percent 2): each unit gets its own count
  CHECK(close_to(spin.singleStep(), 6.0));
  CHECK(spin.text() == QStringLiteral("50.00%"));

  // Back to pixels: the construction decimals and step return, still silently.
  spin.set_display_unit(SpinUnit::Pixels);
  CHECK(value_changes == 0);
  CHECK(spin.decimals() == 1);
  CHECK(close_to(spin.singleStep(), 1.0));
  CHECK(spin.text() == QStringLiteral("300.0") + patchy::ui::pixel_suffix());

  // A PPI change re-derives the step for the shown unit.
  double ppi = 300.0;
  spin.set_context_provider([&ppi] { return UnitConversionContext{ppi, 600.0}; });
  spin.set_display_unit(SpinUnit::Millimeters);
  CHECK(close_to(spin.singleStep(), 300.0 / 25.4));
  ppi = 72.0;
  spin.refresh_display_metrics();
  CHECK(close_to(spin.singleStep(), 72.0 / 25.4));
  CHECK(spin.text() == QStringLiteral("105.8") + suffix_for(patchy::ui::MeasurementUnit::Millimeters));
}

// The MeasurementUnit/SpinUnit bridge and the helpers the dimension fields use
// to present themselves in the ruler unit (docs/resolution-units.md).
void unit_spin_box_document_field_units_helpers() {
  using patchy::ui::MeasurementUnit;
  for (const auto unit : {MeasurementUnit::Pixels, MeasurementUnit::Inches, MeasurementUnit::Centimeters,
                          MeasurementUnit::Millimeters, MeasurementUnit::Points, MeasurementUnit::Percent}) {
    const auto spin_unit = patchy::ui::spin_unit_for(unit);
    CHECK(patchy::ui::measurement_unit_for(spin_unit) == unit);
  }
  CHECK(!patchy::ui::measurement_unit_for(SpinUnit::Degrees).has_value());

  patchy::ui::DocumentFieldUnits units;
  units.display_unit = MeasurementUnit::Millimeters;
  units.ppi = 72.0;
  units.document_width = 800.0;
  units.document_height = 600.0;
  const auto horizontal = patchy::ui::document_field_context(units, true);
  CHECK(close_to(horizontal.ppi, 72.0));
  CHECK(close_to(horizontal.percent_reference_pixels, 800.0));
  const auto vertical = patchy::ui::document_field_context(units, false);
  CHECK(close_to(vertical.percent_reference_pixels, 600.0));
  units.ppi = 0.0;  // sanitized
  CHECK(close_to(patchy::ui::document_field_context(units, true).ppi, 300.0));
  units.ppi = 72.0;

  UnitSpinBox spin(SpinUnit::Pixels);
  spin.setRange(0.0, 60000.0);
  spin.setDecimals(1);
  spin.setValue(72.0);
  patchy::ui::apply_document_field_units(&spin, units, false);
  CHECK(spin.display_unit_switchable());
  CHECK(spin.display_unit() == SpinUnit::Millimeters);
  CHECK(close_to(spin.conversion_context().ppi, 72.0));
  CHECK(close_to(spin.conversion_context().percent_reference_pixels, 600.0));
  CHECK(close_to(spin.value(), 72.0));  // native pixels untouched
  CHECK(spin.text() == QStringLiteral("25.4 ") + patchy::ui::measurement_unit_suffix(MeasurementUnit::Millimeters));

  // A plain number typed now means millimeters.
  spin.show();
  QApplication::processEvents();
  commit_text(spin, QStringLiteral("50.8"));
  CHECK(close_to(spin.value(), 144.0, 0.05));

  // The same unit again only refreshes; a different one switches.
  patchy::ui::set_field_display_unit(&spin, MeasurementUnit::Millimeters);
  CHECK(spin.display_unit() == SpinUnit::Millimeters);
  patchy::ui::set_field_display_unit(&spin, MeasurementUnit::Pixels);
  CHECK(spin.display_unit() == SpinUnit::Pixels);
  CHECK(spin.text() == QStringLiteral("144.0") + patchy::ui::pixel_suffix());
  patchy::ui::set_field_display_unit(nullptr, MeasurementUnit::Inches);  // tolerated
}

}  // namespace

// The right-click menu lists the units and its pick goes through pick_display_unit:
// the field shows the unit and emits display_unit_picked (MainWindow's cue to change
// the ruler preference). A typed token emits display_unit_changed only, and an
// unswitchable field ignores picks.
void unit_spin_box_context_menu_pick_emits_picked() {
  using patchy::ui::MeasurementUnit;
  UnitSpinBox spin(SpinUnit::Pixels);
  spin.setRange(0.0, 10000.0);
  spin.setDecimals(1);
  spin.setValue(300.0);
  spin.set_context_provider([] { return UnitConversionContext{300.0, 600.0}; });
  spin.set_display_unit_switchable(true);
  spin.show();
  QApplication::processEvents();
  std::vector<SpinUnit> picked;
  std::vector<SpinUnit> changed;
  QObject::connect(&spin, &UnitSpinBox::display_unit_picked, &spin, [&picked](SpinUnit unit) { picked.push_back(unit); });
  QObject::connect(&spin, &UnitSpinBox::display_unit_changed, &spin,
                   [&changed](SpinUnit unit) { changed.push_back(unit); });

  bool saw_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || !menu->isVisible()) {
        continue;
      }
      QStringList names;
      for (auto* action : menu->actions()) {
        names << action->text();
      }
      CHECK(names.contains(patchy::ui::measurement_unit_name(MeasurementUnit::Inches)));
      CHECK(names.contains(patchy::ui::measurement_unit_name(MeasurementUnit::Percent)));  // has a basis
      for (auto* action : menu->actions()) {
        if (action->text() == patchy::ui::measurement_unit_name(MeasurementUnit::Inches)) {
          saw_menu = true;
          action->trigger();
          menu->close();
          return;
        }
      }
    }
    CHECK(false);
  });
  const auto point = spin.rect().center();
  QContextMenuEvent event(QContextMenuEvent::Mouse, point, spin.mapToGlobal(point));
  QApplication::sendEvent(&spin, &event);
  QApplication::processEvents();
  CHECK(saw_menu);
  CHECK(spin.display_unit() == SpinUnit::Inches);
  CHECK(spin.text() == QStringLiteral("1.000") + patchy::ui::inch_suffix());
  CHECK(picked == std::vector<SpinUnit>{SpinUnit::Inches});
  CHECK(changed == std::vector<SpinUnit>{SpinUnit::Inches});

  // A typed token switches the display without a pick.
  commit_text(spin, QStringLiteral("25.4 mm"));
  CHECK(spin.display_unit() == SpinUnit::Millimeters);
  CHECK(close_to(spin.value(), 300.0));
  CHECK(picked.size() == 1);
  CHECK(changed.size() == 2);

  // Direct picks: a no-op once the field stops being switchable.
  spin.pick_display_unit(SpinUnit::Centimeters);
  CHECK(spin.display_unit() == SpinUnit::Centimeters);
  CHECK(picked.size() == 2);
  spin.set_display_unit_switchable(false);
  spin.pick_display_unit(SpinUnit::Pixels);
  CHECK(spin.display_unit() == SpinUnit::Centimeters);
  CHECK(picked.size() == 2);
}

std::vector<patchy::test::TestCase> unit_spin_box_tests() {
  return {
      {"unit_spin_box_parses_unit_tokens", unit_spin_box_parses_unit_tokens},
      {"unit_spin_box_converts_between_units", unit_spin_box_converts_between_units},
      {"unit_spin_box_pixel_field_accepts_percent_and_physical", unit_spin_box_pixel_field_accepts_percent_and_physical},
      {"unit_spin_box_percent_field_accepts_pixels", unit_spin_box_percent_field_accepts_pixels},
      {"unit_spin_box_rejects_incompatible_units", unit_spin_box_rejects_incompatible_units},
      {"unit_spin_box_plain_number_uses_native_unit", unit_spin_box_plain_number_uses_native_unit},
      {"unit_spin_box_keeps_translated_suffix", unit_spin_box_keeps_translated_suffix},
      {"unit_spin_box_language_switch_refreshes_suffix_without_changing_values",
       unit_spin_box_language_switch_refreshes_suffix_without_changing_values},
      {"unit_spin_box_display_unit_follows_typed_unit", unit_spin_box_display_unit_follows_typed_unit},
      {"unit_spin_box_int_rounds_converted_values", unit_spin_box_int_rounds_converted_values},
      {"unit_spin_box_display_unit_adjusts_decimals_and_step", unit_spin_box_display_unit_adjusts_decimals_and_step},
      {"unit_spin_box_document_field_units_helpers", unit_spin_box_document_field_units_helpers},
      {"unit_spin_box_context_menu_pick_emits_picked", unit_spin_box_context_menu_pick_emits_picked},
  };
}
