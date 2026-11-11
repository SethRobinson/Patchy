#pragma once

#include "core/document.hpp"
#include "core/layer.hpp"

#include <span>
#include <string>
#include <string_view>

// Layer > Export > Copy CSS (GitHub issue 688): the selected layers as CSS classes, one
// per layer. Qt-free so the core tests pin the text. What maps to what:
// docs/web-export-copy-css.md.
namespace patchy::css {

struct LayerCss {
  // The whole text (a comment naming the document, then one class per layer in panel
  // order, top first). Empty when no listed layer gave a class.
  std::string text;
  int classes{0};
  // Listed layers left out: hidden ones (their own eye or a folder's), and layers with
  // nothing to measure (no pixels, an adjustment layer, an empty folder).
  int hidden_skipped{0};
  int empty_skipped{0};
};

// One class per listed layer that is visible and has an extent. A folder gives its
// contents' bounds; its layers get classes only when listed themselves. Each class's
// z-index is the layer's row in the whole panel from the bottom (1), every layer counted, so
// it does not depend on which layers are listed. Output is
// deterministic: the same document and ids give the same bytes on every platform.
[[nodiscard]] LayerCss layers_to_css(const Document& document, std::span<const LayerId> layer_ids,
                                     std::string_view document_name);

// The class name a layer name becomes, before deduplication: ASCII letters lowercased,
// digits, '-' and '_' kept, non-ASCII characters kept as they are (CSS identifiers allow
// them), everything else (spaces included) a single '-'; a leading digit gets "layer-"
// in front and a name with nothing left is "layer".
[[nodiscard]] std::string class_name_from_layer_name(std::string_view layer_name);

}  // namespace patchy::css
