#pragma once

#include <QDialog>
#include <QImage>
#include <QTimer>

#include <cstdint>
#include <functional>

class QComboBox;
class QLabel;
class QPushButton;
class QSizeGrip;

namespace patchy {
class Document;
}

namespace patchy::ui {

class TileViewWidget;

// Document-metadata key recording the wrap offset applied by "Shift Seams to Center", as
// "dx,dy". Present = seams currently sit in the center; the toggle then applies the exact
// inverse and erases the key. Undo snapshots copy document metadata, so the parity can
// never desync from the pixels across undo/redo.
inline constexpr const char* kTileSeamOffsetMetadataKey = "patchy.tile.seamOffset";

// View > Seamless Tile Preview: a live tool window painting the flattened composite tiled
// across the viewport so wrap seams are visible while painting. Drag pans, double-click
// recenters, and the corner size grip resizes (size remembered across sessions). The main
// canvas can also tile in place (View > Seamless Tiling in Window); this window remains
// the way to watch the tiling while zoomed into the document itself.
// A ~150 ms timer polls a cheap revision probe (const reads only — mutable Layer accessors
// bump revisions). A document switch re-renders immediately at any size; content edits
// re-render immediately up to 1 Mpx, after edits pause for one tick up to 16 Mpx, and via
// the manual Refresh button beyond that (a full recomposite per tick would stall painting).
class TilePreviewWindow : public QDialog {
  Q_OBJECT

public:
  // document_provider returns the CURRENT document (or null); pulled fresh on every tick so
  // tab switches and closes can never leave a dangling pointer here. shift_seams toggles
  // the document's wrap offset (MainWindow::toggle_tile_seam_offset); the button label and
  // enabled state follow the provider document's kTileSeamOffsetMetadataKey parity.
  TilePreviewWindow(std::function<const Document*()> document_provider,
                    std::function<void()> shift_seams, QWidget* parent);

  void refresh_now();

  // Every dismissal funnels here: the chrome X and Esc call reject() -> done(), and
  // close() (View menu uncheck, Alt+F4) reaches reject() via QDialog::closeEvent. Do NOT
  // instead override reject() to call close(): QDialog::closeEvent itself calls reject()
  // and treats a dialog still visible afterwards as a vetoed close, so that routing makes
  // every close path a no-op.
  void done(int result) override;

signals:
  void preview_closed();

protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  void tick();
  [[nodiscard]] std::uint64_t document_probe(const Document* document) const;
  [[nodiscard]] std::uint64_t document_identity_probe(const Document* document) const;
  void refresh_seam_button(const Document* document);

  std::function<const Document*()> provider_;
  std::function<void()> shift_seams_;
  QTimer timer_;
  TileViewWidget* view_{nullptr};
  QComboBox* zoom_combo_{nullptr};
  QLabel* status_label_{nullptr};
  QPushButton* refresh_button_{nullptr};
  QPushButton* seam_button_{nullptr};
  QSizeGrip* size_grip_{nullptr};
  // Probe bookkeeping for tick(): what the view currently renders, what the
  // last tick observed (the debounce comparator), and which document identity
  // the render came from (so tab switches refresh at any document size).
  std::uint64_t last_rendered_probe_{0};
  std::uint64_t last_seen_probe_{0};
  std::uint64_t last_rendered_identity_{0};
};

}  // namespace patchy::ui
