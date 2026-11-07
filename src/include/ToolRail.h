#pragma once

#include <QWidget>

class QAction;
class QVBoxLayout;

// Floating column of icon buttons on the viewport's left edge. Each button shows
// a QAction, so its shortcut, tooltip, checked and enabled state live in one place.
class ToolRail : public QWidget {
public:
  explicit ToolRail(QWidget* parent);

  void addTool(QAction* action);
  void addSeparator();

private:
  QVBoxLayout* column_ = nullptr;
};
