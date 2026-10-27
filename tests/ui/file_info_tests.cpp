#include <QAbstractButton>
#include "ui/canvas_widget.hpp"
#include "core/adjustment_layer.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include <QIODevice>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
// File > File Info end to end (docs/file-info.md): metadata captured when a file opens,
// the dialog viewing and editing it, undo, Strip All Metadata, and what each save and
// export policy writes back.
#include "formats/metadata/exif_tags.hpp"
#include "formats/metadata/metadata_containers.hpp"
#include "formats/metadata/metadata_edit.hpp"
#include "formats/metadata/metadata_inject.hpp"
#include "formats/metadata/metadata_view.hpp"
#include "formats/metadata/tiff_ifd.hpp"
#include "formats/metadata/xmp.hpp"
#include "ai/watermark_engine.hpp"
#include "core/invisible_watermark.hpp"
#include "invisible_watermark_reference.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/file_info_dialog.hpp"
#include "ui/image_document_io.hpp"
#include "ui/save_metadata_settings.hpp"
#include "ui/image_save_option_keys.hpp"
#include "ui/web_export.hpp"
#include <QPushButton>
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include <QApplication>
#include <QBuffer>
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QStringList>
#include <QMessageBox>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <QMenu>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include "local_psd_fixtures.hpp"
#include <QFileInfo>
#include <QScrollArea>
#include <QTreeWidget>
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;
namespace md = patchy::metadata;

std::vector<std::uint8_t> srgb_profile() {
  const auto icc = QColorSpace(QColorSpace::SRgb).iccProfile();
  return {reinterpret_cast<const std::uint8_t*>(icc.constData()),
          reinterpret_cast<const std::uint8_t*>(icc.constData()) + icc.size()};
}

QString artifact_dir() {
  const auto dir = QStringLiteral("test-artifacts/file-info");
  QDir().mkpath(dir);
  return dir;
}

// A 16x8 JPEG tagged "rotate 90 CW", with GPS, a camera serial, XMP and an sRGB profile.
QString write_tagged_jpeg() {
  QImage image(16, 8, QImage::Format_RGB32);
  image.fill(QColor(200, 40, 40));
  QBuffer buffer;
  buffer.open(QIODevice::WriteOnly);
  QImageWriter writer(&buffer, "jpg");
  CHECK(writer.write(image));
  const auto& encoded = buffer.data();
  std::vector<std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(encoded.constData()),
                                  reinterpret_cast<const std::uint8_t*>(encoded.constData()) + encoded.size());

  md::TiffStream exif;
  exif.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kMake, "PatchyCam");
  exif.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kArtist, "Ann Example");
  exif.set_short(md::IfdKind::Ifd0, md::exif_tag::kOrientation, 6);
  exif.set_ascii(md::IfdKind::Exif, md::exif_tag::kBodySerialNumber, "SN123");
  exif.set_ascii(md::IfdKind::Gps, md::exif_tag::kGpsLatitudeRef, "N");
  exif.set_rational(md::IfdKind::Gps, md::exif_tag::kGpsLatitude, 47, 1);
  exif.set_ascii(md::IfdKind::Gps, md::exif_tag::kGpsLongitudeRef, "W");
  exif.set_rational(md::IfdKind::Gps, md::exif_tag::kGpsLongitude, 122, 1);
  md::XmpPacket xmp;
  xmp.set_lang_alt(md::xmp_ns::kDc, "title", "Original title");
  xmp.set_array(md::xmp_ns::kDc, "creator", md::XmpValue::Kind::Seq, {"Ann Example"});
  xmp.set_lang_alt(md::xmp_ns::kDc, "rights", "(c) Ann");
  // Long enough to wrap under QLabel's short-text width heuristic (GitHub issue 103).
  xmp.set_simple(md::xmp_ns::kXmp, "CreatorTool", "Adobe Photoshop CC 2017 (Macintosh)");

  md::SaveMetadata metadata;
  metadata.exif = md::write_tiff(exif);
  metadata.xmp = md::serialize_xmp(xmp);
  metadata.icc = srgb_profile();
  bytes = md::inject_metadata(std::move(bytes), "jpg", metadata);

  const auto path = artifact_dir() + QStringLiteral("/tagged.jpg");
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
  file.close();
  return path;
}

std::string title_of(const patchy::DocumentFileInfo& info) {
  return md::read_fields(info).title;
}

std::string creator_tool_of(const patchy::DocumentFileInfo& info) {
  return md::read_fields(info).creator_tool;
}

void ui_file_info_opens_views_edits_and_undoes() {
  const auto path = write_tagged_jpeg();
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto& document = MainWindowTestAccess::document(window);
  // Qt applied the EXIF orientation, so the stored tag now reads "normal".
  CHECK(document.width() == 8 && document.height() == 16);
  const auto& info = document.file_info();
  CHECK(info.source_format == "JPEG");
  CHECK(md::summarize_camera(info).orientation == "Horizontal (normal)");
  CHECK(std::any_of(info.observed.begin(), info.observed.end(),
                    [](const auto& item) { return item.summary == "Rotate 90 CW"; }));
  CHECK(document.color_state().embedded_icc_profile == srgb_profile());
  CHECK(title_of(info) == "Original title");

  bool inspected = false;
  QTimer::singleShot(0, [&inspected] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyFileInfoDialog"));
    CHECK(dialog != nullptr);
    auto* banner = dialog->findChild<QLabel*>(QStringLiteral("fileInfoPrivacyBanner"));
    CHECK(banner != nullptr && !banner->isHidden());
    CHECK(banner->text().contains(patchy::ui::privacy_class_label(patchy::MetadataPrivacy::Location)));
    CHECK(banner->text().contains(patchy::ui::privacy_class_label(patchy::MetadataPrivacy::DeviceIdentity)));
    auto* tree = dialog->findChild<QTreeWidget*>(QStringLiteral("fileInfoAllMetadataTree"));
    CHECK(tree != nullptr && tree->topLevelItemCount() >= 4);
    auto* title = dialog->findChild<QLineEdit*>(QStringLiteral("fileInfoTitleEdit"));
    CHECK(title != nullptr && title->text() == QStringLiteral("Original title"));
    auto* authors = dialog->findChild<QLineEdit*>(QStringLiteral("fileInfoAuthorEdit"));
    CHECK(authors != nullptr && authors->text() == QStringLiteral("Ann Example"));
    title->setText(QStringLiteral("Edited title"));
    // The read-only Application value never wraps: the page's scroll bar
    // used to narrow a wrapped QLabel after its one-line row height was set,
    // clipping the second line (GitHub issue 103).
    auto* application = dialog->findChild<QLabel*>(QStringLiteral("fileInfoApplicationLabel"));
    CHECK(application != nullptr);
    if (application != nullptr) {
      QApplication::processEvents();
      CHECK(application->text() == QStringLiteral("Adobe Photoshop CC 2017 (Macintosh)"));
      CHECK(!application->wordWrap());
      CHECK(application->isVisible());
      CHECK(application->width() >= application->fontMetrics().horizontalAdvance(application->text()));
      CHECK(application->height() < application->fontMetrics().lineSpacing() * 2);
    }
    dialog->grab().save(QStringLiteral("test-artifacts/ui_file_info_dialog.png"));
    // Visual QA captures of the read-only pages.
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("fileInfoCategoryList"));
    CHECK(categories != nullptr);
    for (int row = 0; row < categories->count(); ++row) {
      const auto page = categories->item(row)->data(Qt::UserRole).toString();
      if (page == QStringLiteral("fileInfoCameraPage") || page == QStringLiteral("fileInfoAllMetadataPage") ||
          page == QStringLiteral("fileInfoColorPage")) {
        categories->setCurrentRow(row);
        QApplication::processEvents();
        dialog->grab().save(QStringLiteral("test-artifacts/ui_file_info_%1.png").arg(page));
      }
    }
    inspected = true;
    dialog->accept();
  });
  require_action(window, "fileInfoAction")->trigger();
  QApplication::processEvents();
  CHECK(inspected);
  CHECK(title_of(MainWindowTestAccess::document(window).file_info()) == "Edited title");
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(title_of(MainWindowTestAccess::document(window).file_info()) == "Original title");
}

