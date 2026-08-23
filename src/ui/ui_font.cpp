#include "ui/ui_font.hpp"

namespace patchy::ui {

std::vector<UiFontCandidate> windows_ui_font_candidates() {
  return {
      {QStringLiteral("Arial"),
       {QStringLiteral("C:/Windows/Fonts/arial.ttf"), QStringLiteral("C:/Windows/Fonts/arialbd.ttf"),
        QStringLiteral("C:/Windows/Fonts/ariali.ttf"), QStringLiteral("C:/Windows/Fonts/arialbi.ttf")}},
      {QStringLiteral("Segoe UI"),
       {QStringLiteral("C:/Windows/Fonts/segoeui.ttf"), QStringLiteral("C:/Windows/Fonts/segoeuib.ttf"),
        QStringLiteral("C:/Windows/Fonts/segoeuii.ttf"), QStringLiteral("C:/Windows/Fonts/segoeuiz.ttf")}},
      {QStringLiteral("Calibri"),
       {QStringLiteral("C:/Windows/Fonts/calibri.ttf"), QStringLiteral("C:/Windows/Fonts/calibrib.ttf"),
        QStringLiteral("C:/Windows/Fonts/calibrii.ttf"), QStringLiteral("C:/Windows/Fonts/calibriz.ttf")}},
  };
}

QString installed_ui_font_family(const QStringList& installed_families) {
  for (const auto& candidate : windows_ui_font_candidates()) {
    if (installed_families.contains(candidate.family, Qt::CaseInsensitive)) {
      return candidate.family;
    }
  }
  return {};
}

QStringList ui_font_files_to_register(const QStringList& installed_families) {
  if (!installed_ui_font_family(installed_families).isEmpty()) {
    return {};  // the family is installed: registering its file would break PDF embedding
  }
  return windows_ui_font_candidates().front().files;
}

}  // namespace patchy::ui
