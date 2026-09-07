// Patchy scripting API type definitions (API version 1).
//
// Reference for editors (VS Code autocomplete) and for AI agents driving
// Patchy through `patchy --run-script`. Scripts are plain JavaScript run by
// Patchy's embedded engine (ES6-level); this file is documentation, it is
// never executed. The human-readable companion is scripting-guide.md next to
// this file (Help in the Script Manager opens it).
//
// Script header directives (read by the Script Manager and File > Scripts
// menu from the comment block at the top of a .js file; parsing stops at the
// first non-comment line):
//   // @name Breakout        display name shown instead of the file name
//   // @description ...      hover-card blurb (repeat the line to continue it)
//   // @author Jane Doe      hover-card credit line
//   // @window               the script creates its own window or document
//                            (shown as a window badge; scripts without it
//                            work on the active document)
//   // @cli ...              the argument part of the script's command-line
//                            example, everything after "--run-script <script>"
//                            (repeat the line to continue it). Shown by the
//                            Script Manager's C:\ button; without it the
//                            example falls back to an "example.png"
//                            placeholder for active-document scripts.
// A 128x128 PNG next to the script with the same base name (breakout.js ->
// breakout.png) becomes its icon; right-click a script in the Script Manager
// for "Set Icon from Current Window".
//
// Convention: keep the tweakable defaults in an OPTIONS object at the top of
// the script and pass them through patchy.ui.showOptions, which handles the
// options dialog, --script-arg overrides, and unattended runs in one call.
//
// Long-running scripts: there is NO runtime limit - a batch job may run for
// hours. The watchdog only stops a script that shows no sign of life (no
// pixel write, file operation, or console output) for 2 minutes, so log
// progress periodically inside heavy pure-JS computations. GUI runs longer
// than half a second show a busy overlay and a Stop panel automatically.
//
// Color strings use Qt order: "#rrggbb", "#aarrggbb", or named ("red").
// Blend mode ids: "pass-through", "normal", "dissolve", "multiply", "screen", "overlay",
// "darken", "lighten", "color-dodge", "color-burn", "hard-light",
// "soft-light", "difference", "linear-burn", "pin-light", "saturation",
// "luminosity", "exclusion", "hue", "color", "linear-dodge", "subtract",
// "divide", "vivid-light", "linear-light", "hard-mix", "darker-color",
// "lighter-color".

interface PatchyRect {
  x: number;
  y: number;
  width: number;
  height: number;
}

