#include "ui/status_document_info.hpp"

#include "core/document.hpp"
#include "formats/metadata/metadata_view.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QLocale>

#include <algorithm>
#include <initializer_list>
#include <span>

namespace patchy::ui {

namespace {

// Up to two decimals, trailing zeros dropped, in the UI locale without grouping:
// 72, 28.35, 1200.
QString short_number(double value) {
  QLocale locale;
  locale.setNumberOptions(QLocale::OmitGroupSeparator);
  auto text = locale.toString(value, 'f', 2);
  const auto point = text.lastIndexOf(locale.decimalPoint());
  if (point >= 0) {
    while (text.endsWith(QLatin1Char('0'))) {
      text.chop(1);
    }
    if (text.size() == point + locale.decimalPoint().size()) {
      text.truncate(point);
    }
  }
  return text;
}

void add_layer_bytes(const Layer& layer, std::uint64_t& total) {
  total += layer.pixels().byte_size();
  if (const auto& mask = layer.mask(); mask.has_value()) {
    total += mask->pixels.byte_size();
  }
  for (const auto& child : layer.children()) {
    add_layer_bytes(child, total);
  }
}

}  // namespace

std::uint64_t document_layer_bytes(const Document& document) {
  std::uint64_t total = 0;
  for (const auto& layer : document.layers()) {
    add_layer_bytes(layer, total);
  }
  for (const auto& channel : document.channels()) {
    total += channel.pixels().byte_size();
  }
  return total;
}

StatusDocumentInfoLabel::StatusDocumentInfoLabel(QWidget* parent) : QLabel(parent) {
  setObjectName(QStringLiteral("statusDocumentInfoLabel"));
  setTextFormat(Qt::PlainText);
  setVisible(false);  // until there is a document to describe
}

QString StatusDocumentInfoLabel::separator() { return QStringLiteral(" · "); }

StatusDocumentInfoLabel::Info StatusDocumentInfoLabel::describe(const Document& document,
                                                                const ColorContext& color) {
  const auto& print = document.print_settings();
  // The resolution in the document's own display unit (PSD resource 1005 keeps
  // px/cm; docs/resolution-units.md), and both axes when they differ.
  const bool per_centimeter = print.horizontal_resolution_display_unit == 2;
  const auto shown = [per_centimeter](double ppi) { return short_number(per_centimeter ? ppi / 2.54 : ppi); };
  const auto horizontal = shown(print.horizontal_ppi);
  const auto vertical = shown(print.vertical_ppi);
  QString resolution;
  if (horizontal == vertical) {
    resolution = per_centimeter ? tr("%1 px/cm").arg(horizontal) : tr("%1 ppi").arg(horizontal);
  } else {
    resolution = per_centimeter ? tr("%1 × %2 px/cm").arg(horizontal, vertical)
                                : tr("%1 × %2 ppi").arg(horizontal, vertical);
  }

  const bool indexed = document.palette_editing().has_value();
  const bool cmyk_proof = color.cmyk_proof && !indexed;
  const auto mode = indexed ? tr("Indexed") : cmyk_proof ? tr("CMYK Proof") : tr("RGB");
  QString depth;
  switch (document.color_state().bit_depth) {
    case BitDepth::UInt8:
      depth = tr("8-bit");
      break;
    case BitDepth::UInt16:
      depth = tr("16-bit");
      break;
    case BitDepth::Float32:
      depth = tr("32-bit float");
      break;
  }

  Info info;
  info.parts << tr("%1 × %2 px").arg(document.width()).arg(document.height()) << resolution
             << tr("%1 %2", "status bar: color mode, then bit depth").arg(mode, depth);

  // CMYK Proof shows and saves through the working CMYK profile; otherwise the
  // profile is the one a save embeds (the File Info description).
  QString profile;
  if (cmyk_proof) {
    profile = color.cmyk_profile_name;
  } else {
    const auto& icc = document.color_state().embedded_icc_profile;
    if (const auto summary = metadata::summarize_icc(std::span<const std::uint8_t>(icc.data(), icc.size()));
        summary.has_value()) {
      profile = QString::fromStdString(summary->description).trimmed();
    }
  }
  if (profile.isEmpty()) {
    profile = tr("None");
  }
  const auto memory = QLocale().formattedDataSize(static_cast<qint64>(document_layer_bytes(document)), 1,
                                                  QLocale::DataSizeTraditionalFormat);
  info.tooltip = QStringList{info.parts.join(separator()), tr("Color profile: %1").arg(profile),
                             tr("Memory: %1").arg(memory)}
                     .join(QLatin1Char('\n'));
  return info;
}

void StatusDocumentInfoLabel::set_info(const Info& info) {
  if (info.parts == parts_ && info.tooltip == toolTip() && isHidden() == info.parts.isEmpty()) {
    return;
  }
  parts_ = info.parts;
  setToolTip(info.tooltip);
  update_shown_text();
  apply_visibility(!parts_.isEmpty());
}

void StatusDocumentInfoLabel::set_separators(QWidget* before, QWidget* after) {
  separator_before_ = before;
  separator_after_ = after;
  apply_visibility(!isHidden());
}

void StatusDocumentInfoLabel::apply_visibility(bool shown) {
  setVisible(shown);
  for (auto* separator : {separator_before_, separator_after_}) {
    if (separator != nullptr) {
      separator->setVisible(shown);
    }
  }
}

int StatusDocumentInfoLabel::reserved_text_width() const {
  // The same strings describe() shows, so a translation sizes the field too.
  const auto sample =
      QStringList{tr("%1 × %2 px").arg(10000).arg(10000), tr("%1 ppi").arg(300),
                  tr("%1 %2", "status bar: color mode, then bit depth").arg(tr("CMYK Proof"), tr("16-bit"))}
          .join(separator());
  return fontMetrics().horizontalAdvance(sample);
}

void StatusDocumentInfoLabel::changeEvent(QEvent* event) {
  QLabel::changeEvent(event);
  if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
    update_shown_text();
  }
}

