#include "MeshRenderer.h"

#include "ScalarVizUtils.h"
#include "mesh_utils.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vtkActor.h>
#include <vtkBoundingBox.h>
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkCellCenters.h>
#include <vtkCellData.h>
#include <vtkCommand.h>
#include <vtkCoordinate.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkGlyph3D.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkLineSource.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <vtkPointSet.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>
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

// Row-major cell (index) of a cols x rows grid, as a VTK viewport (y grows up).
void gridViewport(size_t index, size_t cols, size_t rows, double out[4]) {
  const size_t row = index / cols;
  const double r = static_cast<double>(row);
  const double c = static_cast<double>(index % cols);
  const double nc = static_cast<double>(cols);
  const double nr = static_cast<double>(rows);
  out[0] = c / nc;
  out[1] = 1.0 - (r + 1.0) / nr;
  out[2] = (c + 1.0) / nc;
  out[3] = 1.0 - r / nr;
}

size_t squareGridCols(size_t count) {
  return static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
}

void addPanelLabel(vtkRenderer* ren, const std::string& text) {
  vtkNew<vtkTextActor> label;
  label->SetInput(text.c_str());
  label->GetPositionCoordinate()->SetCoordinateSystemToNormalizedViewport();
  // Top-center: the top-left corner belongs to the parts-tree overlay.
  label->SetPosition(0.5, 0.97);
  vtkTextProperty* prop = label->GetTextProperty();
  prop->SetFontSize(14);
  prop->SetColor(0.89, 0.89, 0.89);
  prop->SetJustificationToCentered();
  prop->SetVerticalJustificationToTop();
  ren->AddViewProp(label);
}