interface PatchyStroke extends PatchyBrushSettings {
  /** 1..100000 points across the batch; coordinates -100000..100000 document pixels. */
  points: PatchyStrokePoint[];
}
interface PatchyStrokePoint {
  x: number; y: number;
  /** 0..1; omitted inputs are unavailable, never inherited from a tablet. */
  pressure?: number;
  /** Degrees -90..90, supplied together. */
  xTilt?: number; yTilt?: number;
  /** Barrel rotation in degrees -360..360. */
  rotation?: number;
  /** Stylus wheel -1..1. */
  tangentialPressure?: number;
  /** Integer 0..3600000. Every point or none; starts at zero, nondecreasing.
   * Airbrush requires timestamps. Repeated positions with later times dwell.
   * At most 1000000 airbrush/smoothing ticks across a stroke batch. */
  timeMs?: number;
}
type PatchyBrushControl = "off" | "fade" | "penPressure" | "penTilt" | "penRotation" | "stylusWheel";
interface PatchyBrushDynamics {
  sizeJitter?: number; minimumDiameter?: number;
  sizeControl?: PatchyBrushControl | "global"; sizeFadeSteps?: number;
  angleJitter?: number; angleControl?: PatchyBrushControl | "direction" | "initialDirection"; angleFadeSteps?: number;
  roundnessJitter?: number; minimumRoundness?: number;
  roundnessControl?: PatchyBrushControl | "global"; roundnessFadeSteps?: number;
  flipXJitter?: boolean; flipYJitter?: boolean;
  /** Scatter 0..10; other jitter/minimum fractions 0..1. */
  scatter?: number; scatterBothAxes?: boolean; scatterControl?: PatchyBrushControl; scatterFadeSteps?: number;
  /** Count integer 1..16; every FadeSteps field integer 1..9999. */
  count?: number; countJitter?: number; countControl?: PatchyBrushControl; countFadeSteps?: number;
  opacityJitter?: number; minimumOpacity?: number; opacityControl?: PatchyBrushControl | "global"; opacityFadeSteps?: number;
  flowJitter?: number; minimumFlow?: number; flowControl?: PatchyBrushControl; flowFadeSteps?: number;
  textureEnabled?: boolean; textureStyle?: "fineGrain" | "canvas" | "speckle";
  /** Static grain scale .01..10, depth 0..1, unsigned 32-bit seed. */
  textureScale?: number; textureDepth?: number; textureInvert?: boolean; textureSeed?: number;
  dualBrushEnabled?: boolean;
  /** Secondary size .05..4, hardness 0..1, spacing .1..10. */
  dualBrushSize?: number; dualBrushHardness?: number; dualBrushSpacing?: number;
  colorDynamicsEnabled?: boolean; foregroundBackgroundJitter?: number;
  colorControl?: PatchyBrushControl; colorFadeSteps?: number;
  hueJitter?: number; saturationJitter?: number; brightnessJitter?: number;
  /** Purity -1..1; Color Dynamics varies selected colors, never canvas pickup. */
  purity?: number; colorPerTip?: boolean;
  /** Whole-stroke wash boundary treatment, not Mixer pickup. Disabled in palette mode. */
  wetEdges?: boolean;
}
interface PatchyBrushSettings {
  /** Default brush; Mixer uses Flow with its native full opacity. */
  tool?: "brush" | "eraser" | "mixer";
  presetId?: string;
  tipId?: string;
  /** Fraction of brush diameter .01..10; absent preserves native/default tip spacing. */
  spacing?: number;
  /** Degrees -180..360; roundness integer percent 1..100. */
  angle?: number; roundness?: number;
  backgroundColor?: string;
  /** Brush only. Input fields are validated before the batch paints. */
  dynamics?: PatchyBrushDynamics;
  airbrush?: boolean;
  /** Mixer only; Wet/Mix 0..100, Load 1..100. Defaults 50; Sample All Layers false. */
  mixer?: {wet?: number; load?: number; mix?: number; sampleAllLayers?: boolean};
  /** Explicit mapping of "global" controls; independent of artist preferences. */
  pen?: {pressureSize?: boolean; pressureOpacity?: boolean; sizeMinimum?: number;
    opacityMinimum?: number; tiltShape?: boolean; tiltMinimumRoundness?: number};
  /** Amount 0..100, default 0. Default pulledString false; catch-up/end/adjustForZoom true.
   * referenceZoom is a percentage .01..100000, default 100; used for screen-relative smoothing. */
  smoothing?: {amount?: number; pulledString?: boolean; catchUp?: boolean;
    catchUpOnEnd?: boolean; adjustForZoom?: boolean; referenceZoom?: number};
  /** Default black; CSS/Qt color, #rrggbb or #aarrggbb. */
  color?: string;
  /** Integer 1..1024, default 1. Size-one paths use exact pixel segments. */
  size?: number;
  /** Integer 1..100, default 100: opacity caps a stroke, Flow meters each dab. */
  opacity?: number;
  flow?: number;
  /** Integer 0..100, default 0. */
  softness?: number;
  /** Unsigned 32-bit integer, default 0. Identical strokes and seeds reproduce pixels. */
  seed?: number;
  /** Fractions: 0..1 size jitter and 0..10 scatter; defaults 0, Brush only. */
  sizeJitter?: number;
  scatter?: number;
  // Pressure 0..1 defaults to unavailable/full strength. Explicit pressure scales
  // size and opacity with the native 20% / 15% floors; it does not inherit preferences.
}

interface PatchyBrushTipInfo {
  id: string; name: string; source: "builtin" | "library" | "session"; folder?: string;
  width?: number; height?: number; spacing?: number; angle?: number; roundness?: number;
  dynamics?: PatchyBrushDynamics;
}
interface PatchyBrushPreset {
  id: string; name: string; source: "builtin" | "user"; folder?: string;
  includeColors?: boolean; settings: PatchyBrushSettings;
}
interface PatchyBrushes {
  listTips(): PatchyBrushTipInfo[];
  getTip(id: string): PatchyBrushTipInfo;
  listPresets(): PatchyBrushPreset[];
  getPreset(id: string): PatchyBrushPreset;
  /** Detached current brush/pen settings; requires an active document. Captured tip
   * IDs live for this Patchy process. savePreset makes a persistent independent copy. */
  getCurrent(): PatchyBrushSettings;
  resolve(settings?: PatchyBrushSettings): {settings: PatchyBrushSettings;
    capabilities: {dynamics: boolean; mixer: boolean; airbrush: boolean; timingRequired: boolean}};
  /** Explicitly changes the selected UI tool/settings; ordinary strokes restore them. */
  activate(settings: PatchyBrushSettings): ReturnType<PatchyBrushes["resolve"]>;
  /** Native swatch PNG, default 320x160; each dimension 32..1024. No document edits/Undo. */
  renderPreview(path: string, settings?: PatchyBrushSettings,
    options?: {width?: number; height?: number; backgroundColor?: string}): {path: string; width: number; height: number};
  /** Creates a persistent library tip. Buffers are 8-bit coverage (255 paints), max 4096x4096.
   * Images/documents use inverted luminance times alpha. Selection applies by default. */
  createTip(name: string, source: string | {width: number; height: number; data: ArrayBuffer} |
    {documentId: string; rect?: PatchyRect; useSelection?: boolean},
    options?: {spacing?: number; folder?: string}): PatchyBrushTipInfo;
  importAbr(path: string, options?: {}): {ids: string[]; warnings: {message: string}[]};
  /** Saves an independent tip/settings snapshot. Colors excluded by default. Library writes are outside document Undo. */
  savePreset(name: string, settings: PatchyBrushSettings,
    options?: {includeColors?: boolean; folder?: string}): PatchyBrushPreset;
  /** Replaces settings explicitly, retaining name/folder/color inclusion unless supplied. */
  updatePreset(id: string, settings: PatchyBrushSettings,
    options?: {name?: string; includeColors?: boolean; folder?: string}): PatchyBrushPreset;
  duplicatePreset(id: string, name: string,
    options?: {includeColors?: boolean; folder?: string}): PatchyBrushPreset;
  removePreset(id: string): boolean;
}

