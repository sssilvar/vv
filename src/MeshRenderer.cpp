#include "MeshRenderer.h"

#include "FacetPages.h"
#include "ScalarVizUtils.h"
#include "mesh_utils.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vtkActor.h>
#include <vtkBoundingBox.h>
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCellCenters.h>
#include <vtkCellData.h>
#include <vtkCellPicker.h>
#include <vtkCommand.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkFeatureEdges.h>
#include <vtkGenericCell.h>
#include <vtkGlyph3D.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkLineSource.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPointSet.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataNormals.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkScalarsToColors.h>
#include <vtkShaderProperty.h>
#include <vtkStaticCellLocator.h>
#include <vtkVersionMacros.h>

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

// Glass shading, appended to VTK's lighting in the fragment shader. A fresnel rim, 0
// where the surface faces the viewer and 1 along its silhouette, blends a broad glow
// with a sharp edge and turns the color white and the alpha opaque; elsewhere the
// color is the field's or the part's, frosted, at the property's opacity (the glass
// density). The highlight comes from a light above and left of the camera. VTK has
// already turned back-face normals toward the viewer.
//
// VTK declares no view-space normal or position for primitives it does not light
// (lines and vertices without normals) and only there leaves the PositionVC tag in
// the fragment shader, so the define keeps VTK's shading for them.
constexpr const char* kGlassUnlitTag = "//VTK::PositionVC::Impl";
constexpr const char* kGlassUnlit = "#define vvGlassUnlit\n";
constexpr const char* kGlassShading = R"(//VTK::Light::Impl
#ifndef vvGlassUnlit
  {
    vec3 vvView = cameraParallel == 1 ? vec3(0.0, 0.0, 1.0) : normalize(-vertexVC.xyz);
    float vvGrazing = 1.0 - min(abs(dot(normalVCVSOutput, vvView)), 1.0);
    float vvRim = min(0.5 * pow(vvGrazing, 1.5) + pow(vvGrazing, 6.0), 1.0);
    vec3 vvHalf = normalize(vvView + normalize(vec3(-0.5, 0.7, 1.0)));
    float vvShine = 0.7 * pow(max(dot(normalVCVSOutput, vvHalf), 0.0), 120.0);
    vec3 vvColor = mix(mix(diffuseColor, vec3(1.0), 0.3), vec3(1.0), vvRim) + vvShine;
    float vvAlpha = mix(opacity, 1.0, vvRim) + vvShine;
    gl_FragData[0] = vec4(min(vvColor, vec3(1.0)), min(vvAlpha, 1.0));
  }
#endif
)";
// Width of the open borders' outline in logical pixels.
constexpr double kGlassOutlineWidth = 1.5;
// Across, in logical pixels.
constexpr double kPinMarkerSize = 11.0;

// Trackball camera without vtkInteractorStyle's plain keys (w wireframe, s surface,
// 3 stereo, p pick, f fly to, e exit...), which would change the view behind vv's
// own controls.
class CameraStyle : public vtkInteractorStyleTrackballCamera {
public:
  static CameraStyle* New();
  void OnChar() override {}
  // No semicolon: the macro ends in an access specifier.
  vtkTypeMacro(CameraStyle, vtkInteractorStyleTrackballCamera)
};
vtkStandardNewMacro(CameraStyle);

} // namespace

MeshRenderer::~MeshRenderer() {
  for (const auto& panel : panelRenderers_) {
    panel->RemoveObserver(clipCb_);
  }
}
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

  if (!context.window) {
    context.window = vtkSmartPointer<vtkRenderWindow>::New();
  }
  if (!embeddedMode) {
    int screenWidth = 1200, screenHeight = 1024;
    context.window->SetSize(screenWidth, screenHeight);
    context.window->SetPosition(100, 100);
  }
  const size_t panelCount = std::max<size_t>(1, groups.size());
  facetColumns_ = 0;
  pageSize_ = 0;
  page_ = 0;
  buildPanels(panelCount, 0);
  meshPanel_.assign(meshes.size(), 0);
  for (size_t g = 0; g < groups.size(); ++g) {
    for (size_t meshIndex : groups[g].partIndices) {
      if (meshIndex < meshPanel_.size()) {
        meshPanel_[meshIndex] = g;
      }
    }
  }

  mappers.clear();
  context.actors.clear();
  actorSlots_.clear();
  for (size_t i = 0; i < meshes.size(); ++i) {
    vtkNew<vtkDataSetMapper> mapper;
    mapper->SetInputData(meshes[i]);
    mapper->ScalarVisibilityOff();
    vtkNew<vtkActor> actor;
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(colorsHex[i][0], colorsHex[i][1], colorsHex[i][2]);
    panelRenderers_[meshPanel_[i]]->AddActor(actor);
    mappers.push_back(mapper);
    context.actors.push_back(actor);
    actorSlots_.push_back({i, meshPanel_[i]});
  }
  context.colorsHex = colorsHex;
  finishPanels(meshes);

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
  interactor->SetInteractorStyle(vtkSmartPointer<CameraStyle>::New());
  buildGlassStages(meshes);
  applySurfaceLook();

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
  blanks_.clear();
  pinMarkers_.clear();
  markerHit_.reset();
  probeLocators_.clear();

  adaptiveGrid_ = cols == 0;
  facetPanels.clear();
  vtkNew<vtkCamera> camera;
  for (const auto& panel : panelRenderers_) {
    panel->RemoveObserver(clipCb_);
  }
  panelRenderers_.clear();
  for (size_t i = 0; i < count; ++i) {
    auto ren = vtkSmartPointer<vtkRenderer>::New();
    ren->SetActiveCamera(camera);
    context.window->AddRenderer(ren);
    panelRenderers_.push_back(ren);
  }
  renderer = panelRenderers_.front();
  layoutPanels();
}

