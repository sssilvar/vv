#include "ColorPanel.h"

#include "ColorBarWidget.h"
#include "OverlayStyle.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QDoubleValidator>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace {

constexpr int kColumns = 3;
constexpr int kPadding = 12;
constexpr int kSpacing = 4;
constexpr int kButtonWidth = 92;
constexpr int kButtonHeight = 46;
constexpr QSize kGradientSize(76, 12);

constexpr int kCategoryWidth = 88; // three columns and a scroll bar fit under the tiles
constexpr int kCategoryHeight = 24;
constexpr int kCategoryRows = 7; // shown before the list scrolls: every category of most fields
constexpr int kCategorySwatch = 12;
constexpr int kCategoryGap = 4; // added to the gap QToolButton leaves after its icon

constexpr int kHistogramHeight = 64;
constexpr double kKnobWidth = 10.0;
constexpr double kKnobHeight = 16.0;
constexpr double kHandleTolerance = 8.0;

QString fromView(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

QColor toQColor(const std::array<double, 3>& rgb) {
  return QColor::fromRgbF(static_cast<QColorComponent>(rgb[0]),
                          static_cast<QColorComponent>(rgb[1]),
                          static_cast<QColorComponent>(rgb[2]));
}

QString formatBound(double value) {
  return QString::number(value, 'g', 6);
}

// A category's color as a small rounded square, then the gap before its label; the rim
// keeps a dark color apart from the panel.
QIcon categorySwatch(const QColor& color) {
  QIcon icon;
  for (const int scale : {1, 2, 3}) {
    QPixmap pixmap(QSize(kCategorySwatch + kCategoryGap, kCategorySwatch) * scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(255, 255, 255, 60), scale));
    painter.setBrush(color);
    const double inset = scale / 2.0;
    const QRectF square(0.0, 0.0, kCategorySwatch * scale, kCategorySwatch * scale);
    painter.drawRoundedRect(
        square.adjusted(inset, inset, -inset, -inset), 3.0 * scale, 3.0 * scale);
    painter.end();
    icon.addPixmap(pixmap);
  }
  return icon;
}

} // namespace

QString colormapGroupName(ColormapGroup group) {
  switch (group) {
  case ColormapGroup::Sequential:
    return QStringLiteral("Sequential");
  case ColormapGroup::Diverging:
    return QStringLiteral("Diverging");
  case ColormapGroup::Cyclic:
    return QStringLiteral("Cyclic");
  }
  return {};
}

QIcon colormapIcon(const Colormap& colormap, bool reversed, const QSize& size) {
  constexpr int kStops = 32;
  QIcon icon;
  for (const int scale : {1, 2, 3}) {
    QPixmap pixmap(size * scale);
    pixmap.fill(Qt::transparent);
    QLinearGradient gradient(0.0, 0.0, pixmap.width(), 0.0);
    for (int i = 0; i <= kStops; ++i) {
      const double t = static_cast<double>(i) / kStops;
      gradient.setColorAt(t, toQColor(sampleColormap(colormap, t, reversed)));
    }
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(pixmap.rect()), 3.0 * scale, 3.0 * scale);
    painter.end();
    icon.addPixmap(pixmap);
  }
  return icon;
}

