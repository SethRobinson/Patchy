#include "ui_test_support.hpp"

#include "ui_test_access.hpp"

namespace patchy::test::ui {

patchy::PixelBuffer solid_pixels(std::int32_t width, std::int32_t height, patchy::PixelFormat format, QColor color) {
  patchy::PixelBuffer pixels(width, height, format);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      if (format.channels >= 4) {
        px[3] = static_cast<std::uint8_t>(color.alpha());
      }
    }
  }
  return pixels;
}

void fill_pixel_rect(patchy::PixelBuffer& pixels, QRect rect, QColor color) {
  rect = rect.intersected(QRect(0, 0, pixels.width(), pixels.height()));
  if (rect.isEmpty()) {
    return;
  }

  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    for (int x = rect.left(); x <= rect.right(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = static_cast<std::uint8_t>(color.red());
      px[1] = static_cast<std::uint8_t>(color.green());
      px[2] = static_cast<std::uint8_t>(color.blue());
      if (pixels.format().channels >= 4) {
        px[3] = static_cast<std::uint8_t>(color.alpha());
      }
    }
  }
}

void ensure_artifact_dir() {
  std::filesystem::create_directories("test-artifacts");
}

double text_points_for_pixels(int pixels, double ppi) noexcept {
  return static_cast<double>(pixels) * 72.0 / ppi;
}

void save_widget_artifact(const std::string& name, QWidget& widget) {
  ensure_artifact_dir();
  const auto path = QString::fromStdString((std::filesystem::path("test-artifacts") / (name + ".png")).string());
  const auto pixmap = widget.grab();
  CHECK(!pixmap.isNull());
  CHECK(pixmap.save(path));
}

