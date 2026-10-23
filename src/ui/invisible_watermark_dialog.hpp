#pragma once

#include "core/trustmark_payload.hpp"

#include <QString>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

// Image > Add Invisible Watermark (docs/watermarks.md): an Adobe TrustMark payload for the
// active layer. TrustMark has no key, so the dialog says plainly that anyone can read it.
struct InvisibleWatermarkRequest {
  trustmark::Schema schema{trustmark::Schema::Bch5};
  std::vector<std::uint8_t> data_bits;  // the payload, schema_data_bits(schema) bits
  QString text;                         // the typed text, empty for a random ID
  float strength{1.0F};                 // TrustMark's WM_STRENGTH (1.0 is its default)
};

struct InvisibleWatermarkDialogSupport {
  // True when the watermark models are present.
  std::function<bool()> models_available;
  // Downloads the watermark models with progress over `parent`; true when present after.
  std::function<bool(QWidget* parent)> download_models;
};

// The payload of a random ID: `bits` random bits (QRandomGenerator::system).
[[nodiscard]] std::vector<std::uint8_t> random_watermark_id_bits(int bits);

// nullopt on Cancel.
[[nodiscard]] std::optional<InvisibleWatermarkRequest> request_invisible_watermark(
    QWidget* parent, const InvisibleWatermarkDialogSupport& support);

}  // namespace patchy::ui
