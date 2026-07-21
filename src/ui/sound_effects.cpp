// Script sound effects (docs/scripting.md): a tiny deterministic tone synth
// plus fire-and-forget WAV playback with zero library dependencies. Windows
// plays through winmm's PlaySound, macOS through NSSound (sound_effects_mac.mm),
// and Linux/other spawns a detached paplay/pw-play/aplay. Qt Multimedia is
// deliberately not used (absent from the vendored Qt; adding it would grow
// provisioning and packaging on every platform).

#include "ui/sound_effects.hpp"

#include <QFile>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdint>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX  // windows.h min/max macros break std::clamp/min/max
#endif
#include <windows.h>

#include <mmsystem.h>
#elif !defined(Q_OS_MACOS)
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#endif

namespace patchy::ui {

namespace {

constexpr int kSampleRate = 44100;
constexpr double kPi = 3.14159265358979323846;

void append_u16(QByteArray& out, std::uint16_t value) {
  out.append(static_cast<char>(value & 0xff));
  out.append(static_cast<char>((value >> 8) & 0xff));
}

void append_u32(QByteArray& out, std::uint32_t value) {
  out.append(static_cast<char>(value & 0xff));
  out.append(static_cast<char>((value >> 8) & 0xff));
  out.append(static_cast<char>((value >> 16) & 0xff));
  out.append(static_cast<char>((value >> 24) & 0xff));
}

bool sound_disabled() { return qEnvironmentVariableIsSet("PATCHY_NO_SOUND"); }

}  // namespace

QByteArray build_tone_wav(double frequency_hz, int duration_ms, double volume, ToneWave wave) {
  frequency_hz = std::clamp(frequency_hz, 20.0, 20000.0);
  duration_ms = std::clamp(duration_ms, 1, 4000);
  volume = std::clamp(volume, 0.0, 1.0);
  const int sample_count = std::max(1, kSampleRate * duration_ms / 1000);
  // 0.6 leaves headroom; a square wave carries more energy than a sine of the
  // same peak, so it is pulled down to sound equally loud.
  const double amplitude =
      volume * (wave == ToneWave::Square ? 0.45 : 0.6) * 32767.0;
  const int attack_samples = std::min(sample_count / 4, kSampleRate * 5 / 1000);
  const int release_samples = std::min(sample_count / 4, kSampleRate * 5 / 1000);
  const double omega = 2.0 * kPi * frequency_hz / kSampleRate;

  QByteArray data;
  data.reserve(sample_count * 2);
  for (int i = 0; i < sample_count; ++i) {
    double value = std::sin(omega * i);
    if (wave == ToneWave::Square) {
      value = value >= 0.0 ? 1.0 : -1.0;
    }
    double envelope = std::exp(-3.0 * i / sample_count);  // plucky blip, not an organ tone
    if (attack_samples > 0 && i < attack_samples) {
      envelope *= static_cast<double>(i) / attack_samples;
    }
    if (release_samples > 0 && i >= sample_count - release_samples) {
      envelope *= static_cast<double>(sample_count - i) / release_samples;
    }
    const auto sample =
        static_cast<std::int16_t>(std::lround(value * envelope * amplitude));
    data.append(static_cast<char>(sample & 0xff));
    data.append(static_cast<char>((sample >> 8) & 0xff));
  }

  QByteArray wav;
  wav.reserve(44 + data.size());
  wav.append("RIFF");
  append_u32(wav, 36 + static_cast<std::uint32_t>(data.size()));
  wav.append("WAVE");
  wav.append("fmt ");
  append_u32(wav, 16);
  append_u16(wav, 1);  // PCM
  append_u16(wav, 1);  // mono
  append_u32(wav, kSampleRate);
  append_u32(wav, kSampleRate * 2);  // byte rate
  append_u16(wav, 2);                // block align
  append_u16(wav, 16);               // bits per sample
  wav.append("data");
  append_u32(wav, static_cast<std::uint32_t>(data.size()));
  wav.append(data);
  return wav;
}

#ifdef Q_OS_WIN

void play_wav_bytes(QByteArray wav) {
  if (wav.isEmpty() || sound_disabled()) {
    return;
  }
  // PlaySound reads the buffer asynchronously, so it must outlive the call;
  // the static keeps it alive until the next sound (which cancels the current
  // one - acceptable for game blips, and how PlaySound behaves anyway).
  static QByteArray playing;
  playing = std::move(wav);
  PlaySoundW(reinterpret_cast<LPCWSTR>(playing.constData()), nullptr,
             SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void play_wav_file(const QString& path) {
  if (sound_disabled()) {
    return;
  }
  // Through the memory path: SND_FILENAME keeps the file in use for the whole
  // async playback, and callers validated size and header already.
  QFile file(path);
  if (file.open(QIODevice::ReadOnly)) {
    play_wav_bytes(file.readAll());
  }
}

#elif !defined(Q_OS_MACOS)

namespace {

// paplay covers PulseAudio and PipeWire's pulse shim; pw-play covers bare
// PipeWire; aplay (ALSA) is the last resort. None installed = silent no-op.
QStringList player_command(const QString& wav_path) {
  const auto paplay = QStandardPaths::findExecutable(QStringLiteral("paplay"));
  if (!paplay.isEmpty()) {
    return {paplay, wav_path};
  }
  const auto pw_play = QStandardPaths::findExecutable(QStringLiteral("pw-play"));
  if (!pw_play.isEmpty()) {
    return {pw_play, wav_path};
  }
  const auto aplay = QStandardPaths::findExecutable(QStringLiteral("aplay"));
  if (!aplay.isEmpty()) {
    return {aplay, QStringLiteral("-q"), wav_path};
  }
  return {};
}

}  // namespace

void play_wav_bytes(QByteArray wav) {
  if (wav.isEmpty() || sound_disabled()) {
    return;
  }
  // The detached player streams from disk, so the temp file must outlive it:
  // tones cap at 4 s, so a lazy timer delete is plenty.
  static int counter = 0;
  const auto path = QDir::temp().absoluteFilePath(
      QStringLiteral("patchy-sfx-%1-%2.wav")
          .arg(QCoreApplication::applicationPid())
          .arg(counter++));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return;
  }
  file.write(wav);
  file.close();
  const auto command = player_command(path);
  if (command.isEmpty()) {
    QFile::remove(path);
    return;
  }
  QProcess::startDetached(command.first(), command.mid(1));
  QTimer::singleShot(15000, [path] { QFile::remove(path); });
}

void play_wav_file(const QString& path) {
  if (sound_disabled()) {
    return;
  }
  const auto command = player_command(path);
  if (!command.isEmpty()) {
    QProcess::startDetached(command.first(), command.mid(1));
  }
}

#endif  // platform playback (macOS lives in sound_effects_mac.mm)

}  // namespace patchy::ui
