// Patchy theme script API (theme packages' scripts/main.js).
//
// Theme scripts run in their own locked-down engine, not the user scripting engine
// described in patchy.d.ts: the only globals are the JavaScript built-ins and the
// declarations below. There is no file, document, process or network access, every
// path is relative to the theme package, and every call into theme code runs under a
// time budget. See docs/theme-files.md ("Theme scripts") for the limits and the trust
// prompt. Identifiers here are permanent; changes are logged in
// docs/scripting-api-changes.md.

/** The theme events, in the order Patchy lists them. */
type PatchyThemeEvent =
  | "save-done"
  | "export-done"
  | "long-operation-done"
  | "error"
  | "warning"
  | "script-alert"
  | "layer-deleted"
  | "document-opened"
  | "document-new"
  | "document-closed"
  | "undo"
  | "redo"
  | "startup"
  | "tool-changed"
  | "idle"
  | "stroke-start"
  | "stroke-end";

/** What an event handler receives. Only these events carry fields. */
interface PatchyThemeEventPayload {
  /** layer-deleted: how many layers went. */
  count?: number;
  /** document-opened: the document size in pixels. */
  width?: number;
  height?: number;
  /** tool-changed: the new tool's id ("crop", "brush", "type", ...); stroke-start and
   * stroke-end: the painting tool's id ("brush", "eraser", "clone", ...). */
  tool?: string;
}

/** A built-in effect, or a sprite sheet from the package. */
interface PatchyThemeEffect {
  name?: "fireworks" | "sparkles" | "confetti" | "sprite";
  /** Position as a fraction of the document area (default 0.5, 0.4). */
  x?: number;
  y?: number;
  /** Milliseconds, 100..10000. */
  duration?: number;
  /** fireworks: 1..8 bursts. */
  bursts?: number;
  /** sparkles: spread in pixels and count; confetti: count. */
  radius?: number;
  count?: number;
  /** sprite: a package image holding the frames side by side (or in a grid). */
  image?: string;
  frames?: number;
  frameWidth?: number;
  frameHeight?: number;
  fps?: number;
  /** sprite: cycle the frames for the whole duration (default true). */
  loop?: boolean;
  scale?: number;
  /** sprite: mirror it horizontally. */
  flip?: boolean;
  /** sprite: where it moves to over the duration, as fractions. */
  toX?: number;
  toY?: number;
  /** "start-screen": only over the empty start screen, never over a document. */
  where?: "start-screen";
}

interface PatchyThemeCommand {
  /** [A-Za-z0-9_-], unique in the theme. The hotkey id is theme.<theme id>.<id>. */
  id: string;
  title: string;
  /** A key sequence such as "Ctrl+Alt+D"; a built-in default wins a collision. */
  hotkey?: string;
}

interface PatchyThemeApi {
  /** The theme's id (its folder or zip name) and display name. */
  readonly id: string;
  readonly name: string;
  /** Runs fn on every occurrence of the event; an unknown name throws. */
  on(event: PatchyThemeEvent, fn: (payload: PatchyThemeEventPayload) => void): void;
  /** Plays a WAV file from the package (4 a second at most). False when refused. */
  playSound(path: string): boolean;
  /** Plays an effect by name with options, or a full spec (4 a second at most). */
  playEffect(name: string, options?: PatchyThemeEffect): boolean;
  playEffect(spec: PatchyThemeEffect): boolean;
  /** Shows a status-bar message for ms (1000..10000, default 4000). */
  showStatus(text: string, ms?: number): void;
  /** Adds a command to the theme menu right of Help (32 at most). Throws when refused. */
  addCommand(command: PatchyThemeCommand, fn: () => void): void;
  /**
   * Replaces a tool's plain pointer with a package image (128 px at most); an empty
   * image restores Patchy's. Feedback cursors (handles, modifiers, brush sizes) stay.
   */
  setToolCursor(tool: string, image: string, hotX?: number, hotY?: number): boolean;
  /** Shows a package image centered over the window for ms (500..6000, default 2500). */
  showSplash(image: string, ms?: number): boolean;
  /**
   * A faint background image behind a panel, or behind every dialog ("dialogs").
   * opacity 0.05..0.6 (default 0.3); an empty image removes it.
   */
  setPanelBackground(
    panel: "layers" | "channels" | "paths" | "history" | "properties" | "info" | "palette" | "color" | "tools" | "dialogs",
    image: string,
    mode?: "tile" | "stretch" | "center",
    opacity?: number
  ): boolean;
  /** Writes to the theme log (Patchy's debug output). */
  log(...values: unknown[]): void;
}

declare const patchy: { readonly theme: PatchyThemeApi };
declare function setTimeout(fn: () => void, ms?: number): number;
declare function setInterval(fn: () => void, ms: number): number;
declare function clearTimeout(id: number): void;
declare function clearInterval(id: number): void;
declare const console: { log(...values: unknown[]): void; info(...values: unknown[]): void; warn(...values: unknown[]): void; error(...values: unknown[]): void };
