// GIMP 3 wire codec (docs/gimp-plugins.md, "Protocol"). Patchy's own code: field order
// from the gimpprotocol.h layouts, framing from observed GIMP 3.2.6 traffic
// (docs/gimp-plugins-provenance.md). Records not yet seen in any observed traffic decode
// from the header layout with the same framing; CURVE values are refused outright.

#include "plugins/gimp/gimp_wire_codec.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <sstream>
#include <utility>

namespace patchy::gimp {

void SpanSource::read_exact(std::span<std::uint8_t> out) {
  if (out.size() > data_.size() - at_) {
    throw WireError("the message ends early");
  }
  if (!out.empty()) {
    std::memcpy(out.data(), data_.data() + at_, out.size());
  }
  at_ += out.size();
}

void FunctionSource::read_exact(std::span<std::uint8_t> out) {
  std::size_t done = 0;
  while (done < out.size()) {
    const auto got = read_(out.data() + done, out.size() - done);
    if (got == 0) {
      throw WireError("the plug-in closed its connection");
    }
    if (got < 0) {
      throw WireError("reading from the plug-in failed");
    }
    done += static_cast<std::size_t>(got);
  }
}

namespace {

class Reader {
public:
  explicit Reader(ByteSource& source) : source_(source) {}

  std::uint8_t u8() {
    std::uint8_t v = 0;
    source_.read_exact({&v, 1});
    return v;
  }
  std::uint32_t u32() {
    std::array<std::uint8_t, 4> b{};
    source_.read_exact(b);
    return (std::uint32_t{b[0]} << 24) | (std::uint32_t{b[1]} << 16) | (std::uint32_t{b[2]} << 8) | b[3];
  }
  std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
  std::uint64_t u64() {
    const std::uint64_t hi = u32();
    return (hi << 32) | u32();
  }
  std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
  double f64() { return std::bit_cast<double>(u64()); }

