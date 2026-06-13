#include "MeshRenderer.h"

#include "ScalarVizUtils.h"
#include "mesh_utils.h"

#include <algorithm>
#include <iterator>
#include <vtkActor.h>
#include <vtkBoundingBox.h>
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkCellData.h>
#include <vtkCommand.h>
#include <vtkDataSetMapper.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkUnstructuredGrid.h>

const char* kVVWindowTitle = "VV mesh viewer";

namespace {

std::vector<vtkDataSet*> rawMeshPointers(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes) {
  std::vector<vtkDataSet*> result;
  result.reserve(meshes.size());
  std::transform(meshes.begin(), meshes.end(), std::back_inserter(result), [](const auto& mesh) {
    return mesh.GetPointer();
  });
  return result;
}

} // namespace

MeshRenderer::~MeshRenderer() = default;
MeshRenderer::MeshRenderer() {}

void MeshRenderer::setRenderContext(vtkRenderWindow* externalWindow,
                                    vtkRenderWindowInteractor* externalInteractor) {
  context.window = externalWindow;
  interactor = externalInteractor;
  embeddedMode = (externalWindow != nullptr && externalInteractor != nullptr);
}

void MeshRenderer::setup(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes,
                         const std::vector<std::string>& names,
                         const std::vector<std::array<double, 3>>& colorsHex) {
  (void)names;
  renderer = vtkSmartPointer<vtkRenderer>::New();
  sceneMeshes = meshes;
  facetPanels.clear();

  if (!context.window) {
    context.window = vtkSmartPointer<vtkRenderWindow>::New();
  }
  mappers.clear();
  context.actors.clear();
  for (size_t i = 0; i < meshes.size(); ++i) {
    vtkNew<vtkDataSetMapper> mapper;
    mapper->SetInputData(meshes[i]);
    mapper->ScalarVisibilityOff();
    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(colorsHex[i][0], colorsHex[i][1], colorsHex[i][2]);
    actor->GetProperty()->SetOpacity(1.0);
    if (vtkUnstructuredGrid::SafeDownCast(meshes[i])) {
      actor->GetProperty()->SetRepresentationToSurface();
    }
    renderer->AddActor(actor);
    mappers.push_back(mapper);
    context.actors.push_back(actor);
  }
  context.colorsHex = colorsHex;

  context.window->AddRenderer(renderer);
  if (!embeddedMode) {
    int screenWidth = 1200, screenHeight = 1024;
    context.window->SetSize(screenWidth, screenHeight);
    context.window->SetPosition(100, 100);
  }
  context.window->SetWindowName(kVVWindowTitle);
  context.window->SetDesiredUpdateRate(120.0);

  if (!interactor) {
    interactor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
  }
  if (interactor->GetRenderWindow() != context.window) {
    interactor->SetRenderWindow(context.window);
  }
  interactor->SetRecognizeGestures(false);
  interactor->SetDesiredUpdateRate(120.0);
  interactor->SetStillUpdateRate(45.0);
  auto defaultStyle = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
  interactor->SetInteractorStyle(defaultStyle);

  // Scalar selection is driven by the owning viewer (point and cell fields
  // alike); start with geometry-only shading.
  clearActiveScalar();
}

void MeshRenderer::start() {
  context.window->Render();
  if (!embeddedMode) {
    interactor->Start();
  }
}

