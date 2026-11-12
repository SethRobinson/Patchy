// The visibility eye of the Layers panel, the Channels panel and the Layer Styles rows
// (GitHub issue 715): a hidden layer's eye has to read differently from a visible one at a
// glance, in every scheme and bundled theme, at 100% and on a 2x screen.
#include "core/layer.hpp"
#include "ui/action_icons.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_file.hpp"
#include "ui/theme_manager.hpp"
#include "ui/theme_palette.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QRegion>
#include <QString>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;

// Four rows, top to bottom: Title (visible), Sketch (hidden), Photo (visible), Paper (hidden).
patchy::Document eye_rows_document() {
  patchy::Document document(160, 120, patchy::PixelFormat::rgb8());
  auto& paper =
      document.add_pixel_layer("Paper", solid_pixels(160, 120, patchy::PixelFormat::rgb8(), QColor(238, 234, 224)));
  paper.set_visible(false);
  document.add_pixel_layer("Photo", solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(70, 130, 190)));
  auto& sketch =
      document.add_pixel_layer("Sketch", solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(210, 90, 60)));
  sketch.set_visible(false);
  document.add_pixel_layer("Title", solid_pixels(160, 120, patchy::PixelFormat::rgba8(), QColor(80, 170, 100)));
  return document;
}

// A layer whose style lists an enabled and a disabled effect, for the Layer Styles rows.
patchy::Document styled_rows_document() {
  patchy::Document document(64, 48, patchy::PixelFormat::rgb8());
  document.add_pixel_layer("Background", solid_pixels(64, 48, patchy::PixelFormat::rgb8(), QColor(255, 255, 255)));
  auto& badge =
      document.add_pixel_layer("Badge", solid_pixels(64, 48, patchy::PixelFormat::rgba8(), QColor(220, 40, 40)));
  patchy::LayerStroke stroke;
  stroke.enabled = false;
  stroke.size = 3.0F;
  stroke.color = patchy::RgbColor{20, 40, 220};
  badge.layer_style().strokes.push_back(stroke);
  patchy::LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.color = patchy::RgbColor{20, 200, 40};
  badge.layer_style().color_overlays.push_back(overlay);
  document.set_active_layer(badge.id());
  return document;
}

// `rect` of `widget` at 100% and, as a 2x screen paints it, at 200%.
void save_region_shots(const std::string& name, QWidget& widget, QRect rect) {
  rect = rect.intersected(widget.rect());
  if (rect.isEmpty()) {
    patchy::test::report_failure("capture '" + name + "' has nothing to show");
    return;
  }
  for (const int scale : {1, 2}) {
    QPixmap shot(rect.size() * scale);
    shot.setDevicePixelRatio(scale);
    shot.fill(Qt::transparent);
    widget.render(&shot, QPoint(), QRegion(rect));
    save_image_artifact(name + (scale == 1 ? "_100" : "_200"), shot.toImage());
  }
}

struct ShotTheme {
  const char* label;
  patchy::ui::ColorSchemePreference scheme;
  const char* bundled;  // a bundled theme's file name, or null for the plain scheme
};

constexpr std::array<ShotTheme, 4> kShotThemes{{
    {"dark", patchy::ui::ColorSchemePreference::Dark, nullptr},
    {"light", patchy::ui::ColorSchemePreference::Light, nullptr},
    {"medium_gray", patchy::ui::ColorSchemePreference::Dark, "medium-gray.patchytheme"},
    {"high_contrast", patchy::ui::ColorSchemePreference::Dark, "high-contrast.patchytheme"},
}};

void apply_shot_theme(const ShotTheme& shot_theme) {
  auto& manager = patchy::ui::ThemeManager::instance();
  manager.clear_custom_theme(/*persist=*/false);
  ColorSchemeRestorer::apply(shot_theme.scheme);
  if (shot_theme.bundled != nullptr) {
    const QString id = patchy::ui::kBundledThemeIdPrefix + QString::fromLatin1(shot_theme.bundled);
    const auto loaded = patchy::ui::load_theme_by_id(id);
    CHECK(loaded.theme.has_value());
    if (loaded.theme) {
      manager.set_custom_theme(id, *loaded.theme, /*persist=*/false);
    }
  }
  QApplication::processEvents();
}