  WireString string() {
    const auto n = u32();
    if (n == 0) {
      return std::nullopt;
    }
    if (n > kMaxWireString) {
      throw WireError("a string in the message is too long");
    }
    std::string s(n, '\0');
    source_.read_exact({reinterpret_cast<std::uint8_t*>(s.data()), s.size()});
    if (s.back() != '\0') {
      throw WireError("a string in the message is not terminated");
    }
    s.pop_back();
    return s;
  }
  std::vector<std::uint8_t> blob(std::uint32_t limit = kMaxWireString) {
    const auto n = u32();
    if (n > limit) {
      throw WireError("a byte array in the message is too long");
    }
    std::vector<std::uint8_t> v(n);
    source_.read_exact(v);
    return v;
  }
  std::uint32_t count() {
    const auto n = u32();
    if (n > kMaxWireCount) {
      throw WireError("a list in the message is too long");
    }
    return n;
  }
  void bytes(std::span<std::uint8_t> out) { source_.read_exact(out); }

private:
  ByteSource& source_;
};

class Writer {
public:
  explicit Writer(std::vector<std::uint8_t>& out) : out_(out) {}
  void u8(std::uint8_t v) { out_.push_back(v); }
  void i8(std::int8_t v) { out_.push_back(static_cast<std::uint8_t>(v)); }
  void u32(std::uint32_t v) {
    out_.push_back(static_cast<std::uint8_t>(v >> 24));
    out_.push_back(static_cast<std::uint8_t>(v >> 16));
    out_.push_back(static_cast<std::uint8_t>(v >> 8));
    out_.push_back(static_cast<std::uint8_t>(v));
  }
  void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
  void u64(std::uint64_t v) {
    u32(static_cast<std::uint32_t>(v >> 32));
    u32(static_cast<std::uint32_t>(v));
  }
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
  void f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }
  void string(const WireString& s) {
    if (!s.has_value()) {
      u32(0);
      return;
    }
    u32(static_cast<std::uint32_t>(s->size() + 1));
    out_.insert(out_.end(), s->begin(), s->end());
    out_.push_back(0);
  }
  void blob(std::span<const std::uint8_t> v) {
    u32(static_cast<std::uint32_t>(v.size()));
    out_.insert(out_.end(), v.begin(), v.end());
  }
  void raw(std::span<const std::uint8_t> v) { out_.insert(out_.end(), v.begin(), v.end()); }

private:
  std::vector<std::uint8_t>& out_;
};

// A color: pixel bytes, then the babl encoding, then the ICC profile (observed in the
// config message, PDB color returns and color parameter definitions alike).
Color read_color(Reader& r) {
  Color c;
  c.bytes = r.blob(40);
  c.encoding = r.string();
  c.profile = r.blob();
  return c;
}

void write_color(Writer& w, const Color& c) {
  w.blob(c.bytes);
  w.string(c.encoding);
  w.blob(c.profile);
}

ParamDef read_param_def(Reader& r) {
  ParamDef d;
  const auto kind = r.u32();
  if (kind > static_cast<std::uint32_t>(ParamDefKind::Curve)) {
    throw WireError("unknown parameter definition type " + std::to_string(kind));
  }
  d.kind = static_cast<ParamDefKind>(kind);
  d.type_name = r.string();
  d.value_type_name = r.string();
  d.name = r.string();
  d.nick = r.string();
  d.blurb = r.string();
  d.flags = r.u32();
  switch (d.kind) {
    case ParamDefKind::Default:
    case ParamDefKind::ExportOptions:
      break;
    case ParamDefKind::Int:
      d.int_min = r.i64();
      d.int_max = r.i64();
      d.int_default = r.i64();
      break;
    case ParamDefKind::Unit:
      d.unit_allow_pixels = r.i32();
      d.unit_allow_percent = r.i32();
      d.int32_default = r.i32();
      break;
    case ParamDefKind::Enum:
    case ParamDefKind::Boolean:
      d.int32_default = r.i32();
      break;
    case ParamDefKind::Choice: {
      // Observed: the default nick first, then the choices.
      d.string_default = r.string();
      const auto n = r.count();
      d.choices.reserve(n);
      for (std::uint32_t i = 0; i < n; ++i) {
        Choice c;
        c.nick = r.string();
        c.id = r.i32();
        c.label = r.string();
        c.help = r.string();
        d.choices.push_back(std::move(c));
      }
      break;
    }
    case ParamDefKind::Double:
      d.double_min = r.f64();
      d.double_max = r.f64();
      d.double_default = r.f64();
      break;
    case ParamDefKind::String:
      d.string_default = r.string();
      break;
    case ParamDefKind::GeglColor:
      d.color_has_alpha = r.i32();
      d.color_default = read_color(r);
      break;
    case ParamDefKind::Id:
    case ParamDefKind::Curve:
      d.none_ok = r.i32();
      break;
    case ParamDefKind::IdArray:
      d.item_type = r.string();
      break;
    case ParamDefKind::Resource:
      d.none_ok = r.i32();
      d.resource_default_to_context = r.i32();
      d.resource_default_id = r.i32();
      break;
    case ParamDefKind::File:
      d.file_action = r.i32();
      d.none_ok = r.i32();
      d.string_default = r.string();
      break;
  }
  return d;
}

void write_param_def(Writer& w, const ParamDef& d) {
  w.u32(static_cast<std::uint32_t>(d.kind));
  w.string(d.type_name);
  w.string(d.value_type_name);
  w.string(d.name);
  w.string(d.nick);
  w.string(d.blurb);
  w.u32(d.flags);
  switch (d.kind) {
    case ParamDefKind::Default:
    case ParamDefKind::ExportOptions:
      break;
    case ParamDefKind::Int:
      w.i64(d.int_min);
      w.i64(d.int_max);
      w.i64(d.int_default);
      break;
    case ParamDefKind::Unit:
      w.i32(d.unit_allow_pixels);
      w.i32(d.unit_allow_percent);
      w.i32(d.int32_default);
      break;
    case ParamDefKind::Enum:
    case ParamDefKind::Boolean:
      w.i32(d.int32_default);
      break;
    case ParamDefKind::Choice:
      w.string(d.string_default);
      w.u32(static_cast<std::uint32_t>(d.choices.size()));
      for (const auto& c : d.choices) {
        w.string(c.nick);
        w.i32(c.id);
        w.string(c.label);
        w.string(c.help);
      }
      break;
    case ParamDefKind::Double:
      w.f64(d.double_min);
      w.f64(d.double_max);
      w.f64(d.double_default);
      break;
    case ParamDefKind::String:
      w.string(d.string_default);
      break;
    case ParamDefKind::GeglColor:
      w.i32(d.color_has_alpha);
      write_color(w, d.color_default);
      break;
    case ParamDefKind::Id:
    case ParamDefKind::Curve:
      w.i32(d.none_ok);
      break;
    case ParamDefKind::IdArray:
      w.string(d.item_type);
      break;
    case ParamDefKind::Resource:
      w.i32(d.none_ok);
      w.i32(d.resource_default_to_context);
      w.i32(d.resource_default_id);
      break;
    case ParamDefKind::File:
      w.i32(d.file_action);
      w.i32(d.none_ok);
      w.string(d.string_default);
      break;
  }
}

const char* param_kind_name(std::uint32_t kind) {
  static constexpr const char* kNames[] = {"INT",         "DOUBLE",   "STRING",         "STRV",      "BYTES",
                                           "FILE",        "BABL_FORMAT", "GEGL_COLOR",  "COLOR_ARRAY", "PARASITE",
                                           "ARRAY",       "ID_ARRAY", "EXPORT_OPTIONS", "PARAM_DEF", "VALUE_ARRAY",
                                           "CURVE"};
  return kind < std::size(kNames) ? kNames[kind] : "unknown";
}

Param read_param(Reader& r, int depth) {
  if (depth > 8) {
    throw WireError("parameter values nest too deeply");
  }
  Param p;
  const auto kind = r.u32();
  if (kind > static_cast<std::uint32_t>(ParamKind::Curve)) {
    throw WireError("unknown parameter type " + std::to_string(kind));
  }
  p.kind = static_cast<ParamKind>(kind);
  p.type_name = r.string();
  switch (p.kind) {
    case ParamKind::Int:
      p.int_value = r.i32();
      break;
    case ParamKind::Double:
      p.double_value = r.f64();
      break;
    case ParamKind::String:
    case ParamKind::File:
      p.string_value = r.string();
      break;
    case ParamKind::Strv: {
      const auto n = r.count();
      for (std::uint32_t i = 0; i < n; ++i) {
        p.strings.push_back(r.string());
      }
      break;
    }
    case ParamKind::Bytes:
    case ParamKind::Array:
      p.bytes = r.blob();
      break;
    case ParamKind::BablFormat:
      p.format_encoding = r.string();
      p.format_profile = r.blob();
      break;
    case ParamKind::GeglColor:
      p.color = read_color(r);
      break;
    case ParamKind::ColorArray: {
      const auto n = r.count();
      for (std::uint32_t i = 0; i < n; ++i) {
        p.colors.push_back(read_color(r));
      }
      break;
    }
    case ParamKind::Parasite: {
      // GimpParasite (public gimpparasite.h): name, flags, size, data.
      p.string_value = r.string();
      if (p.string_value.has_value()) {
        p.int_value = r.i32();
        p.bytes = r.blob();
      }
      break;
    }
    case ParamKind::IdArray: {
      p.item_type = r.string();
      const auto n = r.count();
      p.ids.reserve(n);
      for (std::uint32_t i = 0; i < n; ++i) {
        p.ids.push_back(r.i32());
      }
      break;
    }
    case ParamKind::ExportOptions:
      break;
    case ParamKind::ParamDef:
      p.param_def.push_back(read_param_def(r));
      break;
    case ParamKind::ValueArray: {
      const auto n = r.count();
      for (std::uint32_t i = 0; i < n; ++i) {
        p.values.push_back(read_param(r, depth + 1));
      }
      break;
    }
    case ParamKind::Curve:
      throw WireError("GIMP curve values are not supported by Patchy yet");
  }
  return p;
}

void write_param(Writer& w, const Param& p) {
  w.u32(static_cast<std::uint32_t>(p.kind));
  w.string(p.type_name);
  switch (p.kind) {
    case ParamKind::Int:
      w.i32(p.int_value);
      break;
    case ParamKind::Double:
      w.f64(p.double_value);
      break;
    case ParamKind::String:
    case ParamKind::File:
      w.string(p.string_value);
      break;
    case ParamKind::Strv:
      w.u32(static_cast<std::uint32_t>(p.strings.size()));
      for (const auto& s : p.strings) {
        w.string(s);
      }
      break;
    case ParamKind::Bytes:
    case ParamKind::Array:
      w.blob(p.bytes);
      break;
    case ParamKind::BablFormat:
      w.string(p.format_encoding);
      w.blob(p.format_profile);
      break;
    case ParamKind::GeglColor:
      write_color(w, p.color);
      break;
    case ParamKind::ColorArray:
      w.u32(static_cast<std::uint32_t>(p.colors.size()));
      for (const auto& c : p.colors) {
        write_color(w, c);
      }
      break;
    case ParamKind::Parasite:
      w.string(p.string_value);
      if (p.string_value.has_value()) {
        w.i32(p.int_value);
        w.blob(p.bytes);
      }
      break;
    case ParamKind::IdArray:
      w.string(p.item_type);
      w.u32(static_cast<std::uint32_t>(p.ids.size()));
      for (const auto id : p.ids) {
        w.i32(id);
      }
      break;
    case ParamKind::ExportOptions:
      break;
    case ParamKind::ParamDef:
      if (p.param_def.size() != 1) {
        throw WireError("a parameter definition value needs exactly one definition");
      }
      write_param_def(w, p.param_def.front());
      break;
    case ParamKind::ValueArray:
      w.u32(static_cast<std::uint32_t>(p.values.size()));
      for (const auto& v : p.values) {
        write_param(w, v);
      }
      break;
    case ParamKind::Curve:
      throw WireError("GIMP curve values are not supported by Patchy yet");
  }
}

ProcCall read_call(Reader& r) {
  ProcCall c;
  c.name = r.string();
  const auto n = r.count();
  c.params.reserve(n);
  for (std::uint32_t i = 0; i < n; ++i) {
    c.params.push_back(read_param(r, 0));
  }
  return c;
}

void write_call(Writer& w, const ProcCall& c) {
  w.string(c.name);
  w.u32(static_cast<std::uint32_t>(c.params.size()));
  for (const auto& p : c.params) {
    write_param(w, p);
  }
}

}  // namespace

