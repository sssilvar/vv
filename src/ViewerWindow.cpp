#include "ViewerWindow.h"

#include "ColorBarWidget.h"
#include "PlaybackBar.h"
#include "ScalarVizUtils.h"
#include "TemporalSource.h"
#include "mesh_utils.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVTKOpenGLNativeWidget.h>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
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

constexpr int kOverlayMargin = 12;
constexpr int kOverlayMinWidth = 96;
constexpr int kOverlayMaxWidth = 180;
constexpr double kOverlayHeightRatio = 0.45;
constexpr int kOverlayMinHeight = 150;
constexpr int kOverlayMaxHeight = 260;
constexpr int kTreeOverlayMargin = 16;
constexpr int kTreeOverlayWidth = 360;
constexpr double kTreeOverlayHeightRatio = 0.40;
constexpr int kTreeOverlayMinHeight = 140;
constexpr int kTreeOverlayMaxHeight = 340;
constexpr int kFacetBarMargin = 6;
constexpr int kFacetBarMinWidth = 68;
constexpr int kFacetBarMaxWidth = 140;
constexpr double kPlaybackBaseFps = 15.0;
constexpr int kPlaybackBarMargin = 16;
constexpr int kPlaybackBarMaxWidth = 760;
constexpr int kPlaybackBarHeight = 44;

QRect colorBarOverlayGeometry(const QWidget* viewport, const ColorBarWidget* colorBar) {
  const int height = std::clamp(static_cast<int>(viewport->height() * kOverlayHeightRatio),
                                kOverlayMinHeight,
                                kOverlayMaxHeight);
  const int width = std::clamp(colorBar->sizeHint().width(), kOverlayMinWidth, kOverlayMaxWidth);
  const int x = std::max(kOverlayMargin, viewport->width() - width - kOverlayMargin);
  const int y = std::max(kOverlayMargin, (viewport->height() - height) / 2);
  return QRect(x, y, width, height);
}

QRect treeOverlayGeometry(const QWidget* viewport) {
  const int height = std::clamp(static_cast<int>(viewport->height() * kTreeOverlayHeightRatio),
                                kTreeOverlayMinHeight,
                                kTreeOverlayMaxHeight);
  return QRect(kTreeOverlayMargin, kTreeOverlayMargin, kTreeOverlayWidth, height);
}

