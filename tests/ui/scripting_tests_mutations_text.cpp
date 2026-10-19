#include <QStatusBar>
#include "formats/metadata/metadata_containers.hpp"
#include "formats/metadata/metadata_edit.hpp"
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "ui/image_document_io.hpp"
#include "core/pixel_depth.hpp"
#include "formats/document_flatten.hpp"
#include "formats/pdf_document_io.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/color_panel.hpp"
#include "ui/palette_panel.hpp"
#include "ui/qt_paths.hpp"
#include "ui/brush_automation.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/app_settings.hpp"
#include "psd/psd_text_runs.hpp"
#include "ui/font_face_name_index.hpp"
#include "ui/main_window.hpp"
#include <QFileInfo>
#include <QFontComboBox>
#include "ui/script_engine.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"
#include <QAction>
#include <QFontDatabase>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHelpEvent>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolTip>
#include <span>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "scripting_test_support.hpp"

namespace {
using namespace patchy::test::ui;
using namespace patchy::test::ui::scripting_support;

// Samples, from a timer that runs inside the busy pump's processEvents, whether
// the app-modal stop panel was ever on screen at the same time as a script
// canvas window. That overlap is the defect: Qt marks every window shown while
// an application-modal window is up as blocked, and a blocked window is skipped
// by the key-delivery path entirely. The desktop platforms lift the block when
// the modal hides, but the wasm plugin never does, so the game window paints
// and takes clicks yet never receives a keystroke again (docs/wasm.md).
class StopPanelOverlapWatch {
public:
  explicit StopPanelOverlapWatch(patchy::ui::MainWindow& window) : window_(window) {
    timer_ = new QTimer(&window);
    QObject::connect(timer_, &QTimer::timeout, &window, [this] { sample(); });
    timer_->start(5);
  }

  void sample() {
    auto* panel = window_.findChild<QDialog*>(QStringLiteral("scriptStopPanel"));
    const bool panel_up = panel != nullptr && panel->isVisible();
    if (panel_up) {
      saw_panel_ = true;
      if (window_.findChild<QDialog*>(QStringLiteral("scriptCanvasWindowDialog")) != nullptr) {
        overlapped_ = true;
      }
    }
  }

  void stop() { timer_->stop(); }
  [[nodiscard]] bool overlapped() const { return overlapped_; }
  [[nodiscard]] bool saw_panel() const { return saw_panel_; }

private:
  patchy::ui::MainWindow& window_;
  QTimer* timer_{nullptr};
  bool overlapped_{false};
  bool saw_panel_{false};
};


void ui_script_file_info_reads_edits_and_strips() {
  patchy::ui::MainWindow window;
  show_window(window);
  QDir().mkpath(QStringLiteral("test-artifacts/file-info"));
  const auto png = QDir(QStringLiteral("test-artifacts/file-info")).absoluteFilePath(QStringLiteral("script.png"));
  const auto bare = QDir(QStringLiteral("test-artifacts/file-info")).absoluteFilePath(QStringLiteral("script-none.png"));
  CHECK(run_script(window, QStringLiteral(R"JS(
var d = app.newDocument(8, 8);
d.setFileInfo({title: "Harbor", authors: ["Ann Example"], keywords: ["harbor", "dawn"],
               copyrightStatus: "copyrighted", rating: 3, contact: {email: "ann@example.com"}});
var info = d.fileInfo;
if (info.title !== "Harbor" || info.keywords.length !== 2 || info.rating !== 3) throw new Error("fields");
if (info.copyrightStatus !== "copyrighted" || info.contact.email !== "ann@example.com") throw new Error("status");
if (d.xmp.indexOf("Harbor") < 0) throw new Error("xmp");
if (!d.metadataEntries().some(function (e) { return e.group === "XMP dc" && e.name === "dc:title"; }))
  throw new Error("entries");
function throws(f) { try { f(); } catch (e) { return true; } return false; }
if (!throws(function () { d.setFileInfo({bogus: 1}); })) throw new Error("unknown key");
if (!throws(function () { d.setFileInfo({created: "2026"}); })) throw new Error("read-only key");
if (!throws(function () { d.setFileInfo({rating: 9}); })) throw new Error("rating range");
if (!throws(function () { d.exportAs("%2", {metadata: "some"}); })) throw new Error("token");
d.saveAs("%1");
d.exportAs("%2", {metadata: "none"});
if (d.removeLocation()) throw new Error("no location to remove");
d.stripMetadata();
if (d.fileInfo.title !== "" || d.xmp !== "") throw new Error("strip");
)JS").arg(png, bare)));
  const auto saved = patchy::metadata::extract_file_info_from_path(patchy::ui::to_filesystem_path(png));
  CHECK(patchy::metadata::read_fields(saved).title == "Harbor");
  const auto none = patchy::metadata::extract_file_info_from_path(patchy::ui::to_filesystem_path(bare));
  CHECK(none.xmp == nullptr);
  CHECK(!patchy::ui::MainWindowTestAccess::document(window).file_info().has_metadata());
}

// doc.importFilesAsLayers: array and string forms, argument order, the copy suffix
// on a name collision, all-or-nothing on a missing file, and one undo step per run.
void ui_script_import_files_as_layers() {
  patchy::test::ui::ensure_artifact_dir();
  const auto dir = QFileInfo(QStringLiteral("test-artifacts/script-files-as-layers")).absoluteFilePath();
  CHECK(QDir().mkpath(dir));
  const auto write_png = [&](const QString& name, int width, int height, QColor color) {
    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(color);
    const auto path = QDir::toNativeSeparators(dir + QLatin1Char('/') + name);
    QFile::remove(path);
    CHECK(image.save(path));
    return path;
  };
  const auto a = write_png(QStringLiteral("a.png"), 20, 12, QColor(200, 30, 30, 255));
  const auto b = write_png(QStringLiteral("b.png"), 8, 8, QColor(30, 200, 30, 255));
  const auto missing = QDir::toNativeSeparators(dir + QStringLiteral("/missing.png"));
  QFile::remove(missing);
  const auto json = [](const QString& path) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{path}).toJson(QJsonDocument::Compact)).chopped(1).mid(1);
  };

  patchy::ui::MainWindow window;
  show_window(window);
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var before = doc.layers.length;
    var added = doc.importFilesAsLayers([%1, %2]);
    if (added.length !== 2) throw new Error('expected 2 layers, got ' + added.length);
    if (added[0].name !== 'a' || added[1].name !== 'b') throw new Error('names ' + added[0].name + ',' + added[1].name);
    if (doc.layers.length !== before + 2) throw new Error('layer count ' + doc.layers.length);
    if (doc.layers[doc.layers.length - 1].name !== 'b') throw new Error('b is not on top');
    if (doc.layers[doc.layers.length - 2].name !== 'a') throw new Error('a is not below b');
    if (doc.activeLayer.name !== 'b') throw new Error('active ' + doc.activeLayer.name);
    // The string form; the name collision earns the copy suffix.
    var one = doc.importFilesAsLayers(%1);
    if (one.length !== 1 || one[0].name !== 'a copy') throw new Error('string form: ' + one[0].name);
    if (doc.layers.length !== before + 3) throw new Error('string form count');
    var threw = false;
    try { doc.importFilesAsLayers([%2, %3]); } catch (e) { threw = true; }
    if (!threw) throw new Error('missing file accepted');
    if (doc.layers.length !== before + 3) throw new Error('missing file changed the document');
    threw = false;
    try { doc.importFilesAsLayers([]); } catch (e) { threw = true; }
    if (!threw) throw new Error('empty array accepted');
    threw = false;
    try { doc.importFilesAsLayers(42); } catch (e) { threw = true; }
    if (!threw) throw new Error('number accepted');
    console.log('files-ok');
  )JS")
                                .arg(json(a), json(b), json(missing))));
  CHECK(backlog_contains(window, QStringLiteral("files-ok")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1);
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto& layers = std::as_const(document).layers();
  CHECK(layers.size() >= 3);
  CHECK(layers.back().name() == "a copy");
  CHECK(layers[layers.size() - 2].name() == "b");
  CHECK(layers[layers.size() - 3].name() == "a");
}

// app.exportPdf: several documents become the pages of one file; bad arguments throw
// instead of writing anything.
void ui_script_export_pdf_writes_pages() {
  patchy::test::ui::ensure_artifact_dir();
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QDir::current().filePath(QStringLiteral("test-artifacts/ui_script_export_pages.pdf"));
  QFile::remove(path);
  const auto preset_path = QDir::current().filePath(QStringLiteral("test-artifacts/ui_script_export_preset.pdf"));
  QFile::remove(preset_path);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var a = app.newDocument(300, 150);
    a.activeLayer.fill('#ff0000');
    var b = app.newDocument(150, 300);
    b.activeLayer.fill('#0000ff');
    if (!app.exportPdf([a, b], %1, { lossless: true })) throw new Error('export failed');
    var threw = false;
    try { app.exportPdf([], %1); } catch (e) { threw = true; }
    if (!threw) throw new Error('empty list accepted');
    threw = false;
    try { app.exportPdf('nope', %1); } catch (e) { threw = true; }
    if (!threw) throw new Error('non-document accepted');
    // imageQuality names a preset and wins over lossless; an unknown id throws.
    if (!app.exportPdf(a, %2, { lossless: true, imageQuality: 'medium' })) throw new Error('preset export failed');
    threw = false;
    try { app.exportPdf(a, %2, { imageQuality: 'ultra' }); } catch (e) { threw = true; }
    if (!threw) throw new Error('unknown imageQuality accepted');
    console.log('pdf-ok');
  )JS")
                                .arg(QString::fromUtf8(QJsonDocument(QJsonArray{path}).toJson(QJsonDocument::Compact))
                                         .chopped(1)
                                         .mid(1),
                                     QString::fromUtf8(
                                         QJsonDocument(QJsonArray{preset_path}).toJson(QJsonDocument::Compact))
                                         .chopped(1)
                                         .mid(1))));
  CHECK(backlog_contains(window, QStringLiteral("pdf-ok")));
  {
    QFile preset_file(preset_path);
    CHECK(preset_file.open(QIODevice::ReadOnly));
    const QByteArray preset_bytes = preset_file.readAll();
    CHECK(preset_bytes.contains("/DCTDecode"));
    CHECK(!preset_bytes.contains("/FlateDecode"));
  }
  QFile file(path);
  CHECK(file.open(QIODevice::ReadOnly));
  const QByteArray bytes = file.readAll();
  CHECK(bytes.contains("/FlateDecode"));  // lossless: true
  const std::span<const std::uint8_t> span(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                           static_cast<std::size_t>(bytes.size()));
  CHECK(patchy::pdf::page_count(span) == 2);
  // A script document carries the core 300 ppi default, so 300 x 150 px is a 72 x 36 pt
  // page; read back at 300 px per inch it is 300 x 150 again, and page 2 is 150 x 300.
  const auto first = patchy::pdf::page_size_in_pixels(span, 0, 300.0 / 72.0);
  const auto second = patchy::pdf::page_size_in_pixels(span, 1, 300.0 / 72.0);
  CHECK(first[0] == 300 && first[1] == 150);
  CHECK(second[0] == 150 && second[1] == 300);
}

