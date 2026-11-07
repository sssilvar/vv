#include "PlaybackBar.h"

#include "ColorBarWidget.h"
#include "LucideIcon.h"
#include "OverlayStyle.h"
#include "ScalarPager.h"

#include <QAction>
#include <QActionGroup>
#include <QFont>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QString>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace {

constexpr int kBarHeight = 96;
constexpr int kBarRadius = 16;
constexpr int kStepButtonSize = 36;
constexpr int kStepIconSize = 20;
constexpr int kPlaySize = 46;
constexpr int kPlayIconSize = 18;
constexpr int kLoopSize = 36;
constexpr int kLoopIconSize = 18;
constexpr int kDividerHeight = 44;

// The timeline, top to bottom: the header, the area the playhead line crosses (where a
// probe trace goes), the track with its knob, then the tick labels.
constexpr int kHeaderHeight = 18;
constexpr int kTraceTop = 22;
constexpr int kTraceBottom = 48;
constexpr double kTraceWidth = 1.5;
constexpr int kTrackY = 57;
constexpr double kTrackHeight = 4.0;
constexpr double kKnobRadius = 7.0;
constexpr int kTickTop = 67;
constexpr int kTickHeight = 12;
constexpr int kTimelineHeight = 80;
constexpr int kTimelineMinWidth = 72;
constexpr int kHeaderGap = 8;
constexpr int kLabelMinWidth = 32;
// Narrower than this, the tick labels would crowd each other, so they go first.
constexpr int kTicksMinWidth = 200;
// At most one tick label per this many pixels of track.
constexpr int kTickSpacing = 60;

const QColor kTrackColor(255, 255, 255, 24);
const QColor kPlayheadColor(237, 237, 240, 170);
const QColor kTraceAhead(255, 255, 255, 70);
const QColor kPlayGlyph(24, 24, 27);
const QColor kLoopTint(80, 150, 250, 48);

QFont pixelFont(QFont font, int pixels, QFont::Weight weight) {
  font.setPixelSize(pixels);
  font.setWeight(weight);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
  // Tabular digits keep times and frame numbers from jiggling while they count.
  font.setFeature(QFont::Tag("tnum"), 1);
#endif
  return font;
}

QString frameText(int step, int count) {
  return QStringLiteral("frame %1 of %2").arg(step + 1).arg(count);
}

QString tickText(double value) {
  // Ten digits print round ticks such as 0.3 without the binary residue of 3 * 0.1.
  return QString::number(value, 'g', 10);
}

QString playLabel(bool playing) {
  return playing ? QStringLiteral("Pause") : QStringLiteral("Play");
}

QString withKey(const QString& name, const QKeySequence& key) {
  return QStringLiteral("%1 (%2)").arg(name, key.toString(QKeySequence::NativeText));
}

QWidget* divider(QWidget* parent) {
  auto* line = new QWidget(parent);
  line->setObjectName("playbackDivider");
  line->setAttribute(Qt::WA_StyledBackground, true);
  line->setFixedSize(1, kDividerHeight);
  return line;
}

} // namespace

std::vector<double> timeTicks(double first, double last, int maxCount) {
  std::vector<double> ticks;
  if (!std::isfinite(first) || !std::isfinite(last) || !(last > first) || maxCount < 1) {
    return ticks;
  }
  // A tenth of the average spacing is finer than any step can be, so the search starts there.
  for (auto exponent = static_cast<int>(std::floor(std::log10((last - first) / maxCount))) - 1;;
       ++exponent) {
    for (const double mantissa : {1.0, 2.0, 2.5, 5.0}) {
      const double step = mantissa * std::pow(10.0, exponent);
      // The tolerance keeps an end that is a multiple of step, up to rounding, inside.
      const double begin = std::ceil(first / step - 1e-9);
      const double end = std::floor(last / step + 1e-9);
      if (end - begin + 1.0 > maxCount) {
        continue;
      }
      const auto count = static_cast<long long>(end - begin) + 1;
      for (long long index = 0; index < count; ++index) {
        // + 0.0 turns -0 into 0, so the first tick never prints as "-0".
        ticks.push_back((begin + static_cast<double>(index)) * step + 0.0);
      }
      return ticks;
    }
  }
}

