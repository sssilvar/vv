#include "PanelLabel.h"

#include "LayersPanel.h"
#include "OverlayStyle.h"

#include <QLabel>
#include <QResizeEvent>
#include <algorithm>

PanelLabel::PanelLabel(size_t file, const QString& name, QWidget* parent)
    : QWidget(parent),
      badge_(overlay::identityBadge(this, fileLetter(file), overlay::identityColor(file))),
      name_(new QLabel(this)), status_(new QLabel(this)), filename_(name) {
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setFocusPolicy(Qt::NoFocus);
  name_->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; font-weight: 600;")
                           .arg(overlay::css(overlay::kTextSecondary)));
  status_->setStyleSheet(
      QStringLiteral("color: %1; font-size: 11px;").arg(overlay::css(overlay::kTextSecondary)));
  status_->hide();
}

void PanelLabel::setCaption(PanelCaption caption) {
  caption_ = caption;
  layoutCaption();
}

void PanelLabel::setStatus(const QString& status) {
  status_->setText(status);
  status_->setVisible(!status.isEmpty());
  setAccessibleDescription(status);
  layoutCaption();
}

void PanelLabel::layoutCaption() {
  constexpr int margin = 12;
  badge_->move(margin, margin);
  badge_->resize(badge_->sizeHint());
  const int left = badge_->geometry().right() + 10;
  const int available = std::max(0, width() - left - margin);
  name_->setGeometry(left, margin, available, badge_->height());
  name_->setText(name_->fontMetrics().elidedText(filename_, Qt::ElideMiddle, available));
  status_->setGeometry(margin,
                       margin + badge_->height() + 6,
                       std::max(0, width() - 2 * margin),
                       status_->sizeHint().height());
  name_->setVisible(caption_ == PanelCaption::Filename && available > 0);
}

void PanelLabel::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  layoutCaption();
}