// moveTo and the x/y setters round like Photoshop (halves up), never truncate toward zero.
void ui_script_layer_move_to_rounds_like_photoshop() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto bounds_of = [&document]() {
    const auto* layer = layer_named(document, "Rounded");
    CHECK(layer != nullptr);
    return layer != nullptr ? layer->bounds() : patchy::Rect{};
  };
  CHECK(run_script(window, QStringLiteral(R"JS(
    var layer = app.activeDocument.addLayer('Rounded');
    layer.fill('#ff4000');
    layer.moveTo(10.6, -10.9);
  )JS")));
  CHECK(bounds_of().x == 11);
  CHECK(bounds_of().y == -11);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var layer = app.activeDocument.activeLayer;
    layer.x = 3.4;
    layer.y = 6.5;
  )JS")));
  CHECK(bounds_of().x == 3);
  CHECK(bounds_of().y == 7);
  CHECK(run_script(window, QStringLiteral(R"JS(
    app.activeDocument.activeLayer.moveTo(-3.5, -3.5);
  )JS")));
  CHECK(bounds_of().x == -3);
  CHECK(bounds_of().y == -3);
}

void ui_script_palette_validation_and_history() {
  using patchy::ui::MainWindowTestAccess;
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, "var d=app.newDocument(8,8);d.activeLayer.fill('#123456');"));
  const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var d=app.activeDocument;
    if(d.getPalette()!==null)throw Error('unexpected palette');
    d.setPalette(['#000000','#ffffff','#000000'],{names:['Ink','Bone','Duplicate'],alphaThreshold:96});
    var p=d.getPalette();
    if(!p.enabled||p.colors[2]!=='#000000'||p.names[1]!=='Bone'||p.alphaThreshold!==96)throw Error('palette metadata');
    p.colors[0]='#ff0000';p.names[1]='Changed';
    if(d.getPalette().colors[0]!=='#000000'||d.getPalette().names[1]!=='Bone')throw Error('not detached');
    var raw=new Uint8Array(d.activeLayer.getPixels().data);
    if(raw[0]!==18||raw[1]!==52||raw[2]!==86)throw Error('rewrote layer');
  )JS")));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  auto& host = window.script_engine_host();
  const auto revision = host.session_document_const(host.active_session_id())->palette_editing()->palette_revision;
  CHECK(run_script(window, QStringLiteral(R"JS(
    var d=app.activeDocument;
    d.setPalette(['#000000','#ffffff','#000000'],{names:['Ink','Bone','Duplicate'],alphaThreshold:96});
    var invalid=[[],['transparent'],[17],new Array(257).fill('#000000')];
    invalid.forEach(function(v){var caught=false;try{d.setPalette(v);}catch(e){caught=true;}if(!caught)throw Error('accepted invalid colors');});
    [{enabled:'yes'},{typo:true},{alphaThreshold:256},{alphaThreshold:NaN},{alphaThreshold:1.5},
     {names:['short']},{names:['bad\nname','','']}].forEach(function(o){
      var caught=false;try{d.setPalette(['#000000','#ffffff','#000000'],o);}catch(e){caught=true;}
      if(!caught)throw Error('accepted invalid options');
    });
  )JS")));
  CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
  CHECK(run_script(window, "app.activeDocument.undo();if(app.activeDocument.getPalette()!==null)throw Error('undo palette');"));
  CHECK(run_script(window, "app.activeDocument.redo();app.activeDocument.activeLayer.fill('#eeeeee');"));
  const auto* doc = host.session_document_const(host.active_session_id());
  CHECK(doc->layers().front().pixels().pixel(0,0)[0] == 255);
  CHECK(run_script(window, "var d=app.activeDocument;d.setPalette(['#ff0000'],{enabled:false,names:['Red']});"));
  CHECK(!host.session_document_const(host.active_session_id())->palette_editing());
  CHECK(run_script(window, "app.activeDocument.setPalette(['#ffffff'],{names:['Pearl']});"));
  CHECK(host.session_document_const(host.active_session_id())->palette_editing()->palette_revision > revision);
}

void ui_script_palette_unicode_files_and_indexed_png() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  const auto dir = patchy::test::unicode_artifact_dir(u8"script-palette");
  const auto base = patchy::ui::to_qstring(dir / patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem));
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("palette-files");
  options.args = QStringList{QStringLiteral("base=") + base};
  CHECK(host.run_source(QStringLiteral(R"JS(
    var base=patchy.args.base,d=app.newDocument(8,8);
    var colors=['#ffffff','#000000','#bbaa99','#ffffff'];
    var names=['Bone 骨 café','Ink','','Duplicate'];
    d.setPalette(colors,{names:names,alphaThreshold:96});
    d.activeLayer.fill('transparent');
    d.activeLayer.fillRect(0,0,4,8,'#ffffff');
    var path=d.path,modified=d.modified;
    ['gpl','pal','hex','act','aco'].forEach(function(ext){
      if(!d.savePalette(base+'.'+ext,'Beads'))throw Error('save palette');
      var p=d.loadPalette(base+'.'+ext,{enabled:false});
      if(JSON.stringify(p.colors)!==JSON.stringify(colors))throw Error('color order');
      if(ext==='gpl'&&JSON.stringify(p.names)!==JSON.stringify(names))throw Error('GPL names');
      d.setPalette(colors,{names:names,alphaThreshold:96});
    });
    if(d.path!==path||d.modified!==modified)throw Error('palette changed file identity');
    if(!d.saveAs(base+'.psd'))throw Error('PSD save');
    if(!d.exportAs(base+'.png'))throw Error('PNG export');
    d.close();
    var p=app.open(base+'.png').getPalette();
    if(!p||p.colors.length!==5||p.names[0]!==names[0]||p.names[4]!=='')throw Error('indexed PNG names');
    app.activeDocument.close();
    d=app.open(base+'.psd');p=d.getPalette();
    if(!p.enabled||p.alphaThreshold!==96||JSON.stringify(p.names)!==JSON.stringify(names))throw Error('PSD names');
    d.setPalette(colors,{names:names,enabled:false});
    if(!d.saveAs(base+'-rgb.png'))throw Error('RGB export');
    var caught=false;try{d.loadPalette(base+'-missing.gpl');}catch(e){caught=true;}if(!caught)throw Error('missing palette accepted');
    caught=false;try{d.savePalette(base+'.bad');}catch(e){caught=true;}if(!caught)throw Error('unsupported format accepted');
  )JS"), std::move(options)));
  wait_for_run_end(host);
  CHECK(!host.last_run_had_error());
  QFile png(base + QStringLiteral(".png"));
  CHECK(png.open(QIODevice::ReadOnly));
  const auto header = png.read(26);
  CHECK(header.size() == 26 && static_cast<unsigned char>(header[25]) == 3);
  QImage indexed(base + QStringLiteral(".png"));
  CHECK(indexed.size() == QSize(8,8));
  CHECK(indexed.format() == QImage::Format_Indexed8);
  CHECK(indexed.colorCount() == 5);
  CHECK(qAlpha(indexed.color(4)) == 0);
  QFile rgb(base + QStringLiteral("-rgb.png"));
  CHECK(rgb.open(QIODevice::ReadOnly));
  CHECK(static_cast<unsigned char>(rgb.read(26)[25]) != 3);
}

void ui_script_palette_named_controls_and_rename() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, "var d=app.newDocument(128,128);d.setPalette(['#ffffff','#000000'],{names:['','Ink']});d.activeLayer.fill('#ffffff');"));
  patchy::ui::PatchyColorPicker picker(QColor(Qt::white), &window);
  picker.resize(600,400);
  picker.show();
  auto* grid = window.findChild<QWidget*>(QStringLiteral("paletteSwatchGrid"));
  auto* picker_grid = picker.findChild<QWidget*>(QStringLiteral("patchyColorPaletteGrid"));
  auto* label = picker.findChild<QLabel*>(QStringLiteral("patchyColorNameLabel"));
  auto* readout = window.findChild<QLabel*>(QStringLiteral("paletteCountLabel"));
  CHECK(grid && picker_grid && label && readout);
  const auto rename = [&](QWidget* target, const QString& expected_action, const QString& answer, bool accept) {
    bool saw_menu = false, saw_dialog = false;
    QTimer::singleShot(0, &window, [&] {
      auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!menu) { return; }
      auto* action = menu->findChild<QAction*>(QStringLiteral("paletteRenameAction"));
      if (!action) { menu->close(); return; }
      saw_menu = action->text() == expected_action;
      QTimer::singleShot(0, &window, [&] {
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (auto* dialog = qobject_cast<QInputDialog*>(widget); dialog && dialog->objectName() == QStringLiteral("paletteColorNameDialog")) {
            saw_dialog = true;
            dialog->setTextValue(answer);
            if (accept) { dialog->accept(); } else { dialog->reject(); }
          }
        }
      });
      action->trigger();
      menu->close();
    });
    QContextMenuEvent context(QContextMenuEvent::Mouse, QPoint(5,5), target->mapToGlobal(QPoint(5,5)));
    QApplication::sendEvent(target, &context);
    QApplication::processEvents();
    CHECK(saw_menu && saw_dialog);
  };
  rename(grid, QStringLiteral("Set Name"), QStringLiteral("Bone <white>"), true);
  CHECK(label->text() == QStringLiteral("Bone <white>"));
  CHECK(readout->text().contains(QStringLiteral("Bone <white>")) && readout->text().contains(QStringLiteral("#ffffff")));
  rename(picker_grid, QStringLiteral("Rename"), QStringLiteral("Pearl"), false);
  CHECK(label->text() == QStringLiteral("Bone <white>"));
  rename(picker_grid, QStringLiteral("Rename"), QStringLiteral("Pearl"), true);
  CHECK(label->text() == QStringLiteral("Pearl"));
  CHECK(run_script(window, "app.activeDocument.undo();"));
  CHECK(label->text() == QStringLiteral("Bone <white>"));
  rename(grid, QStringLiteral("Rename"), QString(), true);
  CHECK(label->text().isEmpty());
  CHECK(run_script(window, "app.activeDocument.undo();"));
  picker.setCurrentColor(QColor(2,3,4));
  CHECK(label->text().isEmpty());
  picker.setCurrentColor(Qt::white);
  CHECK(label->text() == QStringLiteral("Bone <white>"));
  save_widget_artifact("palette_named_picker", picker);
  picker.hide();
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  canvas->set_tool(patchy::ui::CanvasTool::Eyedropper);
  const auto point = canvas->widget_position_for_document_point(QPoint(64,64));
  QTest::mouseMove(canvas, point);
  QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, point);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Bone <white>")));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("#FFFFFF")));
  CHECK(window.findChild<QPushButton*>(QStringLiteral("foregroundColorButton"))->toolTip().contains(QStringLiteral("Bone &lt;white&gt;")));
  CHECK(window.findChild<QLabel*>(QStringLiteral("canvasInfoLabel"))->text().contains(QStringLiteral("Bone <white>")));
  // A remembered file palette keeps its own labels and edits them in memory,
  // without silently attaching or renaming the active document's palette.
  CHECK(run_script(window, "app.activeDocument.savePalette('test-artifacts/named-picker.gpl','Named');app.newDocument(8,8);"));
  auto settings = patchy::ui::app_settings();
  const auto choice_key = QString::fromLatin1(patchy::ui::kColorPickerPaletteChoiceKey);
  const auto file_key = QStringLiteral("palettes/lastPaletteFile");
  const auto old_choice = settings.value(choice_key), old_file = settings.value(file_key);
  const auto restore = qScopeGuard([&] {
    if (old_choice.isValid()) { settings.setValue(choice_key, old_choice); } else { settings.remove(choice_key); }
    if (old_file.isValid()) { settings.setValue(file_key, old_file); } else { settings.remove(file_key); }
  });
  settings.setValue(choice_key, QStringLiteral("file"));
  settings.setValue(file_key, QFileInfo(QStringLiteral("test-artifacts/named-picker.gpl")).absoluteFilePath());
  patchy::ui::PatchyColorPicker file_picker(Qt::white, &window);
  file_picker.resize(600,400);
  file_picker.show();
  auto* file_label = file_picker.findChild<QLabel*>(QStringLiteral("patchyColorNameLabel"));
  CHECK(file_label && file_label->text() == QStringLiteral("Bone <white>"));
  rename(file_picker.findChild<QWidget*>(QStringLiteral("patchyColorPaletteGrid")), QStringLiteral("Rename"), QStringLiteral("File pearl"), true);
  CHECK(file_label->text() == QStringLiteral("File pearl"));
  CHECK(run_script(window, "if(app.activeDocument.getPalette()!==null)throw Error('file rename changed document');"));
}

void ui_palette_panel_named_readout_stays_visible() {
  patchy::ui::PalettePanel panel;
  const std::vector<patchy::RgbColor> colors{{230,214,173}, {0,0,0}, {255,255,255}};
  panel.set_palette(colors, true, {"Bone <white>", "Ink", ""});
  panel.resize(390, 300);
  panel.show();
  QApplication::processEvents();
  auto* label = panel.findChild<QLabel*>(QStringLiteral("paletteCountLabel"));
  auto* grid = panel.findChild<QWidget*>(QStringLiteral("paletteSwatchGrid"));
  auto* copy = panel.findChild<QWidget*>(QStringLiteral("paletteCopyHexButton"));
  CHECK(label && grid && copy);
  CHECK(label->isVisibleTo(&panel));
  CHECK(label->width() >= panel.width() - 20);
  CHECK(label->height() >= label->fontMetrics().lineSpacing() * 2);
  CHECK(label->geometry().top() > copy->geometry().bottom());
  CHECK(label->text() == QStringLiteral("Bone <white>\nIndex 0: #e6d6ad"));
  QHelpEvent tip(QEvent::ToolTip, QPoint(9,9), grid->mapToGlobal(QPoint(9,9)));
  QApplication::sendEvent(grid, &tip);
  CHECK(QToolTip::text().contains(QStringLiteral("Bone &lt;white&gt;")));
  CHECK(QToolTip::text().contains(QStringLiteral("#E6D6AD")));
  CHECK(QToolTip::text().contains(QStringLiteral("RGB: 230, 214, 173")));
  QToolTip::hideText();
  save_widget_artifact("palette_named_panel", panel);

  // Long labels wrap inside a narrow dock without taking away the code readout.
  panel.resize(260, 300);
  panel.set_palette(colors, true, {"A long bead color name that wraps across several lines in a narrow palette panel", "Ink", ""});
  QApplication::processEvents();
  CHECK(panel.width() == 260);
  CHECK(label->width() >= 240);
  CHECK(label->height() >= label->fontMetrics().lineSpacing() * 3);
  CHECK(label->geometry().bottom() < grid->parentWidget()->mapTo(&panel, QPoint()).y());
  QTest::mouseClick(grid, Qt::LeftButton, Qt::NoModifier, QPoint(49,9));
  QApplication::processEvents();
  CHECK(label->text() == QStringLiteral("Index 2: #ffffff"));
  CHECK(label->width() >= 240);
  panel.set_palette({}, false);
  CHECK(label->isHidden());
}

void ui_script_palette_extract_preserves_matching_names() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, R"JS(
    var d=app.newDocument(8,8);
    d.setPalette(['#ffffff','#000000','#ffffff','#ff0000'],
      {enabled:false,names:['Bone','Ink','Duplicate white','Unused red']});
    d.activeLayer.fill('#ffffff');
    d.activeLayer.fillRect(0,0,4,8,'#000000');
    d.activeLayer.fillRect(7,7,1,1,'#fffffe');
  )JS"));
  auto* extract = window.findChild<QWidget*>(QStringLiteral("paletteExtractButton"));
  CHECK(extract);
  QTest::mouseClick(extract, Qt::LeftButton);
  CHECK(run_script(window, R"JS(
    var p=app.activeDocument.getPalette();
    if(p.colors.length!==3 || p.names[p.colors.indexOf('#ffffff')]!=='Bone' ||
       p.names[p.colors.indexOf('#000000')]!=='Ink' ||
       p.names[p.colors.indexOf('#fffffe')]!=='' || p.enabled) throw Error('extracted names');
    app.activeDocument.undo();
    p=app.activeDocument.getPalette();
    if(p.colors.length!==4 || p.names[2]!=='Duplicate white' || p.names[3]!=='Unused red') throw Error('extract undo');
  )JS"));
}

