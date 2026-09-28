// Compare actual UI properties, including controls without translation metadata.
// Shared by the normal UI suite and the mandatory desktop build checker.
#include "test_harness.hpp"
#include "ui_test_support.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QGroupBox>
#include <QMap>
#include <QScopeGuard>

namespace {
using namespace patchy::test::ui;
using Snapshot = QMap<QString, QString>;

void settle_translations() {
  for (int pass = 0; pass < 3; ++pass) {
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
}

void collect_text(const QObject* object, const QString& path, Snapshot& result) {
  // Item-view delegates contain user layer names, paths, and history entries.
  // Combo-box item text is application chrome and is collected explicitly below.
  if (qobject_cast<const QAbstractItemView*>(object) != nullptr) return;
  const auto add = [&](const char* property, const QString& text) {
    result.insert(path + QLatin1Char('/') + QString::fromLatin1(property), text);
  };
  if (const auto* action = qobject_cast<const QAction*>(object)) {
    if (!action->isSeparator()) {
      add("text", action->text());
      add("toolTip", action->toolTip());
      add("statusTip", action->statusTip());
    }
  }
  if (const auto* widget = qobject_cast<const QWidget*>(object)) {
    add("toolTip", widget->toolTip());
    add("statusTip", widget->statusTip());
  }
  if (const auto* label = qobject_cast<const QLabel*>(object)) add("text", label->text());
  if (const auto* button = qobject_cast<const QAbstractButton*>(object)) add("text", button->text());
  if (const auto* menu = qobject_cast<const QMenu*>(object)) add("title", menu->title());
  if (const auto* dock = qobject_cast<const QDockWidget*>(object)) add("title", dock->windowTitle());
  if (const auto* bar = qobject_cast<const QToolBar*>(object)) add("title", bar->windowTitle());
  if (const auto* group = qobject_cast<const QGroupBox*>(object)) add("title", group->title());
  if (const auto* edit = qobject_cast<const QLineEdit*>(object)) add("placeholder", edit->placeholderText());
  // Family names belong to the font database, which can discover additional
  // platform fonts lazily. They are not translated application strings.
  if (const auto* combo = qobject_cast<const QComboBox*>(object);
      combo != nullptr && qobject_cast<const QFontComboBox*>(object) == nullptr) {
    for (int index = 0; index < combo->count(); ++index) {
      result.insert(path + QStringLiteral("/item[%1]").arg(index), combo->itemText(index));
    }
  }
  if (const auto* spin = qobject_cast<const QSpinBox*>(object)) {
    add("prefix", spin->prefix());
    add("suffix", spin->suffix());
    add("specialValueText", spin->specialValueText());
  }
  if (const auto* spin = qobject_cast<const QDoubleSpinBox*>(object)) {
    add("prefix", spin->prefix());
    add("suffix", spin->suffix());
    add("specialValueText", spin->specialValueText());
  }
  QHash<QString, int> ordinals;
  for (const auto* child : object->children()) {
    // QMenu::clear detaches old recent-file submenus without deleting them.
    // Only menus still attached to their parent's action list reach the UI.
    if (const auto* parent_menu = qobject_cast<const QMenu*>(object)) {
      if (const auto* submenu = qobject_cast<const QMenu*>(child);
          submenu != nullptr && !parent_menu->actions().contains(submenu->menuAction())) continue;
    }
    const auto identity = QString::fromLatin1(child->metaObject()->className()) +
                          QLatin1Char(':') + child->objectName();
    const auto ordinal = ordinals[identity]++;
    collect_text(child, path + QLatin1Char('/') + identity + QStringLiteral("[%1]").arg(ordinal), result);
  }
}

Snapshot snapshot(const QObject& root) {
  Snapshot result;
  collect_text(&root, QStringLiteral("window"), result);
  return result;
}

void show_translation_window(patchy::ui::MainWindow& window, bool with_document) {
  // The plug-in menu's placeholder note changes identity when the startup scan
  // finishes ("Scanning plug-in folders..." becomes "No plug-ins found"), so a
  // window snapshotted mid-scan never matches one snapshotted after it. Wait
  // for the scan before either snapshot.
  if (!with_document) {
    show_window_empty(window);
    wait_for_legacy_plugin_scan(window);
    return;
  }
  show_window(window);
  wait_for_legacy_plugin_scan(window);
  // Layer names are document content. Use the same user-owned name in every
  // locale while still checking the translated property labels around it.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.active_layer_id().has_value());
  document.find_layer(*document.active_layer_id())->set_name("Translation fixture");
  patchy::ui::MainWindowTestAccess::refresh_layer_ui(window);
  patchy::ui::MainWindowTestAccess::refresh_document_info(window);
}

QStringList differences(const Snapshot& expected, const Snapshot& actual, const QString& language) {
  QStringList failures;
  for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
    const auto found = actual.constFind(it.key());
    if (found == actual.cend() || *found != it.value()) {
      failures << QStringLiteral("%1: %2: expected '%3', got '%4'")
                      .arg(language, it.key(), it.value(), found == actual.cend() ? QStringLiteral("<missing>") : *found);
    }
  }
  return failures;
}

void ui_translation_language_switch_matches_fresh_windows() {
  auto& manager = patchy::ui::LocalizationManager::instance();
  const auto original = manager.current_language();
  const auto restore = qScopeGuard([&] { manager.set_language(original, false); settle_translations(); });
  QStringList failures;
  for (const bool with_document : {false, true}) {
    QMap<QString, Snapshot> fresh;
    for (const auto& language : manager.languages()) {
      CHECK(manager.set_language(language.code, false));
      patchy::ui::MainWindow window;
      show_translation_window(window, with_document);
      // Run the same refresh path for generated tooltips and dynamic menus. This
      // leaves an unbound control in its correctly translated startup language.
      QEvent event(QEvent::LanguageChange);
      QCoreApplication::sendEvent(&window, &event);
      settle_translations();
      fresh.insert(language.code, snapshot(window));
    }
    CHECK(manager.set_language(QStringLiteral("en"), false));
    patchy::ui::MainWindow live;
    show_translation_window(live, with_document);
    QEvent initial_event(QEvent::LanguageChange);
    QCoreApplication::sendEvent(&live, &initial_event);
    settle_translations();
    const auto check = [&](const QString& language) {
      CHECK(manager.set_language(language, false));
      settle_translations();
      failures += differences(fresh.value(language), snapshot(live),
                              language + (with_document ? QStringLiteral(" (document)") : QStringLiteral(" (empty)")));
    };
    for (const auto& language : manager.languages()) {
      check(language.code);
      check(QStringLiteral("en"));
    }
    // Direct transitions exercise translations retained from the previous locale.
    for (const auto& language : manager.languages()) check(language.code);
  }
  failures.removeDuplicates();
  for (const auto& failure : failures) std::fprintf(stderr, "%s\n", failure.toUtf8().constData());
  CHECK(failures.isEmpty());
}

void ui_translation_snapshot_detects_unbound_controls() {
  QWidget widget;
  QLabel label(QStringLiteral("English label"), &widget);
  QAction action(QStringLiteral("English action"), &widget);
  const auto untranslated = snapshot(widget);
  label.setText(QStringLiteral("Translated label"));
  action.setText(QStringLiteral("Translated action"));
  const auto expected = snapshot(widget);
  CHECK(differences(expected, untranslated, QStringLiteral("fixture")).size() >= 2);
  CHECK(differences(expected, snapshot(widget), QStringLiteral("fixture")).isEmpty());
}
}  // namespace

std::vector<patchy::test::TestCase> translation_runtime_tests() {
  return {
      {"ui_translation_snapshot_detects_unbound_controls", ui_translation_snapshot_detects_unbound_controls},
      {"ui_translation_language_switch_matches_fresh_windows", ui_translation_language_switch_matches_fresh_windows},
  };
}
