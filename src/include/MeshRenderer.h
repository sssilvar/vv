#pragma once
#include "ScalarVizUtils.h"

#include <array>
#include <string>
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
  std::string title;
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
  void setup(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes,
             const std::vector<std::string>& names,
             const std::vector<std::array<double, 3>>& colorsHex);
  void start();

  // When set, all categorical scalars use this shared LUT instead of per-scalar detection.
  // Pass an empty ScalarAnalysis to clear.
  void setSharedCatAnalysis(const ScalarAnalysis& shared);

  bool setActiveScalar(const std::string& scalarName, FieldAssociation association);
  void clearActiveScalar();
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

  void setupFacetGrid(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes,
                      const std::vector<std::string>& names,
                      const std::vector<std::array<double, 3>>& colorsHex);
  void startFacetGrid();

  RendererContext context;

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
  struct FacetPanelState {
    vtkSmartPointer<vtkDataSetMapper> mapper;
    std::string title;
    ScalarAnalysis analysis;
    double globalRange[2] = {0.0, 1.0};
    double clipRange[2] = {0.0, 1.0};
    double viewport[4] = {0.0, 0.0, 1.0, 1.0};
  };
  std::vector<FacetPanelState> facetPanels;
  // Keeps the facet-grid cameras synchronized (observer shared by all panels).
  vtkSmartPointer<vtkCallbackCommand> camLinkCb_;
  bool embeddedMode = false;
};
