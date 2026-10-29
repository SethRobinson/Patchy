#include "ui/text_document.hpp"
#include "ui/text_font_service.hpp"
#include "ui/text_layout.hpp"
#include "ui/text_area_layout.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/edit_conversions.hpp"
#include "core/pixel_grid.hpp"
#include "core/layer_metadata.hpp"
#include "core/text_area.hpp"
#include "psd/psd_text_runs.hpp"
#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QBrush>
#include <QByteArray>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRawFont>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QTextOption>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace patchy::ui {
void scale_font_width(QFont& font, double scale);
void scale_document_font_widths(QTextDocument& document, double scale);
QString normalized_rich_text_html(const QTextDocument& source);
void apply_plain_text_format(QTextDocument& document, const QFont& font, QColor color);
void resolve_document_font_styles(QTextDocument& document);
void apply_faux_bold_to_document(QTextDocument& document);

bool is_text_runs_version_line(const QString& line) {
  if (line.size() < 2 || line.front() != QLatin1Char('v')) {
    return false;
  }
  return std::all_of(line.begin() + 1, line.end(), [](QChar ch) { return ch >= QLatin1Char('0') && ch <= QLatin1Char('9'); });
}

namespace {

psd::PsdTextStyleRun psd_run_with_styles(const TextCharacterStyles& styles) {
  psd::PsdTextStyleRun run;
  run.font_caps = styles.caps;
  run.font_baseline = styles.baseline;
  run.underline = styles.underline;
  run.strikethrough = styles.strikethrough;
  run.ordinals = styles.ordinals;
  run.fractions = styles.fractions;
  return run;
}

int format_choice(const QTextCharFormat& format, int property_id) {
  const auto value = format.property(property_id).toInt();
  return value == 1 || value == 2 ? value : 0;
}

}  // namespace

TextCharacterStyles text_character_styles_from_format(const QTextCharFormat& format) {
  TextCharacterStyles styles;
  styles.caps = format_choice(format, kTextFontCapsFormatProperty);
  styles.baseline = format_choice(format, kTextFontBaselineFormatProperty);
  styles.underline = format.property(kTextUnderlineFormatProperty).toBool();
  styles.strikethrough = format.property(kTextStrikethroughFormatProperty).toBool();
  styles.ordinals = format.property(kTextOrdinalsFormatProperty).toBool();
  styles.fractions = format.property(kTextFractionsFormatProperty).toBool();
  return styles;
}

void set_text_character_styles_on_format(QTextCharFormat& format, const TextCharacterStyles& styles) {
  // Off is written as an explicit 0/false rather than cleared, so a mergeCharFormat of the
  // change reaches runs that had the style on.
  format.setProperty(kTextFontCapsFormatProperty, styles.caps == 1 || styles.caps == 2 ? styles.caps : 0);
  format.setProperty(kTextFontBaselineFormatProperty,
                     styles.baseline == 1 || styles.baseline == 2 ? styles.baseline : 0);
  format.setProperty(kTextUnderlineFormatProperty, styles.underline);
  format.setProperty(kTextStrikethroughFormatProperty, styles.strikethrough);
  format.setProperty(kTextOrdinalsFormatProperty, styles.ordinals);
  format.setProperty(kTextFractionsFormatProperty, styles.fractions);
}

QString serialize_text_character_styles(const TextCharacterStyles& styles) {
  return QString::fromStdString(psd::serialize_text_run_character_styles(psd_run_with_styles(styles)));
}

TextCharacterStyles parse_text_character_styles(const QString& field) {
  psd::PsdTextStyleRun run;
  psd::parse_text_run_character_styles(field.trimmed().toStdString(), run);
  TextCharacterStyles styles;
  styles.caps = run.font_caps;
  styles.baseline = run.font_baseline;
  styles.underline = run.underline;
  styles.strikethrough = run.strikethrough;
  styles.ordinals = run.ordinals;
  styles.fractions = run.fractions;
  return styles;
}

void set_text_display_family(QTextCharFormat& format, const QString& family) {
  format.setProperty(kTextDisplayFamilyFormatProperty, canonical_text_display_family(family));
}

QString text_display_family_from_format(const QTextCharFormat& format, const QString& fallback) {
  const auto stored = format.property(kTextDisplayFamilyFormatProperty).toString().trimmed();
  if (!stored.isEmpty()) {
    return stored;
  }
  const auto families = format.font().families();
  if (families.size() >= 2 && text_family_uses_photoshop_latin_fallback(families.at(1))) {
    return families.at(1);
  }
  const auto family = format.font().family().trimmed();
  if (!family.isEmpty()) {
    return family;
  }
  return fallback.trimmed().isEmpty() ? default_text_family() : fallback.trimmed();
}

QString paragraph_alignment_name(Qt::Alignment alignment) {
  if ((alignment & Qt::AlignHCenter) != 0) {
    return QStringLiteral("center");
  }
  if ((alignment & Qt::AlignRight) != 0) {
    return QStringLiteral("right");
  }
  if ((alignment & Qt::AlignJustify) != 0) {
    return QStringLiteral("justify");
  }
  return QStringLiteral("left");
}

Qt::Alignment paragraph_alignment_from_name(const QString& name) {
  if (name.compare(QStringLiteral("center"), Qt::CaseInsensitive) == 0) {
    return Qt::AlignHCenter;
  }
  if (name.compare(QStringLiteral("right"), Qt::CaseInsensitive) == 0) {
    return Qt::AlignRight;
  }
  if (name.compare(QStringLiteral("justify"), Qt::CaseInsensitive) == 0) {
    return Qt::AlignJustify;
  }
  return Qt::AlignLeft;
}

QTextCharFormat text_format_with_smoothing(QTextCharFormat format, int anti_alias) {
  auto font = format.font();
  configure_text_font_smoothing(font, anti_alias);
  format.setFont(font);
  return format;
}

void apply_text_smoothing_to_document(QTextDocument& document, int anti_alias) {
  auto default_font = document.defaultFont();
  configure_text_font_smoothing(default_font, anti_alias);
  document.setDefaultFont(default_font);

  struct FormatRange {
    int position{0};
    int length{0};
    QTextCharFormat format;
  };
  std::vector<FormatRange> ranges;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      ranges.push_back(FormatRange{fragment.position(), fragment.length(),
                                    text_format_with_smoothing(fragment.charFormat(), anti_alias)});
    }
  }

  for (const auto& range : ranges) {
    QTextCursor cursor(&document);
    cursor.setPosition(range.position);
    cursor.setPosition(range.position + range.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(range.format);
  }
}

// Sets the stretch whose RENDERED advances scale by `ratio` of the unstretched face.
// `naive_value` is the caller's historical round(ratio * 100) encoding, computed by the
// caller so each site's arithmetic stays exactly what it always was. That value is correct
// on DirectWrite/GDI and FreeType, whose advances scale linearly with the stored stretch,
// but Qt's CoreText engine applies a synthesized stretch to advances TWICE -- once baked
// into the CTFont matrix and once as an explicit multiply on the already-scaled advances
// (qfontengine_coretext.mm, loadAdvancesForGlyphs, Qt 6.8) -- so stretch 90 rendered at 81%
// and the SNES box blurb lost 10% of its line length against Photoshop's raster on macOS.
// The sqrt encoding lands the requested ratio on that engine. Which one is right is decided
// by MEASURING this font, not by platform or by a per-process probe: fonts with a native
// width axis answer setStretch through real width matching (.AppleSystemUIFont measured
// x0.39 at stretch 50, neither linear nor squared, and a per-process probe cached off it),
// and a missing family's fallback engine ignores stretch entirely. On a linear engine the
// naive candidate measures ~exact and always wins (ties keep it too), so Windows and Linux
// pinned pixel output stays bit for bit; a Qt release that fixes CoreText flips macOS back
// by itself, and the imported-PSD text tests pin the rendered result on every platform.
void set_stretch_for_advance_ratio(QFont& font, double ratio, int naive_value) {
  if (!std::isfinite(ratio) || ratio <= 0.0) {
    font.setStretch(naive_value);
    return;
  }
  const auto sqrt_value =
      std::clamp(static_cast<int>(std::lround(std::sqrt(ratio) * 100.0)), 1, 400);
  if (sqrt_value == naive_value) {
    font.setStretch(naive_value);
    return;
  }
  QFont unstretched = font;
  unstretched.setStretch(QFont::AnyStretch);
  const auto reference = QFontMetricsF(unstretched).horizontalAdvance(QLatin1Char('H'));
  if (!(reference > 0.0)) {
    font.setStretch(naive_value);
    return;
  }
  const auto target = reference * ratio;
  const auto advance_error = [&font, target](int value) {
    QFont probe = font;
    probe.setStretch(value);
    return std::abs(QFontMetricsF(probe).horizontalAdvance(QLatin1Char('H')) - target);
  };
  font.setStretch(advance_error(sqrt_value) < advance_error(naive_value) ? sqrt_value
                                                                         : naive_value);
}

