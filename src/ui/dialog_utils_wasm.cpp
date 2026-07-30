#include "ui/dialog_utils_wasm.hpp"

#include <emscripten.h>
#include <emscripten/val.h>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
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
// dropped Files is pure JS; only after every byte sits in a plain JS queue
// does it call back into the module (the app idles in its main loop at drop
// time, so the call-in is an ordinary event entry).
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
      _patchy_wasm_drops_ready();
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

void install_web_drop_target(std::function<void(const QString& path)> open_dropped_path) {
  web_drop_handler() = std::move(open_dropped_path);
  patchy_js_install_drop_target();
}

// Called from the EM_JS drop handler once every dropped file's bytes sit in
// the page-side queue; runs as an ordinary JS->wasm event entry while the app
// idles in its main loop.
extern "C" EMSCRIPTEN_KEEPALIVE void patchy_wasm_drops_ready() {
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

// --------------------------------------------------------------------------
// Dialog combo popup workaround (see the header comment).

namespace {

QComboBox* combo_box_for(QObject* object) {
  for (auto* current = object; current != nullptr; current = current->parent()) {
    if (auto* combo = qobject_cast<QComboBox*>(current)) {
      return combo;
    }
    if (qobject_cast<QWidget*>(current) == nullptr) {
      return nullptr;
    }
  }
  return nullptr;
}

// Combos whose window is the main window keep the native popup: those work.
bool combo_needs_chooser(const QComboBox& combo) {
  return combo.isEnabled() && combo.count() > 0 &&
         qobject_cast<QMainWindow*>(combo.window()) == nullptr;
}

bool press_would_open_popup(QComboBox& combo, QPoint combo_position) {
  if (!combo.rect().contains(combo_position)) {
    return false;
  }
  if (!combo.isEditable()) {
    return true;
  }
  QStyleOptionComboBox option;
  option.initFrom(&combo);
  option.editable = true;
  option.subControls = QStyle::SC_All;
  return combo.style()->hitTestComplexControl(QStyle::CC_ComboBox, &option, combo_position,
                                              &combo) == QStyle::SC_ComboBoxArrow;
}

bool key_would_open_popup(const QComboBox& combo, const QKeyEvent& key) {
  switch (key.key()) {
    case Qt::Key_F4:
      return true;
    case Qt::Key_Space:
      return !combo.isEditable();
    case Qt::Key_Up:
    case Qt::Key_Down:
      return (key.modifiers() & Qt::AltModifier) != 0;
    default:
      return false;
  }
}

// The chooser is a plain child widget inside the dialog's own window, never a
// new top-level window: every kind of top-level shown from a dialog context
// is broken on this platform (the native popup paints but takes no input; a
// dialog-parented or even parentless chooser dialog took no input and did not
// paint). Child widgets inside the already-working dialog window both paint
// and receive input.
class DialogComboChooser final : public QListWidget {
public:
  DialogComboChooser(QComboBox& combo, QWidget* host)
      : QListWidget(host), combo_(&combo) {
    setObjectName(QStringLiteral("wasmComboChooserList"));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (int index = 0; index < combo.count(); ++index) {
      addItem(new QListWidgetItem(combo.itemIcon(index), combo.itemText(index)));
    }
    setCurrentRow(combo.currentIndex());
    connect(this, &QListWidget::itemClicked, this, [this] { pick(currentRow()); });
    connect(this, &QListWidget::itemActivated, this, [this] { pick(currentRow()); });

    // Popup-like sizing and placement, clamped to the host window.
    const auto host_rect = host->rect();
    const auto row_height = std::max(1, sizeHintForRow(0));
    const auto frame = 2 * (frameWidth() + 1);
    const auto height = std::min(row_height * combo.count() + frame,
                                 std::max(row_height + frame, host_rect.height() - 8));
    const auto width =
        std::clamp(std::max(combo.width(), sizeHintForColumn(0) + frame +
                                               verticalScrollBar()->sizeHint().width()),
                   60, std::max(60, host_rect.width() - 8));
    resize(width, height);
    auto position = combo.mapTo(host, QPoint(0, combo.height()));
    position.setX(std::clamp(position.x(), 0, std::max(0, host_rect.width() - width)));
    if (position.y() + height > host_rect.height()) {
      position.setY(std::max(0, combo.mapTo(host, QPoint(0, 0)).y() - height));
    }
    move(position);
    raise();
    show();
    setFocus();
    scrollToItem(currentItem());
  }

  void dismiss() {
    hide();
    deleteLater();
  }

  // True when a global-position press lands inside the chooser; presses
  // outside dismiss it, matching popup semantics.
  [[nodiscard]] bool contains_global(QPoint global_position) const {
    return rect().contains(mapFromGlobal(global_position));
  }

private:
  void pick(int row) {
    if (combo_ != nullptr && row >= 0 && row < combo_->count()) {
      if (row != combo_->currentIndex()) {
        combo_->setCurrentIndex(row);
      }
      // A real popup pick reports through activated/textActivated (several
      // dialogs act only on those, e.g. "Custom color..." rows); signals
      // cannot be emitted from outside the class, so raise them by metacall.
      QMetaObject::invokeMethod(combo_, "activated", Q_ARG(int, row));
      QMetaObject::invokeMethod(combo_, "textActivated",
                                Q_ARG(QString, combo_->itemText(row)));
    }
    dismiss();
  }

  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Escape) {
      dismiss();
      return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      pick(currentRow());
      return;
    }
    QListWidget::keyPressEvent(event);
  }

  QPointer<QComboBox> combo_;
};

class DialogComboPopupFilter final : public QObject {
public:
  using QObject::QObject;

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonPress) {
      const auto global_position =
          static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
      if (chooser_ != nullptr) {
        // Presses inside the open chooser flow to it normally; the first
        // press outside dismisses it and is consumed, like a real popup.
        if (chooser_->contains_global(global_position)) {
          return false;
        }
        chooser_->dismiss();
        return true;
      }
      auto* combo = combo_box_for(watched);
      if (combo == nullptr || !combo_needs_chooser(*combo)) {
        return false;
      }
      if (!press_would_open_popup(*combo, combo->mapFromGlobal(global_position))) {
        return false;
      }
      chooser_ = new DialogComboChooser(*combo, combo->window());
      return true;
    }
    if (event->type() == QEvent::KeyPress) {
      if (chooser_ != nullptr) {
        return false;  // the focused chooser handles its own keys
      }
      auto* combo = combo_box_for(watched);
      if (combo == nullptr || !combo_needs_chooser(*combo) ||
          !key_would_open_popup(*combo, *static_cast<QKeyEvent*>(event))) {
        return false;
      }
      chooser_ = new DialogComboChooser(*combo, combo->window());
      return true;
    }
    return false;
  }

private:
  QPointer<DialogComboChooser> chooser_;
};

}  // namespace

void install_wasm_dialog_combo_workaround() {
  static DialogComboPopupFilter* filter = nullptr;
  if (filter != nullptr) {
    return;
  }
  auto* app = QCoreApplication::instance();
  filter = new DialogComboPopupFilter(app);
  app->installEventFilter(filter);
}

}  // namespace patchy::ui::wasm_files
