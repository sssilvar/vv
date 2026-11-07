#include "ToolRail.h"

#include "OverlayStyle.h"

#include <QAction>
#include <QFrame>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kButtonSize = 40;
constexpr int kIconSize = 20;
} // namespace

ToolRail::ToolRail(QWidget* parent) : QWidget(parent) {
  setObjectName("toolRail");
  setAttribute(Qt::WA_StyledBackground, true);
  // A press on the rail's padding must not reach the viewport and start a rotation.
  setAttribute(Qt::WA_NoMousePropagation, true);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::ArrowCursor);
  setStyleSheet(
      QStringLiteral("QWidget#toolRail { background: %1; border: 1px solid %6;"
                     "  border-radius: 16px; }"
                     "QToolButton { background: transparent; border: 1px solid "
                     "transparent; border-radius: 11px; }"
                     "QToolButton:hover { background: %3; }"
                     "QToolButton:checked { background: %4; border-color: %5; }"
                     "QToolButton:disabled { background: transparent; }"
                     "QFrame#toolRailSeparator { background: %2; }")
          .arg(overlay::css(overlay::kSurface),
               overlay::css(overlay::kSurfaceBorder),
               overlay::css(overlay::kHover),
               overlay::css(overlay::kSelected),
               overlay::css(overlay::solidBorder(overlay::kSelectedBorder, overlay::kSelected)),
               overlay::css(overlay::solidBorder(overlay::kSurfaceBorder))));

  column_ = new QVBoxLayout(this);
  column_->setContentsMargins(6, 6, 6, 6);
  column_->setSpacing(4);
}

void ToolRail::addTool(QAction* action) {
  auto* button = new QToolButton(this);
  button->setDefaultAction(action);
  button->setIconSize(QSize(kIconSize, kIconSize));
  button->setFixedSize(kButtonSize, kButtonSize);
  button->setAutoRaise(true);
  // Keeps focus on the viewport, so Space keeps cycling fields after a click.
  button->setFocusPolicy(Qt::NoFocus);
  column_->addWidget(button);
}

void ToolRail::addSeparator() {
  auto* line = new QFrame(this);
  line->setObjectName("toolRailSeparator");
  line->setFixedSize(kButtonSize - 12, 1);
  column_->addSpacing(4);
  column_->addWidget(line, 0, Qt::AlignHCenter);
  column_->addSpacing(4);
}
