// gimp-selection-*: read-only views of Patchy's selection. Changing the selection is an
// image operation Patchy does not offer plug-ins.

#include "plugins/gimp/host/pdb.hpp"
#include "plugins/gimp/host/session.hpp"

namespace patchy::gimp::host {

void register_selection_procedures(PdbTable& t) {
  t.add("gimp-selection-is-empty", [](Session& s, const Args& a) {
    auto* m = s.model();
    if (m == nullptr || a.i(0) != kImageId) return pdb_error();
    return pdb_ok({pdb_bool(!m->has_selection())});
  });
  t.add("gimp-selection-bounds", [](Session& s, const Args& a) {
    auto* m = s.model();
    if (m == nullptr || a.i(0) != kImageId) return pdb_error();
    // Image coordinates: the layer's selection bounds moved by its offset.
    const auto r = m->has_selection() ? m->selection_bounds() : PixelRect{-m->layer_x(), -m->layer_y(), m->image_width(), m->image_height()};
    const auto x = r.x + m->layer_x();
    const auto y = r.y + m->layer_y();
    return pdb_ok({pdb_bool(m->has_selection()), pdb_int(x), pdb_int(y), pdb_int(x + r.width), pdb_int(y + r.height)});
  });
}

}  // namespace patchy::gimp::host
