#pragma once

// The GIMP 3 plug-in wire protocol as Patchy speaks it (docs/gimp-plugins.md, "Protocol").
//
// Patchy's own declarations. The message and parameter numbers and the field order and
// widths of each record are interface facts taken from libgimpbase's gimpprotocol.h
// (GIMP 3.2.6, LGPL-3, used under its section 3; NOTICE-THIRD-PARTY.md); how each record
// is framed on the pipe was taken from observed traffic between GIMP 3.2.6 and its
// plug-ins (docs/gimp-plugins-provenance.md). No GIMP code is included. Qt-free: the
// helper (src/plugins/gimp/host/) and the core tests use it.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace patchy::gimp {

// The one protocol version Patchy speaks (GIMP 3.2.x). A plug-in built for another one
// refuses to start, and Patchy refuses plug-ins whose registration says so.
inline constexpr std::uint32_t kProtocolVersion = 0x0117;

enum class MessageType : std::uint32_t {
  Quit = 0,
  Config = 1,
  TileRequest = 2,
  TileAck = 3,
  TileData = 4,
  ProcRun = 5,
  ProcReturn = 6,
  TempProcRun = 7,
  TempProcReturn = 8,
  ProcInstall = 9,
  ProcUninstall = 10,
  ExtensionAck = 11,
  HasInit = 12,
};

enum class ParamDefKind : std::uint32_t {
  Default = 0,
  Int = 1,
  Unit = 2,
  Enum = 3,
  Choice = 4,
  Boolean = 5,
  Double = 6,
  String = 7,
  GeglColor = 8,
  Id = 9,
  IdArray = 10,
  ExportOptions = 11,
  Resource = 12,
  File = 13,
  Curve = 14,
};

enum class ParamKind : std::uint32_t {
  Int = 0,
  Double = 1,
  String = 2,
  Strv = 3,
  Bytes = 4,
  File = 5,
  BablFormat = 6,
  GeglColor = 7,
  ColorArray = 8,
  Parasite = 9,
  Array = 10,
  IdArray = 11,
  ExportOptions = 12,
  ParamDef = 13,
  ValueArray = 14,
  Curve = 15,
};

// Public enumerations from GIMP's installed libgimpbase headers (gimpbaseenums.h).
enum class PdbStatus : std::int32_t {
  ExecutionError = 0,
  CallingError = 1,
  PassThrough = 2,
  Success = 3,
  Cancel = 4,
};

enum class RunMode : std::int32_t {
  Interactive = 0,
  NonInteractive = 1,
  WithLastValues = 2,
};

enum class ProcType : std::uint32_t {
  Internal = 0,
  Plugin = 1,
  Persistent = 2,
  Temporary = 3,
};

inline constexpr std::uint32_t kSensitiveDrawable = 1u << 0;
inline constexpr std::uint32_t kSensitiveDrawables = 1u << 2;
inline constexpr std::uint32_t kSensitiveNoDrawables = 1u << 3;
inline constexpr std::uint32_t kSensitiveNoImage = 1u << 4;

// A string on the wire is either absent (NULL) or a NUL-terminated UTF-8 string.
using WireString = std::optional<std::string>;

struct Color {
  std::vector<std::uint8_t> bytes;  // pixel bytes in `encoding` (at most 40)
  WireString encoding;              // a babl format name, "R'G'B'A float"
  std::vector<std::uint8_t> profile;  // ICC profile, empty for none
};

struct Choice {
  WireString nick;
  std::int32_t id{0};
  WireString label;
  WireString help;
};

struct ParamDef {
  ParamDefKind kind{ParamDefKind::Default};
  WireString type_name;        // the GParamSpec type ("GParamInt", "GimpParamChoice")
  WireString value_type_name;  // the value's GType ("gint", "GimpImage")
  WireString name;
  WireString nick;
  WireString blurb;
  std::uint32_t flags{0};