Message read_message(ByteSource& source) {
  Reader r(source);
  Message m;
  const auto type = r.u32();
  if (type > static_cast<std::uint32_t>(MessageType::HasInit)) {
    throw WireError("unknown message type " + std::to_string(type));
  }
  m.type = static_cast<MessageType>(type);
  switch (m.type) {
    case MessageType::Quit:
    case MessageType::TileAck:
    case MessageType::ExtensionAck:
    case MessageType::HasInit:
      break;
    case MessageType::Config: {
      auto& c = m.config;
      c.tile_width = r.u32();
      c.tile_height = r.u32();
      c.shm_id = r.i32();
      c.check_size = static_cast<std::int8_t>(r.u8());
      c.check_type = static_cast<std::int8_t>(r.u8());
      // Observed: the two check colors (protocol 0x0111 on) come before the flag bytes.
      c.check_custom_color1 = read_color(r);
      c.check_custom_color2 = read_color(r);
      c.show_help_button = static_cast<std::int8_t>(r.u8());
      c.use_cpu_accel = static_cast<std::int8_t>(r.u8());
      c.use_opencl = static_cast<std::int8_t>(r.u8());
      c.export_color_profile = static_cast<std::int8_t>(r.u8());
      c.export_comment = static_cast<std::int8_t>(r.u8());
      c.export_exif = static_cast<std::int8_t>(r.u8());
      c.export_xmp = static_cast<std::int8_t>(r.u8());
      c.export_iptc = static_cast<std::int8_t>(r.u8());
      c.update_metadata = static_cast<std::int8_t>(r.u8());
      c.default_display_id = r.i32();
      c.app_name = r.string();
      c.wm_class = r.string();
      c.display_name = r.string();
      c.monitor_number = r.i32();
      c.timestamp = r.u32();
      c.icon_theme_dir = r.string();
      c.tile_cache_size = r.u64();
      c.swap_path = r.string();
      c.swap_compression = r.string();
      c.num_processors = r.i32();
      break;
    }
    case MessageType::TileRequest:
      m.tile_request.drawable_id = r.i32();
      m.tile_request.tile_num = r.u32();
      m.tile_request.shadow = r.u32();
      break;
    case MessageType::TileData: {
      auto& t = m.tile_data;
      t.drawable_id = r.i32();
      t.tile_num = r.u32();
      t.shadow = r.u32();
      t.bpp = r.u32();
      t.width = r.u32();
      t.height = r.u32();
      t.use_shm = r.u32();
      if (t.use_shm == 0) {
        const std::uint64_t size = std::uint64_t{t.width} * t.height * t.bpp;
        if (size > kMaxTileBytes) {
          throw WireError("a tile in the message is too large");
        }
        t.data.resize(static_cast<std::size_t>(size));
        r.bytes(t.data);
      }
      break;
    }
    case MessageType::ProcRun:
    case MessageType::ProcReturn:
    case MessageType::TempProcRun:
    case MessageType::TempProcReturn:
      m.call = read_call(r);
      break;
    case MessageType::ProcInstall: {
      auto& p = m.install;
      p.name = r.string();
      p.type = static_cast<ProcType>(r.u32());
      const auto n = r.count();
      const auto rv = r.count();
      for (std::uint32_t i = 0; i < n; ++i) {
        p.params.push_back(read_param_def(r));
      }
      for (std::uint32_t i = 0; i < rv; ++i) {
        p.return_values.push_back(read_param_def(r));
      }
      break;
    }
    case MessageType::ProcUninstall:
      m.uninstall_name = r.string();
      break;
  }
  return m;
}