void scale_font_width(QFont& font, double scale) {
  if (scale <= 0.0 || !std::isfinite(scale) || std::abs(scale - 1.0) < 0.0001) {
    return;
  }
  const auto stretch = font.stretch() > 0 ? font.stretch() : 100;
  // The historical encoding, byte for byte, for the linear engines.
  const auto naive =
      std::clamp(static_cast<int>(std::round(static_cast<double>(stretch) * scale)), 1, 400);
  // The current advance ratio is MEASURED rather than decoded from the stored value, so a
  // sqrt-compensated stretch (see set_stretch_for_advance_ratio) rescales correctly too.
  QFont unstretched = font;
  unstretched.setStretch(QFont::AnyStretch);
  const auto reference = QFontMetricsF(unstretched).horizontalAdvance(QLatin1Char('H'));
  const auto current = QFontMetricsF(font).horizontalAdvance(QLatin1Char('H'));
  const auto current_ratio =
      reference > 0.0 && current > 0.0 ? current / reference : stretch / 100.0;
  set_stretch_for_advance_ratio(font, current_ratio * scale, naive);
}

void scale_document_font_sizes(QTextDocument& document, double scale) {
  if (scale <= 0.0 || !std::isfinite(scale) || std::abs(scale - 1.0) < 0.0001) {
    return;
  }

  auto default_font = document.defaultFont();
  scale_font_size(default_font, scale);
  document.setDefaultFont(default_font);

  struct FormatRange {
    int position{0};
    int length{0};
    QTextCharFormat format;
  };
  struct BlockFormatRange {
    int position{0};
    int length{0};
    QTextBlockFormat format;
  };
  std::vector<FormatRange> ranges;
  std::vector<BlockFormatRange> block_ranges;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    auto block_format = block.blockFormat();
    auto scaled_block_format = block_format;
    scaled_block_format.setLeftMargin(block_format.leftMargin() * scale);
    scaled_block_format.setRightMargin(block_format.rightMargin() * scale);
    scaled_block_format.setTextIndent(block_format.textIndent() * scale);
    scaled_block_format.setTopMargin(block_format.topMargin() * scale);
    scaled_block_format.setBottomMargin(block_format.bottomMargin() * scale);
    if (std::abs(scaled_block_format.leftMargin() - block_format.leftMargin()) > 0.0001 ||
        std::abs(scaled_block_format.rightMargin() - block_format.rightMargin()) > 0.0001 ||
        std::abs(scaled_block_format.textIndent() - block_format.textIndent()) > 0.0001 ||
        std::abs(scaled_block_format.topMargin() - block_format.topMargin()) > 0.0001 ||
        std::abs(scaled_block_format.bottomMargin() - block_format.bottomMargin()) > 0.0001) {
      block_ranges.push_back(BlockFormatRange{block.position(), std::max(1, block.length()), scaled_block_format});
    }

    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }

      auto format = fragment.charFormat();
      auto format_font = format.font();
      const auto before_pixel_size = format_font.pixelSize();
      const auto before_point_size = format_font.pointSizeF();
      const auto before_shift = format.property(kTextBaselineShiftFormatProperty).toDouble();
      const auto before_leading = format.hasProperty(kTextLeadingFormatProperty)
                                      ? format.property(kTextLeadingFormatProperty).toDouble()
                                      : 0.0;
      // The exact (fractional) size scales alongside the int pixel size so Photoshop-layout
      // math stays sub-pixel across editor zoom changes and editor->document conversion.
      const auto before_exact_size = format.hasProperty(kTextExactSizeFormatProperty)
                                         ? format.property(kTextExactSizeFormatProperty).toDouble()
                                         : 0.0;
      scale_font_size(format_font, scale);
      bool changed = format_font.pixelSize() != before_pixel_size ||
                     std::abs(format_font.pointSizeF() - before_point_size) >= 0.0001;
      if (format.hasProperty(kTextBaselineShiftFormatProperty) && std::isfinite(before_shift)) {
        const auto scaled_shift = before_shift * scale;
        format.setProperty(kTextBaselineShiftFormatProperty, scaled_shift);
        changed = changed || std::abs(scaled_shift - before_shift) >= 0.0001;
      }
      if (format.hasProperty(kTextLeadingFormatProperty) && std::isfinite(before_leading) && before_leading > 0.0) {
        const auto scaled_leading = before_leading * scale;
        format.setProperty(kTextLeadingFormatProperty, scaled_leading);
        changed = changed || std::abs(scaled_leading - before_leading) >= 0.0001;
      }
      if (std::isfinite(before_exact_size) && before_exact_size > 0.0) {
        const auto scaled_exact = before_exact_size * scale;
        format.setProperty(kTextExactSizeFormatProperty, scaled_exact);
        // Re-derive the int pixel size from the exact value so repeated zoom round trips
        // do not accumulate integer-rounding drift. The exact size is the FontSize basis;
        // the glyph pixel size folds the character panel's vertical scale back in.
        if (format_font.pixelSize() > 0) {
          auto vertical_glyph_scale = 1.0;
          if (format.hasProperty(kTextVerticalScaleFormatProperty)) {
            const auto value = format.property(kTextVerticalScaleFormatProperty).toDouble();
            if (std::isfinite(value) && value > 0.01 && value < 100.0) {
              vertical_glyph_scale = value;
            }
          }
          format_font.setPixelSize(
              std::max(1, static_cast<int>(std::lround(scaled_exact * vertical_glyph_scale))));
        }
        changed = true;
      }
      if (format.hasProperty(kTextTrackingFormatProperty) && format_font.letterSpacingType() == QFont::AbsoluteSpacing) {
        const auto tracking = format.property(kTextTrackingFormatProperty).toDouble();
        if (std::isfinite(tracking) && std::abs(tracking) > 0.0001) {
          const auto exact = std::isfinite(before_exact_size) && before_exact_size > 0.0
                                 ? before_exact_size * scale
                                 : static_cast<double>(format_font.pixelSize());
          auto horizontal_glyph_scale = 1.0;
          if (format.hasProperty(kTextHorizontalScaleFormatProperty)) {
            const auto value = format.property(kTextHorizontalScaleFormatProperty).toDouble();
            if (std::isfinite(value) && value > 0.01 && value < 100.0) {
              horizontal_glyph_scale = value;
            }
          }
          format_font.setLetterSpacing(QFont::AbsoluteSpacing,
                                       tracking / 1000.0 * exact * horizontal_glyph_scale);
          changed = true;
        }
      }
      if (!changed) {
        continue;
      }
      format.setFont(format_font);
      ranges.push_back(FormatRange{fragment.position(), fragment.length(), format});
    }
  }

  for (const auto& range : block_ranges) {
    QTextCursor cursor(&document);
    cursor.setPosition(range.position);
    cursor.setPosition(range.position + range.length, QTextCursor::KeepAnchor);
    cursor.mergeBlockFormat(range.format);
  }

  for (const auto& range : ranges) {
    QTextCursor cursor(&document);
    cursor.setPosition(range.position);
    cursor.setPosition(range.position + range.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(range.format);
  }
}