void StatusDocumentInfoLabel::set_width_budget(int pixels) {
  if (pixels == width_budget_) {
    return;
  }
  width_budget_ = pixels;
  update_shown_text();
}

void StatusDocumentInfoLabel::update_shown_text() {
  if (parts_.isEmpty()) {
    setText(QString());
    return;
  }
  ensurePolished();  // the status-bar style sets the font and padding the fit measures with
  const auto metrics = fontMetrics();
  // One width for every document (GitHub issue 206), so the message after the field
  // holds still across tab switches; only a narrow bar makes it smaller.
  const auto padding = rect().width() - contentsRect().width();
  const auto reserved = reserved_text_width();
  const auto budget = width_budget_ > 0 ? std::max(0, width_budget_ - padding) : reserved;
  const auto narrowed = budget < reserved;
  const auto room = std::min(budget, reserved);
  // Dimensions always show; each further part only when it fits whole. With the full
  // width to spare, a rare longer part is elided instead (the tooltip has it all).
  auto text = parts_.front();
  for (qsizetype index = 1; index < parts_.size(); ++index) {
    const auto longer = text + separator() + parts_[index];
    if (metrics.horizontalAdvance(longer) > room) {
      if (!narrowed) {
        text = metrics.elidedText(longer, Qt::ElideRight, room);
      }
      break;
    }
    text = longer;
  }
  const auto width = std::max(room, metrics.horizontalAdvance(text)) + padding;
  if (width != this->width() || minimumWidth() != maximumWidth()) {
    setFixedWidth(width);
  }
  if (text != this->text()) {
    setText(text);
  }
}

StatusSelectionInfoLabel::StatusSelectionInfoLabel(QWidget* parent) : QLabel(parent) {
  setObjectName(QStringLiteral("statusSelectionInfoLabel"));
  setTextFormat(Qt::PlainText);
  setVisible(false);  // until there is a selection
}

QString StatusSelectionInfoLabel::describe(QRect bounds, MeasurementUnit unit, QSize document_size,
                                           double horizontal_ppi, double vertical_ppi) {
  const auto decimals = measurement_unit_decimals(unit);
  const auto horizontal = [&](int pixels) {
    return pixels_to_measurement_unit(pixels, unit, sanitized_document_ppi(horizontal_ppi),
                                      std::max(document_size.width(), 1));
  };
  const auto vertical = [&](int pixels) {
    return pixels_to_measurement_unit(pixels, unit, sanitized_document_ppi(vertical_ppi),
                                      std::max(document_size.height(), 1));
  };
  //: Status bar selection bounds: left, top, width, then height (which carries the unit).
  return tr("X: %1  Y: %2  W: %3  H: %4")
      .arg(format_readout_number(horizontal(bounds.x()), decimals),
           format_readout_number(vertical(bounds.y()), decimals),
           format_readout_number(horizontal(bounds.width()), decimals),
           format_measurement(vertical(bounds.height()), unit, decimals));
}

QString StatusSelectionInfoLabel::tooltip_text() {
  return tr("Selection bounds: left edge (X), top edge (Y), width (W) and height (H), in the ruler unit");
}

void StatusSelectionInfoLabel::set_readout(const QString& text) {
  if (text.isEmpty()) {
    if (!isHidden()) {
      setVisible(false);
      setText(QString());
      setMinimumWidth(0);
    }
    return;
  }
  if (text != this->text()) {
    setText(text);
  }
  ensurePolished();  // the status-bar style sets the font and padding the width needs
  if (const auto needed = sizeHint().width(); needed > minimumWidth()) {
    setMinimumWidth(needed);
  }
  if (isHidden()) {
    setVisible(true);
  }
}

}  // namespace patchy::ui
