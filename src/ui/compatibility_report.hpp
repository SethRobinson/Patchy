#pragma once

#include "core/document.hpp"

#include <QString>
#include <QStringList>

class QWidget;

namespace patchy::ui {

[[nodiscard]] QStringList compatibility_warnings_for_document(const Document& document);
void show_compatibility_report(QWidget* parent, const Document& document, const QString& source_name);

}  // namespace patchy::ui