RangeHistogram::RangeHistogram(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void RangeHistogram::setRange(double min, double max, std::vector<size_t> counts) {
  range_ = {min, std::max(min, max)};
  counts_ = std::move(counts);
  setClipRange(clip_[0], clip_[1]);
}

void RangeHistogram::setClipRange(double lower, double upper) {
  lower = std::clamp(lower, range_[0], range_[1]);
  upper = std::clamp(upper, range_[0], range_[1]);
  clip_ = {std::min(lower, upper), std::max(lower, upper)};
  update();
}

void RangeHistogram::setColormap(const ColormapChoice& colormap) {
  colormap_ = colormap;
  update();
}

QSize RangeHistogram::sizeHint() const {
  return {200, kHistogramHeight};
}

QRectF RangeHistogram::plotRect() const {
  const double inset = kKnobWidth / 2.0 + 1.0;
  return {inset,
          2.0,
          std::max(1.0, width() - 2.0 * inset),
          std::max(1.0, height() - kKnobHeight / 2.0 - 4.0)};
}

double RangeHistogram::valueToX(double value) const {
  const QRectF plot = plotRect();
  const double span = range_[1] - range_[0];
  const double t = span > 0.0 ? std::clamp((value - range_[0]) / span, 0.0, 1.0) : 0.5;
  return plot.left() + t * plot.width();
}

double RangeHistogram::xToValue(double x) const {
  const QRectF plot = plotRect();
  const double t = std::clamp((x - plot.left()) / plot.width(), 0.0, 1.0);
  return range_[0] + t * (range_[1] - range_[0]);
}

RangeHistogram::Handle RangeHistogram::handleAt(const QPointF& pos) const {
  const double lower = std::abs(pos.x() - valueToX(clip_[0]));
  const double upper = std::abs(pos.x() - valueToX(clip_[1]));
  if (lower > kHandleTolerance && upper > kHandleTolerance) {
    return Handle::None;
  }
  // Stacked handles: the side the pointer is on decides, so either can move off the other.
  if (std::abs(lower - upper) < 1e-9) {
    return pos.x() >= valueToX(clip_[1]) ? Handle::Upper : Handle::Lower;
  }
  return lower < upper ? Handle::Lower : Handle::Upper;
}

void RangeHistogram::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF plot = plotRect();

  const size_t peak = counts_.empty() ? 0 : *std::max_element(counts_.begin(), counts_.end());
  if (peak > 0) {
    const double barWidth = plot.width() / static_cast<double>(counts_.size());
    const double clipSpan = std::max(clip_[1] - clip_[0], 1e-12);
    const QColor dimmed(255, 255, 255, 46);
    p.setPen(Qt::NoPen);
    for (size_t i = 0; i < counts_.size(); ++i) {
      if (counts_[i] == 0) {
        continue;
      }
      const double left = plot.left() + static_cast<double>(i) * barWidth;
      const double center = xToValue(left + barWidth / 2.0);
      const bool inside = center >= clip_[0] && center <= clip_[1];
      p.setBrush(inside ? toQColor(sampleColormap(
                              *colormap_.map, (center - clip_[0]) / clipSpan, colormap_.reversed))
                        : dimmed);
      // An occupied bin keeps at least a sliver, so a sparse tail stays visible.
      const double height = std::max(
          1.0, plot.height() * static_cast<double>(counts_[i]) / static_cast<double>(peak));
      p.drawRect(QRectF(left + 0.5, plot.bottom() - height, barWidth - 1.0, height));
    }
  }
  p.setPen(QPen(overlay::kSurfaceBorder, 1.0));
  p.drawLine(plot.bottomLeft(), plot.bottomRight());

  for (const Handle handle : {Handle::Lower, Handle::Upper}) {
    const double x = valueToX(handle == Handle::Lower ? clip_[0] : clip_[1]);
    p.setPen(QPen(QColor(255, 255, 255, 150), 1.0));
    p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    const bool active = hover_ == handle || drag_ == handle;
    p.setPen(active ? QPen(overlay::kAccent, 2.0) : QPen(QColor(0, 0, 0, 150), 1.0));
    p.setBrush(Qt::white);
    p.drawRoundedRect(
        QRectF(x - kKnobWidth / 2.0, plot.bottom() - kKnobHeight / 2.0, kKnobWidth, kKnobHeight),
        3.0,
        3.0);
  }
}

void RangeHistogram::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    drag_ = handleAt(mouseLocalPos(event));
    update();
  }
  event->accept();
}

void RangeHistogram::mouseMoveEvent(QMouseEvent* event) {
  const QPointF pos = mouseLocalPos(event);
  if (drag_ != Handle::None) {
    const double value = xToValue(pos.x());
    if (drag_ == Handle::Upper) {
      clip_[1] = std::clamp(value, clip_[0], range_[1]);
    } else {
      clip_[0] = std::clamp(value, range_[0], clip_[1]);
    }
    update();
    emit clipRangeChanged(clip_[0], clip_[1]);
    event->accept();
    return;
  }
  const Handle handle = handleAt(pos);
  setCursor(handle == Handle::None ? Qt::ArrowCursor : Qt::SizeHorCursor);
  if (handle != hover_) {
    hover_ = handle;
    update();
  }
  event->accept();
}

