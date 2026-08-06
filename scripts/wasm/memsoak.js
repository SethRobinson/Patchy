// @name Memory Soak (diagnostics)
// @description Bounded loop of memory-churning operations for the wasm memory
// @description diagnostics harness: each iteration creates a document, paints,
// @description filters, flattens, and closes it, so heap-size vs used-bytes
// @description divergence (the wasm ratchet signature) becomes visible.
// @cli --script-arg iterations=12 --script-arg size=2048
// @window
//
// Iterations chain through setTimeout so the main thread returns to the event
// loop between rounds: the 1 Hz memory telemetry (ui/wasm_memory_telemetry)
// and the harness page sampler only tick while the script yields. Idle timers
// keep the script run alive (see ui/script_engine.cpp).
var OPTIONS = { iterations: 12, size: 2048, settleMs: 1500, filter: true };

var options = patchy.ui.showOptions({
  title: "Memory Soak",
  description: "Diagnostics loop; see docs/performance.md (headless wasm stress harness).",
  fields: [
    { key: "iterations", label: "Iterations", type: "number", value: OPTIONS.iterations, min: 1, max: 500 },
    { key: "size", label: "Document size (px)", type: "number", value: OPTIONS.size, min: 256, max: 8192 },
    { key: "settleMs", label: "Settle between iterations (ms)", type: "number", value: OPTIONS.settleMs, min: 0, max: 10000 },
    { key: "filter", label: "Apply a Gaussian Blur each iteration", type: "checkbox", value: OPTIONS.filter },
  ],
});

if (options) {
  var iteration = 0;
  var runOne = function () {
    iteration += 1;
    var doc = app.newDocument(options.size, options.size);
    var layer = doc.addLayer("soak");
    layer.fillRect(0, 0, options.size, options.size, "#4080c0");
    if (options.filter) {
      layer.applyFilter("patchy.filters.gaussian_blur", { radius: 8 });
    }
    doc.flatten();
    doc.close();
    console.log("memsoak iteration " + iteration + "/" + options.iterations);
    patchy.ui.setStatusMessage("Memory soak " + iteration + "/" + options.iterations);
    if (iteration < options.iterations) {
      setTimeout(runOne, options.settleMs);
    } else {
      console.log("memsoak done");
    }
  };
  setTimeout(runOne, 0);
}
