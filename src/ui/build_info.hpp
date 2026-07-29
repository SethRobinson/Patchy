#pragma once

#include <QString>

namespace patchy::ui {

// The date this binary was compiled, as ISO yyyy-MM-dd, shown next to the
// version on the About dialog and the start panel. Never empty.
[[nodiscard]] QString build_date_text();

}  // namespace patchy::ui
