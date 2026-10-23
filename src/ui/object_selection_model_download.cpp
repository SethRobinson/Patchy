#include "ui/object_selection_model_download.hpp"

#include "ai/segmentation_engine.hpp"
#include "ui/app_settings.hpp"
#include "ui/host_resolve.hpp"
#include "ui/qt_paths.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

#include <utility>

namespace patchy::ui {
namespace {

// Persisted settings key (never rename).
constexpr auto kHighQualitySettingKey = "tools/objectSelectModel";
constexpr auto kHighQualitySettingValue = "highQuality";
constexpr auto kStandardSettingValue = "standard";

// Follows at most this many redirects (GitHub sends release downloads to one storage host).
constexpr int kMaxRedirects = 5;
// A stalled transfer (no bytes for this long) fails instead of waiting forever.
constexpr int kTransferTimeoutMs = 60000;

QByteArray file_sha256_hex(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file)) {
    return {};
  }
  return hash.result().toHex();
}

bool file_matches(const QString& path, const DownloadableModelFile& file) {
  return QFileInfo(path).size() == file.size && file_sha256_hex(path) == file.sha256_hex;
}

// One download job: the files in order, each fetched by hand through its redirects.
class ModelDownloadJob final : public ModelDownload, public std::enable_shared_from_this<ModelDownloadJob> {
public:
  ModelDownloadJob(QObject* owner, QString folder, std::vector<DownloadableModelFile> files,
                   std::function<void(const ModelDownloadProgress&)> progress,
                   std::function<void(ModelDownloadResult)> finished)
      : owner_(owner),
        folder_(std::move(folder)),
        files_(std::move(files)),
        progress_(std::move(progress)),
        finished_(std::move(finished)) {
    for (const auto& file : files_) {
      total_ += file.size;
    }
  }

  void start() { next_file(); }

  void cancel() override {
    if (done_) {
      return;
    }
    cancelled_ = true;
    if (reply_ != nullptr) {
      reply_->abort();  // finished() follows and reports the cancel
    } else {
      finish({false, true, {}});
    }
  }

private:
  void next_file() {
    while (index_ < files_.size()) {
      const auto& file = files_[index_];
      const auto path = QDir(folder_).filePath(file.name);
      if (!file_matches(path, file)) {
        break;
      }
      completed_ += file.size;
      ++index_;
    }
    if (index_ == files_.size()) {
      report_progress(0);
      finish({true, false, {}});
      return;
    }
    if (!QDir().mkpath(folder_)) {
      finish({false, false, QStringLiteral("cannot create %1").arg(folder_)});
      return;
    }
    redirects_ = 0;
    url_index_ = 0;
    received_ = 0;
    report_progress(0);
    request(current_url());
  }

  [[nodiscard]] QUrl current_url() const {
    const auto& file = files_[index_];
    return url_index_ == 0 ? file.url : file.fallback_urls[url_index_ - 1];
  }

  // A link that could not serve the file: the next fallback link, else the failure.
  void fail_or_fall_back(ModelDownloadResult result) {
    output_.reset();  // an uncommitted QSaveFile discards its temporary file
    if (!cancelled_ && index_ < files_.size() && url_index_ < files_[index_].fallback_urls.size()) {
      ++url_index_;
      redirects_ = 0;
      received_ = 0;
      report_progress(0);
      request(current_url());
      return;
    }
    finish(std::move(result));
  }

  void request(const QUrl& url) {
    if (cancelled_) {
      finish({false, true, {}});
      return;
    }
    if (url.isLocalFile()) {
      get(url);  // tests serve the files from disk; there is no host to resolve
      return;
    }
    const std::weak_ptr<ModelDownloadJob> weak = shared_from_this();
    resolve_host_first(owner_, url.host(), [weak, url](bool resolved) {
      if (auto self = weak.lock()) {
        if (!resolved) {
          self->fail_or_fall_back({false, false, QStringLiteral("could not reach %1").arg(url.host())});
        } else {
          self->get(url);
        }
      }
    });
  }