void ui_file_info_saves_metadata_in_every_carrier() {
  const auto path = write_tagged_jpeg();
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto& info = MainWindowTestAccess::document(window).file_info();
  auto fields = md::read_fields(info);
  const auto before = fields;
  fields.title = "Saved title";
  fields.keywords = {"one", "two"};
  CHECK(md::apply_fields(info, before, fields));

  for (const auto* extension : {"jpg", "png", "tif", "webp", "gif"}) {
    const auto target = artifact_dir() + QStringLiteral("/saved.") + QString::fromLatin1(extension);
    CHECK(MainWindowTestAccess::save_document_to_path(window, target, patchy::ui::ImageSaveOptions{}));
    const auto saved = md::extract_file_info_from_path(patchy::ui::to_filesystem_path(target));
    CHECK(title_of(saved) == "Saved title");
    CHECK(md::read_fields(saved).keywords == std::vector<std::string>({"one", "two"}));
    if (std::string_view(extension) != "gif") {
      CHECK(saved.exif != nullptr);
      const auto camera = md::summarize_camera(saved);
      CHECK(camera.orientation == "Horizontal (normal)");
      CHECK(camera.has_location());
      CHECK(saved.source_icc != nullptr && *saved.source_icc == srgb_profile());
    }
    // The written file still opens as an image.
    CHECK(!QImage(target).isNull());
  }
  // PDF: the fields become the document information dictionary.
  const auto pdf_path = artifact_dir() + QStringLiteral("/saved.pdf");
  CHECK(MainWindowTestAccess::save_document_to_path(window, pdf_path, patchy::ui::ImageSaveOptions{}));
  const auto pdf = md::extract_file_info_from_path(patchy::ui::to_filesystem_path(pdf_path));
  const auto pdf_title = std::find_if(pdf.text.begin(), pdf.text.end(), [](const auto& text) {
    return text.kind == patchy::FileInfoTextKind::PdfInfo && text.key == "Title";
  });
  CHECK(pdf_title != pdf.text.end() && pdf_title->value == "Saved title");
  const auto psd_path = artifact_dir() + QStringLiteral("/saved.psd");
  CHECK(MainWindowTestAccess::save_document_to_path(window, psd_path));
  const auto psd = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(psd_path));
  CHECK(title_of(psd.file_info()) == "Saved title");
  CHECK(md::summarize_camera(psd.file_info()).has_location());
}

void ui_file_info_strip_and_export_policies() {
  const auto path = write_tagged_jpeg();
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  // Export policies, before the strip.
  const auto exported = [&](patchy::metadata::MetadataPolicy policy, const QString& name) {
    patchy::ui::ImageSaveOptions options;
    options.metadata_policy = policy;
    const auto target = artifact_dir() + QLatin1Char('/') + name;
    CHECK(MainWindowTestAccess::save_document_to_path(window, target, options));
    return md::extract_file_info_from_path(patchy::ui::to_filesystem_path(target));
  };
  const auto none = exported(md::MetadataPolicy::None, QStringLiteral("export-none.jpg"));
  CHECK(none.exif == nullptr && none.xmp == nullptr);
  CHECK(none.source_icc != nullptr);  // the color profile always stays
  const auto copyright = exported(md::MetadataPolicy::CopyrightAndContact, QStringLiteral("export-copyright.jpg"));
  CHECK(!md::summarize_camera(copyright).has_location());
  CHECK(md::summarize_camera(copyright).body_serial.empty());
  const auto copyright_fields = md::read_fields(copyright);
  CHECK(copyright_fields.authors == std::vector<std::string>({"Ann Example"}));
  CHECK(copyright_fields.copyright_notice == "(c) Ann");
  CHECK(copyright_fields.title.empty());

  bool confirmed = false;
  QTimer::singleShot(0, [&confirmed] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* box = qobject_cast<QMessageBox*>(widget);
      if (box == nullptr || box->objectName() != QStringLiteral("fileInfoStripConfirm")) {
        continue;
      }
      for (auto* button : box->buttons()) {
        if (box->buttonRole(button) == QMessageBox::AcceptRole) {
          confirmed = true;
          button->click();
          return;
        }
      }
    }
  });
  require_action(window, "fileStripMetadataAction")->trigger();
  QApplication::processEvents();
  CHECK(confirmed);
  auto& document = MainWindowTestAccess::document(window);
  CHECK(!document.file_info().has_metadata());
  CHECK(document.color_state().embedded_icc_profile == srgb_profile());
  const auto target = artifact_dir() + QStringLiteral("/stripped.jpg");
  CHECK(MainWindowTestAccess::save_document_to_path(window, target, patchy::ui::ImageSaveOptions{}));
  const auto stripped = md::extract_file_info_from_path(patchy::ui::to_filesystem_path(target));
  // Only the JPEG save stamp comes back (docs/file-info.md, "Save stamp"): no EXIF, no IPTC,
  // and nothing of the stripped XMP.
  CHECK(stripped.exif == nullptr && stripped.iptc == nullptr);
  CHECK(creator_tool_of(stripped).rfind("Patchy ", 0) == 0U);
  CHECK(title_of(stripped).empty() && md::read_fields(stripped).authors.empty());
  CHECK(stripped.source_icc != nullptr);
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::document(window).file_info().has_metadata());
}


// Issue 219: a PSD save records its dates and Patchy as the creator in the XMP (docs/file-info.md,
// "Save stamp"); a second save keeps the creation date and moves the modify date.
void ui_psd_save_stamps_dates_and_creator() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::Document document(16, 16, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", patchy::PixelBuffer(16, 16, patchy::PixelFormat::rgb8()));
  window.add_document_session(std::move(document), QStringLiteral("Stamp"));
  const auto path = artifact_dir() + QStringLiteral("/stamped.psd");
  QFile::remove(path);
  CHECK(MainWindowTestAccess::save_document_to_path(window, path));
  const auto first = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
  CHECK(first.file_info().xmp != nullptr);
  if (first.file_info().xmp == nullptr) {
    return;
  }
  const auto packet = md::parse_xmp(*first.file_info().xmp).value_or(md::XmpPacket{});
  const auto creator = packet.text(md::xmp_ns::kXmp, "CreatorTool");
  const auto created = packet.text(md::xmp_ns::kXmp, "CreateDate");
  CHECK(creator.rfind("Patchy ", 0) == 0U && creator.size() > 7U);
  CHECK(created.size() == 25U && created[10] == 'T');
  CHECK(packet.text(md::xmp_ns::kXmp, "ModifyDate") == created);

  CHECK(MainWindowTestAccess::save_document_to_path(window, path));
  const auto second = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
  const auto again = md::parse_xmp(*second.file_info().xmp).value_or(md::XmpPacket{});
  CHECK(again.text(md::xmp_ns::kXmp, "CreateDate") == created);
  CHECK(again.text(md::xmp_ns::kXmp, "ModifyDate") >= created);
  const auto* history = again.find(md::xmp_ns::kXmpMM, "History");
  CHECK(history != nullptr && history->value.items.size() == 2U);
}

std::vector<std::string> history_instance_ids(const md::XmpPacket& packet) {
  std::vector<std::string> ids;
  if (const auto* history = packet.find(md::xmp_ns::kXmpMM, "History")) {
    for (const auto& item : history->value.items) {
      for (const auto& field : item.fields) {
        if (field.name == "instanceID") {
          ids.push_back(field.value.text);
        }
      }
    }
  }
  return ids;
}

std::string ifd0_text(const patchy::DocumentFileInfo& info, std::uint16_t tag) {
  const auto stream = info.exif ? md::parse_tiff(*info.exif) : std::nullopt;
  const auto* ifd = stream ? stream->find(md::IfdKind::Ifd0) : nullptr;
  const auto* entry = ifd != nullptr ? ifd->find(tag) : nullptr;
  return entry != nullptr ? stream->string_value(*entry) : std::string{};
}

// A camera JPEG another application edited in 2019: EXIF Software and DateTime, and an
// XMP packet with its creator, its dates, its ids and one History entry.
QString write_dated_jpeg() {
  QImage image(16, 8, QImage::Format_RGB32);
  image.fill(QColor(40, 120, 200));
  QBuffer buffer;
  buffer.open(QIODevice::WriteOnly);
  QImageWriter writer(&buffer, "jpg");
  CHECK(writer.write(image));
  const auto& encoded = buffer.data();
  std::vector<std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(encoded.constData()),
                                  reinterpret_cast<const std::uint8_t*>(encoded.constData()) + encoded.size());
  md::TiffStream exif;
  exif.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kMake, "PatchyCam");
  exif.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kModel, "Model X");
  exif.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kSoftware, "OtherApp 1.0");
  exif.set_ascii(md::IfdKind::Ifd0, md::exif_tag::kDateTime, "2019:05:04 12:00:00");
  md::SaveMetadata metadata;
  metadata.exif = md::write_tiff(exif);
  metadata.xmp =
      R"x(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">)x"
      R"x(<rdf:Description rdf:about="" xmlns:xmp="http://ns.adobe.com/xap/1.0/" xmlns:xmpMM="http://ns.adobe.com/xap/1.0/mm/")x"
      R"x( xmlns:stEvt="http://ns.adobe.com/xap/1.0/sType/ResourceEvent#" xmp:CreatorTool="OtherApp 1.0")x"
      R"x( xmp:CreateDate="2019-05-04T12:00:00+09:00" xmp:ModifyDate="2019-05-04T12:00:00+09:00")x"
      R"x( xmp:MetadataDate="2019-05-04T12:00:00+09:00" xmpMM:DocumentID="xmp.did:0a1b2c3d-2019-4000-8000-000000000001")x"
      R"x( xmpMM:OriginalDocumentID="xmp.did:0a1b2c3d-2019-4000-8000-000000000001")x"
      R"x( xmpMM:InstanceID="xmp.iid:0a1b2c3d-2019-4000-8000-000000000001">)x"
      R"x(<xmpMM:History><rdf:Seq><rdf:li rdf:parseType="Resource"><stEvt:action>created</stEvt:action>)x"
      R"x(<stEvt:instanceID>xmp.iid:0a1b2c3d-2019-4000-8000-000000000001</stEvt:instanceID>)x"
      R"x(<stEvt:when>2019-05-04T12:00:00+09:00</stEvt:when><stEvt:softwareAgent>OtherApp 1.0</stEvt:softwareAgent>)x"
      R"x(</rdf:li></rdf:Seq></xmpMM:History></rdf:Description></rdf:RDF></x:xmpmeta>)x";
  bytes = md::inject_metadata(std::move(bytes), "jpg", metadata);
  const auto path = artifact_dir() + QStringLiteral("/dated.jpg");
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
  file.close();
  return path;
}

