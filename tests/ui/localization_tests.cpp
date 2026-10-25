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
// Translation catalog checks (docs/localization.md).
//
// - translation_template_is_current reruns lupdate with the manifest CMake wrote
//   (same binary, options and source list as the patchy_update_translations target)
//   and fails when translations/patchy_en.ts does not match the code, i.e. when a
//   string change landed without scripts\update-translations.ps1.
// - translation_catalogs_are_complete requires every shipped language to carry the
//   template's exact string set with no unfinished entries and with placeholders,
//   modifier tokens, accelerators and trailing punctuation preserved.
// - translation_template_covers_runtime_sources walks the strings that reach the UI
//   outside lupdate's view (bound properties, tooltip details, preset tables) and
//   fails when one is missing from the template.
#include "test_harness.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "core/contour_presets.hpp"
#include "core/gradient_presets.hpp"
#include "core/palette_presets.hpp"
#include "core/pattern_presets.hpp"
#include "core/style_presets.hpp"
#include "ui/default_custom_shapes.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTranslator>
#include <QXmlStreamReader>
#include <cstdio>
#include <vector>

namespace {

using namespace patchy::test::ui;

struct CatalogMessage {
  QString context;
  QString source;
  QString comment;  // lupdate disambiguation comment
  bool numerus{false};
  QString type;  // translation "type" attribute: empty, unfinished, vanished, obsolete
  QStringList translations;  // one entry, or one per plural form
};

struct Catalog {
  QString language;
  QString source_language;
  std::vector<CatalogMessage> messages;
};

Catalog read_catalog(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    std::fprintf(stderr, "cannot open catalog %s\n", qPrintable(path));
    CHECK(false);
  }
  QXmlStreamReader xml(&file);
  Catalog catalog;
  QString context;
  CatalogMessage current;
  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isStartElement()) {
      const auto name = xml.name();
      if (name == u"TS") {
        catalog.language = xml.attributes().value(u"language").toString();
        catalog.source_language = xml.attributes().value(u"sourcelanguage").toString();
      } else if (name == u"name") {
        context = xml.readElementText();
      } else if (name == u"message") {
        current = CatalogMessage{};
        current.context = context;
        current.numerus = xml.attributes().value(u"numerus") == u"yes";
      } else if (name == u"source") {
        current.source = xml.readElementText();
      } else if (name == u"comment") {
        current.comment = xml.readElementText();
      } else if (name == u"translation") {
        current.type = xml.attributes().value(u"type").toString();
        if (current.numerus) {
          while (!xml.atEnd() && !(xml.isEndElement() && xml.name() == u"translation")) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == u"numerusform") {
              current.translations.push_back(xml.readElementText());
            }
          }
        } else {
          current.translations.push_back(xml.readElementText());
        }
      }
    } else if (xml.isEndElement() && xml.name() == u"message") {
      catalog.messages.push_back(current);
    }
  }
  if (xml.hasError()) {
    std::fprintf(stderr, "%s: %s\n", qPrintable(path), qPrintable(xml.errorString()));
    CHECK(false);
  }
  return catalog;
}

struct LupdateManifest {
  QString lupdate;
  QStringList options;
  QString include;
  QString template_path;
  QStringList languages;
  QStringList sources;
};

LupdateManifest read_manifest() {
  LupdateManifest manifest;
  QFile file(QStringLiteral(PATCHY_LUPDATE_MANIFEST));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    std::fprintf(stderr, "cannot open lupdate manifest %s\n", PATCHY_LUPDATE_MANIFEST);
    CHECK(false);
  }
  QTextStream in(&file);
  while (!in.atEnd()) {
    const auto line = in.readLine().trimmed();
    if (line.isEmpty()) {
      continue;
    }
    const auto take = [&line](const char* prefix, QString* target) {
      const auto key = QString::fromLatin1(prefix);
      if (!line.startsWith(key)) {
        return false;
      }
      *target = line.mid(key.size());
      return true;
    };
    QString value;
    if (take("lupdate=", &manifest.lupdate) || take("include=", &manifest.include) ||
        take("template=", &manifest.template_path)) {
      continue;
    }
    if (take("options=", &value)) {
      manifest.options = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    } else if (take("languages=", &value)) {
      manifest.languages = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    } else {
      manifest.sources.push_back(line);
    }
  }
  CHECK(!manifest.lupdate.isEmpty());
  CHECK(!manifest.template_path.isEmpty());
  CHECK(!manifest.languages.isEmpty());
  CHECK(!manifest.sources.isEmpty());
  return manifest;
}

