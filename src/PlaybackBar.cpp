#include "PlaybackBar.h"

#include <QAction>
#include <QActionGroup>
#include <QColor>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRectF>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QString>
#include <QToolButton>
#include <QTransform>
#include <algorithm>

namespace {

// Icon geometry, in a 24×24 logical box. Icons are painted as vector paths so
// they stay crisp at any DPI and never depend on a system font shipping the
// Unicode media glyphs (which renders as tofu on minimal Linux/Windows images).
constexpr int kIconBox = 24;
const QColor kIconColor(232, 232, 232); // matches the bar's text color

enum class Glyph { Play, Pause, Prev, Next, Loop };

QPainterPath glyphPath(Glyph glyph) {
  QPainterPath path;
  switch (glyph) {
  case Glyph::Play: {
    path.moveTo(8.0, 6.0);
    path.lineTo(18.0, 12.0);
    path.lineTo(8.0, 18.0);
    path.closeSubpath();
    break;
  }
  case Glyph::Pause: {
    path.addRoundedRect(QRectF(7.5, 6.0, 3.5, 12.0), 1.2, 1.2);
    path.addRoundedRect(QRectF(13.0, 6.0, 3.5, 12.0), 1.2, 1.2);
    break;
  }
  case Glyph::Prev:
  case Glyph::Next: {
    // Triangle against a bar, mirrored for Prev.
    QPainterPath shape;
    shape.moveTo(17.5, 6.0);
    shape.lineTo(17.5, 18.0);
    shape.lineTo(8.5, 12.0);
    shape.closeSubpath();
    shape.addRoundedRect(QRectF(5.5, 6.0, 2.6, 12.0), 1.0, 1.0);
    path = glyph == Glyph::Next ? QTransform().translate(24.0, 0.0).scale(-1.0, 1.0).map(shape)
                                : shape;
    break;
  }
  case Glyph::Loop: {
    // Circular arrow: an open ring with an arrowhead at the top opening.
    QPainterPath ring;
    ring.addEllipse(QRectF(5.5, 5.5, 13.0, 13.0));
    QPainterPath stroked;
    {
      QPainterPathStroker stroker;
      stroker.setWidth(2.4);
      stroker.setCapStyle(Qt::FlatCap);
      stroked = stroker.createStroke(ring);
    }
    // Cut a gap at the top so the ring reads as a refresh arrow.
    QPainterPath gap;
    gap.addRect(QRectF(11.0, 2.0, 5.0, 6.0));
    path = stroked.subtracted(gap);
    // Arrowhead pointing clockwise into the gap.
    QPainterPath head;
    head.moveTo(16.2, 3.2);
    head.lineTo(16.2, 8.2);
    head.lineTo(11.6, 5.7);
    head.closeSubpath();
    path = path.united(head);
    break;
  }
  }
  return path;
}

QIcon makeGlyphIcon(Glyph glyph) {
  QIcon icon;
  for (const int scale : {1, 2}) {
    const int px = kIconBox * scale;
    QPixmap pix(px, px);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(scale, scale);
    painter.fillPath(glyphPath(glyph), kIconColor);
    painter.end();
    icon.addPixmap(pix);
  }
  return icon;
}

} // namespace

