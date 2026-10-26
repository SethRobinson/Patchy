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
// "lighter-color", and the Paint.NET-compatibility modes "reflect", "glow", "negation"
// and "xor" (for .pdn files), and GIMP's "grain-extract", "grain-merge", "hsv-hue",
// "hsv-saturation", "hsl-color", "hsv-value", "lch-hue", "lch-chroma", "lch-color",
// "lch-lightness", "luminance", "luma-darken-only", "luma-lighten-only", "linear-multiply",
// "linear-addition", "linear-subtract" and "linear-divide" (for .xcf files), and Krita's
// "soft-light-svg", "additive-subtractive", "arc-tangent", "geometric-mean", "allanon",
// "parallel", "hard-overlay", "interpolation" and "easy-dodge" (for .kra files): 8 and 16-bit
// documents only, not offered in Patchy's blend menus, and a PSD save writes them as
// "normal" or bakes them (save option unsupportedBlendModes).
// Brush Mode ids (stroke `mode`): every blend mode id except "pass-through", plus
// "behind" (paints only into transparent pixels) and "clear" (removes alpha).

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
  /** Brush only, default "normal": how each dab combines with the pixels under it, a
   * Brush Mode id (see the header). Eraser and Mixer strokes reject any other value. */
  mode?: string;
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
  /** Integer 0..100, default 100: opacity caps a stroke, Flow meters each dab; 0 paints nothing. */
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

/** An artboard's background: "color" paints `color`. */
type PatchyArtboardBackground = "white" | "black" | "transparent" | "color";
/** The right column's panels, by permanent name (patchy.ui.expandPanel and friends). */
type PatchyPanelName = "layers" | "channels" | "paths" | "history" | "properties" | "info" | "palette" | "color";

/** An artboard's rect (document pixels) and background, as layer.artboard reads it. */
interface PatchyArtboard {
  x: number;
  y: number;
  width: number;
  height: number;
  background: PatchyArtboardBackground;
  /** "#rrggbb"; meaningful when background is "color" (white otherwise). */
  color: string;
}