PlaybackTimeline::PlaybackTimeline(std::vector<double> times, QWidget* parent)
    : QAbstractSlider(parent), times_(std::move(times)) {
  if (times_.empty()) {
    times_.push_back(0.0);
  }
  const int count = static_cast<int>(times_.size());
  timeAxis_ =
      count > 1 && times_.back() > times_.front() && std::is_sorted(times_.begin(), times_.end());
  axis_.reserve(times_.size());
  for (size_t frame = 0; frame < times_.size(); ++frame) {
    axis_.push_back(timeAxis_ ? times_[frame] : static_cast<double>(frame));
  }
  setOrientation(Qt::Horizontal);
  setRange(0, count - 1);
  setSingleStep(1);
  setPageStep(std::max(1, count / 20));
  setAccessibleName(QStringLiteral("Frame"));
  // Tab reaches it for the slider keys; a click leaves the focus on the view, so Space
  // keeps playing and pausing.
  setFocusPolicy(Qt::TabFocus);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  const QFontMetricsF frames(pixelFont(font(), 12, QFont::Normal));
  frameWidth_ = static_cast<int>(std::ceil(frames.horizontalAdvance(frameText(count - 1, count))));
  const QFontMetricsF strong(pixelFont(font(), 13, QFont::DemiBold));
  double widest = 0.0;
  for (const double time : times_) {
    widest = std::max(widest, strong.horizontalAdvance(overlay::formatValue(time)));
  }
  timeWidth_ = static_cast<int>(std::ceil(widest));
}

void PlaybackTimeline::setLabel(const QString& label) {
  label_ = label;
  update();
}

void PlaybackTimeline::setTrace(std::vector<std::optional<double>> values,
                                double low,
                                double high) {
  trace_ = std::move(values);
  traceLow_ = low;
  traceHigh_ = high;
  update();
}

QSize PlaybackTimeline::sizeHint() const {
  return {360, kTimelineHeight};
}

QSize PlaybackTimeline::minimumSizeHint() const {
  return {kTimelineMinWidth, kTimelineHeight};
}

PlaybackTimeline::Header PlaybackTimeline::header() const {
  const int step = value();
  Header shown;
  shown.time = overlay::formatValue(times_[static_cast<size_t>(step)]);
  // Measured with the widest time, so playing never makes the frame number come and go.
  const double inner = width() - 2.0 * kKnobRadius;
  const bool frameFits =
      inner >= kLabelMinWidth + kHeaderGap + timeWidth_ + kHeaderGap + frameWidth_;
  if (frameFits) {
    shown.frame = frameText(step, static_cast<int>(times_.size()));
  }
  const double labelWidth =
      inner - timeWidth_ - kHeaderGap - (frameFits ? kHeaderGap + frameWidth_ : 0);
  const QFontMetricsF plain(pixelFont(font(), 12, QFont::Normal));
  shown.label = plain.elidedText(label_, Qt::ElideRight, std::max(0.0, labelWidth));
  return shown;
}

bool PlaybackTimeline::ticksShown() const {
  return timeAxis_ && width() >= kTicksMinWidth;
}

QRectF PlaybackTimeline::trackRect() const {
  return {kKnobRadius,
          kTrackY - kTrackHeight / 2.0,
          std::max(1.0, width() - 2.0 * kKnobRadius),
          kTrackHeight};
}

QRectF PlaybackTimeline::traceRect() const {
  const QRectF track = trackRect();
  return {track.left(), static_cast<double>(kTraceTop), track.width(), kTraceBottom - kTraceTop};
}