interface PatchyPreviewOptions {
  /** Document rectangle, clipped to the canvas; positive size, integer coordinates. */
  rect?: PatchyRect;
  /** Integer 1..4096, defaults 1024. Aspect ratio is preserved. */
  maxWidth?: number;
  maxHeight?: number;
  /** Default false: smooth downscale only. True permits pixel-sharp enlargement. */
  nearestNeighbor?: boolean;
}

interface PatchyPreview {
  documentId: string;
  rect: PatchyRect;
  width: number;
  height: number;
  scaleX: number;
  scaleY: number;
  path: string;
  offscreen: boolean;
}

/** RGBA8 pixel block; data holds width * height * 4 bytes. */
interface PatchyImageData {
  /** Document-space position of the block. */
  x: number;
  y: number;
  width: number;
  height: number;
  data: ArrayBuffer;
}

interface PatchyLayer {
  /** Decimal string identity, scoped to this open document. Re-query after undo/reopen. */
  readonly id: string;
  /** Native Brush/Eraser paths; validated as a batch before any painting. */
  drawStrokes(strokes: PatchyStroke[]): void;
  name: string;
  /** 0..100 */
  opacity: number;
  visible: boolean;
  /** Blend mode id string (see the list above). */
  blendMode: string;
  locked: boolean;
  /** Content offset in document pixels; setting either moves the layer. */
  x: number;
  y: number;
  readonly bounds: PatchyRect;
  readonly isGroup: boolean;
  readonly isText: boolean;
  readonly isShape: boolean;
  getShape(): PatchyShapeState | null;
  /** Partial update. geometry and path are mutually exclusive; group targets one existing shape group. */
  updateShape(changes: {geometry?: PatchyVectorGeometry; group?: number; path?: PatchyVectorPath;
    fill?: PatchyVectorPaint; stroke?: PatchyVectorStroke; pathDisabled?: boolean; pathInverted?: boolean}): void;
  /** Affine [a,b,c,d,tx,ty]: x'=a*x+c*y+tx, y'=b*x+d*y+ty. Native stroke width stays fixed unless strokeScale is supplied. */
  transformShape(matrix: PatchyVectorMatrix, options?: {strokeScale?: number}): void;
  getVectorMask(): PatchyVectorMask | null;
  /** Creates or partially updates a mask on an ordinary layer or group. For shapes, mask their group.
   * An empty path reveals all, or hides all when inverted. */
  setVectorMask(options: Partial<PatchyVectorMask>): void;
  removeVectorMask(): void;
  transformVectorMask(matrix: PatchyVectorMatrix): void;
  rasterizeVectorMask(): void;
  /** Native raster fill on an unlocked RGB/RGBA8 pixel layer; selection clips the paint. */
  fillPath(path: PatchyVectorPath, options?: {paint?: PatchyVectorPaint; opacity?: number}): void;
  /** Native Brush/Eraser along actual segments, including the closing segment only for closed paths. */
  strokePath(path: PatchyVectorPath, options?: PatchyBrushSettings & {pressure?: number; durationMs?: number}): void;
  /** Child layers (groups only). */
  readonly children: PatchyLayer[];
  /** Text layers: setting text re-renders the layer; an empty string clears its ink. */
  text: string;

  /** Finite signed 32-bit positions; throws if the position or resulting bounds overflow. */
  moveTo(x: number, y: number): void;
  /** Inserts the copy directly above this layer; returns it. */
  duplicate(): PatchyLayer;
  remove(): void;
  /** Ungroups this folder into its parent; returns the released layers top to bottom. */
  ungroup(): PatchyLayer[];
  /** Fills the selection (or the whole canvas on an empty layer). RGB8 and RGBA8 are supported. */
  fill(color: string): void;
  /**
   * Overwrites one document-space rect of the layer's pixels (clipped to its
   * buffer); a transparent color like "#00000000" clears. On an empty layer
   * this allocates a buffer covering exactly the rect, so small sprite layers
   * can be created with one call and then animated cheaply via x/y. Throws
   * for a side over 30000.
   */
  fillRect(x: number, y: number, width: number, height: number, color: string): void;
  /**
   * Applies a filter to this layer's pixels by registry id, e.g.
   * applyFilter("patchy.filters.gaussian_blur", {radius: 8}). Unknown ids or
   * parameters throw.
   */
  applyFilter(filterId: string, params?: Record<string, number | boolean | string>): void;
  /**
   * A copy of the layer's pixels (empty layers report width/height 0). Layers
   * that store opaque 8-bit RGB (photos opened from JPEG and similar) are
   * returned expanded to RGBA with alpha 255.
   */
  getPixels(): PatchyImageData;
  /**
   * Replaces the layer's pixels. data must hold width * height * 4 RGBA
   * bytes; x/y default to the layer's current position. In palette mode the
   * pixels snap to the document palette (like every tool write).
   */
  setPixels(imageData: PatchyImageData): void;
  /**
   * Trace Image to Shapes: quantizes this pixel layer to a few colors and
   * fits every color region into shape layers (one solid-fill shape layer per
   * color, named "#RRGGBB"). The shape layers land in a new group inserted
   * above this layer, this layer is hidden, and the group is returned (null
   * when nothing traced, e.g. a fully transparent layer). Options mirror the
   * Layer > New > Trace Image to Shapes dialog; omitted fields use its defaults.
   * With a document selection only the selected area is traced.
   */
  traceToShapes(options?: PatchyTraceOptions): PatchyLayer | null;
  /**
   * Simplify Path: refits this shape layer's path (or its vector mask) with
   * fewer anchors within the tolerance, like Layer > Shape > Simplify Path.
   * Returns {anchorsBefore, anchorsAfter}. Changed shapes lose their live
   * rectangle/ellipse parameters, like any direct path edit.
   */
  simplifyPath(options?: PatchySimplifyOptions): { anchorsBefore: number; anchorsAfter: number };
}

