// Game of Life
// Runs Conway's Game of Life directly inside a real layer of a real document:
// every generation is written into the layer's pixels with setPixels. Watch it
// live on the canvas; when it stops, the final state is an ordinary layer and
// one undo removes the whole run.

var CELL = 4;      // pixels per cell
var COLS = 96;
var ROWS = 64;
var GENERATIONS = 200;

var doc = app.newDocument(COLS * CELL, ROWS * CELL);
var layer = doc.addLayer("Game of Life");

var grid = new Array(COLS * ROWS);
var next = new Array(COLS * ROWS);
for (var i = 0; i < grid.length; i++) {
  grid[i] = Math.random() < 0.28 ? 1 : 0;
}

var pixels = new Uint8Array(COLS * CELL * ROWS * CELL * 4);

function renderGrid() {
  var width = COLS * CELL;
  for (var cy = 0; cy < ROWS; cy++) {
    for (var cx = 0; cx < COLS; cx++) {
      var alive = grid[cy * COLS + cx] === 1;
      for (var py = 0; py < CELL; py++) {
        for (var px = 0; px < CELL; px++) {
          var offset = ((cy * CELL + py) * width + cx * CELL + px) * 4;
          if (alive) {
            pixels[offset] = 80;
            pixels[offset + 1] = 220;
            pixels[offset + 2] = 120;
            pixels[offset + 3] = 255;
          } else {
            pixels[offset] = 12;
            pixels[offset + 1] = 16;
            pixels[offset + 2] = 24;
            pixels[offset + 3] = 255;
          }
        }
      }
    }
  }
  layer.setPixels({ x: 0, y: 0, width: width, height: ROWS * CELL, data: pixels.buffer });
}

function stepGrid() {
  var changed = false;
  for (var y = 0; y < ROWS; y++) {
    for (var x = 0; x < COLS; x++) {
      var neighbors = 0;
      for (var dy = -1; dy <= 1; dy++) {
        for (var dx = -1; dx <= 1; dx++) {
          if (dx === 0 && dy === 0) { continue; }
          var nx = (x + dx + COLS) % COLS;
          var ny = (y + dy + ROWS) % ROWS;
          neighbors += grid[ny * COLS + nx];
        }
      }
      var alive = grid[y * COLS + x] === 1;
      var lives = alive ? (neighbors === 2 || neighbors === 3) : neighbors === 3;
      next[y * COLS + x] = lives ? 1 : 0;
      if (lives !== alive) { changed = true; }
    }
  }
  var swap = grid; grid = next; next = swap;
  return changed;
}

renderGrid();
var generation = 0;
console.log("Running " + GENERATIONS + " generations of " + COLS + "x" + ROWS + " Life...");
var timer = setInterval(function () {
  generation++;
  var changed = stepGrid();
  renderGrid();
  if (!changed || generation >= GENERATIONS) {
    clearInterval(timer);
    console.log("Stopped after " + generation + " generations.");
  }
}, 33);