void ui_script_advanced_brush_pen_pose_and_dab_cancellation() {
  using namespace patchy::ui;
  MainWindow window;show_window(window);window.set_cli_automation_mode(true);
  CHECK(run_script(window,"app.newDocument(96,96).activeLayer.fill('#c6bdab');"));
  auto& host=window.script_engine_host();auto* canvas=MainWindowTestAccess::canvas(window);
  auto& library=window.brush_automation_library();canvas->set_zoom(4);
  const QStringList configs{
    R"({"dynamics":{"angleControl":"penRotation","roundnessControl":"penTilt","minimumRoundness":0.2,"opacityControl":"stylusWheel"}})",
    R"({"dynamics":{"sizeControl":"penPressure","opacityControl":"off"}})",
    R"({"pen":{"pressureSize":false,"pressureOpacity":true,"tiltShape":true}})",
    R"({"tool":"eraser","pen":{"pressureSize":true,"pressureOpacity":false}})"
  };
  for(const auto& config:configs) {
    auto settings=QJsonDocument::fromJson(config.toUtf8()).object();settings["size"]=23;settings["roundness"]=45;
    settings["color"]="#713828";settings["flow"]=60;
    const auto encoded=QString::fromUtf8(QJsonDocument(settings).toJson(QJsonDocument::Compact));
    CHECK(run_script(window,"var s="+encoded+R"JS(;s.points=[
      {x:12,y:20,pressure:.3,xTilt:10,yTilt:15,rotation:20,tangentialPressure:-.5},
      {x:40,y:63,pressure:.9,xTilt:35,yTilt:25,rotation:90,tangentialPressure:.3},
      {x:79,y:34,pressure:.6,xTilt:55,yTilt:5,rotation:150,tangentialPressure:.8}];
      app.activeDocument.activeLayer.drawStrokes([s]);)JS"));
    const auto scripted=patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
    CHECK(run_script(window,"app.activeDocument.undo();"));canvas->apply_script_brush(library.resolve(settings));
    canvas->set_brush_dynamics_test_seed(0);
    const auto tablet=[&](QEvent::Type type,QPoint p,double pressure,float tilt_x,float tilt_y,double rotation,float wheel) {
      patchy::test::ui::send_tablet(*canvas,type,canvas->widget_position_for_document_point(p),pressure,
        type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,type==QEvent::TabletRelease?Qt::NoButton:Qt::LeftButton,
        Qt::NoModifier,QPointingDevice::PointerType::Pen,
        QInputDevice::Capability::Position|QInputDevice::Capability::Pressure|QInputDevice::Capability::XTilt|
        QInputDevice::Capability::YTilt|QInputDevice::Capability::Rotation|QInputDevice::Capability::TangentialPressure,
        tilt_x,tilt_y,rotation,wheel);
    };
    tablet(QEvent::TabletPress,{12,20},.3,10,15,20,-.5F);
    tablet(QEvent::TabletMove,{40,63},.9,35,25,90,.3F);
    tablet(QEvent::TabletMove,{79,34},.6,55,5,150,.8F);
    tablet(QEvent::TabletRelease,{79,34},.6,55,5,150,.8F);
    const auto native=patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
    CHECK(std::equal(scripted.data().begin(),scripted.data().end(),native.data().begin(),native.data().end()));
    MainWindowTestAccess::undo(window);
  }
  // One very long segment must service cancellation inside its native dab loop.
  const auto before=BrushAutomationLibrary::settings(canvas->current_script_brush());
  auto stroke=library.resolve(QJsonObject{{"size",12},{"spacing",.01},{"dynamics",QJsonObject{{"textureEnabled",true}}}});
  ScriptStrokePoint start;start.position={5,5};
  ScriptStrokePoint end;end.position={99999,5};
  stroke.points={start,end};int callbacks=0;
  const auto dirty=canvas->paint_script_stroke(stroke,[&](const QRect&){return ++callbacks>=3;});
  CHECK(!dirty.isEmpty());CHECK(callbacks>=3);CHECK(callbacks<10);
  CHECK(before==BrushAutomationLibrary::settings(canvas->current_script_brush()));
}

void ui_script_advanced_brush_native_parity() {
  using namespace patchy::ui;
  MainWindow window; show_window(window); window.set_cli_automation_mode(true);
  CHECK(run_script(window, "var d=app.newDocument(96,96);d.activeLayer.fill('#b9c7d8');"));
  auto& host=window.script_engine_host(); auto* canvas=MainWindowTestAccess::canvas(window);
  canvas->set_zoom(4.0);
  auto& library=window.brush_automation_library();
  const QStringList settings{
    R"({"dynamics":{"wetEdges":true}})",
    R"({"dynamics":{"textureEnabled":true,"textureStyle":"canvas","textureDepth":0.7}})",
    R"({"dynamics":{"dualBrushEnabled":true,"dualBrushSize":0.4,"dualBrushSpacing":0.7}})",
    R"({"dynamics":{"colorDynamicsEnabled":true,"foregroundBackgroundJitter":0.6,"hueJitter":0.1}})",
    R"({"dynamics":{"sizeJitter":0.3,"angleJitter":0.3,"roundnessJitter":0.5,"flipXJitter":true}})",
    R"({"roundness":30,"dynamics":{"angleControl":"direction"}})",
    R"({"dynamics":{"scatter":0.4,"scatterBothAxes":true,"count":3,"countJitter":0.4}})",
    R"({"dynamics":{"opacityJitter":0.3,"flowJitter":0.4,"flowControl":"fade","flowFadeSteps":40}})",
    R"({"tool":"mixer","mixer":{"wet":65,"load":40,"mix":75,"sampleAllLayers":true}})",
    R"({"tool":"mixer","mixer":{"wet":0,"load":10,"mix":0}})",
    R"({"smoothing":{"amount":12,"pulledString":true,"catchUp":false,"catchUpOnEnd":true}})"
  };
  for (const auto& settings_json:settings) {
    auto config=QJsonDocument::fromJson(settings_json.toUtf8()).object();
    config["size"]=19;config["flow"]=35;config["color"]="#813c22";config["seed"]=21;
    const auto serialized=QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
    const auto before=BrushAutomationLibrary::settings(canvas->current_script_brush());
    CHECK(run_script(window,"var s="+serialized+";s.points=[{x:12,y:20},{x:35,y:66},{x:78,y:36}];app.activeDocument.activeLayer.drawStrokes([s]);"));
    CHECK(before==BrushAutomationLibrary::settings(canvas->current_script_brush()));
    const auto scripted=patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
    CHECK(run_script(window,"app.activeDocument.undo();"));
    canvas->apply_script_brush(library.resolve(config));canvas->set_brush_dynamics_test_seed(21);
    patchy::test::ui::drag_document_path(*canvas,{{12,20},{35,66},{78,36}},1);
    const auto native=patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
    if (!std::equal(scripted.data().begin(),scripted.data().end(),native.data().begin(),native.data().end())) {
      QImage(scripted.data().data(),96,96,96*4,QImage::Format_RGBA8888).save("test-artifacts/advanced-scripted.png");
      QImage(native.data().data(),96,96,96*4,QImage::Format_RGBA8888).save("test-artifacts/advanced-native.png");
      throw std::runtime_error("advanced brush parity: "+settings_json.toStdString());
    }
    MainWindowTestAccess::undo(window);
  }
}

