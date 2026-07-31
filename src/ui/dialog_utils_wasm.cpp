#include "ui/dialog_utils_wasm.hpp"

#include <emscripten.h>
#include <emscripten/val.h>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Opens the browser file chooser through our own <input type=file>. Everything
// that happens between the click and the polled result is pure page-side JS
// writing into a plain JS object: while the C++ side blocks in a nested event
// loop the wasm is Asyncify-suspended, and a JS callback that called into the
// module there would be swallowed (Qt's own getOpenFileContent promise chain
// stalls exactly that way, which is why it is not used here). Qt timers, by
// contrast, resume the suspended loop reliably, so the C++ side polls.
EM_JS(void, patchy_js_open_file_picker, (const char* accept, int generation), {
  const acceptStr = UTF8ToString(accept);
  const state = {generation : generation, done : false, cancelled : false, name : null, bytes : null, error : null};
  window.__patchyPickState = state;
  const input = document.createElement("input");
  input.type = "file";
  if (acceptStr) {
    input.accept = acceptStr;
  }
  input.addEventListener("change", () => {
    const file = input.files && input.files[0];
    if (!file) {
      state.cancelled = true;
      state.done = true;
      return;
    }
    file.arrayBuffer().then((buf) => {
      if (window.__patchyPickState !== state) {
        return;
      }
      state.name = file.name;
      state.bytes = new Uint8Array(buf);
      state.done = true;
    }, (err) => {
      state.error = String(err);
      state.cancelled = true;
      state.done = true;
    });
  });
  input.addEventListener("cancel", () => {
    if (!state.done) {
      state.cancelled = true;
      state.done = true;
    }
  });
  input.click();
});

// Page-side drop target for files dragged in from the desktop. Reading the
// dropped Files is pure JS writing into a plain JS queue; the C++ side drains
// that queue from a QTimer (install_web_drop_target). The JS must never call a
// wasm export directly from the promise callback: a raw JS->wasm entry runs
// outside Qt's suspend-resume control, and when the open path it triggers
// suspends again (the open progress dialog's nested event loop) while the
// suspended main loop has a resume already in flight (any live timer of an
// open document), the single-slot Asyncify state is clobbered and the main
// thread's continuation is silently lost - the app parks forever with no
// console error. A Qt timer resumes the main loop first and runs the handler
// inside it, which is the same reliable shape the file picker uses.
EM_JS(void, patchy_js_install_drop_target, (), {
  if (window.__patchyDropQueue) {
    return;
  }
  window.__patchyDropQueue = [];
  const target = document.documentElement;
  const carriesFiles = (ev) =>
      ev.dataTransfer && Array.from(ev.dataTransfer.types || []).includes("Files");
  target.addEventListener("dragover", (ev) => {
    if (!carriesFiles(ev)) {
      return;
    }
    ev.preventDefault();
    ev.dataTransfer.dropEffect = "copy";
  });
  target.addEventListener("drop", (ev) => {
    if (!carriesFiles(ev)) {
      return;
    }
    ev.preventDefault();
    const files = Array.from(ev.dataTransfer.files || []);
    if (files.length === 0) {
      return;
    }
    const reads = files.map((file) => file.arrayBuffer().then(
        (buf) => ({name : file.name, bytes : new Uint8Array(buf)})));
    Promise.all(reads).then((entries) => {
      window.__patchyDropQueue.push(...entries);
    }, () => {});
  });
});

