// GIMP 3 plug-in support (docs/gimp-plugins.md): the wire codec against messages
// observed between GIMP 3.2.6 and its plug-ins, round trips of every message and
// parameter type, broken-stream refusals, the query classification, the files GIMP
// installs (interpreters, environ, gimprc, plug-in folders) under Unicode paths, the
// commands that start GIMP's Flatpak plug-ins in its sandbox, and the Patchy-to-helper link. Nothing here needs GIMP installed.

#include "plugins/gimp/gimp_flatpak.hpp"
#include "plugins/gimp/gimp_host_link.hpp"
#include "plugins/gimp/gimp_install.hpp"
#include "plugins/gimp/gimp_procedure.hpp"
#include "plugins/gimp/gimp_wire_codec.hpp"
#include "support/path_utils.hpp"
#include "test_groups.hpp"
#include "test_harness.hpp"
#include "test_scratch_remove.hpp"
#include "unicode_path_names.hpp"

#include <climits>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace patchy::gimp;

std::vector<std::uint8_t> from_hex(const std::string& hex) {
  std::vector<std::uint8_t> out;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    out.push_back(static_cast<std::uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
  }
  return out;
}

Message decode(const std::vector<std::uint8_t>& bytes, std::size_t* left = nullptr) {
  SpanSource source(bytes);
  auto m = read_message(source);
  if (left != nullptr) *left = source.remaining();
  return m;
}

// GIMP 3.2.6 running plug-in-blinds non-interactively (traffic logged by
// scripts/dev/gimp/wire_tap.cpp, docs/gimp-plugins-provenance.md).
void gimp_wire_codec_reads_observed_gimp_messages() {
  const auto run = from_hex(
      "000000050000000f706c75672d696e2d626c696e64730000000007000000000000000c47696d7052756e4d6f6465000000000100000000"
      "0000000a47696d70496d61676500000000010000000b0000001447696d70436f72654f626a6563744172726179000000000947696d70"
      "4974656d000000000100000002000000000000000567696e74000000001e000000000000000567696e74000000000500000002000000"
      "0b67636861726172726179000000000b686f72697a6f6e74616c00000000000000000967626f6f6c65616e0000000000");
  std::size_t left = 99;
  const auto m = decode(run, &left);
  CHECK(left == 0);
  CHECK(m.type == MessageType::ProcRun);
  CHECK(m.call.name == std::optional<std::string>("plug-in-blinds"));
  CHECK(m.call.params.size() == 7);
  if (m.call.params.size() == 7) {
    CHECK(m.call.params[0].kind == ParamKind::Int && m.call.params[0].type_name == std::optional<std::string>("GimpRunMode") &&
          m.call.params[0].int_value == 1);
    CHECK(m.call.params[2].kind == ParamKind::IdArray && m.call.params[2].item_type == std::optional<std::string>("GimpItem") &&
          m.call.params[2].ids == std::vector<std::int32_t>{2});
    CHECK(m.call.params[3].int_value == 30 && m.call.params[4].int_value == 5);
    CHECK(m.call.params[5].kind == ParamKind::String && m.call.params[5].string_value == std::optional<std::string>("horizontal"));
    CHECK(m.call.params[6].type_name == std::optional<std::string>("gboolean") && m.call.params[6].int_value == 0);
  }
  // Re-encoding gives GIMP's bytes back.
  CHECK(encode_message(m) == run);

  const auto tile_request = from_hex("00000002000000020000000000000000");
  const auto t = decode(tile_request);
  CHECK(t.type == MessageType::TileRequest && t.tile_request.drawable_id == 2 && t.tile_request.tile_num == 0 &&
        t.tile_request.shadow == 0);
  CHECK(encode_message(t) == tile_request);

  const auto mask = from_hex(
      "000000060000001d67696d702d6472617761626c652d6d61736b2d696e746572736563740000000006000000000000001247696d70504442"
      "537461747573547970650000000003000000000000000967626f6f6c65616e0000000001000000000000000567696e74000000001400000000"
      "0000000567696e74000000001e000000000000000567696e740000000096000000000000000567696e740000000064");
  const auto r = decode(mask);
  CHECK(r.type == MessageType::ProcReturn && r.call.params.size() == 6);
  if (r.call.params.size() == 6) {
    CHECK(r.call.params[0].int_value == static_cast<std::int32_t>(PdbStatus::Success));
    CHECK(r.call.params[2].int_value == 20 && r.call.params[3].int_value == 30 && r.call.params[4].int_value == 150 &&
          r.call.params[5].int_value == 100);
  }
  CHECK(encode_message(r) == mask);
}

