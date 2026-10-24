// OpenRaster in the app (docs/ora.md): opening keeps the file's layers and selected layer,
// a save names what bakes before writing, and Save stays on the .ora file. Also the other
// layered saves' blend-mode question (docs/blend-modes.md, "Layered saves").

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "formats/document_flatten.hpp"
#include "formats/ora_document_io.hpp"
#include "psd/psd_document_io.hpp"
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
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
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

bool any_paint_dot_net_mode(const std::vector<patchy::Layer>& layers) {
  return std::any_of(layers.begin(), layers.end(), [](const patchy::Layer& layer) {
    return patchy::blend_mode_is_paint_dot_net_only(layer.blend_mode()) || any_paint_dot_net_mode(layer.children());
  });
}

// Largest composite difference in levels (alpha, and colour weighted by cover).
int composite_difference(const patchy::Document& a, const patchy::Document& b) {
  const patchy::Rect canvas{0, 0, a.width(), a.height()};
  const auto left = patchy::flatten_document_rgba8_rect(a, canvas);
  const auto right = patchy::flatten_document_rgba8_rect(b, canvas);
  if (left.width() != right.width() || left.height() != right.height()) {
    return 255;
  }
  const auto l = left.data();
  const auto r = right.data();
  int worst = 0;
  for (std::size_t i = 0; i < l.size(); i += 4) {
    worst = std::max(worst, std::abs(int{l[i + 3]} - int{r[i + 3]}));
    const int cover = std::min(int{l[i + 3]}, int{r[i + 3]});
    for (std::size_t c = 0; c < 3; ++c) {
      worst = std::max(worst, (std::abs(int{l[i + c]} - int{r[i + c]}) * cover + 254) / 255);
    }
  }
  return worst;
}

// A PSD save of a document in Paint.NET's modes asks first: Cancel writes nothing, Save as
// Normal writes Normal layers, Keep the Look writes layers whose composite matches the canvas.
// A plain document saves without the question.
void ui_save_asks_about_unsupported_blend_modes() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(
      window, patchy::ui::to_qstring(patchy::test::committed_format_fixture_path("pdn", "demo.pdn")));
  QApplication::processEvents();
  const patchy::Document original = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(any_paint_dot_net_mode(original.layers()));
  QTemporaryDir temp;
  CHECK(temp.isValid());
  enum class Answer { Cancel, Normal, Keep };
  bool prompt_seen = false;
  const auto answer_with = [&prompt_seen](Answer answer, bool capture) {
    prompt_seen = false;
    QTimer::singleShot(0, [&prompt_seen, answer, capture] {
      auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("unsupportedBlendModesMessageBox")));
      CHECK(box != nullptr);
      if (box == nullptr) {
        return;
      }
      prompt_seen = true;
      CHECK(box->text().contains(QStringLiteral(" files can't store these blend modes")));
      CHECK(box->text().contains(QStringLiteral("Glow")) && box->text().contains(QStringLiteral("Xor")));
      auto* keep = box->findChild<QAbstractButton*>(QStringLiteral("unsupportedBlendModesKeepButton"));
      auto* normal = box->findChild<QAbstractButton*>(QStringLiteral("unsupportedBlendModesNormalButton"));
      CHECK(keep != nullptr && normal != nullptr && static_cast<QAbstractButton*>(box->defaultButton()) == keep);
      if (capture) {
        patchy::test::ui::save_widget_artifact("ui_save_unsupported_blend_modes_prompt", *box);
      }
      if (answer == Answer::Cancel) {
        box->button(QMessageBox::Cancel)->click();
      } else if (keep != nullptr && normal != nullptr) {
        (answer == Answer::Keep ? keep : normal)->click();
      }
    });
  };
  const auto read_psd = [](const QString& path) {
    return patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
  };

  const auto cancelled = temp.filePath(QStringLiteral("cancelled.psd"));
  answer_with(Answer::Cancel, true);
  CHECK(!patchy::ui::MainWindowTestAccess::save_document_to_path(window, cancelled, patchy::ui::ImageSaveOptions{}));
  CHECK(prompt_seen && !QFileInfo::exists(cancelled));

  const auto as_normal = temp.filePath(QStringLiteral("normal.psd"));
  answer_with(Answer::Normal, false);
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, as_normal, patchy::ui::ImageSaveOptions{}));
  CHECK(prompt_seen);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("saved as Normal")));
  const auto normal_file = read_psd(as_normal);
  CHECK(normal_file.layers().size() == original.layers().size() && !any_paint_dot_net_mode(normal_file.layers()));
  CHECK(composite_difference(original, normal_file) > 20);

  const auto kept = temp.filePath(QStringLiteral("kept.psd"));
  answer_with(Answer::Keep, false);
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, kept, patchy::ui::ImageSaveOptions{}));
  CHECK(prompt_seen);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("keep the look")));
  const auto kept_file = read_psd(kept);
  CHECK(!any_paint_dot_net_mode(kept_file.layers()));
  CHECK(composite_difference(original, kept_file) <= 1);
  // The open document keeps its modes.
  CHECK(any_paint_dot_net_mode(patchy::ui::MainWindowTestAccess::document(window).layers()));

  // A layered TIFF asks too; a flat PNG does not.
  const auto tiff = temp.filePath(QStringLiteral("layered.tif"));
  answer_with(Answer::Keep, false);
  patchy::ui::ImageSaveOptions tiff_options;
  tiff_options.tiff_layers = true;
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, tiff, tiff_options));
  CHECK(prompt_seen && QFileInfo::exists(tiff));

  bool unexpected_prompt = false;
  QTimer guard;
  QObject::connect(&guard, &QTimer::timeout, [&unexpected_prompt] {
    if (auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("unsupportedBlendModesMessageBox")))) {
      unexpected_prompt = true;
      box->button(QMessageBox::Cancel)->click();
    }
  });
  guard.start(50);
  const auto png = temp.filePath(QStringLiteral("flat.png"));
  (void)patchy::ui::MainWindowTestAccess::save_document_to_path_flatten_confirmed(window, png,
                                                                                   patchy::ui::ImageSaveOptions{});
  CHECK(QFileInfo::exists(png));
  patchy::ui::MainWindowTestAccess::open_document_path(window, committed_ora_fixture("python-layers.ora"));
  QApplication::processEvents();
  const auto plain = temp.filePath(QStringLiteral("plain.psd"));
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, plain, patchy::ui::ImageSaveOptions{}));
  guard.stop();
  CHECK(!unexpected_prompt && QFileInfo::exists(plain));
}

