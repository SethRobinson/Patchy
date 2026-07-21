// @name Breakout
// @description Breakout played on a real Patchy document: the bricks, paddle,
// @description and ball are live layers on a real canvas. A small controller
// @description window takes the keys (Left/Right or A/D, Space to launch).
// @author Seth A. Robinson
// @window
//
// Drawn incrementally: the bricks render once into their own layer and a
// destroyed brick is just cleared with a transparent fillRect, while the ball
// and paddle live on their own tiny layers that move via layer.x/y each frame.
// No full-canvas upload ever happens, so the compositor only recomposites the
// few pixels that changed. Undo is disabled for speed with app.undoEnabled.
// The playfield backdrop comes from the bundled Fancy Background script - one
// script calling another via include().

// ---------------------------------------------------------------------------
// Options - tweak the game here (it starts immediately, no dialog).
var OPTIONS = {
  width: 480,      // playfield document size
  height: 360,
  lives: 3,
  paddleSpeed: 7,
  sound: true      // retro blips (patchy.ui.playTone)
};
// ---------------------------------------------------------------------------

app.undoEnabled = false;

function sfx(freq, ms, vol) {
  if (OPTIONS.sound) { patchy.ui.playTone(freq, ms, vol, "square"); }
}

var W = OPTIONS.width;
var H = OPTIONS.height;
var doc = app.newDocument(W, H);
// The Background layer becomes the playfield; keep it dark and subtle so the
// bricks and ball stay readable.
include("Effects/fancy-background.js");
drawFancyBackground(doc.layers[0], W, H,
                    { top: "#141b2e", bottom: "#0a0e17", vignette: 0.4, noise: 0.035, seed: 7 });

// Bricks: 10 x 5 grid, one color per row, drawn ONCE into the bricks layer.
var COLS = 10;
var ROWS = 5;
var BRICK_W = 44;
var BRICK_H = 14;
var BRICK_GAP = 2;
var BRICK_TOP = 32;
var BRICK_LEFT = Math.floor((W - COLS * (BRICK_W + BRICK_GAP) + BRICK_GAP) / 2);
var rowColors = ["#e65050", "#e6a03c", "#e6dc46", "#5ac85a", "#508ce6"];

var bricksLayer = doc.addLayer("Bricks");
var fieldH = BRICK_TOP + ROWS * (BRICK_H + BRICK_GAP);
// Allocate the layer's buffer (transparent) covering the brick field, then
// stamp the bricks into it.
bricksLayer.setPixels({ x: 0, y: 0, width: W, height: fieldH,
                        data: new Uint8Array(W * fieldH * 4).buffer });
var bricks = [];
for (var by = 0; by < ROWS; by++) {
  for (var bx = 0; bx < COLS; bx++) {
    var brick = { x: BRICK_LEFT + bx * (BRICK_W + BRICK_GAP),
                  y: BRICK_TOP + by * (BRICK_H + BRICK_GAP), row: by, alive: true };
    bricksLayer.fillRect(brick.x, brick.y, BRICK_W, BRICK_H, rowColors[by]);
    bricks.push(brick);
  }
}
var bricksLeft = bricks.length;

// Paddle and ball: one tiny layer each, animated by moving the layer.
var PADDLE_W = 64;
var PADDLE_H = 8;
var PADDLE_Y = H - 18;
var paddleLayer = doc.addLayer("Paddle");
paddleLayer.fillRect(0, 0, PADDLE_W, PADDLE_H, "#dcdce6");
var BALL = 6;
var ballLayer = doc.addLayer("Ball");
ballLayer.fillRect(0, 0, BALL, BALL, "#ffffff");

// Leave the Bricks layer active in the Layers panel (addLayer activated the
// ball): painting on the brick field mid-game is half the fun.
doc.activeLayer = bricksLayer;

// Zoom in on the playfield: Fit on Screen scales the small document up to the
// window and centers it (at 100% it would sit tiny in a corner of the view).
app.runCommand("view.fit_on_screen");

var paddleX = (W - PADDLE_W) / 2;
var ball = { x: 0, y: 0, vx: 0, vy: 0, stuck: true };
var score = 0;
var lives = OPTIONS.lives;
var state = "play";  // play | won | lost

var pad = patchy.ui.createCanvas({ width: 260, height: 96, title: "Breakout Controls" });

