#pragma once

#include "SearchList.h"

#include <QWidget>
#include <functional>
#include <vector>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

/// A row of the command palette and what choosing it does.
struct PaletteItem {
  SearchItem row;
  std::function<void()> run;
};

/// Searches what the window gives it, e.g. its fields, colormaps and commands, on a card
/// over its dimmed parent: a large search field, the matches grouped under headers, and
/// the keys in a footer. Choosing a row closes the palette, then runs it; a disabled row
/// is greyed out and never runs. A click beside the card closes it.
class CommandPalette : public QWidget {
  Q_OBJECT

public:
  explicit CommandPalette(QWidget* parent = nullptr);

  /// Show items, the first that can run highlighted, and focus the search; set the
  /// geometry first.
  void open(std::vector<PaletteItem> items);

  /// Hide without running anything; emits closed when it was open.
  void dismiss();

signals:
  /// The palette hid: before a chosen row runs, after Esc or a click beside the card.
  void closed();

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  void place();

  QWidget* card_ = nullptr;
  SearchList* list_ = nullptr;
  QWidget* footer_ = nullptr;
  std::vector<PaletteItem> items_;
};