void scale_document_font_widths(QTextDocument& document, double scale) {
  if (scale <= 0.0 || !std::isfinite(scale) || std::abs(scale - 1.0) < 0.0001) {
    return;
  }

  auto default_font = document.defaultFont();
  scale_font_width(default_font, scale);
  document.setDefaultFont(default_font);

  struct FormatRange {
    int position{0};
    int length{0};
    QTextCharFormat format;
  };
  std::vector<FormatRange> ranges;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }

      auto format = fragment.charFormat();
      auto format_font = format.font();
      const auto before_stretch = format_font.stretch();
      scale_font_width(format_font, scale);
      if (format_font.stretch() == before_stretch) {
        continue;
      }
      format.setFont(format_font);
      ranges.push_back(FormatRange{fragment.position(), fragment.length(), format});
    }
  }

  for (const auto& range : ranges) {
    QTextCursor cursor(&document);
    cursor.setPosition(range.position);
    cursor.setPosition(range.position + range.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(range.format);
  }
}

QString normalized_rich_text_html(const QTextDocument& source) {
  QTextDocument copy;
  copy.setDocumentMargin(0);
  copy.setDefaultFont(source.defaultFont());
  copy.setDefaultTextOption(source.defaultTextOption());
  copy.setHtml(source.toHtml());
  return copy.toHtml();
}

std::unique_ptr<QTextDocument> copy_text_document_formats(const QTextDocument& source) {
  auto document = std::make_unique<QTextDocument>();
  document->setDocumentMargin(0);
  document->setDefaultFont(source.defaultFont());
  document->setDefaultTextOption(source.defaultTextOption());
  document->setPlainText(source.toPlainText());

  const int plain_length = static_cast<int>(document->toPlainText().size());
  for (auto block = source.begin(); block.isValid(); block = block.next()) {
    QTextCursor block_cursor(document.get());
    const auto block_start = std::clamp(block.position(), 0, std::max(0, plain_length));
    const auto block_end = std::clamp(block.position() + std::max(1, block.length()), 0, std::max(0, plain_length));
    block_cursor.setPosition(block_start);
    block_cursor.setPosition(block_end, QTextCursor::KeepAnchor);
    block_cursor.mergeBlockFormat(block.blockFormat());

    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      const auto start = std::clamp(fragment.position(), 0, std::max(0, plain_length));
      const auto end = std::clamp(fragment.position() + fragment.length(), 0, std::max(0, plain_length));
      if (start >= end) {
        continue;
      }
      QTextCursor cursor(document.get());
      cursor.setPosition(start);
      cursor.setPosition(end, QTextCursor::KeepAnchor);
      cursor.mergeCharFormat(fragment.charFormat());
    }
  }
  return document;
}

QString rich_text_runs_from_document(const QTextDocument& document, const TextToolSettings& fallback,
                                     QColor fallback_color) {
  QStringList lines;
  lines << QStringLiteral("v1");
  bool includes_leading = false;
  bool photoshop_layout = false;
  bool includes_faux_bold = false;
  bool includes_style = false;
  bool includes_faux_italic = false;
  bool includes_rotated = false;
  bool includes_shift = false;
  bool includes_character_styles = false;
  const auto fallback_family = fallback.family.isEmpty() ? default_text_family() : fallback.family;
  const auto fallback_size = std::max(1, fallback.size);
  const auto fallback_color_name = (fallback_color.isValid() ? fallback_color : QColor(Qt::black)).name(QColor::HexRgb);

  struct SerializedRun {
    int start{0};
    int length{0};
    double size{1.0};
    bool bold{false};
    bool italic{false};
    QString color;
    QString family;
    bool auto_leading{false};
    double leading{0.0};
    double tracking{0.0};
    double horizontal_scale{1.0};
    double vertical_scale{1.0};
    bool faux_bold{false};
    bool faux_italic{false};
    bool rotated_roman{false};
    double baseline_shift{0.0};
    QString style;
    QString character_styles;
  };
  std::vector<SerializedRun> collected;

  const auto append_run = [&collected, &includes_leading, &photoshop_layout, &includes_faux_bold, &includes_style,
                           &includes_faux_italic, &includes_rotated, &includes_shift, &includes_character_styles,
                           &fallback_family, fallback_size,
                           &fallback_color_name](int start, int length, const QTextCharFormat& format) {
    if (length <= 0) {
      return;
    }
    auto format_font = format.font();
    auto family = text_display_family_from_format(format, fallback_family);
    int size = format_font.pixelSize();
    if (size <= 0) {
      size = format_font.pointSizeF() > 0.0 ? static_cast<int>(std::round(format_font.pointSizeF())) : fallback_size;
    }
    QColor color = format.foreground().color();
    if (!color.isValid()) {
      color = QColor(fallback_color_name);
    }
    SerializedRun run;
    run.start = start;
    run.length = length;
    if (format.hasProperty(kTextHorizontalScaleFormatProperty)) {
      const auto value = format.property(kTextHorizontalScaleFormatProperty).toDouble();
      if (std::isfinite(value) && value > 0.01 && value < 100.0) {
        run.horizontal_scale = value;
      }
    }
    if (format.hasProperty(kTextVerticalScaleFormatProperty)) {
      const auto value = format.property(kTextVerticalScaleFormatProperty).toDouble();
      if (std::isfinite(value) && value > 0.01 && value < 100.0) {
        run.vertical_scale = value;
      }
    }
    // The serialized size is the FontSize basis; the font's pixel size folds the vertical
    // glyph scale in, so divide it back out when no exact value survives.
    run.size = std::max(1, size) / run.vertical_scale;
    // The exact (fractional) size wins while it still agrees with the int pixel size; a UI
    // size change only touches the pixel size, which orphans the stale exact value.
    if (format.hasProperty(kTextExactSizeFormatProperty)) {
      const auto exact = format.property(kTextExactSizeFormatProperty).toDouble();
      if (std::isfinite(exact) && exact > 0.0 &&
          static_cast<int>(std::lround(exact * run.vertical_scale)) == std::max(1, size)) {
        run.size = exact;
        // Only a fractional size (or a vertical glyph scale) needs the Photoshop-layout
        // columns; every run carries an exact size now, and a whole-pixel one serializes
        // exactly as it always did.
        if (std::abs(exact - std::round(exact)) > 0.0001 || std::abs(run.vertical_scale - 1.0) > 0.0001) {
          photoshop_layout = true;
        }
      }
    }
    run.bold = format_font.weight() >= QFont::Bold;
    run.italic = format_font.italic();
    run.color = color.name(QColor::HexRgb);
    run.family = QString::fromLatin1(family.toUtf8().toPercentEncoding());
    run.auto_leading = format.hasProperty(kTextAutoLeadingFormatProperty) &&
                       format.property(kTextAutoLeadingFormatProperty).toBool();
    const auto leading = format.hasProperty(kTextLeadingFormatProperty)
                             ? format.property(kTextLeadingFormatProperty).toDouble()
                             : 0.0;
    if (!run.auto_leading && std::isfinite(leading) && leading > 0.0) {
      run.leading = leading;
      includes_leading = true;
    }
    if (format.hasProperty(kTextTrackingFormatProperty)) {
      const auto tracking = format.property(kTextTrackingFormatProperty).toDouble();
      if (std::isfinite(tracking) && std::abs(tracking) > 0.0001 && std::abs(tracking) < 10000.0) {
        run.tracking = tracking;
      }
    }
    run.faux_bold = format.hasProperty(kTextFauxBoldFormatProperty) &&
                    format.property(kTextFauxBoldFormatProperty).toBool();
    includes_faux_bold = includes_faux_bold || run.faux_bold;
    run.style = format.property(kTextStyleNameFormatProperty).toString().trimmed();
    includes_style = includes_style || !run.style.isEmpty();
    run.faux_italic = format.hasProperty(kTextFauxItalicFormatProperty) &&
                      format.property(kTextFauxItalicFormatProperty).toBool();
    includes_faux_italic = includes_faux_italic || run.faux_italic;
    run.rotated_roman = format.hasProperty(kTextRotatedRomanFormatProperty) &&
                        format.property(kTextRotatedRomanFormatProperty).toBool();
    includes_rotated = includes_rotated || run.rotated_roman;
    run.baseline_shift = format.property(kTextBaselineShiftFormatProperty).toDouble();
    includes_shift = includes_shift || std::abs(run.baseline_shift) > 0.0001;
    const auto styles = text_character_styles_from_format(format);
    const bool has_styles = styles.any();
    run.character_styles = serialize_text_character_styles(styles);
    includes_character_styles = includes_character_styles || has_styles;
    photoshop_layout = photoshop_layout || has_styles || run.auto_leading || run.faux_bold || run.faux_italic ||
                       run.rotated_roman ||
                       !run.style.isEmpty() || std::abs(run.baseline_shift) > 0.0001 ||
                       std::abs(run.tracking) > 0.0001 ||
                       std::abs(run.horizontal_scale - 1.0) > 0.0001 ||
                       std::abs(run.vertical_scale - 1.0) > 0.0001;
    collected.push_back(std::move(run));
  };

  auto fallback_format = [&fallback_family, fallback_size, &fallback_color_name, &fallback]() {
    QTextCharFormat format;
    auto font = render_text_font_for_display_family(fallback_family, fallback_size, fallback.bold,
                                                    fallback.italic, fallback.anti_alias);
    format.setFont(font);
    set_text_display_family(format, fallback_family);
    format.setForeground(QBrush(QColor(fallback_color_name)));
    return format;
  }();

  bool found_run = false;
  QTextCharFormat last_format = fallback_format;
  const int plain_length = static_cast<int>(document.toPlainText().size());
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    bool block_has_fragment = false;
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      block_has_fragment = true;
      found_run = true;
      last_format = fragment.charFormat();
      append_run(fragment.position(), fragment.length(), fragment.charFormat());
    }
    if (block.next().isValid()) {
      const auto separator_position = block.position() + block.length() - 1;
      if (separator_position >= 0 && separator_position < plain_length) {
        const auto separator_format = block_has_fragment ? last_format
                                                         : (block.charFormat().isValid() ? block.charFormat()
                                                                                         : last_format);
        found_run = true;
        append_run(separator_position, 1, separator_format);
        last_format = separator_format;
      }
    }
  }

  if (!found_run) {
    append_run(0, document.toPlainText().size(), fallback_format);
  }
  if (includes_character_styles) {
    lines[0] = QStringLiteral("v9");
  } else if (includes_shift) {
    lines[0] = QStringLiteral("v8");
  } else if (includes_rotated) {
    lines[0] = QStringLiteral("v7");
  } else if (includes_faux_italic) {
    lines[0] = QStringLiteral("v6");
  } else if (includes_style) {
    lines[0] = QStringLiteral("v5");
  } else if (includes_faux_bold) {
    lines[0] = QStringLiteral("v4");
  } else if (photoshop_layout) {
    lines[0] = QStringLiteral("v3");
  } else if (includes_leading) {
    lines[0] = QStringLiteral("v2");
  }
  for (const auto& run : collected) {
    auto line = QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7")
                    .arg(run.start)
                    .arg(run.length)
                    .arg(photoshop_layout ? QString::number(run.size, 'g', 17)
                                          : QString::number(static_cast<int>(std::lround(run.size))))
                    .arg(run.bold ? 1 : 0)
                    .arg(run.italic ? 1 : 0)
                    .arg(run.color)
                    .arg(run.family);
    if (photoshop_layout) {
      // v3 leading column: fixed value or the literal "auto" (paragraph fraction x size).
      line += QStringLiteral("\t%1").arg(
          run.auto_leading || run.leading <= 0.0 ? QStringLiteral("auto")
                                                 : QString::number(run.leading, 'g', 17));
      line += QStringLiteral("\t%1").arg(QString::number(run.tracking, 'g', 17));
      line += QStringLiteral("\t%1").arg(QString::number(run.horizontal_scale, 'g', 17));
      line += QStringLiteral("\t%1").arg(QString::number(run.vertical_scale, 'g', 17));
      // Column 11 is faux bold and column 12 the style name; the style column needs the faux
      // one in front of it, so a styled run emits both.
      const bool through_shift = includes_shift || includes_character_styles;
      if (includes_faux_bold || includes_style || includes_faux_italic || includes_rotated || through_shift) {
        line += QStringLiteral("\t%1").arg(run.faux_bold ? 1 : 0);
      }
      if (includes_style || includes_faux_italic || includes_rotated || through_shift) {
        line += QStringLiteral("\t%1").arg(QString::fromLatin1(run.style.toUtf8().toPercentEncoding()));
      }
      if (includes_faux_italic || includes_rotated || through_shift) {
        line += QStringLiteral("\t%1").arg(run.faux_italic ? 1 : 0);
      }
      if (includes_rotated || through_shift) {
        line += QStringLiteral("\t%1").arg(run.rotated_roman ? 2 : 0);
      }
      if (through_shift) {
        line += QStringLiteral("\t%1").arg(QString::number(run.baseline_shift, 'g', 17));
      }
      if (includes_character_styles) {
        line += QStringLiteral("\t%1").arg(run.character_styles);
      }
    } else if (includes_leading) {
      line += QStringLiteral("\t%1").arg(QString::number(run.leading, 'g', 17));
    }
    lines << line;
  }
  return lines.join(QLatin1Char('\n'));
}

