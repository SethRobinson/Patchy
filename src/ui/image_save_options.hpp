#pragma once

// Save/export choices are independent of the image reader and renderer APIs.
#include "formats/metadata/metadata_edit.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/dds_document_io.hpp"
#include "formats/rttex_document_io.hpp"

#include <QColor>
#include <QString>
#include <optional>
#include <vector>

namespace patchy::ui {

enum class IcoResample {
  Auto,     // nearest for palette-mode or small (<= 64 px) documents, smooth otherwise
  Nearest,
  Smooth,
};

struct ImageSaveOptions {
  // File Info metadata written with the image (docs/file-info.md). Save and Save As
  // write all of it; the Export section offers the choice and persists it as
  // saveOptions/exportMetadata (metadata_policy_key tokens); scripts pass `metadata`.
  metadata::MetadataPolicy metadata_policy{metadata::MetadataPolicy::All};
  int jpeg_quality{95};
  // JPEG coding (File > Export for Web, docs/web-export.md; scripts pass `progressive` and
  // `optimizeCoding`): progressive scans and optimized Huffman tables. Off here, so Save,
  // Save As and Export Flat Image keep their bytes; the Export for Web dialog turns both on.
  bool jpeg_progressive{false};
  bool jpeg_optimize{false};
  // JPEG: a save that writes EXIF adds an IFD1 thumbnail of the saved image
  // (docs/file-info.md, "Thumbnails"). Export for Web clears it unless "Include thumbnail"
  // is on; scripts pass `thumbnail`.
  bool jpeg_exif_thumbnail{true};
  // With jpeg_exif_thumbnail, a JPEG that would carry no EXIF (Metadata None) gets a minimal
  // one for the thumbnail alone: IFD0 resolution and IFD1. Set by Export for Web's "Include
  // thumbnail" and a script's explicit `thumbnail: true`; off for Save, Save As and Export.
  bool jpeg_exif_thumbnail_without_metadata{false};
  bmp::BmpEncoding bmp_encoding{bmp::BmpEncoding::Rgba32};
  bmp::BmpPaletteMode bmp_palette_mode{bmp::BmpPaletteMode::Exact};
  QString bmp_palette_path;
  std::vector<int> ico_sizes{16, 24, 32, 48, 64, 128, 256};
  IcoResample ico_resample{IcoResample::Auto};
  int cur_hotspot_x{0};
  int cur_hotspot_y{0};
  // PDF image data: pixel-exact Flate, or JPEG at pdf_jpeg_quality. The pair is one of
  // the pdf_image_quality_presets (ui/pdf_export.hpp) and persists by preset id as
  // saveOptions/pdfImageQuality; load_image_save_option_defaults answers "high" when
  // nothing is stored, while this struct default stays lossless for scripts and the CLI.
  bool pdf_lossless{true};
  int pdf_jpeg_quality{90};
  // PDF: a page imported from a PDF and not visibly changed since is written with its
  // original image bytes (PdfExportOptions::keep_original_image_data). Persists as
  // saveOptions/pdfKeepOriginalImages. pdf_original_image_data_available is per save and
  // never persisted: the caller sets it when the document carries such data, and the PDF
  // Options dialog shows the checkbox only then.
  bool pdf_keep_original_images{true};
  bool pdf_original_image_data_available{false};
  // PDF: what a page is (PdfExportOptions::pages), as its token: "artboards", "groups"
  // or "canvas". Chosen per save, never persisted: the document decides the default
  // (artboards when it has them, groups for a PDF imported as page folders, else the
  // canvas). The *_available flags are per save and show the choices in PDF Options.
  QString pdf_pages{QStringLiteral("artboards")};
  // With pdf_pages "groups": the visible ungrouped root layers draw on every page, and
  // hidden groups are pages too (PdfExportOptions of the same names).
  bool pdf_include_ungrouped_layers{false};
  bool pdf_include_hidden_groups{true};
  bool pdf_artboards_available{false};
  bool pdf_groups_available{false};
  // PDF: keep layers as editable objects (paths, real text, images) instead of one
  // flattened image. Per save, never a persisted default: MainWindow::resolve_pdf_layer_choice
  // sets it from the saveOptions/pdfLayerPolicy preference or the flatten-or-keep question.
  // See PdfExportOptions::editable_layers.
  bool pdf_editable_layers{false};
  // TIFF: write Photoshop's layer data beside the composite (docs/tiff.md). Per save, never
  // persisted. Unset means the default MainWindow::save_document_to_path applies: on when
  // a flat save would discard the document's layers, off for exports. The TIFF Options
  // dialog (Save As) and scripts' `layers` set it.
  std::optional<bool> tiff_layers;
  // PDF, editable layers only: text whose font is not installed is embedded as the layer's
  // pixels instead of drawn as real text in a substitute face. Persists as
  // saveOptions/pdfMissingFontsAsImages. See PdfExportOptions::missing_fonts_as_images.
  bool pdf_missing_fonts_as_images{false};
  // Export-only transforms, offered by the Export Flat Image flow (never Save/Save As:
  // rescaling a save would silently mutate the file the session points at). Deliberately
  // not part of the persisted option defaults; the export dialog persists its own
  // saveOptions/export* keys. write_flat_image_file applies them in this order: trim,
  // resize, nearest-neighbor scale, background fill. With all of them at their defaults
  // the writer path is byte-identical to a plain save.
  // Nearest-neighbor pixel-art scale (1x/2x/4x/8x).
  int export_scale{1};
  // Crop to the bounding box of alpha != 0 before anything else. A fully transparent image
  // keeps its size and adds a writer notice.
  bool export_trim_transparent{false};
  // Smooth (bilinear) resize target for the FULL canvas, 0 = no resize. A trimmed export
  // scales by the same factors so it never distorts; one zero side follows the aspect.
  int export_width{0};
  int export_height{0};
  // Composite the final pixels over export_background_color (straight alpha). The result
  // is opaque and drops the document-alpha mask structure.
  bool export_fill_transparent{false};
  QColor export_background_color{Qt::white};
  // JPEG, TIFF and flat PDF: write CMYK separated through the working CMYK space and
  // embed its profile (docs/cmyk.md). Composited over white paper first, at 8 bits per
  // ink. Offered by the Export dialog only (persisted per format as
  // saveOptions/exportColorJpeg, ...Tiff, ...Pdf); scripts pass colorMode: "cmyk".
  bool export_cmyk{false};
  // With export_cmyk: black and grays print with black ink alone (SeparationOptions).
  // Persisted per format as saveOptions/exportBlackInkOnlyJpeg, ...Tiff, ...Pdf; scripts pass
  // blackInkOnly: true.
  bool export_black_ink_only{false};
  // True when the Export dialog's Color row or a script's colorMode chose export_cmyk.
  // Otherwise a save of a CMYK Proof document takes the color form from the mode
  // (MainWindow::save_document_to_path, docs/cmyk.md).
  bool export_color_explicit{false};
  // PSD and PSB in CMYK Proof (docs/cmyk.md): the writer gets the working CMYK space and
  // writes CMYK whenever the layers allow it (psd::DocumentIo::layered_save_color_mode).
  // Set from the document's mode by MainWindow::save_document_to_path.
  bool psd_cmyk_proof{false};
  // PSD, PSB, layered TIFF and Aseprite: layers in blend modes the format cannot store are
  // rebuilt as Normal layers that keep the look (bake_unsupported_blend_modes) instead of
  // only switching to Normal. Set per save by the Save prompt or a script's
  // unsupportedBlendModes: "bake"; never persisted (docs/blend-modes.md, "Layered saves").
  bool bake_unsupported_blend_modes{false};
  // UI-only: MainWindow reveals the written file afterwards; the writers ignore it.
  bool export_reveal_in_file_explorer{false};
  // WebP: 0-100 through QImageWriter::setQuality (75 is Qt's own default, so an unset
  // save keeps today's bytes). Persists as saveOptions/webpQuality.
  int webp_quality{75};
  // WebP: lossless, sent as quality 100 (Qt's WebP plugin encodes losslessly at 100).
  // Persists as saveOptions/webpLossless.
  bool webp_lossless{false};
  bool webp_animate{false};
  int animation_frame_delay_ms{100};
  int webp_loop_count{0};  // 0 forever; otherwise total plays, up to 65535
  // Dialog-only context for the WebP and JPEG XL animation-or-flatten choice.
  // Non-interactive saves remain still images.
  bool offer_animation{false};
  bool has_visible_frames{true};
  // GIF: write the visible top-level layers as a looping animation (top layer = frame 1)
  // instead of one flattened image. Per save, like pdf_editable_layers: only the GIF
  // options dialog and the Export Layers as Animated GIF action set it, so CLI/scripted
  // saves and every non-dialog path keep writing today's single-frame bytes.
  bool gif_animate{false};
  // GIF animation: default per-frame delay in centiseconds (the wire unit); a trailing
  // "0.25s" token in a layer name overrides it per frame. Persists as
  // saveOptions/gifFrameDelayCs.
  int gif_frame_delay_cs{10};
  // GIF and palette PNG (File > Export for Web, docs/web-export.md; scripts pass `colors`,
  // `dither`, `ditherAmount` and `pngColors`): the color count (2..256, one slot reserved
  // for transparency when needed), Floyd-Steinberg error diffusion and its amount (0..100),
  // and PNG written as an indexed palette image (tRNS for transparency, cut at 50% alpha).
  // The defaults keep Save and Export Flat Image byte-identical. Palette-mode documents
  // keep their own palette.
  int palette_colors{256};
  bool palette_dither{false};
  int palette_dither_amount{100};
  bool png_palette{false};
  // JPEG XR: 1-100, mapped onto the WIC encoder's ImageQuality (0.0-1.0). Persists as
  // saveOptions/jxrQuality.
  int jxr_quality{90};
  // JPEG XR: lossless compression, which the codec treats as overriding the quality value.
  // Persists as saveOptions/jxrLossless.
  bool jxr_lossless{false};
  // JPEG XL (docs/jxl.md): 1-100 on libjxl's quality scale, lossless, and encoder effort
  // 1-9. Persist as saveOptions/jxlQuality, saveOptions/jxlLossless and
  // saveOptions/jxlEffort. The encoder's patches and dots are always off and have no option.
  int jxl_quality{90};
  bool jxl_lossless{false};
  int jxl_effort{7};
  // AVIF (docs/avif.md): 0-100 on libavif's quality scale, lossless (identity matrix), and
  // encoder speed 0-10 (0 slowest and smallest, 6 avifenc's default). Persist as
  // saveOptions/avifQuality, saveOptions/avifLossless and saveOptions/avifSpeed.
  int avif_quality{80};
  bool avif_lossless{false};
  int avif_speed{6};
  // JPEG XL animation from the visible top-level layers, like webp_animate; the default
  // frame delay is the shared animation_frame_delay_ms.
  bool jxl_animate{false};
  int jxl_loop_count{0};  // 0 forever; otherwise total plays, up to 65535
  // Animated PNG from the visible top-level layers (File > Export > Layers as Animated
  // PNG and scripts' exportAnimatedPng only; Save As .png stays flat). The default frame
  // delay is the shared animation_frame_delay_ms.
  bool apng_animate{false};
  int apng_loop_count{0};  // 0 forever; otherwise total plays, up to 65535
  // Proton texture (.rttex): pixel encoding, JPEG quality (Jpeg only), power-of-two handling,
  // and the RTPack-style flags. Persist as saveOptions/rttexEncoding, rttexJpegQuality,
  // rttexPowerOfTwo, rttexForceSquare, rttexForceAlpha, and rttexCompress.
  rttex::Encoding rttex_encoding{rttex::Encoding::Rgba8};
  int rttex_jpeg_quality{90};
  rttex::PowerOfTwo rttex_power_of_two{rttex::PowerOfTwo::Pad};
  bool rttex_force_square{false};
  bool rttex_force_alpha{false};
  bool rttex_compress{true};
  // DDS texture: block compression choice (Automatic keeps the opened .dds file's format,
  // otherwise BC1 when opaque and BC3 with transparency) and the mipmap choice (Automatic
  // follows the opened .dds file and generates a chain for every other document). Persist
  // as saveOptions/ddsCompression and saveOptions/ddsMipmapMode.
  dds::Compression dds_compression{dds::Compression::Automatic};
  dds::MipmapChoice dds_mipmaps{dds::MipmapChoice::Automatic};
  // Not persisted: what the reader recorded about the .dds the document was opened from
  // (MainWindow::image_save_defaults_for_document fills it from the patchy.dds.* metadata).
  // Both Automatic choices resolve against it at write time and the dialog names it.
  dds::SourceShape dds_source;
};

}  // namespace patchy::ui