  void get(const QUrl& url) {
    if (cancelled_) {
      finish({false, true, {}});
      return;
    }
    if (owner_ == nullptr) {
      return;
    }
    if (manager_ == nullptr) {
      manager_ = new QNetworkAccessManager(owner_);
    }
    const auto& file = files_[index_];
    output_ = std::make_unique<QSaveFile>(QDir(folder_).filePath(file.name));
    if (!output_->open(QIODevice::WriteOnly)) {
      finish({false, false, QStringLiteral("cannot write %1: %2").arg(output_->fileName(), output_->errorString())});
      return;
    }
    hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    received_ = 0;
    QNetworkRequest request(url);
    // Redirects are followed by hand so each new host resolves first (host_resolve.hpp).
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(kTransferTimeoutMs);
    reply_ = manager_->get(request);
    const std::weak_ptr<ModelDownloadJob> weak = shared_from_this();
    QObject::connect(reply_, &QNetworkReply::readyRead, owner_, [weak] {
      if (auto self = weak.lock()) {
        self->read_available();
      }
    });
    QObject::connect(reply_, &QNetworkReply::finished, owner_, [weak] {
      if (auto self = weak.lock()) {
        self->reply_finished();
      }
    });
  }

  void read_available() {
    if (reply_ == nullptr || output_ == nullptr) {
      return;
    }
    const auto status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400) {
      reply_->readAll();  // a redirect body; the Location header is what counts
      return;
    }
    const auto chunk = reply_->readAll();
    if (chunk.isEmpty()) {
      return;
    }
    received_ += chunk.size();
    if (received_ > files_[index_].size) {
      reply_->abort();
      return;
    }
    hash_->addData(chunk);
    if (output_->write(chunk) != chunk.size()) {
      write_failed_ = true;
      reply_->abort();
      return;
    }
    report_progress(received_);
  }

  void reply_finished() {
    auto* reply = reply_;
    reply_ = nullptr;
    reply->deleteLater();
    if (cancelled_) {
      output_.reset();  // an uncommitted QSaveFile discards its temporary file
      finish({false, true, {}});
      return;
    }
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && status >= 300 && status < 400) {
      output_.reset();
      const auto target = reply->url().resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
      if (++redirects_ > kMaxRedirects || !target.isValid() || target.scheme() != QStringLiteral("https")) {
        fail_or_fall_back({false, false, QStringLiteral("unexpected redirect for %1").arg(files_[index_].name)});
        return;
      }
      request(target);
      return;
    }
    read_available();
    const auto& file = files_[index_];
    if (write_failed_) {
      output_.reset();
      finish({false, false, QStringLiteral("could not write %1").arg(file.name)});
      return;
    }
    if (reply->error() != QNetworkReply::NoError) {
      fail_or_fall_back({false, false, reply->errorString()});
      return;
    }
    if (received_ != file.size || hash_->result().toHex() != file.sha256_hex) {
      fail_or_fall_back({false, false, QStringLiteral("%1 did not match its expected checksum").arg(file.name)});
      return;
    }
    if (!output_->commit()) {
      const auto error = output_->errorString();
      output_.reset();
      finish({false, false, QStringLiteral("could not save %1: %2").arg(file.name, error)});
      return;
    }
    output_.reset();
    completed_ += file.size;
    ++index_;
    next_file();
  }

  void report_progress(qint64 current_file_bytes) {
    if (!progress_ || owner_ == nullptr) {
      return;
    }
    ModelDownloadProgress progress;
    progress.received = completed_ + current_file_bytes;
    progress.total = total_;
    progress.file_count = static_cast<int>(files_.size());
    if (index_ < files_.size()) {
      const auto& file = files_[index_];
      progress.file_number = static_cast<int>(index_) + 1;
      progress.file_name = file.name;
      progress.file_size = file.size;
      progress.file_received = current_file_bytes;
      progress.url = current_url();
    } else {
      progress.file_number = progress.file_count;
    }
    progress_(progress);
  }

  void finish(ModelDownloadResult result) {
    if (done_) {
      return;
    }
    done_ = true;
    if (manager_ != nullptr) {
      manager_->deleteLater();
      manager_ = nullptr;
    }
    if (finished_ && owner_ != nullptr) {
      finished_(std::move(result));
    }
  }

  QPointer<QObject> owner_;
  QString folder_;
  std::vector<DownloadableModelFile> files_;
  std::function<void(const ModelDownloadProgress&)> progress_;
  std::function<void(ModelDownloadResult)> finished_;
  QNetworkAccessManager* manager_{nullptr};
  QNetworkReply* reply_{nullptr};
  std::unique_ptr<QSaveFile> output_;
  std::unique_ptr<QCryptographicHash> hash_;
  std::size_t index_{0};
  std::size_t url_index_{0};  // 0: the file's url, then its fallback_urls in order
  int redirects_{0};
  qint64 total_{0};
  qint64 completed_{0};
  qint64 received_{0};
  bool write_failed_{false};
  bool cancelled_{false};
  bool done_{false};
};

}  // namespace