void ui_script_advanced_brush_timing_validation_and_restore() {
  using namespace patchy::ui;
  MainWindow window; show_window(window);window.set_cli_automation_mode(true);
  CHECK(run_script(window,"app.newDocument(96,96).addLayer('Timed');"));
  auto& host=window.script_engine_host();const auto id=host.active_session_id();
  const auto stroke=QStringLiteral(R"JS(
    var s={size:24,color:'#934321',flow:15,airbrush:true,
      smoothing:{amount:15,catchUp:true},dynamics:{opacityControl:'off',sizeControl:'penPressure'},
      points:[{x:20,y:40,pressure:.2,timeMs:0},{x:65,y:40,pressure:1,timeMs:100},{x:65,y:40,pressure:1,timeMs:500}]};
    app.activeDocument.activeLayer.drawStrokes([s]);
  )JS");
  const auto before=host.automation_fingerprint();
  const auto depth=MainWindowTestAccess::active_session_undo_depth(window);
  const QStringList bad{
    "{airbrush:true,points:[{x:2,y:2}]}",
    "{points:[{x:2,y:2,timeMs:1}]}",
    "{points:[{x:2,y:2,timeMs:0},{x:4,y:4}]}",
    "{points:[{x:2,y:2,xTilt:4}]}",
    "{dynamics:{wetEdges:1},points:[{x:2,y:2}]}",
    "{dynamics:{sizeJitter:NaN},points:[{x:2,y:2}]}",
    "{tool:'mixer',dynamics:{wetEdges:true},points:[{x:2,y:2}]}",
    "{tipId:'missing',points:[{x:2,y:2}]}",
    "{sizeJitter:.2,dynamics:{sizeJitter:.4},points:[{x:2,y:2}]}"
  };
  for(const auto& s:bad) {
    CHECK(!run_script(window,"app.activeDocument.activeLayer.drawStrokes([{size:12,points:[{x:8,y:8}]} ,"+s+"]);"));
    CHECK(host.automation_fingerprint()==before);
    CHECK(MainWindowTestAccess::active_session_undo_depth(window)==depth);
  }
  CHECK(run_script(window,stroke));
  const auto first=patchy::flatten_document_rgba8(*host.session_document_const(id));
  CHECK(run_script(window,"app.activeDocument.undo();patchy.ui.present(60);"+stroke+"patchy.ui.present(60);"));
  const auto second=patchy::flatten_document_rgba8(*host.session_document_const(id));
  CHECK(std::equal(first.data().begin(),first.data().end(),second.data().begin(),second.data().end()));
  CHECK(run_script(window,"app.activeDocument.undo();patchy.ui.slowMode=true;"+stroke));
  const auto slow=patchy::flatten_document_rgba8(*host.session_document_const(id));
  CHECK(std::equal(first.data().begin(),first.data().end(),slow.data().begin(),slow.data().end()));
  CHECK(run_script(window,"patchy.ui.slowMode=false;"));
  CHECK(run_script(window,"app.activeDocument.undo();app.activeDocument.redo();"));
  CHECK(run_script(window,R"JS(
    var b=patchy.brushes.resolve({dynamics:{sizeControl:'penPressure',opacityControl:'off'}}).settings;
    if(b.dynamics.sizeControl!=='penPressure'||b.dynamics.opacityControl!=='off')throw Error('independent pressure');
    patchy.setResult(patchy.brushes.getCurrent());
  )JS"));
}

void ui_script_advanced_brush_presets_snapshots_and_refresh() {
  using namespace patchy::ui;
  QTemporaryDir dir(QDir::currentPath()+"/test-artifacts/brush-library-XXXXXX");CHECK(dir.isValid());
  BrushTipLibrary tips(dir.path()+"/tips");
  BrushAutomationLibrary first(tips,nullptr,dir.path()+"/presets");
  BrushTipLibrary other_tips(dir.path()+"/tips");
  BrushAutomationLibrary other(other_tips,nullptr,dir.path()+"/presets");
  QImage mask(12,8,QImage::Format_Grayscale8);mask.fill(0);
  for(int y=2;y<6;++y)for(int x=1;x<11;++x)mask.scanLine(y)[x]=255;
  const auto tip=tips.add_tip(QStringLiteral("\u6bdb brush"),mask,.4);CHECK(!tip.isEmpty());first.refresh();
  const auto s=first.resolve(QJsonObject{{"tipId",tip},{"size",23},{"dynamics",QJsonObject{{"wetEdges",true}}}});
  const auto id=first.save(QStringLiteral("\u6cb9 Oil"),s,false);
  other.refresh();CHECK(other.preset(id)["name"]==QStringLiteral("\u6cb9 Oil"));
  CHECK(tips.remove_tip(tip));first.refresh();
  const auto saved=first.resolve(QJsonObject{{"presetId",id}});CHECK(saved.tip != nullptr);CHECK(saved.dynamics.wet_edges);
  CHECK(saved.tip->mask==s.tip->mask);CHECK(saved.spacing==s.spacing);
  const auto revision=first.revision();first.refresh();CHECK(first.revision()==revision);
  CHECK(first.save("Renamed",saved,false,id)==id);other.refresh();CHECK(other.preset(id)["name"]=="Renamed");
  first.remove(id);other.refresh();bool stale=false;
  try{(void)other.resolve(QJsonObject{{"presetId",id}});}catch(const std::exception&){stale=true;}CHECK(stale);
  QFile blocked(dir.path()+"/blocked");CHECK(blocked.open(QIODevice::WriteOnly));blocked.close();
  BrushAutomationLibrary unwritable(tips,nullptr,blocked.fileName());
  const auto unchanged=unwritable.revision();bool failed=false;
  try{(void)unwritable.save("Cannot save",s,false);}catch(const std::exception&){failed=true;}
  CHECK(failed);CHECK(unwritable.revision()==unchanged);CHECK(unwritable.presets().size()==first.presets().size());
}

void ui_script_advanced_brush_creation_preview_and_psd() {
  using namespace patchy::ui;
  MainWindow window;show_window(window);window.set_cli_automation_mode(true);
  CHECK(run_script(window,"app.newDocument(96,96).activeLayer.fill('#dbc8a7');"));
  auto& host=window.script_engine_host();auto& tips=window.brush_tip_library();auto& library=window.brush_automation_library();
  QString tip_id,preset_id;
  const auto cleanup=qScopeGuard([&]{if(!tip_id.isEmpty())tips.remove_tip(tip_id);if(!preset_id.isEmpty()){try{library.remove(preset_id);}catch(const std::exception&){}}});
  CHECK(run_script(window,R"JS(
    var bytes=new Uint8Array(12*8);for(var y=1;y<7;y++)for(var x=1;x<11;x++)bytes[y*12+x]=(x%3)?255:80;
    var tip=patchy.brushes.createTip('Test brush',{width:12,height:8,data:bytes.buffer});
    var preset=patchy.brushes.savePreset('Test oil',{tipId:tip.id,size:17,dynamics:{textureEnabled:true,textureDepth:.4}});
    patchy.setResult({tip:tip.id,preset:preset.id});
  )JS"));
  tip_id=host.last_result().toObject()["tip"].toString();preset_id=host.last_result().toObject()["preset"].toString();CHECK(!tip_id.isEmpty());CHECK(!preset_id.isEmpty());
  auto* combo=window.findChild<QComboBox*>("brushPresetCombo");CHECK(combo);CHECK(combo->findData(preset_id)>=0);
  const auto before=host.automation_fingerprint();
  const auto preview=QStringLiteral("test-artifacts/\u6bdb-swatch.png");
  CHECK(run_script(window,"patchy.brushes.renderPreview('"+preview+"',{presetId:'"+preset_id+"'});"));
  CHECK(QFileInfo::exists(preview));CHECK(host.automation_fingerprint()==before);
  CHECK(run_script(window,"var l=app.activeDocument.addLayer('Fur');l.drawStrokes([{presetId:'"+preset_id+"',color:'#9a4b2b',points:[{x:10,y:20},{x:45,y:60},{x:80,y:30}]}]);"));
  const auto saved=patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
  CHECK(run_script(window,"if(!app.activeDocument.saveAs('test-artifacts/advanced-brush.psd'))throw Error('save');app.activeDocument.close();app.open('test-artifacts/advanced-brush.psd');"));
  const auto reopened=patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
  CHECK(std::equal(saved.data().begin(),saved.data().end(),reopened.data().begin(),reopened.data().end()));
  combo->setCurrentIndex(combo->findData(preset_id));
  CHECK(MainWindowTestAccess::canvas(window)->has_brush_tip());
  for (int repeat=0;repeat<2;++repeat) {
    bool opened=false;
    QTimer::singleShot(0,&window,[&] {
      if(auto* dialog=window.findChild<QInputDialog*>()) {opened=true;dialog->reject();}
    });
    combo->setCurrentIndex(combo->findData("__saveBrush"));
    CHECK(opened);CHECK(combo->currentData().toString()==preset_id);
  }
  save_widget_artifact("advanced_brush_presets",window);
}

// drawStrokes `mode` (Brush Mode, core/paint_mode.hpp): a Multiply dab blends with the
// layer, unknown ids, Pass Through and a Mode on the Eraser fail before painting, and
// getCurrent/activate carry the Brush's mode.
void ui_script_brush_mode_strokes_and_validation() {
  SettingsValueRestorer saved_mode(QStringLiteral("tools/brushMode"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = window.script_engine_host();
  CHECK(run_script(window, QStringLiteral(
      "var d=app.newDocument(32,32); var l=d.addLayer('Ink'); l.fill('#808080');"
      "l.drawStrokes([{tool:'brush',color:'#ff0000',size:9,mode:'multiply',points:[{x:16,y:16}]}]);")));
  const auto* document = host.session_document_const(host.active_session_id());
  CHECK(document != nullptr);
  const auto* ink = patchy::test::ui::scripting_support::layer_named(*document, "Ink");
  CHECK(ink != nullptr);
  const auto* px = ink->pixels().pixel(16 - ink->bounds().x, 16 - ink->bounds().y);
  CHECK(px[0] == 128 && px[1] == 0 && px[2] == 0 && px[3] == 255);
  for (const auto* stroke : {"{mode:'pass-through',points:[{x:4,y:4}]}", "{mode:'Multiply',points:[{x:4,y:4}]}",
                             "{mode:7,points:[{x:4,y:4}]}", "{tool:'eraser',mode:'multiply',points:[{x:4,y:4}]}",
                             "{tool:'mixer',mode:'clear',points:[{x:4,y:4}]}"}) {
    CHECK(run_script(window, QStringLiteral(
        "var r='painted'; try { app.activeDocument.activeLayer.drawStrokes([%1]); } catch (e) { r='threw'; }"
        "patchy.setResult(r);").arg(QString::fromLatin1(stroke))));
    CHECK(host.last_result().toString() == QStringLiteral("threw"));
  }
  CHECK(run_script(window, QStringLiteral(
      "patchy.brushes.activate({tool:'brush',mode:'behind'});"
      "var a=patchy.brushes.getCurrent().mode;"
      "var e=patchy.brushes.resolve({tool:'eraser'}).settings.mode;"
      "patchy.brushes.activate({tool:'brush'});"
      "patchy.setResult([a, e === undefined, patchy.brushes.getCurrent().mode]);")));
  const auto result = host.last_result().toArray();
  CHECK(result.size() == 3);
  CHECK(result.at(0).toString() == QStringLiteral("behind"));
  CHECK(result.at(1).toBool());
  CHECK(result.at(2).toString() == QStringLiteral("normal"));
}

void ui_script_automation_strokes_match_native_and_undo() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral("var d=app.newDocument(64,64); d.addLayer('Ink').fill('#e0d8c8'); d.selection.selectRect(10,10,40,40);")));
  auto& host = window.script_engine_host();
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  canvas->set_zoom(4.0);
  for (const int size : {1, 2, 14}) {
    for (const int flow : {100, 25}) {
      for (const bool pressure : {false, true}) {
        for (const bool erase : {false, true}) {
          const auto points = pressure
              ? QStringLiteral("[{x:12,y:20,pressure:0.1},{x:35,y:40,pressure:1},{x:50,y:18,pressure:0.3}]")
              : QStringLiteral("[{x:12,y:20},{x:35,y:40},{x:50,y:18}]");
          const auto source = QStringLiteral(
              "app.activeDocument.activeLayer.drawStrokes([{size:%1,flow:%2,opacity:65,softness:40,color:'#245b93',seed:9,tool:'%3',points:%4}]);")
              .arg(size).arg(flow).arg(erase ? "eraser" : "brush", points);
          const auto depth = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
          CHECK(run_script(window, source));
          CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
          const auto scripted = patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
          CHECK(run_script(window, QStringLiteral("app.activeDocument.undo();")));
          canvas->set_tool(erase ? patchy::ui::CanvasTool::Eraser : patchy::ui::CanvasTool::Brush);
          canvas->set_primary_color(QColor("#245b93"));
          canvas->set_brush_size(size);
          canvas->set_brush_opacity(65);
          canvas->set_brush_softness(40);
          canvas->set_brush_flow(flow);
          canvas->set_brush_build_up(false);
          canvas->set_brush_tip(nullptr, {});
          canvas->set_brush_dynamics({});
          canvas->set_brush_smoothing(0);
          canvas->set_pen_input_settings({});
          canvas->set_brush_dynamics_test_seed(9);
          if (pressure) {
            using patchy::test::ui::send_tablet;
            const auto point = [&](int x, int y) { return canvas->widget_position_for_document_point(QPoint(x,y)); };
            send_tablet(*canvas, QEvent::TabletPress, point(12,20), 0.1);
            send_tablet(*canvas, QEvent::TabletMove, point(35,40), 1.0, Qt::NoButton);
            send_tablet(*canvas, QEvent::TabletMove, point(50,18), 0.3, Qt::NoButton);
            send_tablet(*canvas, QEvent::TabletRelease, point(50,18), 0.3, Qt::LeftButton, Qt::NoButton);
          } else {
            patchy::test::ui::drag_document_path(*canvas, {{12,20},{35,40},{50,18}}, 1);
          }
          const auto native = patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
          if (!std::equal(scripted.data().begin(), scripted.data().end(), native.data().begin(), native.data().end())) {
            throw std::runtime_error("stroke parity: size=" + std::to_string(size) + " flow=" + std::to_string(flow) +
                                     " pressure=" + std::to_string(pressure) + " erase=" + std::to_string(erase));
          }
          patchy::ui::MainWindowTestAccess::undo(window);
        }
      }
    }
  }
}

void ui_script_automation_preview_ids_and_errors() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  auto& host = window.script_engine_host();
  host.set_connector_mode(true);
  const auto dir = QString::fromUtf8(reinterpret_cast<const char*>(patchy::test::kUnicodeDirName.data()),
                                    static_cast<qsizetype>(patchy::test::kUnicodeDirName.size()));
  QDir().mkpath(QStringLiteral("test-artifacts/") + dir + "/preview");
  const auto base = QStringLiteral("test-artifacts/") + dir + "/preview/";
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("automation-preview");
  options.args = {QStringLiteral("base=") + base};
  options.unattended = true;
  CHECK(host.run_source(QStringLiteral(R"JS(
    var d=app.newDocument(16,16), l=d.addLayer('Ink');
    l.fillRect(4,4,4,4,'#ff0000');
    if(!d.saveAs(patchy.args.base+'doc.psd')) throw new Error('save');
    var before=d.path, changed=d.modified;
    var preview=d.renderPreview(patchy.args.base+'preview.png',
        {rect:{x:4,y:4,width:4,height:4},maxWidth:32,maxHeight:32,nearestNeighbor:true});
    if(d.path!==before || d.modified!==changed) throw new Error('preview changed save state');
    patchy.setResult({documentId:d.id,layerId:l.id,preview:preview});
  )JS"), std::move(options)));
  wait_for_run_end(host);
  CHECK(!host.last_run_had_error());
  const auto result = host.last_result().toObject();
  const QImage png(base + "preview.png");
  CHECK(png.size() == QSize(32,32));
  CHECK(png.pixelColor(5,5) == QColor("#ff0000"));
  const auto id = result["documentId"].toString();
  const auto layer_id = result["layerId"].toString();
  CHECK(run_script(window, QStringLiteral("var l=app.getDocument('%1').getLayer('%2'); l.fill('#00ff00');").arg(id,layer_id)));
  QJsonObject metadata;
  const auto preview = host.render_preview(id.toLongLong(), {}, &metadata);
  CHECK(preview.pixelColor(5,5) == QColor("#00ff00"));
  CHECK(!run_script(window, QStringLiteral("app.getDocument('999999');")));
  CHECK(!run_script(window, QStringLiteral("app.activeDocument.getLayer('999999');")));
  CHECK(!run_script(window, QStringLiteral("app.activeDocument.activeLayer.drawStrokes([{points:[{x:2,y:2}],size:0}]);")));
  CHECK(!run_script(window, QStringLiteral("app.runCommand('file.open');")));
  CHECK(!run_script(window, QStringLiteral("patchy.ui.createCanvas();")));
  CHECK(run_script(window, QStringLiteral("app.getDocument('%1').close();").arg(id)));
  CHECK(!run_script(window, QStringLiteral("app.getDocument('%1');").arg(id)));
}

void ui_script_automation_pressure_seed_selection_palette() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral("var d=app.newDocument(64,64); d.addLayer('Ink'); d.selection.selectRect(10,10,40,40);")));
  auto& host = window.script_engine_host();
  const auto script = QStringLiteral(R"JS(
    app.activeDocument.activeLayer.drawStrokes([
      {size:18,flow:25,opacity:60,softness:50,color:'#ef3322',seed:17,sizeJitter:0.3,scatter:0.4,
       points:[{x:3,y:20,pressure:0.1},{x:24,y:20,pressure:1},{x:58,y:20,pressure:0.3}]},
      {tool:'eraser',size:3,points:[{x:20,y:10},{x:20,y:35}]}
    ]);
  )JS");
  CHECK(run_script(window, script));
  const auto first = patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
  CHECK(run_script(window, QStringLiteral("app.activeDocument.undo();")));
  CHECK(run_script(window, script));
  const auto second = patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
  CHECK(std::equal(first.data().begin(), first.data().end(), second.data().begin(), second.data().end()));
  CHECK(run_script(window, QStringLiteral("app.activeDocument.undo();")));
  auto* canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  CHECK(canvas != nullptr);
  canvas->set_zoom(4.0);
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  canvas->set_primary_color(QColor("#ef3322"));
  canvas->set_brush_size(18);
  canvas->set_brush_opacity(60);
  canvas->set_brush_flow(25);
  canvas->set_brush_softness(50);
  canvas->set_brush_build_up(false);
  canvas->set_brush_tip(nullptr, {});
  patchy::BrushDynamics dynamics;
  dynamics.seed = 17;
  dynamics.size_jitter = 0.3;
  dynamics.scatter = 0.4;
  canvas->set_brush_dynamics(dynamics);
  canvas->set_brush_dynamics_test_seed(17);
  canvas->set_brush_smoothing(0);
  canvas->set_pen_input_settings({});
  using patchy::test::ui::send_tablet;
  const auto point = [&](int x, int y) { return canvas->widget_position_for_document_point(QPoint(x,y)); };
  send_tablet(*canvas, QEvent::TabletPress, point(3,20), 0.1);
  send_tablet(*canvas, QEvent::TabletMove, point(24,20), 1.0, Qt::NoButton);
  send_tablet(*canvas, QEvent::TabletMove, point(58,20), 0.3, Qt::NoButton);
  send_tablet(*canvas, QEvent::TabletRelease, point(58,20), 0.3, Qt::LeftButton, Qt::NoButton);
  canvas->set_tool(patchy::ui::CanvasTool::Eraser);
  canvas->set_brush_size(3);
  canvas->set_brush_opacity(100);
  canvas->set_brush_flow(100);
  canvas->set_brush_softness(0);
  patchy::test::ui::drag_document_path(*canvas, {{20,10},{20,35}}, 1);
  const auto native = patchy::flatten_document_rgba8(*host.session_document_const(host.active_session_id()));
  CHECK(std::equal(first.data().begin(), first.data().end(), native.data().begin(), native.data().end()));
  patchy::ui::MainWindowTestAccess::undo(window);
  patchy::ui::MainWindowTestAccess::undo(window);
  auto* doc = host.session_document(host.active_session_id());
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors = {{0,0,0},{255,0,0}};
  editing.palette_revision = 1;
  doc->palette_editing() = editing;
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.drawStrokes([{size:1,color:'#fb2311',points:[{x:0,y:25},{x:60,y:25}]}]);")));
  const auto* layer = std::as_const(*doc).find_layer(*doc->active_layer_id());
  CHECK(layer != nullptr);
  const auto bounds = layer->bounds();
  // Native storage may include transparent pixels beyond the selection.
  // Assert painted coverage rather than the allocation rectangle.
  for (int y = 0; y < layer->pixels().height(); ++y) {
    for (int x = 0; x < layer->pixels().width(); ++x) {
      if (!QRect(10,10,40,40).contains(x + bounds.x, y + bounds.y)) {
        CHECK(layer->pixels().pixel(x,y)[3] == 0);
      }
    }
  }
  const auto* p = layer->pixels().pixel(20 - bounds.x,25 - bounds.y);
  CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255);
  const auto scripted_palette = patchy::flatten_document_rgba8(std::as_const(*doc));
  CHECK(run_script(window, QStringLiteral("app.activeDocument.undo();")));
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  canvas->set_primary_color(QColor("#fb2311"));
  canvas->set_brush_size(1);
  canvas->set_brush_dynamics({});
  canvas->set_brush_dynamics_test_seed(0);
  patchy::test::ui::drag_document_path(*canvas, {{0,25},{60,25}}, 1);
  const auto native_palette = patchy::flatten_document_rgba8(std::as_const(*doc));
  CHECK(std::equal(scripted_palette.data().begin(), scripted_palette.data().end(),
                   native_palette.data().begin(), native_palette.data().end()));
}