void append_message(std::vector<std::uint8_t>& out, const Message& m) {
  Writer w(out);
  w.u32(static_cast<std::uint32_t>(m.type));
  switch (m.type) {
    case MessageType::Quit:
    case MessageType::TileAck:
    case MessageType::ExtensionAck:
    case MessageType::HasInit:
      break;
    case MessageType::Config: {
      const auto& c = m.config;
      w.u32(c.tile_width);
      w.u32(c.tile_height);
      w.i32(c.shm_id);
      w.i8(c.check_size);
      w.i8(c.check_type);
      write_color(w, c.check_custom_color1);
      write_color(w, c.check_custom_color2);
      w.i8(c.show_help_button);
      w.i8(c.use_cpu_accel);
      w.i8(c.use_opencl);
      w.i8(c.export_color_profile);
      w.i8(c.export_comment);
      w.i8(c.export_exif);
      w.i8(c.export_xmp);
      w.i8(c.export_iptc);
      w.i8(c.update_metadata);
      w.i32(c.default_display_id);
      w.string(c.app_name);
      w.string(c.wm_class);
      w.string(c.display_name);
      w.i32(c.monitor_number);
      w.u32(c.timestamp);
      w.string(c.icon_theme_dir);
      w.u64(c.tile_cache_size);
      w.string(c.swap_path);
      w.string(c.swap_compression);
      w.i32(c.num_processors);
      break;
    }
    case MessageType::TileRequest:
      w.i32(m.tile_request.drawable_id);
      w.u32(m.tile_request.tile_num);
      w.u32(m.tile_request.shadow);
      break;
    case MessageType::TileData: {
      const auto& t = m.tile_data;
      w.i32(t.drawable_id);
      w.u32(t.tile_num);
      w.u32(t.shadow);
      w.u32(t.bpp);
      w.u32(t.width);
      w.u32(t.height);
      w.u32(t.use_shm);
      if (t.use_shm == 0) {
        const std::uint64_t size = std::uint64_t{t.width} * t.height * t.bpp;
        if (t.data.size() != size) {
          throw WireError("tile data does not match its size");
        }
        w.raw(t.data);
      }
      break;
    }
    case MessageType::ProcRun:
    case MessageType::ProcReturn:
    case MessageType::TempProcRun:
    case MessageType::TempProcReturn:
      write_call(w, m.call);
      break;
    case MessageType::ProcInstall:
      w.string(m.install.name);
      w.u32(static_cast<std::uint32_t>(m.install.type));
      w.u32(static_cast<std::uint32_t>(m.install.params.size()));
      w.u32(static_cast<std::uint32_t>(m.install.return_values.size()));
      for (const auto& d : m.install.params) {
        write_param_def(w, d);
      }
      for (const auto& d : m.install.return_values) {
        write_param_def(w, d);
      }
      break;
    case MessageType::ProcUninstall:
      w.string(m.uninstall_name);
      break;
  }
}

