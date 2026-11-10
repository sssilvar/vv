#pragma once
#include "VolumeScene.h"

#include <QPoint>
#include <QVTKOpenGLNativeWidget.h>
#include <array>
#include <optional>
#include <vtkSmartPointer.h>

class QLabel;
class vtkImageData;
class vtkImageSliceMapper;
class vtkPoints;
class vtkRenderer;

// One orthogonal slice of the scene's volume, in radiological convention: patient
// left on screen right. Left click or drag sets the crosshair, the wheel steps the
// slice, Ctrl+wheel zooms, middle drag pans and right drag sets window and level.
class SliceView : public QVTKOpenGLNativeWidget {
  Q_OBJECT
public:
  SliceView(VolumeScene& scene, SliceOrientation orientation, QWidget* parent);

  // After the cursor, the gray window or the label display changed; renders.
  void refresh();
  void resetCamera(); // renders

signals:
  // This view moved the cursor or changed the gray window, which every view shows.
  void sceneChanged();

protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  void updateSlice(); // without rendering
  void stepSlice(int steps);
  void moveCursorTo(const QPoint& pos);
  void fitCamera(); // without rendering
  void updateCrosshair();

  VolumeScene& scene_;
  SliceOrientation orientation_;
  int axis_; // the voxel axis this view slices across
  // The camera's unit axes in world coordinates: into the screen, up it and across it.
  std::array<double, 3> direction_{};
  std::array<double, 3> up_{};
  std::array<double, 3> right_{};
  vtkSmartPointer<vtkRenderer> renderer_;
  vtkSmartPointer<vtkImageSliceMapper> grayMapper_;
  vtkSmartPointer<vtkImageSliceMapper> labelMapper_;
  vtkSmartPointer<vtkImageData> labelSlice_;
  std::optional<std::array<int, 2>> labelSliceKey_; // slice index and outline it holds
  vtkSmartPointer<vtkPoints> crosshairPoints_;
  QLabel* badge_ = nullptr;
  double zoom_ = 1.0;
  std::array<double, 3> pan_{}; // world offset of the view centre from the volume's
  double wheelRemainder_ = 0.0;
  Qt::MouseButton dragButton_ = Qt::NoButton;
  QPoint dragFrom_; // the last pointer position while dragButton_ is held
};