ParamDef def(ParamDefKind kind, std::string name, std::string value_type) {
  ParamDef d;
  d.kind = kind;
  d.type_name = std::string("GParamX");
  d.value_type_name = std::move(value_type);
  d.name = std::move(name);
  d.nick = std::string("Nick");
  d.blurb = std::string("Blurb");
  d.flags = 3;
  return d;
}

void gimp_wire_codec_round_trips_every_message() {
  // Every parameter definition kind in one registration.
  Message install;
  install.type = MessageType::ProcInstall;
  install.install.name = std::string("plug-in-test");
  install.install.type = ProcType::Plugin;
  auto run_mode = def(ParamDefKind::Enum, "run-mode", "GimpRunMode");
  run_mode.int32_default = 1;
  auto image = def(ParamDefKind::Id, "image", "GimpImage");
  auto drawables = def(ParamDefKind::IdArray, "drawables", "GimpCoreObjectArray");
  drawables.item_type = std::string("GimpDrawable");
  auto integer = def(ParamDefKind::Int, "amount", "gint");
  integer.int_min = -5;
  integer.int_max = 1LL << 40;
  integer.int_default = 7;
  auto real = def(ParamDefKind::Double, "radius", "gdouble");
  real.double_min = 0.5;
  real.double_max = 9.25;
  real.double_default = 1.125;
  auto choice = def(ParamDefKind::Choice, "mode", "gchararray");
  choice.string_default = std::string("b");
  choice.choices = {{std::string("a"), 0, std::string("A"), std::nullopt}, {std::string("b"), 1, std::string("B"), std::string("help")}};
  auto color = def(ParamDefKind::GeglColor, "color", "GeglColor");
  color.color_has_alpha = 1;
  color.color_default.bytes = {1, 2, 3, 4};
  color.color_default.encoding = std::string("R'G'B'A u8");
  auto text = def(ParamDefKind::String, "text", "gchararray");
  text.string_default = std::nullopt;
  auto unit = def(ParamDefKind::Unit, "unit", "GimpUnit");
  unit.unit_allow_pixels = 1;
  unit.int32_default = 2;
  auto resource = def(ParamDefKind::Resource, "brush", "GimpBrush");
  resource.resource_default_id = 9;
  auto file = def(ParamDefKind::File, "file", "GFile");
  file.file_action = 1;
  file.string_default = std::string("file:///x");
  auto boolean = def(ParamDefKind::Boolean, "flag", "gboolean");
  boolean.int32_default = 1;
  install.install.params = {run_mode, image, drawables, integer, real, choice, color, text, unit, resource, file, boolean,
                            def(ParamDefKind::Default, "other", "GObject"), def(ParamDefKind::ExportOptions, "opts", "GimpExportOptions")};
  install.install.return_values = {def(ParamDefKind::Curve, "curve", "GimpCurve")};
  const auto bytes = encode_message(install);
  const auto back = decode(bytes);
  CHECK(encode_message(back) == bytes);
  CHECK(back.install.params.size() == install.install.params.size());
  CHECK(back.install.params[3].int_max == (1LL << 40));
  CHECK(back.install.params[4].double_default == 1.125);
  CHECK(back.install.params[5].choices.size() == 2 && back.install.params[5].choices[1].help == std::optional<std::string>("help"));
  CHECK(back.install.params[7].string_default == std::nullopt);
  CHECK(is_image_procedure(back.install));

  // Every parameter value kind Patchy reads or writes.
  Message call;
  call.type = MessageType::ProcReturn;
  call.call.name = std::string("x");
  call.call.params.push_back(int_param("gint", -3));
  call.call.params.push_back(double_param("gdouble", 2.5));
  call.call.params.push_back(string_param("gchararray", std::string("\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88")));
  call.call.params.push_back(string_param("gchararray", std::nullopt));
  Param strv;
  strv.kind = ParamKind::Strv;
  strv.type_name = std::string("GStrv");
  strv.strings = {std::string("a"), std::nullopt, std::string("")};
  call.call.params.push_back(strv);
  call.call.params.push_back(bytes_param("GBytes", {0, 255, 7}));
  call.call.params.push_back(id_array_param("GimpCoreObjectArray", "GimpLayer", {2, 5}));
  Color c;
  c.bytes = {9, 8};
  c.encoding = std::string("Y u8");
  c.profile = {1, 2, 3};
  call.call.params.push_back(color_param(c));
  Param nested;
  nested.kind = ParamKind::ValueArray;
  nested.type_name = std::string("GimpValueArray");
  nested.values = {int_param("gint", 1), string_param("gchararray", std::string("v"))};
  call.call.params.push_back(nested);
  Param pdef;
  pdef.kind = ParamKind::ParamDef;
  pdef.type_name = std::string("GParam");
  pdef.param_def = {integer};
  call.call.params.push_back(pdef);
  Param parasite;
  parasite.kind = ParamKind::Parasite;
  parasite.type_name = std::string("GimpParasite");
  parasite.string_value = std::string("gimp-comment");
  parasite.int_value = 1;
  parasite.bytes = {'h', 'i', 0};
  call.call.params.push_back(parasite);
  const auto call_bytes = encode_message(call);
  CHECK(encode_message(decode(call_bytes)) == call_bytes);

  // Config, tiles and the bodiless messages.
  Message config;
  config.type = MessageType::Config;
  config.config.app_name = std::string("Patchy");
  config.config.check_custom_color1.bytes = std::vector<std::uint8_t>(32, 1);
  config.config.check_custom_color1.encoding = std::string("R'G'B'A double");
  config.config.default_display_id = 1;
  config.config.tile_cache_size = 1ull << 33;
  const auto config_bytes = encode_message(config);
  const auto config_back = decode(config_bytes);
  CHECK(encode_message(config_back) == config_bytes);
  CHECK(config_back.config.tile_cache_size == (1ull << 33) && config_back.config.app_name == std::optional<std::string>("Patchy"));

  Message tile;
  tile.type = MessageType::TileData;
  tile.tile_data.drawable_id = 2;
  tile.tile_data.tile_num = 3;
  tile.tile_data.bpp = 4;
  tile.tile_data.width = 2;
  tile.tile_data.height = 3;
  tile.tile_data.data.assign(24, 0x5A);
  const auto tile_bytes = encode_message(tile);
  CHECK(tile_bytes.size() == 4 + 7 * 4 + 24);
  CHECK(decode(tile_bytes).tile_data.data == tile.tile_data.data);
  for (const auto type : {MessageType::Quit, MessageType::TileAck, MessageType::ExtensionAck, MessageType::HasInit}) {
    Message bodiless;
    bodiless.type = type;
    CHECK(encode_message(bodiless).size() == 4);
    CHECK(decode(encode_message(bodiless)).type == type);
  }
}