/** Layers panel color labels, saved to PSD as Photoshop's own layer colors. */
type PatchyLayerColorLabel = "none" | "red" | "orange" | "yellow" | "green" | "blue" | "violet" | "gray";

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
  /** The Layers panel color label. Setting an unknown value throws; a label from a newer
   *  Photoshop that Patchy does not know reads as "none" and is kept on save. */
  colorLabel: PatchyLayerColorLabel;
  /** Content offset in document pixels; setting either moves the layer like `moveTo`.
   *  For an artboard, the artboard's top-left corner. */
  x: number;
  y: number;
  /** The layer's bounds; an artboard's rect for an artboard. */
  readonly bounds: PatchyRect;
  readonly isGroup: boolean;
  readonly isText: boolean;
  readonly isShape: boolean;
  readonly isSmartObject: boolean;
  /** True for an artboard: a top-level group with its own canvas rect and background. */
  readonly isArtboard: boolean;
  /** The artboard's rect and background, or null for every other layer. */
  readonly artboard: PatchyArtboard | null;
  /**
   * Changes an artboard's rect or background (absent keys keep their value; `color`
   * alone means background "color"), or makes a top-level group an artboard sized to
   * its content unless the rect is given. null turns an artboard back into a plain
   * group. The canvas grows to hold every artboard; growing up or left moves every
   * document coordinate by the same amount. Throws for a layer that cannot be one.
   */
  setArtboard(options: Partial<PatchyArtboard> | null): void;
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
   * Edit > Free Transform in one call (GitHub issue 225), with the interactive tool's
   * rules for every layer type, as one undo step with the rest of the run. Give exactly
   * one of: `matrix`, applied to the layer's transform box ([a, b, c, d, e, f] maps
   * x' = a x + c y + e, y' = b x + d y + f, like transformShape); or `quad`, the box's
   * new corners in document pixels, in the layer's own order (top left, top right,
   * bottom right, bottom left), so any four points that form a convex shape distort or
   * apply perspective (a crossed or concave quad throws). Smart
   * objects keep the result as a perspective placement; type layers take only a
   * parallelogram (rasterize first for perspective) and throw otherwise. A group
   * transforms its contents. `interpolation` defaults to the Free Transform setting.
   * With a selection on a pixel layer only the selected pixels transform: the box is the
   * selection bounds clipped to the layer's pixels, and the selection follows them.
   * Throws while a transform is pending in that document.
   */
  transform(options: {
    matrix?: PatchyVectorMatrix;
    quad?: [[number, number], [number, number], [number, number], [number, number]];
    interpolation?: PatchyResampleMethod;
  }): void;
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
  /**
   * Fills the selection (or the whole canvas on an empty layer); a soft selection's edge
   * blends the fill in by its coverage. RGB8 and RGBA8 are supported.
   */
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
   * applyFilter("patchy.filters.gaussian_blur", {radius: 8}). With a selection only
   * the selected pixels change, a soft edge blended by its coverage (the Filter menu's
   * rule). Unknown ids or parameters throw.
   */
  applyFilter(filterId: string, params?: Record<string, number | boolean | string>): void;
  /**
   * Filter > Camera Raw Filter on this RGB pixel layer at its own depth, e.g.
   * applyCameraRawFilter({exposure: 0.5, contrast: 20, vibrance: 30}). Temperature and tint
   * are relative (0 keeps the layer's colors), -100..100; exposure is -5..5 EV; the rest are
   * -100..100. Missing settings stay neutral and out-of-range numbers clamp. With a selection
   * only the selected pixels change, soft edges blended by coverage. Unknown names throw.
   */
  applyCameraRawFilter(settings?: {
    temperature?: number;
    tint?: number;
    exposure?: number;
    contrast?: number;
    highlights?: number;
    shadows?: number;
    saturation?: number;
    vibrance?: number;
  }): void;
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
   * Runs a G'MIC library filter (an id from patchy.gmic.list()) on this layer, limited
   * to the document selection, as part of the run's undo entry. `values` is an array
   * (one per parameter, in list() order) or an object keyed by parameter name; anything
   * left out uses the filter's default. A value has the kind list() reports as the
   * default: a number, a boolean, a choice's index or label, "#rrggbb" / "#rrggbbaa" or
   * [r, g, b, a] for a color, [x, y] in percent for a point, a string for text.
   * `inputLayers` names the layers the filter works on, counted from this layer
   * ("active", "activeAndBelow", "activeAndAbove", "all", "visible", "hidden"); the
   * default is the filter's own. "visible" and "hidden" follow what the document shows,
   * enclosing folders included. Every chosen layer the filter may change is changed, as
   * one undo step: a filter made for one image runs on each chosen layer alone, at its
   * own size, as if applied to each in turn, and any further image it returns becomes a
   * new layer below that layer (G'MIC lists images top first; under a layer named
   * Background they go above it instead). A filter made for several layers (its list() entry's
   * `inputLayers` is not "active") gets them lined up on the document; if it combines
   * them, the result replaces this layer and the others stay as they are. Locked, text,
   * shape and Smart Object layers among the chosen ones are read but never changed.
   * Returns the number of layers added.
   * Throws when the filter is unknown, a value does not fit, the filter fails, no layer
   * matches `inputLayers` or none of them may be changed, the run would not fit in memory,
   * this layer is a text, shape or Smart Object layer or has locked pixels, Quick Mask is
   * on, or the build has no G'MIC engine (patchy.gmic.available).
   */
  applyGmic(
    filterId: string,
    values?: PatchyGmicValue[] | { [parameterName: string]: PatchyGmicValue },
    options?: { inputLayers?: PatchyGmicInputLayers },
  ): number;
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
  /**
   * Layer > Layer Mask > Remove Background: runs Select Subject on the merged document and
   * sets this layer's mask to the subject (an existing mask is multiplied, keeping what it
   * already hides). Pixel, adjustment, or group layers. The selection is unchanged. Throws
   * when doc.selection.subjectSelectionAvailable is false.
   */
  removeBackground(options?: {sampleAllLayers?: boolean}): void;
  removeObject(options?: {
    method?: "contentAware" | "nearestEdge";
    attempt?: number;
    toneMatch?: number;
    feather?: number;
  }): { method: "contentAware" | "nearestEdge"; patches: number; source: number; sourceCount: number; attempt: number };
  /**
   * The Red Eye tool: {x, y} clicks there (document pixels), {x, y, width, height} drags a box.
   * The connected red area at or nearest the point is desaturated and darkened; a selection
   * limits the change. pupilSize and darkenAmount are 1..100 (default 50). The layer must be
   * the active layer. Returns the pupil's pixel count and the changed bounds; throws when no
   * red pixel is found.
   */
  redEye(options: {
    x: number;
    y: number;
    width?: number;
    height?: number;
    pupilSize?: number;
    darkenAmount?: number;
  }): { pupilPixels: number; x: number; y: number; width: number; height: number };
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
  /** Select > All: the canvas, or the active layer's artboard when it is on one. */
  selectAll(): void;
  deselect(): void;
  /** Sides are limited to 30000; larger values throw. */
  /** Clips to the canvas; a disjoint rectangle clears the selection. */
  selectRect(x: number, y: number, width: number, height: number): void;
  selectEllipse(x: number, y: number, width: number, height: number): void;
  /**
   * True when this build ships automatic selection (desktop builds with the bundled models,
   * and the web build, whose first objectSelect downloads the models). selectSubject and
   * objectSelect throw when it is false.
   */
  readonly automaticSelectionAvailable: boolean;
  /**
   * True when this build includes Select Subject's model. Current builds leave it out
   * (held back pending patent review), so this is false and selectSubject, selectBackground
   * and layer.removeBackground throw. objectSelect needs only automaticSelectionAvailable.
   */
  readonly subjectSelectionAvailable: boolean;
  /**
   * The model objectSelect runs: "standard" (bundled) or "highQuality" (desktop builds, once
   * downloaded with app.downloadObjectSelectionModel or from the tool, unless the Object
   * Selection tool's Model setting is Standard).
   */
  readonly objectSelectionModel: "standard" | "highQuality";
  /**
   * Select > Subject: selects the main subject with the bundled model, one pass over the
   * merged document on the CPU (several seconds). The soft matte combines by `operation`
   * (default "replace"); `feather` (px, default 0) and `antialias` (default true) shape it.
   * Throws when subjectSelectionAvailable is false.
   */
  selectSubject(options?: {operation?: "replace" | "add" | "subtract" | "intersect"; feather?: number;
    antialias?: boolean; sampleAllLayers?: boolean}): void;
  /** Select > Select Background: everything selectSubject would not select. Same options. */
  selectBackground(options?: {operation?: "replace" | "add" | "subtract" | "intersect"; feather?: number;
    antialias?: boolean; sampleAllLayers?: boolean}): void;
  /**
   * The Object Selection tool: selects the object inside `rect`, or the object under
   * `point` (document pixels). Exactly one of rect or point. `operation` is "replace"
   * (default), "add" or "subtract"; `sampleAllLayers` (default true) analyzes the merged
   * document instead of the active pixel layer. A rect that is small against the image is
   * analyzed with its surroundings at full resolution, like the tool.
   */
  objectSelect(options: {rect?: PatchyRect; point?: {x: number; y: number};
    operation?: "replace" | "add" | "subtract"; sampleAllLayers?: boolean; feather?: number;
    antialias?: boolean}): void;
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
  /** .jpg/.jpeg (0..100), .webp (0..100), .jxr (1..100), .jxl (1..100), .avif (0..100), .rttex JPEG encoding (1..100). */
  quality?: number;
  /** .webp, .jxr, .jxl, .avif: lossless compression; .pdf: Flate image data (an imageQuality preset wins). */
  lossless?: boolean;
  /** .gif, and .png with pngColors "palette": at most this many colors, 2..256 (default 256;
   *  a transparent entry counts toward it). */
  colors?: number;
  /** .gif and palette .png: "diffusion" mixes neighboring palette colors so gradients band less
   *  (default "none"). */
  dither?: "none" | "diffusion";
  /** .gif and palette .png: how strongly diffusion spreads, 0..100 (default 100). */
  ditherAmount?: number;
  /** .png: "palette" writes an indexed PNG (up to 256 colors, pixels under 50% alpha transparent);
   *  "full" (the default) writes full color. colors, dither and ditherAmount need "palette". */
  pngColors?: "full" | "palette";
  /** .jpg/.jpeg: progressive scans (a rough image shows first while a page loads). Default false;
   *  File > Export for Web turns it on. */
  progressive?: boolean;
  /** .jpg/.jpeg: optimized Huffman tables, a smaller file with the same image. Default false;
   *  File > Export for Web turns it on. */
  optimizeCoding?: boolean;
  /** .jpg/.jpeg: write a small preview thumbnail into the EXIF data when the file keeps EXIF
   *  (metadata "all" or "copyright" with EXIF to keep). Default true. */
  thumbnail?: boolean;
  /** .jxl: encoder effort, 1..9 (higher is smaller and slower). */
  effort?: number;
  /** .avif: encoder speed, 0..10 (lower is smaller and slower; 6 is the default). */
  speed?: number;
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
  /** .pdf: what a page is. "artboards" (the default) writes one page per artboard when
   *  the document has artboards, "groups" one page per top-level group (hidden ones too;
   *  a PDF imported as page folders goes back page by page at each page's size),
   *  "canvas" one page of the whole canvas. */
  pages?: "artboards" | "groups" | "canvas";
  /** Older spelling: true is pages "artboards", false is "canvas". */
  artboardPages?: boolean;
  /** .gif, .jxl: write the visible top-level layers as an animation (top layer = frame 1)
   *  instead of one flattened image; frameDelayMs is the default per-frame delay. */
  animate?: boolean;
  frameDelayMs?: number;
  /** .jxl animation: total plays, 0 (the default) loops forever, up to 65535. */
  loopCount?: number;
  /** .jpg/.jpeg, .png, .tif/.tiff, .webp, .gif, .jxl, .avif: the File Info metadata to embed. "all"
   *  (the default for saveAs and exportAs) writes everything; "copyright" keeps the author,
   *  copyright and creator contact fields; "none" writes no metadata. The color profile
   *  and resolution are always written. */
  metadata?: "all" | "copyright" | "none";
  /** .jpg/.jpeg, .tif/.tiff, .pdf (flattened pages only): "cmyk" writes the image in CMYK
   *  for print, separated through the working CMYK profile (app.cmykProfile) and with that
   *  profile embedded; transparent areas become paper white and 16/32-bit documents are
   *  written at 8 bits per ink. Without it the document's mode decides: "cmyk" for a
   *  document in CMYK Proof mode (doc.cmykProof), else "rgb". Throws when the user chose no
   *  CMYK profile. Patchy opens a CMYK file as RGB again. */
  colorMode?: "rgb" | "cmyk";
  /** With colorMode "cmyk": black, grays and black-ink colors print with black ink alone
   *  instead of four-ink rich black, so small text stays sharp. Colors still separate
   *  through the profile. Default false; throws without colorMode "cmyk". */
  blackInkOnly?: boolean;
  /** .tif/.tiff: write the layers as Photoshop does (a layered TIFF: the flattened image
   *  plus Photoshop's layer data, which Photoshop and Patchy open as layers). saveAs
   *  defaults to true when the document has layers a flat file would lose; exportAs and
   *  colorMode "cmyk" default to false. A TIFF with layers is RGB, so true with colorMode
   *  "cmyk" throws. */
  layers?: boolean;
  /** .psd, .psb, layered .tif/.tiff, .aseprite/.ase: layers whose blend mode the format
   *  cannot store (Paint.NET's Reflect, Glow, Negation and Xor everywhere; more in
   *  Aseprite). "normal" (the default) writes them as Normal, so the saved image can look
   *  different; "bake" rebuilds each as a Normal layer that keeps the look. The document
   *  itself is not changed. */
  unsupportedBlendModes?: "normal" | "bake";
}

