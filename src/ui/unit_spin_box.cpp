#include "ui/unit_spin_box.hpp"

#include "ui/measurement_units.hpp"

#include <QAction>
#include <QChar>
#include <QEvent>
#include <QLineEdit>
#include <QMenu>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// English abbreviations and names always work, whatever the UI language (Photoshop
// behavior). Built once; QStringLiteral keeps the non-ASCII tokens UTF-16 regardless
// of the compiler's execution charset.
const std::vector<std::pair<QString, SpinUnit>>& english_tokens() {
  static const std::vector<std::pair<QString, SpinUnit>> tokens = {
      {QStringLiteral("px"), SpinUnit::Pixels},
      {QStringLiteral("pixel"), SpinUnit::Pixels},
      {QStringLiteral("pixels"), SpinUnit::Pixels},
      {QStringLiteral("in"), SpinUnit::Inches},
      {QStringLiteral("inch"), SpinUnit::Inches},
      {QStringLiteral("inches"), SpinUnit::Inches},
      {QStringLiteral("\""), SpinUnit::Inches},
      {QStringLiteral("\u2033"), SpinUnit::Inches},
      {QStringLiteral("cm"), SpinUnit::Centimeters},
      {QStringLiteral("centimeter"), SpinUnit::Centimeters},
      {QStringLiteral("centimeters"), SpinUnit::Centimeters},
      {QStringLiteral("centimetre"), SpinUnit::Centimeters},
      {QStringLiteral("centimetres"), SpinUnit::Centimeters},
      {QStringLiteral("mm"), SpinUnit::Millimeters},
      {QStringLiteral("millimeter"), SpinUnit::Millimeters},
      {QStringLiteral("millimeters"), SpinUnit::Millimeters},
      {QStringLiteral("millimetre"), SpinUnit::Millimeters},
      {QStringLiteral("millimetres"), SpinUnit::Millimeters},
      {QStringLiteral("pt"), SpinUnit::Points},
      {QStringLiteral("point"), SpinUnit::Points},
      {QStringLiteral("points"), SpinUnit::Points},
      {QStringLiteral("%"), SpinUnit::Percent},
      {QStringLiteral("percent"), SpinUnit::Percent},
      {QStringLiteral("pct"), SpinUnit::Percent},
      {QStringLiteral("deg"), SpinUnit::Degrees},
      {QStringLiteral("degree"), SpinUnit::Degrees},
      {QStringLiteral("degrees"), SpinUnit::Degrees},
      {QStringLiteral("\u00b0"), SpinUnit::Degrees},
  };
  return tokens;
}

// Localized suffixes (trimmed, lowercased) so a translated display suffix round-trips.
std::vector<std::pair<QString, SpinUnit>> localized_tokens() {
  std::vector<std::pair<QString, SpinUnit>> tokens;
  constexpr std::array<SpinUnit, 7> kUnits = {SpinUnit::Pixels,  SpinUnit::Inches,  SpinUnit::Centimeters,
                                              SpinUnit::Millimeters, SpinUnit::Points, SpinUnit::Percent,
                                              SpinUnit::Degrees};
  for (const auto unit : kUnits) {
    const auto suffix = spin_unit_suffix(unit).trimmed().toLower();
    if (!suffix.isEmpty()) {
      tokens.emplace_back(suffix, unit);
    }
  }
  return tokens;
}

// Longest leading run that can be a signed decimal number: optional sign, digits,
// at most one decimal point (locale or '.'). Returns the number of characters.
qsizetype number_prefix_length(const QString& text, const QLocale& locale) {
  const auto decimal = locale.decimalPoint();
  qsizetype index = 0;
  if (index < text.size() && (text[index] == QLatin1Char('+') || text[index] == QLatin1Char('-'))) {
    ++index;
  }
  bool seen_point = false;
  while (index < text.size()) {
    const auto ch = text[index];
    if (ch.isDigit()) {
      ++index;
      continue;
    }
    const bool is_point = ch == QLatin1Char('.') || (!decimal.isEmpty() && ch == decimal[0]);
    if (is_point && !seen_point) {
      seen_point = true;
      ++index;
      continue;
    }
    break;
  }
  return index;
}

