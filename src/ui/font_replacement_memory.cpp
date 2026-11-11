#include "ui/font_replacement_memory.hpp"

#include "ui/app_settings.hpp"
#include "ui/text_font_service.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <limits>

namespace patchy::ui {
namespace {

struct RememberedFont {
  QString key;
  std::vector<RememberedFontReplacement> picks;  // most recent first
};

// Reads the stored list, dropping anything malformed and applying the caps (a hand-edited or
// damaged value must never grow the settings or break the dialog).
std::vector<RememberedFont> load_remembered_fonts() {
  std::vector<RememberedFont> fonts;
  const auto stored = app_settings().value(QString::fromLatin1(kMissingFontReplacementsKey)).toString();
  if (stored.isEmpty()) {
    return fonts;
  }
  const auto document = QJsonDocument::fromJson(stored.toUtf8());
  if (!document.isArray()) {
    return fonts;
  }
  for (const auto& font_value : document.array()) {
    if (static_cast<int>(fonts.size()) >= kMaxRememberedReplacementFonts) {
      break;
    }
    const auto font_object = font_value.toObject();
    RememberedFont font{font_object.value(QStringLiteral("font")).toString(), {}};
    if (font.key.isEmpty() ||
        std::ranges::any_of(fonts, [&font](const RememberedFont& other) { return other.key == font.key; })) {
      continue;
    }
    for (const auto& pick_value : font_object.value(QStringLiteral("picks")).toArray()) {
      if (static_cast<int>(font.picks.size()) >= kMaxRememberedReplacementsPerFont) {
        break;
      }
      const auto pick_object = pick_value.toObject();
      const auto family = pick_object.value(QStringLiteral("family")).toString().trimmed();
      const auto count = pick_object.value(QStringLiteral("count")).toInt();
      if (family.isEmpty() || count <= 0 ||
          std::ranges::any_of(font.picks, [&family](const RememberedFontReplacement& other) {
            return other.family.compare(family, Qt::CaseInsensitive) == 0;
          })) {
        continue;
      }
      font.picks.push_back(RememberedFontReplacement{family, count});
    }
    if (!font.picks.empty()) {
      fonts.push_back(std::move(font));
    }
  }
  return fonts;
}

void save_remembered_fonts(const std::vector<RememberedFont>& fonts) {
  QJsonArray array;
  for (const auto& font : fonts) {
    QJsonArray picks;
    for (const auto& pick : font.picks) {
      picks.append(QJsonObject{{QStringLiteral("family"), pick.family}, {QStringLiteral("count"), pick.count}});
    }
    array.append(QJsonObject{{QStringLiteral("font"), font.key}, {QStringLiteral("picks"), picks}});
  }
  auto settings = app_settings();
  if (array.isEmpty()) {
    settings.remove(QString::fromLatin1(kMissingFontReplacementsKey));
  } else {
    settings.setValue(QString::fromLatin1(kMissingFontReplacementsKey),
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
  }
}

}  // namespace

std::vector<RememberedFontReplacement> remembered_font_replacements(const QString& missing_family) {
  const auto key = text_font_replacement_key(missing_family);
  if (key.isEmpty()) {
    return {};
  }
  auto fonts = load_remembered_fonts();
  const auto found = std::ranges::find_if(fonts, [&key](const RememberedFont& font) { return font.key == key; });
  return found == fonts.end() ? std::vector<RememberedFontReplacement>{} : std::move(found->picks);
}

std::vector<RememberedFontReplacement> remembered_font_replacements_by_count(const QString& missing_family) {
  auto picks = remembered_font_replacements(missing_family);
  std::stable_sort(picks.begin(), picks.end(),
                   [](const RememberedFontReplacement& a, const RememberedFontReplacement& b) {
                     return a.count > b.count;
                   });
  return picks;
}

void record_font_replacement(const QString& missing_family, const QString& chosen_family) {
  const auto key = text_font_replacement_key(missing_family);
  const auto family = chosen_family.trimmed();
  if (key.isEmpty() || family.isEmpty()) {
    return;
  }
  auto fonts = load_remembered_fonts();
  RememberedFont font{key, {}};
  if (const auto found = std::ranges::find_if(fonts, [&key](const RememberedFont& other) { return other.key == key; });
      found != fonts.end()) {
    font = std::move(*found);
    fonts.erase(found);
  }
  RememberedFontReplacement pick{family, 0};
  if (const auto found = std::ranges::find_if(font.picks,
                                              [&family](const RememberedFontReplacement& other) {
                                                return other.family.compare(family, Qt::CaseInsensitive) == 0;
                                              });
      found != font.picks.end()) {
    pick.count = found->count;
    font.picks.erase(found);
  }
  if (pick.count < std::numeric_limits<int>::max()) {
    ++pick.count;
  }
  font.picks.insert(font.picks.begin(), std::move(pick));
  if (static_cast<int>(font.picks.size()) > kMaxRememberedReplacementsPerFont) {
    font.picks.resize(static_cast<std::size_t>(kMaxRememberedReplacementsPerFont));
  }
  fonts.insert(fonts.begin(), std::move(font));
  if (static_cast<int>(fonts.size()) > kMaxRememberedReplacementFonts) {
    fonts.resize(static_cast<std::size_t>(kMaxRememberedReplacementFonts));
  }
  save_remembered_fonts(fonts);
}

void forget_font_replacement(const QString& missing_family, const QString& chosen_family) {
  const auto key = text_font_replacement_key(missing_family);
  const auto family = chosen_family.trimmed();
  if (key.isEmpty() || family.isEmpty()) {
    return;
  }
  auto fonts = load_remembered_fonts();
  const auto found = std::ranges::find_if(fonts, [&key](const RememberedFont& font) { return font.key == key; });
  if (found == fonts.end()) {
    return;
  }
  const auto removed = std::erase_if(found->picks, [&family](const RememberedFontReplacement& pick) {
    return pick.family.compare(family, Qt::CaseInsensitive) == 0;
  });
  if (removed == 0) {
    return;
  }
  if (found->picks.empty()) {
    fonts.erase(found);
  }
  save_remembered_fonts(fonts);
}

void clear_remembered_font_replacements() {
  auto settings = app_settings();
  settings.remove(QString::fromLatin1(kMissingFontReplacementsKey));
}

}  // namespace patchy::ui
