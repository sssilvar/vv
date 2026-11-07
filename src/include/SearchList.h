#pragma once

#include <QIcon>
#include <QSize>
#include <QString>
#include <QWidget>
#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;

/// One row of a SearchList.
struct SearchItem {
  QString text;         // what a search matches
  QString detail;       // secondary text, under the text or right-aligned
  bool current = false; // checked
  QString group;        // a header names the group above its first row
  QString shortcut;     // drawn as a key at the right
  QIcon icon;           // in the column left of the text, with RowStyle::Inline
  bool enabled = true;  // a disabled row is greyed out and cannot be highlighted or chosen
  QString toolTip;
};

enum class RowStyle {
  Stacked, // the detail under the text
  Inline,  // an icon column, the text, then the detail and the shortcut right-aligned
};

/// A search field over rows, grouped under headers, that typing narrows to those whose
/// text holds the search, ignoring case, the matched run in bold accent. Up and Down
/// move the highlight, Return or a click chooses the highlighted row and Esc cancels.
/// The keys work while the search field has the focus, which the rows never take;
/// focusing the list focuses the search field.
class SearchList : public QWidget {
  Q_OBJECT

public:
  explicit SearchList(RowStyle style, QWidget* parent = nullptr);

  /// Replace the rows and clear the search; while it is empty, items[start] is
  /// highlighted, or the first row that can be chosen when start is -1. Emits nothing.
  void setItems(const std::vector<SearchItem>& items, int start);

  void setPlaceholder(const QString& text);

  /// Give the search field the keyboard focus.
  void focusSearch();

  /// The search field and the rows the search shows.
  QSize sizeHint() const override;

signals:
  /// The user chose the row of items[index], as given to setItems.
  void chosen(int index);

  /// The user pressed Esc.
  void canceled();

  /// The search changed which rows show, and so the size hint.
  void filtered();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  void filter(const QString& search);
  bool selectable(int row) const;
  void highlight(int row); // -1 for none
  void moveHighlight(int step);
  void choose(int row);

  QLineEdit* search_ = nullptr;
  QListWidget* rows_ = nullptr;
  QLabel* empty_ = nullptr;
  int start_ = -1;
};
