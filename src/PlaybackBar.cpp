#include "PlaybackBar.h"

#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRectF>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QString>
#include <QToolButton>
#include <algorithm>

namespace {

// Icon geometry, in a 24×24 logical box. Icons are painted as vector paths so
// they stay crisp at any DPI and never depend on a system font shipping the
// Unicode media glyphs (which renders as tofu on minimal Linux/Windows images).
constexpr int kIconBox = 24;
const QColor kIconColor(232, 232, 232); // matches the bar's text color

enum class Glyph { Play, Pause, Loop };

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
                "QComboBox {"
                "  background: rgba(255,255,255,18); color: #E8E8E8;"
                "  border: none; border-radius: 4px; padding: 2px 6px;"
                "}"
                "QComboBox QAbstractItemView { background: #202020; color: #E8E8E8; "
                "selection-background-color: #5096FA; }"
                "QSlider::groove:horizontal { height: 4px; background: rgba(255,255,255,50); "
                "border-radius: 2px; }"
                "QSlider::handle:horizontal {"
                "  width: 12px; margin: -5px 0; border-radius: 6px; background: #E8E8E8;"
                "}"
                "QSlider::sub-page:horizontal { background: #5096FA; border-radius: 2px; }");

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(10, 6, 10, 6);
  row->setSpacing(8);

  playButton_ = new QToolButton(this);
  playButton_->setIcon(makeGlyphIcon(Glyph::Play));
  playButton_->setIconSize(QSize(18, 18));
  playButton_->setToolTip("Play/Pause");
  playButton_->setFocusPolicy(Qt::NoFocus);
  row->addWidget(playButton_);

  slider_ = new QSlider(Qt::Horizontal, this);
  slider_->setMinimum(0);
  slider_->setMaximum(numSteps_ - 1);
  slider_->setSingleStep(1);
  slider_->setPageStep(std::max(1, numSteps_ / 20));
  slider_->setFocusPolicy(Qt::NoFocus);
  row->addWidget(slider_, 1);

  readout_ = new QLabel(this);
  readout_->setMinimumWidth(140);
  readout_->setAlignment(Qt::AlignCenter);
  row->addWidget(readout_);

  speedBox_ = new QComboBox(this);
  speedBox_->setFocusPolicy(Qt::NoFocus);
  for (const char* label : {"0.25x", "0.5x", "1x", "2x", "4x", "8x"}) {
    speedBox_->addItem(QString::fromLatin1(label));
  }
  speedBox_->setCurrentIndex(2); // 1x
  row->addWidget(speedBox_);

  loopButton_ = new QToolButton(this);
  loopButton_->setIcon(makeGlyphIcon(Glyph::Loop));
  loopButton_->setIconSize(QSize(18, 18));
  loopButton_->setToolTip("Loop");
  loopButton_->setCheckable(true);
  loopButton_->setChecked(true);
  loopButton_->setFocusPolicy(Qt::NoFocus);
  row->addWidget(loopButton_);

  updateReadout(0, 0.0);

  connect(playButton_, &QToolButton::clicked, this, [this]() {
    setPlaying(!playing_);
    emit playToggled(playing_);
  });
  connect(slider_, &QSlider::valueChanged, this, [this](int value) { emit stepRequested(value); });
  connect(speedBox_, &QComboBox::currentTextChanged, this, [this]() {
    emit speedChanged(speedMultiplier());
  });
  connect(loopButton_, &QToolButton::toggled, this, [this](bool on) { emit loopToggled(on); });
}

int PlaybackBar::currentStep() const {
  return slider_->value();
}

double PlaybackBar::speedMultiplier() const {
  switch (speedBox_->currentIndex()) {
  case 0:
    return 0.25;
  case 1:
    return 0.5;
  case 2:
    return 1.0;
  case 3:
    return 2.0;
  case 4:
    return 4.0;
  case 5:
    return 8.0;
  default:
    return 1.0;
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
