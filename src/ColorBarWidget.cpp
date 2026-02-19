#include "ColorBarWidget.h"

#include <QCursor>
#include <QDoubleValidator>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <algorithm>
#include <cmath>

// ── layout constants ────────────────────────────────────────────────
static constexpr int kBarLeft = 10;
static constexpr int kBarWidth = 16;
static constexpr int kHandleW = 10;
static constexpr int kHandleH = 10;
static constexpr int kTitleTopPad = 4;
static constexpr int kLabelGap = 2;
static constexpr int kBarPad = 4;

// ── tick-step helper ────────────────────────────────────────────────
static double niceTickStep(double range, int maxTicks) {
  if (range <= 0 || maxTicks <= 0)
    return 1.0;
  double rough = range / maxTicks;
  double mag = std::pow(10.0, std::floor(std::log10(rough)));
  double frac = rough / mag;
  if (frac < 1.5)
    return mag;
  if (frac < 3.0)
    return 2.0 * mag;
  if (frac < 7.0)
    return 5.0 * mag;
  return 10.0 * mag;
}

static QPointF mouseLocalPos(const QMouseEvent* ev) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return ev->position();
#else
  return ev->localPos();
#endif
}

// ═══════════════════════════════════════════════════════════════════
ColorBarWidget::ColorBarWidget(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setCursor(Qt::ArrowCursor);

  inlineEditor_ = new QLineEdit(this);
  inlineEditor_->setVisible(false);
  inlineEditor_->setAlignment(Qt::AlignCenter);
  auto* validator = new QDoubleValidator(this);
  validator->setLocale(QLocale::c());
  inlineEditor_->setValidator(validator);
  inlineEditor_->setLocale(QLocale::c());
  QObject::connect(
      inlineEditor_, &QLineEdit::editingFinished, this, [this]() { commitInlineEditor(); });
}

// ── public API ──────────────────────────────────────────────────────
void ColorBarWidget::setRange(double min, double max) {
  if (max <= min)
    return;
  globalMin_ = min;
  globalMax_ = max;
  clipMin_ = std::max(clipMin_, globalMin_);
  clipMax_ = std::min(clipMax_, globalMax_);
  update();
}

void ColorBarWidget::setClipRange(double lower, double upper) {
  lower = std::clamp(lower, globalMin_, globalMax_);
  upper = std::clamp(upper, globalMin_, globalMax_);
  if (lower > upper)
    std::swap(lower, upper);
  clipMin_ = lower;
  clipMax_ = upper;
  update();
}

void ColorBarWidget::setTitle(const QString& title) {
  title_ = title;
  setToolTip(title_);
  update();
}

QSize ColorBarWidget::sizeHint() const {
  return {90, 340};
}
QSize ColorBarWidget::minimumSizeHint() const {
  return {72, 180};
}

// ── geometry helpers ────────────────────────────────────────────────
QRectF ColorBarWidget::barRect() const {
  const int fh = fontMetrics().height();
  int top = kBarPad + fh + kLabelGap + kBarPad; // title  + max-label + gap
  if (!title_.isEmpty())
    top += fh + kTitleTopPad;
  int bottom = height() - fh - kLabelGap - kBarPad;
  if (bottom <= top + 40)
    bottom = top + 40;
  return QRectF(kBarLeft, top, kBarWidth, bottom - top);
}

double ColorBarWidget::valueToY(double value) const {
  const QRectF bar = barRect();
  if (globalMax_ <= globalMin_)
    return bar.center().y();
  double t = (value - globalMin_) / (globalMax_ - globalMin_);
  t = std::clamp(t, 0.0, 1.0);
  return bar.bottom() - t * bar.height();
}

double ColorBarWidget::yToValue(double y) const {
  const QRectF bar = barRect();
  if (bar.height() <= 0)
    return globalMin_;
  double t = (bar.bottom() - y) / bar.height();
  t = std::clamp(t, 0.0, 1.0);
  return globalMin_ + t * (globalMax_ - globalMin_);
}

ColorBarWidget::Handle ColorBarWidget::hitTestHandle(const QPointF& pos, double tolerance) const {
  const QRectF bar = barRect();
  const double labelHitWidth = 90.0;
  if (pos.x() < bar.left() - tolerance || pos.x() > bar.right() + kHandleW + labelHitWidth)
    return None;

  double dU = std::abs(pos.y() - valueToY(clipMax_));
  double dL = std::abs(pos.y() - valueToY(clipMin_));

  if (dU <= tolerance && dL <= tolerance)
    return (dU <= dL) ? Upper : Lower;
  if (dU <= tolerance)
    return Upper;
  if (dL <= tolerance)
    return Lower;
  return None;
}