void MeshRenderer::layoutPanels() {
  const size_t count = panelRenderers_.size();
  if (count == 0) {
    return;
  }
  const int* size = context.window->GetSize();
  const int width = std::max(1, size[0]);
  const int height = std::max(1, size[1]);
  const int inset = getFacetPageCount() > 1 ? std::clamp(bottomInset_, 0, height - 1) : 0;
  // A row holds a file's panels, one per facet column; a page shows the same columns
  // of every row, each panel in its cell of the page's grid.
  const size_t perRow = facetColumns_ > 0 ? facetColumns_ : count;
  const size_t pageColumns = pageSize_ > 0 ? std::min(pageSize_, perRow) : perRow;
  const size_t cells = count / perRow * pageColumns;
  const size_t cols = adaptiveGrid_ ? adaptiveGridCols(cells, width, height - inset) : pageColumns;
  const size_t rows = 1 + (cells - 1) / cols;
  const double bottom = static_cast<double>(inset) / height;
  const auto cellViewport = [&](size_t cell, double viewport[4]) {
    gridViewport(cell, cols, rows, viewport);
    viewport[1] = bottom + viewport[1] * (1.0 - bottom);
    viewport[3] = bottom + viewport[3] * (1.0 - bottom);
  };

  std::vector<bool> filled(cols * rows, false);
  for (size_t i = 0; i < count; ++i) {
    const size_t column = i % perRow;
    const size_t cell = i / perRow * pageColumns + column % pageColumns;
    const bool shown = column / pageColumns == page_;
    double viewport[4];
    cellViewport(cell, viewport);
    vtkRenderer* panel = panelRenderers_[i];
    panel->SetViewport(viewport);
    panel->SetDraw(shown ? 1 : 0);
    panel->SetInteractive(shown ? 1 : 0);
    filled[cell] = filled[cell] || shown;
    if (i < facetPanels.size()) {
      std::copy_n(viewport, 4, facetPanels[i].viewport);
    }
  }

  // Each empty cell of the facet grid is cleared by a renderer that draws nothing.
  size_t blank = 0;
  for (size_t cell = 0; facetColumns_ > 0 && cell < filled.size(); ++cell) {
    if (filled[cell]) {
      continue;
    }
    if (blank == blanks_.size()) {
      auto ren = vtkSmartPointer<vtkRenderer>::New();
      vtkNew<vtkCamera> camera; // vtkRenderer::Render reads it before drawing anything
      ren->SetActiveCamera(camera);
      ren->InteractiveOff();
      context.window->AddRenderer(ren);
      blanks_.push_back(ren);
    }
    double viewport[4];
    cellViewport(cell, viewport);
    blanks_[blank++]->SetViewport(viewport);
  }
  for (size_t extra = blank; extra < blanks_.size(); ++extra) {
    context.window->RemoveRenderer(blanks_[extra]);
  }
  blanks_.resize(blank);
}

void MeshRenderer::setFacetPages(size_t pageSize, size_t page, int bottomInset) {
  if (facetColumns_ == 0) {
    return;
  }
  pageSize_ = pageSize >= facetColumns_ ? 0 : pageSize;
  page_ = std::min(page, getFacetPageCount() - 1);
  bottomInset_ = std::max(0, bottomInset);
  layoutPanels();
  updatePinMarkers();
}

void MeshRenderer::showFacetPage(size_t page) {
  setFacetPages(pageSize_, page, bottomInset_);
  if (context.window) {
    context.window->Render();
  }
}

size_t MeshRenderer::getFacetPageSize() const {
  return pageSize_ > 0 ? pageSize_ : facetColumns_;
}

