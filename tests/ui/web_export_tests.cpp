// File > Export for Web (docs/web-export.md): the settings model and its defaults, the
// JPEG coding options read back from the bytes, the preview engine's stale-request
// handling, and the dialog end to end (previews, sizes, compare modes, Save).
#include "core/document.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/web_export.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;
using patchy::ui::WebExportFormat;
using patchy::ui::WebExportPreviewEngine;
using patchy::ui::WebExportSettings;

// Every exportForWeb/* key, restored after the test.
struct WebExportSettingsGuard {
  WebExportSettingsGuard() {
    for (const auto* key : {"format", "jpegQuality", "jpegProgressive", "jpegOptimize", "jpegBackground",
                            "webpQuality", "webpLossless", "webpAnimate", "metadata", "thumbnail", "resample",
                            "trim", "convertToSrgb", "compare", "revealWhenDone"}) {
      const auto full = QStringLiteral("exportForWeb/") + QLatin1String(key);
      restorers.push_back(std::make_unique<SettingsValueRestorer>(full));
      auto settings = patchy::ui::app_settings();
      settings.remove(full);
    }
  }
  std::vector<std::unique_ptr<SettingsValueRestorer>> restorers;
};

// A photo-like test image: smooth gradients plus deterministic noise, so lossy coding
// choices change the byte count.
patchy::Document textured_document(int width, int height) {
  QImage image(width, height, QImage::Format_RGBA8888);
  std::uint32_t seed = 12345U;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      seed = seed * 1664525U + 1013904223U;
      const int noise = static_cast<int>((seed >> 24) % 40U) - 20;
      image.setPixelColor(x, y,
                          QColor(std::clamp(x * 255 / std::max(1, width - 1) + noise, 0, 255),
                                 std::clamp(y * 255 / std::max(1, height - 1) - noise, 0, 255),
                                 std::clamp(128 + noise * 2, 0, 255), 255));
    }
  }
  return patchy::ui::document_from_qimage(image, "Texture");
}

std::vector<std::uint8_t> encode_jpeg(const patchy::Document& document, const WebExportSettings& settings) {
  const QSize size(document.width(), document.height());
  const auto job = patchy::ui::web_export_job(settings, WebExportFormat::Jpeg, size);
  const auto prepared = WebExportPreviewEngine::prepare(document, job.geometry, job.convert_to_srgb, nullptr);
  const auto result = WebExportPreviewEngine::encode(document, job, prepared);
  CHECK(result.error.isEmpty());
  CHECK(!result.decoded.isNull());
  return result.bytes != nullptr ? *result.bytes : std::vector<std::uint8_t>{};
}

// The first start-of-frame marker: 0xC0 baseline, 0xC1 extended, 0xC2 progressive.
int jpeg_frame_marker(const std::vector<std::uint8_t>& bytes) {
  std::size_t at = 2;
  while (at + 4 <= bytes.size()) {
    if (bytes[at] != 0xFF) {
      return -1;
    }
    const auto marker = bytes[at + 1];
    if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
      return marker;
    }
    const auto length = static_cast<std::size_t>((bytes[at + 2] << 8) | bytes[at + 3]);
    at += 2 + length;
  }
  return -1;
}

bool wait_until(const std::function<bool()>& condition, int timeout_ms = 20000) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < timeout_ms) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  return condition();
}

QString artifact_dir() {
  const auto dir = QFileInfo(QStringLiteral("test-artifacts/web-export")).absoluteFilePath();
  QDir().mkpath(dir);
  return dir;
}

