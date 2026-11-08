#include "ViewerWindow.h"

#include "ColorBarWidget.h"
#include "ColorPanel.h"
#include "CommandPalette.h"
#include "FacetPages.h"
#include "FieldList.h"
#include "KeyHints.h"
#include "LayersPanel.h"
#include "LucideIcon.h"
#include "Notice.h"
#include "OutsidePress.h"
#include "OverlayStyle.h"
#include "PanelLabel.h"
#include "PlaybackBar.h"
#include "ProbeCard.h"
#include "ScalarPager.h"
#include "ScalarVizUtils.h"
#include "StylePanel.h"
#include "TemporalSource.h"
#include "ToolRail.h"
#include "mesh_utils.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QSignalBlocker>
#include <QSize>
#include <QStringList>
#include <QTimer>
#include <QVTKOpenGLNativeWidget.h>
#include <QWheelEvent>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkCellData.h>
#include <vtkCellPicker.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkExtractCells.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkIdList.h>
#include <vtkIntArray.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLPolyDataWriter.h>
#include <vtkXMLUnstructuredGridWriter.h>

namespace {

constexpr int kOverlayMargin = 16;
constexpr int kRailIconSize = 20;
constexpr int kRailPanelGap = 8;
constexpr int kFacetLegendMargin = 8;
constexpr double kPlaybackBaseFps = 15.0;
constexpr int kOverlayGap = 8;
constexpr size_t kHistogramBins = 48;
// How long the probe trace reads frames in one event-loop turn, so playback and the
// camera stay smooth while it fills.
constexpr std::chrono::milliseconds kTraceBudget{6};

// Centered on the left edge, in the space above a bottom bar.
QRect railGeometry(const QWidget* viewport, const QWidget* rail, const QWidget* barBelow) {
  const QSize size = rail->sizeHint();
  const int bottom = barBelow && !barBelow->isHidden() ? barBelow->geometry().top() - kOverlayGap
                                                       : viewport->height() - kOverlayMargin;
  const int y = std::max(kOverlayMargin, (kOverlayMargin + bottom - size.height()) / 2);
  return QRect(QPoint(kOverlayMargin, y), size);
}

// Where a panel width wide may go beside the rail: inside the viewport's margins,
// below any top bar and above any bottom bar it would otherwise cover.
QRect railPanelArea(const QWidget* viewport,
                    const QRect& rail,
                    int width,
                    std::initializer_list<const QWidget*> barsAbove,
                    std::initializer_list<const QWidget*> barsBelow) {
  const int left = rail.right() + 1 + kRailPanelGap;
  const QRect column(left, 0, width, viewport->height());
  int top = kOverlayMargin;
  for (const QWidget* bar : barsAbove) {
    if (bar && !bar->isHidden() && bar->geometry().intersects(column)) {
      top = std::max(top, bar->geometry().bottom() + 1 + kOverlayGap);
    }
  }
  int bottom = viewport->height() - kOverlayMargin;
  for (const QWidget* bar : barsBelow) {
    if (bar && !bar->isHidden() && bar->geometry().intersects(column)) {
      bottom = std::min(bottom, bar->geometry().top() - kOverlayGap);
    }
  }
  return {left, top, width, std::max(1, bottom - top)};
}

// Top-aligned with the rail, shortened and lifted to stay inside area.
QRect railPanelGeometry(const QRect& area, const QRect& rail, const QSize& size) {
  const int height = std::min(size.height(), area.height());
  const int y =
      std::clamp(rail.top(), area.top(), std::max(area.top(), area.top() + area.height() - height));
  return {area.left(), y, size.width(), height};
}

// "Name (shortcut)", the tooltip of a rail tool that applies.
QString railToolTip(const QAction* action) {
  const QKeySequence key = action->shortcut();
  return key.isEmpty() ? action->text()
                       : QStringLiteral("%1 (%2)").arg(action->text(),
                                                       key.toString(QKeySequence::NativeText));
}

// Bottom-right, lifted above any bottom bar it would otherwise cover.
QRect legendGeometry(const QWidget* viewport,
                     const QWidget* legend,
                     std::initializer_list<const QWidget*> barsBelow) {
  const QSize hint = legend->sizeHint();
  const int width = std::min(hint.width(), std::max(1, viewport->width() - 2 * kOverlayMargin));
  const int x = std::max(kOverlayMargin, viewport->width() - width - kOverlayMargin);
  const QRect column(x, 0, width, viewport->height());
  int bottom = viewport->height() - kOverlayMargin;
  for (const QWidget* bar : barsBelow) {
    if (bar && !bar->isHidden() && bar->geometry().intersects(column)) {
      bottom = std::min(bottom, bar->geometry().top() - kOverlayGap);
    }
  }
  const int available = std::max(legend->minimumSizeHint().height(), bottom - kOverlayMargin);
  const int height = std::min(hint.height(), available);
  return QRect(x, bottom - height, width, height);
}

// Bottom center, above whichever of barsBelow are showing.
QRect bottomCenterGeometry(const QWidget* viewport,
                           const QWidget* widget,
                           std::initializer_list<const QWidget*> barsBelow) {
  int bottom = viewport->height() - kOverlayMargin;
  for (const QWidget* bar : barsBelow) {
    if (bar && !bar->isHidden()) {
      bottom = std::min(bottom, bar->geometry().top() - kOverlayGap);
    }
  }
  const QSize size = widget->sizeHint();
  const int x = std::max(kOverlayMargin, (viewport->width() - size.width()) / 2);
  const int y = std::max(kOverlayMargin, bottom - size.height());
  return QRect(QPoint(x, y), size);
}

// Top center, under a bar along the top while one shows, and inside the view's margins; a
// long path shortens in the middle, keeping its file name.
QRect noticeGeometry(const QWidget* viewport, const QWidget* notice, const QWidget* barAbove) {
  constexpr int kMaxNoticeWidth = 520;
  const QSize hint = notice->sizeHint();
  const int width = std::min(
      {hint.width(), kMaxNoticeWidth, std::max(1, viewport->width() - 2 * kOverlayMargin)});
  const int top = barAbove && !barAbove->isHidden()
                      ? barAbove->geometry().bottom() + 1 + kOverlayGap
                      : kOverlayMargin;
  return {(viewport->width() - width) / 2, top, width, hint.height()};
}

QString QStringFromUtf8(const std::string& value) {
  return QString::fromUtf8(value.c_str());
}

// Union of selectable scalar fields across all meshes, point fields first then
// cell fields, each group sorted by name. Cell fields are suffixed " (cells)" in
// the colorbar title so the user can tell which association is shown.
// wantComponents selects which arrays are collected: 1 for colorable scalars,
// 3 for vector fields drawn as glyphs.
std::vector<ScalarField> collectScalarUnion(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes,
                                            int wantComponents = 1) {
  std::set<std::string> pointNames;
  std::set<std::string> cellNames;
  for (const auto& mesh : meshes) {
    if (!mesh) {
      continue;
    }
    if (auto* pd = mesh->GetPointData()) {
      for (int i = 0; i < pd->GetNumberOfArrays(); ++i) {
        vtkDataArray* arr = pd->GetArray(i);
        if (arr && arr->GetName() && arr->GetNumberOfComponents() == wantComponents)
          pointNames.insert(arr->GetName());
      }
    }
    if (auto* cd = mesh->GetCellData()) {
      for (int i = 0; i < cd->GetNumberOfArrays(); ++i) {
        vtkDataArray* arr = cd->GetArray(i);
        if (arr && arr->GetName() && arr->GetNumberOfComponents() == wantComponents)
          cellNames.insert(arr->GetName());
      }
    }
  }
  std::vector<ScalarField> fields;
  fields.reserve(pointNames.size() + cellNames.size());
  for (const std::string& name : pointNames) {
    fields.push_back({name, FieldAssociation::Point});
  }
  for (const std::string& name : cellNames) {
    fields.push_back({name, FieldAssociation::Cell});
  }
  return fields;
}

QString scalarTitle(const ScalarField& field) {
  QString title = QStringFromUtf8(field.name);
  if (field.association == FieldAssociation::Cell) {
    title += QStringLiteral(" (cells)");
  }
  return title;
}

// Small circle cursor for paint mode. ponytail: fixed size, not mapped to the
// ring-based brush (rings aren't pixels); it just signals "brush, not camera".
QCursor makeBrushCursor() {
  constexpr int kSize = 20;
  QPixmap pix(kSize, kSize);
  pix.fill(Qt::transparent);
  QPainter painter(&pix);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(QColor(20, 20, 20, 200), 2.0));
  painter.drawEllipse(2, 2, kSize - 4, kSize - 4);
  painter.setPen(QPen(QColor(255, 255, 255, 220), 1.0));
  painter.drawEllipse(2, 2, kSize - 4, kSize - 4);
  return QCursor(pix, kSize / 2, kSize / 2);
}

QColor qColor(const std::array<double, 3>& rgb) {
  return QColor::fromRgbF(static_cast<QColorComponent>(rgb[0]),
                          static_cast<QColorComponent>(rgb[1]),
                          static_cast<QColorComponent>(rgb[2]));
}