QColor ColorBarWidget::colorForValue(double value) const {
  if (globalMax_ <= globalMin_)
    return QColor(128, 128, 128);
  double t = std::clamp((value - globalMin_) / (globalMax_ - globalMin_), 0.0, 1.0);
  // Match VTK default rainbow: hue 0.0 → 0.8
  return QColor::fromHsvF(t * 0.8, 1.0, 1.0);
}

void ColorBarWidget::emitClipChanged() {
  emit clipRangeChanged(clipMin_, clipMax_);
}

void ColorBarWidget::showInlineEditor(Handle handle) {
  if (!inlineEditor_ || handle == None)
    return;

  editHandle_ = handle;
  const QRectF bar = barRect();
  const double y = valueToY(handle == Upper ? clipMax_ : clipMin_);
  const int x = static_cast<int>(bar.right() + kHandleW + 4);
  const int w = 58;
  const int h = 22;
  const int top = std::clamp(static_cast<int>(y - h / 2), 2, height() - h - 2);
  inlineEditor_->setGeometry(x, top, w, h);
  inlineEditor_->setText(QString::number(handle == Upper ? clipMax_ : clipMin_, 'f', 2));
  inlineEditor_->setVisible(true);
  inlineEditor_->setFocus();
  inlineEditor_->selectAll();
}

void ColorBarWidget::commitInlineEditor() {
  if (!inlineEditor_ || !inlineEditor_->isVisible() || editHandle_ == None)
    return;

  bool ok = false;
  const double entered = QLocale::c().toDouble(inlineEditor_->text(), &ok);
  inlineEditor_->setVisible(false);
  if (!ok) {
    editHandle_ = None;
    return;
  }

  if (editHandle_ == Upper) {
    clipMax_ = std::clamp(entered, clipMin_, globalMax_);
  } else {
    clipMin_ = std::clamp(entered, globalMin_, clipMax_);
  }
  editHandle_ = None;
  update();
  emitClipChanged();
}

// ── painting ────────────────────────────────────────────────────────
void ColorBarWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const QRectF bar = barRect();
  const QFont base = font();
  const QFontMetrics fm(base);
  const QColor txtCol = palette().text().color();

  // ── title ─────────────────────────────────────────────────────
  if (!title_.isEmpty()) {
    QFont bold = base;
    bold.setBold(true);
    p.setFont(bold);
    p.setPen(txtCol);
    const QRectF titleRect(2.0, kTitleTopPad, width() - 4.0, fm.height());
    const QString elided =
        QFontMetrics(bold).elidedText(title_, Qt::ElideRight, static_cast<int>(titleRect.width()));
    p.drawText(titleRect, Qt::AlignHCenter, elided);
    p.setFont(base);
  }

  // ── gradient bar ──────────────────────────────────────────────
  {
    QLinearGradient grad(bar.topLeft(), bar.bottomLeft());
    // pos 0 = bar top = global max value, pos 1 = bar bottom = global min value
    // clip range is compressed to full rainbow [0..0.8 hue], matching mapper
    const int N = 128;
    const double clipSpan = std::max(clipMax_ - clipMin_, 1e-12);
    for (int i = 0; i <= N; ++i) {
      double pos = double(i) / N;
      double value = globalMax_ - pos * (globalMax_ - globalMin_);
      double mappedValue = value;
      if (value > clipMax_)
        mappedValue = clipMax_;
      else if (value < clipMin_)
        mappedValue = clipMin_;

      const double t = (mappedValue - clipMin_) / clipSpan;
      grad.setColorAt(pos, QColor::fromHsvF(std::clamp(t, 0.0, 1.0) * 0.8, 1.0, 1.0));
    }
    p.fillRect(bar, grad);
  }

  // ── bar outline ───────────────────────────────────────────────
  p.setPen(QPen(palette().mid().color(), 1));
  p.setBrush(Qt::NoBrush);
  p.drawRect(bar);

  // ── tick marks (left edge of bar) ─────────────────────────────
  {
    double range = clipMax_ - clipMin_;
    if (range > 0) {
      double step = niceTickStep(range, 6);
      double first = std::ceil(clipMin_ / step) * step;
      p.setPen(QPen(txtCol, 0.5));
      for (double v = first; v <= clipMax_ + step * 0.001; v += step) {
        double y = valueToY(v);
        if (y < bar.top() + 1 || y > bar.bottom() - 1)
          continue;
        p.drawLine(QPointF(bar.left() - 4, y), QPointF(bar.left(), y));
      }
    }
  }

  // ── global min/max labels ─────────────────────────────────────
  {
    QFont small = base;
    small.setPointSizeF(base.pointSizeF() * 0.85);
    p.setFont(small);
    p.setPen(txtCol);
    QFontMetrics sfm(small);

    // max label just above bar
    p.drawText(
        QRectF(
            bar.left(), bar.top() - sfm.height() - kLabelGap, width() - bar.left(), sfm.height()),
        Qt::AlignLeft | Qt::AlignBottom,
        QString::number(globalMax_, 'g', 4));
    // min label just below bar
    p.drawText(QRectF(bar.left(), bar.bottom() + kLabelGap, width() - bar.left(), sfm.height()),
               Qt::AlignLeft | Qt::AlignTop,
               QString::number(globalMin_, 'g', 4));
    p.setFont(base);
  }

  // ── handles ───────────────────────────────────────────────────
  auto drawHandle = [&](double value, Handle which) {
    const double y = valueToY(value);
    const bool on = (hoverHandle_ == which || dragHandle_ == which);

    // triangle at right edge of bar, pointing right
    QPainterPath tri;
    const double rx = bar.right();
    tri.moveTo(rx, y - kHandleH / 2.0);
    tri.lineTo(rx + kHandleW, y);
    tri.lineTo(rx, y + kHandleH / 2.0);
    tri.closeSubpath();

    p.setPen(QPen(Qt::black, 1));
    p.setBrush(on ? Qt::white : QColor(210, 210, 210));
    p.drawPath(tri);

    // value label right of triangle
    QFont hf = base;
    hf.setBold(on);
    hf.setPointSizeF(base.pointSizeF() * 0.9);
    p.setFont(hf);
    p.setPen(txtCol);
    QFontMetrics hfm(hf);
    p.drawText(QPointF(rx + kHandleW + 3, y + hfm.ascent() / 2.0 - 1),
               QString::number(value, 'f', 2));
    p.setFont(base);
  };

  drawHandle(clipMax_, Upper);
  drawHandle(clipMin_, Lower);
}