namespace patchy::ui::wasm_files {
namespace {

struct FilterRow {
  QString row;           // the verbatim row, returned through *selected_filter
  QStringList patterns;  // "*.psd"-style tokens from the row's last group
};

// Splits a ";;"-joined Qt dialog filter. Per the repo's open-filter contract
// (docs/file-formats.md), the machine-readable patterns are the LAST
// parenthesized group of each row; "*.*"/"*" rows carry no browser filter.
std::vector<FilterRow> parse_filter_rows(const QString& filter) {
  std::vector<FilterRow> rows;
  const auto row_strings = filter.split(QStringLiteral(";;"), Qt::SkipEmptyParts);
  for (const auto& row : row_strings) {
    const auto open = row.lastIndexOf(QLatin1Char('('));
    const auto close = row.lastIndexOf(QLatin1Char(')'));
    if (open < 0 || close <= open) {
      continue;
    }
    FilterRow parsed;
    parsed.row = row;
    const auto pattern_text = row.mid(open + 1, close - open - 1);
    const auto patterns =
        pattern_text.split(QRegularExpression(QStringLiteral("[\\s;]+")), Qt::SkipEmptyParts);
    for (const auto& pattern : patterns) {
      if (pattern == QStringLiteral("*") || pattern == QStringLiteral("*.*")) {
        continue;
      }
      parsed.patterns.push_back(pattern);
    }
    rows.push_back(std::move(parsed));
  }
  return rows;
}

// The browser chooser has no format dropdown, so every row's patterns collapse
// into one accept attribute (".psd,.png,..."). Empty leaves it unrestricted.
QString accept_attribute_for_filter(const QString& filter) {
  QStringList extensions;
  for (const auto& row : parse_filter_rows(filter)) {
    for (const auto& pattern : row.patterns) {
      auto extension = pattern;
      extension.remove(QLatin1Char('*'));
      if (extension.startsWith(QLatin1Char('.')) && !extensions.contains(extension)) {
        extensions.push_back(extension);
      }
    }
  }
  return extensions.join(QLatin1Char(','));
}

// Numbered per-transfer MEMFS directories keep original basenames intact (the
// pipeline dispatches on the extension and shows the name in titles/recents)
// while making same-name transfers collision-free.
QString next_transfer_directory(const char* root) {
  static int counter = 0;
  ++counter;
  return QStringLiteral("%1/%2").arg(QLatin1String(root)).arg(counter);
}

QString sanitized_file_name(QString name) {
  name.replace(QLatin1Char('\\'), QLatin1Char('_'));
  name.replace(QLatin1Char('/'), QLatin1Char('_'));
  name = name.trimmed();
  return name;
}

// Copies one JS {name, bytes} transfer into MEMFS under `root` and returns
// the new path ("" on any failure). The typed_memory_view target is written
// before anything else can grow the wasm heap.
QString materialize_transfer(const emscripten::val& entry, const char* root) {
  const auto base_name = sanitized_file_name(QString::fromStdString(entry["name"].as<std::string>()));
  if (base_name.isEmpty()) {
    return {};
  }
  const auto js_bytes = entry["bytes"];
  const auto length = js_bytes["length"].as<unsigned>();
  QByteArray content(static_cast<qsizetype>(length), Qt::Uninitialized);
  if (length > 0) {
    auto heap_view = emscripten::val(emscripten::typed_memory_view(
        static_cast<std::size_t>(length), reinterpret_cast<std::uint8_t*>(content.data())));
    heap_view.call<void>("set", js_bytes);
  }
  const auto dir = next_transfer_directory(root);
  if (!QDir().mkpath(dir)) {
    return {};
  }
  const auto path = dir + QLatin1Char('/') + base_name;
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
    qWarning("wasm file transfer: could not materialize %s", path.toUtf8().constData());
    return {};
  }
  return path;
}

std::function<void(const QString&)>& web_drop_handler() {
  static std::function<void(const QString&)> handler;
  return handler;
}

}  // namespace

QString pick_open_file(QWidget* parent, const QString& caption, const QString& filter) {
  // One picker at a time: a second request would nest a second Asyncify
  // suspend inside the first, which hangs the runtime.
  static bool picker_active = false;
  if (picker_active) {
    return {};
  }
  picker_active = true;
  struct ActiveReset {
    ~ActiveReset() { picker_active = false; }
  } active_reset;

  static int generation = 0;
  ++generation;
  const auto accept = accept_attribute_for_filter(filter).toUtf8();
  patchy_js_open_file_picker(accept.constData(), generation);

  // The modal is the universal escape hatch: a cancelled chooser fires no
  // event on older engines, and a click refused without user activation fires
  // nothing anywhere. The native chooser is window-modal in practice, so this
  // dialog is rarely even seen; a pick closes it automatically below.
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("browserFilePickerWaitDialog"));
  dialog.setWindowTitle(caption);
  dialog.setModal(true);
  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(new QLabel(QObject::tr("Waiting for the browser file picker..."), &dialog));
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);

  QString picked_path;
  QTimer poll_timer(&dialog);
  poll_timer.setInterval(100);
  QObject::connect(&poll_timer, &QTimer::timeout, &dialog, [&dialog, &picked_path] {
    const auto state = emscripten::val::global("window")["__patchyPickState"];
    if (state.isUndefined() || state.isNull() || !state["done"].as<bool>()) {
      return;
    }
    if (!state["cancelled"].as<bool>() && !state["bytes"].isNull()) {
      picked_path = materialize_transfer(state, "/opened");
    }
    dialog.accept();
  });
  poll_timer.start();
  dialog.exec();
  // Drop the state so a pick landing after a manual cancel is ignored (the
  // page-side handler also checks it before storing bytes).
  emscripten::val::global("window").set("__patchyPickState", emscripten::val::undefined());
  return picked_path;
}

