#include "MeshRenderer.h"
#include "ProbeTrace.h"
#include "TemporalSource.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vtkActorCollection.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCommand.h>
#include <vtkFloatArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkIntArray.h>
#include <vtkInteractorStyle.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPropCollection.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRendererCollection.h>
#include <vtkShaderProperty.h>

namespace {
// Shows field in the first pane and returns its index among the scene's fields.
size_t show(MeshRenderer& renderer, const ScalarField& field) {
  const size_t index = renderer.findField(field).value();
  const std::vector<size_t>& fields = renderer.panes().front().fields;
  renderer.showField(
      0, static_cast<int>(std::find(fields.begin(), fields.end(), index) - fields.begin()));
  return index;
}

vtkLookupTable* lutOf(const MeshRenderer& renderer, size_t field) {
  return renderer.fields()[field].lut;
}

std::vector<size_t> shownTiles(const MeshRenderer& renderer) {
  std::vector<size_t> shown;
  for (size_t tile = 0; tile < renderer.tiles().size(); ++tile) {
    if (renderer.tiles()[tile].shown) {
      shown.push_back(tile);
    }
  }
  return shown;
}

// The tiles shown fill the cells of a cols x rows grid over the window in order.
bool checkGrid(MeshRenderer& renderer, size_t count, size_t cols, size_t rows) {
  // A renderer that clears the window, then every pane's pool of tiles.
  auto* renderers = renderer.context.window->GetRenderers();
  const std::vector<size_t> shown = shownTiles(renderer);
  if (static_cast<size_t>(renderers->GetNumberOfItems()) != 1 + renderer.tiles().size() ||
      shown.size() != count) {
    std::cerr << "Expected " << count << " tiles shown\n";
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    const Tile& tile = renderer.tiles()[shown[i]];
    const double* viewport = tile.renderer->GetViewport();
    const size_t row = i / cols;
    const double expected[4] = {static_cast<double>(i % cols) / static_cast<double>(cols),
                                1.0 - static_cast<double>(row + 1) / static_cast<double>(rows),
                                static_cast<double>(i % cols + 1) / static_cast<double>(cols),
                                1.0 - static_cast<double>(row) / static_cast<double>(rows)};
    for (size_t j = 0; j < 4; ++j) {
      if (std::abs(viewport[j] - expected[j]) > 1e-12 || tile.viewport[j] != viewport[j] ||
          !tile.renderer->GetDraw()) {
        std::cerr << "Unexpected viewport for tile " << i << " in " << cols << 'x' << rows << '\n';
        return false;
      }
    }
  }
  return true;
}

// Within one step of an 8-bit table entry.
bool nearColor(const double* actual, const std::array<double, 3>& expected) {
  for (size_t c = 0; c < 3; ++c) {
    if (std::abs(actual[c] - expected[c]) > 1.5 / 255.0) {
      return false;
    }
  }
  return true;
}

bool sameColor(vtkLookupTable* lut, vtkIdType entry, const std::array<double, 3>& expected) {
  double rgba[4];
  lut->GetTableValue(entry, rgba);
  return nearColor(rgba, expected);
}

bool checkColormaps(MeshRenderer& renderer) {
  // The default maps keep vv's old table: vtkLookupTable's own ramp over a hue range.
  const double range[2] = {0.0, 1.0};
  for (const auto& [id, hueMax] : {std::pair<std::string_view, double>{"rainbow", 0.8},
                                   std::pair<std::string_view, double>{"hue wheel", 1.0}}) {
    vtkNew<vtkLookupTable> old;
    old->SetHueRange(0.0, hueMax);
    old->Build();
    const auto lut = createColormapLookupTable({findColormap(id), false}, range);
    for (vtkIdType entry = 0; entry < old->GetNumberOfTableValues(); ++entry) {
      double rgba[4];
      old->GetTableValue(entry, rgba);
      if (!sameColor(lut, entry, {rgba[0], rgba[1], rgba[2]})) {
        std::cerr << id << " differs from the HSV table at entry " << entry << '\n';
        return false;
      }
    }
  }

  // More distinct values than a categorical field may have.
  constexpr int kValues = 32;
  vtkNew<vtkPoints> points;
  for (int i = 0; i < kValues; ++i) {
    points->InsertNextPoint(i, 0.0, 0.0);
  }
  vtkNew<vtkPolyData> mesh;
  mesh->SetPoints(points);
  for (const char* name : {"lat", "other", "phase"}) {
    vtkNew<vtkFloatArray> field;
    field->SetName(name);
    for (int i = 0; i < kValues; ++i) {
      field->InsertNextValue(static_cast<float>(i));
    }
    mesh->GetPointData()->AddArray(field);
  }
  renderer.setup({mesh}, {{"ramp", {0}}}, {{0.0, 0.5, 1.0}}, PaneMode::Single);

  const Colormap& rainbow = defaultColormap(ColormapGroup::Sequential);
  const Colormap& viridis = *findColormap("viridis");
  const size_t phase = show(renderer, {"phase", FieldAssociation::Point});
  if (renderer.fields()[phase].analysis.colormap.map->id != "hue wheel") {
    std::cerr << "A phase field does not start on the hue wheel\n";
    return false;
  }
  const size_t lat = show(renderer, {"lat", FieldAssociation::Point});
  vtkMapper* mapper = renderer.context.actors.front()->GetMapper();
  if (!sameColor(lutOf(renderer, lat), 255, rainbow.stops.back()) ||
      !renderer.setColormap(lat, "viridis", true) ||
      !sameColor(lutOf(renderer, lat), 0, viridis.stops.back()) ||
      !sameColor(lutOf(renderer, lat), 255, viridis.stops.front()) ||
      mapper->GetLookupTable() != lutOf(renderer, lat)) {
    std::cerr << "Choosing a colormap did not recolor the field\n";
    return false;
  }
  const size_t other = show(renderer, {"other", FieldAssociation::Point});
  if (!sameColor(lutOf(renderer, other), 255, rainbow.stops.back()) ||
      mapper->GetLookupTable() != lutOf(renderer, other)) {
    std::cerr << "A colormap chosen for one field colors another\n";
    return false;
  }
  show(renderer, {"lat", FieldAssociation::Point});
  if (mapper->GetLookupTable() != lutOf(renderer, lat) ||
      !renderer.fields()[lat].analysis.colormap.reversed) {
    std::cerr << "A field lost its colormap after cycling away and back\n";
    return false;
  }

  // The clip window is 0..31, so -1 and 32 fall outside it.
  const auto grayAt = [&renderer](size_t field, double value) {
    double rgb[3];
    lutOf(renderer, field)->GetColor(value, rgb);
    return nearColor(rgb, kOutOfRangeGray);
  };
  if (!renderer.setOutOfRange(lat, OutOfRange::Gray) || !grayAt(lat, -1.0) || !grayAt(lat, 32.0) ||
      grayAt(other, -1.0)) {
    std::cerr << "Gray did not color values outside one field's clip window only\n";
    return false;
  }
  // The window narrows inside the range and stays with the field.
  if (!renderer.setClipRange(lat, 5.0, 99.0) || renderer.setClipRange(lat, 40.0, 50.0) ||
      renderer.setClipRange(renderer.fields().size(), 0.0, 1.0)) {
    std::cerr << "A clip window was not clamped to the field's range\n";
    return false;
  }
  show(renderer, {"other", FieldAssociation::Point});
  show(renderer, {"lat", FieldAssociation::Point});
  const double* clip = renderer.fields()[lat].clipRange;
  if (clip[0] != 5.0 || clip[1] != 31.0 || mapper->GetScalarRange()[0] != 5.0 ||
      !grayAt(lat, 4.0) || !renderer.fields()[lat].analysis.colormap.reversed) {
    std::cerr << "A field lost its clip window or colors after cycling away and back\n";
    return false;
  }
  return true;
}

bool depthPeeling(MeshRenderer& renderer, bool on) {
  return std::all_of(renderer.tiles().begin(), renderer.tiles().end(), [on](const Tile& tile) {
    return (tile.renderer->GetUseDepthPeeling() != 0) == on;
  });
}

bool glassShaded(vtkActor* actor) {
  vtkShaderProperty* shader = actor->GetShaderProperty();
  for (vtkIdType i = 0; i < shader->GetNumberOfShaderReplacements(); ++i) {
    if (shader->GetNthShaderReplacementTypeAsString(i) == "Fragment") {
      return true;
    }
  }
  return false;
}

bool checkGlassInteraction(MeshRenderer& renderer, vtkRenderWindowInteractor* interactor) {
  auto* style = vtkInteractorStyle::SafeDownCast(interactor->GetInteractorStyle());
  if (!style) {
    return false;
  }
  const auto peelsAre = [&renderer](int expected) {
    for (const Tile& tile : renderer.tiles()) {
      tile.renderer->InvokeEvent(vtkCommand::StartEvent);
      if (tile.renderer->GetMaximumNumberOfPeels() != expected) {
        return false;
      }
    }
    return true;
  };

  for (int state : {VTKIS_ROTATE, VTKIS_PAN, VTKIS_DOLLY, VTKIS_SPIN}) {
    style->StartState(state);
    const bool interactive = peelsAre(2);
    style->StopState();
    if (!interactive || !peelsAre(4)) {
      std::cerr << "Glass must use fewer peels during motion and restore them on release\n";
      return false;
    }
  }

  style->StartRotate();
  renderer.setSurfaceLook({SurfaceStyle::XRay, 0.4, 0.5});
  const bool xrayUnchanged = peelsAre(4);
  renderer.setSurfaceLook({SurfaceStyle::Glass, 0.4, 0.5});
  const bool glassResumed = peelsAre(2);
  style->StopState();
  if (!xrayUnchanged || !glassResumed || !peelsAre(4)) {
    std::cerr << "Changing styles during motion did not update the peeling budget\n";
    return false;
  }
  return true;
}

// Run after checkColormaps, on its scene with fields lat and other.
bool checkSurfaceLook(MeshRenderer& renderer, vtkRenderWindowInteractor* interactor) {
  const auto actorsAre = [&renderer](int representation, bool edges, double opacity) {
    for (const auto& actor : renderer.context.actors) {
      vtkProperty* property = actor->GetProperty();
      if (property->GetRepresentation() != representation ||
          (property->GetEdgeVisibility() != 0) != edges ||
          std::abs(property->GetOpacity() - opacity) > 1e-12) {
        return false;
      }
    }
    return !renderer.context.actors.empty();
  };
  const auto peeling = [&renderer](bool on) { return depthPeeling(renderer, on); };

  const auto anyGlassShaded = [&renderer]() {
    return std::any_of(renderer.context.actors.begin(),
                       renderer.context.actors.end(),
                       [](const auto& actor) { return glassShaded(actor); });
  };

  renderer.setSurfaceLook({SurfaceStyle::SolidWithEdges, 0.35, 0.25});
  if (!actorsAre(VTK_SURFACE, true, 1.0) || !peeling(false)) {
    std::cerr << "Solid with edges is not a surface with edges\n";
    return false;
  }
  renderer.setSurfaceLook({SurfaceStyle::Wireframe, 0.35, 0.25});
  if (!actorsAre(VTK_WIREFRAME, false, 1.0)) {
    std::cerr << "Wireframe is not a wireframe\n";
    return false;
  }
  renderer.setSurfaceLook({SurfaceStyle::XRay, 0.4, 0.5});
  show(renderer, {"lat", FieldAssociation::Point});
  if (!actorsAre(VTK_SURFACE, false, 0.4) || !peeling(true) || anyGlassShaded()) {
    std::cerr << "X-ray is not a plain depth-peeled surface at its opacity after cycling fields\n";
    return false;
  }
  renderer.setSurfaceLook({SurfaceStyle::Glass, 0.4, 0.5});
  show(renderer, {"other", FieldAssociation::Point});
  if (!actorsAre(VTK_SURFACE, false, 0.5) || !peeling(true) || !anyGlassShaded()) {
    std::cerr << "Glass is not a depth-peeled glass surface at its density after cycling fields\n";
    return false;
  }
  if (!checkGlassInteraction(renderer, interactor)) {
    return false;
  }
  renderer.setSurfaceLook({SurfaceStyle::XRay, 0.4, 0.5});
  if (!actorsAre(VTK_SURFACE, false, 0.4) || !peeling(true) || anyGlassShaded()) {
    std::cerr << "X-ray straight after Glass kept the glass shading or density\n";
    return false;
  }

  // vtkInteractorStyle's own w key would switch to wireframe behind the Style panel.
  renderer.setSurfaceLook({SurfaceStyle::Solid, 0.4, 0.5});
  interactor->SetKeyEventInformation(0, 0, 'w', 0, "w");
  interactor->InvokeEvent(vtkCommand::CharEvent, nullptr);
  if (!actorsAre(VTK_SURFACE, false, 1.0) || !peeling(false)) {
    std::cerr << "A VTK key changed the surface style\n";
    return false;
  }

  // A scene set up later, here a split pane, takes the look in force.
  renderer.setSurfaceLook({SurfaceStyle::Glass, 0.4, 0.5});
  vtkSmartPointer<vtkDataSet> mesh = renderer.getPrimaryMesh();
  renderer.setup({mesh}, {{"ramp", {0}}}, {{0.0, 0.5, 1.0}}, PaneMode::Split);
  const bool everyActorGlass = std::all_of(renderer.context.actors.begin(),
                                           renderer.context.actors.end(),
                                           [](const auto& actor) { return glassShaded(actor); });
  if (!actorsAre(VTK_SURFACE, false, 0.5) || !peeling(true) || !everyActorGlass) {
    std::cerr << "The split pane did not take the surface style\n";
    return false;
  }
  return checkGlassInteraction(renderer, interactor);
}

bool checkHistogram() {
  vtkNew<vtkPolyData> mesh;
  vtkNew<vtkFloatArray> field;
  field->SetName("lat");
  for (int i = 0; i < 32; ++i) {
    field->InsertNextValue(static_cast<float>(i));
  }
  mesh->GetPointData()->AddArray(field);
  const std::vector<vtkDataSet*> meshes{mesh, mesh};

  // Two copies of 0..31: the maximum lands in the last bin, and values past the
  // range in none.
  const double whole[2] = {0.0, 31.0};
  const double part[2] = {0.0, 15.0};
  if (computeScalarHistogram(meshes, "lat", FieldAssociation::Point, whole, 4) !=
          std::vector<size_t>{16, 16, 16, 16} ||
      computeScalarHistogram(meshes, "lat", FieldAssociation::Point, part, 3) !=
          std::vector<size_t>{10, 10, 12}) {
    std::cerr << "Unexpected histogram counts\n";
    return false;
  }
  return true;
}

// The range and histogram of a field holding infinities and NaN cover its finite values.
bool checkNonFiniteValues() {
  constexpr double kInfinity = std::numeric_limits<double>::infinity();
  vtkNew<vtkPolyData> mesh;
  vtkNew<vtkFloatArray> field;
  field->SetName("lat");
  for (int i = 0; i < 32; ++i) {
    field->InsertNextValue(static_cast<float>(i));
  }
  for (const double value : {kInfinity, -kInfinity, std::numeric_limits<double>::quiet_NaN()}) {
    field->InsertNextValue(static_cast<float>(value));
  }
  mesh->GetPointData()->AddArray(field);
  const std::vector<vtkDataSet*> meshes{mesh};

  double range[2] = {0.0, 0.0};
  if (!computeScalarGlobalRange(meshes, "lat", FieldAssociation::Point, range) || range[0] != 0.0 ||
      range[1] != 31.0) {
    std::cerr << "The range of a field with infinities is not that of its finite values\n";
    return false;
  }
  const double infinite[2] = {-kInfinity, kInfinity};
  if (computeScalarHistogram(meshes, "lat", FieldAssociation::Point, range, 4) !=
          std::vector<size_t>{8, 8, 8, 8} ||
      computeScalarHistogram(meshes, "lat", FieldAssociation::Point, infinite, 4) !=
          std::vector<size_t>(4, 0)) {
    std::cerr << "A histogram counted values that are not finite\n";
    return false;
  }
  return true;
}

// The display point of world point (x, y, 0) in panel.
std::array<double, 2> displayPoint(vtkRenderer* panel, double x, double y) {
  panel->SetWorldPoint(x, y, 0.0, 1.0);
  panel->WorldToDisplay();
  const double* display = panel->GetDisplayPoint();
  return {display[0], display[1]};
}

// A unit square of 4 x 4 quads in z = 0, cell 6 spanning [0.5, 0.75] x [0.25, 0.5],
// with a linear point field lat, which interpolation reproduces exactly anywhere, and
// a cell field region.
vtkSmartPointer<vtkPolyData> probeSquare() {
  constexpr int kSide = 4;
  vtkNew<vtkPoints> points;
  vtkNew<vtkFloatArray> lat;
  lat->SetName("lat");
  for (int row = 0; row <= kSide; ++row) {
    for (int col = 0; col <= kSide; ++col) {
      const double x = col / static_cast<double>(kSide);
      const double y = row / static_cast<double>(kSide);
      points->InsertNextPoint(x, y, 0.0);
      lat->InsertNextValue(static_cast<float>(10.0 * x + 100.0 * y));
    }
  }
  vtkNew<vtkCellArray> quads;
  vtkNew<vtkFloatArray> region;
  region->SetName("region");
  for (vtkIdType row = 0; row < kSide; ++row) {
    for (vtkIdType col = 0; col < kSide; ++col) {
      const vtkIdType corner = row * (kSide + 1) + col;
      const vtkIdType quad[4] = {corner, corner + 1, corner + kSide + 2, corner + kSide + 1};
      const vtkIdType cell = quads->InsertNextCell(4, quad);
      region->InsertNextValue(static_cast<float>(1.5 * static_cast<double>(cell) + 0.25));
    }
  }
  auto mesh = vtkSmartPointer<vtkPolyData>::New();
  mesh->SetPoints(points);
  mesh->SetPolys(quads);
  mesh->GetPointData()->AddArray(lat);
  mesh->GetCellData()->AddArray(region);
  return mesh;
}

// The probe in panel at (0.6, 0.3), inside cell 6 of probeSquare.
std::optional<ProbeHit> probeSquareAt(MeshRenderer& renderer, vtkRenderer* panel) {
  const std::array<double, 2> display = displayPoint(panel, 0.6, 0.3);
  return renderer.probe(display[0], display[1]);
}

bool readsCell6(const std::optional<ProbeHit>& hit, const char* field, double value) {
  return hit && hit->cell == 6 && hit->field == field && std::abs(hit->value - value) < 1e-4;
}

bool checkProbe(MeshRenderer& renderer) {
  const vtkSmartPointer<vtkPolyData> mesh = probeSquare();
  renderer.context.window->SetSize(800, 400);
  renderer.setup({mesh}, {{"square", {0}}}, {{0.0, 0.5, 1.0}}, PaneMode::Single);

  renderer.showField(0, -1);
  std::optional<ProbeHit> hit = probeSquareAt(renderer, renderer.getRenderer());
  if (!hit || hit->cell != 6 || !hit->field.empty()) {
    std::cerr << "Probing the plain mesh did not find cell 6 alone\n";
    return false;
  }
  show(renderer, {"lat", FieldAssociation::Point});
  if (!readsCell6(probeSquareAt(renderer, renderer.getRenderer()), "lat", 36.0)) {
    std::cerr << "Probing a point field did not interpolate it in the picked cell\n";
    return false;
  }
  show(renderer, {"region", FieldAssociation::Cell});
  hit = probeSquareAt(renderer, renderer.getRenderer());
  if (!readsCell6(hit, "region", 9.25) || !hit->category || hit->category->label != "9.25") {
    std::cerr << "Probing a cell field did not give the cell's category\n";
    return false;
  }
  if (renderer.probe(1.0, 1.0)) {
    std::cerr << "Probing beside the mesh found a cell\n";
    return false;
  }

  // Each file's pane reads its own mesh and the field it shows, though the shared camera
  // lines the meshes up.
  renderer.setup({mesh, mesh},
                 {{"first", {0}}, {"second", {1}}},
                 {{0.0, 0.5, 1.0}, {0.0, 0.5, 1.0}},
                 PaneMode::Single);
  renderer.showField(1, 1);
  vtkRenderer* first = renderer.getRenderer();
  vtkRenderer* second = renderer.tiles()[renderer.panes()[1].firstTile].renderer;
  hit = probeSquareAt(renderer, second);
  if (!readsCell6(hit, "region", 9.25) || hit->mesh != 1 ||
      !readsCell6(probeSquareAt(renderer, first), "lat", 36.0)) {
    std::cerr << "A pane did not probe its own mesh and field\n";
    return false;
  }
  const std::array<double, 2> inSecond = displayPoint(second, 0.6, 0.3);
  if (renderer.paneAt(inSecond[0], inSecond[1]) != std::optional<size_t>(1) ||
      renderer.paneAt(-1.0, -1.0)) {
    std::cerr << "The pane under a point is wrong\n";
    return false;
  }

  // Each tile of a split pane reads its own field.
  renderer.setup({mesh}, {{"square", {0}}}, {{0.0, 0.5, 1.0}}, PaneMode::Split);
  if (shownTiles(renderer) != std::vector<size_t>{0, 1} ||
      !readsCell6(probeSquareAt(renderer, renderer.tiles()[0].renderer), "lat", 36.0) ||
      !readsCell6(probeSquareAt(renderer, renderer.tiles()[1].renderer), "region", 9.25)) {
    std::cerr << "A tile did not probe its own field\n";
    return false;
  }
  return true;
}

bool sameViewport(const double* a, const double* b) {
  return std::equal(a, a + 4, b, [](double x, double y) { return std::abs(x - y) < 1e-12; });
}

// A split pane pages its fields through the same tiles, above the pager's strip; fields
// keep their colors and clip range off the page, and the camera stays where it was. Each
// pane keeps its own mode and field unless the panes are linked.
bool checkSplitPages(MeshRenderer& renderer) {
  const vtkSmartPointer<vtkPolyData> mesh = probeSquare();
  vtkDataArray* lat = mesh->GetPointData()->GetArray("lat");
  for (const char* name : {"f1", "f2", "f3"}) {
    vtkNew<vtkFloatArray> field;
    field->DeepCopy(lat);
    field->SetName(name);
    mesh->GetPointData()->AddArray(field);
  }
  // Two tiles of at least 280 x 220 fit above the strip, so f1, f2, f3, lat and the cell
  // field region page as two, two and one.
  renderer.context.window->SetSize(600, 400);
  LayoutMetrics metrics;
  metrics.pagerStrip = 60;
  renderer.setLayoutMetrics(metrics);
  renderer.setup({mesh}, {{"square", {0}}}, {{0.0, 0.5, 1.0}}, PaneMode::Split);
  const Pane& pane = renderer.panes().front();
  const std::vector<Tile>& tiles = renderer.tiles();
  const auto names = [&renderer, &tiles]() {
    std::vector<std::string> shown;
    for (const size_t tile : shownTiles(renderer)) {
      if (tiles[tile].field) {
        shown.push_back(renderer.fields()[*tiles[tile].field].field.name);
      }
    }
    return shown;
  };
  double firstCell[4];
  double secondCell[4];
  std::copy_n(tiles[0].viewport, 4, firstCell);
  std::copy_n(tiles[1].viewport, 4, secondCell);
  if (pane.pageCount() != 3 || pane.pageSize != 2 || tiles.size() != 5 ||
      names() != std::vector<std::string>{"f1", "f2"} || firstCell[1] < 0.15 - 1e-12 ||
      secondCell[1] < 0.15 - 1e-12) {
    std::cerr << "The first page did not show the first two fields above the strip\n";
    return false;
  }

  auto* firstRenderer = renderer.getRenderer();
  auto* camera = firstRenderer ? firstRenderer->GetActiveCamera() : nullptr;
  if (!camera) {
    std::cerr << "The first tile has no active camera\n";
    return false;
  }
  camera->Azimuth(30.0);
  const vtkMTimeType cameraTime = camera->GetMTime();
  renderer.stepPane(0, 1);
  if (names() != std::vector<std::string>{"f3", "lat"} ||
      !sameViewport(tiles[0].viewport, firstCell) || !sameViewport(tiles[1].viewport, secondCell) ||
      camera->GetMTime() != cameraTime) {
    std::cerr << "The second page did not show the next two fields in the same cells\n";
    return false;
  }
  camera->Azimuth(-30.0);
  const std::optional<ProbeHit> hit = probeSquareAt(renderer, tiles[0].renderer);
  if (!hit || hit->field != "f3") {
    std::cerr << "The probe did not read the field shown in the tile\n";
    return false;
  }

  const size_t f3 = renderer.findField({"f3", FieldAssociation::Point}).value();
  const size_t region = renderer.findField({"region", FieldAssociation::Cell}).value();
  if (!renderer.setColormap(f3, "viridis", true) || renderer.setColormap(f3, "missing", false) ||
      renderer.setColormap(999, "viridis", false) ||
      renderer.setColormap(region, "viridis", false) || !renderer.setClipRange(f3, 20.0, 60.0)) {
    std::cerr << "Colormap selection accepted invalid input or rejected a continuous field\n";
    return false;
  }
  renderer.stepPane(0, -1);
  renderer.stepPane(0, 1);
  const SceneField& colored = renderer.fields()[f3];
  const double* lutRange = colored.lut->GetRange();
  if (colored.analysis.colormap.map != findColormap("viridis") ||
      !colored.analysis.colormap.reversed || colored.clipRange[0] != 20.0 ||
      colored.clipRange[1] != 60.0 || lutRange[0] != 20.0 || lutRange[1] != 60.0 ||
      renderer.context.actors[tiles[0].firstActor]->GetMapper()->GetLookupTable() != colored.lut ||
      renderer.fields()[f3 + 1].analysis.colormap.map == findColormap("viridis")) {
    std::cerr << "A field's colors or clip range did not survive a page round trip\n";
    return false;
  }

  // The last page's one field leaves the second tile undrawn; the next step wraps.
  renderer.stepPane(0, 1);
  if (names() != std::vector<std::string>{"region"} || tiles[1].renderer->GetDraw() ||
      tiles[1].renderer->GetInteractive()) {
    std::cerr << "The last page drew a tile without a field\n";
    return false;
  }
  renderer.stepPane(0, 1);
  if (names() != std::vector<std::string>{"f1", "f2"}) {
    std::cerr << "Paging did not wrap\n";
    return false;
  }

  // Single shows the page's first field over the whole pane, and steps through plain
  // geometry too; Split takes up the page holding the field shown.
  renderer.showField(0, 3);
  renderer.setPaneMode(0, PaneMode::Single);
  if (names() != std::vector<std::string>{"f3"} ||
      !sameViewport(tiles[0].viewport, pane.viewport) || pane.pageCount() != 6) {
    std::cerr << "A single pane did not show one field over the whole pane\n";
    return false;
  }
  renderer.stepPane(0, -3);
  if (pane.first != -1 || !names().empty() || shownTiles(renderer) != std::vector<size_t>{0}) {
    std::cerr << "A single pane did not step back to plain geometry\n";
    return false;
  }
  renderer.showField(0, 3);
  renderer.setPaneMode(0, PaneMode::Split);
  if (names() != std::vector<std::string>{"f3", "lat"}) {
    std::cerr << "Splitting did not show the page holding the field\n";
    return false;
  }

  // Two files: one split, the other single, each on its own field.
  renderer.context.window->SetSize(1200, 400);
  renderer.setup(
      {mesh, mesh}, {{"a", {0}}, {"b", {1}}}, {{0.0, 0.5, 1.0}, {0.0, 0.5, 1.0}}, PaneMode::Single);
  renderer.setPaneMode(0, PaneMode::Split);
  renderer.showField(1, 4);
  const std::vector<Pane>& panes = renderer.panes();
  if (panes[0].mode != PaneMode::Split || panes[1].mode != PaneMode::Single ||
      names() != std::vector<std::string>{"f1", "f2", "region"} || panes[0].viewport[2] != 0.5 ||
      panes[1].viewport[0] != 0.5) {
    std::cerr << "Panes did not keep their own mode and field\n";
    return false;
  }
  // A field's colors are the same in every pane that shows it.
  renderer.showField(1, 0);
  const size_t f1 = renderer.findField({"f1", FieldAssociation::Point}).value();
  if (!renderer.setColormap(f1, "magma", true)) {
    return false;
  }
  for (const size_t tile : {panes[0].firstTile, panes[1].firstTile}) {
    if (renderer.context.actors[tiles[tile].firstActor]->GetMapper()->GetLookupTable() !=
        renderer.fields()[f1].lut) {
      std::cerr << "A colormap did not follow its field across panes\n";
      return false;
    }
  }
  // Linked, the other pane follows to the page holding the field, or to plain geometry
  // when it shows one field.
  renderer.setFieldsLinked(true);
  renderer.showField(1, 3);
  if (names() != std::vector<std::string>{"f3", "lat", "lat"}) {
    std::cerr << "A linked pane did not follow to the field\n";
    return false;
  }
  renderer.showField(1, -1);
  renderer.stepPane(0, 1);
  if (names() != std::vector<std::string>{"region", "region"}) {
    std::cerr << "A linked pane did not follow a page step\n";
    return false;
  }
  renderer.setFieldsLinked(false);
  renderer.setLayoutMetrics({});
  return true;
}

// A pinned hit reads the field shown now at the same point, carries a marker that nothing
// picks, and maps back to where it was picked.
bool checkPin(MeshRenderer& renderer) {
  const vtkSmartPointer<vtkPolyData> mesh = probeSquare();
  renderer.context.window->SetSize(400, 400);
  renderer.setup({mesh}, {{"square", {0}}}, {{0.0, 0.5, 1.0}}, PaneMode::Single);
  show(renderer, {"lat", FieldAssociation::Point});
  vtkRenderer* panel = renderer.getRenderer();
  const std::optional<ProbeHit> pin = probeSquareAt(renderer, panel);
  if (!readsCell6(pin, "lat", 36.0) || std::abs(pin->position[0] - 0.6) > 1e-3 ||
      std::abs(pin->position[1] - 0.3) > 1e-3) {
    std::cerr << "A probe hit did not give where it lies\n";
    return false;
  }
  show(renderer, {"region", FieldAssociation::Cell});
  if (!readsCell6(renderer.probeAgain(*pin), "region", 9.25)) {
    std::cerr << "A pinned hit did not read the field shown now\n";
    return false;
  }
  const int actors = panel->GetActors()->GetNumberOfItems();
  renderer.setPinMarker(pin);
  vtkActor* marker = vtkActor::SafeDownCast(panel->GetActors()->GetLastProp());
  if (panel->GetActors()->GetNumberOfItems() != actors + 1 || !marker ||
      marker->GetPickable() != 0 || !readsCell6(probeSquareAt(renderer, panel), "region", 9.25)) {
    std::cerr << "The pin marker is missing or in the way of the probe\n";
    return false;
  }
  renderer.setPartVisible(0, false);
  if (marker->GetVisibility() || renderer.probeAgain(*pin)) {
    std::cerr << "Hiding a mesh left its pin visible or readable\n";
    return false;
  }
  renderer.setPartVisible(0, true);
  if (!marker->GetVisibility() || !renderer.probeAgain(*pin)) {
    std::cerr << "Showing a mesh did not restore its retained pin\n";
    return false;
  }
  // A zoom moves the pin on screen.
  const std::array<double, 2> picked = displayPoint(panel, 0.6, 0.3);
  panel->GetActiveCamera()->Dolly(1.5);
  const std::optional<std::array<double, 2>> shown = renderer.displayPosition(*pin);
  const std::array<double, 2> turned = displayPoint(panel, 0.6, 0.3);
  if (!shown || std::abs((*shown)[0] - turned[0]) > 0.5 ||
      std::abs((*shown)[1] - turned[1]) > 0.5 || std::abs(turned[0] - picked[0]) < 5.0) {
    std::cerr << "A pinned hit did not follow the camera on screen\n";
    return false;
  }
  renderer.setPinMarker(std::nullopt);
  if (panel->GetActors()->GetNumberOfItems() != actors) {
    std::cerr << "Unpinning left the marker\n";
    return false;
  }
  return true;
}

// probeSquare's fields over 9 frames, read like the XDMF source: only its one active array,
// into whatever dataset it is given, created there when missing. lat grows by 10 per
// frame, drift is lat times minus the frame, region grows by 1 per frame and frame 5
// cannot be read.
class SquareSeries : public TemporalSource {
public:
  explicit SquareSeries(vtkSmartPointer<vtkPolyData> mesh) : mesh_(std::move(mesh)) {
    for (int step = 0; step < 9; ++step) {
      timeValues_.push_back(step * 0.5);
    }
  }

