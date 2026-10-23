#include "ui/watermark_model_download.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QUrl>

namespace patchy::ui {

const std::vector<DownloadableModelFile>& watermark_model_files() {
  // Adobe's float16 ONNX exports (github.com/adobe/trustmark, MIT for code and models), from
  // the Content Authenticity Initiative's model host, renamed with a trustmark_ prefix. A
  // file the host replaces no longer matches its SHA-256, so the download fails rather than
  // installing anything unreviewed.
  static const std::vector<DownloadableModelFile> files{
      {QStringLiteral("trustmark_decoder_Q.onnx"),
       QUrl(QStringLiteral("https://cai-watermark.adobe.net/watermarking/trustmark-models/decoder_Q.onnx")), 47401222,
       QByteArrayLiteral("ee3268f057c9dabef680e169302f5973d0589feea86189ed229a896cc3aa88df")},
      {QStringLiteral("trustmark_decoder_P.onnx"),
       QUrl(QStringLiteral("https://cai-watermark.adobe.net/watermarking/trustmark-models/decoder_P.onnx")), 47400467,
       QByteArrayLiteral("be6d7c33f8a7b376f179e75f3f7c58ff816a9ac7bb6d37fd0a729a635f624c35")},
      {QStringLiteral("trustmark_decoder_C.onnx"),
       QUrl(QStringLiteral("https://cai-watermark.adobe.net/watermarking/trustmark-models/decoder_C.onnx")), 22457887,
       QByteArrayLiteral("3d92f4a278bcf87ac7dc40248c36ae664fe5fdedfd19ca265f7ca730b271100a")},
      {QStringLiteral("trustmark_encoder_Q.onnx"),
       QUrl(QStringLiteral("https://cai-watermark.adobe.net/watermarking/trustmark-models/encoder_Q.onnx")), 17312208,
       QByteArrayLiteral("19b3d1b25836130ffd78775a8f61539f993375d1823ef0e59ba5b8dffb4f892d")},
  };
  return files;
}

qint64 watermark_model_download_bytes() {
  qint64 total = 0;
  for (const auto& file : watermark_model_files()) {
    total += file.size;
  }
  return total;
}

int watermark_model_download_megabytes() {
  return static_cast<int>((watermark_model_download_bytes() + 500000) / 1000000);
}

bool downloaded_watermark_models_present(const QString& folder) {
  for (const auto& file : watermark_model_files()) {
    if (!QFileInfo(QDir(folder).filePath(file.name)).isFile()) {
      return false;
    }
  }
  return true;
}

bool delete_downloaded_watermark_models(const QString& folder, QString* error) {
  if (folder.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("the models folder is not set");
    }
    return false;
  }
  QStringList failed;
  for (const auto& file : watermark_model_files()) {
    const auto path = QDir(folder).filePath(file.name);
    if (QFileInfo(path).isFile() && !QFile::remove(path)) {
      failed << file.name;
    }
  }
  // Non-recursive: succeeds only when nothing else is left in the folder.
  if (failed.isEmpty() && QDir(folder).exists() && QDir(folder).isEmpty()) {
    QDir().rmdir(folder);
  }
  if (!failed.isEmpty() && error != nullptr) {
    *error = QStringLiteral("could not delete %1").arg(failed.join(QStringLiteral(", ")));
  }
  return failed.isEmpty();
}

}  // namespace patchy::ui