QRect playbackBarGeometry(const QWidget* viewport) {
  const int width =
      std::min(kPlaybackBarMaxWidth, std::max(280, viewport->width() - 2 * kPlaybackBarMargin));
  const int x = std::max(kPlaybackBarMargin, (viewport->width() - width) / 2);
  const int y =
      std::max(kPlaybackBarMargin, viewport->height() - kPlaybackBarHeight - kPlaybackBarMargin);
  return QRect(x, y, width, kPlaybackBarHeight);
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

QIcon partColorIcon(const std::array<double, 3>& rgb) {
  constexpr int kSize = 12;
  QPixmap pix(kSize, kSize);
  pix.fill(Qt::transparent);

  const int r = std::clamp(static_cast<int>(std::lround(rgb[0] * 255.0)), 0, 255);
  const int g = std::clamp(static_cast<int>(std::lround(rgb[1] * 255.0)), 0, 255);
  const int b = std::clamp(static_cast<int>(std::lround(rgb[2] * 255.0)), 0, 255);

  QPainter painter(&pix);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(QColor(22, 22, 22, 220), 1.0));
  painter.setBrush(QColor(r, g, b));
  QPolygon poly;
  poly << QPoint(2, kSize - 2) << QPoint(kSize / 2, 2) << QPoint(kSize - 2, kSize - 2);
  painter.drawPolygon(poly);
  return QIcon(pix);
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

// Swatch list for a categorical scalar: analysis unique values + LUT colors,
// highest value first (top of the bar).
std::vector<std::pair<QString, QColor>> categoricalEntries(vtkLookupTable* lut,
                                                           const ScalarAnalysis& analysis) {
  std::vector<std::pair<QString, QColor>> entries;
  if (!lut) {
    return entries;
  }
  const auto& uv = analysis.uniqueValues;
  int idx = static_cast<int>(uv.size()) - 1; // reverse: highest first
  for (auto it = uv.rbegin(); it != uv.rend(); ++it, --idx) {
    double rgba[4];
    lut->GetTableValue(idx < 0 ? 0 : idx, rgba);
    char label[32];
    const double v = *it;
    if (v == std::floor(v))
      std::snprintf(label, sizeof(label), "%g", v);
    else
      std::snprintf(label, sizeof(label), "%.3g", v);
    entries.push_back({QString::fromLatin1(label),
                       QColor::fromRgbF(static_cast<float>(rgba[0]),
                                        static_cast<float>(rgba[1]),
                                        static_cast<float>(rgba[2]))});
  }
  return entries;
}

// ─────────────────────────────────────────────────────────────────────
// Event filter that keeps VTK interactions predictable:
// - swallow hover-only motion to avoid implicit rotate state,
// - route wheel zoom through a single camera-dolly path,
// - handle scalar cycling/quit hotkeys.
// ─────────────────────────────────────────────────────────────────────
class VtkMouseFilter : public QObject {
public:
  explicit VtkMouseFilter(QWidget* vtkRoot,
                          QWidget* overlayColorBar,
                          QWidget* overlayTree,
                          std::function<void()> onSpaceCycle,
                          std::function<void()> onViewportResize,
                          std::function<bool(QMouseEvent*)> onPointerEvent,
                          std::function<void(bool active, bool erase)> onPaintHold,
                          std::function<bool(int key)> onHotkey,
                          QObject* parent = nullptr)
      : QObject(parent), vtkRoot_(vtkRoot), overlayColorBar_(overlayColorBar),
        overlayTree_(overlayTree), onSpaceCycle_(std::move(onSpaceCycle)),
        onViewportResize_(std::move(onViewportResize)), onPointerEvent_(std::move(onPointerEvent)),
        onPaintHold_(std::move(onPaintHold)), onHotkey_(std::move(onHotkey)) {}

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

    QWidget* overlayColorBar = overlayColorBar_.data();
    QWidget* overlayTree = overlayTree_.data();

    bool insideOverlay = false;
    for (QWidget* current = widget; current; current = current->parentWidget()) {
      if ((overlayColorBar && current == overlayColorBar) ||
          qobject_cast<ColorBarWidget*>(current)) {
        insideOverlay = true;
        break;
      }
    }
    if (insideOverlay) {
      return QObject::eventFilter(watched, event);
    }

    // Annotation gets first dibs on pointer events so a paint stroke is not also
    // interpreted as a camera rotate/dolly by VTK. Only the raw render surface
    // paints — events on child overlays (the annotation bar's swatches, slider,
    // save button) must pass through to those widgets.
    if (onPointerEvent_ && widget == vtkRoot &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove ||
         event->type() == QEvent::MouseButtonRelease)) {
      if (onPointerEvent_(static_cast<QMouseEvent*>(event))) {
        return true;
      }
    }

    switch (event->type()) {
    case QEvent::Resize:
      if (widget == vtkRoot) {
        if (auto* bar = qobject_cast<ColorBarWidget*>(overlayColorBar)) {
          bar->setGeometry(colorBarOverlayGeometry(vtkRoot, bar));
        }
        if (overlayTree) {
          overlayTree->setGeometry(treeOverlayGeometry(vtkRoot));
        }
        if (onViewportResize_) {
          onViewportResize_();
        }
      }
      break;
    case QEvent::MouseMove: {
      auto* me = static_cast<QMouseEvent*>(event);
      if (me->buttons() == Qt::NoButton)
        return true; // swallow hover‐only moves
      break;
    }
    case QEvent::Wheel:
      if (widget != vtkRoot) {
        return true;
      }
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
      if (ke->key() == Qt::Key_Space && onSpaceCycle_) {
        onSpaceCycle_();
        return true;
      }
      if (ke->key() == Qt::Key_Q) {
        QApplication::quit();
        return true;
      }
      if (onHotkey_ && onHotkey_(ke->key())) {
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
          ke->key() == Qt::Key_E || ke->key() == Qt::Key_C || ke->key() == Qt::Key_V) {
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
  QPointer<QWidget> vtkRoot_;
  QPointer<QWidget> overlayColorBar_;
  QPointer<QWidget> overlayTree_;
  std::function<void()> onSpaceCycle_;
  std::function<void()> onViewportResize_;
  std::function<bool(QMouseEvent*)> onPointerEvent_;
  std::function<void(bool active, bool erase)> onPaintHold_;
  std::function<bool(int key)> onHotkey_;
};

// Number of paintable labels (1..kNumLabels); value 0 is "unlabeled". Kept at 9
// so the categorical LUT uses the distinct tab10 palette.
constexpr int kNumLabels = 9;

QRect annotationBarGeometry(const QWidget* viewport, const QWidget* bar) {
  const int width = bar->sizeHint().width();
  const int height = bar->sizeHint().height();
  const int x = std::max(kPlaybackBarMargin, (viewport->width() - width) / 2);
  const int y = std::max(kPlaybackBarMargin, viewport->height() - height - kPlaybackBarMargin);
  return QRect(x, y, width, height);
}

enum class AnnotGlyph { Pencil, Save };

QIcon makeAnnotIcon(AnnotGlyph glyph) {
  QIcon icon;
  for (const int scale : {1, 2}) {
    const int px = 24 * scale;
    QPixmap pix(px, px);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(scale, scale);
    QPainterPath path;
    if (glyph == AnnotGlyph::Pencil) {
      path.moveTo(16.0, 4.0); // diagonal pencil body
      path.lineTo(20.0, 8.0);
      path.lineTo(8.0, 20.0);
      path.lineTo(4.0, 16.0);
      path.closeSubpath();
      QPainterPath tip; // graphite tip at the lower-left corner
      tip.moveTo(4.0, 16.0);
      tip.lineTo(8.0, 20.0);
      tip.lineTo(4.0, 20.0);
      tip.closeSubpath();
      path = path.united(tip);
    } else {
      path.addRect(QRectF(10.5, 4.0, 3.0, 7.0)); // down-arrow into a tray = save
      QPainterPath head;
      head.moveTo(7.0, 10.0);
      head.lineTo(17.0, 10.0);
      head.lineTo(12.0, 16.0);
      head.closeSubpath();
      path = path.united(head);
      path.addRect(QRectF(5.0, 17.5, 14.0, 2.5));
    }
    painter.fillPath(path, QColor(232, 232, 232));
    painter.end();
    icon.addPixmap(pix);
  }
  return icon;
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

// ═════════════════════════════════════════════════════════════════════
ViewerWindow::ViewerWindow(MeshLoadResult loadResult, const ViewerOptions& options, QWidget* parent)
    : QMainWindow(parent), load_(std::move(loadResult)), options_(options),
      temporal_(load_.temporal) {
  // Title: "vv - .../parent/stem.ext"
  if (!load_.meshes.names.empty()) {
    QFileInfo fi(QStringFromUtf8(load_.meshes.names.front()));
    setWindowTitle(QStringLiteral("vv - …/") + fi.dir().dirName() + "/" + fi.fileName());
  }
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

  buildViewport();

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
      colorBar_,
      partsTree_,
      [this]() { cycleScalar(); },
      [this]() { onViewportResize(); },
      options_.annotate ? std::function<bool(QMouseEvent*)>(
                              [this](QMouseEvent* e) { return handleAnnotatePointer(e); })
                        : nullptr,
      options_.annotate ? std::function<void(bool, bool)>(
                              [this](bool active, bool erase) { setPaintHold(active, erase); })
                        : nullptr,
      [this](int key) {
        if (key == Qt::Key_C) {
          toggleCyclicColormap();
          return true;
        }
        if (key == Qt::Key_V) {
          cycleVectorField();
          return true;
        }
        return false;
      },
      this));

  QTimer::singleShot(0, this, [this]() {
    colorBar_->setGeometry(colorBarOverlayGeometry(vtkWidget_, colorBar_));
    partsTree_->setGeometry(treeOverlayGeometry(vtkWidget_));
  });

  if (options_.explodeView) {
    setupFacetMode();
  } else {
    setupNormalMode();
    if (options_.annotate) {
      setupAnnotateMode();
    }
  }

  vtkWidget_->setFocus();
}

void ViewerWindow::buildViewport() {
  auto* central = new QWidget(this);
  auto* layout = new QHBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // The VTK 3‑D view
  vtkWidget_ = new QVTKOpenGLNativeWidget(central);
  vtkWidget_->setFocusPolicy(Qt::StrongFocus);
  vtkWidget_->setAttribute(Qt::WA_AcceptTouchEvents, false);
  layout->addWidget(vtkWidget_, 1);
  setCentralWidget(central);

  auto renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
  renderWindow->SetMultiSamples(0);
  renderWindow->SetDesiredUpdateRate(120.0);
  vtkWidget_->setRenderWindow(renderWindow);

  renderer_.setRenderContext(renderWindow, vtkWidget_->interactor());

  colorBar_ = new ColorBarWidget(vtkWidget_);
  colorBar_->setVisible(false);
  colorBar_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
  colorBar_->setFocusPolicy(Qt::NoFocus);
  colorBar_->setGeometry(colorBarOverlayGeometry(vtkWidget_, colorBar_));
  colorBar_->raise();

  partsTree_ = new QTreeWidget(vtkWidget_);
  partsTree_->setColumnCount(1);
  partsTree_->setHeaderHidden(true);
  partsTree_->setRootIsDecorated(true);
  partsTree_->setUniformRowHeights(true);
  partsTree_->setIndentation(18);
  partsTree_->setGeometry(treeOverlayGeometry(vtkWidget_));
  partsTree_->setVisible(false);
  partsTree_->setFocusPolicy(Qt::NoFocus);
  partsTree_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  partsTree_->setStyleSheet("QTreeWidget {"
                            "  background: rgba(0,0,0,0);"
                            "  color: #E2E2E2;"
                            "  outline: none;"
                            "  padding: 2px;"
                            "}");
  partsTree_->raise();
}

// ── facet (exploded) mode ──────────────────────────────────────────────
void ViewerWindow::setupFacetMode() {
  renderer_.setupFacetGrid(load_.meshes.meshes, load_.meshes.names, partColors_);
  renderer_.startFacetGrid();
  colorBar_->setVisible(false);
  partsTree_->setVisible(false);

  const size_t panelCount = renderer_.getFacetPanelCount();
  facetColorBars_.reserve(panelCount);
  for (size_t panelIndex = 0; panelIndex < panelCount; ++panelIndex) {
    FacetPanelInfo panelInfo;
    if (!renderer_.getFacetPanelInfo(panelIndex, panelInfo)) {
      continue;
    }

    if (options_.hasFixedRange && !panelInfo.analysis.categorical &&
        renderer_.setFacetPanelGlobalRange(
            panelIndex, options_.fixedRange[0], options_.fixedRange[1])) {
      renderer_.getFacetPanelInfo(panelIndex, panelInfo);
    }

    auto* panelBar = new ColorBarWidget(vtkWidget_);
    panelBar->setFocusPolicy(Qt::NoFocus);
    panelBar->setTitle(QStringFromUtf8(panelInfo.title));
    panelBar->setRange(panelInfo.globalRange[0], panelInfo.globalRange[1]);
    panelBar->setCyclic(panelInfo.analysis.cyclic);

    if (panelInfo.analysis.categorical) {
      vtkLookupTable* lut = renderer_.getFacetPanelLUT(panelIndex);
      panelBar->setCategorical(categoricalEntries(lut, panelInfo.analysis));
    } else {
      panelBar->setClipRange(panelInfo.clipRange[0], panelInfo.clipRange[1]);
    }
    panelBar->setVisible(true);

    QObject::connect(panelBar,
                     &ColorBarWidget::clipRangeChanged,
                     this,
                     [this, panelIndex, panelBar](double lo, double hi) {
                       if (renderer_.setFacetPanelClipRange(panelIndex, lo, hi)) {
                         FacetPanelInfo updated;
                         if (renderer_.getFacetPanelInfo(panelIndex, updated)) {
                           panelBar->setClipRange(updated.clipRange[0], updated.clipRange[1]);
                         }
                       }
                     });

    facetColorBars_.push_back(panelBar);
  }

  layoutFacetColorBars();
  QTimer::singleShot(0, this, [this]() { layoutFacetColorBars(); });
}

// ── normal (single-view) mode ──────────────────────────────────────────
void ViewerWindow::setupNormalMode() {
  renderer_.setup(load_.meshes.meshes, load_.meshes.names, partColors_);
  renderer_.start();

  buildPartsTree();

  QObject::connect(colorBar_,
                   &ColorBarWidget::clipRangeChanged,
                   this,
                   [this](double lo, double hi) { renderer_.setClipRange(lo, hi); });

  scalarFields_ = collectScalarUnion(load_.meshes.meshes);
  vectorFields_ = collectScalarUnion(load_.meshes.meshes, 3);
  if (!scalarFields_.empty()) {
    applyScalarAtIndex(0);
  } else {
    applyNoScalar();
  }

  if (temporal_ && temporal_->playable()) {
    setupPlayback();
  }
}

void ViewerWindow::buildPartsTree() {
  partsTree_->clear();
  for (const MeshGroup& group : load_.meshes.groups) {
    auto* groupItem = new QTreeWidgetItem(partsTree_);
    groupItem->setText(0, QStringFromUtf8(group.name));
    groupItem->setFlags(groupItem->flags() | Qt::ItemIsUserCheckable);
    groupItem->setCheckState(0, Qt::Checked);

    for (size_t partIndex : group.partIndices) {
      if (partIndex >= load_.meshes.partNames.size()) {
        continue;
      }
      auto* partItem = new QTreeWidgetItem(groupItem);
      partItem->setText(0, QStringFromUtf8(load_.meshes.partNames[partIndex]));
      if (partIndex < partColors_.size()) {
        partItem->setIcon(0, partColorIcon(partColors_[partIndex]));
      }
      partItem->setFlags(partItem->flags() | Qt::ItemIsUserCheckable);
      partItem->setCheckState(0, Qt::Checked);
      partItem->setData(0, Qt::UserRole, static_cast<qulonglong>(partIndex));
    }
    groupItem->setExpanded(group.partIndices.size() <= 8);
  }
  partsTree_->setVisible(!load_.meshes.groups.empty());

  QObject::connect(
      partsTree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (!item || column != 0) {
          return;
        }

        const bool checked = (item->checkState(0) == Qt::Checked);
        QSignalBlocker block(partsTree_);

        if (item->childCount() > 0) {
          for (int childIndex = 0; childIndex < item->childCount(); ++childIndex) {
            QTreeWidgetItem* child = item->child(childIndex);
            child->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
            const size_t partIndex =
                static_cast<size_t>(child->data(0, Qt::UserRole).toULongLong());
            renderer_.setPartVisible(partIndex, checked);
          }
          return;
        }

        const size_t partIndex = static_cast<size_t>(item->data(0, Qt::UserRole).toULongLong());
        renderer_.setPartVisible(partIndex, checked);

        QTreeWidgetItem* parent = item->parent();
        if (!parent) {
          return;
        }
        int checkedChildren = 0;
        for (int childIndex = 0; childIndex < parent->childCount(); ++childIndex) {
          if (parent->child(childIndex)->checkState(0) == Qt::Checked) {
            ++checkedChildren;
          }
        }
        if (checkedChildren == 0) {
          parent->setCheckState(0, Qt::Unchecked);
        } else if (checkedChildren == parent->childCount()) {
          parent->setCheckState(0, Qt::Checked);
        } else {
          parent->setCheckState(0, Qt::PartiallyChecked);
        }
      });
}

