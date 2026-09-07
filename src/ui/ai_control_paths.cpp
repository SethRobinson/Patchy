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
  const auto quoted = [](const QString& path) {
    return QLatin1Char('"') + QDir::toNativeSeparators(path) + QLatin1Char('"');
  };
  const auto url = QString::fromLatin1(kAiControlSetupUrl);
  const auto app_id = QString::fromLatin1(kFlatpakAppId);
  const auto flatpak_skill = QString::fromLatin1(kFlatpakSkillDirectory);

  QStringList lines;
  lines << QStringLiteral(
               "Please set up this AI assistant so it can control Patchy, the image editor")
        << QStringLiteral(
               "installed on this computer. Do the setup yourself. Do not change, remove, or")
        << QStringLiteral("reorder any other MCP servers, skills, or settings.")
        << QString() << QStringLiteral("Read the setup guide first:");
  if (paths.flatpak) {
    lines << QStringLiteral("  Online: %1").arg(url)
          << QStringLiteral("  (Patchy runs as a Flatpak; the local copy is inside its sandbox at")
          << QStringLiteral("  %1/references/setup.md)").arg(flatpak_skill);
  } else if (paths.setup_document_path.isEmpty()) {
    lines << QStringLiteral("  Local:  not installed; use the online copy")
          << QStringLiteral("  Online: %1").arg(url);
  } else {
    lines << QStringLiteral("  Local:  %1").arg(quoted(paths.setup_document_path))
          << QStringLiteral("  Online: %1").arg(url);
  }
  lines << QString() << QStringLiteral("Then:")
        << QStringLiteral(
               "1. Register Patchy's local MCP connector as a stdio server named \"patchy\".");
  if (paths.flatpak) {
    lines << QStringLiteral("   Command: flatpak run --command=patchy-mcp %1").arg(app_id)
          << QStringLiteral(
                 "   (the program is \"flatpak\" with the arguments run --command=patchy-mcp %1;")
                 .arg(app_id)
          << QStringLiteral("   no Python or Node needed)");
  } else if (paths.connector_path.isEmpty()) {
    lines << QStringLiteral("   Command: NOT FOUND (expected %1)")
                 .arg(quoted(QDir(QCoreApplication::applicationDirPath())
                                 .absoluteFilePath(QStringLiteral("patchy-mcp"))));
  } else {
    lines << QStringLiteral("   Command (no arguments, no Python or Node needed): %1")
                 .arg(quoted(paths.connector_path));
  }
  lines << QStringLiteral(
      "2. Install the \"patchy-control\" skill by copying this whole folder into your");
  if (paths.flatpak) {
    lines << QStringLiteral("   skills directory, keeping its name. It is inside the sandbox at")
          << QStringLiteral("   %1").arg(flatpak_skill)
          << QStringLiteral("   Copy it out with: flatpak run --command=cp %1 -R %2 <a folder the")
                 .arg(app_id, flatpak_skill)
          << QStringLiteral("   sandbox can see>");
  } else if (paths.skill_directory.isEmpty()) {
    lines << QStringLiteral("   skills directory, keeping its name: NOT FOUND (expected %1)")
                 .arg(quoted(QDir(QCoreApplication::applicationDirPath())
                                 .absoluteFilePath(QStringLiteral("ai/patchy-control"))));
  } else {
    lines << QStringLiteral("   skills directory, keeping its name: %1")
                 .arg(quoted(paths.skill_directory));
  }
  lines << QStringLiteral("3. Reconnect or restart if you do not see the new server, call the patchy")
        << QStringLiteral("   \"get_info\" tool, then create a 64x64 document, draw a small smiley face,")
        << QStringLiteral("   and show me the get_preview image.") << QString()
        << QStringLiteral(
               "If anything above says NOT FOUND, tell me exactly what is missing and stop.");
  return lines.join(QLatin1Char('\n'));
}

}  // namespace patchy::ui
