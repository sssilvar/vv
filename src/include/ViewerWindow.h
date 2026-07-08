#pragma once

#include "MeshLoading.h"
#include "MeshRenderer.h"
#include "ScalarVizUtils.h"

#include <QMainWindow>
#include <QPointer>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class ColorBarWidget;
class PlaybackBar;
class QTimer;
class QTreeWidget;
class QVTKOpenGLNativeWidget;
class TemporalSource;

struct ViewerOptions {
  bool explodeView = false;
  bool commonCatLut = false;
  bool annotate = false;
};

class QSlider;
class QToolButton;
class QWidget;
class vtkCellPicker;
class vtkLookupTable;
class QMouseEvent;

// Main application window: owns the VTK viewport, the overlay widgets
// (colorbar, parts tree, playback bar) and all viewer state previously held as
// lambda captures in main().
class ViewerWindow : public QMainWindow {
  Q_OBJECT
public:
  ViewerWindow(MeshLoadResult loadResult, const ViewerOptions& options, QWidget* parent = nullptr);

private:
  // ── setup ─────────────────────────────────────────────────────────
  void buildViewport();
  void setupFacetMode();
  void setupNormalMode();
  void buildPartsTree();
  void setupPlayback();

  // ── annotation mode ───────────────────────────────────────────────
  void setupAnnotateMode();
  void ensureLabelArray();
  void togglePaint();
  void setPaintHold(bool active, bool erase);
  // Handle a pointer event over the viewport while annotating; returns true if
  // consumed (so the VTK camera does not also react). erase = paint value 0.
  bool handleAnnotatePointer(QMouseEvent* event);
  void paintAtWidgetPos(const QPointF& pos, bool erase);
  void updateBrushPreview(const QPointF& pos); // faded overlay of cells a click would hit
  void clearBrushPreview();
  void applyLabelColoring();
  void undoStroke();
  void saveAnnotations();

  // ── scalar handling ───────────────────────────────────────────────
  void applyScalarAtIndex(int index);
  void applyNoScalar();
  void cycleScalar();

  // ── layout / playback ─────────────────────────────────────────────
  void layoutFacetColorBars();
  void onViewportResize();
  void showFrame(int step);
  void applyPlayTimerInterval();

  // ── state ─────────────────────────────────────────────────────────
  MeshLoadResult load_;
  ViewerOptions options_;
  std::vector<std::array<double, 3>> partColors_;

  MeshRenderer renderer_;
  QVTKOpenGLNativeWidget* vtkWidget_ = nullptr;
  ColorBarWidget* colorBar_ = nullptr;
  QTreeWidget* partsTree_ = nullptr;
  std::vector<ColorBarWidget*> facetColorBars_;

  std::vector<ScalarField> scalarFields_;
  int activeScalarIdx_ = -1;

  // Temporal (playable) support: when a time-series file is loaded, the color
  // range is fixed across the whole animation (sampled once per scalar) so the
  // colormap stays stable while frames advance.
  std::shared_ptr<TemporalSource> temporal_;
  std::map<std::string, std::array<double, 2>> temporalRangeCache_;
  QPointer<PlaybackBar> playbackBar_;
  QTimer* playTimer_ = nullptr;
  int currentPlaybackStep_ = 0;

  // Annotation state (only populated in --annotate mode).
  QWidget* annotationBar_ = nullptr;
  QToolButton* pencilButton_ = nullptr; // checked = paint, unchecked = rotate
  QSlider* brushSlider_ = nullptr;
  int currentLabel_ = 1;
  vtkSmartPointer<vtkCellPicker> picker_;
  vtkSmartPointer<vtkLookupTable> labelLut_; // fixed value→color, stable while painting
  class vtkIntArray* labelArray_ = nullptr;  // owned by the mesh's cell data
  bool painting_ = false;
  bool eraseMode_ = false; // 'e' toggles: left-drag erases instead of painting
  vtkSmartPointer<class vtkActor> previewActor_; // faded hover overlay of the brush footprint
  // Undo: each stroke records the (cell, previous value) pairs it changed.
  std::vector<std::pair<long long, int>> currentStroke_;
  std::vector<std::vector<std::pair<long long, int>>> undoStack_;
};