// ── playback toolbar for temporal (time-series) meshes ─────────────────
void ViewerWindow::setupPlayback() {
  const int numSteps = temporal_->steps();
  playbackBar_ = new PlaybackBar(numSteps, vtkWidget_);
  playbackBar_->setGeometry(playbackBarGeometry(vtkWidget_));
  playbackBar_->raise();
  playbackBar_->show();

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

  QObject::connect(playbackBar_, &PlaybackBar::playToggled, this, [this, numSteps](bool playing) {
    if (playing) {
      // Restart from the beginning if paused at the last frame.
      if (playbackBar_->currentStep() >= numSteps - 1) {
        showFrame(0);
      }
      restartPlayClock();
      applyPlayTimerInterval();
      playTimer_->start();
    } else {
      playTimer_->stop();
    }
  });

  QObject::connect(playbackBar_, &PlaybackBar::stepRequested, this, [this](int step) {
    showFrame(step);
    restartPlayClock();
  });

  QObject::connect(playbackBar_, &PlaybackBar::speedChanged, this, [this](double) {
    restartPlayClock();
    if (playTimer_->isActive()) {
      applyPlayTimerInterval();
    }
  });

  QTimer::singleShot(
      0, this, [this]() { playbackBar_->setGeometry(playbackBarGeometry(vtkWidget_)); });
}

