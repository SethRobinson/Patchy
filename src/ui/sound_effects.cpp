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
#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX  // windows.h min/max macros break std::clamp/min/max
#endif
#include <windows.h>

#include <mmsystem.h>
#elif !defined(Q_OS_MACOS) && !defined(Q_OS_WASM)
// Q_OS_WASM is excluded: wasm Qt ships no QProcess at all (its no-op playback
// branch below needs none of these).
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QPointer>
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

#if !defined(Q_OS_MACOS)
bool sound_disabled() { return qEnvironmentVariableIsSet("PATCHY_NO_SOUND"); }
#endif

[[nodiscard]] std::uint32_t read_u32(const QByteArray& bytes, qsizetype offset) {
  const auto* data = reinterpret_cast<const unsigned char*>(bytes.constData()) + offset;
  return static_cast<std::uint32_t>(data[0]) | static_cast<std::uint32_t>(data[1]) << 8 |
         static_cast<std::uint32_t>(data[2]) << 16 | static_cast<std::uint32_t>(data[3]) << 24;
}

[[nodiscard]] std::uint16_t read_u16(const QByteArray& bytes, qsizetype offset) {
  const auto* data = reinterpret_cast<const unsigned char*>(bytes.constData()) + offset;
  return static_cast<std::uint16_t>(data[0] | data[1] << 8);
}

}  // namespace

bool is_well_formed_wav(const QByteArray& wav) {
  if (wav.size() < 12 || !wav.startsWith("RIFF") || wav.mid(8, 4) != "WAVE") {
    return false;
  }
  // The RIFF size is what the players read: it must not reach past the bytes.
  const auto riff_end = static_cast<qint64>(read_u32(wav, 4)) + 8;
  if (riff_end < 12 || riff_end > wav.size()) {
    return false;
  }
  bool have_format = false;
  qint64 offset = 12;
  while (offset + 8 <= riff_end) {
    const auto id = wav.mid(offset, 4);
    const auto size = static_cast<qint64>(read_u32(wav, offset + 4));
    const auto body = offset + 8;
    if (size > riff_end - body) {
      return false;
    }
    if (id == "fmt ") {
      if (size < 16) {
        return false;
      }
      const auto tag = read_u16(wav, body);
      const auto channels = read_u16(wav, body + 2);
      const auto rate = read_u32(wav, body + 4);
      const auto align = read_u16(wav, body + 12);
      const auto bits = read_u16(wav, body + 14);
      // PCM, IEEE float, or WAVE_FORMAT_EXTENSIBLE.
      if ((tag != 1 && tag != 3 && tag != 0xFFFE) || channels < 1 || channels > 8 || rate < 1000 ||
          rate > 384000 || (bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
          align != channels * (bits / 8)) {
        return false;
      }
      have_format = true;
    } else if (id == "data") {
      return have_format && size > 0;
    }
    offset = body + size + (size & 1);  // chunks are padded to an even size
  }
  return false;
}

WavPcm parse_wav(const QByteArray& wav) {
  WavPcm pcm;
  if (!is_well_formed_wav(wav)) {
    return pcm;
  }
  const auto riff_end = static_cast<qint64>(read_u32(wav, 4)) + 8;
  qint64 offset = 12;
  while (offset + 8 <= riff_end) {
    const auto id = wav.mid(offset, 4);
    const auto size = static_cast<qint64>(read_u32(wav, offset + 4));
    const auto body = offset + 8;
    if (id == "fmt ") {
      pcm.format_tag = read_u16(wav, body);
      pcm.channels = read_u16(wav, body + 2);
      pcm.sample_rate = read_u32(wav, body + 4);
      pcm.block_align = read_u16(wav, body + 12);
      pcm.format_chunk = wav.mid(body, size);
    } else if (id == "data") {
      // Whole sample frames only: a player handed a partial frame may click or refuse it.
      pcm.data = wav.mid(body, size - size % pcm.block_align);
      return pcm;
    }
    offset = body + size + (size & 1);
  }
  return pcm;
}

QByteArray make_wav(const QByteArray& format_chunk, const QByteArray& data) {
  const auto padded_format = format_chunk.size() + (format_chunk.size() & 1);
  QByteArray wav;
  wav.reserve(20 + padded_format + 8 + data.size() + 1);
  wav.append("RIFF");
  append_u32(wav, static_cast<std::uint32_t>(4 + 8 + padded_format + 8 + data.size() + (data.size() & 1)));
  wav.append("WAVE");
  wav.append("fmt ");
  append_u32(wav, static_cast<std::uint32_t>(format_chunk.size()));
  wav.append(format_chunk);
  if ((format_chunk.size() & 1) != 0) {
    wav.append('\0');
  }
  wav.append("data");
  append_u32(wav, static_cast<std::uint32_t>(data.size()));
  wav.append(data);
  if ((data.size() & 1) != 0) {
    wav.append('\0');
  }
  return wav;
}

int wav_duration_ms(const QByteArray& wav) {
  const auto pcm = parse_wav(wav);
  if (pcm.data.isEmpty()) {
    return 0;
  }
  const auto frames = pcm.data.size() / pcm.block_align;
  return static_cast<int>(frames * 1000 / pcm.sample_rate);
}

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

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WASM)