void ui_script_mutations_ride_single_undo_entry() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 0);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Scripted');
    layer.fill('#ff4000');
    layer.opacity = 40;
    layer.blendMode = 'multiply';
    layer.moveTo(5, 7);
    console.log('mutated');
  )JS")));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* layer = layer_named(document, "Scripted");
  CHECK(layer != nullptr);
  CHECK(layer->bounds().x == 5);
  CHECK(layer->bounds().y == 7);
  CHECK(layer->opacity() > 0.39F && layer->opacity() < 0.41F);
  CHECK(layer->blend_mode() == patchy::BlendMode::Multiply);
  const auto* pixel = std::as_const(*layer).pixels().pixel(10, 10);
  CHECK(pixel[0] == 255 && pixel[1] == 64 && pixel[2] == 0 && pixel[3] == 255);
  // Five mutating calls, exactly one history entry; undo removes the whole run.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 1);
  patchy::ui::MainWindowTestAccess::undo(window);
  CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window), "Scripted") == nullptr);
}

void ui_script_stale_layer_wrapper_throws() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Doomed');
    layer.remove();
    var threw = false;
    try { var unused = layer.name; } catch (error) { threw = true; }
    console.log('stale-threw=' + threw);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("stale-threw=true")));
}

void ui_script_shape_feather_and_density() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addShape('Soft', {type: 'rectangle', x: 20, y: 20, width: 60, height: 40});
    layer.updateShape({feather: 4, density: 60});
    var state = layer.getShape();
    console.log('feather=' + state.feather + ' density=' + state.density);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("feather=4 density=60")));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* layer = layer_named(document, "Soft");
  CHECK(layer != nullptr && layer->vector_shape() != nullptr);
  CHECK(std::abs(layer->vector_shape()->feather - 4.0) < 1e-9);
  CHECK(layer->vector_shape()->density == 153);
  // Density floors the whole canvas at 40%.
  CHECK(layer->bounds().x == 0);
  const auto far_alpha = static_cast<int>(std::as_const(*layer).pixels().pixel(2, 2)[3]);
  CHECK(far_alpha >= 98 && far_alpha <= 106);
}

void ui_script_pixels_roundtrip_and_palette_snap() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Pixels');
    var w = 8, h = 4;
    var bytes = new Uint8Array(w * h * 4);
    for (var i = 0; i < w * h; i++) {
      bytes[i * 4] = 10; bytes[i * 4 + 1] = 200; bytes[i * 4 + 2] = 30; bytes[i * 4 + 3] = 255;
    }
    layer.setPixels({x: 3, y: 2, width: w, height: h, data: bytes.buffer});
    var back = layer.getPixels();
    var view = new Uint8Array(back.data);
    console.log('roundtrip=' + back.x + ',' + back.y + ',' + back.width + 'x' + back.height +
                ',' + view[1]);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("roundtrip=3,2,8x4,200")));

  // Palette mode on: script pixel writes snap like every tool write.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  patchy::DocumentPaletteEditing editing;
  editing.palette.colors = {{0, 0, 0}, {255, 0, 0}};
  editing.palette_revision = 1;
  document.palette_editing() = editing;
  CHECK(run_script(window, QStringLiteral(R"JS(
    var layer = app.activeDocument.findLayer('Pixels');
    var bytes = new Uint8Array(4 * 1 * 4);
    for (var i = 0; i < 4; i++) {
      bytes[i * 4] = 250; bytes[i * 4 + 1] = 40; bytes[i * 4 + 2] = 40; bytes[i * 4 + 3] = 200;
    }
    layer.setPixels({x: 0, y: 0, width: 4, height: 1, data: bytes.buffer});
    var view = new Uint8Array(layer.getPixels().data);
    console.log('snapped=' + view[0] + ',' + view[1] + ',' + view[2] + ',' + view[3]);
  )JS")));
  // 250,40,40 snaps to the red entry; alpha 200 hardens to 255.
  CHECK(backlog_contains(window, QStringLiteral("snapped=255,0,0,255")));
  document.palette_editing().reset();
}

void ui_script_get_pixels_reads_rgb_layers() {
  patchy::ui::MainWindow window;
  show_window(window);
  // Opaque opened photos (JPEG and friends) store 3-channel RGB layers;
  // getPixels hands scripts RGBA with alpha 255 and setPixels writes RGBA8.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  patchy::PixelBuffer rgb(document.width(), document.height(), patchy::PixelFormat::rgb8());
  const auto span = rgb.data();
  for (std::size_t i = 0; i < span.size(); i += 3) {
    span[i] = 200;
    span[i + 1] = 100;
    span[i + 2] = 50;
  }
  document.add_layer(patchy::Layer(document.allocate_layer_id(), "RgbPhoto", std::move(rgb)));
  CHECK(run_script(window, QStringLiteral(R"JS(
    var layer = app.activeDocument.findLayer('RgbPhoto');
    var img = layer.getPixels();
    var view = new Uint8Array(img.data);
    console.log('rgb=' + img.width + 'x' + img.height + ',' +
                view[0] + ',' + view[1] + ',' + view[2] + ',' + view[3]);
    view[0] = 10;
    layer.setPixels(img);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("rgb=1024x768,200,100,50,255")));
  const auto* layer = layer_named(patchy::ui::MainWindowTestAccess::document(window), "RgbPhoto");
  CHECK(layer != nullptr);
  CHECK(layer->pixels().format().channels == 4);
  const auto* pixel = layer->pixels().pixel(0, 0);
  CHECK(pixel[0] == 10 && pixel[1] == 100 && pixel[2] == 50 && pixel[3] == 255);
}

void ui_script_fill_rect_partial_updates() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Sprite');
    // Empty layer + fillRect allocates a buffer covering exactly the rect.
    layer.fillRect(20, 30, 8, 4, '#ff0000');
    var b = layer.bounds;
    console.log('alloc=' + b.x + ',' + b.y + ',' + b.width + 'x' + b.height);
    // Partial overwrite, then a transparent clear of the same sub-rect.
    layer.fillRect(22, 30, 2, 2, '#00ff00');
    var v1 = new Uint8Array(layer.getPixels().data);
    layer.fillRect(22, 30, 2, 2, '#00000000');
    var v2 = new Uint8Array(layer.getPixels().data);
    // Buffer-local pixel (2,0) = document (22,30); (0,0) stays red throughout.
    console.log('painted=' + v1[2 * 4 + 1] + ' cleared=' + v2[2 * 4 + 3] +
                ' kept=' + v2[0] + ',' + v2[3]);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("alloc=20,30,8x4")));
  CHECK(backlog_contains(window, QStringLiteral("painted=255 cleared=0 kept=255,255")));
}

// layer.removeObject: the selection form of Spot Healing through the script
// API. Uniform surroundings heal the marked pixels to the base color exactly;
// a repeat walks the source cycle, an explicit attempt pins it, and a missing
// selection throws.
void ui_script_remove_object_heals_selection() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Heal');
    layer.fillRect(0, 0, 64, 64, '#2850a0');
    layer.fillRect(30, 30, 4, 4, '#ffffff');
    doc.activeLayer = layer;
    doc.selection.selectRect(28, 28, 8, 8);
    var r0 = layer.removeObject();
    console.log('method=' + r0.method + ' patches=' + (r0.patches > 0));
    var r1 = layer.removeObject({method: 'nearestEdge'});
    var r2 = layer.removeObject({method: 'nearestEdge'});
    var r3 = layer.removeObject({method: 'nearestEdge', attempt: 0});
    console.log('sources=' + r1.source + ',' + r2.source + ',' + r3.source + ' of ' + r1.sourceCount);
    var v = layer.removeObject({attempt: 2, toneMatch: 0, feather: 2});
    console.log('variation=' + v.attempt + ' method=' + v.method + ' patches=' + (v.patches > 0));
    try {
      layer.removeObject({sharpen: 1});
      console.log('option-no-throw');
    } catch (e) {
      console.log('option-refused=' + (e.message.indexOf('sharpen') >= 0));
    }
    var px = new Uint8Array(layer.getPixels().data);
    var i = (31 * 64 + 31) * 4;
    console.log('healed=' + px[i] + ',' + px[i + 1] + ',' + px[i + 2] + ',' + px[i + 3]);
    doc.selection.deselect();
    try {
      layer.removeObject();
      console.log('no-throw');
    } catch (e) {
      console.log('refused=' + (e.message.length > 0));
    }
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("method=contentAware patches=true")));
  CHECK(backlog_contains(window, QStringLiteral("sources=1,2,1 of ")));
  CHECK(backlog_contains(window, QStringLiteral("variation=2 method=contentAware patches=true")));
  CHECK(backlog_contains(window, QStringLiteral("option-refused=true")));
  CHECK(!backlog_contains(window, QStringLiteral("option-no-throw")));
  CHECK(backlog_contains(window, QStringLiteral("healed=40,80,160,255")));
  CHECK(backlog_contains(window, QStringLiteral("refused=true")));
  CHECK(!backlog_contains(window, QStringLiteral("no-throw")));
}

// A big photo (about 98 MB RGBA) healed with removeObject and then re-opened
// from the same file, the headless door_reopen.js scenario: the first document's
// deferred refresh and background composite must not touch memory the second
// open frees (September 2026 crash, about one run in two). Needs the local
// fixture copied per AGENTS.md; skips without it.
void ui_script_remove_object_then_reopen_large_document() {
  const auto fixture = patchy::test::local_format_fixture_path("remove-object-reopen", "door.jpg");
  patchy::test::skip_unless_exists(fixture);
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QDir::fromNativeSeparators(patchy::ui::to_qstring(fixture));
  auto& host = start_script(window, QStringLiteral(R"JS(
    var path = '%1';
    var doc = app.open(path);
    var layer = doc.activeLayer;
    doc.selection.selectRect(1830, 1720, 900, 470);
    var r = layer.removeObject();
    doc.selection.deselect();
    var again = app.open(path);
    console.log('reopen=' + again.width + 'x' + again.height + ' method=' + r.method);
  )JS").arg(path));
  wait_for_run_end(host, 120000);
  CHECK(!host.run_active());
  CHECK(!host.last_run_had_error());
  // The method is whatever the fill decides for this photo; the reopen is the point.
  CHECK(backlog_contains(window, QStringLiteral("reopen=4284x5712 method=")));
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == 3);
  // Let every deferred refresh and background composite of both documents land
  // while the sessions are still alive.
  patchy::test::ui::process_events_for(1500);
}

void ui_script_align_and_distribute_layers() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var a = doc.addLayer('A'); a.fillRect(10, 10, 20, 20, '#ff0000');
    var b = doc.addLayer('B'); b.fillRect(60, 40, 30, 10, '#00ff00');
    var c = doc.addLayer('C'); c.fillRect(100, 90, 20, 30, '#0000ff');
    var moved = doc.alignLayers('left', {layers: [a, b, c]});
    console.log('aligned=' + moved);
    moved = doc.alignLayers('left', {layers: [a, b, c]});
    console.log('again=' + moved);
    moved = doc.distributeLayers('vcenter', {layers: [a, b, c]});
    console.log('distributed=' + moved);
    moved = doc.alignLayers('hcenter', {layers: [b], alignTo: 'canvas'});
    console.log('canvas=' + moved);
    try { doc.distributeLayers('left', {layers: [a, b]}); console.log('no-throw'); }
    catch (e) { console.log('refused=' + (e.message.indexOf('three') >= 0)); }
    try { doc.alignLayers('middle'); console.log('no-throw'); }
    catch (e) { console.log('bad-edge=' + (e.message.indexOf('middle') >= 0)); }
    try { doc.alignLayers('left', {bogus: 1}); console.log('no-throw'); }
    catch (e) { console.log('bad-option=' + (e.message.indexOf('bogus') >= 0)); }
  )JS")));
  // Align works on the layers' opaque rects (A already sits at the union's left
  // edge, so two layers move); vcenter distribute puts B's center at
  // (20 + 105) / 2 = 62.5 -> 63 (top 58); the canvas hcenter centers B's 30 px.
  CHECK(backlog_contains(window, QStringLiteral("aligned=2")));
  CHECK(backlog_contains(window, QStringLiteral("again=0")));
  CHECK(backlog_contains(window, QStringLiteral("distributed=1")));
  CHECK(backlog_contains(window, QStringLiteral("canvas=1")));
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto opaque = [&](const char* name) {
    const auto* layer = layer_named(document, name);
    return layer != nullptr ? patchy::ui::move_layer_outline_bounds(*layer).value_or(patchy::Rect{})
                            : patchy::Rect{};
  };
  CHECK(opaque("A").x == 10 && opaque("C").x == 10);
  CHECK(opaque("A").y == 10 && opaque("C").y == 90);
  CHECK(opaque("B").y == 58);
  CHECK(opaque("B").x == document.width() / 2 - 15);
  CHECK(backlog_contains(window, QStringLiteral("refused=true")));
  CHECK(backlog_contains(window, QStringLiteral("bad-edge=true")));
  CHECK(backlog_contains(window, QStringLiteral("bad-option=true")));
  CHECK(!backlog_contains(window, QStringLiteral("no-throw")));
  // Every mutation rode the run's single history entry.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 1);
}

void ui_script_canvas_window_receives_space_key() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = start_script(window, QStringLiteral(R"JS(
    var win = patchy.ui.createCanvas({width: 120, height: 90, title: 'Keys'});
    win.onKeyDown = function (key) { console.log('key=' + key); };
    win.onFrame = function () {};
  )JS"));
  CHECK(host.run_active());
  auto* dialog = window.findChild<QDialog*>(QStringLiteral("scriptCanvasWindowDialog"));
  CHECK(dialog != nullptr);
  auto* surface = dialog->findChild<QWidget*>(QStringLiteral("scriptCanvasSurface"));
  CHECK(surface != nullptr);
  // Space must reach the game window instead of being swallowed by the
  // application-level spacebar canvas pan filter.
  QTest::keyClick(surface, Qt::Key_Space);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
  CHECK(backlog_contains(window, QStringLiteral("key=Space")));
  host.stop_active_run();
  wait_for_run_end(host);
}

