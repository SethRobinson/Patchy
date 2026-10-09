#include "ui/image_save_option_keys.hpp"

#include "formats/dds_document_io.hpp"
#include "formats/jxr_document_io.hpp"
#include "formats/rttex_document_io.hpp"

namespace patchy::ui {

QString normalized_save_extension(QString extension) {
  extension = extension.toLower();
  if (extension.startsWith(QLatin1Char('.'))) {
    extension.remove(0, 1);
  }
  return extension;
}

bool is_jpeg_extension(const QString& extension) {
  const auto normalized = normalized_save_extension(extension);
  return normalized == QStringLiteral("jpg") || normalized == QStringLiteral("jpeg");
}

bool is_webp_extension(const QString& extension) {
  return normalized_save_extension(extension) == QStringLiteral("webp");
}

bool is_bmp_extension(const QString& extension) {
  return normalized_save_extension(extension) == QStringLiteral("bmp");
}

bool is_ico_extension(const QString& extension) {
  return normalized_save_extension(extension) == QStringLiteral("ico");
}

bool is_cur_extension(const QString& extension) {
  return normalized_save_extension(extension) == QStringLiteral("cur");
}

bool is_jxr_extension(const QString& extension) {
  return jxr::is_jxr_extension(normalized_save_extension(extension).toStdString());
}

bool is_rttex_extension(const QString& extension) {
  return rttex::is_rttex_extension(normalized_save_extension(extension).toStdString());
}

bool is_dds_extension(const QString& extension) {
  return dds::is_dds_extension(normalized_save_extension(extension).toStdString());
}

QString dds_compression_key(dds::Compression compression) {
  const auto token = dds::compression_token(compression);
  return QString::fromLatin1(token.data(), static_cast<qsizetype>(token.size()));
}

dds::Compression dds_compression_from_key(const QString& key, dds::Compression fallback) {
  return dds::compression_from_token(key.toStdString()).value_or(fallback);
}

QString dds_mipmap_choice_key(dds::MipmapChoice choice) {
  const auto token = dds::mipmap_choice_token(choice);
  return QString::fromLatin1(token.data(), static_cast<qsizetype>(token.size()));
}

dds::MipmapChoice dds_mipmap_choice_from_key(const QString& key, dds::MipmapChoice fallback) {
  return dds::mipmap_choice_from_token(key.toStdString()).value_or(fallback);
}

// The Proton texture tokens live with the codec so the settings keys, the dialog, and the
// reader's session metadata can never disagree.
QString rttex_encoding_key(rttex::Encoding encoding) {
  const auto token = rttex::encoding_token(encoding);
  return QString::fromLatin1(token.data(), static_cast<qsizetype>(token.size()));
}

rttex::Encoding rttex_encoding_from_key(const QString& key, rttex::Encoding fallback) {
  return rttex::encoding_from_token(key.toStdString()).value_or(fallback);
}

QString rttex_power_of_two_key(rttex::PowerOfTwo mode) {
  const auto token = rttex::power_of_two_token(mode);
  return QString::fromLatin1(token.data(), static_cast<qsizetype>(token.size()));
}

rttex::PowerOfTwo rttex_power_of_two_from_key(const QString& key, rttex::PowerOfTwo fallback) {
  return rttex::power_of_two_from_token(key.toStdString()).value_or(fallback);
}

QString ico_resample_key(IcoResample resample) {
  switch (resample) {
    case IcoResample::Auto:
      return QStringLiteral("auto");
    case IcoResample::Nearest:
      return QStringLiteral("nearest");
    case IcoResample::Smooth:
      return QStringLiteral("smooth");
  }
  return QStringLiteral("auto");
}

IcoResample ico_resample_from_key(const QString& key, IcoResample fallback) {
  if (key == QStringLiteral("auto")) {
    return IcoResample::Auto;
  }
  if (key == QStringLiteral("nearest")) {
    return IcoResample::Nearest;
  }
  if (key == QStringLiteral("smooth")) {
    return IcoResample::Smooth;
  }
  return fallback;
}

QString bmp_encoding_key(bmp::BmpEncoding encoding) {
  switch (encoding) {
    case bmp::BmpEncoding::Rgba32:
      return QStringLiteral("rgba32");
    case bmp::BmpEncoding::Rgb24:
      return QStringLiteral("rgb24");
    case bmp::BmpEncoding::Indexed8:
      return QStringLiteral("indexed8");
    case bmp::BmpEncoding::Indexed4:
      return QStringLiteral("indexed4");
    case bmp::BmpEncoding::Indexed2:
      return QStringLiteral("indexed2");
  }
  return QStringLiteral("rgba32");
}

bmp::BmpEncoding bmp_encoding_from_key(const QString& key, bmp::BmpEncoding fallback) {
  if (key == QStringLiteral("rgba32")) {
    return bmp::BmpEncoding::Rgba32;
  }
  if (key == QStringLiteral("rgb24")) {
    return bmp::BmpEncoding::Rgb24;
  }
  if (key == QStringLiteral("indexed8")) {
    return bmp::BmpEncoding::Indexed8;
  }
  if (key == QStringLiteral("indexed4")) {
    return bmp::BmpEncoding::Indexed4;
  }
  if (key == QStringLiteral("indexed2")) {
    return bmp::BmpEncoding::Indexed2;
  }
  return fallback;
}

QString bmp_palette_mode_key(bmp::BmpPaletteMode mode) {
  switch (mode) {
    case bmp::BmpPaletteMode::Exact:
      return QStringLiteral("exact");
    case bmp::BmpPaletteMode::Quantize:
      return QStringLiteral("quantize");
    case bmp::BmpPaletteMode::PaletteFile:
      return QStringLiteral("paletteFile");
  }
  return QStringLiteral("exact");
}

bmp::BmpPaletteMode bmp_palette_mode_from_key(const QString& key, bmp::BmpPaletteMode fallback) {
  if (key == QStringLiteral("exact")) {
    return bmp::BmpPaletteMode::Exact;
  }
  if (key == QStringLiteral("quantize")) {
    return bmp::BmpPaletteMode::Quantize;
  }
  if (key == QStringLiteral("paletteFile")) {
    return bmp::BmpPaletteMode::PaletteFile;
  }
  return fallback;
}

}  // namespace patchy::ui
