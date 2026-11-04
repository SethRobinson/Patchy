#pragma once

#include <QString>

#include <cstdint>
#include <functional>
#include <mutex>

namespace patchy::ui {

// What a running save reports to its progress dialog (docs/file-formats-saving.md, "Save
// progress"). The writer may run on a worker thread; the UI thread reads state().
class SaveProgress {
public:
  enum class Kind { Layers, Pages };
  struct State {
    // 0 of a known total: compositing; total of total: writing the file.
    int done{0};
    int total{0};
    Kind kind{Kind::Layers};
    QString label;  // the layer or artboard being written, when there is one
  };

  // `pump` runs after each report made on the thread that owns the dialog (a save that
  // writes on the UI thread), so the dialog paints between steps.
  void set_pump(std::function<void()> pump) { pump_ = std::move(pump); }
  void report(int done, int total, Kind kind, QString label = {});
  [[nodiscard]] State state() const;

private:
  mutable std::mutex mutex_;
  State state_;
  std::function<void()> pump_;
};

// Makes `progress` the one report_save_progress reaches on this thread, for writers deep
// in the call chain (the PDF page loops) that have no progress parameter.
class ScopedSaveProgress {
public:
  explicit ScopedSaveProgress(SaveProgress& progress) noexcept;
  ~ScopedSaveProgress();
  ScopedSaveProgress(const ScopedSaveProgress&) = delete;
  ScopedSaveProgress& operator=(const ScopedSaveProgress&) = delete;

private:
  SaveProgress* previous_{nullptr};
};

// Reports to this thread's ScopedSaveProgress, if any.
void report_save_progress(int done, int total, SaveProgress::Kind kind, const QString& label = {});

}  // namespace patchy::ui
