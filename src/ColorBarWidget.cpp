#include "ColorBarWidget.h"

#include "ColorPanel.h"
#include "Colormaps.h"
#include "OverlayStyle.h"

#include <QActionGroup>
#include <QColorDialog>
#include <QDoubleValidator>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QRegion>
#include <QStringList>
#include <QToolButton>
#include <QToolTip>
#include <QWidgetAction>
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <limits>
#include <utility>

namespace {

// Layout dimensions are in logical pixels.
constexpr int kWidth = 220;
constexpr double kPad = 12.0;
constexpr double kSubtitleGap = 2.0;
constexpr double kHeaderGap = 10.0;
constexpr double kBarHeight = 10.0;
constexpr double kKnobWidth = 10.0;
constexpr double kKnobHeight = 18.0;
constexpr double kHandleTolerance = 8.0;
constexpr double kValueGap = 4.0;
constexpr double kLabelSpacing = 8.0;
constexpr double kRowHeight = 26.0;
constexpr double kMinRowHeight = 18.0;
constexpr double kSwatch = 14.0;
constexpr double kSwatchGap = 10.0;

constexpr int kCompactWidth = 200;
constexpr int kMinimalWidth = 180;
constexpr double kCompactPad = 8.0;
constexpr double kMinimalPad = 6.0;
constexpr double kCompactGap = 6.0; // between the name line and the bar or chips
constexpr double kSmallKnobHeight = 16.0;
constexpr double kSmallBarHeight = 8.0;
constexpr double kValueSpacing = 6.0; // between a clip value and the bar
constexpr double kMinBarWidth = 32.0; // narrower, the bar takes the values' room
constexpr double kChipHeight = 20.0;
constexpr double kChipPad = 6.0;
constexpr double kChipSwatch = 10.0;
constexpr double kChipSwatchGap = 5.0;
constexpr double kChipSpacing = 4.0;
constexpr double kChipLabelWidth = 72.0; // longer labels are shortened
constexpr double kChipRadius = 6.0;
constexpr double kMiniSwatch = 14.0;
constexpr double kMiniRowHeight = 16.0;

// The Full card, 220 x 100 for a bar, takes under a tenth of a 560 x 400 panel and stays
// clear of its middle; the compact one, 200 x 53, a quarter of a 260 x 160 panel.
constexpr int kFullPanelWidth = 560;
constexpr int kFullPanelHeight = 400;
constexpr int kCompactPanelWidth = 260;
constexpr int kCompactPanelHeight = 160;

constexpr int kSwatchColumns = 10;
constexpr std::array<QRgb, kSwatchColumns> kVividSwatches = {
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

} // namespace

LegendForm legendFormFor(const QSize& panel) {
  if (panel.width() >= kFullPanelWidth && panel.height() >= kFullPanelHeight) {
    return LegendForm::Full;
  }
  if (panel.width() >= kCompactPanelWidth && panel.height() >= kCompactPanelHeight) {
    return LegendForm::Compact;
  }
  return LegendForm::Minimal;
}

ColorBarWidget::ColorBarWidget(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setCursor(Qt::ArrowCursor);
  // Presses and wheel turns on the card must not reach the viewport behind it.
  setAttribute(Qt::WA_NoMousePropagation, true);

  inlineEditor_ = new QLineEdit(this);
  inlineEditor_->setVisible(false);
  inlineEditor_->setAlignment(Qt::AlignCenter);
  inlineEditor_->setStyleSheet(
      QStringLiteral("QLineEdit { background: #FF1E1E22; color: %1;"
                     "  border: 1px solid %2; border-radius: 6px; }")
          .arg(overlay::css(overlay::kText), overlay::css(overlay::kAccent)));
  auto* validator = new QDoubleValidator(this);
  validator->setLocale(QLocale::c());
  inlineEditor_->setValidator(validator);
  inlineEditor_->setLocale(QLocale::c());
  connect(inlineEditor_, &QLineEdit::editingFinished, this, &ColorBarWidget::commitInlineEditor);
  updateValueColumns();
}

ColorBarWidget::~ColorBarWidget() {
  // QWidget teardown can finish a focused child edit after this derived object is gone.
  QObject::disconnect(inlineEditor_, nullptr, this, nullptr);
}

void ColorBarWidget::setRange(double min, double max) {
  if (max <= min)
    return;
  globalMin_ = min;
  globalMax_ = max;
  setClipRange(clipMin_, clipMax_);
}

void ColorBarWidget::setClipRange(double lower, double upper) {
  lower = std::clamp(lower, globalMin_, globalMax_);
  upper = std::clamp(upper, globalMin_, globalMax_);
  if (lower > upper)
    std::swap(lower, upper);
  if (lower != clipMin_)
    clipValueFormats_[0] = ValueFormat::Rounded;
  if (upper != clipMax_)
    clipValueFormats_[1] = ValueFormat::Rounded;
  clipMin_ = lower;
  clipMax_ = upper;
  updateValueColumns();
}

void ColorBarWidget::setClipValueFormat(Bound bound, ValueFormat format) {
  auto& current = clipValueFormats_[bound == Bound::Lower ? 0 : 1];
  if (current == format)
    return;
  current = format;
  updateValueColumns();
}

void ColorBarWidget::setTitle(const QString& title) {
  title_ = title;
  update();
}

void ColorBarWidget::setSubtitle(const QString& subtitle) {
  subtitle_ = subtitle;
  updateGeometry();
  update();
}

void ColorBarWidget::setCategorical(const std::vector<std::pair<QString, QColor>>& entries) {
  categorical_ = true;
  catEntries_ = entries;
  hoverEntry_ = -1;
  updateGeometry();
  update();
}

void ColorBarWidget::clearCategorical() {
  categorical_ = false;
  catEntries_.clear();
  clipValueFormats_.fill(ValueFormat::Rounded);
  updateValueColumns();
  hoverEntry_ = -1;
  updateGeometry();
}

void ColorBarWidget::setPalettes(std::vector<PaletteOption> palettes, const QString& current) {
  palettes_ = std::move(palettes);
  currentPalette_ = current;
}

void ColorBarWidget::setColormap(const ColormapChoice& colormap) {
  colormap_ = colormap;
  update();
}

void ColorBarWidget::setForm(LegendForm form) {
  if (form == form_) {
    return;
  }
  form_ = form;
  hoverEntry_ = -1;
  updateValueColumns();
  updateGeometry();
}

QSize ColorBarWidget::fittedSize(const QSize& bounds) const {
  const int maxWidth = std::max(1, bounds.width());
  const int maxHeight = std::max(1, bounds.height());
  double width = kWidth;
  double height = 0.0;
  if (form_ == LegendForm::Full) {
    height = fullHeight();
    if (!categorical_) {
      width = std::max(width, 2.0 * pad() + valueColumns_[0] + valueColumns_[1] + kLabelSpacing);
    }
  } else {
    const bool compact = form_ == LegendForm::Compact;
    const double padding = pad();
    const double top = bodyTop();
    width = compact ? (categorical_ ? kWidth : kCompactWidth) : kMinimalWidth;
    if (!categorical_) {
      height = top + kSmallKnobHeight + padding;
      width = std::max(width,
                       2.0 * padding + valueColumns_[0] + valueColumns_[1] + 2.0 * kValueSpacing +
                           kKnobWidth + kMinBarWidth);
    } else if (!compact) {
      // One row of swatches, no wider than they need.
      const auto count = static_cast<double>(std::max<size_t>(1, catEntries_.size()));
      width = std::min(width, 2.0 * padding + count * (kMiniSwatch + kChipSpacing) - kChipSpacing);
      height = top + kMiniRowHeight + padding;
    } else {
      // Every chip's row, or as many rows as the bounds hold.
      const double inner = std::min<double>(width, maxWidth) - 2.0 * padding;
      const CategoryLayout all =
          flowCategories(QRectF(padding, top, std::max(1.0, inner), 1e6), INT_MAX);
      const int fit = std::max(1,
                               static_cast<int>((maxHeight - top - padding + kChipSpacing) /
                                                (kChipHeight + kChipSpacing)));
      const int rows = std::clamp(all.rows, 1, fit);
      height = top + rows * kChipHeight + (rows - 1) * kChipSpacing + padding;
    }
  }
  return {std::min(static_cast<int>(std::ceil(width)), maxWidth),
          std::min(static_cast<int>(std::ceil(height)), maxHeight)};
}

QSize ColorBarWidget::fitPanel(const QSize& panel, int margin) {
  setForm(legendFormFor(panel));
  // Below the panel's middle, which is where its mesh is centered.
  QSize bounds(std::max(1, panel.width() - 2 * margin), std::max(1, panel.height() / 2 - margin));
  if (categorical_) {
    bounds.setHeight(
        std::min(bounds.height(),
                 std::max(panel.height() / 5, static_cast<int>(2 * kMinimalPad + kMiniRowHeight))));
    if (form_ == LegendForm::Full && fullHeight() > bounds.height()) {
      setForm(LegendForm::Compact);
    }
    if (form_ == LegendForm::Compact && bodyTop() + kChipHeight + pad() > bounds.height()) {
      setForm(LegendForm::Minimal);
    }
  }
  QSize size = fittedSize(bounds);
  const qint64 quarter = static_cast<qint64>(panel.width()) * panel.height() / 4;
  if (static_cast<qint64>(size.width()) * size.height() > quarter) {
    // Category rows and chips give up rows; a bar or a row of swatches gives up width.
    if (categorical_ && form_ != LegendForm::Minimal) {
      bounds.setHeight(static_cast<int>(std::max<qint64>(1, quarter / size.width())));
    } else {
      bounds.setWidth(static_cast<int>(std::max<qint64>(1, quarter / size.height())));
    }
    size = fittedSize(bounds);
  }
  return size;
}

QSize ColorBarWidget::sizeHint() const {
  return fittedSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
}

QSize ColorBarWidget::minimumSizeHint() const {
  return {120, static_cast<int>(std::ceil(bodyTop() + kMinRowHeight + kPad))};
}

QFont ColorBarWidget::titleFont() const {
  QFont title = font();
  title.setPixelSize(form_ == LegendForm::Full ? 13 : 12);
  title.setBold(true);
  return title;
}

QFont ColorBarWidget::labelFont() const {
  QFont label = font();
  label.setPixelSize(12);
  return label;
}

QFont ColorBarWidget::smallFont() const {
  QFont small = font();
  small.setPixelSize(11);
  return small;
}

double ColorBarWidget::pad() const {
  switch (form_) {
  case LegendForm::Full:
    return kPad;
  case LegendForm::Compact:
    return kCompactPad;
  case LegendForm::Minimal:
    break;
  }
  return kMinimalPad;
}

double ColorBarWidget::knobHeight() const {
  return form_ == LegendForm::Full ? kKnobHeight : kSmallKnobHeight;
}

double ColorBarWidget::headerHeight() const {
  return std::max(QFontMetricsF(titleFont()).height(), QFontMetricsF(smallFont()).height());
}

double ColorBarWidget::bodyTop() const {
  switch (form_) {
  case LegendForm::Full:
    break;
  case LegendForm::Compact:
    return kCompactPad + headerHeight() + kCompactGap;
  case LegendForm::Minimal:
    return kMinimalPad;
  }
  double top = kPad + QFontMetricsF(titleFont()).height();
  if (!subtitle_.isEmpty()) {
    top += kSubtitleGap + QFontMetricsF(smallFont()).height();
  }
  return top + kHeaderGap;
}

double ColorBarWidget::fullHeight() const {
  double height = bodyTop() + kPad;
  if (categorical_) {
    height += static_cast<double>(catEntries_.size()) * kRowHeight;
  } else {
    height += kKnobHeight + kValueGap + QFontMetricsF(smallFont()).height();
  }
  return height;
}

QRectF ColorBarWidget::bodyRect() const {
  const double top = bodyTop();
  const double padding = pad();
  return {padding,
          top,
          std::max(1.0, width() - 2.0 * padding),
          std::max(1.0, height() - padding - top)};
}

bool ColorBarWidget::valuesBeside() const {
  return form_ != LegendForm::Full && bodyRect().width() - valueColumns_[0] - valueColumns_[1] -
                                              2.0 * kValueSpacing - kKnobWidth >=
                                          kMinBarWidth;
}

QString ColorBarWidget::valueText(double value) const {
  if ((value == clipMin_ && clipValueFormats_[0] == ValueFormat::Exact) ||
      (value == clipMax_ && clipValueFormats_[1] == ValueFormat::Exact)) {
    return QString::number(value, 'g', QLocale::FloatingPointShortest);
  }
  return roundedValueText(value);
}

QString ColorBarWidget::roundedValueText(double value) const {
  if (value == 0.0) {
    return QStringLiteral("0");
  }
  const double span = globalMax_ - globalMin_;
  if (!std::isfinite(value) || !std::isfinite(span) || span <= 0.0) {
    return overlay::formatValue(value);
  }
  const double magnitude = std::abs(value);
  const int exponent = static_cast<int>(std::floor(std::log10(magnitude)));
  // Keep two significant digits when span-based rounding would be coarser.
  const int decimals = std::max(2 - static_cast<int>(std::ceil(std::log10(span))), 1 - exponent);
  const bool scientific = magnitude < 1e-4 || magnitude >= 1e6;
  const int precision =
      scientific ? std::clamp(decimals + exponent, 1, std::numeric_limits<double>::max_digits10 - 1)
                 : std::max(0, decimals);
  QString text = QString::number(value, scientific ? 'e' : 'f', precision);
  if (precision > 0) {
    auto end = text.indexOf(QLatin1Char('e'));
    if (end < 0) {
      end = text.size();
    }
    auto trimmed = end;
    while (text.at(trimmed - 1) == QLatin1Char('0')) {
      --trimmed;
    }
    if (text.at(trimmed - 1) == QLatin1Char('.')) {
      --trimmed;
    }
    text.remove(trimmed, end - trimmed);
  }
  return text;
}

void ColorBarWidget::updateValueColumns() {
  const auto before = valueColumns_;
  const double magnitude = std::max(std::abs(globalMin_), std::abs(globalMax_));
  const QFontMetricsF metrics(smallFont());
  const double reserve = metrics.horizontalAdvance(
      roundedValueText((globalMin_ < 0.0 ? -0.8765 : 0.8765) * magnitude));
  // Reserving one end's width for the other can hide both labels in narrow cards.
  const auto column = [&](double end, double clip) {
    return std::ceil(std::max({reserve,
                               metrics.horizontalAdvance(roundedValueText(end)),
                               metrics.horizontalAdvance(valueText(clip))}));
  };
  valueColumns_ = {column(globalMin_, clipMin_), column(globalMax_, clipMax_)};
  if (valueColumns_ != before) {
    updateGeometry();
    emit sizeHintChanged();
  }
  update();
}

QRectF ColorBarWidget::barRect() const {
  const QRectF body = bodyRect();
  const double inset = kKnobWidth / 2.0;
  if (form_ == LegendForm::Full) {
    return {body.left() + inset,
            body.top() + (kKnobHeight - kBarHeight) / 2.0,
            std::max(1.0, body.width() - 2.0 * inset),
            kBarHeight};
  }
  const bool beside = valuesBeside();
  const double lower = beside ? valueColumns_[0] + kValueSpacing : 0.0;
  const double upper = beside ? valueColumns_[1] + kValueSpacing : 0.0;
  return {body.left() + lower + inset,
          body.center().y() - kSmallBarHeight / 2.0,
          std::max(1.0, body.width() - lower - upper - 2.0 * inset),
          kSmallBarHeight};
}

double ColorBarWidget::valueToX(double value) const {
  const QRectF bar = barRect();
  if (globalMax_ <= globalMin_)
    return bar.center().x();
  const double t = std::clamp((value - globalMin_) / (globalMax_ - globalMin_), 0.0, 1.0);
  return bar.left() + t * bar.width();
}

double ColorBarWidget::xToValue(double x) const {
  const QRectF bar = barRect();
  const double t = std::clamp((x - bar.left()) / bar.width(), 0.0, 1.0);
  return globalMin_ + t * (globalMax_ - globalMin_);
}

std::array<QRectF, 2> ColorBarWidget::clipValueRects() const {
  if (categorical_ || (form_ != LegendForm::Full && !valuesBeside())) {
    return {};
  }
  const QRectF body = bodyRect();
  const QRectF bar = barRect();
  const QFontMetricsF metrics(smallFont());
  const double lowerWidth = metrics.horizontalAdvance(valueText(clipMin_));
  const double upperWidth = metrics.horizontalAdvance(valueText(clipMax_));
  if (form_ != LegendForm::Full) {
    const double top = bar.center().y() - metrics.height() / 2.0;
    return {QRectF(body.left() + valueColumns_[0] - lowerWidth, top, lowerWidth, metrics.height()),
            QRectF(body.right() - valueColumns_[1], top, upperWidth, metrics.height())};
  }

  // Clip values under their handles, pushed apart when the handles are close.
  const auto fitLeft = [&body](double left, double width) {
    return std::clamp(left, body.left(), std::max(body.left(), body.right() - width));
  };
  const double lowerX = valueToX(clipMin_);
  const double upperX = valueToX(clipMax_);
  double lowerLeft = fitLeft(lowerX - lowerWidth / 2.0, lowerWidth);
  double upperLeft = fitLeft(upperX - upperWidth / 2.0, upperWidth);
  if (lowerLeft + lowerWidth + kLabelSpacing > upperLeft) {
    const double mid = (lowerX + upperX) / 2.0;
    upperLeft = fitLeft(mid + kLabelSpacing / 2.0, upperWidth);
    lowerLeft = std::max(body.left(), upperLeft - kLabelSpacing - lowerWidth);
    upperLeft = std::max(upperLeft, lowerLeft + lowerWidth + kLabelSpacing);
  }
  const double top = bar.center().y() + kKnobHeight / 2.0 + kValueGap;
  return {QRectF(lowerLeft, top, lowerWidth, metrics.height()),
          QRectF(upperLeft, top, upperWidth, metrics.height())};
}

ColorBarWidget::Handle ColorBarWidget::hitTestValue(const QPointF& pos) const {
  const auto rects = clipValueRects();
  if (rects[0].contains(pos))
    return Lower;
  return rects[1].contains(pos) ? Upper : None;
}

ColorBarWidget::Handle ColorBarWidget::hitTestHandle(const QPointF& pos) const {
  const QRectF bar = barRect();
  if (categorical_ || std::abs(pos.y() - bar.center().y()) >= knobHeight() / 2.0 + kValueGap)
    return None;
  const double lower = valueToX(clipMin_);
  const double upper = valueToX(clipMax_);
  const double dL = std::abs(pos.x() - lower);
  const double dU = std::abs(pos.x() - upper);
  if (dL > kHandleTolerance && dU > kHandleTolerance)
    return None;
  // Stacked handles: the side the pointer is on decides, so either can move off the other.
  if (std::abs(dL - dU) < 1e-9)
    return pos.x() >= upper ? Upper : Lower;
  return dL < dU ? Lower : Upper;
}

int ColorBarWidget::categoryRows() const {
  const int count = static_cast<int>(catEntries_.size());
  if (count == 0)
    return 0;
  const int fit = std::max(1, static_cast<int>(bodyRect().height() / kMinRowHeight));
  const int columns = (count + fit - 1) / fit;
  return (count + columns - 1) / columns;
}

QRectF ColorBarWidget::categoryRect(int entry) const {
  const QRectF body = bodyRect();
  const int count = static_cast<int>(catEntries_.size());
  const int rows = std::max(1, categoryRows());
  const int columns = (count + rows - 1) / rows;
  const double rowHeight = std::min(kRowHeight, body.height() / rows);
  const double columnWidth = body.width() / std::max(1, columns);
  // Entries fill a column top to bottom before the next column starts.
  const int column = entry / rows;
  const int row = entry % rows;
  return {body.left() + column * columnWidth, body.top() + row * rowHeight, columnWidth, rowHeight};
}

double ColorBarWidget::chipWidth(int entry) const {
  const double label =
      QFontMetricsF(smallFont()).horizontalAdvance(catEntries_[static_cast<size_t>(entry)].first);
  return std::ceil(2.0 * kChipPad + kChipSwatch + kChipSwatchGap +
                   std::min(label, kChipLabelWidth));
}

ColorBarWidget::CategoryLayout ColorBarWidget::flowCategories(const QRectF& body,
                                                              int maxRows) const {
  CategoryLayout layout;
  const int count = static_cast<int>(catEntries_.size());
  layout.entries.assign(catEntries_.size(), QRectF());
  if (count == 0) {
    return layout;
  }
  const bool chips = form_ == LegendForm::Compact;
  const double rowHeight = chips ? kChipHeight : kMiniRowHeight;
  const auto rowTop = [&](int row) { return body.top() + row * (rowHeight + kChipSpacing); };

  // Left to right, onto a new row where the next one does not fit.
  double x = body.left();
  int row = 0;
  int shown = 0;
  for (; shown < count; ++shown) {
    const double width = std::min(chips ? chipWidth(shown) : kMiniSwatch, body.width());
    if (x > body.left() && x + width > body.right()) {
      if (row + 1 >= maxRows) {
        break;
      }
      ++row;
      x = body.left();
    }
    layout.entries[static_cast<size_t>(shown)] = QRectF(x, rowTop(row), width, rowHeight);
    x += width + kChipSpacing;
  }
  layout.rows = row + 1;
  if (shown == count) {
    return layout;
  }

  // The rest behind a +N chip after the last one shown, which gives way until it fits.
  const QFontMetricsF metrics(smallFont());
  while (true) {
    const double width =
        std::min(body.width(),
                 std::ceil(2.0 * kChipPad +
                           metrics.horizontalAdvance(QStringLiteral("+%1").arg(count - shown))));
    const QRectF last = shown > 0 ? layout.entries[static_cast<size_t>(shown - 1)] : QRectF();
    const double left = shown > 0 ? last.right() + kChipSpacing : body.left();
    if (shown == 0 || left + width <= body.right()) {
      layout.more = QRectF(left, shown > 0 ? last.top() : body.top(), width, rowHeight);
      break;
    }
    layout.entries[static_cast<size_t>(--shown)] = QRectF();
  }
  layout.hidden = count - shown;
  return layout;
}

ColorBarWidget::CategoryLayout ColorBarWidget::categoryLayout() const {
  if (form_ == LegendForm::Full) {
    CategoryLayout layout;
    layout.entries.reserve(catEntries_.size());
    for (int i = 0; i < static_cast<int>(catEntries_.size()); ++i) {
      layout.entries.push_back(categoryRect(i));
    }
    layout.rows = categoryRows();
    return layout;
  }
  const QRectF body = bodyRect();
  const double rowHeight = form_ == LegendForm::Compact ? kChipHeight : kMiniRowHeight;
  const int rows =
      std::max(1, static_cast<int>((body.height() + kChipSpacing) / (rowHeight + kChipSpacing)));
  return flowCategories(body, rows);
}

QRectF ColorBarWidget::swatchRect(const CategoryLayout& layout, int entry) const {
  const QRectF target = layout.entries[static_cast<size_t>(entry)];
  if (target.isEmpty()) {
    return {};
  }
  switch (form_) {
  case LegendForm::Full:
    break;
  case LegendForm::Compact:
    return {target.left() + kChipPad,
            target.center().y() - kChipSwatch / 2.0,
            kChipSwatch,
            kChipSwatch};
  case LegendForm::Minimal: {
    const double side = std::min({kMiniSwatch, target.width(), target.height()});
    return {target.center().x() - side / 2.0, target.center().y() - side / 2.0, side, side};
  }
  }
  const double side = std::min(kSwatch, target.height() - 4.0);
  return {target.left() + 6.0, target.center().y() - side / 2.0, side, side};
}

int ColorBarWidget::categoryAt(const CategoryLayout& layout, const QPointF& pos) const {
  for (size_t i = 0; i < layout.entries.size(); ++i) {
    if (layout.entries[i].contains(pos))
      return static_cast<int>(i);
  }
  return -1;
}

QString ColorBarWidget::hiddenCategories(const CategoryLayout& layout) const {
  QStringList labels;
  for (size_t i = 0; i < layout.entries.size(); ++i) {
    if (layout.entries[i].isEmpty()) {
      labels << catEntries_[i].first;
    }
  }
  return labels.join(QStringLiteral(", "));
}

void ColorBarWidget::setClipValue(Handle handle, double value) {
  if (handle == Upper) {
    setClipRange(clipMin_, std::clamp(value, clipMin_, globalMax_));
  } else {
    setClipRange(std::clamp(value, globalMin_, clipMax_), clipMax_);
  }
  emit clipRangeChanged(clipMin_, clipMax_);
}

void ColorBarWidget::showInlineEditor(Handle handle) {
  if (handle == None)
    return;

  editHandle_ = handle;
  const QPointF center = clipValueRects()[handle == Upper ? 1 : 0].center();
  const int w = 72;
  const int h = 22;
  const int left =
      std::clamp(static_cast<int>(center.x() - w / 2.0), 2, std::max(2, width() - w - 2));
  const int top =
      std::clamp(static_cast<int>(center.y() - h / 2.0), 2, std::max(2, height() - h - 2));
  inlineEditor_->setGeometry(left, top, w, h);
  inlineEditor_->setText(
      QString::number(handle == Upper ? clipMax_ : clipMin_, 'g', QLocale::FloatingPointShortest));
  inlineEditor_->setVisible(true);
  inlineEditor_->setFocus();
  inlineEditor_->selectAll();
}

void ColorBarWidget::commitInlineEditor() {
  if (!inlineEditor_->isVisible() || editHandle_ == None)
    return;
  // Cleared first: moving the focus below makes Qt 5 emit editingFinished again.
  const Handle handle = std::exchange(editHandle_, None);

  bool ok = false;
  const double entered = QLocale::c().toDouble(inlineEditor_->text(), &ok);
  const bool typed = ok && inlineEditor_->isModified();
  // Hiding the focused editor would hand the focus on down the focus chain, such as
  // to the Color panel's Min field, instead of back to the view.
  if (inlineEditor_->hasFocus() && parentWidget()) {
    parentWidget()->setFocus(Qt::OtherFocusReason);
  }
  inlineEditor_->setVisible(false);
  if (!typed) {
    return;
  }

  setClipValue(handle, entered);
  const Bound bound = handle == Upper ? Bound::Upper : Bound::Lower;
  const double accepted = handle == Upper ? clipMax_ : clipMin_;
  const ValueFormat format = accepted == entered ? ValueFormat::Exact : ValueFormat::Rounded;
  setClipValueFormat(bound, format);
  emit clipValueFormatChanged(bound, format);
}

void ColorBarWidget::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // A strip as short as the minimal form's rounds into a capsule.
  const double radius = std::min<double>(overlay::kPanelRadius, height() / 2.0);
  p.setPen(QPen(overlay::solidBorder(overlay::kSurfaceBorder), 1.0));
  p.setBrush(overlay::kSurface);
  p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