void MeshRenderer::setupFacetGrid(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes,
                                  const std::vector<std::string>& names,
                                  const std::vector<std::array<double, 3>>& colorsHex) {
  (void)names;
  if (meshes.empty())
    return;

  // Collect all (mesh_index, scalar_name, association) tuples — one facet per
  // scalar, point and cell fields alike.
  struct MeshScalarPair {
    size_t meshIndex;
    std::string scalarName;
    FieldAssociation association;
  };
  std::vector<MeshScalarPair> pairs;
  for (size_t j = 0; j < meshes.size(); ++j) {
    if (auto* pd = meshes[j]->GetPointData()) {
      for (int i = 0; i < pd->GetNumberOfArrays(); ++i) {
        if (auto* a = pd->GetArray(i); a && a->GetName()) {
          pairs.push_back({j, a->GetName(), FieldAssociation::Point});
        }
      }
    }
    if (auto* cd = meshes[j]->GetCellData()) {
      for (int i = 0; i < cd->GetNumberOfArrays(); ++i) {
        if (auto* a = cd->GetArray(i); a && a->GetName()) {
          pairs.push_back({j, a->GetName(), FieldAssociation::Cell});
        }
      }
    }
  }
  if (pairs.empty())
    return;

  if (!context.window) {
    context.window = vtkSmartPointer<vtkRenderWindow>::New();
  }
  context.window->SetWindowName((std::string(kVVWindowTitle) + " - Exploded (facet) view").c_str());
  context.window->SetDesiredUpdateRate(120.0);
  if (!embeddedMode) {
    const int* sw = context.window->GetScreenSize();
    const int screenW = (sw ? sw[0] : 1400);
    const int screenH = (sw ? sw[1] : 1200);
    context.window->SetSize(std::min(screenW, 1400), std::min(screenH, 1200));
    context.window->SetPosition(80, 60);
  }

  std::vector<vtkRenderer*> renderersToRemove;
  auto* existingRenderers = context.window->GetRenderers();
  vtkCollectionSimpleIterator removeCookie;
  existingRenderers->InitTraversal(removeCookie);
  for (vtkRenderer* existing = existingRenderers->GetNextRenderer(removeCookie); existing;
       existing = existingRenderers->GetNextRenderer(removeCookie)) {
    renderersToRemove.push_back(existing);
  }
  for (vtkRenderer* existing : renderersToRemove) {
    context.window->RemoveRenderer(existing);
  }

  const size_t n = pairs.size();
  const int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
  const int rows = static_cast<int>(std::ceil(static_cast<double>(n) / cols));

  mappers.clear();
  context.actors.clear();
  facetPanels.clear();
  context.colorsHex = colorsHex;

  for (size_t i = 0; i < n; ++i) {
    const int r = static_cast<int>(i) / cols, c = static_cast<int>(i) % cols;
    const double xmin = double(c) / cols, xmax = double(c + 1) / cols;
    const double ymin = 1.0 - double(r + 1) / rows, ymax = 1.0 - double(r) / rows;

    auto ren = vtkSmartPointer<vtkRenderer>::New();
    ren->SetViewport(xmin, ymin, xmax, ymax);
    context.window->AddRenderer(ren);

    const auto& pair = pairs[i];
    auto& srcMesh = meshes[pair.meshIndex];

    vtkNew<vtkDataSetMapper> mapper;
    mapper->SetInputData(srcMesh);
    mapper->SelectColorArray(pair.scalarName.c_str());
    if (pair.association == FieldAssociation::Cell) {
      mapper->SetScalarModeToUseCellFieldData();
    } else {
      mapper->SetScalarModeToUsePointFieldData();
    }
    mapper->SetColorModeToMapScalars();

    auto* arr = arrayForAssociation(srcMesh, pair.scalarName, pair.association);
    if (arr) {
      double range[2];
      arr->GetRange(range);
      std::vector<vtkDataSet*> allPtrs = rawMeshPointers(meshes);
      auto analysis = analyzeScalar(allPtrs, pair.scalarName, pair.association);
      if (analysis.categorical && sharedCatAnalysis.categorical)
        analysis = sharedCatAnalysis;
      auto lut = buildLookupTable(analysis, range);
      mapper->SetLookupTable(lut);
      mapper->SetScalarRange(range);
      mapper->ScalarVisibilityOn();

      FacetPanelState panel;
      panel.mapper = mapper;
      panel.title = pair.scalarName;
      panel.analysis = std::move(analysis);
      panel.globalRange[0] = range[0];
      panel.globalRange[1] = range[1];
      panel.clipRange[0] = range[0];
      panel.clipRange[1] = range[1];
      panel.viewport[0] = xmin;
      panel.viewport[1] = ymin;
      panel.viewport[2] = xmax;
      panel.viewport[3] = ymax;
      facetPanels.push_back(std::move(panel));
    } else {
      mapper->ScalarVisibilityOff();

      FacetPanelState panel;
      panel.mapper = mapper;
      panel.title = pair.scalarName;
      panel.viewport[0] = xmin;
      panel.viewport[1] = ymin;
      panel.viewport[2] = xmax;
      panel.viewport[3] = ymax;
      facetPanels.push_back(std::move(panel));
    }

    auto color = (pair.meshIndex < colorsHex.size())
                     ? colorsHex[pair.meshIndex]
                     : generateDistinctColor(static_cast<int>(pair.meshIndex));
    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(color[0], color[1], color[2]);
    ren->AddActor(actor);
    mappers.push_back(mapper);
    context.actors.push_back(actor);
  }

  vtkBoundingBox bbox;
  for (auto& p : meshes) {
    double bb[6];
    p->GetBounds(bb);
    bbox.AddBounds(bb);
  }
  double ub[6];
  bbox.GetBounds(ub);

  auto tmplRen = vtkSmartPointer<vtkRenderer>::New();
  auto tmplCam = vtkSmartPointer<vtkCamera>::New();
  tmplRen->SetActiveCamera(tmplCam);
  tmplRen->ResetCamera(ub);

  auto* rens = context.window->GetRenderers();
  vtkCollectionSimpleIterator cookie;
  rens->InitTraversal(cookie);
  for (vtkRenderer* ren = rens->GetNextRenderer(cookie); ren; ren = rens->GetNextRenderer(cookie)) {
    auto cam = vtkSmartPointer<vtkCamera>::New();
    cam->DeepCopy(tmplCam);
    ren->SetActiveCamera(cam);
    ren->ResetCameraClippingRange(ub);
  }

  if (!camLinkCb_)
    camLinkCb_ = vtkSmartPointer<vtkCallbackCommand>::New();
  camLinkCb_->SetClientData(context.window);
  camLinkCb_->SetCallback([](vtkObject* caller, unsigned long, void* cd, void*) {
    auto* src = vtkCamera::SafeDownCast(caller);
    auto* win = static_cast<vtkRenderWindow*>(cd);
    if (!src || !win)
      return;
    auto* r = win->GetRenderers();
    vtkCollectionSimpleIterator it;
    r->InitTraversal(it);
    for (vtkRenderer* ren = r->GetNextRenderer(it); ren; ren = r->GetNextRenderer(it)) {
      auto* cam = ren->GetActiveCamera();
      if (cam && cam != src)
        cam->DeepCopy(src);
    }
  });

  rens->InitTraversal(cookie);
  for (vtkRenderer* ren = rens->GetNextRenderer(cookie); ren; ren = rens->GetNextRenderer(cookie))
    ren->GetActiveCamera()->AddObserver(vtkCommand::ModifiedEvent, camLinkCb_);

  if (!interactor) {
    interactor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
  }
  if (interactor->GetRenderWindow() != context.window) {
    interactor->SetRenderWindow(context.window);
  }
  interactor->SetRecognizeGestures(false);
  interactor->SetDesiredUpdateRate(120.0);
  interactor->SetStillUpdateRate(45.0);
  auto style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
  style->SetMotionFactor(10.0);
  interactor->SetInteractorStyle(style);
}

