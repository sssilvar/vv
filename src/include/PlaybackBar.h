#pragma once

#include <QWidget>

class QLabel;
class QSlider;
class QComboBox;
class QToolButton;

// Bottom-overlay media bar for temporal (playable) meshes: play/pause, a scrub
// slider, a frame/time readout, a speed multiplier, and a loop toggle.
//
// The widget is intent-only: it emits what the user asked for and reflects state
// pushed back via setStep()/setPlaying(). The owner drives the actual frame timer.
class PlaybackBar : public QWidget {
  Q_OBJECT
public:
  explicit PlaybackBar(int numSteps, QWidget* parent = nullptr);

  int currentStep() const;
  double speedMultiplier() const;
  bool loopEnabled() const;
  bool isPlaying() const {
    return playing_;
  }

  // Reflect externally driven state without re-emitting signals.
  void setStep(int step, double timeValue);
  void setPlaying(bool playing);

signals:
  void playToggled(bool playing);
  void stepRequested(int step);
  void speedChanged(double multiplier);
  void loopToggled(bool loop);

private:
  void updateReadout(int step, double timeValue);

  int numSteps_ = 0;
  bool playing_ = false;

  QToolButton* playButton_ = nullptr;
  QSlider* slider_ = nullptr;
  QLabel* readout_ = nullptr;
  QComboBox* speedBox_ = nullptr;
  QToolButton* loopButton_ = nullptr;
};