vtkSmartPointer<vtkActor>
makeGlyphActor(vtkDataSet* mesh, const std::string& name, FieldAssociation association) {
  auto* arr = arrayForAssociation(mesh, name, association);
  auto* pointSet = vtkPointSet::SafeDownCast(mesh);
  if (!arr || arr->GetNumberOfComponents() != 3 || !pointSet) {
    return nullptr;
  }

  auto seed = vtkSmartPointer<vtkPolyData>::New();
  if (association == FieldAssociation::Cell) {
    vtkNew<vtkCellCenters> centers;
    centers->SetInputData(mesh);
    centers->Update();
    seed->ShallowCopy(centers->GetOutput());
  } else {
    seed->SetPoints(pointSet->GetPoints());
    seed->GetPointData()->ShallowCopy(mesh->GetPointData());
  }
  if (!seed->GetPointData()->GetArray(name.c_str())) {
    return nullptr;
  }
  seed->GetPointData()->SetActiveVectors(name.c_str());

  double bounds[6];
  mesh->GetBounds(bounds);
  const double diagonal =
      std::sqrt(std::pow(bounds[1] - bounds[0], 2) + std::pow(bounds[3] - bounds[2], 2) +
                std::pow(bounds[5] - bounds[4], 2));

  // Roughly one mean cell width, so glyphs read as a direction field rather than
  // a solid mat or a dot cloud. Centered on the cell so a fibre direction and its
  // opposite draw the same segment.
  const double cells = std::max(1.0, static_cast<double>(mesh->GetNumberOfCells()));
  const double length = 2.0 * diagonal / std::sqrt(cells);
  vtkNew<vtkLineSource> line;
  line->SetPoint1(-0.5 * length, 0.0, 0.0);
  line->SetPoint2(0.5 * length, 0.0, 0.0);

  vtkNew<vtkGlyph3D> glyph;
  glyph->SetInputData(seed);
  glyph->SetSourceConnection(line->GetOutputPort());
  glyph->OrientOn();
  glyph->SetVectorModeToUseVector();
  glyph->SetScaleModeToDataScalingOff();

  vtkNew<vtkPolyDataMapper> mapper;
  mapper->SetInputConnection(glyph->GetOutputPort());
  mapper->ScalarVisibilityOff();
  // Glyphs are coplanar with the surface they describe; without a depth offset the
  // surface wins the depth test and hides all but a few pixels of each segment.
  vtkPolyDataMapper::SetResolveCoincidentTopologyToPolygonOffset();
  mapper->SetRelativeCoincidentTopologyLineOffsetParameters(0.0, -6.0);
  auto actor = vtkSmartPointer<vtkActor>::New();
  actor->SetMapper(mapper);
  actor->GetProperty()->SetColor(0.05, 0.05, 0.05);
  actor->GetProperty()->SetLineWidth(2.0);
  return actor;
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
                         const std::vector<MeshGroup>& groups,
                         const std::vector<std::array<double, 3>>& colorsHex) {
  sceneMeshes = meshes;
  facetPanels.clear();

  if (!context.window) {
    context.window = vtkSmartPointer<vtkRenderWindow>::New();
  }
  const size_t panelCount = std::max<size_t>(1, groups.size());
  buildPanels(panelCount, squareGridCols(panelCount));
  meshPanel_.assign(meshes.size(), 0);
  for (size_t g = 0; g < groups.size(); ++g) {
    for (size_t meshIndex : groups[g].partIndices) {
      if (meshIndex < meshPanel_.size()) {
        meshPanel_[meshIndex] = g;
      }
    }
    if (groups.size() > 1) {
      addPanelLabel(panelRenderers_[g], groups[g].name);
    }
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
    panelRenderers_[meshPanel_[i]]->AddActor(actor);
    mappers.push_back(mapper);
    context.actors.push_back(actor);
  }
  context.colorsHex = colorsHex;
  finishPanels(meshes);

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

void MeshRenderer::buildPanels(size_t count, size_t cols) {
  std::vector<vtkRenderer*> existing;
  auto* renderers = context.window->GetRenderers();
  vtkCollectionSimpleIterator cookie;
  renderers->InitTraversal(cookie);
  for (vtkRenderer* ren = renderers->GetNextRenderer(cookie); ren;
       ren = renderers->GetNextRenderer(cookie)) {
    existing.push_back(ren);
  }
  for (vtkRenderer* ren : existing) {
    context.window->RemoveRenderer(ren);
  }
  glyphActors_.clear();

  const size_t rows = (count + cols - 1) / cols;
  vtkNew<vtkCamera> camera;
  panelRenderers_.clear();
  for (size_t i = 0; i < count; ++i) {
    auto ren = vtkSmartPointer<vtkRenderer>::New();
    double viewport[4];
    gridViewport(i, cols, rows, viewport);
    ren->SetViewport(viewport);
    ren->SetActiveCamera(camera);
    context.window->AddRenderer(ren);
    panelRenderers_.push_back(ren);
  }
  renderer = panelRenderers_.front();
}

void MeshRenderer::finishPanels(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes) {
  vtkBoundingBox bbox;
  for (const auto& mesh : meshes) {
    double bounds[6];
    mesh->GetBounds(bounds);
    bbox.AddBounds(bounds);
  }
  if (bbox.IsValid()) {
    bbox.GetBounds(sceneBounds_);
  }
  renderer->ResetCamera(sceneBounds_);
  fittedCameraMTime_ = renderer->GetActiveCamera()->GetMTime();

  // The interactor style resets clipping from the panel under the cursor only;
  // re-deriving it from every mesh before each panel draws keeps the shared
  // camera from clipping geometry that only another panel shows.
  if (!clipCb_) {
    clipCb_ = vtkSmartPointer<vtkCallbackCommand>::New();
    clipCb_->SetCallback([](vtkObject* caller, unsigned long, void* bounds, void*) {
      static_cast<vtkRenderer*>(caller)->ResetCameraClippingRange(static_cast<double*>(bounds));
    });
  }
  clipCb_->SetClientData(sceneBounds_);
  for (const auto& ren : panelRenderers_) {
    ren->AddObserver(vtkCommand::StartEvent, clipCb_);
  }
}

void MeshRenderer::start() {
  context.window->Render();
  if (!embeddedMode) {
    interactor->Start();
  }
}

void MeshRenderer::setupFacetGrid(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes,
                                  const std::vector<MeshGroup>& groups,
                                  const std::vector<std::array<double, 3>>& colorsHex) {
  if (meshes.empty() || groups.empty())
    return;

  // Columns: every named point and cell array across all meshes, in first-seen
  // order, so a scalar shared by several files lines up in one column.
  struct Column {
    std::string name;
    FieldAssociation association;
  };
  std::vector<Column> columns;
  auto addColumns = [&columns](vtkFieldData* data, FieldAssociation association) {
    for (int i = 0; data && i < data->GetNumberOfArrays(); ++i) {
      vtkAbstractArray* a = data->GetAbstractArray(i);
      if (!a || !a->GetName() || !vtkDataArray::SafeDownCast(a)) {
        continue;
      }
      const std::string name = a->GetName();
      const bool seen = std::any_of(columns.begin(), columns.end(), [&](const Column& c) {
        return c.name == name && c.association == association;
      });
      if (!seen) {
        columns.push_back({name, association});
      }
    }
  };
  for (const auto& mesh : meshes) {
    addColumns(mesh->GetPointData(), FieldAssociation::Point);
    addColumns(mesh->GetCellData(), FieldAssociation::Cell);
  }
  const bool geometryOnly = columns.empty();
  const size_t columnCount = geometryOnly ? 1 : columns.size();

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

  const bool matrix = groups.size() > 1;
  const size_t panelCount = groups.size() * columnCount;
  buildPanels(panelCount, matrix ? columnCount : squareGridCols(panelCount));

  mappers.clear();
  context.actors.clear();
  facetPanels.clear();
  context.colorsHex = colorsHex;
  const std::vector<vtkDataSet*> allPtrs = rawMeshPointers(meshes);

  for (size_t g = 0; g < groups.size(); ++g) {
    for (size_t col = 0; col < columnCount; ++col) {
      vtkRenderer* ren = panelRenderers_[g * columnCount + col];

      FacetPanelState panel;
      panel.column = col;
      ren->GetViewport(panel.viewport);
      if (!geometryOnly) {
        const Column& column = columns[col];
        panel.title = column.name;
        // One range per column (across every file) so equal values match colors.
        if (computeScalarGlobalRange(allPtrs, column.name, column.association, panel.globalRange)) {
          panel.analysis = analyzeScalar(allPtrs, column.name, column.association);
          if (panel.analysis.categorical && sharedCatAnalysis.categorical)
            panel.analysis = sharedCatAnalysis;
          panel.clipRange[0] = panel.globalRange[0];
          panel.clipRange[1] = panel.globalRange[1];
        }
      }

      for (size_t meshIndex : groups[g].partIndices) {
        if (meshIndex >= meshes.size()) {
          continue;
        }
        vtkNew<vtkDataSetMapper> mapper;
        mapper->SetInputData(meshes[meshIndex]);
        if (!geometryOnly && setMapperScalar(meshes[meshIndex],
                                             mapper,
                                             columns[col].name,
                                             columns[col].association,
                                             panel.clipRange,
                                             panel.analysis)) {
          if (!panel.lut) {
            panel.lut = vtkLookupTable::SafeDownCast(mapper->GetLookupTable());
          }
          mapper->SetLookupTable(panel.lut);
          panel.scalarMappers.emplace_back(mapper);
          panel.hasScalar = true;
        } else {
          mapper->ScalarVisibilityOff();
        }

        const auto color = meshIndex < colorsHex.size()
                               ? colorsHex[meshIndex]
                               : generateDistinctColor(static_cast<int>(meshIndex));
        vtkNew<vtkActor> actor;
        actor->SetMapper(mapper);
        actor->GetProperty()->SetColor(color[0], color[1], color[2]);
        ren->AddActor(actor);
        mappers.emplace_back(mapper);
        context.actors.emplace_back(actor);
      }
      // Scalar panels are titled by their colorbar; a plain one names what it lacks.
      if (matrix) {
        addPanelLabel(ren,
                      panel.hasScalar || panel.title.empty()
                          ? groups[g].name
                          : groups[g].name + " \u00b7 no " + panel.title);
      }
      facetPanels.push_back(std::move(panel));
    }
  }
  finishPanels(meshes);

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

void MeshRenderer::refitCameraIfUntouched() {
  if (!renderer || renderer->GetActiveCamera()->GetMTime() != fittedCameraMTime_) {
    return;
  }
  renderer->ResetCamera(sceneBounds_);
  fittedCameraMTime_ = renderer->GetActiveCamera()->GetMTime();
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

void MeshRenderer::colorByFixedCategorical(const std::string& scalarName,
                                           FieldAssociation association,
                                           vtkLookupTable* lut,
                                           const double range[2]) {
  if (sceneMeshes.empty() || mappers.empty() || !sceneMeshes.front() || !lut) {
    return;
  }
  activeScalarName = scalarName;
  activeScalarAssociation = association;
  vtkDataSet* mesh = sceneMeshes.front();
  vtkDataSetMapper* mapper = mappers.front();
  if (association == FieldAssociation::Cell) {
    mesh->GetCellData()->SetActiveScalars(scalarName.c_str());
    mapper->SetScalarModeToUseCellFieldData();
  } else {
    mesh->GetPointData()->SetActiveScalars(scalarName.c_str());
    mapper->SetScalarModeToUsePointFieldData();
  }
  mapper->SelectColorArray(scalarName.c_str());
  mapper->SetColorModeToMapScalars();
  mapper->ScalarVisibilityOn();
  mapper->SetLookupTable(lut);
  mapper->SetScalarRange(range[0], range[1]);
  if (context.window) {
    context.window->Render();
  }
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

bool MeshRenderer::toggleCyclicColormap() {
  if (activeScalarName.empty() || activeScalarAnalysis.categorical) {
    return false;
  }
  activeScalarAnalysis.cyclic = !activeScalarAnalysis.cyclic;
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
  return activeScalarAnalysis.cyclic;
}

bool MeshRenderer::setVectorGlyphs(const std::string& name, FieldAssociation association) {
  for (const auto& [ren, actor] : glyphActors_) {
    ren->RemoveActor(actor);
  }
  glyphActors_.clear();
  if (!name.empty()) {
    for (size_t i = 0; i < sceneMeshes.size() && i < meshPanel_.size(); ++i) {
      if (auto actor = makeGlyphActor(sceneMeshes[i], name, association)) {
        vtkRenderer* ren = panelRenderers_[meshPanel_[i]];
        ren->AddActor(actor);
        glyphActors_.emplace_back(ren, actor);
      }
    }
  }
  if (context.window) {
    context.window->Render();
  }
  return !glyphActors_.empty();
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
  outInfo.hasScalar = panel.hasScalar;
  outInfo.column = panel.column;
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
  return facetPanels[panelIndex].lut;
}

bool MeshRenderer::setFacetPanelGlobalRange(size_t panelIndex, double minValue, double maxValue) {
  if (panelIndex >= facetPanels.size() || minValue >= maxValue) {
    return false;
  }
  FacetPanelState& panel = facetPanels[panelIndex];
  panel.globalRange[0] = minValue;
  panel.globalRange[1] = maxValue;
  panel.clipRange[0] = minValue;
  panel.clipRange[1] = maxValue;
  return setFacetPanelClipRange(panelIndex, minValue, maxValue);
}

bool MeshRenderer::setFacetPanelClipRange(size_t panelIndex, double minValue, double maxValue) {
  if (panelIndex >= facetPanels.size()) {
    return false;
  }

  FacetPanelState& panel = facetPanels[panelIndex];
  if (!panel.hasScalar) {
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

  if (panel.lut) {
    applyLookupTableRange(panel.lut, panel.clipRange, panel.analysis.cyclic);
  }
  for (const auto& mapper : panel.scalarMappers) {
    mapper->SetScalarRange(panel.clipRange);
  }
  if (context.window) {
    context.window->Render();
  }
  return true;
}
