// The built-in font substitute table (GitHub issue 463). See
// docs/font-resolution-missing-fonts.md, "Substitute table".
#include "core/font_substitutes.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using patchy::FontClass;
using patchy::FontSubstituteMatch;
using patchy::find_font_substitutes;
using patchy::font_substitute_key;
using patchy::rank_font_substitutes;

std::string_view found_family(std::string_view name) {
  const auto found = find_font_substitutes(name);
  return found.has_value() && found->entry != nullptr ? found->entry->family : std::string_view{};
}

void font_substitutes_lookup_matches_family_and_postscript_names() {
  CHECK(font_substitute_key("Times New Roman") == "timesnewroman");
  CHECK(font_substitute_key("Myriad-Pro_Semibold 2") == "myriadprosemibold2");
  CHECK(font_substitute_key("\xE4\xB8\xAD Font") == "\xE4\xB8\xAD" "font");  // non-ASCII kept

  CHECK(found_family("Arial") == "Arial");
  CHECK(found_family("ArialMT") == "Arial");
  CHECK(found_family("arial mt") == "Arial");
  CHECK(found_family("Myriad Pro") == "Myriad Pro");
  CHECK(found_family("MyriadPro") == "Myriad Pro");
  CHECK(found_family("TimesNewRomanPSMT") == "Times New Roman");
  CHECK(found_family("Times") == "Times");
  CHECK(found_family("Arial Narrow") == "Arial Narrow");
  CHECK(found_family("KozGoPr6N") == "Kozuka Gothic Pr6N");
  CHECK(found_family("Cambria") == "Cambria");
  CHECK(find_font_substitutes("Cambria")->entry->font_class == FontClass::serif);
  CHECK(find_font_substitutes("Courier New")->entry->font_class == FontClass::mono);

  CHECK(!find_font_substitutes("PatchyDefinitelyMissingFont123456").has_value());
  CHECK(!find_font_substitutes("").has_value());
  CHECK(!find_font_substitutes("Bold").has_value());
  CHECK(!find_font_substitutes("Arialish").has_value());
}

void font_substitutes_face_names_fall_back_to_the_base_family() {
  const auto semibold = find_font_substitutes("Myriad Pro Semibold");
  CHECK(semibold.has_value() && semibold->entry->family == "Myriad Pro" && semibold->style == "Semibold");
  const auto postscript = find_font_substitutes("MyriadPro-BoldIt");
  CHECK(postscript.has_value() && postscript->entry->family == "Myriad Pro" && postscript->style == "BoldIt");
  CHECK(!find_font_substitutes("Myriad Pro Display").has_value());  // not a face word
  const auto regular = find_font_substitutes("MyriadPro-Regular");
  CHECK(regular.has_value() && regular->entry->family == "Myriad Pro" && regular->style == "Regular");
  const auto two_words = find_font_substitutes("Arial Narrow Bold Italic");
  CHECK(two_words.has_value() && two_words->entry->family == "Arial Narrow" && two_words->style == "Bold Italic");
  const auto mt = find_font_substitutes("Arial-BoldMT");
  CHECK(mt.has_value() && mt->entry->family == "Arial" && mt->style == "BoldMT");
  // An exact entry wins over dropping a word: "Arial Narrow" is not "Arial" + "Narrow".
  CHECK(find_font_substitutes("Arial Narrow")->style.empty());
}

void font_substitutes_rank_metric_before_lookalike_in_table_order() {
  const auto comic = find_font_substitutes("Comic Sans MS");
  CHECK(comic.has_value());
  if (!comic.has_value()) {
    return;
  }
  const auto all = rank_font_substitutes(*comic->entry, [](std::string_view) { return true; });
  CHECK(all.size() == 2);
  CHECK(all.size() == 2 && all[0].family == "Comic Relief" && all[0].match == FontSubstituteMatch::metric);
  CHECK(all.size() == 2 && all[1].family == "Comic Neue" && all[1].match == FontSubstituteMatch::lookalike);

  // Only installed candidates come back, in table order.
  const auto helvetica = find_font_substitutes("Helvetica");
  CHECK(helvetica.has_value());
  if (!helvetica.has_value()) {
    return;
  }
  const std::set<std::string_view> installed{"TeX Gyre Heros", "Liberation Sans"};
  const auto some = rank_font_substitutes(*helvetica->entry,
                                          [&installed](std::string_view family) { return installed.contains(family); });
  CHECK(some.size() == 2);
  CHECK(some.size() == 2 && some[0].family == "Liberation Sans" && some[1].family == "TeX Gyre Heros");
  CHECK(rank_font_substitutes(*helvetica->entry, [](std::string_view) { return false; }).empty());
  CHECK(rank_font_substitutes(*helvetica->entry, {}).empty());

  // A metric candidate listed after a look-alike still ranks first.
  constexpr patchy::FontSubstituteCandidate mixed[] = {
      {"Look", FontSubstituteMatch::lookalike, ""},
      {"Same", FontSubstituteMatch::metric, ""},
      {"Requested", FontSubstituteMatch::metric, ""},
      {"same", FontSubstituteMatch::lookalike, ""},
  };
  const patchy::FontSubstituteEntry entry{"Requested", {}, FontClass::sans, mixed};
  const auto ranked = rank_font_substitutes(entry, [](std::string_view) { return true; });
  CHECK(ranked.size() == 2);
  CHECK(ranked.size() == 2 && ranked[0].family == "Same" && ranked[1].family == "Look");
}

