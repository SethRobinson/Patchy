#pragma once

#include <QImage>
#include <QString>

#include <functional>

class QWidget;

namespace patchy::ui {

class BrushTipLibrary;

// Opens the modal Brush Tips manager: browse the library with a live stroke preview (rendered by
// the real paint engine), import .abr files, define a new tip from the current selection/layer,
// rename, duplicate, delete, and edit per-tip spacing.
//
// capture_define_source returns the coverage mask (grayscale, 255 = paints) for "Define from
// Selection", or a null image when nothing is available — the button is disabled then.
// activate_tip is called when the user chooses a tip to paint with (double-click or the Use
// button); pass the currently active id in initial_tip_id so it is preselected.
void request_brush_tip_manager(QWidget* parent, BrushTipLibrary& library, const QString& initial_tip_id,
                               const std::function<QImage()>& capture_define_source,
                               const std::function<void(const QString&)>& activate_tip);

}  // namespace patchy::ui
