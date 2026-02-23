#pragma once
#include <array>
#include <string>
#include <vector>
#include <vtkActor.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>

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

  const std::vector<std::string>& getScalarNames() const;
  bool setActiveScalar(const std::string& scalarName);
  void clearActiveScalar();
  bool getActiveScalarGlobalRange(double outRange[2]) const;
  void getClipRange(double outRange[2]) const;
  bool setClipRange(double minValue, double maxValue);
  size_t getPartCount() const;
  bool setPartVisible(size_t partIndex, bool visible);
  bool isPartVisible(size_t partIndex) const;
  size_t getFacetPanelCount() const;
  bool getFacetPanelInfo(size_t panelIndex, FacetPanelInfo& outInfo) const;
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
  std::vector<std::string> availableScalars;
  std::string activeScalarName;
  double activeScalarGlobalRange[2] = {0.0, 1.0};
  double clipRange[2] = {0.0, 1.0};
  struct FacetPanelState {
    vtkSmartPointer<vtkDataSetMapper> mapper;
    std::string title;
    double globalRange[2] = {0.0, 1.0};
    double clipRange[2] = {0.0, 1.0};
    double viewport[4] = {0.0, 0.0, 1.0, 1.0};
  };
  std::vector<FacetPanelState> facetPanels;
  bool embeddedMode = false;
};
