#pragma once
#include "MeshLoading.h"
#include "ScalarVizUtils.h"

#include <array>
#include <string>
#include <utility>
#include <vector>
#include <vtkActor.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkLookupTable.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>

class vtkCallbackCommand;

struct RendererContext {
  vtkSmartPointer<vtkRenderWindow> window;
  std::vector<vtkSmartPointer<vtkActor>> actors;
  std::vector<std::array<double, 3>> colorsHex;
};

struct FacetPanelInfo {
  std::string title;      // scalar name; empty for a geometry-only column
  bool hasScalar = false; // false: the panel's file lacks this column's scalar
  size_t column = 0;      // panels in one column share a scalar and its color range
  double globalRange[2] = {0.0, 1.0};
  double clipRange[2] = {0.0, 1.0};
  double viewport[4] = {0.0, 0.0, 1.0, 1.0};
  ScalarAnalysis analysis;
};

class MeshRenderer {
public:
  MeshRenderer();
  ~MeshRenderer();
  void setRenderContext(vtkRenderWindow* externalWindow,
                        vtkRenderWindowInteractor* externalInteractor);
  // One viewport per group (file), all sharing a single camera; a lone group
  // fills the window.
  void setup(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes,
             const std::vector<MeshGroup>& groups,
             const std::vector<std::array<double, 3>>& colorsHex);
  void start();

  // When set, all categorical scalars use this shared LUT instead of per-scalar detection.
  // Pass an empty ScalarAnalysis to clear.
  void setSharedCatAnalysis(const ScalarAnalysis& shared);

  bool setActiveScalar(const std::string& scalarName, FieldAssociation association);
  // Color the primary mesh by a categorical array using an externally-owned LUT,
  // bypassing per-scalar analysis so the value→color mapping stays fixed while
  // the array is edited (annotation mode).
  void colorByFixedCategorical(const std::string& scalarName,
                               FieldAssociation association,
                               vtkLookupTable* lut,
                               const double range[2]);
  void clearActiveScalar();
  // Swap the active continuous field between the linear and the cyclic (full hue
  // wheel) colormap. Returns the state after the toggle.
  bool toggleCyclicColormap();
  // Re-apply the current scalar mapping after the underlying mesh data changed
  // (e.g. a new playback frame was shallow-copied in), keeping the color range
  // fixed, then re-render.
  void refreshAfterDataChange();
  // Override the active scalar's color range (used to fix a stable range across a
  // whole time series instead of the current frame's range).
  void setActiveScalarRange(double minValue, double maxValue);
  bool getActiveScalarGlobalRange(double outRange[2]) const;
  const ScalarAnalysis& getActiveScalarAnalysis() const;
  vtkLookupTable* getActiveLUT() const;
  void getClipRange(double outRange[2]) const;
  bool setClipRange(double minValue, double maxValue);
  bool setPartVisible(size_t partIndex, bool visible);
  size_t getFacetPanelCount() const;
  bool getFacetPanelInfo(size_t panelIndex, FacetPanelInfo& outInfo) const;
  vtkLookupTable* getFacetPanelLUT(size_t panelIndex) const;
  bool setFacetPanelClipRange(size_t panelIndex, double minValue, double maxValue);
  // Widen or narrow a panel's full range (what the colorbar ends show), not just
  // the clip window inside it — used to pin one range across every panel.
  bool setFacetPanelGlobalRange(size_t panelIndex, double minValue, double maxValue);

  // Draw a short line glyph per cell (or per point) tangent to the surface, one
  // for each tuple of a 3-component array, on every mesh that has it. Empty name
  // removes the glyphs.
  bool setVectorGlyphs(const std::string& name, FieldAssociation association);

  // One panel per (group, scalar): a square grid of scalars for a single group,
  // otherwise a matrix with a row per group and a column per scalar.
  void setupFacetGrid(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes,
                      const std::vector<MeshGroup>& groups,
                      const std::vector<std::array<double, 3>>& colorsHex);
  void startFacetGrid();
  // Re-frame the scene for the current viewport shape unless the camera moved
  // since vv last framed it; the first fit can run before the widget is laid out.
  void refitCameraIfUntouched();

  RendererContext context;

  // Annotation mode needs the renderer (for picking) and the primary dataset; it
  // is only enabled for a single file, so the first panel holds that dataset.
  vtkRenderer* getRenderer() const {
    return renderer;
  }
  vtkDataSet* getPrimaryMesh() const {
    return sceneMeshes.empty() ? nullptr : sceneMeshes.front();
  }

private:
  vtkSmartPointer<vtkRenderer> renderer;
  vtkSmartPointer<vtkRenderWindowInteractor> interactor;
  std::vector<vtkSmartPointer<vtkDataSet>> sceneMeshes;
  std::vector<vtkSmartPointer<vtkDataSetMapper>> mappers;
  std::string activeScalarName;
  FieldAssociation activeScalarAssociation = FieldAssociation::Point;
  ScalarAnalysis activeScalarAnalysis;
  ScalarAnalysis sharedCatAnalysis; // non-empty = override per-scalar detection
  double activeScalarGlobalRange[2] = {0.0, 1.0};
  double clipRange[2] = {0.0, 1.0};
  // Viewports sharing one camera; clipping follows the bounds of every mesh so
  // a panel never clips geometry another panel frames.
  void buildPanels(size_t count, size_t cols);
  void finishPanels(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes);

  std::vector<vtkSmartPointer<vtkRenderer>> panelRenderers_;
  std::vector<size_t> meshPanel_; // mesh index -> panel (normal mode)
  double sceneBounds_[6] = {0.0, 1.0, 0.0, 1.0, 0.0, 1.0};
  vtkSmartPointer<vtkCallbackCommand> clipCb_;
  vtkMTimeType fittedCameraMTime_ = 0;
  struct FacetPanelState {
    std::vector<vtkSmartPointer<vtkDataSetMapper>> scalarMappers; // share lut
    vtkSmartPointer<vtkLookupTable> lut;
    std::string title;
    bool hasScalar = false;
    size_t column = 0;
    ScalarAnalysis analysis;
    double globalRange[2] = {0.0, 1.0};
    double clipRange[2] = {0.0, 1.0};
    double viewport[4] = {0.0, 0.0, 1.0, 1.0};
  };
  std::vector<FacetPanelState> facetPanels;
  std::vector<std::pair<vtkRenderer*, vtkSmartPointer<vtkActor>>> glyphActors_;
  bool embeddedMode = false;
};
