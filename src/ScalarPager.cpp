#include "ScalarPager.h"

#include <QColor>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QSize>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>

namespace {
// Fixed so the buttons stay put while names of different lengths page through.
constexpr int kTitleWidth = 280;
const QColor kIconColor(232, 232, 232); // matches the title text color

enum class Direction { Previous, Next };

// Lucide's chevrons-left and chevrons-right (https://lucide.dev, derived from
// Feather under the MIT license): the SVG strokes, painted in the same 24x24 box
// so no SVG module is needed.
QIcon chevronsIcon(Direction direction) {
  QPainterPath path;
  path.moveTo(11.0, 17.0);
  path.lineTo(6.0, 12.0);
  path.lineTo(11.0, 7.0);
  path.moveTo(18.0, 17.0);
  path.lineTo(13.0, 12.0);
  path.lineTo(18.0, 7.0);
  if (direction == Direction::Next) {
    path = QTransform().translate(24.0, 0.0).scale(-1.0, 1.0).map(path);
  }
  const QPen pen(kIconColor, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  QIcon icon;
  for (const int scale : {1, 2}) {
    QPixmap pix(24 * scale, 24 * scale);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(scale, scale);
    painter.strokePath(path, pen);
    painter.end();
    icon.addPixmap(pix);
  }
  return icon;
}
} // namespace

ScalarPager::ScalarPager(QWidget* parent) : QWidget(parent) {
  setObjectName("scalarPager");
  setAttribute(Qt::WA_StyledBackground, true);
  setFocusPolicy(Qt::NoFocus);
  setStyleSheet("QWidget#scalarPager {"
                "  background: rgba(20,20,20,200);"
                "  border-radius: 8px;"
                "}"
                "QToolButton {"
                "  background: rgba(255,255,255,18);"
                "  border: none;"
                "  border-radius: 4px;"
                "}"
                "QToolButton:hover { background: rgba(255,255,255,40); }"
                "QLabel#scalarPagerTitle { color: #E8E8E8; font-size: 13px; }"
                "QLabel#scalarPagerCounter { color: #A0A0A0; font-size: 11px; }");

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(6, 6, 6, 6);
  row->setSpacing(8);

  const auto makeButton = [this](Direction direction, const QString& name, const QString& keys) {
    auto* button = new QToolButton(this);
    button->setIcon(chevronsIcon(direction));
    button->setIconSize(QSize(20, 20));
    button->setAccessibleName(name);
    button->setToolTip(QStringLiteral("%1 (%2)").arg(name, keys));
    // Keeps focus on the viewport, so Space keeps cycling instead of pressing the button.
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(30, 36);
    const int step = direction == Direction::Next ? 1 : -1;
    connect(button, &QToolButton::clicked, this, [this, step]() { emit stepRequested(step); });
    return button;
  };

  title_ = new QLabel(this);
  title_->setObjectName("scalarPagerTitle");
  title_->setAlignment(Qt::AlignCenter);
  title_->setFixedWidth(kTitleWidth);
  counter_ = new QLabel(this);
  counter_->setObjectName("scalarPagerCounter");
  counter_->setAlignment(Qt::AlignCenter);

  auto* text = new QVBoxLayout;
  text->setSpacing(0);
  text->addWidget(title_);
  text->addWidget(counter_);

  const QString back = QKeySequence(static_cast<int>(kStepBackModifier) | Qt::Key_Space)
                           .toString(QKeySequence::NativeText);
  row->addWidget(makeButton(Direction::Previous, QStringLiteral("Previous field"), back));
  row->addLayout(text);
  row->addWidget(
      makeButton(Direction::Next, QStringLiteral("Next field"), QStringLiteral("Space")));
}

void ScalarPager::setPage(const QString& title, int position, int count) {
  // The first page is set before the label is shown; polishing applies the
  // style sheet's font so the elision measures with it.
  title_->ensurePolished();
  const QString shown = title_->fontMetrics().elidedText(title, Qt::ElideMiddle, kTitleWidth);
  title_->setText(shown);
  title_->setToolTip(shown == title ? QString() : title);
  counter_->setText(QStringLiteral("%1/%2").arg(position).arg(count));
}