// Issue 219 follow-up: a PSD save of a file another application wrote in 2019 keeps its
// creator and creation date, and writes Patchy and the save time into EXIF (IFD0 Software
// and DateTime) and XMP (ModifyDate, MetadataDate); every save gets a new
// xmpMM:InstanceID that matches its new History entry, as Photoshop does. Saves
// after-exif-software.png (File Info > All Metadata, cut to the EXIF IFD0, XMP xmp and
// XMP xmpMM groups) into test-artifacts/file-info.
void ui_psd_save_updates_exif_and_instance_id() {
  constexpr auto kOldInstance = "xmp.iid:0a1b2c3d-2019-4000-8000-000000000001";
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, write_dated_jpeg());
  QApplication::processEvents();
  const auto path = artifact_dir() + QStringLiteral("/exif-stamped.psd");
  QFile::remove(path);
  CHECK(MainWindowTestAccess::save_document_to_path(window, path));
  const auto first = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
  const auto software = ifd0_text(first.file_info(), md::exif_tag::kSoftware);
  const auto date_time = ifd0_text(first.file_info(), md::exif_tag::kDateTime);
  CHECK(software.rfind("Patchy ", 0) == 0U && software.size() > 7U);
  CHECK(date_time.size() == 19U && date_time[4] == ':' && date_time[10] == ' ' && date_time != "2019:05:04 12:00:00");
  CHECK(ifd0_text(first.file_info(), md::exif_tag::kMake) == "PatchyCam");
  CHECK(first.file_info().xmp != nullptr);
  if (first.file_info().xmp == nullptr) {
    return;
  }
  const auto packet = md::parse_xmp(*first.file_info().xmp).value_or(md::XmpPacket{});
  // The other application stays the creator, with its creation date; the save is recorded.
  CHECK(packet.text(md::xmp_ns::kXmp, "CreatorTool") == "OtherApp 1.0");
  CHECK(packet.text(md::xmp_ns::kXmp, "CreateDate") == "2019-05-04T12:00:00+09:00");
  const auto modified = packet.text(md::xmp_ns::kXmp, "ModifyDate");
  CHECK(modified.size() == 25U && modified.rfind("2019-", 0) != 0U);
  CHECK(packet.text(md::xmp_ns::kXmp, "MetadataDate") == modified);
  // EXIF DateTime is the same local time as xmp:ModifyDate.
  CHECK(date_time.substr(0, 4) == modified.substr(0, 4) && date_time.substr(11, 8) == modified.substr(11, 8));
  CHECK(packet.text(md::xmp_ns::kXmpMM, "DocumentID") == "xmp.did:0a1b2c3d-2019-4000-8000-000000000001");
  const auto instance = packet.text(md::xmp_ns::kXmpMM, "InstanceID");
  CHECK(instance.rfind("xmp.iid:", 0) == 0U && instance.size() == 44U && instance != kOldInstance);
  CHECK((history_instance_ids(packet) == std::vector<std::string>{kOldInstance, instance}));

  CHECK(MainWindowTestAccess::save_document_to_path(window, path));
  const auto second = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
  const auto again = md::parse_xmp(*second.file_info().xmp).value_or(md::XmpPacket{});
  const auto next = again.text(md::xmp_ns::kXmpMM, "InstanceID");
  CHECK(next.rfind("xmp.iid:", 0) == 0U && next != instance);
  CHECK((history_instance_ids(again) == std::vector<std::string>{kOldInstance, instance, next}));
  CHECK(again.text(md::xmp_ns::kXmp, "CreateDate") == "2019-05-04T12:00:00+09:00");
  CHECK(again.text(md::xmp_ns::kXmp, "ModifyDate") >= modified);

  // The driver runs inside Qt's event dispatch: failures are recorded, never thrown.
  const auto expect = [](bool ok, const char* what) {
    if (!ok) {
      patchy::test::report_failure(std::string("after-exif-software: ") + what);
    }
  };
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyFileInfoDialog"));
    expect(dialog != nullptr, "no File Info dialog");
    if (dialog == nullptr) {
      return;
    }
    dialog->resize(std::max(dialog->width(), 1180), std::max(dialog->height(), 980));
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("fileInfoCategoryList"));
    for (int row = 0; categories != nullptr && row < categories->count(); ++row) {
      if (categories->item(row)->data(Qt::UserRole).toString() == QStringLiteral("fileInfoAllMetadataPage")) {
        categories->setCurrentRow(row);
      }
    }
    QApplication::processEvents();
    auto* tree = dialog->findChild<QTreeWidget*>(QStringLiteral("fileInfoAllMetadataTree"));
    expect(tree != nullptr, "no All Metadata tree");
    if (tree == nullptr) {
      dialog->reject();
      return;
    }
    const auto software_rows = tree->findItems(QStringLiteral("Software"), Qt::MatchExactly | Qt::MatchRecursive);
    expect(!software_rows.isEmpty() && software_rows.front()->text(1).startsWith(QStringLiteral("Patchy ")),
           "EXIF Software does not name Patchy");
    // Only the groups the save changes, fully expanded, with the value column wide
    // enough for the ids.
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
      auto* group = tree->topLevelItem(index);
      const auto name = group->text(0);
      group->setHidden(name != QStringLiteral("EXIF IFD0") && name != QStringLiteral("XMP xmp") &&
                       name != QStringLiteral("XMP xmpMM"));
      group->setExpanded(true);
    }
    tree->resizeColumnToContents(0);
    tree->resizeColumnToContents(1);
    tree->clearSelection();
    tree->setCurrentItem(nullptr);
    tree->scrollToTop();
    QApplication::processEvents();
    save_widget_artifact("file-info/after-exif-software", *tree);
    dialog->reject();
  });
  require_action(window, "fileInfoAction")->trigger();
  QApplication::processEvents();
}


// Issue 219 captures: File Info after a PSD save shows the created and modified dates and
// the creating application; a Photoshop file resaved by Patchy keeps its application and
// created date and lists Patchy in its history. Saves after-new-document.png,
// after-photoshop-file.png and after-photoshop-file-history.png into test-artifacts/file-info.
QString show_file_info_and_capture(patchy::ui::MainWindow& window, const QString& name, bool history_too) {
  QString application;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyFileInfoDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    dialog->resize(dialog->width(), std::max(dialog->height(), 760));
    QApplication::processEvents();
    auto* created = dialog->findChild<QLabel*>(QStringLiteral("fileInfoCreatedLabel"));
    auto* app = dialog->findChild<QLabel*>(QStringLiteral("fileInfoApplicationLabel"));
    CHECK(created != nullptr && app != nullptr);
    if (created != nullptr && app != nullptr) {
      CHECK(!created->text().isEmpty());
      application = app->text();
      for (auto* parent = app->parentWidget(); parent != nullptr; parent = parent->parentWidget()) {
        if (auto* scroll = qobject_cast<QScrollArea*>(parent)) {
          scroll->ensureWidgetVisible(app, 0, 24);
          break;
        }
      }
    }
    QApplication::processEvents();
    dialog->grab().save(artifact_dir() + QStringLiteral("/%1.png").arg(name));
    if (history_too) {
      auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("fileInfoCategoryList"));
      for (int row = 0; categories != nullptr && row < categories->count(); ++row) {
        if (categories->item(row)->data(Qt::UserRole).toString() == QStringLiteral("fileInfoAllMetadataPage")) {
          categories->setCurrentRow(row);
        }
      }
      QApplication::processEvents();
      auto* tree = dialog->findChild<QTreeWidget*>(QStringLiteral("fileInfoAllMetadataTree"));
      CHECK(tree != nullptr);
      if (tree != nullptr) {
        const auto matches = tree->findItems(QStringLiteral("xmpMM:History"), Qt::MatchContains | Qt::MatchRecursive);
        CHECK(!matches.isEmpty());
        if (!matches.isEmpty()) {
          tree->scrollToItem(matches.back(), QAbstractItemView::PositionAtBottom);
          tree->setCurrentItem(matches.back());
        }
      }
      QApplication::processEvents();
      dialog->grab().save(artifact_dir() + QStringLiteral("/%1-history.png").arg(name));
    }
    dialog->reject();
  });
  require_action(window, "fileInfoAction")->trigger();
  QApplication::processEvents();
  return application;
}