void ui_web_export_defaults_and_options() {
  WebExportSettingsGuard guard;
  const auto loaded = patchy::ui::load_web_export_settings();
  // First use: JPEG at quality 80, progressive and optimized, no metadata, no thumbnail.
  CHECK(loaded.format == WebExportFormat::Jpeg);
  CHECK(loaded.jpeg_quality == 80);
  CHECK(loaded.jpeg_progressive);
  CHECK(loaded.jpeg_optimize);
  CHECK(loaded.metadata == patchy::metadata::MetadataPolicy::None);
  CHECK(!loaded.thumbnail);
  CHECK(loaded.compare == patchy::ui::WebExportCompare::Split);

  const QSize size(200, 100);
  auto settings = loaded;
  settings.width = 200;
  settings.height = 100;
  const auto jpeg = patchy::ui::web_export_image_options(settings, WebExportFormat::Jpeg, size);
  CHECK(jpeg.jpeg_quality == 80);
  CHECK(jpeg.jpeg_progressive && jpeg.jpeg_optimize);
  CHECK(!jpeg.jpeg_exif_thumbnail);
  CHECK(jpeg.metadata_policy == patchy::metadata::MetadataPolicy::None);
  CHECK(jpeg.export_fill_transparent);
  // The document's own size is no resize.
  CHECK(jpeg.export_width == 0 && jpeg.export_height == 0 && jpeg.export_scale == 1);
  settings.width = 100;
  settings.height = 50;
  const auto halved = patchy::ui::web_export_image_options(settings, WebExportFormat::Png, size);
  CHECK(halved.export_width == 100 && halved.export_height == 50);
  CHECK(!halved.export_fill_transparent);
  settings.resample = patchy::ui::WebExportResample::PixelArt;
  settings.pixel_scale = 3;
  const auto pixel_art = patchy::ui::web_export_image_options(settings, WebExportFormat::Webp, size);
  CHECK(pixel_art.export_scale == 3 && pixel_art.export_width == 0);

  // Settings round trip through the persisted keys; each format keeps its own.
  settings.format = WebExportFormat::Webp;
  settings.jpeg_quality = 55;
  settings.webp_quality = 61;
  settings.webp_lossless = true;
  settings.metadata = patchy::metadata::MetadataPolicy::CopyrightAndContact;
  settings.compare = patchy::ui::WebExportCompare::SideBySide;
  patchy::ui::save_web_export_settings(settings);
  const auto reloaded = patchy::ui::load_web_export_settings();
  CHECK(reloaded.format == WebExportFormat::Webp);
  CHECK(reloaded.jpeg_quality == 55);
  CHECK(reloaded.webp_quality == 61 && reloaded.webp_lossless);
  CHECK(reloaded.metadata == patchy::metadata::MetadataPolicy::CopyrightAndContact);
  CHECK(reloaded.resample == patchy::ui::WebExportResample::PixelArt);
  CHECK(reloaded.compare == patchy::ui::WebExportCompare::SideBySide);
  CHECK(patchy::ui::web_export_format_key(WebExportFormat::Jpeg) == QStringLiteral("jpeg"));
  CHECK(patchy::ui::web_export_format_from_key(QStringLiteral("png")) == WebExportFormat::Png);
  CHECK(!patchy::ui::web_export_format_from_key(QStringLiteral("gif")).has_value());
}

void ui_web_export_jpeg_progressive_and_optimize_read_back() {
  const auto document = textured_document(160, 120);
  WebExportSettings settings;
  settings.jpeg_progressive = false;
  settings.jpeg_optimize = false;
  const auto baseline = encode_jpeg(document, settings);
  settings.jpeg_optimize = true;
  const auto optimized = encode_jpeg(document, settings);
  settings.jpeg_progressive = true;
  const auto progressive = encode_jpeg(document, settings);
  CHECK(jpeg_frame_marker(baseline) == 0xC0);
  CHECK(jpeg_frame_marker(optimized) == 0xC0);
  CHECK(jpeg_frame_marker(progressive) == 0xC2);
  // Optimized Huffman tables code the same image in fewer bytes.
  CHECK(optimized.size() < baseline.size());
  for (const auto* bytes : {&baseline, &optimized, &progressive}) {
    const auto image = QImage::fromData(QByteArrayView(reinterpret_cast<const char*>(bytes->data()),
                                                       static_cast<qsizetype>(bytes->size())),
                                        "jpg");
    CHECK(image.width() == 160 && image.height() == 120);
  }
  // Save, Save As and Export Flat Image keep baseline coding (their bytes do not change).
  const patchy::ui::ImageSaveOptions plain;
  CHECK(!plain.jpeg_progressive && !plain.jpeg_optimize && plain.jpeg_exif_thumbnail);
  const auto flat = patchy::ui::encode_flat_image_bytes(document, QStringLiteral("jpg"), plain);
  CHECK(jpeg_frame_marker(flat) == 0xC0);
}

