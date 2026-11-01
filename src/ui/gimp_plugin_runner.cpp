// Running a GIMP 3 plug-in through the helper (gimp_plugin_runner.hpp).

#include "ui/gimp_plugin_runner.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
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

}  // namespace

GimpPluginRunResult run_gimp_plugin_out_of_process(const GimpPluginRunInput& input,
                                                   const GimpPluginRunCallbacks& callbacks) {
  const std::uint64_t pixel_bytes = static_cast<std::uint64_t>(input.width) * static_cast<std::uint64_t>(input.height) *
                                    input.channels * (input.bit_depth / 8);
  if (input.input == nullptr || input.input->size() != pixel_bytes || pixel_bytes == 0) {
    return error_result(QStringLiteral("pixels"), QStringLiteral("the layer's pixels do not match its size"));
  }
  const std::uint64_t mask_bytes =
      input.mask != nullptr ? static_cast<std::uint64_t>(input.width) * static_cast<std::uint64_t>(input.height) : 0;
  PixelFile pixels;
  if (!pixels.create(pixel_bytes * 2 + mask_bytes)) {
    return error_result(QStringLiteral("pixels"), QStringLiteral("could not create the shared pixel file"));
  }
  std::memcpy(pixels.data(), input.input->data(), pixel_bytes);
  std::memcpy(pixels.data() + pixel_bytes, input.input->data(), pixel_bytes);
  if (input.mask != nullptr) {
    std::memcpy(pixels.data() + 2 * pixel_bytes, input.mask->data(), mask_bytes);
  }

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
  request.pixel_file = QDir::toNativeSeparators(pixels.path()).toStdString();
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

#ifdef Q_OS_WIN
  // The plug-in's dialog is another process's window: let it take the foreground.
  AllowSetForegroundWindow(ASFW_ANY);
#endif
  QProcess helper;
  helper.setProgram(input.helper);
  helper.setProcessChannelMode(QProcess::SeparateChannels);
  helper.start();
  if (!helper.waitForStarted(10000)) {
    return error_result(QStringLiteral("start"), QStringLiteral("the GIMP plug-in helper could not be started"));
  }
  const auto frame = gimp::link::encode(request);
  helper.write(reinterpret_cast<const char*>(frame.data()), static_cast<qint64>(frame.size()));

  GimpPluginRunResult result;
  bool have_result = false;
  bool cancel_sent = false;
  std::uint64_t plugin_pid = 0;
  QElapsedTimer window_timer;
  int answers_sent = 0;
  QElapsedTimer last_answer;
  QElapsedTimer cancel_timer;
  gimp::link::FrameBuffer buffer;
  for (;;) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    helper.waitForReadyRead(10);
    const auto bytes = helper.readAllStandardOutput();
    if (!bytes.isEmpty()) {
      buffer.append(reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size()));
    }
    const auto errors = helper.readAllStandardError();
    if (!errors.isEmpty() && input.trace) {
      qWarning("patchy-gimp-host: %s", errors.constData());
    }
    try {
      while (auto f = buffer.next()) {
        const auto& payload = f->second;
        switch (f->first) {
          case gimp::link::Frame::Log:
            if (input.trace) {
              qWarning("gimp: %s", gimp::link::decode_text(payload).c_str());
            }
            break;
          case gimp::link::Frame::Progress: {
            const auto p = gimp::link::decode_progress(payload);
            if (callbacks.progress) callbacks.progress(p.fraction, QString::fromStdString(p.text));
            break;
          }
          case gimp::link::Frame::Message: {
            const auto text = QString::fromStdString(gimp::link::decode_text(payload)).trimmed();
            if (!text.isEmpty()) {
              result.messages << text;
              if (callbacks.message) callbacks.message(text);
            }
            break;
          }
          case gimp::link::Frame::Update: {
            const auto [rect, flags] = gimp::link::decode_rect(payload);
            (void)flags;
            if (callbacks.update) {
              callbacks.update(QRect(rect.x, rect.y, rect.width, rect.height), pixels.data() + pixel_bytes);
            }
            break;
          }
          case gimp::link::Frame::PluginWindow: {
            const auto [rect, visible] = gimp::link::decode_rect(payload);
            if (visible != 0 && plugin_pid == 0) {
              window_timer.start();
            }
            plugin_pid = visible;
            if (callbacks.plugin_window) {
              callbacks.plugin_window(visible != 0 ? QRect(rect.x, rect.y, rect.width, rect.height) : QRect());
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
            have_result = true;
            break;
          }
          default:
            break;
        }
      }
    } catch (const gimp::link::LinkError& e) {
      helper.kill();
      helper.waitForFinished(3000);
      return error_result(QStringLiteral("link"), QString::fromUtf8(e.what()));
    }
    if (have_result) {
      break;
    }
    if (callbacks.tick) callbacks.tick();
    // Scripts and tests answer the plug-in's dialog once it has settled (and its
    // preview has drawn): an image of it first, then OK or Cancel; again every three
    // seconds while it stays up, at most three times.
    if (plugin_pid != 0 && (input.answer_dialog != 0 || !input.capture_dialog_path.isEmpty()) &&
        window_timer.isValid() && window_timer.elapsed() > input.settle_ms && answers_sent < 3 &&
        (answers_sent == 0 || last_answer.elapsed() > 3000)) {
      if (const auto window = gimp_plugin_window_for_process(plugin_pid); window != 0) {
        if (answers_sent == 0 && !input.capture_dialog_path.isEmpty()) {
          (void)capture_gimp_plugin_window(window, input.capture_dialog_path);
        }
        if (input.answer_dialog != 0) {
          (void)answer_gimp_plugin_window(window, input.answer_dialog == 1, answers_sent);
          ++answers_sent;
          last_answer.start();
        } else {
          answers_sent = 3;  // captured; the dialog is the user's
        }
      }
    }
    if (!cancel_sent && callbacks.cancelled && callbacks.cancelled()) {
      gimp::link::Writer w;
      const auto cancel = w.frame(gimp::link::Frame::Cancel);
      helper.write(reinterpret_cast<const char*>(cancel.data()), static_cast<qint64>(cancel.size()));
      cancel_sent = true;
      cancel_timer.start();
    }
    if (cancel_sent && cancel_timer.elapsed() > 5000) {
      helper.kill();
      helper.waitForFinished(3000);
      GimpPluginRunResult r;
      r.status = GimpPluginRunStatus::Cancelled;
      r.code = QStringLiteral("cancelled");
      return r;
    }
    if (helper.state() == QProcess::NotRunning && helper.bytesAvailable() == 0) {
      // Read whatever was still buffered, then give up.
      const auto rest = helper.readAllStandardOutput();
      if (!rest.isEmpty()) {
        buffer.append(reinterpret_cast<const std::uint8_t*>(rest.constData()), static_cast<std::size_t>(rest.size()));
        continue;
      }
      return error_result(QStringLiteral("helper-crashed"),
                          QStringLiteral("the GIMP plug-in helper ended unexpectedly (exit code %1)").arg(helper.exitCode()));
    }
  }
  helper.closeWriteChannel();
  if (!helper.waitForFinished(5000)) {
    helper.kill();
    helper.waitForFinished(3000);
  }
  if (result.status == GimpPluginRunStatus::Ok) {
    result.output.assign(pixels.data() + pixel_bytes, pixels.data() + 2 * pixel_bytes);
  }
  return result;
}

}  // namespace patchy::ui
