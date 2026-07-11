#pragma once

// Cross-platform test font registration (see AGENTS.md "Platform portability").
//
// The offscreen platform does not enumerate installed system fonts on any OS, so tests
// register the font FILES they need with QFontDatabase::addApplicationFont. All such
// registration goes through this header: the Windows candidate lists are the exact
// historical ones (so Windows pixel baselines never move), macOS/Linux list the closest
// stock system equivalents, and a role with no usable candidate on the current machine
// registers nothing -- tests must keep degrading gracefully (skip with a reason, or take
// their substitution path) exactly as they already do when a font file is absent.
//
// Never call QFontDatabase::removeApplicationFont to clean up: invalidating an in-use
// font cache can hard-crash the suite (see AGENTS.md Testing notes).

#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QString>
#include <QStringList>

namespace patchy::test {

enum class TestFontRole {
  UiDefault,       // Arial/Segoe/Calibri-class family used as the suite-wide widget font
  CenturyGothic,   // GOTHIC.TTF on Windows (Century Gothic; PSD fixture text face)
  Verdana,
  ArialBlack,
  Candara,         // restaurant-menu fixture description face (Candara-BoldItalic)
  JapaneseGothic,  // a Japanese-writing-system family (font picker preview tests)
  Wingdings,       // a symbol-writing-system family (font picker preview tests)
};

inline QStringList test_font_candidates(TestFontRole role) {
  switch (role) {
    case TestFontRole::UiDefault:
#if defined(Q_OS_WIN)
      return {
          QStringLiteral("C:/Windows/Fonts/arial.ttf"),
          QStringLiteral("C:/Windows/Fonts/arialbd.ttf"),
          QStringLiteral("C:/Windows/Fonts/ariali.ttf"),
          QStringLiteral("C:/Windows/Fonts/arialbi.ttf"),
          QStringLiteral("C:/Windows/Fonts/segoeui.ttf"),
          QStringLiteral("C:/Windows/Fonts/segoeuib.ttf"),
          QStringLiteral("C:/Windows/Fonts/segoeuii.ttf"),
          QStringLiteral("C:/Windows/Fonts/segoeuiz.ttf"),
          QStringLiteral("C:/Windows/Fonts/calibri.ttf"),
          QStringLiteral("C:/Windows/Fonts/calibrib.ttf"),
          QStringLiteral("C:/Windows/Fonts/calibrii.ttf"),
          QStringLiteral("C:/Windows/Fonts/calibriz.ttf"),
      };
#elif defined(Q_OS_MACOS)
      return {
          QStringLiteral("/System/Library/Fonts/Supplemental/Arial.ttf"),
          QStringLiteral("/System/Library/Fonts/Supplemental/Arial Bold.ttf"),
          QStringLiteral("/System/Library/Fonts/Supplemental/Arial Italic.ttf"),
          QStringLiteral("/System/Library/Fonts/Supplemental/Arial Bold Italic.ttf"),
      };
#else
      return {
          QStringLiteral("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"),
          QStringLiteral("/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"),
          QStringLiteral("/usr/share/fonts/truetype/liberation/LiberationSans-Italic.ttf"),
          QStringLiteral("/usr/share/fonts/truetype/liberation/LiberationSans-BoldItalic.ttf"),
          QStringLiteral("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
          QStringLiteral("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"),
      };
#endif
    case TestFontRole::CenturyGothic:
#if defined(Q_OS_WIN)
      return {
          QStringLiteral("C:/Windows/Fonts/GOTHIC.TTF"),
          QStringLiteral("C:/Windows/Fonts/GOTHICB.TTF"),
      };
#else
      return {};  // no stock equivalent; tests take their substitution path
#endif
    case TestFontRole::Verdana:
#if defined(Q_OS_WIN)
      return {
          QStringLiteral("C:/Windows/Fonts/verdana.ttf"),
          QStringLiteral("C:/Windows/Fonts/verdanab.ttf"),
          QStringLiteral("C:/Windows/Fonts/verdanai.ttf"),
          QStringLiteral("C:/Windows/Fonts/verdanaz.ttf"),
      };
#elif defined(Q_OS_MACOS)
      return {
          QStringLiteral("/System/Library/Fonts/Supplemental/Verdana.ttf"),
          QStringLiteral("/System/Library/Fonts/Supplemental/Verdana Bold.ttf"),
          QStringLiteral("/System/Library/Fonts/Supplemental/Verdana Italic.ttf"),
          QStringLiteral("/System/Library/Fonts/Supplemental/Verdana Bold Italic.ttf"),
      };
#else
      return {};  // DejaVu is metric-compatible but registers under its own family name
#endif
    case TestFontRole::ArialBlack:
#if defined(Q_OS_WIN)
      return {QStringLiteral("C:/Windows/Fonts/ariblk.ttf")};
#elif defined(Q_OS_MACOS)
      return {QStringLiteral("/System/Library/Fonts/Supplemental/Arial Black.ttf")};
#else
      return {};
#endif
    case TestFontRole::Candara:
#if defined(Q_OS_WIN)
      return {
          QStringLiteral("C:/Windows/Fonts/Candara.ttf"),
          QStringLiteral("C:/Windows/Fonts/Candarab.ttf"),
          QStringLiteral("C:/Windows/Fonts/Candarai.ttf"),
          QStringLiteral("C:/Windows/Fonts/Candaraz.ttf"),
      };
#else
      return {};  // no stock equivalent; tests take their substitution path
#endif
    case TestFontRole::JapaneseGothic:
#if defined(Q_OS_WIN)
      return {
          QStringLiteral("C:/Windows/Fonts/msgothic.ttc"),
          QStringLiteral("C:/Windows/Fonts/YuGothM.ttc"),
      };
#elif defined(Q_OS_LINUX)
      return {QStringLiteral("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc")};
#else
      return {};  // macOS ships Hiragino under non-ASCII paths; tests skip with a reason
#endif
    case TestFontRole::Wingdings:
#if defined(Q_OS_WIN)
      return {QStringLiteral("C:/Windows/Fonts/wingding.ttf")};
#elif defined(Q_OS_MACOS)
      return {QStringLiteral("/System/Library/Fonts/Supplemental/Wingdings.ttf")};
#else
      return {};
#endif
  }
  return {};
}

// Registers every candidate file that exists for the role. Callers keep their own
// semantic availability checks (family present, style present) and skip paths.
inline void register_test_fonts(TestFontRole role) {
  for (const auto& path : test_font_candidates(role)) {
    if (QFileInfo::exists(path)) {
      (void)QFontDatabase::addApplicationFont(path);
    }
  }
}

// The suite-wide widget font: an Arial-class family at a fixed point size so text
// metrics stay deterministic within a platform. Mirrors the app's font bootstrap but
// forces the size on every platform (tests want stable layout, not native sizing).
inline QFont visual_test_font(int point_size = 9) {
#if defined(Q_OS_LINUX)
  const QStringList preferred_families = {QStringLiteral("Liberation Sans"), QStringLiteral("DejaVu Sans")};
#else
  const QStringList preferred_families = {QStringLiteral("Arial")};
#endif
  QString preferred_family;
  QString first_family;
  for (const auto& path : test_font_candidates(TestFontRole::UiDefault)) {
    if (!QFileInfo::exists(path)) {
      continue;
    }
    const auto font_id = QFontDatabase::addApplicationFont(path);
    const auto families = QFontDatabase::applicationFontFamilies(font_id);
    if (first_family.isEmpty() && !families.isEmpty()) {
      first_family = families.front();
    }
    for (const auto& wanted : preferred_families) {
      if (families.contains(wanted) && (preferred_family.isEmpty() || wanted == preferred_families.front())) {
        preferred_family = wanted;
        break;
      }
    }
  }
  if (preferred_family.isEmpty()) {
    preferred_family = first_family;
  }
  if (!preferred_family.isEmpty()) {
    QFont font(preferred_family);
    font.setPointSize(point_size);
    return font;
  }

  auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
  font.setPointSize(point_size);
  return font;
}

}  // namespace patchy::test