// A script that has already been working long enough to raise the stop panel
// must not create its canvas window underneath it.
void ui_script_canvas_window_dismisses_stop_panel() {
  const EnvironmentVariableRestorer restore_busy_delay("PATCHY_SCRIPT_BUSY_DELAY_MS");
  qputenv("PATCHY_SCRIPT_BUSY_DELAY_MS", "0");
  {
    patchy::ui::MainWindow window;
    show_window(window);
    StopPanelOverlapWatch watch(window);
    // Busy long enough to raise the panel, open the window, then stay busy so
    // the sampler gets ticks in which the two could still overlap.
    auto& host = start_script(window, QStringLiteral(R"JS(
      var doc = app.activeDocument;
      var layer = doc.addLayer('busy');
      var start = Date.now();
      while (Date.now() - start < 150) { layer.fillRect(0, 0, 2, 2, '#00ff00'); }
      var win = patchy.ui.createCanvas({width: 120, height: 90, title: 'Game'});
      win.onKeyDown = function (key) { console.log('key=' + key); };
      win.onFrame = function () {};
      start = Date.now();
      while (Date.now() - start < 150) { layer.fillRect(0, 0, 2, 2, '#00ff00'); }
    )JS"));
    watch.stop();
    CHECK(host.run_active());
    CHECK(watch.saw_panel());    // the burst really was slow enough to raise it
    CHECK(!watch.overlapped());  // ...and it was gone before the window existed
    auto* dialog = window.findChild<QDialog*>(QStringLiteral("scriptCanvasWindowDialog"));
    CHECK(dialog != nullptr);
    CHECK(QApplication::activeModalWidget() == nullptr);
    // The window is usable: keys reach the script rather than being dropped.
    auto* surface = dialog->findChild<QWidget*>(QStringLiteral("scriptCanvasSurface"));
    CHECK(surface != nullptr);
    QTest::keyClick(surface, Qt::Key_Space);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
    CHECK(backlog_contains(window, QStringLiteral("key=Space")));
    host.stop_active_run();
    wait_for_run_end(host);
  }
}

// While a script owns an open canvas window, a slow frame callback must not
// raise the stop panel over it (same blocking hazard, plus a game behind an
// application-modal panel is unplayable on every platform).
void ui_script_canvas_window_suppresses_stop_panel() {
  const EnvironmentVariableRestorer restore_busy_delay("PATCHY_SCRIPT_BUSY_DELAY_MS");
  qputenv("PATCHY_SCRIPT_BUSY_DELAY_MS", "0");
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto& host = start_script(window, QStringLiteral(R"JS(
      var doc = app.activeDocument;
      var layer = doc.addLayer('game');
      var win = patchy.ui.createCanvas({width: 120, height: 90, title: 'Game'});
      var frames = 0;
      win.onFrame = function () {
        var start = Date.now();
        while (Date.now() - start < 60) { layer.fillRect(0, 0, 2, 2, '#0000ff'); }
        console.log('frame=' + (++frames));
      };
    )JS"));
    CHECK(host.run_active());
    CHECK(window.findChild<QDialog*>(QStringLiteral("scriptCanvasWindowDialog")) != nullptr);
    // Sample across several slow frames; each one crosses the zeroed threshold.
    StopPanelOverlapWatch watch(window);
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 600) {
      QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
    }
    watch.stop();
    CHECK(backlog_contains(window, QStringLiteral("frame=3")));  // slow frames really ran
    CHECK(!watch.saw_panel());
    CHECK(QApplication::activeModalWidget() == nullptr);
    host.stop_active_run();
    wait_for_run_end(host);
  }
}

void ui_script_undo_disable_skips_history() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    app.undoEnabled = false;
    var doc = app.activeDocument;
    doc.addLayer('NoUndo').fill('#123456');
    console.log('undo-flag=' + app.undoEnabled);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("undo-flag=false")));
  CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window), "NoUndo") != nullptr);
  // No history entry, but the session still counts as modified work.
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 0);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));

  // Re-enabling mid-run snapshots from that point on (and resets per run).
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    app.undoEnabled = false;
    doc.addLayer('StillNoUndo');
    app.undoEnabled = true;
    doc.addLayer('UndoAgain');
  )JS")));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 1);
}

void ui_script_timer_keeps_run_alive() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = start_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    setTimeout(function () {
      doc.addLayer('FromTimer');
      console.log('timer-done');
    }, 60);
  )JS"));
  CHECK(host.run_active());  // sync code finished, the timer keeps it alive
  wait_for_run_end(host);
  CHECK(!host.run_active());
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("timer-done")));
  CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window), "FromTimer") != nullptr);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == 1);
}

void ui_script_watchdog_interrupts_infinite_loop() {
  const EnvironmentVariableRestorer restore_timeout("PATCHY_SCRIPT_TIMEOUT_MS");
  qputenv("PATCHY_SCRIPT_TIMEOUT_MS", QByteArray("300"));
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto& host = start_script(window, QStringLiteral("while (true) {}"));
    wait_for_run_end(host);
    CHECK(!host.run_active());
    CHECK(host.last_run_had_error());
    CHECK(backlog_contains(window, QStringLiteral("no activity")));
  }
}

// The watchdog measures INACTIVITY, not runtime: a script that keeps making
// API calls outlives any number of windows (a contact sheet may run for
// hours), while total silence still dies (the test above).
void ui_script_watchdog_allows_busy_scripts() {
  const EnvironmentVariableRestorer restore_timeout("PATCHY_SCRIPT_TIMEOUT_MS");
  // The window is generous so a loaded machine (a release run beside a build)
  // cannot stretch one ~50 ms ping gap past it; the run still lasts more than
  // two windows, so only inactivity, never total runtime, can be measured.
  qputenv("PATCHY_SCRIPT_TIMEOUT_MS", QByteArray("1500"));
  {
    patchy::ui::MainWindow window;
    show_window(window);
    // ~3.2 s of wall-clock work with a console ping every ~50 ms; each ping
    // feeds the watchdog, so the run must complete.
    CHECK(run_script(window, QStringLiteral(R"JS(
      var start = Date.now();
      var pings = 0;
      while (Date.now() - start < 3200) {
        var t = Date.now();
        while (Date.now() - t < 50) {}
        console.log('ping ' + (++pings));
      }
      console.log('busy-done');
    )JS")));
    CHECK(backlog_contains(window, QStringLiteral("busy-done")));
  }
}

// The running-script stop panel: appears once a burst crosses the
// (env-zeroed) threshold; Stop opens the NON-BLOCKING confirm (the script
// keeps working underneath) - Cancel just dismisses it, Stop Script with the
// undo box checked interrupts the run and rolls the document back to its
// pre-script state.
void ui_script_stop_panel_confirm_and_undo() {
  const EnvironmentVariableRestorer restore_busy_delay("PATCHY_SCRIPT_BUSY_DELAY_MS");
  qputenv("PATCHY_SCRIPT_BUSY_DELAY_MS", "0");
  {
    patchy::ui::MainWindow window;
    show_window(window);
    auto& host = window.script_engine_host();

    // Driven from a repeating timer: the pump's processEvents runs it while
    // the script blocks the UI thread. One panel click and one confirm answer
    // per run.
    bool click_panel = false;
    bool confirm_with_undo = false;
    bool panel_clicked = false;
    bool confirm_answered = false;
    bool artifact_saved = false;
    const char* pending_layer_name = "";
    auto* driver = new QTimer(&window);
    QObject::connect(driver, &QTimer::timeout, &window, [&] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr || !dialog->isVisible()) {
          continue;
        }
        if (dialog->objectName() == QStringLiteral("scriptStopConfirmDialog") &&
            !confirm_answered) {
          confirm_answered = true;
          if (confirm_with_undo) {
            auto* undo_box =
                dialog->findChild<QCheckBox*>(QStringLiteral("scriptStopUndoCheckBox"));
            CHECK(undo_box != nullptr && undo_box->isVisible());
            undo_box->setChecked(true);
            dialog->findChild<QPushButton*>(QStringLiteral("scriptStopConfirmButton"))->click();
          } else {
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
          }
        } else if (dialog->objectName() == QStringLiteral("scriptStopPanel") && click_panel &&
                   !panel_clicked) {
          // The script's first mutation pumps events before its undo snapshot
          // is recorded, so the panel can appear (and be clicked) with nothing
          // to undo yet; the confirm samples the snapshot state when it opens.
          // Wait until the script's layer landed before stopping the run.
          if (layer_named(patchy::ui::MainWindowTestAccess::document(window),
                          pending_layer_name) == nullptr) {
            continue;
          }
          panel_clicked = true;
          if (!artifact_saved) {
            artifact_saved = true;
            save_widget_artifact("script_stop_panel", *dialog);
          }
          dialog->findChild<QPushButton*>(QStringLiteral("scriptStopPanelButton"))->click();
        }
      }
    });
    driver->start(30);

    const auto busy_script = QStringLiteral(R"JS(
      var doc = app.activeDocument;
      var layer = doc.addLayer('%1');
      var start = Date.now();
      while (Date.now() - start < 700) {
        layer.fillRect(0, 0, 2, 2, '#ff0000');
      }
      console.log('survived');
    )JS");

    // Pass 1: Cancel at the confirm - the script must finish normally.
    click_panel = true;
    pending_layer_name = "stop-cancel-layer";
    CHECK(run_script(window, busy_script.arg(QStringLiteral("stop-cancel-layer"))));
    CHECK(panel_clicked);
    CHECK(confirm_answered);
    CHECK(backlog_contains(window, QStringLiteral("survived")));
    CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window),
                      "stop-cancel-layer") != nullptr);

    // Pass 2: Stop Script with undo checked - the run dies stopped and the
    // layer it added is rolled back (pass 1's layer stays, proving only the
    // stopped run's snapshot was undone).
    panel_clicked = false;
    confirm_answered = false;
    confirm_with_undo = true;
    pending_layer_name = "stop-undo-layer";
    patchy::ui::ScriptEngineHost::RunOptions options;
    options.name = QStringLiteral("stoppable");
    (void)host.run_source(busy_script.arg(QStringLiteral("stop-undo-layer")), std::move(options));
    wait_for_run_end(host);
    driver->stop();
    CHECK(panel_clicked);
    CHECK(confirm_answered);
    CHECK(host.last_run_had_error());
    CHECK(backlog_contains(window, QStringLiteral("Script stopped.")));
    CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window),
                      "stop-undo-layer") == nullptr);
    CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window),
                      "stop-cancel-layer") != nullptr);
    auto* panel = window.findChild<QDialog*>(QStringLiteral("scriptStopPanel"));
    CHECK(panel != nullptr && !panel->isVisible());
    // Slow mode creates several groups before the Stop confirmation. Its Undo
    // option must restore all retained steps of this run, not just the last one.
    panel_clicked = false;
    confirm_answered = false;
    pending_layer_name = "stop-slow-layer";
    const auto before_slow = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
    driver->start(30);
    (void)host.run_source(QStringLiteral(R"JS(
      patchy.ui.slowMode=true;
      app.activeDocument.addLayer('slow-earlier-layer').fill('#335577');
    )JS") + busy_script.arg(QStringLiteral("stop-slow-layer")), {});
    wait_for_run_end(host);
    driver->stop();
    CHECK(panel_clicked && confirm_answered && host.last_run_had_error());
    CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == before_slow);
    CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window), "slow-earlier-layer") == nullptr);
    CHECK(layer_named(patchy::ui::MainWindowTestAccess::document(window), "stop-slow-layer") == nullptr);
  }
}

// The busy overlay and stop panel step aside while the script shows its own
// modal (alert here): ModalWatchdogPause ends the indicator on entry.
void ui_script_busy_panel_yields_to_script_dialogs() {
  const EnvironmentVariableRestorer restore_busy_delay("PATCHY_SCRIPT_BUSY_DELAY_MS");
  qputenv("PATCHY_SCRIPT_BUSY_DELAY_MS", "0");
  {
    patchy::ui::MainWindow window;
    show_window(window);
    bool alert_seen = false;
    bool panel_visible_during_alert = false;
    auto* dismisser = new QTimer(&window);
    QObject::connect(dismisser, &QTimer::timeout, &window, [&] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* box = qobject_cast<QMessageBox*>(widget);
        if (box != nullptr && box->isVisible() &&
            box->objectName() == QStringLiteral("scriptAlertMessageBox")) {
          alert_seen = true;
          auto* panel = window.findChild<QDialog*>(QStringLiteral("scriptStopPanel"));
          panel_visible_during_alert =
              panel_visible_during_alert || (panel != nullptr && panel->isVisible());
          box->accept();
        }
      }
    });
    dismisser->start(30);
    CHECK(run_script(window, QStringLiteral(R"JS(
      var doc = app.activeDocument;
      var layer = doc.addLayer('yield-test');
      var start = Date.now();
      while (Date.now() - start < 150) {
        layer.fillRect(0, 0, 2, 2, '#00ff00');
      }
      app.alert('paused at a dialog');
      console.log('after-alert');
    )JS")));
    dismisser->stop();
    CHECK(alert_seen);
    CHECK(!panel_visible_during_alert);
    CHECK(backlog_contains(window, QStringLiteral("after-alert")));
  }
}

void ui_script_console_and_error_line_numbers() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(!run_script(window, QStringLiteral("console.log('hello console');\nthrow new Error('boom');")));
  CHECK(backlog_contains(window, QStringLiteral("hello console")));
  CHECK(backlog_contains(window, QStringLiteral("boom")));
  CHECK(backlog_contains(window, QStringLiteral("test-script:2")));
}