void send_mouse(QWidget& widget, QEvent::Type type, QPoint position, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
  QMouseEvent event(type, position, widget.mapToGlobal(position), button, buttons, modifiers);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

const QPointingDevice& tablet_test_device(QPointingDevice::PointerType pointer_type, QInputDevice::Capabilities capabilities) {
  static QPointingDevice pen(QStringLiteral("Patchy test pen"), 1001, QInputDevice::DeviceType::Stylus,
                             QPointingDevice::PointerType::Pen,
                             QInputDevice::Capability::Position | QInputDevice::Capability::Pressure |
                                 QInputDevice::Capability::XTilt | QInputDevice::Capability::YTilt |
                                 QInputDevice::Capability::Rotation |
                                 QInputDevice::Capability::TangentialPressure |
                                 QInputDevice::Capability::ZPosition,
                             1, 3);
  static QPointingDevice eraser(QStringLiteral("Patchy test eraser"), 1002, QInputDevice::DeviceType::Stylus,
                                QPointingDevice::PointerType::Eraser,
                                QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                                1, 3);
  static QPointingDevice no_pressure(QStringLiteral("Patchy no-pressure pen"), 1003,
                                     QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                                     QInputDevice::Capability::Position, 1, 3);

  if (pointer_type == QPointingDevice::PointerType::Eraser) {
    return eraser;
  }
  if (!capabilities.testFlag(QInputDevice::Capability::Pressure)) {
    return no_pressure;
  }
  return pen;
}

void send_tablet(QWidget& widget, QEvent::Type type, QPoint position, qreal pressure, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers, QPointingDevice::PointerType pointer_type, QInputDevice::Capabilities capabilities, float x_tilt, float y_tilt, qreal rotation, float tangential_pressure, float z) {
  const auto& device = tablet_test_device(pointer_type, capabilities);
  QTabletEvent event(type, &device, QPointF(position), QPointF(widget.mapToGlobal(position)), pressure, x_tilt,
                     y_tilt, tangential_pressure, rotation, z, modifiers, button, buttons);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

void drag(QWidget& widget, QPoint from, QPoint to, Qt::KeyboardModifiers modifiers, Qt::MouseButton button) {
  send_mouse(widget, QEvent::MouseButtonPress, from, button, button, modifiers);
  send_mouse(widget, QEvent::MouseMove, to, Qt::NoButton, button, modifiers);
  send_mouse(widget, QEvent::MouseButtonRelease, to, button, Qt::NoButton, modifiers);
}

void send_double_click(QWidget& widget, QPoint position, Qt::KeyboardModifiers modifiers) {
  QMouseEvent event(QEvent::MouseButtonDblClick, position, widget.mapToGlobal(position), Qt::LeftButton,
                    Qt::LeftButton, modifiers);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

void send_key(QWidget& widget, int key, Qt::KeyboardModifiers modifiers) {
  QKeyEvent press(QEvent::KeyPress, key, modifiers);
  QApplication::sendEvent(&widget, &press);
  QKeyEvent release(QEvent::KeyRelease, key, modifiers);
  QApplication::sendEvent(&widget, &release);
  QApplication::processEvents();
}

void send_key_press(QWidget& widget, int key, Qt::KeyboardModifiers modifiers) {
  QKeyEvent event(QEvent::KeyPress, key, modifiers);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

void send_key_release(QWidget& widget, int key, Qt::KeyboardModifiers modifiers) {
  QKeyEvent event(QEvent::KeyRelease, key, modifiers);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

void send_wheel(QWidget& widget, QPoint position, int delta, Qt::KeyboardModifiers modifiers) {
  QWheelEvent event(QPointF(position), QPointF(widget.mapToGlobal(position)), QPoint(), QPoint(0, delta),
                    Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&widget, &event);
  QApplication::processEvents();
}

void send_layer_drag_enter(QListWidget& list, QPoint position, const std::vector<patchy::LayerId>& ids) {
  QMimeData mime_data;
  mime_data.setData(QString::fromLatin1(patchy::ui::kLayerDragMimeType), patchy::ui::layer_ids_to_mime_data(ids));
  QDragEnterEvent event(position, Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(list.viewport(), &event);
  QApplication::processEvents();
}

void send_layer_drag_move(QListWidget& list, QPoint position, const std::vector<patchy::LayerId>& ids) {
  QMimeData mime_data;
  mime_data.setData(QString::fromLatin1(patchy::ui::kLayerDragMimeType), patchy::ui::layer_ids_to_mime_data(ids));
  QDragMoveEvent event(position, Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(list.viewport(), &event);
  QApplication::processEvents();
}

void send_layer_drag_leave(QListWidget& list) {
  QDragLeaveEvent event;
  QApplication::sendEvent(list.viewport(), &event);
  QApplication::processEvents();
}

void send_layer_drop(QListWidget& list, QPoint position, const std::vector<patchy::LayerId>& ids) {
  QMimeData mime_data;
  mime_data.setData(QString::fromLatin1(patchy::ui::kLayerDragMimeType), patchy::ui::layer_ids_to_mime_data(ids));
  QDragEnterEvent enter(position, Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(list.viewport(), &enter);
  QDragMoveEvent move(position, Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(list.viewport(), &move);
  QDropEvent event(QPointF(position), Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(list.viewport(), &event);
  QApplication::processEvents();
  QApplication::processEvents();
}

void send_layer_button_drop(QWidget& button, const std::vector<patchy::LayerId>& ids) {
  QMimeData mime_data;
  mime_data.setData(QString::fromLatin1(patchy::ui::kLayerDragMimeType), patchy::ui::layer_ids_to_mime_data(ids));
  const auto position = button.rect().center();
  QDragEnterEvent enter(position, Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&button, &enter);
  QDragMoveEvent move(position, Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&button, &move);
  QDropEvent drop(QPointF(position), Qt::MoveAction, &mime_data, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&button, &drop);
  QApplication::processEvents();
  QApplication::processEvents();
}

QAction* require_action(QWidget& root, const char* object_name) {
  auto* action = root.findChild<QAction*>(QString::fromLatin1(object_name));
  CHECK(action != nullptr);
  return action;
}

QAction* require_hotkey_action(patchy::ui::MainWindow& window, const QString& id) {
  const auto* command = window.hotkey_registry().find_command(id);
  CHECK(command != nullptr);
  CHECK(command->action != nullptr);
  return command->action;
}

patchy::RgbColor filter_rgb(const QColor& color) {
  return patchy::RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                         static_cast<std::uint8_t>(color.blue())};
}

bool filter_rgb_equal(patchy::RgbColor lhs, patchy::RgbColor rhs) {
  return lhs.red == rhs.red && lhs.green == rhs.green && lhs.blue == rhs.blue;
}

bool filter_rect_equal(patchy::Rect lhs, patchy::Rect rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width && lhs.height == rhs.height;
}

bool smart_object_placements_equal(const std::optional<patchy::SmartObjectPlacement>& lhs, const std::optional<patchy::SmartObjectPlacement>& rhs) {
  if (!lhs.has_value() || !rhs.has_value()) {
    return lhs.has_value() == rhs.has_value();
  }
  return lhs->uuid == rhs->uuid && lhs->transform == rhs->transform &&
         lhs->width == rhs->width && lhs->height == rhs->height &&
         lhs->resolution == rhs->resolution &&
         lhs->placed_type == rhs->placed_type &&
         lhs->anti_alias == rhs->anti_alias;
}

patchy::FilterInvocation filter_invocation(patchy::FilterRegistry& registry, std::string_view id, QColor foreground, QColor background) {
  return registry.default_invocation(id, filter_rgb(foreground), filter_rgb(background));
}

void set_filter_integer(patchy::FilterInvocation& invocation, std::string key, int value) {
  invocation.parameters.insert_or_assign(std::move(key), static_cast<std::int64_t>(value));
}

int filter_integer(const patchy::FilterInvocation& invocation, std::string_view key) {
  const auto found = invocation.parameters.find(key);
  CHECK(found != invocation.parameters.end());
  const auto* value = std::get_if<std::int64_t>(&found->second);
  CHECK(value != nullptr);
  return static_cast<int>(*value);
}

QAction* find_action_by_text(QWidget& root, const QString& text) {
  const auto actions = root.findChildren<QAction*>();
  for (auto* action : actions) {
    if (action->menu() != nullptr) {
      continue;
    }
    if (action->text().remove('&') == text) {
      return action;
    }
  }
  return nullptr;
}

QAction* require_action_by_text(QWidget& root, const QString& text) {
  auto* action = find_action_by_text(root, text);
  CHECK(action != nullptr);
  return action;
}

QImage image_from_pixels_for_visuals(const patchy::PixelBuffer& pixels) {
  QImage image(pixels.width(), pixels.height(),
               pixels.format().channels >= 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
  image.fill(pixels.format().channels >= 4 ? Qt::transparent : Qt::black);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      image.setPixelColor(x, y, QColor(px[0], px[1], px[2], pixels.format().channels >= 4 ? px[3] : 255));
    }
  }
  return image;
}

QImage flattened_on_white(const patchy::PixelBuffer& pixels) {
  const auto source = image_from_pixels_for_visuals(pixels);
  QImage flattened(source.size(), QImage::Format_RGB32);
  flattened.fill(Qt::white);
  QPainter painter(&flattened);
  painter.drawImage(QPoint(0, 0), source);
  painter.end();
  return flattened;
}

patchy::PixelBuffer make_filter_stroke_source() {
  QImage image(220, 160, QImage::Format_ARGB32);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(QColor(220, 28, 24), 12, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(pen);
  painter.drawLine(QPoint(26, 132), QPoint(78, 28));
  painter.drawLine(QPoint(70, 88), QPoint(156, 45));
  painter.drawLine(QPoint(86, 118), QPoint(196, 122));
  painter.drawArc(QRect(62, 28, 78, 82), 30 * 16, 290 * 16);
  painter.drawArc(QRect(94, 72, 78, 52), 190 * 16, 250 * 16);
  painter.end();
  return patchy::ui::pixels_from_image_rgba(image);
}

patchy::Document make_filter_gallery_document(patchy::LayerId& layer_id, patchy::Rect& bounds, patchy::PixelBuffer& original_pixels) {
  original_pixels = make_filter_stroke_source();
  patchy::Document document(320, 240, patchy::PixelFormat::rgba8());
  patchy::Layer layer(document.allocate_layer_id(), "Gallery Subject", original_pixels);
  layer_id = layer.id();
  bounds = patchy::Rect{48, 38, original_pixels.width(), original_pixels.height()};
  layer.set_bounds(bounds);
  document.add_layer(std::move(layer));
  document.set_active_layer(layer_id);
  return document;
}

// `offset_x/offset_y` map an `after` pixel back to `before`-space; they are
// non-zero when the filter grew the layer (the grown buffer's (0,0) corresponds
// to before-space (offset_x, offset_y), which is negative). Pixels that fall
// outside `before` were transparent (nonexistent) before the filter ran.
bool spatial_filter_spreads_clean_red_alpha(const patchy::PixelBuffer& before, const patchy::PixelBuffer& after, int offset_x, int offset_y) {
  int spread_pixels = 0;
  bool clean_red = false;
  for (std::int32_t y = 0; y < after.height(); ++y) {
    for (std::int32_t x = 0; x < after.width(); ++x) {
      const auto bx = x + offset_x;
      const auto by = y + offset_y;
      const bool was_opaque = bx >= 0 && by >= 0 && bx < before.width() && by < before.height() &&
                              before.pixel(bx, by)[3] != 0;
      const auto* dst = after.pixel(x, y);
      if (!was_opaque && dst[3] > 8) {
        ++spread_pixels;
        clean_red = clean_red || (dst[0] > 170 && dst[1] < 90 && dst[2] < 90);
      }
    }
  }
  return spread_pixels > 0 && clean_red;
}

QAction* find_menu_action_by_text(QMenu& menu, const QString& text) {
  for (auto* action : menu.actions()) {
    auto action_text = action->text();
    action_text.remove('&');
    if (action_text == text) {
      return action;
    }
    // The layer context menu groups related actions in submenus (July 2026).
    if (auto* child_menu = action->menu(); child_menu != nullptr) {
      if (auto* nested = find_menu_action_by_text(*child_menu, text); nested != nullptr) {
        return nested;
      }
    }
  }
  return nullptr;
}

LayerStyleContextMenuState layer_style_context_menu_state(QListWidget& layer_list, QListWidgetItem& item) {
  LayerStyleContextMenuState state;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("layerContextMenu")) {
        continue;
      }
      if (auto* action = find_menu_action_by_text(*menu, QStringLiteral("Copy Layer Style")); action != nullptr) {
        state.saw_copy = true;
        state.copy_enabled = action->isEnabled();
      }
      if (auto* action = find_menu_action_by_text(*menu, QStringLiteral("Paste Layer Style")); action != nullptr) {
        state.saw_paste = true;
        state.paste_enabled = action->isEnabled();
      }
      if (auto* action = find_menu_action_by_text(*menu, QStringLiteral("Delete Layer Style")); action != nullptr) {
        state.saw_delete = true;
        state.delete_enabled = action->isEnabled();
      }
      menu->close();
      return;
    }
    CHECK(false);
  });

  const auto context_point = layer_list.visualItemRect(&item).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  layer_list.viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(layer_list.viewport(), &context_event);
  QApplication::processEvents();
  return state;
}

patchy::ui::CanvasWidget* require_canvas(patchy::ui::MainWindow& window) {
  auto* canvas = dynamic_cast<patchy::ui::CanvasWidget*>(window.centralWidget());
  if (canvas == nullptr) {
    auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
    if (tabs != nullptr) {
      canvas = dynamic_cast<patchy::ui::CanvasWidget*>(tabs->currentWidget());
    }
  }
  CHECK(canvas != nullptr);
  return canvas;
}

void show_window(patchy::ui::MainWindow& window) {
  window.resize(1180, 780);
  window.show();
  QApplication::processEvents();
  // Startup no longer auto-creates a document; the suite's tests predate that and
  // assume the historical 1024x768 canvas, so create it here for fresh windows.
  // Windows that already hold documents (opened before show) are left alone; use
  // show_window_empty to exercise the real empty-startup state.
  if (auto* tabs = qobject_cast<QTabWidget*>(window.centralWidget());
      tabs != nullptr && tabs->count() == 0 &&
      patchy::ui::MainWindowTestAccess::session_count(window) == 0) {
    patchy::ui::MainWindowTestAccess::create_default_document(window);
    QApplication::processEvents();
  }
}

void show_window_empty(patchy::ui::MainWindow& window) {
  window.resize(1180, 780);
  window.show();
  QApplication::processEvents();
}

void process_events_for(int milliseconds) {
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec(QEventLoop::AllEvents);
  QApplication::processEvents();
}

bool process_events_until(const std::function<bool()>& condition, int timeout_ms) {
  if (condition()) {
    return true;
  }
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    const auto remaining = std::max(1, timeout_ms - static_cast<int>(timer.elapsed()));
    process_events_for(std::min(20, remaining));
    if (condition()) {
      return true;
    }
  }
  return condition();
}

QDialog* find_top_level_dialog(const QString& object_name) {
  for (auto* widget : QApplication::topLevelWidgets()) {
    auto* dialog = qobject_cast<QDialog*>(widget);
    if (dialog != nullptr && dialog->objectName() == object_name) {
      return dialog;
    }
  }
  return nullptr;
}

void accept_missing_psd_text_font_warning_if_present() {
  QTimer::singleShot(0, [] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("missingPsdTextFontMessageBox")));
    if (dialog == nullptr) {
      return;
    }
    for (auto* button : dialog->findChildren<QPushButton*>()) {
      if (dialog->buttonRole(button) == QMessageBox::AcceptRole) {
        button->click();
        return;
      }
    }
    CHECK(false);
  });
}

void accept_compatibility_report_when_present(const std::shared_ptr<bool>& done, int attempts) {
  QTimer::singleShot(50, [done, attempts] {
    if (done == nullptr || *done) {
      return;
    }
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyCompatibilityReportDialog")));
    if (dialog != nullptr) {
      dialog->accept();
      *done = true;
      return;
    }
    if (attempts > 0) {
      accept_compatibility_report_when_present(done, attempts - 1);
    }
  });
}

QString write_psd_import_warning_fixture(const QString& file_name) {
  ensure_artifact_dir();
  const auto path = QFileInfo(QDir(QStringLiteral("test-artifacts")).filePath(file_name)).absoluteFilePath();
  patchy::Document document(8, 6, patchy::PixelFormat::rgb8());
  auto& layer = document.add_pixel_layer(
      "Unknown PSD Block", solid_pixels(8, 6, patchy::PixelFormat::rgba8(), QColor(40, 90, 220, 255)));
  layer.unknown_psd_blocks().push_back(patchy::UnknownPsdBlock{"zzzz", {1, 2, 3, 4}});
  patchy::psd::DocumentIo::write_layered_rgb8_file(document, std::filesystem::path(path.toStdString()));
  return path;
}

void verify_open_progress_dialog(const QString& expected_file_name, bool& saw_dialog) {
  auto* dialog = qobject_cast<QProgressDialog*>(find_top_level_dialog(QStringLiteral("openProgressDialog")));
  CHECK(dialog != nullptr);
  CHECK(dialog->isVisible());
  const auto title = dialog->windowTitle();
  CHECK(title.startsWith(QStringLiteral("Opening ")));
  CHECK(title != QStringLiteral("Opening File"));
  CHECK(!title.contains(QStringLiteral("Patchy")));
  const auto title_file_name = title.mid(QStringLiteral("Opening ").size());
  if (title_file_name.contains(QChar(0x2026))) {
    CHECK(title_file_name.size() < expected_file_name.size());
    CHECK(title_file_name.endsWith(QFileInfo(expected_file_name).suffix()));
  } else {
    CHECK(title_file_name == expected_file_name);
  }
  CHECK(dialog->windowModality() == Qt::WindowModal);
  CHECK(dialog->minimum() == 0);
  CHECK(dialog->maximum() == 0);
  CHECK(dialog->labelText().contains(expected_file_name));
  CHECK(dialog->findChildren<QPushButton*>().isEmpty());
  saw_dialog = true;
}

void cleanup_after_visual_test() {
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (qobject_cast<QDialog*>(widget) != nullptr || qobject_cast<QMenu*>(widget) != nullptr) {
      widget->close();
      widget->deleteLater();
    }
  }
  QApplication::processEvents();
  QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
  patchy::ui::LocalizationManager::instance().set_language(QStringLiteral("en"), false);
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("preferences/language"));
  settings.sync();
}

