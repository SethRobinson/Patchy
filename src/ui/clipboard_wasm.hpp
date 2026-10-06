#pragma once

class QImage;

// Browser clipboard writes for the WebAssembly build (docs/clipboard.md).
//
// Qt 6.10.3's wasm clipboard backend serializes an image to PNG and hands it to
// navigator.clipboard.write() inside a Blob built by qstdweb's
// Blob::fromArrayBuffer, which sets no MIME type. Chromium rejects a
// ClipboardItem whose Blob type does not match its key ("Type image/png does
// not match the blob's type"), Qt only logs the rejection, and the copy never
// reaches the system clipboard in Chrome, Brave, or Edge (October 2026, the
// 1.06 web build). Patchy therefore writes the PNG itself with a typed Blob.
// Only compiled for Emscripten.
namespace patchy::ui::wasm_clipboard {

// Encodes `image` as PNG and writes it to the browser clipboard through the
// async Clipboard API. Returns immediately; a refusal (insecure context,
// missing user activation, denied permission) is reported on the browser
// console only. Call from the user gesture that triggered the copy so the
// browser's transient activation still covers the write.
void write_image_png(const QImage& image);

}  // namespace patchy::ui::wasm_clipboard