  bool readStepInto(int step, vtkDataSet* target) override {
    const bool cell = active_ == "region";
    vtkDataArray* source = arrayForAssociation(
        mesh_, cell ? "region" : "lat", cell ? FieldAssociation::Cell : FieldAssociation::Point);
    if (!target || step < 0 || step >= steps() || step == 5 ||
        (active_ != "lat" && active_ != "drift" && !cell)) {
      return false;
    }
    vtkDataSetAttributes* data = cell ? static_cast<vtkDataSetAttributes*>(target->GetCellData())
                                      : static_cast<vtkDataSetAttributes*>(target->GetPointData());
    vtkNew<vtkFloatArray> values;
    values->SetName(active_.c_str());
    for (vtkIdType i = 0; i < source->GetNumberOfTuples(); ++i) {
      const double base = source->GetComponent(i, 0);
      values->InsertNextValue(static_cast<float>(
          active_ == "drift" ? -base * step : base + (cell ? 1.0 : 10.0) * step));
    }
    data->AddArray(values);
    return true;
  }

  bool
  sampledScalarRange(const ScalarField& /*field*/, double /*out*/[2], int /*maxSamples*/) override {
    return false;
  }

  void setActiveArrays(const std::vector<std::string>& scalarNames) override {
    active_ = scalarNames.empty() ? std::string() : scalarNames.front();
  }

private:
  vtkSmartPointer<vtkPolyData> mesh_;
  std::string active_;
};

bool sameTrace(const ProbeTrace& trace, const std::vector<std::optional<double>>& expected) {
  const std::vector<std::optional<double>>& values = trace.values();
  if (values.size() != expected.size()) {
    return false;
  }
  for (size_t frame = 0; frame < values.size(); ++frame) {
    const bool same =
        values[frame].has_value() == expected[frame].has_value() &&
        (!values[frame] || (std::isnan(*values[frame]) && std::isnan(*expected[frame])) ||
         std::abs(*values[frame] - *expected[frame]) < 1e-4);
    if (!same) {
      return false;
    }
  }
  return true;
}

// The point (0.6, 0.3) of probeSquare, inside cell 6, traced through SquareSeries.
bool checkTrace() {
  ProbeTrace order;
  order.reset(10);
  std::vector<int> visited;
  for (std::optional<int> frame = order.next(); frame; frame = order.next()) {
    visited.push_back(*frame);
    order.fill(0.0);
  }
  if (visited != std::vector<int>{0, 9, 8, 4, 2, 6, 1, 3, 5, 7}) {
    std::cerr << "A trace did not visit every frame once, coarse to fine\n";
    return false;
  }

  const vtkSmartPointer<vtkPolyData> mesh = probeSquare();
  const double pcoords[3] = {0.4, 0.2, 0.0};
  const std::optional<CellPoint> at = cellPointAtPick(mesh, 6, 0, pcoords);
  SquareSeries series(mesh);
  vtkNew<vtkPolyData> frame;
  const ScalarField lat{"lat", FieldAssociation::Point};
  const auto readSome =
      [&](const ScalarField& field, ProbeTrace& trace, std::chrono::steady_clock::duration budget) {
        return readTraceFrames(series, frame, *at, field, ScalarAnalysis{}, trace, budget);
      };
  if (!at) {
    std::cerr << "No point to trace in cell 6\n";
    return false;
  }
  series.setActiveArrays({"lat"});
  ProbeTrace trace;
  trace.reset(9);
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const std::optional<double> unread;
  // No time to spare still reads a frame each turn, the ends first.
  if (readSome(lat, trace, std::chrono::steady_clock::duration::zero()) != 1 ||
      !sameTrace(trace, {36.0, unread, unread, unread, unread, unread, unread, unread, unread}) ||
      readSome(lat, trace, std::chrono::steady_clock::duration::zero()) != 1 ||
      !sameTrace(trace, {36.0, unread, unread, unread, unread, unread, unread, unread, 116.0})) {
    std::cerr << "A trace did not fill a frame per turn, ends first\n";
    return false;
  }
  readSome(lat, trace, std::chrono::hours(1));
  if (!trace.complete() ||
      !sameTrace(trace, {36.0, 46.0, 56.0, 66.0, 76.0, nan, 96.0, 106.0, 116.0})) {
    std::cerr << "A trace did not read every frame at the point, NaN where one is missing\n";
    return false;
  }
  const std::optional<double> shown = fieldValueAt(mesh, lat, ScalarAnalysis{}, *at);
  if (!shown || std::abs(*shown - 36.0) > 1e-4) {
    std::cerr << "Tracing changed the mesh it was pinned on\n";
    return false;
  }

  // Another field at the same point: the window streams it, then traces it afresh.
  series.setActiveArrays({"drift"});
  trace.reset(9);
  readSome({"drift", FieldAssociation::Point}, trace, std::chrono::hours(1));
  if (!sameTrace(trace, {0.0, -36.0, -72.0, -108.0, -144.0, nan, -216.0, -252.0, -288.0})) {
    std::cerr << "A trace did not follow the field to the same point\n";
    return false;
  }
  series.setActiveArrays({"region"});
  trace.reset(9);
  readSome({"region", FieldAssociation::Cell}, trace, std::chrono::hours(1));
  if (!sameTrace(trace, {9.25, 10.25, 11.25, 12.25, 13.25, nan, 15.25, 16.25, 17.25})) {
    std::cerr << "A trace of a cell field did not read the pinned cell\n";
    return false;
  }
  return true;
}

// Glass on a mesh with open borders: the fragment shader over smooth normals that keep
// the cell ids, an outline of the borders that nothing picks, and the plain surface
// back on leaving.
bool checkGlass(MeshRenderer& renderer) {
  const vtkSmartPointer<vtkPolyData> mesh = probeSquare();
  // A 45 degree crease beside cell 6: normals split there would add points.
  for (int row = 0; row <= 4; ++row) {
    mesh->GetPoints()->SetPoint(row * 5 + 4, 1.0, row / 4.0, 0.25);
  }
  renderer.setSurfaceLook({SurfaceStyle::Solid, 0.6, 0.3});
  renderer.setup({mesh}, {{"square", {0}}}, {{0.0, 0.5, 1.0}}, PaneMode::Single);
  show(renderer, {"lat", FieldAssociation::Point});
  vtkRenderer* panel = renderer.getRenderer();
  const auto outline = [&renderer, panel]() -> vtkActor* {
    vtkActorCollection* actors = panel->GetActors();
    actors->InitTraversal();
    for (vtkActor* actor = actors->GetNextActor(); actor; actor = actors->GetNextActor()) {
      if (std::none_of(renderer.context.actors.begin(),
                       renderer.context.actors.end(),
                       [actor](const auto& meshActor) { return meshActor.Get() == actor; })) {
        return actor;
      }
    }
    return nullptr;
  };
  vtkActor* actor = renderer.context.actors.front();
  vtkMapper* mapper = actor->GetMapper();
  if (!readsCell6(probeSquareAt(renderer, panel), "lat", 36.0)) {
    std::cerr << "Probing the solid square did not read cell 6\n";
    return false;
  }

  renderer.setSurfaceLook({SurfaceStyle::Glass, 0.6, 0.3});
  if (!glassShaded(actor) || std::abs(actor->GetProperty()->GetOpacity() - 0.3) > 1e-12 ||
      !depthPeeling(renderer, true)) {
    std::cerr << "Glass is not a depth-peeled, glass-shaded surface at its density\n";
    return false;
  }
  mapper->Update();
  vtkDataSet* drawn = mapper->GetInput();
  if (!drawn || drawn == mesh.Get() || !drawn->GetPointData()->GetNormals() ||
      drawn->GetNumberOfPoints() != mesh->GetNumberOfPoints() ||
      drawn->GetNumberOfCells() != mesh->GetNumberOfCells()) {
    std::cerr << "Glass did not draw the mesh's own points and cells with point normals\n";
    return false;
  }
  if (!readsCell6(probeSquareAt(renderer, panel), "lat", 36.0)) {
    std::cerr << "Probing through Glass did not read the same cell\n";
    return false;
  }
  vtkActor* border = outline();
  if (!border || border->GetVisibility() == 0 || border->GetPickable() != 0) {
    std::cerr << "Glass did not outline the borders with an actor nothing picks\n";
    return false;
  }
  border->GetMapper()->Update();
  if (border->GetMapper()->GetInput()->GetNumberOfCells() != 16) {
    std::cerr << "The outline does not follow the square's 16 border edges\n";
    return false;
  }
  renderer.setPartVisible(0, false);
  if (border->GetVisibility() != 0) {
    std::cerr << "A hidden part kept its outline\n";
    return false;
  }
  renderer.setPartVisible(0, true);

  renderer.setSurfaceLook({SurfaceStyle::XRay, 0.6, 0.3});
  if (glassShaded(actor) || mapper->GetInputDataObject(0, 0) != mesh.Get() ||
      border->GetVisibility() != 0 || std::abs(actor->GetProperty()->GetOpacity() - 0.6) > 1e-12 ||
      !depthPeeling(renderer, true)) {
    std::cerr << "X-ray after Glass did not draw the plain mesh at its own opacity\n";
    return false;
  }

  renderer.setSurfaceLook({SurfaceStyle::Solid, 0.6, 0.3});
  if (glassShaded(actor) || mapper->GetInputDataObject(0, 0) != mesh.Get() ||
      border->GetVisibility() != 0 || actor->GetProperty()->GetOpacity() != 1.0 ||
      !depthPeeling(renderer, false)) {
    std::cerr << "Leaving Glass did not restore the plain surface\n";
    return false;
  }
  return true;
}

// One vertex cell per category, valued 0, 10, 20...
vtkSmartPointer<vtkPolyData> categoricalCells(const char* name, int categories) {
  vtkNew<vtkPoints> points;
  vtkNew<vtkCellArray> vertices;
  vtkNew<vtkFloatArray> field;
  field->SetName(name);
  for (int i = 0; i < categories; ++i) {
    const vtkIdType point = points->InsertNextPoint(i, 0.0, 0.0);
    vertices->InsertNextCell(1, &point);
    field->InsertNextValue(static_cast<float>(10 * i));
  }
  auto mesh = vtkSmartPointer<vtkPolyData>::New();
  mesh->SetPoints(points);
  mesh->SetVerts(vertices);
  mesh->GetCellData()->AddArray(field);
  return mesh;
}

// Each file's pane lists and shows its own fields, each drawn with its own table.
bool checkPaneFields(MeshRenderer& renderer) {
  renderer.setup({categoricalCells("zones", 3), categoricalCells("regions", 5)},
                 {{"a", {0}}, {"b", {1}}},
                 {{0.0, 0.5, 1.0}, {0.0, 0.5, 1.0}},
                 PaneMode::Single);
  const size_t zones = renderer.findField({"zones", FieldAssociation::Cell}).value();
  const size_t regions = renderer.findField({"regions", FieldAssociation::Cell}).value();
  const std::vector<Pane>& panes = renderer.panes();
  const auto tableOf = [&renderer](const Pane& pane) {
    const Tile& tile = renderer.tiles()[pane.firstTile];
    return renderer.context.actors[tile.firstActor]->GetMapper()->GetLookupTable();
  };
  if (panes[0].fields != std::vector<size_t>{zones} ||
      panes[1].fields != std::vector<size_t>{regions} ||
      tableOf(panes[0]) != lutOf(renderer, zones) ||
      tableOf(panes[1]) != lutOf(renderer, regions) ||
      lutOf(renderer, zones)->GetNumberOfAnnotatedValues() != 3 ||
      lutOf(renderer, regions)->GetAnnotatedValueIndex(40.0) != 4) {
    std::cerr << "The panes do not show their own fields side by side\n";
    return false;
  }
  // Stepping one pane leaves the other's field.
  renderer.stepPane(0, 1);
  if (panes[0].first != -1 || panes[1].first != 0 ||
      renderer.context.actors.front()->GetMapper()->GetScalarVisibility()) {
    std::cerr << "Stepping a pane changed the other, or kept its field\n";
    return false;
  }
  return true;
}

bool categoryColored(vtkLookupTable* lut, double value, const std::array<double, 3>& rgb) {
  return lut && lut->GetAnnotatedValueIndex(value) >= 0 &&
         sameColor(lut, lut->GetAnnotatedValueIndex(value), rgb);
}

bool checkCategoryNaNs(MeshRenderer& renderer) {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const auto mesh = categoricalCells("nan_zones", 6);
  auto* field = mesh->GetCellData()->GetArray("nan_zones");
  const std::array<double, 6> values = {nan, 0.0, nan, 1.0, nan, 0.0};
  for (size_t i = 0; i < values.size(); ++i) {
    field->SetComponent(static_cast<vtkIdType>(i), 0, values[i]);
  }
  mesh->GetPointData()->AddArray(field);
  renderer.setup({mesh}, {{"cells", {0}}}, {{0.0, 0.5, 1.0}}, PaneMode::Single);
  const std::array<double, 3> magenta = {1.0, 0.0, 1.0};
  const CategoryPalette& defaults = categoryPalettes().front();
  for (const int count : {0, 1, 2, 20, 21}) {
    const auto candidate = categoricalCells("limits", count + 1);
    candidate->GetCellData()->GetArray("limits")->SetComponent(count, 0, nan);
    const auto analysis = analyzeScalar({candidate}, "limits", FieldAssociation::Cell);
    if (analysis.categorical != (count >= 1 && count <= 20) ||
        (analysis.categorical &&
         (!analysis.hasNaN || analysis.uniqueValues.size() != static_cast<size_t>(count)))) {
      std::cerr << "NaN changed the finite category limit\n";
      return false;
    }
  }
  for (const auto association : {FieldAssociation::Point, FieldAssociation::Cell}) {
    const size_t zones = show(renderer, {"nan_zones", association});
    const ScalarAnalysis& analysis = renderer.fields()[zones].analysis;
    if (!analysis.categorical || !analysis.hasNaN ||
        analysis.uniqueValues != std::set<double>{0.0, 1.0} ||
        lutOf(renderer, zones)->GetNumberOfAnnotatedValues() != 2) {
      std::cerr << "NaN entered the finite category values\n";
      return false;
    }
    if (!renderer.setCategoryColor(zones, nan, magenta) ||
        !categoryColored(lutOf(renderer, zones), 0.0, defaults.colors[0]) ||
        !categoryColored(lutOf(renderer, zones), 1.0, defaults.colors[1])) {
      std::cerr << "Recoloring NaN changed a finite category\n";
      return false;
    }
    auto* mapped = renderer.context.actors.front()->GetMapper()->MapScalars(1.0);
    if (!mapped || mapped->GetComponent(0, 0) != 255.0 || mapped->GetComponent(0, 1) != 0.0 ||
        mapped->GetComponent(0, 2) != 255.0) {
      std::cerr << "The mapper did not draw NaN in its chosen color\n";
      return false;
    }
    if (!analysis.coloring.customColors.empty()) {
      std::cerr << "NaN entered the custom category map\n";
      return false;
    }
    renderer.showField(0, -1);
    show(renderer, {"nan_zones", association});
    double rgb[3];
    lutOf(renderer, zones)->GetColor(nan, rgb);
    if (!nearColor(rgb, magenta)) {
      std::cerr << "A field lost its NaN color after cycling\n";
      return false;
    }
    renderer.setCategoryPalette(zones, "okabe-ito");
    lutOf(renderer, zones)->GetColor(nan, rgb);
    if (!nearColor(rgb, magenta)) {
      std::cerr << "A palette change overwrote the NaN color\n";
      return false;
    }
    renderer.setCategoryColor(zones, nan, std::nullopt);
    lutOf(renderer, zones)->GetColor(nan, rgb);
    if (!nearColor(rgb, kCategoryNanColor)) {
      std::cerr << "Reset did not restore the NaN color\n";
      return false;
    }
  }
  return true;
}

// Default draws the colors a file's table gives a field. A palette picked for a field, or a
// category recolored, which makes its colors custom, stays with that field as fields cycle.
bool checkCategoryPalettes(MeshRenderer& renderer) {
  const vtkSmartPointer<vtkPolyData> mesh = categoricalCells("zones", 4);
  // Attached as VTK's legacy reader attaches a LOOKUP_TABLE section.
  vtkNew<vtkLookupTable> table;
  table->SetNumberOfTableValues(3);
  table->SetTableValue(0, 0.9, 0.9, 0.9, 1.0);
  table->SetTableValue(1, 0.8, 0.1, 0.5, 1.0);
  table->SetTableValue(2, 0.1, 0.8, 0.8, 1.0);
  vtkNew<vtkIntArray> tissue;
  tissue->SetName("tissue");
  for (const int value : {0, 1, 2, 2}) {
    tissue->InsertNextValue(value);
  }
  tissue->SetLookupTable(table);
  mesh->GetCellData()->AddArray(tissue);
  renderer.setup({mesh}, {{"cells", {0}}}, {{0.0, 0.5, 1.0}}, PaneMode::Single);
  const std::array<double, 3> fileColor = {0.8, 0.1, 0.5};
  const CategoryPalette& defaults = categoryPalettes().front();
  const CategoryPalette& okabeIto = *findCategoryPalette("okabe-ito");

  const size_t tissueField = show(renderer, {"tissue", FieldAssociation::Cell});
  // What a render does first: vtkMapper colors an array with the array's own table.
  renderer.context.actors.front()->GetMapper()->MapScalars(1.0);
  if (!categoryColored(lutOf(renderer, tissueField), 1.0, fileColor)) {
    std::cerr << "Default does not draw the colors of the file's table\n";
    return false;
  }
  if (!renderer.setCategoryPalette(tissueField, "okabe-ito") ||
      !categoryColored(lutOf(renderer, tissueField), 1.0, okabeIto.colors[1])) {
    std::cerr << "Picking a palette did not recolor the field\n";
    return false;
  }
  const size_t zones = show(renderer, {"zones", FieldAssociation::Cell});
  if (!categoryColored(lutOf(renderer, zones), 10.0, defaults.colors[1])) {
    std::cerr << "A palette picked for one field colors another\n";
    return false;
  }
  const std::array<double, 3> red = {1.0, 0.0, 0.0};
  if (!renderer.setCategoryColor(zones, 20.0, red) ||
      !renderer.fields()[zones].analysis.coloring.custom ||
      !categoryColored(lutOf(renderer, zones), 20.0, red) ||
      !categoryColored(lutOf(renderer, zones), 30.0, defaults.colors[3]) ||
      renderer.setCategoryPalette(renderer.fields().size(), "okabe-ito")) {
    std::cerr << "A recolor did not make the field's colors custom over its palette\n";
    return false;
  }
  show(renderer, {"tissue", FieldAssociation::Cell});
  vtkMapper* mapper = renderer.context.actors.front()->GetMapper();
  if (mapper->GetLookupTable() != lutOf(renderer, tissueField) ||
      !categoryColored(lutOf(renderer, tissueField), 1.0, okabeIto.colors[1])) {
    std::cerr << "A field lost its palette after cycling away and back\n";
    return false;
  }
  show(renderer, {"zones", FieldAssociation::Cell});
  if (mapper->GetLookupTable() != lutOf(renderer, zones) ||
      !categoryColored(lutOf(renderer, zones), 20.0, red)) {
    std::cerr << "A field lost its custom colors after cycling away and back\n";
    return false;
  }
  if (!renderer.setCategoryPalette(tissueField, "default") ||
      !categoryColored(lutOf(renderer, tissueField), 1.0, fileColor)) {
    std::cerr << "Default lost the file's colors once the field was drawn\n";
    return false;
  }

  // A scene set up again still finds the file's colors, and analyzing a field again
  // keeps the palette picked for it.
  renderer.setup({mesh}, {{"cells", {0}}}, {{0.0, 0.5, 1.0}}, PaneMode::Split);
  const size_t again = renderer.findField({"tissue", FieldAssociation::Cell}).value();
  if (!categoryColored(lutOf(renderer, again), 1.0, fileColor) ||
      !renderer.setCategoryPalette(again, "okabe-ito")) {
    std::cerr << "A split pane does not draw the file's colors\n";
    return false;
  }
  tissue->SetValue(3, 1);
  renderer.reanalyzeField(again);
  if (renderer.fields()[again].analysis.uniqueValues.size() != 3 ||
      !categoryColored(lutOf(renderer, again), 1.0, okabeIto.colors[1])) {
    std::cerr << "Analyzing a field again lost its palette\n";
    return false;
  }
  return true;
}

// The picker reports a hit on a triangle with two coincident vertices, as at a mesh's
// pole, with NaN along the zero-length edge between them.
bool checkDegeneratePick() {
  vtkNew<vtkPoints> points;
  points->InsertNextPoint(0.0, 0.0, 0.0);
  points->InsertNextPoint(1.0, 0.0, 0.0);
  points->InsertNextPoint(0.0, 0.0, 0.0);
  vtkNew<vtkCellArray> triangles;
  const vtkIdType triangle[3] = {0, 1, 2};
  triangles->InsertNextCell(3, triangle);
  vtkNew<vtkFloatArray> lat;
  lat->SetName("lat");
  for (const float value : {10.0F, 30.0F, 10.0F}) {
    lat->InsertNextValue(value);
  }
  vtkNew<vtkPolyData> mesh;
  mesh->SetPoints(points);
  mesh->SetPolys(triangles);
  mesh->GetPointData()->AddArray(lat);

  const double pcoords[3] = {0.25, std::numeric_limits<double>::quiet_NaN(), 0.0};
  const std::optional<double> value =
      fieldValueAtPick(mesh, "lat", FieldAssociation::Point, ScalarAnalysis{}, 0, 0, pcoords);
  if (!value || !(std::abs(*value - 15.0) < 1e-9)) {
    std::cerr << "A hit on a degenerate triangle did not interpolate along its edge\n";
    return false;
  }
  return true;
}
} // namespace