bool color_close(QColor actual, QColor expected, int tolerance) {
  return std::abs(actual.red() - expected.red()) <= tolerance &&
         std::abs(actual.green() - expected.green()) <= tolerance &&
         std::abs(actual.blue() - expected.blue()) <= tolerance;
}

bool images_equal_rgba(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) {
    return false;
  }
  const auto left_rgba = left.convertToFormat(QImage::Format_RGBA8888);
  const auto right_rgba = right.convertToFormat(QImage::Format_RGBA8888);
  const auto row_bytes = static_cast<std::size_t>(left_rgba.width()) * 4U;
  for (int y = 0; y < left_rgba.height(); ++y) {
    if (std::memcmp(left_rgba.constScanLine(y), right_rgba.constScanLine(y), row_bytes) != 0) {
      return false;
    }
  }
  return true;
}

std::optional<QRect> image_mismatch_bounds_rgba(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) {
    return QRect(QPoint(0, 0), left.size().expandedTo(right.size()));
  }
  const auto left_rgba = left.convertToFormat(QImage::Format_RGBA8888);
  const auto right_rgba = right.convertToFormat(QImage::Format_RGBA8888);
  int min_x = left_rgba.width();
  int min_y = left_rgba.height();
  int max_x = -1;
  int max_y = -1;
  for (int y = 0; y < left_rgba.height(); ++y) {
    const auto* left_row = left_rgba.constScanLine(y);
    const auto* right_row = right_rgba.constScanLine(y);
    for (int x = 0; x < left_rgba.width(); ++x) {
      if (std::memcmp(left_row + static_cast<std::size_t>(x) * 4U,
                      right_row + static_cast<std::size_t>(x) * 4U, 4U) == 0) {
        continue;
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }
  if (max_x < min_x || max_y < min_y) {
    return std::nullopt;
  }
  return QRect(QPoint(min_x, min_y), QPoint(max_x, max_y));
}

QImage render_widget_image(QWidget& widget, const QRegion& region) {
  QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  if (region.isEmpty()) {
    widget.render(&painter);
  } else {
    widget.render(&painter, QPoint(), region);
  }
  return image;
}

QImage grab_widget_window_image(QWidget& widget) {
  auto* screen = widget.windowHandle() != nullptr ? widget.windowHandle()->screen() : QApplication::primaryScreen();
  CHECK(screen != nullptr);
  const auto pixmap = screen->grabWindow(widget.winId());
  CHECK(!pixmap.isNull());
  return pixmap.toImage();
}

int count_pixels_close(const QImage& image, QRect region, QColor expected, int tolerance) {
  region = region.intersected(image.rect());
  int count = 0;
  for (int y = region.top(); y <= region.bottom(); ++y) {
    for (int x = region.left(); x <= region.right(); ++x) {
      if (color_close(image.pixelColor(x, y), expected, tolerance)) {
        ++count;
      }
    }
  }
  return count;
}

QColor canvas_pixel(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto widget_point = canvas.widget_position_for_document_point(document_point);
  return canvas.grab().toImage().pixelColor(widget_point);
}

// The Fill command honors the dedicated Fill Opacity/Soft settings (default 100% / 0, so fills are
// solid out of the box). Tests that lay down a solid setup color force those defaults here in case a
// persisted setting differs.
void use_solid_fill_settings(patchy::ui::CanvasWidget* canvas) {
  if (canvas != nullptr) {
    canvas->set_fill_opacity(100);
    canvas->set_fill_softness(0);
  }
}

int count_blended_document_pixels(patchy::ui::CanvasWidget& canvas, QRect document_rect, QColor foreground, QColor background, int tolerance) {
  const auto image = canvas.grab().toImage();
  int count = 0;
  for (int y = document_rect.top(); y <= document_rect.bottom(); ++y) {
    for (int x = document_rect.left(); x <= document_rect.right(); ++x) {
      const auto widget_point = canvas.widget_position_for_document_point(QPoint(x, y));
      if (!image.rect().contains(widget_point)) {
        continue;
      }
      const auto color = image.pixelColor(widget_point);
      if (!color_close(color, foreground, tolerance) && !color_close(color, background, tolerance)) {
        ++count;
      }
    }
  }
  return count;
}

QColor canvas_pixel_center(patchy::ui::CanvasWidget& canvas, QPoint document_point) {
  const auto top_left = canvas.widget_position_for_document_point(document_point);
  const auto bottom_right = canvas.widget_position_for_document_point(document_point + QPoint(1, 1));
  const QPoint center((top_left.x() + bottom_right.x()) / 2, (top_left.y() + bottom_right.y()) / 2);
  return canvas.grab().toImage().pixelColor(center);
}

std::optional<QRect> dark_document_bounds(patchy::ui::CanvasWidget& canvas, QRect document_rect) {
  const auto image = canvas.grab().toImage();
  int min_x = document_rect.right() + 1;
  int min_y = document_rect.bottom() + 1;
  int max_x = document_rect.left() - 1;
  int max_y = document_rect.top() - 1;
  for (int y = document_rect.top(); y <= document_rect.bottom(); ++y) {
    for (int x = document_rect.left(); x <= document_rect.right(); ++x) {
      const auto widget_point = canvas.widget_position_for_document_point(QPoint(x, y));
      if (!image.rect().contains(widget_point)) {
        continue;
      }
      const auto color = image.pixelColor(widget_point);
      if (color.red() < 80 && color.green() < 80 && color.blue() < 80) {
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
      }
    }
  }
  if (max_x < min_x || max_y < min_y) {
    return std::nullopt;
  }
  return QRect(QPoint(min_x, min_y), QPoint(max_x, max_y));
}

std::vector<AlphaRowBand> alpha_row_bands(const patchy::PixelBuffer& pixels) {
  std::vector<AlphaRowBand> bands;
  if (pixels.empty() || pixels.width() <= 0 || pixels.height() <= 0) {
    return bands;
  }
  const auto channels = pixels.format().channels;
  if (channels != 1U && channels < 4U) {
    return bands;
  }
  const auto alpha_channel = channels == 1U ? 0U : 3U;
  const auto stride = pixels.stride_bytes();
  const auto bytes = pixels.data();
  const auto active_threshold = std::max(2, pixels.width() / 180);
  constexpr int kAlphaThreshold = 12;
  constexpr int kMergeGap = 2;

  bool in_band = false;
  int band_top = 0;
  int last_active = -1;
  for (int y = 0; y < pixels.height(); ++y) {
    int active_pixels = 0;
    const auto row_offset = static_cast<std::size_t>(y) * stride;
    for (int x = 0; x < pixels.width(); ++x) {
      const auto offset = row_offset + static_cast<std::size_t>(x) * channels + alpha_channel;
      if (offset < bytes.size() && bytes[offset] >= kAlphaThreshold) {
        ++active_pixels;
      }
    }
    const bool active = active_pixels >= active_threshold;
    if (active) {
      if (!in_band) {
        in_band = true;
        band_top = y;
      }
      last_active = y;
    } else if (in_band && last_active >= 0 && y - last_active > kMergeGap) {
      if (last_active + 1 - band_top >= 2) {
        bands.push_back(AlphaRowBand{band_top, last_active + 1});
      }
      in_band = false;
      last_active = -1;
    }
  }
  if (in_band && last_active + 1 - band_top >= 2) {
    bands.push_back(AlphaRowBand{band_top, last_active + 1});
  }
  return bands;
}

int alpha_row_band_span(const std::vector<AlphaRowBand>& bands) {
  if (bands.empty()) {
    return 0;
  }
  return std::max(0, bands.back().bottom - bands.front().top);
}

// Visible-alpha extents (local pixel coordinates) restricted to the rows [top, bottom).
std::optional<QRect> alpha_pixel_bounds_in_rows(const patchy::PixelBuffer& pixels, int top, int bottom) {
  if (pixels.empty() || pixels.width() <= 0 || pixels.height() <= 0) {
    return std::nullopt;
  }
  const auto channels = pixels.format().channels;
  if (channels != 1U && channels < 4U) {
    return std::nullopt;
  }
  const auto alpha_channel = channels == 1U ? 0U : 3U;
  const auto stride = pixels.stride_bytes();
  const auto bytes = pixels.data();
  constexpr int kAlphaThreshold = 12;
  int min_x = pixels.width();
  int max_x = -1;
  int min_y = pixels.height();
  int max_y = -1;
  for (int y = std::max(0, top); y < std::min(pixels.height(), bottom); ++y) {
    const auto row_offset = static_cast<std::size_t>(y) * stride;
    for (int x = 0; x < pixels.width(); ++x) {
      const auto offset = row_offset + static_cast<std::size_t>(x) * channels + alpha_channel;
      if (offset < bytes.size() && bytes[offset] >= kAlphaThreshold) {
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
      }
    }
  }
  if (max_x < min_x) {
    return std::nullopt;
  }
  return QRect(QPoint(min_x, min_y), QPoint(max_x, max_y));
}

patchy::Layer* preview_layer_for_editor(patchy::Document& document, const QTextEdit& editor) {
  if (!editor.property("patchy.textPreviewLayerId").isValid()) {
    return nullptr;
  }
  return document.find_layer(static_cast<patchy::LayerId>(editor.property("patchy.textPreviewLayerId").toULongLong()));
}

int count_internal_text_preview_layers(const std::vector<patchy::Layer>& layers) {
  int count = 0;
  for (const auto& layer : layers) {
    if (layer.metadata().contains("patchy.internal.text_preview")) {
      ++count;
    }
    count += count_internal_text_preview_layers(layer.children());
  }
  return count;
}

int count_internal_text_preview_layers(const patchy::Document& document) {
  return count_internal_text_preview_layers(document.layers());
}

std::optional<QRectF> editor_document_line_rect_containing(const QTextEdit& editor, const QString& needle) {
  const auto* layout = editor.document()->documentLayout();
  if (layout == nullptr || needle.isEmpty()) {
    return std::nullopt;
  }
  const QPointF document_origin(editor.property("patchy.documentTextX").toDouble(),
                                editor.property("patchy.documentTextY").toDouble());
  for (auto block = editor.document()->begin(); block.isValid(); block = block.next()) {
    const auto block_text = block.text();
    const auto index = block_text.indexOf(needle);
    if (index < 0) {
      continue;
    }
    const auto* text_layout = block.layout();
    if (text_layout == nullptr) {
      return std::nullopt;
    }
    const auto block_rect = layout->blockBoundingRect(block);
    for (int i = 0; i < text_layout->lineCount(); ++i) {
      const auto line = text_layout->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const auto line_start = line.textStart();
      const auto line_end = line_start + line.textLength();
      if (index < line_start || index >= line_end) {
        continue;
      }
      return line.rect().translated(block_rect.topLeft()).translated(document_origin);
    }
  }
  return std::nullopt;
}

std::optional<QRect> dark_bounds_in_editor_line_lower_half(patchy::ui::CanvasWidget& canvas, const QRectF& document_line_rect) {
  const auto top = static_cast<int>(std::floor(document_line_rect.center().y())) - 2;
  const auto bottom = static_cast<int>(std::ceil(document_line_rect.bottom())) + 8;
  const QRect sample_rect(static_cast<int>(std::floor(document_line_rect.left())) - 4,
                          top,
                          static_cast<int>(std::ceil(document_line_rect.width())) + 8,
                          std::max(1, bottom - top + 1));
  return dark_document_bounds(canvas, sample_rect);
}

// The imported-PSD raster-preview text tests pin their fixture face as INSTALLED Arial;
// on a machine that genuinely lacks Arial (stock Linux ships Liberation instead),
// entering the editor correctly raises the Missing Font substitution prompt, which
// nothing can answer under offscreen (the suite would hang in the nested dialog loop).
// Skip honestly rather than auto-clicking through behavior the test doesn't model.
bool skip_without_arial_for_psd_text_preview() {
  register_test_fonts(TestFontRole::UiDefault);
  if (QFontDatabase::families().contains(QStringLiteral("Arial"))) {
    return false;
  }
  std::cout << "[SKIP] Arial is not installed (imported-PSD text fixture face)\n";
  return true;
}

QAction* require_legacy_plugin_action(QWidget& root, const QString& text) {
  for (auto* action : root.findChildren<QAction*>(QStringLiteral("legacyPluginAction"))) {
    if (action->text().contains(text, Qt::CaseInsensitive)) {
      return action;
    }
  }
  CHECK(false);
  return nullptr;
}

QListWidgetItem* find_layer_item(QListWidget& list, const QString& text) {
  for (int row = 0; row < list.count(); ++row) {
    if (list.item(row)->text() == text) {
      return list.item(row);
    }
  }
  return nullptr;
}

QListWidgetItem* require_layer_item(QListWidget& list, const QString& text) {
  if (auto* item = find_layer_item(list, text); item != nullptr) {
    return item;
  }
  CHECK(false);
  return nullptr;
}

// A press on a layer-row thumbnail can retarget editing or toggle the mask,
// which rebuilds the layer row and deletes the widget the press was sent to.
// Fetch the thumbnail fresh for each event so the release never goes to a
// deleted row (the real mouse path re-resolves its receiver the same way).
void click_layer_row_thumbnail(QListWidget& layers, const QString& layer_name, const QString& thumbnail_name, Qt::KeyboardModifiers modifiers) {
  const auto fetch_thumbnail = [&layers, &layer_name, &thumbnail_name]() -> QLabel* {
    auto* item = require_layer_item(layers, layer_name);
    auto* row = layers.itemWidget(item);
    CHECK(row != nullptr);
    auto* thumbnail = row->findChild<QLabel*>(thumbnail_name);
    CHECK(thumbnail != nullptr);
    return thumbnail;
  };
  auto* press_target = fetch_thumbnail();
  send_mouse(*press_target, QEvent::MouseButtonPress, press_target->rect().center(), Qt::LeftButton, Qt::LeftButton,
             modifiers);
  auto* release_target = fetch_thumbnail();
  send_mouse(*release_target, QEvent::MouseButtonRelease, release_target->rect().center(), Qt::LeftButton,
             Qt::NoButton, modifiers);
  QApplication::processEvents();
}

bool top_level_widget_exists(const QString& object_name) {
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (widget->objectName() == object_name) {
      return true;
    }
  }
  return false;
}