interface PatchySimplifyOptions {
  /** Maximum deviation in document pixels (default 1). */
  tolerance?: number;
  /** Bends sharper than this many degrees stay corners (default 60). */
  cornerAngle?: number;
  /** Collapse near-straight curves into straight segments (default false). */
  snapCurvesToLines?: boolean;
}

interface PatchyTraceOptions {
  /** "color" (default), "grayscale", or "blackAndWhite". */
  mode?: "color" | "grayscale" | "blackAndWhite";
  /** Palette size for color/grayscale, 2..256 (default 16). */
  colors?: number;
  /** Black-and-white luminance threshold, 1..255 (default 128). */
  threshold?: number;
  /** Curve fit fidelity 0..100 (default 50): higher follows the pixels more tightly. */
  paths?: number;
  /** Corner sharpness 0..100 (default 75): higher keeps more bends as corners. */
  corners?: number;
  /** Regions smaller than this many pixels merge into their neighbors, 1..100 (default 25). */
  noise?: number;
  /** Denoise blur before colors are chosen, 0..10 px (default 0 = off). */
  smoothing?: number;
  /** Anchor budget: curve fitting loosens until the result fits; 0 = unlimited (default). */
  maxAnchors?: number;
  /**
   * Merge traced colors within this per-channel difference into one (0..100,
   * default 0 = off). Keeps flat areas clean when the image has fewer
   * distinct colors than requested.
   */
  mergeColors?: number;
  /** "abutting" (exact cutouts, default) or "overlapping" (stacked, no holes). */
  method?: "abutting" | "overlapping";
  /** Replace nearly straight curves with straight segments (default false). */
  snapCurvesToLines?: boolean;
  /** Leave white regions untraced (default false). */
  ignoreWhite?: boolean;
  /**
   * With a document selection: pick the traced colors from every pixel of the
   * layer (default true), so a selection traces with the same colors as the
   * whole layer. false chooses colors only from the selected pixels. Ignored
   * without a selection.
   */
  paletteFromLayer?: boolean;
}

interface PatchySelection {
  fromPath(path: PatchyVectorPath, options?: {operation?: "replace" | "add" | "subtract" | "intersect";
    feather?: number; antialias?: boolean}): void;
  /** Fits the hard selection boundary once. tolerance: 0.5..10 px, default 2. */
  toPath(options?: {tolerance?: number}): PatchyVectorPath;
  readonly exists: boolean;
  /** Bounding box, or undefined when there is no selection. */
  readonly bounds: PatchyRect | undefined;
  selectAll(): void;
  deselect(): void;
  /** Sides are limited to 30000; larger values throw. */
  /** Clips to the canvas; a disjoint rectangle clears the selection. */
  selectRect(x: number, y: number, width: number, height: number): void;
  selectEllipse(x: number, y: number, width: number, height: number): void;
}