void ViewerWindow::showFrame(int step) {
  if (!temporal_ || step < 0 || step >= temporal_->steps() || load_.meshes.meshes.empty()) {
    return;
  }
  temporal_->readStepInto(step, load_.meshes.meshes.front());
  renderer_.refreshAfterDataChange();
  currentPlaybackStep_ = step;
  if (playbackBar_) {
    playbackBar_->setStep(step, temporal_->timeAt(step));
  }
}

void ViewerWindow::restartPlayClock() {
  playAnchorStep_ = currentPlaybackStep_;
  playClock_.restart();
}

void ViewerWindow::applyPlayTimerInterval() {
  const double fps = kPlaybackBaseFps * playbackBar_->speedMultiplier();
  playTimer_->setInterval(std::max(4, static_cast<int>(std::round(1000.0 / fps))));
}

// ── annotation mode ─────────────────────────────────────────────────────
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

void ViewerWindow::setupAnnotateMode() {
  ensureLabelArray();
  picker_ = vtkSmartPointer<vtkCellPicker>::New();
  picker_->SetTolerance(0.0005);

  // Fixed value→color LUT so a value keeps its color while the array is edited.
  std::set<double> domain;
  for (int v = 0; v <= kNumLabels; ++v) {
    domain.insert(static_cast<double>(v));
  }
  labelLut_ = createCategoricalLookupTable(domain);
  labelLut_->SetTableValue(0, 0.6, 0.6, 0.6, 1.0); // value 0 = unlabeled → grey
  labelLut_->Modified();
  applyLabelColoring();

  // Floating bar: pencil toggle, one color swatch per label, brush size, save.
  annotationBar_ = new QWidget(vtkWidget_);
  annotationBar_->setObjectName("annotationBar");
  annotationBar_->setAttribute(Qt::WA_StyledBackground, true);
  annotationBar_->setFocusPolicy(Qt::NoFocus);
  annotationBar_->setStyleSheet(
      "QWidget#annotationBar { background: rgba(20,20,20,200); border-radius: 8px; }"
      "QToolButton { background: rgba(255,255,255,18); color: #E8E8E8; border: none;"
      "  border-radius: 4px; padding: 3px; }"
      "QToolButton:hover { background: rgba(255,255,255,40); }"
      "QToolButton:checked { background: rgba(80,150,250,160); }"
      "QLabel { color: #D8D8D8; font-size: 12px; }"
      "QSlider::groove:horizontal { height: 4px; background: rgba(255,255,255,50);"
      "  border-radius: 2px; }"
      "QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 6px;"
      "  background: #E8E8E8; }"
      "QSlider::sub-page:horizontal { background: #5096FA; border-radius: 2px; }");

  auto* row = new QHBoxLayout(annotationBar_);
  row->setContentsMargins(10, 6, 10, 6);
  row->setSpacing(8);

  pencilButton_ = new QToolButton(annotationBar_);
  pencilButton_->setIcon(makeAnnotIcon(AnnotGlyph::Pencil));
  pencilButton_->setIconSize(QSize(20, 20));
  pencilButton_->setCheckable(true);
  pencilButton_->setChecked(false); // navigation by default; click (or 'a') to paint
  pencilButton_->setFocusPolicy(Qt::NoFocus);
  pencilButton_->setToolTip(
      QStringLiteral("Paint (a) — off = rotate. 'e' toggles erase; right-drag also erases."));
  row->addWidget(pencilButton_);
  const QCursor brushCursor = makeBrushCursor();
  QObject::connect(pencilButton_, &QToolButton::toggled, this, [this, brushCursor](bool on) {
    vtkWidget_->setCursor(on ? brushCursor : QCursor(Qt::ArrowCursor));
  });
  vtkWidget_->setCursor(Qt::ArrowCursor);
  // Toolbar is a child of vtkWidget_, so it inherits the brush cursor; force arrow.
  annotationBar_->setCursor(Qt::ArrowCursor);

  auto* swatches = new QButtonGroup(this);
  swatches->setExclusive(true);
  for (int v = 0; v <= kNumLabels; ++v) { // v == 0 is the eraser (grey / unlabeled)
    double rgb[3];
    labelLut_->GetColor(static_cast<double>(v), rgb);
    const QColor color = QColor::fromRgbF(
        static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2]));
    auto* sw = new QToolButton(annotationBar_);
    sw->setCheckable(true);
    sw->setFixedSize(22, 22);
    sw->setFocusPolicy(Qt::NoFocus);
    sw->setToolTip(v == 0 ? QStringLiteral("Erase") : QString::number(v));
    sw->setStyleSheet(QStringLiteral("QToolButton { background: %1; border: 2px solid "
                                     "rgba(0,0,0,0); border-radius: 4px; }"
                                     "QToolButton:checked { border: 2px solid white; }")
                          .arg(color.name()));
    swatches->addButton(sw, v);
    row->addWidget(sw);
    if (v == currentLabel_) {
      sw->setChecked(true);
    }
  }
  QObject::connect(
      swatches, &QButtonGroup::idClicked, this, [this](int id) { currentLabel_ = id; });

  row->addWidget(new QLabel(QStringLiteral("Brush"), annotationBar_));
  brushSlider_ = new QSlider(Qt::Horizontal, annotationBar_);
  brushSlider_->setRange(0, 15);
  brushSlider_->setValue(3);
  brushSlider_->setFixedWidth(90);
  brushSlider_->setFocusPolicy(Qt::NoFocus);
  brushSlider_->setToolTip(QStringLiteral("Brush size: surface rings around the picked cell."));
  row->addWidget(brushSlider_);

  auto* saveButton = new QToolButton(annotationBar_);
  saveButton->setIcon(makeAnnotIcon(AnnotGlyph::Save));
  saveButton->setIconSize(QSize(20, 20));
  saveButton->setFocusPolicy(Qt::NoFocus);
  saveButton->setToolTip(QStringLiteral("Save annotations to .vtp/.vtu"));
  row->addWidget(saveButton);
  QObject::connect(saveButton, &QToolButton::clicked, this, [this]() { saveAnnotations(); });

  annotationBar_->show();
  annotationBar_->raise();
  QTimer::singleShot(0, this, [this]() {
    annotationBar_->setGeometry(annotationBarGeometry(vtkWidget_, annotationBar_));
    annotationBar_->raise();
  });

  // 'a'/'e' are handled as press-and-hold in VtkMouseFilter, not QShortcuts.
  auto bumpBrush = [this](int delta) {
    if (brushSlider_) {
      brushSlider_->setValue(brushSlider_->value() + delta);
    }
  };
  for (const auto key : {Qt::Key_Plus, Qt::Key_Equal}) { // '=' so + needs no Shift
    QObject::connect(new QShortcut(QKeySequence(key), this),
                     &QShortcut::activated,
                     this,
                     [bumpBrush]() { bumpBrush(+1); });
  }
  for (const auto key : {Qt::Key_Minus, Qt::Key_Underscore}) {
    QObject::connect(new QShortcut(QKeySequence(key), this),
                     &QShortcut::activated,
                     this,
                     [bumpBrush]() { bumpBrush(-1); });
  }
  auto* undo = new QShortcut(QKeySequence(QKeySequence::Undo), this); // Cmd/Ctrl+Z
  QObject::connect(undo, &QShortcut::activated, this, [this]() { undoStroke(); });
}