patchy::LayerBlendIf blend_if_ui_test_settings() {
  patchy::LayerBlendIf settings;
  settings.channels[0].this_layer = patchy::BlendIfThresholds{10, 20, 200, 240};
  settings.channels[0].underlying_layer = patchy::BlendIfThresholds{12, 24, 190, 230};
  settings.channels[1].this_layer = patchy::BlendIfThresholds{30, 40, 180, 220};
  settings.channels[1].underlying_layer = patchy::BlendIfThresholds{32, 42, 170, 210};
  settings.channels[2].this_layer = patchy::BlendIfThresholds{50, 60, 160, 200};
  settings.channels[2].underlying_layer = patchy::BlendIfThresholds{52, 62, 150, 190};
  settings.channels[3].this_layer = patchy::BlendIfThresholds{70, 80, 140, 180};
  settings.channels[3].underlying_layer = patchy::BlendIfThresholds{72, 82, 130, 170};
  return settings;
}

QListWidgetItem* require_gallery_filter_item(QListWidget& looks, const QString& filter_id) {
  for (int row = 0; row < looks.count(); ++row) {
    auto* item = looks.item(row);
    if (item != nullptr &&
        item->data(Qt::UserRole + 1).toString() == filter_id) {
      return item;
    }
  }
  CHECK(false);
  return nullptr;
}

