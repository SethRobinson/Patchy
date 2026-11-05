#pragma once

#include <string>

namespace patchy {

class Document;

enum class GpuDocumentRenderMode {
  Unsupported,
  PixelStackSourceOver,
  PixelStackShader,
};

struct GpuDocumentCapability {
  GpuDocumentRenderMode mode{GpuDocumentRenderMode::Unsupported};
  std::string reason;

  [[nodiscard]] bool supported() const noexcept {
    return mode != GpuDocumentRenderMode::Unsupported;
  }
};

// Returns the most conservative render mode for the complete document. GPU
// composition is all-or-nothing: a document with one unsupported feature must
// stay on the CPU compositor instead of mixing an approximation into the stack.
[[nodiscard]] GpuDocumentCapability gpu_document_capability(const Document& document);

}  // namespace patchy