void report(const QString& heading, const QStringList& lines) {
  if (lines.isEmpty()) {
    return;
  }
  std::fprintf(stderr, "%s (%lld):\n", qPrintable(heading), static_cast<long long>(lines.size()));
  const auto shown = std::min<qsizetype>(lines.size(), 60);
  for (qsizetype index = 0; index < shown; ++index) {
    std::fprintf(stderr, "  %s\n", qPrintable(lines[index]));
  }
  if (shown < lines.size()) {
    std::fprintf(stderr, "  ... and %lld more\n", static_cast<long long>(lines.size() - shown));
  }
}

void run_catalog_check(const QString& mode) {
  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(QStringLiteral(PATCHY_PYTHON_EXECUTABLE),
                {QStringLiteral(PATCHY_SOURCE_DIR "/scripts/check-translations.py"),
                 QStringLiteral("--manifest"), QStringLiteral(PATCHY_LUPDATE_MANIFEST),
                 QStringLiteral("--check"), mode});
  CHECK(process.waitForStarted(30000));
  CHECK(process.waitForFinished(360000));
  const auto output = process.readAll();
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    std::fprintf(stderr, "%s", output.constData());
    CHECK(false);
  }
}

void ui_translation_template_is_current() {
  run_catalog_check(QStringLiteral("template"));
}

void ui_translation_catalogs_are_complete() {
  run_catalog_check(QStringLiteral("catalogs"));
}

void ui_translation_compiled_catalogs_match_sources() {
  const auto manifest = read_manifest();
  QStringList languages;
  for (const auto& language : patchy::ui::LocalizationManager::instance().languages()) {
    if (language.code != QStringLiteral("en")) languages << language.code;
  }
  CHECK(languages == manifest.languages);
  const QDir runtime(QCoreApplication::applicationDirPath() + QStringLiteral("/translations"));
  const auto source = QFileInfo(manifest.template_path).dir();
  QStringList failures;
  for (const auto& language : languages) {
    QTranslator app_catalog;
    QTranslator qt_catalog;
    CHECK(app_catalog.load(runtime.filePath(QStringLiteral("patchy_%1.qm").arg(language))));
    CHECK(qt_catalog.load(runtime.filePath(QStringLiteral("qtbase_%1.qm").arg(language))));
    const auto catalog = read_catalog(source.filePath(QStringLiteral("patchy_%1.ts").arg(language)));
    for (const auto& message : catalog.messages) {
      for (const int count : {0, 1, 2, 5, 11, 12, 21, 22, 25, 101}) {
        const auto translated = app_catalog.translate(message.context.toUtf8().constData(),
            message.source.toUtf8().constData(), message.comment.toUtf8().constData(), message.numerus ? count : -1);
        if (translated.isEmpty() || !message.translations.contains(translated)) {
          failures << QStringLiteral("%1: [%2] %3: compiled translation missing or stale")
                          .arg(language, message.context, message.source);
          break;
        }
        if (message.numerus && (language == QStringLiteral("ru") || language == QStringLiteral("pl"))) {
          CHECK(message.translations.size() == 3);
          // Pin Qt's compiled choice at the singular/few/many boundaries. Polish
          // uses many for 21 and 101, where Russian returns to singular.
          const bool singular = count == 1 ||
              (language == QStringLiteral("ru") && (count == 21 || count == 101));
          const int form = singular ? 0 : (count == 2 || count == 22) ? 1 : 2;
          CHECK(translated == message.translations[form]);
        }
        if (message.numerus && language == QStringLiteral("pt_BR")) {
          CHECK(message.translations.size() == 2);
          CHECK(translated == message.translations[count <= 1 ? 0 : 1]);
        }
        if (message.numerus && language == QStringLiteral("ko")) {
          CHECK(message.translations.size() == 1);
          CHECK(translated == message.translations.front());
        }
        if (!message.numerus) break;
      }
    }
  }
  report(QStringLiteral("Compiled translation problems"), failures);
  CHECK(failures.isEmpty());
}

