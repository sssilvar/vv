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
};

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
};
