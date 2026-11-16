// Text recognition on the MainWindow side (docs/ocr.md): the style new recognized text
// takes from the Type tool, and placing a finished "Recognized Text" group in a document.
// The group itself is built in main_window.cpp (make_recognized_text_group), beside the
// text metadata writers it needs.
//
// Binding rule (the "Text recognition" entry of docs/legal-constraints-scanning.md): "show
// text and hide original" hides layers. Nothing here erases, fills or heals the pixels the
// text was read from.

#include "ui/main_window.hpp"

#include "core/layer.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/document_session.hpp"
#include "ui/localization.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/ocr_recognition.hpp"
#include "ui/recognize_text_dialog.hpp"
#include "ui/text_font_service.hpp"
#include "ui/unit_spin_box.hpp"

#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QProgressDialog>
#include <QSize>
#include <QStatusBar>

#include <exception>
#include <utility>

namespace patchy::ui {

namespace {

// How long a recognition runs before its progress dialog appears.
constexpr int kOcrProgressDelayMs = 400;

}  // namespace

OcrLayerStyle MainWindow::default_ocr_layer_style() const {
  OcrLayerStyle style;
  style.family = text_font_combo_ != nullptr ? text_options_bar_family() : default_text_family();
  style.anti_alias = text_anti_alias_;
  if (text_size_spin_ != nullptr && has_active_document()) {
    style.size = text_points_to_pixels(text_size_spin_->value(), document());
  }
  if (canvas_ != nullptr) {
    style.color = canvas_->primary_color();
  }
  return style;
}

QString MainWindow::run_text_recognition(DocumentSession& target, const OcrRecognitionRequest& request,
                                         OcrRecognitionOutcome& outcome,
                                         const std::function<void(ocr::Cancel&)>& pump,
                                         const std::function<QString()>& begin_change) {
  QString error;
  const auto choice = resolve_ocr_choice(request.engine, request.language, &error);
  if (choice.engine == nullptr) {
    return error;
  }
  const auto source = ocr_source_image(std::as_const(target.document), request.layer, request.rect, &error);
  if (!source.has_value()) {
    return error;
  }
  const auto session_id = target.session_id;
  const QSize canvas_size(target.document.width(), target.document.height());

  ocr::Cancel cancel;
  ocr::Result found;
  try {
    found = recognize_on_worker(choice.engine, choice.language, source->image, cancel, [&] {
      if (pump) {
        pump(cancel);
      }
    });
  } catch (const ocr::RecognitionCancelled&) {
    outcome.cancelled = true;
    return {};
  } catch (const std::exception& failure) {
    return tr("Text recognition failed: %1").arg(translate_data_text(failure.what()));
  }

  // The wait turned the event loop: the document may have closed or changed under it.
  auto* session = session_with_id(session_id);
  if (session == nullptr || session->document.width() != canvas_size.width() ||
      session->document.height() != canvas_size.height() ||
      (request.layer.has_value() && std::as_const(session->document).find_layer(*request.layer) == nullptr)) {
    return tr("The document changed while its text was being recognized.");
  }
  outcome.engine = QString::fromUtf8(choice.engine->id().data(), static_cast<qsizetype>(choice.engine->id().size()));
  outcome.language = QString::fromStdString(choice.language);
  outcome.result = recognition_in_document(found, source->origin);
  if (request.layers.mode == OcrLayerMode::None || found.lines.empty()) {
    return {};
  }
  if (begin_change) {
    if (const auto refused = begin_change(); !refused.isEmpty()) {
      return refused;
    }
    session = session_with_id(session_id);
    if (session == nullptr) {
      return tr("The document changed while its text was being recognized.");
    }
  }
  auto group = make_recognized_text_group(session->document, found, *source, request.layers, choice.engine->id(),
                                          choice.language);
  if (group.has_value()) {
    outcome.group = insert_recognized_text_group(*session, std::move(*group), request.layer, request.layers.mode);
  }
  return {};
}

RecognizeTextDialogContext MainWindow::recognize_text_context() {
  RecognizeTextDialogContext context;
  select_only_layer_if_none_active();
  context.has_layer = document().active_layer_id().has_value();
  context.has_selection = canvas_->has_selection() && canvas_->selected_document_rect().has_value();
  context.type_tool_family = default_ocr_layer_style().family;
  return context;
}

void MainWindow::recognize_text() {
  if (canvas_ == nullptr || !has_active_document() || preview_dialog_edit_locked()) {
    return;
  }
  finish_active_text_editor();
  if (const auto choice = request_recognize_text(this, recognize_text_context()); choice.has_value()) {
    run_recognize_text_choice(*choice);
  }
}

void MainWindow::copy_text_from_image() {
  if (canvas_ == nullptr || !has_active_document() || preview_dialog_edit_locked()) {
    return;
  }
  finish_active_text_editor();
  const auto context = recognize_text_context();
  if (resolve_ocr_choice({}, {}, nullptr).engine == nullptr) {
    // Nothing can run: the dialog says why and what to do about it.
    if (const auto choice = request_recognize_text(this, context); choice.has_value()) {
      run_recognize_text_choice(*choice);
    }
    return;
  }
  // No dialog: the selection, else the active layer, else the image; the stored engine and
  // language (empty picks resolve to them); the text on the clipboard.
  RecognizeTextChoice choice;
  choice.source = context.has_selection ? OcrSource::Selection
                                        : context.has_layer ? OcrSource::Layer : OcrSource::Image;
  choice.mode = OcrLayerMode::None;
  choice.copy_text = true;
  run_recognize_text_choice(choice);
}

void MainWindow::run_recognize_text_choice(const RecognizeTextChoice& choice) {
  auto* session = active_session();
  if (session == nullptr || canvas_ == nullptr) {
    return;
  }
  if (canvas_->quick_mask_active()) {
    show_status_error(tr("%1 is unavailable in Quick Mask mode").arg(tr("Recognize Text")));
    return;
  }
  OcrRecognitionRequest request;
  request.engine = choice.engine;
  request.language = choice.language;
  switch (choice.source) {
    case OcrSource::Layer:
      request.layer = std::as_const(session->document).active_layer_id();
      break;
    case OcrSource::Selection:
      request.rect = canvas_->selected_document_rect();
      break;
    case OcrSource::Image:
      break;
  }
  request.layers.mode = choice.mode;
  request.layers.grouping = choice.grouping;
  request.layers.style = default_ocr_layer_style();
  if (!choice.font_family.isEmpty()) {
    request.layers.style.family = choice.font_family;
  }
  request.layers.style.match_style = choice.match_style;

  OcrRecognitionOutcome outcome;
  QString error;
  {
    // A window-modal dialog, shown by the pump only once the wait is worth one (the built-in
    // engine usually answers sooner). Input reaches nothing but that dialog: before it is
    // up, and after Cancel hid it, input waits in the queue, so the document cannot change
    // under the engine.
    QProgressDialog progress(tr("Recognizing text..."), tr("Cancel"), 0, 0, this);
    progress.setObjectName(QStringLiteral("recognizeTextProgressDialog"));
    progress.setWindowTitle(tr("Recognize Text"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    // The pump shows it; the dialog's own timer, where it runs, agrees on when.
    progress.setMinimumDuration(kOcrProgressDelayMs);
    QElapsedTimer waited;
    waited.start();
    const auto pump = [&progress, &waited](ocr::Cancel& cancel) {
      if (!progress.wasCanceled() && !progress.isVisible() && waited.elapsed() >= kOcrProgressDelayMs) {
        progress.show();
      }
      QApplication::processEvents(progress.isVisible() ? QEventLoop::AllEvents
                                                       : QEventLoop::ExcludeUserInputEvents);
      if (progress.wasCanceled()) {
        cancel.request();
      }
    };
    const auto begin_change = [this]() -> QString {
      push_undo_snapshot(tr("Recognize Text"));
      return {};
    };
    error = run_text_recognition(*session, request, outcome, pump, begin_change);
    progress.reset();
  }
  if (!error.isEmpty()) {
    show_status_error(error);
    return;
  }
  if (outcome.cancelled) {
    statusBar()->showMessage(tr("Text recognition cancelled"));
    return;
  }
  const int lines = static_cast<int>(outcome.result.lines.size());
  if (lines == 0) {
    show_status_error(tr("No text was recognized"));
    return;
  }
  if (choice.copy_text) {
    const auto text = outcome.result.text();
    QGuiApplication::clipboard()->setText(QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())));
    statusBar()->showMessage(tr("Copied %n line(s) of recognized text", nullptr, lines));
    return;
  }
  statusBar()->showMessage(tr("Recognized %n line(s) of text", nullptr, lines));
}

LayerId MainWindow::insert_recognized_text_group(DocumentSession& target, Layer group,
                                                 std::optional<LayerId> source_layer, OcrLayerMode mode) {
  auto& doc = target.document;
  const auto group_id = group.id();
  if (mode == OcrLayerMode::Replace) {
    // Hidden, never erased: Show brings the original back as it was.
    if (source_layer.has_value()) {
      if (auto* source = doc.find_layer(*source_layer); source != nullptr) {
        source->set_visible(false);
      }
    } else {
      for (auto& layer : doc.layers()) {
        layer.set_visible(false);
      }
    }
  }
  // Above the layer it was read from; on top of the stack when the whole image was read.
  insert_layer_after_anchor(doc, std::move(group), source_layer);
  doc.set_active_layer(group_id);
  if (&target == active_session()) {
    refresh_layer_list();
    reveal_layer_in_layer_list(group_id);
    refresh_layer_controls();
    refresh_document_info();
  }
  if (target.canvas != nullptr) {
    target.canvas->document_changed();
  }
  return group_id;
}

}  // namespace patchy::ui