namespace {

// play_wav_loop's checks: the loop's samples (empty when refused) and the intro's,
// which stay only when they share the loop's format.
struct LoopParts {
  WavPcm loop;
  QByteArray intro_data;
};

LoopParts loop_parts(const QByteArray& intro, const QByteArray& loop) {
  LoopParts parts;
  if (sound_disabled() || wav_duration_ms(loop) < kMinWavLoopMs) {
    return parts;
  }
  parts.loop = parse_wav(loop);
  if (const auto intro_pcm = parse_wav(intro);
      !intro_pcm.data.isEmpty() && intro_pcm.format_chunk == parts.loop.format_chunk) {
    parts.intro_data = intro_pcm.data;
  }
  return parts;
}

}  // namespace

#endif

#ifdef Q_OS_WIN

namespace {

// The loop channel: its own waveOut device, the intro buffer queued ahead of a loop
// buffer marked to repeat (WHDR_BEGINLOOP | WHDR_ENDLOOP), so the hand-over and every
// repeat are sample-exact. The buffers stay alive until the device is reset.
struct WaveLoop {
  HWAVEOUT device = nullptr;
  QByteArray format;
  QByteArray intro;
  QByteArray loop;
  WAVEHDR intro_header{};
  WAVEHDR loop_header{};
};

WaveLoop& wave_loop() {
  static WaveLoop state;
  return state;
}

}  // namespace

void play_wav_loop(QByteArray intro, QByteArray loop) {
  stop_wav_loop();
  auto parts = loop_parts(intro, loop);
  if (parts.loop.data.isEmpty()) {
    return;
  }
  auto& state = wave_loop();
  // waveOut takes the "fmt " body as a WAVEFORMATEX (or WAVEFORMATEXTENSIBLE); a plain
  // 16-byte PCM body lacks the cbSize field.
  state.format = parts.loop.format_chunk;
  if (state.format.size() < 18) {
    state.format.append(18 - state.format.size(), '\0');
  }
  if (waveOutOpen(&state.device, WAVE_MAPPER, reinterpret_cast<const WAVEFORMATEX*>(state.format.constData()), 0, 0,
                  CALLBACK_NULL) != MMSYSERR_NOERROR) {
    state = WaveLoop{};
    return;
  }
  state.intro = std::move(parts.intro_data);
  state.loop = std::move(parts.loop.data);
  if (!state.intro.isEmpty()) {
    state.intro_header.lpData = state.intro.data();
    state.intro_header.dwBufferLength = static_cast<DWORD>(state.intro.size());
    waveOutPrepareHeader(state.device, &state.intro_header, sizeof(WAVEHDR));
    waveOutWrite(state.device, &state.intro_header, sizeof(WAVEHDR));
  }
  state.loop_header.lpData = state.loop.data();
  state.loop_header.dwBufferLength = static_cast<DWORD>(state.loop.size());
  // Prepare wants dwFlags zero; the loop flags go on between prepare and write.
  waveOutPrepareHeader(state.device, &state.loop_header, sizeof(WAVEHDR));
  state.loop_header.dwFlags |= WHDR_BEGINLOOP | WHDR_ENDLOOP;
  state.loop_header.dwLoops = 0xFFFFFFFF;  // stop_wav_loop ends it, never the count
  waveOutWrite(state.device, &state.loop_header, sizeof(WAVEHDR));
}

void stop_wav_loop() {
  auto& state = wave_loop();
  if (state.device == nullptr) {
    return;
  }
  // Reset returns every queued buffer before they are unprepared and freed.
  waveOutReset(state.device);
  if (!state.intro.isEmpty()) {
    waveOutUnprepareHeader(state.device, &state.intro_header, sizeof(WAVEHDR));
  }
  waveOutUnprepareHeader(state.device, &state.loop_header, sizeof(WAVEHDR));
  waveOutClose(state.device);
  state = WaveLoop{};
}

bool wav_loop_active() { return wave_loop().device != nullptr; }

