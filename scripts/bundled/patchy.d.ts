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
/** Image Size resampling methods (`doc.resizeImage`): "automatic" picks Bicubic Sharper for a
 *  reduction and Bicubic Smoother for an enlargement. */
type PatchyResampleMethod = "automatic" | "nearest" | "bilinear" | "bicubic" | "bicubicSmoother" | "bicubicSharper";
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

/** One run of formatted text: the layer options apply unless a field overrides them. */
interface PatchyTextRun {
  text: string;
  font?: string; size?: number; bold?: boolean; italic?: boolean; color?: string;
}

/** A stored run read back from a text layer (see PatchyLayer.textRuns). */
interface PatchyTextRunInfo {
  text: string; font: string; style: string; size: number; bold: boolean; italic: boolean; color: string;
}

/** What a smart-object layer holds (see PatchyLayer.getSmartObject). */
interface PatchySmartObjectState {
  /** true: the layer references a file on disk (Place Linked); false: the contents are stored in the document. */
  linked: boolean;
  /** The source file's name, e.g. "logo.svg". */
  fileName: string;
  /** Linked: the file the link resolves to right now ("/" separators), or the stored absolute path when it is missing. "" for embedded contents. */
  path: string;
  /** Linked: the stored path relative to the document's folder (what makes the PSD portable). "" for embedded contents. */
  relativePath: string;
  /** Linked: the file could not be found. */
  missing: boolean;
  /** Linked: the file on disk differs from the copy the layer was rendered from; updateSmartObject() refreshes it. */
  changed: boolean;
  /** Identity of the shared source element. Layers with the same sourceId show the same contents, and one update refreshes all of them. */
  sourceId: string;
  /** The contents' own size in pixels and their pixels per inch. */
  width: number;
  height: number;
  resolution: number;
  /** The placement quad in document pixels: top-left, top-right, bottom-right, bottom-left as [x0, y0, x1, y1, x2, y2, x3, y3]. */
  quad: number[];
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
  /** Content offset in document pixels; setting either moves the layer like `moveTo`. */
  x: number;
  y: number;
  readonly bounds: PatchyRect;
  readonly isGroup: boolean;
  readonly isText: boolean;
  readonly isShape: boolean;
  readonly isSmartObject: boolean;
  /** The smart object's state, or null for other layers. */
  getSmartObject(): PatchySmartObjectState | null;
  /**
   * Update Smart Object Content: re-reads this linked layer's file from disk and
   * re-renders every layer that shares its source, as one undo step. Returns the
   * number of layers re-rendered. Throws for an embedded smart object, a plain
   * layer, or a linked file that is missing or cannot be decoded.
   */
  updateSmartObject(): number;
  /**
   * Renders this embedded smart object again from the file it stores, for every layer
   * that shares its source, as one undo step. A smart object opened from a PSD shows
   * the pixels saved in the file until it is transformed or its contents change; this
   * replaces them with Patchy's own render. Returns the number of layers re-rendered.
   * Throws for a linked smart object (see updateSmartObject), a plain layer, one
   * Patchy keeps locked, or contents that cannot be decoded.
   */
  rerenderSmartObject(): number;
  getShape(): PatchyShapeState | null;
  /** Partial update. geometry and path are mutually exclusive; group targets one existing shape group. */
  updateShape(changes: {geometry?: PatchyVectorGeometry; group?: number; path?: PatchyVectorPath;
    fill?: PatchyVectorPaint; stroke?: PatchyVectorStroke; pathDisabled?: boolean; pathInverted?: boolean;
    /** Photoshop's vector-mask Feather (px, 0..1000) and Density (0..100) on the shape's own path. */
    feather?: number; density?: number}): void;
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
  /** Text layers: setting text re-renders the layer with the first character's formatting
   * (size, glyph scales, leading, tracking); an empty string clears its ink. Like every
   * text setter here, it logs a console warning (never a dialog) when a font is not
   * installed or has no glyphs for the text; a missing font is replaced by the
   * substitute it was drawn in, which textFont then reports. Compact family spellings
   * such as LiberationSans resolve to Liberation Sans without a substitution warning. */
  text: string;
  /** Text layers: "horizontal" or "vertical" (columns top to bottom, right to left). Setting it re-renders. */
  textOrientation: 'horizontal' | 'vertical';
  /** Text layers: paragraph base direction, "auto" (first strong character), "ltr" or "rtl". Setting it re-renders. */
  textDirection: 'auto' | 'ltr' | 'rtl';
  /** Text layers: the font family name the layer uses; "" for other layers. */
  readonly textFont: string;
  /**
   * Text layers: the formatted runs in text order, each {text, font, style, size,
   * bold, italic, color} (style is a recorded face beyond bold/italic such as
   * "Black", size in document px before any layer transform). Concatenated
   * texts equal `text`. Empty for other layers.
   */
  readonly textRuns: PatchyTextRunInfo[];
  /** Text layers: the paragraph box {width, height}, or null for point text. */
  readonly textBox: { width: number; height: number } | null;
  /** Detached boundary in document pixels. One closed contour; null converts area text to box text. */
  textArea: PatchyVectorPath | null;
  /** Text layers: the first paragraph's alignment; setting it aligns every paragraph and re-renders. */
  textAlign: 'left' | 'center' | 'right' | 'justify';
  /**
   * Text layers: the first paragraph's indents and spacing in DOCUMENT PIXELS
   * (Photoshop's Paragraph panel: first line indent, left indent, right indent,
   * space before, space after). Setting it merges the given fields into every
   * paragraph and re-renders; a field left out keeps its value. A negative
   * firstLineIndent with a positive startIndent is a hanging indent. null for
   * other layers.
   */
  textParagraph: { firstLineIndent: number; startIndent: number; endIndent: number; spaceBefore: number; spaceAfter: number } | null;
  /**
   * Text layers: replaces the content with formatted runs the way retyping
   * does. Every run starts from the first character's current formatting and
   * applies its own font, size, bold, italic and color on top, so
   * setTextRuns([{text: "Ask "}, {text: "Seth", bold: true}]) keeps the
   * layer's face and size and bolds one word.
   */
  setTextRuns(runs: (PatchyTextRun | string)[]): void;
  /**
   * Text layers: renders the layer again from its stored text, fonts and formatting,
   * changing none of them. A type layer opened from a PSD shows the pixels saved in the
   * file until it is edited; this replaces them with Patchy's own render. Logs the same
   * font warnings as the text setters. Throws on a layer that is not text or is locked.
   */
  rerenderText(): void;