std::optional<double> parse_number(QString number, const QLocale& locale) {
  QLocale strict(locale);
  strict.setNumberOptions(strict.numberOptions() | QLocale::RejectGroupSeparator);
  bool ok = false;
  auto value = strict.toDouble(number, &ok);
  if (!ok) {
    QLocale c = QLocale::c();
    c.setNumberOptions(c.numberOptions() | QLocale::RejectGroupSeparator);
    value = c.toDouble(number, &ok);
  }
  if (!ok || !std::isfinite(value)) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

std::optional<MeasurementUnit> measurement_unit_for(SpinUnit unit) noexcept {
  switch (unit) {
    case SpinUnit::Pixels:
      return MeasurementUnit::Pixels;
    case SpinUnit::Inches:
      return MeasurementUnit::Inches;
    case SpinUnit::Centimeters:
      return MeasurementUnit::Centimeters;
    case SpinUnit::Millimeters:
      return MeasurementUnit::Millimeters;
    case SpinUnit::Points:
      return MeasurementUnit::Points;
    case SpinUnit::Percent:
      return MeasurementUnit::Percent;
    case SpinUnit::Degrees:
      return std::nullopt;
  }
  return std::nullopt;
}

SpinUnit spin_unit_for(MeasurementUnit unit) noexcept {
  switch (unit) {
    case MeasurementUnit::Pixels:
      return SpinUnit::Pixels;
    case MeasurementUnit::Inches:
      return SpinUnit::Inches;
    case MeasurementUnit::Centimeters:
      return SpinUnit::Centimeters;
    case MeasurementUnit::Millimeters:
      return SpinUnit::Millimeters;
    case MeasurementUnit::Points:
      return SpinUnit::Points;
    case MeasurementUnit::Percent:
      return SpinUnit::Percent;
  }
  return SpinUnit::Pixels;
}

QString spin_unit_suffix(SpinUnit unit) {
  switch (unit) {
    case SpinUnit::Pixels:
      return pixel_suffix();
    case SpinUnit::Inches:
      return inch_suffix();
    case SpinUnit::Centimeters:
      return QStringLiteral(" ") + measurement_unit_suffix(MeasurementUnit::Centimeters);
    case SpinUnit::Millimeters:
      return QStringLiteral(" ") + measurement_unit_suffix(MeasurementUnit::Millimeters);
    case SpinUnit::Points:
      return QStringLiteral(" ") + measurement_unit_suffix(MeasurementUnit::Points);
    case SpinUnit::Percent:
      return percent_suffix();
    case SpinUnit::Degrees:
      return degree_suffix();
  }
  return pixel_suffix();
}

UnitEntryParse parse_unit_entry(QString text, const QLocale& locale, const QString& prefix,
                                const QString& suffix) {
  UnitEntryParse result;
  if (!prefix.isEmpty() && text.startsWith(prefix)) {
    text.remove(0, prefix.size());
  }
  if (!suffix.isEmpty() && text.endsWith(suffix)) {
    text.chop(suffix.size());
  }
  text = text.trimmed();
  if (text.isEmpty()) {
    result.state = QValidator::Intermediate;
    return result;
  }

  const auto number_length = number_prefix_length(text, locale);
  const auto number = text.left(number_length);
  const auto token = text.mid(number_length).trimmed().toLower();
  result.has_unit_token = !token.isEmpty();

  const bool number_is_partial = number.isEmpty() || number == QStringLiteral("+") ||
                                 number == QStringLiteral("-") || number == QStringLiteral(".") ||
                                 (!locale.decimalPoint().isEmpty() && number == locale.decimalPoint()) ||
                                 number == QStringLiteral("+.") || number == QStringLiteral("-.");

  std::optional<SpinUnit> unit;
  bool token_is_partial = false;
  if (!token.isEmpty()) {
    for (const auto& [candidate, candidate_unit] : english_tokens()) {
      if (candidate == token) {
        unit = candidate_unit;
        break;
      }
      if (candidate.startsWith(token)) {
        token_is_partial = true;
      }
    }
    if (!unit.has_value()) {
      for (const auto& [candidate, candidate_unit] : localized_tokens()) {
        if (candidate == token) {
          unit = candidate_unit;
          break;
        }
        if (candidate.startsWith(token)) {
          token_is_partial = true;
        }
      }
    }
    if (!unit.has_value()) {
      result.state = token_is_partial ? QValidator::Intermediate : QValidator::Invalid;
      return result;
    }
  }

  if (number_is_partial) {
    // "-", "." or "in" alone: still typing, unless a unit already follows a
    // missing number (" px" is not going to become a number).
    result.state = (number.isEmpty() && unit.has_value()) ? QValidator::Invalid : QValidator::Intermediate;
    return result;
  }
  const auto value = parse_number(number, locale);
  if (!value.has_value()) {
    result.state = QValidator::Invalid;
    return result;
  }
  result.state = QValidator::Acceptable;
  result.entry.value = *value;
  result.entry.unit = unit;
  return result;
}

std::optional<double> convert_unit_entry(const UnitEntry& entry, SpinUnit native,
                                         const UnitConversionContext& context) {
  if (!std::isfinite(entry.value)) {
    return std::nullopt;
  }
  if (!entry.unit.has_value() || *entry.unit == native) {
    return entry.value;
  }
  const auto typed = *entry.unit;
  if (typed == SpinUnit::Degrees || native == SpinUnit::Degrees) {
    return std::nullopt;  // angles do not convert to lengths
  }
  const auto ppi = sanitized_document_ppi(context.ppi);
  const auto reference = std::isfinite(context.percent_reference_pixels) ? context.percent_reference_pixels : 0.0;
  const auto typed_unit = measurement_unit_for(typed);
  const auto native_unit = measurement_unit_for(native);
  if (!typed_unit.has_value() || !native_unit.has_value()) {
    return std::nullopt;
  }
  if (typed == SpinUnit::Percent || native == SpinUnit::Percent) {
    if (reference <= 0.0) {
      return std::nullopt;  // no percent basis for this field
    }
  }
  // Everything goes through pixels: typed unit -> px -> native unit.
  const auto pixels = measurement_unit_to_pixels(entry.value, *typed_unit, ppi, reference);
  return pixels_to_measurement_unit(pixels, *native_unit, ppi, reference);
}

// ---------------------------------------------------------------------------

UnitSpinBox::UnitSpinBox(SpinUnit native, QWidget* parent)
    : QDoubleSpinBox(parent), native_(native), display_(native) {
  refresh_suffix();
  connect(this, &QAbstractSpinBox::editingFinished, this, [this] {
    if (!pending_display_unit_.has_value()) {
      return;
    }
    const auto unit = *pending_display_unit_;
    pending_display_unit_.reset();
    if (switchable_) {
      set_display_unit(unit);
    }
  });
}

void UnitSpinBox::set_context_provider(ContextProvider provider) {
  context_provider_ = std::move(provider);
}

UnitConversionContext UnitSpinBox::conversion_context() const {
  return context_provider_ ? context_provider_() : UnitConversionContext{};
}

void UnitSpinBox::refresh_suffix() {
  setSuffix(spin_unit_suffix(display_));
}

void UnitSpinBox::changeEvent(QEvent* event) {
  QDoubleSpinBox::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    refresh_suffix();
  }
}