// saveAs({unsupportedBlendModes}): "normal" is the default, "bake" keeps the look, and
// neither asks; a bad token or another format's file throws.
void ui_script_save_unsupported_blend_modes_option() {
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto normal_path = temp.filePath(QStringLiteral("default.psd"));
  const auto baked_path = temp.filePath(QStringLiteral("baked.psd"));
  bool prompt_seen = false;
  QTimer guard;
  QObject::connect(&guard, &QTimer::timeout, [&prompt_seen] {
    if (auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("unsupportedBlendModesMessageBox")))) {
      prompt_seen = true;
      box->button(QMessageBox::Cancel)->click();
    }
  });
  guard.start(50);
  const auto source = QStringLiteral(R"JS(
    var d = app.newDocument(40, 30);
    var base = d.addLayer('Base');
    base.fill('#3366cc');
    var top = d.addLayer('Top');
    top.fill('#ff3366');
    top.blendMode = 'xor';
    if (!d.saveAs('%1')) throw Error('default saveAs');
    if (!d.saveAs('%2', {unsupportedBlendModes: 'bake'})) throw Error('bake saveAs');
    if (top.blendMode !== 'xor') throw Error('the document changed');
    var threw = false;
    try { d.saveAs('%2', {unsupportedBlendModes: 'keep'}); } catch (e) { threw = true; }
    if (!threw) throw Error('bad token accepted');
    threw = false;
    try { d.exportAs('%3', {unsupportedBlendModes: 'bake'}); } catch (e) { threw = true; }
    if (!threw) throw Error('png accepted the key');
  )JS").arg(normal_path, baked_path, temp.filePath(QStringLiteral("flat.png")));
  CHECK(patchy::test::ui::scripting_support::run_script(window, source));
  guard.stop();
  CHECK(!prompt_seen);
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto normal_file = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(normal_path));
  const auto baked_file = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(baked_path));
  CHECK(!any_paint_dot_net_mode(normal_file.layers()) && !any_paint_dot_net_mode(baked_file.layers()));
  CHECK(composite_difference(document, normal_file) > 20);
  CHECK(composite_difference(document, baked_file) <= 1);
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
      {"ui_save_asks_about_unsupported_blend_modes", ui_save_asks_about_unsupported_blend_modes},
      {"ui_script_save_unsupported_blend_modes_option", ui_script_save_unsupported_blend_modes_option},
      {"ui_script_save_as_ora_keeps_layers_without_a_prompt", ui_script_save_as_ora_keeps_layers_without_a_prompt},
      {"ui_script_import_notes_list_reader_notices", ui_script_import_notes_list_reader_notices},
      {"ui_ora_review_capture_krita_file", ui_ora_review_capture_krita_file},
  };
}