void gimp_wire_codec_refuses_broken_streams() {
  const auto expect_error = [](const std::vector<std::uint8_t>& bytes) {
    try {
      SpanSource source(bytes);
      (void)read_message(source);
      return false;
    } catch (const WireError&) {
      return true;
    }
  };
  CHECK(expect_error({0, 0, 0, 99}));                                  // unknown message type
  CHECK(expect_error({0, 0, 0, 2, 0, 0}));                             // truncated tile request
  CHECK(expect_error({0, 0, 0, 5, 0xFF, 0xFF, 0xFF, 0xFF}));           // absurd string length
  CHECK(expect_error({0, 0, 0, 5, 0, 0, 0, 2, 'a', 'b'}));             // string without its NUL
  CHECK(expect_error({0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 15, 0, 0, 0, 0}));  // curve value
  CHECK(expect_error({0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 77}));              // unknown parameter type
  // A tile larger than any GIMP tile.
  CHECK(expect_error(from_hex("0000000400000002000000000000000000000004000100000001000000000000")));
}

ProcedureInfo image_procedure(std::string name) {
  ProcedureInfo p;
  p.install.name = std::move(name);
  auto run_mode = def(ParamDefKind::Enum, "run-mode", "GimpRunMode");
  auto image = def(ParamDefKind::Id, "image", "GimpImage");
  auto drawables = def(ParamDefKind::IdArray, "drawables", "GimpCoreObjectArray");
  drawables.item_type = std::string("GimpDrawable");
  p.install.params = {run_mode, image, drawables};
  p.menu_paths = {"<Image>/Filters/Distorts"};
  p.image_types = "RGB*, GRAY*";
  return p;
}

