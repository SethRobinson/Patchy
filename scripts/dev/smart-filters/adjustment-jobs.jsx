// Jobs for ps-probe-smart-filter-stack.ps1: Image > Adjustments applied to a Smart Object as
// Smart Filters, in the descriptor forms Photoshop's dialogs record (October 2026 capture,
// test-fixtures/psd/photoshop-smart-filter-adjustments.psd).
function lvla(c, inpt, gmm, otpt) {
  var d = new ActionDescriptor();
  d.putReference(cid('Chnl'), chnl(c));
  if (inpt) { var l = new ActionList(); l.putInteger(inpt[0]); l.putInteger(inpt[1]); d.putList(cid('Inpt'), l); }
  if (gmm !== null) d.putDouble(cid('Gmm '), gmm);
  if (otpt) { var o = new ActionList(); o.putInteger(otpt[0]); o.putInteger(otpt[1]); d.putList(cid('Otpt'), o); }
  return d;
}
function levels(records, preset) {
  var d = new ActionDescriptor();
  if (preset) d.putEnumerated(sid('presetKind'), sid('presetKindType'), sid('presetKindCustom'));
  var l = new ActionList();
  for (var i = 0; i < records.length; i++) l.putObject(cid('LvlA'), records[i]);
  d.putList(cid('Adjs'), l);
  return d;
}
function crva(c, points) {
  var d = new ActionDescriptor();
  d.putReference(cid('Chnl'), chnl(c));
  var l = new ActionList();
  for (var i = 0; i < points.length; i++) {
    var p = new ActionDescriptor();
    p.putDouble(cid('Hrzn'), points[i][0]); p.putDouble(cid('Vrtc'), points[i][1]);
    l.putObject(cid('Pnt '), p);
  }
  d.putList(cid('Crv '), l);
  return d;
}
function curves(records, preset) {
  var d = new ActionDescriptor();
  if (preset) d.putEnumerated(sid('presetKind'), sid('presetKindType'), sid('presetKindCustom'));
  var l = new ActionList();
  for (var i = 0; i < records.length; i++) l.putObject(cid('CrvA'), records[i]);
  d.putList(cid('Adjs'), l);
  return d;
}
function hueSat(colorize, h, s, l, preset) {
  var d = new ActionDescriptor();
  if (preset) d.putEnumerated(sid('presetKind'), sid('presetKindType'), sid('presetKindCustom'));
  d.putBoolean(cid('Clrz'), colorize);
  var a = new ActionDescriptor();
  a.putInteger(cid('H   '), h); a.putInteger(cid('Strt'), s); a.putInteger(cid('Lght'), l);
  var list = new ActionList(); list.putObject(cid('Hst2'), a);
  d.putList(cid('Adjs'), list);
  return d;
}
function brgc(b, c, legacy) {
  var d = new ActionDescriptor();
  d.putInteger(cid('Brgh'), b); d.putInteger(cid('Cntr'), c);
  if (legacy !== null) d.putBoolean(sid('useLegacy'), legacy);
  return d;
}
function ints(values) { var l = new ActionList(); for (var i = 0; i < values.length; i++) l.putInteger(values[i]); return l; }
function colorBalance(sh, md, hl, preserve) {
  var d = new ActionDescriptor();
  d.putList(cid('ShdL'), ints(sh)); d.putList(cid('MdtL'), ints(md)); d.putList(cid('HghL'), ints(hl));
  d.putBoolean(cid('PrsL'), preserve);
  return d;
}
function chmx(r, g, b, k) {
  var d = new ActionDescriptor();
  d.putInteger(cid('Rd  '), r); d.putInteger(cid('Grn '), g); d.putInteger(cid('Bl  '), b);
  if (k !== null) d.putInteger(cid('Cnst'), k);
  return d;
}
var JOBS = [
  {name: 'Levels master', run: function () { apply('Lvls', levels([lvla('Cmps', [8, 210], 0.8, null)], false)); }},
  {name: 'Levels output', run: function () { apply('Lvls', levels([lvla('Cmps', null, null, [16, 235])], false)); }},
  {name: 'Levels channels preset', run: function () { apply('Lvls', levels([lvla('Rd  ', [20, 240], 1.5, null), lvla('Bl  ', null, 0.6, [10, 250]), lvla('Cmps', null, 0.7, null)], true)); }},
  {name: 'Curves master', run: function () { apply('Crvs', curves([crva('Cmps', [[0, 0], [37, 30], [223, 230], [255, 255]])], false)); }},
  {name: 'Curves channels preset', run: function () { apply('Crvs', curves([crva('Grn ', [[0, 20], [128, 160], [255, 240]]), crva('Cmps', [[0, 0], [30, 217], [94, 161], [161, 225], [228, 189], [255, 255]])], true)); }},
  {name: 'Hue Saturation desaturate', run: function () { apply('HStr', hueSat(false, 0, -27, 0, false)); }},
  {name: 'Hue Saturation master', run: function () { apply('HStr', hueSat(false, 40, 30, -20, true)); }},
  {name: 'Hue Saturation colorize', run: function () { apply('HStr', hueSat(true, 30, 25, 0, false)); }},
  {name: 'Brightness Contrast modern', run: function () { apply('BrgC', brgc(20, 10, false)); }},
  {name: 'Brightness Contrast no legacy key', run: function () { apply('BrgC', brgc(-35, 45, null)); }},
  {name: 'Brightness Contrast legacy', run: function () { apply('BrgC', brgc(-30, 40, true)); }},
  {name: 'Color Balance preserve', run: function () { apply('ClrB', colorBalance([46, 0, -55], [0, -19, -80], [0, 12, -5], true)); }},
  {name: 'Color Balance plain', run: function () { apply('ClrB', colorBalance([10, -20, 0], [35, -35, -35], [-15, 0, 25], false)); }},
  {name: 'Channel Mixer', run: function () {
    var d = new ActionDescriptor();
    d.putObject(cid('Rd  '), cid('ChMx'), chmx(30, 68, 8, 8));
    d.putObject(cid('Grn '), cid('ChMx'), chmx(24, 74, 8, 4));
    d.putObject(cid('Bl  '), cid('ChMx'), chmx(24, 68, 14, -2));
    apply('ChnM', d); }},
  {name: 'Channel Mixer monochrome', run: function () {
    var d = new ActionDescriptor();
    d.putBoolean(cid('Mnch'), true);
    d.putObject(cid('Gry '), cid('ChMx'), chmx(95, 6, 15, null));
    apply('ChnM', d); }},
  {name: 'Exposure', run: function () {
    var d = new ActionDescriptor();
    d.putDouble(cid('Exps'), 1.25); d.putDouble(cid('Ofst'), 0.02); d.putDouble(sid('gammaCorrection'), 0.9);
    apply('Exps', d); }},
  {name: 'Posterize', run: function () { var d = new ActionDescriptor(); d.putInteger(cid('Lvls'), 5); apply('Pstr', d); }},
  {name: 'Threshold', run: function () { var d = new ActionDescriptor(); d.putInteger(cid('Lvl '), 140); apply('Thrs', d); }},
  {name: 'Photo Filter', run: function () {
    var d = new ActionDescriptor(); var lab = new ActionDescriptor();
    lab.putDouble(cid('Lmnc'), 67.06); lab.putDouble(cid('A   '), 32); lab.putDouble(cid('B   '), 66);
    d.putObject(cid('Clr '), cid('LbCl'), lab); d.putInteger(cid('Dnst'), 25); d.putBoolean(cid('PrsL'), true);
    apply('photoFilter', d); }},
  {name: 'Black White', run: function () {
    var d = new ActionDescriptor();
    d.putInteger(cid('Rd  '), 40); d.putInteger(cid('Yllw'), 60); d.putInteger(cid('Grn '), 40);
    d.putInteger(cid('Cyn '), 60); d.putInteger(cid('Bl  '), 20); d.putInteger(cid('Mgnt'), 80);
    d.putBoolean(sid('useTint'), false);
    apply('BanW', d); }},
  {name: 'Levels then Curves', run: function () {
    apply('Lvls', levels([lvla('Cmps', [8, 210], 0.8, null)], false));
    apply('Crvs', curves([crva('Cmps', [[0, 0], [37, 30], [223, 230], [255, 255]])], false)); }},
  {name: 'Gaussian then Hue Saturation', run: function () {
    var g = new ActionDescriptor(); g.putUnitDouble(cid('Rds '), cid('#Pxl'), 1.5); apply('GsnB', g);
    apply('HStr', hueSat(false, 40, 30, -20, false)); }},
  {name: 'Levels auto', run: function () { var d = new ActionDescriptor(); d.putBoolean(cid('Auto'), true); apply('Lvls', d); }}
];
