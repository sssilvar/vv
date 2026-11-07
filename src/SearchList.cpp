#include "SearchList.h"

#include "LucideIcon.h"
#include "OverlayStyle.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QModelIndex>
#include <QPainter>
#include <QPalette>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QTextOption>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kStackedHeight = 40;
constexpr int kInlineHeight = 34;
constexpr int kHeaderHeight = 28;
constexpr int kRowPadding = 10;
constexpr int kIconSize = 16;
constexpr int kCheckSize = 16;
constexpr int kGap = 8;
constexpr int kTextSize = 13;
constexpr int kSmallSize = 11;

// Every row has its index into the items given to setItems; a header has -1.
constexpr int kItemRole = Qt::UserRole;
constexpr int kDetailRole = Qt::UserRole + 1;
constexpr int kCurrentRole = Qt::UserRole + 2;
constexpr int kShortcutRole = Qt::UserRole + 3;
constexpr int kMatchRole = Qt::UserRole + 4; // QPoint(start, length) of the matched run

QFont pixelFont(QFont font, int pixels, QFont::Weight weight = QFont::Normal) {
  font.setPixelSize(pixels);
  font.setWeight(weight);
  return font;
}

// text, its matched run [match.x(), match.x() + match.y()) bold in matchColor, cut with
// an ellipsis where it overflows rect.
void drawText(QPainter* painter,
              const QRect& rect,
              const QString& text,
              const QFont& font,
              const QColor& color,
              const QPoint& match,
              const QColor& matchColor) {
  const QString shown = QFontMetrics(font).elidedText(text, Qt::ElideRight, rect.width());
  QTextLayout layout(shown, font);
  QTextOption option;
  option.setWrapMode(QTextOption::NoWrap);
  layout.setTextOption(option);
  // A run cut by the ellipsis is bold as far as it shows.
  const int visible = static_cast<int>(shown == text ? text.size() : shown.size() - 1);
  const int end = std::min(match.x() + match.y(), visible);
  if (match.x() >= 0 && end > match.x()) {
    QTextLayout::FormatRange run;
    run.start = match.x();
    run.length = end - match.x();
    run.format.setFontWeight(QFont::DemiBold);
    run.format.setForeground(matchColor);
    layout.setFormats({run});
  }
  layout.beginLayout();
  const QTextLine line = layout.createLine();
  layout.endLayout();
  painter->save();
  painter->setClipRect(rect);
  painter->setPen(color);
  layout.draw(painter, QPointF(rect.left(), rect.top() + (rect.height() - line.height()) / 2.0));
  painter->restore();
}

class RowDelegate : public QStyledItemDelegate {
public:
  RowDelegate(RowStyle style, QObject* parent)
      : QStyledItemDelegate(parent), style_(style),
        check_(lucideIcon(Lucide::Check, kCheckSize, overlay::kAccent)) {}

  QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override {
    if (index.data(kItemRole).toInt() < 0) {
      return {0, kHeaderHeight};
    }
    return {0, style_ == RowStyle::Stacked ? kStackedHeight : kInlineHeight};
  }

