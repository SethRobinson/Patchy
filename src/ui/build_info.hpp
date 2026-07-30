#pragma once

#include <QString>

namespace patchy::ui {

// The date and time this binary was compiled, as "yyyy-MM-dd HH:mm JST",
// shown next to the version on the About dialog and the start panel so a
// stale build is recognizable at a glance. Never empty.
[[nodiscard]] QString build_timestamp_text();

}  // namespace patchy::ui
