#pragma once

// The persisted token spellings of ImageSaveOptions' enumerated fields, shared by the
// saveOptions/* settings, the format-options dialogs and the script API's saveAs options
// (script_save_options.hpp), so none of them can drift apart. Every token is a
// compatibility contract: scripts and settings files in the wild carry them.

#include "ui/image_document_io.hpp"

#include <QString>

#include <array>

namespace patchy::ui {

// "jpg" from "JPG" or ".jpg".
[[nodiscard]] QString normalized_save_extension(QString extension);

[[nodiscard]] bool is_jpeg_extension(const QString& extension);
[[nodiscard]] bool is_webp_extension(const QString& extension);
[[nodiscard]] bool is_bmp_extension(const QString& extension);
[[nodiscard]] bool is_ico_extension(const QString& extension);
[[nodiscard]] bool is_cur_extension(const QString& extension);
[[nodiscard]] bool is_jxr_extension(const QString& extension);
[[nodiscard]] bool is_rttex_extension(const QString& extension);
[[nodiscard]] bool is_dds_extension(const QString& extension);

// DDS and Proton tokens wrap the codec's own helpers (docs/dds.md, docs/rttex.md).
[[nodiscard]] QString dds_compression_key(dds::Compression compression);
[[nodiscard]] dds::Compression dds_compression_from_key(const QString& key, dds::Compression fallback);
[[nodiscard]] QString dds_mipmap_choice_key(dds::MipmapChoice choice);
[[nodiscard]] dds::MipmapChoice dds_mipmap_choice_from_key(const QString& key, dds::MipmapChoice fallback);
[[nodiscard]] QString rttex_encoding_key(rttex::Encoding encoding);
[[nodiscard]] rttex::Encoding rttex_encoding_from_key(const QString& key, rttex::Encoding fallback);
[[nodiscard]] QString rttex_power_of_two_key(rttex::PowerOfTwo mode);
[[nodiscard]] rttex::PowerOfTwo rttex_power_of_two_from_key(const QString& key, rttex::PowerOfTwo fallback);

// The icon sizes the ICO/CUR dialog offers and the writer accepts.
inline constexpr std::array<int, 7> kIcoSizeChoices = {16, 24, 32, 48, 64, 128, 256};

[[nodiscard]] QString ico_resample_key(IcoResample resample);
[[nodiscard]] IcoResample ico_resample_from_key(const QString& key, IcoResample fallback);
[[nodiscard]] QString bmp_encoding_key(bmp::BmpEncoding encoding);
[[nodiscard]] bmp::BmpEncoding bmp_encoding_from_key(const QString& key, bmp::BmpEncoding fallback);
[[nodiscard]] QString bmp_palette_mode_key(bmp::BmpPaletteMode mode);
[[nodiscard]] bmp::BmpPaletteMode bmp_palette_mode_from_key(const QString& key, bmp::BmpPaletteMode fallback);

}  // namespace patchy::ui