/** File > File Info fields (doc.fileInfo, doc.setFileInfo). Strings are empty and arrays
 *  empty when absent. Read from XMP first, then legacy IPTC and EXIF. */
interface PatchyFileInfo {
  title: string;
  authors: string[];
  authorTitle: string;
  description: string;
  /** 0 = none, 1..5 stars, -1 = rejected. */
  rating: number;
  descriptionWriter: string;
  keywords: string[];
  copyrightStatus: "unknown" | "copyrighted" | "publicDomain";
  copyrightNotice: string;
  copyrightUrl: string;
  headline: string;
  dateCreated: string;
  sublocation: string;
  city: string;
  state: string;
  country: string;
  countryCode: string;
  credit: string;
  source: string;
  instructions: string;
  jobIdentifier: string;
  usageTerms: string;
  intellectualGenre: string;
  scene: string[];
  subjectCode: string[];
  contact: {address: string; city: string; region: string; postalCode: string; country: string;
    phone: string; email: string; url: string};
  /** Read only: XMP or EXIF dates and the creating application. */
  readonly created: string;
  readonly modified: string;
  readonly creatorTool: string;
  /** Read only: the format the document was opened from ("JPEG", "PSD", ...). */
  readonly sourceFormat: string;
}

/** One row of File Info's All Metadata list. */
interface PatchyMetadataEntry {
  /** Container: "EXIF IFD0", "EXIF GPS", "XMP dc", "IPTC-IIM", "PNG text", "ICC profile", ... */
  group: string;
  /** Specification identifier: "Make", "dc:title", "By-line". */
  name: string;
  value: string;
  source: string;
  privacy: "none" | "location" | "deviceIdentity" | "personal" | "hiddenImage" | "editHistory" |
    "machineInfo" | "timestamp" | "aiPrompt" | "provenance" | "watermark";
}