void RangeHistogram::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && drag_ != Handle::None) {
    drag_ = Handle::None;
    hover_ = handleAt(mouseLocalPos(event));
    update();
  }
  event->accept();
}

void RangeHistogram::leaveEvent(QEvent* event) {
  hover_ = Handle::None;
  setCursor(Qt::ArrowCursor);
  update();
  QWidget::leaveEvent(event);
}

ColorPanel::ColorPanel(QWidget* parent) : QWidget(parent) {
  setObjectName("colorPanel");
  setAttribute(Qt::WA_StyledBackground, true);
  // A press on the panel's padding must not reach the viewport and start a rotation.
  setAttribute(Qt::WA_NoMousePropagation, true);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::ArrowCursor);
  setStyleSheet(
      overlay::panelStyleSheet(objectName()) +
      QStringLiteral(
          "QScrollArea#colorPanelScroll, QWidget#colorPanelContent { background: transparent; }"
          "QToolButton#colorPanelLink { color: %1; padding: 1px 4px; border-radius: 6px; }"
          "QLineEdit { background: %2; color: %3; font-size: 12px; border: 1px solid %4;"
          "  border-radius: 6px; padding: 2px 6px; }"
          "QLineEdit:focus { border-color: %1; }"
          "QScrollArea#colorPanelCategories, QWidget#colorPanelCategoryList {"
          "  background: transparent; }"
          "QToolButton#colorPanelCategory { color: %3; font-size: 12px; padding: 0 4px;"
          "  border-radius: 6px; }")
          .arg(overlay::css(overlay::kAccent),
               overlay::css(overlay::kHover),
               overlay::css(overlay::kText),
               overlay::css(overlay::solidBorder(overlay::kSurfaceBorder, overlay::kHover))));

  // Scrolls only when the viewport is too short for the whole panel.
  auto* frame = new QVBoxLayout(this);
  frame->setContentsMargins(1, 1, 1, 1);
  scroll_ = new QScrollArea(this);
  scroll_->setObjectName("colorPanelScroll");
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll_->setWidgetResizable(true);
  scroll_->setFocusPolicy(Qt::NoFocus);
  scroll_->viewport()->setAutoFillBackground(false);
  auto* content = new QWidget(scroll_);
  content->setObjectName("colorPanelContent");
  frame->addWidget(scroll_);

  auto* column = new QVBoxLayout(content);
  column->setContentsMargins(
      kPadding - 1, kPadding - 1, kPadding - 1, kPadding - 1); // inside the 1px frame
  column->setSpacing(kSpacing);

  auto* header = new QHBoxLayout;
  auto* titles = new QVBoxLayout;
  titles->setSpacing(2);
  auto* title = new QLabel(QStringLiteral("Color"), content);
  title->setObjectName("panelTitle");
  titles->addWidget(title);
  field_ = new QLabel(content);
  titles->addWidget(field_);
  header->addLayout(titles, 1);
  reverse_ = new QCheckBox(QStringLiteral("Reverse"), content);
  reverse_->setToolTip(QStringLiteral("Flip the colormap end to end"));
  // Keeps focus on the viewport, so Space keeps cycling fields after a click.
  reverse_->setFocusPolicy(Qt::NoFocus);
  header->addWidget(reverse_, 0, Qt::AlignTop);
  column->addLayout(header);

  // A continuous field's sections, then a categorical field's; one of them shows.
  const auto addKind = [&]() {
    auto* kind = new QWidget(content);
    auto* sections = new QVBoxLayout(kind);
    sections->setContentsMargins(0, 0, 0, 0);
    sections->setSpacing(kSpacing);
    column->addWidget(kind);
    return std::pair{kind, sections};
  };
  const auto addSection = [](QVBoxLayout* sections, const QString& name) {
    sections->addSpacing(kSpacing);
    auto* row = new QHBoxLayout;
    auto* label = new QLabel(name, sections->parentWidget());
    label->setObjectName("panelSection");
    row->addWidget(label);
    row->addStretch(1);
    sections->addLayout(row);
    return row;
  };
  QVBoxLayout* continuousSections = nullptr;
  QVBoxLayout* categoricalSections = nullptr;
  std::tie(continuous_, continuousSections) = addKind();
  std::tie(categorical_, categoricalSections) = addKind();
  categorical_->setVisible(false);

  addSection(categoricalSections, QStringLiteral("Palette"));
  paletteGrid_ = new QGridLayout;
  paletteGrid_->setSpacing(kSpacing);
  paletteGrid_->setAlignment(Qt::AlignLeft);
  categoricalSections->addLayout(paletteGrid_);
  paletteTiles_ = new QButtonGroup(this);

  addSection(categoricalSections, QStringLiteral("Categories"));
  categoryScroll_ = new QScrollArea(categorical_);
  categoryScroll_->setObjectName("colorPanelCategories");
  categoryScroll_->setFrameShape(QFrame::NoFrame);
  categoryScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  categoryScroll_->setWidgetResizable(true);
  categoryScroll_->setFocusPolicy(Qt::NoFocus);
  categoryScroll_->viewport()->setAutoFillBackground(false);
  auto* categoryList = new QWidget(categoryScroll_);
  categoryList->setObjectName("colorPanelCategoryList");
  categoryGrid_ = new QGridLayout(categoryList);
  categoryGrid_->setContentsMargins(0, 0, 0, 0);
  categoryGrid_->setSpacing(kSpacing);
  categoryGrid_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  categoryScroll_->setWidget(categoryList);
  categoricalSections->addWidget(categoryScroll_);

  buttons_ = new QButtonGroup(this);
  const std::vector<Colormap>& colormapList = colormaps();
  for (const ColormapGroup group :
       {ColormapGroup::Sequential, ColormapGroup::Diverging, ColormapGroup::Cyclic}) {
    addSection(continuousSections, colormapGroupName(group));
    auto* grid = new QGridLayout;
    grid->setSpacing(kSpacing);
    // Left-aligned, so a short group keeps the columns of a full one.
    grid->setAlignment(Qt::AlignLeft);
    continuousSections->addLayout(grid);
    int slot = 0;
    for (size_t index = 0; index < colormapList.size(); ++index) {
      if (colormapList[index].group != group) {
        continue;
      }
      auto* button = new QToolButton(continuous_);
      button->setText(fromView(colormapList[index].label));
      button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
      button->setIconSize(kGradientSize);
      button->setFixedSize(kButtonWidth, kButtonHeight);
      button->setCheckable(true);
      button->setFocusPolicy(Qt::NoFocus);
      grid->addWidget(button, slot / kColumns, slot % kColumns);
      buttons_->addButton(button, static_cast<int>(index));
      ++slot;
    }
  }
  buttons_->button(0)->setChecked(true);
  updatePreviews();

  QHBoxLayout* rangeHeader = addSection(continuousSections, QStringLiteral("Range"));
  auto* dataRange = new QToolButton(continuous_);
  dataRange->setObjectName("colorPanelLink");
  dataRange->setText(QStringLiteral("Use data range"));
  dataRange->setToolTip(QStringLiteral("Color the field's whole range"));
  dataRange->setFocusPolicy(Qt::NoFocus);
  rangeHeader->addWidget(dataRange);

  histogram_ = new RangeHistogram(continuous_);
  histogram_->setToolTip(QStringLiteral("Drag a handle to clip the color range"));
  continuousSections->addWidget(histogram_);

  auto* bounds = new QHBoxLayout;
  bounds->setSpacing(6);
  const auto addBound = [&](const QString& name) {
    auto* label = new QLabel(name, continuous_);
    bounds->addWidget(label);
    auto* edit = new QLineEdit(continuous_);
    edit->setObjectName(QStringLiteral("colorPanel") + name);
    edit->setAccessibleName(name);
    auto* validator = new QDoubleValidator(edit);
    validator->setLocale(QLocale::c());
    edit->setValidator(validator);
    edit->installEventFilter(this);
    // The fields share whatever width the colormap grid leaves, never widening the panel.
    edit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    edit->setMinimumWidth(48);
    connect(edit, &QLineEdit::editingFinished, this, [this]() { commitBounds(); });
    bounds->addWidget(edit, 1);
    return edit;
  };
  min_ = addBound(QStringLiteral("Min"));
  bounds->addSpacing(6);
  max_ = addBound(QStringLiteral("Max"));
  continuousSections->addLayout(bounds);

  auto* outsideRow = new QHBoxLayout;
  outsideRow->addWidget(new QLabel(QStringLiteral("Outside range"), continuous_));
  outsideRow->addStretch(1);
  outside_ = new QButtonGroup(this);
  outsideRow->addWidget(overlay::segmentedControl(
      continuous_,
      outside_,
      {{static_cast<int>(OutOfRange::Clamp),
        QStringLiteral("Clamp"),
        QStringLiteral("Values outside the range take the colormap's end colors")},
       {static_cast<int>(OutOfRange::Gray),
        QStringLiteral("Gray"),
        QStringLiteral("Values outside the range are gray")}}));
  outside_->button(static_cast<int>(OutOfRange::Clamp))->setChecked(true);
  continuousSections->addLayout(outsideRow);

  scroll_->setWidget(content);

  connect(paletteTiles_, &QButtonGroup::idClicked, this, [this](int id) {
    emit paletteChosen(paletteIds_[static_cast<size_t>(id)]);
  });
  connect(buttons_, &QButtonGroup::idClicked, this, [this](int id) {
    emit colormapChosen(fromView(colormaps()[static_cast<size_t>(id)].id), reverse_->isChecked());
  });
  connect(reverse_, &QCheckBox::toggled, this, [this]() { updatePreviews(); });
  connect(reverse_, &QCheckBox::clicked, this, [this](bool reversed) {
    emit colormapChosen(fromView(colormaps()[static_cast<size_t>(buttons_->checkedId())].id),
                        reversed);
  });
  connect(outside_, &QButtonGroup::idClicked, this, [this](int id) {
    emit outOfRangeChosen(static_cast<OutOfRange>(id));
  });
  connect(dataRange, &QToolButton::clicked, this, [this]() {
    applyClip(histogram_->range()[0], histogram_->range()[1]);
    emit clipValueFormatChanged(ColorBarWidget::Bound::Lower, ColorBarWidget::ValueFormat::Rounded);
    emit clipValueFormatChanged(ColorBarWidget::Bound::Upper, ColorBarWidget::ValueFormat::Rounded);
  });
  connect(histogram_, &RangeHistogram::clipRangeChanged, this, [this](double lower, double upper) {
    showBounds(TypedText::Replace);
    emit clipRangeChanged(lower, upper);
  });
}

