#include "ColorBarWidget.h"

#include "ScalarVizUtils.h"

#include <QColorDialog>
#include <QCursor>
#include <QDoubleValidator>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QToolButton>
#include <QWidgetAction>
#include <algorithm>
#include <array>
#include <cmath>

// ── layout constants ────────────────────────────────────────────────
static constexpr int kBarWidth = 16;
static constexpr int kHandleW = 10;
static constexpr int kHandleH = 10;
static constexpr int kTitleTopPad = 4;
static constexpr int kTitleLines = 3; // fixed so the bar keeps its height however the title wraps
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

// ── category color popup ────────────────────────────────────────────
static constexpr int kSwatchColumns = 10;
static constexpr std::array<QRgb, kSwatchColumns> kVividSwatches = {
    0xFF0000,
    0xFF8000,
    0xFFFF00,
    0x80FF00,
    0x00C000,
    0x00FFFF,
    0x0080FF,
    0x0000FF,
    0x8000FF,
    0xFF00FF,
};

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
  // Zero-width spaces let snake_case names wrap after each underscore.
  title_ = QString(title).replace(QLatin1Char('_'), QStringLiteral("_\u200B"));
  update();
}

void ColorBarWidget::setCategorical(const std::vector<std::pair<QString, QColor>>& entries) {
  categorical_ = true;
  catEntries_ = entries;
  update();
}

void ColorBarWidget::clearCategorical() {
  categorical_ = false;
  catEntries_.clear();
  update();
}

void ColorBarWidget::setCyclic(bool cyclic) {
  cyclic_ = cyclic;
  update();
}

QSize ColorBarWidget::sizeHint() const {
  int width = 90;
  if (!title_.isEmpty()) {
    QFont bold = font();
    bold.setBold(true);
    width = std::max(width, QFontMetrics(bold).horizontalAdvance(title_) + 8);
  }
  // The bar is centered, so labels on its right need the same space mirrored on its left.
  for (const auto& entry : catEntries_) {
    width = std::max(width, 2 * fontMetrics().horizontalAdvance(entry.first) + kBarWidth + 12);
  }
  if (catEntries_.empty()) {
    QFont handle = font();
    handle.setBold(true);
    handle.setPointSizeF(font().pointSizeF() * 0.9);
    for (const double v : {globalMin_, globalMax_}) {
      const int label = QFontMetrics(handle).horizontalAdvance(QString::number(v, 'f', 2));
      width = std::max(width, 2 * (label + kHandleW + 3) + kBarWidth);
    }
  }
  width = std::clamp(width, 90, 180);
  return {width, 340};
}
QSize ColorBarWidget::minimumSizeHint() const {
  return {72, 180};
}

