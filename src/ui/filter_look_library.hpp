#pragma once

#include "filters/filter_registry.hpp"
#include "ui/preset_library.hpp"

#include <QString>

#include <cstdint>
#include <vector>

namespace patchy::ui {

inline constexpr int kFilterLookRecordVersion = 1;

// Library failures stay non-localized so callers can choose the appropriate
// translated message for their UI. A structurally valid recipe can still be
// unsupported by a particular FilterRegistry; that is deliberately not a
// library error.
enum class FilterLookLibraryError : std::uint8_t {
  None,
  InvalidName,
  InvalidRecipe,
  StorageUnavailable,
  WriteFailed,
  NotFound,
  RemoveFailed,
};

struct FilterLookLibraryEntry {
  QString id;  // canonical UUID and JSON filename stem; never changes
  QString name;
  FilterRecipe recipe;
};

struct FilterLookLibraryTraits {
  using Entry = FilterLookLibraryEntry;
  static constexpr const char* kSubdir = "looks";
  static constexpr bool kHasFolders = false;
  static constexpr bool kUngroupedSortsFirst = false;
  [[nodiscard]] static const QString& id(const Entry& entry) { return entry.id; }
};

using FilterLookLibraryBase = PresetLibraryT<FilterLookLibraryTraits>;

// Persistent application-wide user Looks. Each Look is one bounded,
// self-contained version-1 JSON record in <settings directory>/looks. Records
// are independent so one malformed file cannot hide or damage its neighbors.
// The storage directory is injectable for tests.
class FilterLookLibrary : public FilterLookLibraryBase {
  Q_OBJECT

public:
  explicit FilterLookLibrary(QString storage_dir = {}, QObject* parent = nullptr);

  // add_look always creates a new stable UUID. Rename and remove never change
  // or reuse it. In-memory state changes only after the disk operation succeeds.
  [[nodiscard]] QString add_look(const QString& name, const FilterRecipe& recipe,
                                 FilterLookLibraryError* error = nullptr);
  bool rename_look(const QString& id, const QString& name,
                   FilterLookLibraryError* error = nullptr);
  bool remove_look(const QString& id, FilterLookLibraryError* error = nullptr);

private:
  void reload();
  [[nodiscard]] bool write_entry(const FilterLookLibraryEntry& entry) const;
};

}  // namespace patchy::ui
