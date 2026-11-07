#include "CommandPalette.h"

#include "OverlayStyle.h"

#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMargins>
#include <QMouseEvent>
#include <QPainter>
#include <QStringList>
#include <QVBoxLayout>
#include <algorithm>
#include <initializer_list>
#include <utility>

namespace {

constexpr int kWidth = 580;
constexpr int kMargin = 16;
constexpr int kMaxHeight = 480;
constexpr int kFooterGap = 4; // above the rule over the footer
const QColor kDim(0, 0, 0, 110);

struct Hint {
  QStringList keys;
  QString action;
};

} // namespace

CommandPalette::CommandPalette(QWidget* parent) : QWidget(parent) {
  setObjectName("commandPalette");
  // A press beside the card closes the palette and goes no further.
  setAttribute(Qt::WA_NoMousePropagation, true);
  setCursor(Qt::ArrowCursor);

  card_ = new QWidget(this);
  card_->setObjectName("commandPaletteCard");
  card_->setAttribute(Qt::WA_StyledBackground, true);
  card_->setAttribute(Qt::WA_NoMousePropagation, true);
  card_->setStyleSheet(
      overlay::panelStyleSheet(card_->objectName()) +
      QStringLiteral("QWidget#commandPaletteCard { background: %1; }"
                     "QLineEdit#searchListField { font-size: 17px; }"
                     "QFrame#commandPaletteRule { background: %2; }"
                     "QLabel#keyCap { color: %3; border: 1px solid %4; border-radius: 4px;"
                     "  padding: 0 4px; }")
          .arg(overlay::css(overlay::kPopupSurface),
               overlay::css(overlay::kSurfaceBorder),
               overlay::css(overlay::kText),
               overlay::css(overlay::solidBorder(overlay::kSelectedBorder))));

  auto* column = new QVBoxLayout(card_);
  column->setContentsMargins(8, 8, 8, 0);
  column->setSpacing(0);
  list_ = new SearchList(RowStyle::Inline, card_);
  list_->setPlaceholder(QStringLiteral("Search fields, colors and commands"));
  column->addWidget(list_, 1);
  // A press that no row takes (the card's padding, a header, a disabled row) would give
  // the focus to the first parent that takes click focus, the view, and the keys with it.
  setFocusPolicy(Qt::ClickFocus);
  setFocusProxy(list_);
  column->addSpacing(kFooterGap);
  auto* rule = new QFrame(card_);
  rule->setObjectName("commandPaletteRule");
  rule->setFixedHeight(1);
  column->addWidget(rule);

  footer_ = new QWidget(card_);
  auto* hints = new QHBoxLayout(footer_);
  hints->setContentsMargins(6, 8, 6, 10);
  hints->setSpacing(4);
  for (const Hint& hint :
       {Hint{{QStringLiteral("Up"), QStringLiteral("Down")}, QStringLiteral("navigate")},
        Hint{{QStringLiteral("Return")}, QStringLiteral("select")},
        Hint{{QStringLiteral("Esc")}, QStringLiteral("close")}}) {
    for (const QString& key : hint.keys) {
      auto* cap = new QLabel(key, footer_);
      cap->setObjectName("keyCap");
      hints->addWidget(cap);
    }
    hints->addSpacing(2);
    hints->addWidget(new QLabel(hint.action, footer_));
    hints->addSpacing(12);
  }
  hints->addStretch(1);
  column->addWidget(footer_);

  connect(list_, &SearchList::chosen, this, [this](int index) {
    // A copy: the row may open the palette again, replacing the items.
    const std::function<void()> run = items_[static_cast<size_t>(index)].run;
    dismiss();
    if (run) {
      run();
    }
  });
  connect(list_, &SearchList::canceled, this, [this]() { dismiss(); });
  connect(list_, &SearchList::filtered, this, [this]() { place(); });
}

void CommandPalette::open(std::vector<PaletteItem> items) {
  items_ = std::move(items);
  std::vector<SearchItem> rows;
  rows.reserve(items_.size());
  for (const PaletteItem& item : items_) {
    rows.push_back(item.row);
  }
  list_->setItems(rows, -1);
  place();
  show();
  raise();
  list_->focusSearch();
}

void CommandPalette::dismiss() {
  if (isHidden()) {
    return;
  }
  hide();
  emit closed();
}

void CommandPalette::paintEvent(QPaintEvent*) {
  QPainter(this).fillRect(rect(), kDim);
}

void CommandPalette::mousePressEvent(QMouseEvent* event) {
  dismiss();
  event->accept();
}

void CommandPalette::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  place();
}

// Centered across, a sixth of the way down, as tall as the rows the search shows.
void CommandPalette::place() {
  const int width = std::min(kWidth, std::max(1, this->width() - 2 * kMargin));
  const int top = std::max(kMargin, height() / 6);
  // From the parts, whose hints change while the card's layout may still hold its old one.
  const QMargins margins = card_->layout()->contentsMargins();
  const int wanted = margins.top() + list_->sizeHint().height() + kFooterGap + 1 +
                     footer_->sizeHint().height() + margins.bottom();
  const int height = std::max(1, std::min({wanted, kMaxHeight, this->height() - top - kMargin}));
  card_->setGeometry((this->width() - width) / 2, top, width, height);
}