void ui_script_filters_and_text_layers() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addLayer('Filtered');
    layer.fill('#ff0000');
    layer.applyFilter('patchy.filters.invert');
    var view = new Uint8Array(layer.getPixels().data);
    console.log('inverted=' + view[0] + ',' + view[1] + ',' + view[2]);
    var text = doc.addTextLayer('Scripted Text', {size: 24, x: 40, y: 80});
    console.log('text=' + text.isText + ',' + text.text);
    var badMode = false;
    try { text.blendMode = 'no-such-mode'; } catch (error) { badMode = true; }
    console.log('bad-mode-threw=' + badMode);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("inverted=0,255,255")));
  CHECK(backlog_contains(window, QStringLiteral("text=true,Scripted Text")));
  CHECK(backlog_contains(window, QStringLiteral("bad-mode-threw=true")));
}

// A soft selection (a feather, Object Selection's edge) limits destructive edits by its
// coverage, as in Photoshop: a half-covered pixel lands halfway, an uncovered one keeps its
// color. Covers the menu's filter (Invert) and adjustment (Levels) paths and the script's
// applyFilter (which used to ignore the selection) and fill.
void ui_destructive_edits_blend_by_soft_selection_coverage() {
  patchy::Document source(40, 10, patchy::PixelFormat::rgba8());
  source.add_pixel_layer("Paint", solid_pixels(40, 10, patchy::PixelFormat::rgba8(), QColor(200, 40, 40)));
  patchy::ui::MainWindow window;
  window.add_document_session(std::move(source), QStringLiteral("Soft selection"));
  show_window(window);
  auto* canvas = require_canvas(window);
  // Uncovered left of x 10, half covered to x 20, fully covered from there.
  patchy::PixelBuffer coverage(40, 10, patchy::PixelFormat::gray8());
  for (int y = 0; y < 10; ++y) {
    for (int x = 0; x < 40; ++x) {
      coverage.pixel(x, y)[0] = x < 10 ? 0 : (x < 20 ? 128 : 255);
    }
  }
  canvas->apply_grayscale_to_selection(coverage);
  QApplication::processEvents();
  CHECK(canvas->has_selection() && canvas->selection_has_partial_alpha());

  const QColor start(200, 40, 40);
  const auto layer_color = [&](int x) {
    const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto* layer = document.find_layer(*document.active_layer_id());
    const auto* pixel = layer->pixels().pixel(x, 5);
    return QColor(pixel[0], pixel[1], pixel[2], pixel[3]);
  };
  const auto check_blend = [&](const char* label, QColor full, const std::function<void()>& edit) {
    {
      auto& document = patchy::ui::MainWindowTestAccess::document(window);
      document.find_layer(*document.active_layer_id())
          ->set_pixels(solid_pixels(40, 10, patchy::PixelFormat::rgba8(), start));
      canvas->document_changed();
    }
    edit();
    QApplication::processEvents();
    const auto half = 128.0 / 255.0;
    const QColor midway(static_cast<int>(std::lround(start.red() + (full.red() - start.red()) * half)),
                        static_cast<int>(std::lround(start.green() + (full.green() - start.green()) * half)),
                        static_cast<int>(std::lround(start.blue() + (full.blue() - start.blue()) * half)));
    const auto outside = layer_color(5);
    const auto edge = layer_color(15);
    const auto inside = layer_color(30);
    const auto ok = color_close(outside, start, 0) && color_close(edge, midway, 2) && color_close(inside, full, 2);
    if (!ok) {
      std::cerr << label << ": outside " << outside.name().toStdString() << " edge " << edge.name().toStdString()
                << " (want " << midway.name().toStdString() << ") inside " << inside.name().toStdString() << '\n';
    }
    CHECK(ok);
  };

  check_blend("menu invert", QColor(55, 215, 215), [&] {
    accept_filter_dialog();
    require_action(window, "imageAdjustInvertAction")->trigger();
  });
  // White input 128 doubles every channel: 200 clips to 255, 40 becomes 80.
  check_blend("menu levels", QColor(255, 80, 80), [&] {
    accept_levels_dialog(0, 128, 100);
    require_action(window, "imageAdjustLevelsAction")->trigger();
  });
  check_blend("script applyFilter", QColor(55, 215, 215), [&] {
    CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.applyFilter('patchy.filters.invert');")));
  });
  check_blend("script fill", QColor(0, 0, 255), [&] {
    CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.fill('#0000ff');")));
  });
}

// doc.bitDepth / convertBitDepth and filters on 16/32-bit layers
// (docs/high-bit-depth.md): a deep kernel (Invert), the 8-bit-precision path on 16
// bits (Median), Median refused on 32 bits, and the Filter menu following the depth.
void ui_script_bit_depth_and_deep_filters() {
  patchy::set_deep_editing_override(true);
  {
    patchy::ui::MainWindow window;
    show_window(window);
    CHECK(run_script(window, QStringLiteral(R"JS(
      var doc = app.activeDocument;
      console.log('depth-start=' + doc.bitDepth);
      doc.convertBitDepth(16);
      console.log('depth-after=' + doc.bitDepth);
      var layer = doc.addLayer('Deep');
      layer.fill('#336699');
      layer.applyFilter('patchy.filters.invert');
      var view = new Uint8Array(layer.getPixels().data);
      console.log('deep-inverted=' + view[0] + ',' + view[1] + ',' + view[2]);
      layer.applyFilter('patchy.filters.median');
      var bad = false;
      try { doc.convertBitDepth(12); } catch (error) { bad = true; }
      console.log('bad-bits=' + bad);
    )JS")));
    CHECK(backlog_contains(window, QStringLiteral("depth-start=8")));
    CHECK(backlog_contains(window, QStringLiteral("depth-after=16")));
    CHECK(backlog_contains(window, QStringLiteral("deep-inverted=204,153,102")));
    CHECK(backlog_contains(window, QStringLiteral("bad-bits=true")));
    const auto& document = patchy::ui::MainWindowTestAccess::document(window);
    const auto* deep_layer = layer_named(document, "Deep");
    CHECK(deep_layer != nullptr && deep_layer->pixels().format().bit_depth == patchy::BitDepth::UInt16);
    const auto find_action = [&window](const QString& name) { return window.findChild<QAction*>(name); };
    auto* median_action = find_action(QStringLiteral("filterAction_patchy_filters_median"));
    auto* gaussian_action = find_action(QStringLiteral("filterAction_patchy_filters_gaussian_blur"));
    CHECK(median_action != nullptr && gaussian_action != nullptr);
    CHECK(median_action != nullptr && median_action->isEnabled());

    CHECK(run_script(window, QStringLiteral(R"JS(
      var doc = app.activeDocument;
      doc.convertBitDepth(32);
      var layer = doc.activeLayer;
      var threw = false;
      try { layer.applyFilter('patchy.filters.median'); } catch (error) { threw = true; }
      console.log('median32-threw=' + threw);
      layer.applyFilter('patchy.filters.gaussian_blur', {radius: 2});
      console.log('depth32=' + doc.bitDepth);
      var screenThrew = false;
      try { layer.blendMode = 'screen'; } catch (error) { screenThrew = true; }
      layer.blendMode = 'multiply';
      console.log('screen32-threw=' + screenThrew + ' mode=' + layer.blendMode);
    )JS")));
    CHECK(backlog_contains(window, QStringLiteral("median32-threw=true")));
    CHECK(backlog_contains(window, QStringLiteral("depth32=32")));
    CHECK(backlog_contains(window, QStringLiteral("screen32-threw=true mode=multiply")));
    CHECK(median_action != nullptr && !median_action->isEnabled());
    CHECK(gaussian_action != nullptr && gaussian_action->isEnabled());
    CHECK(patchy::document_depth_problems(patchy::ui::MainWindowTestAccess::document(window)).empty());
  }
  patchy::set_deep_editing_override(std::nullopt);
}

// 16/32-bit documents save to the 8-bit-only formats through a copy narrowed to 8 bits
// (the native flat writers, SVG, Aseprite, and the export-transform stand-in), instead
// of failing in a writer that composites at 8 bits.
void ui_script_deep_documents_save_to_8_bit_formats() {
  patchy::set_deep_editing_override(true);
  {
    patchy::ui::MainWindow window;
    show_window(window);
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const auto base = directory.path() + QStringLiteral("/deep");
    auto& host = window.script_engine_host();
    patchy::ui::ScriptEngineHost::RunOptions options;
    options.name = QStringLiteral("deep-saves");
    options.args = QStringList{QStringLiteral("base=") + base};
    const auto source = QStringLiteral(R"JS(
      var p = patchy.args.base;
      var exts = ['bmp', 'dds', 'tga', 'gif', 'pcx', 'ico', 'rttex', 'iff', 'svg', 'aseprite'];
      [16, 32].forEach(function (bits) {
        var d = app.newDocument(16, 16);
        d.activeLayer.fill('#ff8000');
        d.convertBitDepth(bits);
        exts.forEach(function (ext) {
          if (!d.exportAs(p + bits + '.' + ext)) throw Error(ext + ' at ' + bits + ' bits');
        });
        if (d.bitDepth !== bits) throw Error('exportAs changed the depth');
        var back = app.open(p + bits + '.bmp');
        var px = new Uint8Array(back.activeLayer.getPixels().data);
        console.log('bmp' + bits + '=' + px[0] + ',' + px[1] + ',' + px[2]);
      });
    )JS");
    (void)host.run_source(source, std::move(options));
    wait_for_run_end(host);
    CHECK(!host.run_active());
    if (host.last_run_had_error()) {
      for (const auto& line : host.message_backlog()) std::cerr << line.toStdString() << '\n';
    }
    CHECK(!host.last_run_had_error());
    CHECK(backlog_contains(window, QStringLiteral("bmp16=255,128,0")));
    CHECK(backlog_contains(window, QStringLiteral("bmp32=255,128,0")));

    // An export transform (here Scale) builds an 8-bit stand-in, so it narrows too.
    patchy::Document deep(8, 8, patchy::PixelFormat::rgba8());
    patchy::PixelBuffer orange(8, 8, patchy::PixelFormat::rgba8());
    for (std::int32_t y = 0; y < 8; ++y) {
      for (std::int32_t x = 0; x < 8; ++x) {
        auto* px = orange.pixel(x, y);
        px[0] = 255;
        px[1] = 128;
        px[2] = 0;
        px[3] = 255;
      }
    }
    deep.add_pixel_layer("Layer", std::move(orange));
    patchy::convert_document_depth(deep, patchy::BitDepth::UInt16);
    patchy::ui::ImageSaveOptions scaled;
    scaled.export_scale = 2;
    const auto png_path = base + QStringLiteral("-scaled.png");
    patchy::ui::write_flat_image_file(deep, png_path, QStringLiteral("png"), scaled);
    const QImage png(png_path);
    CHECK(png.width() == 16 && png.height() == 16);
    CHECK(QColor(png.pixel(3, 3)) == QColor(255, 128, 0));
  }
  patchy::set_deep_editing_override(std::nullopt);
}

// addTextLayer's size is document pixels: the committed raster must not depend
// on the canvas zoom (the July 2026 dialog-showcase overlap bug: the script
// path set a point-sized font on the zoom-scaled inline editor).
void ui_script_text_size_is_zoom_independent() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var a = doc.addTextLayer('Zoom probe', {size: 24, x: 10, y: 40});
    app.runCommand('view.zoom_out');
    app.runCommand('view.zoom_out');
    var b = doc.addTextLayer('Zoom probe', {size: 24, x: 10, y: 200});
    console.log('a=' + a.bounds.width + 'x' + a.bounds.height +
                ' b=' + b.bounds.width + 'x' + b.bounds.height);
    // The editor font rounds to whole editor pixels, so fractional zooms may
    // drift the committed size by a pixel or two; the bug this guards against
    // committed ~2x bigger, so a proportional tolerance is enough.
    function close(u, v) {
      return Math.abs(u - v) <= Math.max(3, Math.round(0.12 * Math.max(u, v)));
    }
    console.log('match=' + (close(a.bounds.width, b.bounds.width) &&
                            close(a.bounds.height, b.bounds.height)));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("match=true")));
}

// addTextLayer's font option used to be dropped: the script set the family on the editor's
// char format only, while the commit reads the session family, so every script-made text layer
// rendered in the options bar's current font (September 2026; an AI-built poster fell back to
// drawing its lettering as vector outlines). Two different registered families both have to
// stick, whichever one the bar happens to hold.
void ui_script_text_font_option_applies() {
  patchy::ui::MainWindow window;
  show_window(window);
  QStringList families;
  for (const auto& family : QFontDatabase::families()) {
    if (!family.startsWith(QLatin1Char('.')) && !QFontDatabase::isPrivateFamily(family) &&
        !QFontDatabase::styles(family).isEmpty()) {
      families.push_back(family);
    }
  }
  CHECK(families.size() >= 2);
  if (families.size() < 2) {
    return;
  }
  const auto first = families.front();
  const auto second = families.back();
  const auto js_string = [](const QString& text) {
    const auto array = QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact));
    return array.mid(1, array.size() - 2);  // the quoted element without the brackets
  };
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var a = doc.addTextLayer('Font probe', {font: %1, size: 24, x: 10, y: 40});
    var b = doc.addTextLayer('Font probe', {font: %2, size: 24, x: 10, y: 120});
    console.log('first=' + a.textFont);
    console.log('second=' + b.textFont);
    var c = doc.addTextLayer('Font probe', {font: 'Patchy No Such Family', size: 24, x: 10, y: 200});
    console.log('plain=' + JSON.stringify(doc.addLayer('Plain').textFont));
  )JS")
                             .arg(js_string(first), js_string(second))));
  CHECK(backlog_contains(window, QStringLiteral("first=") + first));
  CHECK(backlog_contains(window, QStringLiteral("second=") + second));
  CHECK(backlog_contains(window, QStringLiteral("font not available, rendered with a fallback: Patchy No Such Family")));
  CHECK(backlog_contains(window, QStringLiteral("plain=\"\"")));
}

