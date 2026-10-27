#include "core/font_substitutes.hpp"

#include <algorithm>
#include <iterator>

namespace patchy {
namespace {

using M = FontSubstituteMatch;

// Project pages. Only names and these links are recorded here: no font data is bundled.
constexpr std::string_view kLiberation = "https://github.com/liberationfonts/liberation-fonts";
constexpr std::string_view kArimo = "https://fonts.google.com/specimen/Arimo";
constexpr std::string_view kTinos = "https://fonts.google.com/specimen/Tinos";
constexpr std::string_view kCousine = "https://fonts.google.com/specimen/Cousine";
constexpr std::string_view kCarlito = "https://fonts.google.com/specimen/Carlito";
constexpr std::string_view kCaladea = "https://fonts.google.com/specimen/Caladea";
constexpr std::string_view kGelasio = "https://fonts.google.com/specimen/Gelasio";
constexpr std::string_view kSelawik = "https://github.com/microsoft/Selawik";
constexpr std::string_view kComicRelief = "https://github.com/loudifier/Comic-Relief";
constexpr std::string_view kComicNeue = "https://fonts.google.com/specimen/Comic+Neue";
constexpr std::string_view kUrwBase35 = "https://github.com/ArtifexSoftware/urw-base35-fonts";
constexpr std::string_view kTexGyre = "https://www.gust.org.pl/projects/e-foundry/tex-gyre";
constexpr std::string_view kSourceSans = "https://github.com/adobe-fonts/source-sans";
constexpr std::string_view kSourceSerif = "https://github.com/adobe-fonts/source-serif";
constexpr std::string_view kPtSans = "https://fonts.google.com/specimen/PT+Sans";
constexpr std::string_view kCrimsonPro = "https://fonts.google.com/specimen/Crimson+Pro";
constexpr std::string_view kEbGaramond = "https://fonts.google.com/specimen/EB+Garamond";
constexpr std::string_view kJost = "https://fonts.google.com/specimen/Jost";
constexpr std::string_view kInconsolata = "https://fonts.google.com/specimen/Inconsolata";
constexpr std::string_view kDejaVu = "https://dejavu-fonts.github.io/";
constexpr std::string_view kAnton = "https://fonts.google.com/specimen/Anton";
constexpr std::string_view kNotoSansJp = "https://fonts.google.com/noto/specimen/Noto+Sans+JP";
constexpr std::string_view kNotoSerifJp = "https://fonts.google.com/noto/specimen/Noto+Serif+JP";
constexpr std::string_view kCommercial = "";

// Sources for the metric pairs, each the substitute's own statement: the Liberation fonts README
// (metric-compatible with Arial, Times New Roman and Courier New, Liberation Sans Narrow with
// Arial Narrow); Google's Croscore descriptions (Arimo, Tinos, Cousine: metrically compatible with
// Arial, Times New Roman, Courier New); Carlito's and Caladea's descriptions (Calibri, Cambria);
// Gelasio's description (Georgia); Selawik's README (a metric-compatible fallback for Segoe UI);
// Comic Relief's README (Comic Sans MS); the URW base 35 README and TeX Gyre's documentation
// (metric clones of the 35 PostScript core fonts: Helvetica, Times, Courier, Palatino, Bookman,
// New Century Schoolbook, Avant Garde, Zapf Chancery). Arial, Times New Roman, Courier New,
// Palatino Linotype and Century Gothic were drawn to the widths of Helvetica, Times, Courier,
// Palatino and ITC Avant Garde. Every other pair is a look-alike. Written for Patchy; not taken
// from another program's substitution list.

constexpr std::string_view kArialAliases[] = {"ArialMT"};
constexpr FontSubstituteCandidate kArial[] = {
    {"Liberation Sans", M::metric, kLiberation},
    {"Arimo", M::metric, kArimo},
};
constexpr FontSubstituteCandidate kHelvetica[] = {
    {"Arial", M::metric, kCommercial},
    {"Liberation Sans", M::metric, kLiberation},
    {"Arimo", M::metric, kArimo},
    {"Nimbus Sans", M::metric, kUrwBase35},
    {"TeX Gyre Heros", M::metric, kTexGyre},
};
constexpr FontSubstituteCandidate kHelveticaNeue[] = {
    {"Arial", M::lookalike, kCommercial},
    {"Liberation Sans", M::lookalike, kLiberation},
    {"Nimbus Sans", M::lookalike, kUrwBase35},
};
constexpr FontSubstituteCandidate kArialNarrow[] = {
    {"Liberation Sans Narrow", M::metric, kLiberation},
    {"Nimbus Sans Narrow", M::metric, kUrwBase35},
};
constexpr FontSubstituteCandidate kHelveticaNarrow[] = {
    {"Arial Narrow", M::metric, kCommercial},
    {"Liberation Sans Narrow", M::metric, kLiberation},
    {"Nimbus Sans Narrow", M::metric, kUrwBase35},
};
constexpr std::string_view kTimesNewRomanAliases[] = {"TimesNewRomanPSMT", "Times New Roman PS"};
constexpr FontSubstituteCandidate kTimesNewRoman[] = {
    {"Liberation Serif", M::metric, kLiberation},
    {"Tinos", M::metric, kTinos},
    {"Nimbus Roman", M::metric, kUrwBase35},
    {"TeX Gyre Termes", M::metric, kTexGyre},
};
constexpr std::string_view kTimesAliases[] = {"Times Roman"};
constexpr FontSubstituteCandidate kTimes[] = {
    {"Times New Roman", M::metric, kCommercial},
    {"Liberation Serif", M::metric, kLiberation},
    {"Tinos", M::metric, kTinos},
    {"Nimbus Roman", M::metric, kUrwBase35},
    {"TeX Gyre Termes", M::metric, kTexGyre},
};
constexpr std::string_view kCourierNewAliases[] = {"CourierNewPSMT", "Courier New PS"};
constexpr FontSubstituteCandidate kCourierNew[] = {
    {"Liberation Mono", M::metric, kLiberation},
    {"Cousine", M::metric, kCousine},
    {"Nimbus Mono PS", M::metric, kUrwBase35},
    {"TeX Gyre Cursor", M::metric, kTexGyre},
};
constexpr FontSubstituteCandidate kCourier[] = {
    {"Courier New", M::metric, kCommercial},
    {"Liberation Mono", M::metric, kLiberation},
    {"Cousine", M::metric, kCousine},
    {"Nimbus Mono PS", M::metric, kUrwBase35},
    {"TeX Gyre Cursor", M::metric, kTexGyre},
};
constexpr FontSubstituteCandidate kCalibri[] = {{"Carlito", M::metric, kCarlito}};
constexpr FontSubstituteCandidate kCambria[] = {{"Caladea", M::metric, kCaladea}};
constexpr FontSubstituteCandidate kGeorgia[] = {{"Gelasio", M::metric, kGelasio}};
constexpr FontSubstituteCandidate kSegoeUi[] = {{"Selawik", M::metric, kSelawik}};
constexpr std::string_view kComicSansAliases[] = {"Comic Sans"};
constexpr FontSubstituteCandidate kComicSans[] = {
    {"Comic Relief", M::metric, kComicRelief},
    {"Comic Neue", M::lookalike, kComicNeue},
};
constexpr FontSubstituteCandidate kPalatino[] = {
    {"Palatino Linotype", M::metric, kCommercial},
    {"P052", M::metric, kUrwBase35},
    {"TeX Gyre Pagella", M::metric, kTexGyre},
};
constexpr FontSubstituteCandidate kPalatinoLinotype[] = {
    {"P052", M::metric, kUrwBase35},
    {"TeX Gyre Pagella", M::metric, kTexGyre},
};
constexpr FontSubstituteCandidate kBookAntiqua[] = {
    {"Palatino Linotype", M::lookalike, kCommercial},
    {"P052", M::lookalike, kUrwBase35},
    {"TeX Gyre Pagella", M::lookalike, kTexGyre},
};
constexpr std::string_view kBookmanAliases[] = {"Bookman", "ITC Bookman Std"};
constexpr FontSubstituteCandidate kBookman[] = {
    {"URW Bookman", M::metric, kUrwBase35},
    {"TeX Gyre Bonum", M::metric, kTexGyre},
    {"Bookman Old Style", M::lookalike, kCommercial},
};
constexpr FontSubstituteCandidate kBookmanOldStyle[] = {
    {"URW Bookman", M::lookalike, kUrwBase35},
    {"TeX Gyre Bonum", M::lookalike, kTexGyre},
};
constexpr std::string_view kSchoolbookAliases[] = {"Century Schoolbook"};
constexpr FontSubstituteCandidate kSchoolbook[] = {
    {"C059", M::metric, kUrwBase35},
    {"TeX Gyre Schola", M::metric, kTexGyre},
};
constexpr std::string_view kAvantGardeAliases[] = {"Avant Garde", "ITC Avant Garde Gothic Std"};
constexpr FontSubstituteCandidate kAvantGarde[] = {
    {"Century Gothic", M::metric, kCommercial},
    {"URW Gothic", M::metric, kUrwBase35},
    {"TeX Gyre Adventor", M::metric, kTexGyre},
};
constexpr FontSubstituteCandidate kCenturyGothic[] = {
    {"URW Gothic", M::lookalike, kUrwBase35},
    {"TeX Gyre Adventor", M::lookalike, kTexGyre},
};
constexpr std::string_view kZapfChanceryAliases[] = {"Zapf Chancery", "ITC Zapf Chancery Std"};
constexpr FontSubstituteCandidate kZapfChancery[] = {
    {"Z003", M::metric, kUrwBase35},
    {"TeX Gyre Chorus", M::metric, kTexGyre},
};
// Source Sans comes from Myriad's design lineage but has its own widths: a look-alike, not metric.
constexpr FontSubstituteCandidate kMyriad[] = {
    {"Source Sans 3", M::lookalike, kSourceSans},
    {"Source Sans Pro", M::lookalike, kSourceSans},
    {"PT Sans", M::lookalike, kPtSans},
};
constexpr FontSubstituteCandidate kMinion[] = {
    {"Source Serif 4", M::lookalike, kSourceSerif},
    {"Source Serif Pro", M::lookalike, kSourceSerif},
    {"Crimson Pro", M::lookalike, kCrimsonPro},
};
constexpr std::string_view kGaramondAliases[] = {"Adobe Garamond", "Garamond", "Garamond Premier Pro"};
constexpr FontSubstituteCandidate kGaramond[] = {{"EB Garamond", M::lookalike, kEbGaramond}};
constexpr std::string_view kFuturaAliases[] = {"Futura PT", "Futura Std"};
constexpr FontSubstituteCandidate kFutura[] = {{"Jost", M::lookalike, kJost}};
constexpr FontSubstituteCandidate kConsolas[] = {{"Inconsolata", M::lookalike, kInconsolata}};
constexpr FontSubstituteCandidate kVerdana[] = {{"DejaVu Sans", M::lookalike, kDejaVu}};
constexpr FontSubstituteCandidate kImpact[] = {{"Anton", M::lookalike, kAnton}};
constexpr std::string_view kKozukaGothicAliases[] = {"Kozuka Gothic Pro", "KozGoPr6N", "KozGoPro"};
constexpr FontSubstituteCandidate kKozukaGothic[] = {
    {"Yu Gothic", M::lookalike, kCommercial},
    {"Noto Sans JP", M::lookalike, kNotoSansJp},
};
constexpr std::string_view kKozukaMinchoAliases[] = {"Kozuka Mincho Pro", "KozMinPr6N", "KozMinPro"};
constexpr FontSubstituteCandidate kKozukaMincho[] = {
    {"Yu Mincho", M::lookalike, kCommercial},
    {"Noto Serif JP", M::lookalike, kNotoSerifJp},
};
constexpr std::string_view kMsGothicAliases[] = {"MS PGothic", "Meiryo", "Yu Gothic"};
constexpr FontSubstituteCandidate kMsGothic[] = {{"Noto Sans JP", M::lookalike, kNotoSansJp}};
constexpr std::string_view kMsMinchoAliases[] = {"MS PMincho", "Yu Mincho"};
constexpr FontSubstituteCandidate kMsMincho[] = {{"Noto Serif JP", M::lookalike, kNotoSerifJp}};

constexpr std::span<const std::string_view> kNoAliases{};

constexpr FontSubstituteEntry kTable[] = {
    {"Arial", kArialAliases, FontClass::sans, kArial},
    {"Helvetica", kNoAliases, FontClass::sans, kHelvetica},
    {"Helvetica Neue", kNoAliases, FontClass::sans, kHelveticaNeue},
    {"Arial Narrow", kNoAliases, FontClass::sans, kArialNarrow},
    {"Helvetica Narrow", kNoAliases, FontClass::sans, kHelveticaNarrow},
    {"Times New Roman", kTimesNewRomanAliases, FontClass::serif, kTimesNewRoman},
    {"Times", kTimesAliases, FontClass::serif, kTimes},
    {"Courier New", kCourierNewAliases, FontClass::mono, kCourierNew},
    {"Courier", kNoAliases, FontClass::mono, kCourier},
    {"Calibri", kNoAliases, FontClass::sans, kCalibri},
    {"Cambria", kNoAliases, FontClass::serif, kCambria},
    {"Georgia", kNoAliases, FontClass::serif, kGeorgia},
    {"Segoe UI", kNoAliases, FontClass::sans, kSegoeUi},
    {"Comic Sans MS", kComicSansAliases, FontClass::script, kComicSans},
    {"Palatino", kNoAliases, FontClass::serif, kPalatino},
    {"Palatino Linotype", kNoAliases, FontClass::serif, kPalatinoLinotype},
    {"Book Antiqua", kNoAliases, FontClass::serif, kBookAntiqua},
    {"ITC Bookman", kBookmanAliases, FontClass::serif, kBookman},
    {"Bookman Old Style", kNoAliases, FontClass::serif, kBookmanOldStyle},
    {"New Century Schoolbook", kSchoolbookAliases, FontClass::serif, kSchoolbook},
    {"ITC Avant Garde Gothic", kAvantGardeAliases, FontClass::sans, kAvantGarde},
    {"Century Gothic", kNoAliases, FontClass::sans, kCenturyGothic},
    {"ITC Zapf Chancery", kZapfChanceryAliases, FontClass::script, kZapfChancery},
    {"Myriad Pro", kNoAliases, FontClass::sans, kMyriad},
    {"Minion Pro", kNoAliases, FontClass::serif, kMinion},
    {"Adobe Garamond Pro", kGaramondAliases, FontClass::serif, kGaramond},
    {"Futura", kFuturaAliases, FontClass::sans, kFutura},
    {"Consolas", kNoAliases, FontClass::mono, kConsolas},
    {"Verdana", kNoAliases, FontClass::sans, kVerdana},
    {"Impact", kNoAliases, FontClass::display, kImpact},
    {"Kozuka Gothic Pr6N", kKozukaGothicAliases, FontClass::sans, kKozukaGothic},
    {"Kozuka Mincho Pr6N", kKozukaMinchoAliases, FontClass::serif, kKozukaMincho},
    {"MS Gothic", kMsGothicAliases, FontClass::sans, kMsGothic},
    {"MS Mincho", kMsMinchoAliases, FontClass::serif, kMsMincho},
};

// Trailing words a face-baked family name carries ("Myriad Pro Semibold", "Arial-BoldMT"),
// compared as keys.
constexpr std::string_view kFaceWords[] = {
    "regular",  "roman",      "book",       "plain",      "normal",       "medium",   "semibold",
    "semibd",   "demibold",   "demi",       "bold",       "bd",           "extrabold", "ultrabold",
    "black",    "heavy",      "light",      "lt",         "extralight",   "ultralight", "thin",
    "italic",   "it",         "oblique",    "condensed",  "cond",         "boldit",   "bolditalic",
    "semiboldit", "semibolditalic", "lightit", "mediumit", "blackit",     "boldmt",   "italicmt",
    "bolditalicmt", "mt",     "psmt",       "boldpsmt",   "italicpsmt",   "bolditalicpsmt",
};

bool is_face_word(std::string_view word) {
  const auto key = font_substitute_key(word);
  return !key.empty() && std::ranges::find(kFaceWords, std::string_view(key)) != std::end(kFaceWords);
}

const FontSubstituteEntry* entry_for_key(std::string_view key, std::span<const FontSubstituteEntry> table) {
  if (key.empty()) {
    return nullptr;
  }
  for (const auto& entry : table) {
    if (font_substitute_key(entry.family) == key) {
      return &entry;
    }
    for (const auto alias : entry.aliases) {
      if (font_substitute_key(alias) == key) {
        return &entry;
      }
    }
  }
  return nullptr;
}

}  // namespace

std::span<const FontSubstituteEntry> builtin_font_substitutes() noexcept {
  return kTable;
}

std::string font_substitute_key(std::string_view name) {
  std::string key;
  key.reserve(name.size());
  for (const auto ch : name) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte >= 0x80U) {
      key.push_back(ch);
    } else if (byte >= 'A' && byte <= 'Z') {
      key.push_back(static_cast<char>(byte - 'A' + 'a'));
    } else if ((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9')) {
      key.push_back(ch);
    }
  }
  return key;
}

