#include "ui/ui_text_size.hpp"

#include "ui/app_settings.hpp"

#include <QEvent>
#include <QFontInfo>
#include <QLabel>
#include <QObject>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QStringView>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace patchy::ui {

namespace {

class WrappedLabelHeight final : public QObject {
 public:
  explicit WrappedLabelHeight(QLabel& label) : QObject(&label), label_(label) {}

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (watched == &label_ && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
      const int needed = label_.heightForWidth(label_.width());
      if (needed > 0 && needed != label_.minimumHeight()) {
        label_.setMinimumHeight(needed);
      }
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  QLabel& label_;
};

std::atomic<int>& text_size_percent_storage() {
  static std::atomic<int> percent{kDefaultUiTextSizePercent};
  return percent;
}

}  // namespace

int ui_text_size_percent() { return text_size_percent_storage().load(std::memory_order_relaxed); }

double ui_text_size_factor() { return ui_text_size_percent() / 100.0; }

void set_ui_text_size_percent(int percent) {
  text_size_percent_storage().store(normalize_ui_text_size_percent(percent), std::memory_order_relaxed);
}

int ui_text_px(int px) {
  const int percent = ui_text_size_percent();
  if (percent == 100 || px <= 0) {
    return px;
  }
  return std::max(1, static_cast<int>(std::lround(px * percent / 100.0)));
}

int ui_text_length(int px) { return std::max(px, ui_text_px(px)); }

double ui_text_pt(double points) {
  const int percent = ui_text_size_percent();
  return percent == 100 ? points : points * percent / 100.0;
}

QFont ui_text_scaled_font(QFont font) {
  if (ui_text_size_percent() == 100) {
    return font;
  }
  // In whole pixels, rounded as the sheets' font sizes are: a point size scaled to 9.6 px
  // against a sheet's 10 px measured every widget a little short before it was polished.
  const int pixels = font.pixelSize() > 0 ? font.pixelSize() : QFontInfo(font).pixelSize();
  if (pixels > 0) {
    font.setPixelSize(ui_text_px(pixels));
  } else if (font.pointSizeF() > 0.0) {
    font.setPointSizeF(ui_text_pt(font.pointSizeF()));
  }
  return font;
}

void keep_wrapped_label_height(QLabel& label) {
  if (ui_text_size_percent() == 100) {
    return;
  }
  label.installEventFilter(new WrappedLabelHeight(label));
}

QString resolve_ui_text_sizes(const QString& qss) {
  if (!qss.contains(QLatin1String("font-size")) && !qss.contains(QLatin1String("@tx("))) {
    return qss;
  }
  static const QRegularExpression pattern(
      QStringLiteral(R"((font-size:\s*)(\d+)px(\s*/\*\s*scaled\s*\*/)?|@tx\((\d+)\))"));
  QString resolved;
  resolved.reserve(qss.size());
  qsizetype copied = 0;
  for (auto it = pattern.globalMatch(qss); it.hasNext();) {
    const auto match = it.next();
    resolved += QStringView(qss).mid(copied, match.capturedStart() - copied);
    if (match.capturedLength(4) > 0) {
      resolved += QString::number(ui_text_length(match.captured(4).toInt()));
    } else if (match.capturedLength(3) > 0) {
      resolved += match.captured(1) + match.captured(2) + QStringLiteral("px");
    } else {
      resolved += match.captured(1) + QString::number(ui_text_px(match.captured(2).toInt())) +
                  QStringLiteral("px");
    }
    copied = match.capturedEnd();
  }
  resolved += QStringView(qss).mid(copied);
  return resolved;
}

}  // namespace patchy::ui