void accept_new_document_dialog(int width_value, int height_value) {
  QTimer::singleShot(0, [width_value, height_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyNewDocumentDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
      auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentHeightSpin"));
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      CHECK(width->buttonSymbols() == QAbstractSpinBox::NoButtons);
      CHECK(height->buttonSymbols() == QAbstractSpinBox::NoButtons);
      width->setValue(width_value);
      height->setValue(height_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_new_document_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_clipboard_new_document_dialog(QSize clipboard_size) {
  QTimer::singleShot(0, [clipboard_size] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyNewDocumentDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* presets = dialog->findChild<QListWidget*>(QStringLiteral("newDocumentPresetList"));
      auto* screen_chip = dialog->findChild<QToolButton*>(QStringLiteral("newDocumentScreenChip"));
      auto* print_chip = dialog->findChild<QToolButton*>(QStringLiteral("newDocumentPrintChip"));
      auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentWidthSpin"));
      auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentHeightSpin"));
      auto* resolution = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("newDocumentResolutionSpin"));
      auto* background = dialog->findChild<QComboBox*>(QStringLiteral("newDocumentBackgroundCombo"));
      CHECK(dialog != nullptr);
      CHECK(presets != nullptr);
      CHECK(screen_chip != nullptr);
      CHECK(print_chip != nullptr);
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      CHECK(resolution != nullptr);
      CHECK(background != nullptr);

      const auto find_card = [presets](const QString& id) -> QListWidgetItem* {
        for (int row = 0; row < presets->count(); ++row) {
          if (presets->item(row)->data(patchy::ui::kNewDocumentPresetIdRole).toString() == id) {
            return presets->item(row);
          }
        }
        return nullptr;
      };

      // An image on the clipboard preselects the (enabled) Clipboard card and
      // locks the fields to the clipboard dimensions.
      CHECK(screen_chip->isChecked());
      auto* clipboard_card = find_card(QStringLiteral("clipboard"));
      CHECK(clipboard_card != nullptr);
      CHECK((clipboard_card->flags() & Qt::ItemIsEnabled) != 0);
      CHECK(presets->currentItem() == clipboard_card);
      CHECK(width->value() == clipboard_size.width());
      CHECK(height->value() == clipboard_size.height());
      CHECK(!width->isEnabled());
      CHECK(!height->isEnabled());
      CHECK(!resolution->isEnabled());
      CHECK(!background->isEnabled());

      // Preset cards carry a resolution: screen/web/video cards follow
      // Photoshop's 72 PPI convention, physical print cards are 300 PPI.
      struct ExpectedPreset {
        QString id;
        QSize size;
        double ppi;
      };
      const std::vector<ExpectedPreset> screen_presets = {
          {QStringLiteral("screen-1024x768"), QSize(1024, 768), 72.0},
          {QStringLiteral("screen-720p"), QSize(1280, 720), 72.0},
          {QStringLiteral("screen-1080p"), QSize(1920, 1080), 72.0},
          {QStringLiteral("screen-4k"), QSize(3840, 2160), 72.0},
          {QStringLiteral("screen-square-2048"), QSize(2048, 2048), 72.0},
          {QStringLiteral("social-square-1080"), QSize(1080, 1080), 72.0},
          {QStringLiteral("phone-story-1080x1920"), QSize(1080, 1920), 72.0},
          {QStringLiteral("photo-3x2-3000"), QSize(3000, 2000), 72.0},
      };
      const std::vector<ExpectedPreset> print_presets = {
          {QStringLiteral("print-a5"), QSize(1748, 2480), 300.0},
          {QStringLiteral("print-a4"), QSize(2480, 3508), 300.0},
          {QStringLiteral("print-a3"), QSize(3508, 4961), 300.0},
          {QStringLiteral("print-us-letter"), QSize(2550, 3300), 300.0},
          {QStringLiteral("print-us-legal"), QSize(2550, 4200), 300.0},
          {QStringLiteral("print-5x7"), QSize(1500, 2100), 300.0},
          {QStringLiteral("print-8x10"), QSize(2400, 3000), 300.0},
      };
      const auto check_presets = [&](const std::vector<ExpectedPreset>& expected) {
        for (const auto& preset : expected) {
          auto* card = find_card(preset.id);
          CHECK(card != nullptr);
          presets->setCurrentItem(card);
          QApplication::processEvents();
          CHECK(width->isEnabled());
          CHECK(width->value() == preset.size.width());
          CHECK(height->value() == preset.size.height());
          CHECK(std::abs(resolution->value() - preset.ppi) < 0.01);
        }
      };
      check_presets(screen_presets);
      print_chip->click();
      QApplication::processEvents();
      check_presets(print_presets);

      // Back to the Clipboard card; accepting creates the clipboard document.
      screen_chip->click();
      QApplication::processEvents();
      clipboard_card = find_card(QStringLiteral("clipboard"));
      CHECK(clipboard_card != nullptr);
      presets->setCurrentItem(clipboard_card);
      QApplication::processEvents();
      CHECK(!width->isEnabled());
      CHECK(!height->isEnabled());
      CHECK(!resolution->isEnabled());
      CHECK(!background->isEnabled());
      widget->grab().save(QStringLiteral("test-artifacts/ui_new_document_dialog_clipboard.png"));
      dialog->accept();
      return;
    }
    CHECK(false);
  });
}

void accept_integer_dialog(const QString& object_name, int value) {
  QTimer::singleShot(0, [object_name, value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != object_name) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      auto* spin = dialog->findChild<QSpinBox*>(QStringLiteral("integerInputSpin"));
      CHECK(spin != nullptr);
      CHECK(spin->minimum() <= value);
      CHECK(spin->maximum() >= value);
      CHECK(spin->buttonSymbols() == QAbstractSpinBox::NoButtons);
      spin->setValue(value);
      dialog->accept();
      return;
    }
  });
}