void ui_web_export_engine_drops_stale_requests() {
  auto document = std::make_shared<const patchy::Document>(textured_document(320, 240));
  WebExportPreviewEngine engine(document);
  std::vector<QString> reported;
  engine.set_result_callback(
      [&reported](const patchy::ui::WebExportJob& job, const patchy::ui::WebExportResult& result) {
        CHECK(result.error.isEmpty());
        reported.push_back(job.signature);
      });
  const QSize size(320, 240);
  WebExportSettings first;
  first.jpeg_quality = 20;
  std::vector<patchy::ui::WebExportJob> stale;
  for (const auto format : patchy::ui::web_export_formats()) {
    stale.push_back(patchy::ui::web_export_job(first, format, size));
  }
  const auto stale_png = stale.back().signature;
  engine.request(stale);
  // A new request before the worker gets far: the old queue is dropped.
  WebExportSettings second;
  second.jpeg_quality = 90;
  const auto current = patchy::ui::web_export_job(second, WebExportFormat::Jpeg, size);
  engine.request({current});
  CHECK(wait_until([&engine] { return !engine.busy(); }));
  // At most the job already handed to a worker ran besides the current one.
  CHECK(engine.encodes_started() <= 2);
  CHECK(engine.cached(current.signature).has_value());
  CHECK(!engine.cached(stale_png).has_value());
  CHECK(!reported.empty() && reported.back() == current.signature);
  const auto result = engine.cached(current.signature);
  CHECK(result.has_value() && result->bytes != nullptr && !result->bytes->empty());
  CHECK(result.has_value() && result->decoded.size() == QSize(320, 240));
  CHECK(result.has_value() && result->original.size() == QSize(320, 240));
  // An identical request is served from the cache without encoding again.
  const auto before = engine.encodes_started();
  engine.request({current});
  CHECK(wait_until([&engine] { return !engine.busy(); }));
  CHECK(engine.encodes_started() == before);
  engine.close();
}