// ── geometry helpers ────────────────────────────────────────────────
QRectF ColorBarWidget::barRect() const {
  const int fh = fontMetrics().height();
  int top = kBarPad + fh + kLabelGap + kBarPad; // title  + max-label + gap
  if (!title_.isEmpty())
    top += kTitleLines * fh + kTitleTopPad;
  int bottom = height() - fh - kLabelGap - kBarPad;
  if (bottom <= top + 40)
    bottom = top + 40;
  // Centered so it lines up with the title.
  return QRectF((width() - kBarWidth) / 2.0, top, kBarWidth, bottom - top);
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

int ColorBarWidget::categoryAt(const QPointF& pos) const {
  const QRectF bar = barRect();
  if (catEntries_.empty() || pos.x() < bar.left() || pos.y() < bar.top() ||
      pos.y() >= bar.bottom()) {
    return -1;
  }
  const int n = static_cast<int>(catEntries_.size());
  return std::min(static_cast<int>((pos.y() - bar.top()) * n / bar.height()), n - 1);
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
  const int top = std::clamp(static_cast<int>(y - h / 2.0), 2, height() - h - 2);
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
  const QColor txtCol = Qt::white;
  const QColor txtShadow(0, 0, 0, 220);
  auto drawTextRect = [&](const QRectF& rect, int flags, const QString& text) {
    p.setPen(txtShadow);
    p.drawText(rect.translated(1.0, 1.0), flags, text);
    p.setPen(txtCol);
    p.drawText(rect, flags, text);
  };
  auto drawTextPoint = [&](const QPointF& point, const QString& text) {
    p.setPen(txtShadow);
    p.drawText(point + QPointF(1.0, 1.0), text);
    p.setPen(txtCol);
    p.drawText(point, text);
  };

  // ── title ─────────────────────────────────────────────────────
  if (!title_.isEmpty()) {
    QFont bold = base;
    bold.setBold(true);
    p.setFont(bold);
    drawTextRect(QRectF(2.0, kTitleTopPad, width() - 4.0, kTitleLines * fontMetrics().height()),
                 Qt::AlignHCenter | Qt::AlignBottom | Qt::TextWordWrap,
                 title_);
    p.setFont(base);
  }

  // ── categorical (indexed) mode ─────────────────────────────────
  if (categorical_ && !catEntries_.empty()) {
    const int n = static_cast<int>(catEntries_.size());
    const double swatchH = bar.height() / n;

    QFont small = base;
    small.setPointSizeF(base.pointSizeF() * 0.82);
    p.setFont(small);
    QFontMetrics sfm(small);

    for (int i = 0; i < n; ++i) {
      // catEntries_ is stored top→bottom (highest value first)
      const auto& [label, color] = catEntries_[static_cast<size_t>(i)];
      const double y = bar.top() + i * swatchH;
      const QRectF swatch(bar.left(), y, kBarWidth, swatchH);
      p.fillRect(swatch, color);
      p.setPen(QPen(QColor(0, 0, 0, 180), 0.5));
      p.drawRect(swatch);

      // value label to the right
      const QRectF labelRect(bar.right() + 4, y, width() - bar.right() - 6, swatchH);
      drawTextRect(labelRect,
                   Qt::AlignLeft | Qt::AlignVCenter,
                   sfm.elidedText(label, Qt::ElideRight, static_cast<int>(labelRect.width())));
    }

    // min / max flanking labels (outside bar, same as continuous mode)
    p.setFont(small);
    drawTextRect(QRectF(0.0, bar.top() - sfm.height() - kLabelGap, width(), sfm.height()),
                 Qt::AlignHCenter | Qt::AlignBottom,
                 catEntries_.front().first);
    drawTextRect(QRectF(0.0, bar.bottom() + kLabelGap, width(), sfm.height()),
                 Qt::AlignHCenter | Qt::AlignTop,
                 catEntries_.back().first);
    return; // skip gradient, handles, ticks
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
      // hue 1.0 is out of range for QColor, so a cyclic bar stops just short of it.
      const double hueSpan = cyclic_ ? 0.999 : 0.8;
      grad.setColorAt(
          pos,
          QColor::fromHsvF(static_cast<QColorComponent>(std::clamp(t, 0.0, 1.0) * hueSpan), 1, 1));
    }
    p.fillRect(bar, grad);
  }

  // ── bar outline ───────────────────────────────────────────────
  p.setPen(QPen(QColor(0, 0, 0, 220), 1));
  p.setBrush(Qt::NoBrush);
  p.drawRect(bar);

  // ── tick marks (left edge of bar) ─────────────────────────────
  {
    double range = clipMax_ - clipMin_;
    if (range > 0) {
      double step = niceTickStep(range, 6);
      double first = std::ceil(clipMin_ / step) * step;
      p.setPen(QPen(txtCol, 0.75));
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
    QFontMetrics sfm(small);

    // max label just above bar
    drawTextRect(QRectF(0.0, bar.top() - sfm.height() - kLabelGap, width(), sfm.height()),
                 Qt::AlignHCenter | Qt::AlignBottom,
                 QString::number(globalMax_, 'g', 4));
    // min label just below bar
    drawTextRect(QRectF(0.0, bar.bottom() + kLabelGap, width(), sfm.height()),
                 Qt::AlignHCenter | Qt::AlignTop,
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
    QFontMetrics hfm(hf);
    drawTextPoint(QPointF(rx + kHandleW + 3, y + hfm.ascent() / 2.0 - 1),
                  QString::number(value, 'f', 2));
    p.setFont(base);
  };

  drawHandle(clipMax_, Upper);
  drawHandle(clipMin_, Lower);
}

// ── category color menu ─────────────────────────────────────────────
void ColorBarWidget::showCategoryMenu(int entry, const QPoint& globalPos) {
  // The menu runs its own event loop, which may replace the entries: copy this
  // one and recheck the index afterwards.
  const QColor current = catEntries_[static_cast<size_t>(entry)].second;
  const QString title =
      QStringLiteral("Color for ") + catEntries_[static_cast<size_t>(entry)].first;

  QMenu menu(this);
  // Lets the rounded corners show the view behind instead of a square frame.
  menu.setAttribute(Qt::WA_TranslucentBackground);
  menu.setStyleSheet(
      "QMenu { background: rgba(20,20,20,235); border: 1px solid rgba(255,255,255,40);"
      "  border-radius: 8px; padding: 4px 0; }"
      "QMenu::item { color: #D8D8D8; padding: 5px 14px; }"
      "QMenu::item:selected { background: rgba(255,255,255,30); }"
      "QMenu::separator { height: 1px; background: rgba(255,255,255,30); margin: 4px 10px; }"
      "QLabel { color: #A0A0A0; }"
      "QToolButton { border: 2px solid #4A4A4A; border-radius: 4px; }"
      "QToolButton:hover { border-color: #B0B0B0; }"
      "QToolButton:checked { border-color: white; }");

  auto* panel = new QWidget(&menu);
  auto* grid = new QGridLayout(panel);
  grid->setContentsMargins(12, 6, 12, 6);
  grid->setSpacing(6);
  auto* heading = new QLabel(title, panel);
  heading->setAlignment(Qt::AlignHCenter);
  grid->addWidget(heading, 0, 0, 1, kSwatchColumns);

  QColor picked;
  const auto addSwatch = [&](int row, int column, const QColor& color) {
    auto* swatch = new QToolButton(panel);
    swatch->setCheckable(true);
    swatch->setChecked(color == current);
    swatch->setFixedSize(22, 22);
    swatch->setToolTip(color.name());
    swatch->setStyleSheet(QStringLiteral("QToolButton { background: %1; }").arg(color.name()));
    QObject::connect(swatch, &QToolButton::clicked, &menu, [&picked, &menu, color]() {
      picked = color;
      menu.close();
    });
    grid->addWidget(swatch, row + 1, column);
  };
  const auto tab20 = [](int index) {
    return QColor::fromRgbF(static_cast<QColorComponent>(kTab20[index][0]),
                            static_cast<QColorComponent>(kTab20[index][1]),
                            static_cast<QColorComponent>(kTab20[index][2]));
  };
  for (int column = 0; column < kSwatchColumns; ++column) {
    addSwatch(0, column, tab20(2 * column));
    addSwatch(1, column, tab20(2 * column + 1));
    addSwatch(2, column, QColor(kVividSwatches[static_cast<size_t>(column)]));
    const auto grey = static_cast<QColorComponent>(1.0 - column / (kSwatchColumns - 1.0));
    addSwatch(3, column, QColor::fromRgbF(grey, grey, grey));
  }

  auto* swatches = new QWidgetAction(&menu);
  swatches->setDefaultWidget(panel);
  menu.addAction(swatches);
  menu.addSeparator();
  const QAction* more = menu.addAction(QStringLiteral("More colors…"));
  const QAction* reset = menu.addAction(QStringLiteral("Reset to default"));
  const QAction* chosen = menu.exec(globalPos);
  if (chosen == more) {
    picked = QColorDialog::getColor(current, this, title);
  }
  if (static_cast<size_t>(entry) >= catEntries_.size()) {
    return;
  }
  if (chosen == reset) {
    emit categoryColorChanged(entry, std::nullopt);
  } else if (picked.isValid()) {
    emit categoryColorChanged(entry, picked);
  }
}

void ColorBarWidget::contextMenuEvent(QContextMenuEvent* ev) {
  const int entry = categorical_ ? categoryAt(ev->pos()) : -1;
  if (entry < 0) {
    QWidget::contextMenuEvent(ev);
    return;
  }
  ev->accept();
  showCategoryMenu(entry, ev->globalPos());
}

// ── mouse interaction ───────────────────────────────────────────────
void ColorBarWidget::mousePressEvent(QMouseEvent* ev) {
  if (categorical_) {
    // contextMenuEvent opens the menu; taking the press keeps the viewport behind
    // from also starting a right-drag zoom.
    if (ev->button() == Qt::RightButton && categoryAt(mouseLocalPos(ev)) >= 0) {
      ev->accept();
      return;
    }
    QWidget::mousePressEvent(ev);
    return;
  }
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
  if (categorical_) {
    QWidget::mouseMoveEvent(ev);
    return;
  }
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
  if (categorical_) {
    QWidget::mouseDoubleClickEvent(ev);
    return;
  }
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
