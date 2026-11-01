// Running a GIMP 3 plug-in through the helper (gimp_plugin_runner.hpp).

#include "ui/gimp_plugin_runner.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QUuid>

#include <cstring>

#include "ui/gimp_plugin_window.hpp"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace patchy::ui {

namespace {

// The pixel file: removed however the run ends.
class PixelFile {
public:
  ~PixelFile() {
    if (mapped_ != nullptr) {
      file_.unmap(mapped_);
    }
    file_.close();
    if (!path_.isEmpty()) {
      QFile::remove(path_);
    }
  }
  bool create(std::uint64_t size) {
    path_ = QDir(QDir::tempPath())
                .filePath(QStringLiteral("patchy-gimp-%1.pixels").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    file_.setFileName(path_);
    if (!file_.open(QIODevice::ReadWrite) || !file_.resize(static_cast<qint64>(size))) {
      return false;
    }
    mapped_ = file_.map(0, static_cast<qint64>(size));
    return mapped_ != nullptr;
  }
  [[nodiscard]] std::uint8_t* data() const noexcept { return mapped_; }
  [[nodiscard]] const QString& path() const noexcept { return path_; }

private:
  QString path_;
  QFile file_;
  std::uint8_t* mapped_{nullptr};
};

GimpPluginRunResult error_result(QString code, QString message) {
  GimpPluginRunResult r;
  r.status = GimpPluginRunStatus::Error;
  r.code = std::move(code);
  r.message = std::move(message);
  return r;
}

gimp::link::RunRequest make_request(const GimpPluginRunInput& input, const QString& pixel_path, std::uint64_t pixel_bytes,
                                    std::uint64_t mask_bytes) {
  gimp::link::RunRequest request;
  request.launch = input.launch;
  request.procedure = input.procedure.toStdString();
  request.install.assign(input.install.begin(), input.install.end());
  request.run_mode = input.run_mode;
  request.arguments.assign(input.arguments.begin(), input.arguments.end());
  request.argument_count = input.argument_count;
  request.image_width = input.image_width;
  request.image_height = input.image_height;
  request.layer_x = input.layer_x;
  request.layer_y = input.layer_y;
  request.layer_width = input.width;
  request.layer_height = input.height;
  request.bit_depth = input.bit_depth;
  request.channels = input.channels;
  request.protect_alpha = input.protect_alpha;
  request.layer_name = input.layer_name.toStdString();
  request.image_name = input.image_name.toStdString();
  request.resolution_x = input.resolution;
  request.resolution_y = input.resolution;
  request.has_mask = input.mask != nullptr;
  request.mask_x = input.mask_bounds.x();
  request.mask_y = input.mask_bounds.y();
  request.mask_width = input.mask_bounds.width();
  request.mask_height = input.mask_bounds.height();
  request.pixel_file = QDir::toNativeSeparators(pixel_path).toStdString();
  request.pixel_file_size = pixel_bytes * 2 + mask_bytes;
  request.input_offset = 0;
  request.output_offset = pixel_bytes;
  request.mask_offset = 2 * pixel_bytes;
  std::memcpy(request.foreground, input.foreground, sizeof request.foreground);
  std::memcpy(request.background, input.background, sizeof request.background);
  request.parent_window = input.parent_window;
  request.language = input.language.toStdString();
  request.timeout_ms = input.timeout_ms;
  request.stored_data = input.stored_data;
  request.trace = input.trace;
  request.wire_log = input.wire_log.toStdString();
  request.color_profile = input.color_profile;
  return request;
}

}  // namespace

struct GimpPluginRun::Shared {
  PixelFile pixels;
  std::uint64_t pixel_bytes{0};
  gimp::link::FrameBuffer buffer;
  GimpPluginRunResult result;
  bool trace{false};
  QString capture_dialog_path;
  int answer_dialog{0};
  int settle_ms{1500};
  bool cancel_sent{false};
  std::uint64_t plugin_pid{0};
  QElapsedTimer window_timer;
  int answers_sent{0};
  QElapsedTimer last_answer;
  QElapsedTimer cancel_timer;
};

GimpPluginRun::GimpPluginRun(QObject* parent) : QObject(parent) {}

GimpPluginRun::~GimpPluginRun() {
  if (helper_ != nullptr && helper_->state() != QProcess::NotRunning) {
    // The link closing ends the plug-in tree; the helper goes with it.
    helper_->closeWriteChannel();
    helper_->kill();
    helper_->waitForFinished(2000);
  }
}

void GimpPluginRun::start(const GimpPluginRunInput& input, GimpPluginRunCallbacks callbacks,
                          std::function<void(GimpPluginRunResult)> finished) {
  callbacks_ = std::move(callbacks);
  finished_ = std::move(finished);
  state_ = State::Running;
  shared_ = std::make_unique<Shared>();
  auto& s = *shared_;
  s.trace = input.trace;
  s.capture_dialog_path = input.capture_dialog_path;
  s.answer_dialog = input.answer_dialog;
  s.settle_ms = input.settle_ms;
  const auto fail_later = [this](QString code, QString message) {
    QTimer::singleShot(0, this, [this, code, message] { finish(error_result(code, message)); });
  };

  s.pixel_bytes = static_cast<std::uint64_t>(input.width) * static_cast<std::uint64_t>(input.height) * input.channels *
                  (input.bit_depth / 8);
  if (input.input == nullptr || input.input->size() != s.pixel_bytes || s.pixel_bytes == 0) {
    fail_later(QStringLiteral("pixels"), QStringLiteral("the layer's pixels do not match its size"));
    return;
  }
  const std::uint64_t mask_bytes =
      input.mask != nullptr ? static_cast<std::uint64_t>(input.width) * static_cast<std::uint64_t>(input.height) : 0;
  if (!s.pixels.create(s.pixel_bytes * 2 + mask_bytes)) {
    fail_later(QStringLiteral("pixels"), QStringLiteral("could not create the shared pixel file"));
    return;
  }
  std::memcpy(s.pixels.data(), input.input->data(), s.pixel_bytes);
  std::memcpy(s.pixels.data() + s.pixel_bytes, input.input->data(), s.pixel_bytes);
  if (input.mask != nullptr) {
    std::memcpy(s.pixels.data() + 2 * s.pixel_bytes, input.mask->data(), mask_bytes);
  }
  const auto request = make_request(input, s.pixels.path(), s.pixel_bytes, mask_bytes);

#ifdef Q_OS_WIN
  // The plug-in's dialog is another process's window: let it take the foreground.
  AllowSetForegroundWindow(ASFW_ANY);
#endif
  helper_ = new QProcess(this);
  helper_->setProgram(input.helper);
  helper_->setProcessChannelMode(QProcess::SeparateChannels);
  connect(helper_, &QProcess::readyReadStandardOutput, this, [this] { read_frames(); });
  connect(helper_, &QProcess::readyReadStandardError, this, [this] {
    const auto errors = helper_->readAllStandardError();
    if (!errors.isEmpty() && shared_ != nullptr && shared_->trace) {
      qWarning("patchy-gimp-host: %s", errors.constData());
    }
  });
  connect(helper_, &QProcess::finished, this, [this] { helper_ended(); });
  connect(helper_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
      helper_ended();
    }
  });
  helper_->start();
  const auto frame = gimp::link::encode(request);
  helper_->write(reinterpret_cast<const char*>(frame.data()), static_cast<qint64>(frame.size()));

  timer_ = new QTimer(this);
  timer_->setInterval(20);
  connect(timer_, &QTimer::timeout, this, [this] { on_tick(); });
  timer_->start();
}

