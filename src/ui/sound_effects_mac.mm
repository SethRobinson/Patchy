// macOS playback half of sound_effects.hpp: NSSound, which plays WAV data
// directly with no extra frameworks (AppKit is already linked). NSSound stops
// if its object deallocates mid-play, so live sounds are retained in a static
// set and released by the finished-playing delegate (with a size cap in case
// a delegate callback never arrives). Compiled with ARC (see CMakeLists).

#include "ui/sound_effects.hpp"

#include <QFile>
#include <QtGlobal>

#import <AppKit/AppKit.h>

@interface PatchySoundReaper : NSObject <NSSoundDelegate>
@end

// The loop channel's intro delegate: starts the looping sound when the intro ends.
@interface PatchyLoopChain : NSObject <NSSoundDelegate>
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

// play_wav_loop's two sounds; both nil while no loop plays.
NSSound* g_loop_intro = nil;
NSSound* g_loop_sound = nil;

PatchyLoopChain* loop_chain() {
  static PatchyLoopChain* chain = [PatchyLoopChain new];
  return chain;
}

}  // namespace

@implementation PatchySoundReaper
- (void)sound:(NSSound*)sound didFinishPlaying:(BOOL)finished {
  Q_UNUSED(finished);
  [live_sounds() removeObjectIdenticalTo:sound];
}
@end

@implementation PatchyLoopChain
- (void)sound:(NSSound*)sound didFinishPlaying:(BOOL)finished {
  if (sound != g_loop_intro) {
    return;
  }
  g_loop_intro = nil;
  if (finished && g_loop_sound != nil) {
    [g_loop_sound play];
  }
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
  NSSound* looping = [[NSSound alloc] initWithData:[NSData dataWithBytes:loop.constData()
                                                                 length:static_cast<NSUInteger>(loop.size())]];
  if (looping == nil) {
    return;
  }
  looping.loops = YES;
  g_loop_sound = looping;
  const auto intro_pcm = parse_wav(intro);
  if (!intro_pcm.data.isEmpty() && intro_pcm.format_chunk == parse_wav(loop).format_chunk) {
    NSSound* first = [[NSSound alloc] initWithData:[NSData dataWithBytes:intro.constData()
                                                                 length:static_cast<NSUInteger>(intro.size())]];
    if (first != nil) {
      first.delegate = loop_chain();
      g_loop_intro = first;
      [first play];
      return;
    }
  }
  [looping play];
}

void stop_wav_loop() {
  if (g_loop_intro != nil) {
    g_loop_intro.delegate = nil;  // so stopping it does not start the loop
    [g_loop_intro stop];
    g_loop_intro = nil;
  }
  if (g_loop_sound != nil) {
    [g_loop_sound stop];
    g_loop_sound = nil;
  }
}

bool wav_loop_active() { return g_loop_sound != nil; }

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
