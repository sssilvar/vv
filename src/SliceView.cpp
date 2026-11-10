#include "SliceView.h"

#include "OverlayStyle.h"

#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkImageProperty.h>
#include <vtkImageSlice.h>
#include <vtkImageSliceMapper.h>
#include <vtkImageStack.h>
#include <vtkLookupTable.h>
#include <vtkMath.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>

namespace {

constexpr int kBadgeMargin = 8;
// Pixels of right drag that change window or level by the initial window.
constexpr double kWindowDragPixels = 300.0;

// Radiological convention in RAS world coordinates, indexed by SliceOrientation. The
// camera looks along direction: axial from the feet and coronal from the front, both
// with patient left on screen right; sagittal from the patient's left, with anterior
// on screen left.
struct ViewConvention {
  const char* name;
  std::array<double, 3> direction;
  std::array<double, 3> up;
};
constexpr std::array<ViewConvention, 3> kConventions{{
    {"Sagittal", {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}},
    {"Coronal", {0.0, -1.0, 0.0}, {0.0, 0.0, 1.0}},
    {"Axial", {0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}},
}};

} // namespace

SliceView::SliceView(VolumeScene& scene, SliceOrientation orientation, QWidget* parent)
    : QVTKOpenGLNativeWidget(parent), scene_(scene), orientation_(orientation),
      axis_(scene.voxelAxis(orientation)), renderer_(vtkSmartPointer<vtkRenderer>::New()),
      grayMapper_(vtkSmartPointer<vtkImageSliceMapper>::New()),
      labelMapper_(vtkSmartPointer<vtkImageSliceMapper>::New()),
      labelSlice_(vtkSmartPointer<vtkImageData>::New()),
      crosshairPoints_(vtkSmartPointer<vtkPoints>::New()) {
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_AcceptTouchEvents, false);
  vtkNew<vtkGenericOpenGLRenderWindow> window;
  window->SetMultiSamples(0);
  setRenderWindow(window);
  window->AddRenderer(renderer_);
  // The view handles its own input; VTK's default style would also turn the camera.
  interactor()->SetInteractorStyle(nullptr);
  renderer_->SetBackground(0.0, 0.0, 0.0);

  // The slice plane's world normal, signed to follow the convention, and the
  // convention's up made perpendicular to it.
  const ViewConvention& view = kConventions[static_cast<size_t>(orientation)];
  for (int row = 0; row < 3; ++row) {
    direction_[static_cast<size_t>(row)] = scene.toWorld()->GetElement(row, axis_);
  }
  vtkMath::Normalize(direction_.data());
  if (vtkMath::Dot(direction_.data(), view.direction.data()) < 0.0) {
    vtkMath::MultiplyScalar(direction_.data(), -1.0);
  }
  up_ = view.up;
  const double along = vtkMath::Dot(up_.data(), direction_.data());
  for (size_t i = 0; i < 3; ++i) {
    up_[i] -= along * direction_[i];
  }
  vtkMath::Normalize(up_.data());
  vtkMath::Cross(direction_.data(), up_.data(), right_.data());

  vtkNew<vtkImageStack> stack;
  if (vtkImageData* intensity = scene.intensity()) {
    grayMapper_->SetInputData(intensity);
    grayMapper_->SetOrientation(axis_);
    vtkNew<vtkImageSlice> gray;
    gray->SetMapper(grayMapper_);
    gray->SetProperty(scene.grayProperty());
    gray->SetUserMatrix(scene.toWorld());
    stack->AddImage(gray);
  }
  if (scene.hasLabels()) {
    labelMapper_->SetInputData(labelSlice_);
    labelMapper_->SetOrientation(axis_);
    vtkNew<vtkImageProperty> labelProperty;
    labelProperty->SetLookupTable(scene.labelTable());
    labelProperty->UseLookupTableScalarRangeOn();
    labelProperty->SetInterpolationTypeToNearest();
    labelProperty->SetLayerNumber(1);
    vtkNew<vtkImageSlice> labels;
    labels->SetMapper(labelMapper_);
    labels->SetProperty(labelProperty);
    labels->SetUserMatrix(scene.toWorld());
    stack->AddImage(labels);
  }
  renderer_->AddViewProp(stack);

  vtkNew<vtkPolyData> lines;
  crosshairPoints_->SetNumberOfPoints(4);
  lines->SetPoints(crosshairPoints_);
  vtkNew<vtkCellArray> cells;
  const vtkIdType first[2] = {0, 1};
  const vtkIdType second[2] = {2, 3};
  cells->InsertNextCell(2, first);
  cells->InsertNextCell(2, second);
  lines->SetLines(cells);
  vtkNew<vtkPolyDataMapper> lineMapper;
  lineMapper->SetInputData(lines);
  vtkNew<vtkActor> crosshair;
  crosshair->SetMapper(lineMapper);
  const QColor accent = overlay::kAccentBright;
  crosshair->GetProperty()->SetColor(static_cast<double>(accent.redF()),
                                     static_cast<double>(accent.greenF()),
                                     static_cast<double>(accent.blueF()));
  crosshair->GetProperty()->SetOpacity(0.7);
  crosshair->GetProperty()->LightingOff();
  renderer_->AddActor(crosshair);

  renderer_->GetActiveCamera()->ParallelProjectionOn();

  badge_ = overlay::badge(this);
  // The overlay surface rather than the badge's faint fill, which bright slices wash out.
  badge_->setStyleSheet(QStringLiteral("QLabel { color: %1; background: %2; font-size: 11px;"
                                       "  border-radius: 6px; padding: 3px 7px; }")
                            .arg(overlay::css(overlay::kText), overlay::css(overlay::kSurface)));
  badge_->setAttribute(Qt::WA_TransparentForMouseEvents);
  badge_->move(kBadgeMargin, kBadgeMargin);

  updateSlice();
  fitCamera();
}

