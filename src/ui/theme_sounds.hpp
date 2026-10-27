#pragma once

// Theme sounds (docs/theme-files.md, "Sounds"): a theme package may hold
// sounds/<event id>.wav for a short list of named events, played through
// sound_effects.hpp's player (no Qt Multimedia). Without a package sound an event
// is silent: Patchy has no sounds of its own. The "Play theme sounds" preference
// turns them all off, and PATCHY_NO_SOUND silences the player itself.

#include <QByteArray>
#include <QLatin1StringView>
#include <QString>

#include <optional>
#include <span>

namespace patchy::ui {

struct ThemeAssets;

// Append-only: the ids below are persisted. ToolChanged and Idle exist for theme
// scripts and theme.json "events" entries; the others also play sounds/<id>.wav.
enum class ThemeSoundEvent {
  SaveDone,
  ExportDone,
  LongOperationDone,
  Error,
  Warning,
  ScriptAlert,
  LayerDeleted,
  DocumentOpened,
  DocumentNew,
  DocumentClosed,
  Undo,
  Redo,
  Startup,
  ToolChanged,
  Idle,
  StrokeStart,
  StrokeEnd,
};

// Persisted identifiers (the sounds/<id>.wav file names and theme.json "events" keys
// themes ship): "save-done", "export-done", "long-operation-done", "error", "warning",
// "script-alert", "layer-deleted", "document-opened", "document-new", "document-closed",
// "undo", "redo", "startup", "tool-changed", "idle", "stroke-start", "stroke-end". Never rename
// one; a new event appends.
[[nodiscard]] QLatin1StringView theme_sound_event_id(ThemeSoundEvent event);
[[nodiscard]] std::optional<ThemeSoundEvent> theme_sound_event_from_id(const QString& id);
[[nodiscard]] std::span<const ThemeSoundEvent> theme_sound_events();

// The WAV bytes `assets` holds for `event`; empty when it has none or is null.
[[nodiscard]] QByteArray theme_sound_for_event(const ThemeAssets* assets, ThemeSoundEvent event);

// "preferences/playThemeSounds" (a persisted key), on by default.
[[nodiscard]] bool theme_sounds_enabled();
void set_theme_sounds_enabled(bool enabled);

// Plays the active theme's sound for `event`. Returns true when a sound was handed
// to the player (the preference is on and the theme has one), whether or not
// PATCHY_NO_SOUND then keeps it quiet.
bool play_theme_sound(ThemeSoundEvent event);

// Called once at launch, before the main window is built, when the startup event will
// fire: if the active theme has a startup sound, wakes the audio output now
// (wake_audio_output), and play_theme_sound then holds that sound until the output has
// had kStartupSoundWakeMs to wake. A receiver on HDMI otherwise drops the first part of
// the first sound after silence. Window construction usually covers the wait.
inline constexpr int kStartupSoundWakeMs = 800;
void prepare_theme_startup_sound();

// The "long operation finished" event: plays when a progress dialog that stayed up
// for at least kLongOperationSoundMs closes. The watcher is an application event
// filter installed only while the active theme has that sound, so other themes pay
// nothing for it. ThemeManager calls this whenever the theme changes.
inline constexpr int kLongOperationSoundMs = 3000;
void refresh_long_operation_sound_watcher();

}  // namespace patchy::ui
