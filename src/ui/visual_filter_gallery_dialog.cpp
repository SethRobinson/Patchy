#include "ui/visual_filter_gallery_dialog.hpp"

#include "ui/dialog_utils.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/zoomable_image_preview.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

constexpr int kFilterIdRole = Qt::UserRole + 1;
constexpr int kMaximumProxyDimension = 640;
constexpr int kMaximumThumbnailDimension = 180;

constexpr std::array<const char*, 7> kPhotoLookIds = {
    "patchy.filters.soft_glow",       "patchy.filters.punchy_color",
    "patchy.filters.noir",            "patchy.filters.cinematic_matte",
    "patchy.filters.vintage_fade",    "patchy.filters.sepia",
    "patchy.filters.vignette",
};

struct GalleryProxy {
  PixelBuffer original;
  QRegion selection;
  bool selection_restricted{false};
  double spatial_scale{1.0};
};

[[nodiscard]] std::uint8_t clamp_byte(double value) {
  return static_cast<std::uint8_t>(
      std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

// Bounded premultiplied bilinear resampling. It reads the immutable source in
// place and allocates only the at-most-640px proxy, even for very large layers.
[[nodiscard]] PixelBuffer make_proxy_pixels(const PixelBuffer& source,
                                            int width, int height) {
  if (source.width() == width && source.height() == height) {
    return source;
  }
  PixelBuffer result(width, height, source.format());
  const auto channels = source.format().channels;
  const auto scale_x = static_cast<double>(source.width()) / width;
  const auto scale_y = static_cast<double>(source.height()) / height;
  for (int y = 0; y < height; ++y) {
    const auto sy = (y + 0.5) * scale_y - 0.5;
    const auto y0 = std::clamp(static_cast<int>(std::floor(sy)), 0,
                               source.height() - 1);
    const auto y1 = std::min(y0 + 1, source.height() - 1);
    const auto fy = std::clamp(sy - std::floor(sy), 0.0, 1.0);
    for (int x = 0; x < width; ++x) {
      const auto sx = (x + 0.5) * scale_x - 0.5;
      const auto x0 = std::clamp(static_cast<int>(std::floor(sx)), 0,
                                 source.width() - 1);
      const auto x1 = std::min(x0 + 1, source.width() - 1);
      const auto fx = std::clamp(sx - std::floor(sx), 0.0, 1.0);
      const std::array<const std::uint8_t*, 4> samples = {
          source.pixel(x0, y0), source.pixel(x1, y0),
          source.pixel(x0, y1), source.pixel(x1, y1)};
      const std::array<double, 4> weights = {
          (1.0 - fx) * (1.0 - fy), fx * (1.0 - fy),
          (1.0 - fx) * fy, fx * fy};
      auto* destination = result.pixel(x, y);
      double alpha = 0.0;
      std::array<double, 3> premultiplied{};
      for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto sample_alpha =
            channels >= 4 ? samples[index][3] / 255.0 : 1.0;
        alpha += weights[index] * sample_alpha;
        for (int channel = 0; channel < 3; ++channel) {
          premultiplied[static_cast<std::size_t>(channel)] +=
              weights[index] * samples[index][channel] * sample_alpha;
        }
      }
      for (int channel = 0; channel < 3; ++channel) {
        destination[channel] =
            alpha > 0.0
                ? clamp_byte(premultiplied[static_cast<std::size_t>(channel)] /
                             alpha)
                : 0;
      }
      if (channels >= 4) {
        destination[3] = clamp_byte(alpha * 255.0);
      }
    }
  }
  return result;
}

[[nodiscard]] GalleryProxy make_gallery_proxy(const PixelBuffer& source,
                                               Rect bounds,
                                               const QRegion& selection,
                                               int maximum_dimension) {
  GalleryProxy proxy;
  if (source.empty() || source.format().bit_depth != BitDepth::UInt8 ||
      source.format().channels < 3) {
    return proxy;
  }
  const auto largest = std::max(source.width(), source.height());
  proxy.spatial_scale =
      largest > maximum_dimension
          ? static_cast<double>(maximum_dimension) / largest
          : 1.0;
  const auto width = std::max(
      1, static_cast<int>(std::lround(source.width() * proxy.spatial_scale)));
  const auto height = std::max(
      1, static_cast<int>(std::lround(source.height() * proxy.spatial_scale)));
  proxy.original = make_proxy_pixels(source, width, height);
  proxy.selection_restricted = !selection.isEmpty();
  if (!proxy.selection_restricted) {
    return proxy;
  }

  const QRect source_bounds(bounds.x, bounds.y, source.width(), source.height());
  const auto selected = selection.intersected(QRegion(source_bounds));
  const auto scale_x = static_cast<double>(width) / source.width();
  const auto scale_y = static_cast<double>(height) / source.height();
  for (const auto& rect : selected) {
    const auto local_left = rect.left() - bounds.x;
    const auto local_top = rect.top() - bounds.y;
    const auto local_right = rect.right() + 1 - bounds.x;
    const auto local_bottom = rect.bottom() + 1 - bounds.y;
    const auto left = static_cast<int>(std::floor(local_left * scale_x));
    const auto top = static_cast<int>(std::floor(local_top * scale_y));
    const auto right = static_cast<int>(std::ceil(local_right * scale_x));
    const auto bottom = static_cast<int>(std::ceil(local_bottom * scale_y));
    proxy.selection += QRect(left, top, std::max(1, right - left),
                             std::max(1, bottom - top));
  }
  proxy.selection &= QRegion(QRect(0, 0, width, height));
  return proxy;
}

[[nodiscard]] QImage image_from_pixels(const PixelBuffer& pixels) {
  if (pixels.empty()) {
    return {};
  }
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  const auto channels = pixels.format().channels;
  for (int y = 0; y < pixels.height(); ++y) {
    auto* row = image.scanLine(y);
    for (int x = 0; x < pixels.width(); ++x) {
      const auto* source = pixels.pixel(x, y);
      row[x * 4 + 0] = source[0];
      row[x * 4 + 1] = source[1];
      row[x * 4 + 2] = source[2];
      row[x * 4 + 3] = channels >= 4 ? source[3] : 255;
    }
  }
  return image;
}

[[nodiscard]] QImage render_proxy(const GalleryProxy& proxy,
                                  const FilterRegistry& registry,
                                  const FilterInvocation& invocation) {
  if (proxy.original.empty() ||
      (proxy.selection_restricted && proxy.selection.isEmpty())) {
    return image_from_pixels(proxy.original);
  }
  const auto scaled = registry.scale(invocation, proxy.spatial_scale);
  if (!scaled.has_value()) {
    return image_from_pixels(proxy.original);
  }
  const Rect local_bounds =
      Rect::from_size(proxy.original.width(), proxy.original.height());
  const FilterPreviewSettings settings{true, *scaled};
  return image_from_pixels(build_filter_preview_pixels(
      proxy.original, proxy.selection, local_bounds, registry, settings));
}

[[nodiscard]] QIcon thumbnail_icon(const QImage& source) {
  constexpr int width = 128;
  constexpr int height = 78;
  QImage thumbnail(width, height, QImage::Format_ARGB32_Premultiplied);
  QPainter painter(&thumbnail);
  constexpr int tile = 10;
  for (int y = 0; y < height; y += tile) {
    for (int x = 0; x < width; x += tile) {
      const auto light = ((x / tile) + (y / tile)) % 2 == 0;
      painter.fillRect(QRect(x, y, tile, tile),
                       light ? QColor(218, 220, 224)
                             : QColor(184, 187, 192));
    }
  }
  if (!source.isNull()) {
    const auto scaled = source.scaled(width, height, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation);
    painter.drawImage((width - scaled.width()) / 2,
                      (height - scaled.height()) / 2, scaled);
  }
  painter.end();
  return QIcon(QPixmap::fromImage(thumbnail));
}

[[nodiscard]] double numeric_value(const FilterParameterValue& value,
                                   double fallback) {
  if (const auto* integer = std::get_if<std::int64_t>(&value)) {
    return static_cast<double>(*integer);
  }
  if (const auto* number = std::get_if<double>(&value)) {
    return *number;
  }
  return fallback;
}

}  // namespace