void font_substitutes_table_is_well_formed() {
  const auto table = patchy::builtin_font_substitutes();
  CHECK(table.size() >= 30U);
  std::set<std::string> keys;
  for (const auto& entry : table) {
    const auto family_key = font_substitute_key(entry.family);
    CHECK(!family_key.empty());
    CHECK(keys.insert(family_key).second);
    for (const auto alias : entry.aliases) {
      const auto alias_key = font_substitute_key(alias);
      CHECK(!alias_key.empty());
      CHECK(alias_key != family_key);
      CHECK(keys.insert(alias_key).second);
    }
    CHECK(entry.font_class != FontClass::unknown);
    // Only a symbol font may list no stand-in: its entry still states its type.
    CHECK(!entry.candidates.empty() || entry.font_class == FontClass::symbol);
    std::set<std::string> candidate_keys;
    for (const auto& candidate : entry.candidates) {
      const auto key = font_substitute_key(candidate.family);
      CHECK(!key.empty());
      CHECK(key != family_key);  // never the font itself
      CHECK(candidate_keys.insert(key).second);
      CHECK(candidate.url.empty() || candidate.url.starts_with("https://"));
    }
    // Every entry can be found by its own name and by each alias.
    CHECK(found_family(entry.family) == entry.family);
    for (const auto alias : entry.aliases) {
      CHECK(found_family(alias) == entry.family);
    }
  }
  // Myriad Pro's substitutes are look-alikes: Source Sans has its own widths.
  const auto myriad = find_font_substitutes("Myriad Pro");
  CHECK(myriad.has_value());
  if (myriad.has_value()) {
    CHECK(std::ranges::all_of(myriad->entry->candidates,
                              [](const auto& candidate) { return candidate.match == FontSubstituteMatch::lookalike; }));
  }
}

void font_substitutes_classify_font_types_from_name_words() {
  using patchy::classify_font_name_words;
  CHECK(classify_font_name_words("PT Serif") == FontClass::serif);
  CHECK(classify_font_name_words("NotoSerifJP-Regular") == FontClass::serif);
  CHECK(classify_font_name_words("Kozuka Mincho") == FontClass::serif);
  CHECK(classify_font_name_words("Roboto Slab") == FontClass::serif);
  CHECK(classify_font_name_words("Open Sans") == FontClass::sans);
  CHECK(classify_font_name_words("Microsoft Sans Serif") == FontClass::sans);  // "sans" wins over "serif"
  CHECK(classify_font_name_words("MS UI Gothic") == FontClass::sans);
  CHECK(classify_font_name_words("Noto Sans Mono") == FontClass::mono);       // "mono" wins over "sans"
  CHECK(classify_font_name_words("Fira Code") == FontClass::mono);
  CHECK(classify_font_name_words("SourceCodePro-Bold") == FontClass::mono);
  CHECK(classify_font_name_words("Lucida Console") == FontClass::mono);
  CHECK(classify_font_name_words("Patchy Missing Serif") == FontClass::serif);
  // Whole words only: "Sansation" and "Monaco" say nothing, nor does a lone style word.
  CHECK(classify_font_name_words("Sansation") == FontClass::unknown);
  CHECK(classify_font_name_words("Monaco") == FontClass::unknown);
  CHECK(classify_font_name_words("Bold") == FontClass::unknown);
  CHECK(classify_font_name_words("") == FontClass::unknown);

  // The table's column wins over the name: "Courier New" is mono, Cambria serif, Impact display.
  CHECK(patchy::font_class_for_name("Cambria") == FontClass::serif);
  CHECK(patchy::font_class_for_name("Impact") == FontClass::display);
  CHECK(patchy::font_class_for_name("Myriad Pro Semibold") == FontClass::sans);
  CHECK(patchy::font_class_for_name("Some Unlisted Mono") == FontClass::mono);
  CHECK(patchy::font_class_for_name("PatchyDefinitelyMissingFont123456") == FontClass::unknown);
}

