// Dink Smallwood theme script. It runs in Patchy's locked-down theme engine: it can play
// this package's sounds and effects, add the Dink menu and set cursors, a splash and
// panel backgrounds, and nothing else (no files, no documents).

var theme = patchy.theme;

// Dink strolls along the bottom of the empty start screen, never over a document.
function walk(y, startScreenOnly) {
  var spec = {
    name: 'sprite', image: 'images/dink-walk.png', frames: 8, frameWidth: 78, frameHeight: 76,
    fps: 9, x: -0.05, y: y, toX: 1.05, toY: y, duration: 9000
  };
  if (startScreenOnly) spec.where = 'start-screen';
  theme.playEffect(spec);
}

// The startup sound (Dink's title-screen sound) is sounds/startup.wav; the splash shows with it.
theme.on('startup', function () {
  theme.showSplash('images/splash.png', 1600);
  setTimeout(function () { walk(0.84, true); }, 2000);
});

// Now and then while nothing is open.
var idleCount = 0;
theme.on('idle', function () {
  idleCount += 1;
  if (idleCount % 2 === 1) walk(0.84, true);
});

theme.on('document-new', function () {
  theme.playEffect('sparkles', { y: 0.45 });
});

// Painting sounds: each stroke plays a start (the brush touching down), a loop for as
// long as it lasts, and an end as it lifts (sounds/strokes, synthesized for this theme).
// Clone and Healing open with Dink's spell before the brush loop takes over.
var strokeSets = {
  brush: 'brush', mixer_brush: 'brush', pattern_stamp: 'brush', sharpen: 'brush',
  clone: 'brush', healing: 'brush',
  eraser: 'eraser',
  smudge: 'soft', blur: 'soft', sponge: 'soft', dodge: 'soft', burn: 'soft'
};
var strokeIntros = { clone: 'sounds/fx/spell.wav', healing: 'sounds/fx/spell.wav' };
var strokeSet = null;

function onStrokeStart(data) {
  strokeSet = strokeSets[data.tool] || 'brush';
  var base = 'sounds/strokes/' + strokeSet;
  theme.startLoop(base + '-loop.wav', strokeIntros[data.tool] || base + '-start.wav');
}

function onStrokeEnd() {
  if (strokeSet !== null) {
    theme.stopLoop('sounds/strokes/' + strokeSet + '-end.wav');
    strokeSet = null;
  }
}

// An older Patchy has no stroke events or loops (on() throws on the unknown name); the
// theme then just paints quietly.
if (typeof theme.startLoop === 'function') {
  try {
    theme.on('stroke-start', onStrokeStart);
    theme.on('stroke-end', onStrokeEnd);
  } catch (e) {
    theme.log('no stroke events in this Patchy');
  }
}

// The rest is declarative (theme.json and the sounds folder), so it works even when the
// scripts are not allowed: the bomb when a layer is deleted, a door warp when a file
// opens, the stairs when one closes, the sword cursor for Crop, and the stone wall
// behind dialogs.

theme.addCommand({ id: 'celebrate', title: 'Celebrate', hotkey: 'Ctrl+Alt+Shift+C' }, function () {
  theme.playEffect('fireworks', { bursts: 6 });
  theme.playSound('sounds/long-operation-done.wav');
  theme.showStatus('Level up!');
});

theme.addCommand({ id: 'confetti', title: 'Confetti' }, function () {
  theme.playEffect('confetti');
  theme.playSound('sounds/export-done.wav');
});

theme.addCommand({ id: 'secret', title: 'Find a Secret' }, function () {
  theme.playEffect('sparkles', { count: 90 });
  theme.playSound('sounds/fx/secret.wav');
  theme.showStatus('You found a secret!');
});

theme.addCommand({ id: 'walk', title: 'Take a Walk' }, function () {
  walk(0.95, false);
});

theme.addCommand({ id: 'hello', title: 'Say Hello' }, function () {
  theme.showStatus("Dink: Hi, I'm Dink Smallwood. Pig farmer, mostly.", 6000);
  theme.playSound('sounds/script-alert.wav');
});