void GimpPluginRun::read_frames() {
  if (helper_ == nullptr || shared_ == nullptr) {
    return;
  }
  auto& s = *shared_;
  const auto bytes = helper_->readAllStandardOutput();
  if (!bytes.isEmpty()) {
    s.buffer.append(reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size()));
  }
  if (state_ != State::Running) {
    return;
  }
  auto& result = s.result;
  try {
    while (auto f = s.buffer.next()) {
      const auto& payload = f->second;
      switch (f->first) {
        case gimp::link::Frame::Log:
          if (s.trace) {
            qWarning("gimp: %s", gimp::link::decode_text(payload).c_str());
          }
          break;
        case gimp::link::Frame::Progress: {
          const auto p = gimp::link::decode_progress(payload);
          if (callbacks_.progress) callbacks_.progress(p.fraction, QString::fromStdString(p.text));
          break;
        }
        case gimp::link::Frame::Message: {
          const auto text = QString::fromStdString(gimp::link::decode_text(payload)).trimmed();
          if (!text.isEmpty()) {
            result.messages << text;
            if (callbacks_.message) callbacks_.message(text);
          }
          break;
        }
        case gimp::link::Frame::Update: {
          const auto [rect, flags] = gimp::link::decode_rect(payload);
          (void)flags;
          if (callbacks_.update) {
            callbacks_.update(QRect(rect.x, rect.y, rect.width, rect.height), s.pixels.data() + s.pixel_bytes);
          }
          break;
        }
        case gimp::link::Frame::PluginWindow: {
          const auto [rect, visible] = gimp::link::decode_rect(payload);
          if (visible != 0 && s.plugin_pid == 0) {
            s.window_timer.start();
          }
          s.plugin_pid = visible;
          if (callbacks_.plugin_window) {
            callbacks_.plugin_window(visible != 0 ? QRect(rect.x, rect.y, rect.width, rect.height) : QRect());
          }
          break;
        }
        case gimp::link::Frame::Unsupported:
          result.unsupported_calls << QString::fromStdString(gimp::link::decode_text(payload));
          break;
        case gimp::link::Frame::RunResult: {
          const auto r = gimp::link::decode_run_result(payload);
          result.status = r.status == gimp::link::RunStatus::Ok          ? GimpPluginRunStatus::Ok
                          : r.status == gimp::link::RunStatus::Cancelled ? GimpPluginRunStatus::Cancelled
                                                                         : GimpPluginRunStatus::Error;
          result.code = QString::fromStdString(r.code);
          result.detail = QString::fromStdString(r.detail);
          result.message = QString::fromStdString(r.message);
          result.changed = r.changed;
          result.changed_rect = QRect(r.changed_x, r.changed_y, r.changed_width, r.changed_height);
          result.stored_data = r.stored_data;
          result.foreground_changed = r.foreground_changed;
          result.background_changed = r.background_changed;
          std::memcpy(result.foreground, r.foreground, sizeof result.foreground);
          std::memcpy(result.background, r.background, sizeof result.background);
          for (const auto& name : r.unsupported_calls) {
            if (!result.unsupported_calls.contains(QString::fromStdString(name))) {
              result.unsupported_calls << QString::fromStdString(name);
            }
          }
          if (result.status == GimpPluginRunStatus::Ok) {
            result.output.assign(s.pixels.data() + s.pixel_bytes, s.pixels.data() + 2 * s.pixel_bytes);
          }
          auto done = std::move(result);
          finish(std::move(done));
          return;
        }
        default:
          break;
      }
      if (state_ != State::Running) {
        return;  // a callback cancelled or aborted the run
      }
    }
  } catch (const gimp::link::LinkError& e) {
    helper_->kill();
    finish(error_result(QStringLiteral("link"), QString::fromUtf8(e.what())));
  }
}