void ui_file_info_shows_the_psd_save_stamp() {
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 640, 480);
  QApplication::processEvents();
  const auto saved = artifact_dir() + QStringLiteral("/new-document.psd");
  QFile::remove(saved);
  CHECK(MainWindowTestAccess::save_document_to_path(window, saved));
  CHECK(show_file_info_and_capture(window, QStringLiteral("after-new-document"), false).startsWith(QStringLiteral("Patchy ")));

  const auto source = QString::fromStdWString(patchy::test::local_psd_fixture_path("checkbox.psd").wstring());
  if (!QFileInfo::exists(source)) {
    patchy::test::skip("optional fixture missing: local-test-fixtures/psd/checkbox.psd");
  }
  MainWindowTestAccess::open_document_path(window, source);
  QApplication::processEvents();
  const auto resaved = artifact_dir() + QStringLiteral("/photoshop-resave.psd");
  QFile::remove(resaved);
  CHECK(MainWindowTestAccess::save_document_to_path(window, resaved));
  CHECK(show_file_info_and_capture(window, QStringLiteral("after-photoshop-file"), true).startsWith(QStringLiteral("Adobe Photoshop")));
}

// The JFIF image in image resource 1036 of a read PSD, or a null image.
QImage psd_thumbnail(const patchy::Document& document, QSize* header_size = nullptr) {
  for (const auto& [id, payload] : document.file_info().psd_resources) {
    if (id != 1036 || !payload || payload->size() <= 28U) {
      continue;
    }
    const auto u32 = [&payload](std::size_t at) {
      return static_cast<int>((std::uint32_t{(*payload)[at]} << 24U) | (std::uint32_t{(*payload)[at + 1]} << 16U) |
                              (std::uint32_t{(*payload)[at + 2]} << 8U) | std::uint32_t{(*payload)[at + 3]});
    };
    if (header_size != nullptr) {
      *header_size = QSize(u32(4), u32(8));
    }
    return QImage::fromData(QByteArray(reinterpret_cast<const char*>(payload->data()) + 28,
                                       static_cast<qsizetype>(payload->size() - 28U)),
                            "JPG");
  }
  return {};
}

std::uint32_t exif_number(const patchy::DocumentFileInfo& info, md::IfdKind kind, std::uint16_t tag) {
  const auto stream = info.exif ? md::parse_tiff(*info.exif) : std::nullopt;
  const auto* ifd = stream ? stream->find(kind) : nullptr;
  const auto* entry = ifd != nullptr ? ifd->find(tag) : nullptr;
  return entry != nullptr ? stream->unsigned_value(*entry).value_or(0U) : 0U;
}

// File Info > All Metadata for the active document, cut to `groups` (expanded), saved as
// test-artifacts/<name>.png.
void capture_all_metadata(patchy::ui::MainWindow& window, const std::string& name, const QStringList& groups) {
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyFileInfoDialog"));
    if (dialog == nullptr) {
      patchy::test::report_failure(name + ": no File Info dialog");
      return;
    }
    dialog->resize(std::max(dialog->width(), 1180), std::max(dialog->height(), 980));
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("fileInfoCategoryList"));
    for (int row = 0; categories != nullptr && row < categories->count(); ++row) {
      if (categories->item(row)->data(Qt::UserRole).toString() == QStringLiteral("fileInfoAllMetadataPage")) {
        categories->setCurrentRow(row);
      }
    }
    QApplication::processEvents();
    auto* tree = dialog->findChild<QTreeWidget*>(QStringLiteral("fileInfoAllMetadataTree"));
    if (tree == nullptr) {
      patchy::test::report_failure(name + ": no All Metadata tree");
      dialog->reject();
      return;
    }
    for (int index = 0; index < tree->topLevelItemCount(); ++index) {
      auto* group = tree->topLevelItem(index);
      group->setHidden(!groups.contains(group->text(0)));
      group->setExpanded(true);
    }
    tree->resizeColumnToContents(0);
    tree->resizeColumnToContents(1);
    tree->clearSelection();
    tree->setCurrentItem(nullptr);
    tree->scrollToTop();
    QApplication::processEvents();
    save_widget_artifact(name, *tree);
    dialog->reject();
  });
  require_action(window, "fileInfoAction")->trigger();
  QApplication::processEvents();
}

void fill_first_layer(patchy::Document& document, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  auto& pixels = document.layers().front().pixels();
  const auto channels = static_cast<std::size_t>(pixels.format().channels);
  auto bytes = pixels.data();
  for (std::size_t at = 0; at + channels <= bytes.size(); at += channels) {
    bytes[at] = red;
    bytes[at + 1] = green;
    bytes[at + 2] = blue;
    if (channels == 4U) {
      bytes[at + 3] = 255;
    }
  }
}

// Issues 418 and 419: a new document's PSD save carries what a Photoshop document does:
// dc:format, photoshop:ColorMode and an EXIF block (Software, DateTime, size, resolution,
// color space), and a thumbnail of the image as saved (resource 1036, at most 160 pixels).
// A later save writes a new thumbnail and a new instance; a file with another thumbnail
// gets the current one; after Strip All Metadata no EXIF comes back. Saves the decoded
// thumbnails (thumbnail-*.png) into test-artifacts/file-info.
void ui_psd_save_writes_metadata_and_thumbnail() {
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 640, 480);
  QApplication::processEvents();
  fill_first_layer(MainWindowTestAccess::document(window), 30, 90, 200);
  const auto path = artifact_dir() + QStringLiteral("/new-document-metadata.psd");
  QFile::remove(path);
  CHECK(MainWindowTestAccess::save_document_to_path(window, path));
  const auto first = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
  CHECK(first.file_info().xmp != nullptr && first.file_info().exif != nullptr);
  if (first.file_info().xmp == nullptr) {
    return;
  }
  const auto packet = md::parse_xmp(*first.file_info().xmp).value_or(md::XmpPacket{});
  CHECK(packet.text(md::xmp_ns::kDc, "format") == "application/vnd.adobe.photoshop");
  CHECK(packet.text(md::xmp_ns::kPhotoshop, "ColorMode") == "3");
  CHECK(packet.text(md::xmp_ns::kXmp, "CreatorTool").rfind("Patchy ", 0) == 0U);
  CHECK(ifd0_text(first.file_info(), md::exif_tag::kSoftware).rfind("Patchy ", 0) == 0U);
  CHECK(ifd0_text(first.file_info(), md::exif_tag::kDateTime).size() == 19U);
  CHECK(exif_number(first.file_info(), md::IfdKind::Ifd0, md::exif_tag::kOrientation) == 1U);
  CHECK(exif_number(first.file_info(), md::IfdKind::Exif, md::exif_tag::kPixelXDimension) == 640U);
  CHECK(exif_number(first.file_info(), md::IfdKind::Exif, md::exif_tag::kPixelYDimension) == 480U);
  QSize header;
  const auto thumbnail = psd_thumbnail(first, &header);
  CHECK(!thumbnail.isNull() && thumbnail.size() == QSize(160, 120) && header == QSize(160, 120));
  CHECK(thumbnail.pixelColor(80, 60).blue() > 170 && thumbnail.pixelColor(80, 60).red() < 60);
  thumbnail.save(artifact_dir() + QStringLiteral("/thumbnail-new-document.png"));
  capture_all_metadata(window, "file-info/after-new-document-metadata",
                       {QStringLiteral("EXIF IFD0"), QStringLiteral("EXIF Exif"), QStringLiteral("XMP xmp"),
                        QStringLiteral("XMP dc"), QStringLiteral("XMP photoshop"), QStringLiteral("XMP xmpMM")});

  // Edited and saved again: a new thumbnail, a new instance, the same creation record.
  fill_first_layer(MainWindowTestAccess::document(window), 220, 40, 30);
  CHECK(MainWindowTestAccess::save_document_to_path(window, path));
  const auto second = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
  const auto again = md::parse_xmp(*second.file_info().xmp).value_or(md::XmpPacket{});
  CHECK(again.text(md::xmp_ns::kXmp, "CreateDate") == packet.text(md::xmp_ns::kXmp, "CreateDate"));
  CHECK(again.text(md::xmp_ns::kXmpMM, "InstanceID") != packet.text(md::xmp_ns::kXmpMM, "InstanceID"));
  const auto resaved = psd_thumbnail(second);
  CHECK(!resaved.isNull() && resaved.pixelColor(80, 60).red() > 180 && resaved.pixelColor(80, 60).blue() < 80);

  // A file whose thumbnail shows another image (as a Photoshop file edited elsewhere does):
  // the save replaces it with the image as it is now.
  auto stale_source = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
  QImage old_picture(160, 120, QImage::Format_RGB888);
  old_picture.fill(QColor(20, 160, 60));
  const auto old_jfif = patchy::ui::encode_thumbnail_jpeg(old_picture);
  auto& resources = stale_source.file_info().psd_resources;
  std::erase_if(resources, [](const auto& resource) { return resource.first == 1036; });
  resources.emplace_back(std::uint16_t{1036}, patchy::make_metadata_bytes(patchy::psd::thumbnail_resource(160, 120, old_jfif)));
  const auto stale_path = artifact_dir() + QStringLiteral("/old-thumbnail.psd");
  {
    const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(stale_source);
    QFile file(stale_path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
  }
  const auto before = psd_thumbnail(patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(stale_path)));
  CHECK(!before.isNull() && before.pixelColor(80, 60).green() > 120);
  before.save(artifact_dir() + QStringLiteral("/thumbnail-before.png"));
  MainWindowTestAccess::open_document_path(window, stale_path);
  QApplication::processEvents();
  const auto after_path = artifact_dir() + QStringLiteral("/old-thumbnail-resaved.psd");
  QFile::remove(after_path);
  CHECK(MainWindowTestAccess::save_document_to_path(window, after_path));
  const auto after = psd_thumbnail(patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(after_path)));
  CHECK(!after.isNull() && after.pixelColor(80, 60).red() > 180 && after.pixelColor(80, 60).green() < 90);
  after.save(artifact_dir() + QStringLiteral("/thumbnail-after.png"));

  // Strip All Metadata: the next save records the XMP stamp and the thumbnail, no EXIF.
  patchy::metadata::strip_metadata(MainWindowTestAccess::document(window), true);
  const auto stripped_path = artifact_dir() + QStringLiteral("/stripped.psd");
  QFile::remove(stripped_path);
  CHECK(MainWindowTestAccess::save_document_to_path(window, stripped_path));
  const auto stripped = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(stripped_path));
  CHECK(stripped.file_info().exif == nullptr);
  CHECK(stripped.file_info().xmp != nullptr);
  CHECK(!psd_thumbnail(stripped).isNull());
}

