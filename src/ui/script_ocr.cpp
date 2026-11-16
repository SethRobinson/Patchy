// patchy.ocr, layer.recognizeText and doc.recognizeText (docs/ocr.md, docs/scripting.md).
// A recognition runs only because the script called for it, with the engine and language
// the call names or the user's stored picks: nothing is chosen from the image, and no
// model is downloaded from here (the same boundary as the interactive command).

#include "ui/script_api.hpp"
#include "ui/script_vector.hpp"

#include "ocr/ocr_engine.hpp"
#include "ui/document_session.hpp"
#include "ui/localization.hpp"
#include "ui/main_window.hpp"
#include "ui/ocr_recognition.hpp"
#include "ui/script_engine.hpp"

#include <QJSEngine>
#include <QJsonObject>

#include <exception>
#include <utility>

namespace patchy::ui {
namespace {

constexpr int kMaxScriptDimension = 30000;

QString from_utf8(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QJSValue box_object(QJSEngine& engine, const ocr::Box& box) {
  auto value = engine.newObject();
  value.setProperty(QStringLiteral("x"), box.x);
  value.setProperty(QStringLiteral("y"), box.y);
  value.setProperty(QStringLiteral("width"), box.width);
  value.setProperty(QStringLiteral("height"), box.height);
  return value;
}

// The options both calls take, read strictly: an unknown key or a value of the wrong kind
// throws before anything is read or changed.
OcrRecognitionRequest parse_request(const QJSValue& options) {
  OcrRecognitionRequest request;
  request.layers.mode = OcrLayerMode::None;
  request.layers.style.family.clear();  // empty: the Type tool's font (recognize_text fills it)
  const auto args = script_vector::object(options, true);
  script_vector::keys(args, {QStringLiteral("engine"), QStringLiteral("language"), QStringLiteral("rect"),
                             QStringLiteral("layers"), QStringLiteral("grouping"), QStringLiteral("font"),
                             QStringLiteral("matchStyle"), QStringLiteral("color")});
  request.engine = script_vector::string(args, QStringLiteral("engine"));
  request.language = script_vector::string(args, QStringLiteral("language"));
  if (args.contains(QStringLiteral("rect"))) {
    const auto rect = script_vector::child_object(args, QStringLiteral("rect"));
    script_vector::keys(rect, {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"), QStringLiteral("height")});
    request.rect = QRect(script_vector::integer(rect, QStringLiteral("x"), 0, -kMaxScriptDimension, kMaxScriptDimension),
                         script_vector::integer(rect, QStringLiteral("y"), 0, -kMaxScriptDimension, kMaxScriptDimension),
                         script_vector::integer(rect, QStringLiteral("width"), 0, 1, kMaxScriptDimension),
                         script_vector::integer(rect, QStringLiteral("height"), 0, 1, kMaxScriptDimension));
  }
  if (const auto mode = ocr_layer_mode_from_token(script_vector::string(args, QStringLiteral("layers"), QStringLiteral("none")))) {
    request.layers.mode = *mode;
  } else {
    script_vector::invalid(QStringLiteral("layers (none, visible, hidden or replace)"));
  }
  if (const auto grouping =
          ocr_grouping_from_token(script_vector::string(args, QStringLiteral("grouping"), QStringLiteral("blocks")))) {
    request.layers.grouping = *grouping;
  } else {
    script_vector::invalid(QStringLiteral("grouping (blocks or lines)"));
  }
  if (const auto font = script_vector::string(args, QStringLiteral("font")); !font.trimmed().isEmpty()) {
    request.layers.style.family = font.trimmed();
  }
  request.layers.style.match_style = script_vector::boolean(args, QStringLiteral("matchStyle"), true);
  if (const auto color = script_vector::string(args, QStringLiteral("color"), QStringLiteral("sample"));
      color != QLatin1String("sample")) {
    const QColor parsed(color);
    if (!color.startsWith(QLatin1Char('#')) || !parsed.isValid()) {
      script_vector::invalid(QStringLiteral("color (\"sample\" or \"#rrggbb\")"));
    }
    request.layers.style.fixed_color = parsed;
  }
  return request;
}

QJSValue outcome_object(ScriptEngineHost& host, std::int64_t session_id, const OcrRecognitionOutcome& outcome) {
  auto& engine = *host.engine();
  auto value = engine.newObject();
  value.setProperty(QStringLiteral("engine"), outcome.engine);
  value.setProperty(QStringLiteral("language"), outcome.language);
  value.setProperty(QStringLiteral("text"), from_utf8(outcome.result.text()));
  value.setProperty(QStringLiteral("angle"), outcome.result.text_angle_degrees.value_or(0.0));
  auto lines = engine.newArray(static_cast<quint32>(outcome.result.lines.size()));
  for (std::size_t index = 0; index < outcome.result.lines.size(); ++index) {
    const auto& line = outcome.result.lines[index];
    auto entry = box_object(engine, line.box);
    entry.setProperty(QStringLiteral("text"), from_utf8(line.text));
    entry.setProperty(QStringLiteral("angle"), line.angle_degrees);
    entry.setProperty(QStringLiteral("vertical"), line.vertical);
    auto words = engine.newArray(static_cast<quint32>(line.words.size()));
    for (std::size_t word_index = 0; word_index < line.words.size(); ++word_index) {
      auto word = box_object(engine, line.words[word_index].box);
      word.setProperty(QStringLiteral("text"), from_utf8(line.words[word_index].text));
      words.setProperty(static_cast<quint32>(word_index), word);
    }
    entry.setProperty(QStringLiteral("words"), words);
    lines.setProperty(static_cast<quint32>(index), entry);
  }
  value.setProperty(QStringLiteral("lines"), lines);
  value.setProperty(QStringLiteral("group"), outcome.group.has_value() ? make_layer_value(host, session_id, *outcome.group)
                                                                        : QJSValue(QJSValue::NullValue));
  return value;
}

QJSValue recognize_text_call(ScriptEngineHost& host, std::int64_t session_id, std::optional<LayerId> layer,
                             const QJSValue& options) {
  OcrRecognitionRequest request;
  try {
    request = parse_request(options);
  } catch (const std::exception& error) {
    host.throw_js_error(ScriptEngineHost::tr("Invalid recognizeText option: %1.").arg(QString::fromUtf8(error.what())));
    return QJSValue();
  }
  request.layer = layer;
  OcrRecognitionOutcome outcome;
  if (const auto error = host.recognize_text(session_id, request, outcome); !error.isEmpty()) {
    host.throw_js_error(error);
    return QJSValue();
  }
  return outcome_object(host, session_id, outcome);
}

}  // namespace

QString ScriptEngineHost::recognize_text(std::int64_t session_id, const OcrRecognitionRequest& request,
                                         OcrRecognitionOutcome& outcome) {
  auto* session = window_.session_with_id(session_id);
  if (session == nullptr) {
    return tr("The document is no longer open.");
  }
  const auto pump = [this](ocr::Cancel& cancel) {
    keep_alive_for_ui();
    if (engine()->isInterrupted()) {
      cancel.request();
    }
  };
  const auto begin_change = [this, session_id]() -> QString {
    if (engine()->isInterrupted() || !prepare_mutation(session_id) || engine()->isInterrupted()) {
      return tr("The script was interrupted.");
    }
    return {};
  };
  // What the script left unsaid comes from the Type tool: its font, size, colour, smoothing.
  auto filled = request;
  const auto asked = request.layers.style;
  filled.layers.style = window_.default_ocr_layer_style();
  if (!asked.family.isEmpty()) {
    filled.layers.style.family = asked.family;
  }
  filled.layers.style.match_style = asked.match_style;
  filled.layers.style.fixed_color = asked.fixed_color;
  const auto error = window_.run_text_recognition(*session, filled, outcome, pump, begin_change);
  if (!error.isEmpty()) {
    return error;
  }
  if (outcome.cancelled) {
    return tr("The script was interrupted.");
  }
  if (outcome.group.has_value()) {
    note_structure_changed(session_id);
  }
  return {};
}

ScriptOcrObject::ScriptOcrObject(ScriptEngineHost& host) : host_(host) {}

bool ScriptOcrObject::available() const {
  const ScriptApiCall api_call(host_);
  for (const auto& engine : ocr::engines()) {
    if (engine->unavailable() == ocr::Unavailable::None) {
      return true;
    }
  }
  return false;
}

QJSValue ScriptOcrObject::engines() {
  const ScriptApiCall api_call(host_);
  auto& js = *host_.engine();
  const auto listed = ocr::engines();
  auto array = js.newArray(static_cast<quint32>(listed.size()));
  for (std::size_t index = 0; index < listed.size(); ++index) {
    const auto& engine = listed[index];
    auto entry = js.newObject();
    entry.setProperty(QStringLiteral("id"), from_utf8(engine->id()));
    entry.setProperty(QStringLiteral("name"), QString::fromStdString(engine->display_name()));
    entry.setProperty(QStringLiteral("available"), engine->unavailable() == ocr::Unavailable::None);
    entry.setProperty(QStringLiteral("unavailableReason"), translate_data_text(engine->unavailable_reason()));
    entry.setProperty(QStringLiteral("defaultLanguage"), QString::fromStdString(engine->default_language()));
    const auto languages = engine->languages();
    auto language_array = js.newArray(static_cast<quint32>(languages.size()));
    for (std::size_t language_index = 0; language_index < languages.size(); ++language_index) {
      auto language = js.newObject();
      language.setProperty(QStringLiteral("tag"), QString::fromStdString(languages[language_index].tag));
      language.setProperty(QStringLiteral("name"), QString::fromStdString(languages[language_index].display_name));
      language_array.setProperty(static_cast<quint32>(language_index), language);
    }
    entry.setProperty(QStringLiteral("languages"), language_array);
    array.setProperty(static_cast<quint32>(index), entry);
  }
  return array;
}

QJSValue ScriptLayerObject::recognizeText(const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  return recognize_text_call(host_, session_id_, layer_id_, options);
}

QJSValue ScriptDocumentObject::recognizeText(const QJSValue& options) {
  const ScriptApiCall api_call(host_);
  return recognize_text_call(host_, session_id_, std::nullopt, options);
}

}  // namespace patchy::ui
