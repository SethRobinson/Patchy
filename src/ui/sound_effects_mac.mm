// macOS playback half of sound_effects.hpp. One-shot sounds use NSSound, which plays
// WAV data directly (AppKit is already linked). NSSound stops if its object deallocates
// mid-play, so live sounds are retained in a static set and released by the
// finished-playing delegate (with a size cap in case a delegate callback never
// arrives). The loop channel uses an AVAudioEngine player node: the intro and a
// looping buffer are scheduled back to back, so the hand-over and the repeats are
// sample-exact. Compiled with ARC (see CMakeLists).

#include "ui/sound_effects.hpp"

#include <QFile>
#include <QtGlobal>

#include <cstring>

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>

@interface PatchySoundReaper : NSObject <NSSoundDelegate>
@end

namespace {

NSMutableArray<NSSound*>* live_sounds() {
  static NSMutableArray<NSSound*>* sounds = [NSMutableArray new];
  return sounds;
}

PatchySoundReaper* sound_reaper() {
  static PatchySoundReaper* reaper = [PatchySoundReaper new];
  return reaper;
}

bool sound_disabled() { return qEnvironmentVariableIsSet("PATCHY_NO_SOUND"); }

// The loop channel. The engine is made on first use and paused between loops; the
// player node is reconnected only when a loop's format differs from the last one.
AVAudioEngine* g_engine = nil;
AVAudioPlayerNode* g_player = nil;
AVAudioFormat* g_connected_format = nil;
bool g_loop_playing = false;

void drop_engine() {
  g_engine = nil;
  g_player = nil;
  g_connected_format = nil;
  g_loop_playing = false;
}

float sample_at(const unsigned char* bytes, const patchy::ui::WavPcm& pcm) {
  switch (pcm.bits) {
    case 8:
      return (static_cast<float>(bytes[0]) - 128.0f) / 128.0f;
    case 16: {
      const auto value = static_cast<std::int16_t>(bytes[0] | bytes[1] << 8);
      return static_cast<float>(value) / 32768.0f;
    }
    case 24: {
      auto value = static_cast<std::int32_t>(bytes[0] | bytes[1] << 8 | bytes[2] << 16);
      if ((value & 0x800000) != 0) {
        value -= 0x1000000;
      }
      return static_cast<float>(value) / 8388608.0f;
    }
    case 32: {
      std::uint32_t raw = static_cast<std::uint32_t>(bytes[0]) | static_cast<std::uint32_t>(bytes[1]) << 8 |
                          static_cast<std::uint32_t>(bytes[2]) << 16 | static_cast<std::uint32_t>(bytes[3]) << 24;
      if (pcm.is_float) {
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
      }
      return static_cast<float>(static_cast<std::int32_t>(raw)) / 2147483648.0f;
    }
    default:
      return 0.0f;
  }
}

// The samples as the engine's standard format (float, one plane per channel).
AVAudioPCMBuffer* make_buffer(const patchy::ui::WavPcm& pcm, AVAudioFormat* format) {
  const auto frames = static_cast<AVAudioFrameCount>(pcm.data.size() / pcm.block_align);
  AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:frames];
  if (buffer == nil || buffer.floatChannelData == nullptr) {
    return nil;
  }
  buffer.frameLength = frames;
  const auto* data = reinterpret_cast<const unsigned char*>(pcm.data.constData());
  const int sample_bytes = pcm.bits / 8;
  for (AVAudioFrameCount frame = 0; frame < frames; ++frame) {
    for (int channel = 0; channel < pcm.channels; ++channel) {
      buffer.floatChannelData[channel][frame] =
          sample_at(data + frame * pcm.block_align + channel * sample_bytes, pcm);
    }
  }
  return buffer;
}

}  // namespace

@implementation PatchySoundReaper
- (void)sound:(NSSound*)sound didFinishPlaying:(BOOL)finished {
  Q_UNUSED(finished);
  [live_sounds() removeObjectIdenticalTo:sound];
}
@end

