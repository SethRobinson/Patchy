#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace patchy::ui {

enum class ToneWave { Sine, Square };

// 44100 Hz 16-bit mono PCM RIFF/WAVE bytes for one tone: a ~5 ms attack ramp,
// an exponential decay, and a short release fade so blips never click. Volume
// is baked into the sample amplitude so every platform backend plays the same
// loudness. Inputs are clamped (frequency 20..20000 Hz, duration 1..4000 ms,
// volume 0..1) and the output is deterministic.
[[nodiscard]] QByteArray build_tone_wav(double frequency_hz, int duration_ms, double volume,
                                        ToneWave wave);

// True for RIFF/WAVE bytes every backend can play safely: every chunk, and the RIFF
// size the players trust (winmm reads that many bytes from the buffer), lies inside
// the bytes; a PCM or float "fmt " chunk with sane fields comes before a non-empty
// "data" chunk. Theme packages are checked with it at load, and every play_wav_bytes
// refuses anything else, since a theme's WAV comes from anywhere.
[[nodiscard]] bool is_well_formed_wav(const QByteArray& wav);

// Fire-and-forget playback through the platform's built-in facility - winmm
// PlaySound (Windows), NSSound (macOS, sound_effects_mac.mm), or a detached
// paplay/pw-play/aplay process (Linux). Deliberately NOT Qt Multimedia: that
// module is absent from the vendored Qt and would grow provisioning and
// packaging on all three platforms. Best-effort: a missing device or player
// is a silent no-op, and PATCHY_NO_SOUND=1 skips the OS call entirely (tests,
// headless runs).
void play_wav_bytes(QByteArray wav);
void play_wav_file(const QString& path);

// The format and samples of a well-formed WAV; `data` is empty for anything else.
struct WavPcm {
  std::uint16_t format_tag = 0;  // 1 PCM, 3 IEEE float, 0xFFFE extensible
  std::uint16_t channels = 0;
  std::uint32_t sample_rate = 0;
  std::uint16_t block_align = 0;
  std::uint16_t bits = 0;  // per sample: 8, 16, 24 or 32
  bool is_float = false;   // IEEE float samples (tag 3, or an extensible float subformat)
  QByteArray format_chunk;  // the "fmt " chunk body as written
  QByteArray data;
};
[[nodiscard]] WavPcm parse_wav(const QByteArray& wav);
// RIFF/WAVE bytes holding `format_chunk` and `data` (the inverse of parse_wav).
[[nodiscard]] QByteArray make_wav(const QByteArray& format_chunk, const QByteArray& data);
// Length of a well-formed WAV's samples in milliseconds, 0 for anything else.
[[nodiscard]] int wav_duration_ms(const QByteArray& wav);

// One looping sound at a time (docs/theme-files.md, "Sounds"), beside play_wav_bytes,
// which keeps playing over it: `intro` (may be empty) plays once, then `loop` repeats
// until stop_wav_loop or the next play_wav_loop. An intro in another format than the
// loop is dropped, and a loop shorter than kMinWavLoopMs is refused (a tiny buffer
// would spin the audio thread). Windows queues both on one waveOut device and macOS
// schedules both on an AVAudioEngine player node, so the intro runs into the loop and
// the repeats without a gap. Linux plays only the intro, once (looping would need an
// external player process kept fed per stroke). PATCHY_NO_SOUND and wasm make it a
// no-op.
inline constexpr int kMinWavLoopMs = 50;
void play_wav_loop(QByteArray intro, QByteArray loop);
// Stops the loop and its intro at once; a no-op when none plays.
void stop_wav_loop();
// True between a play_wav_loop the backend accepted and stop_wav_loop (never on Linux).
[[nodiscard]] bool wav_loop_active();

// Wakes the audio output ahead of a sound that must not lose its start. Outputs that
// power down when idle (HDMI to a receiver, USB interfaces, Bluetooth) drop the first
// part of a sound while they wake, so a stream of silence plays for
// kAudioWakeStreamMs: on Windows its own waveOut device (PlaySound sounds mix over it),
// on macOS a silent NSSound. Linux and wasm do nothing. A no-op under PATCHY_NO_SOUND.
inline constexpr int kAudioWakeStreamMs = 2500;
void wake_audio_output();

}  // namespace patchy::ui