void ui_translation_template_covers_runtime_sources() {
  const auto manifest = read_manifest();
  const auto template_catalog = read_catalog(manifest.template_path);
  QSet<QString> known;
  for (const auto& message : template_catalog.messages) {
    known.insert(message.context + QLatin1Char('\x1f') + message.source);
  }
  QStringList missing;
  const auto check = [&](const QString& context, const QString& source, const QString& where) {
    if (source.isEmpty()) {
      return;
    }
    if (!known.contains(context + QLatin1Char('\x1f') + source)) {
      missing << QStringLiteral("%1: [%2] %3").arg(where, context, source.left(90));
    }
  };

  patchy::ui::MainWindow window;
  show_window(window);
  const auto main_context = QString::fromLatin1(patchy::ui::kMainWindowTranslationContext);
  const auto visit = [&](const QObject* object) {
    auto context = object->property(patchy::ui::kTranslationContextProperty).toString();
    if (context.isEmpty()) {
      context = main_context;
    }
    const auto where = object->objectName().isEmpty() ? QString::fromLatin1(object->metaObject()->className())
                                                      : object->objectName();
    for (const auto* property : {patchy::ui::kTranslationTextProperty, patchy::ui::kTranslationToolTipProperty,
                                 patchy::ui::kTranslationStatusTipProperty}) {
      const auto value = object->property(property);
      if (value.isValid()) {
        check(context, value.toString(), where);
      }
    }
    const auto detail = object->property(patchy::ui::kActionTooltipDetailProperty);
    if (detail.isValid()) {
      check(main_context, detail.toString(), where + QStringLiteral(" tooltip detail"));
    }
  };
  visit(&window);
  const auto children = window.findChildren<QObject*>();
  for (const auto* child : children) {
    visit(child);
  }

  const auto data_context = QString::fromLatin1(patchy::ui::kDataTranslationContext);
  const auto check_data = [&](const char* text, const char* where) {
    check(data_context, QString::fromUtf8(text), QString::fromLatin1(where));
  };
  for (const auto& preset : patchy::builtin_pattern_presets()) {
    check_data(preset.english_name, "pattern preset");
  }
  for (const auto& preset : patchy::photo_pattern_presets()) {
    check_data(preset.english_name, "photo pattern preset");
  }
  for (const auto& preset : patchy::builtin_contour_presets()) {
    check_data(preset.english_name, "contour preset");
  }
  for (const auto& preset : patchy::builtin_style_presets()) {
    check_data(preset.english_name, "style preset");
    check_data(preset.english_folder, "style preset folder");
  }
  for (const auto& preset : patchy::builtin_gradient_presets()) {
    check_data(preset.english_name, "gradient preset");
    check_data(preset.english_folder, "gradient preset folder");
  }
  for (const auto& preset : patchy::builtin_palette_presets()) {
    check_data(preset.english_name, "palette preset");
  }
  for (const auto& shape : patchy::ui::builtin_custom_shapes()) {
    check_data(shape.english_name, "custom shape");
    check_data(shape.english_folder, "custom shape folder");
  }

  report(QStringLiteral("Runtime-translated strings missing from translations/patchy_en.ts"), missing);
  CHECK(missing.isEmpty());
}

}  // namespace

std::vector<patchy::test::TestCase> translation_runtime_tests();

std::vector<patchy::test::TestCase> localization_tests() {
  auto tests = translation_runtime_tests();
  const std::vector<patchy::test::TestCase> catalogs = {
      {"ui_translation_template_is_current", ui_translation_template_is_current},
      {"ui_translation_catalogs_are_complete", ui_translation_catalogs_are_complete},
      {"ui_translation_compiled_catalogs_match_sources", ui_translation_compiled_catalogs_match_sources},
      {"ui_translation_template_covers_runtime_sources", ui_translation_template_covers_runtime_sources},
  };
  tests.insert(tests.end(), catalogs.begin(), catalogs.end());
  return tests;
}