ColorPanel::~ColorPanel() {
  // Focus-out during QWidget teardown must not call into the destroyed derived object.
  QObject::disconnect(min_, nullptr, this, nullptr);
  QObject::disconnect(max_, nullptr, this, nullptr);
}

void ColorPanel::setField(const QString& name) {
  field_->ensurePolished();
  const int width =
      kColumns * kButtonWidth + (kColumns - 1) * kSpacing - reverse_->sizeHint().width() - kSpacing;
  field_->setText(field_->fontMetrics().elidedText(name, Qt::ElideMiddle, width));
  field_->setToolTip(name);
}

void ColorPanel::setColormap(const ColormapChoice& colormap) {
  showKind(Kind::Continuous);
  const auto index = static_cast<int>(colormap.map - colormaps().data());
  if (QAbstractButton* button = buttons_->button(index)) {
    button->setChecked(true);
  }
  reverse_->setChecked(colormap.reversed);
  outside_->button(static_cast<int>(colormap.outside))->setChecked(true);
  histogram_->setColormap(colormap);
}

void ColorPanel::setCategories(const std::vector<PaletteOption>& palettes,
                               const QString& current,
                               const std::vector<std::pair<QString, QColor>>& entries) {
  showKind(Kind::Categorical);
  // Tiles and rows are kept and refilled: this runs again while one of their clicks is
  // being handled.
  paletteIds_.clear();
  for (size_t index = 0; index < palettes.size(); ++index) {
    const PaletteOption& palette = palettes[index];
    QAbstractButton* tile = paletteTiles_->button(static_cast<int>(index));
    if (!tile) {
      auto* button = new QToolButton(categorical_);
      button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
      button->setIconSize(kGradientSize);
      button->setFixedSize(kButtonWidth, kButtonHeight);
      button->setCheckable(true);
      button->setFocusPolicy(Qt::NoFocus);
      paletteGrid_->addWidget(
          button, static_cast<int>(index) / kColumns, static_cast<int>(index) % kColumns);
      paletteTiles_->addButton(button, static_cast<int>(index));
      tile = button;
    }
    tile->setText(palette.label);
    tile->setToolTip(palette.detail);
    tile->setIcon(paletteIcon(palette.colors, kGradientSize));
    tile->setChecked(palette.id == current);
    tile->setVisible(true);
    paletteIds_.push_back(palette.id);
  }
  for (QAbstractButton* tile : paletteTiles_->buttons()) {
    if (static_cast<size_t>(paletteTiles_->id(tile)) >= palettes.size()) {
      tile->setVisible(false);
    }
  }

  entries_ = entries;
  for (size_t index = 0; index < entries.size(); ++index) {
    if (index == categoryRows_.size()) {
      auto* row = new QToolButton(categoryScroll_->widget());
      row->setObjectName("colorPanelCategory");
      row->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
      row->setIconSize(QSize(kCategorySwatch + kCategoryGap, kCategorySwatch));
      row->setFixedSize(kCategoryWidth, kCategoryHeight);
      row->setFocusPolicy(Qt::NoFocus);
      categoryGrid_->addWidget(
          row, static_cast<int>(index) / kColumns, static_cast<int>(index) % kColumns);
      connect(row, &QToolButton::clicked, this, [this, row, index]() {
        if (index >= entries_.size()) {
          return;
        }
        // The menu runs its own event loop, which may replace the entries.
        const auto [label, color] = entries_[index];
        const std::optional<CategoryMenuPick> pick = execCategoryMenu(
            this, label, color, row->mapToGlobal(QPoint(0, row->height())), {}, {});
        if (!pick || index >= entries_.size()) {
          return;
        }
        emit categoryColorChanged(static_cast<int>(index),
                                  pick->kind == CategoryMenuPick::Kind::Color
                                      ? std::optional<QColor>(pick->color)
                                      : std::nullopt);
      });
      categoryRows_.push_back(row);
    }
    QToolButton* row = categoryRows_[index];
    const auto& [label, color] = entries[index];
    row->ensurePolished();
    row->setText(row->fontMetrics().elidedText(
        label, Qt::ElideMiddle, kCategoryWidth - kCategorySwatch - kCategoryGap - 16));
    row->setAccessibleName(QStringLiteral("Color of %1").arg(label));
    row->setToolTip(QStringLiteral("Change the color of %1").arg(label));
    row->setIcon(categorySwatch(color));
    row->setVisible(true);
  }
  for (size_t index = entries.size(); index < categoryRows_.size(); ++index) {
    categoryRows_[index]->setVisible(false);
  }
  // Every row up to kCategoryRows, then the list scrolls.
  const int rows = static_cast<int>((entries.size() + kColumns - 1) / kColumns);
  const int shown = std::min(rows, kCategoryRows);
  categoryScroll_->setFixedHeight(std::max(0, shown * (kCategoryHeight + kSpacing) - kSpacing));
}