  void paint(QPainter* painter,
             const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (index.data(kItemRole).toInt() < 0) {
      painter->setFont(pixelFont(option.font, kSmallSize, QFont::DemiBold));
      painter->setPen(overlay::kTextSecondary);
      painter->drawText(option.rect.adjusted(kRowPadding, 0, -kRowPadding, -6),
                        Qt::AlignLeft | Qt::AlignBottom,
                        index.data(Qt::DisplayRole).toString());
      painter->restore();
      return;
    }

    const bool enabled = option.state.testFlag(QStyle::State_Enabled);
    const QRect row = option.rect.adjusted(0, 1, 0, -1);
    if (enabled && (option.state.testFlag(QStyle::State_Selected) ||
                    option.state.testFlag(QStyle::State_MouseOver))) {
      painter->setPen(Qt::NoPen);
      painter->setBrush(option.state.testFlag(QStyle::State_Selected) ? overlay::kSelected
                                                                      : overlay::kHover);
      painter->drawRoundedRect(QRectF(row), 8.0, 8.0);
    }
    const QColor text = enabled ? overlay::kText : overlay::kIconDisabled;
    const QColor secondary = enabled ? overlay::kTextSecondary : overlay::kIconDisabled;
    const QFont small = pixelFont(option.font, kSmallSize);
    QRect content = row.adjusted(kRowPadding, 0, -kRowPadding, 0);

    // From the right edge inward: the check, the shortcut's key, an inline detail.
    if (index.data(kCurrentRole).toBool()) {
      const QRect mark(content.right() + 1 - kCheckSize,
                       content.center().y() + 1 - kCheckSize / 2,
                       kCheckSize,
                       kCheckSize);
      check_.paint(painter, mark);
      content.setRight(mark.left() - kGap);
    }
    const QString shortcut = index.data(kShortcutRole).toString();
    if (!shortcut.isEmpty()) {
      const int width = QFontMetrics(small).horizontalAdvance(shortcut) + 10;
      const QRect key(content.right() + 1 - width, content.center().y() - 9, width, 19);
      painter->setPen(enabled ? overlay::kSelectedBorder : overlay::kSurfaceBorder);
      painter->setBrush(Qt::NoBrush);
      painter->drawRoundedRect(QRectF(key).adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);
      painter->setFont(small);
      painter->setPen(secondary);
      painter->drawText(key, Qt::AlignCenter, shortcut);
      content.setRight(key.left() - kGap);
    }
    const QString detail = index.data(kDetailRole).toString();
    if (style_ == RowStyle::Inline && !detail.isEmpty()) {
      const QFontMetrics metrics(small);
      // Rounded up: elidedText cuts text a fraction of a pixel too wide.
      const int width =
          std::min(static_cast<int>(std::ceil(QFontMetricsF(small).horizontalAdvance(detail))),
                   content.width() / 2);
      const QRect place(content.right() + 1 - width, content.top(), width, content.height());
      painter->setFont(small);
      painter->setPen(secondary);
      painter->drawText(place,
                        Qt::AlignRight | Qt::AlignVCenter,
                        metrics.elidedText(detail, Qt::ElideRight, width));
      content.setRight(place.left() - 2 * kGap);
    }
    if (style_ == RowStyle::Inline) {
      const QRect box(
          content.left(), content.center().y() + 1 - kIconSize / 2, kIconSize, kIconSize);
      index.data(Qt::DecorationRole)
          .value<QIcon>()
          .paint(painter, box, Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
      content.setLeft(box.right() + 1 + kRowPadding);
    }

    const QFont textFont = pixelFont(option.font, kTextSize);
    const QPoint match = index.data(kMatchRole).toPoint();
    const QColor matchColor = enabled ? overlay::kAccent : text;
    if (style_ == RowStyle::Inline || detail.isEmpty()) {
      drawText(painter,
               content,
               index.data(Qt::DisplayRole).toString(),
               textFont,
               text,
               match,
               matchColor);
    } else {
      const int textHeight = QFontMetrics(textFont).height();
      const int detailHeight = QFontMetrics(small).height();
      const QRect line(content.left(),
                       content.top() + (content.height() - textHeight - detailHeight) / 2,
                       content.width(),
                       textHeight);
      drawText(
          painter, line, index.data(Qt::DisplayRole).toString(), textFont, text, match, matchColor);
      drawText(painter,
               QRect(line.left(), line.bottom() + 1, line.width(), detailHeight),
               detail,
               small,
               secondary,
               QPoint(-1, 0),
               secondary);
    }
    painter->restore();
  }

private:
  RowStyle style_;
  QIcon check_;
};

// As tall as the rows it shows, so a card can fit them before they scroll.
class RowList : public QListWidget {
public:
  using QListWidget::QListWidget;

  QSize sizeHint() const override {
    int height = 2 * frameWidth();
    for (int row = 0; row < count(); ++row) {
      if (!item(row)->isHidden()) {
        height += sizeHintForRow(row);
      }
    }
    return {QListWidget::sizeHint().width(), height};
  }
};

} // namespace

SearchList::SearchList(RowStyle style, QWidget* parent) : QWidget(parent) {
  setObjectName("searchList");
  // The search field's font size is the container's to set.
  setStyleSheet(QStringLiteral("QLineEdit#searchListField { background: transparent; border: none;"
                               "  color: %1; selection-background-color: %2; }"
                               "QFrame#searchListRule { background: %3; }"
                               "QListWidget#searchListRows { background: transparent; border: none;"
                               "  outline: none; }"
                               "QLabel#searchListEmpty { color: %4; font-size: 12px; }")
                    .arg(overlay::css(overlay::kText),
                         overlay::css(overlay::kAccent),
                         overlay::css(overlay::kSurfaceBorder),
                         overlay::css(overlay::kTextSecondary)));

  auto* column = new QVBoxLayout(this);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(0);

  search_ = new QLineEdit(this);
  search_->setObjectName("searchListField");
  search_->setMinimumHeight(32);
  search_->addAction(lucideIcon(Lucide::Search, kIconSize, overlay::kTextSecondary),
                     QLineEdit::LeadingPosition);
  QPalette palette = search_->palette();
  palette.setColor(QPalette::PlaceholderText, overlay::kTextSecondary);
  search_->setPalette(palette);
  search_->installEventFilter(this);
  column->addWidget(search_);
  setFocusProxy(search_);

  auto* rule = new QFrame(this);
  rule->setObjectName("searchListRule");
  rule->setFixedHeight(1);
  column->addWidget(rule);
  column->addSpacing(4);

  empty_ = new QLabel(QStringLiteral("No matches"), this);
  empty_->setObjectName("searchListEmpty");
  empty_->setContentsMargins(kRowPadding, 8, kRowPadding, 8);
  empty_->hide();
  column->addWidget(empty_);

  rows_ = new RowList(this);
  rows_->setObjectName("searchListRows");
  rows_->setItemDelegate(new RowDelegate(style, rows_));
  rows_->setFrameShape(QFrame::NoFrame);
  rows_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  rows_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  rows_->setSelectionMode(QAbstractItemView::SingleSelection);
  // The search field keeps the focus, so typing always searches.
  rows_->setFocusPolicy(Qt::NoFocus);
  rows_->setMouseTracking(true);
  rows_->viewport()->setAttribute(Qt::WA_Hover, true);
  rows_->viewport()->setAutoFillBackground(false);
  column->addWidget(rows_, 1);

  connect(search_, &QLineEdit::textChanged, this, &SearchList::filter);
  connect(rows_, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) {
    choose(index.row());
  });
}

