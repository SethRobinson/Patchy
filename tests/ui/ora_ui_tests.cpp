// OpenRaster in the app (docs/ora.md): opening keeps the file's layers and selected layer,
// a save names what bakes before writing, and Save stays on the .ora file.

#include "core/layer_metadata.hpp"
#include "formats/ora_document_io.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "scripting_test_support.hpp"
#include "ui_test_support.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QFileInfo>
#include <QFile>
#include <QMessageBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>

#include <string>
#include <vector>

namespace {

using patchy::test::ui::find_top_level_dialog;
using patchy::test::ui::show_window;

QString committed_ora_fixture(const char* name) {
  return QString::fromStdWString(patchy::test::committed_format_fixture_path("ora", name).wstring());
}

const patchy::Layer* find_named(const std::vector<patchy::Layer>& layers, const std::string& name) {
  for (const auto& layer : layers) {
    if (layer.name() == name) {
      return &layer;
    }
    if (const auto* child = find_named(layer.children(), name); child != nullptr) {
      return child;
    }
  }
  return nullptr;
}

void ui_ora_open_keeps_layers_and_the_selected_layer() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, committed_ora_fixture("python-layers.ora"));
  QApplication::processEvents();
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.width() == 32 && document.height() == 24);
  CHECK(document.layers().size() == 3U);
  const auto* folder = find_named(document.layers(), "Folder");
  CHECK(folder != nullptr && folder->blend_mode() == patchy::BlendMode::PassThrough);
  const auto* top = find_named(document.layers(), "Top");
  CHECK(top != nullptr && top->opacity() == 0.5F && top->bounds().x == 4);
  // The file's selected layer is the active one, not the topmost default.
  CHECK(top != nullptr && document.active_layer_id() == top->id());
  CHECK(document.print_settings().horizontal_ppi == 96.0);

  // A lone layer keeps its transparency as pixels: no flat-file alpha mask.
  patchy::ui::MainWindowTestAccess::open_document_path(window, committed_ora_fixture("python-single-layer-16.ora"));
  QApplication::processEvents();
  const auto& single = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(single.layers().size() == 1U && !single.layers().front().mask().has_value());
}

void ui_ora_save_warns_about_baked_content_and_stays_on_ora() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, committed_ora_fixture("python-layers.ora"));
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  {
    patchy::PixelBuffer pixels(12, 6, patchy::PixelFormat::rgba8());
    pixels.clear(255);
    patchy::Layer text(document.allocate_layer_id(), "Headline", std::move(pixels));
    text.set_bounds(patchy::Rect{2, 2, 12, 6});
    text.metadata()[patchy::kLayerMetadataText] = "Headline";
    document.add_layer(std::move(text));
  }
  patchy::ui::MainWindowTestAccess::canvas(window)->document_changed();

  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto out_path = temp.filePath(QStringLiteral("layers.ora"));
  bool prompt_seen = false;
  QTimer::singleShot(0, [&prompt_seen] {
    auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("oraLostContentMessageBox")));
    CHECK(box != nullptr);
    if (box == nullptr) {
      return;
    }
    CHECK(box->text().contains(QStringLiteral("OpenRaster keeps layers")));
    CHECK(box->text().contains(QStringLiteral("text layer \"Headline\"")));
    prompt_seen = true;
    box->button(QMessageBox::Save)->click();
  });
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, out_path, patchy::ui::ImageSaveOptions{}));
  CHECK(prompt_seen);
  // A layered save, not a flattened copy: the session now points at the .ora file.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == out_path);
  const auto written = patchy::ora::DocumentIo::read_file(patchy::ui::to_filesystem_path(out_path));
  CHECK(written.layers().size() == 4U);
  const auto* headline = find_named(written.layers(), "Headline");
  CHECK(headline != nullptr && !patchy::layer_is_text(*headline));

  // Save again: still the same .ora, after the same warning, and no Save As dialog.
  prompt_seen = false;
  QTimer::singleShot(0, [&prompt_seen] {
    if (auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("oraLostContentMessageBox")))) {
      prompt_seen = true;
      box->button(QMessageBox::Save)->click();
    }
  });
  CHECK(patchy::ui::MainWindowTestAccess::save_document(window));
  CHECK(prompt_seen);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_path(window) == out_path);

  // A document with nothing to bake saves without a question.
  patchy::ui::MainWindowTestAccess::open_document_path(window, committed_ora_fixture("python-layers.ora"));
  QApplication::processEvents();
  const auto plain_path = temp.filePath(QStringLiteral("plain.ora"));
  bool unexpected_prompt = false;
  QTimer::singleShot(0, [&unexpected_prompt] {
    if (auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("oraLostContentMessageBox")))) {
      unexpected_prompt = true;
      box->button(QMessageBox::Cancel)->click();
    }
  });
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, plain_path, patchy::ui::ImageSaveOptions{}));
  CHECK(!unexpected_prompt);
  CHECK(QFileInfo::exists(plain_path));
}