// Issues 418 and 419 (decided October 25, 2026): with Saved metadata Basic, a new document's
// JPEG Save As records itself as a PSD save does: the XMP stamp with dc:format image/jpeg, a
// minimal EXIF block and an EXIF thumbnail of the saved image. After Strip All Metadata the
// next save writes the XMP stamp but no EXIF and so no thumbnail. Saves
// after-new-jpeg-metadata.png (All Metadata) and after-new-jpeg-thumbnail.png (Embedded
// Thumbnails, from the reopened file) into test-artifacts/file-info.
void ui_jpeg_save_as_stamps_a_new_document() {
  patchy::ui::set_stored_save_metadata_mode(patchy::ui::SaveMetadataMode::Basic);
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 640, 480);
  QApplication::processEvents();
  {
    auto& pixels = MainWindowTestAccess::document(window).layers().front().pixels();
    const auto channels = static_cast<std::size_t>(pixels.format().channels);
    auto bytes = pixels.data();
    for (int y = 0; y < pixels.height(); ++y) {
      for (int x = 0; x < pixels.width(); ++x) {
        auto* pixel = bytes.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(pixels.width()) +
                                      static_cast<std::size_t>(x)) * channels;
        pixel[0] = static_cast<std::uint8_t>(x * 255 / 639);
        pixel[1] = static_cast<std::uint8_t>(y * 255 / 479);
        pixel[2] = 160;
        if (channels == 4U) {
          pixel[3] = 255;
        }
      }
    }
  }
  const auto path = artifact_dir() + QStringLiteral("/new-document.jpg");
  QFile::remove(path);
  // A new document can hold more than one layer: the JPEG is then a flattened copy.
  CHECK(MainWindowTestAccess::save_document_to_path_flatten_confirmed(window, path, patchy::ui::ImageSaveOptions{}));
  const auto saved = md::extract_file_info_from_path(patchy::ui::to_filesystem_path(path));
  CHECK(saved.xmp != nullptr && saved.exif != nullptr);
  if (saved.xmp == nullptr || saved.exif == nullptr) {
    return;
  }
  const auto packet = md::parse_xmp(*saved.xmp).value_or(md::XmpPacket{});
  CHECK(packet.text(md::xmp_ns::kXmp, "CreatorTool").rfind("Patchy ", 0) == 0U);
  CHECK(packet.text(md::xmp_ns::kXmp, "CreateDate").size() == 25U);
  CHECK(packet.text(md::xmp_ns::kXmpMM, "InstanceID").rfind("xmp.iid:", 0) == 0U);
  CHECK(packet.find(md::xmp_ns::kXmpMM, "History") != nullptr);
  CHECK(packet.text(md::xmp_ns::kDc, "format") == "image/jpeg");
  CHECK(packet.text(md::xmp_ns::kPhotoshop, "ColorMode") == "3");
  CHECK(ifd0_text(saved, md::exif_tag::kSoftware).rfind("Patchy ", 0) == 0U);
  CHECK(ifd0_text(saved, md::exif_tag::kDateTime).size() == 19U);
  CHECK(exif_number(saved, md::IfdKind::Ifd0, md::exif_tag::kOrientation) == 1U);
  CHECK(exif_number(saved, md::IfdKind::Exif, md::exif_tag::kPixelXDimension) == 640U);
  CHECK(exif_number(saved, md::IfdKind::Exif, md::exif_tag::kPixelYDimension) == 480U);
  const auto stream = md::parse_tiff(*saved.exif);
  CHECK(stream.has_value() && !stream->thumbnail.empty() && stream->find(md::IfdKind::Ifd1) != nullptr);
  if (stream && !stream->thumbnail.empty()) {
    const auto image = QImage::fromData(QByteArray(reinterpret_cast<const char*>(stream->thumbnail.data()),
                                                   static_cast<qsizetype>(stream->thumbnail.size())),
                                        "JPG");
    CHECK(image.size() == QSize(160, 120));
  }
  CHECK(!QImage(path).isNull());
  capture_all_metadata(window, "file-info/after-new-jpeg-metadata",
                       {QStringLiteral("EXIF IFD0"), QStringLiteral("EXIF Exif"), QStringLiteral("EXIF IFD1 (thumbnail)"),
                        QStringLiteral("XMP xmp"), QStringLiteral("XMP dc"), QStringLiteral("XMP photoshop"),
                        QStringLiteral("XMP xmpMM")});

  // The reopened file shows its thumbnail on File Info's Embedded Thumbnails page.
  MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  bool shown = false;
  QTimer::singleShot(0, [&shown] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyFileInfoDialog"));
    if (dialog == nullptr) {
      patchy::test::report_failure("new JPEG: no File Info dialog");
      return;
    }
    auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("fileInfoCategoryList"));
    for (int row = 0; categories != nullptr && row < categories->count(); ++row) {
      if (categories->item(row)->data(Qt::UserRole).toString() == QStringLiteral("fileInfoThumbnailsPage")) {
        categories->setCurrentRow(row);
        shown = true;
      }
    }
    dialog->resize(dialog->width(), std::max(dialog->height(), 640));
    QApplication::processEvents();
    if (shown) {
      save_widget_artifact("file-info/after-new-jpeg-thumbnail", *dialog);
    }
    dialog->reject();
  });
  require_action(window, "fileInfoAction")->trigger();
  QApplication::processEvents();
  CHECK(shown);

  // Strip All Metadata: the next Save As writes the XMP stamp but no EXIF, so no thumbnail.
  patchy::metadata::strip_metadata(MainWindowTestAccess::document(window), true);
  const auto stripped_path = artifact_dir() + QStringLiteral("/stripped-new.jpg");
  QFile::remove(stripped_path);
  CHECK(MainWindowTestAccess::save_document_to_path_flatten_confirmed(window, stripped_path,
                                                                      patchy::ui::ImageSaveOptions{}));
  const auto stripped = md::extract_file_info_from_path(patchy::ui::to_filesystem_path(stripped_path));
  CHECK(stripped.exif == nullptr);
  CHECK(stripped.xmp != nullptr);
}