double PlaybackTimeline::xFor(double axisValue) const {
  const QRectF track = trackRect();
  const double span = axis_.back() - axis_.front();
  return span > 0.0 ? track.left() + (axisValue - axis_.front()) / span * track.width()
                    : track.left();
}

double PlaybackTimeline::frameX(int frame) const {
  return xFor(axis_[static_cast<size_t>(std::clamp(frame, 0, maximum()))]);
}

int PlaybackTimeline::frameAt(double x) const {
  const QRectF track = trackRect();
  const double along = std::clamp((x - track.left()) / track.width(), 0.0, 1.0);
  const double target = axis_.front() + along * (axis_.back() - axis_.front());
  const auto after = std::lower_bound(axis_.begin(), axis_.end(), target);
  if (after == axis_.begin()) {
    return 0;
  }
  if (after == axis_.end()) {
    return maximum();
  }
  const auto index = static_cast<int>(std::distance(axis_.begin(), after));
  const double previous = axis_[static_cast<size_t>(index - 1)];
  return target - previous <= *after - target ? index - 1 : index;
}

void PlaybackTimeline::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const Header shown = header();
  const QFont plain = pixelFont(font(), 12, QFont::Normal);
  const QFont strong = pixelFont(font(), 13, QFont::DemiBold);
  const double right = width() - kKnobRadius;
  double timeRight = right;
  if (!shown.frame.isEmpty()) {
    painter.setFont(plain);
    painter.setPen(overlay::kTextSecondary);
    painter.drawText(QRectF(right - frameWidth_, 0, frameWidth_, kHeaderHeight),
                     Qt::AlignRight | Qt::AlignVCenter,
                     shown.frame);
    timeRight = right - frameWidth_ - kHeaderGap;
  }
  painter.setFont(strong);
  painter.setPen(overlay::kText);
  painter.drawText(QRectF(kKnobRadius, 0, timeRight - kKnobRadius, kHeaderHeight),
                   Qt::AlignRight | Qt::AlignVCenter,
                   shown.time);
  painter.setFont(plain);
  painter.setPen(overlay::kTextSecondary);
  painter.drawText(QRectF(kKnobRadius, 0, timeRight - kKnobRadius, kHeaderHeight),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   shown.label);

  const QRectF track = trackRect();
  const double x = frameX(value());
  if (!trace_.empty() && traceHigh_ > traceLow_) {
    const QRectF area = traceRect();
    QPainterPath path;
    bool drawing = false;
    for (size_t frame = 0; frame < trace_.size() && frame < axis_.size(); ++frame) {
      const std::optional<double>& sample = trace_[frame];
      if (!sample) {
        continue;
      }
      if (!std::isfinite(*sample)) {
        drawing = false;
        continue;
      }
      const QPointF point(xFor(axis_[frame]),
                          area.bottom() -
                              (*sample - traceLow_) / (traceHigh_ - traceLow_) * area.height());
      if (drawing) {
        path.lineTo(point);
      } else {
        path.moveTo(point);
      }
      drawing = true;
    }
    // Values past the range leave the area rather than flatten along its edge.
    const double top = area.top() - kTraceWidth;
    const double bandHeight = area.height() + 2.0 * kTraceWidth;
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setClipRect(QRectF(0.0, top, x, bandHeight));
    painter.setPen(QPen(overlay::kAccentBright, kTraceWidth, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(path);
    painter.setClipRect(QRectF(x, top, width() - x, bandHeight));
    painter.setPen(QPen(kTraceAhead, kTraceWidth, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(path);
    painter.restore();
  }
  painter.setPen(Qt::NoPen);
  painter.setBrush(kTrackColor);
  painter.drawRoundedRect(track, kTrackHeight / 2.0, kTrackHeight / 2.0);
  painter.setBrush(overlay::kAccent);
  painter.drawRoundedRect(QRectF(track.left(), track.top(), x - track.left(), track.height()),
                          kTrackHeight / 2.0,
                          kTrackHeight / 2.0);

  // On whole logical pixels, so the 1 px line stays sharp.
  painter.setPen(QPen(kPlayheadColor, 1.0));
  painter.drawLine(QPointF(std::round(x), kTraceTop), QPointF(std::round(x), kTrackY));

  if (hasFocus()) {
    painter.setPen(QPen(overlay::kAccent, 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(x, kTrackY), kKnobRadius + 3.0, kKnobRadius + 3.0);
  }
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::white);
  painter.drawEllipse(QPointF(x, kTrackY), kKnobRadius, kKnobRadius);

  if (!ticksShown()) {
    return;
  }
  const QFont tickFont = pixelFont(font(), 10, QFont::Normal);
  const QFontMetricsF metrics(tickFont);
  painter.setFont(tickFont);
  painter.setPen(overlay::kTextSecondary);
  const int maxCount = std::clamp(static_cast<int>(track.width() / kTickSpacing), 2, 6);
  double previousRight = -1e9;
  for (const double tick : timeTicks(axis_.front(), axis_.back(), maxCount)) {
    const QString text = tickText(tick);
    const double textWidth = metrics.horizontalAdvance(text);
    // Centered under its time, kept inside the timeline; one that would touch the
    // previous label is left out.
    const double left = std::clamp(xFor(tick) - textWidth / 2.0, 0.0, width() - textWidth);
    if (left < previousRight + 6.0) {
      continue;
    }
    painter.drawText(QRectF(left, kTickTop, textWidth, kTickHeight), Qt::AlignCenter, text);
    previousRight = left + textWidth;
  }
}

void PlaybackTimeline::mousePressEvent(QMouseEvent* event) {
  // The header is text, not a place to seek to.
  if (event->button() != Qt::LeftButton || mouseLocalPos(event).y() < kTraceTop) {
    event->ignore();
    return;
  }
  setSliderDown(true);
  setSliderPosition(frameAt(mouseLocalPos(event).x()));
  event->accept();
}

void PlaybackTimeline::mouseMoveEvent(QMouseEvent* event) {
  if (!isSliderDown()) {
    event->ignore();
    return;
  }
  setSliderPosition(frameAt(mouseLocalPos(event).x()));
}

void PlaybackTimeline::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !isSliderDown()) {
    event->ignore();
    return;
  }
  setSliderPosition(frameAt(mouseLocalPos(event).x()));
  setSliderDown(false);
}

PlaybackBar::PlaybackBar(std::vector<double> times, QWidget* parent) : QWidget(parent) {
  if (times.empty()) {
    times.push_back(0.0);
  }
  numSteps_ = static_cast<int>(times.size());
  setObjectName("playbackBar");
  setAttribute(Qt::WA_StyledBackground, true);
  // A press on the bar's padding must not reach the viewport and start a rotation.
  setAttribute(Qt::WA_NoMousePropagation, true);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::ArrowCursor);
  setFixedHeight(kBarHeight);
  setStyleSheet(
      QStringLiteral(
          "QWidget#playbackBar { background: %1; border: 1px solid %2; border-radius: %3px; }"
          "QWidget#playbackDivider { background: %2; }"
          "QToolButton { background: transparent; border: none; }"
          "QToolButton#playbackStep { border-radius: %4px; }"
          "QToolButton#playbackStep:hover, QToolButton#playbackLoop:hover,"
          "  QToolButton#playbackSpeed:hover { background: %5; }"
          "QToolButton#playbackStep:pressed, QToolButton#playbackSpeed:pressed { background: %6; }"
          "QToolButton#playbackPlay { background: %7; border-radius: %8px; }"
          "QToolButton#playbackPlay:hover { background: #FFFFFF; }"
          "QToolButton#playbackPlay:pressed { background: #D4D4D8; }"
          "QToolButton#playbackSpeed { color: %7; font-size: 13px; font-weight: 600;"
          "  border: 1px solid %9; border-radius: 10px; padding: 0 8px; }"
          "QToolButton#playbackLoop { border-radius: 10px; }"
          "QToolButton#playbackLoop:checked { background: %10; }")
          .arg(overlay::css(overlay::kSurface),
               overlay::css(overlay::solidBorder(overlay::kSurfaceBorder)),
               QString::number(kBarRadius),
               QString::number(kStepButtonSize / 2),
               overlay::css(overlay::kHover),
               overlay::css(overlay::kSelected),
               overlay::css(overlay::kText),
               QString::number(kPlaySize / 2),
               overlay::css(overlay::solidBorder(overlay::kSelectedBorder)),
               overlay::css(kLoopTint)));

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(6, 0, 12, 0);
  row->setSpacing(0);

  // The field switcher and its divider hide together.
  switcher_ = new QWidget(this);
  auto* switcherRow = new QHBoxLayout(switcher_);
  switcherRow->setContentsMargins(0, 0, 0, 0);
  switcherRow->setSpacing(0);
  pager_ = new ScalarPager(PagerPlacement::PlaybackBar, switcher_);
  switcherRow->addWidget(pager_);
  switcherRow->addSpacing(10);
  switcherRow->addWidget(divider(switcher_));
  switcherRow->addSpacing(12);
  row->addWidget(switcher_);

  const auto makeStep = [this, row](Lucide icon, const QString& name, Qt::Key key) {
    auto* button = new QToolButton(this);
    button->setObjectName("playbackStep");
    button->setIcon(lucideIcon(icon, kStepIconSize));
    button->setIconSize(QSize(kStepIconSize, kStepIconSize));
    button->setFixedSize(kStepButtonSize, kStepButtonSize);
    button->setAccessibleName(name);
    button->setToolTip(withKey(name, QKeySequence(key)));
    // Keeps focus on the viewport, so Space keeps playing and pausing after a click.
    button->setFocusPolicy(Qt::NoFocus);
    row->addWidget(button);
    return button;
  };

  prevButton_ = makeStep(Lucide::ChevronFirst, QStringLiteral("Previous frame"), Qt::Key_Left);
  row->addSpacing(4);
  playButton_ = new QToolButton(this);
  playButton_->setObjectName("playbackPlay");
  playButton_->setIconSize(QSize(kPlayIconSize, kPlayIconSize));
  playButton_->setFixedSize(kPlaySize, kPlaySize);
  playButton_->setFocusPolicy(Qt::NoFocus);
  row->addWidget(playButton_);
  row->addSpacing(4);
  nextButton_ = makeStep(Lucide::ChevronLast, QStringLiteral("Next frame"), Qt::Key_Right);
  row->addSpacing(18);

  timeline_ = new PlaybackTimeline(std::move(times), this);
  row->addWidget(timeline_, 1);
  row->addSpacing(18);
  row->addWidget(divider(this));
  row->addSpacing(14);

  speedButton_ = new QToolButton(this);
  speedButton_->setObjectName("playbackSpeed");
  speedButton_->setText(QStringLiteral("1×"));
  speedButton_->setAccessibleName(QStringLiteral("Playback speed"));
  speedButton_->setToolTip(QStringLiteral("Playback speed"));
  speedButton_->setFixedHeight(kLoopSize);
  speedButton_->setMinimumWidth(kLoopSize + 6);
  speedButton_->setFocusPolicy(Qt::NoFocus);
  row->addWidget(speedButton_);
  row->addSpacing(6);

  speedMenu_ = new QMenu(speedButton_);
  overlay::styleMenu(speedMenu_);
  auto* speedGroup = new QActionGroup(speedMenu_);
  for (const double multiplier : {1.0, 2.0, 4.0, 8.0, 12.0}) {
    const QString label = QStringLiteral("%1×").arg(multiplier);
    QAction* action = speedMenu_->addAction(label);
    action->setCheckable(true);
    action->setChecked(multiplier == speed_);
    speedGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, multiplier, label]() {
      speed_ = multiplier;
      speedButton_->setText(label);
      emit speedChanged(speed_);
    });
  }
  // Drop *up*: the bar sits at the bottom of the viewport, so a menu opened
  // downwards would fall outside the window.
  // speedGroup is owned by speedMenu_ (Qt parent), which the analyzer doesn't model.
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  connect(speedButton_, &QToolButton::clicked, this, [this]() {
    const QSize hint = speedMenu_->sizeHint();
    speedMenu_->popup(speedButton_->mapToGlobal(QPoint(0, -hint.height() - 4)));
  });

  loopButton_ = new QToolButton(this);
  loopButton_->setObjectName("playbackLoop");
  loopButton_->setCheckable(true);
  loopButton_->setChecked(true);
  loopButton_->setIconSize(QSize(kLoopIconSize, kLoopIconSize));
  loopButton_->setFixedSize(kLoopSize, kLoopSize);
  loopButton_->setAccessibleName(QStringLiteral("Loop"));
  loopButton_->setToolTip(QStringLiteral("Loop"));
  loopButton_->setFocusPolicy(Qt::NoFocus);
  row->addWidget(loopButton_);
  showLoop(true);

  setPlaying(false);

  connect(playButton_, &QToolButton::clicked, this, [this]() {
    setPlaying(!playing_);
    emit playToggled(playing_);
  });
  connect(prevButton_, &QToolButton::clicked, this, [this]() { stepBy(-1); });
  connect(nextButton_, &QToolButton::clicked, this, [this]() { stepBy(1); });
  connect(timeline_, &QAbstractSlider::valueChanged, this, [this](int value) {
    emit stepRequested(value);
  });
  connect(loopButton_, &QToolButton::toggled, this, [this](bool on) {
    showLoop(on);
    emit loopToggled(on);
  });
}