interface PatchyDocument {
  readonly paths: PatchyDocumentPath[];
  readonly workPath: PatchyDocumentPath | null;
  clippingPath: PatchyDocumentPath | null;
  getPath(id: string): PatchyDocumentPath;
  addPath(name: string, data: PatchyVectorPath): PatchyDocumentPath;
  /** Replaces the single work path while preserving its ID, or creates it. */
  setWorkPath(data: PatchyVectorPath): PatchyDocumentPath;
  /** Creates a top-level native shape and activates it. Defaults: black fill, no stroke. */
  addShape(name: string, geometry: PatchyVectorGeometry,
    appearance?: {fill?: PatchyVectorPaint; stroke?: PatchyVectorStroke}): PatchyLayer;
  addFillLayer(name: string, paint: PatchyVectorPaint): PatchyLayer;
  addGroup(name: string): PatchyLayer;
  /** Siblings only; preserves their bottom-to-top order, inserts the group at the bottom selected position. */
  groupLayers(layers: PatchyLayer[], name: string): PatchyLayer;
  /** index is measured AFTER removing the moved layers. null/omitted parentId means document root; omitted index appends. */
  moveLayers(layers: PatchyLayer[], destination: {parentId?: string | null; index?: number}): void;
  listVectorResources(): {customShapes: {resourceId: string; name: string; folder: string}[];
    gradients: {presetId: string; name: string; folder: string}[];
    patterns: {source: "library" | "document"; resourceId: string; name: string; folder?: string; width: number; height: number}[]};
  /** Decimal string identity, valid while this document remains open. */
  readonly id: string;
  readonly modified: boolean;
  readonly canUndo: boolean;
  readonly canRedo: boolean;
  /** Throws when the ID is absent from the current document. */
  getLayer(id: string): PatchyLayer;
  /** Restore one history step. Call before any edits in this script; false if unavailable. */
  undo(): boolean;
  redo(): boolean;
  /** Writes a PNG without changing path, modified status, or history. */
  renderPreview(path: string, options?: PatchyPreviewOptions): PatchyPreview;
  readonly width: number;
  readonly height: number;
  readonly name: string;
  /** File path, empty for unsaved documents. */
  readonly path: string;
  /** Pixels per inch. */
  readonly resolution: number;
  /** Top-level layers, bottom to top; groups expose .children. */
  readonly layers: PatchyLayer[];
  /**
   * Setting it also reveals the layer's row in the Layers panel (collapsed
   * ancestor folders expand and the row scrolls into view), like a click.
   */
  activeLayer: PatchyLayer | undefined;
  readonly selection: PatchySelection;

  /** Adds an empty pixel layer on top and makes it active. */
  addLayer(name: string): PatchyLayer;
  /**
   * Adds a text layer rendered through Patchy's text engine. Options:
   * {font, size, x, y, color, bold, italic}; x/y is the text anchor point.
   * size is the text height in DOCUMENT PIXELS, independent of the canvas
   * zoom and the document PPI (the Character panel shows the pt equivalent).
   */
  addTextLayer(text: string, options?: {
    font?: string; size?: number; x?: number; y?: number;
    color?: string; bold?: boolean; italic?: boolean;
  }): PatchyLayer;
  /** First layer (depth-first) with this exact name, or undefined. */
  findLayer(name: string): PatchyLayer | undefined;
  /**
   * Combine Shapes: merges the shape layers (siblings of one folder) into the
   * bottom-most one and returns it; the others are removed. op: "unite",
   * "subtract" (front shapes cut from the base), "intersect", or "exclude".
   */
  combineShapes(layers: PatchyLayer[], op: "unite" | "subtract" | "intersect" | "exclude"): PatchyLayer;
  flatten(): void;
  resizeImage(width: number, height: number): void;
  resizeCanvas(width: number, height: number): void;
  /** Crops to the canvas intersection; throws if the rectangle is outside the canvas. */
  crop(x: number, y: number, width: number, height: number): void;
  /** Saves to the path; the format follows the extension (.psd, .png, ...). */
  saveAs(path: string): boolean;
  /** Same as saveAs; reads better for export-a-copy flows. */
  exportAs(path: string): boolean;
  /** Closes without prompting (the script decided). */
  close(): void;
  /** Makes this the active document tab. */
  activate(): void;
}

interface PatchyApp {
  /** Throws when the document is no longer open. */
  getDocument(id: string): PatchyDocument;
  readonly version: string;
  readonly apiVersion: number;
  readonly documents: PatchyDocument[];
  readonly activeDocument: PatchyDocument | undefined;
  /**
   * Normally a script run is one undo entry. Set false (ideally before the
   * first edit) to skip the undo snapshot for speed, e.g. games or huge batch
   * jobs; edits made while false cannot be undone. Resets to true each run.
   * Connector sessions reject false to preserve recoverable edit history.
   */
  undoEnabled: boolean;
  /** Opens a file; throws on failure. Unattended RAW/PDF opens use default import settings. */
  open(path: string): PatchyDocument;
  newDocument(width: number, height: number): PatchyDocument;
  /** Message box; logs to the console instead in unattended CLI runs. */
  alert(text: string): void;
  /**
   * Text input dialog. Returns the entered text, or null when cancelled.
   * Unattended CLI runs return defaultValue.
   */
  prompt(text: string, defaultValue?: string): string | null;
  /** Folder picker. Empty string when cancelled or in unattended CLI runs. */
  chooseFolder(title?: string): string;
  /**
   * File pickers. filter uses Qt syntax, e.g. "Images (*.png *.jpg)". Empty
   * string when cancelled or in unattended CLI runs.
   */
  chooseOpenFile(title?: string, filter?: string): string;
  chooseSaveFile(title?: string, filter?: string): string;
  /**
   * Triggers a registered application command (menu items, tools) by its
   * stable hotkey command id, e.g. runCommand("file.scripts.editor"). Returns
   * false when the id is unknown, disabled, or is edit.undo, edit.redo, or file.quit.
   * Use doc.undo/redo outside an active script mutation. Unattended commands
   * cancel dialogs and refuse to close modified documents; doc.close is explicit.
   */
  runCommand(commandId: string): boolean;
  /** Every registered command id, sorted. */
  commandIds(): string[];
}

