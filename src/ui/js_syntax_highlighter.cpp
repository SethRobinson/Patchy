#include "ui/js_syntax_highlighter.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_manager.hpp"

namespace patchy::ui {

JsSyntaxHighlighter::JsSyntaxHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document) {
  rebuild_formats();
  connect(&ThemeManager::instance(), &ThemeManager::color_scheme_changed, this, [this] {
    rebuild_formats();
    rehighlight();
  });
}

void JsSyntaxHighlighter::rebuild_formats() {
  rules_.clear();
  QTextCharFormat keyword_format;
  keyword_format.setForeground(theme().script_keyword);
  const QStringList keywords = {
      QStringLiteral("break"),    QStringLiteral("case"),     QStringLiteral("catch"),
      QStringLiteral("class"),    QStringLiteral("const"),    QStringLiteral("continue"),
      QStringLiteral("default"),  QStringLiteral("delete"),   QStringLiteral("do"),
      QStringLiteral("else"),     QStringLiteral("finally"),  QStringLiteral("for"),
      QStringLiteral("function"), QStringLiteral("if"),       QStringLiteral("in"),
      QStringLiteral("instanceof"), QStringLiteral("let"),    QStringLiteral("new"),
      QStringLiteral("of"),       QStringLiteral("return"),   QStringLiteral("switch"),
      QStringLiteral("this"),     QStringLiteral("throw"),    QStringLiteral("try"),
      QStringLiteral("typeof"),   QStringLiteral("var"),      QStringLiteral("void"),
      QStringLiteral("while"),    QStringLiteral("yield")};
  for (const auto& keyword : keywords) {
    rules_.push_back({QRegularExpression(QStringLiteral("\\b%1\\b").arg(keyword)), keyword_format});
  }

  QTextCharFormat literal_format;
  literal_format.setForeground(theme().script_literal);
  rules_.push_back(
      {QRegularExpression(QStringLiteral("\\b(true|false|null|undefined|NaN|Infinity)\\b")),
       literal_format});

  QTextCharFormat builtin_format;
  builtin_format.setForeground(theme().script_builtin);
  rules_.push_back(
      {QRegularExpression(QStringLiteral("\\b(app|patchy|console|Math|JSON|include)\\b")),
       builtin_format});

  QTextCharFormat number_format;
  number_format.setForeground(theme().script_number);
  rules_.push_back(
      {QRegularExpression(QStringLiteral("\\b(0[xX][0-9a-fA-F]+|\\d+(\\.\\d+)?([eE][+-]?\\d+)?)\\b")),
       number_format});

  QTextCharFormat string_format;
  string_format.setForeground(theme().script_string);
  rules_.push_back({QRegularExpression(QStringLiteral("\"[^\"\\n]*\"")), string_format});
  rules_.push_back({QRegularExpression(QStringLiteral("'[^'\\n]*'")), string_format});
  rules_.push_back({QRegularExpression(QStringLiteral("`[^`\\n]*`")), string_format});

  comment_format_.setForeground(theme().script_comment);
  rules_.push_back({QRegularExpression(QStringLiteral("//[^\\n]*")), comment_format_});
  comment_start_ = QRegularExpression(QStringLiteral("/\\*"));
  comment_end_ = QRegularExpression(QStringLiteral("\\*/"));
}

void JsSyntaxHighlighter::highlightBlock(const QString& text) {
  for (const auto& rule : rules_) {
    auto matches = rule.pattern.globalMatch(text);
    while (matches.hasNext()) {
      const auto match = matches.next();
      setFormat(static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength()),
                rule.format);
    }
  }

  // Multi-line /* comments */ via block state 1.
  setCurrentBlockState(0);
  int start_index = 0;
  if (previousBlockState() != 1) {
    start_index = static_cast<int>(text.indexOf(comment_start_));
  }
  while (start_index >= 0) {
    const auto end_match = comment_end_.match(text, start_index);
    int length = 0;
    if (!end_match.hasMatch()) {
      setCurrentBlockState(1);
      length = static_cast<int>(text.length()) - start_index;
    } else {
      length = static_cast<int>(end_match.capturedEnd()) - start_index;
    }
    setFormat(start_index, length, comment_format_);
    start_index = static_cast<int>(text.indexOf(comment_start_, start_index + length));
  }
}

}  // namespace patchy::ui