[[nodiscard]] double relative_luminance(const QColor& color) {
  const auto channel = [](double value) {
    return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}

[[nodiscard]] double contrast_ratio(const QColor& first, const QColor& second) {
  const double a = relative_luminance(first);
  const double b = relative_luminance(second);
  return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

// The eye's two colors against a layer row, in Dark, Light and every bundled theme: the
// visible eye is strong ink, the hidden one at most a third of its contrast, and the hidden
// one still shows on a plain row and on a selected row. Light used to derive both from Dark
// and landed them 2.5 times apart; Medium Gray and High Contrast about 2 times.
void ui_layer_eye_hidden_reads_weaker_in_every_theme() {
  struct NamedPalette {
    QString name;
    patchy::ui::ThemePalette palette;
  };
  std::vector<NamedPalette> palettes{{QStringLiteral("Dark"), patchy::ui::dark_palette()},
                                     {QStringLiteral("Light"), patchy::ui::light_palette()}};
  const auto bundled = patchy::ui::bundled_theme_file_names();
  CHECK(bundled.size() >= 8);
  for (const auto& name : bundled) {
    const auto loaded = patchy::ui::load_theme_by_id(patchy::ui::kBundledThemeIdPrefix + name);
    CHECK(loaded.theme.has_value());
    if (loaded.theme) {
      palettes.push_back({name, loaded.theme->palette});
    }
  }

  constexpr double kMinimumVisible = 6.0;           // the visible eye on a row
  constexpr double kMinimumSeparation = 3.0;        // visible contrast over hidden contrast
  constexpr double kMinimumHidden = 1.8;            // the hidden eye on a row
  constexpr double kMinimumHiddenOnSelected = 1.5;  // and on a selected row
  for (const auto& [name, palette] : palettes) {
    const double visible = contrast_ratio(palette.layer_eye_on, palette.layer_row_bg);
    const double hidden = contrast_ratio(palette.layer_eye_off, palette.layer_row_bg);
    const double hidden_selected = contrast_ratio(palette.layer_eye_off, palette.layer_row_selected_bg);
    const bool ok = visible >= kMinimumVisible && visible / hidden >= kMinimumSeparation &&
                    hidden >= kMinimumHidden && hidden_selected >= kMinimumHiddenOnSelected;
    if (!ok) {
      fprintf(stderr,
              "  %s: visible %.2f (want >= %.1f), hidden %.2f (want >= %.1f), visible/hidden %.2f (want >= %.1f), "
              "hidden on a selected row %.2f (want >= %.1f)\n",
              name.toUtf8().constData(), visible, kMinimumVisible, hidden, kMinimumHidden, visible / hidden,
              kMinimumSeparation, hidden_selected, kMinimumHiddenOnSelected);
    }
    CHECK(ok);
  }
}

// The glyphs themselves, at the sizes the panels draw them and on a 2x screen: the visible eye
// is an open almond with a solid pupil and clear space between the two; the hidden one is a
// closed lid, lower, lighter and a different outline (not the same eye struck through).
void ui_layer_eye_icons_read_as_open_and_closed() {
  const auto render = [](const char* name, int size, qreal ratio) {
    return patchy::ui::simple_icon(QString::fromLatin1(name), QColor(Qt::white))
        .pixmap(QSize(size, size), ratio)
        .toImage()
        .convertToFormat(QImage::Format_ARGB32);
  };
  const auto is_ink = [](const QImage& image, int x, int y) { return image.pixelColor(x, y).alpha() > 96; };
  struct Ink {
    QRect bounds;
    int count = 0;
  };
  const auto ink_of = [&](const QImage& image) {
    Ink ink;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        if (is_ink(image, x, y)) {
          ink.bounds = ink.bounds.united(QRect(x, y, 1, 1));
          ++ink.count;
        }
      }
    }
    return ink;
  };

  // The sizes in use (16 nested rows, 18 Channels, 20 layer rows) at 100%, 150% and 200%.
  constexpr std::array<std::pair<int, double>, 8> kSizes{
      {{16, 1.0}, {18, 1.0}, {20, 1.0}, {16, 1.5}, {20, 1.5}, {16, 2.0}, {18, 2.0}, {20, 2.0}}};
  for (const auto& [size, ratio] : kSizes) {
    const auto open = render("eye", size, ratio);
    const auto closed = render("eyeOff", size, ratio);
    const int extent = open.width();
    CHECK(extent == qRound(size * ratio) && closed.width() == extent);
    const int centre = extent / 2;
    const auto open_ink = ink_of(open);
    const auto closed_ink = ink_of(closed);
    CHECK(open_ink.count > 0 && closed_ink.count > 0);
    if (open_ink.count == 0 || closed_ink.count == 0) {
      continue;
    }

    // Open: a solid pupil, and between it and the upper lid a pixel that is clear.
    const int pupil = open.pixelColor(centre, centre).alpha();
    int clearest = 255;
    for (int y = open_ink.bounds.top(); y < centre; ++y) {
      clearest = std::min(clearest, open.pixelColor(centre, y).alpha());
    }
    const double open_width = static_cast<double>(open_ink.bounds.width()) / extent;
    const double open_height = static_cast<double>(open_ink.bounds.height()) / extent;

    // Closed: no pupil, nothing in the upper part of the icon, little shared with the open eye.
    const int closed_centre = closed.pixelColor(centre, centre).alpha();
    const double closed_top = static_cast<double>(closed_ink.bounds.top()) / extent;
    int shared = 0;
    for (int y = 0; y < extent; ++y) {
      for (int x = 0; x < extent; ++x) {
        shared += is_ink(closed, x, y) && is_ink(open, x, y) ? 1 : 0;
      }
    }
    const double shared_part = static_cast<double>(shared) / closed_ink.count;
    const double ink_part = static_cast<double>(closed_ink.count) / open_ink.count;

    const bool ok = pupil >= 200 && clearest <= 48 && open_width >= 0.80 && open_height >= 0.45 &&
                    closed_centre <= 48 && closed_top >= 0.28 && shared_part <= 0.6 && ink_part <= 0.7;
    if (!ok) {
      fprintf(stderr,
              "  %d px at %.0f%%: pupil %d, clearest %d, open %.2f x %.2f, closed centre %d, closed top %.2f, "
              "shared %.2f, closed/open ink %.2f\n",
              size, ratio * 100.0, pupil, clearest, open_width, open_height, closed_centre, closed_top, shared_part,
              ink_part);
    }
    CHECK(ok);
  }
}