// Issue 419: a JPEG save that writes EXIF puts a thumbnail of the saved image in IFD1.
void ui_jpeg_save_writes_an_exif_thumbnail() {
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, write_tagged_jpeg());
  QApplication::processEvents();
  const auto target = artifact_dir() + QStringLiteral("/thumbnail.jpg");
  QFile::remove(target);
  CHECK(MainWindowTestAccess::save_document_to_path(window, target, patchy::ui::ImageSaveOptions{}));
  const auto saved = md::extract_file_info_from_path(patchy::ui::to_filesystem_path(target));
  const auto stream = saved.exif ? md::parse_tiff(*saved.exif) : std::nullopt;
  CHECK(stream.has_value() && !stream->thumbnail.empty());
  if (stream && !stream->thumbnail.empty()) {
    const auto image = QImage::fromData(QByteArray(reinterpret_cast<const char*>(stream->thumbnail.data()),
                                                   static_cast<qsizetype>(stream->thumbnail.size())),
                                        "JPG");
    CHECK(!image.isNull() && image.width() <= 160 && image.height() <= 160);
    CHECK(stream->find(md::IfdKind::Ifd1) != nullptr);
  }
  CHECK(md::summarize_camera(saved).has_location());
  CHECK(!QImage(target).isNull());
}

// Export for Web (docs/web-export.md): metadata None by default, so no EXIF and no
// thumbnail; with EXIF kept the IFD1 thumbnail is written only when asked for.
void ui_web_export_metadata_and_thumbnail_choices() {
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, write_tagged_jpeg());
  QApplication::processEvents();
  const auto& document = MainWindowTestAccess::document(window);
  const QSize size(document.width(), document.height());
  int index = 0;
  const auto export_with = [&](const patchy::ui::WebExportSettings& settings) {
    const auto job = patchy::ui::web_export_job(settings, patchy::ui::WebExportFormat::Jpeg, size);
    const auto prepared =
        patchy::ui::WebExportPreviewEngine::prepare(document, job.geometry, job.convert_to_srgb, nullptr);
    const auto result = patchy::ui::WebExportPreviewEngine::encode(document, job, prepared);
    const auto path = artifact_dir() + QStringLiteral("/web-export-%1.jpg").arg(index++);
    patchy::ui::write_encoded_image_file(path, *result.bytes);
    CHECK(!QImage(path).isNull());
    return md::extract_file_info_from_path(patchy::ui::to_filesystem_path(path));
  };
  patchy::ui::WebExportSettings defaults;
  CHECK(defaults.metadata == md::MetadataPolicy::None);
  CHECK(!defaults.thumbnail);
  const auto plain = export_with(defaults);
  CHECK(plain.exif == nullptr);
  CHECK(plain.xmp == nullptr);

  auto kept = defaults;
  kept.metadata = md::MetadataPolicy::All;
  const auto without_thumbnail = export_with(kept);
  const auto stream = without_thumbnail.exif ? md::parse_tiff(*without_thumbnail.exif) : std::nullopt;
  CHECK(stream.has_value() && stream->thumbnail.empty());
  CHECK(without_thumbnail.xmp != nullptr);

  kept.thumbnail = true;
  const auto with_thumbnail = export_with(kept);
  const auto thumbnail_stream = with_thumbnail.exif ? md::parse_tiff(*with_thumbnail.exif) : std::nullopt;
  CHECK(thumbnail_stream.has_value() && !thumbnail_stream->thumbnail.empty());

  // None without the thumbnail writes no APP1 at all; with it, one APP1 Exif holding IFD0's
  // resolution and IFD1 with the thumbnail, and nothing else (no XMP, IPTC or other tags).
  const auto app1_count = [](const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      return -1;
    }
    const auto bytes = file.readAll();
    int count = 0;
    qsizetype at = 2;
    while (at + 4 <= bytes.size() && static_cast<unsigned char>(bytes[at]) == 0xFF) {
      const auto marker = static_cast<unsigned char>(bytes[at + 1]);
      if (marker == 0xDA) {
        break;
      }
      count += marker == 0xE1 ? 1 : 0;
      at += 2 + ((static_cast<unsigned char>(bytes[at + 2]) << 8) | static_cast<unsigned char>(bytes[at + 3]));
    }
    return count;
  };
  CHECK(app1_count(artifact_dir() + QStringLiteral("/web-export-0.jpg")) == 0);
  auto none_with_thumbnail = defaults;
  none_with_thumbnail.thumbnail = true;
  const auto thumbnail_only = export_with(none_with_thumbnail);
  CHECK(app1_count(artifact_dir() + QStringLiteral("/web-export-%1.jpg").arg(index - 1)) == 1);
  CHECK(thumbnail_only.xmp == nullptr && thumbnail_only.iptc == nullptr && thumbnail_only.text.empty());
  const auto only = thumbnail_only.exif ? md::parse_tiff(*thumbnail_only.exif) : std::nullopt;
  CHECK(only.has_value() && !only->thumbnail.empty());
  if (only.has_value()) {
    CHECK(only->find(md::IfdKind::Ifd1) != nullptr);
    CHECK(only->find(md::IfdKind::Exif) == nullptr && only->find(md::IfdKind::Gps) == nullptr);
    const auto* ifd0 = only->find(md::IfdKind::Ifd0);
    CHECK(ifd0 != nullptr);
    if (ifd0 != nullptr) {
      for (const auto& entry : ifd0->entries) {
        CHECK(entry.tag == md::exif_tag::kXResolution || entry.tag == md::exif_tag::kYResolution ||
              entry.tag == md::exif_tag::kResolutionUnit);
      }
    }
    const auto image = QImage::fromData(QByteArray(reinterpret_cast<const char*>(only->thumbnail.data()),
                                                   static_cast<qsizetype>(only->thumbnail.size())),
                                        "JPG");
    CHECK(!image.isNull() && image.width() <= 160 && image.height() <= 160);
  }
  CHECK(!md::summarize_camera(thumbnail_only).has_location());

  // Basic: the save stamp alone, for this file; nothing from the document's File Info.
  auto basic = defaults;
  basic.metadata = md::MetadataPolicy::Basic;
  const auto stamped = export_with(basic);
  const auto basic_exif = stamped.exif ? md::parse_tiff(*stamped.exif) : std::nullopt;
  CHECK(basic_exif.has_value() && basic_exif->thumbnail.empty());
  if (basic_exif.has_value()) {
    const auto* ifd0 = basic_exif->find(md::IfdKind::Ifd0);
    CHECK(ifd0 != nullptr && ifd0->find(md::exif_tag::kSoftware) != nullptr &&
          ifd0->find(md::exif_tag::kDateTime) != nullptr);
    CHECK(ifd0 != nullptr && ifd0->find(md::exif_tag::kArtist) == nullptr &&
          ifd0->find(md::exif_tag::kCopyright) == nullptr);
    CHECK(basic_exif->find(md::IfdKind::Gps) == nullptr);
  }
  CHECK(!md::summarize_camera(stamped).has_location());
  CHECK(stamped.iptc == nullptr);
  const auto packet = stamped.xmp ? md::parse_xmp(*stamped.xmp) : std::nullopt;
  CHECK(packet.has_value());
  if (packet.has_value()) {
    CHECK(packet->text(md::xmp_ns::kXmp, "CreatorTool").starts_with("Patchy "));
    CHECK(!packet->text(md::xmp_ns::kXmp, "CreateDate").empty() && !packet->text(md::xmp_ns::kXmp, "ModifyDate").empty());
    CHECK(packet->text(md::xmp_ns::kDc, "format") == "image/jpeg");
    CHECK(packet->find(md::xmp_ns::kDc, "creator") == nullptr && packet->find(md::xmp_ns::kDc, "rights") == nullptr &&
          packet->find(md::xmp_ns::kDc, "description") == nullptr);
  }
  // Basic with the thumbnail: the stamp's EXIF carries it.
  basic.thumbnail = true;
  const auto stamped_thumbnail = export_with(basic);
  const auto basic_thumbnail = stamped_thumbnail.exif ? md::parse_tiff(*stamped_thumbnail.exif) : std::nullopt;
  CHECK(basic_thumbnail.has_value() && !basic_thumbnail->thumbnail.empty());

  // The token round trips.
  CHECK(patchy::ui::metadata_policy_key(md::MetadataPolicy::Basic) == QStringLiteral("basic"));
  CHECK(patchy::ui::metadata_policy_from_key(QStringLiteral("basic")) == md::MetadataPolicy::Basic);
  // Save As keeps its bytes: no thumbnail-only EXIF unless asked for.
  CHECK(!patchy::ui::ImageSaveOptions{}.jpeg_exif_thumbnail_without_metadata);
}