QString fromView(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

// Legend rows for a categorical scalar: analysis unique values + LUT colors,
// in ascending value order. Rows stop at the first value the table lacks, so row i
// stays analysis value i (see categoryValue).
std::vector<std::pair<QString, QColor>> categoricalEntries(vtkLookupTable* lut,
                                                           const ScalarAnalysis& analysis) {
  std::vector<std::pair<QString, QColor>> entries;
  if (!lut) {
    return entries;
  }
  entries.reserve(analysis.uniqueValues.size());
  for (const double value : analysis.uniqueValues) {
    const vtkIdType row = lut->GetAnnotatedValueIndex(value);
    if (row < 0 || row >= lut->GetNumberOfTableValues()) {
      break;
    }
    double rgba[4];
    lut->GetTableValue(row, rgba);
    entries.push_back(
        {QString::fromStdString(lut->GetAnnotation(row)), qColor({rgba[0], rgba[1], rgba[2]})});
  }
  return entries;
}

// The palettes a categorical field can take, each with the colors of the field's first
// categories under it; Custom only while the field has colors of its own.
std::vector<PaletteOption> paletteOptions(const ScalarAnalysis& analysis) {
  constexpr size_t kStrip = 8;
  const auto strip = [&analysis](const CategoryColoring& coloring) {
    std::vector<QColor> colors;
    size_t rank = 0;
    for (auto value = analysis.uniqueValues.begin();
         value != analysis.uniqueValues.end() && rank < kStrip;
         ++value, ++rank) {
      colors.push_back(qColor(categoryColor(coloring, analysis.fileColors, *value, rank)));
    }
    return colors;
  };
  std::vector<PaletteOption> options;
  for (const CategoryPalette& palette : categoryPalettes()) {
    CategoryColoring coloring;
    coloring.palette = &palette;
    const bool fromFile = &palette == &categoryPalettes().front() && !analysis.fileColors.empty();
    options.push_back({fromView(palette.id),
                       fromView(palette.label),
                       fromFile ? QStringLiteral("The file's colors") : fromView(palette.detail),
                       strip(coloring)});
  }
  if (!analysis.coloring.customColors.empty()) {
    CategoryColoring custom = analysis.coloring;
    custom.custom = true;
    options.push_back(
        {fromView(kCustomPaletteId),
         QStringLiteral("Custom"),
         QStringLiteral("Your colors over %1").arg(fromView(custom.customBase->label)),
         strip(custom)});
  }
  return options;
}

// The PaletteOption id of how a field is colored now.
QString paletteId(const CategoryColoring& coloring) {
  return fromView(coloring.custom ? kCustomPaletteId : coloring.palette->id);
}

// The category value shown at categoricalEntries()[entry].
std::optional<double> categoryValue(const ScalarAnalysis& analysis, int entry) {
  if (entry < 0 || static_cast<size_t>(entry) >= analysis.uniqueValues.size()) {
    return std::nullopt;
  }
  return *std::next(analysis.uniqueValues.begin(), entry);
}

// What the legend's rows or bar cannot show, e.g. "Cells · 3 categories".
QString legendSubtitle(std::optional<FieldAssociation> association,
                       const ScalarAnalysis& analysis) {
  QStringList parts;
  if (association) {
    parts << (*association == FieldAssociation::Cell ? QStringLiteral("Cells")
                                                     : QStringLiteral("Points"));
  }
  if (analysis.categorical) {
    parts << QStringLiteral("%1 categories").arg(analysis.uniqueValues.size());
  } else {
    parts << (analysis.colormap.map->group == ColormapGroup::Cyclic ? QStringLiteral("cyclic")
                                                                    : QStringLiteral("continuous"));
  }
  return parts.join(QStringLiteral(" · "));
}

// VTK display coordinates of a point in widget: device pixels from the bottom-left.
std::array<double, 2> displayPoint(const QWidget* widget, const QPointF& pos) {
  const double ratio = widget->devicePixelRatioF();
  return {pos.x() * ratio, (widget->height() - pos.y()) * ratio};
}

// The point of widget at VTK display coordinates, the inverse of displayPoint.
QPointF widgetPoint(const QWidget* widget, const std::array<double, 2>& display) {
  const double ratio = widget->devicePixelRatioF();
  return {display[0] / ratio, widget->height() - display[1] / ratio};
}

bool insideScrollArea(QWidget* widget, const QWidget* stop) {
  for (; widget && widget != stop; widget = widget->parentWidget()) {
    if (qobject_cast<QAbstractScrollArea*>(widget)) {
      return true;
    }
  }
  return false;
}

std::optional<std::array<double, 3>> lutRgb(const std::optional<QColor>& color) {
  if (!color) {
    return std::nullopt;
  }
  return std::array<double, 3>{static_cast<double>(color->redF()),
                               static_cast<double>(color->greenF()),
                               static_cast<double>(color->blueF())};
}

class MeshViewport : public QVTKOpenGLNativeWidget {
public:
  MeshViewport(QWidget* parent, std::function<void()> onResize)
      : QVTKOpenGLNativeWidget(parent), onResize_(std::move(onResize)) {}

protected:
  void resizeEvent(QResizeEvent* event) override {
    // VTK must receive the new pixel size before the grid and camera are fitted.
    QVTKOpenGLNativeWidget::resizeEvent(event);
    onResize_();
  }

private:
  std::function<void()> onResize_;
};

// Event filter that keeps VTK interactions predictable:
// - swallow hover-only motion to avoid implicit rotate state,
// - route wheel zoom through a single camera-dolly path,
// - handle the Space (play or cycle fields) and quit hotkeys.
class VtkMouseFilter : public QObject {
public:
  // onPointerEvent gets first refusal on presses, moves (hover included) and releases
  // over the view; true consumes the event. onHover gets where the pointer hovers the
  // view with no button held, also after a release or wheel zoom changed what lies
  // under it, and std::nullopt once it stops: a press or drag, leaving the view,
  // moving onto an overlay, or onPointerEvent consuming the event. onClick gets where a
  // left press was released without dragging, before the release's onHover.
  explicit VtkMouseFilter(QWidget* vtkRoot,
                          std::function<void(Qt::KeyboardModifiers)> onSpace,
                          std::function<bool(QMouseEvent*)> onPointerEvent,
                          std::function<void(const std::optional<QPointF>&)> onHover,
                          std::function<void(const QPointF&)> onClick,
                          std::function<void(bool active, bool erase)> onPaintHold,
                          QObject* parent = nullptr)
      : QObject(parent), vtkRoot_(vtkRoot), onSpace_(std::move(onSpace)),
        onPointerEvent_(std::move(onPointerEvent)), onHover_(std::move(onHover)),
        onClick_(std::move(onClick)), onPaintHold_(std::move(onPaintHold)) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    auto* widget = qobject_cast<QWidget*>(watched);
    QWidget* vtkRoot = vtkRoot_.data();
    if (!widget || !vtkRoot) {
      return QObject::eventFilter(watched, event);
    }

    const bool insideVtkWidget = (widget == vtkRoot || vtkRoot->isAncestorOf(widget));
    if (!insideVtkWidget) {
      return QObject::eventFilter(watched, event);
    }

    // Overlays (rail, panels, legends, bars) handle their own events. A wheel turn
    // over one scrolls a list or does nothing; it never zooms the view behind.
    if (widget != vtkRoot) {
      if (event->type() == QEvent::Enter) {
        hover(std::nullopt);
      }
      if (event->type() == QEvent::Wheel && !insideScrollArea(widget, vtkRoot)) {
        return true;
      }
      return QObject::eventFilter(watched, event);
    }

    // Annotation gets first dibs on pointer events so a paint stroke is not also
    // interpreted as a camera rotate/dolly by VTK.
    if (onPointerEvent_ &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove ||
         event->type() == QEvent::MouseButtonRelease)) {
      if (onPointerEvent_(static_cast<QMouseEvent*>(event))) {
        pressedAt_.reset();
        hover(std::nullopt);
        return true;
      }
    }

    switch (event->type()) {
    case QEvent::MouseMove: {
      auto* me = static_cast<QMouseEvent*>(event);
      if (me->buttons() == Qt::NoButton) {
        hover(mouseLocalPos(me));
        return true; // swallow hover‐only moves
      }
      if (pressedAt_ && (mouseLocalPos(me) - *pressedAt_).manhattanLength() >=
                            QApplication::startDragDistance()) {
        pressedAt_.reset(); // a drag, which turns the camera
      }
      hover(std::nullopt);
      break;
    }
    case QEvent::MouseButtonPress: {
      auto* me = static_cast<QMouseEvent*>(event);
      if (me->button() == Qt::LeftButton && me->buttons() == Qt::LeftButton) {
        pressedAt_ = mouseLocalPos(me);
      } else {
        pressedAt_.reset();
      }
      hover(std::nullopt);
      break;
    }
    case QEvent::Leave:
      hover(std::nullopt);
      break;
    case QEvent::MouseButtonRelease: {
      auto* me = static_cast<QMouseEvent*>(event);
      if (me->button() == Qt::LeftButton && pressedAt_ && onClick_) {
        onClick_(mouseLocalPos(me));
      }
      pressedAt_.reset();
      if (me->buttons() == Qt::NoButton) {
        hover(mouseLocalPos(me));
      }
      break;
    }
    case QEvent::Wheel:
      if (auto* we = static_cast<QWheelEvent*>(event)) {
        auto* vtkView = qobject_cast<QVTKOpenGLNativeWidget*>(vtkRoot);
        if (!vtkView || !vtkView->renderWindow()) {
          return true;
        }

        double steps = 0.0;
        if (!we->pixelDelta().isNull()) {
          steps = static_cast<double>(we->pixelDelta().y()) / 120.0;
        } else {
          steps = static_cast<double>(we->angleDelta().y()) / 120.0;
        }
        if (std::abs(steps) < 1e-6) {
          return true;
        }

        auto* renderWindow = vtkView->renderWindow();
        auto* renderers = renderWindow->GetRenderers();
        if (!renderers) {
          return true;
        }

        vtkCollectionSimpleIterator cameraCookie;
        renderers->InitTraversal(cameraCookie);
        vtkRenderer* renderer = renderers->GetNextRenderer(cameraCookie);
        if (!renderer || !renderer->GetActiveCamera()) {
          return true;
        }

        const double factor = std::pow(1.20, steps);
        renderer->GetActiveCamera()->Dolly(factor);
        renderer->ResetCameraClippingRange();
        renderWindow->Render();
        hover(we->position());
        return true;
      }
      return true;
    case QEvent::HoverMove:
    case QEvent::NativeGesture:
    case QEvent::Gesture:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
      return true; // block trackpad rotate / pinch gestures
    case QEvent::KeyPress: {
      auto* ke = static_cast<QKeyEvent*>(event);
      if (ke->key() == Qt::Key_Space && onSpace_) {
        onSpace_(ke->modifiers());
        return true;
      }
      // Closing rather than quitting lets the window ask about unsaved labels.
      if (ke->key() == Qt::Key_Q) {
        vtkRoot->window()->close();
        return true;
      }
      // Hold 'a' to paint, 'e' to erase; ignore X11 auto-repeat so the hold sticks.
      if (!ke->isAutoRepeat() && onPaintHold_ &&
          (ke->key() == Qt::Key_A || ke->key() == Qt::Key_E)) {
        onPaintHold_(true, ke->key() == Qt::Key_E);
        return true;
      }
      break;
    }
    case QEvent::KeyRelease: {
      auto* ke = static_cast<QKeyEvent*>(event);
      if (!ke->isAutoRepeat() && onPaintHold_ &&
          (ke->key() == Qt::Key_A || ke->key() == Qt::Key_E)) {
        onPaintHold_(false, false);
        return true;
      }
      break;
    }
    case QEvent::ShortcutOverride: {
      auto* ke = static_cast<QKeyEvent*>(event);
      if (ke->key() == Qt::Key_Space || ke->key() == Qt::Key_Q || ke->key() == Qt::Key_A ||
          ke->key() == Qt::Key_E) {
        ke->accept();
        return true;
      }
      break;
    }
    default:
      break;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  void hover(const std::optional<QPointF>& pos) const {
    if (onHover_) {
      onHover_(pos);
    }
  }

  QPointer<QWidget> vtkRoot_;
  std::function<void(Qt::KeyboardModifiers)> onSpace_;
  std::function<bool(QMouseEvent*)> onPointerEvent_;
  std::function<void(const std::optional<QPointF>&)> onHover_;
  std::function<void(const QPointF&)> onClick_;
  std::function<void(bool active, bool erase)> onPaintHold_;
  std::optional<QPointF> pressedAt_; // where a left press that may become a click began
};

// Paintable labels 1..kNumLabels, one swatch each; value 0 is "unlabeled". Kept at
// 9 so the categorical LUT uses the distinct tab10 palette.
constexpr int kNumLabels = AnnotationBar::kLabels;
constexpr size_t kMaxUndo = 50;

// Top center, clear of the rail's column and wrapped to fit beside it.
QRect annotationBarGeometry(const QWidget* viewport, AnnotationBar* bar, const QRect& rail) {
  const int left = rail.right() + 1 + kRailPanelGap;
  const int right = viewport->width() - kOverlayMargin;
  const QSize size = bar->fitWidth(std::max(1, right - left));
  const int x = std::clamp(
      (viewport->width() - size.width()) / 2, left, std::max(left, right - size.width()));
  return {QPoint(x, kOverlayMargin), size};
}

// Grow a set of cells outward from `seed` over shared-vertex adjacency, up to
// `rings` expansion steps (ring 0 = just the seed cell). This walks the mesh
// surface: the far side of a thin sheet shares no vertices with the near side,
// so paint never jumps the gap the way a 3-D sphere brush would.
std::vector<vtkIdType> growCells(vtkDataSet* ds, vtkIdType seed, int rings) {
  std::vector<vtkIdType> out;
  if (!ds || seed < 0) {
    return out;
  }
  std::set<vtkIdType> visited{seed};
  std::vector<vtkIdType> frontier{seed};
  out.push_back(seed);
  auto cellPts = vtkSmartPointer<vtkIdList>::New();
  auto ptCells = vtkSmartPointer<vtkIdList>::New();
  for (int ring = 0; ring < rings && !frontier.empty(); ++ring) {
    std::vector<vtkIdType> next;
    for (vtkIdType cell : frontier) {
      ds->GetCellPoints(cell, cellPts);
      for (vtkIdType i = 0; i < cellPts->GetNumberOfIds(); ++i) {
        ds->GetPointCells(cellPts->GetId(i), ptCells);
        for (vtkIdType j = 0; j < ptCells->GetNumberOfIds(); ++j) {
          const vtkIdType nb = ptCells->GetId(j);
          if (visited.insert(nb).second) {
            next.push_back(nb);
            out.push_back(nb);
          }
        }
      }
    }
    frontier.swap(next);
  }
  return out;
}

} // namespace

ViewerWindow::ViewerWindow(MeshLoadResult loadResult, const ViewerOptions& options, QWidget* parent)
    : ViewerWindow(std::move(loadResult),
                   options,
                   vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New(),
                   parent) {}