QString paragraph_runs_from_document(const QTextDocument& document) {
  struct ParagraphRunLine {
    int start{0};
    int length{0};
    QString alignment;
    double first_line_indent{0.0};
    double start_indent{0.0};
    double end_indent{0.0};
    double space_before{0.0};
    double space_after{0.0};
    double auto_lead_fraction{1.2};
    QString direction;  // "ltr" / "rtl", empty for auto
    int composer{1};    // Photoshop's paragraph composer; 1 World-Ready (the default)
  };
  const auto normalized_metric = [](double value) {
    return std::isfinite(value) && std::abs(value) >= 0.0001 ? value : 0.0;
  };
  const auto metric_text = [](double value) {
    value = std::isfinite(value) && std::abs(value) >= 0.0001 ? value : 0.0;
    return QString::number(value, 'g', 17);
  };

  std::vector<ParagraphRunLine> run_lines;
  bool include_layout = false;
  bool include_fraction = false;
  bool include_direction = false;
  bool include_composer = false;
  const int plain_length = static_cast<int>(document.toPlainText().size());
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    const auto start = std::clamp(block.position(), 0, std::max(0, plain_length));
    const auto length = std::max(0, std::min(block.length(), std::max(0, plain_length - start)));
    const auto format = block.blockFormat();
    ParagraphRunLine run;
    run.start = start;
    run.length = length;
    run.alignment = paragraph_alignment_name(format.alignment());
    run.first_line_indent = normalized_metric(format.textIndent());
    run.start_indent = normalized_metric(format.leftMargin());
    run.end_indent = normalized_metric(format.rightMargin());
    run.space_before = normalized_metric(format.topMargin());
    run.space_after = normalized_metric(format.bottomMargin());
    if (format.hasProperty(kTextBlockAutoLeadFractionProperty)) {
      const auto fraction = format.property(kTextBlockAutoLeadFractionProperty).toDouble();
      if (std::isfinite(fraction) && fraction > 0.01 && fraction < 10.0) {
        run.auto_lead_fraction = fraction;
        include_fraction = include_fraction || std::abs(fraction - 1.2) > 0.0001;
      }
    }
    include_layout = include_layout || run.first_line_indent != 0.0 || run.start_indent != 0.0 ||
                     run.end_indent != 0.0 || run.space_before != 0.0 || run.space_after != 0.0;
    // An explicit paragraph direction is a v4 column; auto (the bidi default) leaves the
    // paragraph on the older formats so ordinary text stays byte-identical.
    if (format.layoutDirection() == Qt::LeftToRight) {
      run.direction = QStringLiteral("ltr");
    } else if (format.layoutDirection() == Qt::RightToLeft) {
      run.direction = QStringLiteral("rtl");
    }
    include_direction = include_direction || !run.direction.isEmpty();
    if (format.hasProperty(kTextBlockComposerProperty)) {
      run.composer = std::clamp(format.intProperty(kTextBlockComposerProperty), 0, 2);
    }
    include_composer = include_composer || run.composer != 1;
    run_lines.push_back(std::move(run));
  }
  include_direction = include_direction || include_composer;
  include_fraction = include_fraction || include_direction;
  include_layout = include_layout || include_fraction;

  QStringList lines;
  lines << (include_composer
                ? QStringLiteral("v5")
                : (include_direction
                       ? QStringLiteral("v4")
                       : (include_fraction ? QStringLiteral("v3")
                                           : (include_layout ? QStringLiteral("v2") : QStringLiteral("v1")))));
  for (const auto& run : run_lines) {
    auto line = QStringLiteral("%1\t%2\t%3").arg(run.start).arg(run.length).arg(run.alignment);
    if (include_layout) {
      line += QStringLiteral("\t%1\t%2\t%3\t%4\t%5")
                  .arg(metric_text(run.first_line_indent))
                  .arg(metric_text(run.start_indent))
                  .arg(metric_text(run.end_indent))
                  .arg(metric_text(run.space_before))
                  .arg(metric_text(run.space_after));
    }
    if (include_fraction) {
      line += QStringLiteral("\t%1").arg(QString::number(run.auto_lead_fraction, 'g', 17));
    }
    if (include_direction) {
      line += QStringLiteral("\t%1").arg(run.direction.isEmpty() ? QStringLiteral("auto") : run.direction);
    }
    if (include_composer) {
      line += QStringLiteral("\t%1").arg(run.composer);
    }
    lines << line;
  }
  return lines.join(QLatin1Char('\n'));
}

