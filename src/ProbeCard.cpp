#include "ProbeCard.h"

#include "LucideIcon.h"
#include "OverlayStyle.h"

#include <QCursor>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QRect>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kRadius = 10;
constexpr int kCursorGap = 12; // between the pointer's hot spot and the card
constexpr int kSwatch = 10;
constexpr int kMaxSourceWidth = 240;
constexpr int kCloseSize = 24;
constexpr int kMaxFieldWidth = 140;

class UnpinButton final : public QToolButton {
public:
  explicit UnpinButton(QWidget* parent) : QToolButton(parent) {
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
  }

protected:
  bool hitButton(const QPoint& point) const override {
    const QPointF offset = QPointF(point) - QRectF(rect()).center();
    const double radius = (width() - 1) / 2.0;
    return QPointF::dotProduct(offset, offset) <= radius * radius;
  }

  bool event(QEvent* event) override {
    if (event->type() == QEvent::Enter) {
      setHovered(hitButton(mapFromGlobal(QCursor::pos())));
    } else if (event->type() == QEvent::Leave) {
      setHovered(false);
    }
    return QToolButton::event(event);
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    setHovered(hitButton(event->pos()));
    QToolButton::mouseMoveEvent(event);
  }

  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(hasFocus() ? overlay::kAccentBright
                              : overlay::solidBorder(overlay::kSurfaceBorder));
    painter.setBrush(isDown()   ? overlay::solidBorder(overlay::kSelected)
                     : hovered_ ? overlay::solidBorder(overlay::kHover)
                                : overlay::kPopupSurface);
    painter.drawEllipse(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    const QRect glyph(
        QPoint((width() - iconSize().width()) / 2, (height() - iconSize().height()) / 2),
        iconSize());
    icon().paint(&painter, glyph, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
  }

private:
  void setHovered(bool hovered) {
    hovered_ = hovered;
    setCursor(hovered ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
  }

  bool hovered_ = false;
};
} // namespace

ProbeCard::ProbeCard(QWidget* parent) : QWidget(parent) {
  setObjectName("probeCard");
  // The view keeps the hover under the card, so the card never hides what it reads.
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::ArrowCursor);
  setAttribute(Qt::WA_NoMousePropagation, true);
  setStyleSheet(QStringLiteral("QWidget#probeCardBody { background: %1; border: 1px solid %2;"
                               "  border-radius: %3px; }"
                               "QLabel { color: %4; font-size: 12px; }"
                               "QLabel#probeSource { font-size: 11px; }"
                               "QLabel#probeValue, QLabel#probeCell { color: %5; }")
                    .arg(overlay::css(overlay::kSurface),
                         overlay::css(overlay::solidBorder(overlay::kSurfaceBorder)),
                         QString::number(kRadius),
                         overlay::css(overlay::kTextSecondary),
                         overlay::css(overlay::kText)));

  auto* outer = new QVBoxLayout(this);
  outer->setSizeConstraint(QLayout::SetFixedSize);
  outer->setSpacing(0);
  body_ = new QWidget(this);
  body_->setObjectName(QStringLiteral("probeCardBody"));
  body_->setAttribute(Qt::WA_StyledBackground, true);
  outer->addWidget(body_);

  auto* grid = new QGridLayout(body_);
  grid->setContentsMargins(10, 7, 10, 8);
  grid->setHorizontalSpacing(16);
  grid->setVerticalSpacing(2);

  source_ = new QLabel(body_);
  source_->setObjectName("probeSource");
  grid->addWidget(source_, 0, 0, 1, 2);

  field_ = new QLabel(body_);
  grid->addWidget(field_, 1, 0);
  auto* valueRow = new QHBoxLayout;
  valueRow->setSpacing(6);
  swatch_ = new QLabel(body_);
  swatch_->setObjectName("probeSwatch");
  swatch_->setFixedSize(kSwatch, kSwatch);
  valueRow->addWidget(swatch_);
  value_ = new QLabel(body_);
  value_->setObjectName("probeValue");
  valueRow->addWidget(value_);
  grid->addLayout(valueRow, 1, 1, Qt::AlignRight);

