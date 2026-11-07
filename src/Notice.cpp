#include "Notice.h"

#include "OverlayStyle.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QTimer>
#include <QVariant>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>

namespace {

constexpr double kPadX = 14.0;
constexpr double kPadY = 8.0;
constexpr double kRadius = 10.0;
constexpr double kMark = 6.0; // the dot before an error's text
constexpr double kMarkGap = 8.0;
// Qt reports no reduced-motion preference on every platform, so the fades stay short.
constexpr std::chrono::milliseconds kFadeIn{120};
constexpr std::chrono::milliseconds kFadeOut{200};

} // namespace

Notice::Notice(QWidget* parent) : QWidget(parent) {
  setObjectName("notice");
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setFocusPolicy(Qt::NoFocus);
  setVisible(false);

  fade_ = new QVariantAnimation(this);
  connect(fade_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
    opacity_ = value.toDouble();
    update();
  });
  connect(fade_, &QVariantAnimation::finished, this, [this]() {
    if (opacity_ <= 0.0) {
      hide();
    }
  });

  hold_ = new QTimer(this);
  hold_->setSingleShot(true);
  connect(hold_, &QTimer::timeout, this, [this]() { fadeTo(0.0, kFadeOut); });
}

void Notice::post(const QString& text, NoticeKind kind) {
  text_ = text;
  kind_ = kind;
  updateGeometry();
  show();
  raise();
  fadeTo(1.0, kFadeIn);
  hold_->start(kFadeIn + (kind == NoticeKind::Error ? errorDuration_ : infoDuration_));
  update();
}

void Notice::setDuration(NoticeKind kind, std::chrono::milliseconds duration) {
  if (kind == NoticeKind::Error) {
    errorDuration_ = duration;
  } else {
    infoDuration_ = duration;
  }
}

QFont Notice::textFont() const {
  QFont face = font();
  face.setPixelSize(12);
  return face;
}

QSize Notice::sizeHint() const {
  const QFontMetricsF metrics(textFont());
  const double mark = kind_ == NoticeKind::Error ? kMark + kMarkGap : 0.0;
  return {static_cast<int>(std::ceil(2.0 * kPadX + mark + metrics.horizontalAdvance(text_))),
          static_cast<int>(std::ceil(2.0 * kPadY + metrics.height()))};
}

void Notice::fadeTo(double opacity, std::chrono::milliseconds duration) {
  fade_->stop();
  fade_->setStartValue(opacity_);
  fade_->setEndValue(opacity);
  fade_->setDuration(static_cast<int>(duration.count()));
  fade_->start();
}

void Notice::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setOpacity(opacity_);
  p.setPen(QPen(overlay::solidBorder(overlay::kSurfaceBorder), 1.0));
  p.setBrush(overlay::kSurface);
  p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);

  double left = kPadX;
  if (kind_ == NoticeKind::Error) {
    p.setPen(Qt::NoPen);
    p.setBrush(overlay::kError);
    p.drawEllipse(QRectF(left, (height() - kMark) / 2.0, kMark, kMark));
    left += kMark + kMarkGap;
  }
  const QFont face = textFont();
  const QRectF box(left, 0.0, std::max(0.0, width() - left - kPadX), height());
  p.setFont(face);
  p.setPen(overlay::kText);
  p.drawText(box,
             Qt::AlignLeft | Qt::AlignVCenter,
             QFontMetricsF(face).elidedText(text_, Qt::ElideMiddle, box.width()));
}