pad.onFrame = function (dt) {
  var step = Math.min(dt, 50) / 16.0;

  if (state === "play") {
    var speed = OPTIONS.paddleSpeed * step;
    if (pad.isKeyDown("Left") || pad.isKeyDown("A")) { paddleX -= speed; }
    if (pad.isKeyDown("Right") || pad.isKeyDown("D")) { paddleX += speed; }
    paddleX = Math.max(0, Math.min(W - PADDLE_W, paddleX));

    if (ball.stuck) {
      ball.x = paddleX + PADDLE_W / 2 - BALL / 2;
      ball.y = PADDLE_Y - BALL - 1;
      if (pad.isKeyDown("Space")) {
        ball.stuck = false;
        var lean = (Math.random() - 0.5) * 2;
        ball.vx = 3 * (lean < 0 ? -1 : 1) * (0.6 + Math.abs(lean));
        ball.vy = -4.2;
        sfx(440, 80, 0.35);
      }
    } else {
      ball.x += ball.vx * step;
      ball.y += ball.vy * step;
      if (ball.x < 0) { ball.x = 0; ball.vx = Math.abs(ball.vx); sfx(330, 50, 0.3); }
      if (ball.x > W - BALL) { ball.x = W - BALL; ball.vx = -Math.abs(ball.vx); sfx(330, 50, 0.3); }
      if (ball.y < 0) { ball.y = 0; ball.vy = Math.abs(ball.vy); sfx(330, 50, 0.3); }

      // Paddle bounce steers by hit position.
      if (ball.vy > 0 && ball.y + BALL >= PADDLE_Y && ball.y + BALL <= PADDLE_Y + PADDLE_H + 4 &&
          ball.x + BALL > paddleX && ball.x < paddleX + PADDLE_W) {
        ball.vy = -Math.abs(ball.vy);
        var hit = (ball.x + BALL / 2 - (paddleX + PADDLE_W / 2)) / (PADDLE_W / 2);
        ball.vx = hit * 5;
        sfx(880, 60, 0.4);
      }

      // Brick collisions: clearing the hit brick is one transparent fillRect.
      for (var i = 0; i < bricks.length; i++) {
        var b = bricks[i];
        if (!b.alive) { continue; }
        if (ball.x + BALL > b.x && ball.x < b.x + BRICK_W &&
            ball.y + BALL > b.y && ball.y < b.y + BRICK_H) {
          b.alive = false;
          bricksLeft--;
          score += 10;
          bricksLayer.fillRect(b.x, b.y, BRICK_W, BRICK_H, "#00000000");
          // Higher rows ring higher, like the arcade original.
          sfx(460 + (ROWS - 1 - b.row) * 70, 60, 0.4);
          var overlapX = Math.min(ball.x + BALL - b.x, b.x + BRICK_W - ball.x);
          var overlapY = Math.min(ball.y + BALL - b.y, b.y + BRICK_H - ball.y);
          if (overlapX < overlapY) { ball.vx = -ball.vx; } else { ball.vy = -ball.vy; }
          break;
        }
      }
      if (bricksLeft === 0) {
        state = "won";
        sfx(523, 250, 0.45);
        setTimeout(function () { sfx(784, 400, 0.45); }, 260);
      }

      if (ball.y > H) {
        lives--;
        sfx(150, 300, 0.5);
        if (lives <= 0) {
          state = "lost";
          ballLayer.visible = false;
          setTimeout(function () { sfx(110, 500, 0.5); }, 320);
        } else {
          ball.stuck = true;
        }
      }
    }

    // Present: two layer moves, nothing else.
    paddleLayer.moveTo(Math.round(paddleX), PADDLE_Y);
    if (state !== "lost") {
      ballLayer.moveTo(Math.round(ball.x), Math.round(ball.y));
    }
  }

  // Controller window: score readout + instructions.
  var g = pad.graphics;
  g.clear("#181c24");
  g.text(12, 26, "Score " + score + "   Lives " + lives, "#e8e8e8", 14);
  if (state === "play") {
    g.text(12, 52, ball.stuck ? "Space to launch" : "Keep it up!", "#8fc8ff", 11);
    g.text(12, 74, "Left/Right or A/D move. Close to quit.", "#8a90a0", 9);
  } else if (state === "won") {
    g.text(12, 56, "Cleared! Close this window to finish.", "#80e080", 11);
  } else {
    g.text(12, 56, "Game over. Close this window to finish.", "#e08080", 11);
  }
};

console.log("Breakout is playing on the document canvas. Focus the controller window to steer.");
