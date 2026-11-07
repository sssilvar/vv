#pragma once

#include "SearchList.h"

#include <QRect>
#include <QWidget>
#include <vector>

class QMouseEvent;

/// The pages the field pager steps through, in its order, on a card above the pager: a
/// search field, then a row per page with its kind under its name, the shown one checked.
/// While open it covers its parent, so a click beside the card closes it instead of
/// reaching what lies under it.
class FieldList : public QWidget {
  Q_OBJECT

public:
  explicit FieldList(QWidget* parent = nullptr);

  /// Show pages, the current one highlighted, and focus the search; set the geometry and
  /// anchor first.
  void open(const std::vector<SearchItem>& pages);

  /// Where the pager is, in this widget's coordinates: the card sits above it.
  void setAnchor(const QRect& anchor);

  /// Hide without choosing; emits closed when it was open.
  void dismiss();

signals:
  /// The user picked pages[page], as given to open; closed came first.
  void pageChosen(int page);

  /// The list hid: after a pick, Esc or a click beside the card.
  void closed();

protected:
  void mousePressEvent(QMouseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  void place();

  QWidget* card_ = nullptr;
  SearchList* list_ = nullptr;
  QRect anchor_;
};