void play_wav_bytes(QByteArray wav) {
  if (!is_well_formed_wav(wav) || sound_disabled()) {
    return;
  }
  // PlaySound reads the buffer asynchronously, so it must outlive the call;
  // the static keeps it alive until the next sound, which cancels the current
  // one (acceptable for game blips, and how PlaySound behaves anyway). The
  // current sound is stopped BEFORE its buffer is replaced: winmm may still be
  // reading it, and freeing it first is a read after free.
  static QByteArray playing;
  PlaySoundW(nullptr, nullptr, 0);
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

#elif defined(Q_OS_WASM)

// No QProcess (or any process) exists in the browser; script sounds are a
// silent no-op until a Web Audio backend is worth writing. build_tone_wav
// above stays shared so the synth logic keeps compiling on every platform,
// and the sound_disabled() gate keeps the same shape as the real backends.
void play_wav_bytes(QByteArray wav) {
  if (!is_well_formed_wav(wav) || sound_disabled()) {
    return;
  }
}

void play_wav_file(const QString& /*path*/) {}

void play_wav_loop(QByteArray /*intro*/, QByteArray /*loop*/) {}

void stop_wav_loop() {}

bool wav_loop_active() { return false; }

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
  if (!is_well_formed_wav(wav) || sound_disabled()) {
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

namespace {

// The loop channel: an owned player process per file. The loop is written repeated
// to about kLoopFileSeconds (the intro ahead of the first copy), so the gap of a
// process restart falls only every few seconds.
constexpr int kLoopFileSeconds = 6;
constexpr qsizetype kMaxLoopFileBytes = 8 * 1024 * 1024;
// A player that exits sooner than this failed (no device, say): the loop stops
// instead of respawning it in a tight cycle.
constexpr qint64 kMinLoopProcessMs = 100;

struct ProcessLoop {
  QPointer<QProcess> process;
  QString first_path;
  QString loop_path;
  bool active = false;
  int generation = 0;
};

ProcessLoop& process_loop() {
  static ProcessLoop state;
  return state;
}

QString write_loop_file(const QString& tag, const QByteArray& wav) {
  const auto path = QDir::temp().absoluteFilePath(QStringLiteral("patchy-sfx-loop-%1-%2-%3.wav")
                                                      .arg(QCoreApplication::applicationPid())
                                                      .arg(process_loop().generation)
                                                      .arg(tag));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(wav) != wav.size()) {
    return {};
  }
  return path;
}

void start_loop_process(const QString& path, int generation) {
  auto& state = process_loop();
  const auto command = player_command(path);
  if (command.isEmpty() || QCoreApplication::instance() == nullptr) {
    stop_wav_loop();
    return;
  }
  auto* process = new QProcess(QCoreApplication::instance());
  state.process = process;
  auto started = std::make_shared<QElapsedTimer>();
  started->start();
  QObject::connect(process, &QProcess::finished, process, [process, generation, started] {
    process->deleteLater();
    auto& current = process_loop();
    if (!current.active || current.generation != generation || current.process != process) {
      return;
    }
    if (started->elapsed() < kMinLoopProcessMs) {
      stop_wav_loop();
      return;
    }
    start_loop_process(current.loop_path, generation);
  });
  QObject::connect(process, &QProcess::errorOccurred, process, [process, generation](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
      process->deleteLater();
      if (process_loop().generation == generation) {
        stop_wav_loop();
      }
    }
  });
  process->start(command.first(), command.mid(1));
}

}  // namespace

void play_wav_loop(QByteArray intro, QByteArray loop) {
  stop_wav_loop();
  const auto parts = loop_parts(intro, loop);
  if (parts.loop.data.isEmpty()) {
    return;
  }
  const qsizetype seconds_bytes = static_cast<qsizetype>(parts.loop.sample_rate) * parts.loop.block_align *
                                  kLoopFileSeconds;
  QByteArray repeated = parts.loop.data;
  while (repeated.size() < seconds_bytes && repeated.size() + parts.loop.data.size() <= kMaxLoopFileBytes) {
    repeated.append(parts.loop.data);
  }
  auto& state = process_loop();
  state.loop_path = write_loop_file(QStringLiteral("loop"), make_wav(parts.loop.format_chunk, repeated));
  state.first_path = parts.intro_data.isEmpty()
                         ? state.loop_path
                         : write_loop_file(QStringLiteral("first"),
                                           make_wav(parts.loop.format_chunk, parts.intro_data + repeated));
  if (state.loop_path.isEmpty() || state.first_path.isEmpty()) {
    stop_wav_loop();
    return;
  }
  state.active = true;
  start_loop_process(state.first_path, state.generation);
}

void stop_wav_loop() {
  auto& state = process_loop();
  const bool had_files = !state.loop_path.isEmpty() || !state.first_path.isEmpty();
  state.active = false;
  ++state.generation;  // a finishing player sees a stale generation and stays stopped
  if (state.process != nullptr) {
    state.process->kill();
  }
  state.process = nullptr;
  if (had_files) {
    // The killed player may still hold its file for a moment.
    QTimer::singleShot(2000, [first = state.first_path, loop = state.loop_path] {
      QFile::remove(first);
      QFile::remove(loop);
    });
  }
  state.first_path.clear();
  state.loop_path.clear();
}

bool wav_loop_active() { return process_loop().active; }

#endif  // platform playback (macOS lives in sound_effects_mac.mm)

}  // namespace patchy::ui