void SliceView::refresh() {
  updateSlice();
  renderWindow()->Render();
}

void SliceView::updateSlice() {
  const int index = scene_.cursor()[static_cast<size_t>(axis_)];
  grayMapper_->SetSliceNumber(index);
  if (scene_.hasLabels()) {
    const std::array<int, 2> key{index, scene_.outline() ? 1 : 0};
    if (labelSliceKey_ != key) {
      scene_.labelSlice(axis_, index, labelSlice_);
      labelSliceKey_ = key;
    }
    labelMapper_->SetSliceNumber(index);
  }
  updateCrosshair();
  badge_->setText(QStringLiteral("%1  %2 / %3")
                      .arg(QLatin1String(kConventions[static_cast<size_t>(orientation_)].name))
                      .arg(index + 1)
                      .arg(scene_.dimensions()[static_cast<size_t>(axis_)]));
  badge_->adjustSize();
  renderer_->ResetCameraClippingRange();
}

void SliceView::resetCamera() {
  zoom_ = 1.0;
  pan_ = {};
  fitCamera();
  renderWindow()->Render();
}

void SliceView::fitCamera() {
  const auto bounds = scene_.worldBounds();
  const std::array<double, 3> centre{
      0.5 * (bounds[0] + bounds[1]), 0.5 * (bounds[2] + bounds[3]), 0.5 * (bounds[4] + bounds[5])};
  // Half the volume's extent across and up the screen.
  double halfWidth = 0.0;
  double halfHeight = 0.0;
  for (int corner = 0; corner < 8; ++corner) {
    const std::array<double, 3> offset{bounds[(corner & 1) ? 1 : 0] - centre[0],
                                       bounds[(corner & 2) ? 3 : 2] - centre[1],
                                       bounds[(corner & 4) ? 5 : 4] - centre[2]};
    halfWidth = std::max(halfWidth, std::abs(vtkMath::Dot(offset.data(), right_.data())));
    halfHeight = std::max(halfHeight, std::abs(vtkMath::Dot(offset.data(), up_.data())));
  }
  const double aspect = height() > 0 ? static_cast<double>(std::max(1, width())) / height() : 1.0;
  const double diagonal = std::sqrt(vtkMath::Distance2BetweenPoints(
      std::array<double, 3>{bounds[0], bounds[2], bounds[4]}.data(),
      std::array<double, 3>{bounds[1], bounds[3], bounds[5]}.data()));

  vtkCamera* camera = renderer_->GetActiveCamera();
  std::array<double, 3> focal{};
  std::array<double, 3> position{};
  for (size_t i = 0; i < 3; ++i) {
    focal[i] = centre[i] + pan_[i];
    position[i] = focal[i] - direction_[i] * (diagonal + 1.0);
  }
  camera->SetFocalPoint(focal.data());
  camera->SetPosition(position.data());
  camera->SetViewUp(up_.data());
  camera->SetParallelScale(1.05 * std::max(halfHeight, halfWidth / aspect) / zoom_);
  renderer_->ResetCameraClippingRange();
}

