// gimp-drawable-*: geometry, format, the selection bounds, shadow merging, updates and
// thumbnails of the layer. Tiles themselves travel as TILE_REQ/TILE_DATA (session.cpp).

#include "plugins/gimp/host/pdb.hpp"
#include "plugins/gimp/host/session.hpp"

namespace patchy::gimp::host {

namespace {

ImageModel* drawable(Session& s, const Args& a, std::size_t index = 0) {
  auto* m = s.model();
  return m != nullptr && m->valid_drawable(a.i(index)) ? m : nullptr;
}

PdbReply thumbnail_reply(const ImageModel::Thumbnail& t) {
  if (t.data.empty()) return pdb_error();
  return pdb_ok({pdb_int(t.width), pdb_int(t.height), pdb_int(t.bpp), bytes_param("GBytes", t.data)});
}

}  // namespace

void register_drawable_procedures(PdbTable& t) {
  t.add("gimp-drawable-get-width", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    return m ? pdb_ok({pdb_int(m->drawable_width(a.i(0)))}) : pdb_error();
  });
  t.add("gimp-drawable-get-height", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    return m ? pdb_ok({pdb_int(m->drawable_height(a.i(0)))}) : pdb_error();
  });
  t.add("gimp-drawable-get-bpp", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    return m ? pdb_ok({pdb_int(static_cast<std::int32_t>(m->drawable_bpp(a.i(0))))}) : pdb_error();
  });
  t.add("gimp-drawable-get-format", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    return m ? pdb_ok({pdb_string(m->drawable_format(a.i(0)))}) : pdb_error();
  });
  t.add("gimp-drawable-get-thumbnail-format", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    if (m == nullptr) return pdb_error();
    return pdb_ok({pdb_string(a.i(0) == kSelectionId ? "Y' u8" : m->has_alpha() ? "R'G'B'A u8" : "R'G'B' u8")});
  });
  t.add("gimp-drawable-get-offsets", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    if (m == nullptr) return pdb_error();
    const bool layer = a.i(0) == kLayerId;
    return pdb_ok({pdb_int(layer ? m->layer_x() : 0), pdb_int(layer ? m->layer_y() : 0)});
  });
  t.add("gimp-drawable-has-alpha", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    return m ? pdb_ok({pdb_bool(a.i(0) == kLayerId && m->has_alpha())}) : pdb_error();
  });
  t.add("gimp-drawable-is-rgb", [](Session& s, const Args& a) {
    return drawable(s, a) ? pdb_ok({pdb_bool(a.i(0) == kLayerId)}) : pdb_error();
  });
  t.add("gimp-drawable-is-gray", [](Session& s, const Args& a) {
    return drawable(s, a) ? pdb_ok({pdb_bool(a.i(0) == kSelectionId)}) : pdb_error();
  });
  t.add("gimp-drawable-is-indexed", [](Session& s, const Args& a) {
    return drawable(s, a) ? pdb_ok({pdb_bool(false)}) : pdb_error();
  });
  t.add("gimp-drawable-type", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    if (m == nullptr) return pdb_error();
    const int type = a.i(0) == kSelectionId ? 2 : m->has_alpha() ? 1 : 0;  // GimpImageType
    return pdb_ok({int_param("GimpImageType", type)});
  });
  t.add("gimp-drawable-type-with-alpha", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    return m ? pdb_ok({int_param("GimpImageType", a.i(0) == kSelectionId ? 3 : 1)}) : pdb_error();
  });
  t.add("gimp-drawable-mask-intersect", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    if (m == nullptr) return pdb_error();
    if (a.i(0) == kSelectionId) {
      return pdb_ok({pdb_bool(true), pdb_int(0), pdb_int(0), pdb_int(m->image_width()), pdb_int(m->image_height())});
    }
    const auto r = m->selection_bounds();
    return pdb_ok({pdb_bool(!r.empty()), pdb_int(r.x), pdb_int(r.y), pdb_int(r.width), pdb_int(r.height)});
  });
  t.add("gimp-drawable-mask-bounds", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    if (m == nullptr) return pdb_error();
    const auto r = a.i(0) == kSelectionId ? PixelRect{0, 0, m->image_width(), m->image_height()} : m->selection_bounds();
    return pdb_ok({pdb_bool(m->has_selection()), pdb_int(r.x), pdb_int(r.y), pdb_int(r.x + r.width), pdb_int(r.y + r.height)});
  });
  t.add("gimp-drawable-merge-shadow", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    if (m == nullptr || a.i(0) != kLayerId) return pdb_error();
    m->merge_shadow();
    return pdb_ok();
  });
  t.add("gimp-drawable-free-shadow", [](Session& s, const Args& a) {
    return drawable(s, a) ? pdb_ok() : pdb_error();
  });
  t.add("gimp-drawable-update", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    if (m == nullptr) return pdb_error();
    if (a.i(0) == kLayerId) m->mark_update({a.i(1), a.i(2), a.i(3), a.i(4)});
    return pdb_ok();
  });
  t.add("gimp-drawable-thumbnail", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    if (m == nullptr) return pdb_error();
    const auto id = a.i(0);
    return thumbnail_reply(m->thumbnail(id, {0, 0, m->drawable_width(id), m->drawable_height(id)}, a.i(1), a.i(2)));
  });
  t.add("gimp-drawable-sub-thumbnail", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    if (m == nullptr) return pdb_error();
    return thumbnail_reply(m->thumbnail(a.i(0), {a.i(1), a.i(2), a.i(3), a.i(4)}, a.i(5), a.i(6)));
  });
  t.add("gimp-drawable-get-pixel", [](Session& s, const Args& a) {
    auto* m = drawable(s, a);
    if (m == nullptr || a.i(0) != kLayerId) return pdb_error();
    const auto px = m->pixel(a.i(1), a.i(2));
    if (px.empty()) return pdb_error();
    Color c;
    c.bytes = px;
    c.encoding = m->format_name();
    if (s.request() != nullptr) c.profile = s.request()->color_profile;
    return pdb_ok({color_param(std::move(c))});
  });
}

}  // namespace patchy::gimp::host