void accept_canvas_size_dialog(int width_value, int height_value) {
  QTimer::singleShot(0, [width_value, height_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyCanvasSizeDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* width = dialog->findChild<QSpinBox*>(QStringLiteral("canvasSizeWidthSpin"));
      auto* height = dialog->findChild<QSpinBox*>(QStringLiteral("canvasSizeHeightSpin"));
      auto* new_size = dialog->findChild<QLabel*>(QStringLiteral("canvasSizeNewSizeLabel"));
      auto* relative = dialog->findChild<QCheckBox*>(QStringLiteral("canvasSizeRelativeCheck"));
      auto* width_unit = dialog->findChild<QComboBox*>(QStringLiteral("canvasSizeWidthUnitCombo"));
      auto* height_unit = dialog->findChild<QComboBox*>(QStringLiteral("canvasSizeHeightUnitCombo"));
      auto* extension_color = dialog->findChild<QComboBox*>(QStringLiteral("canvasSizeExtensionColorCombo"));
      auto* color_swatch = dialog->findChild<QPushButton*>(QStringLiteral("canvasSizeExtensionColorSwatch"));
      const auto anchors = dialog->findChildren<QToolButton*>(QStringLiteral("canvasSizeAnchorButton"));
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      CHECK(new_size != nullptr);
      CHECK(relative != nullptr);
      CHECK(width_unit != nullptr);
      CHECK(height_unit != nullptr);
      CHECK(extension_color != nullptr);
      CHECK(color_swatch != nullptr);
      CHECK(anchors.size() == 9);
      CHECK(color_swatch->toolTip() == QStringLiteral("Choose canvas extension color"));
      CHECK(width->buttonSymbols() == QAbstractSpinBox::NoButtons);
      CHECK(height->buttonSymbols() == QAbstractSpinBox::NoButtons);
      CHECK(!relative->isChecked());
      CHECK(width_unit->currentText() == QStringLiteral("Pixels"));
      CHECK(height_unit->currentText() == QStringLiteral("Pixels"));
      CHECK(extension_color->currentText() == QStringLiteral("Other..."));
      CHECK(std::any_of(anchors.begin(), anchors.end(), [](QToolButton* button) {
        return button->isChecked() && button->toolTip() == QStringLiteral("Anchor center");
      }));
      width->setValue(width_value);
      height->setValue(height_value);
      CHECK(new_size->text().contains(QStringLiteral("New Size:")));
      widget->grab().save(QStringLiteral("test-artifacts/ui_canvas_size_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_image_size_dialog(int width_value, int height_value) {
  QTimer::singleShot(0, [width_value, height_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyImageSizeDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("imageSizeWidthSpin"));
      auto* height = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("imageSizeHeightSpin"));
      auto* dimensions = dialog->findChild<QLabel*>(QStringLiteral("imageSizeDimensionsLabel"));
      auto* preview = dialog->findChild<QLabel*>(QStringLiteral("imageSizePreview"));
      auto* resample = dialog->findChild<QCheckBox*>(QStringLiteral("imageSizeResampleCheck"));
      auto* method = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeResampleCombo"));
      auto* link = dialog->findChild<QToolButton*>(QStringLiteral("imageSizeLinkButton"));
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      CHECK(dimensions != nullptr);
      CHECK(preview != nullptr);
      CHECK(resample != nullptr);
      CHECK(method != nullptr);
      CHECK(link != nullptr);
      CHECK(width->buttonSymbols() == QAbstractSpinBox::NoButtons);
      CHECK(height->buttonSymbols() == QAbstractSpinBox::NoButtons);
      CHECK(dimensions->text().contains(QStringLiteral("px x")));
      CHECK(resample->isChecked());
      CHECK(method->currentText() == QStringLiteral("Bicubic Sharper (reduction)"));
      CHECK(link->isChecked());
      width->setValue(width_value);
      height->setValue(height_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_image_size_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_image_size_resolution_dialog(int resolution_value) {
  QTimer::singleShot(0, [resolution_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyImageSizeDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* resolution = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("imageSizeResolutionSpin"));
      auto* resample = dialog->findChild<QCheckBox*>(QStringLiteral("imageSizeResampleCheck"));
      auto* width_unit = dialog->findChild<QComboBox*>(QStringLiteral("imageSizeWidthUnitCombo"));
      CHECK(resolution != nullptr);
      CHECK(resample != nullptr);
      CHECK(width_unit != nullptr);
      resample->setChecked(false);
      // Unchecking Resample flips pixel/percent units to Inches (Photoshop behavior).
      CHECK(width_unit->currentText() == QStringLiteral("Inches"));
      resolution->setValue(resolution_value);
      dialog->accept();
      return;
    }
  });
}

void accept_layer_style_dialog(bool stroke_enabled, bool gradient_enabled, bool shadow_enabled) {
  QTimer::singleShot(0, [stroke_enabled, gradient_enabled, shadow_enabled] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyLayerStyleDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("layerStyleCategoryList"));
      auto* blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleBlendModeCombo"));
      auto* opacity = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOpacitySpin"));
      auto* opacity_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOpacitySlider"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("layerStylePreviewCheck"));
      auto* options_stack = dialog->findChild<QWidget*>(QStringLiteral("layerStyleOptionsStack"));
      auto* stroke_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleStrokeCategoryCheck"));
      auto* gradient_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleGradientOverlayCategoryCheck"));
      auto* outer_glow_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleOuterGlowCategoryCheck"));
      auto* inner_glow_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleInnerGlowCategoryCheck"));
      auto* shadow_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleDropShadowCategoryCheck"));
      auto* inner_shadow_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleInnerShadowCategoryCheck"));
      auto* bevel_check = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleBevelEmbossCategoryCheck"));
      auto* stroke_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeSizeSpin"));
      auto* stroke_size_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleStrokeSizeSlider"));
      auto* gradient_angle = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientAngleSpin"));
      auto* gradient_angle_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleGradientAngleSlider"));
      auto* gradient_editor =
          dialog->findChild<patchy::ui::GradientStopsEditorWidget*>(QStringLiteral("layerStyleGradientStopsEditor"));
      auto* gradient_blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleGradientBlendModeCombo"));
      auto* gradient_reverse = dialog->findChild<QCheckBox*>(QStringLiteral("layerStyleGradientReverseCheck"));
      auto* gradient_style_combo = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleGradientStyleCombo"));
      auto* gradient_stop_location = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleGradientStopLocationSpin"));
      auto* gradient_stop_hex = dialog->findChild<QLineEdit*>(QStringLiteral("layerStyleGradientStopHexEdit"));
      auto* add_gradient_stop = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleGradientAddStopButton"));
      auto* outer_glow_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowSizeSpin"));
      auto* outer_glow_size_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOuterGlowSizeSlider"));
      auto* inner_glow_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleInnerGlowSizeSpin"));
      auto* inner_glow_size_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleInnerGlowSizeSlider"));
      auto* add_inner_glow = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddInnerGlowButton"));
      auto* remove_inner_glow = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleRemoveInnerGlowButton"));
      auto* add_inner_glow_instance = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddInnerGlowInstanceButton"));
      auto* add_stroke_instance = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddStrokeInstanceButton"));
      auto* add_color_overlay_instance =
          dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddColorOverlayInstanceButton"));
      auto* add_gradient_instance =
          dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddGradientOverlayInstanceButton"));
      auto* add_outer_glow_instance =
          dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddOuterGlowInstanceButton"));
      auto* add_drop_shadow_instance =
          dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddDropShadowInstanceButton"));
      auto* remove_selected_instance =
          dialog->findChild<QPushButton*>(QStringLiteral("layerStyleRemoveSelectedInstanceButton"));
      auto* shadow_blend = dialog->findChild<QComboBox*>(QStringLiteral("layerStyleDropShadowBlendModeCombo"));
      auto* shadow_distance = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowDistanceSpin"));
      auto* shadow_distance_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleDropShadowDistanceSlider"));
      auto* inner_shadow_distance = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleInnerShadowDistanceSpin"));
      auto* inner_shadow_distance_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleInnerShadowDistanceSlider"));
      auto* add_inner_shadow = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddInnerShadowButton"));
      auto* remove_inner_shadow = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleRemoveInnerShadowButton"));
      auto* add_inner_shadow_instance =
          dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddInnerShadowInstanceButton"));
      auto* shadow_red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowRedSpin"));
      auto* shadow_red_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleDropShadowRedSlider"));
      auto* shadow_green = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowGreenSpin"));
      auto* shadow_blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleDropShadowBlueSpin"));
      auto* bevel_size = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleBevelSizeSpin"));
      auto* bevel_size_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleBevelSizeSlider"));
      auto* stroke_red = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleStrokeRedSpin"));
      auto* stroke_red_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleStrokeRedSlider"));
      auto* outer_glow_blue = dialog->findChild<QSpinBox*>(QStringLiteral("layerStyleOuterGlowBlueSpin"));
      auto* outer_glow_blue_slider = dialog->findChild<QSlider*>(QStringLiteral("layerStyleOuterGlowBlueSlider"));
      CHECK(categories != nullptr);
      CHECK(blend != nullptr);
      CHECK(opacity != nullptr);
      CHECK(opacity_slider != nullptr);
      CHECK(preview != nullptr);
      CHECK(options_stack != nullptr);
      CHECK(stroke_check != nullptr);
      CHECK(gradient_check != nullptr);
      CHECK(outer_glow_check != nullptr);
      CHECK(inner_glow_check != nullptr);
      CHECK(shadow_check != nullptr);
      CHECK(inner_shadow_check != nullptr);
      CHECK(bevel_check != nullptr);
      CHECK(stroke_size != nullptr);
      CHECK(stroke_size_slider != nullptr);
      CHECK(gradient_angle != nullptr);
      CHECK(gradient_angle_slider != nullptr);
      CHECK(gradient_editor != nullptr);
      CHECK(gradient_blend != nullptr);
      CHECK(gradient_reverse != nullptr);
      CHECK(gradient_style_combo != nullptr);
      CHECK(gradient_stop_location != nullptr);
      CHECK(gradient_stop_hex != nullptr);
      CHECK(add_gradient_stop != nullptr);
      CHECK(outer_glow_size != nullptr);
      CHECK(outer_glow_size_slider != nullptr);
      CHECK(inner_glow_size != nullptr);
      CHECK(inner_glow_size_slider != nullptr);
      CHECK(add_inner_glow != nullptr);
      CHECK(remove_inner_glow != nullptr);
      CHECK(add_inner_glow_instance != nullptr);
      CHECK(add_stroke_instance != nullptr);
      CHECK(add_color_overlay_instance != nullptr);
      CHECK(add_gradient_instance != nullptr);
      CHECK(add_outer_glow_instance != nullptr);
      CHECK(add_drop_shadow_instance != nullptr);
      CHECK(remove_selected_instance != nullptr);
      CHECK(shadow_blend != nullptr);
      CHECK(shadow_distance != nullptr);
      CHECK(shadow_distance_slider != nullptr);
      CHECK(inner_shadow_distance != nullptr);
      CHECK(inner_shadow_distance_slider != nullptr);
      CHECK(add_inner_shadow != nullptr);
      CHECK(remove_inner_shadow != nullptr);
      CHECK(add_inner_shadow_instance != nullptr);
      CHECK(shadow_red != nullptr);
      CHECK(shadow_red_slider != nullptr);
      CHECK(shadow_green != nullptr);
      CHECK(shadow_blue != nullptr);
      CHECK(bevel_size != nullptr);
      CHECK(bevel_size_slider != nullptr);
      CHECK(stroke_red != nullptr);
      CHECK(stroke_red_slider != nullptr);
      CHECK(outer_glow_blue != nullptr);
      CHECK(outer_glow_blue_slider != nullptr);
      const auto check_compact_symbol_button = [](QPushButton* button) {
        CHECK(button != nullptr);
        CHECK(button->property("compactSymbolButton").toBool());
        CHECK(button->width() >= 22);
        CHECK(button->height() >= 22);
        CHECK(button->text().isEmpty());
        CHECK(!button->icon().isNull());
        CHECK(button->iconSize() == QSize(16, 16));
      };
      const auto check_category_symbol_button = [&](QPushButton* button) {
        check_compact_symbol_button(button);
        CHECK(button->parentWidget() != nullptr);
        CHECK(button->parentWidget()->height() >= button->height() + 6);
        CHECK(button->geometry().top() >= 2);
        CHECK(button->geometry().bottom() <= button->parentWidget()->height() - 3);
      };
      for (auto* button : {add_inner_glow, add_inner_shadow, remove_inner_shadow, remove_inner_glow}) {
        check_compact_symbol_button(button);
      }
      for (auto* button : {add_inner_glow_instance,
                           add_stroke_instance,
                           add_color_overlay_instance,
                           add_gradient_instance,
                           add_outer_glow_instance,
                           add_drop_shadow_instance,
                           add_inner_shadow_instance}) {
        check_category_symbol_button(button);
      }
      CHECK(!dialog->isModal());
      CHECK(dialog->windowModality() == Qt::NonModal);
      CHECK(preview->isChecked());
      auto find_item = [categories](const QString& text) {
        const auto items = categories->findItems(text, Qt::MatchExactly);
        CHECK(!items.empty());
        return items.front();
      };
      auto* blending_item = find_item(QStringLiteral("Blending Options"));
      auto* bevel_item = find_item(QStringLiteral("Bevel & Emboss"));
      auto* stroke_item = find_item(QStringLiteral("Stroke"));
      auto* inner_shadow_item = find_item(QStringLiteral("Inner Shadow"));
      auto* inner_glow_item = find_item(QStringLiteral("Inner Glow"));
      auto* gradient_item = find_item(QStringLiteral("Gradient Overlay"));
      auto* outer_glow_item = find_item(QStringLiteral("Outer Glow"));
      auto* shadow_item = find_item(QStringLiteral("Drop Shadow"));
      CHECK((bevel_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((stroke_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((inner_shadow_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((inner_glow_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((gradient_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((outer_glow_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((shadow_item->flags() & Qt::ItemIsUserCheckable) != 0);
      CHECK((blending_item->flags() & Qt::ItemIsUserCheckable) == 0);
      CHECK(blend->findText(QStringLiteral("Pin Light")) >= 0);
      opacity_slider->setValue(92);
      CHECK(opacity->value() == 92);
      stroke_item->setCheckState(stroke_enabled ? Qt::Checked : Qt::Unchecked);
      gradient_item->setCheckState(gradient_enabled ? Qt::Checked : Qt::Unchecked);
      outer_glow_item->setCheckState(Qt::Checked);
      inner_glow_item->setCheckState(Qt::Checked);
      shadow_item->setCheckState(shadow_enabled ? Qt::Checked : Qt::Unchecked);
      inner_shadow_item->setCheckState(Qt::Checked);
      bevel_item->setCheckState(Qt::Checked);
      categories->setCurrentItem(stroke_item);
      stroke_size_slider->setValue(6);
      CHECK(stroke_size->value() == 6);
      stroke_red_slider->setValue(32);
      CHECK(stroke_red->value() == 32);
      categories->setCurrentItem(bevel_item);
      bevel_size_slider->setValue(7);
      CHECK(bevel_size->value() == 7);
      categories->setCurrentItem(outer_glow_item);
      outer_glow_size_slider->setValue(8);
      CHECK(outer_glow_size->value() == 8);
      outer_glow_blue_slider->setValue(210);
      CHECK(outer_glow_blue->value() == 210);
      categories->setCurrentItem(inner_glow_item);
      inner_glow_size_slider->setValue(9);
      CHECK(inner_glow_size->value() == 9);
      categories->setCurrentItem(gradient_enabled ? gradient_item : blending_item);
      gradient_angle_slider->setValue(0);
      CHECK(gradient_angle->value() == 0);
      categories->setCurrentItem(shadow_item);
      shadow_blend->setCurrentIndex(std::max(0, shadow_blend->findData(static_cast<int>(patchy::BlendMode::Normal))));
      CHECK(shadow_blend->currentData().toInt() == static_cast<int>(patchy::BlendMode::Normal));
      shadow_red_slider->setValue(245);
      CHECK(shadow_red->value() == 245);
      shadow_green->setValue(246);
      shadow_blue->setValue(247);
      shadow_distance_slider->setValue(10);
      CHECK(shadow_distance->value() == 10);
      categories->setCurrentItem(gradient_enabled ? gradient_item : blending_item);
      CHECK(gradient_stop_location->value() == 0);
      add_gradient_stop->click();
      CHECK(gradient_stop_location->value() == 10);
      gradient_stop_location->setValue(50);
      gradient_stop_hex->setText(QStringLiteral("#FFA000"));
      send_key(*gradient_stop_hex, Qt::Key_Return);
      CHECK(gradient_stop_hex->text() == QStringLiteral("#FFA000"));
      categories->setCurrentItem(inner_shadow_item);
      inner_shadow_distance_slider->setValue(3);
      CHECK(inner_shadow_distance->value() == 3);
      categories->setCurrentItem(inner_glow_item);
      add_inner_glow_instance->click();
      QApplication::processEvents();
      CHECK(categories->findItems(QStringLiteral("Inner Glow"), Qt::MatchExactly).size() == 2);
      remove_selected_instance->click();
      QApplication::processEvents();
      CHECK(categories->findItems(QStringLiteral("Inner Glow"), Qt::MatchExactly).size() == 1);
      inner_shadow_item = find_item(QStringLiteral("Inner Shadow"));
      categories->setCurrentItem(inner_shadow_item);
      add_inner_shadow_instance = dialog->findChild<QPushButton*>(QStringLiteral("layerStyleAddInnerShadowInstanceButton"));
      CHECK(add_inner_shadow_instance != nullptr);
      add_inner_shadow_instance->click();
      QApplication::processEvents();
      CHECK(categories->findItems(QStringLiteral("Inner Shadow"), Qt::MatchExactly).size() == 2);
      // The selected category row must be highlighted across its full width, not just in a
      // sliver of item padding peeking out around the opaque row widget.
      const auto dialog_image = widget->grab().toImage();
      auto category_row_probe_color = [&](QListWidgetItem* item, int x_offset) {
        const auto rect = categories->visualItemRect(item);
        const auto probe =
            categories->viewport()->mapTo(widget, QPoint(rect.left() + x_offset, rect.center().y()));
        return dialog_image.pixelColor(probe);
      };
      // Probe near the left edge and in the (glyph-free) label area right of the text, so an
      // opaque child widget covering the middle of the row fails the check.
      for (const auto x_offset : {5, categories->visualItemRect(categories->currentItem()).width() - 40}) {
        CHECK(category_row_probe_color(categories->currentItem(), x_offset) == QColor(0x2d, 0x4c, 0x6d));
        CHECK(category_row_probe_color(find_item(QStringLiteral("Bevel & Emboss")), x_offset) ==
              QColor(0x2b, 0x2b, 0x2b));
      }
      dialog_image.save(QStringLiteral("test-artifacts/ui_layer_style_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_transform_dialog(int x_value, int y_value, int width_value, int height_value) {
  QTimer::singleShot(0, [x_value, y_value, width_value, height_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyTransformDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* x = dialog->findChild<QSpinBox*>(QStringLiteral("transformXSpin"));
      auto* y = dialog->findChild<QSpinBox*>(QStringLiteral("transformYSpin"));
      auto* width = dialog->findChild<QSpinBox*>(QStringLiteral("transformWidthSpin"));
      auto* height = dialog->findChild<QSpinBox*>(QStringLiteral("transformHeightSpin"));
      CHECK(x != nullptr);
      CHECK(y != nullptr);
      CHECK(width != nullptr);
      CHECK(height != nullptr);
      x->setValue(x_value);
      y->setValue(y_value);
      width->setValue(width_value);
      height->setValue(height_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_transform_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_hue_saturation_dialog(int hue_value, int saturation_value, int lightness_value) {
  QTimer::singleShot(0, [hue_value, saturation_value, lightness_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyHueSaturationDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* hue = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationHueSpin"));
      auto* saturation = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationSaturationSpin"));
      auto* lightness = dialog->findChild<QSpinBox*>(QStringLiteral("hueSaturationLightnessSpin"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("hueSaturationPreviewCheck"));
      CHECK(hue != nullptr);
      CHECK(saturation != nullptr);
      CHECK(lightness != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      hue->setValue(hue_value);
      saturation->setValue(saturation_value);
      lightness->setValue(lightness_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_hue_saturation_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_levels_dialog(int black_value, int white_value, int gamma_value) {
  QTimer::singleShot(0, [black_value, white_value, gamma_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyLevelsDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* black = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackInputSpin"));
      auto* white = dialog->findChild<QSpinBox*>(QStringLiteral("levelsWhiteInputSpin"));
      auto* gamma = dialog->findChild<QSpinBox*>(QStringLiteral("levelsGammaSpin"));
      auto* black_output = dialog->findChild<QSpinBox*>(QStringLiteral("levelsBlackOutputSpin"));
      auto* white_output = dialog->findChild<QSpinBox*>(QStringLiteral("levelsWhiteOutputSpin"));
      auto* input_graph = dialog->findChild<QWidget*>(QStringLiteral("levelsInputGraph"));
      auto* output_range = dialog->findChild<QWidget*>(QStringLiteral("levelsOutputRange"));
      auto* channel = dialog->findChild<QComboBox*>(QStringLiteral("levelsChannelCombo"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("levelsPreviewCheck"));
      CHECK(black != nullptr);
      CHECK(white != nullptr);
      CHECK(gamma != nullptr);
      CHECK(black_output != nullptr);
      CHECK(white_output != nullptr);
      CHECK(input_graph != nullptr);
      CHECK(output_range != nullptr);
      CHECK(channel != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      CHECK(channel->count() == 4);
      CHECK(channel->itemText(0) == QStringLiteral("RGB"));
      CHECK(channel->itemText(1) == QStringLiteral("Red"));
      CHECK(channel->itemText(2) == QStringLiteral("Green"));
      CHECK(channel->itemText(3) == QStringLiteral("Blue"));
      black->setValue(black_value);
      white->setValue(white_value);
      gamma->setValue(gamma_value);
      CHECK(black_output->value() == 0);
      CHECK(white_output->value() == 255);
      widget->grab().save(QStringLiteral("test-artifacts/ui_levels_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

QPoint curves_graph_position(QWidget& graph, int input, int output) {
  const auto available = graph.rect().adjusted(31, 10, -10, -28);
  const auto side = std::max(1, std::min(available.width(), available.height()));
  const auto left = available.left() + (available.width() - side) / 2;
  const auto top = available.top() + (available.height() - side) / 2;
  const QRect graph_rect(left, top, side, side);
  const auto x = graph_rect.left() +
                 static_cast<int>(std::lround(static_cast<double>(std::clamp(input, 0, 255)) *
                                              static_cast<double>(graph_rect.width() - 1) / 255.0));
  const auto y = graph_rect.bottom() -
                 static_cast<int>(std::lround(static_cast<double>(std::clamp(output, 0, 255)) *
                                              static_cast<double>(graph_rect.height() - 1) / 255.0));
  return QPoint(x, y);
}

void click_curves_graph(QWidget& graph, int input, int output) {
  const auto position = curves_graph_position(graph, input, output);
  send_mouse(graph, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
  send_mouse(graph, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
}

void accept_curves_dialog(int shadow_value, int midtone_value, int highlight_value) {
  QTimer::singleShot(0, [shadow_value, midtone_value, highlight_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyCurvesDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* graph = dialog->findChild<QWidget*>(QStringLiteral("curvesGraph"));
      auto* channel_tabs = dialog->findChild<QTabBar*>(QStringLiteral("curvesChannelTabs"));
      auto* input = dialog->findChild<QSpinBox*>(QStringLiteral("curvesInputSpin"));
      auto* output = dialog->findChild<QSpinBox*>(QStringLiteral("curvesOutputSpin"));
      auto* auto_button = dialog->findChild<QPushButton*>(QStringLiteral("curvesAutoButton"));
      auto* reset_button = dialog->findChild<QPushButton*>(QStringLiteral("curvesResetButton"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("curvesPreviewCheck"));
      CHECK(graph != nullptr);
      CHECK(channel_tabs != nullptr);
      CHECK(input != nullptr);
      CHECK(output != nullptr);
      CHECK(auto_button != nullptr);
      CHECK(reset_button != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      CHECK(channel_tabs->count() == 4);
      CHECK(channel_tabs->tabText(0) == QStringLiteral("RGB"));
      CHECK(channel_tabs->tabText(1) == QStringLiteral("Red"));
      CHECK(channel_tabs->tabText(2) == QStringLiteral("Green"));
      CHECK(channel_tabs->tabText(3) == QStringLiteral("Blue"));
      click_curves_graph(*graph, 0, 0);
      output->setValue(shadow_value);
      click_curves_graph(*graph, 128, midtone_value);
      CHECK(std::abs(input->value() - 128) <= 1);
      output->setValue(midtone_value);
      click_curves_graph(*graph, 255, 255);
      output->setValue(highlight_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_curves_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_color_balance_dialog(int cyan_red_value, int magenta_green_value, int yellow_blue_value) {
  QTimer::singleShot(0, [cyan_red_value, magenta_green_value, yellow_blue_value] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyColorBalanceDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      auto* cyan_red = dialog->findChild<QSpinBox*>(QStringLiteral("colorBalanceCyanRedSpin"));
      auto* magenta_green = dialog->findChild<QSpinBox*>(QStringLiteral("colorBalanceMagentaGreenSpin"));
      auto* yellow_blue = dialog->findChild<QSpinBox*>(QStringLiteral("colorBalanceYellowBlueSpin"));
      auto* preview = dialog->findChild<QCheckBox*>(QStringLiteral("colorBalancePreviewCheck"));
      CHECK(cyan_red != nullptr);
      CHECK(magenta_green != nullptr);
      CHECK(yellow_blue != nullptr);
      CHECK(preview != nullptr);
      CHECK(preview->isChecked());
      cyan_red->setValue(cyan_red_value);
      magenta_green->setValue(magenta_green_value);
      yellow_blue->setValue(yellow_blue_value);
      widget->grab().save(QStringLiteral("test-artifacts/ui_color_balance_dialog.png"));
      dialog->accept();
      return;
    }
  });
}

void accept_filter_dialog(std::vector<std::pair<QString, int>> spin_values) {
  QTimer::singleShot(0, [spin_values = std::move(spin_values)] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyFilterDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      CHECK(dialog->findChild<QCheckBox*>(QStringLiteral("filterPreviewCheck")) != nullptr);
      for (const auto& [object_name, value] : spin_values) {
        auto* spin = dialog->findChild<QSpinBox*>(object_name);
        CHECK(spin != nullptr);
        spin->setValue(value);
      }
      QApplication::processEvents();
      dialog->accept();
      return;
    }
    CHECK(false);
  });
}

QString brush_tip_test_storage_dir() {
  return QFileInfo(patchy::ui::app_settings().fileName()).absolutePath() + QStringLiteral("/brushes");
}

QString pattern_test_storage_dir() {
  return QFileInfo(patchy::ui::app_settings().fileName()).absolutePath() + QStringLiteral("/patterns");
}

void clear_pattern_test_state() {
  QDir(pattern_test_storage_dir()).removeRecursively();
  auto settings = patchy::ui::app_settings();
  // Keep unrelated MainWindow tests from changing their pattern library on disk.
  // The dedicated default-seeding test explicitly resets this to zero.
  settings.setValue(QStringLiteral("patterns/defaultPatternsVersion"), 999999);
  settings.sync();
}

void clear_brush_tip_test_state() {
  QDir(brush_tip_test_storage_dir()).removeRecursively();
  auto settings = patchy::ui::app_settings();
  settings.remove(QStringLiteral("tools/brushTip"));
  // Suppress first-run default-tip seeding so library contents stay deterministic; the
  // dedicated seeding test resets this to 0 explicitly.
  settings.setValue(QStringLiteral("brushes/defaultTipsVersion"), 999999);
  settings.sync();
}

// Opens the committed placed-smart-object fixture and activates its "small"
// smart-object layer; returns that layer's id. Import notes stay in the status bar
// (the popup only appears when imports/showPsdWarningsAndInfo is on).
patchy::LayerId open_smart_object_fixture(patchy::ui::MainWindow& window) {
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path("photoshop-place-embedded-png.psd").wstring());
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const patchy::Layer* smart_layer = nullptr;
  for (const auto& layer : std::as_const(document).layers()) {
    if (layer.name() == "small") {
      smart_layer = &layer;
    }
  }
  CHECK(smart_layer != nullptr);
  CHECK(patchy::layer_is_smart_object(*smart_layer));
  const auto id = smart_layer->id();
  document.set_active_layer(id);
  QApplication::processEvents();
  return id;
}

// Converts the opened fixture's embedded source into an ExternalFile reference
// pointing at `linked_path` (writing the png there), stamping the stored date/size
// from the file so staleness checks have a baseline.
void convert_fixture_source_to_external(patchy::ui::MainWindow& window, patchy::LayerId layer_id,
                                        const QString& linked_path) {
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto uuid = patchy::smart_object_source_uuid(*document.find_layer(layer_id));
  auto* source = document.metadata().smart_objects.find(uuid);
  CHECK(source != nullptr && source->file_bytes != nullptr);
  QFile out(linked_path);
  CHECK(out.open(QIODevice::WriteOnly));
  out.write(reinterpret_cast<const char*>(source->file_bytes->data()),
            static_cast<qint64>(source->file_bytes->size()));
  out.close();
  const QFileInfo linked_info(linked_path);
  const auto modified = linked_info.lastModified();
  source->kind = patchy::SmartObjectSourceKind::ExternalFile;
  source->file_bytes = nullptr;
  source->original_element_bytes = nullptr;
  source->filename = linked_info.fileName().toStdString();
  source->external_original_path = QDir::toNativeSeparators(linked_info.absoluteFilePath()).toStdString();
  source->external_rel_path = linked_info.fileName().toStdString();
  source->external_mod_year = modified.date().year();
  source->external_mod_month = static_cast<std::uint8_t>(modified.date().month());
  source->external_mod_day = static_cast<std::uint8_t>(modified.date().day());
  source->external_mod_hour = static_cast<std::uint8_t>(modified.time().hour());
  source->external_mod_minute = static_cast<std::uint8_t>(modified.time().minute());
  source->external_file_size = static_cast<std::uint64_t>(linked_info.size());
  auto* layer = document.find_layer(layer_id);
  CHECK(layer != nullptr);
  layer->metadata()[patchy::kLayerMetadataSmartObjectLock] = "external";
}

}  // namespace patchy::test::ui
