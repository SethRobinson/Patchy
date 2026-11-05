const list = workspace.windowList();
for (let i = 0; i < list.length; ++i) {
  const w = list[i];
  print("WINDOW " + w.caption + " " + w.resourceClass + " " + w.frameGeometry.x + "," + w.frameGeometry.y + " " +
        w.frameGeometry.width + "x" + w.frameGeometry.height + " active=" + w.active);
  if (w.resourceClass.indexOf("patchy") >= 0 || w.caption.indexOf("Patchy") >= 0) {
    w.setMaximize(true, true);
    workspace.activeWindow = w;
  }
}
