#include "ui/qimage_composite_target.hpp"

// The one instantiation of the compositor for the UI's QImage target. image_document_io.cpp
// only declares it (qimage_composite_target.hpp), so editing the render templates no longer
// recompiles the whole render tree there.
PATCHY_COMPOSITOR_TARGET_INSTANTIATIONS(, patchy::ui::QImageCompositeTarget)
