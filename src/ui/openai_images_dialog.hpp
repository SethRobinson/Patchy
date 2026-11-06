#pragma once
// Plugins > OpenAI Image Tools (docs/openai-images.md): a non-modal dialog that sends the user's
// prompt, and optional reference images, to OpenAI's Images API with the user's own API key
// and adds each returned image to the document as a new pixel layer.
#include "ui/openai_images.hpp"

#include <QDialog>
#include <QElapsedTimer>
#include <QMargins>
#include <QPointer>
#include <QWidget>

#include <functional>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QButtonGroup;
class QMimeData;
class QAction;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTimer;

namespace patchy::ui {
class MainWindow;

// Expand's frame: a thumbnail of the image inside the grown canvas. Dragging an edge or a
// corner outward adds pixels on that side; the frame never goes inside the image (Expand
// only grows the canvas). The four pixel fields and this view stay in step.
class OpenAiExpandFrameView final : public QWidget {
 public:
  explicit OpenAiExpandFrameView(QWidget* parent);
  void set_image(const QImage& thumbnail, QSize canvas);
  void set_margins(QMargins margins);
  [[nodiscard]] QMargins margins() const { return margins_; }
  // Called with the new margins while a drag changes them.
  void set_on_margins_changed(std::function<void(QMargins)> callback) { on_changed_ = std::move(callback); }
  // The frame's rectangle in widget coordinates (tests drag its edges).
  [[nodiscard]] QRectF frame_rect() const;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  enum Edge { None = 0, Left = 1, Top = 2, Right = 4, Bottom = 8 };
  [[nodiscard]] double layout_scale() const;
  [[nodiscard]] int edges_at(QPointF position) const;
  void update_cursor(int edges);

  QImage thumbnail_;
  QSize canvas_;
  QMargins margins_;
  std::function<void(QMargins)> on_changed_;
  int drag_edges_{None};
  QPointF drag_start_;
  QMargins drag_start_margins_;
  double drag_scale_{1.0};
};

class OpenAiImagesDialog final : public QDialog {
  Q_OBJECT
 public:
  // The operation tabs, in order.
  enum Tab { CreateTab = 0, EditTab = 1, RemoveTab = 2, ExpandTab = 3, AnimateTab = 4, DescribeTab = 5, ConfigTab = 6 };
  // `window` may be null (the offscreen dialog sweep): the reference buttons and results
  // then have nowhere to go.
  explicit OpenAiImagesDialog(MainWindow* window, QWidget* parent = nullptr);
  ~OpenAiImagesDialog() override;

  void done(int result) override;

 protected:
  // Claims the Paste keys from Patchy's application-wide hotkeys while this dialog is active
  // and the focus is not in a text field, so Ctrl+V adds a reference instead of pasting into
  // the document.
  bool eventFilter(QObject* watched, QEvent* event) override;

 public:

  // Tests.
  [[nodiscard]] openai::Client& client() { return *client_; }
  [[nodiscard]] int reference_count() const;
  // The Variations choice: images per request.
  [[nodiscard]] int image_count() const;
  void add_reference_for_testing(const QImage& image, const QString& label);
  // Adds the clipboard's image (or its copied image files) as references. False when it
  // holds none. Paste and Ctrl+V outside the text fields call it.
  bool paste_references_from_clipboard();
  // The platform's note on where a remembered key is kept (the info mark beside Remember).
  [[nodiscard]] static QString key_storage_note();
  [[nodiscard]] bool busy() const;
  // The Results row: how many variations it holds (0 without a current result) and which one
  // the result layer shows; show_variation swaps another in (tests click its buttons too).
  [[nodiscard]] int result_count() const;
  [[nodiscard]] int shown_variation() const;
  bool show_variation(int index);