std::optional<FontSubstituteLookup> find_font_substitutes(std::string_view family,
                                                          std::span<const FontSubstituteEntry> table) {
  if (const auto* entry = entry_for_key(font_substitute_key(family), table); entry != nullptr) {
    return FontSubstituteLookup{entry, {}};
  }
  // Split at spaces, hyphens and underscores, then drop face words from the end one at a time.
  std::vector<std::string_view> words;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= family.size(); ++i) {
    if (i == family.size() || family[i] == ' ' || family[i] == '-' || family[i] == '_') {
      if (i > start) {
        words.push_back(family.substr(start, i - start));
      }
      start = i + 1;
    }
  }
  std::size_t kept = words.size();
  while (kept > 1 && is_face_word(words[kept - 1])) {
    --kept;
    std::string base;
    for (std::size_t i = 0; i < kept; ++i) {
      base += words[i];
    }
    if (const auto* entry = entry_for_key(font_substitute_key(base), table); entry != nullptr) {
      std::string style;
      for (std::size_t i = kept; i < words.size(); ++i) {
        if (!style.empty()) {
          style += ' ';
        }
        style += words[i];
      }
      return FontSubstituteLookup{entry, style};
    }
  }
  return std::nullopt;
}

std::vector<FontSubstituteSuggestion> rank_font_substitutes(const FontSubstituteEntry& entry,
                                                            const std::function<bool(std::string_view)>& usable) {
  std::vector<FontSubstituteSuggestion> ranked;
  std::vector<std::string> seen{font_substitute_key(entry.family)};
  for (const auto match : {FontSubstituteMatch::metric, FontSubstituteMatch::lookalike}) {
    for (const auto& candidate : entry.candidates) {
      if (candidate.match != match) {
        continue;
      }
      auto key = font_substitute_key(candidate.family);
      if (std::ranges::find(seen, key) != seen.end()) {
        continue;
      }
      seen.push_back(std::move(key));
      if (usable && usable(candidate.family)) {
        ranked.push_back(FontSubstituteSuggestion{std::string(candidate.family), candidate.match});
      }
    }
  }
  return ranked;
}

}  // namespace patchy