void MeshRenderer::startFacetGrid() {
  if (!context.window || !interactor)
    return;
  context.window->Render();
  if (!embeddedMode) {
    interactor->Start();
  }
}

bool MeshRenderer::setActiveScalar(const std::string& scalarName, FieldAssociation association) {
  if (scalarName.empty()) {
    clearActiveScalar();
    return true;
  }

  std::vector<vtkDataSet*> meshPtrs = rawMeshPointers(sceneMeshes);

  double range[2] = {0.0, 1.0};
  if (!computeScalarGlobalRange(meshPtrs, scalarName, association, range)) {
    return false;
  }

  activeScalarName = scalarName;
  activeScalarAssociation = association;
  activeScalarAnalysis = analyzeScalar(meshPtrs, scalarName, association);
  if (activeScalarAnalysis.categorical && sharedCatAnalysis.categorical)
    activeScalarAnalysis = sharedCatAnalysis;
  activeScalarGlobalRange[0] = range[0];
  activeScalarGlobalRange[1] = range[1];
  clipRange[0] = range[0];
  clipRange[1] = range[1];

  bool found = false;
  for (size_t index = 0; index < sceneMeshes.size() && index < mappers.size(); ++index) {
    if (setMapperScalar(sceneMeshes[index],
                        mappers[index],
                        activeScalarName,
                        activeScalarAssociation,
                        clipRange,
                        activeScalarAnalysis)) {
      found = true;
    }
  }

  if (!found) {
    clearActiveScalar();
    return false;
  }

  if (context.window) {
    context.window->Render();
  }
  return true;
}

