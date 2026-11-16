#include "ui/ocr_model_download.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QUrl>

#include <utility>

namespace patchy::ui {

namespace {

constexpr auto kLicenseFileName = "ppocr_LICENSE.txt";

// The Patchy release that rehosts PaddlePaddle's files unchanged under Patchy's file names
// (docs/ocr-engines.md, "PP-OCR"). Assets there are never replaced.
QUrl release_url(const char* name) {
  return QUrl(QStringLiteral("https://github.com/SethRobinson/Patchy/releases/download/ai-models-4/") +
              QLatin1String(name));
}

// One file of an official model repository at the commit its license declaration was read
// at (docs/legal-licensing.md).
DownloadableModelFile model(const char* name, qint64 size, const char* sha256, const char* repository,
                            const char* commit, const char* upstream_name) {
  return {QLatin1String(name), release_url(name), size, QByteArray(sha256),
          {QUrl(QStringLiteral("https://huggingface.co/PaddlePaddle/%1/resolve/%2/%3")
                    .arg(QLatin1String(repository), QLatin1String(commit), QLatin1String(upstream_name)))}};
}

OcrModelDownloader& downloader() {
  static OcrModelDownloader installed;
  return installed;
}

}  // namespace

const std::vector<DownloadableModelFile>& ocr_model_download_files() {
  // A file a host replaces no longer matches its SHA-256, so the download fails rather than
  // installing anything unreviewed.
  static const std::vector<DownloadableModelFile> files{
      model("ppocr_v6_small_det.onnx", 9880512,
            "d73e0058b7a8086bbd57f3d10b8bcd4ff95363f67e06e2762b5e814fe9c9410e",
            "PP-OCRv6_small_det_onnx", "28fe5895c24fd108c19eb3e8479f4ab385fbfc62", "inference.onnx"),
      model("ppocr_v6_small_rec.onnx", 21159378,
            "5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634",
            "PP-OCRv6_small_rec_onnx", "b8f84f0b80c529de40b4fbb3544b84fa7233a513", "inference.onnx"),
      model("ppocr_v6_small_rec.yml", 150579,
            "ab078671bb49f06228eadccd34f1bb501e157f7a047095ffb943ba81512c77d1",
            "PP-OCRv6_small_rec_onnx", "b8f84f0b80c529de40b4fbb3544b84fa7233a513", "inference.yml"),
      model("ppocr_korean_rec.onnx", 13418787,
            "92f0b7785e64fc9090106a241cf4c1eb97472824558272751b88a2a4476d3a08",
            "korean_PP-OCRv5_mobile_rec_onnx", "5c6f574b8e2230adf4287b33e736d71b9fabd28e", "inference.onnx"),
      model("ppocr_korean_rec.yml", 96039,
            "f757fa1c40e99edcf27e9cce879b93eb2a51fa46f5ef39095689b8c37dd75998",
            "korean_PP-OCRv5_mobile_rec_onnx", "5c6f574b8e2230adf4287b33e736d71b9fabd28e", "inference.yml"),
      model("ppocr_cyrillic_rec.onnx", 8048799,
            "5371ee1ddaa7983cc62d0818d99e982b6804638c85e4f960d59a574094e172e5",
            "cyrillic_PP-OCRv5_mobile_rec_onnx", "2cef88145434beb8afa9dd82d77d799eb1ad7b29", "inference.onnx"),
      model("ppocr_cyrillic_rec.yml", 6991,
            "5c76cc91fa98410178a09f498db10050d0ec1634a660053d3005ab7be581f501",
            "cyrillic_PP-OCRv5_mobile_rec_onnx", "2cef88145434beb8afa9dd82d77d799eb1ad7b29", "inference.yml"),
      model("ppocr_eslav_rec.onnx", 7887627,
            "b3018ef2b09a0250b6e0c8e871c927098363e5fd4df890cc68e8358eb0aaf1bd",
            "eslav_PP-OCRv5_mobile_rec_onnx", "9a32171fc5718746875e1a261818884517975013", "inference.onnx"),
      model("ppocr_eslav_rec.yml", 4538,
            "025039bac23eb4a308efcefa4d58eab3af440767815c6ba6938468bf6353ee5a",
            "eslav_PP-OCRv5_mobile_rec_onnx", "9a32171fc5718746875e1a261818884517975013", "inference.yml"),
      model("ppocr_el_rec.onnx", 7808735,
            "2acf17fcaea2bc81b878e311e6263b8885f48bb03796f75f9f30ed3242bbaa6d",
            "el_PP-OCRv5_mobile_rec_onnx", "8152b89d2ee0e1d4c92decab75fe75e5e3d836b6", "inference.onnx"),
      model("ppocr_el_rec.yml", 3317,
            "17d85b2fe2d2f24cd4ab07bcbc33e0c126859b956ced36e281dc65e2d0c1f0bf",
            "el_PP-OCRv5_mobile_rec_onnx", "8152b89d2ee0e1d4c92decab75fe75e5e3d836b6", "inference.yml"),
      model("ppocr_th_rec.onnx", 7891015,
            "27618be66018f8598ac0a526a593f9f1cebf794e7eded93428e8fb016e537f5f",
            "th_PP-OCRv5_mobile_rec_onnx", "1d4adbbafb1034a2fd6618498575b81ea7b69f69", "inference.onnx"),
      model("ppocr_th_rec.yml", 4667,
            "f6ba7fefc38ca1ff398ddafa75d67d16e0b3757c4e6c833adffee98a981766c9",
            "th_PP-OCRv5_mobile_rec_onnx", "1d4adbbafb1034a2fd6618498575b81ea7b69f69", "inference.yml"),
      model("ppocr_devanagari_rec.onnx", 7912311,
            "cb789212ce96c69d3e74728ae4309d179281d68cb3945d0616b67cafab41c986",
            "devanagari_PP-OCRv5_mobile_rec_onnx", "251aec19e36739540d35e2cc943f6aa7503b98e5", "inference.onnx"),
      model("ppocr_devanagari_rec.yml", 5027,
            "9bd172dd26440c8ce94d1cde5d5baea6aefdc7cf3c5c8492e0beedef656d4e54",
            "devanagari_PP-OCRv5_mobile_rec_onnx", "251aec19e36739540d35e2cc943f6aa7503b98e5", "inference.yml"),
      model("ppocr_ta_rec.onnx", 7885691,
            "c6d2b682d2a0ea4cb1fccdba295976f93fd439964d16cdc666cadef531accbee",
            "ta_PP-OCRv5_mobile_rec_onnx", "0db89798af0d5218fad9ea7ace0557526e9077f1", "inference.onnx"),
      model("ppocr_ta_rec.yml", 4579,
            "88a28f5a1bb30cabe38a0985cb5e6619fa4f0c7c78e57a08274674228c5219a6",
            "ta_PP-OCRv5_mobile_rec_onnx", "0db89798af0d5218fad9ea7ace0557526e9077f1", "inference.yml"),
      model("ppocr_te_rec.onnx", 7898759,
            "8238bfc46d4cffe720ed6706e3842802467343497428693ff2bfb4e6b3caa36b",
            "te_PP-OCRv5_mobile_rec_onnx", "f1197c83430241c58c64b0952b3a80366d04eeba", "inference.onnx"),
      model("ppocr_te_rec.yml", 4795,
            "acebbe53f1831bf28ddfed75aedf58225d7aa5d09100c1d5a9140a2a53b137ce",
            "te_PP-OCRv5_mobile_rec_onnx", "f1197c83430241c58c64b0952b3a80366d04eeba", "inference.yml"),
      // The Apache License 2.0 text as PaddleOCR publishes it (LICENSE at commit b9da97be of
      // github.com/PaddlePaddle/PaddleOCR, 11376 bytes), saved beside the models: the
      // license asks that a copy travel with them.
      {QLatin1String(kLicenseFileName), release_url(kLicenseFileName), 11376,
       QByteArrayLiteral("3840c5c0c61c294264d2dd77b8777be6ddd90121ef4e0e64abcd22edea581d6e"),
       {QUrl(QStringLiteral("https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/"
                            "b9da97bea9db53ea5cbb1c32bf5c53b9d09be7b1/LICENSE"))}},
  };
  return files;
}

qint64 ocr_model_download_bytes() {
  qint64 total = 0;
  for (const auto& file : ocr_model_download_files()) {
    total += file.size;
  }
  return total;
}

int ocr_model_download_megabytes() {
  return static_cast<int>((ocr_model_download_bytes() + 500000) / 1000000);
}

QString ocr_model_download_summary() {
  return QObject::tr("%1 MB from %2")
      .arg(ocr_model_download_megabytes())
      .arg(model_download_source_text(ocr_model_download_files()));
}

bool downloaded_ocr_models_present(const QString& folder) {
  for (const auto& file : ocr_model_download_files()) {
    if (file.name == QLatin1String(kLicenseFileName)) {
      continue;  // the models decide; a build tree stages them without the license
    }
    if (!QFileInfo(QDir(folder).filePath(file.name)).isFile()) {
      return false;
    }
  }
  return true;
}

bool delete_downloaded_ocr_models(const QString& folder, QString* error) {
  if (folder.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("the models folder is not set");
    }
    return false;
  }
  QStringList failed;
  for (const auto& file : ocr_model_download_files()) {
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

void set_ocr_model_downloader(OcrModelDownloader installed) {
  downloader() = std::move(installed);
}

const OcrModelDownloader& ocr_model_downloader() {
  return downloader();
}

}  // namespace patchy::ui
