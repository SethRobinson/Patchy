#include "ui/app_credits.hpp"

#include <QLatin1String>
#include <QStringList>

namespace patchy::ui {

QString contributors_link_html(const QString& link_color) {
  struct Contributor {
    const char* name;
    const char* github_url;
  };
  // Everyone whose code, bug report or suggestion made it into Patchy, by GitHub
  // handle, never real name: code contributors first in the order their first
  // pull request was merged, then issue reporters in the order their first issue
  // was resolved. The About dialog renders this list (the start panel keeps its
  // footer short so the recent files get the room); the release checklist in
  // docs/release-process.md keeps it current.
  static constexpr Contributor kContributors[] = {
      {"mcapogna", "https://github.com/mcapogna"},
      {"csbun", "https://github.com/csbun"},
      {"ifloppy", "https://github.com/ifloppy"},
      {"lucastucious", "https://github.com/lucastucious"},
      {"c-sanchez", "https://github.com/c-sanchez"},
      {"egofree71", "https://github.com/egofree71"},
      {"PorkingMane", "https://github.com/PorkingMane"},
      {"alexanderadam", "https://github.com/alexanderadam"},
      {"danielmigueltejedor", "https://github.com/danielmigueltejedor"},
      {"ProShi", "https://github.com/ProShi"},
      {"Kevdoy", "https://github.com/Kevdoy"},
      {"popkc3", "https://github.com/popkc3"},
      {"WinterTreat", "https://github.com/WinterTreat"},
      {"jackpini", "https://github.com/jackpini"},
      {"fivetenth", "https://github.com/fivetenth"},
      {"revae", "https://github.com/revae"},
      {"RDR77", "https://github.com/RDR77"},
      {"Oppa-YA", "https://github.com/Oppa-YA"},
  };

  QStringList links;
  for (const auto& contributor : kContributors) {
    links.append(QStringLiteral("<a style=\"color:%1; text-decoration:none;\" href=\"%2\">%3</a>")
                     .arg(link_color, QLatin1String(contributor.github_url), QLatin1String(contributor.name)));
  }
  return links.join(QStringLiteral(", "));
}

}  // namespace patchy::ui
