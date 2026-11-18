#pragma once

#include <QIcon>
#include <QPointer>
#include <QWidget>

namespace patchy::ui {

class LogoIntro;

// The painted Patchy logo card shared by the About dialog and the start panel.
class SplashArtwork final : public QWidget {
 public:
  enum class Playback { FollowSystem, Animate };
  explicit SplashArtwork(QWidget* parent = nullptr, Playback playback = Playback::FollowSystem);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void hideEvent(QHideEvent* event) override;

 private:
  void start_intro();

  enum class IntroState { FirstPaint, Queued, Playing, Finished };
  const Playback playback_;
  IntroState state_{IntroState::FirstPaint};
  QPointer<LogoIntro> intro_;
  QIcon logo_;
};

}  // namespace patchy::ui