void ColorPanel::showKind(Kind kind) {
  continuous_->setVisible(kind == Kind::Continuous);
  reverse_->setVisible(kind == Kind::Continuous);
  categorical_->setVisible(kind == Kind::Categorical);
}

void ColorPanel::setRange(double min, double max, std::vector<size_t> histogram) {
  histogram_->setRange(min, max, std::move(histogram));
  showBounds(TypedText::Keep);
}

void ColorPanel::setClipRange(double lower, double upper) {
  histogram_->setClipRange(lower, upper);
  showBounds(TypedText::Keep);
}

QSize ColorPanel::sizeHint() const {
  return scroll_->widget()->sizeHint() + QSize(2, 2);
}

QSize ColorPanel::fittedSize(int maxHeight) const {
  const QSize size = sizeHint();
  if (size.height() <= maxHeight) {
    return size;
  }
  return {size.width() + scroll_->verticalScrollBar()->sizeHint().width(), std::max(1, maxHeight)};
}

bool ColorPanel::eventFilter(QObject* watched, QEvent* event) {
  // Input the validator never accepted (empty, a lone "-") commits nothing, so
  // leaving the field shows the range in force again.
  if (event->type() == QEvent::FocusOut && (watched == min_ || watched == max_) &&
      !static_cast<QLineEdit*>(watched)->hasAcceptableInput()) {
    showBounds(TypedText::Replace);
  }
  return QWidget::eventFilter(watched, event);
}