void UnitSpinBox::set_display_unit(SpinUnit unit) {
  if (unit == display_) {
    return;
  }
  if (unit == SpinUnit::Degrees || native_ == SpinUnit::Degrees) {
    return;  // angles have one unit
  }
  if (display_ == native_) {
    // Leaving the native presentation: remember it for the way back.
    native_decimals_ = decimals();
    native_single_step_ = singleStep();
  }
  display_ = unit;
  refresh_display_metrics();
  refresh_suffix();  // setSuffix re-renders the edit through textFromValue
  Q_EMIT display_unit_changed(display_);
}

void UnitSpinBox::refresh_display_metrics() {
  // setDecimals re-rounds the value and would fire valueChanged into the field's
  // handler (a shape resize, a transform nudge) for a presentation change, so it
  // runs only when the count really changes and never as a value edit.
  const auto set_decimals_quietly = [this](int count) {
    if (decimals() == count) {
      return;
    }
    const QSignalBlocker blocker(this);
    setDecimals(count);
  };
  if (display_ == native_) {
    if (native_decimals_.has_value()) {
      set_decimals_quietly(*native_decimals_);
      native_decimals_.reset();
    }
    if (native_single_step_.has_value()) {
      setSingleStep(*native_single_step_);
      native_single_step_.reset();
    }
    return;
  }
  const auto shown = measurement_unit_for(display_);
  if (!shown.has_value() || !native_decimals_.has_value()) {
    return;
  }
  set_decimals_quietly(std::max(*native_decimals_, measurement_unit_decimals(*shown)));
  // One display unit, expressed in the native unit; a percent display without a
  // basis keeps the native step.
  const auto step = convert_unit_entry(UnitEntry{measurement_unit_single_step(*shown), display_}, native_,
                                       conversion_context());
  if (step.has_value() && *step > 0.0) {
    setSingleStep(*step);
  }
}