void gimp_procedure_classification_explains_refusals() {
  auto filter = image_procedure("plug-in-a");
  CHECK(classify_procedure(filter, false).use == ProcedureUse::LayerFilter);
  CHECK(classify_procedure(filter, true).reason == kReasonScriptFu);

  auto no_menu = filter;
  no_menu.menu_paths.clear();
  CHECK(classify_procedure(no_menu, false).use == ProcedureUse::Hidden);

  auto loader = filter;
  loader.file_handler = true;
  CHECK(classify_procedure(loader, false).reason == kReasonFileHandler);

  auto gray = filter;
  gray.image_types = "GRAY*, INDEXED*";
  CHECK(classify_procedure(gray, false).reason == kReasonNotRgb);

  auto several = filter;
  several.sensitivity_set = true;
  several.sensitivity_mask = kSensitiveDrawables;
  CHECK(classify_procedure(several, false).reason == kReasonSeveralLayers);

  auto no_image = filter;
  no_image.install.params.resize(1);
  CHECK(classify_procedure(no_image, false).reason == kReasonNotImageProcedure);

  auto image_menu = filter;
  image_menu.menu_paths = {"<Image>/Image/Crop"};
  CHECK(classify_procedure(image_menu, false).reason == kReasonMenuOutsideFilters);

  auto animation = filter;
  animation.menu_paths = {"<Image>/Filters/Animation"};
  CHECK(classify_procedure(animation, false).reason == kReasonAnimation);

  auto film = filter;
  film.install.name = std::string("plug-in-film");
  CHECK(classify_procedure(film, false).reason == kReasonSeveralImages);
  auto pagecurl = filter;
  pagecurl.install.name = std::string("plug-in-pagecurl");
  CHECK(classify_procedure(pagecurl, false).reason == kReasonNewLayer);

  auto colors = filter;
  colors.menu_paths = {"<Image>/Colors/Map"};
  CHECK(classify_procedure(colors, false).use == ProcedureUse::LayerFilter);
}

void gimp_procedure_menu_paths_and_image_types() {
  CHECK(image_types_accept_rgb("RGB*, GRAY*"));
  CHECK(image_types_accept_rgb("*"));
  CHECK(image_types_accept_rgb("RGBA"));
  CHECK(image_types_accept_rgb("gray, rgb"));
  CHECK(!image_types_accept_rgb("GRAY*, INDEXED*"));
  CHECK(!image_types_accept_rgb(""));
  CHECK(image_menu_segments("<Image>/Filters/Light and Shadow") == (std::vector<std::string>{"Filters", "Light and Shadow"}));
  CHECK(image_menu_segments("<Image>/_Colors/Ma_p/") == (std::vector<std::string>{"Colors", "Map"}));
  CHECK(image_menu_segments("<Load>/x").empty());
  CHECK(strip_mnemonic("_Blinds...") == "Blinds...");
  CHECK(strip_mnemonic("A__B") == "A_B");
}

