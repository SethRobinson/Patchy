// @name Pong
// @window
// A playable Pong game in a script window. Move with the Up/Down arrows (or
// W/S); first to 5 wins. Close the window to quit.

var W = 640;
var H = 400;
var win = patchy.ui.createCanvas({ width: W, height: H, title: "Patchy Pong" });

var paddleH = 70;
var paddleW = 10;
var player = { y: (H - paddleH) / 2, score: 0 };
var rival = { y: (H - paddleH) / 2, score: 0 };
var ball = {};
var message = "First to 5 points!";
var messageTimer = 1500;

function resetBall(towardPlayer) {
  ball.x = W / 2;
  ball.y = H / 2;
  var angle = (Math.random() - 0.5) * 1.2;
  var speed = 5;
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
  var rivalSpeed = 4.4 * step;
  if (rival.y < target) { rival.y = Math.min(rival.y + rivalSpeed, target); }
  if (rival.y > target) { rival.y = Math.max(rival.y - rivalSpeed, target); }
  rival.y = Math.max(0, Math.min(H - paddleH, rival.y));

  // Ball.
  ball.x += ball.vx * step;
  ball.y += ball.vy * step;
  if (ball.y < 5) { ball.y = 5; ball.vy = Math.abs(ball.vy); }
  if (ball.y > H - 5) { ball.y = H - 5; ball.vy = -Math.abs(ball.vy); }
  // Player paddle at x = 20; rival at x = W - 30.
  if (ball.x < 30 && ball.x > 20 && ball.y > player.y - 5 && ball.y < player.y + paddleH + 5 && ball.vx < 0) {
    ball.vx = -ball.vx * 1.05;
    ball.vy += ((ball.y - (player.y + paddleH / 2)) / paddleH) * 6;
  }
  if (ball.x > W - 40 && ball.x < W - 30 && ball.y > rival.y - 5 && ball.y < rival.y + paddleH + 5 && ball.vx > 0) {
    ball.vx = -ball.vx * 1.05;
    ball.vy += ((ball.y - (rival.y + paddleH / 2)) / paddleH) * 6;
  }
  if (ball.x < 0) {
    rival.score++;
    message = rival.score >= 5 ? "Patchy wins! Close the window." : "Patchy scores!";
    messageTimer = 1200;
    resetBall(true);
  }
  if (ball.x > W) {
    player.score++;
    message = player.score >= 5 ? "You win! Close the window." : "You score!";
    messageTimer = 1200;
    resetBall(false);
  }
  var gameOver = player.score >= 5 || rival.score >= 5;

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
