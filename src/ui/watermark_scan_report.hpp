#pragma once

// The per-kind results of Scan for Watermarks (docs/watermarks.md, "Scan results"): one row
// for every watermark kind Patchy checks, in a fixed order, each Found, Not found or Not
// checked with the reason. File Info's Watermarks page and doc.scanForWatermarks() both read
// it. Detection only: nothing here removes a watermark (docs/legal-constraints.md,
// "Watermark detection").

#include "ai/trustmark_watermark.hpp"
#include "core/document_file_info.hpp"
#include "core/invisible_watermark.hpp"
#include "core/pixel_buffer.hpp"

#include <QString>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace patchy::ui {

enum class WatermarkCheckStatus : std::uint8_t { Found, NotFound, NotChecked };

// One kind of watermark. `id`, the status token and `reason` are permanent scripting
// identifiers.
struct WatermarkCheck {
  // "stableDiffusion1", "stableDiffusion2", "stableDiffusionXL", "trustmarkQ", "trustmarkP",
  // "trustmarkC", "c2paDeclared", in that order.
  QString id;
  QString name;  // display name: product names stay untranslated
  WatermarkCheckStatus status{WatermarkCheckStatus::NotFound};
  // NotChecked only: "imageTooSmall", "modelsNotDownloaded", "notSupported" (this build
  // cannot run TrustMark) or "failed".
  QString reason;
  QString detail;  // translated: what was found, or why the kind was not checked
  std::optional<InvisibleWatermarkMatch> match;  // Stable Diffusion rows, when found
  std::optional<ai::TrustMarkMark> mark;         // TrustMark rows, when found
  std::vector<std::string> declarations;         // the C2PA row: each declaration's summary
};

struct WatermarkScanReport {
  InvisibleWatermarkScan stable_diffusion;
  std::optional<ai::TrustMarkScan> trustmark;  // set when the TrustMark decoders ran
  std::string trustmark_error;                 // set when they started and failed
  std::vector<WatermarkCheck> checks;

  [[nodiscard]] bool any_found() const noexcept;
};

// The rows for finished scans. `trustmark` is nullopt when TrustMark did not run; then
// `trustmark_error` (non-empty when it failed) or the runtime and model availability give
// the reason. `observed` is the file's FileInfoObserved list (its watermark declarations).
[[nodiscard]] std::vector<WatermarkCheck> watermark_checks(const InvisibleWatermarkScan& stable_diffusion,
                                                           const std::optional<ai::TrustMarkScan>& trustmark,
                                                           const std::string& trustmark_error,
                                                           const std::vector<FileInfoObserved>& observed);

// Scans an 8-bit flatten (Compositor::flatten_rgb8) for the Stable Diffusion marks and, when
// the watermark models are downloaded, TrustMark (a TrustMark failure lands in
// trustmark_error), then fills `checks`. Throws InferenceCancelled on cancel.
[[nodiscard]] WatermarkScanReport scan_watermarks(const PixelBuffer& rgb8, const std::vector<FileInfoObserved>& observed,
                                                  ai::InferenceCancel* cancel = nullptr);

[[nodiscard]] QString watermark_check_status_text(WatermarkCheckStatus status);   // "Found", ...
[[nodiscard]] QString watermark_check_status_token(WatermarkCheckStatus status);  // "found", "notFound", "notChecked"
// What no public tool can check, shown under the results so a column of Not found is not
// read as "no watermark".
[[nodiscard]] QString watermark_scan_limits_note();

}  // namespace patchy::ui