// Press-and-hold from VtkMouseFilter: arm paint (erase = label 0) while held,
// disarm on release. Clicking pencilButton stays as a sticky lock.
void ViewerWindow::setPaintHold(bool active, bool erase) {
  if (!pencilButton_) {
    return;
  }
  eraseMode_ = active && erase;
  pencilButton_->setChecked(active);
  if (!active) {
    clearBrushPreview();
  }
}

void ViewerWindow::togglePaint() {
  if (pencilButton_) {
    pencilButton_->setChecked(!pencilButton_->isChecked());
  }
}

bool ViewerWindow::handleAnnotatePointer(QMouseEvent* event) {
  if (!pencilButton_ || !pencilButton_->isChecked()) {
    return false;
  }
  switch (event->type()) {
  case QEvent::MouseButtonPress:
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
      painting_ = true;
      currentStroke_.clear();
      clearBrushPreview();
      paintAtWidgetPos(mouseLocalPos(event), eraseMode_ || event->button() == Qt::RightButton);
      return true;
    }
    return false;
  case QEvent::MouseMove:
    if (painting_ && (event->buttons() & (Qt::LeftButton | Qt::RightButton))) {
      paintAtWidgetPos(mouseLocalPos(event),
                       eraseMode_ || (event->buttons() & Qt::RightButton) != 0);
      return true;
    }
    updateBrushPreview(mouseLocalPos(event)); // hover with no button: show the footprint
    return false;
  case QEvent::MouseButtonRelease:
    if (painting_) {
      painting_ = false;
      if (!currentStroke_.empty()) {
        undoStack_.push_back(std::move(currentStroke_));
        currentStroke_.clear();
        constexpr size_t kMaxUndo = 50;
        if (undoStack_.size() > kMaxUndo) {
          undoStack_.erase(undoStack_.begin());
        }
      }
      return true;
    }
    return false;
  default:
    return false;
  }
}