  /**
   * Finite signed 32-bit positions; throws if the position or resulting bounds overflow.
   * Layers sit on whole pixels: a fraction rounds like Photoshop (halves up, 3.5 -> 4,
   * -3.5 -> -3), the same rule `x`/`y` assignment uses.
   * The move is the Move tool's: the layer's placement travels with its pixels (a text
   * layer's anchor, a shape's path and geometry, a smart object's quad, a linked raster
   * or vector mask), so later edits, re-renders and saved PSDs keep the new position. An
   * unlinked mask stays where it is. On a group, every layer inside moves; a group has
   * no position of its own (`x`/`y` read 0), so `moveTo(dx, dy)` offsets its contents.
   */
  moveTo(x: number, y: number): void;
  /**
   * Inserts the copy directly above this layer and returns it. With another
   * open document as `targetDocument`, the copy lands above that document's
   * active layer instead (same coordinates, or centered when the sizes differ),
   * keeps its name unless the target already uses it, and the returned layer
   * belongs to the target; the target is not activated.
   */
  duplicate(targetDocument?: PatchyDocument): PatchyLayer;
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
   * Runs a legacy Photoshop filter plug-in (an id from patchy.plugins.list()) on this
   * 8 or 16-bit layer (16 bits go through an 8-bit copy; a 32-bit layer throws), limited
   * to the document selection, as one undoable edit. {dialog: false}
   * skips the plug-in's own settings dialog and reuses its last (or default) settings
   * (a plug-in that opens its dialog anyway gets its OK pressed); unattended runs never
   * show it. {captureDialog: "<png path>"} saves an image of the plug-in's dialog while
   * it is up (an unattended run then shows the dialog for the capture and answers it
   * itself). Windows only; unknown or unsupported ids throw.
   */
  applyPlugin(pluginId: string, options?: { dialog?: boolean; captureDialog?: string }): void;
  /**
   * Edit > Remove Object: fills the document selection from its surroundings
   * (the dialog's fill, without the dialog). `method` "contentAware"
   * (default) is the deterministic exemplar fill (an exhaustive best-patch
   * search over the nearby image, no AI); it falls back to "nearestEdge" when
   * no clean source patch is in reach. For it, `attempt` is the variation:
   * 0 (default) is the best-match fill and each N > 0 is a different,
   * reproducible near-best fill (the dialog's Reroll); `toneMatch` (0..100,
   * default 0) scales the tone match that follows the fill, 0 keeping the
   * raw fill; `feather` (px, default 0) softens the fill's edge outward from
   * the selection on top of the selection's own feather. "nearestEdge" is the
   * selection form of Spot Healing (a mirrored patch of the nearby texture
   * blended by the healing membrane); calling it again on the same selection
   * tries the next source candidate, and `attempt` picks one explicitly
   * (0-based, wrapping); `feather` applies, `toneMatch` does not. The layer
   * must be the document's active layer and a selection must exist. Returns
   * the method that ran, the number of patches copied (content-aware), the
   * 1-based source used plus the candidate count (nearest edge), and the
   * 0-based `attempt` that ran.
   */
  removeObject(options?: {
    method?: "contentAware" | "nearestEdge";
    attempt?: number;
    toneMatch?: number;
    feather?: number;
  }): { method: "contentAware" | "nearestEdge"; patches: number; source: number; sourceCount: number; attempt: number };
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

/**
 * Options for doc.saveAs(path, options) and doc.exportAs(path, options): the same choices
 * the format's Save Options dialog offers, with no dialog. Which keys apply depends on
 * the path's extension; a key for another format, an unknown key, a wrong type or an
 * out-of-range value throws, so a typo never saves with defaults. Keys you leave out
 * follow your Save Options defaults (and the document, such as a .dds source's format).
 * Tokens are the persisted spellings and never change.
 */
interface PatchySaveOptions {
  /** .jpg/.jpeg (0..100), .webp (0..100), .jxr (1..100), .rttex JPEG encoding (1..100). */
  quality?: number;
  /** .webp, .jxr: lossless compression; .pdf: Flate image data (an imageQuality preset wins). */
  lossless?: boolean;
  /** .dds: "auto" keeps an opened .dds file's format, otherwise BC1 when opaque and BC3
   *  with transparency. Everything but "bc7" writes a legacy header. */
  compression?: "auto" | "uncompressed" | "bc1" | "bc3" | "bc4" | "bc5" | "bc7";
  /** .dds: "on" writes the full chain, "off" level 0 only, "auto" follows an opened .dds
   *  file and generates a chain for every other document. */
  mipmaps?: "auto" | "on" | "off";
  /** .ico/.cur: the icon sizes to write, each one of 16, 24, 32, 48, 64, 128, 256. */
  sizes?: number[];
  /** .ico/.cur: how generated sizes resample ("auto": nearest for palette or small documents). */
  resample?: "auto" | "nearest" | "smooth";
  /** .cur: the cursor hotspot in pixels (0..255 each). */
  hotspot?: {x: number; y: number};
  /** .bmp pixel encoding, or .rttex pixel encoding ("rgba8", "rgba4444", "jpeg"). */
  encoding?: "rgba32" | "rgb24" | "indexed8" | "indexed4" | "indexed2" | "rgba8" | "rgba4444" | "jpeg";
  /** .bmp indexed encodings: "exact" uses the document palette, "quantize" builds one,
   *  "paletteFile" reads palettePath. */
  paletteMode?: "exact" | "quantize" | "paletteFile";
  /** .bmp with paletteMode "paletteFile": the palette file. */
  palettePath?: string;
  /** .rttex: "pad" or "stretch" to a power of two, or "none". */
  powerOfTwo?: "pad" | "stretch" | "none";
  /** .rttex flags. */
  forceSquare?: boolean;
  forceAlpha?: boolean;
  compress?: boolean;
  /** .pdf: the same keys as app.exportPdf. */
  imageQuality?: "lossless" | "high" | "medium" | "low";
  editableLayers?: boolean;
  keepOriginalImageData?: boolean;
  missingFontsAsImages?: boolean;
  /** .gif: write the visible top-level layers as an animation (top layer = frame 1)
   *  instead of one flattened image; frameDelayMs is the default per-frame delay. */
  animate?: boolean;
  frameDelayMs?: number;
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
  /** Snapshot in export order, including attached palettes when mode is off; null if absent.
   * PSD save/reopen preserves colors, names and palette-mode settings. */
  getPalette(): {colors: string[]; names: string[]; enabled: boolean; alphaThreshold: number | null; sourceBitDepth: number} | null;
  /** 1..256 opaque color strings. Metadata only: existing layer pixels are preserved.
   * enabled defaults true, alphaThreshold defaults 128 (integer 0..255).
   * names must parallel colors: single lines, at most 4096 UTF-8 bytes each; omitted names clears labels.
   * Enabled mode constrains tool writes, display and PNG export. Unknown options throw.
   * enabled:false attaches the colors without the constraint. Undoable; preserves duplicates/order.
   * saveAs("art.psd") embeds the attached palette and names, including when mode is off. */
  setPalette(colors: string[], options?: {enabled?: boolean; alphaThreshold?: number; names?: string[]}): void;
  /** Loads native palette formats, then applies setPalette with the same options.
   * Preserves GPL color names unless options.names overrides them. Transparency indexes are not imported.
   * A subsequent PSD save embeds this palette; reopening in Patchy needs no companion palette file. */
  loadPalette(path: string, options?: {enabled?: boolean; alphaThreshold?: number; names?: string[]}): NonNullable<ReturnType<PatchyDocument["getPalette"]>>;
  /** Native .pal/.gpl/.hex/.act/.aco writer; GPL preserves color names.
   * Throws on failure; no history/path/modified change. */
  savePalette(path: string, name?: string): boolean;
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
  /** Bits per channel: 8, 16 or 32 (32 is floating point, linear light). */
  readonly bitDepth: 8 | 16 | 32;
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
   * Adds a text layer rendered through Patchy's text engine. text is a string
   * or an array of runs ({text, font?, size?, bold?, italic?, color?}): each
   * run is typed in its own format on top of the layer options, so one layer
   * can mix faces, sizes and colors ("Hold the " + bold "LEFT TRIGGER").
   * Options: {font, size, x, y, color, bold, italic, orientation, direction,
   * box, align}; x/y is the text anchor point (for vertical text: the first
   * column's top centre). box: {width, height} (each at least 16 document px)
   * opens a paragraph text box with x/y as its top-left corner: lines wrap at
   * the box width, exactly like dragging a box with the Type tool. align
   * ("left", "center", "right", "justify") sets every paragraph's alignment;
   * paragraph ({firstLineIndent, startIndent, endIndent, spaceBefore,
   * spaceAfter}, document pixels, each optional) sets every paragraph's
   * indents and spacing, like layer.textParagraph.
   * size is the text height in DOCUMENT PIXELS, independent of the canvas
   * zoom and the document PPI (the Character panel shows the pt equivalent).
   * orientation "vertical" stacks upright glyphs in columns that advance right
   * to left (Photoshop's Vertical Type); direction sets the paragraph base
   * direction ("auto" follows the first strong character). font is a family
   * name ("Georgia"), family plus face ("Arial Black"), or on Windows a face's
   * full or PostScript name ("Futura Extra Black BT"); a font that is not
   * installed, or that has no glyph for any character of the text (the bundled
   * Noto Naskh Arabic has no Latin letters), renders in a fallback and logs a
   * console warning that says which of the two it was. The face is
   * exactly what font/bold/italic name, never the options bar's current one.
   * text (and any run's text) may contain "\n": every line lands in the SAME
   * layer as a new paragraph, so a heading and its subline need no second
   * layer.
   */
  addTextLayer(text: string | PatchyTextRun[], options?: {
    font?: string; size?: number; x?: number; y?: number;
    color?: string; bold?: boolean; italic?: boolean;
    orientation?: 'horizontal' | 'vertical';
    direction?: 'auto' | 'ltr' | 'rtl';
    box?: { width: number; height: number };
    /** Copy a closed contour in document coordinates. Mutually exclusive with box; x/y are ignored. */
    area?: PatchyVectorPath;
    align?: 'left' | 'center' | 'right' | 'justify';
    paragraph?: { firstLineIndent?: number; startIndent?: number; endIndent?: number; spaceBefore?: number; spaceAfter?: number };
  }): PatchyLayer;
  /**
   * Files as Layers: adds each image file as a new layer directly above the
   * active layer, bottom to top in argument order (the last file ends on top
   * and active). A file with several layers (a PSD, an animated GIF) becomes
   * a folder named after it. Pixels keep their size: a file the size of the
   * document lands exactly, any other size is centered on the canvas.
   * Throws, adding nothing, when a file cannot be read. Returns the new
   * top-level layers in argument order.
   */
  importFilesAsLayers(paths: string | string[]): PatchyLayer[];
  /**
   * Place Embedded / Place Linked: adds the file (PSD, PSB, PNG, JPEG, TIFF, BMP,
   * SVG, ...) as a smart-object layer on top and makes it active. Embedded
   * (default) stores a copy of the file in the document; `linked: true` stores a
   * reference to the file, so editing the file and calling updateSmartObject()
   * refreshes the layer, and placing the same file linked again shares that
   * reference (one update refreshes every layer placed from it). Without a
   * position or size the file lands at its physical size (its pixels scaled by
   * the document's resolution over the file's), centered, and scaled down to fit
   * a smaller canvas, like the menu commands. `x`/`y` place the top-left corner
   * (an omitted axis centers); `width`/`height` set the placed size in document
   * pixels, one of them alone keeping the aspect ratio; `scale` multiplies the
   * physical size (1 = 100%) and is ignored when a size is given. SVG contents
   * render sharp at any size. `name` overrides the layer name (default: the
   * file's base name). Throws, adding nothing, for a file that cannot be read or
   * decoded, a size outside 1..30000 pixels, or an unknown option. The linked
   * file's path is stored relative to the document's folder when the document is
   * saved as PSD/PSB; until then an absolute path keeps the link working.
   */
  addSmartObject(path: string, options?: {
    linked?: boolean; x?: number; y?: number; width?: number; height?: number; scale?: number; name?: string;
  }): PatchyLayer;
  /** First layer (depth-first) with this exact name, or undefined. */
  findLayer(name: string): PatchyLayer | undefined;
  /**
   * Combine Shapes: merges the shape layers (siblings of one folder) into the
   * bottom-most one and returns it; the others are removed. op: "unite",
   * "subtract" (front shapes cut from the base), "intersect", or "exclude".
   */
  combineShapes(layers: PatchyLayer[], op: "unite" | "subtract" | "intersect" | "exclude"): PatchyLayer;
  /**
   * Merges exactly the supplied layers and selected groups' contents. Returns
   * surviving selected leaf layers in bottom-to-top paint order. keepVectors and separateVectorTypes
   * default true; withinGroups defaults false. Vector parts preserve their individual appearances.
   * keepVectors=false explicitly rasterizes merges; separateVectorTypes separates
   * solid, gradient, pattern and mixed-paint categories, irrespective of colors/stroke settings.
   * A single leaf is unchanged (no implicit layer below, unlike Merge Down).
   * singleVector=true overrides the three options above: at least two editable vector
   * layers become one at the bottommost source's stack position, including across unselected layers.
   * Removes individual layer effects unless effectsFrom names a vector included in the merge;
   * that layer's effects apply once to the combined silhouette. Masks, clipping, locks,
   * unsupported data and incompatible blending/group boundaries throw before mutation.
   */
  mergeLayers(layers: PatchyLayer[], options?: {
    keepVectors?: boolean; withinGroups?: boolean; separateVectorTypes?: boolean;
    singleVector?: boolean; effectsFrom?: PatchyLayer;
  }): PatchyLayer[];
  /** Layer > Arrange > Align: lines the layers' edges or centers up with the reference
   *  (the selection when one exists and alignTo is "selection", the canvas when alignTo is
   *  "canvas" or only one layer is given, else the layers' union). A group counts as one
   *  unit. Defaults: the layer selection, alignTo "selection". Returns the layers moved;
   *  rides the run's single undo entry. */
  alignLayers(edge: "left" | "hcenter" | "right" | "top" | "vcenter" | "bottom",
    options?: {layers?: PatchyLayer[]; alignTo?: "selection" | "canvas"}): number;
  /** Layer > Arrange > Distribute over three or more units: feature modes keep the outermost
   *  units and space the others evenly; "hspacing" / "vspacing" share one equal gap. Throws
   *  with fewer than three movable units. Returns the layers moved. */
  distributeLayers(mode: "left" | "hcenter" | "right" | "top" | "vcenter" | "bottom" | "hspacing" | "vspacing",
    options?: {layers?: PatchyLayer[]}): number;
  flatten(): void;
  /** Image > Image Size. `options.method` is the resampling method (default "automatic":
   *  Bicubic Sharper for a reduction, Bicubic Smoother for an enlargement); an unknown id
   *  throws. Text layers and smart objects re-render from their sources afterwards. */
  resizeImage(width: number, height: number, options?: {method?: PatchyResampleMethod}): void;
  resizeCanvas(width: number, height: number): void;
  /** Image > Mode > 8/16/32 Bits/Channel: converts every layer, mask and channel.
   *  16 and 32 need deep editing to be on; Indexed documents stay 8-bit. */
  convertBitDepth(bits: 8 | 16 | 32): void;
  /** Crops to the canvas intersection; throws if the rectangle is outside the canvas. */
  crop(x: number, y: number, width: number, height: number): void;
  /** Saves to the path; the format follows the extension (.psd, .png, ...). The document
   * then points at that file (a layered document saved to a flat format keeps its own
   * path and stays modified, Photoshop's save-a-copy rule). `options` sets the format's
   * save options explicitly, with no dialog (see PatchySaveOptions); without it the save
   * uses your Save Options defaults. A scripted save never changes those defaults.
   * PSD embeds attached palette colors/names/settings as optional Patchy metadata and retains normal RGB
   * layers, not Photoshop's native named swatches. All PSD output must open without Photoshop warnings/errors.
   * Check the result; a successful save alone does not verify Photoshop compatibility. */
  saveAs(path: string, options?: PatchySaveOptions): boolean;
  /** Writes a copy to the path with the same formats and options as saveAs, and leaves the
   * document alone: its path, title and modified state never change, whatever the format. */
  exportAs(path: string, options?: PatchySaveOptions): boolean;
  /** Exports visible top-level layers, top first, as animated WebP. Groups become one frame.
   * Trailing seconds tokens ("blink 0.033s") override frameDelayMs. Preserves the document path
   * and modified state. loopCount is total plays (0 = forever), defaults to the imported or
   * last exported count, otherwise 0. Other defaults: frameDelayMs 100, quality 75, lossless false.
   * quality 100 also selects lossless. Integers: frameDelayMs 0..16777215, loopCount 0..65535,
   * quality 0..100. Unknown/invalid options, non-.webp paths and export failures throw.
   * saveAs/exportAs with .webp continue to write a single flattened image. */
  exportAnimatedWebp(path: string, options?: {
    frameDelayMs?: number; loopCount?: number; quality?: number; lossless?: boolean;
  }): boolean;
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
  /** Opens a file; throws on failure. RAW opens read the photo's .rawprefs sidecar or use defaults, without writing settings. Unattended PDF opens use default import settings. */
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
  /**
   * Every font family the text engine can use right now (installed and user-added),
   * with its face names and the writing systems the font declares (a declared
   * system can still lack letters: addTextLayer warns when a font cannot draw
   * the text), sorted by family. Pass
   * a family (or family plus face) as addTextLayer's font. Under --headless on
   * Windows this also loads the installed fonts first.
   */
  listFonts(): {family: string; styles: string[]; writingSystems: string[]}[];
  /**
   * Writes one PDF with a page per document, in array order, each page sized
   * from that document's pixels and resolution. A single document is accepted
   * in place of the array. options.lossless (default true) keeps images
   * lossless, false writes JPEG; options.imageQuality picks a preset instead
   * ("lossless", "high", "medium", "low") and wins over lossless. Gray pages
   * are written as one channel. options.keepOriginalImageData (default true)
   * writes a page that was imported from a PDF and has not visibly changed
   * with the image data it came with. options.editableLayers (default false) keeps
   * shapes and text as PDF objects the way Save As PDF's "Keep Layers
   * Editable" does; options.missingFontsAsImages embeds text in uninstalled
   * fonts as pixels. Returns true on success and throws on bad arguments or a
   * write failure.
   */
  exportPdf(
    documents: PatchyDocument | PatchyDocument[],
    path: string,
    options?: {
      lossless?: boolean;
      imageQuality?: "lossless" | "high" | "medium" | "low";
      keepOriginalImageData?: boolean;
      editableLayers?: boolean;
      missingFontsAsImages?: boolean;
    },
  ): boolean;
}

/** One field of a patchy.ui.showDialog / showOptions form. */
interface PatchyDialogField {
  /** Property name of this field's value in the returned object. */
  key: string;
  /** Row label; defaults to key. */
  label?: string;
  /** folder/file rows are a path line edit plus a Browse button. color rows open Patchy's
   * color picker (palettes, names, hex) and keep any alpha the value came with. */
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
   * Waits up to 60 seconds for enabled Dynamic Vector Preview to settle. Returns false
   * on timeout or write failure; throws on an empty path.
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
  /**
   * Replaces the file whole (a sibling temporary and a rename), so a failed write
   * keeps the previous file. Throws when the file cannot be written.
   */
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

/**
 * Automatic document recovery: Patchy writes a PSB copy of every modified document
 * to a per-instance recovery folder on a timer (Preferences > Application) and, after
 * a crash, reopens the copies on the next launch as "(Recovered)" documents. The web
 * build has no recovery store: enabled is false and every list is empty.
 */
/** One script of the Scripts menu (bundled or yours). */
interface PatchyScriptEntry {
  /** The @name display name (the file base name without one). */
  name: string;
  fileName: string;
  /** "/"-separated path below its root ("Utilities/quick-export-dds.js"); the key of
   *  setHotkey/getHotkey and the basis of commandId. */
  relativePath: string;
  /** The file that runs: your copy when it shadows a bundled script. */
  path: string;
  bundled: boolean;
  /** A bundled script shadowed by your edited copy. */
  modified: boolean;
  description: string;
  author: string;
  /** The shortcut in effect (Qt portable spelling such as "Ctrl+Alt+D"), "" when none. */
  hotkey: string;
  /** The script's own "// @hotkey" line, "" when absent. */
  defaultHotkey: string;
  /** The hotkey command id ("script." + the percent-encoded relativePath). */
  commandId: string;
}

/** patchy.scripts: the script library, so a script (or an agent through MCP) can write a
 *  script into the user folder and put it on a key in one run. */
interface PatchyScripts {
  /** The user scripts folder ("/" separators). Files below it appear in File > Scripts. */
  readonly userFolder: string;
  readonly bundledFolder: string;
  /** Every script of the merged scan, bundled first. Rescans the folders and refreshes
   *  the menu and hotkey commands, like rescan(). */
  list(): PatchyScriptEntry[];
  rescan(): PatchyScriptEntry[];
  /** Writes source to userFolder/relativePath (a .js path below the folder, folders
   *  created as needed), rescans, binds options.hotkey when given (a Preferences override,
   *  which wins over a "// @hotkey" line in the source), and returns the entry. Throws
   *  for a path outside the folder, a write failure, or a shortcut Patchy cannot bind. */
  install(relativePath: string, source: string, options?: {hotkey?: string}): PatchyScriptEntry;
  /** Sets the script's Preferences > Hotkeys override ("" clears it, restoring the
   *  "// @hotkey" default) and returns the shortcut in effect afterwards, which is "" when
   *  another override already owns the key. Throws for an unknown script or a shortcut
   *  Patchy cannot bind. */
  setHotkey(relativePath: string, shortcut: string): string;
  getHotkey(relativePath: string): string;
}

interface PatchyPlugins {
  /**
   * The plug-ins folder next to the application ("/" separators), the place Plugins >
   * Open Plug-ins Folder shows; created with its README.txt when read. "" on macOS and
   * Linux, which cannot run these plug-ins.
   */
  readonly folder: string;
  /** The user-added plug-in folders (persisted, Preferences > Plug-ins). Setting it rescans. */
  folders: string[];
  /**
   * Every plug-in file the last scan saw, in menu order. `supported` is false for files
   * that cannot run (not a filter, wrong platform), with the reason.
   */
  list(): {
    id: string;
    name: string;
    category: string;
    path: string;
    supported: boolean;
    reason: string;
    architecture: string;
  }[];
  /** Rescans the automatic and user folders and returns list(). */
  rescan(): ReturnType<PatchyPlugins["list"]>;
}

interface PatchyRecovery {
  /** The Preferences checkbox (persisted). Setting it re-arms the timer. */
  enabled: boolean;
  /** The timer interval, one of 5, 10, 15, 30, or 60 (persisted); other values throw. */
  intervalMinutes: number;
  /** This instance's recovery folder ("/" separators). It exists once something was written. */
  readonly directory: string;
  /**
   * Writes a recovery copy of every modified document whose state changed since its
   * last copy and waits for the files. Returns the PSB paths written; empty when
   * nothing changed or the app was busy (a modal dialog, a canvas gesture).
   */
  writeNow(): string[];
  /** This instance's copies: file (PSB path), title, originalPath ("" when never saved), savedAt (Unix ms). */
  listFiles(): { file: string; title: string; originalPath: string; savedAt: number }[];
  /** Copies left by instances that no longer run, with the folder each lives in. */
  listOrphaned(): { directory: string; file: string; title: string; originalPath: string; savedAt: number }[];
  /** Reopens every orphaned copy as a modified "(Recovered)" document and returns them. */
  recoverAll(): PatchyDocument[];
  /** Deletes every orphaned folder; returns how many documents were dropped. */
  discardOrphaned(): number;
}

interface PatchyNamespace {
  /** Set the structured result returned by MCP (null by default). Supports timer callbacks.
   * JSON serializable values only, up to 4 Mi characters. Does not end the run. */
  setResult(value: unknown): void;
  readonly app: PatchyApp;
  readonly io: PatchyIo;
  readonly ui: PatchyUi;
  readonly recovery: PatchyRecovery;
  readonly plugins: PatchyPlugins;
  readonly scripts: PatchyScripts;
  readonly brushes: PatchyBrushes;
  readonly apiVersion: number;
  readonly version: string;
  /**
   * CLI parameters: each `--script-arg key=value` becomes args.key = "value"
   * (all values are strings; an empty object otherwise).
   */
  readonly args: Record<string, string>;
  // Per-script hotkeys: a "// @hotkey Ctrl+Alt+D" header line is a script's default
  // shortcut and Preferences > Hotkeys (category Scripts) overrides it. The binding is
  // stored under the command id "script." + the percent-encoded relative path
  // ("script.Utilities%2Fquick-export-dds.js"); app.commandIds() lists those ids, and
  // app.runCommand refuses them because only one script runs at a time (use include()).
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
  /** Vector-mask Feather (px) and Density (0..100) on the shape's own path. */
  feather?: number; density?: number;
  /** Independent paints in a merged vector layer; empty on ordinary shapes.
   * Read-only snapshots. Group numbers refer to path.subpaths[].group.
   * Whole-layer appearance updates change only the edited fields in every part. */
  parts?: {groups: number[]; fill: PatchyVectorPaint; stroke: PatchyVectorStroke;
    opacity: number; fillOpacity: number; pathDisabled: boolean; pathInverted: boolean; wholeCanvas: boolean}[];
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