// GitHub issue 463 follow-up: Wine's symbol fonts and the Windows and URW fonts with the same
// codes stand in for each other; a symbol font never gets a text font of any type.
void font_substitutes_symbol_fonts_pair_only_fonts_with_the_same_codes() {
  const auto ranked_all = [](std::string_view family) {
    std::vector<std::pair<std::string, FontSubstituteMatch>> out;
    const auto found = find_font_substitutes(family);
    if (found.has_value() && found->entry != nullptr) {
      for (const auto& suggestion : rank_font_substitutes(*found->entry, [](std::string_view) { return true; })) {
        out.emplace_back(suggestion.family, suggestion.match);
      }
    }
    return out;
  };
  using Pairs = std::vector<std::pair<std::string, FontSubstituteMatch>>;
  // Wine's copy is saved under its PostScript name; Patchy shows it humanized.
  for (const auto name : {"Wine Wingdings", "WineWingdings", "WineWingdings-Regular"}) {
    CHECK(found_family(name) == "Wine Wingdings");
    CHECK(ranked_all(name) == (Pairs{{"Wingdings", FontSubstituteMatch::metric}}));
  }
  CHECK(ranked_all("WineWebdings") == (Pairs{{"Webdings", FontSubstituteMatch::metric}}));
  CHECK(ranked_all("WineSymbol") == (Pairs{{"Symbol", FontSubstituteMatch::lookalike},
                                           {"Standard Symbols PS", FontSubstituteMatch::lookalike},
                                           {"Standard Symbols L", FontSubstituteMatch::lookalike}}));
  CHECK(ranked_all("SymbolMT") == (Pairs{{"Standard Symbols PS", FontSubstituteMatch::metric},
                                         {"Standard Symbols L", FontSubstituteMatch::metric}}));
  CHECK(ranked_all("ZapfDingbatsITC") ==
        (Pairs{{"D050000L", FontSubstituteMatch::metric}, {"Dingbats", FontSubstituteMatch::metric}}));
  // Wine installs its copies under the Windows family names, so "Wingdings" itself already finds
  // Wine's copy and lists nothing; OpenSymbol (Unicode code points) is never offered for Symbol.
  CHECK(found_family("Wingdings") == "Wingdings");
  CHECK(found_family("Wingdings-Regular") == "Wingdings");
  CHECK(ranked_all("Wingdings").empty());
  CHECK(ranked_all("Webdings").empty());
  for (const auto& [family, match] : ranked_all("Symbol")) {
    CHECK(family != "OpenSymbol");
  }
  // "Wingdings 2" is its own font: not Wingdings, but still a symbol font by name.
  CHECK(!find_font_substitutes("Wingdings 2").has_value());

  for (const auto name : {"Wine Wingdings", "Wingdings", "Wingdings 2", "Wingdings 3", "Webdings", "Symbol",
                          "SymbolMT", "WineSymbol", "Zapf Dingbats", "ITC Zapf Dingbats", "Marlett", "MT Extra",
                          "Patchy Missing Dingbats"}) {
    CHECK(patchy::font_class_for_name(name) == FontClass::symbol);
  }
  // "Symbol" alone is not a symbol word: these are Unicode text fonts.
  CHECK(patchy::font_class_for_name("Segoe UI Symbol") == FontClass::unknown);
  CHECK(patchy::font_class_for_name("Noto Sans Symbols") == FontClass::sans);
  CHECK(patchy::font_class_for_name("Segoe UI Emoji") == FontClass::unknown);
  // No text font is ever a symbol font's same-type default.
  CHECK(patchy::same_class_default_families(FontClass::symbol).empty());
}

