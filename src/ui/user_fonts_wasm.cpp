#include "ui/user_fonts_wasm.hpp"

#include <emscripten.h>
#include <emscripten/val.h>

#include <QCoreApplication>
#include <QTimer>

#include <cstdint>
#include <string>

// IndexedDB persistence for user-added fonts (DB "PatchyUserFonts", store
// "fonts" keyed by file name). Every function here follows the pinned wasm
// platform rule (docs/wasm.md): the page-side callbacks only write plain JS
// state or the database and never call a wasm export - the C++ side polls
// from a QTimer, which runs inside the resumed main loop where nesting is
// safe.

// Reads every stored font into window.__patchyUserFontLoad
// ({done, error, entries: [{name, bytes}]}); the C++ poll below drains it.
EM_JS(void, patchy_js_user_fonts_begin_load, (), {
  if (window.__patchyUserFontLoad) {
    return;
  }
  const state = {done : false, error : null, entries : []};
  window.__patchyUserFontLoad = state;
  const finish = (err) => {
    if (err) {
      state.error = String(err);
    }
    state.done = true;
  };
  if (!window.indexedDB) {
    finish("IndexedDB unavailable");
    return;
  }
  try {
    const open = indexedDB.open("PatchyUserFonts", 1);
    open.onupgradeneeded = () => { open.result.createObjectStore("fonts", {keyPath : "name"}); };
    open.onerror = () => finish(open.error);
    open.onsuccess = () => {
      const db = open.result;
      let request;
      try {
        request = db.transaction("fonts", "readonly").objectStore("fonts").getAll();
      } catch (err) {
        db.close();
        finish(err);
        return;
      }
      request.onerror = () => {
        db.close();
        finish(request.error);
      };
      request.onsuccess = () => {
        for (const row of request.result || []) {
          if (row && typeof row.name === "string" && row.bytes) {
            state.entries.push({name : row.name, bytes : new Uint8Array(row.bytes)});
          }
        }
        db.close();
        finish(null);
      };
    };
  } catch (err) {
    finish(err);
  }
});

EM_JS(void, patchy_js_user_font_put, (const char* name, const std::uint8_t* data, int size), {
  // Copy out of the wasm heap FIRST: the heap view dies on heap growth, and
  // on the multithreaded build it is a SharedArrayBuffer view, which
  // IndexedDB's structured clone refuses outright.
  const bytes = new Uint8Array(size);
  bytes.set(HEAPU8.subarray(data, data + size));
  const fontName = UTF8ToString(name);
  if (!window.indexedDB) {
    return;
  }
  const warn = (err) => console.warn("Patchy user fonts: could not save", fontName, err);
  try {
    const open = indexedDB.open("PatchyUserFonts", 1);
    open.onupgradeneeded = () => { open.result.createObjectStore("fonts", {keyPath : "name"}); };
    open.onerror = () => warn(open.error);
    open.onsuccess = () => {
      const db = open.result;
      try {
        const tx = db.transaction("fonts", "readwrite");
        tx.objectStore("fonts").put({name : fontName, bytes : bytes});
        tx.oncomplete = () => db.close();
        tx.onerror = () => {  // quota errors land here
          warn(tx.error);
          db.close();
        };
      } catch (err) {
        db.close();
        warn(err);
      }
    };
  } catch (err) {
    warn(err);
  }
});

EM_JS(void, patchy_js_user_fonts_clear, (), {
  if (!window.indexedDB) {
    return;
  }
  try {
    const open = indexedDB.open("PatchyUserFonts", 1);
    open.onupgradeneeded = () => { open.result.createObjectStore("fonts", {keyPath : "name"}); };
    open.onerror = () => {};
    open.onsuccess = () => {
      const db = open.result;
      try {
        const tx = db.transaction("fonts", "readwrite");
        tx.objectStore("fonts").clear();
        tx.oncomplete = () => db.close();
        tx.onerror = () => db.close();
      } catch (err) {
        db.close();
      }
    };
  } catch (err) {
  }
});

namespace patchy::ui::user_fonts::wasm_store {

void put(const QString& name, const QByteArray& bytes) {
  const auto name_utf8 = name.toUtf8();
  patchy_js_user_font_put(name_utf8.constData(),
                          reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                          static_cast<int>(bytes.size()));
}

void clear() { patchy_js_user_fonts_clear(); }

void begin_restore() {
  patchy_js_user_fonts_begin_load();
  auto* poll_timer = new QTimer(QCoreApplication::instance());
  poll_timer->setInterval(250);
  QObject::connect(poll_timer, &QTimer::timeout, poll_timer, [poll_timer] {
    auto window = emscripten::val::global("window");
    const auto state = window["__patchyUserFontLoad"];
    if (state.isUndefined() || state.isNull()) {
      poll_timer->stop();
      poll_timer->deleteLater();
      return;
    }
    if (!state["done"].as<bool>()) {
      return;
    }
    if (!state["error"].isNull() && !state["error"].isUndefined()) {
      qWarning("wasm user fonts: restore failed: %s",
               state["error"].as<std::string>().c_str());
    }
    const auto entries = state["entries"];
    const auto count = entries["length"].as<unsigned>();
    for (unsigned index = 0; index < count; ++index) {
      const auto entry = entries[index];
      const auto name = QString::fromStdString(entry["name"].as<std::string>());
      const auto js_bytes = entry["bytes"];
      const auto length = js_bytes["length"].as<unsigned>();
      QByteArray bytes(static_cast<qsizetype>(length), Qt::Uninitialized);
      if (length > 0) {
        // The typed_memory_view target is written before anything else can
        // grow the wasm heap (same rule as materialize_transfer).
        auto heap_view = emscripten::val(emscripten::typed_memory_view(
            static_cast<std::size_t>(length), reinterpret_cast<std::uint8_t*>(bytes.data())));
        heap_view.call<void>("set", js_bytes);
      }
      register_restored_font(name, bytes);
    }
    // Release the JS-side bytes; begin_restore runs once per boot.
    window.set("__patchyUserFontLoad", emscripten::val::undefined());
    poll_timer->stop();
    poll_timer->deleteLater();
  });
  poll_timer->start();
}

}  // namespace patchy::ui::user_fonts::wasm_store
