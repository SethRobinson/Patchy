#pragma once

#include "ui/book_scan_project.hpp"
#include "ui/scanner_session.hpp"

#include <QDialog>
#include <QImage>
#include <QMap>
#include <QPointer>

#include <map>
#include <memory>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSlider;
class QStackedWidget;

namespace patchy::ui {

class BookScanPreview;

// File > Import > Scan a Book to PDF (docs/book-scan.md): one non-modal wizard window.
// Setup (project folder, title, scanner) -> Cover (outline, spine, up direction,
// reading direction, capture settings) -> Scanning (one Scan per spread) -> Review and
// export (page order, page size, output mode, PDF). The project folder is saved after
// every scan and edit, so closing the window or a crash keeps the work: the setup step
// finds the book in its folder and continues it.
class BookScanDialog final : public QDialog {
  Q_OBJECT

public:
  enum class Step { Setup, Cover, Scanning, Review };

  explicit BookScanDialog(QWidget* parent);
  ~BookScanDialog() override;

  [[nodiscard]] Step step() const noexcept { return step_; }
  [[nodiscard]] const BookProject& project() const noexcept { return project_; }

  // Test and script surface: the same actions the buttons run, without file dialogs.
  void set_project_folder(const QString& folder);
  void set_title(const QString& title);
  // The book saved in the chosen folder, at its scanning or review step.
  void continue_book();
  // Deletes the files of the book saved in the chosen folder (after asking, unless
  // quiet) and keeps the folder for a new book.
  void start_over();
  void set_scan_mode(BookScanMode mode);
  void start_cover_scan();
  void skip_cover();
  void start_page_scanning();
  void start_page_scan();
  void rescan_last();
  // Scanning step: an earlier scan (its index) or the cover shown in the box editor with
  // its options, as a click on its thumbnail does; Back to Next Pages returns to the newest.
  void select_scan_for_editing(int index);
  void select_cover_for_editing();
  void back_to_next_pages();
  // Rescans what the scanning step shows: the newest scan, an earlier one, or the cover.
  void rescan_selected();
  // The scan the scanning step shows (-1 with none or the cover), and whether it is the cover.
  [[nodiscard]] int selected_scan() const;
  [[nodiscard]] bool cover_selected() const noexcept { return edit_cover_; }
  void finish_scanning();
  // Review step: moves the pages at `rows` (in the shown order) to stand before `before`,
  // as a drag and drop in the page list does.
  void move_review_pages(const std::vector<int>& rows, int before);
  // Writes the PDF; `ask_keep_folder` asks afterwards whether to keep the scan folder.
  bool export_pdf(const QString& path, bool ask_keep_folder);
  [[nodiscard]] bool scan_in_progress() const;
  // True while a scanner is open for the following scans; a scanner turning on or off
  // closes it, and the next scan opens it again.
  [[nodiscard]] bool scanner_open() const noexcept { return session_open_; }
  [[nodiscard]] QString last_error() const { return last_error_; }
  // Tests: no message boxes, no beep.
  void set_quiet(bool quiet) { quiet_ = quiet; }

protected:
  void done(int result) override;

private:
  void build_setup_page();
  void build_cover_page();
  void build_scanning_page();
  void build_review_page();
  void go_to(Step step);
  bool ensure_project_folder();
  // `report` false: no message box when the scanner cannot be opened.
  bool ensure_session_open(bool report = true);
  void connect_session();
  void refresh_devices();
  // Fills the scanner list, keeping the selection when it is still there.
  void apply_devices(const std::vector<ScannerDeviceInfo>& devices);
  // A scanner turned on or off (docs/book-scan.md, "Scanners turning on and off").
  void devices_changed(const std::vector<ScannerDeviceInfo>& devices);
  // Lets go of the open scanner; the next scan opens it again.
  void drop_session();
  void start_session_scan(const ScanRequest& request);
  // A scan failed because the scanner went away: open it again and repeat the scan once.
  bool retry_after_device_loss();
  void fill_resolution_combo();
  // Fills `combo` with the scanner's resolutions and selects the one nearest `dpi`.
  void fill_resolution_items(QComboBox* combo, int dpi);
  [[nodiscard]] int nearest_offered_dpi(int dpi) const;
  // The scanning step's Scan at and Mode: the next scans' settings, or what Rescan This
  // Scan uses for an earlier scan (that scan's own until changed).
  void refresh_capture_controls();
  void capture_controls_changed();
  void save_project_quietly();
  void scan_finished(const ScanResult& result);
  void apply_cover_scan(const QString& file, double dpi, bool keep_settings);
  // replace_index >= 0 rescans that scan; -1 adds a new one.
  void apply_page_scan(const QString& file, double dpi, ScanColorMode mode, int replace_index);
  [[nodiscard]] ScanRequest cover_scan_request() const;
  // Top of the book: turns every scan; a half turn keeps the boxes, a quarter turn detects again.
  void set_up_turns(int turns);
  // The book area in scanner coordinates, from the cover outline.
  void update_book_area();
  void edit_page(const BookPageRef& page);
  [[nodiscard]] bool editing_earlier_scan() const;
  QString store_scan_file(const QString& file, const QString& base_name);
  void refresh_cover_page();
  void cover_settings_changed();
  void refresh_scanning_page();
  void refresh_review_page();
  void refresh_review_preview();
  void page_output_changed();
  void update_page_output_combo();
  void select_review_rows(const std::vector<int>& rows);
  [[nodiscard]] std::vector<int> selected_review_rows() const;
  [[nodiscard]] std::vector<BookPageRef> review_order() const;
  void delete_selected_pages();
  void rotate_selected_pages();
  void insert_blank_page();
  void show_scan_error(const QString& text);
  void update_page_size_label();
  void update_cover_labels();
  void refresh_folder_state();
  void open_book_folder();
  [[nodiscard]] SpreadLayout detect_scan_layout(const PixelBuffer& upright, double dpi,
                                                const std::optional<OrientedBox>& fallback) const;
  [[nodiscard]] QImage thumbnail_for(const BookPageRef& page);
  struct SmallScan {
    PixelBuffer pixels;  // the unturned scan at kSmallScanDpi
    double scale{1.0};   // small / full
  };
  // A small copy of a scan, kept in memory and in thumbs/ so a resumed book does not
  // decode every full scan again for its thumbnails.
  [[nodiscard]] const SmallScan* small_scan(const QString& file, double dpi);
  // A screen-sized copy of a scan (thumbs/<name>-view.jpg, the last few in memory): the
  // box editor and the review preview show it, so a click on a page never decodes the
  // full scan (docs/book-scan.md, "Scan copies").
  struct ScanView {
    PixelBuffer pixels;  // the unturned scan, longest side at most kViewLongestSide
    QSize full_size;     // the unturned full scan's size
    double scale{1.0};   // view / full
  };
  [[nodiscard]] const ScanView* scan_view(const QString& file, double dpi);
  // Writes and keeps both copies of a scan just read in full, so neither reads it again.
  void remember_scan_copies(const QString& file, double dpi, const QImage& full);
  // Reads the full scan and makes both copies; false when it cannot be read.
  bool decode_scan_copies(const QString& file, double dpi);
  void forget_scan(const QString& file);
  struct UprightView {
    QImage image;     // the view copy, turned upright
    QSize full_size;  // the full upright scan's size, which box coordinates use
  };
  [[nodiscard]] const UprightView& upright_preview(const QString& file, double dpi, int turns);
  // A page cut from a scan's small or view copy at `dpi`, page turns applied, no output.
  [[nodiscard]] PixelBuffer cut_page_from_copy(const BookPageRef& page, const BookPageSize& size, double dpi,
                                               bool view_copy);
  [[nodiscard]] QString default_project_folder() const;
  [[nodiscard]] std::optional<QRectF> scan_area_for_pages() const;