 private:
  void build_ui();
  void load_settings();
  void save_settings() const;
  // The key in the field, remembered in (or cleared from) the system store as the
  // checkbox says. Empty with a status message when there is none.
  [[nodiscard]] QString take_api_key();
  void check_key();
  void generate();
  void start_request();  // the current tab's request, after the click's feedback
  [[nodiscard]] QString busy_button_text() const;
  void generate_masked(bool remove);
  void generate_expand();
  void generate_animation();
  void generate_description();
  // Describe: the image the chosen source gives now (null with `error` when there is none),
  // and the preview of it on the tab.
  [[nodiscard]] QImage describe_source_image(QString* error) const;
  void refresh_describe_preview();
  void update_expand_readout();
  // A result with several variations: kept here (never in the document), one shown in the
  // result layer, the rest a click away until the next request, a close, or an edit.
  struct ResultSet {
    std::int64_t session_id{-1};
    std::uint64_t layer_id{0};
    std::uint64_t revision{0};
    std::vector<QImage> images;                     // as the API returned them (prepared per kind)
    std::function<QImage(const QImage&)> prepare;   // to the pixels the layer gets
    bool into_active_layer{false};
    QRect region;                                   // the active layer write's region
    int shown{0};
    // Edit and Remove: the selection over the region and the seam band, for Edge blend.
    QImage selection;
    int band{0};
    bool edge_adjustable{false};
    bool expand_adjustable{false};  // Expand: the Blend slider re-prepares the pixels
  };
  void present_results(ResultSet results);
  void clear_results();
  void report_added(int added, std::int64_t tokens);
  // Edge blend: the slider's amount (0 to 1), and its application to the current result.
  [[nodiscard]] double edge_amount() const;
  void apply_edge_blend();
  [[nodiscard]] double expand_blend_amount() const;
  void apply_expand_blend();
  void update_action_button();
  void cancel_request();
  void set_busy(bool busy, const QString& message = {}, const QString& button_text = {});
  void advance_spinner();
  void show_error(const QString& message, const openai::ErrorHelp& help);
  void show_status(const QString& message, bool error = false);
  void add_reference(const QImage& image, const QString& label);
  // References from dropped or pasted data: image files (in order) or image data. False when
  // it holds neither.
  bool add_references_from_mime(const QMimeData* mime);
  // "1. Label", "2. Label"...: the numbers the prompt uses, in the order the request sends them.
  void number_references();
  void fit_tabs_to_current_page();
  void fit_height_to_current_tab();
  // What the first reference is numbered on the current tab: 2 when the picture being edited
  // (Edit) or the starting image (Animate) goes first, else 1.
  [[nodiscard]] int first_reference_number() const;
  [[nodiscard]] std::vector<QImage> reference_images(int max) const;
  void remove_reference(int row);
  void add_reference_from_layer();
  void add_reference_from_visible();
  void add_reference_from_file();
  void update_reference_buttons();
  void paste_or_report();
  void select_available_models(const QStringList& available);
  [[nodiscard]] openai::GenerateRequest build_request(bool* fit_to_canvas) const;

  QPointer<MainWindow> window_;
  std::unique_ptr<openai::Client> client_;
  std::unique_ptr<openai::KeyStore> key_store_;
  QString stored_key_;
  std::int64_t target_session_id_{-1};
  QElapsedTimer elapsed_;

  QLineEdit* key_edit_{nullptr};
  QCheckBox* remember_check_{nullptr};
  QPushButton* check_button_{nullptr};
  QPlainTextEdit* prompt_edit_{nullptr};
  QPlainTextEdit* edit_prompt_edit_{nullptr};
  QPlainTextEdit* expand_prompt_edit_{nullptr};
  QSpinBox* expand_left_{nullptr};
  QSpinBox* expand_top_{nullptr};
  QSpinBox* expand_right_{nullptr};
  QSpinBox* expand_bottom_{nullptr};
  QLabel* expand_readout_{nullptr};
  OpenAiExpandFrameView* expand_view_{nullptr};
  QPlainTextEdit* animate_prompt_edit_{nullptr};
  QComboBox* animate_source_combo_{nullptr};
  QSpinBox* animate_frames_spin_{nullptr};
  QSpinBox* animate_delay_spin_{nullptr};
  QComboBox* animate_size_combo_{nullptr};
  QComboBox* animate_background_combo_{nullptr};
  QComboBox* animate_colors_combo_{nullptr};
  QComboBox* describe_source_combo_{nullptr};
  QLabel* describe_preview_{nullptr};
  QPlainTextEdit* describe_text_edit_{nullptr};
  QWidget* references_box_{nullptr};
  QListWidget* references_list_{nullptr};
  QAction* add_layer_button_{nullptr};
  QAction* add_visible_button_{nullptr};
  QAction* add_file_button_{nullptr};
  QPushButton* paste_reference_button_{nullptr};
  QPushButton* remove_reference_button_{nullptr};
  QComboBox* model_combo_{nullptr};
  QComboBox* size_combo_{nullptr};
  QComboBox* quality_combo_{nullptr};
  QComboBox* background_combo_{nullptr};
  QButtonGroup* count_group_{nullptr};
  QWidget* results_row_{nullptr};
  QLabel* results_label_{nullptr};
  QButtonGroup* results_group_{nullptr};
  QComboBox* target_combo_{nullptr};
  QSlider* edge_slider_{nullptr};
  QSlider* expand_blend_slider_{nullptr};
  QLabel* expand_blend_label_{nullptr};
  QTimer* expand_blend_timer_{nullptr};
  QLabel* edge_value_label_{nullptr};
  QWidget* edge_row_{nullptr};
  QTimer* edge_timer_{nullptr};
  QLabel* target_label_{nullptr};
  std::optional<ResultSet> results_;
  QTabWidget* mode_tabs_{nullptr};
  QLabel* status_label_{nullptr};
  QPushButton* generate_button_{nullptr};
  QPushButton* stop_button_{nullptr};
  QTimer* busy_timer_{nullptr};
  QTimer* spinner_timer_{nullptr};
  int spinner_angle_{0};
  QString busy_message_;
};

}  // namespace patchy::ui