QString prompt_save_file(QWidget* parent, const QString& caption, const QString& initial_path,
                         const QString& filter, QString* selected_filter) {
  const auto rows = parse_filter_rows(filter);
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("wasmSaveFileDialog"));
  dialog.setWindowTitle(caption);
  dialog.setModal(true);
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  auto* name_edit = new QLineEdit(&dialog);
  name_edit->setObjectName(QStringLiteral("wasmSaveFileNameEdit"));
  name_edit->setText(QFileInfo(initial_path).fileName());
  name_edit->selectAll();
  form->addRow(QObject::tr("File name:"), name_edit);
  auto* format_combo = new QComboBox(&dialog);
  format_combo->setObjectName(QStringLiteral("wasmSaveFormatCombo"));
  for (const auto& row : rows) {
    format_combo->addItem(row.row);
  }
  if (selected_filter != nullptr && !selected_filter->isEmpty()) {
    const auto preselect = format_combo->findText(*selected_filter);
    if (preselect >= 0) {
      format_combo->setCurrentIndex(preselect);
    }
  }
  if (!rows.empty()) {
    form->addRow(QObject::tr("Format:"), format_combo);
  } else {
    format_combo->hide();
  }
  layout->addLayout(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  auto* save_button = buttons->button(QDialogButtonBox::Save);
  const auto update_save_enabled = [save_button, name_edit] {
    save_button->setEnabled(!sanitized_file_name(name_edit->text()).isEmpty());
  };
  QObject::connect(name_edit, &QLineEdit::textChanged, &dialog, update_save_enabled);
  update_save_enabled();
  name_edit->setFocus();

  if (dialog.exec() != QDialog::Accepted) {
    return {};
  }
  const auto file_name = sanitized_file_name(name_edit->text());
  if (file_name.isEmpty()) {
    return {};
  }
  if (selected_filter != nullptr && format_combo->count() > 0) {
    *selected_filter = format_combo->currentText();
  }
  const auto dir = next_transfer_directory("/saved");
  if (!QDir().mkpath(dir)) {
    return {};
  }
  return dir + QLatin1Char('/') + file_name;
}

// Drains the page-side drop queue. Runs from the poll timer below, so the
// call chain sits inside the resumed main loop and the open path may nest
// event loops safely (see the comment on patchy_js_install_drop_target).
void drain_web_drop_queue() {
  // The handler can run a nested event loop (open progress dialog), which
  // keeps the poll timer firing; do not re-enter a drain mid-open.
  static bool draining = false;
  if (draining) {
    return;
  }
  draining = true;
  struct DrainingReset {
    ~DrainingReset() { draining = false; }
  } draining_reset;

  auto& handler = web_drop_handler();
  auto queue = emscripten::val::global("window")["__patchyDropQueue"];
  if (!handler || queue.isUndefined() || queue.isNull()) {
    return;
  }
  if (QApplication::activeModalWidget() != nullptr) {
    // Desktop parity: a modal dialog blocks dropping files onto the window.
    queue.set("length", 0);
    return;
  }
  while (queue["length"].as<unsigned>() > 0) {
    const auto entry = queue.call<emscripten::val>("shift");
    const auto path = materialize_transfer(entry, "/dropped");
    if (!path.isEmpty()) {
      handler(path);
    }
  }
}

void install_web_drop_target(std::function<void(const QString& path)> open_dropped_path) {
  web_drop_handler() = std::move(open_dropped_path);
  patchy_js_install_drop_target();
  static QTimer* poll_timer = nullptr;
  if (poll_timer == nullptr) {
    poll_timer = new QTimer(QCoreApplication::instance());
    poll_timer->setInterval(250);
    QObject::connect(poll_timer, &QTimer::timeout, poll_timer, [] { drain_web_drop_queue(); });
    poll_timer->start();
  }
}

void download_file_in_browser(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning("wasm download: could not reopen %s", path.toUtf8().constData());
    return;
  }
  const auto bytes = file.readAll();
  // new Uint8Array(view) copies out of the wasm heap immediately, so the view
  // cannot be invalidated by later heap growth; Blob then owns JS-side bytes.
  const auto view = emscripten::val(emscripten::typed_memory_view(
      static_cast<std::size_t>(bytes.size()), reinterpret_cast<const std::uint8_t*>(bytes.constData())));
  auto copied = emscripten::val::global("Uint8Array").new_(view);
  auto parts = emscripten::val::array();
  parts.call<void>("push", copied);
  auto blob = emscripten::val::global("Blob").new_(parts);
  auto url = emscripten::val::global("URL").call<emscripten::val>("createObjectURL", blob);
  auto document = emscripten::val::global("document");
  auto anchor = document.call<emscripten::val>("createElement", emscripten::val("a"));
  anchor.set("href", url);
  anchor.set("download", emscripten::val(QFileInfo(path).fileName().toStdString()));
  anchor["style"].set("display", emscripten::val("none"));
  auto body = document["body"];
  body.call<void>("appendChild", anchor);
  anchor.call<void>("click");
  body.call<void>("removeChild", anchor);
  emscripten::val::global("URL").call<void>("revokeObjectURL", url);
}

}  // namespace patchy::ui::wasm_files
