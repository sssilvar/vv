#pragma once

#include "SurfaceStyle.h"

#include <QWidget>

class QBoxLayout;
class QButtonGroup;
class QLabel;
class QSlider;

/// How surfaces are drawn: a tile per style (Solid, Solid with edges and Wireframe,
/// then X-ray and Glass), with the x-ray opacity while X-ray is picked and the glass
/// density while Glass is.
class StylePanel : public QWidget {
  Q_OBJECT

public:
  explicit StylePanel(QWidget* parent = nullptr);

  /// Mark the look in use; emits nothing.
  void setLook(const SurfaceLook& look);

signals:
  /// The user clicked a style tile.
  void styleChosen(SurfaceStyle style);

  /// The user moved the Opacity slider; opacity is a fraction, 0.05 to 1.
  void xrayOpacityChosen(double opacity);

  /// The user moved the Density slider; density is a fraction, 0.05 to 1.
  void glassDensityChosen(double density);

private:
  // A labelled slider from 5 to 100 % with its value at the right.
  struct PercentRow {
    QWidget* row = nullptr;
    QSlider* slider = nullptr; // percent
    QLabel* value = nullptr;
  };
  PercentRow addPercentRow(QBoxLayout* column, const QString& name, const QString& toolTip);
  static void showPercent(const PercentRow& row, int percent);
  static void setFraction(const PercentRow& row, double fraction);
  void showRowFor(SurfaceStyle style);

  QButtonGroup* tiles_ = nullptr; // ids are SurfaceStyle values
  PercentRow opacity_;            // shown for X-ray only
  PercentRow density_;            // shown for Glass only
};