// Issue 417: File Info's privacy notice has Don't Show Again, which turns it off for every
// document until Preferences turns it back on. Saves notice-shown.png and notice-hidden.png
// into test-artifacts/file-info.
void ui_file_info_privacy_notice_can_be_hidden() {
  patchy::ui::set_stored_show_file_info_privacy_notice(true);
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, write_tagged_jpeg());
  QApplication::processEvents();
  const auto open_and = [&window](const std::function<void(QDialog&)>& inspect) {
    bool inspected = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyFileInfoDialog"));
      if (dialog == nullptr) {
        patchy::test::report_failure("privacy notice: no File Info dialog");
        return;
      }
      inspect(*dialog);
      inspected = true;
      dialog->reject();
    });
    require_action(window, "fileInfoAction")->trigger();
    QApplication::processEvents();
    if (!inspected) {
      patchy::test::report_failure("privacy notice: the dialog was not inspected");
    }
  };
  const auto expect = [](bool ok, const char* what) {
    if (!ok) {
      patchy::test::report_failure(std::string("privacy notice: ") + what);
    }
  };
  open_and([&expect](QDialog& dialog) {
    auto* banner = dialog.findChild<QLabel*>(QStringLiteral("fileInfoPrivacyBanner"));
    auto* hide = dialog.findChild<QPushButton*>(QStringLiteral("fileInfoHidePrivacyNoticeButton"));
    expect(banner != nullptr && !banner->isHidden(), "the notice is not shown");
    expect(hide != nullptr && !hide->isHidden(), "Don't Show Again is not shown");
    if (auto* row = dialog.findChild<QWidget*>(QStringLiteral("fileInfoPrivacyBannerRow"))) {
      QApplication::processEvents();
      // The button sits inside the bar, as far from its right edge as from its top and bottom.
      if (hide != nullptr) {
        const auto box = hide->geometry();
        const int right = row->width() - (box.x() + box.width());
        const int top = box.y();
        const int bottom = row->height() - (box.y() + box.height());
        expect(box.x() >= 0 && right >= 0 && top >= 0 && bottom >= 0, "Don't Show Again is outside the bar");
        expect(std::abs(top - bottom) <= 1, "Don't Show Again is not centered vertically");
        expect(std::abs(right - top) <= 1, "Don't Show Again's right gap differs from its top gap");
      }
      save_widget_artifact("file-info/notice-shown", *row);
    }
    if (hide != nullptr) {
      hide->click();
    }
    QApplication::processEvents();
    expect(banner == nullptr || banner->isHidden(), "the notice stays after Don't Show Again");
  });
  CHECK(!patchy::ui::stored_show_file_info_privacy_notice());
  open_and([&expect](QDialog& dialog) {
    auto* banner = dialog.findChild<QLabel*>(QStringLiteral("fileInfoPrivacyBanner"));
    expect(banner != nullptr && banner->isHidden(), "the notice is back on the next open");
    dialog.resize(dialog.width(), std::max(dialog.height(), 640));
    QApplication::processEvents();
    save_widget_artifact("file-info/notice-hidden", dialog);
  });
  // The Preferences checkbox writes the same setting.
  patchy::ui::set_stored_show_file_info_privacy_notice(true);
  open_and([&expect](QDialog& dialog) {
    auto* banner = dialog.findChild<QLabel*>(QStringLiteral("fileInfoPrivacyBanner"));
    expect(banner != nullptr && !banner->isHidden(), "the notice does not come back");
  });
}

QImage rgb_qimage(const std::vector<std::uint8_t>& rgb, int width, int height) {
  QImage image(width, height, QImage::Format_RGB888);
  for (int y = 0; y < height; ++y) {
    std::memcpy(image.scanLine(y), rgb.data() + static_cast<std::size_t>(y) * width * 3,
                static_cast<std::size_t>(width) * 3);
  }
  return image;
}

patchy::InvisibleWatermarkScan scan_qimage(const QImage& source) {
  const auto image = source.convertToFormat(QImage::Format_RGB888);
  return patchy::scan_invisible_watermarks(
      std::span(image.constBits(), static_cast<std::size_t>(image.sizeInBytes())), image.width(), image.height(),
      static_cast<std::size_t>(image.bytesPerLine()), 3);
}

// File Info's Watermarks page (docs/file-info.md, "Watermarks"): nothing is scanned
// until Scan for Watermarks is pressed; a hit shows on the page, in the banner and in
// All Metadata; a clean image reports none. Also checks the decoder after JPEG
// recompression.
void ui_file_info_scans_for_watermarks() {
  constexpr int kSize = 512;
  const auto clean = patchy::test::watermark_fixture_image(kSize, kSize, 7);
  const auto& known = patchy::known_invisible_watermarks();
  const auto xl =
      std::find_if(known.begin(), known.end(), [](const auto& item) { return item.id == "stableDiffusionXL"; });
  CHECK(xl != known.end());
  const auto marked_rgb = patchy::test::embed_dwt_dct_reference(clean, kSize, kSize, xl->bits,
                                                                patchy::test::WatermarkEmbedPath::Float);
  const auto marked = rgb_qimage(marked_rgb, kSize, kSize);
  const auto marked_path = artifact_dir() + QStringLiteral("/sdxl-watermarked.png");
  const auto clean_path = artifact_dir() + QStringLiteral("/no-watermark.png");
  CHECK(marked.save(marked_path));
  CHECK(rgb_qimage(clean, kSize, kSize).save(clean_path));

  CHECK(scan_qimage(marked).matches.size() == 1);
  // JPEG recompression (Qt's encoder subsamples chroma 4:2:0): recorded, not required.
  // The mark lives in fine chroma detail, so the decoder does not survive it
  // (docs/file-info.md, "Watermarks"); the scan must still run and report cleanly.
  for (const int quality : {100, 95, 90}) {
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    CHECK(marked.save(&buffer, "JPG", quality));
    const auto reloaded = QImage::fromData(buffer.data(), "JPG").convertToFormat(QImage::Format_RGB888);
    CHECK(scan_qimage(reloaded).scanned);
    int best = 48;
    for (int reading = 0; reading < 8; ++reading) {
      const patchy::DwtDctDecodeOptions options{
          (reading & 1) != 0, (reading & 2) != 0,
          (reading & 4) != 0 ? patchy::DwtDctBitRule::TwoCluster : patchy::DwtDctBitRule::Library};
      const auto bits = patchy::decode_dwt_dct_bits(
          std::span(reloaded.constBits(), static_cast<std::size_t>(reloaded.sizeInBytes())), reloaded.width(),
          reloaded.height(), static_cast<std::size_t>(reloaded.bytesPerLine()), 3, 48, options);
      int differing = 0;
      for (std::size_t i = 0; i < bits.size(); ++i) {
        differing += bits[i] != xl->bits[i] ? 1 : 0;
      }
      best = std::min(best, differing);
    }
    std::cout << "  JPEG quality " << quality << ": best reading has " << best << " of 48 bits wrong ("
              << (scan_qimage(reloaded).matches.empty() ? "not found" : "found") << ")\n";
  }

  // Without the TrustMark models (docs/watermarks.md), so only the Stable Diffusion scan runs.
  const auto no_models = std::filesystem::current_path() / "test-artifacts" / "watermark-no-models";
  std::filesystem::create_directories(no_models);
  patchy::ai::set_downloaded_model_directory(no_models);
  struct Reset {
    ~Reset() { patchy::ai::set_downloaded_model_directory({}); }
  } reset;
  patchy::ui::MainWindow window;
  show_window(window);
  const auto inspect = [&](const QString& path, bool expect_hit) {
    MainWindowTestAccess::open_document_path(window, path);
    QApplication::processEvents();
    bool inspected = false;
    QTimer::singleShot(0, [&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyFileInfoDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      auto* banner = dialog->findChild<QLabel*>(QStringLiteral("fileInfoPrivacyBanner"));
      CHECK(banner != nullptr);
      auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("fileInfoCategoryList"));
      CHECK(categories != nullptr);
      for (int row = 0; categories != nullptr && row < categories->count(); ++row) {
        if (categories->item(row)->data(Qt::UserRole).toString() == QStringLiteral("fileInfoWatermarksPage")) {
          categories->setCurrentRow(row);
        }
      }
      QApplication::processEvents();
      // Nothing is scanned before the button is pressed.
      CHECK(dialog->findChild<QLabel*>(QStringLiteral("fileInfoWatermarkScanResult")) == nullptr);
      CHECK(banner != nullptr && !banner->text().contains(QStringLiteral("Stable Diffusion")));
      auto* scan = dialog->findChild<QPushButton*>(QStringLiteral("fileInfoScanWatermarksButton"));
      CHECK(scan != nullptr && scan->isVisible());
      if (scan != nullptr) {
        scan->click();
      }
      QApplication::processEvents();
      QLabel* result = nullptr;
      for (auto* label : dialog->findChildren<QLabel*>(QStringLiteral("fileInfoWatermarkScanResult"))) {
        if (label->isVisible()) {
          result = label;
        }
      }
      CHECK(result != nullptr);
      auto* tree = dialog->findChild<QTreeWidget*>(QStringLiteral("fileInfoAllMetadataTree"));
      CHECK(tree != nullptr);
      const auto rows = tree != nullptr ? tree->findItems(QStringLiteral("Invisible watermark"),
                                                          Qt::MatchExactly | Qt::MatchRecursive)
                                        : QList<QTreeWidgetItem*>{};
      // One row per kind, in a fixed order; TrustMark is Not checked without its models.
      QStringList order;
      QMap<QString, QString> statuses;
      for (auto* label : dialog->findChildren<QLabel*>(QStringLiteral("fileInfoWatermarkCheckStatus"))) {
        if (label->isVisible()) {
          order.push_back(label->property("watermarkCheck").toString());
          statuses.insert(order.back(), label->property("watermarkStatus").toString());
        }
      }
      CHECK(order == QStringList({QStringLiteral("stableDiffusion1"), QStringLiteral("stableDiffusion2"),
                                  QStringLiteral("stableDiffusionXL"), QStringLiteral("trustmarkQ"),
                                  QStringLiteral("trustmarkP"), QStringLiteral("trustmarkC"),
                                  QStringLiteral("c2paDeclared")}));
      CHECK(statuses.value(QStringLiteral("stableDiffusion1")) == QStringLiteral("notFound"));
      CHECK(statuses.value(QStringLiteral("stableDiffusionXL")) ==
            (expect_hit ? QStringLiteral("found") : QStringLiteral("notFound")));
      CHECK(statuses.value(QStringLiteral("trustmarkQ")) == QStringLiteral("notChecked"));
      CHECK(statuses.value(QStringLiteral("c2paDeclared")) == QStringLiteral("notFound"));
      if (expect_hit) {
        CHECK(result != nullptr && result->text().contains(QStringLiteral("Stable Diffusion XL")));
        CHECK(banner != nullptr && !banner->isHidden() &&
              banner->text().contains(QStringLiteral("Stable Diffusion XL")));
        CHECK(rows.size() == 1);
        if (rows.size() == 1) {
          CHECK(rows.front()->text(3) == patchy::ui::privacy_class_label(patchy::MetadataPrivacy::Watermark));
        }
        dialog->resize(dialog->width(), std::max(dialog->height(), 640));
        QApplication::processEvents();
        capture_widget_artifact("ui_file_info_watermark_found", *dialog);
      } else {
        CHECK(result != nullptr &&
              result->text() == QObject::tr("None of the watermarks that could be checked was found. Some kinds "
                                            "were not checked."));
        CHECK(rows.isEmpty());
        capture_widget_artifact("ui_file_info_watermark_none", *dialog);
      }
      inspected = true;
      dialog->reject();
    });
    require_action(window, "fileInfoAction")->trigger();
    QApplication::processEvents();
    CHECK(inspected);
  };
  inspect(marked_path, true);
  inspect(clean_path, false);
}

