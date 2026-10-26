// The installers' "Open with" lists against the formats File > Open lists
// (MainWindow::openable_file_extensions): the Windows installer's
// $PatchyOpenWithExtensions, the Linux desktop file's MimeType line, and the macOS
// bundle's CFBundleDocumentTypes. A new openable extension fails here until every
// platform claims it or the exception is written down below.
#include "ui/main_window.hpp"
#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>

#include <iostream>
#include <vector>

namespace {

using patchy::ui::MainWindow;

QString read_source_text(const char* relative_path) {
  QFile file(QStringLiteral(PATCHY_SOURCE_DIR "/") + QString::fromLatin1(relative_path));
  CHECK(file.open(QIODevice::ReadOnly));
  return QString::fromUtf8(file.readAll());
}

void report_missing(const char* list_name, const QStringList& missing) {
  if (!missing.isEmpty()) {
    std::cerr << list_name << " is missing: " << missing.join(QStringLiteral(", ")).toStdString() << '\n';
  }
}

// PDF is opened but never claimed: Patchy should not appear beside every PDF reader.
bool claimed_nowhere(const QString& extension) {
  return extension == QStringLiteral("pdf");
}

// JPEG XR decodes only through the Windows codec, so only the Windows list claims it.
bool windows_only(const QString& extension) {
  return extension == QStringLiteral("jxr") || extension == QStringLiteral("wdp") ||
         extension == QStringLiteral("hdp");
}

// Krita's and GIMP's own documents, which Patchy only reads. GNOME has no handler rank:
// for a type with no default it picks the first claimant, and Patchy's Flatpak or
// AppImage entry sorts ahead of distro-packaged Krita and GIMP (measured with gio and
// xdg-mime on Ubuntu 24.04, October 2026), so claiming these would take a Krita or GIMP
// user's double-click. The desktop file must not list them.
bool left_to_their_own_editor_on_linux(const QString& extension) {
  static const QSet<QString> extensions = {QStringLiteral("kra"), QStringLiteral("krz"), QStringLiteral("xcf"),
                                           QStringLiteral("xcf.gz"), QStringLiteral("xcf.bz2")};
  return extensions.contains(extension);
}

QSet<QString> windows_open_with_extensions() {
  const auto script = read_source_text("packaging/windows/InstallPatchy.ps1");
  const auto block =
      QRegularExpression(QStringLiteral(R"(\$PatchyOpenWithExtensions = @\(([^)]*)\))")).match(script);
  CHECK(block.hasMatch());
  QSet<QString> extensions;
  auto names = QRegularExpression(QStringLiteral(R"re("([^"]+)")re")).globalMatch(block.captured(1));
  while (names.hasNext()) {
    extensions.insert(names.next().captured(1));
  }
  return extensions;
}

QSet<QString> desktop_mime_types() {
  const auto desktop = read_source_text("packaging/linux/com.rtsoft.patchy.desktop");
  const auto line = QRegularExpression(QStringLiteral(R"(^MimeType=(.*)$)"),
                                       QRegularExpression::MultilineOption)
                        .match(desktop);
  CHECK(line.hasMatch());
  const auto types = line.captured(1).trimmed().split(QLatin1Char(';'), Qt::SkipEmptyParts);
  return QSet<QString>(types.begin(), types.end());
}

// The shared-mime-info type for each openable extension; an empty type means the
// database has none, so the desktop file cannot claim the extension. A new extension
// needs its row here (look its glob up in shared-mime-info's freedesktop.org.xml).
const QHash<QString, QString>& linux_mime_types() {
  static const QHash<QString, QString> types = {
      {QStringLiteral("psd"), QStringLiteral("image/vnd.adobe.photoshop")},
      {QStringLiteral("psb"), {}},
      {QStringLiteral("png"), QStringLiteral("image/png")},
      {QStringLiteral("apng"), QStringLiteral("image/apng")},
      {QStringLiteral("jpg"), QStringLiteral("image/jpeg")},
      {QStringLiteral("jpeg"), QStringLiteral("image/jpeg")},
      {QStringLiteral("bmp"), QStringLiteral("image/bmp")},
      {QStringLiteral("tif"), QStringLiteral("image/tiff")},
      {QStringLiteral("tiff"), QStringLiteral("image/tiff")},
      {QStringLiteral("webp"), QStringLiteral("image/webp")},
      {QStringLiteral("gif"), QStringLiteral("image/gif")},
      {QStringLiteral("aseprite"), QStringLiteral("image/x-aseprite")},
      {QStringLiteral("ase"), QStringLiteral("image/x-aseprite")},
      {QStringLiteral("ora"), QStringLiteral("image/openraster")},
      {QStringLiteral("tga"), QStringLiteral("image/x-tga")},
      {QStringLiteral("ico"), QStringLiteral("image/vnd.microsoft.icon")},
      {QStringLiteral("cur"), QStringLiteral("image/x-win-bitmap")},
      {QStringLiteral("pcx"), QStringLiteral("image/vnd.zbrush.pcx")},
      {QStringLiteral("lbm"), QStringLiteral("image/x-ilbm")},
      {QStringLiteral("iff"), QStringLiteral("image/x-ilbm")},
      {QStringLiteral("bbm"), {}},
      {QStringLiteral("pbm"), QStringLiteral("image/x-portable-bitmap")},
      {QStringLiteral("pgm"), QStringLiteral("image/x-portable-graymap")},
      {QStringLiteral("ppm"), QStringLiteral("image/x-portable-pixmap")},
      {QStringLiteral("pnm"), QStringLiteral("image/x-portable-anymap")},
      {QStringLiteral("svg"), QStringLiteral("image/svg+xml")},
      {QStringLiteral("svgz"), QStringLiteral("image/svg+xml-compressed")},
      {QStringLiteral("dng"), QStringLiteral("image/x-adobe-dng")},
      {QStringLiteral("cr2"), QStringLiteral("image/x-canon-cr2")},
      {QStringLiteral("cr3"), QStringLiteral("image/x-canon-cr3")},
      {QStringLiteral("crw"), QStringLiteral("image/x-canon-crw")},
      {QStringLiteral("nef"), QStringLiteral("image/x-nikon-nef")},
      {QStringLiteral("nrw"), QStringLiteral("image/x-nikon-nrw")},
      {QStringLiteral("arw"), QStringLiteral("image/x-sony-arw")},
      {QStringLiteral("sr2"), QStringLiteral("image/x-sony-sr2")},
      {QStringLiteral("srf"), QStringLiteral("image/x-sony-srf")},
      {QStringLiteral("orf"), QStringLiteral("image/x-olympus-orf")},
      {QStringLiteral("raf"), QStringLiteral("image/x-fuji-raf")},
      {QStringLiteral("rw2"), QStringLiteral("image/x-panasonic-rw2")},
      {QStringLiteral("pef"), QStringLiteral("image/x-pentax-pef")},
      {QStringLiteral("srw"), QStringLiteral("image/x-samsung-srw")},
      {QStringLiteral("mrw"), QStringLiteral("image/x-minolta-mrw")},
      {QStringLiteral("3fr"), QStringLiteral("image/x-hasselblad-3fr")},
      {QStringLiteral("fff"), QStringLiteral("image/x-hasselblad-fff")},
      {QStringLiteral("iiq"), QStringLiteral("image/x-phaseone-iiq")},
      {QStringLiteral("erf"), QStringLiteral("image/x-epson-erf")},
      {QStringLiteral("kdc"), QStringLiteral("image/x-kodak-kdc")},
      {QStringLiteral("dcr"), QStringLiteral("image/x-kodak-dcr")},
      {QStringLiteral("mos"), QStringLiteral("image/x-leaf-mos")},
      {QStringLiteral("rwl"), QStringLiteral("image/x-panasonic-rw2")},
      {QStringLiteral("x3f"), QStringLiteral("image/x-sigma-x3f")},
      {QStringLiteral("af"), {}},
      {QStringLiteral("afphoto"), {}},
      {QStringLiteral("afdesign"), {}},
      {QStringLiteral("afpub"), {}},
      {QStringLiteral("pdn"), {}},
      {QStringLiteral("xcf"), QStringLiteral("image/x-xcf")},
      {QStringLiteral("xcf.gz"), QStringLiteral("image/x-compressed-xcf")},
      {QStringLiteral("xcf.bz2"), QStringLiteral("image/x-compressed-xcf")},
      {QStringLiteral("xcf.xz"), {}},
      {QStringLiteral("xcfgz"), {}},
      {QStringLiteral("xcfbz2"), {}},
      {QStringLiteral("xcfxz"), {}},
      {QStringLiteral("kra"), QStringLiteral("application/x-krita")},
      {QStringLiteral("krz"), QStringLiteral("application/x-krita")},
      {QStringLiteral("heic"), QStringLiteral("image/heif")},
      {QStringLiteral("heif"), QStringLiteral("image/heif")},
      {QStringLiteral("hif"), QStringLiteral("image/heif")},
      {QStringLiteral("avif"), QStringLiteral("image/avif")},
      {QStringLiteral("avifs"), QStringLiteral("image/avif")},
      {QStringLiteral("jxl"), QStringLiteral("image/jxl")},
      {QStringLiteral("rttex"), {}},
      {QStringLiteral("dds"), QStringLiteral("image/vnd.ms-dds")},
      {QStringLiteral("hdr"), QStringLiteral("image/vnd.radiance")},
      {QStringLiteral("rgbe"), QStringLiteral("image/vnd.radiance")},
      {QStringLiteral("exr"), QStringLiteral("image/x-exr")},
      {QStringLiteral("jp2"), QStringLiteral("image/jp2")},
      {QStringLiteral("j2k"), QStringLiteral("image/x-jp2-codestream")},
      {QStringLiteral("j2c"), QStringLiteral("image/x-jp2-codestream")},
      {QStringLiteral("jpc"), QStringLiteral("image/x-jp2-codestream")},
      {QStringLiteral("jpx"), QStringLiteral("image/jpx")},
      {QStringLiteral("jpf"), QStringLiteral("image/jpx")},
  };
  return types;
}

// The extensions each system type identifier the bundle names stands for. macOS ignores
// CFBundleTypeExtensions in a document type that has LSItemContentTypes, so only these
// count for such an entry.
const QHash<QString, QStringList>& macos_type_identifier_extensions() {
  static const QHash<QString, QStringList> extensions = {
      {QStringLiteral("com.adobe.photoshop-image"), {QStringLiteral("psd")}},
      {QStringLiteral("public.heic"), {QStringLiteral("heic")}},
      {QStringLiteral("public.heif"), {QStringLiteral("heif")}},
      {QStringLiteral("public.png"), {QStringLiteral("png")}},
      {QStringLiteral("public.jpeg"), {QStringLiteral("jpg"), QStringLiteral("jpeg")}},
      {QStringLiteral("com.microsoft.bmp"), {QStringLiteral("bmp")}},
      {QStringLiteral("com.compuserve.gif"), {QStringLiteral("gif")}},
      {QStringLiteral("public.tiff"), {QStringLiteral("tif"), QStringLiteral("tiff")}},
      {QStringLiteral("org.webmproject.webp"), {QStringLiteral("webp")}},
      {QStringLiteral("public.jpeg-xl"), {QStringLiteral("jxl")}},
  };
  return extensions;
}

QStringList read_plist_string_array(QXmlStreamReader& xml) {
  QStringList values;
  while (xml.readNextStartElement()) {
    if (xml.name() == QStringLiteral("string")) {
      values.push_back(xml.readElementText());
    } else {
      xml.skipCurrentElement();
    }
  }
  return values;
}

// The extensions macOS hands to Patchy: every CFBundleDocumentTypes entry's type
// identifiers through the table above, or its extension list when it has none.
QSet<QString> macos_document_type_extensions(QStringList& unknown_identifiers) {
  QXmlStreamReader xml(read_source_text("packaging/macos/Info.plist.in"));
  QString last_key;
  bool found_document_types = false;
  while (!xml.atEnd() && !found_document_types) {
    xml.readNext();
    if (xml.isStartElement() && xml.name() == QStringLiteral("key")) {
      last_key = xml.readElementText();
    } else if (xml.isStartElement() && xml.name() == QStringLiteral("array")) {
      found_document_types = last_key == QStringLiteral("CFBundleDocumentTypes");
    }
  }
  CHECK(found_document_types);
  QSet<QString> claimed;
  while (xml.readNextStartElement()) {
    if (xml.name() != QStringLiteral("dict")) {
      xml.skipCurrentElement();
      continue;
    }
    QStringList identifiers;
    QStringList extensions;
    while (xml.readNextStartElement()) {
      if (xml.name() == QStringLiteral("key")) {
        last_key = xml.readElementText();
      } else if (xml.name() == QStringLiteral("array") && last_key == QStringLiteral("LSItemContentTypes")) {
        identifiers = read_plist_string_array(xml);
      } else if (xml.name() == QStringLiteral("array") && last_key == QStringLiteral("CFBundleTypeExtensions")) {
        extensions = read_plist_string_array(xml);
      } else {
        xml.skipCurrentElement();
      }
    }
    if (identifiers.isEmpty()) {
      claimed.unite(QSet<QString>(extensions.begin(), extensions.end()));
      continue;
    }
    for (const auto& identifier : identifiers) {
      const auto found = macos_type_identifier_extensions().constFind(identifier);
      if (found == macos_type_identifier_extensions().cend()) {
        unknown_identifiers.push_back(identifier);
        continue;
      }
      claimed.unite(QSet<QString>(found->begin(), found->end()));
    }
  }
  CHECK(!xml.hasError());
  return claimed;
}

void open_with_lists_claim_every_openable_extension() {
  const auto openable = MainWindow::openable_file_extensions();
  CHECK(openable.contains(QStringLiteral("psd")) && openable.contains(QStringLiteral("kra")));

  // Windows and macOS associate by the last extension only, so .xcf.gz stays unclaimed.
  const auto windows = windows_open_with_extensions();
  QStringList windows_missing;
  for (const auto& extension : openable) {
    if (!claimed_nowhere(extension) && !extension.contains(QLatin1Char('.')) && !windows.contains(extension)) {
      windows_missing.push_back(extension);
    }
  }
  report_missing("InstallPatchy.ps1 $PatchyOpenWithExtensions", windows_missing);
  CHECK(windows_missing.isEmpty());

  const auto desktop = desktop_mime_types();
  QStringList linux_missing;
  QStringList linux_unmapped;
  QStringList linux_claimed_anyway;
  for (const auto& extension : openable) {
    if (claimed_nowhere(extension) || windows_only(extension)) {
      continue;
    }
    const auto found = linux_mime_types().constFind(extension);
    if (found == linux_mime_types().cend()) {
      linux_unmapped.push_back(extension);
    } else if (left_to_their_own_editor_on_linux(extension)) {
      if (!found->isEmpty() && desktop.contains(*found)) {
        linux_claimed_anyway.push_back(extension + QStringLiteral(" (") + *found + QLatin1Char(')'));
      }
    } else if (!found->isEmpty() && !desktop.contains(*found)) {
      linux_missing.push_back(extension + QStringLiteral(" (") + *found + QLatin1Char(')'));
    }
  }
  report_missing("this test's linux_mime_types table", linux_unmapped);
  report_missing("com.rtsoft.patchy.desktop MimeType", linux_missing);
  if (!linux_claimed_anyway.isEmpty()) {
    std::cerr << "com.rtsoft.patchy.desktop MimeType must not list: "
              << linux_claimed_anyway.join(QStringLiteral(", ")).toStdString() << '\n';
  }
  CHECK(linux_unmapped.isEmpty());
  CHECK(linux_missing.isEmpty());
  CHECK(linux_claimed_anyway.isEmpty());

  QStringList unknown_identifiers;
  const auto macos = macos_document_type_extensions(unknown_identifiers);
  report_missing("this test's macos_type_identifier_extensions table", unknown_identifiers);
  CHECK(unknown_identifiers.isEmpty());
  QStringList macos_missing;
  for (const auto& extension : openable) {
    if (!claimed_nowhere(extension) && !windows_only(extension) && !extension.contains(QLatin1Char('.')) &&
        !macos.contains(extension)) {
      macos_missing.push_back(extension);
    }
  }
  report_missing("Info.plist.in CFBundleDocumentTypes", macos_missing);
  CHECK(macos_missing.isEmpty());
}

}  // namespace

std::vector<patchy::test::TestCase> open_with_registration_tests() {
  return {
      {"open_with_lists_claim_every_openable_extension", open_with_lists_claim_every_openable_extension},
  };
}