size_t MeshRenderer::getFacetPageCount() const {
  return pageSize_ > 0 ? (facetColumns_ + pageSize_ - 1) / pageSize_ : 1;
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
    clipCb_->SetCallback([](vtkObject* caller, unsigned long, void* client, void*) {
      auto* self = static_cast<MeshRenderer*>(client);
      auto* panel = static_cast<vtkRenderer*>(caller);
      panel->ResetCameraClippingRange(self->sceneBounds_);
      auto* style = vtkInteractorStyle::SafeDownCast(self->interactor->GetInteractorStyle());
      // Bound Glass's repeated translucent passes while dragging. StopState clears
      // the interaction state before rendering, so the release frame is full quality.
      const bool movingGlass = self->surfaceLook_.style == SurfaceStyle::Glass && style &&
                               style->GetState() != VTKIS_NONE;
      panel->SetMaximumNumberOfPeels(movingGlass ? 2 : 4);
    });
  }
  clipCb_->SetClientData(this);
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
  sceneMeshes = meshes;

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
  facetColumns_ = columnCount;
  pageSize_ = 0;
  page_ = 0;
  buildPanels(panelCount, matrix ? columnCount : 0);

  mappers.clear();
  context.actors.clear();
  actorSlots_.clear();
  context.colorsHex = colorsHex;
  const std::vector<vtkDataSet*> allPtrs = rawMeshPointers(meshes);

  std::vector<FacetPanelState> columnStates(columnCount);
  for (size_t col = 0; !geometryOnly && col < columnCount; ++col) {
    const Column& field = columns[col];
    auto& state = columnStates[col];
    state.title = field.name;
    state.association = field.association;
    state.column = col;
    if (computeScalarGlobalRange(allPtrs, field.name, field.association, state.globalRange)) {
      state.analysis = analyzeScalar(allPtrs, field.name, field.association);
      if (state.analysis.categorical && sharedCatAnalysis.categorical) {
        state.analysis = sharedCatAnalysis;
      }
      std::copy_n(state.globalRange, 2, state.clipRange);
    }
  }
  for (size_t g = 0; g < groups.size(); ++g) {
    for (size_t col = 0; col < columnCount; ++col) {
      const size_t panelIndex = g * columnCount + col;
      vtkRenderer* ren = panelRenderers_[panelIndex];
      FacetPanelState panel = columnStates[col];
      ren->GetViewport(panel.viewport);

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
        actorSlots_.push_back({meshIndex, panelIndex});
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
  auto style = vtkSmartPointer<CameraStyle>::New();
  style->SetMotionFactor(10.0);
  interactor->SetInteractorStyle(style);
  buildGlassStages(meshes);
  applySurfaceLook();
}

void MeshRenderer::refitCameraIfUntouched() {
  layoutPanels();
  if (!renderer || renderer->GetActiveCamera()->GetMTime() != fittedCameraMTime_) {
    return;
  }
  renderer->ResetCamera(sceneBounds_);
  fittedCameraMTime_ = renderer->GetActiveCamera()->GetMTime();
}

void MeshRenderer::resetCamera() {
  if (!renderer) {
    return;
  }
  // VTK's default camera, which vv opens with before framing the scene.
  vtkCamera* camera = renderer->GetActiveCamera();
  camera->SetPosition(0.0, 0.0, 1.0);
  camera->SetFocalPoint(0.0, 0.0, 0.0);
  camera->SetViewUp(0.0, 1.0, 0.0);
  renderer->ResetCamera(sceneBounds_);
  fittedCameraMTime_ = camera->GetMTime();
  if (context.window) {
    context.window->Render();
  }
}

std::optional<ProbeHit> MeshRenderer::probe(double x, double y) {
  const auto ren =
      std::find_if(panelRenderers_.begin(), panelRenderers_.end(), [x, y](const auto& panel) {
        return panel->GetDraw() && panel->IsInViewport(static_cast<int>(x), static_cast<int>(y));
      });
  if (ren == panelRenderers_.end()) {
    return std::nullopt;
  }
  const auto panel = static_cast<size_t>(std::distance(panelRenderers_.begin(), ren));
  if (!probePicker_) {
    probePicker_ = vtkSmartPointer<vtkCellPicker>::New();
    probePicker_->SetTolerance(0.0005);
    probePicker_->PickFromListOn();
  }
  // This panel's meshes only: the camera is shared, so another panel's mesh lies on
  // the same ray, and glyphs drawn over a mesh are not its data.
  probePicker_->InitializePickList();
  for (size_t i = 0; i < actorSlots_.size() && i < context.actors.size(); ++i) {
    if (actorSlots_[i].panel == panel) {
      probePicker_->AddPickList(context.actors[i]);
    }
  }
  if (probePicker_->Pick(x, y, 0.0, *ren) == 0 || probePicker_->GetCellId() < 0) {
    return std::nullopt;
  }
  vtkActor* actor = probePicker_->GetActor();
  const auto picked =
      std::find_if(context.actors.begin(), context.actors.end(), [actor](const auto& candidate) {
        return candidate.Get() == actor;
      });
  if (!actor || picked == context.actors.end()) {
    return std::nullopt;
  }

  const auto actorIndex = static_cast<size_t>(std::distance(context.actors.begin(), picked));
  ProbeHit hit;
  hit.panel = panel;
  hit.mesh = actorSlots_[actorIndex].mesh;
  hit.cell = probePicker_->GetCellId();
  probePicker_->GetPickPosition(hit.position.data());
  const std::optional<CellPoint> at = cellPointAtPick(
      probePicker_->GetDataSet(), hit.cell, probePicker_->GetSubId(), probePicker_->GetPCoords());
  if (at) {
    hit.at = *at;
    readProbedField(hit, actorIndex, probePicker_->GetDataSet());
  }
  return hit;
}

std::optional<ProbeHit> MeshRenderer::probeAgain(const ProbeHit& hit) const {
  const auto slot = std::find_if(actorSlots_.begin(), actorSlots_.end(), [&hit](const auto& s) {
    return s.mesh == hit.mesh && s.panel == hit.panel;
  });
  if (slot == actorSlots_.end() || hit.mesh >= sceneMeshes.size() ||
      hit.panel >= panelRenderers_.size()) {
    return std::nullopt;
  }
  const size_t actor = static_cast<size_t>(std::distance(actorSlots_.begin(), slot));
  if (actor >= context.actors.size() || !context.actors[actor]->GetVisibility()) {
    return std::nullopt;
  }
  ProbeHit again;
  again.panel = hit.panel;
  again.mesh = hit.mesh;
  again.cell = hit.cell;
  again.at = hit.at;
  again.position = hit.position;
  readProbedField(again, actor, sceneMeshes[hit.mesh]);
  return again;
}

std::vector<ProbeHit> MeshRenderer::matchingProbes(const ProbeHit& hit) {
  std::vector<ProbeHit> matches;
  if (!probeAgain(hit)) {
    return matches;
  }
  // A file can appear in many field columns. Locate its cell only once per update.
  std::map<size_t, std::optional<ProbeHit>> locations;
  locations.emplace(hit.mesh, hit);
  probeLocators_.resize(sceneMeshes.size());
  std::vector<bool> matchedPanels(panelRenderers_.size(), false);
  std::vector<bool> sourcePanels(panelRenderers_.size(), false);
  for (size_t actor = 0; actor < actorSlots_.size(); ++actor) {
    if (actorSlots_[actor].mesh == hit.mesh && context.actors[actor]->GetVisibility()) {
      sourcePanels[actorSlots_[actor].panel] = true;
    }
  }
  for (size_t actor = 0; actor < actorSlots_.size(); ++actor) {
    const auto& slot = actorSlots_[actor];
    if (!context.actors[actor]->GetVisibility() || !panelRenderers_[slot.panel]->GetDraw() ||
        matchedPanels[slot.panel] || (sourcePanels[slot.panel] && slot.mesh != hit.mesh)) {
      continue;
    }
    const auto location = locations.try_emplace(slot.mesh, std::nullopt);
    if (location.second) {
      vtkDataSet* mesh = sceneMeshes[slot.mesh];
      if (mesh->GetNumberOfCells() == 0) {
        continue;
      }
      auto& locator = probeLocators_[slot.mesh];
      if (!locator) {
        locator = vtkSmartPointer<vtkStaticCellLocator>::New();
        locator->SetDataSet(mesh);
      }
      locator->BuildLocator();
      vtkNew<vtkGenericCell> cell;
      double closest[3], distance2;
      vtkIdType id = -1;
      int subId = 0;
      locator->FindClosestPoint(hit.position.data(), closest, cell, id, subId, distance2);
      // Allow roundoff at a picked surface, not a snap to a nearby unrelated mesh.
      const double tolerance = mesh->GetLength() * 1e-7;
      if (id < 0 || distance2 > tolerance * tolerance) {
        continue;
      }
      double pcoords[3], evaluatedDistance2;
      std::vector<double> weights(static_cast<size_t>(cell->GetNumberOfPoints()));
      if (cell->EvaluatePosition(
              closest, nullptr, subId, pcoords, evaluatedDistance2, weights.data()) < 0) {
        continue;
      }
      if (const auto at = cellPointAtPick(mesh, id, subId, pcoords)) {
        ProbeHit mapped;
        mapped.mesh = slot.mesh;
        mapped.cell = id;
        mapped.at = *at;
        std::copy(closest, closest + 3, mapped.position.begin());
        location.first->second = std::move(mapped);
      }
    }
    if (location.first->second) {
      ProbeHit match = *location.first->second;
      match.panel = slot.panel;
      match.field.clear();
      match.category.reset();
      readProbedField(match, actor, sceneMeshes[slot.mesh]);
      matches.push_back(std::move(match));
      matchedPanels[slot.panel] = true;
    }
  }
  return matches;
}

void MeshRenderer::readProbedField(ProbeHit& hit, size_t actorIndex, vtkDataSet* data) const {
  const size_t panel = actorSlots_[actorIndex].panel;
  const FacetPanelState* facet = panel < facetPanels.size() ? &facetPanels[panel] : nullptr;
  const std::string& field = facet ? facet->title : activeScalarName;
  const FieldAssociation association = facet ? facet->association : activeScalarAssociation;
  const ScalarAnalysis& analysis = facet ? facet->analysis : activeScalarAnalysis;
  if (field.empty()) {
    return;
  }
  const std::optional<double> value = fieldValueAt(data, {field, association}, analysis, hit.at);
  if (!value) {
    return;
  }
  hit.field = field;
  hit.value = *value;
  // The colors the mesh is drawn with, which the legend shows too.
  vtkMapper* mapper = context.actors[actorIndex]->GetMapper();
  vtkScalarsToColors* colors = mapper ? mapper->GetLookupTable() : nullptr;
  if (analysis.categorical && colors) {
    const vtkIdType row = colors->GetAnnotatedValueIndex(*value);
    if (row >= 0) {
      double rgba[4];
      colors->GetIndexedColor(row, rgba);
      hit.category = ProbeCategory{colors->GetAnnotation(row), {rgba[0], rgba[1], rgba[2]}};
    }
  }
}

void MeshRenderer::setPinMarker(const std::optional<ProbeHit>& hit) {
  for (const auto& marker : pinMarkers_) {
    panelRenderers_[marker.panel]->RemoveActor(marker.marker);
  }
  pinMarkers_.clear();
  markerHit_ = hit;
  updatePinMarkers();
  if (context.window) {
    context.window->Render();
  }
}

void MeshRenderer::updatePinMarkers() {
  for (const auto& marker : pinMarkers_) {
    marker.marker->VisibilityOff();
  }
  if (!markerHit_) {
    return;
  }
  for (const auto& match : matchingProbes(*markerHit_)) {
    const auto slot = std::find_if(actorSlots_.begin(), actorSlots_.end(), [&match](const auto& s) {
      return s.mesh == match.mesh && s.panel == match.panel;
    });
    const size_t actor = static_cast<size_t>(std::distance(actorSlots_.begin(), slot));
    const auto existing = std::find_if(pinMarkers_.begin(),
                                       pinMarkers_.end(),
                                       [actor](const auto& m) { return m.actor == actor; });
    if (existing != pinMarkers_.end()) {
      existing->marker->VisibilityOn();
      continue;
    }
    vtkNew<vtkPoints> point;
    point->InsertNextPoint(match.position.data());
    vtkNew<vtkCellArray> vertices;
    const vtkIdType vertex = 0;
    vertices->InsertNextCell(1, &vertex);
    vtkNew<vtkPolyData> data;
    data->SetPoints(point);
    data->SetVerts(vertices);
    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputData(data);
    auto marker = vtkSmartPointer<vtkActor>::New();
    marker->SetMapper(mapper);
    marker->PickableOff();
    vtkProperty* property = marker->GetProperty();
    property->SetColor(1.0, 1.0, 1.0);
    property->SetRenderPointsAsSpheres(true);
    // VTK's Qt widget sets DPI to 72 times the device pixel ratio.
    const double dpi = context.window ? context.window->GetDPI() : 72.0;
    property->SetPointSize(static_cast<float>(kPinMarkerSize * dpi / 72.0));
    panelRenderers_[match.panel]->AddActor(marker);
    pinMarkers_.push_back({actor, match.panel, std::move(marker)});
  }
}

std::optional<std::array<double, 2>> MeshRenderer::displayPosition(const ProbeHit& hit) const {
  if (hit.panel >= panelRenderers_.size() || !panelRenderers_[hit.panel]->GetDraw()) {
    return std::nullopt;
  }
  vtkRenderer* ren = panelRenderers_[hit.panel];
  vtkCamera* camera = ren->GetActiveCamera();
  if (camera && !camera->GetParallelProjection()) {
    double eye[3];
    double forward[3];
    camera->GetPosition(eye);
    camera->GetDirectionOfProjection(forward);
    double ahead = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
      ahead += (hit.position[static_cast<size_t>(axis)] - eye[axis]) * forward[axis];
    }
    if (ahead <= 0.0) {
      return std::nullopt;
    }
  }
  ren->SetWorldPoint(hit.position[0], hit.position[1], hit.position[2], 1.0);
  ren->WorldToDisplay();
  double display[3];
  ren->GetDisplayPoint(display);
  return std::array<double, 2>{display[0], display[1]};
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
  activeScalarAnalysis = analyzeField(scalarName, association);
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

ScalarAnalysis MeshRenderer::analyzeField(const std::string& scalarName,
                                          FieldAssociation association) const {
  ScalarAnalysis analysis = analyzeScalar(rawMeshPointers(sceneMeshes), scalarName, association);
  if (analysis.categorical && sharedCatAnalysis.categorical)
    analysis = sharedCatAnalysis;
  if (const auto saved = categoryColorings_.find({scalarName, association});
      saved != categoryColorings_.end())
    analysis.coloring = saved->second;
  if (const auto saved = colormaps_.find({scalarName, association}); saved != colormaps_.end())
    analysis.colormap = saved->second;
  return analysis;
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
  // The caller's table holds the colors, so nothing may recolor it from an analysis. Not
  // `= {}`: GCC 16 takes the maps of that temporary for maybe uninitialized.
  activeScalarAnalysis = ScalarAnalysis();
  activeScalarAnalysis.categorical = true;
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
  activeScalarAnalysis = ScalarAnalysis(); // not `= {}`, see colorByFixedCategorical
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

void MeshRenderer::refreshAfterDataChange(size_t index) {
  if (index >= sceneMeshes.size() || !sceneMeshes[index]) {
    return;
  }
  // Scalar ranges and LUTs stay fixed during playback. Leave static comparison
  // meshes untouched so their mappers and spatial indices remain reusable.
  vtkDataSet* mesh = sceneMeshes[index];
  if (!activeScalarName.empty() &&
      arrayForAssociation(mesh, activeScalarName, activeScalarAssociation)) {
    if (activeScalarAssociation == FieldAssociation::Cell) {
      mesh->GetCellData()->SetActiveScalars(activeScalarName.c_str());
    } else {
      mesh->GetPointData()->SetActiveScalars(activeScalarName.c_str());
    }
  }
  mesh->Modified();
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
  // setMapperScalar leaves the table of a mesh without the field as it was, which
  // may belong to the field shown before.
  for (size_t index = 0; index < sceneMeshes.size() && index < mappers.size(); ++index) {
    if (arrayForAssociation(sceneMeshes[index], activeScalarName, activeScalarAssociation)) {
      return vtkLookupTable::SafeDownCast(mappers[index]->GetLookupTable());
    }
  }
  return nullptr;
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

// string_view copies only its pointer and length. cppcheck 2.7:
// https://github.com/cppcheck-opensource/cppcheck/pull/3817

// cppcheck-suppress passedByValue symbolName=id
bool MeshRenderer::setCategoryPalette(std::string_view id) {
  if (activeScalarName.empty() || !activeScalarAnalysis.categorical ||
      !pickPalette(activeScalarAnalysis.coloring, id)) {
    return false;
  }
  return applyFieldColors();
}

bool MeshRenderer::setCategoryColor(double value, const std::optional<std::array<double, 3>>& rgb) {
  if (activeScalarName.empty() || !activeScalarAnalysis.categorical) {
    return false;
  }
  recolorCategory(activeScalarAnalysis.coloring, value, rgb);
  return applyFieldColors();
}

// string_view copies only its pointer and length. cppcheck 2.7:
// https://github.com/cppcheck-opensource/cppcheck/pull/3817

// cppcheck-suppress passedByValue symbolName=id
bool MeshRenderer::setColormap(std::string_view id, bool reversed) {
  const Colormap* map = findColormap(id);
  if (!map || activeScalarName.empty() || activeScalarAnalysis.categorical) {
    return false;
  }
  activeScalarAnalysis.colormap.map = map;
  activeScalarAnalysis.colormap.reversed = reversed;
  return applyFieldColors();
}

bool MeshRenderer::setOutOfRange(OutOfRange outside) {
  if (activeScalarName.empty() || activeScalarAnalysis.categorical) {
    return false;
  }
  activeScalarAnalysis.colormap.outside = outside;
  return applyFieldColors();
}

bool MeshRenderer::applyFieldColors() {
  const std::pair<std::string, FieldAssociation> field{activeScalarName, activeScalarAssociation};
  colormaps_[field] = activeScalarAnalysis.colormap;
  categoryColorings_[field] = activeScalarAnalysis.coloring;
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
  return true;
}

void MeshRenderer::setSurfaceLook(const SurfaceLook& look) {
  surfaceLook_ = look;
  surfaceLook_.xrayOpacity = std::clamp(look.xrayOpacity, 0.05, 1.0);
  surfaceLook_.glassDensity = std::clamp(look.glassDensity, 0.05, 1.0);
  applySurfaceLook();
  if (context.window) {
    context.window->Render();
  }
}

void MeshRenderer::buildGlassStages(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes) {
  glassStages_.assign(meshes.size(), {});
  for (size_t i = 0; i < meshes.size(); ++i) {
    GlassStages& stages = glassStages_[i];
    stages.mesh = meshes[i];
    auto* poly = vtkPolyData::SafeDownCast(meshes[i]);
    if (!poly) {
      continue;
    }
    // Without splitting, points are not duplicated and cells keep their order, so the
    // ids that picking, the probe and annotation read stay the mesh's. Strips would
    // come out as triangles and renumber the cells.
    if (poly->GetNumberOfStrips() == 0) {
      stages.normals = vtkSmartPointer<vtkPolyDataNormals>::New();
      stages.normals->SetInputData(poly);
      stages.normals->SplittingOff();
    }
    stages.borders = vtkSmartPointer<vtkFeatureEdges>::New();
    stages.borders->SetInputData(poly);
    stages.borders->BoundaryEdgesOn();
    stages.borders->FeatureEdgesOff();
    stages.borders->NonManifoldEdgesOff();
    stages.borders->ManifoldEdgesOff();
    stages.borders->ColoringOff();
  }

  outlines_.clear();
  for (size_t i = 0; i < context.actors.size() && i < actorSlots_.size(); ++i) {
    const ActorSlot& slot = actorSlots_[i];
    if (slot.mesh >= glassStages_.size() || !glassStages_[slot.mesh].borders) {
      outlines_.emplace_back();
      continue;
    }
    vtkNew<vtkPolyDataMapper> mapper;
    mapper->SetInputConnection(glassStages_[slot.mesh].borders->GetOutputPort());
    mapper->ScalarVisibilityOff();
    // The borders lie on the surface's edges; pulled toward the camera they win the
    // depth test instead of fighting it (applySurfaceLook turns the offsets on).
    mapper->SetRelativeCoincidentTopologyLineOffsetParameters(0.0, -4.0);
    auto outline = vtkSmartPointer<vtkActor>::New();
    outline->SetMapper(mapper);
    outline->PickableOff();
    outline->VisibilityOff();
    vtkProperty* property = outline->GetProperty();
    property->SetColor(1.0, 1.0, 1.0);
    property->SetOpacity(0.9);
    property->LightingOff();
    panelRenderers_[slot.panel]->AddActor(outline);
    outlines_.push_back(outline);
  }
}

void MeshRenderer::applyGlass(size_t actorIndex, bool on) {
  vtkActor* actor = context.actors[actorIndex];
  vtkShaderProperty* shader = actor->GetShaderProperty();
  // Only on a change: a new replacement rebuilds the shaders, e.g. per Density step.
  if (on != (shader->GetNumberOfShaderReplacements() > 0)) {
    if (on) {
      shader->AddFragmentShaderReplacement(kGlassUnlitTag, false, kGlassUnlit, false);
      shader->AddFragmentShaderReplacement("//VTK::Light::Impl", false, kGlassShading, false);
    } else {
      shader->ClearAllFragmentShaderReplacements();
    }
  }
  if (actorIndex < actorSlots_.size() && actorSlots_[actorIndex].mesh < glassStages_.size()) {
    const GlassStages& stages = glassStages_[actorSlots_[actorIndex].mesh];
    vtkMapper* mapper = actor->GetMapper();
    if (on && stages.normals &&
        mapper->GetInputConnection(0, 0) != stages.normals->GetOutputPort()) {
      mapper->SetInputConnection(stages.normals->GetOutputPort());
    } else if (!on && mapper->GetInputDataObject(0, 0) != stages.mesh) {
      mapper->SetInputDataObject(stages.mesh);
    }
  }
  if (actorIndex < outlines_.size() && outlines_[actorIndex]) {
    vtkActor* outline = outlines_[actorIndex];
    outline->SetVisibility(on && actor->GetVisibility() ? 1 : 0);
    // VTK's Qt widget sets the window DPI to 72 times the device pixel ratio.
    const double dpi = context.window ? context.window->GetDPI() : 72.0;
    outline->GetProperty()->SetLineWidth(static_cast<float>(kGlassOutlineWidth * dpi / 72.0));
  }
}

void MeshRenderer::applySurfaceLook() {
  const SurfaceStyle style = surfaceLook_.style;
  const bool glass = style == SurfaceStyle::Glass;
  double opacity = 1.0;
  if (style == SurfaceStyle::XRay) {
    opacity = surfaceLook_.xrayOpacity;
  } else if (glass) {
    opacity = surfaceLook_.glassDensity;
  }
  for (size_t i = 0; i < context.actors.size(); ++i) {
    vtkProperty* property = context.actors[i]->GetProperty();
    property->SetRepresentation(style == SurfaceStyle::Wireframe ? VTK_WIREFRAME : VTK_SURFACE);
    property->SetEdgeVisibility(style == SurfaceStyle::SolidWithEdges ? 1 : 0);
#if VTK_VERSION_NUMBER >= VTK_VERSION_CHECK(9, 3, 0)
    // Black blended halfway into the surface reads on light and dark colormap ends.
    property->SetEdgeColor(0.0, 0.0, 0.0);
    property->SetEdgeOpacity(0.45);
#else
    // Edge opacity arrived in VTK 9.3; dark gray is the closest opaque edge.
    property->SetEdgeColor(0.2, 0.2, 0.2);
#endif
    property->SetOpacity(opacity);
    applyGlass(i, glass);
  }
  // Translucent surfaces need depth peeling to blend in depth order.
  const bool peel = seeThrough(style);
  if (peel && context.window) {
    context.window->SetAlphaBitPlanes(1);
  }
  if (glass) {
    vtkMapper::SetResolveCoincidentTopologyToPolygonOffset();
  }
  for (const auto& ren : panelRenderers_) {
    ren->SetUseDepthPeeling(peel ? 1 : 0);
  }
}

std::vector<size_t> MeshRenderer::getActiveScalarHistogram(size_t bins) const {
  if (activeScalarName.empty()) {
    return std::vector<size_t>(bins, 0);
  }
  return computeScalarHistogram(rawMeshPointers(sceneMeshes),
                                activeScalarName,
                                activeScalarAssociation,
                                activeScalarGlobalRange,
                                bins);
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
  return setPartsVisible({{partIndex, visible}});
}

bool MeshRenderer::setPartsVisible(const std::vector<PartVisibilityChange>& changes) {
  if (changes.empty() || std::any_of(changes.begin(), changes.end(), [this](const auto& change) {
        return change.part >= context.actors.size() || !context.actors[change.part];
      })) {
    return false;
  }
  for (const auto& change : changes) {
    context.actors[change.part]->SetVisibility(change.visible ? 1 : 0);
    if (change.part < outlines_.size() && outlines_[change.part]) {
      outlines_[change.part]->SetVisibility(
          change.visible && surfaceLook_.style == SurfaceStyle::Glass ? 1 : 0);
    }
  }
  updatePinMarkers();
  if (context.window) {
    context.window->Render();
  }
  return true;
}

bool MeshRenderer::getPanelViewport(size_t index, double viewport[4]) const {
  if (index >= panelRenderers_.size() || !panelRenderers_[index]->GetDraw()) {
    return false;
  }
  panelRenderers_[index]->GetViewport(viewport);
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
  outInfo.association = panel.association;
  outInfo.hasScalar = panel.hasScalar;
  outInfo.shown = panelRenderers_[panelIndex]->GetDraw() != 0;
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
  if (!clipFacetPanel(panelIndex, minValue, maxValue)) {
    return false;
  }
  if (context.window) {
    context.window->Render();
  }
  return true;
}

bool MeshRenderer::setFacetClipRange(size_t panelIndex, double minValue, double maxValue) {
  if (panelIndex >= facetPanels.size() || !facetPanels[panelIndex].hasScalar ||
      facetPanels[panelIndex].analysis.categorical || !std::isfinite(minValue) ||
      !std::isfinite(maxValue) || minValue > maxValue) {
    return false;
  }
  const size_t column = facetPanels[panelIndex].column;
  for (const auto& panel : facetPanels) {
    if (panel.column == column && panel.hasScalar &&
        (minValue > panel.globalRange[1] || maxValue < panel.globalRange[0])) {
      return false;
    }
  }
  for (size_t i = 0; i < facetPanels.size(); ++i) {
    if (facetPanels[i].column == column && facetPanels[i].hasScalar) {
      clipFacetPanel(i, minValue, maxValue);
    }
  }
  if (context.window) {
    context.window->Render();
  }
  return true;
}

bool MeshRenderer::clipFacetPanel(size_t panelIndex, double minValue, double maxValue) {
  if (panelIndex >= facetPanels.size()) {
    return false;
  }

  FacetPanelState& panel = facetPanels[panelIndex];
  if (!panel.hasScalar) {
    return false;
  }

  if (!std::isfinite(minValue) || !std::isfinite(maxValue) || minValue > maxValue) {
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
    panel.lut->SetRange(panel.clipRange);
  }
  for (const auto& mapper : panel.scalarMappers) {
    mapper->SetScalarRange(panel.clipRange);
  }
  return true;
}

// string_view copies only its pointer and length. cppcheck 2.7:
// https://github.com/cppcheck-opensource/cppcheck/pull/3817

// cppcheck-suppress passedByValue symbolName=id
bool MeshRenderer::setFacetColormap(size_t panelIndex, std::string_view id, bool reversed) {
  const Colormap* map = findColormap(id);
  if (!map || panelIndex >= facetPanels.size() || !facetPanels[panelIndex].hasScalar ||
      facetPanels[panelIndex].analysis.categorical) {
    return false;
  }
  facetPanels[panelIndex].analysis.colormap.map = map;
  facetPanels[panelIndex].analysis.colormap.reversed = reversed;
  return redrawFacetColumn(panelIndex);
}

// string_view copies only its pointer and length. cppcheck 2.7:
// https://github.com/cppcheck-opensource/cppcheck/pull/3817

// cppcheck-suppress passedByValue symbolName=id
bool MeshRenderer::setFacetCategoryPalette(size_t panelIndex, std::string_view id) {
  return panelIndex < facetPanels.size() && facetPanels[panelIndex].lut &&
         facetPanels[panelIndex].analysis.categorical &&
         pickPalette(facetPanels[panelIndex].analysis.coloring, id) &&
         redrawFacetColumn(panelIndex);
}

bool MeshRenderer::setFacetCategoryColor(size_t panelIndex,
                                         double value,
                                         const std::optional<std::array<double, 3>>& rgb) {
  if (panelIndex >= facetPanels.size() || !facetPanels[panelIndex].lut ||
      !facetPanels[panelIndex].analysis.categorical) {
    return false;
  }
  recolorCategory(facetPanels[panelIndex].analysis.coloring, value, rgb);
  return redrawFacetColumn(panelIndex);
}

bool MeshRenderer::redrawFacetColumn(size_t panelIndex) {
  const auto& source = facetPanels[panelIndex];
  for (auto& panel : facetPanels) {
    if (panel.column != source.column) {
      continue;
    }
    panel.analysis.colormap = source.analysis.colormap;
    panel.analysis.coloring = source.analysis.coloring;
    if (!panel.hasScalar) {
      continue;
    }
    panel.lut = buildLookupTable(panel.title, panel.analysis, panel.clipRange);
    for (const auto& mapper : panel.scalarMappers) {
      mapper->SetLookupTable(panel.lut);
    }
  }
  if (context.window) {
    context.window->Render();
  }
  return true;
}
