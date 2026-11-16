#include "ui/ocr_controls.hpp"

#include "ui/app_settings.hpp"
#include "ui/localization.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <utility>

namespace patchy::ui {

namespace {

std::function<void()>& language_settings_opener() {
  static std::function<void()> opener;
  return opener;
}

QString from_utf8(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::shared_ptr<ocr::Engine> engine_with_id(const QString& id) {
  return id.isEmpty() ? nullptr : ocr::find_engine(id.toStdString());
}

}  // namespace

QString ocr_engine_display_name(const ocr::Engine& engine) {
  if (engine.id() == "windows") {
    return QObject::tr("Windows (built in)");
  }
  return QString::fromStdString(engine.display_name());
}

void set_ocr_language_settings_opener_for_testing(std::function<void()> opener) {
  language_settings_opener() = std::move(opener);
}

OcrEnginePicker::OcrEnginePicker(const QString& object_name_prefix, QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  auto* form = new QFormLayout();
  form->setContentsMargins(0, 0, 0, 0);
  engine_ = new QComboBox(this);
  engine_->setObjectName(object_name_prefix + QStringLiteral("EngineCombo"));
  form->addRow(QObject::tr("Engine:"), engine_);
  language_ = new QComboBox(this);
  language_->setObjectName(object_name_prefix + QStringLiteral("LanguageCombo"));
  form->addRow(QObject::tr("Language:"), language_);
  layout->addLayout(form);

  note_ = new QLabel(this);
  note_->setObjectName(object_name_prefix + QStringLiteral("UnavailableNote"));
  note_->setWordWrap(true);
  layout->addWidget(note_);
  language_settings_ = new QPushButton(QObject::tr("Open Language Settings"), this);
  language_settings_->setObjectName(object_name_prefix + QStringLiteral("LanguageSettingsButton"));
  layout->addWidget(language_settings_, 0, Qt::AlignLeft);
  QObject::connect(language_settings_, &QPushButton::clicked, this, [] {
    if (language_settings_opener()) {
      language_settings_opener()();
    } else {
      QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:regionlanguage")));
    }
  });

  fill_engines();
  QObject::connect(engine_, &QComboBox::currentIndexChanged, this, [this] {
    fill_languages();
    update_note();
    notify();
  });
  QObject::connect(language_, &QComboBox::currentIndexChanged, this, [this] { notify(); });
  // A language added in Windows Settings shows up when the user comes back.
  QObject::connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
    if (state == Qt::ApplicationActive) {
      refresh();
    }
  });
}

void OcrEnginePicker::fill_engines() {
  const QSignalBlocker blocker(engine_);
  const auto previous = engine_->count() > 0 ? engine_->currentData().toString()
                                             : app_settings().value(QLatin1String(kOcrEngineSettingsKey)).toString();
  engine_->clear();
  int first_available = -1;
  for (const auto& engine : ocr::engines()) {
    engine_->addItem(ocr_engine_display_name(*engine), from_utf8(engine->id()));
    const int row = engine_->count() - 1;
    if (engine->unavailable() == ocr::Unavailable::None) {
      first_available = first_available < 0 ? row : first_available;
    } else {
      engine_->setItemData(row, translate_data_text(engine->unavailable_reason()), Qt::ToolTipRole);
    }
  }
  // The stored engine while it is still listed and can run, else the first that can, else
  // the first listed (its note then says what is missing).
  int chosen = engine_->findData(previous);
  if (chosen >= 0) {
    const auto engine = engine_with_id(previous);
    if (engine == nullptr || engine->unavailable() != ocr::Unavailable::None) {
      chosen = first_available >= 0 ? first_available : chosen;
    }
  } else {
    chosen = first_available >= 0 ? first_available : 0;
  }
  if (engine_->count() > 0) {
    engine_->setCurrentIndex(chosen);
  }
  engine_->setEnabled(engine_->count() > 0);
  fill_languages();
  update_note();
}

void OcrEnginePicker::fill_languages() {
  const QSignalBlocker blocker(language_);
  const auto previous = language_->count() > 0
                            ? language_->currentData().toString()
                            : app_settings().value(QLatin1String(kOcrLanguageSettingsKey)).toString();
  language_->clear();
  const auto engine = engine_with_id(engine_id());
  if (engine != nullptr) {
    for (const auto& language : engine->languages()) {
      language_->addItem(QString::fromStdString(language.display_name), QString::fromStdString(language.tag));
    }
    int chosen = language_->findData(previous);
    if (chosen < 0) {
      chosen = language_->findData(QString::fromStdString(engine->default_language()));
    }
    if (language_->count() > 0) {
      language_->setCurrentIndex(std::max(0, chosen));
    }
  }
  language_->setEnabled(language_->count() > 0);
}

void OcrEnginePicker::update_note() {
  const auto engine = engine_with_id(engine_id());
  QString text;
  bool offer_settings = false;
  if (engine == nullptr) {
    text = QObject::tr("This version of Patchy has no text recognition engine for this operating system. Text "
                       "recognition uses the engine built into Windows 10 and 11.");
  } else if (engine->unavailable() == ocr::Unavailable::NoLanguages) {
    text = QObject::tr("Windows has no text recognition language installed. Open Settings > Time & language > "
                       "Language & region, add a language that offers text recognition, then come back here.");
    offer_settings = engine->id() == "windows";
  } else if (engine->unavailable() != ocr::Unavailable::None) {
    text = QObject::tr("Text recognition is not available: %1.").arg(translate_data_text(engine->unavailable_reason()));
  }
  note_->setText(text);
  note_->setVisible(!text.isEmpty());
  language_settings_->setVisible(offer_settings);
}

bool OcrEnginePicker::usable() const {
  const auto engine = engine_with_id(engine_id());
  return engine != nullptr && engine->unavailable() == ocr::Unavailable::None && !language_tag().isEmpty();
}

QString OcrEnginePicker::engine_id() const {
  return engine_->currentData().toString();
}

QString OcrEnginePicker::language_tag() const {
  return language_->currentData().toString();
}

QString OcrEnginePicker::summary() const {
  return usable() ? QObject::tr("%1, %2").arg(engine_->currentText(), language_->currentText()) : QString();
}

void OcrEnginePicker::store() const {
  if (!usable()) {
    return;
  }
  auto settings = app_settings();
  settings.setValue(QLatin1String(kOcrEngineSettingsKey), engine_id());
  settings.setValue(QLatin1String(kOcrLanguageSettingsKey), language_tag());
}

void OcrEnginePicker::on_changed(std::function<void()> callback) {
  changed_ = std::move(callback);
}

void OcrEnginePicker::refresh() {
  const bool was_usable = usable();
  const auto engine = engine_id();
  const auto language = language_tag();
  fill_engines();
  if (was_usable != usable() || engine != engine_id() || language != language_tag()) {
    notify();
  }
}

void OcrEnginePicker::notify() {
  if (changed_) {
    changed_();
  }
}

}  // namespace patchy::ui