ViewerWindow::ViewerWindow(MeshLoadResult loadResult,
                           const ViewerOptions& options,
                           const vtkSmartPointer<vtkGenericOpenGLRenderWindow>& renderWindow,
                           QWidget* parent)
    : QMainWindow(parent), load_(std::move(loadResult)), options_(options),
      temporal_(load_.temporal) {
  setupWindowTitle();
  resize(1300, 980);

  const auto& meshes = load_.meshes.meshes;
  partColors_.reserve(meshes.size());
  for (size_t i = 0; i < meshes.size(); ++i) {
    if (i < load_.meshes.partHasColors.size() && load_.meshes.partHasColors[i] &&
        i < load_.meshes.partColors.size()) {
      partColors_.push_back(load_.meshes.partColors[i]);
    } else {
      partColors_.push_back(generateDistinctColor(static_cast<int>(i)));
    }
  }

  buildViewport(renderWindow);

  if (options_.commonCatLut) {
    std::vector<vtkDataSet*> ptrs;
    ptrs.reserve(meshes.size());
    std::transform(meshes.begin(), meshes.end(), std::back_inserter(ptrs), [](const auto& m) {
      return m.GetPointer();
    });
    renderer_.setSharedCatAnalysis(buildCommonCatAnalysis(ptrs));
  }

  qApp->installEventFilter(new VtkMouseFilter(
      vtkWidget_,
      [this](Qt::KeyboardModifiers modifiers) {
        switch (spaceAction(modifiers, playbackBar_ != nullptr)) {
        case SpaceAction::PlayPause:
          if (playbackBar_) {
            playOrPause(!playbackBar_->isPlaying());
          }
          break;
        case SpaceAction::NextField:
          cycleScalar(1);
          break;
        case SpaceAction::PreviousField:
          cycleScalar(-1);
          break;
        }
      },
      [this](QMouseEvent* e) { return handleAnnotatePointer(e); },
      // While annotation paints, the hover belongs to its brush preview. A pinned card
      // stays at its pin, which a turn or zoom of the camera may have moved.
      [this](const std::optional<QPointF>& pos) {
        if (pin_) {
          refreshProbe();
        } else if (pos && probeAction_ && probeAction_->isChecked() && !paintArmed()) {
          requestProbe(*pos);
        } else {
          hideProbe();
        }
      },
      [this](const QPointF& pos) {
        if (probeAction_ && probeAction_->isChecked() && !paintArmed()) {
          pinProbe(pos);
        }
      },
      [this](bool active, bool erase) { setPaintHold(active, erase); },
      this));

  if (options_.explodeView) {
    setupFacetMode();
  } else {
    setupNormalMode();
  }
  buildRail();
  if (load_.meshes.groups.size() > 1) {
    const size_t count =
        options_.explodeView ? renderer_.getFacetPanelCount() : load_.meshes.groups.size();
    const size_t columns = options_.explodeView ? renderer_.getFacetColumnCount() : 1;
    for (size_t i = 0; i < count; ++i) {
      const size_t file = i / columns;
      panelLabels_.push_back(
          new PanelLabel(file, QStringFromUtf8(load_.meshes.groups[file].name), vtkWidget_));
    }
  }
  if (facetPager_) {
    updateFacetPages();
  }
  layoutOverlays();
  if (playbackBar_) {
    // Narrower, the bar's controls would overlap; shorter, the bar would reach the
    // view's edge or the rail would come down over it.
    QSize minimum = playbackBarViewMinimum(*playbackBar_, kOverlayMargin);
    minimum.rheight() += rail_->sizeHint().height() + kOverlayGap;
    vtkWidget_->setMinimumSize(minimum);
  }
  // A press anywhere else in the view closes the open panel and still does its own job, so
  // a drag on the view still rotates. The rail's buttons toggle and switch panels; the
  // field list and the palette sit above the panel and close on a press of their own.
  // Installed after VtkMouseFilter, so it sees the presses an annotation stroke takes.
  qApp->installEventFilter(new OutsidePress(
      vtkWidget_,
      {rail_, layersPanel_, colorPanel_, stylePanel_, fieldList_, palette_},
      [this]() {
        if (QAction* open = panels_->checkedAction()) {
          open->setChecked(false);
        }
      },
      this));
  if (options_.annotate && annotateAction_->isEnabled()) {
    annotateAction_->setChecked(true);
  }

  vtkWidget_->setFocus();
}

void ViewerWindow::setupWindowTitle() {
  if (load_.meshes.names.empty() || load_.meshes.names.front().empty()) {
    setWindowTitle(QStringLiteral("vv[*]"));
    return;
  }

  const QString source = QStringFromUtf8(load_.meshes.names.front());
  const bool stdinSource = source == QStringLiteral("-");
  const QFileInfo file(source);
  QString title = stdinSource ? QStringLiteral("Standard input") : file.fileName();
  if (load_.meshes.groups.size() > 1) {
    title += QStringLiteral(" + %1 more").arg(load_.meshes.groups.size() - 1);
  } else if (!stdinSource) {
    setWindowFilePath(file.absoluteFilePath());
  }
  setWindowTitle(title + QStringLiteral("[*] - vv"));
}

void ViewerWindow::buildViewport(vtkGenericOpenGLRenderWindow* renderWindow) {
  auto* central = new QWidget(this);
  auto* layout = new QHBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // The VTK 3‑D view
  vtkWidget_ = new MeshViewport(central, [this]() { onViewportResize(); });
  vtkWidget_->setFocusPolicy(Qt::StrongFocus);
  vtkWidget_->setAttribute(Qt::WA_AcceptTouchEvents, false);
  layout->addWidget(vtkWidget_, 1);
  setCentralWidget(central);

  renderWindow->SetMultiSamples(0);
  renderWindow->SetDesiredUpdateRate(120.0);
  vtkWidget_->setRenderWindow(renderWindow);

  renderer_.setRenderContext(renderWindow, vtkWidget_->interactor());

  colorBar_ = new ColorBarWidget(vtkWidget_);
  connect(colorBar_, &ColorBarWidget::sizeHintChanged, this, &ViewerWindow::layoutOverlays);
  colorBar_->setVisible(false);
  colorBar_->setFocusPolicy(Qt::NoFocus);
  connect(colorBar_,
          &ColorBarWidget::colormapChosen,
          this,
          [this](const QString& id, bool reversed) { chooseColormap(id.toStdString(), reversed); });

  // Opened from the rail's Layers button.
  layersPanel_ = new LayersPanel(vtkWidget_);
  layersPanel_->setVisible(false);
  connect(
      layersPanel_, &LayersPanel::visibilityBatchChanged, this, &ViewerWindow::changeVisibility);
  QObject::connect(
      layersPanel_, &LayersPanel::expansionChanged, this, [this]() { layoutOverlays(); });

  // Opened from the rail's Color button.
  colorPanel_ = new ColorPanel(vtkWidget_);
  colorPanel_->setVisible(false);
  QObject::connect(
      colorPanel_, &ColorPanel::colormapChosen, this, [this](const QString& id, bool reversed) {
        chooseColormap(id.toStdString(), reversed);
      });
  QObject::connect(colorPanel_, &ColorPanel::outOfRangeChosen, this, [this](OutOfRange outside) {
    if (renderer_.setOutOfRange(outside)) {
      const ColormapChoice& colormap = renderer_.getActiveScalarAnalysis().colormap;
      colorBar_->setColormap(colormap);
      colorPanel_->setColormap(colormap);
    }
  });
  QObject::connect(colorPanel_, &ColorPanel::clipRangeChanged, this, [this](double lo, double hi) {
    renderer_.setClipRange(lo, hi);
    showClipRange();
  });
  connect(colorPanel_,
          &ColorPanel::clipValueFormatChanged,
          colorBar_,
          &ColorBarWidget::setClipValueFormat);
  QObject::connect(colorPanel_, &ColorPanel::paletteChosen, this, [this](const QString& id) {
    chooseCategoryPalette(id);
  });
  QObject::connect(
      colorPanel_,
      &ColorPanel::categoryColorChanged,
      this,
      [this](int entry, const std::optional<QColor>& color) { changeCategoryColor(entry, color); });

  // Zero-delay single shot: one pick per event-loop turn, at the latest position.
  probeTimer_ = new QTimer(this);
  probeTimer_->setSingleShot(true);
  probeTimer_->setInterval(0);
  QObject::connect(probeTimer_, &QTimer::timeout, this, [this]() { showProbe(); });

  // What an action did, briefly, at the top of the view.
  notice_ = new Notice(vtkWidget_);

  // Opened from the field pager, or the -e grid's page pager.
  fieldList_ = new FieldList(vtkWidget_);
  fieldList_->setVisible(false);
  QObject::connect(fieldList_, &FieldList::pageChosen, this, [this](int index) {
    if (options_.explodeView) {
      showFacetPage(static_cast<size_t>(index) / renderer_.getFacetPageSize());
    } else {
      showPage(index);
    }
  });
  // Back to the view, so Space cycles again.
  QObject::connect(fieldList_, &FieldList::closed, this, [this]() {
    vtkWidget_->setFocus();
    refreshProbe();
  });

  // Opened with Ctrl+K (Cmd+K on macOS).
  palette_ = new CommandPalette(vtkWidget_);
  palette_->setVisible(false);
  // Back to the view, so Space cycles again.
  QObject::connect(palette_, &CommandPalette::closed, this, [this]() {
    vtkWidget_->setFocus();
    refreshProbe();
  });

  // Opened from the rail's Style button.
  stylePanel_ = new StylePanel(vtkWidget_);
  stylePanel_->setVisible(false);
  QObject::connect(stylePanel_, &StylePanel::styleChosen, this, [this](SurfaceStyle style) {
    SurfaceLook look = renderer_.getSurfaceLook();
    look.style = style;
    setSurfaceLook(look);
  });
  QObject::connect(stylePanel_, &StylePanel::xrayOpacityChosen, this, [this](double opacity) {
    SurfaceLook look = renderer_.getSurfaceLook();
    look.xrayOpacity = opacity;
    setSurfaceLook(look);
  });
  QObject::connect(stylePanel_, &StylePanel::glassDensityChosen, this, [this](double density) {
    SurfaceLook look = renderer_.getSurfaceLook();
    look.glassDensity = density;
    setSurfaceLook(look);
  });
}

void ViewerWindow::setupFacetMode() {
  renderer_.setupFacetGrid(load_.meshes.meshes, load_.meshes.groups, partColors_);
  colorBar_->setVisible(false);

  const size_t panelCount = renderer_.getFacetPanelCount();
  facetColorBars_.reserve(panelCount);
  for (size_t panelIndex = 0; panelIndex < panelCount; ++panelIndex) {
    FacetPanelInfo panelInfo;
    // facetColorBars_ stays index-aligned with the panels; plain panels get none.
    if (!renderer_.getFacetPanelInfo(panelIndex, panelInfo) || !panelInfo.hasScalar) {
      facetColorBars_.push_back(nullptr);
      continue;
    }

    if (options_.fixedRange && !panelInfo.analysis.categorical &&
        renderer_.setFacetPanelGlobalRange(
            panelIndex, (*options_.fixedRange)[0], (*options_.fixedRange)[1])) {
      renderer_.getFacetPanelInfo(panelIndex, panelInfo);
    }

    auto* panelBar = new ColorBarWidget(vtkWidget_);
    panelBar->setFocusPolicy(Qt::NoFocus);
    panelBar->setTitle(QStringFromUtf8(panelInfo.title));
    panelBar->setSubtitle(legendSubtitle(std::nullopt, panelInfo.analysis));
    panelBar->setRange(panelInfo.globalRange[0], panelInfo.globalRange[1]);
    panelBar->setColormap(panelInfo.analysis.colormap);

    connect(panelBar,
            &ColorBarWidget::colormapChosen,
            this,
            [this, panelIndex](const QString& id, bool reversed) {
              if (!renderer_.setFacetColormap(panelIndex, id.toStdString(), reversed)) {
                return;
              }
              refreshFacetColumn(panelIndex);
            });
    facetColorBars_.push_back(panelBar);
    connect(panelBar, &ColorBarWidget::sizeHintChanged, this, &ViewerWindow::layoutFacetColorBars);
    if (panelInfo.analysis.categorical) {
      showFacetCategories(panelIndex);
    } else {
      panelBar->setClipRange(panelInfo.clipRange[0], panelInfo.clipRange[1]);
    }

    connect(panelBar,
            &ColorBarWidget::clipRangeChanged,
            this,
            [this, panelIndex](double lo, double hi) {
              if (renderer_.setFacetClipRange(panelIndex, lo, hi)) {
                refreshFacetColumn(panelIndex);
              }
            });
    connect(panelBar,
            &ColorBarWidget::clipValueFormatChanged,
            this,
            [this, panelIndex](ColorBarWidget::Bound bound, ColorBarWidget::ValueFormat format) {
              FacetPanelInfo selected;
              if (!renderer_.getFacetPanelInfo(panelIndex, selected))
                return;
              for (size_t i = 0; i < facetColorBars_.size(); ++i) {
                FacetPanelInfo info;
                if (facetColorBars_[i] && renderer_.getFacetPanelInfo(i, info) &&
                    info.column == selected.column) {
                  facetColorBars_[i]->setClipValueFormat(bound, format);
                }
              }
            });
    connect(panelBar,
            &ColorBarWidget::categoryColorChanged,
            this,
            [this, panelIndex](int entry, const std::optional<QColor>& color) {
              FacetPanelInfo info;
              if (renderer_.getFacetPanelInfo(panelIndex, info)) {
                if (const auto value = categoryValue(info.analysis, entry)) {
                  if (renderer_.setFacetCategoryColor(panelIndex, *value, lutRgb(color))) {
                    refreshFacetColumn(panelIndex);
                  }
                }
              }
            });
    connect(panelBar, &ColorBarWidget::paletteChosen, this, [this, panelIndex](const QString& id) {
      if (renderer_.setFacetCategoryPalette(panelIndex, id.toStdString())) {
        refreshFacetColumn(panelIndex);
      }
    });
  }

  // More fields than the view holds page through like the single view's fields.
  facetPager_ = new ScalarPager(PagerPlacement::Capsule, vtkWidget_);
  facetPager_->setStepNames(QStringLiteral("Previous page"), QStringLiteral("Next page"));
  facetPager_->setVisible(false);
  QObject::connect(
      facetPager_, &ScalarPager::stepRequested, this, [this](int step) { cycleScalar(step); });
  QObject::connect(facetPager_, &ScalarPager::listRequested, this, [this]() { openFieldList(); });
  updateFacetPages();
  renderer_.startFacetGrid();
}