void apply_paragraph_runs_to_document(QTextDocument& document, const QString& paragraph_runs, double scale) {
  const auto lines = paragraph_runs.split(QLatin1Char('\n'));
  const int plain_length = static_cast<int>(document.toPlainText().size());
  const auto metric_value = [scale](const QString& field) {
    bool ok = false;
    const auto value = field.toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
      return 0.0;
    }
    return value * std::max(0.0, scale);
  };
  for (const auto& raw_line : lines) {
    const auto line = raw_line.trimmed();
    if (line.isEmpty() || line == QStringLiteral("v1") || line == QStringLiteral("v2") ||
        line == QStringLiteral("v3") || line == QStringLiteral("v4") || line == QStringLiteral("v5")) {
      continue;
    }
    const auto fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 3) {
      continue;
    }
    bool start_ok = false;
    bool length_ok = false;
    const auto start = std::clamp(fields[0].toInt(&start_ok), 0, std::max(0, plain_length));
    const auto length = std::max(0, fields[1].toInt(&length_ok));
    if (!start_ok || !length_ok || start > plain_length) {
      continue;
    }
    QTextCursor cursor(&document);
    cursor.setPosition(start);
    cursor.setPosition(std::min(plain_length, start + std::max(1, length)), QTextCursor::KeepAnchor);
    QTextBlockFormat format;
    format.setAlignment(paragraph_alignment_from_name(fields[2]));
    if (fields.size() >= 10) {
      if (fields[9] == QStringLiteral("rtl")) {
        format.setLayoutDirection(Qt::RightToLeft);
      } else if (fields[9] == QStringLiteral("ltr")) {
        format.setLayoutDirection(Qt::LeftToRight);
      }
    }
    if (fields.size() >= 8) {
      format.setTextIndent(metric_value(fields[3]));
      format.setLeftMargin(metric_value(fields[4]));
      format.setRightMargin(metric_value(fields[5]));
      format.setTopMargin(metric_value(fields[6]));
      format.setBottomMargin(metric_value(fields[7]));
    }
    if (fields.size() >= 9) {
      bool fraction_ok = false;
      const auto fraction = fields[8].toDouble(&fraction_ok);
      if (fraction_ok && std::isfinite(fraction) && fraction > 0.01 && fraction < 10.0) {
        // Unscaled: the auto-leading fraction multiplies each run's own size at layout time.
        format.setProperty(kTextBlockAutoLeadFractionProperty, fraction);
      }
    }
    if (fields.size() >= 11) {
      bool composer_ok = false;
      const auto composer = fields[10].toInt(&composer_ok);
      if (composer_ok && composer != 1) {
        format.setProperty(kTextBlockComposerProperty, std::clamp(composer, 0, 2));
      }
    }
    cursor.mergeBlockFormat(format);
  }
}

