#pragma once

// What a GIMP 3 plug-in registers when queried, and which of it Patchy can run
// (docs/gimp-plugins.md, "Query and the menu"). Qt-free.

#include "plugins/gimp/gimp_protocol.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace patchy::gimp {

// One procedure as the query run recorded it: the PROC_INSTALL message plus what the
// plug-in told the host through gimp-pdb-set-proc-* and gimp-pdb-add-proc-menu-path.
struct ProcedureInfo {
  ProcInstall install;
  std::string menu_label;               // with GIMP's mnemonic underscores
  std::vector<std::string> menu_paths;  // "<Image>/Filters/Distorts"
  std::string image_types;              // "RGB*, GRAY*"; empty when never set
  bool sensitivity_set{false};
  std::uint32_t sensitivity_mask{0};
  std::string blurb;
  std::string help;
  std::string help_id;
  std::string authors;
  std::string copyright;
  std::string date;
  bool file_handler{false};  // registered as a load or export handler
  bool batch_interpreter{false};
};

// How Patchy treats a registered procedure.
enum class ProcedureUse {
  LayerFilter,  // listed in the menu and runnable on one layer
  Unsupported,  // listed with its reason (Preferences and patchy.gimpPlugins.list)
  Hidden,       // file handlers, helpers without a menu entry: not listed in the menu
};

struct ProcedureClass {
  ProcedureUse use{ProcedureUse::Hidden};
  // A stable code; MainWindow turns it into translated text (gimp_plugin_reason_text).
  std::string reason;
};

// Reason codes (persisted in the query cache).
inline constexpr std::string_view kReasonFileHandler = "file-handler";
inline constexpr std::string_view kReasonNoMenu = "no-menu";
inline constexpr std::string_view kReasonNotImageProcedure = "not-image-procedure";
inline constexpr std::string_view kReasonNoLayer = "needs-no-layer";
inline constexpr std::string_view kReasonSeveralLayers = "needs-several-layers";
inline constexpr std::string_view kReasonNotRgb = "not-rgb";
inline constexpr std::string_view kReasonMenuOutsideFilters = "menu-outside-filters";
inline constexpr std::string_view kReasonScriptFu = "script-fu";
inline constexpr std::string_view kReasonTemporary = "temporary";
inline constexpr std::string_view kReasonAnimation = "animation";
inline constexpr std::string_view kReasonCreatesImage = "creates-image";
inline constexpr std::string_view kReasonImageOperations = "image-operations";
inline constexpr std::string_view kReasonResources = "uses-gimp-resources";
inline constexpr std::string_view kReasonNewLayer = "needs-new-layer";
inline constexpr std::string_view kReasonSeveralImages = "needs-several-images";
inline constexpr std::string_view kReasonPaintingTools = "needs-painting-tools";
inline constexpr std::string_view kReasonClipboard = "needs-clipboard";
inline constexpr std::string_view kReasonGradients = "needs-gradients";

[[nodiscard]] ProcedureClass classify_procedure(const ProcedureInfo& procedure, bool from_script_fu);

// True when a GIMP image-type string ("RGB*, GRAY*", "*", "RGBA") accepts RGB or RGBA.
[[nodiscard]] bool image_types_accept_rgb(std::string_view image_types);

// "<Image>/Filters/Distorts" -> {"Filters", "Distorts"}; empty for paths outside <Image>.
[[nodiscard]] std::vector<std::string> image_menu_segments(std::string_view menu_path);

// "_Blinds..." -> "Blinds..."; "__" stays one underscore.
[[nodiscard]] std::string strip_mnemonic(std::string_view label);

// The procedure's first three arguments are run mode, image and drawables (GIMP's image
// procedure signature).
[[nodiscard]] bool is_image_procedure(const ProcInstall& install);

// The value GIMP gives an argument the caller does not set: the registered default, no
// object (-1) for object arguments, the empty value for arrays. Nullopt with `error` set
// for argument kinds Patchy cannot fill in (curves, other untyped defaults).
[[nodiscard]] std::optional<Param> default_argument(const ParamDef& def, std::string* error);

// A color from GIMP as non-linear sRGB in 0..1 (R'G'B'(A) and linear RGB(A) encodings in
// u8, u16, float or double; the profile is taken as sRGB). False for other encodings.
bool color_to_srgb(const Color& color, double rgba[4]);

std::string base64_encode(const std::vector<std::uint8_t>& data);
bool base64_decode(std::string_view text, std::vector<std::uint8_t>& out);

}  // namespace patchy::gimp
