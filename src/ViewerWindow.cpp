#include "ViewerWindow.h"

#include "ColorBarWidget.h"
#include "PlaybackBar.h"
#include "ScalarVizUtils.h"
#include "TemporalSource.h"
#include "mesh_utils.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QVTKOpenGLNativeWidget.h>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <utility>
#include <vtkCamera.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>

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
std::vector<ScalarField>
collectScalarUnion(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes) {
  std::set<std::string> pointNames;
  std::set<std::string> cellNames;
  for (const auto& mesh : meshes) {
    if (!mesh) {
      continue;
    }
    if (auto* pd = mesh->GetPointData()) {
      for (int i = 0; i < pd->GetNumberOfArrays(); ++i) {
        vtkDataArray* arr = pd->GetArray(i);
        if (arr && arr->GetName())
          pointNames.insert(arr->GetName());
      }
    }
    if (auto* cd = mesh->GetCellData()) {
      for (int i = 0; i < cd->GetNumberOfArrays(); ++i) {
        vtkDataArray* arr = cd->GetArray(i);
        if (arr && arr->GetName())
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
      this));

  QTimer::singleShot(0, this, [this]() {
    colorBar_->setGeometry(colorBarOverlayGeometry(vtkWidget_, colorBar_));
    partsTree_->setGeometry(treeOverlayGeometry(vtkWidget_));
  });

  if (options_.explodeView) {
    setupFacetMode();
  } else {
    setupNormalMode();
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

    auto* panelBar = new ColorBarWidget(vtkWidget_);
    panelBar->setFocusPolicy(Qt::NoFocus);
    panelBar->setTitle(QStringFromUtf8(panelInfo.title));
    panelBar->setRange(panelInfo.globalRange[0], panelInfo.globalRange[1]);

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

  QObject::connect(playTimer_, &QTimer::timeout, this, [this, numSteps]() {
    int next = playbackBar_->currentStep() + 1;
    if (next >= numSteps) {
      if (playbackBar_->loopEnabled()) {
        next = 0;
      } else {
        playTimer_->stop();
        playbackBar_->setPlaying(false);
        return;
      }
    }
    showFrame(next);
  });

  QObject::connect(playbackBar_, &PlaybackBar::playToggled, this, [this, numSteps](bool playing) {
    if (playing) {
      // Restart from the beginning if paused at the last frame.
      if (playbackBar_->currentStep() >= numSteps - 1) {
        showFrame(0);
      }
      applyPlayTimerInterval();
      playTimer_->start();
    } else {
      playTimer_->stop();
    }
  });

  QObject::connect(
      playbackBar_, &PlaybackBar::stepRequested, this, [this](int step) { showFrame(step); });

  QObject::connect(playbackBar_, &PlaybackBar::speedChanged, this, [this](double) {
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

void ViewerWindow::applyPlayTimerInterval() {
  const double fps = 15.0 * playbackBar_->speedMultiplier();
  playTimer_->setInterval(std::max(1, static_cast<int>(std::round(1000.0 / fps))));
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

  double globalRange[2] = {0.0, 1.0};
  if (!renderer_.getActiveScalarGlobalRange(globalRange)) {
    colorBar_->setVisible(false);
    return;
  }
  colorBar_->setVisible(true);
  colorBar_->setRange(globalRange[0], globalRange[1]);

  const ScalarAnalysis& analysis = renderer_.getActiveScalarAnalysis();
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

// ── overlay layout ─────────────────────────────────────────────────────
void ViewerWindow::onViewportResize() {
  layoutFacetColorBars();
  if (playbackBar_) {
    playbackBar_->setGeometry(playbackBarGeometry(vtkWidget_));
    playbackBar_->raise();
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