// doc.saveAs("x.ora") from a script: the layers stay, the document points at the file, and
// the bake warning never blocks (a scripted save answers it).
// doc.importNotes: a script sees what the reader dropped or approximated (here an unknown
// composite-op), the same notes the status bar shows; a new document has none.
void ui_script_import_notes_list_reader_notices() {
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = temp.filePath(QStringLiteral("odd-op.ora"));
  {
    patchy::PixelBuffer pixels(4, 4, patchy::PixelFormat::rgba8());
    pixels.clear(200);
    const std::string stack = R"(<image w="4" h="4"><stack><layer src="data/a.png" name="Odd" composite-op="foo:bar"/></stack></image>)";
    const auto bytes = patchy::ora::detail::zip_archive(
        {{"mimetype", std::vector<std::uint8_t>{'i', 'm', 'a', 'g', 'e', '/', 'o', 'p', 'e', 'n', 'r', 'a', 's', 't', 'e', 'r'}, false},
         {"stack.xml", std::vector<std::uint8_t>(stack.begin(), stack.end()), true},
         {"data/a.png", patchy::ora::detail::encode_png(pixels), false}});
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
  }
  const auto source = QStringLiteral(R"JS(
    var d = app.open('%1');
    var kept = d.importNotes;
    console.log('NOTES[' + kept.join('|') + ']');
    d.close();
    console.log('KEPT[' + kept.length + ']');  // a copy: still there after the close
    var fresh = app.newDocument(8, 8);
    console.log('FRESH[' + fresh.importNotes.length + ']');
  )JS").arg(path);
  CHECK(patchy::test::ui::scripting_support::run_script(window, source));
  CHECK(patchy::test::ui::scripting_support::backlog_contains(window, QStringLiteral("Odd (foo:bar)")));
  CHECK(patchy::test::ui::scripting_support::backlog_contains(window, QStringLiteral("FRESH[0]")));
  CHECK(patchy::test::ui::scripting_support::backlog_contains(window, QStringLiteral("KEPT[1]")));
}

void ui_script_save_as_ora_keeps_layers_without_a_prompt() {
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = temp.filePath(QStringLiteral("scripted.ora"));
  bool prompt_seen = false;
  QTimer guard;
  QObject::connect(&guard, &QTimer::timeout, [&prompt_seen] {
    if (auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("oraLostContentMessageBox")))) {
      prompt_seen = true;
      box->button(QMessageBox::Cancel)->click();
    }
  });
  guard.start(50);
  const auto source = QStringLiteral(R"JS(
    var d = app.newDocument(40, 30);
    var top = d.addLayer('Top');
    top.fill('#ff3366');
    top.blendMode = 'exclusion';  // only Krita reads it: a loss the warning would name
    if (!d.saveAs('%1')) throw Error('saveAs');
    if (d.path.indexOf('scripted.ora') < 0 || d.modified) throw Error('saveAs did not retarget');
  )JS").arg(path);
  CHECK(patchy::test::ui::scripting_support::run_script(window, source));
  guard.stop();
  CHECK(!prompt_seen);
  const auto written = patchy::ora::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
  const auto* top = find_named(written.layers(), "Top");
  CHECK(written.layers().size() == 2U && top != nullptr && top->blend_mode() == patchy::BlendMode::Exclusion);
}

// Review capture for the desk: a file Krita wrote (one tile per blend mode, Krita's own mode
// names), open with its Layers panel (test-artifacts/ora_krita_file_open.png). Copied to a
// plain name first so no local path shows; the status bar is cleared for the same reason.
void ui_ora_review_capture_krita_file() {
  const auto fixture = QString::fromStdWString(
      patchy::test::local_format_fixture_path("ora", "krita-modes-from-psd.ora").wstring());
  patchy::test::skip_unless_exists(patchy::ui::to_filesystem_path(fixture));
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = temp.filePath(QStringLiteral("krita-blend-modes.ora"));
  CHECK(QFile::copy(fixture, path));
  patchy::ui::MainWindow window;
  show_window(window);
  window.resize(1280, 800);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::document(window).layers().size() == 33U);
  patchy::ui::MainWindowTestAccess::canvas(window)->set_zoom_centered(2.0);
  window.statusBar()->clearMessage();
  QApplication::processEvents();
  patchy::test::ui::save_widget_artifact("ora_krita_file_open", window);
}

}  // namespace

std::vector<patchy::test::TestCase> ora_ui_tests() {
  return {
      {"ui_ora_open_keeps_layers_and_the_selected_layer", ui_ora_open_keeps_layers_and_the_selected_layer},
      {"ui_ora_save_warns_about_baked_content_and_stays_on_ora", ui_ora_save_warns_about_baked_content_and_stays_on_ora},
      {"ui_script_save_as_ora_keeps_layers_without_a_prompt", ui_script_save_as_ora_keeps_layers_without_a_prompt},
      {"ui_script_import_notes_list_reader_notices", ui_script_import_notes_list_reader_notices},
      {"ui_ora_review_capture_krita_file", ui_ora_review_capture_krita_file},
  };
}