int main() {
  vtkNew<vtkGenericOpenGLRenderWindow> window;
  window->SetReadyForRendering(false);
  vtkNew<vtkRenderWindowInteractor> interactor;
  MeshRenderer renderer;
  renderer.setRenderContext(window, interactor);

  vtkNew<vtkPoints> points;
  points->InsertNextPoint(0.0, 0.0, 0.0);
  points->InsertNextPoint(1.0, 1.0, 1.0);
  vtkNew<vtkPolyData> mesh;
  mesh->SetPoints(points);
  for (const char* name : {"first", "second", "third"}) {
    vtkNew<vtkFloatArray> field;
    field->SetName(name);
    field->InsertNextValue(0.0F);
    field->InsertNextValue(1.0F);
    mesh->GetPointData()->AddArray(field);
  }
  const std::vector<vtkSmartPointer<vtkDataSet>> meshes{mesh};
  const std::vector<std::array<double, 3>> colors{{0.0, 0.5, 1.0}};
  const std::vector<MeshGroup> groups{{"one", {0}}, {"two", {0}}, {"three", {0}}};
  window->SetSize(2000, 1200);
  renderer.setup(meshes, groups, colors, PaneMode::Single);
  if (!checkGrid(renderer, 3, 3, 1)) {
    return 1;
  }
  window->SetSize(1000, 1000);
  renderer.refitCameraIfUntouched();
  if (!checkGrid(renderer, 3, 2, 2)) {
    return 1;
  }
  auto* firstRenderer = renderer.getRenderer();
  auto* camera = firstRenderer ? firstRenderer->GetActiveCamera() : nullptr;
  if (!camera) {
    std::cerr << "The first tile has no active camera\n";
    return 1;
  }
  camera->Azimuth(25.0);
  const auto cameraTime = camera->GetMTime();
  window->SetSize(600, 1600);
  renderer.refitCameraIfUntouched();
  if (!checkGrid(renderer, 3, 1, 3) || camera->GetMTime() != cameraTime) {
    return 1;
  }
  renderer.resetCamera();
  const double* direction = camera->GetDirectionOfProjection();
  const double* up = camera->GetViewUp();
  const double* focal = camera->GetFocalPoint();
  if (std::abs(direction[2] + 1.0) > 1e-9 || std::abs(up[1] - 1.0) > 1e-9 ||
      std::abs(focal[0] - 0.5) > 1e-9 || std::abs(focal[1] - 0.5) > 1e-9 ||
      std::abs(focal[2] - 0.5) > 1e-9) {
    std::cerr << "Reset view did not restore the opening camera\n";
    return 1;
  }

  // One file's fields split into a tile each.
  window->SetSize(2000, 1200);
  renderer.setup(meshes, {groups.front()}, colors, PaneMode::Split);
  if (!checkGrid(renderer, 3, 3, 1)) {
    return 1;
  }
  window->SetSize(1000, 1000);
  renderer.refitCameraIfUntouched();
  if (!checkGrid(renderer, 3, 2, 2)) {
    return 1;
  }

  // Three files stacked, each split into its own 2 x 2 grid of three fields.
  renderer.setup(meshes, groups, colors, PaneMode::Split);
  window->SetSize(600, 1600);
  renderer.refitCameraIfUntouched();
  const double* corner = renderer.tiles().front().viewport;
  if (shownTiles(renderer).size() != 9 || corner[0] != 0.0 || corner[2] != 0.5 ||
      corner[3] != 1.0 || std::abs(corner[1] - 5.0 / 6.0) > 1e-12) {
    std::cerr << "Split panes did not each lay out their own tiles\n";
    return 1;
  }
  renderer.setup(meshes, {groups.front()}, colors, PaneMode::Single);
  if (!checkGrid(renderer, 1, 1, 1)) {
    return 1;
  }
  return checkColormaps(renderer) && checkSurfaceLook(renderer, interactor) && checkHistogram() &&
                 checkNonFiniteValues() && checkProbe(renderer) && checkGlass(renderer) &&
                 checkPaneFields(renderer) && checkCategoryNaNs(renderer) &&
                 checkCategoryPalettes(renderer) && checkDegeneratePick() && checkPin(renderer) &&
                 checkTrace() && checkSplitPages(renderer)
             ? 0
             : 1;
}
