// The example theme's script. It runs in Patchy's locked-down theme engine: it can play
// the package's sounds and effects and add commands to the theme menu, nothing else.
// The API is in scripts/bundled/patchy-theme.d.ts; docs/theme-files.md has the limits.
patchy.theme.addCommand({ id: 'celebrate', title: 'Celebrate' }, function () {
  patchy.theme.playEffect('fireworks');
  patchy.theme.playSound('sounds/save-done.wav');
});