void SearchList::setItems(const std::vector<SearchItem>& items, int start) {
  const QSignalBlocker block(search_);
  search_->clear();
  rows_->clear();
  start_ = start;
  QString group;
  for (size_t index = 0; index < items.size(); ++index) {
    const SearchItem& item = items[index];
    if (!item.group.isEmpty() && item.group != group) {
      auto* header = new QListWidgetItem(item.group, rows_);
      header->setData(kItemRole, -1);
      header->setFlags(Qt::NoItemFlags);
    }
    group = item.group;
    auto* row = new QListWidgetItem(item.icon, item.text, rows_);
    row->setData(kItemRole, static_cast<int>(index));
    row->setData(kDetailRole, item.detail);
    row->setData(kCurrentRole, item.current);
    row->setData(kShortcutRole, item.shortcut);
    row->setToolTip(item.toolTip);
    if (!item.enabled) {
      row->setFlags(Qt::NoItemFlags);
    }
  }
  filter(QString());
}

void SearchList::setPlaceholder(const QString& text) {
  search_->setPlaceholderText(text);
  search_->setAccessibleName(text);
}

void SearchList::focusSearch() {
  search_->setFocus(Qt::PopupFocusReason);
}

QSize SearchList::sizeHint() const {
  ensurePolished();
  return layout()->sizeHint();
}

bool SearchList::eventFilter(QObject* watched, QEvent* event) {
  if (watched != search_ ||
      (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)) {
    return QWidget::eventFilter(watched, event);
  }
  auto* key = static_cast<QKeyEvent*>(event);
  switch (key->key()) {
  case Qt::Key_Up:
  case Qt::Key_Down:
  case Qt::Key_Return:
  case Qt::Key_Enter:
  case Qt::Key_Escape:
    break;
  default:
    return QWidget::eventFilter(watched, event);
  }
  // Ours, not a window shortcut's: the window's Esc would close a panel instead.
  if (event->type() == QEvent::ShortcutOverride) {
    event->accept();
    return true;
  }
  if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
    moveHighlight(key->key() == Qt::Key_Down ? 1 : -1);
  } else if (key->key() == Qt::Key_Escape) {
    emit canceled();
  } else {
    choose(rows_->currentRow());
  }
  return true;
}

void SearchList::filter(const QString& search) {
  int first = -1;
  int start = -1;
  bool shown = false;
  QListWidgetItem* header = nullptr;
  bool groupShown = false;
  for (int row = 0; row < rows_->count(); ++row) {
    QListWidgetItem* item = rows_->item(row);
    const int index = item->data(kItemRole).toInt();
    if (index < 0) {
      if (header) {
        header->setHidden(!groupShown);
      }
      header = item;
      groupShown = false;
      continue;
    }
    const auto at = static_cast<int>(item->text().indexOf(search, 0, Qt::CaseInsensitive));
    item->setHidden(at < 0);
    item->setData(kMatchRole,
                  search.isEmpty() ? QPoint(-1, 0) : QPoint(at, static_cast<int>(search.size())));
    if (at < 0) {
      continue;
    }
    shown = true;
    groupShown = true;
    if (selectable(row)) {
      first = first < 0 ? row : first;
      start = index == start_ ? row : start;
    }
  }
  if (header) {
    header->setHidden(!groupShown);
  }
  // Nothing typed: the start row, else the first that can be chosen. Then the best
  // match: the first.
  highlight(search.isEmpty() && start >= 0 ? start : first);
  empty_->setVisible(!shown);
  rows_->updateGeometry();
  emit filtered();
}

bool SearchList::selectable(int row) const {
  const QListWidgetItem* item = rows_->item(row);
  return !item->isHidden() && item->flags().testFlag(Qt::ItemIsEnabled);
}

void SearchList::highlight(int row) {
  rows_->setCurrentRow(row);
  if (row < 0) {
    rows_->clearSelection();
    return;
  }
  // The first row of a group brings its header into view.
  if (row > 0 && rows_->item(row - 1)->data(kItemRole).toInt() < 0) {
    rows_->scrollToItem(rows_->item(row - 1));
  }
}

void SearchList::moveHighlight(int step) {
  for (int row = rows_->currentRow() + step; row >= 0 && row < rows_->count(); row += step) {
    if (selectable(row)) {
      highlight(row);
      return;
    }
  }
}

void SearchList::choose(int row) {
  if (row >= 0 && row < rows_->count() && selectable(row)) {
    emit chosen(rows_->item(row)->data(kItemRole).toInt());
  }
}
