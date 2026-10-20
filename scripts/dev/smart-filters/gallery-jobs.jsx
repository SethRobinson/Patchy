// Jobs for ps-probe-smart-filter-stack.ps1: Filter Gallery (GEfc) entries on a Smart Object
// (October 2026 capture, test-fixtures/psd/photoshop-smart-filter-gallery.psd). One effect sits
// inline in the GEfc object; two or more go in a GEfs list of GEfc objects.
function plasticWrap(d, highlight, detail, smoothness) {
  d.putEnumerated(cid('GEfk'), cid('GEft'), cid('PlsW'));
  d.putInteger(cid('Hghl'), highlight); d.putInteger(cid('Dtl '), detail); d.putInteger(cid('Smth'), smoothness);
  return d;
}
function gallery(effects) {
  var d = new ActionDescriptor();
  if (effects.length == 1) return effects[0];
  var l = new ActionList();
  for (var i = 0; i < effects.length; i++) l.putObject(cid('GEfc'), effects[i]);
  d.putList(cid('GEfs'), l);
  return d;
}
function cutout() {
  var d = new ActionDescriptor();
  d.putEnumerated(cid('GEfk'), cid('GEft'), cid('Ct  '));
  d.putInteger(cid('NmbL'), 4); d.putInteger(cid('EdgS'), 4); d.putInteger(cid('EdgF'), 2);
  return d;
}
var JOBS = [
  {name: 'Gallery Plastic Wrap', run: function () { apply('GEfc', gallery([plasticWrap(new ActionDescriptor(), 13, 9, 7)])); }},
  {name: 'Direct Plastic Wrap', run: function () {
    var d = new ActionDescriptor();
    d.putInteger(cid('Hghl'), 13); d.putInteger(cid('Dtl '), 9); d.putInteger(cid('Smth'), 7);
    apply('PlsW', d); }},
  {name: 'Gallery two Plastic Wraps', run: function () {
    apply('GEfc', gallery([plasticWrap(new ActionDescriptor(), 13, 9, 7), plasticWrap(new ActionDescriptor(), 5, 3, 12)])); }},
  {name: 'Gallery Plastic Wrap then Gaussian', run: function () {
    apply('GEfc', gallery([plasticWrap(new ActionDescriptor(), 20, 15, 1)]));
    var g = new ActionDescriptor(); g.putUnitDouble(cid('Rds '), cid('#Pxl'), 1.5); apply('GsnB', g); }},
  {name: 'Gallery Plastic Wrap and Cutout', run: function () {
    apply('GEfc', gallery([plasticWrap(new ActionDescriptor(), 13, 9, 7), cutout()])); }},
  {name: 'Gallery Plastic Wrap defaults', run: function () {
    var d = new ActionDescriptor(); d.putEnumerated(cid('GEfk'), cid('GEft'), cid('PlsW')); apply('GEfc', d); }}
];