const std::vector<DownloadableModelFile>& high_quality_model_files() {
  static const std::vector<DownloadableModelFile> files{
      {QStringLiteral("efficientvit_sam_xl0_encoder_fp16.onnx"),
       QUrl(QStringLiteral("https://github.com/SethRobinson/Patchy/releases/download/ai-models-2/"
                           "efficientvit_sam_xl0_encoder_fp16.onnx")),
       226052787, QByteArrayLiteral("a28cf6978672e202b597e948f71c09a31267ee947d26579f6ca290281e5bf101")},
      {QStringLiteral("efficientvit_sam_xl0_decoder_multi_fp16.onnx"),
       QUrl(QStringLiteral("https://github.com/SethRobinson/Patchy/releases/download/ai-models-2/"
                           "efficientvit_sam_xl0_decoder_multi_fp16.onnx")),
       8368857, QByteArrayLiteral("632613ec55ebd52a72f0a1933c297ce67b8563ff2e970b0c2eea059373ba496b")},
  };
  return files;
}

qint64 high_quality_model_download_bytes() {
  qint64 total = 0;
  for (const auto& file : high_quality_model_files()) {
    total += file.size;
  }
  return total;
}

namespace {

QString default_downloaded_models_folder() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("ai-models"));
}

}  // namespace

QString downloaded_models_folder() {
  // The engine's folder once configured (tests point it at scratch folders), so downloads,
  // the Preferences status and Delete always agree with where the engine looks.
  const auto configured = patchy::ai::downloaded_model_directory();
  return configured.empty() ? default_downloaded_models_folder() : to_qstring(configured);
}

bool object_selection_high_quality_setting() {
  return app_settings().value(QLatin1String(kHighQualitySettingKey), QLatin1String(kHighQualitySettingValue)).toString() !=
         QLatin1String(kStandardSettingValue);
}

void set_object_selection_high_quality_setting(bool high_quality) {
  app_settings().setValue(QLatin1String(kHighQualitySettingKey),
                          QLatin1String(high_quality ? kHighQualitySettingValue : kStandardSettingValue));
  patchy::ai::set_high_quality_model_enabled(high_quality);
}

void configure_downloaded_object_selection_model() {
#if !defined(Q_OS_WASM)
  if (patchy::ai::downloaded_model_directory().empty()) {
    patchy::ai::set_downloaded_model_directory(to_filesystem_path(default_downloaded_models_folder()));
  }
  patchy::ai::set_high_quality_model_enabled(object_selection_high_quality_setting());
#endif
}

bool downloaded_high_quality_model_present(const QString& folder) {
  for (const auto& file : high_quality_model_files()) {
    if (!QFileInfo(QDir(folder).filePath(file.name)).isFile()) {
      return false;
    }
  }
  return true;
}

bool delete_downloaded_high_quality_model(const QString& folder, QString* error) {
  if (folder.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("the models folder is not set");
    }
    return false;
  }
  QStringList failed;
  for (const auto& file : high_quality_model_files()) {
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

QString model_download_source_text(const std::vector<DownloadableModelFile>& files) {
  if (files.empty()) {
    return {};
  }
  const auto& url = files.front().url;
  const auto parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
  // https://github.com/<owner>/<repo>/releases/download/<tag>/<file>
  if (url.host() == QStringLiteral("github.com") && parts.size() >= 5 && parts[2] == QStringLiteral("releases") &&
      parts[3] == QStringLiteral("download")) {
    return QCoreApplication::translate("ModelDownload", "%1 (release %2)")
        .arg(QStringLiteral("github.com/%1/%2").arg(parts[0], parts[1]), parts[4]);
  }
  return url.host();
}

std::shared_ptr<ModelDownload> start_model_download(QObject* owner, QString folder,
                                                    std::vector<DownloadableModelFile> files,
                                                    std::function<void(const ModelDownloadProgress&)> progress,
                                                    std::function<void(ModelDownloadResult)> finished) {
  auto job = std::make_shared<ModelDownloadJob>(owner, std::move(folder), std::move(files), std::move(progress),
                                                std::move(finished));
  job->start();
  return job;
}

}  // namespace patchy::ui
