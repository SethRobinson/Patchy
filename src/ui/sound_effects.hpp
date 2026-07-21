#pragma once

#include <QByteArray>
#include <QString>

namespace patchy::ui {

enum class ToneWave { Sine, Square };

// 44100 Hz 16-bit mono PCM RIFF/WAVE bytes for one tone: a ~5 ms attack ramp,
// an exponential decay, and a short release fade so blips never click. Volume
// is baked into the sample amplitude so every platform backend plays the same
// loudness. Inputs are clamped (frequency 20..20000 Hz, duration 1..4000 ms,
// volume 0..1) and the output is deterministic.
[[nodiscard]] QByteArray build_tone_wav(double frequency_hz, int duration_ms, double volume,
                                        ToneWave wave);

// Fire-and-forget playback through the platform's built-in facility - winmm
// PlaySound (Windows), NSSound (macOS, sound_effects_mac.mm), or a detached
// paplay/pw-play/aplay process (Linux). Deliberately NOT Qt Multimedia: that
// module is absent from the vendored Qt and would grow provisioning and
// packaging on all three platforms. Best-effort: a missing device or player
// is a silent no-op, and PATCHY_NO_SOUND=1 skips the OS call entirely (tests,
// headless runs).
void play_wav_bytes(QByteArray wav);
void play_wav_file(const QString& path);

}  // namespace patchy::ui
