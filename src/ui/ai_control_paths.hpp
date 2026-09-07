#pragma once

// Where the local AI-control pieces live in an installed Patchy, resolved
// relative to the running executable, and the paste-into-your-assistant text
// built from them. Shared by the patchy-mcp connector (get_info's skill
// directory) and Help > Set up AI Control, so the two can never disagree about
// the installed layout. See docs/ai-control.md.

#include <QString>

namespace patchy::ui {

inline constexpr char kAiControlSetupUrl[] =
    "https://github.com/SethRobinson/Patchy/blob/main/agent-kit/patchy-control/references/setup.md";
inline constexpr char kFlatpakAppId[] = "com.rtsoft.patchy";
inline constexpr char kFlatpakSkillDirectory[] = "/app/share/patchy/ai/patchy-control";

struct AiControlPaths {
  QString connector_path;       // absolute path of patchy-mcp; empty when the file is missing
  QString skill_directory;      // absolute path of the assembled skill (needs SKILL.md); empty when missing
  QString setup_document_path;  // skill_directory/references/setup.md when it exists; else empty
  bool flatpak{false};          // running inside a Flatpak sandbox (FLATPAK_ID is set)
};

// The assembled patchy-control skill next to the executable: ai/patchy-control
// (Windows and source builds), ../Resources/ai/patchy-control (macOS bundle), or
// ../share/patchy/ai/patchy-control (Linux prefix and Flatpak). Empty when none has
// a SKILL.md.
[[nodiscard]] QString ai_control_skill_directory();

[[nodiscard]] AiControlPaths resolve_ai_control_paths();

// The English text a user pastes into an AI assistant so the assistant configures
// itself. Deliberately not translated: its reader is the assistant, and the
// commands, config keys, and folder names it refers to are English. Missing pieces
// read "NOT FOUND" so the assistant reports instead of guessing.
[[nodiscard]] QString ai_setup_blurb_text(const AiControlPaths& paths);

}  // namespace patchy::ui