QSize PlaybackBar::sizeHint() const {
  return {QWidget::sizeHint().width(), kBarHeight};
}

void PlaybackBar::hidePager() {
  switcher_->hide();
}

int PlaybackBar::currentStep() const {
  return timeline_->value();
}

double PlaybackBar::speedMultiplier() const {
  return speed_;
}

void PlaybackBar::stepBy(int delta) {
  const int target = std::clamp(currentStep() + delta, 0, numSteps_ - 1);
  if (target != currentStep()) {
    emit stepRequested(target);
  }
}

bool PlaybackBar::loopEnabled() const {
  return loopButton_->isChecked();
}

void PlaybackBar::setStep(int step) {
  const QSignalBlocker block(timeline_);
  timeline_->setValue(step);
}

void PlaybackBar::setPlaying(bool playing) {
  playing_ = playing;
  playButton_->setIcon(
      lucideSolidIcon(playing ? Lucide::Pause : Lucide::Play, kPlayIconSize, kPlayGlyph));
  playButton_->setAccessibleName(playLabel(playing));
  playButton_->setToolTip(withKey(playLabel(playing), QKeySequence(Qt::Key_Space)));
}

void PlaybackBar::setTraceLabel(const QString& label) {
  timeline_->setLabel(label);
}

void PlaybackBar::setTrace(std::vector<std::optional<double>> values, double low, double high) {
  timeline_->setTrace(std::move(values), low, high);
}

void PlaybackBar::showLoop(bool on) {
  loopButton_->setIcon(
      lucideIcon(Lucide::Repeat, kLoopIconSize, on ? overlay::kAccentBright : overlay::kIcon));
}

QRect playbackBarGeometry(const QSize& view, const PlaybackBar& bar, int margin) {
  const int height = bar.sizeHint().height();
  const int width = std::max(bar.minimumSizeHint().width(), view.width() - 2 * margin);
  const int y = std::max(margin, view.height() - height - margin);
  return {margin, y, width, height};
}

QSize playbackBarViewMinimum(const PlaybackBar& bar, int margin) {
  return {bar.minimumSizeHint().width() + 2 * margin, bar.sizeHint().height() + 2 * margin};
}
