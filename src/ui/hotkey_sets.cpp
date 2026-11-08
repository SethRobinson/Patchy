#include "ui/hotkey_sets.hpp"

#include "ui/app_settings.hpp"
#include "ui/hotkey_files.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QKeySequence>
#include <QStringList>
#include <algorithm>
#include <array>

namespace patchy::ui {

namespace {

// Photoshop-style: Adobe's published default shortcut tables (the "Default keyboard shortcuts"
// help page and its PDF) and its Windows menu shortcut reference. Patchy's defaults already
// follow most of them; these are the differences. Tool group members keep Patchy's one letter
// per group (Shift+letter walks the group), and keys for commands Patchy lacks stay out.
constexpr std::array kPhotoshopEntries{
    HotkeySetEntry{"file.close_all", "Ctrl+Alt+W"},
    // New Layer without its dialog (GitHub issue 645). Photoshop has no Open from Clipboard.
    HotkeySetEntry{"layer.new", "Ctrl+Alt+Shift+N"},
    HotkeySetEntry{"file.open_clipboard", ""},
    // macOS: Cmd+, hides layers and Cmd+K opens Preferences, as on Windows (issue 645).
    HotkeySetEntry{"layer.toggle_visibility", "Ctrl+,"},
    HotkeySetEntry{"file.preferences", "Ctrl+K"},
    HotkeySetEntry{"edit.redo", "Ctrl+Shift+Z"},
    HotkeySetEntry{"edit.cut", "Ctrl+X\nF2"},
    HotkeySetEntry{"edit.copy", "Ctrl+C\nF3"},
    HotkeySetEntry{"edit.paste", "Ctrl+V\nF4"},
    HotkeySetEntry{"layer.rename", ""},
    HotkeySetEntry{"layer.merge_visible", "Ctrl+Shift+E"},
    HotkeySetEntry{"layer.merge_visible_in_place", "Ctrl+Alt+Shift+E"},
    HotkeySetEntry{"layer.move_up", "Ctrl+]"},
    HotkeySetEntry{"layer.move_down", "Ctrl+["},
    HotkeySetEntry{"image.rotate_cw", ""},
    HotkeySetEntry{"image.rotate_ccw", ""},
    HotkeySetEntry{"view.proof_colors", "Ctrl+Y"},
    HotkeySetEntry{"view.gamut_warning", "Ctrl+Shift+Y"},
    HotkeySetEntry{"window.brush_settings", "F5"},
    HotkeySetEntry{"window.force_refresh", ""},
    // Adobe's table: Ctrl+Page Up/Down scroll one screen left or right (issue 645).
    HotkeySetEntry{"view.scroll_page_left", "Ctrl+PgUp"},
    HotkeySetEntry{"view.scroll_page_right", "Ctrl+PgDown"},
};

// GIMP-style: the GIMP 3.0 User Manual (docs.gimp.org/3.0/en: the tool pages, the menu
// command pages and the Keys and Mouse Reference). GIMP's plain 1 (zoom 1:1) is a digit,
// which Patchy keeps for layer opacity, so it is left out. Patchy keys that GIMP's take over
// with no GIMP default of their own are cleared ("").
constexpr std::array kGimpEntries{
    // File and Edit
    HotkeySetEntry{"file.export_flat", "Ctrl+Shift+E"},
    HotkeySetEntry{"layer.merge_visible_in_place", ""},
    HotkeySetEntry{"file.import_files_as_layers", "Ctrl+Alt+O"},
    HotkeySetEntry{"edit.undo", "Ctrl+Z"},
    HotkeySetEntry{"edit.redo", "Ctrl+Y"},
    HotkeySetEntry{"edit.paste_in_place", "Ctrl+Alt+V"},
    HotkeySetEntry{"filter.perspective_planes", ""},
    HotkeySetEntry{"file.open_clipboard", "Ctrl+Shift+V"},
    HotkeySetEntry{"edit.free_transform", "Shift+T"},
    // GIMP has no Type Mask tool, and Shift+T is its Unified Transform.
    HotkeySetEntry{"tools.cycle.type", ""},
    HotkeySetEntry{"edit.transform_scale", "Shift+S"},
    HotkeySetEntry{"edit.transform_rotate", "Shift+R"},
    HotkeySetEntry{"edit.transform_perspective", "Shift+P"},
    // The Shear tool's Shift+H (issue 645); GIMP has no Hand tool group to cycle.
    HotkeySetEntry{"edit.transform_skew", "Shift+H"},
    HotkeySetEntry{"tools.cycle.hand", ""},
    HotkeySetEntry{"filter.liquify", "W"},
    HotkeySetEntry{"filter.last", "Ctrl+F"},
    HotkeySetEntry{"help.search_commands", "/"},
    HotkeySetEntry{"layer.lock_transparent_pixels", ""},
    // Layers
    HotkeySetEntry{"layer.duplicate", "Ctrl+Shift+D"},
    HotkeySetEntry{"select.reselect", ""},
    HotkeySetEntry{"layer.merge_visible", "Ctrl+M"},
    HotkeySetEntry{"image.curves", ""},
    // Ctrl+, is Preferences on macOS, so Fill with Foreground keeps Patchy's key there.
    HotkeySetEntry{"layer.fill", "Ctrl+,", "Alt+Backspace"},
    HotkeySetEntry{"layer.toggle_visibility", ""},
    HotkeySetEntry{"layer.fill_background", "Ctrl+."},
    HotkeySetEntry{"select.layer_above", "PgUp"},
    HotkeySetEntry{"select.layer_below", "PgDown"},
    HotkeySetEntry{"view.scroll_page_up", ""},
    HotkeySetEntry{"view.scroll_page_down", ""},
    // Select
    HotkeySetEntry{"select.deselect", "Ctrl+Shift+A"},
    HotkeySetEntry{"filter.camera_raw_filter", ""},
    HotkeySetEntry{"select.inverse", "Ctrl+I"},
    HotkeySetEntry{"patchy.filters.invert", ""},
    HotkeySetEntry{"select.quick_mask", "Shift+Q"},
    // View and windows
    HotkeySetEntry{"view.zoom_in", "+"},
    HotkeySetEntry{"view.zoom_out", "-"},
    HotkeySetEntry{"view.fit_on_screen", "Ctrl+Shift+J"},
    HotkeySetEntry{"layer.via_cut", ""},
    // View > Show Selection shows or hides the marching ants alone (issue 645); Extras keeps Ctrl+H.
    HotkeySetEntry{"view.show_selection_edges", "Ctrl+T"},
    HotkeySetEntry{"view.rulers", "Ctrl+Shift+R"},
    HotkeySetEntry{"view.guides", "Ctrl+Shift+T"},
    HotkeySetEntry{"view.screen_mode_full", "F11"},
    HotkeySetEntry{"view.screen_mode_cycle", ""},
    HotkeySetEntry{"window.panel_layers", "Ctrl+L"},
    HotkeySetEntry{"image.levels", ""},
    HotkeySetEntry{"window.brush_tips", "Ctrl+Shift+B"},
    HotkeySetEntry{"patchy.filters.auto_color", ""},
    // Tools. GIMP has no Hand tool (Space pans) and no Shape tools, so H and U go to Heal and
    // Fuzzy Select; Shift+letter tool keys replace the group cycles that used them.
    HotkeySetEntry{"tools.move", "M"},
    HotkeySetEntry{"tools.marquee", "R"},
    // GIMP rotates the view with Shift+middle drag, not a tool key: R goes to Rectangle Select.
    HotkeySetEntry{"tools.rotate_view", ""},
    HotkeySetEntry{"tools.elliptical_marquee", "E"},
    HotkeySetEntry{"tools.lasso", "F"},
    HotkeySetEntry{"tools.magnetic_lasso", "I"},
    HotkeySetEntry{"tools.magic_wand", "U"},
    HotkeySetEntry{"tools.crop", "Shift+C"},
    HotkeySetEntry{"tools.eyedropper", "O"},
    HotkeySetEntry{"tools.ruler", "Shift+M"},
    HotkeySetEntry{"tools.healing", "H"},
    HotkeySetEntry{"tools.brush", "P"},
    HotkeySetEntry{"tools.eraser", "Shift+E"},
    HotkeySetEntry{"tools.clone", "C"},
    HotkeySetEntry{"tools.fill", "Shift+B"},
    HotkeySetEntry{"tools.smudge", "S"},
    HotkeySetEntry{"tools.blur", "Shift+U"},
    HotkeySetEntry{"tools.dodge", "Shift+D"},
    HotkeySetEntry{"tools.pen", "B"},
    HotkeySetEntry{"tools.hand", ""},
    HotkeySetEntry{"tools.rect", ""},
    HotkeySetEntry{"tools.cycle.crop", ""},
    HotkeySetEntry{"tools.cycle.marquee", ""},
    HotkeySetEntry{"tools.cycle.shape", ""},
    HotkeySetEntry{"tools.cycle.stamp", ""},
    HotkeySetEntry{"tools.cycle.detail", ""},
    HotkeySetEntry{"tools.cycle.pen", ""},
};

// Krita-style: Krita's own default scheme (not its Photoshop-compatible one), from the Krita
// Manual 5.3 (docs.krita.org/en: the tool pages and the main menu pages). Krita's plain 1 and 2
// zoom keys are digits, which Patchy keeps for layer opacity, so they are left out.
constexpr std::array kKritaEntries{
    HotkeySetEntry{"edit.undo", "Ctrl+Z"},
    HotkeySetEntry{"edit.redo", "Ctrl+Shift+Z"},
    HotkeySetEntry{"edit.cut", "Ctrl+X\nShift+Del"},
    HotkeySetEntry{"edit.copy", "Ctrl+C\nCtrl+Ins"},
    HotkeySetEntry{"edit.paste", "Ctrl+V\nShift+Ins"},
    HotkeySetEntry{"image.flatten", "Ctrl+Shift+E"},
    HotkeySetEntry{"layer.merge_visible_in_place", ""},
    HotkeySetEntry{"layer.new", "Ins"},
    HotkeySetEntry{"layer.duplicate", "Ctrl+J"},
    HotkeySetEntry{"layer.via_copy", "Ctrl+Alt+J"},
    HotkeySetEntry{"layer.ungroup", "Ctrl+Alt+G"},
    HotkeySetEntry{"layer.toggle_clipping_mask", "Ctrl+Shift+G"},
    HotkeySetEntry{"layer.move_up", "Ctrl+PgUp"},
    HotkeySetEntry{"layer.move_down", "Ctrl+PgDown"},
    // Issue 645: Page Up and Page Down activate the next layer up or down, Edit > Paste into New
    // Image is Ctrl+Shift+N (new layers are Ins), View > Soft Proofing and Out of Gamut Warnings
    // are Ctrl+Y and Ctrl+Shift+Y, Show Grid is Ctrl+Shift+', Ctrl+[ and Ctrl+] rotate the
    // canvas, Select > Display Selection is Ctrl+H, Shift+I shows the color selector and V is
    // the Straight Line tool.
    HotkeySetEntry{"select.layer_above", "PgUp"},
    HotkeySetEntry{"select.layer_below", "PgDown"},
    HotkeySetEntry{"view.scroll_page_up", ""},
    HotkeySetEntry{"view.scroll_page_down", ""},
    HotkeySetEntry{"file.open_clipboard", "Ctrl+Shift+N"},
    HotkeySetEntry{"layer.new_with_dialog", ""},
    HotkeySetEntry{"view.proof_colors", "Ctrl+Y"},
    HotkeySetEntry{"view.gamut_warning", "Ctrl+Shift+Y"},
    HotkeySetEntry{"view.grid", "Ctrl+Shift+'"},
    HotkeySetEntry{"layer.quick_export_png", ""},
    HotkeySetEntry{"view.rotate_left", "Ctrl+["},
    HotkeySetEntry{"view.rotate_right", "Ctrl+]"},
    HotkeySetEntry{"image.rotate_ccw", ""},
    HotkeySetEntry{"image.rotate_cw", ""},
    HotkeySetEntry{"view.show_selection_edges", "Ctrl+H"},
    HotkeySetEntry{"view.selection_edges", ""},
    HotkeySetEntry{"tools.line", "V"},
    HotkeySetEntry{"layer.fill", "Shift+Backspace"},
    HotkeySetEntry{"edit.fill", "Shift+F5"},
    HotkeySetEntry{"select.deselect", "Ctrl+Shift+A"},
    HotkeySetEntry{"filter.camera_raw_filter", ""},
    HotkeySetEntry{"view.zoom_in", "+"},
    HotkeySetEntry{"view.zoom_out", "-"},
    HotkeySetEntry{"view.screen_mode_full", "Ctrl+Shift+F"},
    HotkeySetEntry{"window.brush_settings", "F5"},
    HotkeySetEntry{"window.force_refresh", ""},
    HotkeySetEntry{"window.brush_tips", "F6"},
    HotkeySetEntry{"window.panel_color", "Shift+I"},
    HotkeySetEntry{"tools.cycle.pick", ""},
    HotkeySetEntry{"help.search_commands", "Ctrl+Return\nCtrl+F\nCtrl+Shift+/"},
    HotkeySetEntry{"tools.move", "T"},
    HotkeySetEntry{"tools.type", ""},
    HotkeySetEntry{"tools.marquee", "Ctrl+R"},
    HotkeySetEntry{"view.rulers", ""},
    HotkeySetEntry{"tools.elliptical_marquee", "J"},
    HotkeySetEntry{"tools.healing", ""},
};

constexpr std::array kHotkeySets{
    HotkeySet{"photoshop", QT_TRANSLATE_NOOP("patchy::ui::HotkeyEditorPanel", "Photoshop-style"), kPhotoshopEntries},
    HotkeySet{"gimp", QT_TRANSLATE_NOOP("patchy::ui::HotkeyEditorPanel", "GIMP-style"), kGimpEntries},
    HotkeySet{"krita", QT_TRANSLATE_NOOP("patchy::ui::HotkeyEditorPanel", "Krita-style"), kKritaEntries},
};

QList<QKeySequence> entry_shortcuts(const HotkeySetEntry& entry) {
#ifdef Q_OS_MACOS
  const char* source = entry.mac_keys != nullptr ? entry.mac_keys : entry.keys;
#else
  const char* source = entry.keys;
#endif
  return hotkey_shortcuts_from_storage(QString::fromLatin1(source));
}

const HotkeyCommand* find_command(const std::vector<HotkeyCommand>& commands, const QString& id) {
  const auto it = std::find_if(commands.begin(), commands.end(),
                               [&id](const HotkeyCommand& command) { return command.id == id; });
  return it != commands.end() ? &*it : nullptr;
}

// Settings keys: persisted identifiers (docs/hotkey-sets.md "Your own sets"). A group of its
// own, never under "hotkeys", whose every child key the registry reads as a command id.
const QString kSetsGroup = QStringLiteral("hotkeySets");
const QString kActiveKey = QStringLiteral("hotkeySets/active");
const QString kToolModifiersKey = QStringLiteral("hotkeySets/toolModifiers");
const QString kLastFolderKey = QStringLiteral("hotkeySets/lastFolder");
const QString kUserArray = QStringLiteral("user");

}  // namespace

bool hotkey_override_maps_equivalent(const HotkeyOverrideMap& a, const HotkeyOverrideMap& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (auto it = a.constBegin(); it != a.constEnd(); ++it) {
    const auto other = b.constFind(it.key());
    if (other == b.constEnd() || !hotkey_shortcut_lists_equivalent(it.value(), *other)) {
      return false;
    }
  }
  return true;
}

std::span<const HotkeySet> builtin_hotkey_sets() {
  return kHotkeySets;
}

const HotkeySet* find_builtin_hotkey_set(QStringView id) {
  for (const auto& set : kHotkeySets) {
    if (id == QLatin1String(set.id)) {
      return &set;
    }
  }
  return nullptr;
}

HotkeyOverrideMap hotkey_set_overrides(const HotkeySet& set, const std::vector<HotkeyCommand>& commands) {
  HotkeyOverrideMap overrides;
  for (const auto& entry : set.entries) {
    const auto id = QString::fromLatin1(entry.id);
    const auto* command = find_command(commands, id);
    if (command == nullptr) {
      continue;
    }
    auto shortcuts = entry_shortcuts(entry);
    if (hotkey_shortcut_lists_equivalent(shortcuts, command->default_shortcuts)) {
      continue;
    }
    overrides.insert(id, std::move(shortcuts));
  }
  return overrides;
}

QString matching_hotkey_set_id(const HotkeyOverrideMap& overrides, const std::vector<HotkeyCommand>& commands) {
  if (overrides.isEmpty()) {
    return QString::fromLatin1(kPatchyHotkeySetId);
  }
  for (const auto& set : kHotkeySets) {
    if (hotkey_override_maps_equivalent(overrides, hotkey_set_overrides(set, commands))) {
      return QString::fromLatin1(set.id);
    }
  }
  return {};
}

std::vector<UserHotkeySet> load_user_hotkey_sets(const std::vector<HotkeyCommand>& commands) {
  std::vector<UserHotkeySet> sets;
  auto settings = app_settings();
  settings.beginGroup(kSetsGroup);
  const int count = settings.beginReadArray(kUserArray);
  for (int index = 0; index < count; ++index) {
    settings.setArrayIndex(index);
    const auto id = settings.value(QStringLiteral("id")).toString();
    const auto stored = settings.value(QStringLiteral("shortcuts")).toString().toUtf8();
    auto read = hotkey_overrides_from_json(stored, commands);
    const bool duplicate = std::any_of(sets.begin(), sets.end(), [&id](const UserHotkeySet& set) { return set.id == id; });
    if (id.isEmpty() || duplicate || !read.ok) {
      continue;
    }
    sets.push_back({id, settings.value(QStringLiteral("name")).toString(), std::move(read.overrides),
                    tool_modifier_style_from_token(settings.value(QStringLiteral("toolModifiers")).toString())});
  }
  settings.endArray();
  settings.endGroup();
  return sets;
}

QString load_active_hotkey_set_id() {
  return app_settings().value(kActiveKey).toString();
}

void store_user_hotkey_sets(const std::vector<UserHotkeySet>& sets, const QString& active_id) {
  auto settings = app_settings();
  const auto last_folder = settings.value(kLastFolderKey);
  settings.remove(kSetsGroup);
  settings.beginGroup(kSetsGroup);
  settings.beginWriteArray(kUserArray, static_cast<int>(sets.size()));
  for (int index = 0; index < static_cast<int>(sets.size()); ++index) {
    const auto& set = sets[static_cast<std::size_t>(index)];
    settings.setArrayIndex(index);
    settings.setValue(QStringLiteral("id"), set.id);
    settings.setValue(QStringLiteral("name"), set.name);
    settings.setValue(QStringLiteral("shortcuts"), QString::fromUtf8(hotkey_overrides_to_json(set.overrides)));
    settings.setValue(QStringLiteral("toolModifiers"), tool_modifier_style_token(set.tool_modifiers));
  }
  settings.endArray();
  settings.endGroup();
  settings.setValue(kActiveKey, active_id);
  const auto active_user_set =
      std::find_if(sets.begin(), sets.end(), [&active_id](const UserHotkeySet& set) { return set.id == active_id; });
  const auto active_style = active_user_set != sets.end() ? active_user_set->tool_modifiers
                                                          : builtin_set_tool_modifier_style(active_id);
  settings.setValue(kToolModifiersKey, tool_modifier_style_token(active_style));
  if (last_folder.isValid()) {
    settings.setValue(kLastFolderKey, last_folder);
  }
  settings.sync();
}

QString hotkey_file_folder() {
  const auto stored = app_settings().value(kLastFolderKey).toString();
  if (!stored.isEmpty() && QFileInfo(stored).isDir()) {
    return stored;
  }
  return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void remember_hotkey_file_folder(const QString& file_path) {
  auto settings = app_settings();
  settings.setValue(kLastFolderKey, QFileInfo(file_path).absolutePath());
  settings.sync();
}

}  // namespace patchy::ui
