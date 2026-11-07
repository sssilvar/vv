#include "ScalarPager.h"

#include "LucideIcon.h"
#include "OverlayStyle.h"

#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QSize>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kListIconSize = 14;
constexpr int kListIconGap = 2;
constexpr int kBadgeGap = 6;
constexpr int kStepIconSize = 20;
constexpr int kMargin = 6;

enum class Direction { Previous, Next };

// The name block is fixed so the pager keeps its size while names of different lengths
// page through; the playback bar's is narrower, leaving its timeline the room.
struct Metrics {
  int nameWidth;
  int buttonSize;
};

Metrics metricsFor(PagerPlacement placement) {
  return placement == PagerPlacement::Capsule ? Metrics{236, 36} : Metrics{160, 32};
}
} // namespace

QString facetPageLabel(std::size_t first, std::size_t count, std::size_t total) {
  if (count == 1) {
    return QStringLiteral("Field %1 of %2").arg(first + 1).arg(total);
  }
  return QStringLiteral("Fields %1–%2 of %3").arg(first + 1).arg(first + count).arg(total);
}

SpaceAction spaceAction(Qt::KeyboardModifiers modifiers, bool timeSeries) {
  if (modifiers.testFlag(kStepBackModifier)) {
    return SpaceAction::PreviousField;
  }
  if (timeSeries && !modifiers.testFlag(Qt::ShiftModifier)) {
    return SpaceAction::PlayPause;
  }
  return SpaceAction::NextField;
}

ScalarPager::ScalarPager(PagerPlacement placement, QWidget* parent)
    : QWidget(parent), placement_(placement) {
  const Metrics metrics = metricsFor(placement);
  const bool capsule = placement == PagerPlacement::Capsule;
  setObjectName("scalarPager");
  setAttribute(Qt::WA_StyledBackground, capsule);
  // A press on the capsule's padding must not reach the viewport and start a rotation.
  setAttribute(Qt::WA_NoMousePropagation, true);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::ArrowCursor);
  const QString surface =
      capsule ? QStringLiteral("QWidget#scalarPager { background: %1; border: 1px solid %2;"
                               "  border-radius: %3px; }")
                    .arg(overlay::css(overlay::kSurface),
                         overlay::css(overlay::solidBorder(overlay::kSurfaceBorder)),
                         QString::number(metrics.buttonSize / 2 + kMargin))
              : QString();
  setStyleSheet(surface + QStringLiteral("QToolButton { background: transparent; border: none;"
                                         "  border-radius: 12px; }"
                                         "QToolButton#scalarPagerStep { border-radius: %1px; }"
                                         "QToolButton:hover { background: %2; }"
                                         "QToolButton:pressed { background: %3; }"
                                         "QLabel#scalarPagerTitle { color: %4; font-size: 13px;"
                                         "  font-weight: 600; }"
                                         "QLabel#scalarPagerDetail { color: %5; font-size: 11px; }")
                              .arg(QString::number(metrics.buttonSize / 2),
                                   overlay::css(overlay::kHover),
                                   overlay::css(overlay::kSelected),
                                   overlay::css(overlay::kText),
                                   overlay::css(overlay::kTextSecondary)));

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(capsule ? kMargin : 0, kMargin, capsule ? kMargin : 0, kMargin);
  row->setSpacing(capsule ? 4 : 2);

  const auto makeButton = [this, &metrics](Direction direction) {
    auto* button = new QToolButton(this);
    button->setObjectName("scalarPagerStep");
    button->setIcon(lucideIcon(
        direction == Direction::Next ? Lucide::ChevronRight : Lucide::ChevronLeft, kStepIconSize));
    button->setIconSize(QSize(kStepIconSize, kStepIconSize));
    // Keeps focus on the viewport, so Space keeps working instead of pressing the button.
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(metrics.buttonSize, metrics.buttonSize);
    const int step = direction == Direction::Next ? 1 : -1;
    connect(button, &QToolButton::clicked, this, [this, step]() { emit stepRequested(step); });
    return button;
  };

  // The name, its kind and the position badge let clicks through to the button under them.
  name_ = new QToolButton(this);
  name_->setAccessibleName(QStringLiteral("Fields"));
  name_->setFocusPolicy(Qt::NoFocus);
  name_->setFixedHeight(capsule ? metrics.buttonSize : 44);
  auto* text = new QWidget(name_);
  text->setFixedWidth(metrics.nameWidth);
  text->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  title_ = new QLabel(text);
  title_->setObjectName("scalarPagerTitle");
  auto* listIcon = new QLabel(text);
  listIcon->setPixmap(lucideIcon(Lucide::ChevronDown, kListIconSize, overlay::kTextSecondary)
                          .pixmap(kListIconSize, kListIconSize));
  detail_ = new QLabel(text);
  detail_->setObjectName("scalarPagerDetail");
  position_ = overlay::badge(capsule ? static_cast<QWidget*>(name_) : text);
  position_->setObjectName("scalarPagerPosition");
  position_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  auto* titleRow = new QHBoxLayout;
  titleRow->setSpacing(kListIconGap);
  titleRow->addWidget(title_);
  titleRow->addWidget(listIcon);
  titleRow->addStretch(1);
  auto* lines = new QVBoxLayout(text);
  lines->setContentsMargins(0, 0, 0, 0);
  lines->setSpacing(capsule ? 0 : 3);
  lines->addLayout(titleRow);
  auto* content = new QHBoxLayout(name_);
  content->setSpacing(10);
  content->addWidget(text);
  if (capsule) {
    lines->addWidget(detail_);
    content->setContentsMargins(10, 0, 8, 1);
    content->addWidget(position_, 0, Qt::AlignVCenter);
  } else {
    auto* detailRow = new QHBoxLayout;
    detailRow->setSpacing(kBadgeGap);
    detailRow->addWidget(detail_);
    detailRow->addWidget(position_);
    detailRow->addStretch(1);
    lines->addLayout(detailRow);
    content->setContentsMargins(8, 0, 8, 0);
  }
  connect(name_, &QToolButton::clicked, this, [this]() { emit listRequested(); });

  const auto spaceWith = [](Qt::KeyboardModifier modifier) {
    return QKeySequence(static_cast<int>(modifier) | Qt::Key_Space)
        .toString(QKeySequence::NativeText);
  };
  previousKeys_ = spaceWith(kStepBackModifier);
  nextKeys_ = capsule ? QStringLiteral("Space") : spaceWith(Qt::ShiftModifier);
  previous_ = makeButton(Direction::Previous);
  next_ = makeButton(Direction::Next);
  row->addWidget(previous_);
  row->addWidget(name_);
  row->addWidget(next_);
  setStepNames(QStringLiteral("Previous field"), QStringLiteral("Next field"));
  fitName();
}

