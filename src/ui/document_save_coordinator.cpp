#include "ui/document_save_coordinator.hpp"
#include "ui/save_progress.hpp"

#include "color/color_management.hpp"
#include "core/layer_metadata.hpp"
#include "core/vector_shape.hpp"
#include "formats/format_registry.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/metadata/metadata_edit.hpp"
#include "psd/psd_document_io.hpp"
#include "formats/svg_document_io.hpp"
#include "ui/document_session.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/pdf_import.hpp"
#include "ui/qt_paths.hpp"
#include "ui/smart_object_render.hpp"
#include "patchy_version.hpp"
#include <QDateTime>
#include <QFileInfo>
#include <QUuid>
#include <utility>

namespace patchy::ui {

metadata::SaveStamp save_stamp_now() {
  const auto now = QDateTime::currentDateTime();
  return metadata::SaveStamp{metadata::xmp_date(now.toSecsSinceEpoch(), now.offsetFromUtc() / 60),
                             std::string("Patchy ") + PATCHY_VERSION,
                             QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()};
}

bool is_photoshop_document_extension(const QString& extension) {
  return extension == QStringLiteral("psd") || extension == QStringLiteral("psb");
}

bool is_tiff_extension(const QString& extension) {
  return extension == QStringLiteral("tif") || extension == QStringLiteral("tiff");
}

// Formats whose writers keep the document's layer structure: PSD/PSB and Aseprite save
// the layer tree, and ICO/CUR are exempt because multi-size icons deliberately live as
// one hidden "WxH" layer per size that their writers round-trip.
bool save_extension_preserves_layers(const QString& extension) {
  return is_photoshop_document_extension(extension) || extension == QStringLiteral("aseprite") ||
         extension == QStringLiteral("ase") || extension == QStringLiteral("ico") ||
         extension == QStringLiteral("cur");
}

// True when the session's file came from a format Patchy can only read (camera raw):
// Save can never write back to such a path, so it must become Save As.
//
// PDF is listed explicitly rather than falling out of the registry, for two reasons. The
// registry has no PDF row at all (the writer is Qt-side, in write_flat_image_file), and
// more importantly a PDF import is a RASTERIZATION at a chosen resolution: saving over the
// source would replace a multi-page vector document with one flat re-render. Save As and
// Export to .pdf still work; only Save-in-place is redirected.
bool is_read_only_source_extension(const QString& extension) {
  if (extension.isEmpty()) {
    return false;
  }
  if (is_pdf_extension(extension)) {
    return true;
  }
  const auto* handler = builtin_format_registry().find_by_extension(extension.toStdString());
  return handler != nullptr && !handler->can_write();
}

// Photoshop's "this format cannot store the document's features" test, reduced to what a
// flat save discards in Patchy: a second layer or group, a non-pixel layer (group,
// adjustment, text, smart object), a hand-authored mask, or layer styles. A single pixel
// layer whose only mask is the document-alpha marker round-trips through flat formats
// (the mask is written back as the file's alpha plane), so it does not count.
bool flat_save_discards_layers(const Document& document) {
  const auto& layers = document.layers();
  if (layers.size() != 1) {
    return !layers.empty();
  }
  const Layer& layer = layers.front();
  if (layer.kind() != LayerKind::Pixel || !layer.children().empty() ||
      layer_pixels_are_procedural(layer)) {
    return true;
  }
  if (layer.mask().has_value() && !layer_mask_is_document_alpha(layer)) {
    return true;
  }
  const auto blend_if_status = layer.blend_if_payload_status();
  const bool has_blend_if = blend_if_status == BlendIfPayloadStatus::Unsupported
                                ? !layer.raw_psd_blending_ranges().empty()
                                : !blend_if_is_identity(layer.blend_if());
  return !layer.layer_style().empty() || has_blend_if;
}

// Whether saving `document` into `extension` loses layer content. Flat formats
// lose everything past one plain pixel layer. SVG keeps shape layers, folders,
// vector masks and paint servers as real vectors, so it asks the writer for a
// dry run and warns only when something bakes (text, pixels, smart objects,
// adjustments, styles, raster masks, inexpressible blend modes): a shape-only
// document saves in place with no warning.
bool save_discards_layers(const Document& document, const QString& extension) {
  // A TIFF keeps the layers unless its options turn them off, which
  // MainWindow::save_document_to_path decides (docs/tiff.md).
  if (save_extension_preserves_layers(extension) || is_tiff_extension(extension) ||
      !flat_save_discards_layers(document)) {
    return false;
  }
  if (extension == QStringLiteral("svg")) {
    return !svg::DocumentIo::baked_content(document).empty();
  }
  return true;
}

bool DocumentSaveCoordinator::save(std::int64_t session_id) {
  auto* target = host_.find_session(session_id);
  if (target == nullptr) return false;
  const auto path = target->path;
  if (target->smart_object_link.has_value()) {
    if (target->smart_object_link->external) {
      // A linked-file child: the file on disk is the source of truth, so write it
      // first and only then refresh the parent's previews and link metadata.
      if (path.isEmpty()) {
        return host_.save_as();
      }
      if (!host_.save_to_path(path)) {
        return false;
      }
      return true;
    }
    // An embedded Edit Smart Object Contents tab: Save applies the contents back to
    // the parent document instead of writing a file (Photoshop semantics).
    return host_.commit_embedded(session_id);
  }
  if (path.isEmpty()) {
    return host_.save_as();
  }
  if (is_read_only_source_extension(QFileInfo(path).suffix().toLower())) {
    // The document was developed from a read-only source (camera raw); writing the raw
    // back is impossible, so Save is really Save As (defaulting to <basename>.psd).
    return host_.save_as();
  }
  if (!target->content_extension.isEmpty()) {
    // The file opened by its content, not its name (a PSD named src.bin, GitHub issue
    // 362): Save asks for a name that fits instead of writing under the misleading one.
    return host_.save_as();
  }
  if (save_discards_layers(std::as_const(target->document), QFileInfo(path).suffix().toLower())) {
    // Photoshop behavior: Save on a document whose file format cannot hold its layers
    // (a JPEG that grew layers) turns into Save As, defaulting to PSD, instead of
    // silently flattening back over the original file. A shape-only SVG holds its
    // layers as vectors, so it saves in place.
    return host_.save_as();
  }
  return host_.save_to_path(path);
}

ImageSaveOptions DocumentSaveCoordinator::effective_options(
    const DocumentSession& session, const QString& path, const QString& extension,
    const std::optional<ImageSaveOptions>& requested, const ImageSaveOptions& defaults) {
  auto effective = requested.value_or(defaults);
  if (!requested.has_value() && image_save_options_apply_to_extension(extension)) {
    if (session.image_save_options.has_value() && session.image_save_options_path == path &&
        session.image_save_options_extension == extension) {
      effective = *session.image_save_options;
    }
  }
  return effective;
}

std::optional<DocumentSaveResult> DocumentSaveCoordinator::write_prepared(
    std::int64_t session_id, const QString& path, const QString& extension,
    const ImageSaveOptions& effective_image_options, bool quiet, bool export_copy, bool discards_layers) {
  auto* target = host_.find_session(session_id);
  if (target == nullptr) return std::nullopt;
  std::vector<std::string> writer_notices;
  if (is_photoshop_document_extension(extension)) {
    // Linked smart objects store their path relative to the document's folder, which
    // is only known here (a never-saved document has none, and Save As can move it).
    // Writer bookkeeping like Photoshop's own: no undo step.
    refresh_smart_object_link_relative_paths(
        target->document.metadata().smart_objects,
        target->path.isEmpty() ? QString() : QFileInfo(target->path).absolutePath(),
        QFileInfo(path).absolutePath());
    // Photoshop's save record in the XMP and EXIF: the modify and metadata dates, a new
    // instance, and on the first save the creation date and Patchy as the creator
    // (docs/file-info.md, "Save stamp").
    metadata::stamp_file_info_for_save(target->document.file_info(), save_stamp_now());
    // The writer reads a copy-on-write snapshot on a worker, so the window keeps painting
    // the Saving dialog through the composite (recovery writes the same way).
    const Document snapshot = std::as_const(*target).document_for_write();
    const bool psb = extension == QStringLiteral("psb");
    const auto cmyk_proof = effective_image_options.psd_cmyk_proof ? working_cmyk_space() : nullptr;
    host_.run_write(path, /*on_worker*/ true, !quiet, [&snapshot, &path, psb, &cmyk_proof](SaveProgress& progress) {
      psd::WriteOptions options{psb};
      options.cmyk_proof = cmyk_proof;
      options.progress = [&progress](int done, int total, const std::string& label) {
        progress.report(done, total, SaveProgress::Kind::Layers, QString::fromStdString(label));
      };
      psd::DocumentIo::write_layered_rgb8_file(snapshot, to_filesystem_path(path), options);
    });
  } else if (extension == QStringLiteral("aseprite") || extension == QStringLiteral("ase")) {
    // Layered save: the Aseprite writer keeps the layer tree instead of flattening.
    const Document written = std::as_const(*target).document_for_write();
    std::optional<Document> narrowed;
    aseprite::DocumentIo::write_file(document_for_8bit_writer(written, narrowed), to_filesystem_path(path));
  } else if (extension == QStringLiteral("svg")) {
    // Structure-preserving vector write (never write_flat_image_file):
    // shape layers stay SVG vectors, the writer reports what it baked.
    std::optional<Document> narrowed;
    svg::DocumentIo::write_file(document_for_8bit_writer(target->document, narrowed), to_filesystem_path(path),
                                &writer_notices);
  } else {
    // Editable PDF keeps layers and reports what it baked, like the SVG writer. Every flat
    // format and PDF writes a copy-on-write snapshot on a worker (a one-page PDF of a grid
    // of scanned pages took 10 s on this thread); PDF reports its pages.
    const Document snapshot = std::as_const(target->document);
    host_.run_write(path, /*on_worker*/ true, !quiet,
                    [&snapshot, &path, &extension, &effective_image_options, &writer_notices](SaveProgress& progress) {
                      const ScopedSaveProgress scope(progress);
                      write_flat_image_file(snapshot, path, extension, effective_image_options, &writer_notices);
                    });
  }
  host_.offer_download(path);
  target = host_.find_session(session_id);
  if (target == nullptr) return std::nullopt;
  const bool saved_flattened_copy =
      discards_layers && !(target->smart_object_link.has_value() && target->smart_object_link->external);
  if (saved_flattened_copy || export_copy) {
    // A copy does not take ownership of the session path or clear its modified state.
    if (!quiet) {
      host_.remember_directory(path);
      if (image_save_options_apply_to_extension(extension)) {
        host_.persist_defaults(effective_image_options);
      }
    }
    host_.add_recent(path);
    return DocumentSaveResult{saved_flattened_copy, std::move(writer_notices)};
  }
  target->path = path;
  target->title = QFileInfo(path).fileName();
  target->content_extension.clear();
  if (!quiet) host_.remember_directory(path);
  target = host_.find_session(session_id);
  if (target == nullptr) return std::nullopt;
  if (!is_photoshop_document_extension(extension) && image_save_options_apply_to_extension(extension)) {
    target->image_save_options = effective_image_options;
    target->image_save_options_path = path;
    target->image_save_options_extension = extension;
    if (!quiet) host_.persist_defaults(effective_image_options);
  } else {
    target->image_save_options.reset();
    target->image_save_options_path.clear();
    target->image_save_options_extension.clear();
  }
  host_.mark_saved(session_id);
  host_.add_recent(path);
  return DocumentSaveResult{false, std::move(writer_notices)};
}

void DocumentSaveCoordinator::mark_saved(DocumentSession& session) {
  session.saved_revision = session.revision;
}

}  // namespace patchy::ui
