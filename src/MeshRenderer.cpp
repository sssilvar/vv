#include "MeshRenderer.h"

#include "FacetPages.h"
#include "ScalarVizUtils.h"
#include "mesh_utils.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
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
#include <vtkImageSlice.h>
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

namespace {

std::vector<vtkDataSet*> rawMeshPointers(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes) {
  std::vector<vtkDataSet*> result;
  result.reserve(meshes.size());
  std::transform(meshes.begin(), meshes.end(), std::back_inserter(result), [](const auto& mesh) {
    return mesh.GetPointer();
  });
  return result;
}

// Row-major cell (index) of a cols x rows grid filling area, as a VTK viewport (y grows up).
void gridViewport(const double area[4], size_t index, size_t cols, size_t rows, double out[4]) {
  const size_t row = index / cols;
  const double r = static_cast<double>(row);
  const double c = static_cast<double>(index % cols);
  const double nc = static_cast<double>(cols);
  const double nr = static_cast<double>(rows);
  const double width = area[2] - area[0];
  const double height = area[3] - area[1];
  out[0] = area[0] + width * (c / nc);
  out[1] = area[1] + height * (1.0 - (r + 1.0) / nr);
  out[2] = area[0] + width * ((c + 1.0) / nc);
  out[3] = area[1] + height * (1.0 - r / nr);
}

// A short line per cell (or per point) of mesh along its 3-component array; nullptr when
// the mesh lacks one.
vtkSmartPointer<vtkGlyph3D>
makeGlyphs(vtkDataSet* mesh, const std::string& name, FieldAssociation association) {
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

  auto glyph = vtkSmartPointer<vtkGlyph3D>::New();
  glyph->SetInputData(seed);
  glyph->SetSourceConnection(line->GetOutputPort());
  glyph->OrientOn();
  glyph->SetVectorModeToUseVector();
  glyph->SetScaleModeToDataScalingOff();
  return glyph;
}

vtkSmartPointer<vtkActor> makeGlyphActor(vtkGlyph3D* glyphs) {
  vtkNew<vtkPolyDataMapper> mapper;
  mapper->SetInputConnection(glyphs->GetOutputPort());
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
  for (const Tile& tile : tiles_) {
    tile.renderer->RemoveObserver(clipCb_);
  }
}
MeshRenderer::MeshRenderer() = default;

void MeshRenderer::setRenderContext(vtkRenderWindow* externalWindow,
                                    vtkRenderWindowInteractor* externalInteractor) {
  context.window = externalWindow;
  interactor = externalInteractor;
}

void MeshRenderer::setup(const std::vector<vtkSmartPointer<vtkDataSet>>& meshes,
                         const std::vector<MeshGroup>& groups,
                         const std::vector<std::array<double, 3>>& colorsHex,
                         PaneMode mode) {
  sceneMeshes = meshes;
  if (!context.window) {
    context.window = vtkSmartPointer<vtkRenderWindow>::New();
  }
  context.window->SetDesiredUpdateRate(120.0);

  fields_.clear();
  for (ScalarField& field : collectFields(rawMeshPointers(meshes), 1)) {
    SceneField scene;
    scene.field = std::move(field);
    analyze(scene);
    fields_.push_back(std::move(scene));
  }

  panes_.clear();
  for (size_t index = 0; index < std::max<size_t>(1, groups.size()); ++index) {
    Pane pane;
    if (groups.empty()) {
      pane.meshes.resize(meshes.size());
      std::iota(pane.meshes.begin(), pane.meshes.end(), size_t{0});
    } else {
      std::copy_if(groups[index].partIndices.begin(),
                   groups[index].partIndices.end(),
                   std::back_inserter(pane.meshes),
                   [&meshes](size_t mesh) { return mesh < meshes.size(); });
    }
    for (size_t field = 0; field < fields_.size(); ++field) {
      const ScalarField& candidate = fields_[field].field;
      if (std::any_of(pane.meshes.begin(), pane.meshes.end(), [&](size_t mesh) {
            return arrayForAssociation(meshes[mesh], candidate.name, candidate.association) !=
                   nullptr;
          })) {
        pane.fields.push_back(field);
      }
    }
    if (!pane.fields.empty()) {
      pane.mode = mode;
      pane.first = 0;
    }
    pane.tileCount = std::clamp<size_t>(pane.fields.size(), 1, kMaxFacetPage);
    panes_.push_back(std::move(pane));
  }
  labels_.reset();
  buildTiles(colorsHex);
  frameScene();

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
  buildGlassStages();
  applySurfaceLook();
}

void MeshRenderer::analyze(SceneField& scene) const {
  const std::vector<vtkDataSet*> meshes = rawMeshPointers(sceneMeshes);
  const ScalarField& field = scene.field;
  ScalarAnalysis analysis = analyzeScalar(meshes, field.name, field.association);
  if (analysis.categorical && sharedCatAnalysis.categorical) {
    const bool hasNaN = analysis.hasNaN;
    analysis = sharedCatAnalysis;
    analysis.hasNaN = hasNaN;
  }
  // Analyzed before: the colors chosen for it stay.
  if (scene.lut) {
    analysis.coloring = scene.analysis.coloring;
    analysis.colormap = scene.analysis.colormap;
  }
  scene.analysis = std::move(analysis);
  if (!computeScalarGlobalRange(meshes, field.name, field.association, scene.globalRange)) {
    scene.globalRange[0] = 0.0;
    scene.globalRange[1] = 1.0;
  }
  std::copy_n(scene.globalRange, 2, scene.clipRange);
  scene.lut = buildLookupTable(field.name, scene.analysis, scene.clipRange);
}

void MeshRenderer::buildTiles(const std::vector<std::array<double, 3>>& colorsHex) {
  for (const Tile& tile : tiles_) {
    tile.renderer->RemoveObserver(clipCb_);
  }
  vtkRendererCollection* renderers = context.window->GetRenderers();
  while (vtkRenderer* stale = renderers->GetFirstRenderer()) {
    context.window->RemoveRenderer(stale);
  }
  glyphActors_.clear();
  pinMarkers_.clear();
  markerHit_.reset();
  probeLocators_.clear();
  tiles_.clear();
  mappers.clear();
  context.actors.clear();
  actorSlots_.clear();

  background_ = vtkSmartPointer<vtkRenderer>::New();
  vtkNew<vtkCamera> idle; // vtkRenderer::Render reads it before drawing anything
  background_->SetActiveCamera(idle);
  background_->InteractiveOff();
  context.window->AddRenderer(background_);

  camera_ = vtkSmartPointer<vtkCamera>::New();
  for (size_t index = 0; index < panes_.size(); ++index) {
    Pane& pane = panes_[index];
    pane.firstTile = tiles_.size();
    for (size_t count = 0; count < pane.tileCount; ++count) {
      Tile tile;
      tile.pane = index;
      tile.firstActor = context.actors.size();
      tile.renderer = vtkSmartPointer<vtkRenderer>::New();
      tile.renderer->SetActiveCamera(camera_);
      context.window->AddRenderer(tile.renderer);
      for (size_t mesh : pane.meshes) {
        vtkNew<vtkDataSetMapper> mapper;
        mapper->SetInputData(sceneMeshes[mesh]);
        mapper->ScalarVisibilityOff();
        const auto color = mesh < colorsHex.size() ? colorsHex[mesh]
                                                   : generateDistinctColor(static_cast<int>(mesh));
        vtkNew<vtkActor> actor;
        actor->SetMapper(mapper);
        actor->GetProperty()->SetColor(color[0], color[1], color[2]);
        tile.renderer->AddActor(actor);
        mappers.emplace_back(mapper);
        context.actors.emplace_back(actor);
        actorSlots_.push_back({mesh, tiles_.size()});
      }
      tiles_.push_back(std::move(tile));
    }
  }
  layout();
}

void MeshRenderer::layout() {
  if (tiles_.empty()) {
    return;
  }
  const int* size = context.window->GetSize();
  const double width = std::max(1, size[0]);
  const double height = std::max(1, size[1]);
  const double ratio = metrics_.pixelRatio > 0.0 ? metrics_.pixelRatio : 1.0;
  const double inset = std::clamp(metrics_.bottomInset * ratio, 0.0, height - 1.0);
  const double view[4] = {0.0, inset / height, 1.0, 1.0};
  const size_t cols =
      adaptiveGridCols(panes_.size(), static_cast<int>(width), static_cast<int>(height - inset));
  const size_t rows = 1 + (panes_.size() - 1) / cols;

  for (size_t index = 0; index < panes_.size(); ++index) {
    Pane& pane = panes_[index];
    gridViewport(view, index, cols, rows, pane.viewport);
    double area[4];
    std::copy_n(pane.viewport, 4, area);
    size_t gridCols = 1;
    size_t gridRows = 1;
    size_t shown = 1;
    pane.pageSize = 1;
    if (pane.mode == PaneMode::Split) {
      const double paneWidth = (area[2] - area[0]) * width;
      const double paneHeight = (area[3] - area[1]) * height;
      const double strip = std::min(metrics_.pagerStrip * ratio, 0.5 * paneHeight);
      area[1] += strip / height;
      const int tilesWidth = static_cast<int>(paneWidth);
      const int tilesHeight = static_cast<int>(paneHeight - strip);
      pane.pageSize = std::max<size_t>(1,
                                       facetPageSize(pane.fields.size(),
                                                     static_cast<int>(tilesWidth / ratio),
                                                     static_cast<int>(tilesHeight / ratio)));
      // The page holding the field shown first before the pages changed size.
      pane.first = static_cast<int>(static_cast<size_t>(std::max(pane.first, 0)) / pane.pageSize *
                                    pane.pageSize);
      // Every page uses the grid of a full one, so a field's tile stays put while paging.
      gridCols = adaptiveGridCols(pane.pageSize, tilesWidth, tilesHeight);
      gridRows = 1 + (pane.pageSize - 1) / gridCols;
      shown = pane.shownCount();
    }
    for (size_t count = 0; count < pane.tileCount; ++count) {
      Tile& tile = tiles_[pane.firstTile + count];
      tile.shown = count < shown;
      tile.field.reset();
      if (tile.shown) {
        gridViewport(area, count, gridCols, gridRows, tile.viewport);
        tile.renderer->SetViewport(tile.viewport);
        if (pane.first >= 0) {
          tile.field = pane.fields[static_cast<size_t>(pane.first) + count];
        }
        colorTile(pane.firstTile + count);
      }
      tile.renderer->SetDraw(tile.shown ? 1 : 0);
      tile.renderer->SetInteractive(tile.shown ? 1 : 0);
    }
  }
  updatePinMarkers();
}

void MeshRenderer::colorTile(size_t index) {
  const Tile& tile = tiles_[index];
  const size_t end = tile.firstActor + panes_[tile.pane].meshes.size();
  for (size_t actor = tile.firstActor; actor < end; ++actor) {
    colorActor(actor);
  }
}

void MeshRenderer::colorActor(size_t actor) {
  const ActorSlot& slot = actorSlots_[actor];
  const Tile& tile = tiles_[slot.tile];
  vtkDataSet* mesh = sceneMeshes[slot.mesh];
  if (labels_ && actor == 0) {
    mapScalar(mesh, mappers[actor], labels_->field, true, labels_->lut, labels_->range);
  } else if (tile.field) {
    const SceneField& scene = fields_[*tile.field];
    mapScalar(
        mesh, mappers[actor], scene.field, scene.analysis.categorical, scene.lut, scene.clipRange);
  } else {
    mappers[actor]->ScalarVisibilityOff();
  }
}

bool MeshRenderer::recolor(size_t field) {
  for (size_t index = 0; index < tiles_.size(); ++index) {
    if (tiles_[index].shown && tiles_[index].field == field) {
      colorTile(index);
    }
  }
  render();
  return true;
}

void MeshRenderer::render() {
  if (context.window) {
    context.window->Render();
  }
}

void MeshRenderer::frameScene() {
  vtkBoundingBox bbox;
  for (const auto& mesh : sceneMeshes) {
    double bounds[6];
    mesh->GetBounds(bounds);
    bbox.AddBounds(bounds);
  }
  if (bbox.IsValid()) {
    bbox.GetBounds(sceneBounds_);
  }
  tiles_.front().renderer->ResetCamera(sceneBounds_);
  fittedCameraMTime_ = camera_->GetMTime();

  // The interactor style resets clipping from the tile under the cursor only;
  // re-deriving it from every mesh before each tile draws keeps the shared
  // camera from clipping geometry that only another tile shows.
  if (!clipCb_) {
    clipCb_ = vtkSmartPointer<vtkCallbackCommand>::New();
    clipCb_->SetCallback([](vtkObject* caller, unsigned long, void* client, void*) {
      auto* self = static_cast<MeshRenderer*>(client);
      auto* tile = static_cast<vtkRenderer*>(caller);
      tile->ResetCameraClippingRange(self->sceneBounds_);
      auto* style = vtkInteractorStyle::SafeDownCast(self->interactor->GetInteractorStyle());
      // Bound Glass's repeated translucent passes while dragging. StopState clears
      // the interaction state before rendering, so the release frame is full quality.
      const bool movingGlass = self->surfaceLook_.style == SurfaceStyle::Glass && style &&
                               style->GetState() != VTKIS_NONE;
      tile->SetMaximumNumberOfPeels(movingGlass ? 2 : 4);
    });
  }
  clipCb_->SetClientData(this);
  for (const Tile& tile : tiles_) {
    tile.renderer->AddObserver(vtkCommand::StartEvent, clipCb_);
  }
}

void MeshRenderer::addImagePlanes(const std::vector<vtkSmartPointer<vtkImageSlice>>& planes,
                                  const std::array<double, 6>& bounds) {
  if (tiles_.empty() || planes.empty()) {
    return;
  }
  for (const Tile& tile : tiles_) {
    for (const auto& plane : planes) {
      tile.renderer->AddViewProp(plane);
    }
  }
  vtkBoundingBox bbox(bounds.data());
  // Without meshes, sceneBounds_ holds only its placeholder unit box.
  if (!sceneMeshes.empty()) {
    bbox.AddBounds(sceneBounds_);
  }
  bbox.GetBounds(sceneBounds_);
  tiles_.front().renderer->ResetCamera(sceneBounds_);
  fittedCameraMTime_ = camera_->GetMTime();
}

std::optional<size_t> MeshRenderer::findField(const ScalarField& field) const {
  const auto found = std::find_if(fields_.begin(), fields_.end(), [&field](const SceneField& s) {
    return s.field.name == field.name && s.field.association == field.association;
  });
  return found == fields_.end()
             ? std::nullopt
             : std::optional<size_t>(static_cast<size_t>(std::distance(fields_.begin(), found)));
}

void MeshRenderer::setPaneMode(size_t pane, PaneMode mode) {
  if (pane >= panes_.size() || panes_[pane].fields.empty() || panes_[pane].mode == mode) {
    return;
  }
  panes_[pane].mode = mode;
  layout();
  render();
}

void MeshRenderer::showField(size_t pane, int position) {
  if (pane >= panes_.size()) {
    return;
  }
  Pane& source = panes_[pane];
  source.first = std::clamp(position,
                            source.mode == PaneMode::Single ? -1 : 0,
                            static_cast<int>(source.fields.size()) - 1);
  for (Pane& other : panes_) {
    if (!linked_ || &other == &source) {
      continue;
    }
    if (source.first < 0) {
      other.first = other.mode == PaneMode::Single ? -1 : other.first;
      continue;
    }
    const auto same = std::find(
        other.fields.begin(), other.fields.end(), source.fields[static_cast<size_t>(source.first)]);
    if (same != other.fields.end()) {
      other.first = static_cast<int>(std::distance(other.fields.begin(), same));
    }
  }
  layout();
  render();
}

void MeshRenderer::stepPane(size_t pane, int step) {
  if (pane >= panes_.size()) {
    return;
  }
  const Pane& target = panes_[pane];
  const int pages = static_cast<int>(target.pageCount());
  const int page = ((static_cast<int>(target.page()) + step) % pages + pages) % pages;
  showField(pane,
            target.mode == PaneMode::Single ? page - 1 : page * static_cast<int>(target.pageSize));
}

void MeshRenderer::setLayoutMetrics(const LayoutMetrics& metrics) {
  metrics_ = metrics;
  layout();
}

std::optional<size_t> MeshRenderer::paneAt(double x, double y) const {
  if (!context.window) {
    return std::nullopt;
  }
  const int* size = context.window->GetSize();
  const double fx = x / std::max(1, size[0]);
  const double fy = y / std::max(1, size[1]);
  for (size_t index = 0; index < panes_.size(); ++index) {
    const double* viewport = panes_[index].viewport;
    if (fx >= viewport[0] && fx < viewport[2] && fy >= viewport[1] && fy < viewport[3]) {
      return index;
    }
  }
  return std::nullopt;
}

void MeshRenderer::refitCameraIfUntouched() {
  layout();
  if (tiles_.empty() || camera_->GetMTime() != fittedCameraMTime_) {
    return;
  }
  tiles_.front().renderer->ResetCamera(sceneBounds_);
  fittedCameraMTime_ = camera_->GetMTime();
}

void MeshRenderer::resetCamera() {
  if (tiles_.empty()) {
    return;
  }
  // VTK's default camera, which vv opens with before framing the scene.
  camera_->SetPosition(0.0, 0.0, 1.0);
  camera_->SetFocalPoint(0.0, 0.0, 0.0);
  camera_->SetViewUp(0.0, 1.0, 0.0);
  tiles_.front().renderer->ResetCamera(sceneBounds_);
  fittedCameraMTime_ = camera_->GetMTime();
  render();
}

void MeshRenderer::dolly(double factor) {
  if (tiles_.empty()) {
    return;
  }
  camera_->Dolly(factor);
  render();
}

std::optional<ProbeHit> MeshRenderer::probe(double x, double y) {
  const auto under = std::find_if(tiles_.begin(), tiles_.end(), [x, y](const Tile& candidate) {
    return candidate.shown &&
           candidate.renderer->IsInViewport(static_cast<int>(x), static_cast<int>(y));
  });
  if (under == tiles_.end()) {
    return std::nullopt;
  }
  const auto tile = static_cast<size_t>(std::distance(tiles_.begin(), under));
  if (!probePicker_) {
    probePicker_ = vtkSmartPointer<vtkCellPicker>::New();
    probePicker_->SetTolerance(0.0005);
    probePicker_->PickFromListOn();
  }
  // This tile's meshes only: the camera is shared, so another tile's mesh lies on
  // the same ray, and glyphs drawn over a mesh are not its data.
  probePicker_->InitializePickList();
  for (size_t i = 0; i < actorSlots_.size(); ++i) {
    if (actorSlots_[i].tile == tile) {
      probePicker_->AddPickList(context.actors[i]);
    }
  }
  if (probePicker_->Pick(x, y, 0.0, under->renderer) == 0 || probePicker_->GetCellId() < 0) {
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
  hit.tile = tile;
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
  if (hit.mesh >= sceneMeshes.size()) {
    return std::nullopt;
  }
  // The hit's own tile, or the first shown one drawing its mesh once a mode change or a
  // shorter page hid that tile.
  std::optional<size_t> actor;
  for (size_t i = 0; i < actorSlots_.size(); ++i) {
    const ActorSlot& slot = actorSlots_[i];
    if (slot.mesh != hit.mesh || !tiles_[slot.tile].shown || !context.actors[i]->GetVisibility()) {
      continue;
    }
    if (slot.tile == hit.tile) {
      actor = i;
      break;
    }
    if (!actor) {
      actor = i;
    }
  }
  if (!actor) {
    return std::nullopt;
  }
  ProbeHit again;
  again.tile = actorSlots_[*actor].tile;
  again.mesh = hit.mesh;
  again.cell = hit.cell;
  again.at = hit.at;
  again.position = hit.position;
  readProbedField(again, *actor, sceneMeshes[hit.mesh]);
  return again;
}

std::vector<ProbeHit> MeshRenderer::matchingProbes(const ProbeHit& hit) {
  std::vector<ProbeHit> matches;
  if (!probeAgain(hit)) {
    return matches;
  }
  // A file can appear in many tiles. Locate its cell only once per update.
  std::map<size_t, std::optional<ProbeHit>> locations;
  locations.emplace(hit.mesh, hit);
  probeLocators_.resize(sceneMeshes.size());
  std::vector<bool> matchedTiles(tiles_.size(), false);
  std::vector<bool> sourceTiles(tiles_.size(), false);
  for (size_t actor = 0; actor < actorSlots_.size(); ++actor) {
    if (actorSlots_[actor].mesh == hit.mesh && context.actors[actor]->GetVisibility()) {
      sourceTiles[actorSlots_[actor].tile] = true;
    }
  }
  for (size_t actor = 0; actor < actorSlots_.size(); ++actor) {
    const auto& slot = actorSlots_[actor];
    if (!context.actors[actor]->GetVisibility() || !tiles_[slot.tile].shown ||
        matchedTiles[slot.tile] || (sourceTiles[slot.tile] && slot.mesh != hit.mesh)) {
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
      match.tile = slot.tile;
      match.field.clear();
      match.category.reset();
      readProbedField(match, actor, sceneMeshes[slot.mesh]);
      matches.push_back(std::move(match));
      matchedTiles[slot.tile] = true;
    }
  }
  return matches;
}

void MeshRenderer::readProbedField(ProbeHit& hit, size_t actorIndex, vtkDataSet* data) const {
  const Tile& tile = tiles_[actorSlots_[actorIndex].tile];
  const bool labeled = labels_ && actorIndex == 0;
  if (!labeled && !tile.field) {
    return;
  }
  ScalarAnalysis labelAnalysis;
  labelAnalysis.categorical = true;
  const std::string& field = labeled ? labels_->field.name : fields_[*tile.field].field.name;
  const FieldAssociation association =
      labeled ? labels_->field.association : fields_[*tile.field].field.association;
  const ScalarAnalysis& analysis = labeled ? labelAnalysis : fields_[*tile.field].analysis;
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
    tiles_[actorSlots_[marker.actor].tile].renderer->RemoveActor(marker.marker);
  }
  pinMarkers_.clear();
  markerHit_ = hit;
  updatePinMarkers();
  render();
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
      return s.mesh == match.mesh && s.tile == match.tile;
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
    tiles_[match.tile].renderer->AddActor(marker);
    pinMarkers_.push_back({actor, std::move(marker)});
  }
}