void ScalarPager::setStepNames(const QString& previous, const QString& next) {
  previous_->setAccessibleName(previous);
  previous_->setToolTip(QStringLiteral("%1 (%2)").arg(previous, previousKeys_));
  next_->setAccessibleName(next);
  next_->setToolTip(QStringLiteral("%1 (%2)").arg(next, nextKeys_));
}

void ScalarPager::fitName() {
  // A tool button sizes to its own text and icon, not to the labels inside it.
  name_->layout()->invalidate();
  name_->setFixedWidth(name_->layout()->minimumSize().width());
}

void ScalarPager::setPage(const QString& title, const QString& detail, int position, int count) {
  // The first page is set before the labels are shown; polishing applies the style
  // sheet's fonts so the elision and the badge measure with them.
  title_->ensurePolished();
  detail_->ensurePolished();
  position_->ensurePolished();
  const int nameWidth = metricsFor(placement_).nameWidth;
  const int titleWidth = nameWidth - kListIconSize - kListIconGap;
  const QString shown = title_->fontMetrics().elidedText(title, Qt::ElideMiddle, titleWidth);
  title_->setText(shown);
  // As wide as the last position, so paging never resizes the pager.
  position_->setText(QStringLiteral("%1 / %1").arg(count));
  position_->setMinimumWidth(position_->sizeHint().width());
  position_->setText(QStringLiteral("%1 / %2").arg(position).arg(count));
  const int detailWidth = placement_ == PagerPlacement::Capsule
                              ? nameWidth
                              : nameWidth - position_->minimumWidth() - kBadgeGap;
  detail_->setText(detail_->fontMetrics().elidedText(detail, Qt::ElideRight, detailWidth));
  fitName();
  const QString hint = QStringLiteral("Show every field");
  name_->setToolTip(shown == title ? hint : title + QLatin1Char('\n') + hint);
}