  // Per-kind metadata (only the fields of `kind` are on the wire).
  std::int64_t int_min{0};
  std::int64_t int_max{0};
  std::int64_t int_default{0};
  double double_min{0};
  double double_max{0};
  double double_default{0};
  std::int32_t int32_default{0};  // Enum, Boolean, Unit
  std::int32_t unit_allow_pixels{0};
  std::int32_t unit_allow_percent{0};
  WireString string_default;  // String, Choice, File (a URI)
  std::vector<Choice> choices;
  std::int32_t color_has_alpha{0};
  Color color_default;
  std::int32_t none_ok{0};  // Id, Resource, File, Curve
  WireString item_type;     // IdArray: the element type ("GimpDrawable")
  std::int32_t resource_default_to_context{0};
  std::int32_t resource_default_id{0};
  std::int32_t file_action{0};
};

struct Param {
  ParamKind kind{ParamKind::Int};
  WireString type_name;  // the value's GType name ("gint", "GimpDrawable", "GimpRunMode")

  std::int32_t int_value{0};
  double double_value{0};
  WireString string_value;  // String, File (a URI)
  std::vector<WireString> strings;  // Strv
  std::vector<std::uint8_t> bytes;  // Bytes, Array
  Color color;                      // GeglColor
  std::vector<Color> colors;        // ColorArray
  WireString item_type;             // IdArray
  std::vector<std::int32_t> ids;    // IdArray
  WireString format_encoding;       // BablFormat
  std::vector<std::uint8_t> format_profile;
  std::vector<ParamDef> param_def;  // ParamDef: exactly one element
  std::vector<Param> values;        // ValueArray
};

struct Config {
  std::uint32_t tile_width{128};
  std::uint32_t tile_height{128};
  std::int32_t shm_id{-1};  // -1: tile data travels in the messages
  std::int8_t check_size{1};
  std::int8_t check_type{1};
  Color check_custom_color1;
  Color check_custom_color2;
  std::int8_t show_help_button{0};
  std::int8_t use_cpu_accel{1};
  std::int8_t use_opencl{0};
  std::int8_t export_color_profile{1};
  std::int8_t export_comment{0};
  std::int8_t export_exif{0};
  std::int8_t export_xmp{0};
  std::int8_t export_iptc{0};
  std::int8_t update_metadata{0};
  std::int32_t default_display_id{-1};
  WireString app_name;
  WireString wm_class;
  WireString display_name;
  std::int32_t monitor_number{0};
  std::uint32_t timestamp{0};
  WireString icon_theme_dir;
  std::uint64_t tile_cache_size{256ull << 20};
  WireString swap_path;
  WireString swap_compression;
  std::int32_t num_processors{1};
};

struct TileRequest {
  std::int32_t drawable_id{0};
  std::uint32_t tile_num{0};
  std::uint32_t shadow{0};
};

struct TileData {
  std::int32_t drawable_id{0};
  std::uint32_t tile_num{0};
  std::uint32_t shadow{0};
  std::uint32_t bpp{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint32_t use_shm{0};
  std::vector<std::uint8_t> data;  // width * height * bpp bytes unless use_shm
};

struct ProcCall {  // ProcRun, ProcReturn, TempProcRun, TempProcReturn
  WireString name;
  std::vector<Param> params;
};

struct ProcInstall {
  WireString name;
  ProcType type{ProcType::Plugin};
  std::vector<ParamDef> params;
  std::vector<ParamDef> return_values;
};

struct Message {
  MessageType type{MessageType::Quit};
  Config config;
  TileRequest tile_request;
  TileData tile_data;
  ProcCall call;
  ProcInstall install;
  WireString uninstall_name;
};

// Convenience constructors for the params Patchy sends.
Param int_param(std::string type_name, std::int32_t value);
Param double_param(std::string type_name, double value);
Param string_param(std::string type_name, WireString value);
Param bytes_param(std::string type_name, std::vector<std::uint8_t> value);
Param id_array_param(std::string type_name, std::string item_type, std::vector<std::int32_t> ids);
Param color_param(Color color);
Param status_param(PdbStatus status);

const char* message_type_name(MessageType type) noexcept;

}  // namespace patchy::gimp