  BookProject project_;
  std::unique_ptr<ScannerSession> session_;
  bool session_open_{false};
  ScannerCapabilities capabilities_;
  // The scanner the book uses is in the current list (false while it is switched off).
  bool device_listed_{true};
  // A device change arrived during a scan: close the scanner once the scan ends.
  bool drop_after_scan_{false};
  bool scan_retried_{false};
  ScanRequest last_request_;
  Step step_{Step::Setup};
  enum class PendingScan { None, Cover, Page, Rescan, RescanCover };
  PendingScan pending_{PendingScan::None};
  int rescan_target_{-1};
  // What the scanning step shows: the cover, an earlier scan, or (neither) the newest scan.
  bool edit_cover_{false};
  std::optional<int> edit_scan_;
  QString last_error_;
  // The book found in the setup step's folder, if any.
  std::optional<BookProject> folder_book_;
  bool title_from_folder_book_{false};
  bool quiet_{false};
  bool grayscale_hint_shown_{false};
  QMap<QString, QImage> thumbnails_;
  std::map<QString, SmallScan> small_scans_;
  std::map<QString, ScanView> scan_views_;
  std::vector<QString> scan_view_order_;  // most recently used last
  QString preview_key_;
  UprightView preview_;
  // What the page strip shows now, so a click that only changes the selection keeps it.
  QStringList strip_signature_;
  // Rescan This Scan's settings for the earlier scan being edited.
  int rescan_dpi_{300};
  ScanColorMode rescan_mode_{ScanColorMode::Color};
  // The upright image currently in the preview and what it shows.
  QImage preview_image_;