void UnitSpinBox::set_display_unit_switchable(bool enabled) {
  switchable_ = enabled;
}

void UnitSpinBox::pick_display_unit(SpinUnit unit) {
  if (!switchable_ || unit == SpinUnit::Degrees || native_ == SpinUnit::Degrees) {
    return;
  }
  set_display_unit(unit);
  Q_EMIT display_unit_picked(unit);
}

UnitEntry UnitSpinBox::effective_entry(UnitEntry entry) const {
  // A plain number means "in the unit on screen".
  if (!entry.unit.has_value() && display_ != native_) {
    entry.unit = display_;
  }
  return entry;
}

QValidator::State UnitSpinBox::validate(QString& input, int& pos) const {
  const auto parsed = parse_unit_entry(input, locale(), prefix(), suffix());
  if (!parsed.has_unit_token && display_ == native_) {
    // Plain numbers keep the stock behavior (range typing rules, prefix/suffix
    // editing, partial input).
    return QDoubleSpinBox::validate(input, pos);
  }
  if (parsed.state != QValidator::Acceptable) {
    return parsed.state;
  }
  // A typed unit converts; out-of-range results clamp on commit, like Photoshop.
  return convert_unit_entry(effective_entry(parsed.entry), native_, conversion_context()).has_value()
             ? QValidator::Acceptable
             : QValidator::Invalid;
}

double UnitSpinBox::valueFromText(const QString& text) const {
  const auto parsed = parse_unit_entry(text, locale(), prefix(), suffix());
  if (!parsed.has_unit_token && display_ == native_) {
    return QDoubleSpinBox::valueFromText(text);
  }
  if (parsed.state != QValidator::Acceptable) {
    return value();
  }
  const auto converted = convert_unit_entry(effective_entry(parsed.entry), native_, conversion_context());
  if (!converted.has_value()) {
    return value();
  }
  if (parsed.has_unit_token && switchable_ && parsed.entry.unit.has_value()) {
    pending_display_unit_ = parsed.entry.unit;
  }
  return std::clamp(*converted, minimum(), maximum());
}

QString trim_trailing_decimal_zeros(QString text, const QLocale& locale) {
  const auto point = locale.decimalPoint();
  if (!text.contains(point)) {
    return text;
  }
  while (text.endsWith(QLatin1Char('0'))) {
    text.chop(1);
  }
  if (text.endsWith(point)) {
    text.chop(point.size());
  }
  return text;
}