/** One field of a patchy.ui.showDialog / showOptions form. */
interface PatchyDialogField {
  /** Property name of this field's value in the returned object. */
  key: string;
  /** Row label; defaults to key. */
  label?: string;
  /** folder/file rows are a path line edit plus a Browse button. */
  type: "number" | "slider" | "checkbox" | "choice" | "text" | "color" | "folder" | "file";
  /** Initial value (number, boolean, or string depending on type). */
  value?: number | boolean | string;
  /** number/slider only. */
  min?: number;
  max?: number;
  /** number only. */
  step?: number;
  decimals?: number;
  /** choice only: the dropdown entries. */
  choices?: string[];
  /** file only: the Browse dialog's name filter ("CSV files (*.csv)"). */
  filter?: string;
}

interface PatchyGraphics {
  clear(color: string): void;
  fillRect(x: number, y: number, width: number, height: number, color: string): void;
  strokeRect(x: number, y: number, width: number, height: number, color: string, lineWidth?: number): void;
  line(x1: number, y1: number, x2: number, y2: number, color: string, lineWidth?: number): void;
  circle(centerX: number, centerY: number, radius: number, color: string, filled?: boolean, lineWidth?: number): void;
  text(x: number, y: number, value: string, color: string, sizePt?: number): void;
  /** Draws a document layer or a getPixels()-style block. */
  drawImage(source: PatchyLayer | PatchyImageData, x: number, y: number): void;
}

/** An interactive window a script can open (games, demos). The script run
 *  stays alive while any window is open; closing the last one ends it. */
interface PatchyCanvasWindow {
  readonly width: number;
  readonly height: number;
  readonly graphics: PatchyGraphics;
  /** ~60fps tick; dt is milliseconds since the previous frame. The surface is
   *  presented automatically after each onFrame call. */
  onFrame: ((dt: number) => void) | undefined;
  /** Key names follow Qt: "Up", "Down", "Space", "A", ... */
  onKeyDown: ((key: string) => void) | undefined;
  onKeyUp: ((key: string) => void) | undefined;
  /** Button values: 1 left, 2 right, 4 middle; move reports held buttons as a bit mask. */
  onMouseDown: ((x: number, y: number, button: number) => void) | undefined;
  onMouseMove: ((x: number, y: number, button: number) => void) | undefined;
  onMouseUp: ((x: number, y: number, button: number) => void) | undefined;
  close(): void;
  /** Blits the surface now (normally automatic after onFrame). */
  present(): void;
  /** True while the key is held (case-insensitive name). */
  isKeyDown(key: string): boolean;
}

interface PatchyUi {
  createCanvas(options?: { width?: number; height?: number; title?: string }): PatchyCanvasWindow;
  /**
   * Modal form built from a declarative field list; returns an object with one
   * property per field key, or null when cancelled. An optional description
   * renders as instructions above the form. Unattended runs (CLI) return the
   * normalized defaults: choices return text, colors #rrggbb (or Qt #aarrggbb),
   * numbers are clamped, text defaults to "", checkboxes to false. Keys must
   * be nonempty strings. Example:
   *   var r = patchy.ui.showDialog({title: "Halftone", fields: [
   *     {key: "size", label: "Dot size", type: "slider", value: 4, min: 1, max: 32},
   *     {key: "invert", label: "Invert", type: "checkbox", value: false}]});
   *   if (r) { apply(r.size, r.invert); }
   */
  showDialog(spec: { title?: string; description?: string; fields: PatchyDialogField[] }):
      Record<string, number | boolean | string> | null;
  /**
   * The standard way for a script to ask for options: showDialog plus the
   * "defaults unless overridden" contract. Matching --script-arg key=value
   * tokens override the field defaults (coerced by field type; a bare
   * "--script-arg flag" turns a checkbox on), and unattended runs (patchy
   * --run-script, forwarded or not) skip the dialog entirely and return the
   * effective values. GUI runs show the dialog seeded with them; null still
   * means the user cancelled. Every bundled script with options uses this.
   */
  showOptions(spec: { title?: string; description?: string; fields: PatchyDialogField[] }):
      Record<string, number | boolean | string> | null;
  /**
   * Plays a short synthesized tone, fire-and-forget (the call returns
   * immediately; a new sound may cut off the previous one). Values are
   * clamped: frequency 20..20000 Hz (default 880), duration 1..4000 ms
   * (default 120), volume 0..1 (default 0.5); wave is "sine" (default) or
   * "square" (retro game blips). Playback is best-effort per platform (on
   * Linux it needs paplay, pw-play, or aplay on PATH) and PATCHY_NO_SOUND=1
   * silences it.
   */
  playTone(frequency?: number, durationMs?: number, volume?: number,
           wave?: "sine" | "square"): void;
  /**
   * Plays a .wav file (16/8-bit PCM is safest; 10 MB max), fire-and-forget.
   * Relative paths resolve like include(): beside the running script, then
   * the user scripts folder, then the bundled scripts. Throws when the file
   * is missing, oversized, or not a RIFF/WAVE file. Same per-platform
   * best-effort playback as playTone.
   */
  playSound(path: string): void;
  /**
   * Resizes the main window (clamped to 320x240..8192x8192). Staging for
   * automation that captures the app; a maximized window is restored first.
   */
  setWindowSize(width: number, height: number): void;
  /** Sets the width of the right panel stack (Layers/Channels/Paths). */
  setSidePanelWidth(width: number): void;
  /**
   * Saves a PNG capture of the main window (never raises or focuses it).
   * Returns false when the file cannot be written; throws on an empty path.
   */
  captureWindow(path: string): boolean;
  /** Shows a message in the main window's status bar (progress readouts). */
  setStatusMessage(message: string): void;
  /**
   * The active document's view zoom in percent, as the status bar shows it.
   * Reads 0 with no document. Setting clamps to 5..12800 and throws for NaN,
   * non-positive values, or with no document. Use doc.activate() first to
   * target another document. Only window captures (captureWindow, the
   * connector's window preview) see the view; document previews never do.
   */
  zoom: number;
  /** View > Fit on Screen for the active document; throws with no document. */
  fitOnScreen(): void;
  /** Show completed edits now; optionally hold the frame for 0..1000 ms (default 0).
   * Services Stop and painting without reentering script callbacks or adding history.
   * Upload edited arrays with setPixels first. Headless work does not need pacing.
   */
  present(delayMs?: number): void;
  /** Same as the status-bar Slow toggle. Defaults to false for each workspace;
   * retained between requests, never saved as a preference. While true, each
   * native stroke and each undoable document edit gets a separate Undo step.
   * Presents each completed edit with a short pause. Enabling requires a visible
   * workspace; headless runs reject it and retain normal grouped Undo.
   * Does not alter simulated paint time.
   * Existing history limits apply. Turning it off groups subsequent edits again.
   */
  slowMode: boolean;
  /** Same as Pause/Resume in visible MCP or command-line automation. Pausing
   * finishes the current native edit. Once Resume appears, manual editing is
   * available. Manual document edits split script Undo groups. Resume uses the
   * changed workspace; missing or incompatible targets raise an error. Refresh
   * cached geometry when needed. Browsing, view navigation and Stop remain usable.
   * Resets on completion/cancellation and does not advance simulated paint time
   * while parked. Hidden runs and interactive scripts reject true.
   * A paused MCP request stays busy; resume with the window's button.
   */
  paused: boolean;
}

