#include "FieldList.h"

#include "OverlayStyle.h"

#include <QLayout>
#include <QMargins>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <algorithm>

namespace {

constexpr int kMargin = 16; // from the edges of the view
constexpr int kGap = 8;     // between the card and the pager
constexpr int kMinWidth = 320;
constexpr int kMaxHeight = 440;

} // namespace

FieldList::FieldList(QWidget* parent) : QWidget(parent) {
  setObjectName("fieldList");
  // A press beside the card closes the list and goes no further.
  setAttribute(Qt::WA_NoMousePropagation, true);
  setCursor(Qt::ArrowCursor);

  card_ = new QWidget(this);
  card_->setObjectName("fieldListCard");
  card_->setAttribute(Qt::WA_StyledBackground, true);
  card_->setAttribute(Qt::WA_NoMousePropagation, true);
  card_->setStyleSheet(overlay::panelStyleSheet(card_->objectName()) +
                       QStringLiteral("QWidget#fieldListCard { background: %1; }"
                                      "QLineEdit#searchListField { font-size: 13px; }")
                           .arg(overlay::css(overlay::kPopupSurface)));
  auto* column = new QVBoxLayout(card_);
  column->setContentsMargins(6, 4, 6, 6);
  list_ = new SearchList(RowStyle::Stacked, card_);
  list_->setPlaceholder(QStringLiteral("Search fields"));
  column->addWidget(list_);
  // A press that no row takes (the card's padding, the scroll bar) would otherwise give
  // the focus to the first parent that takes click focus, the view, and the keys with it.
  setFocusPolicy(Qt::ClickFocus);
  setFocusProxy(list_);

  connect(list_, &SearchList::chosen, this, [this](int page) {
    dismiss();
    emit pageChosen(page);
  });
  connect(list_, &SearchList::canceled, this, [this]() { dismiss(); });
}

void FieldList::open(const std::vector<SearchItem>& pages) {
  const auto current =
      std::find_if(pages.begin(), pages.end(), [](const SearchItem& page) { return page.current; });
  list_->setItems(pages, current == pages.end() ? -1 : static_cast<int>(current - pages.begin()));
  place();
  show();
  raise();
  list_->focusSearch();
}

void FieldList::setAnchor(const QRect& anchor) {
  anchor_ = anchor;
  place();
}

void FieldList::mousePressEvent(QMouseEvent* event) {
  dismiss();
  event->accept();
}

void FieldList::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  place();
}

// Centered over the anchor and as tall as its rows, shortened to stay inside the margins.
void FieldList::place() {
  const QRect anchor = anchor_.isValid() ? anchor_ : QRect(width() / 2, height(), 0, 0);
  const int width = std::max(kMinWidth, anchor.width());
  const int bottom = anchor.top() - kGap;
  // From the list, whose rows change while the card's layout may still hold its old hint.
  const QMargins margins = card_->layout()->contentsMargins();
  const int wanted = list_->sizeHint().height() + margins.top() + margins.bottom();
  const int height = std::max(1, std::min({wanted, kMaxHeight, bottom - kMargin}));
  const int x = std::clamp(
      anchor.center().x() - width / 2, kMargin, std::max(kMargin, this->width() - kMargin - width));
  card_->setGeometry(x, bottom - height, width, height);
}

void FieldList::dismiss() {
  if (isHidden()) {
    return;
  }
  hide();
  emit closed();
}