/** One Adobe TrustMark watermark `doc.scanForWatermarks()` read from the pixels. */
interface PatchyTrustMarkMark {
  /** The TrustMark model variant that reads it: "Q", "P" or "C". */
  variant: "Q" | "P" | "C";
  /** Its error-correction schema. */
  schema: "BCH_SUPER" | "BCH_5" | "BCH_4" | "BCH_3";
  /** The payload read as TrustMark text (7-bit ASCII), or "" when it is not text. */
  text: string;
  /** The payload as hexadecimal, "0x" first (the form addInvisibleWatermark's `id` takes). */
  id: string;
  /** The payload bits as a string of 0 and 1. */
  bits: string;
  /** Bits the error correction repaired. */
  correctedBits: number;
  /** The decoder's mean |logit|; 3 or more counts as a mark, marked images score about 9 to 11. */
  confidence: number;
}

/** One kind of watermark `doc.scanForWatermarks()` checked, in File Info's order. */
interface PatchyWatermarkCheck {
  /** Permanent id: "stableDiffusion1" | "stableDiffusion2" | "stableDiffusionXL" | "trustmarkQ" |
   *  "trustmarkP" | "trustmarkC" | "c2paDeclared" (a watermark the file's C2PA metadata declares). */
  id: string;
  /** Display name, for example "Adobe TrustMark Q". */
  name: string;
  status: "found" | "notFound" | "notChecked";
  /** Why a "notChecked" kind was skipped; "" otherwise. */
  reason: "" | "imageTooSmall" | "modelsNotDownloaded" | "notSupported" | "failed";
  /** File Info's text for the row: what was found, or why it was not checked (translated). */
  detail: string;
  /** Stable Diffusion rows, when found. */
  match?: PatchyWatermarkMatch;
  /** TrustMark rows, when found. */
  mark?: PatchyTrustMarkMark;
  /** The C2PA row: each declaration's summary. */
  declarations?: string[];
}

