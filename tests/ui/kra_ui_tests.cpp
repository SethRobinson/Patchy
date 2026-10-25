// Krita documents in the app (docs/kra.md): a .kra under a Unicode name opens with its
// layers, its vector layer's shapes and text (the text rendered after the open, as for
// SVG), and a file layer loaded from the folder beside it; a .kra under another name opens
// by content.

#include "core/layer_metadata.hpp"
#include "formats/ora_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStatusBar>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

using patchy::test::ui::ShownWindowFixture;

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

std::vector<std::uint8_t> bytes_of(std::string_view text) {
  return {text.begin(), text.end()};
}

// A 64 x 64 tile file of one BGRA color, stored uncompressed (planar, as Krita writes).
std::vector<std::uint8_t> solid_tile_file(std::array<std::uint8_t, 4> bgra) {
  std::string header = "VERSION 2\nTILEWIDTH 64\nTILEHEIGHT 64\nPIXELSIZE 4\nDATA 1\n";
  std::vector<std::uint8_t> planar;
  for (const auto byte : bgra) {
    planar.insert(planar.end(), 64U * 64U, byte);
  }
  header += "0,0,LZF," + std::to_string(planar.size() + 1) + "\n";
  std::vector<std::uint8_t> out(header.begin(), header.end());
  out.push_back(0);
  out.insert(out.end(), planar.begin(), planar.end());
  return out;
}

// 144 ppi: the vector layer's points are 2 pixels each.
constexpr std::string_view kMaindoc = R"(<?xml version="1.0" encoding="UTF-8"?>
<DOC xmlns="http://www.calligra.org/DTD/krita" syntaxVersion="2.0" editor="Krita" kritaVersion="5.3.2">
 <IMAGE colorspacename="RGBA" x-res="144" y-res="144" name="Pic" width="200" height="120">
  <layers>
   <layer name="File" nodetype="filelayer" filename="layer4" source="beside.png" compositeop="normal" opacity="255" visible="1" x="150" y="80"/>
   <layer name="Vectors" nodetype="shapelayer" filename="layer3" compositeop="normal" opacity="255" visible="1" x="0" y="0"/>
   <layer name="Paint" nodetype="paintlayer" filename="layer2" compositeop="normal" opacity="255" visible="1" x="0" y="0" colorspacename="RGBA" selected="true"/>
  </layers>
 </IMAGE>
</DOC>
)";

constexpr std::string_view kVectors = R"(<?xml version="1.0" standalone="no"?>
<svg xmlns="http://www.w3.org/2000/svg" xmlns:krita="http://krita.org/namespaces/svg/krita" width="100pt" height="60pt" viewBox="0 0 100 60">
<rect id="shape0" transform="translate(5, 5)" fill="#00ff00" width="20" height="10"/><text id="shape1" krita:textVersion="3" fill="#ff0000" x="10" y="45" style="font-family: Arial;font-size: 12;">Hi</text>
</svg>
)";

std::vector<std::uint8_t> sample_kra() {
  std::vector<patchy::ora::detail::ZipEntry> entries;
  entries.push_back({"mimetype", bytes_of("application/x-krita"), false});
  entries.push_back({"maindoc.xml", bytes_of(kMaindoc), true});
  entries.push_back({"Pic/layers/layer2", solid_tile_file({40, 30, 20, 255}), true});
  entries.push_back({"Pic/layers/layer3.shapelayer/content.svg", bytes_of(kVectors), true});
  return patchy::ora::detail::zip_archive(entries);
}

void ui_kra_open_keeps_layers_vectors_text_and_file_layers() {
  patchy::test::ui::ensure_artifact_dir();
  const auto dir = patchy::ui::to_qstring(std::filesystem::absolute(patchy::test::unicode_artifact_dir(u8"kra-open")));
  QDir().mkpath(dir);
  const auto bytes = sample_kra();
  const auto write = [&](const QString& name, const std::vector<std::uint8_t>& data) {
    const auto path = QDir(dir).filePath(name);
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<qsizetype>(data.size()));
    return path;
  };
  patchy::PixelBuffer beside(4, 3, patchy::PixelFormat::rgba8());
  beside.clear(255);
  write(QStringLiteral("beside.png"), patchy::ora::detail::encode_png(beside));
  const auto stem = patchy::ui::to_qstring(patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem));
  const auto kra_path = write(stem + QStringLiteral(".kra"), bytes);
  const auto bin_path = write(QStringLiteral("painting.bin"), bytes);

  auto&& [window, canvas] = ShownWindowFixture{};
  const auto check_document = [&] {
    const auto& document = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(document.width() == 200 && document.height() == 120);
    CHECK(document.print_settings().horizontal_ppi == 144.0);
    CHECK(document.layers().size() == 3U);
    const auto* paint = find_named(document.layers(), "Paint");
    CHECK(paint != nullptr && paint->pixels().pixel(0, 0)[0] == 20 && paint->pixels().pixel(0, 0)[2] == 40);
    // The file's selected layer stays active.
    CHECK(paint != nullptr && document.active_layer_id() == paint->id());
    const auto* vectors = find_named(document.layers(), "Vectors");
    CHECK(vectors != nullptr && vectors->kind() == patchy::LayerKind::Group && vectors->children().size() == 2U);
    if (vectors == nullptr || vectors->children().size() != 2U) {
      return;
    }
    // Points to pixels at 144 ppi: the rectangle at (5, 5) pt, 20 x 10 pt, is (10, 10), 40 x 20 px.
    const auto& rect = vectors->children()[0];
    CHECK(rect.vector_shape() != nullptr);
    CHECK(rect.bounds().x == 10 && rect.bounds().y == 10 && rect.bounds().width == 40 && rect.bounds().height == 20);
    // The text was rendered after the open: a text layer with pixels near (20, 90) px.
    const auto& text = vectors->children()[1];
    CHECK(patchy::layer_is_text(text));
    CHECK(!text.pixels().empty());
    CHECK(text.bounds().x >= 16 && text.bounds().x <= 26 && text.bounds().y + text.bounds().height >= 80 &&
          text.bounds().y <= 92);
    const auto* file_layer = find_named(document.layers(), "File");
    CHECK(file_layer != nullptr);
    if (file_layer != nullptr) {
      CHECK(file_layer->bounds().x == 150 && file_layer->bounds().y == 80 && file_layer->bounds().width == 4);
    }
  };
  patchy::ui::MainWindowTestAccess::open_document_path(window, kra_path);
  QApplication::processEvents();
  check_document();
  // Under another name it opens by content, through the same reader (and the same folder).
  patchy::ui::MainWindowTestAccess::open_document_path(window, bin_path);
  QApplication::processEvents();
  check_document();
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Krita")));
}

}  // namespace

std::vector<patchy::test::TestCase> kra_ui_tests() {
  return {
      {"ui_kra_open_keeps_layers_vectors_text_and_file_layers", ui_kra_open_keeps_layers_vectors_text_and_file_layers},
  };
}