namespace patchy::ui {

void play_wav_bytes(QByteArray wav) {
  if (!is_well_formed_wav(wav) || sound_disabled()) {
    return;
  }
  NSData* data = [NSData dataWithBytes:wav.constData()
                                length:static_cast<NSUInteger>(wav.size())];
  NSSound* sound = [[NSSound alloc] initWithData:data];
  if (sound == nil) {
    return;
  }
  sound.delegate = sound_reaper();
  // Cap the retained set: if delegates ever go missing, dropping the oldest
  // (which stops it) beats growing without bound.
  while (live_sounds().count >= 16) {
    [live_sounds() removeObjectAtIndex:0];
  }
  [live_sounds() addObject:sound];
  [sound play];
}

void play_wav_loop(QByteArray intro, QByteArray loop) {
  stop_wav_loop();
  if (sound_disabled() || wav_duration_ms(loop) < kMinWavLoopMs) {
    return;
  }
  const auto loop_pcm = parse_wav(loop);
  // AVAudioEngine reports misuse (no output device, a format it cannot connect) by
  // raising, and a sound must never take the app down.
  @try {
    AVAudioFormat* format =
        [[AVAudioFormat alloc] initStandardFormatWithSampleRate:static_cast<double>(loop_pcm.sample_rate)
                                                       channels:static_cast<AVAudioChannelCount>(loop_pcm.channels)];
    AVAudioPCMBuffer* loop_buffer = format != nil ? make_buffer(loop_pcm, format) : nil;
    if (loop_buffer == nil) {
      return;
    }
    AVAudioPCMBuffer* intro_buffer = nil;
    if (const auto intro_pcm = parse_wav(intro);
        !intro_pcm.data.isEmpty() && intro_pcm.format_chunk == loop_pcm.format_chunk) {
      intro_buffer = make_buffer(intro_pcm, format);
    }
    if (g_engine == nil) {
      g_engine = [AVAudioEngine new];
      g_player = [AVAudioPlayerNode new];
      [g_engine attachNode:g_player];
    }
    if (g_connected_format == nil || ![g_connected_format isEqual:format]) {
      if (g_engine.isRunning) {
        [g_engine stop];
      }
      [g_engine connect:g_player to:g_engine.mainMixerNode format:format];
      g_connected_format = format;
    }
    if (!g_engine.isRunning) {
      NSError* error = nil;
      if (![g_engine startAndReturnError:&error]) {
        drop_engine();  // a fresh engine next time (the output device may have changed)
        return;
      }
    }
    if (intro_buffer != nil) {
      [g_player scheduleBuffer:intro_buffer completionHandler:nil];
    }
    [g_player scheduleBuffer:loop_buffer atTime:nil options:AVAudioPlayerNodeBufferLoops completionHandler:nil];
    [g_player play];
    g_loop_playing = true;
  } @catch (NSException*) {
    drop_engine();
  }
}

void stop_wav_loop() {
  if (!g_loop_playing) {
    return;
  }
  g_loop_playing = false;
  @try {
    [g_player stop];   // drops the scheduled buffers at once
    [g_engine pause];  // keeps the graph; the next loop starts it again
  } @catch (NSException*) {
    drop_engine();
  }
}

bool wav_loop_active() { return g_loop_playing; }

void wake_audio_output() {
  // A silent one-shot: NSSounds mix, so the sound it wakes the output for plays over it.
  play_wav_bytes(build_tone_wav(440.0, kAudioWakeStreamMs, 0.0, ToneWave::Sine));
}

void play_wav_file(const QString& path) {
  if (sound_disabled()) {
    return;
  }
  // Through the memory path, so playback never holds the file open; callers
  // validated size and header already.
  QFile file(path);
  if (file.open(QIODevice::ReadOnly)) {
    play_wav_bytes(file.readAll());
  }
}

}  // namespace patchy::ui