  QStackedWidget* stack_{nullptr};
  QLabel* step_title_{nullptr};
  // Setup.
  QLineEdit* title_edit_{nullptr};
  QLineEdit* author_edit_{nullptr};
  QLineEdit* folder_edit_{nullptr};
  QComboBox* device_combo_{nullptr};
  QCheckBox* system_dialog_check_{nullptr};
  QLabel* disk_label_{nullptr};
  QLabel* folder_status_label_{nullptr};
  QLabel* setup_instructions_{nullptr};
  QPushButton* scan_cover_button_{nullptr};
  QPushButton* skip_cover_button_{nullptr};
  QPushButton* continue_button_{nullptr};
  QPushButton* start_over_button_{nullptr};
  // Cover.
  BookScanPreview* cover_preview_{nullptr};
  QComboBox* up_combo_{nullptr};
  QComboBox* direction_combo_{nullptr};
  QDoubleSpinBox* cover_tilt_spin_{nullptr};
  QLabel* page_size_label_{nullptr};
  QComboBox* resolution_combo_{nullptr};
  QComboBox* color_combo_{nullptr};
  QComboBox* area_combo_{nullptr};
  QComboBox* mode_combo_{nullptr};
  QLabel* mode_notice_{nullptr};
  QCheckBox* covers_check_{nullptr};
  // Scanning.
  BookScanPreview* spread_preview_{nullptr};
  QLabel* instruction_label_{nullptr};
  QLabel* status_label_{nullptr};
  QLabel* edge_warning_{nullptr};
  QLabel* grayscale_hint_{nullptr};
  QPushButton* grayscale_switch_{nullptr};
  QProgressBar* progress_bar_{nullptr};
  QComboBox* pages_combo_{nullptr};
  QComboBox* use_combo_{nullptr};
  QComboBox* scan_up_combo_{nullptr};
  QPushButton* rotate_scan_button_{nullptr};
  QComboBox* scan_resolution_combo_{nullptr};
  QComboBox* scan_color_combo_{nullptr};
  QLabel* capture_note_{nullptr};
  QPushButton* back_to_next_button_{nullptr};
  QFormLayout* scan_form_{nullptr};
  QDoubleSpinBox* spread_tilt_spin_{nullptr};
  QListWidget* strip_{nullptr};
  QCheckBox* beep_check_{nullptr};
  QPushButton* scan_button_{nullptr};
  QPushButton* rescan_button_{nullptr};
  QPushButton* cancel_scan_button_{nullptr};
  QPushButton* finished_button_{nullptr};
  // Review.
  QListWidget* pages_list_{nullptr};
  QPushButton* edit_page_button_{nullptr};
  QLabel* review_preview_{nullptr};
  QComboBox* size_combo_{nullptr};
  QDoubleSpinBox* custom_width_{nullptr};
  QDoubleSpinBox* custom_height_{nullptr};
  QComboBox* output_combo_{nullptr};
  QComboBox* page_output_combo_{nullptr};
  QSlider* threshold_slider_{nullptr};
  QComboBox* output_dpi_combo_{nullptr};
  QComboBox* quality_combo_{nullptr};
  QLabel* review_summary_{nullptr};
  bool refreshing_{false};
};

// Runs the wizard as a non-modal window.
void run_book_scan_dialog(QWidget* parent);

}  // namespace patchy::ui