void write_text(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << text;
}

void gimp_install_reads_installed_files_under_unicode_paths() {
  using patchy::test::kUnicodeCombinedStem;
  const auto root = patchy::test::unicode_artifact_dir(u8"gimp-install");
  const auto prefix = root / patchy::test::unicode_path_piece(kUnicodeCombinedStem);
  const auto lib = prefix / "lib" / "gimp" / "3.0";
  const auto bin = prefix / "bin";
  // The files as the GIMP 3.2.6 Windows installer writes them.
  write_text(lib / "interpreters" / "pygimp.interp",
             "python=python.exe\r\npython3=python.exe\r\n/usr/bin/python3=python.exe\r\n:Python:E::py::python3:\r\n");
  write_text(lib / "interpreters" / "pygimp_win.interp", "python3=pythonw.exe\r\n:Python:E::py::python3:\r\n");
  write_text(lib / "interpreters" / "script-fu.interp", ",ScriptFu,E,,scm,,gimp-script-fu-interpreter-3.0.exe,\r\n");
  write_text(lib / "environ" / "pygimp.env", "# comment\r\nPATH=${gimp_installation_dir}\\bin\r\nPYTHONDONTWRITEBYTECODE=1\r\n");
  write_text(bin / "pythonw.exe", "");
  write_text(bin / "gimp-3.2.exe", "");
  write_text(lib / "plug-ins" / "native" / "native.exe", "");  // Windows
  write_text(lib / "plug-ins" / "native" / "native", "");      // Linux and macOS
  write_text(lib / "plug-ins" / "py-filter" / "py-filter.py", "#!/usr/bin/env python3\n");
  write_text(lib / "plug-ins" / "py-filter" / "helper.py", "");
  write_text(lib / "plug-ins" / "mismatch" / "other.exe", "");
  write_text(lib / "plug-ins" / "native" / "native.pdb", "");

  const auto interpreters = load_interpreters({lib / "interpreters"});
  CHECK(interpreters.programs.at("python3") == "pythonw.exe");  // the later file wins
  CHECK(interpreters.by_extension.at("py") == "python3");
  CHECK(interpreters.by_extension.at("scm") == "gimp-script-fu-interpreter-3.0.exe");

  const auto environment = load_environ({lib / "environ"}, prefix);
  CHECK(environment.size() == 2);
  if (environment.size() == 2) {
    CHECK(environment[0].first == "PATH" && environment[0].second == patchy::path_to_utf8(prefix) + "\\bin");
  }

  const auto files = find_plugin_files(lib / "plug-ins", interpreters);
  CHECK(files.size() == 2);  // "mismatch" holds no file named after its folder

  std::string why;
#ifdef _WIN32
  const auto native = resolve_plugin_command(lib / "plug-ins" / "native" / "native.exe", interpreters, {bin}, &why);
  CHECK(native.has_value() && native->program == lib / "plug-ins" / "native" / "native.exe" && native->leading_args.empty());
#endif
  const auto python = resolve_plugin_command(lib / "plug-ins" / "py-filter" / "py-filter.py", interpreters, {bin}, &why);
  CHECK(python.has_value());
  if (python.has_value()) {
    CHECK(python->program == bin / "pythonw.exe");
    CHECK(python->leading_args.size() == 1 && python->interpreter == "python3");
  }
  const auto missing = resolve_plugin_command(lib / "plug-ins" / "py-filter" / "py-filter.py", interpreters, {}, &why);
  CHECK(!missing.has_value() && !why.empty());

  const auto user = root / "user";
  write_text(user / "gimprc", "# (plug-in-path \"ignored\")\n(plug-in-path \"${gimp_dir}/extra"
#ifdef _WIN32
                              ";"
#else
                              ":"
#endif
                              "${gimp_plug_in_dir}/more\")\n");
  // gimprc lists folders separated by ";" (":" off Windows), so a folder whose name
  // holds one cannot be listed there: this part uses a plain plug-in folder.
  const auto plain_lib = root / "lib";
  const auto extra = gimprc_plugin_path(user / "gimprc", user, plain_lib);
  CHECK(extra.size() == 2);
  if (extra.size() == 2) {
    CHECK(extra[0] == (user / "extra").lexically_normal());
    CHECK(extra[1] == (plain_lib / "more").lexically_normal());
  }

  const auto install = gimp_install_from_folder(bin);
  CHECK(install.has_value());
  if (install.has_value()) {
    CHECK(install->prefix == prefix);
    CHECK(install->series == "3.2");
    CHECK(!install->plugin_folders.empty() && install->plugin_folders.front() == lib / "plug-ins");
  }
  CHECK(!gimp_install_from_folder(root / "nowhere").has_value());
  patchy::test::remove_test_scratch_tree(root);
}

