#pragma once

#include <QString>
#include <QToolButton>

// A compare map's field over its tile: the name and a chevron on a small capsule, which a
// click asks to change from the list of fields.
class FieldChip : public QToolButton {
public:
  explicit FieldChip(QWidget* parent = nullptr);

  // detail is the field's kind, e.g. "Points · continuous", shown in the tooltip.
  void setField(const QString& name, const QString& detail);

  // At most width wide, eliding the name; returns the chip's size.
  QSize fitWidth(int width);

private:
  QString name_;
  QString detail_;
  int widthLimit_ = QWIDGETSIZE_MAX;
};