void ViewerWindow::setupNormalMode() {
  renderer_.setup(load_.meshes.meshes, load_.meshes.groups, partColors_);
  renderer_.start();

  fillLayersPanel();

  QObject::connect(
      colorBar_, &ColorBarWidget::clipRangeChanged, this, [this](double lo, double hi) {
        renderer_.setClipRange(lo, hi);
        showClipRange();
      });
  QObject::connect(
      colorBar_,
      &ColorBarWidget::categoryColorChanged,
      this,
      [this](int entry, const std::optional<QColor>& color) { changeCategoryColor(entry, color); });
  QObject::connect(colorBar_, &ColorBarWidget::paletteChosen, this, [this](const QString& id) {
    chooseCategoryPalette(id);
  });

  scalarFields_ = collectScalarUnion(load_.meshes.meshes);
  vectorFields_ = collectScalarUnion(load_.meshes.meshes, 3);
  // Read before a time series loads its first frame, which holds only the shown field's
  // point arrays.
  fieldDetails_.reserve(scalarFields_.size());
  for (const ScalarField& field : scalarFields_) {
    fieldDetails_.push_back(
        legendSubtitle(field.association, renderer_.analyzeField(field.name, field.association)));
  }
  // A time series switches fields from its playback bar.
  if (temporal_ && temporal_->playable()) {
    setupPlayback();
  }
  if (!scalarFields_.empty()) {
    scalarPager_ =
        playbackBar_ ? playbackBar_->pager() : new ScalarPager(PagerPlacement::Capsule, vtkWidget_);
    QObject::connect(
        scalarPager_, &ScalarPager::stepRequested, this, [this](int step) { cycleScalar(step); });
    QObject::connect(
        scalarPager_, &ScalarPager::listRequested, this, [this]() { openFieldList(); });
    applyScalarAtIndex(0);
  } else {
    if (playbackBar_) {
      playbackBar_->hidePager();
    }
    applyNoScalar();
  }
  if (playbackBar_) {
    restartTrace();
  }
}

void ViewerWindow::changeVisibility(const std::vector<LayerVisibilityChange>& changes) {
  std::vector<PartVisibilityChange> parts;
  parts.reserve(changes.size());
  for (const auto& change : changes) {
    const auto& group = load_.meshes.groups[static_cast<size_t>(change.file)];
    parts.push_back({group.partIndices[static_cast<size_t>(change.part)], change.visible});
  }
  renderer_.setPartsVisible(parts);
  refreshProbe();
}

void ViewerWindow::fillLayersPanel() {
  std::vector<LayerFile> files;
  files.reserve(load_.meshes.groups.size());
  for (const MeshGroup& group : load_.meshes.groups) {
    LayerFile file;
    file.name = QStringFromUtf8(group.name);
    if (!group.partIndices.empty() && group.partIndices.front() < load_.meshes.names.size()) {
      const QString path = QStringFromUtf8(load_.meshes.names[group.partIndices.front()]);
      file.path = path == QStringLiteral("-") ? QStringLiteral("Standard input")
                                              : QFileInfo(path).absoluteFilePath();
    }
    file.parts.reserve(group.partIndices.size());
    for (size_t partIndex : group.partIndices) {
      const std::array<double, 3>& rgb = partColors_[partIndex];
      file.parts.push_back({QStringFromUtf8(load_.meshes.partNames[partIndex]),
                            QColor::fromRgbF(static_cast<QColorComponent>(rgb[0]),
                                             static_cast<QColorComponent>(rgb[1]),
                                             static_cast<QColorComponent>(rgb[2]))});
    }
    files.push_back(std::move(file));
  }
  layersPanel_->setFiles(files);
}

void ViewerWindow::setupPlayback() {
  const int numSteps = temporal_->steps();
  std::vector<double> times;
  times.reserve(static_cast<size_t>(numSteps));
  for (int step = 0; step < numSteps; ++step) {
    times.push_back(temporal_->timeAt(step));
  }
  playbackBar_ = new PlaybackBar(std::move(times), vtkWidget_);
  playbackBar_->raise();
  playbackBar_->show();

  // HDF5 readers are not safe beside playback's reads on another thread, so the trace
  // reads a few frames per event-loop turn here, into a dataset of its own.
  traceFrame_ =
      vtkSmartPointer<vtkDataSet>::Take(load_.meshes.meshes[load_.temporalMesh]->NewInstance());
  traceTimer_ = new QTimer(this);
  traceTimer_->setInterval(0);
  QObject::connect(traceTimer_, &QTimer::timeout, this, &ViewerWindow::readTrace);

  playTimer_ = new QTimer(this);

  // Playback is clock-driven, not tick-driven: each timeout jumps to the frame the
  // elapsed time calls for. If decoding or rendering cannot keep up (high speeds,
  // heavy meshes) frames are dropped instead of queueing up, so the animation runs
  // at the requested rate rather than in slow motion.
  QObject::connect(playTimer_, &QTimer::timeout, this, [this, numSteps]() {
    const double elapsed = double(playClock_.elapsed()) / 1000.0;
    const int advance =
        static_cast<int>(elapsed * kPlaybackBaseFps * playbackBar_->speedMultiplier());
    int next = playAnchorStep_ + advance;
    if (next >= numSteps) {
      if (!playbackBar_->loopEnabled()) {
        playTimer_->stop();
        playbackBar_->setPlaying(false);
        showFrame(numSteps - 1);
        return;
      }
      next %= numSteps;
      playAnchorStep_ = next;
      playClock_.restart();
    }
    if (next != currentPlaybackStep_) {
      showFrame(next);
    }
  });

  QObject::connect(playbackBar_, &PlaybackBar::playToggled, this, [this](bool playing) {
    playOrPause(playing);
  });

  QObject::connect(playbackBar_, &PlaybackBar::stepRequested, this, [this](int step) {
    showFrame(step);
    restartPlayClock();
  });

  // The arrows step a frame like the bar's buttons; a text field with the focus keeps them.
  struct FrameKey {
    Qt::Key key;
    int delta;
  };
  for (const FrameKey frameKey : {FrameKey{Qt::Key_Left, -1}, FrameKey{Qt::Key_Right, 1}}) {
    auto* action = new QAction(this);
    action->setShortcut(QKeySequence(frameKey.key));
    addAction(action);
    QObject::connect(action, &QAction::triggered, this, [this, delta = frameKey.delta]() {
      const int step = std::clamp(currentPlaybackStep_ + delta, 0, temporal_->steps() - 1);
      if (step != currentPlaybackStep_) {
        showFrame(step);
        restartPlayClock();
      }
    });
  }

  QObject::connect(playbackBar_, &PlaybackBar::speedChanged, this, [this](double) {
    restartPlayClock();
    if (playTimer_->isActive()) {
      applyPlayTimerInterval();
    }
  });
}

void ViewerWindow::showFrame(int step) {
  if (!temporal_ || step < 0 || step >= temporal_->steps() || load_.meshes.meshes.empty()) {
    return;
  }
  temporal_->readStepInto(step, load_.meshes.meshes[load_.temporalMesh]);
  renderer_.refreshAfterDataChange(load_.temporalMesh);
  currentPlaybackStep_ = step;
  if (playbackBar_) {
    playbackBar_->setStep(step);
  }
  if (colorPanel_->isVisible()) {
    updateColorRange();
  }
  refreshProbe();
}

void ViewerWindow::playOrPause(bool playing) {
  playbackBar_->setPlaying(playing);
  if (!playing) {
    playTimer_->stop();
    return;
  }
  // Restart from the beginning if paused at the last frame.
  if (currentPlaybackStep_ >= temporal_->steps() - 1) {
    showFrame(0);
  }
  restartPlayClock();
  applyPlayTimerInterval();
  playTimer_->start();
}

void ViewerWindow::restartPlayClock() {
  playAnchorStep_ = currentPlaybackStep_;
  playClock_.restart();
}

void ViewerWindow::applyPlayTimerInterval() {
  const double fps = kPlaybackBaseFps * playbackBar_->speedMultiplier();
  playTimer_->setInterval(std::max(4, static_cast<int>(std::round(1000.0 / fps))));
}

void ViewerWindow::ensureLabelArray() {
  vtkDataSet* mesh = renderer_.getPrimaryMesh();
  if (!mesh) {
    return;
  }
  labelArray_ = vtkIntArray::SafeDownCast(mesh->GetCellData()->GetArray("label"));
  if (!labelArray_) {
    auto arr = vtkSmartPointer<vtkIntArray>::New();
    arr->SetName("label");
    arr->SetNumberOfComponents(1);
    arr->SetNumberOfTuples(mesh->GetNumberOfCells());
    arr->FillComponent(0, 0.0);
    mesh->GetCellData()->AddArray(arr);
    labelArray_ = arr;
  }
  // GetPointCells (used by the surface brush BFS) needs the reverse links built.
  if (auto* pd = vtkPolyData::SafeDownCast(mesh)) {
    pd->BuildLinks();
  } else if (auto* ug = vtkUnstructuredGrid::SafeDownCast(mesh)) {
    ug->BuildLinks();
  }
}

void ViewerWindow::setupAnnotation() {
  ensureLabelArray();
  picker_ = vtkSmartPointer<vtkCellPicker>::New();
  picker_->SetTolerance(0.0005);
  // Only the labelled mesh's actor: the brush preview, glyphs and other parts would
  // answer with cell ids of other datasets.
  picker_->PickFromListOn();
  if (!renderer_.context.actors.empty()) {
    picker_->AddPickList(renderer_.context.actors.front());
  }

  // Fixed value→color LUT so a value keeps its color while the array is edited.
  std::set<double> domain;
  for (int v = 0; v <= kNumLabels; ++v) {
    domain.insert(static_cast<double>(v));
  }
  // Tab10, the colors labels had before palettes could be picked.
  CategoryColoring labelColors;
  labelColors.palette = findCategoryPalette("tab10");
  labelLut_ = createCategoricalLookupTable(domain, labelColors, {});
  labelLut_->SetTableValue(0, 0.6, 0.6, 0.6, 1.0); // value 0 = unlabeled → grey
  labelLut_->Modified();

  annotationBar_ = new AnnotationBar(vtkWidget_);
  annotationBar_->setArrayName(QStringLiteral("label"));
  std::array<QColor, kNumLabels> colors;
  for (int v = 1; v <= kNumLabels; ++v) {
    double rgb[3];
    labelLut_->GetColor(static_cast<double>(v), rgb);
    colors[static_cast<size_t>(v - 1)] = QColor::fromRgbF(static_cast<QColorComponent>(rgb[0]),
                                                          static_cast<QColorComponent>(rgb[1]),
                                                          static_cast<QColorComponent>(rgb[2]));
  }
  annotationBar_->setLabelColors(colors);
  annotationBar_->setLabel(currentLabel_);
  annotationBar_->setBrushRings(brushRings_);
  QObject::connect(annotationBar_, &AnnotationBar::toolChosen, this, [this](AnnotationTool tool) {
    tool_ = tool;
    applyTool();
  });
  QObject::connect(
      annotationBar_, &AnnotationBar::labelChosen, this, [this](int label) { pickLabel(label); });
  QObject::connect(annotationBar_, &AnnotationBar::brushRingsChosen, this, [this](int rings) {
    setBrushRings(rings);
  });
  QObject::connect(annotationBar_, &AnnotationBar::undoRequested, this, [this]() { undoStroke(); });
  QObject::connect(
      annotationBar_, &AnnotationBar::saveRequested, this, [this]() { saveAnnotations(); });
  QObject::connect(annotationBar_, &AnnotationBar::leaveRequested, this, [this]() {
    annotateAction_->setChecked(false);
  });

  const QString undo = QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText);
  keyHints_ = new KeyHints({{{QStringLiteral("A")}, QStringLiteral("hold to paint")},
                            {{QStringLiteral("E")}, QStringLiteral("hold to erase")},
                            {{QStringLiteral("1–9")}, QStringLiteral("pick label")},
                            {{QStringLiteral("+"), QStringLiteral("−")}, QStringLiteral("brush")},
                            {{undo}, QStringLiteral("undo")}},
                           vtkWidget_);
}

// Leaving keeps the labels, the tool, label and brush for the next entry.
void ViewerWindow::setAnnotating(bool on) {
  if (on == annotating_) {
    return;
  }
  if (on) {
    if (!annotationBar_) {
      setupAnnotation();
    }
    double clip[2] = {0.0, 1.0};
    renderer_.getClipRange(clip);
    clipBeforeAnnotating_ = {clip[0], clip[1]};
    annotating_ = true;
    applyLabelColoring();
  } else {
    endStroke();
    heldTool_.reset();
    annotating_ = false;
    // Back to the field shown before, with its clip window.
    applyScalarAtIndex(activeScalarIdx_);
    if (activeScalarIdx_ >= 0 && !renderer_.getActiveScalarAnalysis().categorical &&
        renderer_.setClipRange(clipBeforeAnnotating_[0], clipBeforeAnnotating_[1])) {
      showClipRange();
    }
  }
  annotationBar_->setVisible(on);
  keyHints_->setVisible(on);
  if (scalarPager_) {
    scalarPager_->setVisible(!on);
  }
  syncDigitKeys();
  syncColorPanel();
  applyTool();
  refreshProbe();
  layoutOverlays();
}