void ViewerWindow::paintAtWidgetPos(const QPointF& pos, bool erase) {
  vtkRenderer* ren = renderer_.getRenderer();
  vtkDataSet* mesh = renderer_.getPrimaryMesh();
  if (!ren || !mesh || !labelArray_ || !picker_) {
    return;
  }
  const double ratio = vtkWidget_->devicePixelRatioF();
  const double x = pos.x() * ratio;
  const double y = (vtkWidget_->height() - pos.y()) * ratio; // Qt top-left → VTK bottom-left
  if (picker_->Pick(x, y, 0.0, ren) == 0) {
    return;
  }
  const vtkIdType cell = picker_->GetCellId();
  if (cell < 0) {
    return;
  }
  const int value = erase ? 0 : currentLabel_;
  bool changed = false;
  for (vtkIdType c : growCells(mesh, cell, brushSlider_->value())) {
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

void ViewerWindow::updateBrushPreview(const QPointF& pos) {
  vtkRenderer* ren = renderer_.getRenderer();
  vtkDataSet* mesh = renderer_.getPrimaryMesh();
  if (!ren || !mesh || !picker_ || !labelLut_) {
    clearBrushPreview();
    return;
  }
  const double ratio = vtkWidget_->devicePixelRatioF();
  const double x = pos.x() * ratio;
  const double y = (vtkWidget_->height() - pos.y()) * ratio;
  if (picker_->Pick(x, y, 0.0, ren) == 0 || picker_->GetCellId() < 0) {
    clearBrushPreview();
    return;
  }

  const std::vector<vtkIdType> ids = growCells(mesh, picker_->GetCellId(), brushSlider_->value());
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

  double rgb[3];
  labelLut_->GetColor(static_cast<double>(eraseMode_ ? 0 : currentLabel_), rgb);
  previewActor_->GetProperty()->SetColor(rgb);
  previewActor_->VisibilityOn();
  if (renderer_.context.window) {
    renderer_.context.window->Render();
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

void ViewerWindow::undoStroke() {
  if (!labelArray_ || undoStack_.empty()) {
    return;
  }
  for (const auto& [cell, prev] : undoStack_.back()) {
    if (cell >= 0 && cell < labelArray_->GetNumberOfTuples()) {
      labelArray_->SetValue(static_cast<vtkIdType>(cell), prev);
    }
  }
  undoStack_.pop_back();
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

void ViewerWindow::saveAnnotations() {
  vtkDataSet* mesh = renderer_.getPrimaryMesh();
  if (!mesh) {
    return;
  }
  auto* pd = vtkPolyData::SafeDownCast(mesh);
  auto* ug = vtkUnstructuredGrid::SafeDownCast(mesh);
  if (!pd && !ug) {
    QMessageBox::warning(this,
                         QStringLiteral("Save failed"),
                         QStringLiteral("Only polydata/unstructured-grid meshes can be saved."));
    return;
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
    return;
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
  } else {
    statusBar()->showMessage(QStringLiteral("Saved ") + path, 4000);
  }
}

// ── scalar handling ────────────────────────────────────────────────────
void ViewerWindow::applyNoScalar() {
  renderer_.clearActiveScalar();
  colorBar_->setVisible(false);
  colorBar_->setTitle("Geometry");
  activeScalarIdx_ = -1;
}

void ViewerWindow::applyScalarAtIndex(int index) {
  if (index < 0 || index >= static_cast<int>(scalarFields_.size())) {
    applyNoScalar();
    return;
  }

  const ScalarField& field = scalarFields_[static_cast<size_t>(index)];
  const std::string& scalarName = field.name;

  // Temporal: restrict frame reads to this array and reload the current frame so
  // the mesh holds it before the scalar is applied. Reader array selection only
  // covers point data, so cell fields fall back to reading all arrays per frame.
  const bool temporalPoint =
      temporal_ && temporal_->playable() && field.association == FieldAssociation::Point;
  if (temporalPoint) {
    temporal_->setActiveArray(scalarName);
    temporal_->readStepInto(currentPlaybackStep_, load_.meshes.meshes.front());
  }

  if (!renderer_.setActiveScalar(scalarName, field.association)) {
    return;
  }
  activeScalarIdx_ = index;
  colorBar_->setTitle(scalarTitle(field));

  // For temporal data, fix the color range to the union across sampled steps so
  // the colormap does not flicker as frames advance.
  if (temporalPoint) {
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
  if (options_.hasFixedRange && !analysis.categorical) {
    renderer_.setActiveScalarRange(options_.fixedRange[0], options_.fixedRange[1]);
  }

  double globalRange[2] = {0.0, 1.0};
  if (!renderer_.getActiveScalarGlobalRange(globalRange)) {
    colorBar_->setVisible(false);
    return;
  }
  colorBar_->setVisible(true);
  colorBar_->setRange(globalRange[0], globalRange[1]);

  colorBar_->setCyclic(analysis.cyclic);
  if (analysis.categorical) {
    colorBar_->setCategorical(categoricalEntries(renderer_.getActiveLUT(), analysis));
  } else {
    colorBar_->clearCategorical();
    double clipRange[2] = {globalRange[0], globalRange[1]};
    renderer_.getClipRange(clipRange);
    colorBar_->setClipRange(clipRange[0], clipRange[1]);
  }
}

void ViewerWindow::cycleScalar() {
  if (scalarFields_.empty()) {
    applyNoScalar();
    return;
  }
  if (activeScalarIdx_ < 0) {
    applyScalarAtIndex(0);
    return;
  }
  const int next = activeScalarIdx_ + 1;
  if (next >= static_cast<int>(scalarFields_.size())) {
    applyNoScalar();
    return;
  }
  applyScalarAtIndex(next);
}

// 'c': linear ↔ cyclic colormap for the active continuous field.
void ViewerWindow::toggleCyclicColormap() {
  const bool cyclic = renderer_.toggleCyclicColormap();
  colorBar_->setCyclic(cyclic);
  statusBar()->showMessage(cyclic ? "cyclic colormap" : "linear colormap", 1500);
}

// 'v': cycle vector-field glyphs (each 3-component array, then off).
void ViewerWindow::cycleVectorField() {
  if (vectorFields_.empty()) {
    statusBar()->showMessage("no vector field in this mesh", 1500);
    return;
  }
  activeVectorIdx_ = (activeVectorIdx_ + 1) % (static_cast<int>(vectorFields_.size()) + 1);
  if (activeVectorIdx_ == static_cast<int>(vectorFields_.size())) {
    activeVectorIdx_ = -1;
    renderer_.setVectorGlyphs("", FieldAssociation::Point);
    statusBar()->showMessage("glyphs off", 1500);
    return;
  }
  const ScalarField& field = vectorFields_[static_cast<size_t>(activeVectorIdx_)];
  if (renderer_.setVectorGlyphs(field.name, field.association)) {
    statusBar()->showMessage(QStringLiteral("glyphs: ") + scalarTitle(field), 1500);
  }
}

// ── overlay layout ─────────────────────────────────────────────────────
void ViewerWindow::onViewportResize() {
  layoutFacetColorBars();
  if (playbackBar_) {
    playbackBar_->setGeometry(playbackBarGeometry(vtkWidget_));
    playbackBar_->raise();
  }
  if (annotationBar_) {
    annotationBar_->setGeometry(annotationBarGeometry(vtkWidget_, annotationBar_));
    annotationBar_->raise();
  }
}

void ViewerWindow::layoutFacetColorBars() {
  if (facetColorBars_.empty()) {
    return;
  }
  const int viewportWidth = vtkWidget_->width();
  const int viewportHeight = vtkWidget_->height();
  const int normalTargetHeight = std::clamp(
      static_cast<int>(viewportHeight * kOverlayHeightRatio), kOverlayMinHeight, kOverlayMaxHeight);

  for (size_t panelIndex = 0; panelIndex < facetColorBars_.size(); ++panelIndex) {
    FacetPanelInfo panelInfo;
    if (!renderer_.getFacetPanelInfo(panelIndex, panelInfo)) {
      continue;
    }

    const int panelX = static_cast<int>(std::round(panelInfo.viewport[0] * viewportWidth));
    const int panelY = static_cast<int>(std::round((1.0 - panelInfo.viewport[3]) * viewportHeight));
    const int panelW =
        std::max(1,
                 static_cast<int>(
                     std::round((panelInfo.viewport[2] - panelInfo.viewport[0]) * viewportWidth)));
    const int panelH =
        std::max(1,
                 static_cast<int>(
                     std::round((panelInfo.viewport[3] - panelInfo.viewport[1]) * viewportHeight)));

    ColorBarWidget* bar = facetColorBars_[panelIndex];
    const int margin = kFacetBarMargin;
    const int barW = std::clamp(bar->sizeHint().width(), kFacetBarMinWidth, kFacetBarMaxWidth);
    int barH = normalTargetHeight;
    barH = std::min(barH, std::max(40, panelH - 2 * margin));
    const int barX = panelX + std::max(0, panelW - barW - margin);
    const int barY = panelY + std::max(1, (panelH - barH) / 2);

    bar->setGeometry(barX, barY, barW, barH);
    bar->raise();
  }
}
