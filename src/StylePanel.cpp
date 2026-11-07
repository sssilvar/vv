#include "StylePanel.h"

#include "LucideIcon.h"
#include "OverlayStyle.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <cmath>
#include <initializer_list>

namespace {

constexpr int kPadding = 12;
constexpr int kSpacing = 4;
constexpr int kTileHeight = 64;
constexpr int kTileIconSize = 24;
// Three tiles fill the first row; the two of the second row share its width.
constexpr int kTilesInFirstRow = 3;
constexpr int kTileWidth = 108;
constexpr int kWideTileWidth = (kTilesInFirstRow * kTileWidth + kSpacing) / 2;
const QColor kCheckedKeyBorder(255, 255, 255, 96);

struct Tile {
  SurfaceStyle style;
  Lucide icon;
  const char* name;
  const char* key;
};

// The opaque styles, then the see-through ones.
constexpr Tile kTiles[] = {
    {SurfaceStyle::Solid, Lucide::Box, "Solid", "1"},
    {SurfaceStyle::SolidWithEdges, Lucide::Grid3x3, "Solid with edges", "2"},
    {SurfaceStyle::Wireframe, Lucide::Globe, "Wireframe", "3"},
    {SurfaceStyle::XRay, Lucide::ScanEye, "X-ray", "X"},
    {SurfaceStyle::Glass, Lucide::Gem, "Glass", "G"},
};

} // namespace

StylePanel::StylePanel(QWidget* parent) : QWidget(parent) {
  setObjectName("stylePanel");
  setAttribute(Qt::WA_StyledBackground, true);
  // A press on the panel's padding must not reach the viewport and start a rotation.
  setAttribute(Qt::WA_NoMousePropagation, true);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::ArrowCursor);
  setStyleSheet(
      overlay::panelStyleSheet(objectName()) +
      QStringLiteral(
          "QToolButton#stylePanelTile { font-size: 12px; padding-top: 6px; }"
          "QLabel#stylePanelKey { font-size: 10px; border: 1px solid %4; border-radius: 4px;"
          "  padding: 0 4px; }"
          "QLabel#stylePanelKey[onChecked=\"true\"] { border-color: %5; }"
          "QLabel#stylePanelValue { color: %2; font-size: 12px; }"
          "QSlider::groove:horizontal { height: 4px; background: %1; border-radius: 2px; }"
          "QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 6px;"
          "  background: %2; }"
          "QSlider::sub-page:horizontal { background: %3; border-radius: 2px; }")
          .arg(overlay::css(overlay::kSelectedBorder),
               overlay::css(overlay::kText),
               overlay::css(overlay::kAccent),
               overlay::css(overlay::solidBorder(overlay::kSelectedBorder)),
               // About 3:1 on the checked tile's fill, where the plain border nearly vanishes.
               overlay::css(overlay::solidBorder(kCheckedKeyBorder, overlay::kSelected))));

  auto* column = new QVBoxLayout(this);
  column->setContentsMargins(kPadding, kPadding, kPadding, kPadding);
  column->setSpacing(kSpacing);

  auto* title = new QLabel(QStringLiteral("Style"), this);
  title->setObjectName("panelTitle");
  column->addWidget(title);
  column->addSpacing(kSpacing);

  tiles_ = new QButtonGroup(this);
  QHBoxLayout* row = nullptr;
  int slot = 0;
  for (const Tile& tile : kTiles) {
    if (slot == 0 || slot == kTilesInFirstRow) {
      row = new QHBoxLayout;
      row->setSpacing(kSpacing);
      column->addLayout(row);
    }
    const int width = slot < kTilesInFirstRow ? kTileWidth : kWideTileWidth;
    auto* button = new QToolButton(this);
    button->setObjectName("stylePanelTile");
    button->setText(QString::fromUtf8(tile.name));
    button->setToolTip(QStringLiteral("%1 (%2)").arg(button->text(), QString::fromUtf8(tile.key)));
    button->setIcon(lucideIcon(tile.icon, kTileIconSize));
    button->setIconSize(QSize(kTileIconSize, kTileIconSize));
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setFixedSize(width, kTileHeight);
    button->setCheckable(true);
    // Keeps focus on the viewport, so Space keeps cycling fields after a click.
    button->setFocusPolicy(Qt::NoFocus);
    auto* key = new QLabel(QString::fromUtf8(tile.key), button);
    key->setObjectName("stylePanelKey");
    key->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    key->ensurePolished();
    key->adjustSize();
    key->move(width - key->width() - 6, 6);
    // Repolished so the style sheet's [onChecked="true"] rule follows the tile.
    connect(button, &QToolButton::toggled, key, [key](bool checked) {
      key->setProperty("onChecked", checked);
      key->style()->unpolish(key);
      key->style()->polish(key);
    });
    row->addWidget(button);
    tiles_->addButton(button, static_cast<int>(tile.style));
    ++slot;
  }

  opacity_ =
      addPercentRow(column, QStringLiteral("Opacity"), QStringLiteral("Surface opacity in X-ray"));
  density_ = addPercentRow(column,
                           QStringLiteral("Density"),
                           QStringLiteral("How opaque the glass is away from its rim"));

  setLook(SurfaceLook{});

  connect(tiles_, &QButtonGroup::idClicked, this, [this](int id) {
    showRowFor(static_cast<SurfaceStyle>(id));
    emit styleChosen(static_cast<SurfaceStyle>(id));
  });
  connect(opacity_.slider, &QSlider::valueChanged, this, [this](int percent) {
    showPercent(opacity_, percent);
    emit xrayOpacityChosen(percent / 100.0);
  });
  connect(density_.slider, &QSlider::valueChanged, this, [this](int percent) {
    showPercent(density_, percent);
    emit glassDensityChosen(percent / 100.0);
  });
}