// The document tab's Scan for Watermarks... (docs/document-tabs.md): on the tab of a document
// that is not the active one, it activates that document and opens File Info on the
// Watermarks page with the scan already run. File > Scan for Watermarks... is the same
// command (file.scan_for_watermarks) for the active document.
void ui_document_tab_scan_for_watermarks() {
  constexpr int kSize = 512;
  const auto clean = patchy::test::watermark_fixture_image(kSize, kSize, 11);
  const auto& known = patchy::known_invisible_watermarks();
  const auto xl =
      std::find_if(known.begin(), known.end(), [](const auto& item) { return item.id == "stableDiffusionXL"; });
  CHECK(xl != known.end());
  const auto marked_path = artifact_dir() + QStringLiteral("/tab-scan-sdxl.png");
  const auto clean_path = artifact_dir() + QStringLiteral("/tab-scan-clean.png");
  CHECK(rgb_qimage(patchy::test::embed_dwt_dct_reference(clean, kSize, kSize, xl->bits,
                                                          patchy::test::WatermarkEmbedPath::Float),
                   kSize, kSize)
            .save(marked_path));
  CHECK(rgb_qimage(clean, kSize, kSize).save(clean_path));
  // Without the TrustMark models, so only the Stable Diffusion scan runs (fast and the same everywhere).
  const auto no_models = std::filesystem::current_path() / "test-artifacts" / "watermark-no-models";
  std::filesystem::create_directories(no_models);
  patchy::ai::set_downloaded_model_directory(no_models);
  struct Reset {
    ~Reset() { patchy::ai::set_downloaded_model_directory({}); }
  } reset;

  patchy::ui::MainWindow window;
  show_window(window);
  auto* file_action = require_action(window, "fileScanForWatermarksAction");
  CHECK(file_action->text() == QObject::tr("Scan for &Watermarks..."));
  MainWindowTestAccess::open_document_path(window, marked_path);
  MainWindowTestAccess::open_document_path(window, clean_path);
  QApplication::processEvents();
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  if (tabs == nullptr) {
    return;
  }
  auto* tab_bar = tabs->findChild<QTabBar*>();
  CHECK(tab_bar != nullptr);
  int marked_tab = -1;
  for (int index = 0; index < tabs->count(); ++index) {
    if (tabs->tabText(index).startsWith(QStringLiteral("tab-scan-sdxl"))) {
      marked_tab = index;
    }
  }
  CHECK(marked_tab >= 0 && tabs->currentIndex() != marked_tab);  // the clean one is active
  if (tab_bar == nullptr || marked_tab < 0) {
    return;
  }

  bool saw_menu = false;
  bool saw_results = false;
  QStringList found_rows;
  QTimer driver;
  driver.setInterval(20);
  QObject::connect(&driver, &QTimer::timeout, &window, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyFileInfoDialog"));
    if (dialog == nullptr) {
      return;
    }
    for (auto* label : dialog->findChildren<QLabel*>(QStringLiteral("fileInfoWatermarkCheckStatus"))) {
      if (label->isVisible() && label->property("watermarkStatus").toString() == QStringLiteral("found")) {
        found_rows.push_back(label->property("watermarkCheck").toString());
      }
    }
    if (dialog->findChild<QLabel*>(QStringLiteral("fileInfoWatermarkScanResult")) == nullptr) {
      return;  // the scan runs right after the dialog shows
    }
    saw_results = true;
    driver.stop();
    dialog->reject();
  });
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("documentTabContextMenu")) {
        continue;
      }
      saw_menu = true;
      QAction* scan = nullptr;
      QAction* rename = nullptr;
      for (auto* action : menu->actions()) {
        if (action->objectName() == QStringLiteral("documentTabScanForWatermarksAction")) {
          scan = action;
        } else if (action->objectName() == QStringLiteral("documentTabRenameFileAction")) {
          rename = action;
        }
      }
      CHECK(scan != nullptr && scan->isEnabled() && scan->text() == QObject::tr("Scan for Watermarks..."));
      // Last, under the file items and their separator.
      CHECK(rename != nullptr && menu->actions().indexOf(scan) == menu->actions().size() - 1 &&
            menu->actions().indexOf(rename) == menu->actions().size() - 3);
      if (scan != nullptr) {
        menu->setActiveAction(scan);
        save_widget_artifact("ui_document_tab_menu_scan_for_watermarks", *menu);
        driver.start();
        scan->trigger();
      }
      menu->close();
      return;
    }
  });
  const auto point = tab_bar->tabRect(marked_tab).center();
  QContextMenuEvent event(QContextMenuEvent::Mouse, point, tab_bar->mapToGlobal(point));
  QApplication::sendEvent(tab_bar, &event);
  QElapsedTimer timer;
  timer.start();
  while (!saw_results && timer.elapsed() < 20000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  driver.stop();
  CHECK(saw_menu);
  CHECK(saw_results);
  CHECK(found_rows.contains(QStringLiteral("stableDiffusionXL")));  // the clicked document, not the active one
  CHECK(tabs->currentIndex() == marked_tab);
}

}  // namespace

std::vector<patchy::test::TestCase> file_info_tests() {
  return {
      {"ui_file_info_opens_views_edits_and_undoes", ui_file_info_opens_views_edits_and_undoes},
      {"ui_file_info_saves_metadata_in_every_carrier", ui_file_info_saves_metadata_in_every_carrier},
      {"ui_file_info_strip_and_export_policies", ui_file_info_strip_and_export_policies},
      {"ui_psd_save_stamps_dates_and_creator", ui_psd_save_stamps_dates_and_creator},
      {"ui_psd_save_updates_exif_and_instance_id", ui_psd_save_updates_exif_and_instance_id},
      {"ui_file_info_shows_the_psd_save_stamp", ui_file_info_shows_the_psd_save_stamp},
      {"ui_psd_save_writes_metadata_and_thumbnail", ui_psd_save_writes_metadata_and_thumbnail},
      {"ui_jpeg_save_writes_an_exif_thumbnail", ui_jpeg_save_writes_an_exif_thumbnail},
      {"ui_web_export_metadata_and_thumbnail_choices", ui_web_export_metadata_and_thumbnail_choices},
      {"ui_jpeg_save_as_stamps_a_new_document", ui_jpeg_save_as_stamps_a_new_document},
      {"ui_file_info_privacy_notice_can_be_hidden", ui_file_info_privacy_notice_can_be_hidden},
      {"ui_file_info_scans_for_watermarks", ui_file_info_scans_for_watermarks},
      {"ui_document_tab_scan_for_watermarks", ui_document_tab_scan_for_watermarks},
  };
}
