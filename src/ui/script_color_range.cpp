// doc.selection.selectColorRange (GitHub issue 709; docs/color-range.md, docs/scripting.md):
// Select > Color Range's mask from each pixel's own color, combined with the selection by
// plain boolean. The same core classifier as the dialog's OK.

#include "ui/script_api.hpp"
#include "ui/script_vector.hpp"

#include "core/color_range.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/color_range_dialog.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/script_engine.hpp"
#include "ui/vector_operations.hpp"

#include <QColor>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRect>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <vector>

namespace patchy::ui {
namespace {

int color_range_operation_index(const QString& operation) {
  const QStringList operations{QStringLiteral("replace"), QStringLiteral("add"), QStringLiteral("subtract"),
                               QStringLiteral("intersect")};
  const auto index = operations.indexOf(operation);
  if (index < 0) {
    script_vector::invalid(QStringLiteral("operation"));
  }
  return static_cast<int>(index);
}

// An array of "#rrggbb" strings (or named colors) under `key`, appended as samples.
void append_color_samples(const QJsonObject& args, const QString& key, bool subtract,
                          std::vector<ColorRangeSample>& samples) {
  if (!args.contains(key)) {
    return;
  }
  const auto value = args.value(key);
  if (!value.isArray()) {
    script_vector::invalid(key);
  }
  for (const auto entry : value.toArray()) {
    const QColor color(entry.toString());
    if (!entry.isString() || !color.isValid()) {
      script_vector::invalid(key);
    }
    samples.push_back(ColorRangeSample{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                                       static_cast<std::uint8_t>(color.blue()), subtract});
  }
}

}  // namespace

QString ScriptEngineHost::select_color_range(std::int64_t session_id, const ColorRangeOptions& options, int operation,
                                             bool sample_all_layers) {
  auto* canvas = session_canvas(session_id);
  const auto* document = session_document_const(session_id);
  if (canvas == nullptr || document == nullptr) {
    return tr("The document is no longer open.");
  }
  if (canvas->quick_mask_active()) {
    return tr("Exit Quick Mask before using selectColorRange.");
  }
  const QSize size(document->width(), document->height());
  QImage source;
  if (sample_all_layers) {
    source = qimage_from_document(*document, true).convertToFormat(QImage::Format_RGBA8888);
  } else {
    const auto active = document->active_layer_id();
    if (!active.has_value()) {
      return tr("sampleAllLayers false needs an active layer.");
    }
    QString error;
    source = automatic_selection_layer_image(*document, *active, &error);
    if (source.isNull()) {
      return error;
    }
  }
  if (source.isNull() || source.size() != size) {
    return tr("selectColorRange could not read the document.");
  }

  const auto mask = color_range_mask_image(source, options, [this] { keep_alive_for_ui(); });
  PixelBuffer coverage(size.width(), size.height(), PixelFormat::gray8());
  for (int y = 0; y < size.height(); ++y) {
    std::memcpy(coverage.row(y).data(), mask.constScanLine(y), static_cast<std::size_t>(size.width()));
  }
  if (canvas->has_selection()) {
    combine_selection_coverage(coverage, operation, selection_mask_pixels(*canvas, QRect(QPoint(0, 0), size)));
  } else if (operation == 2) {
    coverage.clear(0);  // nothing to subtract from
  }
  if (engine()->isInterrupted() || !prepare_mutation(session_id) || engine()->isInterrupted()) {
    return tr("The script was interrupted.");
  }
  canvas = session_canvas(session_id);
  if (canvas == nullptr) {
    return tr("The document is no longer open.");
  }
  canvas->apply_grayscale_to_selection(coverage);
  note_pixels_changed(session_id, {});
  return {};
}

void ScriptSelectionObject::selectColorRange(const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  ColorRangeOptions range;
  int operation = 0;
  bool sample_all_layers = true;
  try {
    const auto args = script_vector::object(options, true);
    script_vector::keys(args, {QStringLiteral("range"), QStringLiteral("colors"), QStringLiteral("subtractColors"),
                               QStringLiteral("fuzziness"), QStringLiteral("invert"), QStringLiteral("operation"),
                               QStringLiteral("sampleAllLayers")});
    const auto mode = color_range_mode_from_token(
        script_vector::string(args, QStringLiteral("range"), QStringLiteral("sampled")).toStdString());
    if (!mode.has_value()) {
      script_vector::invalid(QStringLiteral("range"));
    }
    range.mode = *mode;
    append_color_samples(args, QStringLiteral("colors"), false, range.samples);
    append_color_samples(args, QStringLiteral("subtractColors"), true, range.samples);
    if (range.samples.size() > kMaxColorRangeSamples) {
      script_vector::invalid(QStringLiteral("colors (at most %1 with subtractColors)").arg(kMaxColorRangeSamples));
    }
    if (range.mode == ColorRangeMode::SampledColors &&
        std::none_of(range.samples.begin(), range.samples.end(),
                     [](const ColorRangeSample& sample) { return !sample.subtract; })) {
      script_vector::invalid(QStringLiteral("colors (range \"sampled\" needs at least one)"));
    }
    range.fuzziness = script_vector::integer(args, QStringLiteral("fuzziness"), kDefaultColorRangeFuzziness,
                                             kMinColorRangeFuzziness, kMaxColorRangeFuzziness);
    range.invert = script_vector::boolean(args, QStringLiteral("invert"), false);
    operation = color_range_operation_index(
        script_vector::string(args, QStringLiteral("operation"), QStringLiteral("replace")));
    sample_all_layers = script_vector::boolean(args, QStringLiteral("sampleAllLayers"), true);
  } catch (const std::exception& error) {
    host_.throw_js_error(
        ScriptEngineHost::tr("Invalid selectColorRange option: %1.").arg(QString::fromUtf8(error.what())));
    return;
  }
  if (const auto error = host_.select_color_range(session_id_, range, operation, sample_all_layers);
      !error.isEmpty()) {
    host_.throw_js_error(error);
  }
}

}  // namespace patchy::ui
