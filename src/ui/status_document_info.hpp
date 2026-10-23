#pragma once

#include "ui/measurement_units.hpp"

#include <QLabel>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace patchy {
class Document;
}

namespace patchy::ui {

// Memory the document's own buffers hold: every layer's pixels and raster mask plus
// the document channels, summed from byte sizes (O(layer count), never a flatten).
[[nodiscard]] std::uint64_t document_layer_bytes(const Document& document);

// The status-bar document info field (GitHub issues 202 and 206; see
// docs/view-navigation.md): one line with the active document's dimensions,
// resolution, color mode and bit depth, in that order. When the bar is narrow the
// trailing parts drop off whole; the dimensions always stay. The tooltip carries the
// whole line plus the color profile and memory use.
class StatusDocumentInfoLabel final : public QLabel {
  Q_OBJECT

public:
  // Inputs for the color part that live outside the Document: CMYK Proof is a
  // session state, shown through the working CMYK profile (docs/cmyk.md).
  struct ColorContext {
    bool cmyk_proof{false};
    QString cmyk_profile_name;
  };

  struct Info {
    QStringList parts;  // most important first
    QString tooltip;
  };

  explicit StatusDocumentInfoLabel(QWidget* parent = nullptr);

  [[nodiscard]] static Info describe(const Document& document, const ColorContext& color);
  [[nodiscard]] static QString separator();

  // Shows `info` or, with no parts, hides the field. A no-op when nothing changed.
  void set_info(const Info& info);
  // The most text width the field may take; trailing parts that do not fit drop.
  void set_width_budget(int pixels);
  [[nodiscard]] const QStringList& parts() const noexcept { return parts_; }

private:
  void update_shown_text();

  QStringList parts_;
  int width_budget_{0};
};

// The status-bar selection readout (GitHub issue 355; docs/view-navigation.md): the
// selection's bounding box as X, Y, W and H in the ruler unit, after the document info
// field. Shown only while a selection exists or a marquee or lasso drag is in progress.
class StatusSelectionInfoLabel final : public QLabel {
  Q_OBJECT

public:
  explicit StatusSelectionInfoLabel(QWidget* parent = nullptr);

  // The readout for `bounds` (document pixels) in `unit`: physical units convert per
  // axis through the document's resolution, Percent against the document's extent.
  [[nodiscard]] static QString describe(QRect bounds, MeasurementUnit unit, QSize document_size,
                                        double horizontal_ppi, double vertical_ppi);
  [[nodiscard]] static QString tooltip_text();

  // Shows `text`, or hides the field when it is empty. While shown the field only
  // grows, so the status message beside it holds still during a drag.
  void set_readout(const QString& text);
};

}  // namespace patchy::ui