void apply_patchy_text_runs_to_document(QTextDocument& document, const QString& runs_text, const QFont& fallback_font,
                                        QColor fallback_color, double scale, int anti_alias) {
  const auto lines = runs_text.split(QLatin1Char('\n'));
  const int plain_length = static_cast<int>(document.toPlainText().size());
  bool applied_run = false;
  for (const auto& raw_line : lines) {
    const auto line = raw_line.trimmed();
    if (line.isEmpty() || is_text_runs_version_line(line)) {
      continue;
    }
    const auto fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 7) {
      continue;
    }
    bool start_ok = false;
    bool length_ok = false;
    bool size_ok = false;
    const auto start = std::clamp(fields[0].toInt(&start_ok), 0, std::max(0, plain_length));
    const auto length = std::max(0, fields[1].toInt(&length_ok));
    const auto exact_document_size = std::max(1.0, fields[2].toDouble(&size_ok));
    if (!start_ok || !length_ok || !size_ok || length <= 0 || start >= plain_length) {
      continue;
    }

    // Character-panel glyph scales (v3 fields 10/11): glyph height x vertical, width x
    // horizontal. Read before the font so the pixel size can fold the vertical scale in.
    double horizontal_glyph_scale = 1.0;
    double vertical_glyph_scale = 1.0;
    if (fields.size() >= 10) {
      bool scale_ok = false;
      const auto value = fields[9].toDouble(&scale_ok);
      if (scale_ok && std::isfinite(value) && value > 0.01 && value < 100.0) {
        horizontal_glyph_scale = value;
      }
    }
    if (fields.size() >= 11) {
      bool scale_ok = false;
      const auto value = fields[10].toDouble(&scale_ok);
      if (scale_ok && std::isfinite(value) && value > 0.01 && value < 100.0) {
        vertical_glyph_scale = value;
      }
    }

    auto family = QString::fromUtf8(QByteArray::fromPercentEncoding(fields[6].toLatin1()));
    if (family.trimmed().isEmpty()) {
      family = display_text_family_from_font(fallback_font);
    } else {
      family = canonical_text_display_family(family);
    }
    const auto style_name =
        fields.size() >= 13 ? QString::fromUtf8(QByteArray::fromPercentEncoding(fields[12].toLatin1())) : QString();
    auto font = render_text_font_for_display_family(
        family, std::max(1, static_cast<int>(std::round(exact_document_size * vertical_glyph_scale * scale))),
        fields[3].toInt() != 0, fields[4].toInt() != 0, anti_alias, style_name);
    // Photoshop scales glyph width by H and height by V; Qt's pixel size scales both, so the
    // stretch carries the width-to-height ratio.
    if (std::abs(horizontal_glyph_scale - vertical_glyph_scale) > 0.0001) {
      const auto ratio = horizontal_glyph_scale / vertical_glyph_scale;
      set_stretch_for_advance_ratio(
          font, ratio, std::clamp(static_cast<int>(std::lround(ratio * 100.0)), 1, 400));
    }

    QColor color(fields[5]);
    if (!color.isValid()) {
      color = fallback_color.isValid() ? fallback_color : QColor(Qt::black);
    }

    QTextCharFormat format;
    format.setFont(font);
    set_text_display_family(format, family);
    if (!style_name.trimmed().isEmpty()) {
      format.setProperty(kTextStyleNameFormatProperty, style_name.trimmed());
    }
    format.setForeground(QBrush(color));
    // The exact size excludes the vertical glyph scale: it is the leading/tracking basis
    // (FontSize), while the font's pixel size above folds V in. Every run carries it, integral
    // sizes included: the editor's font is whole editor pixels (document px x zoom), and a
    // commit that had only that to go on recovered the document size as round(px / zoom),
    // which at a 15% zoom turned an untouched 60 px layer into 58 px (the "text shrinks when I
    // click into it" report). The commit re-derives the pixel size from this value instead.
    const auto scaled_exact_size = exact_document_size * std::max(0.0, scale);
    format.setProperty(kTextExactSizeFormatProperty, scaled_exact_size);
    if (std::abs(horizontal_glyph_scale - 1.0) > 0.0001) {
      format.setProperty(kTextHorizontalScaleFormatProperty, horizontal_glyph_scale);
    }
    if (std::abs(vertical_glyph_scale - 1.0) > 0.0001) {
      format.setProperty(kTextVerticalScaleFormatProperty, vertical_glyph_scale);
    }
    if (fields.size() >= 8) {
      if (fields[7] == QStringLiteral("auto")) {
        format.setProperty(kTextAutoLeadingFormatProperty, true);
      } else {
        bool leading_ok = false;
        const auto leading = fields[7].toDouble(&leading_ok);
        if (leading_ok && std::isfinite(leading) && leading > 0.0) {
          format.setProperty(kTextLeadingFormatProperty, leading * std::max(0.0, scale));
        }
      }
    }
    if (fields.size() >= 9) {
      bool tracking_ok = false;
      const auto tracking = fields[8].toDouble(&tracking_ok);
      if (tracking_ok && std::isfinite(tracking) && std::abs(tracking) > 0.0001 && std::abs(tracking) < 10000.0) {
        // Photoshop tracking: 1/1000 em per inter-glyph gap, applied as absolute letter
        // spacing; the horizontal glyph scale multiplies the whole advance, tracking included.
        format.setProperty(kTextTrackingFormatProperty, tracking);
        format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        format.setFontLetterSpacing(tracking / 1000.0 * scaled_exact_size * horizontal_glyph_scale);
      }
    }
    // v4 faux bold: a flag only. The stroke and the advance it adds are applied by
    // apply_faux_bold_to_document at render time, so they track later colour/size edits.
    if (fields.size() >= 12 && fields[11].toInt() != 0) {
      format.setProperty(kTextFauxBoldFormatProperty, true);
    }
    if (fields.size() >= 14 && fields[13].toInt() != 0) {
      format.setProperty(kTextFauxItalicFormatProperty, true);
    }
    if (fields.size() >= 15 && fields[14].toInt() == 2) {
      format.setProperty(kTextRotatedRomanFormatProperty, true);
    }
    if (fields.size() >= 16) {
      const auto shift = fields[15].toDouble();
      if (std::isfinite(shift) && std::abs(shift) < 100000.0) {
        const auto scaled_shift = shift * std::max(0.0, scale);
        format.setProperty(kTextBaselineShiftFormatProperty, scaled_shift);
        format.setBaselineOffset(100.0 * scaled_shift / std::max(1, font.pixelSize()));
      }
    }
    if (fields.size() >= 17) {
      set_text_character_styles_on_format(format, parse_text_character_styles(fields[16]));
    }
    QTextCursor cursor(&document);
    cursor.setPosition(start);
    cursor.setPosition(std::min(plain_length, start + length), QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(format);
    applied_run = true;
  }

  if (!applied_run && plain_length > 0) {
    auto font = fallback_font;
    scale_font_size(font, scale);
    configure_text_font_smoothing(font, anti_alias);
    QTextCharFormat format;
    format.setFont(font);
    set_text_display_family(format, display_text_family_from_font(font));
    format.setForeground(QBrush(fallback_color.isValid() ? fallback_color : QColor(Qt::black)));
    QTextCursor cursor(&document);
    cursor.select(QTextCursor::Document);
    cursor.mergeCharFormat(format);
  }
}

QString document_html_from_text_runs(const QString& text, const QString& rich_text_runs,
                                     const TextToolSettings& fallback, QColor fallback_color) {
  const auto fallback_family = fallback.family.isEmpty() ? default_text_family() : fallback.family;
  auto fallback_font = render_text_font_for_display_family(fallback_family, std::max(1, fallback.size),
                                                          fallback.bold, fallback.italic, fallback.anti_alias);

  QTextDocument document;
  document.setDocumentMargin(0);
  document.setDefaultFont(fallback_font);
  document.setPlainText(text);
  apply_patchy_text_runs_to_document(document, rich_text_runs, fallback_font, fallback_color, 1.0,
                                     fallback.anti_alias);
  apply_text_smoothing_to_document(document, fallback.anti_alias);
  return normalized_rich_text_html(document);
}

void apply_plain_text_format(QTextDocument& document, const QFont& font, QColor color) {
  QTextCursor cursor(&document);
  cursor.select(QTextCursor::Document);
  QTextCharFormat format;
  format.setFont(font);
  set_text_display_family(format, display_text_family_from_font(font));
  format.setForeground(QBrush(color));
  cursor.mergeCharFormat(format);
}

// Re-resolve character formats whose primary family is not an available family but matches a
// family + style pair (see available_text_family_style_match).  HTML round-trips drop the style
// name, so documents built from settings.html would otherwise fall back to a regular-weight face
// on platforms whose font database splits GDI-style names.
void resolve_document_font_styles(QTextDocument& document) {
  struct StylePatch {
    int position{0};
    int length{0};
    QFont font;
  };
  std::vector<StylePatch> patches;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      const auto format = fragment.charFormat();
      const auto families = format.font().families();
      if (families.isEmpty()) {
        continue;
      }
      const auto& primary = families.front();
      if (available_text_family_match(primary).has_value()) {
        continue;
      }
      const auto match = available_text_family_style_match(primary);
      if (!match.has_value()) {
        continue;
      }
      auto font = format.font();
      font.setFamilies(QStringList{match->family});
      font.setStyleName(match->style);
      patches.push_back(StylePatch{fragment.position(), fragment.length(), std::move(font)});
    }
  }
  for (const auto& patch : patches) {
    QTextCharFormat format;
    format.setFont(patch.font);
    QTextCursor cursor(&document);
    cursor.setPosition(patch.position);
    cursor.setPosition(patch.position + patch.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(format);
  }
}

// Move every run whose family cannot draw its own characters onto the face that is actually
// drawing it (see substituted_text_family), so the runs the commit serializes name the font the
// user is looking at. Blank paragraphs carry their family in the BLOCK char format, which holds no
// fragment: rich_text_runs_from_document still emits a separator run from it, so a missed one
// leaves the whole layer flagged. Patch-then-apply like resolve_document_font_styles -- mutating
// while iterating invalidates the fragment iterators.
void substitute_missing_document_font_families(QTextDocument& document, const TextFontReplacements* replacements) {
  struct FamilyPatch {
    int position{0};
    int length{0};
    bool block_format{false};
    QString family;
  };
  std::vector<FamilyPatch> patches;
  // One substitute per requested family, so a layer never splits one missing font across two faces.
  QHash<QString, QString> substitutes;
  const auto substitute_for = [&substitutes, replacements](const QString& family, const QString& demanded) {
    QStringList missing;
    append_missing_text_family(missing, family, demanded);
    if (missing.isEmpty()) {
      return QString();
    }
    const auto key = text_font_replacement_key(family);
    if (replacements != nullptr) {
      const auto chosen = replacements->value(key);
      return chosen.compare(family.trimmed(), Qt::CaseInsensitive) == 0 ? QString() : chosen;
    }
    auto found = substitutes.constFind(key);
    if (found == substitutes.constEnd()) {
      found = substitutes.insert(key, substituted_text_family(family, demanded));
    }
    return found->compare(family.trimmed(), Qt::CaseInsensitive) == 0 ? QString() : *found;
  };
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    bool block_has_fragment = false;
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      block_has_fragment = true;
      const auto family = text_display_family_from_format(fragment.charFormat(), QString());
      const auto substitute = substitute_for(family, fragment.text());
      if (!substitute.isEmpty()) {
        patches.push_back(FamilyPatch{fragment.position(), fragment.length(), false, substitute});
      }
    }
    if (block_has_fragment) {
      continue;
    }
    const auto family = text_display_family_from_format(block.charFormat(), QString());
    if (const auto substitute = substitute_for(family, QString()); !substitute.isEmpty()) {
      patches.push_back(FamilyPatch{block.position(), 0, true, substitute});
    }
  }
  for (const auto& patch : patches) {
    QTextCharFormat format;
    format.setFontFamilies(render_text_families_for_display_family(patch.family));
    set_text_display_family(format, patch.family);
    QTextCursor cursor(&document);
    cursor.setPosition(patch.position);
    if (patch.block_format) {
      cursor.mergeBlockCharFormat(format);
      continue;
    }
    cursor.setPosition(patch.position + patch.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(format);
  }
}

