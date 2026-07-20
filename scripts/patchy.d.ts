// Patchy scripting API type definitions (API version 1).
//
// Reference for editors (VS Code autocomplete) and for AI agents driving
// Patchy through `patchy --run-script`. Scripts are plain JavaScript run by
// Patchy's embedded engine (ES6-level); this file is documentation, it is
// never executed.
//
// Colors are CSS-style strings: "#rrggbb", "#aarrggbb", or named ("red").
// Blend mode ids: "pass-through", "normal", "multiply", "screen", "overlay",
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
  /** Child layers (groups only). */
  readonly children: PatchyLayer[];
  /** Text layers: the text content. Setting it re-renders the layer. */
  text: string;

  moveTo(x: number, y: number): void;
  /** Inserts the copy directly above this layer; returns it. */
  duplicate(): PatchyLayer;
  remove(): void;
  /** Fills the selection (or the whole canvas on an empty layer) with a color. */
  fill(color: string): void;
  /**
   * Applies a filter to this layer's pixels by registry id, e.g.
   * applyFilter("patchy.filters.gaussian_blur", {radius: 8}). Unknown ids or
   * parameters throw.
   */
  applyFilter(filterId: string, params?: Record<string, number | boolean | string>): void;
  /** A copy of the layer's pixels (empty layers report width/height 0). */
  getPixels(): PatchyImageData;
  /**
   * Replaces the layer's pixels. data must hold width * height * 4 RGBA
   * bytes; x/y default to the layer's current position. In palette mode the
   * pixels snap to the document palette (like every tool write).
   */
  setPixels(imageData: PatchyImageData): void;
}

interface PatchySelection {
  readonly exists: boolean;
  /** Bounding box, or undefined when there is no selection. */
  readonly bounds: PatchyRect | undefined;
  selectAll(): void;
  deselect(): void;
  selectRect(x: number, y: number, width: number, height: number): void;
  selectEllipse(x: number, y: number, width: number, height: number): void;
}

interface PatchyDocument {
  readonly width: number;
  readonly height: number;
  readonly name: string;
  /** File path, empty for unsaved documents. */
  readonly path: string;
  /** Pixels per inch. */
  readonly resolution: number;
  /** Top-level layers, bottom to top; groups expose .children. */
  readonly layers: PatchyLayer[];
  activeLayer: PatchyLayer | undefined;
  readonly selection: PatchySelection;

  /** Adds an empty pixel layer on top and makes it active. */
  addLayer(name: string): PatchyLayer;
  /**
   * Adds a text layer rendered through Patchy's text engine. Options:
   * {font, size, x, y, color, bold, italic}; x/y is the text anchor point.
   */
  addTextLayer(text: string, options?: {
    font?: string; size?: number; x?: number; y?: number;
    color?: string; bold?: boolean; italic?: boolean;
  }): PatchyLayer;
  /** First layer (depth-first) with this exact name, or undefined. */
  findLayer(name: string): PatchyLayer | undefined;
  flatten(): void;
  resizeImage(width: number, height: number): void;
  resizeCanvas(width: number, height: number): void;
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
  readonly version: string;
  readonly apiVersion: number;
  readonly documents: PatchyDocument[];
  readonly activeDocument: PatchyDocument | undefined;
  open(path: string): PatchyDocument;
  newDocument(width: number, height: number): PatchyDocument;
  /** Message box; logs to the console instead in unattended CLI runs. */
  alert(text: string): void;
  /**
   * Text input dialog. Returns the entered text, or null when cancelled.
   * Unattended CLI runs return defaultValue.
   */
  prompt(text: string, defaultValue?: string): string | null;
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
}

interface PatchyIo {
  /** Throws when the file cannot be read. */
  readTextFile(path: string): string;
  /** Throws when the file cannot be written. */
  writeTextFile(path: string, text: string): void;
}

interface PatchyNamespace {
  readonly app: PatchyApp;
  readonly io: PatchyIo;
  readonly ui: PatchyUi;
  readonly apiVersion: number;
  readonly version: string;
}

declare const app: PatchyApp;
declare const patchy: PatchyNamespace;

declare function setTimeout(callback: (dt: number) => void, ms: number): number;
declare function setInterval(callback: (dt: number) => void, ms: number): number;
declare function clearTimeout(id: number): void;
declare function clearInterval(id: number): void;
declare function requestAnimationFrame(callback: (dt: number) => void): number;
/** Evaluates another script file, resolved relative to the running script. */
declare function include(path: string): void;

declare const console: {
  log(...values: unknown[]): void;
  info(...values: unknown[]): void;
  warn(...values: unknown[]): void;
  error(...values: unknown[]): void;
};
