#include "ui/ai_control_paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLatin1String>
#include <QStringList>

namespace patchy::ui {

QString ai_control_skill_directory() {
  const QDir exe(QCoreApplication::applicationDirPath());
  for (const auto* path : {"ai/patchy-control", "../Resources/ai/patchy-control",
                           "../share/patchy/ai/patchy-control"}) {
    const auto candidate = exe.absoluteFilePath(QLatin1String(path));
    if (QFileInfo::exists(candidate + QStringLiteral("/SKILL.md"))) {
      return QDir::cleanPath(candidate);
    }
  }
  return {};
}

AiControlPaths resolve_ai_control_paths() {
  AiControlPaths paths;
#ifdef Q_OS_WIN
  const auto connector_name = QStringLiteral("patchy-mcp.exe");
#else
  const auto connector_name = QStringLiteral("patchy-mcp");
#endif
  // Windows and Linux keep the connector beside the app; the macOS bundle keeps both
  // executables in Contents/MacOS, which is applicationDirPath there too.
  const auto connector = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(connector_name);
  if (QFileInfo::exists(connector)) {
    paths.connector_path = QDir::cleanPath(connector);
  }
  paths.skill_directory = ai_control_skill_directory();
  if (!paths.skill_directory.isEmpty()) {
    const auto setup = paths.skill_directory + QStringLiteral("/references/setup.md");
    if (QFileInfo::exists(setup)) {
      paths.setup_document_path = setup;
    }
  }
  paths.flatpak = qEnvironmentVariableIsSet("FLATPAK_ID");
  return paths;
}

QString ai_setup_blurb_text(const AiControlPaths& paths) {
  // One sentence per line and every path on its own line: the dialog wraps long
  // lines itself, and hard breaks inside a sentence would fight that wrapping.
  const auto quoted = [](const QString& path) {
    return QLatin1Char('"') + QDir::toNativeSeparators(path) + QLatin1Char('"');
  };
  const auto expected = [&quoted](const char* relative) {
    return quoted(
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QLatin1String(relative)));
  };
  const auto url = QString::fromLatin1(kAiControlSetupUrl);
  const auto app_id = QString::fromLatin1(kFlatpakAppId);
  const auto flatpak_skill = QString::fromLatin1(kFlatpakSkillDirectory);

  QStringList lines;
  lines << QStringLiteral("Please set up this AI assistant so it can control Patchy, the image "
                          "editor installed on this computer. Do the setup yourself. Do not "
                          "change, remove, or reorder any other MCP servers, skills, or settings.")
        << QString() << QStringLiteral("Read the setup guide first:");
  if (paths.flatpak) {
    lines << QStringLiteral("  Online: %1").arg(url)
          << QStringLiteral("  (Patchy runs as a Flatpak; the local copy is inside its sandbox at "
                            "%1/references/setup.md)")
                 .arg(flatpak_skill);
  } else if (paths.setup_document_path.isEmpty()) {
    lines << QStringLiteral("  Local: not installed; use the online copy")
          << QStringLiteral("  Online: %1").arg(url);
  } else {
    lines << QStringLiteral("  Local: %1").arg(quoted(paths.setup_document_path))
          << QStringLiteral("  Online: %1").arg(url);
  }
  lines << QString() << QStringLiteral("Then:")
        << QStringLiteral("1. Register Patchy's local MCP connector as a stdio server named "
                          "\"patchy\". It needs no Python or Node.");
  if (paths.flatpak) {
    lines << QStringLiteral("   Command: flatpak run --command=patchy-mcp %1").arg(app_id)
          << QStringLiteral("   (the program is \"flatpak\" with the arguments run "
                            "--command=patchy-mcp %1)")
                 .arg(app_id);
  } else if (paths.connector_path.isEmpty()) {
    lines << QStringLiteral("   Command: NOT FOUND (expected %1)").arg(expected("patchy-mcp"));
  } else {
    lines << QStringLiteral("   Command: %1").arg(quoted(paths.connector_path));
  }
  lines << QStringLiteral("   For a new connection, use no connector arguments. Reuse an existing matching Patchy connection without changing its workspace mode.")
        << QStringLiteral("   This is a one-time setup. For later tasks, follow the workflow to use my open workspace, work visibly, or work in the background without reinstalling. Use the existing connector or the command-line API as appropriate.");
  lines << QStringLiteral("2. Create a \"patchy-control\" folder in your skills directory and "
                          "copy only SKILL.md from this folder into it. The skill fetches "
                          "current instructions from Patchy; leave references and scripts here:");
  if (paths.flatpak) {
    lines << QStringLiteral("   %1 (inside the Flatpak sandbox)").arg(flatpak_skill)
          << QStringLiteral("   Copy the file out with: flatpak run --command=cp %1 %2/SKILL.md "
                            "<the destination patchy-control folder, visible to the sandbox>")
                 .arg(app_id, flatpak_skill);
  } else if (paths.skill_directory.isEmpty()) {
    lines << QStringLiteral("   NOT FOUND (expected %1)").arg(expected("ai/patchy-control"));
  } else {
    lines << QStringLiteral("   %1").arg(quoted(paths.skill_directory));
  }
  lines << QStringLiteral("3. Call \"get_info\", read get_help(workflow) and get_help(api), and call get_state. In attached mode, use the returned stateToken as expectedState for every editing tool.")
        << QStringLiteral("   For the setup test, create a new 64x64 document without changing any existing documents, "
                          "draw a small smiley face, and show me the get_preview image. Save "
                          "the drawing as a PSD and PNG and tell me where they are.")
        << QStringLiteral("   If the new tools are unavailable in this chat, tell me exactly "
                          "what is configured and how to reconnect or restart. Do not claim "
                          "the drawing test passed until you have called the tools.")
        << QString()
        << QStringLiteral(
               "If anything above says NOT FOUND, tell me exactly what is missing and stop.");
  return lines.join(QLatin1Char('\n'));
}

}  // namespace patchy::ui