  grid->addWidget(new QLabel(QStringLiteral("Cell"), body_), 2, 0);
  cell_ = new QLabel(body_);
  cell_->setObjectName("probeCell");
  grid->addWidget(cell_, 2, 1, Qt::AlignRight);

  unpin_ = new UnpinButton(this);
  unpin_->setObjectName(QStringLiteral("probeUnpin"));
  unpin_->setIcon(lucideIcon(Lucide::X, 14));
  unpin_->setIconSize(QSize(14, 14));
  unpin_->setFixedSize(kCloseSize, kCloseSize);
  unpin_->setAccessibleName(QStringLiteral("Unpin probe"));
  unpin_->setToolTip(QStringLiteral("Unpin probe (Esc)"));
  connect(unpin_, &QToolButton::clicked, this, &ProbeCard::unpinRequested);
  setPinned(false);
}

void ProbeCard::setPinned(bool pinned) {
  // A hover card must not steal the pointer that determines its reading.
  setAttribute(Qt::WA_TransparentForMouseEvents, !pinned);
  layout()->setContentsMargins(0, pinned ? kCloseSize / 2 : 0, pinned ? kCloseSize / 2 : 0, 0);
  unpin_->setVisible(pinned);
  unpin_->setFocusPolicy(pinned ? Qt::TabFocus : Qt::NoFocus);
}

void ProbeCard::setReading(const ProbeReading& reading) {
  // The style sheet sets the fonts the elision measures with.
  ensurePolished();
  source_->setText(
      source_->fontMetrics().elidedText(reading.source, Qt::ElideMiddle, kMaxSourceWidth));
  source_->setVisible(!reading.source.isEmpty());

  const bool field = !reading.field.isEmpty();
  field_->setText(field_->fontMetrics().elidedText(reading.field, Qt::ElideMiddle, kMaxFieldWidth));
  field_->setVisible(field);
  value_->setVisible(field);
  swatch_->setVisible(field && reading.category);
  if (reading.category) {
    value_->setText(reading.category->first);
    if (reading.category->second != swatchColor_) {
      swatchColor_ = reading.category->second;
      swatch_->setStyleSheet(
          QStringLiteral("background: %1; border: 1px solid %2;"
                         "  border-radius: 3px;")
              .arg(swatchColor_.name(),
                   overlay::css(overlay::solidBorder(overlay::kSelectedBorder, swatchColor_))));
    }
  } else {
    value_->setText(std::isnan(reading.value) ? QStringLiteral("no value")
                                              : overlay::formatValue(reading.value));
  }

  cell_->setText(QString::number(reading.cell));
  // Sized now rather than on the next layout request, so moveNear measures this
  // reading; activating also drops the value row's cached size.
  body_->layout()->activate();
  layout()->activate();
}

void ProbeCard::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  unpin_->move(width() - unpin_->width(), 0);
}

void ProbeCard::moveNear(const QPoint& cursor, const QRect& viewport) {
  const QWidget* area = parentWidget();
  if (!area) {
    return;
  }
  const QRect bounds = viewport.isEmpty() ? area->rect() : viewport.intersected(area->rect());
  int x = cursor.x() + kCursorGap;
  if (x + width() > bounds.right() + 1) {
    x = cursor.x() - kCursorGap - width();
  }
  int y = cursor.y() - kCursorGap - height();
  if (y < bounds.top()) {
    y = cursor.y() + kCursorGap;
  }
  move(std::clamp(x, bounds.left(), std::max(bounds.left(), bounds.right() + 1 - width())),
       std::clamp(y, bounds.top(), std::max(bounds.top(), bounds.bottom() + 1 - height())));
}