  const double padding = pad();
  const double textWidth = width() - 2.0 * padding;
  const QFont title = titleFont();
  const QFontMetricsF titleMetrics(title);
  const QFont small = smallFont();
  const QFontMetricsF smallMetrics(small);
  if (form_ == LegendForm::Full) {
    p.setFont(title);
    p.setPen(overlay::kText);
    p.drawText(QRectF(kPad, kPad, textWidth, titleMetrics.height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               titleMetrics.elidedText(title_, Qt::ElideMiddle, textWidth));
    if (!subtitle_.isEmpty()) {
      p.setFont(small);
      p.setPen(overlay::kTextSecondary);
      p.drawText(
          QRectF(
              kPad, kPad + titleMetrics.height() + kSubtitleGap, textWidth, smallMetrics.height()),
          Qt::AlignLeft | Qt::AlignVCenter,
          smallMetrics.elidedText(subtitle_, Qt::ElideRight, textWidth));
    }
  } else if (form_ == LegendForm::Compact) {
    // The name, then its kind on the same baseline while both fit; the kind goes first.
    const double baseline =
        kCompactPad + (headerHeight() + titleMetrics.ascent() - titleMetrics.descent()) / 2.0;
    const QString kind = subtitle_.isEmpty() ? QString() : QStringLiteral(" · ") + subtitle_;
    const double nameWidth = titleMetrics.horizontalAdvance(title_);
    const bool withKind =
        !kind.isEmpty() && nameWidth + smallMetrics.horizontalAdvance(kind) <= textWidth;
    p.setFont(title);
    p.setPen(overlay::kText);
    p.drawText(QPointF(kCompactPad, baseline),
               titleMetrics.elidedText(title_, Qt::ElideMiddle, textWidth));
    if (withKind) {
      p.setFont(small);
      p.setPen(overlay::kTextSecondary);
      p.drawText(QPointF(kCompactPad + nameWidth, baseline), kind);
    }
  }

  if (categorical_) {
    const CategoryLayout layout = categoryLayout();
    const QFont label = form_ == LegendForm::Full ? labelFont() : small;
    const QFontMetricsF labelMetrics(label);
    p.setFont(label);
    for (int i = 0; i < static_cast<int>(catEntries_.size()); ++i) {
      const QRectF target = layout.entries[static_cast<size_t>(i)];
      if (target.isEmpty()) {
        continue;
      }
      const auto& [text, color] = catEntries_[static_cast<size_t>(i)];
      const bool hovered = i == hoverEntry_;
      if (form_ == LegendForm::Compact || (hovered && form_ == LegendForm::Full)) {
        p.setPen(Qt::NoPen);
        p.setBrush(form_ == LegendForm::Full ? overlay::kHover
                   : hovered                 ? overlay::kSelected
                                             : overlay::kBadgeFill);
        if (form_ == LegendForm::Full) {
          p.drawRoundedRect(target.adjusted(0.0, 1.0, 0.0, -1.0), 7.0, 7.0);
        } else {
          p.drawRoundedRect(target, kChipRadius, kChipRadius);
        }
      }
      const QRectF swatch = swatchRect(layout, i);
      const bool outlined = hovered && form_ == LegendForm::Minimal;
      p.setPen(QPen(outlined ? QColor(255, 255, 255, 220) : QColor(255, 255, 255, 50), 1.0));
      p.setBrush(color);
      const double corner = form_ == LegendForm::Compact ? 3.0 : 4.0;
      p.drawRoundedRect(swatch, corner, corner);
      if (form_ == LegendForm::Minimal) {
        continue;
      }
      const double gap = form_ == LegendForm::Full ? kSwatchGap : kChipSwatchGap;
      const double end = form_ == LegendForm::Full ? 4.0 : kChipPad;
      const QRectF textRect(swatch.right() + gap,
                            target.top(),
                            std::max(0.0, target.right() - swatch.right() - gap - end),
                            target.height());
      p.setPen(overlay::kText);
      p.drawText(textRect,
                 Qt::AlignLeft | Qt::AlignVCenter,
                 labelMetrics.elidedText(text, Qt::ElideRight, textRect.width()));
    }
    if (layout.hidden > 0) {
      p.setPen(Qt::NoPen);
      p.setBrush(overlay::kBadgeFill);
      const double corner = std::min(kChipRadius, layout.more.height() / 2.0);
      p.drawRoundedRect(layout.more, corner, corner);
      p.setFont(small);
      p.setPen(overlay::kTextSecondary);
      p.drawText(layout.more, Qt::AlignCenter, QStringLiteral("+%1").arg(layout.hidden));
    }
    return;
  }

  // Show the full range, clamping or graying values outside the clip window.
  const QRectF bar = barRect();
  {
    QLinearGradient grad(bar.topLeft(), bar.topRight());
    // Matches the mapper: the colormap spans the clip window.
    constexpr int kStops = 128;
    const double clipSpan = std::max(clipMax_ - clipMin_, 1e-12);
    const bool gray = colormap_.outside == OutOfRange::Gray;
    const auto colorAt = [&](double value) {
      const std::array<double, 3> rgb =
          gray && (value < clipMin_ || value > clipMax_)
              ? kOutOfRangeGray
              : sampleColormap(*colormap_.map,
                               (std::clamp(value, clipMin_, clipMax_) - clipMin_) / clipSpan,
                               colormap_.reversed);
      return QColor::fromRgbF(static_cast<QColorComponent>(rgb[0]),
                              static_cast<QColorComponent>(rgb[1]),
                              static_cast<QColorComponent>(rgb[2]));
    };
    const double span = std::max(globalMax_ - globalMin_, 1e-12);
    for (int i = 0; i <= kStops; ++i) {
      const double pos = static_cast<double>(i) / kStops;
      grad.setColorAt(pos, colorAt(globalMin_ + pos * span));
    }
    // Gray meets the colormap at a hard edge, which the evenly spaced stops would blur.
    constexpr double kEdge = 1e-4;
    const double lowerPos = (clipMin_ - globalMin_) / span;
    const double upperPos = (clipMax_ - globalMin_) / span;
    if (gray && lowerPos > kEdge) {
      grad.setColorAt(lowerPos - kEdge, colorAt(globalMin_));
      grad.setColorAt(lowerPos, colorAt(clipMin_));
    }
    if (gray && upperPos < 1.0 - kEdge) {
      grad.setColorAt(upperPos, colorAt(clipMax_));
      grad.setColorAt(upperPos + kEdge, colorAt(globalMax_));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawRoundedRect(bar, 3.0, 3.0);
  }

  const double lowerX = valueToX(clipMin_);
  const double upperX = valueToX(clipMax_);
  const double knob = knobHeight();
  const auto drawKnob = [&](double x, Handle which) {
    const bool active = hoverHandle_ == which || dragHandle_ == which;
    p.setPen(active ? QPen(overlay::kAccent, 2.0) : QPen(QColor(0, 0, 0, 150), 1.0));
    p.setBrush(Qt::white);
    p.drawRoundedRect(
        QRectF(x - kKnobWidth / 2.0, bar.center().y() - knob / 2.0, kKnobWidth, knob), 3.0, 3.0);
  };
  drawKnob(lowerX, Lower);
  drawKnob(upperX, Upper);

  const auto valueRects = clipValueRects();
  if (valueRects[0].isEmpty()) {
    return;
  }
  p.setFont(small);
  p.setPen(overlay::kText);
  p.drawText(valueRects[0], Qt::AlignLeft, valueText(clipMin_));
  p.drawText(valueRects[1], Qt::AlignLeft, valueText(clipMax_));
  if (form_ != LegendForm::Full) {
    return;
  }

  const QRectF body = bodyRect();
  // The data range at the bar's ends, once a handle has left that end and its
  // label leaves room.
  p.setPen(overlay::kTextSecondary);
  const auto drawEnd = [&](double value, bool left) {
    const QString text = valueText(value);
    const double endWidth = smallMetrics.horizontalAdvance(text);
    const QRectF rect(left ? body.left() : body.right() - endWidth,
                      valueRects[0].top(),
                      endWidth,
                      smallMetrics.height());
    const QRectF clearance = rect.adjusted(-kLabelSpacing, 0.0, kLabelSpacing, 0.0);
    if (!clearance.intersects(valueRects[0]) && !clearance.intersects(valueRects[1])) {
      p.drawText(rect, Qt::AlignLeft, text);
    }
  };
  if (clipMin_ > globalMin_) {
    drawEnd(globalMin_, true);
  }
  if (clipMax_ < globalMax_) {
    drawEnd(globalMax_, false);
  }
}

void ColorBarWidget::showCategoryMenu(int entry, const QPoint& globalPos) {
  // The menu runs its own event loop, which may replace the entries: copy this
  // one and recheck the index afterwards.
  const auto [label, color] = catEntries_[static_cast<size_t>(entry)];
  const std::optional<CategoryMenuPick> pick =
      execCategoryMenu(this, label, color, globalPos, palettes_, currentPalette_);
  if (!pick || static_cast<size_t>(entry) >= catEntries_.size()) {
    return;
  }
  switch (pick->kind) {
  case CategoryMenuPick::Kind::Color:
    emit categoryColorChanged(entry, pick->color);
    break;
  case CategoryMenuPick::Kind::Reset:
    emit categoryColorChanged(entry, std::nullopt);
    break;
  case CategoryMenuPick::Kind::Palette:
    emit paletteChosen(pick->palette);
    break;
  }
}

void ColorBarWidget::showOverflowMenu(const CategoryLayout& layout, const QPoint& globalPos) {
  QMenu menu(this);
  overlay::styleMenu(&menu);
  for (size_t i = 0; i < catEntries_.size(); ++i) {
    if (layout.entries[i].isEmpty()) {
      const auto& [label, color] = catEntries_[i];
      QAction* action = menu.addAction(paletteIcon({color}, QSize(14, 14)), label);
      action->setData(static_cast<int>(i));
    }
  }
  if (QAction* chosen = menu.exec(globalPos)) {
    const int entry = chosen->data().toInt();
    if (entry >= 0 && static_cast<size_t>(entry) < catEntries_.size()) {
      showCategoryMenu(entry, globalPos);
    }
  }
}

void ColorBarWidget::showColormapMenu(const QPoint& globalPos) {
  QMenu menu(this);
  overlay::styleMenu(&menu);
  QActionGroup choices(&menu);
  for (const ColormapGroup group :
       {ColormapGroup::Sequential, ColormapGroup::Diverging, ColormapGroup::Cyclic}) {
    QMenu* section = menu.addMenu(colormapGroupName(group));
    overlay::styleMenu(section);
    for (const Colormap& map : colormaps()) {
      if (map.group != group) {
        continue;
      }
      QAction* action = section->addAction(
          QString::fromUtf8(map.label.data(), static_cast<int>(map.label.size())));
      action->setCheckable(true);
      choices.addAction(action);
      action->setChecked(&map == colormap_.map);
      connect(action, &QAction::triggered, this, [this, id = map.id]() {
        emit colormapChosen(QString::fromUtf8(id.data(), static_cast<int>(id.size())),
                            colormap_.reversed);
      });
    }
  }
  menu.addSeparator();
  QAction* reverse = menu.addAction(QStringLiteral("Reverse"));
  reverse->setCheckable(true);
  reverse->setChecked(colormap_.reversed);
  connect(reverse, &QAction::triggered, this, [this](bool checked) {
    emit colormapChosen(
        QString::fromUtf8(colormap_.map->id.data(), static_cast<int>(colormap_.map->id.size())),
        checked);
  });
  menu.exec(globalPos);
}

bool ColorBarWidget::event(QEvent* ev) {
  if (ev->type() != QEvent::ToolTip) {
    return QWidget::event(ev);
  }
  const auto* help = static_cast<QHelpEvent*>(ev);
  const QPointF pos = help->pos();
  const CategoryLayout layout = categorical_ ? categoryLayout() : CategoryLayout();
  const int entry = categorical_ ? categoryAt(layout, pos) : -1;
  const Handle value = hitTestValue(pos);
  QString tip;
  if (entry >= 0) {
    const QString& label = catEntries_[static_cast<size_t>(entry)].first;
    tip = form_ == LegendForm::Full ? QStringLiteral("Click the swatch to recolor %1").arg(label)
                                    : QStringLiteral("Click to recolor %1").arg(label);
  } else if (layout.hidden > 0 && layout.more.contains(pos)) {
    tip = QStringLiteral("Click to edit: %1").arg(hiddenCategories(layout));
  } else if (value != None) {
    tip = QStringLiteral("%1\nDouble-click to edit this clip value")
              .arg(QString::number(
                  value == Upper ? clipMax_ : clipMin_, 'g', QLocale::FloatingPointShortest));
  } else if (hitTestHandle(pos) != None) {
    tip = QStringLiteral("Drag to clip the color range");
  } else if (!categorical_ && barRect().contains(pos)) {
    tip = QStringLiteral("Click to choose a colormap");
  } else if (form_ != LegendForm::Full || pos.y() < bodyTop()) {
    // The smaller forms shorten or leave out the name and the kind.
    tip = form_ == LegendForm::Full || subtitle_.isEmpty()
              ? title_
              : QStringLiteral("%1\n%2").arg(title_, subtitle_);
  }
  if (tip.isEmpty()) {
    QToolTip::hideText();
  } else {
    QToolTip::showText(help->globalPos(), tip, this);
  }
  return true;
}

void ColorBarWidget::contextMenuEvent(QContextMenuEvent* ev) {
  if (!categorical_) {
    ev->accept();
    showColormapMenu(ev->globalPos());
    return;
  }
  const CategoryLayout layout = categoryLayout();
  if (layout.hidden > 0 && layout.more.contains(ev->pos())) {
    ev->accept();
    showOverflowMenu(layout, ev->globalPos());
    return;
  }
  const int entry = categoryAt(layout, ev->pos());
  if (entry < 0) {
    QWidget::contextMenuEvent(ev);
    return;
  }
  ev->accept();
  showCategoryMenu(entry, ev->globalPos());
}

void ColorBarWidget::mousePressEvent(QMouseEvent* ev) {
  commitInlineEditor();
  const QPointF pos = mouseLocalPos(ev);
  if (categorical_) {
    const CategoryLayout layout = categoryLayout();
    const int entry = categoryAt(layout, pos);
    // The Full form's rows are wider than their swatches; a chip is a button as a whole.
    if (ev->button() == Qt::LeftButton && entry >= 0 &&
        (form_ != LegendForm::Full || swatchRect(layout, entry).contains(pos))) {
      showCategoryMenu(entry, mapToGlobal(swatchRect(layout, entry).bottomLeft().toPoint()));
    }
    if (ev->button() == Qt::LeftButton && layout.hidden > 0 && layout.more.contains(pos)) {
      showOverflowMenu(layout, mapToGlobal(layout.more.bottomLeft().toPoint()));
    }
    // A right press is taken here; contextMenuEvent opens the menu.
    ev->accept();
    return;
  }
  if (ev->button() == Qt::LeftButton) {
    const Handle h = hitTestHandle(pos);
    if (h != None) {
      dragHandle_ = h;
      update();
    } else if (barRect().contains(pos)) {
      showColormapMenu(mapToGlobal(pos.toPoint()));
    }
  }
  ev->accept();
}

void ColorBarWidget::mouseMoveEvent(QMouseEvent* ev) {
  const QPointF pos = mouseLocalPos(ev);
  if (dragHandle_ != None) {
    setClipValue(dragHandle_, xToValue(pos.x()));
    ev->accept();
    return;
  }

  const Handle handle = hitTestHandle(pos);
  int entry = -1;
  bool clickable = !categorical_ && barRect().contains(pos);
  if (categorical_) {
    const CategoryLayout layout = categoryLayout();
    entry = categoryAt(layout, pos);
    clickable =
        (entry >= 0 && (form_ != LegendForm::Full || swatchRect(layout, entry).contains(pos))) ||
        (layout.hidden > 0 && layout.more.contains(pos));
  }
  setCursor(hitTestValue(pos) != None     ? Qt::IBeamCursor
            : handle != None || clickable ? Qt::PointingHandCursor
                                          : Qt::ArrowCursor);
  if (handle != hoverHandle_ || entry != hoverEntry_) {
    hoverHandle_ = handle;
    hoverEntry_ = entry;
    update();
  }
  ev->accept();
}

void ColorBarWidget::mouseReleaseEvent(QMouseEvent* ev) {
  if (ev->button() == Qt::LeftButton && dragHandle_ != None) {
    dragHandle_ = None;
    hoverHandle_ = hitTestHandle(mouseLocalPos(ev));
    update();
  }
  ev->accept();
}

void ColorBarWidget::mouseDoubleClickEvent(QMouseEvent* ev) {
  if (ev->button() == Qt::LeftButton) {
    showInlineEditor(hitTestValue(mouseLocalPos(ev)));
  }
  ev->accept();
}

void ColorBarWidget::leaveEvent(QEvent* ev) {
  hoverHandle_ = None;
  hoverEntry_ = -1;
  setCursor(Qt::ArrowCursor);
  update();
  QWidget::leaveEvent(ev);
}

QIcon paletteIcon(const std::vector<QColor>& colors, const QSize& size) {
  QIcon icon;
  if (colors.empty()) {
    return icon;
  }
  for (const int scale : {1, 2, 3}) {
    QPixmap pixmap(size * scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(pixmap.rect()), 3.0 * scale, 3.0 * scale);
    painter.setClipPath(clip);
    const double width = static_cast<double>(pixmap.width()) / static_cast<double>(colors.size());
    for (size_t i = 0; i < colors.size(); ++i) {
      // Each band reaches the next one's start, so no seam shows between them.
      painter.fillRect(QRectF(static_cast<double>(i) * width, 0.0, width + 1.0, pixmap.height()),
                       colors[i]);
    }
    painter.end();
    icon.addPixmap(pixmap);
  }
  return icon;
}

std::optional<CategoryMenuPick> execCategoryMenu(QWidget* parent,
                                                 const QString& label,
                                                 const QColor& current,
                                                 const QPoint& globalPos,
                                                 const std::vector<PaletteOption>& palettes,
                                                 const QString& currentPalette) {
  const QString title = QStringLiteral("Color for ") + label;
  QMenu menu(parent);
  overlay::styleMenu(&menu);
  menu.setStyleSheet(menu.styleSheet() +
                     QStringLiteral("QLabel { color: %1; }"
                                    "QToolButton { border: 2px solid #4A4A4A; border-radius: 4px; }"
                                    "QToolButton:hover { border-color: #B0B0B0; }"
                                    "QToolButton:checked { border-color: white; }")
                         .arg(overlay::css(overlay::kTextSecondary)));

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
  const std::vector<std::array<double, 3>>& tab20 = findCategoryPalette("tab20")->colors;
  const auto tab20Color = [&tab20](int index) {
    const std::array<double, 3>& rgb = tab20[static_cast<size_t>(index)];
    return QColor::fromRgbF(static_cast<QColorComponent>(rgb[0]),
                            static_cast<QColorComponent>(rgb[1]),
                            static_cast<QColorComponent>(rgb[2]));
  };
  for (int column = 0; column < kSwatchColumns; ++column) {
    addSwatch(0, column, tab20Color(2 * column));
    addSwatch(1, column, tab20Color(2 * column + 1));
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
  std::vector<std::pair<const QAction*, QString>> paletteActions;
  if (!palettes.empty()) {
    menu.addSeparator();
    QMenu* submenu = menu.addMenu(QStringLiteral("Palette"));
    overlay::styleMenu(submenu);
    auto* choices = new QActionGroup(submenu);
    for (const PaletteOption& palette : palettes) {
      QAction* action = submenu->addAction(palette.label);
      action->setToolTip(palette.detail);
      action->setCheckable(true);
      choices->addAction(action);
      action->setChecked(palette.id == currentPalette);
      paletteActions.emplace_back(action, palette.id);
    }
    submenu->setToolTipsVisible(true);
  }
  const QAction* chosen = menu.exec(globalPos);
  if (chosen == more) {
    picked = QColorDialog::getColor(current, parent, title);
  }
  if (chosen == reset) {
    return CategoryMenuPick{CategoryMenuPick::Kind::Reset, {}, {}};
  }
  for (const auto& [action, id] : paletteActions) {
    if (chosen == action) {
      return CategoryMenuPick{CategoryMenuPick::Kind::Palette, {}, id};
    }
  }
  if (picked.isValid()) {
    return CategoryMenuPick{CategoryMenuPick::Kind::Color, picked, {}};
  }
  return std::nullopt;
}

QImage snapshotWithLegends(QImage frame,
                           qreal devicePixelRatio,
                           const std::vector<ColorBarWidget*>& legends,
                           const std::vector<QWidget*>& panels) {
  frame.setDevicePixelRatio(devicePixelRatio);
  QPainter painter(&frame);
  for (QWidget* panel : panels) {
    if (panel && panel->isVisible()) {
      panel->render(&painter, panel->pos(), QRegion(), QWidget::DrawChildren);
    }
  }
  for (ColorBarWidget* legend : legends) {
    if (legend && legend->isVisible()) {
      // Without DrawWindowBackground: by default render fills the whole rectangle with the
      // palette's window color first, a square behind the card's rounded corners.
      legend->render(&painter, legend->pos(), QRegion(), QWidget::DrawChildren);
    }
  }
  return frame;
}