void font_substitutes_same_class_defaults_cover_each_platform() {
  const auto serif = patchy::same_class_default_families(FontClass::serif);
  const auto sans = patchy::same_class_default_families(FontClass::sans);
  const auto mono = patchy::same_class_default_families(FontClass::mono);
  CHECK(!serif.empty() && serif.front() == "Times New Roman");
  CHECK(!sans.empty() && sans.front() == "Arial");
  CHECK(!mono.empty() && mono.front() == "Consolas");
  // A Windows, a macOS and a Linux default in each list, and every name of its own type.
  for (const auto list : {serif, sans, mono}) {
    CHECK(std::ranges::find(list, std::string_view("Liberation Serif")) != list.end() ||
          std::ranges::find(list, std::string_view("Liberation Sans")) != list.end() ||
          std::ranges::find(list, std::string_view("Liberation Mono")) != list.end());
  }
  for (const auto family : serif) {
    CHECK(patchy::font_class_for_name(family) == FontClass::serif || patchy::font_class_for_name(family) == FontClass::unknown);
  }
  for (const auto family : mono) {
    CHECK(patchy::font_class_for_name(family) == FontClass::mono || patchy::font_class_for_name(family) == FontClass::unknown);
  }
  CHECK(patchy::same_class_default_families(FontClass::unknown).empty());
  CHECK(patchy::same_class_default_families(FontClass::script).empty());
  CHECK(patchy::same_class_default_families(FontClass::display).empty());
}

// GitHub issue 669: a missing font's name splits into family and face the way Photoshop names it.
void font_substitutes_split_font_names_into_family_and_style() {
  const auto split = [](std::string_view name, std::string_view family, std::string_view style) {
    const auto parts = patchy::split_font_family_and_style(name);
    if (parts.family != family || parts.style != style) {
      std::fprintf(stderr, "  '%.*s' -> '%s' + '%s'\n", static_cast<int>(name.size()), name.data(),
                   parts.family.c_str(), parts.style.c_str());
    }
    CHECK(parts.family == family);
    CHECK(parts.style == style);
  };
  // PostScript names split at the hyphen.
  split("MuseoSansRounded-500", "MuseoSansRounded", "500");
  split("ArialMT-BoldItalic", "ArialMT", "BoldItalic");
  split("  MyriadPro-Semibold ", "MyriadPro", "Semibold");
  // Readable names drop their trailing face words: numeric weights, multi-word faces.
  split("Museo Sans Rounded 500", "Museo Sans Rounded", "500");
  split("Museo 300 Italic", "Museo", "300 Italic");
  split("Museo Sans Rounded 1000", "Museo Sans Rounded", "1000");
  split("Myriad Pro Semi Bold Italic", "Myriad Pro", "Semi Bold Italic");
  split("Open Sans Light Italic", "Open Sans", "Light Italic");
  split("Source Sans Pro Extra Light", "Source Sans Pro", "Extra Light");
  split("Arial Black", "Arial", "Black");
  split("Brightwell Sans Bold", "Brightwell Sans", "Bold");
  // No face part: the family alone.
  split("Brightwell Sans", "Brightwell Sans", "");
  split("Times New Roman", "Times New Roman", "");
  split("Arial Narrow", "Arial Narrow", "");
  split("PatchyDefinitelyMissingFont123456", "PatchyDefinitelyMissingFont123456", "");
  split("Font 3", "Font 3", "");
  split("Extra Bold", "Extra", "Bold");  // one family word always stays
  split("Bold", "Bold", "");
  split("-Bold", "-Bold", "");
  split("", "", "");
}

}  // namespace

std::vector<patchy::test::TestCase> font_substitutes_tests() {
  return {
      {"font_substitutes_split_font_names_into_family_and_style",
       font_substitutes_split_font_names_into_family_and_style},
      {"font_substitutes_classify_font_types_from_name_words", font_substitutes_classify_font_types_from_name_words},
      {"font_substitutes_same_class_defaults_cover_each_platform",
       font_substitutes_same_class_defaults_cover_each_platform},
      {"font_substitutes_lookup_matches_family_and_postscript_names",
       font_substitutes_lookup_matches_family_and_postscript_names},
      {"font_substitutes_face_names_fall_back_to_the_base_family",
       font_substitutes_face_names_fall_back_to_the_base_family},
      {"font_substitutes_rank_metric_before_lookalike_in_table_order",
       font_substitutes_rank_metric_before_lookalike_in_table_order},
      {"font_substitutes_table_is_well_formed", font_substitutes_table_is_well_formed},
      {"font_substitutes_symbol_fonts_pair_only_fonts_with_the_same_codes",
       font_substitutes_symbol_fonts_pair_only_fonts_with_the_same_codes},
  };
}
