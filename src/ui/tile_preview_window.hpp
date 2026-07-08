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

// View > Seamless Tile Preview: a live tool window painting the flattened composite tiled
// across the viewport so wrap seams are visible while painting. Drag pans, double-click
// recenters, and the corner size grip resizes (size remembered across sessions).
// Deliberately NOT rendered in-canvas: the
// canvas paint path and its ~10 dirty-rect mapping sites would each need 9-way replication,
// and overlays/ants would only draw on the center tile. A ~150 ms timer polls a cheap
// revision probe (const reads only — mutable Layer accessors bump revisions) and re-renders
// on change; documents above ~1 Mpx switch to the manual Refresh button.
class TilePreviewWindow : public QDialog {
  Q_OBJECT

public:
  // document_provider returns the CURRENT document (or null); pulled fresh on every tick so
  // tab switches and closes can never leave a dangling pointer here.
  TilePreviewWindow(std::function<const Document*()> document_provider, QWidget* parent);

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

  std::function<const Document*()> provider_;
  QTimer timer_;
  TileViewWidget* view_{nullptr};
  QComboBox* zoom_combo_{nullptr};
  QLabel* status_label_{nullptr};
  QPushButton* refresh_button_{nullptr};
  QSizeGrip* size_grip_{nullptr};
  std::uint64_t last_probe_{0};
};

}  // namespace patchy::ui
