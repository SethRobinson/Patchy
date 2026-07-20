// Breakout
// The whole game renders into a REAL Patchy document: every frame is written
// into a layer with setPixels, so you watch it play out on the actual canvas
// (zoom in, change the layer's blend mode mid-game, whatever). A small
// controller window takes the keyboard (Left/Right or A/D, Space to launch)
// and shows the score; close it to quit. Undo is disabled for speed with
// app.undoEnabled, so the document keeps the final frame with no history cost.

app.undoEnabled = false;

var W = 480;
var H = 360;
var doc = app.newDocument(W, H);
var layer = doc.addLayer("Breakout");

// One RGBA framebuffer, reused every frame.
var frame = new Uint8Array(W * H * 4);

function fillRect(x, y, w, h, r, g, b) {
  var x0 = Math.max(0, x | 0);
  var y0 = Math.max(0, y | 0);
  var x1 = Math.min(W, (x + w) | 0);
  var y1 = Math.min(H, (y + h) | 0);
  for (var py = y0; py < y1; py++) {
    var row = py * W;
    for (var px = x0; px < x1; px++) {
      var o = (row + px) * 4;
      frame[o] = r;
      frame[o + 1] = g;
      frame[o + 2] = b;
      frame[o + 3] = 255;
    }
  }
}

// Bricks: 10 x 5 grid, one color per row.
var COLS = 10;
var ROWS = 5;
var BRICK_W = 44;
var BRICK_H = 14;
var BRICK_GAP = 2;
var BRICK_TOP = 32;
var BRICK_LEFT = (W - COLS * (BRICK_W + BRICK_GAP) + BRICK_GAP) / 2;
var rowColors = [
  [230, 80, 80], [230, 160, 60], [230, 220, 70], [90, 200, 90], [80, 140, 230]
];
var bricks = [];
for (var by = 0; by < ROWS; by++) {
  for (var bx = 0; bx < COLS; bx++) {
    bricks.push({ x: BRICK_LEFT + bx * (BRICK_W + BRICK_GAP),
                  y: BRICK_TOP + by * (BRICK_H + BRICK_GAP),
                  color: rowColors[by], alive: true });
  }
}
var bricksLeft = bricks.length;

var PADDLE_W = 64;
var PADDLE_H = 8;
var paddleX = (W - PADDLE_W) / 2;
var BALL = 6;
var ball = { x: 0, y: 0, vx: 0, vy: 0, stuck: true };
var score = 0;
var lives = 3;
var state = "play";  // play | won | lost

var pad = patchy.ui.createCanvas({ width: 260, height: 96, title: "Breakout Controls" });

function render() {
  fillRect(0, 0, W, H, 14, 16, 26);
  for (var i = 0; i < bricks.length; i++) {
    var brick = bricks[i];
    if (brick.alive) {
      fillRect(brick.x, brick.y, BRICK_W, BRICK_H, brick.color[0], brick.color[1], brick.color[2]);
    }
  }
  fillRect(paddleX, H - 18, PADDLE_W, PADDLE_H, 220, 220, 230);
  if (state !== "lost") {
    fillRect(ball.x, ball.y, BALL, BALL, 255, 255, 255);
  }
  layer.setPixels({ x: 0, y: 0, width: W, height: H, data: frame.buffer });
}

pad.onFrame = function (dt) {
  var step = Math.min(dt, 50) / 16.0;

  if (state === "play") {
    var speed = 7 * step;
    if (pad.isKeyDown("Left") || pad.isKeyDown("A")) { paddleX -= speed; }
    if (pad.isKeyDown("Right") || pad.isKeyDown("D")) { paddleX += speed; }
    paddleX = Math.max(0, Math.min(W - PADDLE_W, paddleX));

    if (ball.stuck) {
      ball.x = paddleX + PADDLE_W / 2 - BALL / 2;
      ball.y = H - 18 - BALL - 1;
      if (pad.isKeyDown("Space")) {
        ball.stuck = false;
        var lean = (Math.random() - 0.5) * 2;
        ball.vx = 3 * (lean < 0 ? -1 : 1) * (0.6 + Math.abs(lean));
        ball.vy = -4.2;
      }
    } else {
      ball.x += ball.vx * step;
      ball.y += ball.vy * step;
      if (ball.x < 0) { ball.x = 0; ball.vx = Math.abs(ball.vx); }
      if (ball.x > W - BALL) { ball.x = W - BALL; ball.vx = -Math.abs(ball.vx); }
      if (ball.y < 0) { ball.y = 0; ball.vy = Math.abs(ball.vy); }

      // Paddle bounce steers by hit position.
      if (ball.vy > 0 && ball.y + BALL >= H - 18 && ball.y + BALL <= H - 18 + PADDLE_H + 4 &&
          ball.x + BALL > paddleX && ball.x < paddleX + PADDLE_W) {
        ball.vy = -Math.abs(ball.vy);
        var hit = (ball.x + BALL / 2 - (paddleX + PADDLE_W / 2)) / (PADDLE_W / 2);
        ball.vx = hit * 5;
      }

      // Brick collisions.
      for (var i = 0; i < bricks.length; i++) {
        var brick = bricks[i];
        if (!brick.alive) { continue; }
        if (ball.x + BALL > brick.x && ball.x < brick.x + BRICK_W &&
            ball.y + BALL > brick.y && ball.y < brick.y + BRICK_H) {
          brick.alive = false;
          bricksLeft--;
          score += 10;
          // Bounce off the shorter overlap axis.
          var overlapX = Math.min(ball.x + BALL - brick.x, brick.x + BRICK_W - ball.x);
          var overlapY = Math.min(ball.y + BALL - brick.y, brick.y + BRICK_H - ball.y);
          if (overlapX < overlapY) { ball.vx = -ball.vx; } else { ball.vy = -ball.vy; }
          break;
        }
      }
      if (bricksLeft === 0) { state = "won"; }

      if (ball.y > H) {
        lives--;
        if (lives <= 0) { state = "lost"; } else { ball.stuck = true; }
      }
    }
    render();
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

render();
console.log("Breakout is playing on the document canvas. Focus the controller window to steer.");
