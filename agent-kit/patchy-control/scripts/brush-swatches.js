// Inspect native brush capabilities and save swatches without document edits.
var tips = patchy.brushes.listTips();
var choices = tips.filter(function(t) { return /oil|chalk|bristle|charcoal/i.test(t.name); }).slice(0, 6);
if (!choices.length) choices = tips.slice(0, 3);
var out = patchy.args.out;
var results = choices.map(function(t, i) {
  var settings = {tipId:t.id, size:35, color:'#965329', flow:40, seed:12};
  var resolved = patchy.brushes.resolve(settings);
  var preview = out ? patchy.brushes.renderPreview(out + '/brush-' + i + '.png', settings) : null;
  return {name:t.name, settings:resolved.settings, preview:preview};
});
patchy.setResult(results);
