#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Read-only summary of a JUMBF box tree (ISO 19566-5), the container C2PA Content
// Credentials use in JPEG APP11, PNG caBX, WebP C2PA and BMFF uuid boxes
// (docs/file-info.md). Patchy never verifies or writes manifests; File Info only
// shows what a file claims about its origin.
namespace patchy::metadata {

struct JumbfSummary {
  std::vector<std::string> labels;  // description-box labels in tree order ("c2pa", "c2pa.actions", ...)
  std::string claim_generator;      // from the active claim, when present
  std::vector<std::string> actions; // "c2pa.created (trainedAlgorithmicMedia)", ...
  std::string digital_source_type;  // first digitalSourceType seen in the actions
  // Watermarks the manifest declares: each c2pa.soft-binding assertion's "alg" (for
  // example "com.adobe.trustmark.Q"), and actions named c2pa.watermarked*.
  std::vector<std::string> soft_bindings;
  std::vector<std::string> watermark_actions;
  [[nodiscard]] bool declares_watermark() const noexcept {
    return !soft_bindings.empty() || !watermark_actions.empty();
  }
};

[[nodiscard]] JumbfSummary summarize_jumbf(std::span<const std::uint8_t> bytes);

// Compact text rendering of one CBOR item (maps as {k: v}, byte strings as sizes),
// capped at `max_chars`. Exposed for tests.
[[nodiscard]] std::string cbor_to_text(std::span<const std::uint8_t> bytes, std::size_t max_chars = 4096);

}  // namespace patchy::metadata