std::optional<std::array<double, 2>> MeshRenderer::displayPosition(const ProbeHit& hit) const {
  if (hit.tile >= tiles_.size() || !tiles_[hit.tile].shown) {
    return std::nullopt;
  }
  vtkRenderer* ren = tiles_[hit.tile].renderer;
  if (!camera_->GetParallelProjection()) {
    double eye[3];
    double forward[3];
    camera_->GetPosition(eye);
    camera_->GetDirectionOfProjection(forward);
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

bool MeshRenderer::setClipRange(size_t field, double minValue, double maxValue) {
  if (field >= fields_.size() || fields_[field].analysis.categorical || !std::isfinite(minValue) ||
      !std::isfinite(maxValue)) {
    return false;
  }
  SceneField& scene = fields_[field];
  minValue = std::max(minValue, scene.globalRange[0]);
  maxValue = std::min(maxValue, scene.globalRange[1]);
  if (minValue > maxValue) {
    return false;
  }
  scene.clipRange[0] = minValue;
  scene.clipRange[1] = maxValue;
  scene.lut->SetRange(scene.clipRange);
  return recolor(field);
}

bool MeshRenderer::setFieldRange(size_t field, double minValue, double maxValue) {
  if (field >= fields_.size() || !std::isfinite(minValue) || !std::isfinite(maxValue) ||
      minValue > maxValue) {
    return false;
  }
  SceneField& scene = fields_[field];
  scene.globalRange[0] = scene.clipRange[0] = minValue;
  scene.globalRange[1] = scene.clipRange[1] = maxValue;
  scene.lut->SetRange(scene.clipRange);
  return recolor(field);
}

// string_view copies only its pointer and length. cppcheck 2.7:
// https://github.com/cppcheck-opensource/cppcheck/pull/3817

// cppcheck-suppress passedByValue symbolName=id
bool MeshRenderer::setColormap(size_t field, std::string_view id, bool reversed) {
  const Colormap* map = findColormap(id);
  if (!map || field >= fields_.size() || fields_[field].analysis.categorical) {
    return false;
  }
  fields_[field].analysis.colormap.map = map;
  fields_[field].analysis.colormap.reversed = reversed;
  return retable(field);
}

bool MeshRenderer::setOutOfRange(size_t field, OutOfRange outside) {
  if (field >= fields_.size() || fields_[field].analysis.categorical) {
    return false;
  }
  fields_[field].analysis.colormap.outside = outside;
  return retable(field);
}

// string_view copies only its pointer and length. cppcheck 2.7:
// https://github.com/cppcheck-opensource/cppcheck/pull/3817

// cppcheck-suppress passedByValue symbolName=id
bool MeshRenderer::setCategoryPalette(size_t field, std::string_view id) {
  return field < fields_.size() && fields_[field].analysis.categorical &&
         pickPalette(fields_[field].analysis.coloring, id) && retable(field);
}

bool MeshRenderer::setCategoryColor(size_t field,
                                    double value,
                                    const std::optional<std::array<double, 3>>& rgb) {
  if (field >= fields_.size() || !fields_[field].analysis.categorical) {
    return false;
  }
  recolorCategory(fields_[field].analysis.coloring, value, rgb);
  return retable(field);
}

bool MeshRenderer::retable(size_t field) {
  SceneField& scene = fields_[field];
  scene.lut = buildLookupTable(scene.field.name, scene.analysis, scene.clipRange);
  return recolor(field);
}

std::vector<size_t> MeshRenderer::fieldHistogram(size_t field, size_t bins) const {
  if (field >= fields_.size()) {
    return std::vector<size_t>(bins, 0);
  }
  const SceneField& scene = fields_[field];
  return computeScalarHistogram(rawMeshPointers(sceneMeshes),
                                scene.field.name,
                                scene.field.association,
                                scene.globalRange,
                                bins);
}

void MeshRenderer::reanalyzeField(size_t field) {
  if (field < fields_.size()) {
    analyze(fields_[field]);
    recolor(field);
  }
}

void MeshRenderer::setLabelColoring(const std::string& arrayName,
                                    vtkLookupTable* lut,
                                    const double range[2]) {
  if (tiles_.empty()) {
    return;
  }
  if (lut) {
    labels_ = LabelColoring{{arrayName, FieldAssociation::Cell}, lut, {range[0], range[1]}};
  } else {
    labels_.reset();
  }
  colorTile(0);
  render();
}

void MeshRenderer::refreshAfterDataChange(size_t index) {
  if (index >= sceneMeshes.size() || !sceneMeshes[index]) {
    return;
  }
  // Scalar ranges and LUTs stay fixed during playback. Leave static comparison
  // meshes untouched so their mappers and spatial indices remain reusable.
  for (size_t actor = 0; actor < actorSlots_.size(); ++actor) {
    const ActorSlot& slot = actorSlots_[actor];
    if (slot.mesh == index && tiles_[slot.tile].shown) {
      colorActor(actor);
    }
  }
  sceneMeshes[index]->Modified();
  render();
}

void MeshRenderer::setSharedCatAnalysis(const ScalarAnalysis& shared) {
  sharedCatAnalysis = shared;
}

void MeshRenderer::setSurfaceLook(const SurfaceLook& look) {
  surfaceLook_ = look;
  surfaceLook_.xrayOpacity = std::clamp(look.xrayOpacity, 0.05, 1.0);
  surfaceLook_.glassDensity = std::clamp(look.glassDensity, 0.05, 1.0);
  applySurfaceLook();
  render();
}

void MeshRenderer::buildGlassStages() {
  glassStages_.assign(sceneMeshes.size(), {});
  for (size_t i = 0; i < sceneMeshes.size(); ++i) {
    GlassStages& stages = glassStages_[i];
    stages.mesh = sceneMeshes[i];
    auto* poly = vtkPolyData::SafeDownCast(sceneMeshes[i]);
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
    tiles_[slot.tile].renderer->AddActor(outline);
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
  for (const Tile& tile : tiles_) {
    tile.renderer->SetUseDepthPeeling(peel ? 1 : 0);
  }
}

bool MeshRenderer::setVectorGlyphs(const std::string& name, FieldAssociation association) {
  for (const auto& [ren, actor] : glyphActors_) {
    ren->RemoveActor(actor);
  }
  glyphActors_.clear();
  for (size_t mesh = 0; !name.empty() && mesh < sceneMeshes.size(); ++mesh) {
    const vtkSmartPointer<vtkGlyph3D> glyphs = makeGlyphs(sceneMeshes[mesh], name, association);
    for (size_t actor = 0; glyphs && actor < actorSlots_.size(); ++actor) {
      if (actorSlots_[actor].mesh == mesh) {
        vtkRenderer* ren = tiles_[actorSlots_[actor].tile].renderer;
        glyphActors_.emplace_back(ren, makeGlyphActor(glyphs));
        ren->AddActor(glyphActors_.back().second);
      }
    }
  }
  render();
  return !glyphActors_.empty();
}

bool MeshRenderer::setPartVisible(size_t partIndex, bool visible) {
  return setPartsVisible({{partIndex, visible}});
}

bool MeshRenderer::setPartsVisible(const std::vector<PartVisibilityChange>& changes) {
  if (changes.empty() || std::any_of(changes.begin(), changes.end(), [this](const auto& change) {
        return change.part >= sceneMeshes.size();
      })) {
    return false;
  }
  for (const auto& change : changes) {
    for (size_t actor = 0; actor < actorSlots_.size(); ++actor) {
      if (actorSlots_[actor].mesh != change.part) {
        continue;
      }
      context.actors[actor]->SetVisibility(change.visible ? 1 : 0);
      if (outlines_[actor]) {
        outlines_[actor]->SetVisibility(
            change.visible && surfaceLook_.style == SurfaceStyle::Glass ? 1 : 0);
      }
    }
  }
  updatePinMarkers();
  render();
  return true;
}