PlaybackBar::PlaybackBar(int numSteps, QWidget* parent)
    : QWidget(parent), numSteps_(numSteps > 0 ? numSteps : 1) {
  setObjectName("playbackBar");
  setAttribute(Qt::WA_StyledBackground, true);
  setFocusPolicy(Qt::NoFocus);
  setStyleSheet("QWidget#playbackBar {"
                "  background: rgba(20,20,20,200);"
                "  border-radius: 8px;"
                "}"
                "QToolButton {"
                "  background: rgba(255,255,255,18);"
                "  color: #E8E8E8;"
                "  border: none;"
                "  border-radius: 4px;"
                "  padding: 2px 8px;"
                "  font-size: 14px;"
                "}"
                "QToolButton:hover { background: rgba(255,255,255,40); }"
                "QToolButton:checked { background: rgba(80,150,250,160); color: white; }"
                "QLabel { color: #D8D8D8; font-size: 12px; }"
                "QMenu { background: #202020; color: #E8E8E8; border: 1px solid #3A3A3A; }"
                "QMenu::item:selected { background: #5096FA; }"
                "QSlider::groove:horizontal { height: 4px; background: rgba(255,255,255,50); "
                "border-radius: 2px; }"
                "QSlider::handle:horizontal {"
                "  width: 12px; margin: -5px 0; border-radius: 6px; background: #E8E8E8;"
                "}"
                "QSlider::sub-page:horizontal { background: #5096FA; border-radius: 2px; }");

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(10, 6, 10, 6);
  row->setSpacing(8);

  auto makeButton = [this, row](Glyph glyph, const char* tip) {
    auto* button = new QToolButton(this);
    button->setIcon(makeGlyphIcon(glyph));
    button->setIconSize(QSize(18, 18));
    button->setToolTip(QString::fromLatin1(tip));
    button->setFocusPolicy(Qt::NoFocus);
    row->addWidget(button);
    return button;
  };

  prevButton_ = makeButton(Glyph::Prev, "Previous frame");
  playButton_ = makeButton(Glyph::Play, "Play/Pause");
  nextButton_ = makeButton(Glyph::Next, "Next frame");

  slider_ = new QSlider(Qt::Horizontal, this);
  slider_->setMinimum(0);
  slider_->setMaximum(numSteps_ - 1);
  slider_->setSingleStep(1);
  slider_->setPageStep(std::max(1, numSteps_ / 20));
  slider_->setFocusPolicy(Qt::NoFocus);
  row->addWidget(slider_, 1);

  readout_ = new QLabel(this);
  readout_->setMinimumWidth(150);
  readout_->setAlignment(Qt::AlignCenter);
  row->addWidget(readout_);

  speedButton_ = new QToolButton(this);
  speedButton_->setText(QStringLiteral("1x"));
  speedButton_->setToolTip(QStringLiteral("Playback speed"));
  speedButton_->setFocusPolicy(Qt::NoFocus);
  row->addWidget(speedButton_);

  auto* speedMenu = new QMenu(speedButton_);
  auto* speedGroup = new QActionGroup(speedMenu);
  for (const double multiplier : {1.0, 2.0, 4.0, 8.0, 12.0}) {
    const QString label = QStringLiteral("%1x").arg(multiplier);
    QAction* action = speedMenu->addAction(label);
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
  connect(speedButton_, &QToolButton::clicked, this, [this, speedMenu]() {
    const QSize hint = speedMenu->sizeHint();
    speedMenu->popup(speedButton_->mapToGlobal(QPoint(0, -hint.height())));
  });

  loopButton_ = makeButton(Glyph::Loop, "Loop");
  loopButton_->setCheckable(true);
  loopButton_->setChecked(true);

  updateReadout(0, 0.0);

  connect(playButton_, &QToolButton::clicked, this, [this]() {
    setPlaying(!playing_);
    emit playToggled(playing_);
  });
  connect(prevButton_, &QToolButton::clicked, this, [this]() { stepBy(-1); });
  connect(nextButton_, &QToolButton::clicked, this, [this]() { stepBy(1); });
  connect(slider_, &QSlider::valueChanged, this, [this](int value) { emit stepRequested(value); });
  connect(loopButton_, &QToolButton::toggled, this, [this](bool on) { emit loopToggled(on); });
}

int PlaybackBar::currentStep() const {
  return slider_->value();
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

void PlaybackBar::setStep(int step, double timeValue) {
  const QSignalBlocker block(slider_);
  slider_->setValue(step);
  updateReadout(step, timeValue);
}

void PlaybackBar::setPlaying(bool playing) {
  playing_ = playing;
  playButton_->setIcon(makeGlyphIcon(playing ? Glyph::Pause : Glyph::Play));
}

void PlaybackBar::updateReadout(int step, double timeValue) {
  readout_->setText(
      QStringLiteral("%1 / %2   t=%3").arg(step + 1).arg(numSteps_).arg(timeValue, 0, 'g', 4));
}