void ui_web_export_dialog_previews_and_saves() {
  WebExportSettingsGuard guard;
  SettingsValueRestorer last_save_directory_restorer(QStringLiteral("lastSaveDirectory"));
  patchy::ui::MainWindow window;
  show_window(window);
  // The README's CC0 photo, so the captured dialog shows a real picture.
  const auto photo = patchy::test::committed_format_fixture_path("readme", "havana_vintage_car_cc0.jpg");
  MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(photo));
  QApplication::processEvents();
  const auto& opened = MainWindowTestAccess::document(window);
  const QSize photo_size(opened.width(), opened.height());
  const auto target = artifact_dir() + QStringLiteral("/web-export-dialog.jpg");
  QFile::remove(target);

  bool inspected = false;
  bool saved_dialog_seen = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("webExportDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    dialog->resize(1280, 820);
    auto* list = dialog->findChild<QTreeWidget*>(QStringLiteral("webExportFormatList"));
    auto* preview = dynamic_cast<patchy::ui::ZoomableImagePreview*>(dialog->findChild<QWidget*>(QStringLiteral("webExportPreview")));
    auto* original = dynamic_cast<patchy::ui::ZoomableImagePreview*>(dialog->findChild<QWidget*>(QStringLiteral("webExportOriginalPreview")));
    auto* quality = dialog->findChild<QSpinBox*>(QStringLiteral("webExportJpegQualitySpin"));
    auto* thumbnail = dialog->findChild<QCheckBox*>(QStringLiteral("webExportThumbnailCheck"));
    auto* info = dialog->findChild<QLabel*>(QStringLiteral("webExportInfoLabel"));
    auto* side_by_side = dialog->findChild<QPushButton*>(QStringLiteral("webExportCompareSideBySide"));
    auto* split = dialog->findChild<QPushButton*>(QStringLiteral("webExportCompareSplit"));
    auto* save = dialog->findChild<QPushButton*>(QStringLiteral("webExportSaveButton"));
    CHECK(list != nullptr && preview != nullptr && original != nullptr && quality != nullptr &&
          thumbnail != nullptr && info != nullptr && side_by_side != nullptr && split != nullptr &&
          save != nullptr);
    if (list == nullptr || preview == nullptr || original == nullptr || quality == nullptr || thumbnail == nullptr ||
        info == nullptr || side_by_side == nullptr || split == nullptr || save == nullptr) {
      dialog->reject();
      return;
    }
    // Defaults: JPEG selected, quality 80, the thumbnail off and disabled (metadata None).
    CHECK(list->topLevelItemCount() == 3);
    CHECK(list->currentItem() != nullptr && list->currentItem()->text(0) == QStringLiteral("JPEG"));
    CHECK(quality->value() == 80);
    CHECK(!thumbnail->isChecked() && !thumbnail->isEnabled());
    CHECK(!original->isVisible());
    // Every row gets its real size.
    const auto all_sized = [list] {
      for (int row = 0; row < list->topLevelItemCount(); ++row) {
        const auto text = list->topLevelItem(row)->text(2);
        if (!text.contains(QLatin1Char('B'))) {
          return false;
        }
      }
      return true;
    };
    CHECK(wait_until(all_sized));
    CHECK(preview->property("webExportShowsCurrent").toBool());
    CHECK(info->text().contains(QStringLiteral("%1 x %2").arg(photo_size.width()).arg(photo_size.height())));
    const auto size_at_80 = list->topLevelItem(0)->text(2);

    // A lower quality re-encodes after the debounce and changes the size.
    quality->setValue(40);
    CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool() &&
                                  list->topLevelItem(0)->text(2) != size_at_80; }));
    // The split view at 100% over the car, for the card.
    preview->zoom_to(1.0, QPointF(preview->width() * 0.55, preview->height() * 0.8));
    QApplication::processEvents();
    CHECK(!preview->fit_mode() && preview->property("previewZoomPercent").toInt() == 100);
    capture_widget_artifact("web_export_jpeg_split", *dialog);
    // Results landing later (the other formats) never move the view.
    CHECK(wait_until(all_sized));
    CHECK(!preview->fit_mode());

    // Side by side: the original pane appears and follows the zoom.
    side_by_side->click();
    QApplication::processEvents();
    CHECK(original->isVisible());
    preview->zoom_to(2.0);
    QApplication::processEvents();
    CHECK(original->property("previewZoomPercent").toInt() == 200);
    CHECK(original->property("previewPanOffset") == preview->property("previewPanOffset"));
    CHECK(original->width() == preview->width());
    list->setCurrentItem(list->topLevelItem(2));  // PNG
    CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }));
    CHECK(dialog->findChild<QWidget*>(QStringLiteral("webExportPngGroup"))->isVisible());
    CHECK(!thumbnail->isVisible());
    capture_widget_artifact("web_export_png_side_by_side", *dialog);

    // Back to JPEG in split view, then Save: the file dialog takes the name.
    list->setCurrentItem(list->topLevelItem(0));
    split->click();
    preview->zoom_to_fit();
    CHECK(wait_until([&] { return preview->property("webExportShowsCurrent").toBool(); }));
    inspected = true;
    auto* file_dialog_timer = new QTimer(&window);
    file_dialog_timer->setInterval(20);
    QObject::connect(file_dialog_timer, &QTimer::timeout, &window, [&, file_dialog_timer] {
      auto* file_dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(QStringLiteral("exportForWebFileDialog")));
      if (file_dialog == nullptr) {
        return;
      }
      file_dialog_timer->stop();
      file_dialog_timer->deleteLater();
      saved_dialog_seen = true;
      file_dialog->setDirectory(QFileInfo(target).absolutePath());
      file_dialog->selectFile(target);
      static_cast<QDialog*>(file_dialog)->accept();
    });
    file_dialog_timer->start();
    save->click();
  });
  patchy::test::ui::require_action(window, "fileExportForWebAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
  CHECK(saved_dialog_seen);
  CHECK(QFileInfo(target).isFile());
  QFile file(target);
  CHECK(file.open(QIODevice::ReadOnly));
  const auto bytes = file.readAll();
  const std::vector<std::uint8_t> data(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                       reinterpret_cast<const std::uint8_t*>(bytes.constData()) + bytes.size());
  // Progressive by default and no metadata at all (no APP1 segment).
  CHECK(jpeg_frame_marker(data) == 0xC2);
  CHECK(!bytes.contains(QByteArray("Exif\0\0", 6)));
  CHECK(QImage(target).size() == photo_size);
  // The dialog remembered the format and quality for next time.
  const auto remembered = patchy::ui::load_web_export_settings();
  CHECK(remembered.format == WebExportFormat::Jpeg && remembered.jpeg_quality == 40);
}

}  // namespace

std::vector<patchy::test::TestCase> web_export_tests() {
  return {
      {"ui_web_export_defaults_and_options", ui_web_export_defaults_and_options},
      {"ui_web_export_jpeg_progressive_and_optimize_read_back", ui_web_export_jpeg_progressive_and_optimize_read_back},
      {"ui_web_export_engine_drops_stale_requests", ui_web_export_engine_drops_stale_requests},
      {"ui_web_export_dialog_previews_and_saves", ui_web_export_dialog_previews_and_saves},
  };
}
