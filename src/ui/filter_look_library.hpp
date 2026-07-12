#pragma once

#include "filters/filter_registry.hpp"

#include <QObject>
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

// Persistent application-wide user Looks. Each Look is one bounded,
// self-contained version-1 JSON record in <settings directory>/looks. Records
// are independent so one malformed file cannot hide or damage its neighbors.
// The storage directory is injectable for tests.
class FilterLookLibrary : public QObject {
  Q_OBJECT

public:
  explicit FilterLookLibrary(QString storage_dir = {}, QObject* parent = nullptr);

  [[nodiscard]] const QString& storage_dir() const noexcept;
  [[nodiscard]] const std::vector<FilterLookLibraryEntry>& entries() const noexcept;
  [[nodiscard]] const FilterLookLibraryEntry* find_entry(const QString& id) const;

  // add_look always creates a new stable UUID. Rename and remove never change
  // or reuse it. In-memory state changes only after the disk operation succeeds.
  [[nodiscard]] QString add_look(const QString& name, const FilterRecipe& recipe,
                                 FilterLookLibraryError* error = nullptr);
  bool rename_look(const QString& id, const QString& name,
                   FilterLookLibraryError* error = nullptr);
  bool remove_look(const QString& id, FilterLookLibraryError* error = nullptr);

signals:
  void changed();

private:
  void reload();
  void sort_entries();
  [[nodiscard]] bool write_entry(const FilterLookLibraryEntry& entry) const;
  [[nodiscard]] QString json_path(const QString& id) const;

  QString storage_dir_;
  std::vector<FilterLookLibraryEntry> entries_;
};

}  // namespace patchy::ui