// The row's eye button keeps the box the stylesheet gives it (22 px inside a 1 px border), so
// the hit zone and the row height are what they were, and draws a 20 px eye in it; the Layer
// Styles rows' eyes keep theirs (20 px inside the border) with a 16 px eye.
void ui_layer_eye_fills_its_button() {
  auto&& [window, canvas] = ShownWindowFixture{};
  window.add_document_session(styled_rows_document(), QStringLiteral("Styles"));
  QApplication::processEvents();
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list == nullptr) {
    return;
  }
  for (int index = 0; index < layer_list->count(); ++index) {
    auto* row = layer_list->itemWidget(layer_list->item(index));
    auto* eye = row != nullptr ? row->findChild<QToolButton*>(QStringLiteral("layerVisibilityCheck")) : nullptr;
    CHECK(eye != nullptr);
    if (eye == nullptr) {
      continue;
    }
    CHECK(eye->size() == QSize(24, 24));
    CHECK(eye->iconSize() == QSize(20, 20));
    // The eye's ink is at least 16 px wide at 100% (it was 12).
    const auto image = eye->icon().pixmap(eye->iconSize(), 1.0).toImage();
    int left = image.width();
    int right = -1;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        if (image.pixelColor(x, y).alpha() > 96) {
          left = std::min(left, x);
          right = std::max(right, x);
        }
      }
    }
    CHECK(right - left + 1 >= 16);
  }
  auto* badge = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Badge")));
  auto* disclosure =
      badge != nullptr ? badge->findChild<QToolButton*>(QStringLiteral("layerEffectsDisclosureButton")) : nullptr;
  CHECK(disclosure != nullptr);
  if (disclosure == nullptr) {
    return;
  }
  if (!disclosure->isChecked()) {
    const auto global = disclosure->mapToGlobal(disclosure->rect().center());
    click_widget_like_a_user(*layer_list->viewport(), layer_list->viewport()->mapFromGlobal(global));
    for (int pass = 0; pass < 4; ++pass) {
      QApplication::processEvents();
    }
  }
  badge = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Badge")));
  CHECK(badge != nullptr);
  if (badge == nullptr) {
    return;
  }
  int nested = 0;
  for (auto* button : badge->findChildren<QToolButton*>()) {
    if (button->objectName() == QStringLiteral("layerVisibilityCheck") || !button->isCheckable() ||
        button->icon().isNull() || button->iconSize() != QSize(16, 16)) {
      continue;
    }
    CHECK(button->size() == QSize(22, 22));
    ++nested;
  }
  // The Layer Styles header's eye and one per effect (Stroke, Color Overlay).
  CHECK(nested >= 3);
}