void GimpPluginRun::on_tick() {
  if (state_ != State::Running || shared_ == nullptr) {
    return;
  }
  auto& s = *shared_;
  if (callbacks_.tick) callbacks_.tick();
  if (state_ != State::Running) {
    return;
  }
  // Scripts and tests answer the plug-in's dialog once it has settled (and its
  // preview has drawn): an image of it first, then OK or Cancel; again every three
  // seconds while it stays up, at most three times.
  if (s.plugin_pid != 0 && (s.answer_dialog != 0 || !s.capture_dialog_path.isEmpty()) && s.window_timer.isValid() &&
      s.window_timer.elapsed() > s.settle_ms && s.answers_sent < 3 &&
      (s.answers_sent == 0 || s.last_answer.elapsed() > 3000)) {
    if (const auto window = gimp_plugin_window_for_process(s.plugin_pid); window != 0) {
      if (s.answers_sent == 0 && !s.capture_dialog_path.isEmpty()) {
        (void)capture_gimp_plugin_window(window, s.capture_dialog_path);
      }
      if (s.answer_dialog != 0) {
        (void)answer_gimp_plugin_window(window, s.answer_dialog == 1, s.answers_sent);
        ++s.answers_sent;
        s.last_answer.start();
      } else {
        s.answers_sent = 3;  // captured; the dialog is the user's
      }
    }
  }
  if (!s.cancel_sent && callbacks_.cancelled && callbacks_.cancelled()) {
    cancel();
  }
  if (s.cancel_sent && s.cancel_timer.elapsed() > 5000) {
    helper_->kill();
    GimpPluginRunResult r;
    r.status = GimpPluginRunStatus::Cancelled;
    r.code = QStringLiteral("cancelled");
    finish(std::move(r));
  }
}

