#include "ViewerWindow.h"

#include "ColorBarWidget.h"
#include "ColorPanel.h"
#include "CommandPalette.h"
#include "FacetPages.h"
#include "FieldChip.h"
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
#include "RegionBorders.h"
#include "ScalarPager.h"
#include "ScalarVizUtils.h"
#include "SliceView.h"
#include "StylePanel.h"
#include "TemporalSource.h"
#include "ToolRail.h"
#include "VolumeScene.h"
#include "mesh_utils.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
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
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSize>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QVTKOpenGLNativeWidget.h>
#include <QWheelEvent>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
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
#include <vtkDataSetWriter.h>
#include <vtkErrorCode.h>
#include <vtkExtractCells.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkIdList.h>
#include <vtkIntArray.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLPolyDataWriter.h>
#include <vtkXMLUnstructuredGridWriter.h>

namespace {

constexpr int kOverlayMargin = 16;
constexpr int kRailIconSize = 20;
// Between the 3-D view and the slice views, and between slice views.
constexpr int kSliceViewGap = 4;
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
                    const std::vector<const QWidget*>& barsBelow) {
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

// A viewport of the view, in the view's logical pixels.
QRect viewportRect(const QWidget* view, const double viewport[4]) {
  const int left = static_cast<int>(std::lround(viewport[0] * view->width()));
  const int top = static_cast<int>(std::lround((1.0 - viewport[3]) * view->height()));
  const int right = static_cast<int>(std::lround(viewport[2] * view->width()));
  const int bottom = static_cast<int>(std::lround((1.0 - viewport[1]) * view->height()));
  return {left, top, right - left, bottom - top};
}

// A tile's legend in its bottom-right corner, lifted above its pane's pager where that
// would cover it. A whole pane large enough takes the full card; any other tile the form
// its size calls for.
QRect legendGeometry(const QRect& tile, ColorBarWidget* legend, PaneMode mode, const QRect& pager) {
  const bool full = mode == PaneMode::Single && legendFormFor(tile.size()) == LegendForm::Full;
  const int margin = full ? kOverlayMargin : kFacetLegendMargin;
  QSize size;
  if (full) {
    legend->setForm(LegendForm::Full);
    size = legend->sizeHint();
    size.setWidth(std::min(size.width(), std::max(1, tile.width() - 2 * margin)));
  } else {
    size = legend->fitPanel(tile.size(), margin);
  }
  const int x = tile.right() + 1 - margin - size.width();
  int bottom = tile.bottom() + 1 - margin;
  if (pager.isValid() && pager.intersects(QRect(x, tile.top(), size.width(), tile.height()))) {
    bottom = std::min(bottom, pager.top() - kOverlayGap);
  }
  if (full) {
    size.setHeight(std::min(
        size.height(), std::max(legend->minimumSizeHint().height(), bottom - tile.top() - margin)));
  }
  return {x, bottom - size.height(), size.width(), size.height()};
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

std::vector<vtkDataSet*> rawPointers(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes) {
  std::vector<vtkDataSet*> pointers;
  pointers.reserve(meshes.size());
  std::transform(meshes.begin(), meshes.end(), std::back_inserter(pointers), [](const auto& m) {
    return m.GetPointer();
  });
  return pointers;
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
// in ascending value order, then NaN if present (see categoryValue).
std::vector<std::pair<QString, QColor>> categoricalEntries(vtkLookupTable* lut,
                                                           const ScalarAnalysis& analysis) {
  std::vector<std::pair<QString, QColor>> entries;
  if (!lut) {
    return entries;
  }
  entries.reserve(analysis.uniqueValues.size() + (analysis.hasNaN ? 1 : 0));
  for (const double value : analysis.uniqueValues) {
    const vtkIdType row = lut->GetAnnotatedValueIndex(value);
    if (row < 0 || row >= lut->GetNumberOfTableValues()) {
      return entries;
    }
    double rgba[4];
    lut->GetTableValue(row, rgba);
    entries.push_back(
        {QString::fromStdString(lut->GetAnnotation(row)), qColor({rgba[0], rgba[1], rgba[2]})});
  }
  if (analysis.hasNaN) {
    const double* rgba = lut->GetNanColor();
    entries.push_back({QStringLiteral("NaN / No value"), qColor({rgba[0], rgba[1], rgba[2]})});
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
  if (analysis.hasNaN && entry >= 0 && static_cast<size_t>(entry) == analysis.uniqueValues.size()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
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
  // left or right press was released without dragging, before the release's onHover.
  // onZoom gets the factor a wheel turn moves the camera by.
  explicit VtkMouseFilter(QWidget* vtkRoot,
                          std::function<void(Qt::KeyboardModifiers)> onSpace,
                          std::function<bool(QMouseEvent*)> onPointerEvent,
                          std::function<void(const std::optional<QPointF>&)> onHover,
                          std::function<void(const QPointF&, Qt::MouseButton)> onClick,
                          std::function<void(double factor)> onZoom,
                          std::function<void(bool active, bool erase)> onPaintHold,
                          std::function<bool()> paintKeysLive,
                          QObject* parent = nullptr)
      : QObject(parent), vtkRoot_(vtkRoot), onSpace_(std::move(onSpace)),
        onPointerEvent_(std::move(onPointerEvent)), onHover_(std::move(onHover)),
        onClick_(std::move(onClick)), onZoom_(std::move(onZoom)),
        onPaintHold_(std::move(onPaintHold)), paintKeysLive_(std::move(paintKeysLive)) {
    if (vtkRoot_) {
      connect(vtkRoot_, &QObject::destroyed, this, [this] { vtkRoot_ = nullptr; });
    }
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    auto* widget = qobject_cast<QWidget*>(watched);
    QWidget* vtkRoot = vtkRoot_;
    if (!widget || !vtkRoot) {
      return QObject::eventFilter(watched, event);
    }

    const bool insideVtkWidget = (widget == vtkRoot || vtkRoot->isAncestorOf(widget));
    if (!insideVtkWidget) {
      // A focused slice view keeps the viewer's keys.
      return (qobject_cast<SliceView*>(widget) && handleKey(event)) ||
             QObject::eventFilter(watched, event);
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
      const bool clickable = me->button() == Qt::LeftButton || me->button() == Qt::RightButton;
      if (clickable && me->buttons() == me->button()) {
        pressedAt_ = mouseLocalPos(me);
        pressedButton_ = me->button();
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
      if (me->button() == pressedButton_ && pressedAt_ && onClick_) {
        onClick_(mouseLocalPos(me), pressedButton_);
      }
      pressedAt_.reset();
      if (me->buttons() == Qt::NoButton) {
        hover(mouseLocalPos(me));
      }
      break;
    }
    case QEvent::Wheel: {
      auto* we = static_cast<QWheelEvent*>(event);
      const int delta = we->pixelDelta().isNull() ? we->angleDelta().y() : we->pixelDelta().y();
      if (delta != 0 && onZoom_) {
        onZoom_(std::pow(1.20, delta / 120.0));
        hover(we->position());
      }
      return true;
    }
    case QEvent::HoverMove:
    case QEvent::NativeGesture:
    case QEvent::Gesture:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
      return true; // block trackpad rotate / pinch gestures
    default:
      if (handleKey(event)) {
        return true;
      }
      break;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  // Space, Q and the paint holds, which no QAction carries; true when consumed.
  bool handleKey(QEvent* event) {
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease &&
        event->type() != QEvent::ShortcutOverride) {
      return false;
    }
    auto* ke = static_cast<QKeyEvent*>(event);
    const bool eraseKey = ke->key() == Qt::Key_E || ke->key() == Qt::Key_K;
    // Only bare holds while annotating, so shortcuts such as Ctrl+K keep their keys. A
    // release passes whatever the modifiers, so a hold always ends.
    const bool bare =
        event->type() == QEvent::KeyRelease ||
        !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const bool paintKey = bare && paintKeysLive_ && paintKeysLive_() &&
                          (eraseKey || ke->key() == Qt::Key_A || ke->key() == Qt::Key_J);
    if (event->type() == QEvent::ShortcutOverride) {
      if (ke->key() == Qt::Key_Space || ke->key() == Qt::Key_Q || paintKey) {
        ke->accept();
        return true;
      }
      return false;
    }
    const bool pressed = event->type() == QEvent::KeyPress;
    if (pressed && ke->key() == Qt::Key_Space && onSpace_) {
      onSpace_(ke->modifiers());
      return true;
    }
    // Closing rather than quitting lets the window ask about unsaved labels.
    if (pressed && ke->key() == Qt::Key_Q) {
      vtkRoot_->window()->close();
      return true;
    }
    // Hold A or J to paint, E or K to erase (J and K suit the left hand on the
    // mouse); ignore X11 auto-repeat so the hold sticks.
    if (!ke->isAutoRepeat() && onPaintHold_ && paintKey) {
      onPaintHold_(pressed, pressed && eraseKey);
      return true;
    }
    return false;
  }

  void hover(const std::optional<QPointF>& pos) const {
    if (onHover_) {
      onHover_(pos);
    }
  }

  // Not a QPointer: clang-analyzer 18 reports a false use-after-free in
  // ~QWeakPointer when it analyzes this class's implicit destructor.
  QWidget* vtkRoot_;
  std::function<void(Qt::KeyboardModifiers)> onSpace_;
  std::function<bool(QMouseEvent*)> onPointerEvent_;
  std::function<void(const std::optional<QPointF>&)> onHover_;
  std::function<void(const QPointF&, Qt::MouseButton)> onClick_;
  std::function<void(double factor)> onZoom_;
  std::function<void(bool active, bool erase)> onPaintHold_;
  std::function<bool()> paintKeysLive_;
  std::optional<QPointF> pressedAt_; // where a press that may become a click began
  Qt::MouseButton pressedButton_ = Qt::NoButton;
};

// Paintable labels 1..kNumLabels, one swatch each; value 0 is "unlabeled". Kept at
// 9 so the categorical LUT uses the distinct tab10 palette.
constexpr int kNumLabels = AnnotationBar::kLabels;
static_assert(AnnotationBar::kMaxCompareMaps == static_cast<int>(kMaxReferences),
              "the bar offers a map per reference tile");
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

const QString kConfirmReplaceKey = QStringLiteral("annotation/confirmReplace");

// The bytes of mesh in the format suffix names: legacy vtk, vtp or vtu. std::nullopt
// when the writer reports an error, which it can do while Write() returns 1.
std::optional<std::string> serializeMesh(vtkDataSet* mesh, const QString& suffix) {
  const auto written = [](vtkAlgorithm* writer, int status) {
    return status != 0 && writer->GetErrorCode() == vtkErrorCode::NoError;
  };
  if (suffix == QStringLiteral("vtk")) {
    // Rendering moves the tables a file attached off their arrays (see
    // fileLookupTable); a legacy file keeps them only when they are back on.
    std::vector<vtkDataArray*> restored;
    for (vtkDataSetAttributes* data : {static_cast<vtkDataSetAttributes*>(mesh->GetPointData()),
                                       static_cast<vtkDataSetAttributes*>(mesh->GetCellData())}) {
      for (int i = 0; i < data->GetNumberOfArrays(); ++i) {
        vtkDataArray* array = data->GetArray(i);
        if (array && !array->GetLookupTable()) {
          if (vtkLookupTable* table = fileLookupTable(array)) {
            array->SetLookupTable(table);
            restored.push_back(array);
          }
        }
      }
    }
    auto w = vtkSmartPointer<vtkDataSetWriter>::New();
    w->WriteToOutputStringOn();
    w->SetInputData(mesh);
    w->SetFileTypeToBinary();
    const bool ok = written(w, w->Write());
    for (vtkDataArray* array : restored) {
      array->SetLookupTable(nullptr);
    }
    return ok ? std::optional<std::string>(w->GetOutputStdString()) : std::nullopt;
  }
  if (auto* pd = vtkPolyData::SafeDownCast(mesh); pd && suffix == QStringLiteral("vtp")) {
    auto w = vtkSmartPointer<vtkXMLPolyDataWriter>::New();
    w->WriteToOutputStringOn();
    w->SetInputData(pd);
    w->SetDataModeToBinary(); // VTK 9.1's reader can't read its own appended-mode output
    return written(w, w->Write()) ? std::optional<std::string>(w->GetOutputString()) : std::nullopt;
  }
  if (auto* ug = vtkUnstructuredGrid::SafeDownCast(mesh); ug && suffix == QStringLiteral("vtu")) {
    auto w = vtkSmartPointer<vtkXMLUnstructuredGridWriter>::New();
    w->WriteToOutputStringOn();
    w->SetInputData(ug);
    w->SetDataModeToBinary();
    return written(w, w->Write()) ? std::optional<std::string>(w->GetOutputString()) : std::nullopt;
  }
  return std::nullopt;
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
    renderer_.setSharedCatAnalysis(buildCommonCatAnalysis(rawPointers(meshes)));
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
          stepPane(activePane_, 1);
          break;
        case SpaceAction::PreviousField:
          stepPane(activePane_, -1);
          break;
        }
      },
      [this](QMouseEvent* e) {
        if (e->type() == QEvent::MouseButtonPress) {
          if (const auto pane = paneAt(mouseLocalPos(e))) {
            setActivePane(*pane);
          }
        }
        return handleAnnotatePointer(e);
      },
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
      [this](const QPointF& pos, Qt::MouseButton button) {
        if (button == Qt::RightButton) {
          if (const auto pane = paneAt(pos)) {
            showPaneMenu(*pane, vtkWidget_->mapToGlobal(pos.toPoint()));
          }
        } else if (probeAction_ && probeAction_->isChecked() && !paintArmed()) {
          pinProbe(pos);
        }
      },
      [this](double factor) { renderer_.dolly(factor); },
      [this](bool active, bool erase) { setPaintHold(active, erase); },
      [this]() { return annotating_; },
      this));

  setupPanes();
  if (volumeScene_) {
    renderer_.addImagePlanes(volumeScene_->planes(), volumeScene_->worldBounds());
  }
  buildRail();
  syncLayoutMetrics();
  panesChanged();
  if (playbackBar_) {
    restartTrace(); // its label, until a pin gives it a trace
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

ViewerWindow::~ViewerWindow() = default;

QWidget* ViewerWindow::buildSliceViews(QWidget* parent) {
  volumeScene_ = std::make_unique<VolumeScene>(load_.volumes, partColors_);
  auto* column = new QWidget(parent);
  auto* layout = new QVBoxLayout(column);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(kSliceViewGap);
  for (const SliceOrientation orientation :
       {SliceOrientation::Axial, SliceOrientation::Coronal, SliceOrientation::Sagittal}) {
    auto* view = new SliceView(*volumeScene_, orientation, column);
    connect(view, &SliceView::sceneChanged, this, &ViewerWindow::refreshSliceViews);
    layout->addWidget(view, 1);
    sliceViews_.push_back(view);
  }
  if (volumeScene_->hasLabels()) {
    auto* fill =
        new QAction(lucideIcon(Lucide::Brush, kRailIconSize), QStringLiteral("Fill labels"), this);
    fill->setCheckable(true);
    fill->setShortcut(QKeySequence(Qt::Key_M));
    addAction(fill);
    commands_.push_back(fill);
    connect(fill, &QAction::toggled, this, [this](bool filled) {
      volumeScene_->setOutline(!filled);
      refreshSliceViews();
    });
  }
  return column;
}

void ViewerWindow::refreshSliceViews() {
  for (SliceView* view : sliceViews_) {
    view->refresh();
  }
  vtkWidget_->renderWindow()->Render();
}

void ViewerWindow::setupWindowTitle() {
  if (load_.meshes.names.empty() && !load_.volumes.empty()) {
    setWindowTitle(QStringFromUtf8(load_.volumes.front().name) + QStringLiteral("[*] - vv"));
    return;
  }
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
  layout->addWidget(vtkWidget_, 3);
  if (!load_.volumes.empty()) {
    // The gaps between the views show the window behind them.
    QPalette frame = central->palette();
    frame.setColor(QPalette::Window, overlay::kPopupSurface);
    central->setPalette(frame);
    central->setAutoFillBackground(true);
    layout->setSpacing(kSliceViewGap);
    layout->addWidget(buildSliceViews(central), 2);
  }
  setCentralWidget(central);

  renderWindow->SetMultiSamples(0);
  renderWindow->SetDesiredUpdateRate(120.0);
  vtkWidget_->setRenderWindow(renderWindow);

  renderer_.setRenderContext(renderWindow, vtkWidget_->interactor());

  // Opened from the rail's Layers button.
  layersPanel_ = new LayersPanel(vtkWidget_);
  layersPanel_->setVisible(false);
  connect(
      layersPanel_, &LayersPanel::visibilityBatchChanged, this, &ViewerWindow::changeVisibility);
  QObject::connect(
      layersPanel_, &LayersPanel::expansionChanged, this, [this]() { layoutOverlays(); });

  // Opened from the rail's Color button; it edits colorField().
  colorPanel_ = new ColorPanel(vtkWidget_);
  colorPanel_->setVisible(false);
  QObject::connect(
      colorPanel_, &ColorPanel::colormapChosen, this, [this](const QString& id, bool reversed) {
        if (const auto field = colorField()) {
          chooseColormap(*field, id.toStdString(), reversed);
        }
      });
  QObject::connect(colorPanel_, &ColorPanel::outOfRangeChosen, this, [this](OutOfRange outside) {
    const auto field = colorField();
    if (field && renderer_.setOutOfRange(*field, outside)) {
      fieldChanged(*field);
    }
  });
  QObject::connect(colorPanel_, &ColorPanel::clipRangeChanged, this, [this](double lo, double hi) {
    if (const auto field = colorField()) {
      clipField(*field, lo, hi);
    }
  });
  connect(colorPanel_,
          &ColorPanel::clipValueFormatChanged,
          this,
          [this](ColorBarWidget::Bound bound, ColorBarWidget::ValueFormat format) {
            const auto field = colorField();
            for (size_t tile = 0; field && tile < legends_.size(); ++tile) {
              if (renderer_.tiles()[tile].field == field) {
                legends_[tile]->setClipValueFormat(bound, format);
              }
            }
          });
  QObject::connect(colorPanel_, &ColorPanel::paletteChosen, this, [this](const QString& id) {
    if (const auto field = colorField()) {
      chooseCategoryPalette(*field, id);
    }
  });
  QObject::connect(colorPanel_,
                   &ColorPanel::categoryColorChanged,
                   this,
                   [this](int entry, const std::optional<QColor>& color) {
                     if (const auto field = colorField()) {
                       changeCategoryColor(*field, entry, color);
                     }
                   });

  // Zero-delay single shot: one pick per event-loop turn, at the latest position.
  probeTimer_ = new QTimer(this);
  probeTimer_->setSingleShot(true);
  probeTimer_->setInterval(0);
  QObject::connect(probeTimer_, &QTimer::timeout, this, [this]() { showProbe(); });

  // What an action did, briefly, at the top of the view.
  notice_ = new Notice(vtkWidget_);

  // Opened from a pane's pager. A Single pane lists plain geometry before its fields.
  fieldList_ = new FieldList(vtkWidget_);
  fieldList_->setVisible(false);
  QObject::connect(fieldList_, &FieldList::pageChosen, this, [this](int index) {
    if (listChoose_) {
      listChoose_(index);
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

void ViewerWindow::setupPanes() {
  renderer_.setup(load_.meshes.meshes, load_.meshes.groups, partColors_, options_.mode);
  fillLayersPanel();
  vectorFields_ = collectFields(rawPointers(load_.meshes.meshes), 3);

  const std::vector<SceneField>& fields = renderer_.fields();
  for (size_t field = 0; options_.fixedRange && field < fields.size(); ++field) {
    if (!fields[field].analysis.categorical) {
      renderer_.setFieldRange(field, (*options_.fixedRange)[0], (*options_.fixedRange)[1]);
    }
  }
  if (temporal_ && temporal_->playable()) {
    setupPlayback();
  }

  const std::vector<Pane>& panes = renderer_.panes();
  // A lone time series switches fields from its playback bar.
  const bool barPager = playbackBar_ && panes.size() == 1 && !panes.front().fields.empty();
  if (playbackBar_ && !barPager) {
    playbackBar_->hidePager();
  }
  for (size_t pane = 0; pane < panes.size(); ++pane) {
    PaneUi ui;
    if (panes.size() > 1) {
      ui.label = new PanelLabel(pane, QStringFromUtf8(load_.meshes.groups[pane].name), vtkWidget_);
    }
    if (!panes[pane].fields.empty()) {
      ui.pager = barPager ? playbackBar_->pager()
                          : new ScalarPager(PagerPlacement::Capsule,
                                            spaceAction(Qt::NoModifier, playbackBar_ != nullptr),
                                            vtkWidget_);
      connect(ui.pager, &ScalarPager::stepRequested, this, [this, pane](int step) {
        stepPane(pane, step);
      });
      connect(ui.pager, &ScalarPager::listRequested, this, [this, pane]() { openFieldList(pane); });
      connect(ui.pager, &ScalarPager::splitRequested, this, [this, pane](bool split) {
        setPaneSplit(pane, split);
      });
    }
    paneUi_.push_back(ui);
  }
  for (size_t tile = 0; tile < renderer_.tiles().size(); ++tile) {
    legends_.push_back(makeLegend(tile));
  }
  // Files with the same fields page through them together until told otherwise.
  renderer_.setFieldsLinked(panes.size() > 1 &&
                            std::all_of(panes.begin(), panes.end(), [&panes](const Pane& pane) {
                              return !pane.fields.empty() && pane.fields == panes.front().fields;
                            }));
}

// The legend follows whichever field its tile shows when the user acts on it.
ColorBarWidget* ViewerWindow::makeLegend(size_t tile) {
  auto* legend = new ColorBarWidget(vtkWidget_);
  legend->setVisible(false);
  legend->setFocusPolicy(Qt::NoFocus);
  const auto shown = [this, tile]() { return renderer_.tiles()[tile].field; };
  connect(legend, &ColorBarWidget::sizeHintChanged, this, &ViewerWindow::layoutPanes);
  connect(legend,
          &ColorBarWidget::colormapChosen,
          this,
          [this, shown](const QString& id, bool reversed) {
            if (const auto field = shown()) {
              chooseColormap(*field, id.toStdString(), reversed);
            }
          });
  connect(legend, &ColorBarWidget::clipRangeChanged, this, [this, shown](double lo, double hi) {
    if (const auto field = shown()) {
      clipField(*field, lo, hi);
    }
  });
  connect(legend,
          &ColorBarWidget::clipValueFormatChanged,
          this,
          [this, shown](ColorBarWidget::Bound bound, ColorBarWidget::ValueFormat format) {
            const auto field = shown();
            for (size_t other = 0; field && other < legends_.size(); ++other) {
              if (renderer_.tiles()[other].field == field) {
                legends_[other]->setClipValueFormat(bound, format);
              }
            }
          });
  connect(legend,
          &ColorBarWidget::categoryColorChanged,
          this,
          [this, shown](int entry, const std::optional<QColor>& color) {
            if (const auto field = shown()) {
              changeCategoryColor(*field, entry, color);
            }
          });
  connect(legend, &ColorBarWidget::paletteChosen, this, [this, shown](const QString& id) {
    if (const auto field = shown()) {
      chooseCategoryPalette(*field, id);
    }
  });
  return legend;
}

void ViewerWindow::changeVisibility(const std::vector<LayerVisibilityChange>& changes) {
  std::vector<PartVisibilityChange> parts;
  parts.reserve(changes.size());
  for (const auto& change : changes) {
    const auto& group = load_.meshes.groups[static_cast<size_t>(change.file)];
    parts.push_back({group.partIndices[static_cast<size_t>(change.part)], change.visible});
  }
  renderer_.setPartsVisible(parts);
  if (volumeScene_) {
    for (const PartVisibilityChange& part : parts) {
      volumeScene_->setPartVisible(part.part, part.visible);
    }
    for (SliceView* view : sliceViews_) {
      view->refresh();
    }
  }
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
  vtkDataSet* mesh = load_.meshes.meshes[load_.temporalMesh];
  for (const ScalarField& field : collectFields({mesh}, 1)) {
    if (const auto index = renderer_.findField(field)) {
      temporalFields_.insert(*index);
    }
  }
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
  traceFrame_ = vtkSmartPointer<vtkDataSet>::Take(mesh->NewInstance());
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

// The array strokes write: the shown field when it is categorical with at most
// kNumLabels categories, so an existing annotation is fixed in place with its values
// and colors; otherwise the cell array "label", created when missing.
void ViewerWindow::bindLabelArray() {
  vtkDataSet* mesh = renderer_.getPrimaryMesh();
  if (!mesh) {
    return;
  }
  ScalarField target{"label", FieldAssociation::Cell};
  const SceneField* shown = nullptr;
  const Pane& pane = renderer_.panes().front();
  if (pane.first >= 0) {
    const SceneField& field = renderer_.fields()[pane.fields[static_cast<size_t>(pane.first)]];
    if (field.analysis.categorical &&
        field.analysis.uniqueValues.size() <= static_cast<size_t>(kNumLabels) &&
        arrayForAssociation(mesh, field.field.name, field.field.association)) {
      target = field.field;
      shown = &field;
    }
  }
  if (target.name != labelField_.name || target.association != labelField_.association) {
    undoStack_.clear();
    annotationBar_->setUndoAvailable(false);
  }
  labelField_ = target;
  labelArray_ = arrayForAssociation(mesh, target.name, target.association);
  if (!labelArray_) {
    auto arr = vtkSmartPointer<vtkIntArray>::New();
    arr->SetName("label");
    arr->SetNumberOfComponents(1);
    arr->SetNumberOfTuples(mesh->GetNumberOfCells());
    arr->FillComponent(0, 0.0);
    mesh->GetCellData()->AddArray(arr);
    labelArray_ = arr;
  }

  std::vector<AnnotationBar::Label> labels;
  const auto addLabel = [&labels, this](double value) {
    double rgb[3];
    labelLut_->GetColor(value, rgb);
    labels.push_back({QString::number(value, 'g', 15), qColor({rgb[0], rgb[1], rgb[2]})});
  };
  if (shown) {
    // The swatches are the categories, with the whole-number gaps filled when they fit
    // and the value past the largest when a swatch is left: a region missing from this
    // file (one the segmentation lost) can be painted back. Erase leaves no value: NaN
    // where the array can hold it, else 0.
    const std::set<double>& present = shown->analysis.uniqueValues;
    std::set<double> domain = present;
    constexpr double kExactWhole = 9007199254740992.0; // 2^53
    const bool whole = std::all_of(present.begin(), present.end(), [](double v) {
      return v == std::floor(v) && std::abs(v) < kExactWhole;
    });
    // Only values the array stores as they are: 256 in an unsigned char array would
    // land as 0. Past 2^53 doubles no longer step by one, so nothing is filled in.
    const auto storable = [array = labelArray_](double v) {
      if (array->GetDataType() == VTK_FLOAT) {
        return static_cast<double>(static_cast<float>(v)) == v;
      }
      return v >= array->GetDataTypeMin() && v <= array->GetDataTypeMax();
    };
    // The gaps come before the next value: {1, 3..9} needs all nine swatches for 1..9.
    if (whole && !present.empty() && *present.rbegin() - *present.begin() + 1.0 <= kNumLabels) {
      for (double v = *present.begin(); v <= *present.rbegin(); ++v) {
        if (storable(v)) {
          domain.insert(v);
        }
      }
      const double next = *present.rbegin() + 1.0;
      if (domain.size() < static_cast<size_t>(kNumLabels) && storable(next)) {
        domain.insert(next);
      }
    }
    // Colored as the field is: whole values pick their colors by value.
    labelLut_ =
        createCategoricalLookupTable(domain, shown->analysis.coloring, shown->analysis.fileColors);
    labelRange_[0] = *domain.begin();
    labelRange_[1] = *domain.rbegin();
    const bool floating =
        labelArray_->GetDataType() == VTK_FLOAT || labelArray_->GetDataType() == VTK_DOUBLE;
    labelValues_ = {floating ? std::numeric_limits<double>::quiet_NaN() : 0.0};
    for (const double v : domain) {
      labelValues_.push_back(v);
      addLabel(v);
    }
  } else {
    // Fixed value→color LUT so a value keeps its color while the array is edited;
    // Tab10, the colors labels had before palettes could be picked.
    std::set<double> domain;
    labelValues_.clear();
    for (int v = 0; v <= kNumLabels; ++v) {
      domain.insert(static_cast<double>(v));
      labelValues_.push_back(static_cast<double>(v));
    }
    CategoryColoring labelColors;
    labelColors.palette = findCategoryPalette("tab10");
    labelLut_ = createCategoricalLookupTable(domain, labelColors, {});
    labelLut_->SetTableValue(0, 0.6, 0.6, 0.6, 1.0); // value 0 = unlabeled → grey
    labelLut_->Modified();
    labelRange_[0] = 0.0;
    labelRange_[1] = kNumLabels;
    for (int v = 1; v <= kNumLabels; ++v) {
      addLabel(static_cast<double>(v));
    }
  }
  annotationBar_->setArrayName(QString::fromStdString(target.name));
  annotationBar_->setLabels(labels);
  if (currentLabel_ >= static_cast<int>(labelValues_.size())) {
    currentLabel_ = 1;
  }
  annotationBar_->setLabel(currentLabel_);
}

void ViewerWindow::setupAnnotation() {
  // GetPointCells (used by the surface brush BFS) needs the reverse links built.
  if (auto* pd = vtkPolyData::SafeDownCast(renderer_.getPrimaryMesh())) {
    pd->BuildLinks();
  } else if (auto* ug = vtkUnstructuredGrid::SafeDownCast(renderer_.getPrimaryMesh())) {
    ug->BuildLinks();
  }
  picker_ = vtkSmartPointer<vtkCellPicker>::New();
  picker_->SetTolerance(0.0005);
  // Only the labelled mesh's actor: the brush preview, glyphs and other parts would
  // answer with cell ids of other datasets.
  picker_->PickFromListOn();
  if (!renderer_.context.actors.empty()) {
    picker_->AddPickList(renderer_.context.actors.front());
  }

  annotationBar_ = new AnnotationBar(vtkWidget_);
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
  QObject::connect(annotationBar_, &AnnotationBar::saveRequested, this, [this]() {
    saveAnnotations(SaveMode::InPlace);
  });
  QObject::connect(annotationBar_, &AnnotationBar::saveAsRequested, this, [this]() {
    saveAnnotations(SaveMode::NewFile);
  });
  QObject::connect(annotationBar_, &AnnotationBar::leaveRequested, this, [this]() {
    annotateAction_->setChecked(false);
  });
  QObject::connect(annotationBar_, &AnnotationBar::compareMapsChosen, this, [this](int count) {
    setCompareMaps(static_cast<size_t>(std::max(count, 0)));
    panesChanged();
  });
  for (size_t slot = 0; slot < kMaxReferences; ++slot) {
    auto* chip = new FieldChip(vtkWidget_);
    chip->hide();
    connect(chip, &FieldChip::clicked, this, [this, slot]() { openCompareList(slot); });
    compareChips_.push_back(chip);
  }
  for (size_t line = 0; line < kMaxReferences; ++line) {
    auto* divider = new QWidget(vtkWidget_);
    divider->setAttribute(Qt::WA_TransparentForMouseEvents);
    divider->setAttribute(Qt::WA_StyledBackground);
    divider->setStyleSheet(
        QStringLiteral("background: %1;").arg(overlay::css(overlay::kSelectedBorder)));
    divider->hide();
    compareDividers_.push_back(divider);
  }

  const QString undo = QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText);
  keyHints_ =
      new KeyHints({{{QStringLiteral("A"), QStringLiteral("J")}, QStringLiteral("hold to paint")},
                    {{QStringLiteral("E"), QStringLiteral("K")}, QStringLiteral("hold to erase")},
                    {{QStringLiteral("1–9")}, QStringLiteral("pick label")},
                    {{QStringLiteral("+"), QStringLiteral("−")}, QStringLiteral("brush")},
                    {{QStringLiteral("T")}, QStringLiteral("compare")},
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
    bindLabelArray();
    // The labels take the pane's one tile; the bar's swatches are their legend.
    renderer_.setPaneMode(0, PaneMode::Single);
    annotating_ = true;
    renderer_.setLabelColoring(labelField_, labelLut_, labelRange_);
    // The painted field may be another one now; the maps keep the others.
    setCompareMaps(compareCount_);
  } else {
    endStroke();
    heldTool_.reset();
    annotating_ = false;
    // Back to the field shown before; a label field of the file has new values.
    renderer_.setLabelColoring(labelField_, nullptr, labelRange_);
    if (const auto labels = renderer_.findField(labelField_)) {
      renderer_.reanalyzeField(*labels);
    }
    fieldList_->dismiss(); // a map's
    applyCompare();
  }
  annotationBar_->setVisible(on);
  keyHints_->setVisible(on);
  if (paneUi_.front().pager && paneUi_.front().pager->parentWidget() != vtkWidget_) {
    paneUi_.front().pager->setVisible(!on);
  }
  syncAnnotateKeys();
  panesChanged();
  applyTool();
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

// Press-and-hold from VtkMouseFilter: A or J arms Paint and E or K arms Erase while held; the
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
    // Beside the painted view a drag turns the camera.
    if (!paintArmed() || !inPaintTile(mouseLocalPos(event))) {
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
      const bool brush = inPaintTile(mouseLocalPos(event));
      if (brush != (vtkWidget_->cursor().shape() == Qt::BitmapCursor)) {
        vtkWidget_->setCursor(brush ? makeBrushCursor() : sceneCursor());
      }
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
  // The ray through a point beside the painted view would still meet the mesh.
  if (!ren || !picker_ || !inPaintTile(pos)) {
    return -1;
  }
  const std::array<double, 2> display = displayPoint(vtkWidget_, pos);
  return picker_->Pick(display[0], display[1], 0.0, ren) != 0 ? picker_->GetCellId() : -1;
}

void ViewerWindow::paintAtWidgetPos(const QPointF& pos, int label) {
  vtkDataSet* mesh = renderer_.getPrimaryMesh();
  const vtkIdType cell = annotationCellAt(pos);
  if (!mesh || !labelArray_ || cell < 0 || label < 0 ||
      label >= static_cast<int>(labelValues_.size())) {
    return;
  }
  const double value = labelValues_[static_cast<size_t>(label)];
  std::vector<vtkIdType> ids = growCells(mesh, cell, brushRings_);
  // A point field takes the brushed cells' points.
  if (labelField_.association == FieldAssociation::Point) {
    std::set<vtkIdType> points;
    auto cellPts = vtkSmartPointer<vtkIdList>::New();
    for (const vtkIdType c : ids) {
      mesh->GetCellPoints(c, cellPts);
      points.insert(cellPts->begin(), cellPts->end());
    }
    ids.assign(points.begin(), points.end());
  }
  bool changed = false;
  for (const vtkIdType id : ids) {
    if (id < 0 || id >= labelArray_->GetNumberOfTuples()) {
      continue;
    }
    const double prev = labelArray_->GetTuple1(id);
    if (prev == value || (std::isnan(prev) && std::isnan(value))) {
      continue;
    }
    currentStroke_.emplace_back(static_cast<long long>(id), prev);
    labelArray_->SetTuple1(id, value);
    changed = true;
  }
  if (!changed) {
    return;
  }
  labelsChanged();
}

void ViewerWindow::labelsChanged() {
  // Fixed LUT covers every value, so edits recolor live — no LUT rebuild needed.
  labelArray_->Modified();
  updateRegionBorders();
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
  vtkDataSet* mesh = renderer_.getPrimaryMesh();
  const vtkIdType cell = annotationCellAt(pos); // none without a renderer
  if (!mesh || !labelLut_ || cell < 0) {
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
    // The offset mode is global and shifts the mesh as much as the preview, so only the
    // preview's own extra offset toward the camera keeps it from z-fighting the mesh.
    vtkMapper::SetResolveCoincidentTopologyToPolygonOffset();
    mapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(-1.0, -6.0);
    previewActor_->SetMapper(mapper);
    previewActor_->GetProperty()->SetOpacity(0.5);
    previewActor_->GetProperty()->SetLighting(false);
    // In the maps too, where the brush is aimed by their values.
    const Pane& pane = renderer_.panes().front();
    for (size_t tile = pane.firstTile; tile < pane.firstTile + pane.tileCount; ++tile) {
      renderer_.tiles()[tile].renderer->AddActor(previewActor_);
    }
  }
  vtkDataSetMapper::SafeDownCast(previewActor_->GetMapper())->SetInputData(extract->GetOutput());

  // What a left click would write.
  double rgb[3];
  labelLut_->GetColor(labelValues_[static_cast<size_t>(
                          strokeLabel(activeTool(), Qt::LeftButton, currentLabel_).value_or(0))],
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
  if (label < 1 || label >= static_cast<int>(labelValues_.size())) {
    return;
  }
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
  for (const auto& [id, prev] : undoStack_.back()) {
    if (id >= 0 && id < labelArray_->GetNumberOfTuples()) {
      labelArray_->SetTuple1(static_cast<vtkIdType>(id), prev);
    }
  }
  undoStack_.pop_back();
  annotationBar_->setUndoAvailable(!undoStack_.empty());
  setWindowModified(true);
  labelsChanged();
}

QString ViewerWindow::inPlacePath() const {
  if (load_.meshes.groups.size() != 1 || load_.meshes.groups.front().partIndices.size() != 1 ||
      load_.meshes.names.empty()) {
    return {};
  }
  const QFileInfo file(QStringFromUtf8(load_.meshes.names.front()));
  const QString suffix = file.suffix().toLower();
  vtkDataSet* mesh = renderer_.getPrimaryMesh();
  const bool writable =
      suffix == QStringLiteral("vtk") ||
      (suffix == QStringLiteral("vtp") && vtkPolyData::SafeDownCast(mesh)) ||
      (suffix == QStringLiteral("vtu") && vtkUnstructuredGrid::SafeDownCast(mesh));
  return writable && file.isFile() ? file.absoluteFilePath() : QString();
}

bool ViewerWindow::saveAnnotations(SaveMode mode) {
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

  QString path = mode == SaveMode::InPlace ? inPlacePath() : QString();
  if (!path.isEmpty()) {
    // defaultFormat() rather than the native store, so tests can point it elsewhere.
    QSettings settings(QSettings::defaultFormat(),
                       QSettings::UserScope,
                       QStringLiteral("vv"),
                       QStringLiteral("vv"));
    if (settings.value(kConfirmReplaceKey, true).toBool()) {
      QMessageBox ask(
          QMessageBox::Question,
          QStringLiteral("Replace file"),
          QStringLiteral("Replace %1 with the labeled mesh?").arg(QDir::toNativeSeparators(path)),
          QMessageBox::Save | QMessageBox::Cancel,
          this);
      ask.button(QMessageBox::Save)->setText(QStringLiteral("Replace"));
      ask.setDefaultButton(QMessageBox::Save);
      auto* skip = new QCheckBox(QStringLiteral("Don't ask again"), &ask);
      ask.setCheckBox(skip);
      if (ask.exec() != QMessageBox::Save) {
        return false;
      }
      if (skip->isChecked()) {
        settings.setValue(kConfirmReplaceKey, false);
      }
    }
  } else {
    const bool poly = pd != nullptr;
    QString suggested;
    if (!load_.meshes.names.empty()) {
      QFileInfo fi(QStringFromUtf8(load_.meshes.names.front()));
      suggested =
          fi.dir().filePath(fi.completeBaseName() + (poly ? QStringLiteral(".labeled.vtp")
                                                          : QStringLiteral(".labeled.vtu")));
    }
    const QString filter = poly ? QStringLiteral("VTK PolyData (*.vtp)")
                                : QStringLiteral("VTK UnstructuredGrid (*.vtu)");
    path = QFileDialog::getSaveFileName(this, "Save annotations", suggested, filter);
    if (path.isEmpty()) {
      return false;
    }
  }

  QString format = QFileInfo(path).suffix().toLower();
  if (format != QStringLiteral("vtk") && format != QStringLiteral("vtp") &&
      format != QStringLiteral("vtu")) {
    format = pd ? QStringLiteral("vtp") : QStringLiteral("vtu");
  }
  // QSaveFile writes beside the target and replaces it only once everything is
  // written, so a failed save leaves the original whole and touches no other file.
  const std::optional<std::string> bytes = serializeMesh(mesh, format);
  QSaveFile file(path);
  if (!bytes || !file.open(QIODevice::WriteOnly) ||
      file.write(bytes->data(), static_cast<qint64>(bytes->size())) !=
          static_cast<qint64>(bytes->size()) ||
      !file.commit()) {
    QMessageBox::warning(
        this, QStringLiteral("Save failed"), QStringLiteral("Could not write ") + path);
    return false;
  }
  setWindowModified(false);
  notify(QStringLiteral("Saved ") + QDir::toNativeSeparators(path), NoticeKind::Info);
  return true;
}

void ViewerWindow::syncAnnotateKeys() {
  for (QAction* key : annotateKeys_) {
    key->setEnabled(annotating_);
  }
}

std::vector<size_t> ViewerWindow::compareCandidates() const {
  std::vector<size_t> fields = renderer_.panes().front().fields;
  if (const auto painted = renderer_.findField(labelField_)) {
    fields.erase(std::remove(fields.begin(), fields.end(), *painted), fields.end());
  }
  return fields;
}

void ViewerWindow::setCompareMaps(size_t count) {
  const std::vector<size_t> candidates = compareCandidates();
  const auto offered = [&candidates](size_t field) {
    return std::find(candidates.begin(), candidates.end(), field) != candidates.end();
  };
  std::vector<size_t>& fields = compareFields_;
  fields.erase(std::remove_if(fields.begin(),
                              fields.end(),
                              [&offered](size_t field) { return !offered(field); }),
               fields.end());
  compareCount_ = std::min({count, kMaxReferences, candidates.size()});
  // A new map shows the field the view showed before annotating, then the file's next ones.
  const Pane& pane = renderer_.panes().front();
  std::vector<size_t> order;
  if (pane.first >= 0) {
    order.push_back(pane.fields[static_cast<size_t>(pane.first)]);
  }
  order.insert(order.end(), candidates.begin(), candidates.end());
  for (const size_t field : order) {
    if (fields.size() < compareCount_ && offered(field) &&
        std::find(fields.begin(), fields.end(), field) == fields.end()) {
      fields.push_back(field);
    }
  }
  applyCompare();
}

void ViewerWindow::applyCompare() {
  std::vector<size_t> shown;
  if (annotating_) {
    shown.assign(compareFields_.begin(),
                 compareFields_.begin() + static_cast<std::ptrdiff_t>(compareCount_));
  }
  // Finding the surface's faces walks the whole mesh, so only once maps show.
  if (!shown.empty() && !regionBorders_) {
    regionBorders_ = std::make_unique<RegionBorders>(renderer_.getPrimaryMesh());
  }
  updateRegionBorders();
  renderer_.setReferenceOutline(shown.empty() ? nullptr : regionBorders_->lines());
  if (renderer_.panes().front().references != shown) {
    renderer_.setReferences(0, std::move(shown));
    // The painted view's half frames the meshes again, unless the camera moved.
    renderer_.refitCameraIfUntouched();
    if (renderer_.context.window) {
      renderer_.context.window->Render();
    }
  }
  if (annotationBar_) {
    annotationBar_->setCompareMaps(
        static_cast<int>(compareCount_),
        static_cast<int>(std::min(kMaxReferences, compareCandidates().size())));
  }
}

void ViewerWindow::updateRegionBorders() {
  if (regionBorders_ && annotating_ && compareCount_ > 0 && labelArray_) {
    regionBorders_->update(labelArray_, labelField_.association);
  }
}

void ViewerWindow::openCompareList(size_t slot) {
  if (!annotating_ || slot >= compareCount_) {
    return;
  }
  const std::vector<size_t> fields = compareCandidates();
  std::vector<SearchItem> items;
  for (const size_t field : fields) {
    const SceneField& scene = renderer_.fields()[field];
    SearchItem item;
    item.text = QStringFromUtf8(scene.field.name);
    item.detail = legendSubtitle(scene.field.association, scene.analysis);
    item.current = compareFields_[slot] == field;
    items.push_back(std::move(item));
  }
  openList(compareChips_[slot], items, [this, slot, fields](int row) {
    if (row < 0 || static_cast<size_t>(row) >= fields.size() || slot >= compareFields_.size()) {
      return;
    }
    // A field another map shows trades places with this one's.
    const size_t field = fields[static_cast<size_t>(row)];
    const auto other = std::find(compareFields_.begin(), compareFields_.end(), field);
    if (other != compareFields_.end()) {
      *other = compareFields_[slot];
    }
    compareFields_[slot] = field;
    applyCompare();
    panesChanged();
  });
}

// Each map's chip at the top right of its tile, below the annotation bar where they meet,
// and a line left of the maps and between them.
void ViewerWindow::layoutCompareOverlays() {
  const Pane& pane = renderer_.panes().front();
  for (size_t slot = 0; slot < compareChips_.size(); ++slot) {
    FieldChip* chip = compareChips_[slot];
    QWidget* divider = compareDividers_[slot];
    const size_t index = pane.firstTile + 1 + slot;
    const bool shown = annotating_ && index < pane.firstTile + pane.tileCount &&
                       renderer_.tiles()[index].reference;
    divider->setVisible(shown);
    if (!shown) {
      chip->hide();
      continue;
    }
    const Tile& tile = renderer_.tiles()[index];
    const QRect view = viewportRect(vtkWidget_, tile.viewport);
    const QRect all = viewportRect(vtkWidget_, pane.viewport);
    divider->setGeometry(slot == 0 ? QRect(view.left(), all.top(), 1, all.height())
                                   : QRect(view.left(), view.top(), view.width(), 1));
    divider->lower();

    const SceneField& field = renderer_.fields()[*tile.field];
    chip->setField(QStringFromUtf8(field.field.name),
                   legendSubtitle(field.field.association, field.analysis));
    const QRect area = view.adjusted(
        kFacetLegendMargin, kFacetLegendMargin, -kFacetLegendMargin, -kFacetLegendMargin);
    const QSize size = chip->fitWidth(std::max(0, area.width()));
    QRect place(QPoint(area.right() + 1 - size.width(), area.top()), size);
    if (!annotationBar_->isHidden() && place.intersects(annotationBar_->geometry())) {
      place.moveTop(annotationBar_->geometry().bottom() + 1 + kOverlayGap);
    }
    chip->setGeometry(place);
    chip->setVisible(area.contains(place));
  }
}

bool ViewerWindow::inPaintTile(const QPointF& pos) const {
  vtkRenderer* ren = renderer_.getRenderer();
  const std::array<double, 2> display = displayPoint(vtkWidget_, pos);
  return ren && ren->IsInViewport(static_cast<int>(display[0]), static_cast<int>(display[1]));
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
    if (answer == QMessageBox::Cancel ||
        (answer == QMessageBox::Save && !saveAnnotations(SaveMode::InPlace))) {
      event->ignore();
      return;
    }
  }
  QMainWindow::closeEvent(event);
}

std::optional<size_t> ViewerWindow::paneAt(const QPointF& pos) const {
  const std::array<double, 2> display = displayPoint(vtkWidget_, pos);
  return renderer_.paneAt(display[0], display[1]);
}

void ViewerWindow::setActivePane(size_t pane) {
  if (pane == activePane_ || pane >= paneUi_.size()) {
    return;
  }
  activePane_ = pane;
  for (size_t index = 0; index < paneUi_.size(); ++index) {
    syncPager(index);
  }
  syncSplitAction();
  syncColorPanel();
}

void ViewerWindow::stepPane(size_t pane, int step) {
  // The labels stay on view while annotating; leaving shows the field again.
  if (annotating_) {
    return;
  }
  setActivePane(pane);
  renderer_.stepPane(pane, step);
  panesChanged();
}

void ViewerWindow::showField(size_t pane, int position) {
  if (annotating_) {
    return;
  }
  setActivePane(pane);
  renderer_.showField(pane, position);
  panesChanged();
}

void ViewerWindow::setPaneSplit(size_t pane, bool split) {
  // Annotation paints in the pane's one tile.
  if (!annotating_) {
    setActivePane(pane);
    renderer_.setPaneMode(pane, split ? PaneMode::Split : PaneMode::Single);
  }
  panesChanged();
}

void ViewerWindow::showPaneMenu(size_t pane, const QPoint& globalPos) {
  setActivePane(pane);
  auto* menu = new QMenu(this);
  menu->setAttribute(Qt::WA_DeleteOnClose);
  overlay::styleMenu(menu);
  menu->addAction(splitAction_);
  if (paneUi_[pane].pager) {
    QAction* choose = menu->addAction(QStringLiteral("Choose field…"));
    choose->setEnabled(!annotating_);
    connect(choose, &QAction::triggered, this, [this, pane]() { openFieldList(pane); });
  }
  if (linkAction_) {
    menu->addSeparator();
    menu->addAction(linkAction_);
  }
  menu->popup(globalPos);
}

void ViewerWindow::panesChanged() {
  streamShownFields();
  for (size_t tile = 0; tile < legends_.size(); ++tile) {
    syncLegend(tile);
  }
  for (size_t pane = 0; pane < paneUi_.size(); ++pane) {
    syncPager(pane);
  }
  syncSplitAction();
  syncColorPanel();
  layoutOverlays();
  refreshProbe();
  if (tracedField() != traced_) {
    restartTrace();
  }
}

void ViewerWindow::fieldChanged(size_t field) {
  for (size_t tile = 0; tile < legends_.size(); ++tile) {
    if (renderer_.tiles()[tile].field == field) {
      syncLegend(tile);
    }
  }
  // A cyclic map makes the field "cyclic" on its legends, the pagers and the field list.
  for (size_t pane = 0; pane < paneUi_.size(); ++pane) {
    syncPager(pane);
  }
  if (colorField() == field) {
    syncColorPanel();
  }
  refreshProbe(); // the card's swatch
}

void ViewerWindow::syncPager(size_t index) {
  ScalarPager* pager = paneUi_[index].pager;
  if (!pager) {
    return;
  }
  const Pane& pane = renderer_.panes()[index];
  const bool split = pane.mode == PaneMode::Split;
  pager->setSplit(split);
  pager->setActive(paneUi_.size() > 1 && index == activePane_);
  pager->setStepNames(split ? QStringLiteral("Previous page") : QStringLiteral("Previous field"),
                      split ? QStringLiteral("Next page") : QStringLiteral("Next field"));
  if (!split) {
    const SearchItem item = fieldItem(index, pane.first);
    pager->setPage(item.text, item.detail, pane.first + 1, static_cast<int>(pane.fields.size()));
    return;
  }
  const auto first = static_cast<size_t>(pane.first);
  QStringList names;
  for (size_t position = first; position < first + pane.shownCount(); ++position) {
    names << QStringFromUtf8(renderer_.fields()[pane.fields[position]].field.name);
  }
  pager->setPage(facetPageLabel(first, pane.shownCount(), pane.fields.size()),
                 names.join(QStringLiteral(", ")),
                 static_cast<int>(pane.page()) + 1,
                 static_cast<int>(pane.pageCount()));
}

void ViewerWindow::syncLegend(size_t index) {
  const Tile& tile = renderer_.tiles()[index];
  ColorBarWidget* legend = legends_[index];
  // While annotating, the bar's swatches are the painted view's legend.
  const bool show = tile.shown && tile.field && (!annotating_ || tile.reference);
  legend->setVisible(show);
  if (!show) {
    return;
  }
  const SceneField& field = renderer_.fields()[*tile.field];
  // A tile among others keeps its line short.
  const bool single = renderer_.panes()[tile.pane].mode == PaneMode::Single && !tile.reference;
  legend->setTitle(QStringFromUtf8(field.field.name));
  legend->setSubtitle(legendSubtitle(
      single ? std::optional<FieldAssociation>(field.field.association) : std::nullopt,
      field.analysis));
  legend->setRange(field.globalRange[0], field.globalRange[1]);
  legend->setColormap(field.analysis.colormap);
  if (field.analysis.categorical) {
    legend->setCategorical(categoricalEntries(field.lut, field.analysis));
    legend->setPalettes(paletteOptions(field.analysis), paletteId(field.analysis.coloring));
  } else {
    legend->clearCategorical();
    legend->setClipRange(field.clipRange[0], field.clipRange[1]);
  }
}

void ViewerWindow::syncSplitAction() {
  const Pane& pane = renderer_.panes()[activePane_];
  QString unavailable;
  if (annotating_) {
    unavailable = QStringLiteral("Split view: not available while annotating");
  } else if (pane.fields.empty()) {
    unavailable = QStringLiteral("Split view: no field to show");
  }
  splitAction_->setChecked(pane.mode == PaneMode::Split);
  splitAction_->setEnabled(unavailable.isEmpty());
  splitAction_->setToolTip(unavailable.isEmpty() ? railToolTip(splitAction_) : unavailable);
}

void ViewerWindow::syncLayoutMetrics() {
  LayoutMetrics metrics;
  metrics.pixelRatio = vtkWidget_->devicePixelRatioF();
  if (playbackBar_) {
    const QRect bar = playbackBarGeometry(vtkWidget_->size(), *playbackBar_, kOverlayMargin);
    metrics.bottomInset = vtkWidget_->height() - bar.top() + kOverlayGap;
  }
  const auto capsule = std::find_if(paneUi_.begin(), paneUi_.end(), [this](const PaneUi& ui) {
    return ui.pager && ui.pager->parentWidget() == vtkWidget_;
  });
  if (capsule != paneUi_.end()) {
    metrics.pagerStrip = capsule->pager->sizeHint().height() + kOverlayMargin + kOverlayGap;
  }
  renderer_.setLayoutMetrics(metrics);
}

void ViewerWindow::streamShownFields() {
  if (!playbackBar_) {
    return;
  }
  std::vector<std::string> names;
  std::set<size_t> fields;
  for (const Tile& tile : renderer_.tiles()) {
    const std::vector<size_t>& meshes = renderer_.panes()[tile.pane].meshes;
    if (!tile.field || temporalFields_.count(*tile.field) == 0 ||
        std::find(meshes.begin(), meshes.end(), load_.temporalMesh) == meshes.end()) {
      continue;
    }
    fields.insert(*tile.field);
    const std::string& name = renderer_.fields()[*tile.field].field.name;
    if (std::find(names.begin(), names.end(), name) == names.end()) {
      names.push_back(name);
    }
  }
  const bool selectionChanged = names != streamed_;
  if (selectionChanged) {
    streamed_ = std::move(names);
    temporal_->setActiveArrays(streamed_);
  }
  bool sampledFrames = false;
  for (size_t index : fields) {
    const SceneField& field = renderer_.fields()[index];
    if ((options_.fixedRange && !field.analysis.categorical) ||
        !sampledFields_.insert(index).second) {
      continue;
    }
    sampledFrames = true;
    double sampled[2];
    if (!temporal_->sampledScalarRange(field.field, sampled)) {
      continue;
    }
    // Keep static values in the shared scale and preserve bounds already edited in another pane.
    const double lo = std::min(field.globalRange[0], sampled[0]);
    const double hi = std::max(field.globalRange[1], sampled[1]);
    if (lo != field.globalRange[0] || hi != field.globalRange[1]) {
      const double clipLo = field.clipRange[0] == field.globalRange[0] ? lo : field.clipRange[0];
      const double clipHi = field.clipRange[1] == field.globalRange[1] ? hi : field.clipRange[1];
      renderer_.setFieldRange(index, lo, hi);
      if (clipLo != lo || clipHi != hi) {
        renderer_.setClipRange(index, clipLo, clipHi);
      }
    }
  }
  if (selectionChanged || sampledFrames) {
    temporal_->readStepInto(currentPlaybackStep_, load_.meshes.meshes[load_.temporalMesh]);
    renderer_.refreshAfterDataChange(load_.temporalMesh);
  }
}

SearchItem ViewerWindow::fieldItem(size_t index, int position) const {
  const Pane& pane = renderer_.panes()[index];
  SearchItem item;
  if (position < 0) {
    item.text = QStringLiteral("Geometry");
    item.detail = QStringLiteral("Part colors");
    item.current = pane.first < 0;
    return item;
  }
  const SceneField& field = renderer_.fields()[pane.fields[static_cast<size_t>(position)]];
  item.text = QStringFromUtf8(field.field.name);
  item.detail = legendSubtitle(field.field.association, field.analysis);
  item.current = pane.first >= 0 && position >= pane.first &&
                 position < pane.first + static_cast<int>(pane.shownCount());
  return item;
}

void ViewerWindow::openFieldList(size_t index) {
  ScalarPager* pager = paneUi_[index].pager;
  if (!pager || annotating_) {
    return;
  }
  setActivePane(index);
  const Pane& pane = renderer_.panes()[index];
  std::vector<SearchItem> items;
  for (int position = pane.mode == PaneMode::Single ? -1 : 0;
       position < static_cast<int>(pane.fields.size());
       ++position) {
    items.push_back(fieldItem(index, position));
  }
  openList(pager, items, [this, index](int row) {
    const bool single = renderer_.panes()[index].mode == PaneMode::Single;
    showField(index, single ? row - 1 : row);
  });
}

void ViewerWindow::openList(QWidget* anchor,
                            const std::vector<SearchItem>& items,
                            std::function<void(int)> choose) {
  listAnchor_ = anchor;
  listChoose_ = std::move(choose);
  hideProbe();
  fieldList_->setAnchor(QRect(anchor->mapTo(vtkWidget_, QPoint(0, 0)), anchor->size()));
  fieldList_->open(items);
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
  const Pane& pane = renderer_.panes()[activePane_];
  for (int position = pane.mode == PaneMode::Single ? -1 : 0;
       !pane.fields.empty() && position < static_cast<int>(pane.fields.size());
       ++position) {
    SearchItem row = fieldItem(activePane_, position);
    row.group = QStringLiteral("Fields");
    row.enabled = !annotating_;
    if (annotating_) {
      row.toolTip = QStringLiteral("Fields: not available while annotating");
    }
    items.push_back(
        {row, [this, active = activePane_, position]() { showField(active, position); }});
  }
  const std::optional<size_t> field = colorField();
  if (field && renderer_.fields()[*field].analysis.categorical) {
    const ScalarAnalysis& analysis = renderer_.fields()[*field].analysis;
    const QString current = paletteId(analysis.coloring);
    for (const PaletteOption& palette : paletteOptions(analysis)) {
      SearchItem row;
      row.text = palette.label;
      row.detail = palette.detail;
      row.current = palette.id == current;
      row.group = QStringLiteral("Palettes");
      row.icon = paletteIcon(palette.colors, QSize(16, 16));
      items.push_back(
          {row, [this, shown = *field, id = palette.id]() { chooseCategoryPalette(shown, id); }});
    }
  } else if (field) {
    const ColormapChoice& choice = renderer_.fields()[*field].analysis.colormap;
    for (const Colormap& map : colormaps()) {
      SearchItem row;
      row.text = fromView(map.label);
      row.detail = colormapGroupName(map.group);
      row.current = &map == choice.map;
      row.group = QStringLiteral("Colormaps");
      row.icon = colormapIcon(map, choice.reversed, QSize(16, 16));
      items.push_back(
          {row, [this, shown = *field, id = std::string(map.id), reversed = choice.reversed]() {
             chooseColormap(shown, id, reversed);
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

std::optional<size_t> ViewerWindow::colorField() const {
  const Pane& pane = renderer_.panes()[activePane_];
  if (annotating_ || pane.mode != PaneMode::Single || pane.first < 0) {
    return std::nullopt;
  }
  return pane.fields[static_cast<size_t>(pane.first)];
}

void ViewerWindow::chooseColormap(size_t field, const std::string& id, bool reversed) {
  if (renderer_.setColormap(field, id, reversed)) {
    fieldChanged(field);
  }
}

void ViewerWindow::clipField(size_t field, double lo, double hi) {
  renderer_.setClipRange(field, lo, hi);
  showClipRange(field);
}

void ViewerWindow::chooseCategoryPalette(size_t field, const QString& id) {
  if (renderer_.setCategoryPalette(field, id.toStdString())) {
    fieldChanged(field);
  }
}

void ViewerWindow::changeCategoryColor(size_t field,
                                       int entry,
                                       const std::optional<QColor>& color) {
  const auto value = categoryValue(renderer_.fields()[field].analysis, entry);
  if (value && renderer_.setCategoryColor(field, *value, lutRgb(color))) {
    fieldChanged(field);
  }
}

void ViewerWindow::showClipRange(size_t field) {
  const double* clip = renderer_.fields()[field].clipRange;
  for (size_t tile = 0; tile < legends_.size(); ++tile) {
    if (renderer_.tiles()[tile].field == field) {
      legends_[tile]->setClipRange(clip[0], clip[1]);
    }
  }
  if (colorField() == field) {
    colorPanel_->setClipRange(clip[0], clip[1]);
  }
}

void ViewerWindow::syncColorPanel() {
  const std::optional<size_t> field = colorField();
  if (!field) {
    const Pane& pane = renderer_.panes()[activePane_];
    QString unavailable = QStringLiteral("Color: no field shown");
    if (annotating_) {
      unavailable = QStringLiteral("Color: not available while annotating");
    } else if (pane.mode == PaneMode::Split) {
      unavailable = QStringLiteral("Color: click a legend gradient or category swatch");
    }
    colorAction_->setEnabled(false);
    colorAction_->setToolTip(unavailable);
    colorAction_->setChecked(false);
    return;
  }
  const SceneField& scene = renderer_.fields()[*field];
  colorAction_->setEnabled(true);
  colorAction_->setToolTip(railToolTip(colorAction_));
  colorPanel_->setField(QStringFromUtf8(scene.field.name));
  if (scene.analysis.categorical) {
    colorPanel_->setCategories(paletteOptions(scene.analysis),
                               paletteId(scene.analysis.coloring),
                               categoricalEntries(scene.lut, scene.analysis));
  } else {
    colorPanel_->setColormap(scene.analysis.colormap);
  }
  if (colorPanel_->isVisible()) {
    updateColorRange();
    layoutOverlays(); // the panel's sections follow the field's kind
  }
}

void ViewerWindow::updateColorRange() {
  const std::optional<size_t> field = colorField();
  if (!field || renderer_.fields()[*field].analysis.categorical) {
    return;
  }
  const double* range = renderer_.fields()[*field].globalRange;
  colorPanel_->setRange(range[0], range[1], renderer_.fieldHistogram(*field, kHistogramBins));
  showClipRange(*field);
}

void ViewerWindow::setSurfaceLook(const SurfaceLook& look) {
  if (!seeThrough(look.style)) {
    styleBeforeSeeThrough_ = look.style;
  }
  renderer_.setSurfaceLook(look);
  stylePanel_->setLook(renderer_.getSurfaceLook());
  layoutOverlays(); // the panel shows the Opacity row for X-ray, Density for Glass
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

  layersAction_ = addTool(Lucide::Layers, QStringLiteral("Layers"), QKeySequence());
  layersAction_->setCheckable(true);
  if (load_.meshes.groups.empty()) {
    layersAction_->setEnabled(false);
    layersAction_->setToolTip(QStringLiteral("Layers: no parts to show"));
  }
  QObject::connect(layersAction_, &QAction::toggled, this, [this](bool open) {
    layersPanel_->setVisible(open);
    layoutOverlays();
  });

  colorAction_ = addTool(Lucide::Palette, QStringLiteral("Color"), QKeySequence());
  colorAction_->setCheckable(true);
  QObject::connect(colorAction_, &QAction::toggled, this, [this](bool open) {
    // The histogram is only computed while someone can see it.
    if (open) {
      updateColorRange();
    }
    colorPanel_->setVisible(open);
    layoutOverlays();
  });
  // One panel beside the rail at a time.
  panels_ = new QActionGroup(this);
  panels_->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
  panels_->addAction(layersAction_);
  panels_->addAction(colorAction_);

  styleAction_ = addTool(Lucide::Box, QStringLiteral("Style"), QKeySequence());
  styleAction_->setCheckable(true);
  panels_->addAction(styleAction_);
  QObject::connect(styleAction_, &QAction::toggled, this, [this](bool open) {
    stylePanel_->setVisible(open);
    layoutOverlays();
  });
  // S and W replace VTK's own surface and wireframe keys, which vv's camera style drops.
  // X and G toggle their style from anywhere, back to the last opaque one, which for
  // S and W is the style they set.
  struct StyleKey {
    Lucide icon;
    const char* name;
    Qt::Key key;
    SurfaceStyle style;
  };
  for (const StyleKey styleKey :
       {StyleKey{Lucide::Box, "Solid", Qt::Key_S, SurfaceStyle::Solid},
        StyleKey{Lucide::Globe, "Wireframe", Qt::Key_W, SurfaceStyle::Wireframe},
        StyleKey{Lucide::ScanEye, "X-ray", Qt::Key_X, SurfaceStyle::XRay},
        StyleKey{Lucide::Gem, "Glass", Qt::Key_G, SurfaceStyle::Glass}}) {
    auto* action = new QAction(
        lucideIcon(styleKey.icon, kRailIconSize), QString::fromUtf8(styleKey.name), this);
    action->setShortcut(QKeySequence(styleKey.key));
    addAction(action);
    commands_.push_back(action);
    QObject::connect(action, &QAction::triggered, this, [this, style = styleKey.style]() {
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
  struct SaveKey {
    int keys;
    SaveMode mode;
  };
  // Spelled out: QKeySequence::SaveAs is empty on Windows.
  for (const SaveKey saveKey :
       {SaveKey{static_cast<int>(Qt::CTRL) | Qt::Key_S, SaveMode::InPlace},
        SaveKey{static_cast<int>(Qt::CTRL) | static_cast<int>(Qt::SHIFT) | Qt::Key_S,
                SaveMode::NewFile}}) {
    auto* action = new QAction(this);
    action->setShortcut(QKeySequence(saveKey.keys));
    addAction(action);
    QObject::connect(action, &QAction::triggered, this, [this, mode = saveKey.mode]() {
      saveAnnotations(mode);
    });
    annotateKeys_.push_back(action);
  }
  // T splits a pane, which annotation does not; there it steps through the compare maps.
  auto* compare = new QAction(this);
  compare->setShortcut(QKeySequence(Qt::Key_T));
  addAction(compare);
  QObject::connect(compare, &QAction::triggered, this, [this]() {
    setCompareMaps((compareCount_ + 1) %
                   (std::min(kMaxReferences, compareCandidates().size()) + 1));
    panesChanged();
  });
  annotateKeys_.push_back(compare);
  auto* undo = new QAction(this);
  undo->setShortcut(QKeySequence(QKeySequence::Undo));
  addAction(undo);
  QObject::connect(undo, &QAction::triggered, this, [this]() { undoStroke(); });
  annotateKeys_.push_back(undo);
  syncAnnotateKeys();

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
        for (SliceView* view : sliceViews_) {
          view->resetCamera();
        }
        refreshProbe();
      });
  QObject::connect(addTool(Lucide::Camera, QStringLiteral("Save snapshot"), QKeySequence()),
                   &QAction::triggered,
                   this,
                   [this]() { saveSnapshot(); });

  // The active pane's mode, also on its pager and in its context menu.
  splitAction_ = new QAction(
      lucideIcon(Lucide::LayoutGrid, kRailIconSize), QStringLiteral("Split view"), this);
  splitAction_->setCheckable(true);
  splitAction_->setShortcut(QKeySequence(Qt::Key_T));
  addAction(splitAction_);
  commands_.push_back(splitAction_);
  QObject::connect(splitAction_, &QAction::triggered, this, [this](bool split) {
    setPaneSplit(activePane_, split);
  });
  if (paneUi_.size() > 1) {
    linkAction_ = new QAction(QStringLiteral("Link fields across files"), this);
    linkAction_->setCheckable(true);
    linkAction_->setChecked(renderer_.fieldsLinked());
    commands_.push_back(linkAction_);
    QObject::connect(linkAction_, &QAction::toggled, this, [this](bool linked) {
      renderer_.setFieldsLinked(linked);
    });
  }

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

  std::vector<QWidget*> labels;
  for (const PaneUi& ui : paneUi_) {
    if (ui.label) {
      labels.push_back(ui.label);
    }
  }
  const QImage image = snapshotWithLegends(
      vtkWidget_->grabFramebuffer(), vtkWidget_->devicePixelRatioF(), legends_, labels);
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
    if (!display) {
      card->hide();
      continue;
    }
    const QPoint point = widgetPoint(vtkWidget_, *display).toPoint();
    const QRect bounds = viewportRect(vtkWidget_, renderer_.tiles()[match.tile].viewport);
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

std::optional<size_t> ViewerWindow::tracedField() const {
  if (!playbackBar_ || !pin_ || pin_->mesh != load_.temporalMesh) {
    return std::nullopt;
  }
  const std::optional<ProbeHit> shown = renderer_.probeAgain(*pin_);
  const auto field = shown ? renderer_.tiles()[shown->tile].field : std::nullopt;
  return field && temporalFields_.count(*field) != 0 ? field : std::nullopt;
}

void ViewerWindow::restartTrace() {
  if (!playbackBar_) {
    return;
  }
  traced_ = tracedField();
  trace_.reset(traced_ ? temporal_->steps() : 0);
  if (traced_) {
    traceTimer_->start();
  } else {
    traceTimer_->stop();
  }
  showTrace();
}

void ViewerWindow::readTrace() {
  if (!pin_ || !traced_) {
    traceTimer_->stop();
    return;
  }
  const SceneField& field = renderer_.fields()[*traced_];
  readTraceFrames(
      *temporal_, traceFrame_, pin_->at, field.field, field.analysis, trace_, kTraceBudget);
  if (trace_.complete()) {
    traceTimer_->stop();
  }
  showTrace();
}

void ViewerWindow::showTrace() {
  if (!traced_ || !pin_ || trace_.frames() == 0) {
    playbackBar_->setTraceLabel(QStringLiteral("Pin a point with the probe to trace it"));
    playbackBar_->setTrace({}, 0.0, 1.0);
    return;
  }
  const SceneField& field = renderer_.fields()[*traced_];
  playbackBar_->setTraceLabel(
      QStringLiteral("%1 at cell %2").arg(QStringFromUtf8(field.field.name)).arg(pin_->cell));
  playbackBar_->setTrace(trace_.values(), field.globalRange[0], field.globalRange[1]);
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
  // A resize can arrive while the window is still being built.
  if (!splitAction_) {
    return;
  }
  syncLayoutMetrics();
  renderer_.refitCameraIfUntouched();
  // The widget's resizeEvent has already drawn the tiles as they were laid out before.
  vtkWidget_->renderWindow()->Render();
  // A split pane's page holds what fits, so its tiles may show other fields now.
  panesChanged();
}

void ViewerWindow::notify(const QString& text, NoticeKind kind) {
  notice_->post(text, kind);
  notice_->setGeometry(noticeGeometry(vtkWidget_, notice_, annotationBar_));
  stackOverlays();
}

void ViewerWindow::layoutOverlays() {
  // A panel's signal can ask before the panes exist.
  if (paneUi_.empty()) {
    return;
  }
  if (playbackBar_) {
    playbackBar_->setGeometry(
        playbackBarGeometry(vtkWidget_->size(), *playbackBar_, kOverlayMargin));
  }
  if (rail_) {
    rail_->setGeometry(railGeometry(vtkWidget_, rail_, playbackBar_));
  }
  layoutPanes();
  fieldList_->setGeometry(vtkWidget_->rect());
  palette_->setGeometry(vtkWidget_->rect());
  if (rail_) {
    if (annotating_) {
      annotationBar_->setGeometry(
          annotationBarGeometry(vtkWidget_, annotationBar_, rail_->geometry()));
      // In the pager's place, shown only where it clears the rail's column.
      const QRect hints = bottomCenterGeometry(vtkWidget_, keyHints_, {playbackBar_});
      keyHints_->setGeometry(hints);
      keyHints_->setVisible(hints.left() > rail_->geometry().right() + kRailPanelGap &&
                            hints.right() < vtkWidget_->width() - kOverlayMargin);
    }
    std::vector<const QWidget*> barsBelow{playbackBar_, keyHints_};
    for (const PaneUi& ui : paneUi_) {
      if (ui.pager && ui.pager->parentWidget() == vtkWidget_) {
        barsBelow.push_back(ui.pager);
      }
    }
    const auto besideRail = [this, &barsBelow](int width) {
      return railPanelArea(vtkWidget_, rail_->geometry(), width, {annotationBar_}, barsBelow);
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
  // After the annotation bar, which they go under.
  layoutCompareOverlays();
  if (listAnchor_) {
    fieldList_->setAnchor(QRect(listAnchor_->mapTo(vtkWidget_, QPoint(0, 0)), listAnchor_->size()));
  }
  notice_->setGeometry(noticeGeometry(vtkWidget_, notice_, annotationBar_));
  stackOverlays();
}

void ViewerWindow::layoutPanes() {
  const std::vector<Pane>& panes = renderer_.panes();
  for (size_t index = 0; index < panes.size(); ++index) {
    const Pane& pane = panes[index];
    const PaneUi& ui = paneUi_[index];
    const QRect area = viewportRect(vtkWidget_, pane.viewport);
    if (ui.label) {
      ui.label->setGeometry(area);
      ui.label->lower();
    }
    // Bottom center of its pane, unless the playback bar holds it.
    QRect pager;
    if (ui.pager && ui.pager->parentWidget() == vtkWidget_) {
      const int height = ui.pager->sizeHint().height();
      QRect strip = area.adjusted(kFacetLegendMargin, 0, -kFacetLegendMargin, 0);
      strip.setTop(area.bottom() + 1 - kOverlayMargin - height);
      strip.setHeight(height);
      if (rail_ && strip.intersects(rail_->geometry())) {
        strip.setLeft(rail_->geometry().right() + 1 + kOverlayGap);
      }
      const QSize size = ui.pager->fitWidth(std::max(0, strip.width()));
      const bool fits = size.width() <= strip.width() && area.contains(strip);
      if (fits) {
        pager = QRect(QPoint(strip.left() + (strip.width() - size.width()) / 2, strip.top()), size);
        ui.pager->setGeometry(pager);
      }
      ui.pager->setVisible(fits && !annotating_);
    }
    for (size_t tile = pane.firstTile; tile < pane.firstTile + pane.tileCount; ++tile) {
      if (!legends_[tile]->isHidden()) {
        const Tile& shown = renderer_.tiles()[tile];
        legends_[tile]->setGeometry(
            legendGeometry(viewportRect(vtkWidget_, shown.viewport),
                           legends_[tile],
                           shown.reference ? PaneMode::Split : pane.mode,
                           ui.pager && !ui.pager->isHidden() ? pager : QRect()));
      }
    }
  }
}

void ViewerWindow::stackOverlays() {
  for (ColorBarWidget* legend : legends_) {
    legend->raise();
  }
  for (FieldChip* chip : compareChips_) {
    chip->raise();
  }
  for (ProbeCard* card : probeCards_) {
    card->raise();
  }
  for (const PaneUi& ui : paneUi_) {
    if (ui.pager && ui.pager->parentWidget() == vtkWidget_) {
      ui.pager->raise();
    }
  }
  overlay::stackOverlays({playbackBar_,
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
