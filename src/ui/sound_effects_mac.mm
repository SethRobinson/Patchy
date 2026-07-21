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

}  // namespace

@implementation PatchySoundReaper
- (void)sound:(NSSound*)sound didFinishPlaying:(BOOL)finished {
  Q_UNUSED(finished);
  [live_sounds() removeObjectIdenticalTo:sound];
}
@end

namespace patchy::ui {

void play_wav_bytes(QByteArray wav) {
  if (wav.isEmpty() || sound_disabled()) {
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
