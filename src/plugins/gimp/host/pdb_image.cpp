// gimp-image-*: the one image a run has (id 1), as big as Patchy's document, holding
// the layer the plug-in runs on.

#include "plugins/gimp/host/pdb.hpp"
#include "plugins/gimp/host/session.hpp"

namespace patchy::gimp::host {

namespace {

ImageModel* image(Session& s, const Args& a, std::size_t index = 0) {
  auto* model = s.model();
  if (model == nullptr || a.i(index) != kImageId) return nullptr;
  return model;
}

PdbReply layer_list(Session& s, const Args& a, const char* item_type) {
  if (image(s, a) == nullptr) return pdb_error();
  return pdb_ok({id_array_param("GimpCoreObjectArray", item_type, {kLayerId})});
}

}  // namespace

void register_image_procedures(PdbTable& t) {
  t.add("gimp-image-id-is-valid", [](Session& s, const Args& a) {
    return pdb_ok({pdb_bool(s.model() != nullptr && a.i(0) == kImageId)});
  });
  t.add("gimp-image-get-width", [](Session& s, const Args& a) {
    auto* m = image(s, a);
    return m ? pdb_ok({pdb_int(m->image_width())}) : pdb_error();
  });
  t.add("gimp-image-get-height", [](Session& s, const Args& a) {
    auto* m = image(s, a);
    return m ? pdb_ok({pdb_int(m->image_height())}) : pdb_error();
  });
  t.add("gimp-image-get-base-type", [](Session& s, const Args& a) {
    return image(s, a) ? pdb_ok({int_param("GimpImageBaseType", 0)}) : pdb_error();  // GIMP_RGB
  });
  t.add("gimp-image-get-precision", [](Session& s, const Args& a) {
    auto* m = image(s, a);
    return m ? pdb_ok({int_param("GimpPrecision", m->precision())}) : pdb_error();
  });
  t.add("gimp-image-get-layers", [](Session& s, const Args& a) { return layer_list(s, a, "GimpLayer"); });
  t.add("gimp-image-get-selected-layers", [](Session& s, const Args& a) { return layer_list(s, a, "GimpLayer"); });
  t.add("gimp-image-get-selected-drawables", [](Session& s, const Args& a) { return layer_list(s, a, "GimpDrawable"); });
  t.add("gimp-image-get-channels", [](Session& s, const Args& a) {
    if (image(s, a) == nullptr) return pdb_error();
    return pdb_ok({id_array_param("GimpCoreObjectArray", "GimpChannel", {})});
  });
  t.add("gimp-image-get-selected-channels", [](Session& s, const Args& a) {
    if (image(s, a) == nullptr) return pdb_error();
    return pdb_ok({id_array_param("GimpCoreObjectArray", "GimpChannel", {})});
  });
  t.add("gimp-image-get-paths", [](Session& s, const Args& a) {
    if (image(s, a) == nullptr) return pdb_error();
    return pdb_ok({id_array_param("GimpCoreObjectArray", "GimpPath", {})});
  });
  t.add("gimp-image-get-selection", [](Session& s, const Args& a) {
    return image(s, a) ? pdb_ok({int_param("GimpSelection", kSelectionId)}) : pdb_error();
  });
  t.add("gimp-image-get-color-profile", [](Session& s, const Args& a) {
    // No profile: the image is sRGB, as every Patchy RGB document is shown.
    return image(s, a) ? pdb_ok({bytes_param("GBytes", {})}) : pdb_error();
  });
  t.add("gimp-image-get-simulation-profile", [](Session& s, const Args& a) {
    return image(s, a) ? pdb_ok({bytes_param("GBytes", {})}) : pdb_error();
  });
  t.add("gimp-image-get-resolution", [](Session& s, const Args& a) {
    auto* m = image(s, a);
    return m ? pdb_ok({pdb_double(m->resolution_x()), pdb_double(m->resolution_y())}) : pdb_error();
  });
  t.add("gimp-image-get-unit", [](Session& s, const Args& a) {
    return image(s, a) ? pdb_ok({int_param("GimpUnit", 1)}) : pdb_error();  // inches
  });
  t.add("gimp-image-get-name", [](Session& s, const Args& a) {
    auto* m = image(s, a);
    return m ? pdb_ok({pdb_string(m->image_name())}) : pdb_error();
  });
  t.add("gimp-image-get-file", [](Session& s, const Args& a) {
    if (image(s, a) == nullptr) return pdb_error();
    Param file;
    file.kind = ParamKind::File;
    file.type_name = std::string("GFile");
    return pdb_ok({file});
  });
  t.add("gimp-image-is-dirty", [](Session& s, const Args& a) {
    return image(s, a) ? pdb_ok({pdb_bool(false)}) : pdb_error();
  });
  t.add("gimp-image-get-item-position", [](Session& s, const Args& a) {
    return image(s, a) ? pdb_ok({pdb_int(0)}) : pdb_error();
  });
  // Undo: Patchy makes the whole run one undo step, so groups and freezes are no-ops.
  for (const char* name : {"gimp-image-undo-group-start", "gimp-image-undo-group-end"}) {
    t.add(name, [](Session& s, const Args& a) { return image(s, a) ? pdb_ok() : pdb_error(); });
  }
  for (const char* name : {"gimp-image-undo-disable", "gimp-image-undo-enable", "gimp-image-undo-freeze",
                           "gimp-image-undo-thaw", "gimp-image-undo-is-enabled"}) {
    t.add(name, [](Session& s, const Args& a) { return image(s, a) ? pdb_ok({pdb_bool(true)}) : pdb_error(); });
  }
  t.add("gimp-image-get-parasite", [](Session&, const Args&) { return pdb_error(); });  // none
  t.add("gimp-image-get-parasite-list", [](Session& s, const Args& a) {
    if (image(s, a) == nullptr) return pdb_error();
    Param list;
    list.kind = ParamKind::Strv;
    list.type_name = std::string("GStrv");
    return pdb_ok({list});
  });
  t.add("gimp-get-images", [](Session& s, const Args&) {
    return pdb_ok({id_array_param("GimpCoreObjectArray", "GimpImage", s.model() ? std::vector<std::int32_t>{kImageId}
                                                                                   : std::vector<std::int32_t>{})});
  });
}

}  // namespace patchy::gimp::host