void MeshRenderer::clearActiveScalar() {
  activeScalarName.clear();
  activeScalarAnalysis = {};
  for (size_t index = 0; index < sceneMeshes.size() && index < mappers.size(); ++index) {
    mappers[index]->ScalarVisibilityOff();
    if (!sceneMeshes[index]) {
      continue;
    }
    if (sceneMeshes[index]->GetPointData()) {
      sceneMeshes[index]->GetPointData()->SetActiveScalars(nullptr);
    }
    if (sceneMeshes[index]->GetCellData()) {
      sceneMeshes[index]->GetCellData()->SetActiveScalars(nullptr);
    }
  }
  if (context.window) {
    context.window->Render();
  }
}

void MeshRenderer::refreshAfterDataChange() {
  // Hot path: called once per playback frame. The mapper's lookup table, scalar
  // range and color-array selection were configured when the scalar was first
  // applied and stay fixed across the animation — so we only re-flag the active
  // array on the freshly swapped point data and re-render. Rebuilding the LUT here
  // (as the initial apply does) would re-map and re-upload every frame for nothing.
  for (size_t index = 0; index < sceneMeshes.size(); ++index) {
    vtkDataSet* mesh = sceneMeshes[index];
    if (!mesh) {
      continue;
    }
    if (!activeScalarName.empty() &&
        arrayForAssociation(mesh, activeScalarName, activeScalarAssociation)) {
      if (activeScalarAssociation == FieldAssociation::Cell) {
        mesh->GetCellData()->SetActiveScalars(activeScalarName.c_str());
      } else {
        mesh->GetPointData()->SetActiveScalars(activeScalarName.c_str());
      }
    }
    mesh->Modified();
  }
  if (context.window) {
    context.window->Render();
  }
}

void MeshRenderer::setActiveScalarRange(double minValue, double maxValue) {
  if (activeScalarName.empty() || minValue > maxValue) {
    return;
  }
  activeScalarGlobalRange[0] = minValue;
  activeScalarGlobalRange[1] = maxValue;
  clipRange[0] = minValue;
  clipRange[1] = maxValue;
  for (size_t index = 0; index < sceneMeshes.size() && index < mappers.size(); ++index) {
    setMapperScalar(sceneMeshes[index],
                    mappers[index],
                    activeScalarName,
                    activeScalarAssociation,
                    clipRange,
                    activeScalarAnalysis);
  }
  if (context.window) {
    context.window->Render();
  }
}

bool MeshRenderer::getActiveScalarGlobalRange(double outRange[2]) const {
  if (activeScalarName.empty()) {
    return false;
  }
  outRange[0] = activeScalarGlobalRange[0];
  outRange[1] = activeScalarGlobalRange[1];
  return true;
}

const ScalarAnalysis& MeshRenderer::getActiveScalarAnalysis() const {
  return activeScalarAnalysis;
}