std::vector<std::uint8_t> encode_message(const Message& message) {
  std::vector<std::uint8_t> out;
  append_message(out, message);
  return out;
}

std::string describe_param(const Param& p) {
  std::ostringstream s;
  s << param_kind_name(static_cast<std::uint32_t>(p.kind)) << ' ' << p.type_name.value_or("(null)") << '=';
  switch (p.kind) {
    case ParamKind::Int:
      s << p.int_value;
      break;
    case ParamKind::Double:
      s << p.double_value;
      break;
    case ParamKind::String:
    case ParamKind::File:
      s << (p.string_value ? '"' + *p.string_value + '"' : std::string("NULL"));
      break;
    case ParamKind::IdArray:
      s << p.item_type.value_or("") << '[';
      for (std::size_t i = 0; i < p.ids.size(); ++i) {
        s << (i ? "," : "") << p.ids[i];
      }
      s << ']';
      break;
    case ParamKind::Strv:
      s << p.strings.size() << " strings";
      break;
    case ParamKind::Bytes:
    case ParamKind::Array:
      s << p.bytes.size() << " bytes";
      break;
    case ParamKind::GeglColor:
      s << p.color.encoding.value_or("") << ' ' << p.color.bytes.size() << " bytes";
      break;
    case ParamKind::ParamDef:
      s << (p.param_def.empty() ? std::string() : p.param_def.front().name.value_or(""));
      break;
    default:
      s << "...";
      break;
  }
  return s.str();
}

