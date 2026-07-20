#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <vector>

namespace patchy::ui {

// JavaScript highlighting for the Script Manager: keywords, literals, numbers,
// strings, and comments (multi-line via block state 1), tuned for the dark
// theme. Purely lexical; good enough for an embedded editor pane.
class JsSyntaxHighlighter : public QSyntaxHighlighter {
  Q_OBJECT

public:
  explicit JsSyntaxHighlighter(QTextDocument* document);

protected:
  void highlightBlock(const QString& text) override;

private:
  struct Rule {
    QRegularExpression pattern;
    QTextCharFormat format;
  };

  std::vector<Rule> rules_;
  QRegularExpression comment_start_;
  QRegularExpression comment_end_;
  QTextCharFormat comment_format_;
};

}  // namespace patchy::ui
