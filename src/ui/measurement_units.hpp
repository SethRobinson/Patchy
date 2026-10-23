#pragma once

#include <QString>

#include <initializer_list>

namespace patchy::ui {

// Display/entry units for lengths tied to the document resolution (PPI). Pixels are
// always the stored truth; these units exist only at UI surfaces (Image Size, New
// Document, rulers, the document info panel) as conversions through the PPI.
enum class MeasurementUnit {
  Pixels,
  Inches,
  Centimeters,
  Millimeters,
  Points,   // print points, 72 per inch (Photoshop's type/print convention)
  Percent,  // relative to a caller-supplied reference extent in pixels
};

// Sanitizes a document PPI for conversions: non-finite or <= 0 falls back to 300
// (the document default used across the app).
[[nodiscard]] double sanitized_document_ppi(double ppi) noexcept;

// Localized short suffix: px, in, cm, mm, pt, %.
[[nodiscard]] QString measurement_unit_suffix(MeasurementUnit unit);
// Localized full name for combo boxes: Pixels, Inches, ...
[[nodiscard]] QString measurement_unit_name(MeasurementUnit unit);

// Spin-box suffixes for controls that are not unit-switchable: " px", " in", "%" and
// the degree sign. Every setSuffix in src/ui goes through these (or
// measurement_unit_suffix) so the text stays translatable.
[[nodiscard]] QString pixel_suffix();
[[nodiscard]] QString inch_suffix();
[[nodiscard]] QString percent_suffix();
[[nodiscard]] QString degree_suffix();

// Readout formatting for on-canvas and status text: locale number (no group
// separators) plus the translated suffix. `show_sign` prefixes positive values with
// "+" for deltas. Values within half a unit of zero print as 0, never "-0".
// The bare number those use, with no suffix.
[[nodiscard]] QString format_readout_number(double value, int decimals, bool show_sign = false);
[[nodiscard]] QString format_pixels(double pixels, int decimals = 0, bool show_sign = false);
[[nodiscard]] QString format_percent(double percent, int decimals = 1, bool show_sign = false);
[[nodiscard]] QString format_degrees(double degrees, int decimals = 1, bool show_sign = false);
// A value already converted into `unit`, with that unit's suffix ("12.5 mm", "40%").
[[nodiscard]] QString format_measurement(double value, MeasurementUnit unit, int decimals,
                                         bool show_sign = false);

// Stable settings tokens ("px", "in", "cm", "mm", "pt", "percent"); tokens are
// persisted in user settings, never rename them.
[[nodiscard]] QString measurement_unit_settings_token(MeasurementUnit unit);
[[nodiscard]] MeasurementUnit measurement_unit_from_settings_token(const QString& token,
                                                                   MeasurementUnit fallback);

// A dialog's remembered W/H unit combo (Image Size, Canvas Size, New Document; the
// keys are compatibility contracts, docs/resolution-units.md): the token stored under
// `settings_key`, else the ruler unit (`view/rulerUnits`) on a first run. A unit the
// combo cannot show (not in `offered`, or an unknown token) falls back to Pixels.
[[nodiscard]] MeasurementUnit remembered_dialog_unit(const QString& settings_key,
                                                     std::initializer_list<MeasurementUnit> offered);
void remember_dialog_unit(const QString& settings_key, MeasurementUnit unit);
// The resolution unit combos (index 0 Pixels/Inch, 1 Pixels/Centimeter), stored as
// "in" / "cm" under `settings_key`; anything else reads as Pixels/Inch.
[[nodiscard]] int remembered_resolution_unit_index(const QString& settings_key);
void remember_resolution_unit(const QString& settings_key, int index);

// True for units that convert through physical length (Inches/Centimeters/Millimeters/Points).
[[nodiscard]] bool measurement_unit_is_physical(MeasurementUnit unit) noexcept;
// Units per inch for physical units (Inches 1, Centimeters 2.54, Millimeters 25.4, Points 72).
[[nodiscard]] double measurement_units_per_inch(MeasurementUnit unit) noexcept;

// Pixel <-> unit conversions. reference_pixels is the Percent basis (the axis extent
// the percentage is relative to); it is ignored by every other unit.
[[nodiscard]] double pixels_to_measurement_unit(double pixels, MeasurementUnit unit, double ppi,
                                                double reference_pixels);
[[nodiscard]] double measurement_unit_to_pixels(double value, MeasurementUnit unit, double ppi,
                                                double reference_pixels);

// Spin-box decimal places appropriate for entering values in the unit.
[[nodiscard]] int measurement_unit_decimals(MeasurementUnit unit) noexcept;
// Spin-box single step for a dimension shown in the unit: one arrow-key press or
// one pixel of a scrubby-label drag (dialog_utils.hpp). Whole units for px, mm, pt
// and percent; 0.1 cm; 0.01 in, so a drag never jumps by an inch per pixel.
[[nodiscard]] double measurement_unit_single_step(MeasurementUnit unit) noexcept;

// Ruler tick spacing for a unit-space ruler. pixels_per_unit_on_screen is how many
// screen pixels one unit currently spans (document px/unit x zoom). The major step is
// the smallest 1-2-5 decade value whose screen spacing is at least min_major_screen_px
// (52 keeps the pixel ruler identical to its historical progression); subdivisions is
// the minor tick count per major interval (inches subdivide in quarters below 10).
struct RulerTickSteps {
  double major{1.0};
  int subdivisions{5};
};
[[nodiscard]] RulerTickSteps ruler_tick_steps(MeasurementUnit unit, double pixels_per_unit_on_screen,
                                              double min_major_screen_px = 52.0) noexcept;

}  // namespace patchy::ui