AnnotationTool ViewerWindow::activeTool() const {
  return heldTool_.value_or(tool_);
}

void ViewerWindow::applyTool() {
  annotationBar_->setTool(activeTool());
  if (paintArmed()) {
    vtkWidget_->setCursor(makeBrushCursor());
    hideProbe();
  } else {
    vtkWidget_->setCursor(sceneCursor());
  }
  refreshBrushPreview();
}

// Press-and-hold from VtkMouseFilter: A arms Paint and E arms Erase while held; the
// release goes back to the tool chosen on the bar.
void ViewerWindow::setPaintHold(bool active, bool erase) {
  if (!annotating_) {
    return;
  }
  if (active) {
    heldTool_ = erase ? AnnotationTool::Erase : AnnotationTool::Paint;
  } else {
    heldTool_.reset();
  }
  applyTool();
}

bool ViewerWindow::handleAnnotatePointer(QMouseEvent* event) {
  if (strokeValue_ && strokeReleaseMissed(event->type(), event->button(), event->buttons())) {
    endStroke();
  }
  switch (event->type()) {
  case QEvent::MouseButtonPress:
    if (strokeValue_) {
      return true; // another button during a stroke
    }
    if (!paintArmed()) {
      return false;
    }
    strokeValue_ = strokeLabel(activeTool(), event->button(), currentLabel_);
    if (!strokeValue_) {
      return false;
    }
    currentStroke_.clear();
    clearBrushPreview();
    paintAtWidgetPos(mouseLocalPos(event), *strokeValue_);
    return true;
  case QEvent::MouseMove:
    if (strokeValue_) {
      paintAtWidgetPos(mouseLocalPos(event), *strokeValue_);
      return true;
    }
    if (paintArmed() && event->buttons() == Qt::NoButton) {
      updateBrushPreview(mouseLocalPos(event));
    }
    return false;
  case QEvent::MouseButtonRelease:
    if (!strokeValue_) {
      return false;
    }
    if (event->buttons() == Qt::NoButton) {
      endStroke();
    }
    return true;
  default:
    return false;
  }
}

vtkIdType ViewerWindow::annotationCellAt(const QPointF& pos) {
  vtkRenderer* ren = renderer_.getRenderer();
  if (!ren || !picker_) {
    return -1;
  }
  const std::array<double, 2> display = displayPoint(vtkWidget_, pos);
  return picker_->Pick(display[0], display[1], 0.0, ren) != 0 ? picker_->GetCellId() : -1;
}

void ViewerWindow::paintAtWidgetPos(const QPointF& pos, int value) {
  vtkDataSet* mesh = renderer_.getPrimaryMesh();
  const vtkIdType cell = annotationCellAt(pos);
  if (!mesh || !labelArray_ || cell < 0) {
    return;
  }
  bool changed = false;
  for (vtkIdType c : growCells(mesh, cell, brushRings_)) {
    if (c < 0 || c >= labelArray_->GetNumberOfTuples()) {
      continue;
    }
    const int prev = labelArray_->GetValue(c);
    if (prev == value) {
      continue;
    }
    currentStroke_.emplace_back(static_cast<long long>(c), prev);
    labelArray_->SetValue(c, value);
    changed = true;
  }
  if (!changed) {
    return;
  }
  // Fixed LUT covers every value, so edits recolor live — no LUT rebuild needed.
  labelArray_->Modified();
  if (renderer_.context.window) {
    renderer_.context.window->Render();
  }
}

void ViewerWindow::endStroke() {
  strokeValue_.reset();
  if (currentStroke_.empty()) {
    return;
  }
  undoStack_.push_back(std::move(currentStroke_));
  currentStroke_.clear();
  if (undoStack_.size() > kMaxUndo) {
    undoStack_.erase(undoStack_.begin());
  }
  annotationBar_->setUndoAvailable(true);
  setWindowModified(true);
}

void ViewerWindow::updateBrushPreview(const QPointF& pos) {
  vtkRenderer* ren = renderer_.getRenderer();
  vtkDataSet* mesh = renderer_.getPrimaryMesh();
  const vtkIdType cell = annotationCellAt(pos);
  if (!ren || !mesh || !labelLut_ || cell < 0) {
    clearBrushPreview();
    return;
  }

  const std::vector<vtkIdType> ids = growCells(mesh, cell, brushRings_);
  auto idList = vtkSmartPointer<vtkIdList>::New();
  idList->SetNumberOfIds(static_cast<vtkIdType>(ids.size()));
  for (size_t i = 0; i < ids.size(); ++i) {
    idList->SetId(static_cast<vtkIdType>(i), ids[i]);
  }
  auto extract = vtkSmartPointer<vtkExtractCells>::New();
  extract->SetInputData(mesh);
  extract->SetCellList(idList);
  extract->Update();

  if (!previewActor_) {
    previewActor_ = vtkSmartPointer<vtkActor>::New();
    auto mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    mapper->ScalarVisibilityOff();
    mapper->SetResolveCoincidentTopologyToPolygonOffset(); // draw over the mesh, no z-fight
    previewActor_->SetMapper(mapper);
    previewActor_->GetProperty()->SetOpacity(0.5);
    previewActor_->GetProperty()->SetLighting(false);
    ren->AddActor(previewActor_);
  }
  vtkDataSetMapper::SafeDownCast(previewActor_->GetMapper())->SetInputData(extract->GetOutput());

  // What a left click would write.
  double rgb[3];
  labelLut_->GetColor(
      static_cast<double>(strokeLabel(activeTool(), Qt::LeftButton, currentLabel_).value_or(0)),
      rgb);
  previewActor_->GetProperty()->SetColor(rgb);
  previewActor_->VisibilityOn();
  if (renderer_.context.window) {
    renderer_.context.window->Render();
  }
}

void ViewerWindow::refreshBrushPreview() {
  const QPoint pos = vtkWidget_->mapFromGlobal(QCursor::pos());
  if (paintArmed() && !strokeValue_ && vtkWidget_->rect().contains(pos) &&
      !vtkWidget_->childAt(pos)) {
    updateBrushPreview(pos);
  } else {
    clearBrushPreview();
  }
}

void ViewerWindow::clearBrushPreview() {
  if (previewActor_ && previewActor_->GetVisibility()) {
    previewActor_->VisibilityOff();
    if (renderer_.context.window) {
      renderer_.context.window->Render();
    }
  }
}

void ViewerWindow::pickLabel(int label) {
  currentLabel_ = label;
  annotationBar_->setLabel(label);
  refreshBrushPreview();
}

void ViewerWindow::setBrushRings(int rings) {
  brushRings_ = std::clamp(rings, 0, AnnotationBar::kMaxBrushRings);
  annotationBar_->setBrushRings(brushRings_);
  refreshBrushPreview();
}

void ViewerWindow::undoStroke() {
  if (!labelArray_ || undoStack_.empty() || strokeValue_) {
    return;
  }
  for (const auto& [cell, prev] : undoStack_.back()) {
    if (cell >= 0 && cell < labelArray_->GetNumberOfTuples()) {
      labelArray_->SetValue(static_cast<vtkIdType>(cell), prev);
    }
  }
  undoStack_.pop_back();
  annotationBar_->setUndoAvailable(!undoStack_.empty());
  setWindowModified(true);
  labelArray_->Modified();
  if (renderer_.context.window) {
    renderer_.context.window->Render();
  }
}

void ViewerWindow::applyLabelColoring() {
  const double range[2] = {0.0, static_cast<double>(kNumLabels)};
  renderer_.colorByFixedCategorical("label", FieldAssociation::Cell, labelLut_, range);
  colorBar_->setVisible(false); // the swatches are the legend
}

bool ViewerWindow::saveAnnotations() {
  endStroke();
  vtkDataSet* mesh = renderer_.getPrimaryMesh();
  if (!mesh) {
    return false;
  }
  auto* pd = vtkPolyData::SafeDownCast(mesh);
  auto* ug = vtkUnstructuredGrid::SafeDownCast(mesh);
  if (!pd && !ug) {
    QMessageBox::warning(this,
                         QStringLiteral("Save failed"),
                         QStringLiteral("Only polydata/unstructured-grid meshes can be saved."));
    return false;
  }

  const bool poly = pd != nullptr;
  QString suggested;
  if (!load_.meshes.names.empty()) {
    QFileInfo fi(QStringFromUtf8(load_.meshes.names.front()));
    suggested = fi.dir().filePath(fi.completeBaseName() + (poly ? QStringLiteral(".labeled.vtp")
                                                                : QStringLiteral(".labeled.vtu")));
  }
  const QString filter = poly ? QStringLiteral("VTK PolyData (*.vtp)")
                              : QStringLiteral("VTK UnstructuredGrid (*.vtu)");
  const QString path = QFileDialog::getSaveFileName(this, "Save annotations", suggested, filter);
  if (path.isEmpty()) {
    return false;
  }

  const std::string p = path.toUtf8().toStdString();
  int ok = 0;
  if (poly) {
    auto w = vtkSmartPointer<vtkXMLPolyDataWriter>::New();
    w->SetFileName(p.c_str());
    w->SetInputData(pd);
    w->SetDataModeToBinary(); // VTK 9.1's reader can't read its own appended-mode output
    ok = w->Write();
  } else {
    auto w = vtkSmartPointer<vtkXMLUnstructuredGridWriter>::New();
    w->SetFileName(p.c_str());
    w->SetInputData(ug);
    w->SetDataModeToBinary(); // VTK 9.1's reader can't read its own appended-mode output
    ok = w->Write();
  }
  if (ok == 0) {
    QMessageBox::warning(
        this, QStringLiteral("Save failed"), QStringLiteral("Could not write ") + path);
    return false;
  }
  setWindowModified(false);
  notify(QStringLiteral("Saved ") + QDir::toNativeSeparators(path), NoticeKind::Info);
  return true;
}

void ViewerWindow::syncDigitKeys() {
  // Both sets use the digits, and a key with two enabled actions triggers neither.
  for (QAction* key : styleKeys_) {
    key->setEnabled(styleAction_->isChecked() && !annotating_);
  }
  for (QAction* key : annotateKeys_) {
    key->setEnabled(annotating_);
  }
}

void ViewerWindow::closeEvent(QCloseEvent* event) {
  endStroke();
  if (isWindowModified()) {
    QMessageBox ask(QMessageBox::Warning,
                    QStringLiteral("Unsaved labels"),
                    QStringLiteral("Save the labels you painted before closing?"),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                    this);
    ask.setInformativeText(QStringLiteral("Labels you don't save are lost."));
    ask.setDefaultButton(QMessageBox::Save);
    const int answer = ask.exec();
    if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !saveAnnotations())) {
      event->ignore();
      return;
    }
  }
  QMainWindow::closeEvent(event);
}

void ViewerWindow::applyNoScalar() {
  renderer_.clearActiveScalar();
  colorBar_->setVisible(false);
  activeScalarIdx_ = -1;
  syncPager();
}

void ViewerWindow::applyScalarAtIndex(int index) {
  if (index < 0 || index >= static_cast<int>(scalarFields_.size())) {
    applyNoScalar();
    return;
  }

  const ScalarField& field = scalarFields_[static_cast<size_t>(index)];
  const std::string& scalarName = field.name;

  // Temporal: restrict frame reads to this array and reload the current frame so
  // the mesh holds it before the scalar is applied. VTKHDF's array selection covers
  // point data only, so there a cell field reads every cell array per frame.
  const bool temporalField = temporal_ && temporal_->playable();
  if (temporalField) {
    temporal_->setActiveArray(scalarName);
    temporal_->readStepInto(currentPlaybackStep_, load_.meshes.meshes[load_.temporalMesh]);
  }

  if (!renderer_.setActiveScalar(scalarName, field.association)) {
    return;
  }
  activeScalarIdx_ = index;
  fieldDetails_[static_cast<size_t>(index)] =
      legendSubtitle(field.association, renderer_.getActiveScalarAnalysis());
  colorBar_->setTitle(QStringFromUtf8(field.name));
  syncPager();

  // For temporal data, fix the color range to the union across sampled steps so
  // the colormap does not flicker as frames advance.
  if (temporalField) {
    auto cached = temporalRangeCache_.find(scalarName);
    if (cached == temporalRangeCache_.end()) {
      double sampled[2];
      if (temporal_->sampledScalarRange(scalarName, sampled)) {
        cached =
            temporalRangeCache_.emplace(scalarName, std::array<double, 2>{sampled[0], sampled[1]})
                .first;
      }
    }
    if (cached != temporalRangeCache_.end()) {
      renderer_.setActiveScalarRange(cached->second[0], cached->second[1]);
    }
  }

  const ScalarAnalysis& analysis = renderer_.getActiveScalarAnalysis();
  if (options_.fixedRange && !analysis.categorical) {
    renderer_.setActiveScalarRange((*options_.fixedRange)[0], (*options_.fixedRange)[1]);
  }

  double globalRange[2] = {0.0, 1.0};
  if (!renderer_.getActiveScalarGlobalRange(globalRange)) {
    colorBar_->setVisible(false);
    return;
  }
  colorBar_->setVisible(true);
  colorBar_->setRange(globalRange[0], globalRange[1]);
  colorBar_->setSubtitle(fieldDetails_[static_cast<size_t>(index)]);

  colorBar_->setColormap(analysis.colormap);
  if (analysis.categorical) {
    showCategoryLegend();
  } else {
    colorBar_->clearCategorical();
    double clipRange[2] = {globalRange[0], globalRange[1]};
    renderer_.getClipRange(clipRange);
    colorBar_->setClipRange(clipRange[0], clipRange[1]);
  }
  layoutOverlays(); // the legend's height follows its rows
}

