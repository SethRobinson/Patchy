#include "ui/app_credits.hpp"

#include <QLatin1String>
#include <QStringList>

namespace patchy::ui {

QString contributors_link_html(const QString& link_color) {
  struct Contributor {
    const char* name;
    const char* github_url;
    int count;
  };
  // Everyone whose code, bug report or suggestion made it into Patchy, by GitHub
  // handle, never real name, with their points: one per merged pull request and
  // one per accepted issue, most points first. The About dialog renders this
  // list (the start panel keeps its footer short so the recent files get the
  // room). Generated: scripts/dev/update-contributors.py rewrites the entries
  // and the README's copy from GitHub (docs/release-process.md), so never edit
  // them by hand.
  static constexpr Contributor kContributors[] = {
      {"philipzae", "https://github.com/philipzae", 546},
      {"malnaanah", "https://github.com/malnaanah", 25},
      {"KevDoy", "https://github.com/KevDoy", 22},
      {"lucastucious", "https://github.com/lucastucious", 12},
      {"egofree71", "https://github.com/egofree71", 11},
      {"YuFu-Onship", "https://github.com/YuFu-Onship", 10},
      {"mcapogna", "https://github.com/mcapogna", 7},
      {"ifloppy", "https://github.com/ifloppy", 6},
      {"jackpini", "https://github.com/jackpini", 4},
      {"fivetenth", "https://github.com/fivetenth", 4},
      {"spgrhy-lab", "https://github.com/spgrhy-lab", 4},
      {"revae", "https://github.com/revae", 3},
      {"c-sanchez", "https://github.com/c-sanchez", 2},
      {"alexanderadam", "https://github.com/alexanderadam", 2},
      {"WinterTreat", "https://github.com/WinterTreat", 2},
      {"heshmatagha", "https://github.com/heshmatagha", 2},
      {"csbun", "https://github.com/csbun", 1},
      {"softwaregames1", "https://github.com/softwaregames1", 1},
      {"chiddekel", "https://github.com/chiddekel", 1},
      {"PorkingMane", "https://github.com/PorkingMane", 1},
      {"danielmigueltejedor", "https://github.com/danielmigueltejedor", 1},
      {"ProShi", "https://github.com/ProShi", 1},
      {"popkc3", "https://github.com/popkc3", 1},
      {"RDR77", "https://github.com/RDR77", 1},
      {"Oppa-YA", "https://github.com/Oppa-YA", 1},
      {"CHJ85", "https://github.com/CHJ85", 1},
      {"DavidLee1115", "https://github.com/DavidLee1115", 1},
      {"Uthopik", "https://github.com/Uthopik", 1},
      {"saeedsabe", "https://github.com/saeedsabe", 1},
      {"LiveFreeDead", "https://github.com/LiveFreeDead", 1},
  };

  QStringList links;
  for (const auto& contributor : kContributors) {
    // A no-break space keeps a count on the same line as its handle when the
    // list wraps.
    links.append(QStringLiteral("<a style=\"color:%1; text-decoration:none;\" href=\"%2\">%3</a>&nbsp;(%4)")
                     .arg(link_color, QLatin1String(contributor.github_url), QLatin1String(contributor.name),
                          QString::number(contributor.count)));
  }
  return links.join(QStringLiteral(", "));
}

}  // namespace patchy::ui
