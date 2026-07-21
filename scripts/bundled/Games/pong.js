// @name Pong
// @description A playable Pong game in a script window. Move with the Up/Down
// @description arrows or W/S; first to 5 wins. Close the window to quit.
// @author Seth A. Robinson
// @window

// ---------------------------------------------------------------------------
// Options - tweak the game here (no dialog before a game: it just starts).
var OPTIONS = {
  width: 640,        // window size
  height: 400,
  winningScore: 5,
  paddleHeight: 70,
  ballSpeed: 5,      // serve speed; rallies speed up on paddle hits
  rivalSpeed: 4.4,   // AI paddle speed cap - raise for a harder opponent
  sound: true        // retro blips (patchy.ui.playTone)
};
// ---------------------------------------------------------------------------

var W = OPTIONS.width;
var H = OPTIONS.height;
var win = patchy.ui.createCanvas({ width: W, height: H, title: "Patchy Pong" });

function sfx(freq, ms, vol) {
  if (OPTIONS.sound) { patchy.ui.playTone(freq, ms, vol, "square"); }
}

var paddleH = OPTIONS.paddleHeight;
var paddleW = 10;
var player = { y: (H - paddleH) / 2, score: 0 };
var rival = { y: (H - paddleH) / 2, score: 0 };
var ball = {};
var message = "First to " + OPTIONS.winningScore + " points!";
var messageTimer = 1500;

function resetBall(towardPlayer) {
  ball.x = W / 2;
  ball.y = H / 2;
  var angle = (Math.random() - 0.5) * 1.2;
  var speed = OPTIONS.ballSpeed;
  ball.vx = Math.cos(angle) * speed * (towardPlayer ? -1 : 1);
  ball.vy = Math.sin(angle) * speed;
}
resetBall(Math.random() < 0.5);

win.onFrame = function (dt) {
  var step = Math.min(dt, 50) / 16.0;

  // Player paddle.
  var speed = 6 * step;
  if (win.isKeyDown("Up") || win.isKeyDown("W")) { player.y -= speed; }
  if (win.isKeyDown("Down") || win.isKeyDown("S")) { player.y += speed; }
  player.y = Math.max(0, Math.min(H - paddleH, player.y));

  // Rival AI follows the ball with a speed cap.
  var target = ball.y - paddleH / 2;
  var rivalSpeed = OPTIONS.rivalSpeed * step;
  if (rival.y < target) { rival.y = Math.min(rival.y + rivalSpeed, target); }
  if (rival.y > target) { rival.y = Math.max(rival.y - rivalSpeed, target); }
  rival.y = Math.max(0, Math.min(H - paddleH, rival.y));

  // Ball.
  ball.x += ball.vx * step;
  ball.y += ball.vy * step;
  if (ball.y < 5) { ball.y = 5; ball.vy = Math.abs(ball.vy); sfx(330, 50, 0.3); }
  if (ball.y > H - 5) { ball.y = H - 5; ball.vy = -Math.abs(ball.vy); sfx(330, 50, 0.3); }
  // Player paddle at x = 20; rival at x = W - 30.
  if (ball.x < 30 && ball.x > 20 && ball.y > player.y - 5 && ball.y < player.y + paddleH + 5 && ball.vx < 0) {
    ball.vx = -ball.vx * 1.05;
    ball.vy += ((ball.y - (player.y + paddleH / 2)) / paddleH) * 6;
    sfx(880, 60, 0.4);
  }
  if (ball.x > W - 40 && ball.x < W - 30 && ball.y > rival.y - 5 && ball.y < rival.y + paddleH + 5 && ball.vx > 0) {
    ball.vx = -ball.vx * 1.05;
    ball.vy += ((ball.y - (rival.y + paddleH / 2)) / paddleH) * 6;
    sfx(660, 60, 0.4);
  }
  if (ball.x < 0) {
    rival.score++;
    var lost = rival.score >= OPTIONS.winningScore;
    message = lost ? "Patchy wins! Close the window." : "Patchy scores!";
    sfx(160, lost ? 400 : 250, 0.5);
    if (lost) { setTimeout(function () { sfx(120, 500, 0.5); }, 420); }
    messageTimer = 1200;
    resetBall(true);
  }
  if (ball.x > W) {
    player.score++;
    var won = player.score >= OPTIONS.winningScore;
    message = won ? "You win! Close the window." : "You score!";
    sfx(523, won ? 300 : 200, 0.45);
    if (won) { setTimeout(function () { sfx(784, 450, 0.45); }, 320); }
    messageTimer = 1200;
    resetBall(false);
  }
  var gameOver = player.score >= OPTIONS.winningScore || rival.score >= OPTIONS.winningScore;

  // Draw.
  var g = win.graphics;
  g.clear("#101418");
  for (var y = 0; y < H; y += 24) {
    g.fillRect(W / 2 - 1, y, 2, 12, "#3a4450");
  }
  g.fillRect(20, player.y, paddleW, paddleH, "#50c8ff");
  g.fillRect(W - 30, rival.y, paddleW, paddleH, "#ff8050");
  if (!gameOver) {
    g.circle(ball.x, ball.y, 5, "#ffffff", true);
  }
  g.text(W / 2 - 60, 30, player.score + "  :  " + rival.score, "#e0e0e0", 20);
  if (messageTimer > 0 || gameOver) {
    messageTimer -= dt;
    g.text(W / 2 - message.length * 4.5, H - 24, message, "#f0d060", 12);
  }
};

console.log("Pong is running: Up/Down or W/S to move. Close the window to quit.");