void SliceView::updateCrosshair() {
  const auto& cursor = scene_.cursor();
  const auto& dims = scene_.dimensions();
  // Slightly in front of the slice so the lines never sink into the image.
  const double lift = 0.5 * *std::min_element(scene_.spacing().begin(), scene_.spacing().end());
  vtkIdType point = 0;
  for (int along = 0; along < 3; ++along) {
    if (along == axis_) {
      continue;
    }
    for (const int end : {0, dims[static_cast<size_t>(along)] - 1}) {
      std::array<double, 3> voxel{static_cast<double>(cursor[0]),
                                  static_cast<double>(cursor[1]),
                                  static_cast<double>(cursor[2])};
      voxel[static_cast<size_t>(along)] = end;
      std::array<double, 3> world = scene_.voxelToWorld(voxel);
      for (size_t i = 0; i < 3; ++i) {
        world[i] -= lift * direction_[i];
      }
      crosshairPoints_->SetPoint(point++, world.data());
    }
  }
  crosshairPoints_->Modified();
}

void SliceView::stepSlice(int steps) {
  std::array<int, 3> cursor = scene_.cursor();
  cursor[static_cast<size_t>(axis_)] += steps;
  if (scene_.setCursor(cursor)) {
    emit sceneChanged();
  }
}

void SliceView::moveCursorTo(const QPoint& pos) {
  const double ratio = devicePixelRatioF();
  // VTK counts pixel rows from the bottom one, row 0.
  renderer_->SetDisplayPoint(pos.x() * ratio, (height() - 1 - pos.y()) * ratio, 0.0);
  renderer_->DisplayToWorld();
  std::array<double, 4> world{};
  renderer_->GetWorldPoint(world.data());
  if (world[3] == 0.0) {
    return;
  }
  std::array<int, 3> voxel =
      scene_.worldToVoxel({world[0] / world[3], world[1] / world[3], world[2] / world[3]});
  // Parallel projection: whatever depth the point has, it stays on this view's slice.
  voxel[static_cast<size_t>(axis_)] = scene_.cursor()[static_cast<size_t>(axis_)];
  if (scene_.setCursor(voxel)) {
    emit sceneChanged();
  }
}

void SliceView::mousePressEvent(QMouseEvent* event) {
  setFocus(Qt::MouseFocusReason);
  if (dragButton_ != Qt::NoButton) {
    return;
  }
  dragButton_ = event->button();
  dragFrom_ = event->pos();
  if (dragButton_ == Qt::LeftButton) {
    moveCursorTo(event->pos());
  }
  event->accept();
}

void SliceView::mouseMoveEvent(QMouseEvent* event) {
  if (dragButton_ == Qt::NoButton) {
    return;
  }
  const QPoint delta = event->pos() - dragFrom_;
  dragFrom_ = event->pos();
  switch (dragButton_) {
  case Qt::LeftButton:
    moveCursorTo(event->pos());
    break;
  case Qt::RightButton:
    scene_.adjustWindowLevel(delta.x() / kWindowDragPixels, delta.y() / kWindowDragPixels);
    emit sceneChanged();
    break;
  case Qt::MiddleButton: {
    vtkCamera* camera = renderer_->GetActiveCamera();
    const double worldPerPixel = 2.0 * camera->GetParallelScale() / std::max(1, height());
    for (size_t i = 0; i < 3; ++i) {
      pan_[i] += (-right_[i] * delta.x() + up_[i] * delta.y()) * worldPerPixel;
    }
    fitCamera();
    renderWindow()->Render();
    break;
  }
  default:
    break;
  }
  event->accept();
}

void SliceView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == dragButton_) {
    dragButton_ = Qt::NoButton;
  }
  event->accept();
}

void SliceView::wheelEvent(QWheelEvent* event) {
  const QPoint delta = event->angleDelta();
  const double steps = (delta.y() != 0 ? delta.y() : delta.x()) / 120.0;
  if (event->modifiers() & Qt::ControlModifier) {
    zoom_ = std::clamp(zoom_ * std::pow(1.2, steps), 0.25, 64.0);
    fitCamera();
    renderWindow()->Render();
  } else {
    // Trackpads send fractions of a notch; keep them until they add up to a slice.
    wheelRemainder_ += steps;
    const auto slices = static_cast<int>(wheelRemainder_);
    wheelRemainder_ -= slices;
    if (slices != 0) {
      stepSlice(slices);
    }
  }
  event->accept();
}

void SliceView::keyPressEvent(QKeyEvent* event) {
  switch (event->key()) {
  case Qt::Key_Up:
    stepSlice(1);
    break;
  case Qt::Key_Down:
    stepSlice(-1);
    break;
  case Qt::Key_PageUp:
    stepSlice(10);
    break;
  case Qt::Key_PageDown:
    stepSlice(-10);
    break;
  default:
    QVTKOpenGLNativeWidget::keyPressEvent(event);
    return;
  }
  event->accept();
}

void SliceView::resizeEvent(QResizeEvent* event) {
  QVTKOpenGLNativeWidget::resizeEvent(event);
  fitCamera();
}
