#pragma once

// The font picker's Recent list (GitHub issue 569) and its Style and Language filters (GitHub
// issue 570): persisted choices, the per-family style index and its on-disk cache. See
// docs/font-picker.md.

#include "core/font_style_class.hpp"

#include <QFontDatabase>
#include <QHash>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <span>

namespace patchy::ui {

// Persisted identifiers: never rename these keys or the tokens stored under them.
inline constexpr char kFontPickerRecentFamiliesKey[] = "ui/textFontPickerRecentFamilies";
inline constexpr char kFontPickerStyleFilterKey[] = "ui/textFontPickerStyleFilter";
inline constexpr char kFontPickerLanguageFilterKey[] = "ui/textFontPickerLanguageFilter";
inline constexpr int kMaxRecentFontFamilies = 8;

// The Style combo's entries, in combo order.
enum class FontStyleFilter : std::uint8_t {
  all,
  serif,
  sans_serif,
  slab_serif,
  script,
  decorative,
  symbol,
  monospace,
  variable,
  other,  // no usable style class (GitHub issue 570 follow-up); appended, tokens never move
};

[[nodiscard]] std::span<const FontStyleFilter> font_style_filters() noexcept;
// The settings token ("all", "serif", "sans-serif", ...).
[[nodiscard]] QString font_style_filter_token(FontStyleFilter filter);
[[nodiscard]] FontStyleFilter font_style_filter_from_token(const QString& token);
// Whether the filter needs a family's font tables (everything but All Styles and Monospace).
[[nodiscard]] bool font_style_filter_reads_tables(FontStyleFilter filter) noexcept;

// The Language combo's settings token: the WritingSystem enum key ("Latin", "Cyrillic"), "all"
// for All Languages. nullopt (All Languages) for an unknown token.
[[nodiscard]] QString writing_system_token(QFontDatabase::WritingSystem system);
[[nodiscard]] std::optional<QFontDatabase::WritingSystem> writing_system_from_token(const QString& token);

// The stored Recent list, most recent first. It may name families no longer installed; the
// picker leaves those out.
[[nodiscard]] QStringList stored_recent_font_families();
// Moves `family` to the front of the stored list (no repeats, at most kMaxRecentFontFamilies).
void record_recent_font_family(const QString& family);

struct FontFamilyTraits {
  FontStyleCategory category{FontStyleCategory::unknown};
  bool variable{false};  // the face carries an fvar table
};

// The per-family style answers, read lazily from each family's OS/2 and fvar tables (QRawFont,
// so every platform's font engine answers) and cached in memory and in font-style-cache.txt in
// the app's local data folder. A cache entry holds while the family's style list is unchanged.
class FontStyleIndex {
public:
  static FontStyleIndex& instance();

  // nullopt when the family has not been read yet (the disk cache loads on first use).
  [[nodiscard]] std::optional<FontFamilyTraits> cached(const QString& family);
  // Reads the family's tables (one font engine load) and remembers the answer.
  FontFamilyTraits classify(const QString& family);
  [[nodiscard]] bool fixed_pitch(const QString& family);
  [[nodiscard]] bool matches(FontStyleFilter filter, const QString& family, const FontFamilyTraits& traits);

  // Writes the disk cache when classify added anything since the last save.
  void save();
  // Tests: forget everything in memory and on disk.
  void clear();

private:
  struct Entry {
    FontFamilyTraits traits;
    QString signature;   // QFontDatabase::styles(family), joined
    bool checked{false};  // signature compared against the installed family this session
  };
  void load();
  [[nodiscard]] static QString cache_path();

  QHash<QString, Entry> entries_;
  QHash<QString, bool> fixed_pitch_;
  bool loaded_{false};
  bool dirty_{false};
};

}  // namespace patchy::ui
