#include "FieldChip.h"

#include "LucideIcon.h"
#include "OverlayStyle.h"

#include <QFontMetrics>
#include <algorithm>

namespace {
constexpr int kIconSize = 14;
constexpr int kHeight = 28;
} // namespace

FieldChip::FieldChip(QWidget* parent) : QToolButton(parent) {
  setObjectName("fieldChip");
  // A press on the chip must not reach the viewport and paint or rotate.
  setAttribute(Qt::WA_NoMousePropagation, true);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::ArrowCursor);
  setAccessibleName(QStringLiteral("Compared field"));
  setIcon(lucideIcon(Lucide::ChevronDown, kIconSize, overlay::kTextSecondary));
  setIconSize(QSize(kIconSize, kIconSize));
  setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  // The chevron after the name.
  setLayoutDirection(Qt::RightToLeft);
  setFixedHeight(kHeight);
  setStyleSheet(
      QStringLiteral("QToolButton#fieldChip { background: %1; border: 1px solid %2;"
                     "  border-radius: %3px; color: %4; font-size: 12px;"
                     "  font-weight: 600; padding: 0 8px 0 10px; }"
                     "QToolButton#fieldChip:hover { background: %5; }"
                     "QToolButton#fieldChip:pressed { background: %6; }")
          .arg(overlay::css(overlay::kPopupSurface),
               overlay::css(overlay::solidBorder(overlay::kSurfaceBorder)),
               QString::number(kHeight / 2),
               overlay::css(overlay::kText),
               overlay::css(overlay::solidBorder(overlay::kHover, overlay::kPopupSurface)),
               overlay::css(overlay::solidBorder(overlay::kSelected, overlay::kPopupSurface))));
}

void FieldChip::setField(const QString& name, const QString& detail) {
  name_ = name;
  detail_ = detail;
  fitWidth(widthLimit_);
}

QSize FieldChip::fitWidth(int width) {
  widthLimit_ = width;
  ensurePolished();
  setText(name_);
  const QSize full = sizeHint();
  if (full.width() > width) {
    const int chrome = full.width() - fontMetrics().horizontalAdvance(name_);
    setText(fontMetrics().elidedText(name_, Qt::ElideMiddle, std::max(0, width - chrome)));
  }
  const QString hint = QStringLiteral("Choose the field this map shows");
  setToolTip(QStringLiteral("%1 · %2\n%3").arg(name_, detail_, hint));
  resize(sizeHint());
  return sizeHint();
}
