// gimp-item-* and gimp-layer-*: the layer (id 2) and the selection channel (id 3).

#include "plugins/gimp/host/pdb.hpp"
#include "plugins/gimp/host/session.hpp"

namespace patchy::gimp::host {

namespace {

bool is_layer(Session& s, std::int32_t id) { return s.model() != nullptr && id == kLayerId; }
bool is_item(Session& s, std::int32_t id) { return s.model() != nullptr && (id == kLayerId || id == kSelectionId); }

void add_kind_check(PdbTable& t, const char* name, bool layer, bool selection) {
  t.add(name, [layer, selection](Session& s, const Args& a) {
    const auto id = a.i(0);
    return pdb_ok({pdb_bool(s.model() != nullptr && ((layer && id == kLayerId) || (selection && id == kSelectionId)))});
  });
}

}  // namespace

void register_item_procedures(PdbTable& t) {
  add_kind_check(t, "gimp-item-id-is-valid", true, true);
  add_kind_check(t, "gimp-item-id-is-drawable", true, true);
  add_kind_check(t, "gimp-item-id-is-layer", true, false);
  add_kind_check(t, "gimp-item-id-is-channel", false, true);
  add_kind_check(t, "gimp-item-id-is-selection", false, true);
  for (const char* name : {"gimp-item-id-is-text-layer", "gimp-item-id-is-vector-layer", "gimp-item-id-is-link-layer",
                           "gimp-item-id-is-group-layer", "gimp-item-id-is-layer-mask", "gimp-item-id-is-path"}) {
    add_kind_check(t, name, false, false);
  }
  t.add("gimp-item-get-image", [](Session& s, const Args& a) {
    return is_item(s, a.i(0)) ? pdb_ok({int_param("GimpImage", kImageId)}) : pdb_error();
  });
  t.add("gimp-item-get-name", [](Session& s, const Args& a) {
    const auto id = a.i(0);
    if (!is_item(s, id)) return pdb_error();
    return pdb_ok({pdb_string(id == kLayerId ? s.model()->layer_name() : std::string("Selection Mask"))});
  });
  t.add("gimp-item-get-visible", [](Session& s, const Args& a) {
    return is_item(s, a.i(0)) ? pdb_ok({pdb_bool(true)}) : pdb_error();
  });
  t.add("gimp-item-is-group", [](Session& s, const Args& a) {
    return is_item(s, a.i(0)) ? pdb_ok({pdb_bool(false)}) : pdb_error();
  });
  t.add("gimp-item-get-children", [](Session&, const Args&) { return pdb_error(); });  // not a group
  t.add("gimp-item-get-parent", [](Session& s, const Args& a) {
    return is_item(s, a.i(0)) ? pdb_ok({int_param("GimpItem", -1)}) : pdb_error();
  });
  for (const char* name : {"gimp-item-get-lock-content", "gimp-item-get-lock-position", "gimp-item-get-lock-visibility",
                           "gimp-item-get-expanded"}) {
    t.add(name, [](Session& s, const Args& a) { return is_item(s, a.i(0)) ? pdb_ok({pdb_bool(false)}) : pdb_error(); });
  }
  t.add("gimp-item-get-tattoo", [](Session& s, const Args& a) {
    return is_item(s, a.i(0)) ? pdb_ok({int_param("guint", a.i(0))}) : pdb_error();
  });
  t.add("gimp-item-get-color-tag", [](Session& s, const Args& a) {
    return is_item(s, a.i(0)) ? pdb_ok({int_param("GimpColorTag", 0)}) : pdb_error();
  });
  t.add("gimp-item-get-parasite", [](Session&, const Args&) { return pdb_error(); });

  t.add("gimp-layer-get-opacity", [](Session& s, const Args& a) {
    return is_layer(s, a.i(0)) ? pdb_ok({pdb_double(100.0)}) : pdb_error();
  });
  t.add("gimp-layer-get-mode", [](Session& s, const Args& a) {
    return is_layer(s, a.i(0)) ? pdb_ok({int_param("GimpLayerMode", 28)}) : pdb_error();  // GIMP_LAYER_MODE_NORMAL
  });
  t.add("gimp-layer-get-mask", [](Session& s, const Args& a) {
    return is_layer(s, a.i(0)) ? pdb_ok({int_param("GimpLayerMask", -1)}) : pdb_error();
  });
  t.add("gimp-layer-get-lock-alpha", [](Session& s, const Args& a) {
    return is_layer(s, a.i(0)) ? pdb_ok({pdb_bool(s.request() != nullptr && s.request()->protect_alpha)}) : pdb_error();
  });
  // Adding alpha to a layer that has it changes nothing; a layer without it (Patchy's
  // Background, which locks transparency) cannot gain one during a run.
  t.add("gimp-layer-add-alpha", [](Session& s, const Args& a) {
    if (is_layer(s, a.i(0)) && s.model()->has_alpha()) return pdb_ok();
    s.note_refused("gimp-layer-add-alpha");
    return pdb_error();
  });
  t.add("gimp-layer-is-floating-sel", [](Session& s, const Args& a) {
    return is_layer(s, a.i(0)) ? pdb_ok({pdb_bool(false)}) : pdb_error();
  });
  t.add("gimp-layer-get-show-mask", [](Session& s, const Args& a) {
    return is_layer(s, a.i(0)) ? pdb_ok({pdb_bool(false)}) : pdb_error();
  });
  t.add("gimp-layer-get-apply-mask", [](Session& s, const Args& a) {
    return is_layer(s, a.i(0)) ? pdb_ok({pdb_bool(false)}) : pdb_error();
  });
}

}  // namespace patchy::gimp::host