// GIMP's Flatpak: host paths of the app and of a plug-in extension map into the sandbox,
// scripts start through the sandbox's interpreter, and the flatpak (or flatpak-spawn)
// command line carries the sandbox command, environment and shared folder.
void gimp_flatpak_launch_runs_plugins_in_gimps_sandbox() {
  const auto root = patchy::test::unicode_artifact_dir(u8"gimp-flatpak");
  GimpInstall install;
  install.flatpak = true;
  install.flatpak_root = root / "flatpak";
  install.prefix = install.flatpak_root / "app" / "org.gimp.GIMP" / "current" / "active" / "files";
  install.lib_dir = install.prefix / "lib" / "gimp" / "3.0";
  install.data_dir = install.prefix / "share" / "gimp" / "3.0";
  install.user_dir = root / "home" / ".var" / "app" / "org.gimp.GIMP" / "config" / "GIMP" / "3.2";
  const auto generic = [](const std::filesystem::path& p) { return p.generic_string(); };

  CHECK(generic(flatpak_sandbox_path(install, install.lib_dir / "plug-ins" / "blinds" / "blinds")) ==
        "/app/lib/gimp/3.0/plug-ins/blinds/blinds");
  CHECK(generic(flatpak_sandbox_path(install, install.data_dir)) == "/app/share/gimp/3.0");
  const auto extension = install.flatpak_root / "runtime" / "org.gimp.GIMP.Plugin.Fourier" / "x86_64" / "3" / "active" /
                         "files" / "plug-ins" / "fourier" / "fourier";
  CHECK(generic(flatpak_sandbox_path(install, extension)) == "/app/extensions/Fourier/plug-ins/fourier/fourier");
  const auto user_plugin = install.user_dir / "plug-ins" / "mine" / "mine.py";
  CHECK(flatpak_sandbox_path(install, user_plugin) == user_plugin);  // the home folder is shared

  const auto script = install.lib_dir / "plug-ins" / "py-filter" / "py-filter.py";
  write_text(script, "#!/usr/bin/env python3\n");
  const auto scm = install.lib_dir / "plug-ins" / "sf" / "sf.scm";
  write_text(scm, "#!/usr/bin/env gimp-script-fu-interpreter-3.0\n");
  const Interpreters none;
#ifndef _WIN32
  // The Flatpak's .interp files map no extension: executable scripts are found by name.
  std::filesystem::permissions(script, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
  write_text(install.lib_dir / "plug-ins" / "not-run" / "not-run.py", "#!/usr/bin/env python3\n");
  const auto found = find_plugin_files(install.lib_dir / "plug-ins", none);
  CHECK(found.size() == 1 && found.front() == script);
#endif
  std::string why;
  const auto python = flatpak_plugin_command(install, script, none, &why);
  CHECK(python.has_value());
  if (python.has_value()) {
    CHECK(python->program == "python3" && python->interpreter == "python3");
    CHECK(python->leading_args.size() == 1 && python->leading_args[0] == "/app/lib/gimp/3.0/plug-ins/py-filter/py-filter.py");
  }
  const auto native = flatpak_plugin_command(install, install.lib_dir / "plug-ins" / "blinds" / "blinds", none, &why);
  CHECK(native.has_value() && generic(native->program) == "/app/lib/gimp/3.0/plug-ins/blinds/blinds" &&
        native->leading_args.empty());

  FlatpakLaunchOptions options;
  options.program = "/usr/bin/flatpak";
  options.environment = {"GIMP3_DIRECTORY=/home/u/profile", "LANG=de"};
  options.shared_folders = {"/home/u/profile"};
  const auto sf = flatpak_plugin_command(install, scm, none, &why);
  CHECK(sf.has_value());
  if (sf.has_value()) {
    const auto launch = flatpak_launch(install, *sf, options);
    CHECK(launch.program == "/usr/bin/flatpak");
    const std::vector<std::string> expected{"run",
                                            "--user",
                                            "--die-with-parent",
                                            "--cwd=/app/lib/gimp/3.0/plug-ins/sf",
                                            "--filesystem=/home/u/profile",
                                            "--env=GIMP3_DIRECTORY=/home/u/profile",
                                            "--env=LANG=de",
                                            "--command=gimp-script-fu-interpreter-3.0",
                                            "org.gimp.GIMP",
                                            "/app/lib/gimp/3.0/plug-ins/sf/sf.scm"};
    CHECK(launch.leading_args == expected);

    options.from_flatpak = true;
    options.program = "/usr/bin/flatpak-spawn";
    const auto spawned = flatpak_launch(install, *sf, options);
    CHECK(spawned.program == "/usr/bin/flatpak-spawn");
    CHECK(spawned.leading_args.size() == expected.size() + 5);
    if (spawned.leading_args.size() == expected.size() + 5) {
      const std::vector<std::string> head(spawned.leading_args.begin(), spawned.leading_args.begin() + 5);
      CHECK((head == std::vector<std::string>{"--host", "--watch-bus", "--forward-fd=3", "--forward-fd=4", "flatpak"}));
      CHECK(std::vector<std::string>(spawned.leading_args.begin() + 5, spawned.leading_args.end()) == expected);
    }
  }
  install.flatpak_root = "/var/lib/flatpak";
  if (native.has_value()) {
    const auto system_launch = flatpak_launch(install, *native, FlatpakLaunchOptions{});
    CHECK(system_launch.leading_args.size() > 1 && system_launch.leading_args[1] == "--system");
  }

  CHECK(flatpak_info_allows_host_commands("[Application]\nname=com.rtsoft.patchy\n\n[Session Bus Policy]\n"
                                          "org.freedesktop.Flatpak=talk\n"));
  CHECK(!flatpak_info_allows_host_commands("[Application]\nname=com.rtsoft.patchy\n[Session Bus Policy]\n"
                                           "org.freedesktop.Notifications=talk\n"));
  CHECK(!flatpak_info_allows_host_commands("[System Bus Policy]\norg.freedesktop.Flatpak=talk\n"));
  patchy::test::remove_test_scratch_tree(root);
}

void gimp_default_arguments_follow_registrations() {
  auto integer = def(ParamDefKind::Int, "n", "gint");
  integer.int_default = 1LL << 40;  // clamped to the wire's 32 bits
  CHECK(default_argument(integer, nullptr)->int_value == INT32_MAX);
  // No object, as GIMP passes for an object argument nobody set.
  auto layer = def(ParamDefKind::Id, "bump", "GimpDrawable");
  CHECK(default_argument(layer, nullptr)->int_value == -1);
  auto choice = def(ParamDefKind::Choice, "mode", "gchararray");
  choice.string_default = std::string("b");
  CHECK(default_argument(choice, nullptr)->string_value == std::optional<std::string>("b"));
  std::string why;
  CHECK(!default_argument(def(ParamDefKind::Curve, "c", "GimpCurve"), &why).has_value() && !why.empty());
  CHECK(default_argument(def(ParamDefKind::Default, "points", "GimpDoubleArray"), nullptr)->kind == ParamKind::Array);
  CHECK(!default_argument(def(ParamDefKind::Default, "thing", "GObject"), nullptr).has_value());
}

void gimp_host_link_round_trips_requests() {
  link::RunRequest request;
  request.launch.program = "C:\\GIMP 3\\lib\\x.exe";
  request.launch.leading_args = {"script.py"};
  request.launch.environment = {"A=1", "B=\xC3\xA9"};
  request.launch.unset = {"PYTHONHOME"};
  request.launch.data_dir = "d";
  request.launch.profile_dir = "p";
  request.procedure = "plug-in-x";
  request.install = {1, 2, 3};
  request.run_mode = 2;
  request.layer_width = 300;
  request.bit_depth = 16;
  request.has_mask = true;
  request.mask_width = 5;
  request.foreground[1] = 0.25;
  request.parent_window = 0x123456789ull;
  request.stored_data = {{"plug-in-x", {7, 8}}};
  request.wire_log = "log.bin";
  const auto frame = link::encode(request);
  link::FrameBuffer buffer;
  buffer.append(frame.data(), 5);  // frames arrive in pieces
  CHECK(!buffer.next().has_value());
  buffer.append(frame.data() + 5, frame.size() - 5);
  const auto got = buffer.next();
  CHECK(got.has_value() && got->first == link::Frame::RunRequest);
  if (got.has_value()) {
    const auto back = link::decode_run_request(got->second);
    CHECK(back.launch.environment == request.launch.environment && back.launch.profile_dir == "p");
    CHECK(back.procedure == "plug-in-x" && back.run_mode == 2 && back.layer_width == 300 && back.bit_depth == 16);
    CHECK(back.has_mask && back.mask_width == 5 && back.foreground[1] == 0.25 && back.parent_window == 0x123456789ull);
    CHECK(back.stored_data.size() == 1 && back.stored_data[0].second == (std::vector<std::uint8_t>{7, 8}));
    CHECK(link::encode(back) == frame);
  }
  link::RunResult result;
  result.status = link::RunStatus::Error;
  result.code = "unsupported-procedure";
  result.detail = "gimp-layer-new";
  result.unsupported_calls = {"gimp-layer-new"};
  const auto result_frame = link::encode(result);
  const auto decoded = link::decode_run_result(std::span(result_frame).subspan(8));
  CHECK(decoded.code == result.code && decoded.detail == result.detail && decoded.unsupported_calls == result.unsupported_calls);
}

}  // namespace

std::vector<patchy::test::TestCase> gimp_plugin_tests() {
  return {
      {"gimp_wire_codec_reads_observed_gimp_messages", gimp_wire_codec_reads_observed_gimp_messages},
      {"gimp_wire_codec_round_trips_every_message", gimp_wire_codec_round_trips_every_message},
      {"gimp_wire_codec_refuses_broken_streams", gimp_wire_codec_refuses_broken_streams},
      {"gimp_procedure_classification_explains_refusals", gimp_procedure_classification_explains_refusals},
      {"gimp_procedure_menu_paths_and_image_types", gimp_procedure_menu_paths_and_image_types},
      {"gimp_install_reads_installed_files_under_unicode_paths", gimp_install_reads_installed_files_under_unicode_paths},
      {"gimp_flatpak_launch_runs_plugins_in_gimps_sandbox", gimp_flatpak_launch_runs_plugins_in_gimps_sandbox},
      {"gimp_default_arguments_follow_registrations", gimp_default_arguments_follow_registrations},
      {"gimp_host_link_round_trips_requests", gimp_host_link_round_trips_requests},
  };
}
