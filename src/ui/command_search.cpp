#include "ui/command_search.hpp"

#include "ui/action_icons.hpp"
#include "ui/hotkey_registry.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHash>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <memory>
#include <utility>

namespace patchy::ui {

namespace {

constexpr int kPopupWidth = 680;
constexpr int kVisibleRows = 12;
constexpr int kRowPadding = 6;
constexpr int kCheckColumn = 22;
constexpr int kButtonWidth = 30;
constexpr int kButtonGap = 2;

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kShortcutRole = Qt::UserRole + 2;
constexpr int kCheckRole = Qt::UserRole + 3;  // -1 not checkable, 0 off, 1 on
constexpr int kEntryRole = Qt::UserRole + 4;
constexpr int kDescriptionRole = Qt::UserRole + 5;
constexpr int kKindRole = Qt::UserRole + 6;  // an int CommandSearchKind
constexpr int kIconGap = 6;

const QString kPathSeparator = QStringLiteral(" › ");
const QString kLabelSeparator = QStringLiteral(": ");

// The rows built from data: "Open Document: patchy.jpg", listed by show_command_search
// rather than from their menus.
bool is_data_kind(CommandSearchKind kind) {
  return kind == CommandSearchKind::open_document || kind == CommandSearchKind::recent_file ||
         kind == CommandSearchKind::recent_folder;
}

// The kind an action's kCommandSearchKindProperty names, else `fallback`.
CommandSearchKind action_kind(const QAction* action, CommandSearchKind fallback) {
  if (action == nullptr) {
    return fallback;
  }
  const auto value = action->property(kCommandSearchKindProperty);
  if (!value.isValid()) {
    return fallback;
  }
  const int kind = value.toInt();
  return kind >= 0 && kind <= static_cast<int>(CommandSearchKind::recent_folder) ? static_cast<CommandSearchKind>(kind)
                                                                                  : fallback;
}

struct CommandDescription {
  const char* id;  // the hotkey command id, else the action's objectName (both permanent)
  const char* text;
};

// Short "what it does" lines in other words than the name (GitHub issue 554): the search
// matches them, so a user who types "resize" or "add new layer" finds Image Size or New Layer.
// Every command the search lists has one here or a status tip that says more than its name;
// ui_command_search_every_command_has_a_description names any that lack one, and
// scripts/update-translations.ps1 reports them. Rows built from data are exempt
// (kCommandSearchDataEntryProperty). A new command gets its line in the same change.
constexpr CommandDescription kCommandDescriptions[] = {
    // File
    {"file.new", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Create a new blank image")},
    {"file.open", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Open an image file")},
    {"file.open_clipboard", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "New image from the clipboard")},
    {"file.open_folder", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Open every image in a folder")},
    {"file.save", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save the document")},
    {"file.save_as", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save to a new file or format")},
    {"file.revert", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Reload the saved file, dropping unsaved changes")},
    {"file.export_flat", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save a flattened copy")},
    {"file.export_for_web", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Optimize and save for the web")},
    {"file.export_animated_gif", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save the layers as a GIF animation")},
    {"file.export_sprite_sheet", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Combine the layers into one sheet")},
    {"file.place_embedded", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Insert an image as a Smart Object")},
    {"file.place_linked", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Insert a linked image file")},
    {"file.import_files_as_layers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Open several images as layers")},
    {"file.import_scanner", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Scan an image")},
    {"file.file_info", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Author, copyright and other metadata")},
    {"file.strip_metadata", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove EXIF and other metadata")},
    {"file.print", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Print the image")},
    {"file.close", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Close the document")},
    {"file.preferences", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Settings and options")},
    {"file.quit", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Exit Patchy")},
    {"fileClearRecentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Empty the list of recent files")},
    {"fileClearRecentFoldersAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Empty the list of recent folders")},
    {"file.import_photocopy", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Scan and print in one step")},
    {"file.import_divide_photos", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Scan several photos and split them apart")},
    {"file.import_sprite_sheet", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Cut a sprite sheet into layers")},
    {"file.import_image_sequence", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Load numbered images as layers")},
    {"file.export_multipage_pdf", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save several documents as one PDF")},
    {"file.export_documents_to_folder", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save every open document to a folder")},
    {"file.export_artboards", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save each artboard as its own file")},
    {"file.export_image_sequence", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save each layer as a numbered image")},
    {"file.export_animated_webp", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save the layers as a WebP animation")},
    {"file.export_animated_png", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save the layers as an APNG animation")},
    {"file.scan_for_watermarks", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Look for hidden watermarks in an image")},
    {"file.page_setup", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Choose paper size and orientation")},
    {"file.scripts.editor", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Run, edit and organize scripts")},
    {"file.scripts.browse_folder", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the scripts folder on disk")},
    {"file.close_all", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Close every open document")},
    // Edit
    {"edit.undo", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Take back the last change")},
    {"edit.redo", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Redo the change you took back")},
    {"edit.cut", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move the selection to the clipboard")},
    {"edit.copy", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Copy the selection to the clipboard")},
    {"edit.copy_merged", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Copy what all visible layers show")},
    {"edit.paste", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Paste from the clipboard")},
    {"edit.paste_in_place", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Paste at the same position")},
    {"edit.fill", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fill with a color or pattern")},
    {"edit.stroke_selection", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw an outline along the selection")},
    {"edit.free_transform", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Scale, rotate or move freely")},
    {"edit.transform_scale", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Resize the layer or selection")},
    {"edit.transform_rotate", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Turn the layer or selection")},
    {"edit.warp_transform", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Bend the layer with a grid")},
    {"edit.remove_object", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Erase an object and fill the gap")},
    {"edit.define_brush_tip", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make a brush from the selection")},
    {"edit.define_custom_shape", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make a custom shape from a path")},
    {"edit.convert_to_profile", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Change the color profile")},
    {"edit.presets_gradients", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Manage gradient presets")},
    {"edit.presets_patterns", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Manage pattern presets")},
    {"edit.transform_skew", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Slant the layer or selection")},
    {"edit.transform_distort", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Drag each corner freely")},
    {"edit.transform_perspective", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make one side wider than the other")},
    {"edit.define_custom_shape_svg", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make a custom shape from an SVG file")},
    {"edit.presets_styles", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Manage layer style presets")},
    {"edit.presets_brush_tips", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Manage brush tips and import ABR")},
    {"edit.presets_saved_brushes", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Manage saved brushes")},
    {"edit.presets_custom_shapes", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Manage custom shape presets")},
    {"edit.presets_stroke_presets", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Manage built-in and saved stroke presets")},
    // Image
    {"image.size", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Resize or scale the image")},
    {"image.canvas_size", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add or remove space around the image")},
    {"image.crop_to_selection", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Crop the image to the selection")},
    {"image.trim", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove empty borders")},
    {"image.levels", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Set the black point, white point and midtones")},
    {"image.curves", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Adjust the tones with a curve")},
    {"image.hue_saturation", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Change colors, saturation and lightness")},
    {"image.color_balance", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Shift the colors of shadows and highlights")},
    {"image.black_white", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Turn the image gray")},
    {"image.vibrance", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make muted colors stronger")},
    {"image.exposure", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Brighten or darken the image")},
    {"image.auto_all", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fix tone and color automatically")},
    {"image.mode_indexed", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Reduce the image to a color palette")},
    {"image.mode_16_bit", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Use 16 bits per channel")},
    {"image.mode_rgb", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Convert to full color")},
    {"image.mode_cmyk_proof", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Preview and save for print colors")},
    {"image.mode_8_bit", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Use 8 bits per channel")},
    {"image.mode_32_bit", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Use 32-bit HDR color")},
    {"image.snap_layer_to_palette", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Limit the layer to the palette colors")},
    {"image.snap_image_to_palette", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Limit every layer to the palette colors")},
    {"patchy.filters.brightness_contrast", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make the image lighter, darker or punchier")},
    {"image.photo_filter", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Warm or cool the colors like a lens filter")},
    {"image.channel_mixer", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Blend the red, green and blue channels")},
    {"image.color_lookup", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Apply a color grading table")},
    {"patchy.filters.invert", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make a negative")},
    {"patchy.filters.posterize", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Reduce the number of tones")},
    {"patchy.filters.threshold", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Turn the image black and white")},
    {"image.gradient_map", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Map the tones to a gradient")},
    {"image.selective_color", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Adjust one range of colors")},
    {"patchy.filters.desaturate", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove all color")},
    {"patchy.filters.auto_tone", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Stretch the tones automatically")},
    {"patchy.filters.auto_contrast", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Improve contrast automatically")},
    {"patchy.filters.auto_color", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove a color cast automatically")},
    // Layer
    {"layer.new_with_dialog", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add New Layer")},
    {"layer.new", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add an empty layer at once")},
    {"layer.duplicate", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Copy the layer")},
    {"layer.delete", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove the layer")},
    {"layer.rename", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Change the layer name")},
    {"layer.new_folder", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Put the selected layers in a folder")},
    {"layer.via_copy", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Copy the selection to a new layer")},
    {"layer.via_cut", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move the selection to a new layer")},
    {"layer.merge_down", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Combine with the layer below")},
    {"layer.merge_visible", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Combine all visible layers")},
    {"layer.add_mask", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add a layer mask")},
    {"layer.apply_mask", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Bake the mask into the pixels")},
    {"layer.styles", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Drop shadow, stroke, glow and other effects")},
    {"layer.toggle_clipping_mask", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Clip to the layer below")},
    {"layer.toggle_visibility", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show or hide layers")},
    {"layer.lock_layers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Protect layers from changes")},
    {"layer.move_up", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move the layer up the stack")},
    {"layer.move_down", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move the layer down the stack")},
    {"layer.fill", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fill with the foreground color")},
    {"layer.fill_background", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fill with the background color")},
    {"layer.clear", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Erase the layer or selection")},
    {"layer.new_artboard", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add an artboard")},
    {"layerNewBrightnessContrastAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable lighter, darker or punchier")},
    {"layerNewLevelsAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable black point, white point and midtones")},
    {"layerNewCurvesAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable tone curve")},
    {"layerNewHueSaturationAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable colors, saturation and lightness")},
    {"layerNewColorBalanceAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable color shift in shadows and highlights")},
    {"layerNewInvertAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable negative")},
    {"layerNewPosterizeAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable tone reduction")},
    {"layerNewThresholdAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable black and white cutoff")},
    {"layerNewExposureAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable brightening or darkening")},
    {"layerNewVibranceAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable boost for muted colors")},
    {"layerNewChannelMixerAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable channel blend")},
    {"layerNewPhotoFilterAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable warming or cooling filter")},
    {"layerNewGradientMapAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable mapping of tones to a gradient")},
    {"layerNewBlackWhiteAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable conversion to gray")},
    {"layerNewColorLookupAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable color grading table")},
    {"layerNewSelectiveColorAdjustmentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Editable change to one color range")},
    {"layerNewSolidColorFillAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add a layer filled with one color")},
    {"layerNewGradientFillAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add a layer filled with a gradient")},
    {"layerNewPatternFillAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add a layer filled with a pattern")},
    {"layer.color_none", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove the layer's color label")},
    {"layer.color_red", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Tag the layer red in the Layers panel")},
    {"layer.color_orange", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Tag the layer orange in the Layers panel")},
    {"layer.color_yellow", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Tag the layer yellow in the Layers panel")},
    {"layer.color_green", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Tag the layer green in the Layers panel")},
    {"layer.color_blue", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Tag the layer blue in the Layers panel")},
    {"layer.color_violet", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Tag the layer violet in the Layers panel")},
    {"layer.color_gray", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Tag the layer gray in the Layers panel")},
    {"layer.show_all_layers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make every layer visible")},
    {"layer.remove_background", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Hide the background with a mask automatically")},
    {"layer.edit_mask", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Paint on the mask instead of the layer")},
    {"layer.mask_overlay", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the mask as a red overlay")},
    {"layer.view_mask", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show only the mask in gray")},
    {"layerLinkMaskAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move the mask together with the layer")},
    {"layerDisableMaskAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Turn the mask off for now")},
    {"layer.invert_mask", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Swap what the mask shows and hides")},
    {"layer.delete_mask", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove the layer mask")},
    {"layerVectorMaskRevealAllAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add a vector mask that shows everything")},
    {"layerVectorMaskHideAllAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add a vector mask that hides everything")},
    {"layerVectorMaskCurrentPathAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Mask the layer with the selected path")},
    {"layerVectorMaskDeleteAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove the vector mask")},
    {"layerVectorMaskDisableAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Turn the vector mask off for now")},
    {"layerVectorMaskRasterizeAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Turn the vector mask into a pixel mask")},
    {"layer.edit_adjustment", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Change the settings of the adjustment layer")},
    {"layerConvertSmartObjectAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Wrap the layers so edits stay editable")},
    {"layerSmartObjectEditAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Open the contents in their own tab")},
    {"layerSmartObjectReplaceAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Swap in another file as the contents")},
    {"layerSmartObjectUpdateAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Reload the linked file")},
    {"layerSmartObjectRelinkAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Point the link at another file")},
    {"layerSmartObjectEmbedAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Store the linked file inside the document")},
    {"layerSmartObjectExportAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save the contents as a file")},
    {"layerSmartObjectViaCopyAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Duplicate as an independent Smart Object")},
    {"layerSmartObjectToNormalAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Rasterize the Smart Object into pixels")},
    {"layer.combine_unite", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Merge the shapes into one outline")},
    {"layer.combine_subtract", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Cut the top shape out of the others")},
    {"layer.combine_intersect", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Keep only where the shapes overlap")},
    {"layer.combine_exclude", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Keep everything except the overlap")},
    {"layer.align_left", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Line up layers on the left")},
    {"layer.align_horizontal_centers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Center layers side to side")},
    {"layer.align_right", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Line up layers on the right")},
    {"layer.align_top", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Line up layers along the top")},
    {"layer.align_vertical_centers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Center layers top to bottom")},
    {"layer.align_bottom", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Line up layers along the bottom")},
    {"layer.align_to_selection", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Align to the selected area")},
    {"layer.align_to_canvas", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Align to the document edges")},
    {"layer.distribute_left", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Space the left edges evenly")},
    {"layer.distribute_horizontal_centers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Space the centers evenly across")},
    {"layer.distribute_right", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Space the right edges evenly")},
    {"layer.distribute_top", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Space the top edges evenly")},
    {"layer.distribute_vertical_centers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Space the centers evenly down")},
    {"layer.distribute_bottom", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Space the bottom edges evenly")},
    {"layer.distribute_horizontal_spacing", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Equal gaps between layers across")},
    {"layer.distribute_vertical_spacing", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Equal gaps between layers down")},
    {"layer.flip_horizontal", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Mirror the layer left to right")},
    {"layer.flip_vertical", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Mirror the layer top to bottom")},
    {"layer.quick_export_png", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save the layer as a PNG at once")},
    {"layer.export_as", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Save the layer to a file")},
    {"layer.duplicate_to_document", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Copy the layer into another document")},
    {"layer.prevent_auto_nesting", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Keep dragged layers out of folders and artboards")},
    {"layer.lock_transparent_pixels", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Paint only where the layer has pixels")},
    {"layer.merge_selected_to_new", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Combine the selected layers into a new one")},
    {"layer.blend_mode_next", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Step to the next blend mode")},
    {"layer.blend_mode_previous", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Step to the previous blend mode")},
    {"layer.blend_mode.normal", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the layer as is")},
    {"layer.blend_mode.dissolve", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Scatter pixels by opacity")},
    {"layer.blend_mode.darken", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Keep the darker colors")},
    {"layer.blend_mode.multiply", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Darken like stacked ink")},
    {"layer.blend_mode.color_burn", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Darken with more contrast")},
    {"layer.blend_mode.linear_burn", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Darken by lowering brightness")},
    {"layer.blend_mode.lighten", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Keep the lighter colors")},
    {"layer.blend_mode.screen", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Lighten like projected light")},
    {"layer.blend_mode.color_dodge", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Lighten with less contrast")},
    {"layer.blend_mode.linear_dodge", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Lighten by adding brightness")},
    {"layer.blend_mode.overlay", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Raise contrast by the colors below")},
    {"layer.blend_mode.soft_light", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Gently lighten or darken")},
    {"layer.blend_mode.hard_light", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Strongly lighten or darken")},
    {"layer.blend_mode.vivid_light", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Burn or dodge by contrast")},
    {"layer.blend_mode.linear_light", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Burn or dodge by brightness")},
    {"layer.blend_mode.pin_light", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Replace colors by lightness")},
    {"layer.blend_mode.hard_mix", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Push colors to pure primaries")},
    {"layer.blend_mode.difference", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Subtract the colors from each other")},
    {"layer.blend_mode.exclusion", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "A softer difference")},
    {"layer.blend_mode.hue", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Take only the layer's hue")},
    {"layer.blend_mode.saturation", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Take only the layer's saturation")},
    {"layer.blend_mode.color", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Take the layer's hue and saturation")},
    {"layer.blend_mode.luminosity", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Take only the layer's brightness")},
    // Type
    {"type.resolve_missing_fonts", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Replace fonts that are not installed")},
    {"type.convert_to_shape", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Turn text into editable vector outlines")},
    {"type.create_work_path", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Outline the letters as a path")},
    // Select
    {"select.all", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select the whole image")},
    {"select.deselect", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Clear the selection")},
    {"select.reselect", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Bring back the last selection")},
    {"select.inverse", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Swap selected and unselected areas")},
    {"select.all_layers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select every layer")},
    {"select.expand", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make the selection larger")},
    {"select.contract", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make the selection smaller")},
    {"select.border", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select a band along the edge")},
    {"select.smooth", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Round off corners and remove specks")},
    {"select.feather", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Soften the selection edge")},
    {"select.grow", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add neighboring similar colors")},
    {"select.similar", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add similar colors anywhere")},
    {"select.quick_mask", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Paint a selection as a mask")},
    {"select.transform_selection", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Resize, rotate or skew the selection outline")},
    {"select.deselect_layers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select no layers")},
    {"select.layer_below", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make the next layer down the active one")},
    {"select.layer_above", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make the next layer up the active one")},
    {"select.subject", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select the main subject automatically")},
    {"select.background", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select everything but the main subject")},
    {"select.layer_transparency", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select the layer's opaque pixels")},
    {"select.add_layer_below", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add the next layer down to the selected layers")},
    {"select.add_layer_above", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add the next layer up to the selected layers")},
    {"select.object_in_selection", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select the object inside the selection")},
    // Filter
    {"filter.last", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Repeat the last filter")},
    {"filterAction_patchy_filters_soft_glow", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Dreamy glow on the highlights")},
    {"filterAction_patchy_filters_punchy_color", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Bolder colors and contrast")},
    {"filterAction_patchy_filters_noir", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Moody black and white")},
    {"filterAction_patchy_filters_cinematic_matte", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Film look with lifted blacks")},
    {"filterAction_patchy_filters_vintage_fade", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Faded old photo look")},
    {"filterAction_patchy_filters_sepia", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Brown old photo tint")},
    {"filterAction_patchy_filters_vignette", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Darken the corners")},
    {"filterAction_patchy_filters_box_blur", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Even blur over a square area")},
    {"filterAction_patchy_filters_gaussian_blur", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Smooth soft blur")},
    {"filterAction_patchy_filters_motion_blur", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Streak as if moving")},
    {"filterAction_patchy_filters_radial_blur", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Spin or zoom blur around a center")},
    {"filterAction_patchy_filters_surface_blur", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Smooth while keeping edges")},
    {"filterAction_patchy_filters_lens_blur", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Camera-like depth of field")},
    {"filterAction_patchy_filters_iris_blur", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Sharp spot with blur around it")},
    {"filterAction_patchy_filters_tilt_shift_blur", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Miniature look with a sharp band")},
    {"filterAction_patchy_filters_blur", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Soften slightly")},
    {"filterAction_patchy_filters_blur_more", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Soften a bit more")},
    {"filterAction_patchy_filters_average", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fill with the average color")},
    {"filterAction_patchy_filters_sharpen", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Crisp up slightly")},
    {"filterAction_patchy_filters_unsharp_mask", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Sharpen with amount and radius")},
    {"filterAction_patchy_filters_high_pass", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Keep only the fine detail")},
    {"filterAction_patchy_filters_sharpen_more", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Crisp up a bit more")},
    {"filterAction_patchy_filters_sharpen_edges", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Sharpen only the edges")},
    {"filterAction_patchy_filters_twirl", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Swirl around the center")},
    {"filterAction_patchy_filters_wave", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Ripple in waves")},
    {"filterAction_patchy_filters_pinch_bloat", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Squeeze in or bulge out")},
    {"filterAction_patchy_filters_film_grain", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Film-like grain")},
    {"filterAction_patchy_filters_add_noise", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Sprinkle random speckles")},
    {"filterAction_patchy_filters_median", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Smooth out specks")},
    {"filterAction_patchy_filters_dust_and_scratches", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Hide small dust and scratches")},
    {"filterAction_patchy_filters_pixelate", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Turn into large square blocks")},
    {"filterAction_patchy_filters_color_halftone", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Print-style dots per color")},
    {"filterAction_patchy_filters_plastic_wrap", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Shiny wrapped-in-plastic look")},
    {"filterAction_patchy_filters_diffuse", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Scatter pixels softly")},
    {"filterAction_patchy_filters_edge_detect", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show only the edges")},
    {"filterAction_patchy_filters_emboss", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Raised relief look")},
    {"filterAction_patchy_filters_extrude", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Push out 3D blocks")},
    {"filterAction_patchy_filters_find_edges", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Outline edges on white")},
    {"filterAction_patchy_filters_glowing_edges", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Neon edges on black")},
    {"filterAction_patchy_filters_solarize", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Mix negative and positive")},
    {"filterAction_patchy_filters_tiles", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Break into offset tiles")},
    {"filterAction_patchy_filters_trace_contour", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Outline tone boundaries")},
    {"filterAction_patchy_filters_wind", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Streak pixels sideways")},
    {"filterAction_patchy_filters_clouds", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fill with soft cloud noise")},
    {"filterAction_patchy_filters_difference_clouds", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Blend cloud noise by difference")},
    {"filterAction_patchy_filters_fibers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fill with fiber strands")},
    {"filterAction_patchy_filters_flame", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw flames")},
    {"filterAction_patchy_filters_lens_flare", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add a bright camera flare")},
    // Plugins
    {"plugins.repeat_last", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Run the last plug-in again")},
    {"plugins.last_settings", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Run the last plug-in with its dialog")},
    {"plugins.open_folder", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the plug-ins folder on disk")},
    {"companionsOpenFolderAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the folder of companion app manifests on disk")},
    {"plugins.rescan", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Look for new or removed plug-ins")},
    {"pluginsLegacyWindowsOnlyNote", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "8BF plug-ins need Windows")},
    // View
    {"view.zoom_in", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Magnify the view")},
    {"view.zoom_out", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show more of the image")},
    {"view.fit_on_screen", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the whole image")},
    {"view.actual_pixels", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Zoom to 100%")},
    {"view.rulers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show or hide the rulers")},
    {"view.grid", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show or hide the grid")},
    {"view.guides", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show or hide the guides")},
    {"view.snap", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Line up with guides and edges while dragging")},
    {"view.pixel_grid", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the pixel grid when zoomed in")},
    {"view.new_guide", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add a guide line")},
    {"view.clear_guides", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove all guides")},
    {"view.fill_screen", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Zoom until the image fills the window")},
    {"view.zoom_200", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Zoom to twice the size")},
    {"view.vector_preview", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show shape edits live while dragging")},
    {"view.proof_colors", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Preview the print colors")},
    {"view.gamut_warning", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Mark colors that cannot print")},
    {"view.selection_edges", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show or hide guides, grid and edges")},
    {"view.show_selection_edges", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show or hide the marching ants")},
    {"view.target_path", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show or hide the active path outline")},
    {"view.extras_options", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Choose which extras show")},
    {"view.lock_guides", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Stop guides from moving")},
    {"viewSnapToGuidesAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Snap to guide lines")},
    {"viewSnapToGridAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Snap to the grid")},
    {"viewSnapToDocumentAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Snap to the canvas edges and center")},
    {"viewSnapToLayersAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Snap to other layers")},
    {"viewSnapToSelectionAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Snap to the selection edges")},
    {"view.new_guide_layout", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add column and row guides")},
    {"view.clear_selected_guides", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove the selected guides")},
    {"view.tile_preview", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "See the image repeated as a tile")},
    {"view.screen_mode_standard", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Normal window with panels")},
    {"view.screen_mode_full_with_menus", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Full screen keeping the menus")},
    {"view.screen_mode_full", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Full screen without panels")},
    {"view.screen_mode_cycle", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch to the next screen mode")},
    {"view.hide_panels", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show only the canvas, or bring the toolbars back")},
    {"view.scroll_top_left", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the image's upper-left corner")},
    {"view.scroll_bottom_right", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the image's lower-right corner")},
    {"view.scroll_page_up", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move the view up by a window height")},
    {"view.scroll_page_down", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move the view down by a window height")},
    {"view.scroll_page_left", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move the view left by a window width")},
    {"view.scroll_page_right", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move the view right by a window width")},
    // Window
    {"window.float_document", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move the document into its own window")},
    {"window.dock_document", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Put the document back in a tab")},
    {"window.float_all", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Give every document its own window")},
    {"window.consolidate_all_to_tabs", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Put every document in tabs")},
    {"window.tile_windows", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Arrange document windows side by side")},
    {"window.cascade_windows", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Stack document windows diagonally")},
    {"windowSetScreenSize1280x720Action", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Resize the window to 1280 by 720")},
    {"windowSetScreenSize1366x768Action", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Resize the window to 1366 by 768")},
    {"windowSetScreenSize1600x900Action", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Resize the window to 1600 by 900")},
    {"windowSetScreenSize1920x1080Action", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Resize the window to 1920 by 1080")},
    {"windowSetScreenSize2560x1440Action", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Resize the window to 2560 by 1440")},
    {"windowSetScreenSize3840x2160Action", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Resize the window to 3840 by 2160")},
    {"window.force_refresh", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Redraw the whole window")},
    {"window.customize_tool_palette", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Hide and arrange tools")},
    {"window.brush_settings", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the brush dynamics panel")},
    {"window.brush_tips", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the brush tips panel")},
    {"window.panel_channels", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the Channels panel")},
    {"window.panel_color", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the Color panel")},
    {"window.panel_history", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the undo history panel")},
    {"window.panel_info", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the Info panel")},
    {"window.panel_layers", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the Layers panel")},
    {"window.panel_palette", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the Palette panel")},
    {"window.panel_paths", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the Paths panel")},
    {"window.panel_properties", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show the Properties panel")},
    {"window.reset_panels", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Restore the default panel layout")},
    {"windowToolPaletteAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show or hide the tool palette")},
    {"windowToolOptionsAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Show or hide the options bar")},
    // Help
    {"help.patchy_help", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Open the feature guide in your web browser")},
    {"help.search_commands", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Find any command by name")},
    {"help.scripting_guide", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Read how to write scripts")},
    {"help.ai_setup", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Connect an AI assistant to Patchy")},
    {"help.appimage_menu_entry", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add a launcher to your desktop menu")},
    {"helpAboutAction", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Version and credits")},
    // Tools
    {"tools.move", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move layers and selections")},
    {"tools.marquee", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select a rectangle")},
    {"tools.elliptical_marquee", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select an ellipse or circle")},
    {"tools.lasso", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw a freehand selection")},
    {"tools.polygonal_lasso", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select with straight edges")},
    {"tools.magnetic_lasso", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select along edges it finds")},
    {"tools.magic_wand", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select similar colors with a click")},
    {"tools.quick_select", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Paint a selection that finds edges")},
    {"tools.object_select", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select an object automatically")},
    {"tools.brush", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Paint with a brush")},
    {"tools.mixer_brush", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Mix paint with the colors under it")},
    {"tools.clone", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Copy pixels from one place to another")},
    {"tools.pattern_stamp", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Paint with a pattern")},
    {"tools.healing", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Repair blemishes with sampled pixels")},
    {"tools.spot_healing", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove spots with one click")},
    {"tools.patch", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Replace an area with pixels from elsewhere")},
    {"tools.content_aware_move",
     QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Move or copy a selected object and fill the spot it leaves")},
    {"tools.red_eye", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fix red eyes in photos")},
    {"tools.smudge", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Smear paint")},
    {"tools.dodge", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Lighten areas")},
    {"tools.burn", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Darken areas")},
    {"tools.sponge", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Change the color saturation")},
    {"tools.blur", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Soften areas")},
    {"tools.sharpen", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Sharpen areas")},
    {"tools.eraser", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Erase pixels")},
    {"tools.gradient", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw a color gradient")},
    {"tools.fill", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Fill an area with color")},
    {"tools.line", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw a straight line")},
    {"tools.rect", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw a rectangle")},
    {"tools.ellipse", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw an ellipse or circle")},
    {"tools.polygon", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw a polygon or star")},
    {"tools.custom_shape", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw a custom shape")},
    {"tools.eyedropper", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Pick a color from the image")},
    {"tools.ruler", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Measure distances and angles")},
    {"tools.type", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add text")},
    {"tools.hand", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Scroll the view")},
    {"tools.zoom", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Zoom in and out")},
    {"tools.pen", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Draw paths and curves")},
    {"tools.path_select", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Select and move whole shapes")},
    {"tools.direct_select", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Edit points and handles")},
    {"tools.add_anchor", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add a point to a path")},
    {"tools.delete_anchor", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Remove a point from a path")},
    {"tools.convert_point", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch points between corner and smooth")},
    {"tools.crop", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Crop or straighten the image")},
    {"tools.perspective_crop", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Crop and fix the perspective")},
    {"tools.artboard", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Add and arrange artboards")},
    {"tools.cycle.move", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch between the move tools")},
    {"tools.cycle.marquee", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch between the marquee tools")},
    {"tools.cycle.lasso", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch between the lasso tools")},
    {"tools.cycle.wand", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch between the wand and quick select tools")},
    {"tools.cycle.crop", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch between the crop tools")},
    {"tools.cycle.gradient", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch between the gradient and fill tools")},
    {"tools.cycle.stamp", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch between the stamp tools")},
    {"tools.cycle.healing", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch between the healing tools")},
    {"tools.cycle.detail", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch between blur, sharpen and smudge")},
    {"tools.cycle.tone", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch between dodge, burn and sponge")},
    {"tools.cycle.pen", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch between the pen tools")},
    {"tools.cycle.path_select", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch between the path selection tools")},
    {"tools.cycle.shape", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch between the shape tools")},
    {"tools.cycle.pick", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Switch between the eyedropper and ruler")},
    // Color and brush keys
    {"color.default", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Reset to black and white")},
    {"color.swap", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Exchange foreground and background colors")},
    {"brush.smaller", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make the brush smaller")},
    {"brush.larger", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make the brush bigger")},
    {"brush.much_smaller", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make the brush a lot smaller")},
    {"brush.much_larger", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Make the brush a lot bigger")},
    {"brush.softer", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Soften the brush edge")},
    {"brush.harder", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Harden the brush edge")},
    {"brush.preset_previous", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Pick the previous brush preset")},
    {"brush.preset_next", QT_TRANSLATE_NOOP("patchy::ui::CommandSearch", "Pick the next brush preset")},
};

// The non-menu hotkey categories the search lists, with their row path. Panel commands
// (channels, paths), theme and script commands are left out: they act on a panel's target
// or are listed in their menu.
QString search_category_path(const QString& category) {
  if (category == QStringLiteral("tools")) {
    return QCoreApplication::translate("patchy::ui::HotkeyEditorPanel", "Tools");
  }
  if (category == QStringLiteral("color")) {
    return QCoreApplication::translate("patchy::ui::HotkeyEditorPanel", "Color");
  }
  if (category == QStringLiteral("brush")) {
    return QCoreApplication::translate("patchy::ui::HotkeyEditorPanel", "Brush");
  }
  return {};
}

bool search_lists_category(const QString& category) {
  return category.isEmpty() || category == QStringLiteral("tools") || category == QStringLiteral("color") ||
         category == QStringLiteral("brush");
}

void walk_menu(QMenu* menu, bool visible_only, const QString& top, QStringList& submenus,
               const std::function<void(QAction*, const QString&, const QStringList&)>& visit) {
  for (auto* action : menu->actions()) {
    if (action == nullptr || action->isSeparator() || qobject_cast<QWidgetAction*>(action) != nullptr) {
      continue;
    }
    if (visible_only && !action->isVisible()) {
      continue;
    }
    if (auto* submenu = action->menu(); submenu != nullptr) {
      if (visible_only && !action->isEnabled()) {
        continue;
      }
      submenus.push_back(clean_action_text(action));
      walk_menu(submenu, visible_only, top, submenus, visit);
      submenus.pop_back();
      continue;
    }
    visit(action, top, submenus);
  }
}

// A check mark in the left column for a checked command, the kind icon (text height), the
// menu path in the muted color, then the name ("Filter › Blur › Gaussian Blur"), the
// shortcut at the right edge. A data row shows its label instead of a path ("Open Document:
// patchy.jpg"). The name keeps its full width first; the path elides from the left. The
// description is the row's tooltip only (GitHub issue 554: on every row it was too much).
class CommandRowDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
    return {kPopupWidth, option.fontMetrics.height() + 2 * kRowPadding};
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    const auto rect = option.rect;
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    if (selected) {
      painter->fillRect(rect, theme().menu_item_selected_bg);
    } else if ((option.state & QStyle::State_MouseOver) != 0) {
      painter->fillRect(rect, theme().list_row_hover_bg);
    }
    const QColor primary = selected ? theme().text_on_accent : theme().text_primary;
    const QColor muted = selected ? theme().text_on_accent : theme().text_secondary;
    const auto& metrics = option.fontMetrics;
    const int baseline = rect.top() + (rect.height() - metrics.height()) / 2 + metrics.ascent();

    if (index.data(kCheckRole).toInt() == 1) {
      painter->setRenderHint(QPainter::Antialiasing);
      painter->setPen(QPen(primary, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      const QPointF center(rect.left() + kCheckColumn / 2.0 + 1, rect.center().y() + 0.5);
      QPainterPath check;
      check.moveTo(center + QPointF(-4.0, 0.0));
      check.lineTo(center + QPointF(-1.2, 3.0));
      check.lineTo(center + QPointF(4.0, -3.5));
      painter->drawPath(check);
    }

    int right = rect.right() - 2 * kRowPadding;
    const auto shortcut = index.data(kShortcutRole).toString();
    if (!shortcut.isEmpty()) {
      const int width = metrics.horizontalAdvance(shortcut);
      painter->setPen(muted);
      painter->drawText(right - width, baseline, shortcut);
      right -= width + 3 * kRowPadding;
    }
    int x = rect.left() + kCheckColumn;
    const int icon_side = metrics.height();
    if (const auto icon = index.data(Qt::DecorationRole).value<QIcon>(); !icon.isNull()) {
      const QRect icon_rect(x, rect.top() + (rect.height() - icon_side) / 2, icon_side, icon_side);
      icon.paint(painter, icon_rect, Qt::AlignCenter, QIcon::Normal, QIcon::Off);
    }
    x += icon_side + kIconGap;
    const auto full_name = index.data(Qt::DisplayRole).toString();
    const auto path = index.data(kPathRole).toString();
    if (!path.isEmpty()) {
      const auto separator =
          is_data_kind(static_cast<CommandSearchKind>(index.data(kKindRole).toInt())) ? kLabelSeparator : kPathSeparator;
      const int path_room = right - x - metrics.horizontalAdvance(full_name);
      // Left out when only its ellipsis would fit: the name needs the whole row.
      if (path_room > metrics.horizontalAdvance(QStringLiteral("…") + separator)) {
        const auto shown = metrics.elidedText(path + separator, Qt::ElideLeft, path_room);
        painter->setPen(muted);
        painter->drawText(x, baseline, shown);
        x += metrics.horizontalAdvance(shown);
      }
    }
    const auto name = metrics.elidedText(full_name, Qt::ElideRight, right - x);
    painter->setPen(primary);
    painter->drawText(x, baseline, name);
    painter->restore();
  }
};

// Keeps typing in the field: Up/Down/PageUp/PageDown move the list, Enter runs the
// chosen command, Esc closes.
class SearchKeyFilter final : public QObject {
public:
  SearchKeyFilter(QListWidget* list, std::function<void()> run, QObject* parent)
      : QObject(parent), list_(list), run_(std::move(run)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::ShortcutOverride) {
      // Plain typing belongs to the field, never an application shortcut.
      const auto* key = static_cast<const QKeyEvent*>(event);
      if (key->key() == Qt::Key_Escape || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        event->accept();
        return true;
      }
    }
    if (event->type() == QEvent::KeyPress) {
      const auto* key = static_cast<const QKeyEvent*>(event);
      switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
          QApplication::sendEvent(list_, event);
          return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
          run_();
          return true;
        case Qt::Key_Escape:
          if (auto* popup = list_->window(); popup != nullptr) {
            popup->close();
          }
          return true;
        default:
          break;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QListWidget* list_;
  std::function<void()> run_;
};

// Places the menu bar's search button right after the last visible menu title.
class SearchButtonPlacer final : public QObject {
public:
  SearchButtonPlacer(QMenuBar* bar, QToolButton* button) : QObject(button), bar_(bar), button_(button) {
    bar_->installEventFilter(this);
  }

  void place() {
    if (bar_ == nullptr || button_ == nullptr) {
      return;
    }
    QAction* last = nullptr;
    for (auto* action : bar_->actions()) {
      if (action->isVisible() && action->menu() != nullptr) {
        last = action;
      }
    }
    const auto title = last != nullptr ? bar_->actionGeometry(last) : QRect();
    // A title the bar moved into its overflow (">>") menu is not at its own rectangle.
    const bool title_shown = title.isValid() && bar_->actionAt(title.center()) == last;
    int right_limit = bar_->width();
    if (const auto* controls =
            bar_->findChild<QWidget*>(QStringLiteral("windowChromeControls"), Qt::FindDirectChildrenOnly);
        controls != nullptr && controls->isVisible()) {
      right_limit = controls->x();
    }
    const int x = title.right() + 1 + kButtonGap;
    if (bar_->isNativeMenuBar() || !title_shown || x + kButtonWidth > right_limit) {
      button_->hide();
      return;
    }
    button_->setGeometry(x, 0, kButtonWidth, bar_->height());
    button_->show();
    button_->raise();
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == bar_) {
      switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::LayoutRequest:
        case QEvent::LanguageChange:
        case QEvent::FontChange:
        case QEvent::StyleChange:
        case QEvent::ActionAdded:
        case QEvent::ActionChanged:
        case QEvent::ActionRemoved:
          // The bar lays its titles out when it next handles the change; place after it.
          if (!pending_) {
            pending_ = true;
            QTimer::singleShot(0, this, [this] {
              pending_ = false;
              place();
            });
          }
          break;
        default:
          break;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QPointer<QMenuBar> bar_;
  QPointer<QToolButton> button_;
  bool pending_{false};
};

// The written description, else the status tip when it is more than the name restated
// ("Apply Gaussian Blur to the active layer" is not).
QString entry_description(const QAction* action, const QString& id, const QString& name) {
  if (!id.isEmpty()) {
    if (auto written = command_search_description(id); !written.isEmpty()) {
      return written;
    }
  }
  const auto tip = action != nullptr ? action->statusTip().simplified() : QString();
  if (tip.isEmpty() || tip.contains(name, Qt::CaseInsensitive)) {
    return {};
  }
  return tip;
}

// A description matches a word only at a word start: "move" must not find "Remove EXIF".
bool contains_word_start(const QString& text, const QString& word) {
  for (qsizetype at = text.indexOf(word); at >= 0; at = text.indexOf(word, at + 1)) {
    if (at == 0 || !text.at(at - 1).isLetterOrNumber()) {
      return true;
    }
  }
  return false;
}

}  // namespace

QString strip_menu_ellipsis(QString label) {
  while (label.endsWith(QStringLiteral("..."))) {
    label.chop(3);
  }
  while (label.endsWith(QChar(0x2026))) {
    label.chop(1);
  }
  return label.trimmed();
}

void for_each_menu_bar_command(
    const QMenuBar* menu_bar, bool visible_only,
    const std::function<void(QAction* action, const QString& top, const QStringList& submenus)>& visit) {
  if (menu_bar == nullptr) {
    return;
  }
  for (auto* top_action : menu_bar->actions()) {
    auto* menu = top_action->menu();
    if (menu == nullptr || (visible_only && !top_action->isVisible())) {
      continue;
    }
    QStringList submenus;
    walk_menu(menu, visible_only, clean_action_text(top_action), submenus, visit);
  }
}

std::vector<MenuCommandEntry> collect_menu_commands(const QMenuBar* menu_bar) {
  return collect_menu_entries(menu_bar, false);
}

std::vector<MenuCommandEntry> collect_menu_entries(const QMenuBar* menu_bar, bool unavailable_too) {
  std::vector<MenuCommandEntry> entries;
  QSet<const QAction*> seen;
  for_each_menu_bar_command(menu_bar, !unavailable_too, [&](QAction* action, const QString& top,
                                                            const QStringList& submenus) {
    if ((!unavailable_too && !action->isEnabled()) || seen.contains(action)) {
      return;
    }
    // Open documents and recent files are rows of their own (show_command_search).
    const auto kind = action_kind(action, CommandSearchKind::menu);
    if (is_data_kind(kind)) {
      return;
    }
    const auto name = strip_menu_ellipsis(clean_action_text(action));
    if (name.isEmpty()) {
      return;
    }
    seen.insert(action);
    auto path = top;
    for (const auto& submenu : submenus) {
      path += kPathSeparator + strip_menu_ellipsis(submenu);
    }
    MenuCommandEntry entry{action, name, path};
    entry.kind = kind;
    entries.push_back(std::move(entry));
  });
  return entries;
}

QString command_search_description(const QString& id) {
  for (const auto& entry : kCommandDescriptions) {
    if (id == QLatin1String(entry.id)) {
      return QCoreApplication::translate("patchy::ui::CommandSearch", entry.text);
    }
  }
  return {};
}

QStringList command_search_described_ids() {
  QStringList ids;
  for (const auto& entry : kCommandDescriptions) {
    ids << QLatin1String(entry.id);
  }
  return ids;
}

bool command_search_exempt(const MenuCommandEntry& entry) {
  return entry.action != nullptr && entry.action->property(kCommandSearchDataEntryProperty).toBool();
}

QString command_search_id(const QAction* action, const HotkeyRegistry& registry) {
  if (action == nullptr) {
    return {};
  }
  for (const auto& command : registry.commands()) {
    if (command.action.data() == action) {
      return command.id;
    }
  }
  return action->objectName();
}

std::vector<MenuCommandEntry> collect_search_commands(const QMenuBar* menu_bar, const HotkeyRegistry& registry,
                                                      bool unavailable_too) {
  QHash<const QAction*, QString> id_by_action;
  for (const auto& command : registry.commands()) {
    if (command.action != nullptr && !id_by_action.contains(command.action.data())) {
      id_by_action.insert(command.action.data(), command.id);
    }
  }
  const auto id_of = [&id_by_action](const QAction* action) {
    const auto id = id_by_action.value(action);
    return id.isEmpty() && action != nullptr ? action->objectName() : id;
  };
  auto entries = collect_menu_entries(menu_bar, unavailable_too);
  for (auto& entry : entries) {
    entry.id = id_of(entry.action.data());
    entry.description = entry_description(entry.action.data(), entry.id, entry.name);
  }
  // Everything the menus hold, hidden places included, so a command inside a disabled
  // submenu is not listed again as a key outside the menus.
  QSet<const QAction*> in_menus;
  for_each_menu_bar_command(menu_bar, false, [&in_menus](QAction* action, const QString&, const QStringList&) {
    in_menus.insert(action);
  });
  QSet<const QAction*> listed;
  // The tools first, in palette order, then the other keys outside the menus.
  const auto add_registered = [&](bool tools) {
    for (const auto& command : registry.commands()) {
      const auto* action = command.action.data();
      if (action == nullptr || (command.category == QStringLiteral("tools")) != tools ||
          !search_lists_category(command.category) || in_menus.contains(action) || listed.contains(action) ||
          (!unavailable_too && !action->isEnabled()) || action->menu() != nullptr) {
        continue;
      }
      const auto name = strip_menu_ellipsis(clean_action_text(action));
      if (name.isEmpty()) {
        continue;
      }
      listed.insert(action);
      MenuCommandEntry entry{command.action, name, search_category_path(command.category),
                             entry_description(action, command.id, name), command.id};
      entry.kind = tools ? CommandSearchKind::tool : CommandSearchKind::key;
      entries.push_back(std::move(entry));
    }
  };
  add_registered(true);
  add_registered(false);
  return entries;
}

QString command_search_kind_label(CommandSearchKind kind) {
  switch (kind) {
    case CommandSearchKind::open_document:
      return QCoreApplication::translate("patchy::ui::CommandSearch", "Open Document");
    case CommandSearchKind::recent_file:
      return QCoreApplication::translate("patchy::ui::CommandSearch", "Recent File");
    case CommandSearchKind::recent_folder:
      return QCoreApplication::translate("patchy::ui::CommandSearch", "Recent Folder");
    default:
      return {};
  }
}

QIcon command_search_kind_icon(CommandSearchKind kind) {
  // Patchy's own small glyphs on a 16 px grid in the theme's icon ink (procedural, so a
  // scheme change repaints them): a menu, a panel with its title band, a key cap, a page, and
  // a page or a folder with a clock for the recent ones. A tool or a menu command with an
  // icon of its own shows that instead.
  const auto outline = [](QPainter& painter, const QColor& ink) {
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(ink, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
  };
  const auto page = [](QPainter& painter, qreal left, qreal top, qreal right, qreal bottom) {
    const qreal fold = 3.0;
    QPainterPath sheet;
    sheet.moveTo(left, top);
    sheet.lineTo(right - fold, top);
    sheet.lineTo(right, top + fold);
    sheet.lineTo(right, bottom);
    sheet.lineTo(left, bottom);
    sheet.closeSubpath();
    painter.drawPath(sheet);
    QPainterPath corner;
    corner.moveTo(right - fold, top);
    corner.lineTo(right - fold, top + fold);
    corner.lineTo(right, top + fold);
    painter.drawPath(corner);
  };
  const auto clock = [](QPainter& painter, const QColor& ink) {
    const QPointF center(11.6, 11.6);
    // Cleared first, so the page or folder line under it does not cross the dial.
    painter.save();
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    painter.drawEllipse(center, 4.4, 4.4);
    painter.restore();
    painter.setPen(QPen(ink, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawEllipse(center, 3.5, 3.5);
    painter.drawLine(center, center + QPointF(0.0, -2.1));
    painter.drawLine(center, center + QPointF(1.6, 0.0));
  };
  switch (kind) {
    case CommandSearchKind::menu:
      return themed_glyph_icon(QStringLiteral("command-search-menu"), 16.0, &ThemePalette::icon_ink,
                               [outline](QPainter& painter, const QColor& ink) {
                                 outline(painter, ink);
                                 painter.drawRoundedRect(QRectF(2.5, 2.5, 11.0, 11.0), 1.5, 1.5);
                                 for (const qreal y : {5.8, 8.0, 10.2}) {
                                   painter.drawLine(QPointF(5.0, y), QPointF(11.0, y));
                                 }
                               });
    case CommandSearchKind::panel:
      return themed_glyph_icon(QStringLiteral("command-search-panel"), 16.0, &ThemePalette::icon_ink,
                               [outline](QPainter& painter, const QColor& ink) {
                                 outline(painter, ink);
                                 painter.drawRoundedRect(QRectF(2.0, 2.5, 12.0, 11.0), 1.5, 1.5);
                                 painter.setBrush(ink);
                                 painter.drawRect(QRectF(2.0, 2.5, 12.0, 2.6));
                                 painter.drawLine(QPointF(9.5, 5.1), QPointF(9.5, 13.5));
                               });
    case CommandSearchKind::key:
    case CommandSearchKind::tool:
      return themed_glyph_icon(QStringLiteral("command-search-key"), 16.0, &ThemePalette::icon_ink,
                               [outline](QPainter& painter, const QColor& ink) {
                                 outline(painter, ink);
                                 painter.drawRoundedRect(QRectF(1.8, 2.5, 12.4, 11.0), 2.5, 2.5);
                                 painter.drawRoundedRect(QRectF(4.0, 4.2, 8.0, 6.3), 1.4, 1.4);
                               });
    case CommandSearchKind::open_document:
      return themed_glyph_icon(QStringLiteral("command-search-document"), 16.0, &ThemePalette::icon_ink,
                               [outline, page](QPainter& painter, const QColor& ink) {
                                 outline(painter, ink);
                                 page(painter, 3.5, 1.8, 12.5, 14.2);
                                 for (const qreal y : {8.0, 10.5}) {
                                   painter.drawLine(QPointF(5.8, y), QPointF(10.2, y));
                                 }
                               });
    case CommandSearchKind::recent_file:
      return themed_glyph_icon(QStringLiteral("command-search-recent-file"), 16.0, &ThemePalette::icon_ink,
                               [outline, page, clock](QPainter& painter, const QColor& ink) {
                                 outline(painter, ink);
                                 page(painter, 2.0, 1.5, 10.5, 13.5);
                                 clock(painter, ink);
                               });
    case CommandSearchKind::recent_folder:
      return themed_glyph_icon(QStringLiteral("command-search-recent-folder"), 16.0, &ThemePalette::icon_ink,
                               [outline, clock](QPainter& painter, const QColor& ink) {
                                 outline(painter, ink);
                                 QPainterPath folder;
                                 folder.moveTo(1.5, 3.0);
                                 folder.lineTo(5.5, 3.0);
                                 folder.lineTo(7.0, 4.6);
                                 folder.lineTo(13.5, 4.6);
                                 folder.lineTo(13.5, 12.5);
                                 folder.lineTo(1.5, 12.5);
                                 folder.closeSubpath();
                                 painter.drawPath(folder);
                                 painter.drawLine(QPointF(1.5, 6.6), QPointF(13.5, 6.6));
                                 clock(painter, ink);
                               });
  }
  return {};
}

MenuCommandEntry command_search_path_entry(CommandSearchKind kind, const QString& path, std::function<void()> run) {
  const QFileInfo info(path);
  auto name = info.fileName();
  if (name.isEmpty()) {
    name = QDir::toNativeSeparators(path);  // a drive or share root
  }
  MenuCommandEntry entry{nullptr, name, command_search_kind_label(kind)};
  entry.kind = kind;
  entry.tooltip = QDir::toNativeSeparators(path);
  entry.run = std::move(run);
  return entry;
}

std::vector<int> match_menu_commands(const std::vector<MenuCommandEntry>& entries, const QString& query) {
  const auto words = query.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
  const auto phrase = words.join(QLatin1Char(' '));
  std::vector<std::pair<int, int>> ranked;  // (tier, index)
  for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
    const auto& entry = entries[static_cast<std::size_t>(index)];
    const auto name = entry.name.toLower();
    const auto path = entry.path.toLower();
    const auto description = entry.description.toLower();
    bool all = true;
    for (const auto& word : words) {
      if (!name.contains(word) && !path.contains(word) && !contains_word_start(description, word)) {
        all = false;
        break;
      }
    }
    if (!all) {
      continue;
    }
    // Only the exact and starts-with matches move up; everything else keeps menu bar order.
    int tier = 2;
    if (phrase.isEmpty() || name == phrase || description == phrase) {
      tier = 0;
    } else if (name.startsWith(phrase) || description.startsWith(phrase)) {
      tier = 1;
    }
    ranked.emplace_back(tier, index);
  }
  std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<int> result;
  result.reserve(ranked.size());
  for (const auto& [tier, index] : ranked) {
    result.push_back(index);
  }
  return result;
}

QFrame* show_command_search_popup(QWidget* parent, std::vector<MenuCommandEntry> entries, QPoint top_left) {
  auto* popup = new QFrame(parent, Qt::Popup);
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setObjectName(QStringLiteral("commandSearchPopup"));
  popup->setFrameShape(QFrame::StyledPanel);
  set_themed_style(*popup, QStringLiteral(R"(
    QFrame#commandSearchPopup {
      background: @menu_bg;
      border: 1px solid @menu_border;
    }
    QFrame#commandSearchPopup QListWidget {
      background: @menu_bg;
      color: @text_primary;
      border: 0;
      outline: none;
    }
  )"));

  auto* layout = new QVBoxLayout(popup);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);

  auto* search = new QLineEdit(popup);
  search->setObjectName(QStringLiteral("commandSearchEdit"));
  search->setPlaceholderText(QCoreApplication::translate("patchy::ui::CommandSearch", "Search commands and tools"));
  search->setClearButtonEnabled(true);
  layout->addWidget(search);

  auto* list = new QListWidget(popup);
  list->setObjectName(QStringLiteral("commandSearchList"));
  list->setItemDelegate(new CommandRowDelegate(list));
  list->setUniformItemSizes(true);
  list->setSelectionMode(QAbstractItemView::SingleSelection);
  list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list->setFocusPolicy(Qt::NoFocus);
  list->setMouseTracking(true);
  list->viewport()->setAttribute(Qt::WA_Hover, true);
  // Polished first: the application sheet's QListWidget min-height would otherwise replace
  // this minimum when the popup shows, and its font sets the row height.
  list->ensurePolished();
  const int row_height = list->fontMetrics().height() + 2 * kRowPadding;
  list->setFixedHeight(row_height * kVisibleRows + 2 * list->frameWidth());
  layout->addWidget(list);

  auto shared_entries = std::make_shared<std::vector<MenuCommandEntry>>(std::move(entries));
  QHash<int, QIcon> kind_icons;
  for (int kind = 0; kind <= static_cast<int>(CommandSearchKind::recent_folder); ++kind) {
    kind_icons.insert(kind, command_search_kind_icon(static_cast<CommandSearchKind>(kind)));
  }
  const auto refill = [list, shared_entries, kind_icons](const QString& query) {
    list->clear();
    for (const int index : match_menu_commands(*shared_entries, query)) {
      const auto& entry = (*shared_entries)[static_cast<std::size_t>(index)];
      if (entry.action == nullptr && !entry.run) {
        continue;
      }
      const auto* action = entry.action.data();
      auto* item = new QListWidgetItem(entry.name, list);
      item->setData(kPathRole, entry.path);
      item->setData(kShortcutRole, action != nullptr ? action_shortcut_text(action) : QString());
      item->setData(kCheckRole,
                    action != nullptr && action->isCheckable() ? (action->isChecked() ? 1 : 0) : -1);
      item->setData(kEntryRole, index);
      item->setData(kKindRole, static_cast<int>(entry.kind));
      // The action's own icon (a tool's palette icon), else the kind's; data rows always
      // show their kind.
      const bool own_icon = action != nullptr && !is_data_kind(entry.kind) && !action->icon().isNull();
      item->setData(Qt::DecorationRole, own_icon ? action->icon() : kind_icons.value(static_cast<int>(entry.kind)));
      if (!entry.description.isEmpty()) {
        item->setData(kDescriptionRole, entry.description);
      }
      if (const auto& tip = entry.tooltip.isEmpty() ? entry.description : entry.tooltip; !tip.isEmpty()) {
        item->setToolTip(tip);
      }
    }
    if (list->count() > 0) {
      list->setCurrentRow(0);
    }
  };
  const QPointer<QWidget> owner = parent;
  const auto run = [popup, list, shared_entries, owner] {
    auto* item = list->currentItem();
    if (item == nullptr) {
      return;
    }
    const auto& entry = (*shared_entries)[static_cast<std::size_t>(item->data(kEntryRole).toInt())];
    const QPointer<QAction> action = entry.action;
    auto callback = entry.run;
    popup->close();
    if (callback && action == nullptr) {
      // A recent file or folder: opened once the popup is gone, while its window lives.
      if (owner != nullptr) {
        QTimer::singleShot(0, owner.data(), std::move(callback));
      }
      return;
    }
    if (action == nullptr) {
      return;
    }
    // After the popup is gone, so a command that opens a dialog or a menu never fights
    // the popup's grab.
    QTimer::singleShot(0, action.data(), [action] {
      if (action != nullptr && action->isEnabled()) {
        action->trigger();
      }
    });
  };
  QObject::connect(search, &QLineEdit::textChanged, popup, refill);
  QObject::connect(list, &QListWidget::itemClicked, popup, [run] { run(); });
  search->installEventFilter(new SearchKeyFilter(list, run, search));
  refill(QString());

  popup->resize(kPopupWidth, popup->sizeHint().height());
  auto position = top_left;
  const auto* screen = parent != nullptr ? parent->screen() : QGuiApplication::screenAt(top_left);
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  if (screen != nullptr) {
    const auto available = screen->availableGeometry();
    position.setX(std::clamp(position.x(), available.left(),
                             std::max(available.left(), available.right() - popup->width() + 1)));
    position.setY(std::clamp(position.y(), available.top(),
                             std::max(available.top(), available.bottom() - popup->height() + 1)));
  }
  popup->move(position);
  popup->show();
  search->setFocus(Qt::PopupFocusReason);
  return popup;
}

QToolButton* add_menu_bar_search_button(QMenuBar* menu_bar, QAction* action) {
  auto* button = new QToolButton(menu_bar);
  button->setObjectName(QStringLiteral("menuBarCommandSearchButton"));
  button->setDefaultAction(action);
  button->setFocusPolicy(Qt::NoFocus);
  button->setAutoRaise(true);
  button->setIconSize(QSize(16, 16));
  set_themed_style(*button, QStringLiteral(R"(
    QToolButton#menuBarCommandSearchButton {
      background: transparent;
      border: 0;
      padding: 0;
      min-width: 0;
      min-height: 0;
    }
    QToolButton#menuBarCommandSearchButton:hover,
    QToolButton#menuBarCommandSearchButton:pressed {
      background: @menu_bar_item_hover_bg;
    }
  )"));
  auto* placer = new SearchButtonPlacer(menu_bar, button);
  placer->place();
  return button;
}

}  // namespace patchy::ui