interface PatchyIo {
  /** Throws when the file cannot be read or is larger than 256 MB. */
  readTextFile(path: string): string;
  /** Throws when the file cannot be written. */
  writeTextFile(path: string, text: string): void;
  /**
   * Names (not full paths) of the files in dir matching pattern ("*.png";
   * default all files), sorted. Throws when the folder does not exist.
   */
  listFiles(dir: string, pattern?: string): string[];
  /** True when path is an existing file (folders do not count). Never throws. */
  fileExists(path: string): boolean;
  /** Size in bytes, or -1 when path is not an existing file. Never throws. */
  fileSize(path: string): number;
  /** Creates the folder and any missing parents; true when it exists afterwards. */
  makeDir(path: string): boolean;
  /** Removes one file (never a folder); true when it was removed. */
  deleteFile(path: string): boolean;
}

interface PatchyNamespace {
  /** Set the structured result returned by MCP (null by default). Supports timer callbacks.
   * JSON serializable values only, up to 4 Mi characters. Does not end the run. */
  setResult(value: unknown): void;
  readonly app: PatchyApp;
  readonly io: PatchyIo;
  readonly ui: PatchyUi;
  readonly brushes: PatchyBrushes;
  readonly apiVersion: number;
  readonly version: string;
  /**
   * CLI parameters: each `--script-arg key=value` becomes args.key = "value"
   * (all values are strings; an empty object otherwise).
   */
  readonly args: Record<string, string>;
  /**
   * True in the script the user ran, false while an include()d file's
   * top-level code executes - so one file can both define functions as a
   * library and do something useful when run directly:
   *   if (patchy.isMainScript()) { ... }
   */
  isMainScript(): boolean;
}

declare const app: PatchyApp;
declare const patchy: PatchyNamespace;

declare function setTimeout(callback: (dt: number) => void, ms: number): number;
declare function setInterval(callback: (dt: number) => void, ms: number): number;
declare function clearTimeout(id: number): void;
declare function clearInterval(id: number): void;
declare function requestAnimationFrame(callback: (dt: number) => void): number;
/**
 * Evaluates another script file in the running script's global scope. Relative
 * paths are searched in order: next to the running script, then the user
 * scripts folder, then the bundled scripts folder - so
 * include("Effects/fancy-background.js") works from anywhere. A user copy
 * saved over a bundled script (the Script Manager's Save) is used in its place.
 * The including script's global OPTIONS is preserved across the include: an
 * included file's own top-level OPTIONS block never replaces it.
 */
declare function include(path: string): void;

declare const console: {
  log(...values: unknown[]): void;
  info(...values: unknown[]): void;
  warn(...values: unknown[]): void;
  error(...values: unknown[]): void;
};

/** Document pixels, finite and between -100000 and 100000; controls are absolute.
 * Missing handles coincide with the anchor. Snapshots are detached, never live proxies.
 */
