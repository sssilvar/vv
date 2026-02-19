#include "ColorBarWidget.h"
#include "MeshLoading.h"
#include "MeshRenderer.h"
#include "mesh_utils.h"
#include "version.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPointer>
#include <QSignalBlocker>
#include <QSurfaceFormat>
#include <QTimer>
#include <QTreeWidget>
#include <QVTKOpenGLNativeWidget.h>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <array>
#include <cmath>
#include <cxxopts.hpp>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#include <vtkCamera.h>
#include <vtkDataArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkPointData.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>

namespace {
constexpr int kOverlayMargin = 12;
constexpr int kOverlayWidth = 96;
constexpr double kOverlayHeightRatio = 0.45;
constexpr int kOverlayMinHeight = 150;
constexpr int kOverlayMaxHeight = 260;
constexpr int kTreeOverlayMargin = 16;
constexpr int kTreeOverlayWidth = 360;
constexpr double kTreeOverlayHeightRatio = 0.40;
constexpr int kTreeOverlayMinHeight = 140;
constexpr int kTreeOverlayMaxHeight = 340;
constexpr int kFacetBarMargin = 6;
constexpr int kFacetBarWidth = 68;

QRect colorBarOverlayGeometry(const QWidget* viewport) {
  const int height = std::clamp(static_cast<int>(viewport->height() * kOverlayHeightRatio),
                                kOverlayMinHeight,
                                kOverlayMaxHeight);
  const int x = std::max(kOverlayMargin, viewport->width() - kOverlayWidth - kOverlayMargin);
  const int y = std::max(kOverlayMargin, (viewport->height() - height) / 2);
  return QRect(x, y, kOverlayWidth, height);
}

QRect treeOverlayGeometry(const QWidget* viewport) {
  const int height = std::clamp(static_cast<int>(viewport->height() * kTreeOverlayHeightRatio),
                                kTreeOverlayMinHeight,
                                kTreeOverlayMaxHeight);
  return QRect(kTreeOverlayMargin, kTreeOverlayMargin, kTreeOverlayWidth, height);
}

std::vector<std::string>
collectScalarUnion(const std::vector<vtkSmartPointer<vtkPolyData>>& polys) {
  std::set<std::string> names;
  for (const auto& poly : polys) {
    if (!poly || !poly->GetPointData()) {
      continue;
    }
    vtkPointData* pointData = poly->GetPointData();
    for (int arrayIndex = 0; arrayIndex < pointData->GetNumberOfArrays(); ++arrayIndex) {
      vtkDataArray* arr = pointData->GetArray(arrayIndex);
      if (!arr || !arr->GetName()) {
        continue;
      }
      names.insert(arr->GetName());
    }
  }
  return std::vector<std::string>(names.begin(), names.end());
}
} // namespace

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
                          QObject* parent = nullptr)
      : QObject(parent), vtkRoot_(vtkRoot), overlayColorBar_(overlayColorBar),
        overlayTree_(overlayTree), onSpaceCycle_(std::move(onSpaceCycle)),
        onViewportResize_(std::move(onViewportResize)) {}

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

    switch (event->type()) {
    case QEvent::Resize:
      if (widget == vtkRoot) {
        if (overlayColorBar) {
          overlayColorBar->setGeometry(colorBarOverlayGeometry(vtkRoot));
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
      break;
    }
    case QEvent::ShortcutOverride: {
      auto* ke = static_cast<QKeyEvent*>(event);
      if (ke->key() == Qt::Key_Space || ke->key() == Qt::Key_Q) {
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
};

// ─────────────────────────────────────────────────────────────────────
struct Args {
  std::vector<std::string> meshfiles;
  bool explode_view = false;
  bool version = false;
  bool help = false;
};

Args parseArgs(int argc, char* argv[]) {
  Args args;
  cxxopts::Options options("vv", "A Qt-based mesh viewer");
  options.positional_help("<meshfile> [<meshfile2> ...]");
  options.add_options()(
      "e,explode", "Explode scalar view", cxxopts::value<bool>(args.explode_view))(
      "v,version", "Show version and exit", cxxopts::value<bool>(args.version))(
      "h,help", "Show help and exit", cxxopts::value<bool>(args.help))(
      "meshfiles", "Mesh files or '-'", cxxopts::value<std::vector<std::string>>(args.meshfiles));
  options.parse_positional({"meshfiles"});

  options.parse(argc, argv);

  if (args.help) {
    std::cout << options.help() << '\n';
    std::exit(0);
  }
  if (args.version) {
    std::cout << "vv version " << VV_VERSION << " (built " << VV_BUILD_DATE << ")\n";
    std::exit(0);
  }
  if (args.meshfiles.empty()) {
    std::cerr << "Usage: vv <meshfile> [<meshfile2> ...]\n" << options.help() << '\n';
    std::exit(1);
  }
  return args;
}

// ═════════════════════════════════════════════════════════════════════
int main(int argc, char* argv[]) {
  Args args = parseArgs(argc, argv);

  if (args.meshfiles.size() > 1 && !args.explode_view) {
    std::cerr << "Warning: Multiple mesh files provided without -e flag. "
                 "Using only the first file.\n";
  }

  MeshLoadResult loadResult = loadMeshes(args.meshfiles, args.explode_view);
  if (!loadResult.ok) {
    std::cerr << loadResult.error << std::endl;
    return loadResult.exitCode;
  }

  auto& allPolys = loadResult.meshes.polys;
  auto& allNames = loadResult.meshes.names;

  std::vector<std::array<double, 3>> colorsHex;
  colorsHex.reserve(allPolys.size());
  for (size_t i = 0; i < allPolys.size(); ++i)
    colorsHex.push_back(generateDistinctColor(static_cast<int>(i)));

  // ── Qt + VTK setup ────────────────────────────────────────────
  QSurfaceFormat format = QVTKOpenGLNativeWidget::defaultFormat();
  format.setSwapInterval(0);
  format.setSamples(0);
  QSurfaceFormat::setDefaultFormat(format);
  QApplication app(argc, argv);

  QMainWindow window;
  window.setWindowTitle("VV Qt mesh viewer");
  window.resize(1300, 980);

  auto* central = new QWidget(&window);
  auto* layout = new QHBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // Left pane — the VTK 3‑D view
  auto* vtkWidget = new QVTKOpenGLNativeWidget(central);
  vtkWidget->setFocusPolicy(Qt::StrongFocus);
  vtkWidget->setAttribute(Qt::WA_AcceptTouchEvents, false);
  layout->addWidget(vtkWidget, 1);

  window.setCentralWidget(central);

  // ── VTK render window ─────────────────────────────────────────
  auto renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
  renderWindow->SetMultiSamples(0);
  renderWindow->SetDesiredUpdateRate(120.0);
  vtkWidget->setRenderWindow(renderWindow);

  MeshRenderer renderer;
  renderer.setRenderContext(renderWindow, vtkWidget->interactor());

  auto* colorBar = new ColorBarWidget(vtkWidget);
  colorBar->setVisible(false);
  colorBar->setAttribute(Qt::WA_TransparentForMouseEvents, false);
  colorBar->setFocusPolicy(Qt::NoFocus);
  colorBar->setGeometry(colorBarOverlayGeometry(vtkWidget));
  colorBar->raise();

  auto* partsTree = new QTreeWidget(vtkWidget);
  partsTree->setColumnCount(1);
  partsTree->setHeaderHidden(true);
  partsTree->setRootIsDecorated(true);
  partsTree->setUniformRowHeights(true);
  partsTree->setIndentation(18);
  partsTree->setGeometry(treeOverlayGeometry(vtkWidget));
  partsTree->setVisible(false);
  partsTree->setFocusPolicy(Qt::NoFocus);
  partsTree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  partsTree->setStyleSheet("QTreeWidget {"
                           "  background: rgba(0,0,0,0);"
                           "  color: #E2E2E2;"
                           "  outline: none;"
                           "  padding: 2px;"
                           "}");
  partsTree->raise();

  std::vector<std::string> scalarNames;
  int activeScalarIdx = -1;
  std::vector<ColorBarWidget*> facetColorBars;

  auto layoutFacetColorBars = [&]() {
    if (facetColorBars.empty()) {
      return;
    }
    const int viewportWidth = vtkWidget->width();
    const int viewportHeight = vtkWidget->height();
    const int normalTargetHeight =
        std::clamp(static_cast<int>(viewportHeight * kOverlayHeightRatio),
                   kOverlayMinHeight,
                   kOverlayMaxHeight);

    for (size_t panelIndex = 0; panelIndex < facetColorBars.size(); ++panelIndex) {
      FacetPanelInfo panelInfo;
      if (!renderer.getFacetPanelInfo(panelIndex, panelInfo)) {
        continue;
      }

      const int panelX = static_cast<int>(std::round(panelInfo.viewport[0] * viewportWidth));
      const int panelY =
          static_cast<int>(std::round((1.0 - panelInfo.viewport[3]) * viewportHeight));
      const int panelW =
          std::max(1,
                   static_cast<int>(std::round((panelInfo.viewport[2] - panelInfo.viewport[0]) *
                                               viewportWidth)));
      const int panelH =
          std::max(1,
                   static_cast<int>(std::round((panelInfo.viewport[3] - panelInfo.viewport[1]) *
                                               viewportHeight)));

      const int margin = kFacetBarMargin;
      const int barW = kFacetBarWidth;
      int barH = normalTargetHeight;
      barH = std::min(barH, std::max(40, panelH - 2 * margin));
      const int barX = panelX + std::max(0, panelW - barW - margin);
      const int barY = panelY + std::max(1, (panelH - barH) / 2);

      ColorBarWidget* bar = facetColorBars[panelIndex];
      bar->setGeometry(barX, barY, barW, barH);
      bar->raise();
    }
  };

  auto applyNoScalar = [&]() {
    renderer.clearActiveScalar();
    colorBar->setVisible(false);
    colorBar->setTitle("Geometry");
    activeScalarIdx = -1;
  };

  auto applyScalarAtIndex = [&](int index) {
    if (index < 0 || index >= static_cast<int>(scalarNames.size())) {
      applyNoScalar();
      return;
    }

    if (!renderer.setActiveScalar(scalarNames[index])) {
      return;
    }
    activeScalarIdx = index;
    colorBar->setTitle(QString::fromStdString(scalarNames[index]));

    double globalRange[2] = {0.0, 1.0};
    if (!renderer.getActiveScalarGlobalRange(globalRange)) {
      colorBar->setVisible(false);
      return;
    }
    colorBar->setVisible(true);
    colorBar->setRange(globalRange[0], globalRange[1]);
    double clipRange[2] = {globalRange[0], globalRange[1]};
    renderer.getClipRange(clipRange);
    colorBar->setClipRange(clipRange[0], clipRange[1]);
  };

  auto cycleScalar = [&]() {
    if (scalarNames.empty()) {
      applyNoScalar();
      return;
    }
    if (activeScalarIdx < 0) {
      applyScalarAtIndex(0);
      return;
    }
    const int next = activeScalarIdx + 1;
    if (next >= static_cast<int>(scalarNames.size())) {
      applyNoScalar();
      return;
    }
    applyScalarAtIndex(next);
  };

  app.installEventFilter(
      new VtkMouseFilter(vtkWidget, colorBar, partsTree, cycleScalar, layoutFacetColorBars, &app));

  QTimer::singleShot(0, [&]() { colorBar->setGeometry(colorBarOverlayGeometry(vtkWidget)); });

  QTimer::singleShot(0, [&]() { partsTree->setGeometry(treeOverlayGeometry(vtkWidget)); });

  const bool useFacetGrid = args.explode_view;
  if (useFacetGrid) {
    renderer.setupFacetGrid(allPolys, allNames, colorsHex);
    renderer.startFacetGrid();
    colorBar->setVisible(false);
    partsTree->setVisible(false);

    const size_t panelCount = renderer.getFacetPanelCount();
    facetColorBars.reserve(panelCount);
    for (size_t panelIndex = 0; panelIndex < panelCount; ++panelIndex) {
      FacetPanelInfo panelInfo;
      if (!renderer.getFacetPanelInfo(panelIndex, panelInfo)) {
        continue;
      }

      auto* panelBar = new ColorBarWidget(vtkWidget);
      panelBar->setFocusPolicy(Qt::NoFocus);
      panelBar->setTitle(QString::fromStdString(panelInfo.title));
      panelBar->setRange(panelInfo.globalRange[0], panelInfo.globalRange[1]);
      panelBar->setClipRange(panelInfo.clipRange[0], panelInfo.clipRange[1]);
      panelBar->setVisible(true);

      QObject::connect(panelBar,
                       &ColorBarWidget::clipRangeChanged,
                       [&, panelIndex, panelBar](double lo, double hi) {
                         if (renderer.setFacetPanelClipRange(panelIndex, lo, hi)) {
                           FacetPanelInfo updated;
                           if (renderer.getFacetPanelInfo(panelIndex, updated)) {
                             panelBar->setClipRange(updated.clipRange[0], updated.clipRange[1]);
                           }
                         }
                       });

      facetColorBars.push_back(panelBar);
    }

    layoutFacetColorBars();
    QTimer::singleShot(0, [&]() { layoutFacetColorBars(); });
  } else {
    renderer.setup(allPolys, allNames, colorsHex);
    renderer.start();

    partsTree->clear();
    for (const MeshGroup& group : loadResult.meshes.groups) {
      auto* groupItem = new QTreeWidgetItem(partsTree);
      groupItem->setText(0, QString::fromStdString(group.name));
      groupItem->setFlags(groupItem->flags() | Qt::ItemIsUserCheckable);
      groupItem->setCheckState(0, Qt::Checked);

      for (size_t partIndex : group.partIndices) {
        if (partIndex >= loadResult.meshes.partNames.size()) {
          continue;
        }
        auto* partItem = new QTreeWidgetItem(groupItem);
        partItem->setText(0, QString::fromStdString(loadResult.meshes.partNames[partIndex]));
        partItem->setFlags(partItem->flags() | Qt::ItemIsUserCheckable);
        partItem->setCheckState(0, Qt::Checked);
        partItem->setData(0, Qt::UserRole, static_cast<qulonglong>(partIndex));
      }
      groupItem->setExpanded(group.partIndices.size() <= 8);
    }
    partsTree->setVisible(!loadResult.meshes.groups.empty());

    QObject::connect(partsTree, &QTreeWidget::itemChanged, [&](QTreeWidgetItem* item, int column) {
      if (!item || column != 0) {
        return;
      }

      const bool checked = (item->checkState(0) == Qt::Checked);
      QSignalBlocker block(partsTree);

      if (item->childCount() > 0) {
        for (int childIndex = 0; childIndex < item->childCount(); ++childIndex) {
          QTreeWidgetItem* child = item->child(childIndex);
          child->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
          const size_t partIndex = static_cast<size_t>(child->data(0, Qt::UserRole).toULongLong());
          renderer.setPartVisible(partIndex, checked);
        }
        return;
      }

      const size_t partIndex = static_cast<size_t>(item->data(0, Qt::UserRole).toULongLong());
      renderer.setPartVisible(partIndex, checked);

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

    // ── colorbar handles → renderer ────────────────────────────
    QObject::connect(colorBar, &ColorBarWidget::clipRangeChanged, [&](double lo, double hi) {
      renderer.setClipRange(lo, hi);
    });

    scalarNames = collectScalarUnion(allPolys);
    if (!scalarNames.empty()) {
      applyScalarAtIndex(0);
    } else {
      applyNoScalar();
    }
  }

  window.show();
  vtkWidget->setFocus();
  return app.exec();
}