VisualFilterGalleryResult request_visual_filter_gallery(
    QWidget* parent, const PixelBuffer& immutable_original, Rect bounds,
    const QRegion& selection, const FilterRegistry& registry,
    RgbColor foreground, RgbColor background,
    VisualFilterGalleryPreviewCallback preview_changed) {
  const auto proxy =
      make_gallery_proxy(immutable_original, bounds, selection,
                         kMaximumProxyDimension);
  const auto thumbnail_proxy =
      make_gallery_proxy(immutable_original, bounds, selection,
                         kMaximumThumbnailDimension);
  const auto original_image = image_from_pixels(proxy.original);
  const auto original_thumbnail = image_from_pixels(thumbnail_proxy.original);

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("filterGalleryDialog"));
  dialog.setWindowTitle(QObject::tr("Visual Filters & Looks"));
  dialog.resize(1120, 720);
  dialog.setMinimumSize(880, 560);
  auto* root = new QVBoxLayout(&dialog);
  root->setContentsMargins(10, 10, 10, 10);
  root->setSpacing(8);

  auto* content = new QHBoxLayout();
  content->setSpacing(10);
  root->addLayout(content, 1);

  auto* looks_column = new QVBoxLayout();
  auto* looks_heading = new QLabel(QObject::tr("Looks"), &dialog);
  looks_column->addWidget(looks_heading);
  auto* looks = new QListWidget(&dialog);
  looks->setObjectName(QStringLiteral("filterGalleryLooksList"));
  looks->setIconSize(QSize(128, 78));
  looks->setMinimumWidth(260);
  looks->setMaximumWidth(280);
  looks->setSpacing(3);
  looks_column->addWidget(looks, 1);
  content->addLayout(looks_column);

  auto* center = new QVBoxLayout();
  auto* preview = new ZoomableImagePreview(&dialog);
  preview->setObjectName(QStringLiteral("filterGalleryPreview"));
  preview->set_image(original_image);
  center->addWidget(preview, 1);

  auto* preview_controls = new QHBoxLayout();
  preview_controls->setSpacing(4);
  auto* before = new QPushButton(QObject::tr("Before"), &dialog);
  before->setObjectName(QStringLiteral("filterGalleryBeforeButton"));
  before->setToolTip(
      QObject::tr("Hold to compare with the unadjusted image"));
  preview_controls->addWidget(before);
  auto* canvas_preview =
      new QCheckBox(QObject::tr("Live Canvas Preview"), &dialog);
  canvas_preview->setObjectName(
      QStringLiteral("filterGalleryCanvasPreviewCheck"));
  canvas_preview->setChecked(true);
  preview_controls->addWidget(canvas_preview);
  preview_controls->addStretch(1);

  const auto make_zoom_button = [&dialog](const char* object_name,
                                          const QString& text,
                                          const QString& tooltip) {
    auto* button = new QToolButton(&dialog);
    button->setObjectName(QLatin1String(object_name));
    button->setText(text);
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
  };
  auto* zoom_fit = make_zoom_button(
      "filterGalleryZoomFit", QObject::tr("Fit"),
      QObject::tr("Fit the image in the preview"));
  auto* zoom_100 = make_zoom_button(
      "filterGalleryZoom100", QStringLiteral("100%"),
      QObject::tr("Zoom to 100% (1 image pixel = 1 screen pixel)"));
  auto* zoom_out = make_zoom_button("filterGalleryZoomOut", QStringLiteral("-"),
                                    QObject::tr("Zoom out"));
  auto* zoom_in = make_zoom_button("filterGalleryZoomIn", QStringLiteral("+"),
                                   QObject::tr("Zoom in"));
  auto* zoom_label = new QLabel(&dialog);
  zoom_label->setObjectName(QStringLiteral("filterGalleryZoomLabel"));
  zoom_label->setMinimumWidth(78);
  zoom_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  preview_controls->addWidget(zoom_fit);
  preview_controls->addWidget(zoom_100);
  preview_controls->addWidget(zoom_out);
  preview_controls->addWidget(zoom_in);
  preview_controls->addWidget(zoom_label);
  center->addLayout(preview_controls);
  content->addLayout(center, 1);

  auto* parameters = new QWidget(&dialog);
  parameters->setObjectName(QStringLiteral("filterGalleryParameters"));
  parameters->setFixedWidth(280);
  auto* parameter_layout = new QVBoxLayout(parameters);
  parameter_layout->setContentsMargins(10, 8, 10, 8);
  auto* parameter_heading = new QLabel(QObject::tr("Settings"), parameters);
  parameter_layout->addWidget(parameter_heading);
  auto* parameter_form_host = new QWidget(parameters);
  parameter_layout->addWidget(parameter_form_host);
  parameter_layout->addStretch(1);
  content->addWidget(parameters);

  auto* footer = new QHBoxLayout();
  auto* status = new QLabel(QObject::tr("Ready"), &dialog);
  status->setObjectName(QStringLiteral("filterGalleryStatusLabel"));
  footer->addWidget(status, 1);
  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
          QDialogButtonBox::Reset,
      &dialog);
  buttons->setObjectName(QStringLiteral("filterGalleryButtonBox"));
  if (auto* apply = buttons->button(QDialogButtonBox::Ok)) {
    apply->setText(QObject::tr("Apply"));
  }
  footer->addWidget(buttons);
  root->addLayout(footer);

  std::map<std::string, FilterInvocation, std::less<>> invocations;
  auto* original_item = new QListWidgetItem(QObject::tr("Original"), looks);
  original_item->setData(kFilterIdRole, QString());
  original_item->setIcon(thumbnail_icon(original_thumbnail));
  original_item->setData(kFilterIdRole + 1, true);
  for (const auto* id : kPhotoLookIds) {
    const auto* definition = registry.find(id);
    if (definition == nullptr) {
      continue;
    }
    invocations.emplace(id,
                        registry.default_invocation(id, foreground, background));
    auto* item = new QListWidgetItem(filter_display_name(*definition), looks);
    item->setData(kFilterIdRole, QString::fromLatin1(id));
    item->setData(kFilterIdRole + 1, false);
  }
  looks->setCurrentRow(0);

  const auto current_id = [looks] {
    const auto* item = looks->currentItem();
    return item == nullptr ? std::string{}
                           : item->data(kFilterIdRole).toString().toStdString();
  };
  const auto current_invocation = [&]() -> std::optional<FilterInvocation> {
    const auto id = current_id();
    if (id.empty()) {
      return std::nullopt;
    }
    const auto found = invocations.find(id);
    return found == invocations.end()
               ? std::optional<FilterInvocation>{}
               : std::optional<FilterInvocation>{found->second};
  };
  const auto emit_canvas_preview = [&] {
    if (preview_changed) {
      preview_changed(VisualFilterGalleryPreview{
          canvas_preview->isChecked(), current_invocation()});
    }
  };

  auto* central_timer = new QTimer(&dialog);
  central_timer->setSingleShot(true);
  central_timer->setInterval(35);
  const auto render_current = [&] {
    const auto invocation = current_invocation();
    if (!invocation.has_value()) {
      preview->set_image(original_image);
      status->setText(QObject::tr("Ready"));
      return;
    }
    status->setText(QObject::tr("Rendering preview..."));
    const auto rendered = render_proxy(proxy, registry, *invocation);
    preview->set_image(rendered);
    if (auto* item = looks->currentItem()) {
      item->setIcon(thumbnail_icon(rendered));
      item->setData(kFilterIdRole + 1, true);
    }
    status->setText(QObject::tr("Ready"));
  };
  QObject::connect(central_timer, &QTimer::timeout, &dialog, render_current);
  const auto schedule_render = [central_timer] { central_timer->start(); };

  const auto base_dialog_style = dialog.styleSheet();
  const auto spinbox_style = dialog_spinbox_button_style();
  std::function<void()> rebuild_parameter_editor;
  rebuild_parameter_editor = [&] {
    delete parameter_form_host->layout();
    const auto children = parameter_form_host->findChildren<QWidget*>(
        QString(), Qt::FindDirectChildrenOnly);
    for (auto* child : children) {
      delete child;
    }
    auto* form = new QFormLayout(parameter_form_host);
    form->setContentsMargins(0, 4, 0, 0);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(8);

    const auto id = current_id();
    const auto invocation_it = invocations.find(id);
    const auto* definition = registry.find(id);
    if (id.empty() || invocation_it == invocations.end() ||
        definition == nullptr) {
      auto* hint = new QLabel(
          QObject::tr("Choose a look to adjust its settings."),
          parameter_form_host);
      hint->setWordWrap(true);
      form->addRow(hint);
    } else {
      const auto spec = filter_dialog_spec_for(*definition);
      for (const auto& control : spec.controls) {
        if (control.kind != FilterParameterKind::Integer) {
          continue;
        }
        auto* row_host = new QWidget(parameter_form_host);
        auto* row = new QHBoxLayout(row_host);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        auto* slider = new QSlider(Qt::Horizontal, row_host);
        slider->setObjectName(control.object_name + QStringLiteral("Slider"));
        slider->setRange(control.minimum, control.maximum);
        auto* spin = new QSpinBox(row_host);
        spin->setObjectName(control.object_name + QStringLiteral("Spin"));
        spin->setRange(control.minimum, control.maximum);
        if (!control.suffix.isEmpty()) {
          spin->setSuffix(control.suffix);
        }
        configure_dialog_spinbox(spin, 78);
        spin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
        const auto parameter =
            invocation_it->second.parameters.find(control.parameter_key);
        const auto value = std::clamp(
            static_cast<int>(std::lround(numeric_value(
                parameter != invocation_it->second.parameters.end()
                    ? parameter->second
                    : control.default_value,
                control.value))),
            control.minimum, control.maximum);
        slider->setValue(value);
        spin->setValue(value);
        row->addWidget(slider, 1);
        row->addWidget(spin);
        form->addRow(control.label, row_host);
        QObject::connect(slider, &QSlider::valueChanged, spin,
                         &QSpinBox::setValue);
        QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider,
                         &QSlider::setValue);
        QObject::connect(
            spin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
            [&, id, key = control.parameter_key](int changed) {
              if (const auto found = invocations.find(id);
                  found != invocations.end()) {
                found->second.parameters[key] =
                    static_cast<std::int64_t>(changed);
              }
              schedule_render();
              emit_canvas_preview();
            });
      }
    }
    // QStyleSheetStyle does not reliably lay out spin-box subcontrols that were
    // created after the parent stylesheet. Reapply one stable stylesheet after
    // each editor rebuild; never append to the current value.
    dialog.setStyleSheet(QString());
    dialog.setStyleSheet(base_dialog_style + spinbox_style);
  };

  preview->set_zoom_changed_callback([preview, zoom_label] {
    const auto percent = preview->property("previewZoomPercent").toInt();
    zoom_label->setText(preview->fit_mode()
                            ? QObject::tr("Fit (%1%)").arg(percent)
                            : QStringLiteral("%1%").arg(percent));
  });
  QObject::connect(zoom_fit, &QToolButton::clicked, &dialog,
                   [preview] { preview->zoom_to_fit(); });
  QObject::connect(zoom_100, &QToolButton::clicked, &dialog,
                   [preview] { preview->zoom_to(1.0); });
  QObject::connect(zoom_out, &QToolButton::clicked, &dialog,
                   [preview] { preview->zoom_step(-1); });
  QObject::connect(zoom_in, &QToolButton::clicked, &dialog,
                   [preview] { preview->zoom_step(1); });
  QObject::connect(before, &QPushButton::pressed, &dialog, [&] {
    central_timer->stop();
    preview->set_image(original_image);
    status->setText(QObject::tr("Before"));
  });
  QObject::connect(before, &QPushButton::released, &dialog, schedule_render);
  QObject::connect(canvas_preview, &QCheckBox::toggled, &dialog,
                   [&](bool) { emit_canvas_preview(); });
  QObject::connect(looks, &QListWidget::currentItemChanged, &dialog,
                   [&](QListWidgetItem*, QListWidgetItem*) {
                     rebuild_parameter_editor();
                     schedule_render();
                     emit_canvas_preview();
                   });

  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  if (auto* reset = buttons->button(QDialogButtonBox::Reset)) {
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] {
      const auto id = current_id();
      if (!id.empty()) {
        invocations[id] =
            registry.default_invocation(id, foreground, background);
      }
      rebuild_parameter_editor();
      schedule_render();
      emit_canvas_preview();
    });
  }

  // One proxy render per event-loop turn keeps opening the dialog immediate and
  // never lets thumbnail work block interaction for a whole batch.
  auto* thumbnail_timer = new QTimer(&dialog);
  thumbnail_timer->setInterval(1);
  int next_thumbnail = 1;
  QObject::connect(thumbnail_timer, &QTimer::timeout, &dialog, [&] {
    if (next_thumbnail >= looks->count()) {
      thumbnail_timer->stop();
      return;
    }
    auto* item = looks->item(next_thumbnail++);
    if (item->data(kFilterIdRole + 1).toBool()) {
      return;
    }
    const auto id = item->data(kFilterIdRole).toString().toStdString();
    if (const auto found = invocations.find(id); found != invocations.end()) {
      item->setIcon(thumbnail_icon(
          render_proxy(thumbnail_proxy, registry, found->second)));
      item->setData(kFilterIdRole + 1, true);
    }
  });

  rebuild_parameter_editor();
  thumbnail_timer->start();
  QTimer::singleShot(0, &dialog, [&] {
    if (dialog.isVisible()) {
      emit_canvas_preview();
    }
  });

  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return {};
  }
  const auto invocation = current_invocation();
  if (!invocation.has_value()) {
    return {VisualFilterGalleryOutcome::Original, std::nullopt};
  }
  return {VisualFilterGalleryOutcome::Filter, invocation};
}

}  // namespace patchy::ui