void MeshRenderer::setSharedCatAnalysis(const ScalarAnalysis& shared) {
  sharedCatAnalysis = shared;
}

vtkLookupTable* MeshRenderer::getActiveLUT() const {
  if (mappers.empty())
    return nullptr;
  return vtkLookupTable::SafeDownCast(mappers.front()->GetLookupTable());
}

void MeshRenderer::getClipRange(double outRange[2]) const {
  outRange[0] = clipRange[0];
  outRange[1] = clipRange[1];
}

bool MeshRenderer::setClipRange(double minValue, double maxValue) {
  if (activeScalarName.empty()) {
    return false;
  }

  if (minValue > maxValue) {
    return false;
  }

  minValue = std::max(minValue, activeScalarGlobalRange[0]);
  maxValue = std::min(maxValue, activeScalarGlobalRange[1]);
  if (minValue > maxValue) {
    return false;
  }

  clipRange[0] = minValue;
  clipRange[1] = maxValue;

  bool found = false;
  for (size_t index = 0; index < sceneMeshes.size() && index < mappers.size(); ++index) {
    if (setMapperScalar(sceneMeshes[index],
                        mappers[index],
                        activeScalarName,
                        activeScalarAssociation,
                        clipRange,
                        activeScalarAnalysis)) {
      found = true;
    }
  }

  if (context.window) {
    context.window->Render();
  }
  return found;
}

bool MeshRenderer::setPartVisible(size_t partIndex, bool visible) {
  if (partIndex >= context.actors.size() || !context.actors[partIndex]) {
    return false;
  }
  context.actors[partIndex]->SetVisibility(visible ? 1 : 0);
  if (context.window) {
    context.window->Render();
  }
  return true;
}

size_t MeshRenderer::getFacetPanelCount() const {
  return facetPanels.size();
}

bool MeshRenderer::getFacetPanelInfo(size_t panelIndex, FacetPanelInfo& outInfo) const {
  if (panelIndex >= facetPanels.size()) {
    return false;
  }
  const FacetPanelState& panel = facetPanels[panelIndex];
  outInfo.title = panel.title;
  outInfo.analysis = panel.analysis;
  outInfo.globalRange[0] = panel.globalRange[0];
  outInfo.globalRange[1] = panel.globalRange[1];
  outInfo.clipRange[0] = panel.clipRange[0];
  outInfo.clipRange[1] = panel.clipRange[1];
  outInfo.viewport[0] = panel.viewport[0];
  outInfo.viewport[1] = panel.viewport[1];
  outInfo.viewport[2] = panel.viewport[2];
  outInfo.viewport[3] = panel.viewport[3];
  return true;
}

vtkLookupTable* MeshRenderer::getFacetPanelLUT(size_t panelIndex) const {
  if (panelIndex >= facetPanels.size())
    return nullptr;
  return vtkLookupTable::SafeDownCast(facetPanels[panelIndex].mapper->GetLookupTable());
}

bool MeshRenderer::setFacetPanelClipRange(size_t panelIndex, double minValue, double maxValue) {
  if (panelIndex >= facetPanels.size()) {
    return false;
  }

  FacetPanelState& panel = facetPanels[panelIndex];
  if (!panel.mapper) {
    return false;
  }

  if (minValue > maxValue) {
    return false;
  }

  minValue = std::max(minValue, panel.globalRange[0]);
  maxValue = std::min(maxValue, panel.globalRange[1]);
  if (minValue > maxValue) {
    return false;
  }

  panel.clipRange[0] = minValue;
  panel.clipRange[1] = maxValue;

  vtkLookupTable* lut = vtkLookupTable::SafeDownCast(panel.mapper->GetLookupTable());
  if (lut) {
    applyLookupTableRange(lut, panel.clipRange);
  }
  panel.mapper->SetScalarRange(panel.clipRange);
  if (context.window) {
    context.window->Render();
  }
  return true;
}