void ColorPanel::updatePreviews() {
  const std::vector<Colormap>& maps = colormaps();
  for (size_t index = 0; index < maps.size(); ++index) {
    buttons_->button(static_cast<int>(index))
        ->setIcon(colormapIcon(maps[index], reverse_->isChecked(), kGradientSize));
  }
}

void ColorPanel::showBounds(TypedText typed) {
  const std::array<double, 2>& clip = histogram_->clipRange();
  for (const auto& [edit, bound] : {std::pair{min_, clip[0]}, std::pair{max_, clip[1]}}) {
    if (typed == TypedText::Keep && edit->isModified()) {
      continue;
    }
    // Setting the text it already holds would still move the cursor and drop the
    // selection, which a refresh per playback frame must not do while the field has focus.
    const QString text = formatBound(bound);
    if (edit->text() != text) {
      edit->setText(text);
    }
    edit->setModified(false);
  }
}

void ColorPanel::commitBounds() {
  // An untouched field shows its bound rounded to 6 digits: reading that back would
  // move the bound, and Qt 5 commits each time a field loses the focus.
  const std::array<double, 2> clip = histogram_->clipRange();
  const bool lowerTyped = min_->isModified();
  const bool upperTyped = max_->isModified();
  const auto entered = [](const QLineEdit* edit, double shown) -> std::optional<double> {
    if (!edit->isModified()) {
      return shown;
    }
    bool valid = false;
    const double value = QLocale::c().toDouble(edit->text(), &valid);
    return valid ? std::optional<double>(value) : std::nullopt;
  };
  const std::optional<double> lower = entered(min_, clip[0]);
  const std::optional<double> upper = entered(max_, clip[1]);
  if (!lower || !upper || *lower > *upper) {
    showBounds(TypedText::Replace);
    return;
  }
  applyClip(*lower, *upper);
  const auto& accepted = histogram_->clipRange();
  if (lowerTyped) {
    emit clipValueFormatChanged(ColorBarWidget::Bound::Lower,
                                accepted[0] == *lower ? ColorBarWidget::ValueFormat::Exact
                                                      : ColorBarWidget::ValueFormat::Rounded);
  }
  if (upperTyped) {
    emit clipValueFormatChanged(ColorBarWidget::Bound::Upper,
                                accepted[1] == *upper ? ColorBarWidget::ValueFormat::Exact
                                                      : ColorBarWidget::ValueFormat::Rounded);
  }
}

void ColorPanel::applyClip(double lower, double upper) {
  const std::array<double, 2> before = histogram_->clipRange();
  histogram_->setClipRange(lower, upper);
  showBounds(TypedText::Replace);
  const std::array<double, 2>& after = histogram_->clipRange();
  if (after != before) {
    emit clipRangeChanged(after[0], after[1]);
  }
}