TrimmedDoubleSpinBox::TrimmedDoubleSpinBox(QWidget* parent) : QDoubleSpinBox(parent) {}

QString TrimmedDoubleSpinBox::textFromValue(double value) const {
  return trim_trailing_decimal_zeros(QDoubleSpinBox::textFromValue(value), locale());
}

void UnitSpinBox::set_trim_trailing_zeros(bool enabled) {
  if (trim_trailing_zeros_ == enabled) {
    return;
  }
  trim_trailing_zeros_ = enabled;
  // Re-render the current value in the new form.
  lineEdit()->setText(prefix() + textFromValue(value()) + suffix());
}

QString UnitSpinBox::textFromValue(double value) const {
  const auto trimmed = [this](QString text) {
    return trim_trailing_zeros_ ? trim_trailing_decimal_zeros(std::move(text), locale()) : text;
  };
  if (display_ == native_) {
    return trimmed(QDoubleSpinBox::textFromValue(value));
  }
  const auto shown = convert_unit_entry(UnitEntry{value, native_}, display_, conversion_context());
  if (!shown.has_value()) {
    return trimmed(QDoubleSpinBox::textFromValue(value));
  }
  // Mirror the stock formatting: fixed decimals, group separators only when shown.
  auto text = locale().toString(*shown, 'f', decimals());
  if (!isGroupSeparatorShown() && std::abs(*shown) >= 1000.0) {
    text.remove(locale().groupSeparator());
  }
  return trimmed(text);
}

void UnitSpinBox::fixup(QString& input) const {
  const auto parsed = parse_unit_entry(input, locale(), prefix(), suffix());
  if (!parsed.has_unit_token && display_ == native_) {
    QDoubleSpinBox::fixup(input);
    return;
  }
  input = input.simplified();
}

void UnitSpinBox::contextMenuEvent(QContextMenuEvent* event) {
  if (!switchable_ || native_ == SpinUnit::Degrees) {
    QDoubleSpinBox::contextMenuEvent(event);
    return;
  }
  // Photoshop's field unit menu: pick the unit the field displays in.
  const auto context = conversion_context();
  QMenu menu(this);
  const std::array<std::pair<SpinUnit, MeasurementUnit>, 6> units = {{
      {SpinUnit::Pixels, MeasurementUnit::Pixels},
      {SpinUnit::Inches, MeasurementUnit::Inches},
      {SpinUnit::Centimeters, MeasurementUnit::Centimeters},
      {SpinUnit::Millimeters, MeasurementUnit::Millimeters},
      {SpinUnit::Points, MeasurementUnit::Points},
      {SpinUnit::Percent, MeasurementUnit::Percent},
  }};
  for (const auto& [unit, measurement_unit] : units) {
    if (unit == SpinUnit::Percent && context.percent_reference_pixels <= 0.0 && native_ != SpinUnit::Percent) {
      continue;  // no basis for percent on this field
    }
    auto* action = menu.addAction(measurement_unit_name(measurement_unit));
    action->setCheckable(true);
    action->setChecked(unit == display_);
    const auto chosen = unit;
    connect(action, &QAction::triggered, this, [this, chosen] { pick_display_unit(chosen); });
  }
  menu.exec(event->globalPos());
  event->accept();
}

// ---------------------------------------------------------------------------

UnitIntSpinBox::UnitIntSpinBox(SpinUnit native, QWidget* parent) : QSpinBox(parent), native_(native) {
  refresh_suffix();
}

void UnitIntSpinBox::set_context_provider(ContextProvider provider) {
  context_provider_ = std::move(provider);
}

UnitConversionContext UnitIntSpinBox::conversion_context() const {
  return context_provider_ ? context_provider_() : UnitConversionContext{};
}

void UnitIntSpinBox::refresh_suffix() {
  setSuffix(spin_unit_suffix(native_));
}