std::string describe_message(const Message& m) {
  std::string out = message_type_name(m.type);
  switch (m.type) {
    case MessageType::ProcRun:
    case MessageType::ProcReturn:
    case MessageType::TempProcRun:
    case MessageType::TempProcReturn:
      out += ' ' + m.call.name.value_or("(null)") + '(';
      for (std::size_t i = 0; i < m.call.params.size(); ++i) {
        out += (i ? ", " : "") + describe_param(m.call.params[i]);
      }
      out += ')';
      break;
    case MessageType::ProcInstall:
      out += ' ' + m.install.name.value_or("(null)");
      break;
    case MessageType::TileRequest:
      out += " drawable " + std::to_string(m.tile_request.drawable_id) + " tile " +
             std::to_string(m.tile_request.tile_num) + (m.tile_request.shadow ? " shadow" : "");
      break;
    case MessageType::TileData:
      out += " drawable " + std::to_string(m.tile_data.drawable_id) + " tile " + std::to_string(m.tile_data.tile_num) +
             ' ' + std::to_string(m.tile_data.width) + 'x' + std::to_string(m.tile_data.height) + 'x' +
             std::to_string(m.tile_data.bpp);
      break;
    default:
      break;
  }
  return out;
}

const char* message_type_name(MessageType type) noexcept {
  switch (type) {
    case MessageType::Quit: return "QUIT";
    case MessageType::Config: return "CONFIG";
    case MessageType::TileRequest: return "TILE_REQ";
    case MessageType::TileAck: return "TILE_ACK";
    case MessageType::TileData: return "TILE_DATA";
    case MessageType::ProcRun: return "PROC_RUN";
    case MessageType::ProcReturn: return "PROC_RETURN";
    case MessageType::TempProcRun: return "TEMP_PROC_RUN";
    case MessageType::TempProcReturn: return "TEMP_PROC_RETURN";
    case MessageType::ProcInstall: return "PROC_INSTALL";
    case MessageType::ProcUninstall: return "PROC_UNINSTALL";
    case MessageType::ExtensionAck: return "EXTENSION_ACK";
    case MessageType::HasInit: return "HAS_INIT";
  }
  return "UNKNOWN";
}

Param int_param(std::string type_name, std::int32_t value) {
  Param p;
  p.kind = ParamKind::Int;
  p.type_name = std::move(type_name);
  p.int_value = value;
  return p;
}

Param double_param(std::string type_name, double value) {
  Param p;
  p.kind = ParamKind::Double;
  p.type_name = std::move(type_name);
  p.double_value = value;
  return p;
}

Param string_param(std::string type_name, WireString value) {
  Param p;
  p.kind = ParamKind::String;
  p.type_name = std::move(type_name);
  p.string_value = std::move(value);
  return p;
}

Param bytes_param(std::string type_name, std::vector<std::uint8_t> value) {
  Param p;
  p.kind = ParamKind::Bytes;
  p.type_name = std::move(type_name);
  p.bytes = std::move(value);
  return p;
}

Param id_array_param(std::string type_name, std::string item_type, std::vector<std::int32_t> ids) {
  Param p;
  p.kind = ParamKind::IdArray;
  p.type_name = std::move(type_name);
  p.item_type = std::move(item_type);
  p.ids = std::move(ids);
  return p;
}

Param color_param(Color color) {
  Param p;
  p.kind = ParamKind::GeglColor;
  p.type_name = std::string("GeglColor");
  p.color = std::move(color);
  return p;
}

Param status_param(PdbStatus status) { return int_param("GimpPDBStatusType", static_cast<std::int32_t>(status)); }

}  // namespace patchy::gimp
