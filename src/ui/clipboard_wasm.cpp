#include "ui/clipboard_wasm.hpp"

#include <emscripten.h>
#include <emscripten/val.h>

#include <QBuffer>
#include <QByteArray>
#include <QImage>

#include <cstddef>
#include <cstdint>

// Installed once; takes a JS-owned Uint8Array (already copied out of the wasm
// heap by the caller) and writes it as an image/png ClipboardItem. The Blob
// carries its MIME type, which is the whole difference from Qt's own write.
// Failures only warn: the copy already succeeded into Patchy's internal
// clipboard, and the browser decides whether a page may write (secure context,
// transient user activation, clipboard-write permission).
EM_JS(void, patchy_js_install_clipboard_helpers, (), {
  if (window.__patchyWriteClipboardPng) {
    return;
  }
  window.__patchyWriteClipboardPng = (pngBytes) => {
    if (typeof navigator === "undefined" || !navigator.clipboard ||
        typeof navigator.clipboard.write !== "function" || typeof ClipboardItem === "undefined") {
      console.warn("Patchy: this browser has no async clipboard write; image copy stays inside Patchy");
      return;
    }
    let item;
    try {
      const blob = new Blob([pngBytes], { type: "image/png" });
      item = new ClipboardItem({ "image/png": blob });
    } catch (error) {
      console.warn("Patchy: could not build the clipboard item:", error);
      return;
    }
    navigator.clipboard.write([item]).catch((error) => {
      console.warn("Patchy: the browser refused the image clipboard write:",
                   error && error.name, error && error.message);
    });
  };
});

namespace patchy::ui::wasm_clipboard {

void write_image_png(const QImage& image) {
  if (image.isNull()) {
    return;
  }
  QByteArray png;
  QBuffer buffer(&png);
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG") || png.isEmpty()) {
    return;
  }
  patchy_js_install_clipboard_helpers();
  // new Uint8Array(view) copies out of the wasm heap right away: the QByteArray
  // dies when this function returns, and Blob rejects a view over the
  // threaded build's SharedArrayBuffer-backed heap.
  const auto view = emscripten::val(emscripten::typed_memory_view(
      static_cast<std::size_t>(png.size()), reinterpret_cast<const std::uint8_t*>(png.constData())));
  auto copied = emscripten::val::global("Uint8Array").new_(view);
  emscripten::val::global("window")["__patchyWriteClipboardPng"](copied);
}

}  // namespace patchy::ui::wasm_clipboard