// Card pictures (artifacts issue715_*): the layer rows with visible and hidden layers in Dark,
// Light and two bundled themes, plus the Layer Styles rows and the Channels panel, each at 100%
// and 200%.
void ui_layer_eye_issue_715_shots() {
  ColorSchemeRestorer restore_scheme;
  for (const auto& shot_theme : kShotThemes) {
    apply_shot_theme(shot_theme);
    const std::string prefix = std::string("issue715_") + shot_theme.label;
    {
      auto&& [window, canvas] = ShownWindowFixture{};
      patchy::ui::MainWindowTestAccess::panel_column(window)->set_panel_expanded(QStringLiteral("colorDock"), false);
      window.add_document_session(eye_rows_document(), QStringLiteral("Eyes"));
      QApplication::processEvents();
      auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
      CHECK(layer_list != nullptr);
      if (layer_list == nullptr) {
        continue;
      }
      const auto rows_rect = [&] {
        QRect rect;
        for (int row = 0; row < layer_list->count(); ++row) {
          rect = rect.united(layer_list->visualItemRect(layer_list->item(row)));
        }
        rect.setWidth(std::min(rect.width(), 230));
        return QRect(layer_list->viewport()->mapTo(layer_list, rect.topLeft()), rect.size()).adjusted(-2, -2, 2, 2);
      };
      const auto select = [&](const char* name) {
        auto* item = require_layer_item(*layer_list, QString::fromLatin1(name));
        layer_list->clearSelection();
        layer_list->setCurrentItem(item);
        item->setSelected(true);
        QApplication::processEvents();
      };
      select("Photo");
      save_region_shots(prefix + "_rows", *layer_list, rows_rect());
      // The hardest case for the hidden eye's color: a hidden layer's row selected.
      select("Sketch");
      save_region_shots(prefix + "_rows_hidden_selected", *layer_list, rows_rect());
      if (auto* panel = window.findChild<QWidget*>(QStringLiteral("layersPanel")); panel != nullptr) {
        save_widget_artifact(prefix + "_layers_panel", *panel);
      }

      patchy::ui::MainWindowTestAccess::show_panel(window, QStringLiteral("channelsDock"));
      QApplication::processEvents();
      auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
      CHECK(channels != nullptr);
      if (channels != nullptr && channels->count() >= 4) {
        // Hide Blue through its eye, so the list shows both states.
        const auto blue = channels->visualItemRect(channels->item(3));
        click_widget_like_a_user(*channels->viewport(), QPoint(blue.left() + 10, blue.center().y()));
        QApplication::processEvents();
        QRect rect;
        for (int row = 0; row < channels->count(); ++row) {
          rect = rect.united(channels->visualItemRect(channels->item(row)));
        }
        rect.setWidth(std::min(rect.width(), 230));
        save_region_shots(prefix + "_channels", *channels,
                          QRect(channels->viewport()->mapTo(channels, rect.topLeft()), rect.size()).adjusted(-2, -2, 2, 2));
      }
    }
    {
      auto&& [window, canvas] = ShownWindowFixture{};
      patchy::ui::MainWindowTestAccess::panel_column(window)->set_panel_expanded(QStringLiteral("colorDock"), false);
      window.add_document_session(styled_rows_document(), QStringLiteral("Styles"));
      QApplication::processEvents();
      auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
      CHECK(layer_list != nullptr);
      if (layer_list == nullptr) {
        continue;
      }
      auto* row = layer_list->itemWidget(require_layer_item(*layer_list, QStringLiteral("Badge")));
      auto* disclosure =
          row != nullptr ? row->findChild<QToolButton*>(QStringLiteral("layerEffectsDisclosureButton")) : nullptr;
      CHECK(disclosure != nullptr);
      if (disclosure != nullptr && !disclosure->isChecked()) {
        const auto global = disclosure->mapToGlobal(disclosure->rect().center());
        click_widget_like_a_user(*layer_list->viewport(), layer_list->viewport()->mapFromGlobal(global));
        for (int pass = 0; pass < 4; ++pass) {
          QApplication::processEvents();
        }
      }
      QRect rect;
      for (int index = 0; index < layer_list->count(); ++index) {
        rect = rect.united(layer_list->visualItemRect(layer_list->item(index)));
      }
      rect.setWidth(std::min(rect.width(), 230));
      save_region_shots(prefix + "_style_rows", *layer_list,
                        QRect(layer_list->viewport()->mapTo(layer_list, rect.topLeft()), rect.size()).adjusted(-2, -2, 2, 2));
    }
  }
  patchy::ui::ThemeManager::instance().clear_custom_theme(/*persist=*/false);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_eye_icon_tests() {
  return {
      {"ui_layer_eye_hidden_reads_weaker_in_every_theme", ui_layer_eye_hidden_reads_weaker_in_every_theme},
      {"ui_layer_eye_icons_read_as_open_and_closed", ui_layer_eye_icons_read_as_open_and_closed},
      {"ui_layer_eye_fills_its_button", ui_layer_eye_fills_its_button},
      {"ui_layer_eye_issue_715_shots", ui_layer_eye_issue_715_shots},
  };
}
