#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>
#include <vector>

/// Keys and what they do, e.g. A "hold to paint".
struct KeyHint {
  QStringList keys; // each drawn as a key cap
  QString action;
};

/// Strip of key hints over the view. Every mouse event passes through it to the view.
class KeyHints : public QWidget {
public:
  explicit KeyHints(const std::vector<KeyHint>& hints, QWidget* parent = nullptr);
};