interface PatchyVectorAnchor {
  x: number; y: number; inX?: number; inY?: number; outX?: number; outY?: number; smooth?: boolean;
}
interface PatchyVectorPath {
  /** At most 4096 subpaths, 100000 anchors total, at least two anchors per subpath.
   * Same-group subpaths use even-odd fill; operations combine groups in native order.
   * group defaults to the subpath index, operation to unite, closed to true.
   */
  subpaths: {anchors: PatchyVectorAnchor[]; closed?: boolean;
    operation?: "unite" | "subtract" | "intersect" | "exclude"; group?: number}[];
}
type PatchyVectorMatrix = [number, number, number, number, number, number];
type PatchyVectorGeometry =
  | {type: "rectangle" | "roundedRectangle"; x: number; y: number; width: number; height: number;
      radius?: number; /** TL, TR, BR, BL */ radii?: [number, number, number, number]}
  | {type: "ellipse"; x: number; y: number; width: number; height: number}
  | {type: "line"; x1: number; y1: number; x2: number; y2: number; weight?: number;
      arrowStart?: boolean; arrowEnd?: boolean; arrowWidth?: number; arrowLength?: number}
  | {type: "polygon"; cx: number; cy: number; radius: number; sides?: number;
      /** 0..99 percent, default 0 */ starInset?: number; /** degrees, default -90 */ angle?: number}
  | {type: "custom"; resourceId: string; x: number; y: number; width: number; height: number}
  | {type: "path"; path: PatchyVectorPath};
/** RGB colors use #rrggbb or named colors. Solid alpha belongs to layer opacity/stroke opacity.
 * A color string abbreviates solid paint; "none" disables paint. Numeric scales use 1 = 100%.
 */
type PatchyVectorPaint = string
  | {type: "none"}
  | {type: "solid"; color: string}
  | {type: "gradient"; gradient: PatchyVectorGradient}
  | {type: "pattern"; source?: "library" | "document"; resourceId?: string;
      scale?: number; angle?: number; linked?: boolean; offsetX?: number; offsetY?: number};
interface PatchyVectorGradient {
  /** Applying a preset replaces only the definition, preserving placement.
   * presetId cannot be mixed with form/colorStops/alphaStops/noise in the same update.
   */
  presetId?: string;
  /** Resolve dynamic preset stops at apply time; defaults black/white. */
  foreground?: string; background?: string;
  name?: string; form?: "solid" | "noise";
  type?: "linear" | "radial" | "angle" | "reflected" | "diamond";
  /** position, midpoint, opacity: 0..1. Up to 256 stops in each list. */
  colorStops?: {position: number; color?: string; midpoint?: number; kind?: "color" | "foreground" | "background"}[];
  alphaStops?: {position: number; opacity: number; midpoint?: number}[];
  /** Native 0..4096 smoothness. */ smoothness?: number;
  angle?: number; scale?: number; reverse?: boolean; dither?: boolean;
  interpolation?: "classic" | "perceptual" | "linear";
  alignWithLayer?: boolean;
  /** Native percentage offsets. */ offsetX?: number; offsetY?: number;
  noise?: {seed?: number; roughness?: number; transparency?: boolean; restrictColors?: boolean;
    colorModel?: "rgb" | "hsb" | "lab"; minimum?: [number, number, number, number]; maximum?: [number, number, number, number]};
}
interface PatchyVectorStroke {
  enabled?: boolean; fillEnabled?: boolean; width?: number; paint?: PatchyVectorPaint;
  alignment?: "inside" | "center" | "outside";
  cap?: "butt" | "round" | "square"; join?: "miter" | "round" | "bevel";
  /** Dash values and offset are stroke-width multiples; at most 64 positive values. */
  dashes?: number[]; dashOffset?: number; miterLimit?: number;
  /** 0..1 */ opacity?: number; blendMode?: string; scaleLock?: boolean; adjust?: boolean;
}
interface PatchyShapeState {
  editable: boolean; lockReason: string;
  /** Geometry/appearance may be absent for imported, unparsed vector content. */
  path?: PatchyVectorPath;
  liveShapes?: {group: number; geometry: PatchyVectorGeometry}[];
  fill?: PatchyVectorPaint; stroke?: PatchyVectorStroke;
  pathDisabled?: boolean; pathInverted?: boolean; isFillLayer?: boolean;
}
interface PatchyVectorMask {
  path: PatchyVectorPath; enabled: boolean; inverted: boolean; linked: boolean;
  /** 0..100, default 100. */ density: number;
  /** Document pixels, 0..1000, default 0. */ feather: number;
}
interface PatchyDocumentPath {
  readonly id: string;
  /** Rename saved paths. To name the work path, call save(name). */ name: string;
  readonly kind: "saved" | "work";
  getPath(): PatchyVectorPath;
  setPath(data: PatchyVectorPath): void;
  transform(matrix: PatchyVectorMatrix): void;
  /** Creates a saved copy with a fresh ID; omitted name keeps the source name. */
  duplicate(name?: string): PatchyDocumentPath;
  remove(): void;
  /** Zero-based index among saved paths. */ moveTo(index: number): void;
  activate(): void;
  /** Converts the work path to a saved path at the end, retaining its ID; otherwise renames. */
  save(name: string): void;
}
