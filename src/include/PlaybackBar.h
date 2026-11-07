#pragma once

#include <QAbstractSlider>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QWidget>
#include <optional>
#include <vector>

class QMenu;
class QMouseEvent;
class QPaintEvent;
class QToolButton;
class ScalarPager;

// Round times for an axis from first to last: multiples of 1, 2, 2.5 or 5 times a power of
// ten, the finest step that gives at most maxCount of them inside [first, last]. Empty
// when last is not past first.
std::vector<double> timeTicks(double first, double last, int maxCount);

// The playback bar's timeline: a header with a label at the left and the frame's time and
// number at the right, then a track filled up to the playhead with a knob on it, a playhead
// line through a trace above the track, and time ticks under it. Its value is the frame:
// a press or drag below the header moves to the frame nearest the pointer, and the slider
// keys step through frames. Frames sit along a time axis when their times increase,
// otherwise evenly. In a narrow bar the ticks go first, then the frame number.
class PlaybackTimeline : public QAbstractSlider {
  Q_OBJECT
  friend class UiTests; // reads positions to drive the timeline with simulated input

public:
  explicit PlaybackTimeline(std::vector<double> times, QWidget* parent = nullptr);

  void setLabel(const QString& label);
  void setTrace(std::vector<std::optional<double>> values, double low, double high);

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

private:
  struct Header {
    QString label;
    QString time;
    QString frame; // empty when the timeline is too narrow for it
  };
  Header header() const;
  bool ticksShown() const;
  QRectF trackRect() const;
  QRectF traceRect() const;
  double xFor(double axisValue) const;
  double frameX(int frame) const;
  int frameAt(double x) const;

  std::vector<double> times_;
  std::vector<double> axis_; // each frame's place: its time, or its index
  bool timeAxis_ = false;
  QString label_;
  std::vector<std::optional<double>> trace_;
  double traceLow_ = 0.0;
  double traceHigh_ = 1.0;
  int frameWidth_ = 0; // of the widest frame number, so the header never shifts
  int timeWidth_ = 0;  // of the widest time
};

// Bottom bar for a time series, laid out as a transport: the field switcher, previous
// frame, play or pause and next frame, the timeline, then the speed menu and Loop.
//
// The widget is intent-only: it emits what the user asked for and reflects state
// pushed back via setStep()/setPlaying(). The owner drives the actual frame timer and
// connects the switcher's own signals.
class PlaybackBar : public QWidget {
  Q_OBJECT
public:
  // times holds each frame's time, as the series gives it.
  explicit PlaybackBar(std::vector<double> times, QWidget* parent = nullptr);

  // At the bar's fixed height, which its layout's hint falls short of.
  QSize sizeHint() const override;

  // The field switcher at the left; hidePager drops it when there is no field.
  ScalarPager* pager() const {
    return pager_;
  }
  void hidePager();

  int currentStep() const;
  double speedMultiplier() const;
  bool loopEnabled() const;
  bool isPlaying() const {
    return playing_;
  }

  // Reflect externally driven state without re-emitting signals.
  void setStep(int step);
  void setPlaying(bool playing);
  // Above the track at the left, e.g. the field the timeline traces.
  void setTraceLabel(const QString& label);
  // A value per frame drawn above the track from low (bottom) to high (top), in the accent
  // up to the playhead and muted after it. An unread frame (std::nullopt) is passed over,
  // a NaN breaks the line; no values clear it.
  void setTrace(std::vector<std::optional<double>> values, double low, double high);

signals:
  void playToggled(bool playing);
  void stepRequested(int step);
  void speedChanged(double multiplier);
  void loopToggled(bool loop);

private:
  void stepBy(int delta);
  void showLoop(bool on);

  int numSteps_ = 0;
  bool playing_ = false;
  double speed_ = 1.0;

  ScalarPager* pager_ = nullptr;
  QWidget* switcher_ = nullptr;
  QToolButton* prevButton_ = nullptr;
  QToolButton* playButton_ = nullptr;
  QToolButton* nextButton_ = nullptr;
  PlaybackTimeline* timeline_ = nullptr;
  QToolButton* speedButton_ = nullptr;
  QMenu* speedMenu_ = nullptr;
  QToolButton* loopButton_ = nullptr;
};

// The bar's place in a view of the given size: along the bottom, margin from the bottom
// and side edges.
QRect playbackBarGeometry(const QSize& view, const PlaybackBar& bar, int margin);

// The smallest view that holds the bar with margin all around it.
QSize playbackBarViewMinimum(const PlaybackBar& bar, int margin);