void GimpPluginRun::cancel() {
  if (state_ != State::Running || shared_ == nullptr || shared_->cancel_sent || helper_ == nullptr) {
    return;
  }
  gimp::link::Writer w;
  const auto frame = w.frame(gimp::link::Frame::Cancel);
  helper_->write(reinterpret_cast<const char*>(frame.data()), static_cast<qint64>(frame.size()));
  shared_->cancel_sent = true;
  shared_->cancel_timer.start();
}

void GimpPluginRun::abort() {
  aborted_ = true;
  callbacks_ = {};
  finished_ = nullptr;
  state_ = State::Done;
  if (timer_ != nullptr) {
    timer_->stop();
  }
  if (helper_ == nullptr || helper_->state() == QProcess::NotRunning) {
    deleteLater();
    return;
  }
  // A closed link makes the helper end the plug-in tree and exit; it is killed if it
  // takes longer (the kill-on-close job or the death signal takes the plug-in along).
  helper_->closeWriteChannel();
  QPointer<QProcess> helper(helper_);
  QTimer::singleShot(1500, this, [helper] {
    if (helper != nullptr && helper->state() != QProcess::NotRunning) {
      helper->kill();
    }
  });
}

void GimpPluginRun::finish(GimpPluginRunResult result) {
  if (state_ != State::Running) {
    return;
  }
  state_ = State::Done;
  if (timer_ != nullptr) {
    timer_->stop();
  }
  if (helper_ != nullptr && helper_->state() != QProcess::NotRunning) {
    helper_->closeWriteChannel();
    QPointer<QProcess> helper(helper_);
    QTimer::singleShot(5000, this, [helper] {
      if (helper != nullptr && helper->state() != QProcess::NotRunning) {
        helper->kill();
      }
    });
  }
  callbacks_ = {};
  auto finished = std::move(finished_);
  finished_ = nullptr;
  if (finished) {
    finished(std::move(result));
  }
}

void GimpPluginRun::helper_ended() {
  if (aborted_) {
    deleteLater();
    return;
  }
  if (state_ != State::Running) {
    return;
  }
  // Whatever was still buffered may hold the result.
  read_frames();
  if (state_ != State::Running) {
    return;
  }
  if (shared_ != nullptr && shared_->cancel_sent) {
    GimpPluginRunResult r;
    r.status = GimpPluginRunStatus::Cancelled;
    r.code = QStringLiteral("cancelled");
    finish(std::move(r));
    return;
  }
  if (helper_->error() == QProcess::FailedToStart) {
    finish(error_result(QStringLiteral("start"), QStringLiteral("the GIMP plug-in helper could not be started")));
    return;
  }
  finish(error_result(QStringLiteral("helper-crashed"),
                      QStringLiteral("the GIMP plug-in helper ended unexpectedly (exit code %1)").arg(helper_->exitCode())));
}

GimpPluginRunResult run_gimp_plugin_out_of_process(const GimpPluginRunInput& input,
                                                   const GimpPluginRunCallbacks& callbacks) {
  GimpPluginRunResult result;
  bool done = false;
  auto run = std::make_unique<GimpPluginRun>();
  run->start(input, callbacks, [&result, &done](GimpPluginRunResult r) {
    result = std::move(r);
    done = true;
  });
  while (!done) {
    // The run's 20 ms timer wakes the wait.
    QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents);
  }
  return result;
}

}  // namespace patchy::ui