void ViewerWindow::cycleScalar(int step) {
  // Wrapping both ways, through the -e grid's pages of fields there.
  if (options_.explodeView) {
    const auto pages = static_cast<int>(renderer_.getFacetPageCount());
    const int page = static_cast<int>(renderer_.getFacetPage()) + step;
    showFacetPage(static_cast<size_t>((page % pages + pages) % pages));
    return;
  }
  const int pages = static_cast<int>(scalarFields_.size()) + 1;
  showPage(((activeScalarIdx_ + 1 + step) % pages + pages) % pages);
}

void ViewerWindow::showPage(int page) {
  // The labels stay on view while annotating; leaving shows the field again.
  if (annotating_) {
    return;
  }
  applyScalarAtIndex(page - 1);
  syncColorPanel();
  refreshProbe();
  restartTrace();
}

SearchItem ViewerWindow::pageItem(int page) const {
  SearchItem item;
  item.current = page == activeScalarIdx_ + 1;
  if (page == 0) {
    item.text = QStringLiteral("Geometry");
    item.detail = QStringLiteral("Part colors");
  } else {
    const auto index = static_cast<size_t>(page - 1);
    item.text = QStringFromUtf8(scalarFields_[index].name);
    item.detail = fieldDetails_[index];
  }
  return item;
}

void ViewerWindow::syncPager() {
  if (!scalarPager_) {
    return;
  }
  const int page = activeScalarIdx_ + 1;
  const SearchItem item = pageItem(page);
  scalarPager_->setPage(item.text, item.detail, page, static_cast<int>(scalarFields_.size()));
}

void ViewerWindow::openFieldList() {
  std::vector<SearchItem> pages;
  if (options_.explodeView) {
    pages = facetFieldItems();
  } else {
    pages.reserve(scalarFields_.size() + 1);
    for (int page = 0; page <= static_cast<int>(scalarFields_.size()); ++page) {
      pages.push_back(pageItem(page));
    }
  }
  hideProbe();
  const ScalarPager* pager = facetPager_ ? facetPager_ : scalarPager_;
  fieldList_->setAnchor(QRect(pager->mapTo(vtkWidget_, QPoint(0, 0)), pager->size()));
  fieldList_->open(pages);
}

void ViewerWindow::togglePalette() {
  if (palette_->isVisible()) {
    palette_->dismiss();
    return;
  }
  fieldList_->dismiss();
  hideProbe();
  palette_->open(paletteItems());
}

std::vector<PaletteItem> ViewerWindow::paletteItems() {
  std::vector<PaletteItem> items;
  if (scalarPager_) {
    for (int page = 0; page <= static_cast<int>(scalarFields_.size()); ++page) {
      SearchItem row = pageItem(page);
      row.group = QStringLiteral("Fields");
      row.enabled = !annotating_;
      if (annotating_) {
        row.toolTip = QStringLiteral("Fields: not available while annotating");
      }
      items.push_back({row, [this, page]() { showPage(page); }});
    }
  }
  if (renderer_.getFacetPageCount() > 1) {
    std::vector<SearchItem> fields = facetFieldItems();
    for (size_t field = 0; field < fields.size(); ++field) {
      fields[field].group = QStringLiteral("Fields");
      items.push_back({fields[field],
                       [this, field]() { showFacetPage(field / renderer_.getFacetPageSize()); }});
    }
  }
  // A field is shown, and nothing else stops it changing color.
  const ScalarAnalysis& analysis = renderer_.getActiveScalarAnalysis();
  if (colorAction_->isEnabled() && analysis.categorical) {
    const QString current = paletteId(analysis.coloring);
    for (const PaletteOption& palette : paletteOptions(analysis)) {
      SearchItem row;
      row.text = palette.label;
      row.detail = palette.detail;
      row.current = palette.id == current;
      row.group = QStringLiteral("Palettes");
      row.icon = paletteIcon(palette.colors, QSize(16, 16));
      items.push_back({row, [this, id = palette.id]() { chooseCategoryPalette(id); }});
    }
  } else if (colorAction_->isEnabled()) {
    const ColormapChoice& choice = analysis.colormap;
    for (const Colormap& map : colormaps()) {
      SearchItem row;
      row.text = fromView(map.label);
      row.detail = colormapGroupName(map.group);
      row.current = &map == choice.map;
      row.group = QStringLiteral("Colormaps");
      row.icon = colormapIcon(map, choice.reversed, QSize(16, 16));
      items.push_back({row, [this, id = std::string(map.id), reversed = choice.reversed]() {
                         chooseColormap(id, reversed);
                       }});
    }
  }
  for (QAction* action : commands_) {
    SearchItem row;
    row.text = action->text();
    row.shortcut = action->shortcut().toString(QKeySequence::NativeText);
    row.current = action->isChecked();
    row.group = QStringLiteral("Commands");
    row.icon = action->icon();
    row.enabled = action->isEnabled();
    if (!row.enabled) {
      row.toolTip = action->toolTip(); // says why
    }
    items.push_back({row, [action]() { action->trigger(); }});
  }
  return items;
}

void ViewerWindow::chooseColormap(const std::string& id, bool reversed) {
  if (activeScalarIdx_ < 0 || !renderer_.setColormap(id, reversed)) {
    return;
  }
  const ScalarAnalysis& analysis = renderer_.getActiveScalarAnalysis();
  const auto index = static_cast<size_t>(activeScalarIdx_);
  // A cyclic map makes the field "cyclic" on the legend, the pager and the field list.
  fieldDetails_[index] = legendSubtitle(scalarFields_[index].association, analysis);
  colorBar_->setColormap(analysis.colormap);
  colorBar_->setSubtitle(fieldDetails_[index]);
  syncPager();
  colorPanel_->setColormap(analysis.colormap);
}

void ViewerWindow::syncColorPanel() {
  QString unavailable;
  const ScalarAnalysis& analysis = renderer_.getActiveScalarAnalysis();
  if (options_.explodeView) {
    unavailable =
        QStringLiteral("Color: click a legend gradient or category swatch in the facet grid");
  } else if (annotating_) {
    unavailable = QStringLiteral("Color: not available while annotating");
  } else if (activeScalarIdx_ < 0) {
    unavailable = QStringLiteral("Color: no field shown");
  }
  colorAction_->setEnabled(unavailable.isEmpty());
  if (!unavailable.isEmpty()) {
    colorAction_->setToolTip(unavailable);
    colorAction_->setChecked(false);
    return;
  }
  colorAction_->setToolTip(railToolTip(colorAction_));
  colorPanel_->setField(QStringFromUtf8(scalarFields_[static_cast<size_t>(activeScalarIdx_)].name));
  if (analysis.categorical) {
    colorPanel_->setCategories(paletteOptions(analysis),
                               paletteId(analysis.coloring),
                               categoricalEntries(renderer_.getActiveLUT(), analysis));
  } else {
    colorPanel_->setColormap(analysis.colormap);
  }
  if (colorPanel_->isVisible()) {
    updateColorRange();
    layoutOverlays(); // the panel's sections follow the field's kind
  }
}

void ViewerWindow::showCategoryLegend() {
  const ScalarAnalysis& analysis = renderer_.getActiveScalarAnalysis();
  colorBar_->setCategorical(categoricalEntries(renderer_.getActiveLUT(), analysis));
  colorBar_->setPalettes(paletteOptions(analysis), paletteId(analysis.coloring));
}

void ViewerWindow::refreshFacetColumn(size_t panel) {
  FacetPanelInfo selected;
  if (!renderer_.getFacetPanelInfo(panel, selected)) {
    return;
  }
  for (size_t i = 0; i < facetColorBars_.size(); ++i) {
    FacetPanelInfo info;
    if (!facetColorBars_[i] || !renderer_.getFacetPanelInfo(i, info) ||
        info.column != selected.column) {
      continue;
    }
    if (info.analysis.categorical) {
      showFacetCategories(i);
    } else {
      facetColorBars_[i]->setColormap(info.analysis.colormap);
      facetColorBars_[i]->setClipRange(info.clipRange[0], info.clipRange[1]);
      facetColorBars_[i]->setSubtitle(legendSubtitle(std::nullopt, info.analysis));
    }
  }
  syncFacetPager();
  refreshProbe();
}

void ViewerWindow::showFacetCategories(size_t panel) {
  FacetPanelInfo info;
  if (panel < facetColorBars_.size() && facetColorBars_[panel] &&
      renderer_.getFacetPanelInfo(panel, info)) {
    facetColorBars_[panel]->setCategorical(
        categoricalEntries(renderer_.getFacetPanelLUT(panel), info.analysis));
    facetColorBars_[panel]->setPalettes(paletteOptions(info.analysis),
                                        paletteId(info.analysis.coloring));
  }
}

void ViewerWindow::chooseCategoryPalette(const QString& id) {
  if (renderer_.setCategoryPalette(id.toStdString())) {
    showCategoryColors();
  }
}

void ViewerWindow::changeCategoryColor(int entry, const std::optional<QColor>& color) {
  const auto value = categoryValue(renderer_.getActiveScalarAnalysis(), entry);
  if (value && renderer_.setCategoryColor(*value, lutRgb(color))) {
    showCategoryColors();
  }
}

void ViewerWindow::showCategoryColors() {
  showCategoryLegend();
  syncColorPanel();
  refreshProbe(); // the card's swatch
}

void ViewerWindow::updateColorRange() {
  double range[2] = {0.0, 1.0};
  if (renderer_.getActiveScalarAnalysis().categorical ||
      !renderer_.getActiveScalarGlobalRange(range)) {
    return;
  }
  colorPanel_->setRange(range[0], range[1], renderer_.getActiveScalarHistogram(kHistogramBins));
  showClipRange();
}

void ViewerWindow::setSurfaceLook(const SurfaceLook& look) {
  if (!seeThrough(look.style)) {
    styleBeforeSeeThrough_ = look.style;
  }
  renderer_.setSurfaceLook(look);
  stylePanel_->setLook(renderer_.getSurfaceLook());
  layoutOverlays(); // the panel shows the Opacity row for X-ray, Density for Glass
}

void ViewerWindow::showClipRange() {
  double clip[2] = {0.0, 1.0};
  renderer_.getClipRange(clip);
  colorBar_->setClipRange(clip[0], clip[1]);
  colorPanel_->setClipRange(clip[0], clip[1]);
}

// 'v': cycle vector-field glyphs (each 3-component array, then off).
void ViewerWindow::cycleVectorField() {
  if (vectorFields_.empty()) {
    notify(QStringLiteral("No vector field in this mesh"), NoticeKind::Error);
    return;
  }
  activeVectorIdx_ = (activeVectorIdx_ + 1) % (static_cast<int>(vectorFields_.size()) + 1);
  if (activeVectorIdx_ == static_cast<int>(vectorFields_.size())) {
    activeVectorIdx_ = -1;
    renderer_.setVectorGlyphs("", FieldAssociation::Point);
    notify(QStringLiteral("Vector glyphs off"), NoticeKind::Info);
  } else {
    const ScalarField& field = vectorFields_[static_cast<size_t>(activeVectorIdx_)];
    if (renderer_.setVectorGlyphs(field.name, field.association)) {
      notify(QStringLiteral("Vector glyphs: ") + scalarTitle(field), NoticeKind::Info);
    }
  }
  if (vectorsAction_) {
    vectorsAction_->setChecked(activeVectorIdx_ >= 0);
  }
}