/** One known invisible watermark `doc.scanForWatermarks()` found in the pixels. */
interface PatchyWatermarkMatch {
  /** Permanent id: "stableDiffusion1" | "stableDiffusion2" | "stableDiffusionXL". */
  id: string;
  /** Display name, for example "Stable Diffusion XL". */
  name: string;
  /** Payload length in bits. */
  bits: number;
  /** Decoded bits that differ from the payload (at most one in twelve counts as found). */
  differingBits: number;
  /** True when it matched with the red and blue channels exchanged. */
  redBlueSwapped: boolean;
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
  /** The notes the file's reader left when it opened (what it dropped or approximated, the
   *  import notes the status bar shows), in the app's language; empty for new documents. */
  readonly importNotes: string[];
  /** Pixels per inch. */
  readonly resolution: number;
  /** File > File Info fields. Change them with setFileInfo. */
  readonly fileInfo: PatchyFileInfo;
  /** The XMP packet as text, empty when the document has none. */
  readonly xmp: string;
  /** Bits per channel: 8, 16 or 32 (32 is floating point, linear light). */
  readonly bitDepth: 8 | 16 | 32;
  /**
   * The name of the document's RGB color profile: the embedded profile's description, or
   * "sRGB IEC61966-2.1" when the document has none (Patchy treats untagged RGB as sRGB).
   */
  readonly colorProfile: string;
  /**
   * Image > Mode > CMYK Proof: the canvas shows the document through the working CMYK
   * profile, and saves to .jpg, .tif and flattened .pdf write CMYK unless the options give
   * colorMode. The pixels stay RGB. Not stored in files. Setting true throws in palette
   * (Indexed) mode or without a CMYK profile.
   */
  cmykProof: boolean;
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
  /** File > File Info: sets the given fields (any subset of fileInfo's writable keys, and
   *  `contact` partially). Unknown or read-only keys and wrong types throw. Changes reach
   *  the file on the next save; one undo step per script run. */
  setFileInfo(fields: Partial<Omit<PatchyFileInfo, "created" | "modified" | "creatorTool" | "sourceFormat" |
    "contact">> & {contact?: Partial<PatchyFileInfo["contact"]>}): void;
  /** Every metadata item the document holds (File Info's All Metadata list). */
  metadataEntries(): PatchyMetadataEntry[];
  /** File > Strip All Metadata. The color profile stays unless keepColorProfile is false;
   *  the resolution always stays. */
  stripMetadata(options?: {keepColorProfile?: boolean}): void;
  /** Removes GPS from EXIF and XMP. Returns false when there was none. */
  removeLocation(): boolean;
  /** Removes XMP edit history, document ancestors, the text-layer cache and Camera Raw
   *  settings. Returns false when there was none. */
  removeEditHistory(): boolean;
  /** File > File Info's Scan for Watermarks: checks the composite image for the open
   *  invisible watermarks of Stable Diffusion 1.x, 2.x and SDXL (`scanned` is false for
   *  images under 256 x 256 pixels), and for Adobe TrustMark when the watermark models are
   *  downloaded (`trustmark.available`; see app.downloadWatermarkModels). Read-only.
   *  Watermarks declared in metadata are the metadataEntries() rows with privacy "watermark".
   *  `checks` lists every kind checked, always all seven, with its status. */
  scanForWatermarks(): {scanned: boolean; matches: PatchyWatermarkMatch[];
    trustmark: {available: boolean; marks: PatchyTrustMarkMark[]}; checks: PatchyWatermarkCheck[]};
  /** Image > Add Invisible Watermark: embeds an Adobe TrustMark watermark (variant Q) into the
   *  active pixel layer, which must fill the image, in an 8-bit document. Give `text`
   *  (printable ASCII; 8 characters with BCH_5, 5 with BCH_SUPER, 9 with BCH_4, 10 with
   *  BCH_3) or `id` (hexadecimal, as scanForWatermarks reports it); with neither, a random ID.
   *  `schema` defaults to "BCH_5" (TrustMark's default), `strength` to 1 (0.1 to 2). Anyone
   *  with a TrustMark reader can read the payload: it is not secret. Needs the watermark
   *  models (app.downloadWatermarkModels). One undo step with the rest of the run. */
  addInvisibleWatermark(options?: {text?: string; id?: string; schema?: "BCH_5" | "BCH_SUPER" | "BCH_4" | "BCH_3";
    strength?: number}): {schema: string; text: string; id: string};
  /** Image > Image Size. `options.method` is the resampling method (default "automatic":
   *  Bicubic Sharper for a reduction, Bicubic Smoother for an enlargement); an unknown id
   *  throws. Text layers and smart objects re-render from their sources afterwards. */
  resizeImage(width: number, height: number, options?: {method?: PatchyResampleMethod}): void;
  resizeCanvas(width: number, height: number): void;
  /** The artboards, top of the layer stack first (empty without artboards). */
  readonly artboards: PatchyLayer[];
  /**
   * Layer > New > Artboard: a new artboard on top of the stack. An empty name gives
   * "Artboard N". The canvas grows to hold it (see setArtboard).
   */
  addArtboard(
    name: string,
    rect: PatchyRect,
    options?: {background?: PatchyArtboardBackground; color?: string},
  ): PatchyLayer;
  /** Layer > New > Artboard from Layers: moves the layers (none on an artboard, none an
   *  artboard) onto a new artboard sized to them. */
  artboardFromLayers(layers: PatchyLayer[], name?: string): PatchyLayer;
  /** Image > Fit Canvas to Artboards: the canvas becomes the artboards plus any visible
   *  layer outside them. Returns how far every document coordinate moved. */
  fitCanvasToArtboards(): {x: number; y: number};
  /**
   * File > Export > Artboards to Files: one file per artboard (all of them, or
   * options.artboards), named options.prefix (default "<document name> - ") plus the
   * artboard's name. A taken name gets " 2", " 3" unless options.overwrite. format is
   * any extension the export dialog offers ("png" by default; "psd" keeps layers).
   * Returns the written paths in order.
   */
  exportArtboards(
    folder: string,
    options?: {format?: string; prefix?: string; artboards?: PatchyLayer[]; overwrite?: boolean},
  ): string[];
  /** Image > Mode > 8/16/32 Bits/Channel: converts every layer, mask and channel.
   *  16 and 32 need deep editing to be on; Indexed documents stay 8-bit. */
  convertBitDepth(bits: 8 | 16 | 32): void;
  /**
   * Edit > Convert to Color Profile: converts the pixels from the document's RGB profile to
   * `profile`, "sRGB" or the path of an RGB .icc/.icm file, which becomes the embedded
   * profile (none for sRGB). Pixel layers, Smart Object previews and the imported palette
   * convert; text, shape, fill and adjustment colors and layer styles keep their values.
   * `intent` defaults to "relative-colorimetric", `blackPointCompensation` to true.
   * Throws for an unusable profile, in Indexed mode and in CMYK Proof; 32-bit documents
   * convert only between matrix-based profiles.
   */
  convertToProfile(
    profile: string,
    options?: {
      intent?: "perceptual" | "relative-colorimetric" | "saturation" | "absolute-colorimetric";
      blackPointCompensation?: boolean;
    },
  ): void;
  /** Crops to the canvas intersection; throws if the rectangle is outside the canvas. */
  crop(x: number, y: number, width: number, height: number): void;
  /**
   * Perspective Crop: rectifies the area inside four corners onto a rectangle, on every
   * layer, mask and channel. `corners` lists them in result order, top left, top right,
   * bottom right, bottom left, as [x, y] pairs or {x, y} points; they must form a convex
   * shape. `width`/`height` default to the corners' mean edge lengths, `resolution`
   * (pixels per inch) to the document's. Area the corners take from beyond the canvas is
   * white under a Background layer and transparent elsewhere. A true perspective
   * rasterizes type layers. Throws for a folded quad or a document with artboards.
   */
  perspectiveCrop(
    corners: [number, number][] | {x: number; y: number}[],
    options?: {width?: number; height?: number; resolution?: number},
  ): void;
  /**
   * Image > Trim: crops the canvas to the composite's content. basedOn picks what is cut
   * away: "transparent" (alpha 0, the default), or pixels exactly matching the "topLeft" or
   * "bottomRight" corner pixel. tolerance (0..255, default 0 = exact) also cuts near
   * matches: alpha up to it, or every channel within it of the corner (8-bit units at any
   * depth). top/bottom/left/right (all true by default) choose the sides. Returns the kept rectangle in the old canvas coordinates, or null when nothing
   * was trimmed (no matching edges, or every pixel matches). Throws with artboards.
   */
  trim(options?: {
    basedOn?: "transparent" | "topLeft" | "bottomRight";
    tolerance?: number;
    top?: boolean;
    bottom?: boolean;
    left?: boolean;
    right?: boolean;
  }): PatchyRect | null;
  /** Saves to the path; the format follows the extension (.psd, .png, ...). The document
   * then points at that file (a layered document saved to a flat format keeps its own
   * path and stays modified, Photoshop's save-a-copy rule). .ora (OpenRaster) keeps the layer
   * tree like .psd: what it cannot hold (text, effects, masks, adjustment layers) is baked into
   * layer pixels, with no prompt for a scripted save. `options` sets the format's
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
  /** Exports visible top-level layers, top first, as an animated PNG (APNG, 8-bit RGBA,
   * lossless). Groups become one frame. Trailing seconds tokens ("blink 0.033s") override
   * frameDelayMs. Preserves the document path and modified state. loopCount is total plays
   * (0 = forever), defaults to the imported or last exported count, otherwise 0; frameDelayMs
   * defaults to 100. Integers: frameDelayMs 0..16777215, loopCount 0..65535. Unknown/invalid
   * options, paths not ending in .png or .apng and export failures throw. saveAs/exportAs with
   * .png continue to write a single flattened image. */
  exportAnimatedPng(path: string, options?: { frameDelayMs?: number; loopCount?: number }): boolean;
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
  /**
   * Creates a document with a white Background layer. `name` is the tab title and the
   * default Save As name (characters a file name cannot hold are dropped); `colorMode`
   * "cmykProof" starts it in Image > Mode > CMYK Proof and throws without a CMYK profile
   * in Preferences. Default: "Untitled", "rgb".
   */
  newDocument(width: number, height: number, options?: { name?: string; colorMode?: "rgb" | "cmykProof" }): PatchyDocument;
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
   * The working CMYK space: the profile Preferences > Application > CMYK selects. Patchy
   * edits in RGB; this space is what CMYK numbers mean in the color picker, the Info
   * panel, CMYK swatches, and the two conversions below. name is the profile's own
   * description, empty when hasProfile is false (the user chose no profile, and conversions
   * use an approximate formula). intent is "perceptual", "relative-colorimetric",
   * "saturation" or "absolute-colorimetric".
   */
  readonly cmykProfile: {name: string; hasProfile: boolean; intent: string; blackPointCompensation: boolean};
  /** Ink percentages (0 to 100, unrounded) for a color in the working CMYK space. Alpha is ignored. */
  colorToCmyk(color: string): {c: number; m: number; y: number; k: number};
  /**
   * The "#rrggbb" color for ink percentages in the working CMYK space. Throws for a value
   * outside 0 to 100. RGB cannot hold every ink mix, so colorToCmyk of the result can
   * return different numbers.
   */
  cmykToColor(c: number, m: number, y: number, k: number): string;
  /**
   * Whether the working CMYK profile can reproduce a color: false for the colors View >
   * Gamut Warning marks. The gamut edge is approximate. Always true when cmykProfile.hasProfile
   * is false.
   */
  colorInCmykGamut(color: string): boolean;
  /**
   * Downloads Object Selection's High Quality model (234 MB from GitHub, kept in Patchy's
   * per-user data folder) when it is missing; the call waits, showing progress in the status
   * bar. Returns true once the model is present (immediately if it already is). Throws on a
   * failed download and in the web build. See doc.selection.objectSelectionModel.
   */
  downloadObjectSelectionModel(): boolean;
  /**
   * Downloads the Adobe TrustMark watermark models (135 MB from Adobe, kept in Patchy's data
   * folder) when they are missing, waiting with progress in the status bar. Returns true once
   * they are present; throws if the download fails, and in the web build. Nothing downloads
   * them without this call or the user asking in the app.
   */
  downloadWatermarkModels(): boolean;
  /** True when the TrustMark watermark models are downloaded and can run. */
  readonly watermarkModelsAvailable: boolean;
  /**
   * Preferences > Watermarks (persisted): the TrustMark text saves add (printable ASCII, at
   * most 8 characters), when saves add it ("never", the default, "ask" or "always"; scripted
   * saves never ask, so "ask" adds nothing to them), and whether opened images are checked
   * in the background.
   */
  readonly watermarkPreferences: {text: string; addOnSave: "never" | "ask" | "always"; checkOnOpen: boolean};
  /**
   * Preferences > Metadata and Watermarks, "Saved metadata" (persisted): what saves add of
   * Patchy's own ("none"; "basic", the default: the PSD/PSB save stamp; "personal": basic plus
   * the name, copyright, email and website below, added to files that do not name the user
   * yet). Scripted saves never ask: with askBeforeAdding true they add nothing personal.
   */
  readonly saveMetadataPreferences: {mode: "none" | "basic" | "personal"; name: string; copyright: string;
    email: string; website: string; askBeforeAdding: boolean};
  /** Sets any of saveMetadataPreferences' keys; an unknown key or a bad mode throws. */
  setSaveMetadataPreferences(preferences: {mode?: "none" | "basic" | "personal"; name?: string; copyright?: string;
    email?: string; website?: string; askBeforeAdding?: boolean}): void;
  /** Sets any of watermarkPreferences' keys; an unknown key, a bad token or text TrustMark
   *  cannot hold throws. */
  setWatermarkPreferences(preferences: {text?: string; addOnSave?: "never" | "ask" | "always";
    checkOnOpen?: boolean}): void;
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
   * Every screen, the primary first, with its geometry (device-independent pixels)
   * and HDR capability, read fresh on each call. hdrQueried is false where Patchy
   * cannot ask the system (Linux, the web build, offscreen and headless runs); the
   * other HDR fields then keep their defaults. headroom is the brightest displayable
   * value as a multiple of SDR white (at least 1): Windows divides peakNits by
   * sdrWhiteNits while HDR is on; macOS reports the current EDR headroom, which moves
   * with brightness, and potentialHeadroom what the screen could reach. Nits are
   * Windows only (null elsewhere, and sdrWhiteNits is null while HDR is off).
   */
  displayInfo(): {
    name: string;
    x: number;
    y: number;
    width: number;
    height: number;
    devicePixelRatio: number;
    primary: boolean;
    hdrQueried: boolean;
    hdrEnabled: boolean;
    headroom: number;
    potentialHeadroom: number;
    sdrWhiteNits: number | null;
    peakNits: number | null;
    fullFrameNits: number | null;
    minNits: number | null;
    remoteSession: boolean;
  }[];
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
   * fonts as pixels. options.pages ("artboards", the default, "groups" or "canvas")
   * says what a page of each document is, like the .pdf save option; artboardPages is
   * the older true/false spelling. Returns true on success and throws on bad arguments
   * or a write failure.
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
      pages?: "artboards" | "groups" | "canvas";
      artboardPages?: boolean;
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
   * Opens a panel of the right column the way its Window menu entry does: reopens it
   * if closed, brings its tab to the front of its group, and expands it. Layers,
   * Channels and Paths share one tab group, so they expand and collapse together.
   * Throws for an unknown name (names are exact and lowercase). Like a click, it
   * changes the layout Patchy saves at a normal close; --headless runs save none.
   */
  expandPanel(name: PatchyPanelName): void;
  /** Collapses the panel's section to its title row (it stays open). Throws for an unknown name. */
  collapsePanel(name: PatchyPanelName): void;
  /** Closes the panel, as its header menu's Close Panel does. Throws for an unknown name. */
  closePanel(name: PatchyPanelName): void;
  /**
   * The panel's state: open (not closed; a tab behind another is open), front (its
   * group's current tab, always true outside a group) and expanded.
   */
  panelState(name: PatchyPanelName): {open: boolean; front: boolean; expanded: boolean};
  /**
   * The active document's view zoom in percent, as the status bar shows it.
   * Reads 0 with no document. Setting clamps to 0.01..12800 and throws for NaN,
   * non-positive values, or with no document. Use doc.activate() first to
   * target another document. Only window captures (captureWindow, the
   * connector's window preview) see the view; document previews never do.
   */
  zoom: number;
  /** View > Fit on Screen for the active document; throws with no document. */
  fitOnScreen(): void;
  /**
   * The active 32-bit document's preview exposure in stops, the status bar's Exposure box:
   * it scales the linear values the canvas, thumbnails and 8-bit exports show, never the
   * pixels. Not saved and not an undo step. Reads 0 without a 32-bit document; setting
   * clamps to -20..20 and throws for NaN or without one. Only window captures see it.
   */
  previewExposure: number;
  /**
   * View > Proof Colors: shows every canvas as the working CMYK profile (app.cmykProfile)
   * would print it. Application-wide, not saved, and a view only: pixels, previews, saves
   * and exports are unchanged, so only window captures see it. Stays false when set
   * without a CMYK profile.
   */
  proofColors: boolean;
  /**
   * View > Gamut Warning: paints colors the working CMYK profile cannot reproduce flat
   * gray on the canvas. Same scope and limits as proofColors.
   */
  gamutWarning: boolean;
  /**
   * View > Show > Pixel Grid: thin lines between pixels at 600% zoom and up, each tinted from its
   * own pixel. An application-wide preference, saved, on by default. A view only: only
   * window captures see it.
   */
  pixelGrid: boolean;
  /**
   * Window > Customize Tool Palette, applied live and saved. Read it for the current choice;
   * assign an object with any of its keys to change those. Tool ids are the tools' hotkey
   * ids ("tools.hand"); hidden tools keep their shortcuts. Layer button ids: "new_layer",
   * "new_folder", "new_adjustment", "layer_styles", "add_mask", "duplicate", "animation",
   * "delete". An unknown id or layout throws.
   */
  toolPalette: {
    hiddenTools?: string[];
    layout?: "oneColumn" | "twoColumns";
    wheelScroll?: boolean;
    showGearButton?: boolean;
    hiddenLayerButtons?: string[];
  };
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

type PatchyGmicValue = number | boolean | string | number[];
type PatchyGmicInputLayers = "active" | "activeAndBelow" | "activeAndAbove" | "all" | "visible" | "hidden";

interface PatchyGmicParameter {
  /** G'MIC's own English label; applyGmic accepts it as a key. */
  name: string;
  type: "float" | "int" | "bool" | "choice" | "color" | "point" | "text" | "file" | "folder" | "button" | "value";
  /** In the form applyGmic takes back (a choice's default is its index). */
  default: PatchyGmicValue;
  /** float and int only. */
  minimum?: number;
  maximum?: number;
  /** choice only: the labels, in index order. */
  choices?: string[];
}

interface PatchyGmic {
  /** False in builds without the G'MIC engine; list() is then empty and applyGmic throws. */
  readonly available: boolean;
  /** The G'MIC version the filters come from, such as "4.0.5"; "" when unavailable. */
  readonly version: string;
  /**
   * Every filter the G'MIC Filter Library offers, in menu order. `id` is permanent.
   * `folder` is the menu path ("Colors", or "Various / Misc"). `inputLayers` is what
   * the filter reads by default. `parameters` are the ones that carry a value, in the
   * order applyGmic takes them.
   */
  list(): {
    id: string;
    name: string;
    folder: string;
    inputLayers: PatchyGmicInputLayers;
    parameters: PatchyGmicParameter[];
  }[];
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
  readonly gmic: PatchyGmic;
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