void UnitIntSpinBox::changeEvent(QEvent* event) {
  QSpinBox::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) {
    refresh_suffix();
  }
}

QValidator::State UnitIntSpinBox::validate(QString& input, int& pos) const {
  const auto parsed = parse_unit_entry(input, locale(), prefix(), suffix());
  if (!parsed.has_unit_token) {
    const auto state = QSpinBox::validate(input, pos);
    // A decimal number is on its way to a unit token ("2." then "2.5 mm"): keep it
    // editable. Without a unit it is not a whole number and reverts on commit.
    const bool has_point = input.contains(QLatin1Char('.')) || (!locale().decimalPoint().isEmpty() &&
                                                                input.contains(locale().decimalPoint()));
    if (state == QValidator::Invalid && has_point && parsed.state != QValidator::Invalid) {
      return QValidator::Intermediate;
    }
    return state;
  }
  if (parsed.state != QValidator::Acceptable) {
    return parsed.state;
  }
  return convert_unit_entry(parsed.entry, native_, conversion_context()).has_value() ? QValidator::Acceptable
                                                                                      : QValidator::Invalid;
}

int UnitIntSpinBox::valueFromText(const QString& text) const {
  const auto parsed = parse_unit_entry(text, locale(), prefix(), suffix());
  if (!parsed.has_unit_token) {
    return QSpinBox::valueFromText(text);
  }
  if (parsed.state != QValidator::Acceptable) {
    return value();
  }
  const auto converted = convert_unit_entry(parsed.entry, native_, conversion_context());
  if (!converted.has_value()) {
    return value();
  }
  const auto rounded = std::lround(std::clamp(*converted, static_cast<double>(minimum()), static_cast<double>(maximum())));
  return static_cast<int>(rounded);
}

void UnitIntSpinBox::fixup(QString& input) const {
  const auto parsed = parse_unit_entry(input, locale(), prefix(), suffix());
  if (!parsed.has_unit_token) {
    QSpinBox::fixup(input);
    return;
  }
  input = input.simplified();
}

// ---------------------------------------------------------------------------

UnitConversionContext document_field_context(const DocumentFieldUnits& units, bool horizontal) {
  UnitConversionContext context;
  context.ppi = sanitized_document_ppi(units.ppi);
  const auto basis = horizontal ? units.document_width : units.document_height;
  context.percent_reference_pixels = std::isfinite(basis) && basis > 0.0 ? basis : 0.0;
  return context;
}

void set_field_display_unit(UnitSpinBox* spin, MeasurementUnit unit) {
  if (spin == nullptr) {
    return;
  }
  spin->set_display_unit_switchable(true);
  const auto display = spin_unit_for(unit);
  if (spin->display_unit() == display) {
    spin->refresh_display_metrics();
  } else {
    spin->set_display_unit(display);
  }
}

void apply_document_field_units(UnitSpinBox* spin, const DocumentFieldUnits& units, bool horizontal) {
  if (spin == nullptr) {
    return;
  }
  const auto context = document_field_context(units, horizontal);
  spin->set_context_provider([context] { return context; });
  set_field_display_unit(spin, units.display_unit);
  if (units.on_unit_picked) {
    QObject::connect(spin, &UnitSpinBox::display_unit_picked, spin,
                     [callback = units.on_unit_picked](SpinUnit unit) {
                       if (const auto measurement = measurement_unit_for(unit); measurement.has_value()) {
                         callback(*measurement);
                       }
                     });
  }
}

void link_field_unit_picks(const std::vector<UnitSpinBox*>& fields) {
  for (auto* field : fields) {
    if (field == nullptr) {
      continue;
    }
    QObject::connect(field, &UnitSpinBox::display_unit_picked, field, [fields, field](SpinUnit unit) {
      for (auto* other : fields) {
        if (other != nullptr && other != field) {
          other->set_display_unit(unit);
        }
      }
    });
  }
}

}  // namespace patchy::ui