void StylePanel::setLook(const SurfaceLook& look) {
  tiles_->button(static_cast<int>(look.style))->setChecked(true);
  showRowFor(look.style);
  setFraction(opacity_, look.xrayOpacity);
  setFraction(density_, look.glassDensity);
}

StylePanel::PercentRow
StylePanel::addPercentRow(QBoxLayout* column, const QString& name, const QString& toolTip) {
  PercentRow percent;
  percent.row = new QWidget(this);
  auto* layout = new QHBoxLayout(percent.row);
  layout->setContentsMargins(0, kSpacing, 0, 0);
  layout->addWidget(new QLabel(name, percent.row));
  percent.slider = new QSlider(Qt::Horizontal, percent.row);
  percent.slider->setRange(5, 100);
  percent.slider->setFocusPolicy(Qt::NoFocus);
  percent.slider->setToolTip(toolTip);
  layout->addWidget(percent.slider, 1);
  percent.value = new QLabel(percent.row);
  percent.value->setObjectName("stylePanelValue");
  percent.value->ensurePolished();
  // Wide enough for "100 %", so the slider keeps its length as the value changes.
  percent.value->setMinimumWidth(percent.value->fontMetrics().horizontalAdvance("100 %") + 8);
  percent.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  layout->addWidget(percent.value);
  column->addWidget(percent.row);
  return percent;
}

void StylePanel::showPercent(const PercentRow& row, int percent) {
  row.value->setText(QStringLiteral("%1 %").arg(percent));
}

void StylePanel::setFraction(const PercentRow& row, double fraction) {
  {
    const QSignalBlocker block(row.slider);
    row.slider->setValue(static_cast<int>(std::lround(fraction * 100.0)));
  }
  showPercent(row, row.slider->value());
}

void StylePanel::showRowFor(SurfaceStyle style) {
  opacity_.row->setVisible(style == SurfaceStyle::XRay);
  density_.row->setVisible(style == SurfaceStyle::Glass);
}
