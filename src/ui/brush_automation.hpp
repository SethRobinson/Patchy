#pragma once
#include "ui/script_stroke.hpp"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <map>

namespace patchy::ui {
class BrushTipLibrary;

// Shared by scripting and the artist's preset controls. Only explicit library
// methods persist; resolving a brush never edits a tip or a document.
class BrushAutomationLibrary : public QObject {
  Q_OBJECT
 public:
  explicit BrushAutomationLibrary(BrushTipLibrary& tips, QObject* parent = nullptr,
                                  QString directory = {});
  void refresh();
  [[nodiscard]] QString revision() const;
  [[nodiscard]] QJsonArray tips() const;
  [[nodiscard]] QJsonObject tip_info(const QString& id) const;
  [[nodiscard]] QJsonArray presets() const;
  [[nodiscard]] QJsonObject preset(const QString& id) const;
  [[nodiscard]] ScriptStroke resolve(const QJsonObject& settings) const;
  QJsonObject capture(const ScriptStroke& settings);
  QString save(const QString& name, const ScriptStroke& settings, bool colors,
               const QString& existing = {}, const QString& folder = {});
  void remove(const QString& id);
  static QJsonObject settings(const ScriptStroke& stroke);
  static QStringList setting_keys();
 signals:
  void changed();
 private:
  BrushTipLibrary& tips_;
  QString directory_, fingerprint_, tip_fingerprint_, revision_;
  QJsonArray presets_;
  mutable std::map<QString, std::shared_ptr<const BrushTip>> preset_tips_;
  std::map<QString, std::shared_ptr<const BrushTip>> captured_tips_;
};

namespace brush_input {
void keys(const QJsonObject& object, const QStringList& allowed);
double number(const QJsonObject& object, const QString& key, double fallback, double low, double high,
              bool integral = false);
bool boolean(const QJsonObject& object, const QString& key, bool fallback);
QJsonObject object(const QJsonObject& parent, const QString& key);
[[noreturn]] void invalid(const QString& field);
}
}  // namespace patchy::ui