void ViewerWindow::updateFacetPages() {
  // The pager, with the margin under it and the gap above it, while it shows.
  const int pagerStrip = facetPager_->sizeHint().height() + kOverlayMargin + kOverlayGap;
  const size_t pageSize = std::max<size_t>(1,
                                           facetPageSize(renderer_.getFacetColumnCount(),
                                                         load_.meshes.groups.size(),
                                                         vtkWidget_->width(),
                                                         vtkWidget_->height(),
                                                         pagerStrip));
  const size_t firstShown = renderer_.getFacetPage() * renderer_.getFacetPageSize();
  renderer_.setFacetPages(
      pageSize,
      firstShown / pageSize,
      static_cast<int>(std::lround(pagerStrip * vtkWidget_->devicePixelRatioF())));
  syncFacetPager();
}

void ViewerWindow::showFacetPage(size_t page) {
  if (page == renderer_.getFacetPage()) {
    return;
  }
  renderer_.showFacetPage(page);
  syncFacetPager();
  layoutOverlays();
  refreshProbe();
}

void ViewerWindow::syncFacetPager() {
  const size_t pages = renderer_.getFacetPageCount();
  facetPager_->setVisible(pages > 1);
  if (pages <= 1) {
    return;
  }
  const size_t fields = renderer_.getFacetColumnCount();
  const size_t page = renderer_.getFacetPage();
  const size_t first = page * renderer_.getFacetPageSize();
  const size_t count = std::min(renderer_.getFacetPageSize(), fields - first);
  QStringList names;
  for (size_t column = first; column < first + count; ++column) {
    FacetPanelInfo info;
    renderer_.getFacetPanelInfo(column, info);
    names << QStringFromUtf8(info.title);
  }
  facetPager_->setPage(facetPageLabel(first, count, fields),
                       names.join(QStringLiteral(", ")),
                       static_cast<int>(page) + 1,
                       static_cast<int>(pages));
}

std::vector<SearchItem> ViewerWindow::facetFieldItems() const {
  const size_t fields = renderer_.getFacetColumnCount();
  const size_t pageSize = renderer_.getFacetPageSize();
  std::vector<SearchItem> items;
  items.reserve(fields);
  for (size_t column = 0; column < fields; ++column) {
    // The column's first panel whose file has the field, which knows its kind.
    FacetPanelInfo info;
    size_t panel = column;
    while (renderer_.getFacetPanelInfo(panel, info) && !info.hasScalar) {
      panel += fields;
    }
    SearchItem item;
    item.text = QStringFromUtf8(info.title);
    item.detail = legendSubtitle(info.association, info.analysis);
    item.current = column / pageSize == renderer_.getFacetPage();
    items.push_back(item);
  }
  return items;
}

void ViewerWindow::buildRail() {
  rail_ = new ToolRail(vtkWidget_);
  const auto addTool = [this](Lucide icon, const QString& name, const QKeySequence& key) {
    auto* action = new QAction(lucideIcon(icon, kRailIconSize), name, this);
    action->setShortcut(key);
    action->setToolTip(railToolTip(action));
    rail_->addTool(action);
    commands_.push_back(action);
    return action;
  };

  layersAction_ = addTool(Lucide::Layers, QStringLiteral("Layers"), QKeySequence(Qt::Key_L));
  layersAction_->setCheckable(true);
  if (options_.explodeView || load_.meshes.groups.empty()) {
    layersAction_->setEnabled(false);
    layersAction_->setToolTip(options_.explodeView
                                  ? QStringLiteral("Layers: not available in the -e facet grid")
                                  : QStringLiteral("Layers: no parts to show"));
  }
  QObject::connect(layersAction_, &QAction::toggled, this, [this](bool open) {
    layersPanel_->setVisible(open);
    layoutOverlays();
  });

  colorAction_ = addTool(Lucide::Palette, QStringLiteral("Color"), QKeySequence(Qt::Key_C));
  colorAction_->setCheckable(true);
  QObject::connect(colorAction_, &QAction::toggled, this, [this](bool open) {
    // The histogram is only computed while someone can see it.
    if (open) {
      updateColorRange();
    }
    colorPanel_->setVisible(open);
    layoutOverlays();
  });
  syncColorPanel();
  // One panel beside the rail at a time.
  panels_ = new QActionGroup(this);
  panels_->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
  panels_->addAction(layersAction_);
  panels_->addAction(colorAction_);

  styleAction_ = addTool(Lucide::Box, QStringLiteral("Style"), QKeySequence(Qt::Key_S));
  styleAction_->setCheckable(true);
  panels_->addAction(styleAction_);
  QObject::connect(styleAction_, &QAction::toggled, this, [this](bool open) {
    stylePanel_->setVisible(open);
    syncDigitKeys();
    layoutOverlays();
  });
  // Only while the panel is open, so the digits stay free otherwise.
  struct StyleKey {
    Qt::Key key;
    SurfaceStyle style;
  };
  for (const StyleKey styleKey : {StyleKey{Qt::Key_1, SurfaceStyle::Solid},
                                  StyleKey{Qt::Key_2, SurfaceStyle::SolidWithEdges},
                                  StyleKey{Qt::Key_3, SurfaceStyle::Wireframe}}) {
    auto* action = new QAction(this);
    action->setShortcut(QKeySequence(styleKey.key));
    action->setEnabled(false);
    addAction(action);
    QObject::connect(action, &QAction::triggered, this, [this, style = styleKey.style]() {
      SurfaceLook look = renderer_.getSurfaceLook();
      look.style = style;
      setSurfaceLook(look);
    });
    styleKeys_.push_back(action);
  }
  // X and G toggle their style from anywhere, back to the last opaque one.
  struct SeeThroughKey {
    Lucide icon;
    const char* name;
    Qt::Key key;
    SurfaceStyle style;
  };
  for (const SeeThroughKey seeThroughKey :
       {SeeThroughKey{Lucide::ScanEye, "X-ray", Qt::Key_X, SurfaceStyle::XRay},
        SeeThroughKey{Lucide::Gem, "Glass", Qt::Key_G, SurfaceStyle::Glass}}) {
    auto* action = new QAction(
        lucideIcon(seeThroughKey.icon, kRailIconSize), QString::fromUtf8(seeThroughKey.name), this);
    action->setShortcut(QKeySequence(seeThroughKey.key));
    addAction(action);
    commands_.push_back(action);
    QObject::connect(action, &QAction::triggered, this, [this, style = seeThroughKey.style]() {
      SurfaceLook look = renderer_.getSurfaceLook();
      look.style = look.style == style ? styleBeforeSeeThrough_ : style;
      setSurfaceLook(look);
    });
  }

  // A mode beside the panels, not one of them: it stays on while a panel opens.
  probeAction_ = addTool(Lucide::Crosshair, QStringLiteral("Probe"), QKeySequence(Qt::Key_P));
  probeAction_->setCheckable(true);
  QObject::connect(probeAction_, &QAction::toggled, this, [this](bool on) {
    if (!paintArmed()) {
      vtkWidget_->setCursor(sceneCursor());
    }
    if (!on) {
      unpin();
      hideProbe();
      return;
    }
    // Read what is already under the pointer, e.g. after pressing P over a mesh.
    const QPoint pos = vtkWidget_->mapFromGlobal(QCursor::pos());
    if (!paintArmed() && vtkWidget_->rect().contains(pos) && !vtkWidget_->childAt(pos)) {
      requestProbe(pos);
    }
  });

  // A mode like Probe: a panel may open while it is on.
  annotateAction_ = addTool(Lucide::Brush, QStringLiteral("Annotate"), QKeySequence(Qt::Key_B));
  annotateAction_->setCheckable(true);
  QString annotateUnavailable;
  if (load_.meshes.groups.size() != 1) {
    annotateUnavailable = QStringLiteral("Annotate: needs a single file");
  } else if (options_.explodeView) {
    annotateUnavailable = QStringLiteral("Annotate: not available in the -e facet grid");
  } else if (temporal_ && temporal_->playable()) {
    // A frame replaces the mesh's arrays, labels included.
    annotateUnavailable = QStringLiteral("Annotate: not available for a time series");
  }
  if (!annotateUnavailable.isEmpty()) {
    annotateAction_->setEnabled(false);
    annotateAction_->setToolTip(annotateUnavailable);
  }
  QObject::connect(
      annotateAction_, &QAction::toggled, this, [this](bool on) { setAnnotating(on); });
  // Live while annotating; the digits pick labels then, not styles.
  for (int label = 1; label <= kNumLabels; ++label) {
    auto* action = new QAction(this);
    action->setShortcut(QKeySequence(Qt::Key_0 + label));
    addAction(action);
    QObject::connect(action, &QAction::triggered, this, [this, label]() { pickLabel(label); });
    annotateKeys_.push_back(action);
  }
  struct BrushKey {
    Qt::Key key;
    int step;
  };
  // '=' and '_' so the brush keys need no Shift.
  for (const BrushKey brushKey : {BrushKey{Qt::Key_Plus, 1},
                                  BrushKey{Qt::Key_Equal, 1},
                                  BrushKey{Qt::Key_Minus, -1},
                                  BrushKey{Qt::Key_Underscore, -1}}) {
    auto* action = new QAction(this);
    action->setShortcut(QKeySequence(brushKey.key));
    addAction(action);
    QObject::connect(action, &QAction::triggered, this, [this, step = brushKey.step]() {
      setBrushRings(brushRings_ + step);
    });
    annotateKeys_.push_back(action);
  }
  auto* undo = new QAction(this);
  undo->setShortcut(QKeySequence(QKeySequence::Undo));
  addAction(undo);
  QObject::connect(undo, &QAction::triggered, this, [this]() { undoStroke(); });
  annotateKeys_.push_back(undo);
  syncDigitKeys();

  vectorsAction_ = addTool(Lucide::Wind, QStringLiteral("Vector glyphs"), QKeySequence(Qt::Key_V));
  vectorsAction_->setCheckable(true);
  if (vectorFields_.empty()) {
    vectorsAction_->setEnabled(false);
    vectorsAction_->setToolTip(QStringLiteral("Vector glyphs: no 3-component arrays"));
  }
  // triggered, not toggled: a click steps to the next field, and cycleVectorField
  // sets the checked state to whether glyphs are showing.
  QObject::connect(vectorsAction_, &QAction::triggered, this, [this]() { cycleVectorField(); });

  rail_->addSeparator();
  QObject::connect(
      addTool(Lucide::RotateCcw, QStringLiteral("Reset view"), QKeySequence(Qt::Key_R)),
      &QAction::triggered,
      this,
      [this]() {
        renderer_.resetCamera();
        refreshProbe();
      });
  QObject::connect(addTool(Lucide::Camera, QStringLiteral("Save snapshot"), QKeySequence()),
                   &QAction::triggered,
                   this,
                   [this]() { saveSnapshot(); });

  // Qt reads Ctrl as Cmd on macOS. A search field takes neither, so it opens the palette
  // while one has the focus, and closes it from its own.
  auto* palette = new QAction(QStringLiteral("Command palette"), this);
  palette->setShortcut(QKeySequence(static_cast<int>(Qt::CTRL) | Qt::Key_K));
  addAction(palette);
  QObject::connect(palette, &QAction::triggered, this, [this]() { togglePalette(); });

  // Esc dismisses the open panel before changing the active interaction.
  auto* escape = new QAction(this);
  escape->setShortcut(QKeySequence(Qt::Key_Escape));
  addAction(escape);
  QObject::connect(escape, &QAction::triggered, this, [this]() {
    if (QAction* open = panels_->checkedAction()) {
      open->setChecked(false);
    } else if (pin_) {
      unpin();
    } else {
      annotateAction_->setChecked(false);
    }
  });
  rail_->show();
}

// The 3-D view with the legends that explain its colors; the rail, panels and
// bars are left out.
void ViewerWindow::saveSnapshot() {
  QString suggested = QStringLiteral("vv.png");
  if (!load_.meshes.names.empty()) {
    const QFileInfo fi(QStringFromUtf8(load_.meshes.names.front()));
    suggested = fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".png"));
  }
  const QString path = QFileDialog::getSaveFileName(
      this, QStringLiteral("Save snapshot"), suggested, QStringLiteral("PNG image (*.png)"));
  if (path.isEmpty()) {
    return;
  }

  std::vector<ColorBarWidget*> legends = facetColorBars_;
  legends.push_back(colorBar_);
  const QImage image =
      snapshotWithLegends(vtkWidget_->grabFramebuffer(),
                          vtkWidget_->devicePixelRatioF(),
                          legends,
                          std::vector<QWidget*>(panelLabels_.begin(), panelLabels_.end()));
  if (!image.save(path, "PNG")) {
    QMessageBox::warning(
        this, QStringLiteral("Save failed"), QStringLiteral("Could not write ") + path);
    return;
  }
  notify(QStringLiteral("Saved ") + QDir::toNativeSeparators(path), NoticeKind::Info);
}

bool ViewerWindow::paintArmed() const {
  return annotating_ && activeTool() != AnnotationTool::Rotate;
}

