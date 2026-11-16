#pragma once

// PDF fixtures the PDF viewer's test files share (defined in pdf_viewer_tests.cpp, built
// only with Qt PDF).

#include <QByteArray>

#include <string>
#include <vector>

namespace patchy::test::ui::pdf_viewer_support {

inline constexpr double kTextPageWidth = 300.0;
inline constexpr double kTextPageHeight = 400.0;

// Pages of Helvetica text, one Tj per line (so PDFium sees the line breaks), WinAnsi encoded:
// write a character past ASCII as its octal escape. A page without lines has no text at all.
QByteArray text_pdf_bytes(const std::vector<std::vector<std::string>>& pages, double width = kTextPageWidth,
                          double height = kTextPageHeight, double font_size = 14.0, double left = 30.0,
                          double top_margin = 40.0);

// Five small pages: three lines about a fox, two more, a page that breaks words and a phrase
// across lines ("exam-" / "ple", "a well-" / "known"), a page without text, a last page.
QByteArray find_fixture_pdf_bytes();

// Six US Letter pages of ordinary prose (the desk's pdf-find-sample.pdf).
QByteArray find_sample_pdf_bytes();

}  // namespace patchy::test::ui::pdf_viewer_support