void apply_faux_bold_to_document(QTextDocument& document) {
  struct FauxBoldRange {
    int start{0};
    int length{0};
    QTextCharFormat format;
  };
  // Collect first: merging a format re-splits the block's fragments, which invalidates the
  // iterator mid-walk.
  std::vector<FauxBoldRange> ranges;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      const auto format = fragment.charFormat();
      if (!format.hasProperty(kTextFauxBoldFormatProperty) ||
          !format.property(kTextFauxBoldFormatProperty).toBool()) {
        continue;
      }
      auto font = format.font();
      const auto pixel_size = font.pixelSize() > 0 ? static_cast<double>(font.pixelSize())
                                                   : std::max(1.0, font.pointSizeF());
      const auto advance = kFauxBoldAdvanceEmFraction * pixel_size;
      const auto stroke = std::min(advance, kFauxBoldMaxStrokePixels);
      if (!(stroke > 0.0)) {
        continue;
      }
      auto color = format.foreground().color();
      if (!color.isValid()) {
        color = QColor(Qt::black);
      }
      QPen pen(color, stroke);
      pen.setJoinStyle(Qt::RoundJoin);
      pen.setCapStyle(Qt::RoundCap);
      // Photoshop's synthetic bold pushes the glyph out on BOTH sides and pays for it in the
      // advance, so tracked text keeps its gaps instead of colliding. Past the stroke cap the
      // advance still grows with the em: a 62.5px line gains 1.69px per glyph, its ink 1px.
      const auto spacing =
          font.letterSpacingType() == QFont::AbsoluteSpacing ? font.letterSpacing() : 0.0;
      font.setLetterSpacing(QFont::AbsoluteSpacing, spacing + advance);
      QTextCharFormat overlay;
      overlay.setFont(font);
      overlay.setTextOutline(pen);
      ranges.push_back(FauxBoldRange{fragment.position(), fragment.length(), std::move(overlay)});
    }
  }
  for (const auto& range : ranges) {
    QTextCursor cursor(&document);
    cursor.setPosition(range.start);
    cursor.setPosition(range.start + range.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(range.format);
  }
}

namespace {

constexpr QFont::Tag kSmallCapsFeature("smcp");
constexpr QFont::Tag kOrdinalsFeature("ordn");
constexpr QFont::Tag kFractionsFeature("frac");

struct ShapedText {
  QList<quint32> glyphs;
  double advance{0.0};
};

// One line of `text` shaped the way the render document lays it out (design metrics, no wrap).
ShapedText shape_text_line(const QString& text, const QFont& font) {
  ShapedText shaped;
  QTextLayout layout(text, font);
  QTextOption option;
  option.setWrapMode(QTextOption::NoWrap);
  option.setUseDesignMetrics(true);
  layout.setTextOption(option);
  layout.beginLayout();
  auto line = layout.createLine();
  layout.endLayout();
  if (!line.isValid()) {
    return shaped;
  }
  shaped.advance = line.horizontalAdvance();
  for (const auto& run : line.glyphRuns()) {
    shaped.glyphs += run.glyphIndexes();
  }
  return shaped;
}

double absolute_letter_spacing(const QFont& font) {
  return font.letterSpacingType() == QFont::AbsoluteSpacing ? font.letterSpacing() : 0.0;
}

}  // namespace

QFont text_render_featured_font(const QTextCharFormat& format) {
  auto font = format.font();
  font.setLetterSpacing(QFont::AbsoluteSpacing,
                        format.property(kTextRenderBaseLetterSpacingFormatProperty).toDouble());
  for (const auto& name : format.property(kTextRenderFeaturesFormatProperty).toStringList()) {
    if (const auto tag = QFont::Tag::fromString(name); tag.has_value()) {
      font.setFeature(*tag, 1);
    }
  }
  return font;
}

void apply_text_character_styles_to_document(QTextDocument& document, bool vertical) {
  struct StyleRange {
    int start{0};
    int length{0};
    QTextCharFormat format;
  };
  // Collect first: merging re-splits the block's fragments (see apply_faux_bold_to_document).
  std::vector<StyleRange> ranges;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    for (auto fragment_it = block.begin(); !fragment_it.atEnd(); ++fragment_it) {
      const auto fragment = fragment_it.fragment();
      if (!fragment.isValid() || fragment.length() <= 0) {
        continue;
      }
      const auto format = fragment.charFormat();
      const auto styles = text_character_styles_from_format(format);
      if (styles.caps == 0 && styles.baseline == 0 && !styles.ordinals && !styles.fractions) {
        continue;  // underline and strikethrough are drawn by the render plan
      }
      auto font = format.font();
      QTextCharFormat overlay;
      if (styles.caps == 2) {
        font.setCapitalization(QFont::AllUppercase);
      }
      if (styles.baseline != 0 && !vertical) {
        // Photoshop draws a superscript or subscript at 58.3% of the size with its baseline moved
        // 33.3% of the size (COM captures of five faces at 100 px: the H moves 33 px). The size
        // is the run's glyph em (FontSize x vertical scale), not the shrunken one.
        const auto exact = photoshop_char_exact_size(format);
        auto vertical_scale = format.property(kTextVerticalScaleFormatProperty).toDouble();
        if (!(std::isfinite(vertical_scale) && vertical_scale > 0.01 && vertical_scale < 100.0)) {
          vertical_scale = 1.0;
        }
        const auto em = exact * vertical_scale;
        const auto pixel_size = font.pixelSize() > 0 ? font.pixelSize() : std::max(1, static_cast<int>(std::lround(em)));
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(pixel_size * psd::kTextScriptSizeFraction))));
        // Tracking is per em of the drawn size: "script" at tracking 100 in the issue's sample
        // is 11 px narrower in Photoshop than at the run's full size.
        if (font.letterSpacingType() == QFont::AbsoluteSpacing) {
          font.setLetterSpacing(QFont::AbsoluteSpacing,
                                font.letterSpacing() * font.pixelSize() / static_cast<double>(pixel_size));
        }
        const auto shift = format.property(kTextBaselineShiftFormatProperty).toDouble() +
                           (styles.baseline == 1 ? 1.0 : -1.0) * psd::kTextScriptPositionFraction * em;
        overlay.setProperty(kTextBaselineShiftFormatProperty, shift);
        // Qt reads the offset as a percentage of the font's HEIGHT (ascent + descent), not of its
        // pixel size; the glyph-run path reads the property above instead.
        const auto height = QFontMetricsF(font).height();
        overlay.setBaselineOffset(100.0 * shift / (height > 0.0 ? height : std::max(1, font.pixelSize())));
      }
      // OpenType features the font really has: Photoshop uses the face's own small capitals,
      // ordinals and fractions (Arial's smcp lowercase stand 0.56 em, not the 0.7 x cap height a
      // synthesized small cap would), and leaves text whose font lacks a feature unchanged.
      QStringList features;
      if (!vertical) {
        const auto text = fragment.text();
        const auto plain = shape_text_line(text, font);
        const auto try_feature = [&](QFont::Tag tag) {
          auto featured = font;
          for (const auto& name : features) {
            if (const auto existing = QFont::Tag::fromString(name); existing.has_value()) {
              featured.setFeature(*existing, 1);
            }
          }
          featured.setFeature(tag, 1);
          if (shape_text_line(text, featured).glyphs != plain.glyphs) {
            features << QString::fromLatin1(tag.toString());
          }
        };
        if (styles.caps == 1) try_feature(kSmallCapsFeature);
        if (styles.ordinals) try_feature(kOrdinalsFeature);
        if (styles.fractions) try_feature(kFractionsFeature);
      }
      if (styles.caps == 1 && !features.contains(QString::fromLatin1(kSmallCapsFeature.toString()))) {
        // No small capitals in the face: Qt synthesizes them at 70%, Photoshop's /SmallCapSize.
        font.setCapitalization(QFont::SmallCaps);
      }
      if (!features.isEmpty()) {
        // The layout cannot shape with features, so it reserves the featured width instead:
        // the difference is spread over the fragment's characters as letter spacing, and the
        // plan draws the featured glyphs from their own shaping (draw_featured_glyph_runs).
        const auto base_spacing = absolute_letter_spacing(font);
        auto featured_font = font;
        for (const auto& name : features) {
          if (const auto tag = QFont::Tag::fromString(name); tag.has_value()) {
            featured_font.setFeature(*tag, 1);
          }
        }
        const auto text = fragment.text();
        const auto plain_advance = shape_text_line(text, font).advance;
        const auto featured_advance = shape_text_line(text, featured_font).advance;
        const auto count = std::max(1, static_cast<int>(text.size()));
        font.setLetterSpacing(QFont::AbsoluteSpacing, base_spacing + (featured_advance - plain_advance) / count);
        overlay.setProperty(kTextRenderFeaturesFormatProperty, features);
        overlay.setProperty(kTextRenderBaseLetterSpacingFormatProperty, base_spacing);
      }
      overlay.setFont(font);
      ranges.push_back(StyleRange{fragment.position(), fragment.length(), std::move(overlay)});
    }
  }
  for (const auto& range : ranges) {
    QTextCursor cursor(&document);
    cursor.setPosition(range.start);
    cursor.setPosition(range.start + range.length, QTextCursor::KeepAnchor);
    cursor.mergeCharFormat(range.format);
  }
}