// A scripted layer names its own face. A new session seeds its face from the options bar's
// style picker (Photoshop seeds new type from its toolbar the same way), and that face used to
// ride along into the scripted layer: with the picker parked on a Black layer, a script asking
// for plain Arial got Arial Black. The picker is put on Black by hand here, since the leak needs
// a face the bold/italic flags cannot name; a database that does not list Black under Arial
// (font files vary per machine) skips.
void ui_script_text_face_ignores_the_options_bar_style() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::test::register_test_fonts(patchy::test::TestFontRole::ArialBlack);
  auto* family_combo = window.findChild<QFontComboBox*>(QStringLiteral("textFontCombo"));
  auto* style_combo = window.findChild<QComboBox*>(QStringLiteral("textStyleCombo"));
  CHECK(family_combo != nullptr);
  CHECK(style_combo != nullptr);
  if (family_combo == nullptr || style_combo == nullptr) {
    return;
  }
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var plain = doc.addTextLayer('Face probe', {font: 'Arial', size: 40, x: 10, y: 40});
    console.log('plain=' + plain.bounds.width + 'x' + plain.bounds.height);
    // A picker change applies to the selected text layer (issue 31); park the selection on a
    // pixel layer so the probe layer keeps its face and only the bar's state changes.
    doc.addLayer('Spacer');
  )JS")));
  family_combo->setCurrentFont(QFont(QStringLiteral("Arial")));
  int black_row = -1;
  for (int row = 0; row < style_combo->count(); ++row) {
    if (style_combo->itemData(row).toString().compare(QStringLiteral("Black"), Qt::CaseInsensitive) == 0) {
      black_row = row;
      break;
    }
  }
  if (black_row < 0) {
    patchy::test::skip("the font database lists no Black face under Arial");
  }
  style_combo->setCurrentIndex(black_row);
  QApplication::processEvents();
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var plain = doc.findLayer('Face probe');
    var again = doc.addTextLayer('Face probe', {font: 'Arial', size: 40, x: 10, y: 140});
    var black = doc.addTextLayer('Face probe', {font: 'Arial Black', size: 40, x: 10, y: 240});
    console.log('again-matches-plain=' + (again.bounds.width === plain.bounds.width &&
                                          again.bounds.height === plain.bounds.height));
    console.log('black-is-wider=' + (black.bounds.width > plain.bounds.width));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("again-matches-plain=true")));
  CHECK(backlog_contains(window, QStringLiteral("black-is-wider=true")));
}

// The committed size is the requested document size at every zoom, and an unchanged re-edit
// keeps it. The inline editor's font is whole editor pixels (document px x zoom); a commit that
// recovered the size as round(px / zoom) turned 60 px into 62 px at 13% and 58 px at 15.5%,
// which is how an AI-built poster's untouched headings changed size when clicked into. Every
// run now carries its exact size through the session.
void ui_script_text_size_survives_low_zoom_reedit() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    app.zoom = 100;
    var a = doc.addTextLayer('Zoom probe', {size: 60, x: 10, y: 40});
    var w = a.bounds.width, h = a.bounds.height;
    app.zoom = 13;
    var b = doc.addTextLayer('Zoom probe', {size: 60, x: 10, y: 200});
    a.text = a.text;
    console.log('low-zoom-new=' + (b.bounds.width === w && b.bounds.height === h));
    console.log('low-zoom-reedit=' + (a.bounds.width === w && a.bounds.height === h));
    app.zoom = 100;
    a.text = a.text;
    console.log('back-at-100=' + (a.bounds.width === w && a.bounds.height === h));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("low-zoom-new=true")));
  CHECK(backlog_contains(window, QStringLiteral("low-zoom-reedit=true")));
  CHECK(backlog_contains(window, QStringLiteral("back-at-100=true")));
}

// Windows: a face's full name ("Futura Extra Black BT", the registry's display name and what an
// older PSD reader stored for the face) renders the same face as the family + style the font
// database lists ("Futura XBlk BT" + "Extra Black"), with no missing-font warning, and an
// unchanged re-edit of a layer carrying the full name keeps that face. The database only knows
// the face once the fixture file is registered; the lookup itself asks DirectWrite, so the test
// skips unless the font is installed on this machine and copied into local-test-fixtures.
void ui_script_text_full_face_name_resolves_like_its_family() {
#ifdef Q_OS_WIN
  const auto fixture = QStringLiteral(PATCHY_SOURCE_DIR "/local-test-fixtures/fonts/FUTURAXK.TTF");
  if (!QFileInfo::exists(fixture) || !patchy::psd::installed_font_for_name("Futura Extra Black BT").has_value()) {
    patchy::test::skip("Futura Extra Black BT is not installed and staged in local-test-fixtures/fonts");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(QFontDatabase::addApplicationFont(fixture) >= 0);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var family = doc.addTextLayer('Diorama', {font: 'Futura XBlk BT', size: 40, x: 10, y: 40});
    var full = doc.addTextLayer('Diorama', {font: 'Futura Extra Black BT', size: 40, x: 10, y: 140});
    var same = function () {
      return full.bounds.width === family.bounds.width && full.bounds.height === family.bounds.height;
    };
    console.log('full-name-matches=' + same());
    full.text = full.text;
    console.log('reedit-keeps-face=' + same());
    console.log('stored=' + full.textFont);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("full-name-matches=true")));
  CHECK(backlog_contains(window, QStringLiteral("reedit-keeps-face=true")));
  CHECK(backlog_contains(window, QStringLiteral("stored=Futura Extra Black BT")));
  CHECK(!backlog_contains(window, QStringLiteral("font not available")));
#else
  patchy::test::skip("full face name resolution is DirectWrite-only");
#endif
}

// The name-table decoder behind font_face_for_name_table_name: Windows English records win over
// the Macintosh ones CoreText reports (FUTURABC.TTF is "Futura" + "Bold" to CoreText and
// "Futura BdCn BT" + "Bold" to every Windows database), a Macintosh-only table still yields its
// names, and a truncated table yields nothing.
void ui_text_face_name_table_parser_prefers_windows_records() {
  struct Record {
    quint16 platform;
    quint16 encoding;
    quint16 language;
    quint16 name_id;
    QString text;
  };
  const auto build_table = [](const std::vector<Record>& records) {
    QByteArray strings;
    QByteArray table;
    const auto put_u16 = [](QByteArray& out, quint16 value) {
      out.append(static_cast<char>(value >> 8));
      out.append(static_cast<char>(value & 0xff));
    };
    put_u16(table, 0);
    put_u16(table, static_cast<quint16>(records.size()));
    put_u16(table, static_cast<quint16>(6 + 12 * records.size()));
    for (const auto& record : records) {
      QByteArray encoded;
      if (record.platform == 1) {
        encoded = record.text.toLatin1();
      } else {
        for (const auto ch : record.text) {
          put_u16(encoded, ch.unicode());
        }
      }
      put_u16(table, record.platform);
      put_u16(table, record.encoding);
      put_u16(table, record.language);
      put_u16(table, record.name_id);
      put_u16(table, static_cast<quint16>(encoded.size()));
      put_u16(table, static_cast<quint16>(strings.size()));
      strings.append(encoded);
    }
    return table + strings;
  };
  const std::vector<Record> macintosh{
      {1, 0, 0, 1, QStringLiteral("Futura")},
      {1, 0, 0, 2, QStringLiteral("Bold")},
      {1, 0, 0, 4, QStringLiteral("Futura Bold Condensed BT")},
      {1, 0, 0, 6, QStringLiteral("FuturaBT-BoldCondensed")},
  };
  std::vector<Record> both = macintosh;
  both.push_back({3, 1, 0x0409, 1, QStringLiteral("Futura BdCn BT")});
  both.push_back({3, 1, 0x0409, 2, QStringLiteral("Bold")});
  both.push_back({3, 1, 0x0409, 4, QStringLiteral("Futura Bold Condensed BT")});
  both.push_back({3, 1, 0x0409, 6, QStringLiteral("FuturaBT-BoldCondensed")});
  both.push_back({3, 1, 0x0407, 1, QStringLiteral("Futura BdCn BT (de)")});  // German Windows record loses to English

  const auto parsed = patchy::ui::parse_opentype_face_names(build_table(both));
  CHECK(parsed.has_value());
  if (parsed.has_value()) {
    CHECK(parsed->family == QStringLiteral("Futura BdCn BT"));
    CHECK(parsed->subfamily == QStringLiteral("Bold"));
    CHECK(parsed->full_name == QStringLiteral("Futura Bold Condensed BT"));
    CHECK(parsed->postscript_name == QStringLiteral("FuturaBT-BoldCondensed"));
    CHECK(parsed->typographic_family.isEmpty());
  }
  const auto mac_only = patchy::ui::parse_opentype_face_names(build_table(macintosh));
  CHECK(mac_only.has_value());
  if (mac_only.has_value()) {
    CHECK(mac_only->family == QStringLiteral("Futura"));
    CHECK(mac_only->postscript_name == QStringLiteral("FuturaBT-BoldCondensed"));
  }
  const auto full = build_table(both);
  CHECK(!patchy::ui::parse_opentype_face_names(full.left(5)).has_value());
  CHECK(!patchy::ui::parse_opentype_face_names(full.left(6 + 12 * 3)).has_value());  // records past the end
  CHECK(!patchy::ui::parse_opentype_face_names(QByteArray()).has_value());
  CHECK(patchy::ui::compact_text_family_key(QStringLiteral("FuturaBT-BoldCondensed")) ==
        patchy::ui::compact_text_family_key(QStringLiteral("Futura BT Bold Condensed")));
}

}  // namespace

std::vector<patchy::test::TestCase> scripting_tests_part1() {
  return {
      {"ui_script_file_info_reads_edits_and_strips", ui_script_file_info_reads_edits_and_strips},
      {"ui_script_import_files_as_layers", ui_script_import_files_as_layers},
      {"ui_script_export_pdf_writes_pages", ui_script_export_pdf_writes_pages},
      {"ui_script_layer_move_to_rounds_like_photoshop", ui_script_layer_move_to_rounds_like_photoshop},
      {"ui_script_palette_validation_and_history", ui_script_palette_validation_and_history},
      {"ui_script_palette_unicode_files_and_indexed_png", ui_script_palette_unicode_files_and_indexed_png},
      {"ui_script_palette_named_controls_and_rename", ui_script_palette_named_controls_and_rename},
      {"ui_palette_panel_named_readout_stays_visible", ui_palette_panel_named_readout_stays_visible},
      {"ui_script_palette_extract_preserves_matching_names", ui_script_palette_extract_preserves_matching_names},
      {"ui_script_advanced_brush_pen_pose_and_dab_cancellation", ui_script_advanced_brush_pen_pose_and_dab_cancellation},
      {"ui_script_advanced_brush_native_parity", ui_script_advanced_brush_native_parity},
      {"ui_script_advanced_brush_timing_validation_and_restore", ui_script_advanced_brush_timing_validation_and_restore},
      {"ui_script_advanced_brush_presets_snapshots_and_refresh", ui_script_advanced_brush_presets_snapshots_and_refresh},
      {"ui_script_advanced_brush_creation_preview_and_psd", ui_script_advanced_brush_creation_preview_and_psd},
      {"ui_script_brush_mode_strokes_and_validation", ui_script_brush_mode_strokes_and_validation},
      {"ui_script_automation_strokes_match_native_and_undo", ui_script_automation_strokes_match_native_and_undo},
      {"ui_script_automation_preview_ids_and_errors", ui_script_automation_preview_ids_and_errors},
      {"ui_script_automation_pressure_seed_selection_palette", ui_script_automation_pressure_seed_selection_palette},
      {"ui_script_mutations_ride_single_undo_entry", ui_script_mutations_ride_single_undo_entry},
      {"ui_script_stale_layer_wrapper_throws", ui_script_stale_layer_wrapper_throws},
      {"ui_script_shape_feather_and_density", ui_script_shape_feather_and_density},
      {"ui_script_pixels_roundtrip_and_palette_snap", ui_script_pixels_roundtrip_and_palette_snap},
      {"ui_script_get_pixels_reads_rgb_layers", ui_script_get_pixels_reads_rgb_layers},
      {"ui_script_fill_rect_partial_updates", ui_script_fill_rect_partial_updates},
      {"ui_script_remove_object_heals_selection", ui_script_remove_object_heals_selection},
      {"ui_script_remove_object_then_reopen_large_document", ui_script_remove_object_then_reopen_large_document},
      {"ui_script_align_and_distribute_layers", ui_script_align_and_distribute_layers},
      {"ui_script_canvas_window_receives_space_key", ui_script_canvas_window_receives_space_key},
      {"ui_script_canvas_window_dismisses_stop_panel", ui_script_canvas_window_dismisses_stop_panel},
      {"ui_script_canvas_window_suppresses_stop_panel", ui_script_canvas_window_suppresses_stop_panel},
      {"ui_script_undo_disable_skips_history", ui_script_undo_disable_skips_history},
      {"ui_script_timer_keeps_run_alive", ui_script_timer_keeps_run_alive},
      {"ui_script_watchdog_interrupts_infinite_loop", ui_script_watchdog_interrupts_infinite_loop},
      {"ui_script_watchdog_allows_busy_scripts", ui_script_watchdog_allows_busy_scripts},
      {"ui_script_stop_panel_confirm_and_undo", ui_script_stop_panel_confirm_and_undo},
      {"ui_script_busy_panel_yields_to_script_dialogs", ui_script_busy_panel_yields_to_script_dialogs},
      {"ui_script_console_and_error_line_numbers", ui_script_console_and_error_line_numbers},
      {"ui_script_filters_and_text_layers", ui_script_filters_and_text_layers},
      {"ui_destructive_edits_blend_by_soft_selection_coverage", ui_destructive_edits_blend_by_soft_selection_coverage},
      {"ui_script_bit_depth_and_deep_filters", ui_script_bit_depth_and_deep_filters},
      {"ui_script_deep_documents_save_to_8_bit_formats", ui_script_deep_documents_save_to_8_bit_formats},
      {"ui_script_text_size_is_zoom_independent", ui_script_text_size_is_zoom_independent},
      {"ui_script_text_font_option_applies", ui_script_text_font_option_applies},
      {"ui_script_text_face_ignores_the_options_bar_style", ui_script_text_face_ignores_the_options_bar_style},
      {"ui_script_text_size_survives_low_zoom_reedit", ui_script_text_size_survives_low_zoom_reedit},
      {"ui_script_text_full_face_name_resolves_like_its_family", ui_script_text_full_face_name_resolves_like_its_family},
      {"ui_text_face_name_table_parser_prefers_windows_records", ui_text_face_name_table_parser_prefers_windows_records},
  };
}
