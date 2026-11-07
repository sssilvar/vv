#pragma once

#include <QColor>
#include <QRect>
#include <QString>
#include <QWidget>
#include <QtGlobal>
#include <optional>
#include <utility>

class QLabel;
class QToolButton;

/// What the probe read under the pointer.
struct ProbeReading {
  /// The hovered file or part; empty leaves the line out.
  QString source;
  /// The probed field; empty leaves its row out.
  QString field;
  /// Shown like the legend's values; NaN reads "no value".
  double value = 0.0;
  /// A categorical field's legend row for value, its label and color, shown instead of value.
  std::optional<std::pair<QString, QColor>> category;
  qint64 cell = 0;
};

/// Card that follows the pointer while Probe is on: the hovered file or part, the
/// field's value there and the cell id. Hover readings pass mouse events to the view.
class ProbeCard : public QWidget {
  Q_OBJECT

public:
  explicit ProbeCard(QWidget* parent = nullptr);

  void setReading(const ProbeReading& reading);
  void setPinned(bool pinned);

  /// Just right of and above cursor, a point in the parent, flipped to whichever
  /// side keeps the card inside bounds (the parent when bounds is empty).
  void moveNear(const QPoint& cursor, const QRect& bounds = {});

protected:
  void resizeEvent(QResizeEvent* event) override;

signals:
  void unpinRequested();

private:
  QWidget* body_ = nullptr;
  QToolButton* unpin_ = nullptr;
  QLabel* source_ = nullptr;
  QLabel* field_ = nullptr;
  QLabel* swatch_ = nullptr;
  QLabel* value_ = nullptr;
  QLabel* cell_ = nullptr;
  QColor swatchColor_;
};