// ── mouse interaction ───────────────────────────────────────────────
void ColorBarWidget::mousePressEvent(QMouseEvent* ev) {
  if (inlineEditor_ && inlineEditor_->isVisible()) {
    commitInlineEditor();
  }

  if (ev->button() == Qt::LeftButton) {
    Handle h = hitTestHandle(mouseLocalPos(ev));
    if (h != None) {
      dragHandle_ = h;
      setCursor(Qt::PointingHandCursor);
      ev->accept();
      return;
    }
  }
  QWidget::mousePressEvent(ev);
}

void ColorBarWidget::mouseMoveEvent(QMouseEvent* ev) {
  if (dragHandle_ != None) {
    double v = yToValue(mouseLocalPos(ev).y());
    if (dragHandle_ == Upper)
      clipMax_ = std::clamp(v, clipMin_, globalMax_);
    else
      clipMin_ = std::clamp(v, globalMin_, clipMax_);
    update();
    emitClipChanged();
    ev->accept();
    return;
  }

  // hover highlight
  Handle h = hitTestHandle(mouseLocalPos(ev));
  if (h != hoverHandle_) {
    hoverHandle_ = h;
    setCursor(h != None ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
  }
  QWidget::mouseMoveEvent(ev);
}

void ColorBarWidget::mouseReleaseEvent(QMouseEvent* ev) {
  if (ev->button() == Qt::LeftButton && dragHandle_ != None) {
    dragHandle_ = None;
    Handle h = hitTestHandle(mouseLocalPos(ev));
    hoverHandle_ = h;
    setCursor(h != None ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
    ev->accept();
    return;
  }
  QWidget::mouseReleaseEvent(ev);
}

void ColorBarWidget::mouseDoubleClickEvent(QMouseEvent* ev) {
  Handle h = hitTestHandle(mouseLocalPos(ev));
  if (h != None) {
    showInlineEditor(h);
    ev->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(ev);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void ColorBarWidget::enterEvent(QEnterEvent* ev) {
#else
void ColorBarWidget::enterEvent(QEvent* ev) {
#endif
  const QPoint globalPos = QCursor::pos();
  const QPoint localPos = mapFromGlobal(globalPos);
  Handle h = hitTestHandle(localPos);
  hoverHandle_ = h;
  setCursor(h != None ? Qt::PointingHandCursor : Qt::ArrowCursor);
  update();
  QWidget::enterEvent(ev);
}

void ColorBarWidget::leaveEvent(QEvent* ev) {
  hoverHandle_ = None;
  setCursor(Qt::ArrowCursor);
  update();
  QWidget::leaveEvent(ev);
}