bool text_format_has_render_features(const QTextCharFormat& format) {
  return !format.property(kTextRenderFeaturesFormatProperty).toStringList().isEmpty();
}

// Build the QTextDocument a text layer is rasterized from.  The caret/selection layout is built
// through this same function (build_text_editor_document_space_layout), so the painted glyphs and
// the caret geometry always come from one identical document -- any divergence in construction
// (formats, blank-line heights, wrapping width) shows up as the caret drifting off the text.
TextRenderDocument build_text_render_document(const TextToolSettings& settings, QColor color,
                                              std::int32_t max_width, const QString& paragraph_runs,
                                              const QString& rich_text_runs, double metric_scale,
                                              double layout_scale, double box_scale) {
  TextRenderDocument result;
  metric_scale = std::clamp(std::isfinite(metric_scale) ? metric_scale : 1.0, 0.5, 1.5);
  layout_scale = std::isfinite(layout_scale) && layout_scale > 0.01 ? layout_scale : 1.0;
  // Box dims scale separately from glyph sizes: a transform fold scales both (raw box, raw
  // runs), but a PSD-frame session's box is already in document space while its runs are raw.
  box_scale = std::isfinite(box_scale) && box_scale > 0.01 ? box_scale : 1.0;
  result.font = render_text_font_for_display_family(
      settings.family, std::max(1, static_cast<int>(std::lround(settings.size * layout_scale))), settings.bold,
      settings.italic, settings.anti_alias);
  scale_font_width(result.font, metric_scale);

  result.text_width = settings.boxed
                          ? std::max(kMinimumTextBoxDocumentSize,
                                     static_cast<int>(std::lround(settings.box_width * box_scale)))
                          : std::max(64, max_width);
  if (settings.vertical) {
    // Vertical forms (a rotated long-vowel mark, brackets that open downward, small kana
    // shifted to the upper right) come from the OpenType 'vert' feature. QTextCharFormat
    // cannot carry features (Qt 6.8), so it rides on the default font every run resolves
    // against. Fonts without the feature are unaffected.
    result.font.setFeature(QFont::Tag("vert"), 1);
  }
  result.document = std::make_unique<QTextDocument>();
  auto& document = *result.document;
  document.setDocumentMargin(0);
  document.setDefaultFont(result.font);
  QTextOption option;
  // Vertical text is laid out horizontally with NoWrap for shaping only; the vertical plan
  // wraps it by cells at the frame height.
  option.setWrapMode(settings.boxed && !settings.vertical ? QTextOption::WordWrap : QTextOption::NoWrap);
  option.setUseDesignMetrics(true);
  document.setDefaultTextOption(option);
  if (!rich_text_runs.trimmed().isEmpty()) {
    // Build from the plain text + serialized runs -- the same construction the editor uses.  The
    // HTML round-trip below loses character formats on empty paragraphs (old PSDs separate lines
    // with control characters that import as blank paragraphs), which made the drawn text and the
    // selection highlights drift apart.
    document.setPlainText(settings.text);
    apply_patchy_text_runs_to_document(document, rich_text_runs, result.font, color, layout_scale,
                                       settings.anti_alias);
    scale_document_font_widths(document, metric_scale);
  } else if (!settings.html.trimmed().isEmpty()) {
    document.setHtml(settings.html);
    document.setDocumentMargin(0);
    document.setDefaultTextOption(option);
    // HTML carries family names only; re-resolve names that exist as family + style (e.g.
    // "Arial Black" on platforms whose database splits them) so the correct faces render.
    resolve_document_font_styles(document);
    scale_document_font_widths(document, metric_scale);
    scale_document_font_sizes(document, layout_scale);
  } else {
    document.setPlainText(settings.text);
    apply_plain_text_format(document, result.font, color);
  }
  // Character-panel styles, then faux bold, before any layout: both change advances (and faux
  // bold strokes the shrunken superscript size, not the run's).
  apply_text_character_styles_to_document(document, settings.vertical);
  // Faux bold before any layout: its stroke widens every advance, so the lines have to be laid
  // out with it already in place.
  apply_faux_bold_to_document(document);
  // NoWrap point text must size to the tight glyph idealWidth.  Passing an explicit textWidth makes
  // Qt report THAT width from document.size().width() (not the ideal width) whenever it exceeds the
  // content, which -- when max_width is a layer's already-scaled pixel bounds -- inflated the rendered
  // image and, after the document transform re-scaled it, produced a free-transform rect several times
  // wider than the glyphs.  Use -1 (auto) for point text so size().width() is the ideal content width.
  document.setTextWidth(settings.boxed && !settings.vertical ? static_cast<qreal>(result.text_width) : -1.0);
  if (!paragraph_runs.trimmed().isEmpty()) {
    apply_paragraph_runs_to_document(document, paragraph_runs, layout_scale);
  }
  apply_text_smoothing_to_document(document, settings.anti_alias);
  if (!settings.boxed || settings.vertical) {
    // Qt only honors paragraph alignment against a finite layout width; leaving point text
    // unconstrained (-1) silently lays every line out flush-left, so centered/right-justified
    // multi-line point text (Photoshop aligns each line around the type anchor) collapsed to
    // left-justified on commit.  The ideal width is the tight content width, so this keeps the
    // glyph-snug bounds the -1 form was chosen for while letting alignment position the lines.
    document.setTextWidth(document.idealWidth());
  }
  return result;
}

// The face name of the first serialized run (runs v5 column 12), for the options-bar style
// picker at session start. Empty when the runs predate v5 or the run uses Regular.
QString first_text_run_style_name(const QString& rich_text_runs) {
  for (const auto& raw_line : rich_text_runs.split(QLatin1Char('\n'))) {
    const auto fields = raw_line.trimmed().split(QLatin1Char('\t'));
    if (fields.size() < 13) {
      continue;  // version header, or a run format without the style column
    }
    const auto style = QString::fromUtf8(QByteArray::fromPercentEncoding(fields[12].toLatin1())).trimmed();
    if (!style.isEmpty()) {
      return style;
    }
  }
  return {};
}

}  // namespace patchy::ui
