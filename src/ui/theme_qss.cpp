#include "ui/theme_qss.hpp"

#include "ui/icon_theme.hpp"

#include <QHash>
#include <QLabel>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QVariant>
#include <QWidget>
#include <QtGlobal>

namespace patchy::ui {

namespace {

// '@' is a safe sigil: Qt QSS has no at-rules, and no literal in any Patchy
// stylesheet contains one (the url() values are all resource paths).
const QRegularExpression& token_pattern() {
  static const QRegularExpression pattern(QStringLiteral("@([A-Za-z_][A-Za-z0-9_]*)"));
  return pattern;
}

// url(@icon(name)) picks the light variant of a stylesheet-referenced SVG when
// one exists. QStyleSheetStyle loads these through QPixmap rather than a
// QIconEngine, so they cannot be recolored the way the icon set is; the scheme
// has to choose a different file. Resolved before the color pass so an icon name
// can never be mistaken for a role.
const QRegularExpression& icon_token_pattern() {
  static const QRegularExpression pattern(QStringLiteral("@icon\\(([A-Za-z0-9_-]+)\\)"));
  return pattern;
}

const QHash<QString, QColor ThemePalette::*>& role_members() {
  static const auto members = [] {
    QHash<QString, QColor ThemePalette::*> table;
    for (const auto& [name, member] : theme_palette_roles()) {
      table.insert(QString(name), member);
    }
    return table;
  }();
  return members;
}

}  // namespace

QString apply_theme_tokens(const QString& qss, const ThemePalette& palette) {
  const auto& members = role_members();
  QString resolved;
  resolved.reserve(qss.size());

  // Icon paths first. These follow the active scheme rather than `palette`,
  // which is harmless because the only caller that passes a palette explicitly
  // passes the active one.
  QString with_icons = qss;
  {
    QString rewritten;
    rewritten.reserve(qss.size());
    qsizetype copied = 0;
    auto icons = icon_token_pattern().globalMatch(qss);
    while (icons.hasNext()) {
      const auto match = icons.next();
      rewritten.append(QStringView(qss).sliced(copied, match.capturedStart() - copied));
      rewritten.append(themed_icon_url(match.captured(1)));
      copied = match.capturedEnd();
    }
    rewritten.append(QStringView(qss).sliced(copied));
    with_icons = rewritten;
  }
  const QString& source = with_icons;

  qsizetype copied = 0;
  auto matches = token_pattern().globalMatch(source);
  while (matches.hasNext()) {
    const auto match = matches.next();
    const auto name = match.captured(1);
    const auto member = members.constFind(name);
    if (member == members.constEnd()) {
      // Leave the token in place so the survivor test can name it.
      qWarning("theme_qss: unknown color role \"%s\"", qPrintable(name));
      Q_ASSERT_X(false, "apply_theme_tokens", qPrintable(name));
      continue;
    }
    resolved.append(QStringView(source).sliced(copied, match.capturedStart() - copied));
    resolved.append((palette.**member).name(QColor::HexRgb));
    copied = match.capturedEnd();
  }
  resolved.append(QStringView(source).sliced(copied));
  return resolved;
}

QString apply_theme_tokens(const QString& qss) { return apply_theme_tokens(qss, theme()); }

void append_themed_style(QWidget& widget, const QString& qss_template) {
  const auto stored = widget.property(kThemedStyleTemplateProperty);
  // Seed from a sheet set directly before the first themed append, so literal
  // rules from elsewhere survive.
  const auto base = stored.isValid() ? stored.toString() : widget.styleSheet();
  set_themed_style(widget, base + qss_template);
}

ThemedQss themed_style_template(const QWidget& widget) {
  return ThemedQss(widget.property(kThemedStyleTemplateProperty).toString());
}

void set_themed_style(QWidget& widget, const QString& qss_template) {
  widget.setProperty(kThemedStyleTemplateProperty, qss_template);
  widget.setStyleSheet(apply_theme_tokens(qss_template));
}

void set_themed_label_text(QLabel& label, const QString& text_template) {
  label.setProperty(kThemedTextTemplateProperty, text_template);
  label.setText(apply_theme_tokens(text_template));
}

void rebuild_themed_style(QWidget& widget) {
  if (const auto stored = widget.property(kThemedStyleTemplateProperty); stored.isValid()) {
    widget.setStyleSheet(apply_theme_tokens(stored.toString()));
  }
  if (const auto text = widget.property(kThemedTextTemplateProperty); text.isValid()) {
    if (auto* label = qobject_cast<QLabel*>(&widget); label != nullptr) {
      label->setText(apply_theme_tokens(text.toString()));
    }
  }
}

void rebuild_themed_styles_in(QWidget& root) {
  rebuild_themed_style(root);
  for (auto* child : root.findChildren<QWidget*>()) {
    if (child != nullptr) {
      rebuild_themed_style(*child);
    }
  }
}

}  // namespace patchy::ui