QCursor ViewerWindow::sceneCursor() const {
  return probeAction_ && probeAction_->isChecked() ? Qt::CrossCursor : Qt::ArrowCursor;
}

void ViewerWindow::requestProbe(const QPointF& pos) {
  probePos_ = pos;
  if (!probeTimer_->isActive()) {
    probeTimer_->start();
  }
}

void ViewerWindow::refreshProbe() {
  if (pin_ || std::any_of(probeCards_.begin(), probeCards_.end(), [](const auto* card) {
        return card->isVisible();
      })) {
    requestProbe(probePos_);
  }
}

void ViewerWindow::hideProbe() {
  probeTimer_->stop();
  for (ProbeCard* card : probeCards_) {
    card->hide();
  }
}

void ViewerWindow::showProbe() {
  // A pinned card waits under the palette and the field list, which hid it.
  if (palette_->isVisible() || fieldList_->isVisible()) {
    hideProbe();
    return;
  }
  std::optional<ProbeHit> hit;
  if (pin_) {
    hit = renderer_.probeAgain(*pin_);
  } else {
    const std::array<double, 2> display = displayPoint(vtkWidget_, probePos_);
    hit = renderer_.probe(display[0], display[1]);
  }
  if (!hit) {
    hideProbe();
    return;
  }
  const auto matches = renderer_.matchingProbes(*hit);
  while (probeCards_.size() < matches.size()) {
    auto* card = new ProbeCard(vtkWidget_);
    connect(card, &ProbeCard::unpinRequested, this, &ViewerWindow::unpin);
    probeCards_.push_back(card);
  }
  for (size_t i = 0; i < probeCards_.size(); ++i) {
    ProbeCard* card = probeCards_[i];
    if (i >= matches.size()) {
      card->hide();
      continue;
    }
    const auto& match = matches[i];
    const auto display = renderer_.displayPosition(match);
    double viewport[4];
    if (!display || !renderer_.getPanelViewport(match.panel, viewport)) {
      card->hide();
      continue;
    }
    const QPoint point = widgetPoint(vtkWidget_, *display).toPoint();
    const QRect bounds(QPoint(static_cast<int>(viewport[0] * vtkWidget_->width()),
                              static_cast<int>((1.0 - viewport[3]) * vtkWidget_->height())),
                       QPoint(static_cast<int>(viewport[2] * vtkWidget_->width()) - 1,
                              static_cast<int>((1.0 - viewport[1]) * vtkWidget_->height()) - 1));
    if (!bounds.contains(point)) {
      card->hide();
      continue;
    }
    ProbeReading reading;
    reading.source = meshSource(match.mesh);
    reading.field = QStringFromUtf8(match.field);
    reading.value = match.value;
    if (match.category) {
      reading.category = {QStringFromUtf8(match.category->label), qColor(match.category->rgb)};
    }
    reading.cell = static_cast<qint64>(match.cell);
    card->setPinned(pin_.has_value());
    card->setReading(reading);
    card->moveNear(point, bounds);
    card->show();
  }
  stackOverlays();
}

void ViewerWindow::pinProbe(const QPointF& pos) {
  const std::array<double, 2> display = displayPoint(vtkWidget_, pos);
  const std::optional<ProbeHit> hit = renderer_.probe(display[0], display[1]);
  if (!hit) {
    unpin();
    return;
  }
  pin_ = hit;
  renderer_.setPinMarker(pin_);
  showProbe();
  restartTrace();
}

void ViewerWindow::unpin() {
  if (!pin_) {
    return;
  }
  pin_.reset();
  renderer_.setPinMarker(std::nullopt);
  hideProbe();
  restartTrace();
}

void ViewerWindow::restartTrace() {
  if (!playbackBar_) {
    return;
  }
  const bool traceable = pin_ && activeScalarIdx_ >= 0 && pin_->mesh == load_.temporalMesh;
  trace_.reset(traceable ? temporal_->steps() : 0);
  if (traceable) {
    traceTimer_->start();
  } else {
    traceTimer_->stop();
  }
  showTrace();
}

void ViewerWindow::readTrace() {
  if (!pin_ || activeScalarIdx_ < 0) {
    traceTimer_->stop();
    return;
  }
  readTraceFrames(*temporal_,
                  traceFrame_,
                  pin_->at,
                  scalarFields_[static_cast<size_t>(activeScalarIdx_)],
                  renderer_.getActiveScalarAnalysis(),
                  trace_,
                  kTraceBudget);
  if (trace_.complete()) {
    traceTimer_->stop();
  }
  showTrace();
}

void ViewerWindow::showTrace() {
  QString label;
  if (activeScalarIdx_ >= 0) {
    label =
        pin_ && trace_.frames() > 0
            ? QStringLiteral("%1 at cell %2")
                  .arg(QStringFromUtf8(scalarFields_[static_cast<size_t>(activeScalarIdx_)].name))
                  .arg(pin_->cell)
            : QStringLiteral("Pin a point with the probe to trace it");
  }
  playbackBar_->setTraceLabel(label);
  double range[2] = {0.0, 1.0};
  if (trace_.frames() > 0 && renderer_.getActiveScalarGlobalRange(range)) {
    playbackBar_->setTrace(trace_.values(), range[0], range[1]);
  } else {
    playbackBar_->setTrace({}, 0.0, 1.0);
  }
}

// What tells the mesh apart from the others: its file, after the file's letter, among
// several files, its part among several parts of its file, or both.
QString ViewerWindow::meshSource(size_t mesh) const {
  const std::vector<MeshGroup>& groups = load_.meshes.groups;
  for (size_t index = 0; index < groups.size(); ++index) {
    const MeshGroup& group = groups[index];
    if (std::find(group.partIndices.begin(), group.partIndices.end(), mesh) ==
        group.partIndices.end()) {
      continue;
    }
    QStringList names;
    if (groups.size() > 1) {
      names << fileLetter(index) << QStringFromUtf8(group.name);
    }
    if (group.partIndices.size() > 1 && mesh < load_.meshes.partNames.size()) {
      names << QStringFromUtf8(load_.meshes.partNames[mesh]);
    }
    return names.join(QStringLiteral(" · "));
  }
  return {};
}

void ViewerWindow::onViewportResize() {
  if (facetPager_) {
    updateFacetPages();
  }
  renderer_.refitCameraIfUntouched();
  // The widget's resizeEvent has already drawn the panels as they were laid out before.
  vtkWidget_->renderWindow()->Render();
  layoutOverlays();
  refreshProbe();
}

void ViewerWindow::notify(const QString& text, NoticeKind kind) {
  notice_->post(text, kind);
  notice_->setGeometry(noticeGeometry(vtkWidget_, notice_, annotationBar_));
  stackOverlays();
}

void ViewerWindow::layoutOverlays() {
  // In a time series the pager sits inside the playback bar.
  ScalarPager* listPager = facetPager_ ? facetPager_ : scalarPager_;
  QWidget* bottomPager = playbackBar_ ? nullptr : listPager;
  if (playbackBar_) {
    playbackBar_->setGeometry(
        playbackBarGeometry(vtkWidget_->size(), *playbackBar_, kOverlayMargin));
  } else if (listPager) {
    listPager->setGeometry(bottomCenterGeometry(vtkWidget_, listPager, {}));
  }
  fieldList_->setGeometry(vtkWidget_->rect());
  if (listPager) {
    fieldList_->setAnchor(QRect(listPager->mapTo(vtkWidget_, QPoint(0, 0)), listPager->size()));
  }
  palette_->setGeometry(vtkWidget_->rect());
  if (rail_) {
    rail_->setGeometry(railGeometry(vtkWidget_, rail_, playbackBar_));
    if (annotating_) {
      annotationBar_->setGeometry(
          annotationBarGeometry(vtkWidget_, annotationBar_, rail_->geometry()));
      // In the pager's place, shown only where it clears the rail's column.
      const QRect hints = bottomCenterGeometry(vtkWidget_, keyHints_, {playbackBar_});
      keyHints_->setGeometry(hints);
      keyHints_->setVisible(hints.left() > rail_->geometry().right() + kRailPanelGap &&
                            hints.right() < vtkWidget_->width() - kOverlayMargin);
    }
    const auto besideRail = [this, bottomPager](int width) {
      return railPanelArea(vtkWidget_,
                           rail_->geometry(),
                           width,
                           {annotationBar_},
                           {playbackBar_, keyHints_, bottomPager});
    };
    if (!layersPanel_->isHidden()) {
      const QRect area = besideRail(layersPanel_->sizeHint().width());
      layersPanel_->setGeometry(
          railPanelGeometry(area, rail_->geometry(), layersPanel_->fittedSize(area.height())));
    }
    if (!colorPanel_->isHidden()) {
      const QRect area = besideRail(colorPanel_->sizeHint().width());
      colorPanel_->setGeometry(
          railPanelGeometry(area, rail_->geometry(), colorPanel_->fittedSize(area.height())));
    }
    if (!stylePanel_->isHidden()) {
      const QSize size = stylePanel_->sizeHint();
      stylePanel_->setGeometry(
          railPanelGeometry(besideRail(size.width()), rail_->geometry(), size));
    }
  }
  if (!colorBar_->isHidden()) {
    colorBar_->setGeometry(
        legendGeometry(vtkWidget_, colorBar_, {playbackBar_, keyHints_, bottomPager}));
  }
  // After the annotation bar, which it goes under.
  notice_->setGeometry(noticeGeometry(vtkWidget_, notice_, annotationBar_));
  layoutPanelLabels();
  layoutFacetColorBars();
  stackOverlays();
}

void ViewerWindow::stackOverlays() {
  colorBar_->raise();
  for (ColorBarWidget* legend : facetColorBars_) {
    if (legend) {
      legend->raise();
    }
  }
  for (ProbeCard* card : probeCards_) {
    card->raise();
  }
  overlay::stackOverlays({playbackBar_,
                          scalarPager_,
                          facetPager_,
                          keyHints_,
                          notice_,
                          rail_,
                          layersPanel_,
                          colorPanel_,
                          stylePanel_,
                          annotationBar_,
                          fieldList_,
                          palette_});
}

void ViewerWindow::layoutPanelLabels() {
  for (size_t i = 0; i < panelLabels_.size(); ++i) {
    PanelLabel* frame = panelLabels_[i];
    double viewport[4];
    const bool shown = renderer_.getPanelViewport(i, viewport);
    frame->setVisible(shown);
    if (!shown) {
      continue;
    }
    const int left = static_cast<int>(std::lround(viewport[0] * vtkWidget_->width()));
    const int top = static_cast<int>(std::lround((1.0 - viewport[3]) * vtkWidget_->height()));
    const int right = static_cast<int>(std::lround(viewport[2] * vtkWidget_->width()));
    const int bottom = static_cast<int>(std::lround((1.0 - viewport[1]) * vtkWidget_->height()));
    frame->setGeometry(left, top, right - left, bottom - top);
    FacetPanelInfo info;
    const bool leading = !options_.explodeView ||
                         (renderer_.getFacetPanelInfo(i, info) &&
                          info.column == renderer_.getFacetPage() * renderer_.getFacetPageSize());
    frame->setCaption(leading ? PanelCaption::Filename : PanelCaption::Badge);
    frame->setStatus(options_.explodeView && !info.hasScalar && !info.title.empty()
                         ? QStringLiteral("No %1").arg(QStringFromUtf8(info.title))
                         : QString());
    frame->lower();
  }
}

// Each panel's legend sits in its bottom-right corner, in the form its panel's size calls for.
void ViewerWindow::layoutFacetColorBars() {
  const int viewportWidth = vtkWidget_->width();
  const int viewportHeight = vtkWidget_->height();
  for (size_t panelIndex = 0; panelIndex < facetColorBars_.size(); ++panelIndex) {
    ColorBarWidget* legend = facetColorBars_[panelIndex];
    FacetPanelInfo panelInfo;
    if (!legend || !renderer_.getFacetPanelInfo(panelIndex, panelInfo)) {
      continue;
    }
    legend->setVisible(panelInfo.shown);
    if (!panelInfo.shown) {
      continue;
    }
    const int panelRight = static_cast<int>(std::round(panelInfo.viewport[2] * viewportWidth));
    const int panelBottom =
        static_cast<int>(std::round((1.0 - panelInfo.viewport[1]) * viewportHeight));
    const int panelWidth =
        std::max(1,
                 static_cast<int>(
                     std::round((panelInfo.viewport[2] - panelInfo.viewport[0]) * viewportWidth)));
    const int panelHeight =
        std::max(1,
                 static_cast<int>(
                     std::round((panelInfo.viewport[3] - panelInfo.viewport[1]) * viewportHeight)));

    const QSize size = legend->fitPanel(QSize(panelWidth, panelHeight), kFacetLegendMargin);
    legend->setGeometry(panelRight - kFacetLegendMargin - size.width(),
                        panelBottom - kFacetLegendMargin - size.height(),
                        size.width(),
                        size.height());
  }
}
