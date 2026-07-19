#include "ui/app_credits.hpp"

#include <QLatin1String>
#include <QStringList>

namespace patchy::ui {

QString code_contributors_link_html(const QString& link_color) {
  struct Contributor {
    const char* name;
    const char* github_url;
  };
  // Add new code contributors here; the About dialog and the start panel both
  // render this list.
  static constexpr Contributor kContributors[] = {
      {"Michael Capogna", "https://github.com/mcapogna"},
  };

  QStringList links;
  for (const auto& contributor : kContributors) {
    links.append(QStringLiteral("<a style=\"color:%1; text-decoration:none;\" href=\"%2\">%3</a>")
                     .arg(link_color, QLatin1String(contributor.github_url), QLatin1String(contributor.name)));
  }
  return links.join(QStringLiteral(", "));
}

}  // namespace patchy::ui
