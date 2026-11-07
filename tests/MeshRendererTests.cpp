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
bool checkGrid(MeshRenderer& renderer, size_t count, size_t cols, size_t rows) {
  // The panels, then in the facet grid an empty renderer clearing each empty cell.
  auto* renderers = renderer.context.window->GetRenderers();
  const size_t blanks = renderer.getFacetPanelCount() > 0 ? cols * rows - count : 0;
  if (static_cast<size_t>(renderers->GetNumberOfItems()) != count + blanks) {
    std::cerr << "Expected " << count << " panels and " << blanks << " empty cells\n";
    return false;
  }
  renderers->InitTraversal();
  for (size_t i = 0; i < count; ++i) {
    vtkRenderer* panel = renderers->GetNextItem();
    if (!panel) {
      return false;
    }
    const double* viewport = panel->GetViewport();
    const size_t row = i / cols;
    const double expected[4] = {static_cast<double>(i % cols) / static_cast<double>(cols),
                                1.0 - static_cast<double>(row + 1) / static_cast<double>(rows),
                                static_cast<double>(i % cols + 1) / static_cast<double>(cols),
                                1.0 - static_cast<double>(row) / static_cast<double>(rows)};
    FacetPanelInfo info;
    const bool facet = renderer.getFacetPanelInfo(i, info);
    for (size_t j = 0; j < 4; ++j) {
      if (std::abs(viewport[j] - expected[j]) > 1e-12 ||
          (facet && info.viewport[j] != viewport[j])) {
        std::cerr << "Unexpected viewport for panel " << i << " in " << cols << 'x' << rows << '\n';
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
  renderer.setup({mesh}, {{"ramp", {0}}}, {{0.0, 0.5, 1.0}});

  const Colormap& rainbow = defaultColormap(ColormapGroup::Sequential);
  const Colormap& viridis = *findColormap("viridis");
  renderer.setActiveScalar("phase", FieldAssociation::Point);
  if (renderer.getActiveScalarAnalysis().colormap.map->id != "hue wheel") {
    std::cerr << "A phase field does not start on the hue wheel\n";
    return false;
  }
  renderer.setActiveScalar("lat", FieldAssociation::Point);
  if (!sameColor(renderer.getActiveLUT(), 255, rainbow.stops.back()) ||
      !renderer.setColormap("viridis", true) ||
      !sameColor(renderer.getActiveLUT(), 0, viridis.stops.back()) ||
      !sameColor(renderer.getActiveLUT(), 255, viridis.stops.front())) {
    std::cerr << "Choosing a colormap did not recolor the field\n";
    return false;
  }
  renderer.setActiveScalar("other", FieldAssociation::Point);
  if (!sameColor(renderer.getActiveLUT(), 255, rainbow.stops.back())) {
    std::cerr << "A colormap chosen for one field colors another\n";
    return false;
  }
  renderer.setActiveScalar("lat", FieldAssociation::Point);
  if (!sameColor(renderer.getActiveLUT(), 0, viridis.stops.back()) ||
      !renderer.getActiveScalarAnalysis().colormap.reversed) {
    std::cerr << "A field lost its colormap after cycling away and back\n";
    return false;
  }

  // The clip window is 0..31, so -1 and 32 fall outside it.
  const auto grayAt = [&renderer](double value) {
    double rgb[3];
    renderer.getActiveLUT()->GetColor(value, rgb);
    return nearColor(rgb, kOutOfRangeGray);
  };
  if (!renderer.setOutOfRange(OutOfRange::Gray) || !grayAt(-1.0) || !grayAt(32.0)) {
    std::cerr << "Gray did not color values outside the clip window\n";
    return false;
  }
  renderer.setActiveScalar("other", FieldAssociation::Point);
  if (grayAt(-1.0)) {
    std::cerr << "Gray chosen for one field applies to another\n";
    return false;
  }
  renderer.setActiveScalar("lat", FieldAssociation::Point);
  if (!grayAt(32.0) || !renderer.getActiveScalarAnalysis().colormap.reversed) {
    std::cerr << "A field lost its out-of-range color after cycling away and back\n";
    return false;
  }
  return true;
}

bool depthPeeling(MeshRenderer& renderer, bool on) {
  vtkRendererCollection* renderers = renderer.context.window->GetRenderers();
  renderers->InitTraversal();
  for (vtkRenderer* panel = renderers->GetNextItem(); panel; panel = renderers->GetNextItem()) {
    if ((panel->GetUseDepthPeeling() != 0) != on) {
      return false;
    }
  }
  return true;
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
    auto* panels = renderer.context.window->GetRenderers();
    vtkCollectionSimpleIterator cookie;
    panels->InitTraversal(cookie);
    for (vtkRenderer* panel = panels->GetNextRenderer(cookie); panel;
         panel = panels->GetNextRenderer(cookie)) {
      panel->InvokeEvent(vtkCommand::StartEvent);
      if (panel->GetMaximumNumberOfPeels() != expected) {
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
  renderer.setActiveScalar("lat", FieldAssociation::Point);
  if (!actorsAre(VTK_SURFACE, false, 0.4) || !peeling(true) || anyGlassShaded()) {
    std::cerr << "X-ray is not a plain depth-peeled surface at its opacity after cycling fields\n";
    return false;
  }
  renderer.setSurfaceLook({SurfaceStyle::Glass, 0.4, 0.5});
  renderer.setActiveScalar("other", FieldAssociation::Point);
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

  // A scene set up later, here a facet grid, takes the look in force.
  renderer.setSurfaceLook({SurfaceStyle::Glass, 0.4, 0.5});
  vtkSmartPointer<vtkDataSet> mesh = renderer.getPrimaryMesh();
  renderer.setupFacetGrid({mesh}, {{"ramp", {0}}}, {{0.0, 0.5, 1.0}});
  const bool everyActorGlass = std::all_of(renderer.context.actors.begin(),
                                           renderer.context.actors.end(),
                                           [](const auto& actor) { return glassShaded(actor); });
  if (!actorsAre(VTK_SURFACE, false, 0.5) || !peeling(true) || !everyActorGlass) {
    std::cerr << "The facet grid did not take the surface style\n";
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
  renderer.context.window->SetSize(400, 400);
  renderer.setup({mesh}, {{"square", {0}}}, {{0.0, 0.5, 1.0}});

  std::optional<ProbeHit> hit = probeSquareAt(renderer, renderer.getRenderer());
  if (!hit || hit->cell != 6 || !hit->field.empty()) {
    std::cerr << "Probing the plain mesh did not find cell 6 alone\n";
    return false;
  }
  renderer.setActiveScalar("lat", FieldAssociation::Point);
  if (!readsCell6(probeSquareAt(renderer, renderer.getRenderer()), "lat", 36.0)) {
    std::cerr << "Probing a point field did not interpolate it in the picked cell\n";
    return false;
  }
  renderer.setActiveScalar("region", FieldAssociation::Cell);
  hit = probeSquareAt(renderer, renderer.getRenderer());
  if (!readsCell6(hit, "region", 9.25) || !hit->category || hit->category->label != "9.25") {
    std::cerr << "Probing a cell field did not give the cell's category\n";
    return false;
  }
  if (renderer.probe(1.0, 1.0)) {
    std::cerr << "Probing beside the mesh found a cell\n";
    return false;
  }

  // Each file's panel reads its own mesh, though the shared camera lines them all up.
  renderer.setup(
      {mesh, mesh}, {{"first", {0}}, {"second", {1}}}, {{0.0, 0.5, 1.0}, {0.0, 0.5, 1.0}});
  auto* second =
      vtkRenderer::SafeDownCast(renderer.context.window->GetRenderers()->GetItemAsObject(1));
  hit = second ? probeSquareAt(renderer, second) : std::nullopt;
  if (!hit || hit->mesh != 1) {
    std::cerr << "Probing a file's panel did not find that file's mesh\n";
    return false;
  }

  // Each facet panel reads its own column's field.
  renderer.setupFacetGrid({mesh}, {{"square", {0}}}, {{0.0, 0.5, 1.0}});
  vtkRendererCollection* panels = renderer.context.window->GetRenderers();
  vtkRenderer* latPanel = panels->GetFirstRenderer();
  vtkRenderer* regionPanel = vtkRenderer::SafeDownCast(panels->GetItemAsObject(1));
  if (!regionPanel || !readsCell6(probeSquareAt(renderer, latPanel), "lat", 36.0) ||
      !readsCell6(probeSquareAt(renderer, regionPanel), "region", 9.25)) {
    std::cerr << "A facet panel did not probe its own field\n";
    return false;
  }
  return true;
}

bool sameViewport(const double* a, const double* b) {
  return std::equal(a, a + 4, b, [](double x, double y) { return std::abs(x - y) < 1e-12; });
}

// A page of the facet grid draws its columns in the same cells as every other page, above
// the pager's inset; the other columns keep their clip range and are neither drawn nor
// probed, and the camera stays where it was.
bool checkFacetPages(MeshRenderer& renderer) {
  const vtkSmartPointer<vtkPolyData> mesh = probeSquare();
  vtkDataArray* lat = mesh->GetPointData()->GetArray("lat");
  for (const char* name : {"f1", "f2", "f3"}) {
    vtkNew<vtkFloatArray> field;
    field->DeepCopy(lat);
    field->SetName(name);
    mesh->GetPointData()->AddArray(field);
  }
  renderer.context.window->SetSize(800, 600);
  // Columns lat, f1, f2, f3, then the cell field region.
  renderer.setupFacetGrid({mesh}, {{"square", {0}}}, {{0.0, 0.5, 1.0}});
  renderer.setFacetPages(2, 0, 60);
  const auto shownPanels = [&renderer]() {
    std::vector<size_t> shown;
    FacetPanelInfo info;
    for (size_t i = 0; renderer.getFacetPanelInfo(i, info); ++i) {
      if (info.shown) {
        shown.push_back(i);
      }
    }
    return shown;
  };
  FacetPanelInfo first;
  FacetPanelInfo second;
  renderer.getFacetPanelInfo(0, first);
  renderer.getFacetPanelInfo(1, second);
  if (renderer.getFacetPageCount() != 3 || shownPanels() != std::vector<size_t>{0, 1} ||
      first.viewport[1] < 0.1 - 1e-12 || second.viewport[1] < 0.1 - 1e-12) {
    std::cerr << "The first page did not show the first two columns above the inset\n";
    return false;
  }

  vtkCamera* camera = renderer.getRenderer()->GetActiveCamera();
  camera->Azimuth(30.0);
  const vtkMTimeType cameraTime = camera->GetMTime();
  renderer.showFacetPage(1);
  FacetPanelInfo f2;
  FacetPanelInfo f3;
  renderer.getFacetPanelInfo(2, f2);
  renderer.getFacetPanelInfo(3, f3);
  if (shownPanels() != std::vector<size_t>{2, 3} || !sameViewport(f2.viewport, first.viewport) ||
      !sameViewport(f3.viewport, second.viewport) || camera->GetMTime() != cameraTime) {
    std::cerr << "The second page did not show columns 2 and 3 in the same cells\n";
    return false;
  }
  auto* panels = renderer.context.window->GetRenderers();
  auto* f2Panel = vtkRenderer::SafeDownCast(panels->GetItemAsObject(2));
  camera->Azimuth(-30.0);
  const std::optional<ProbeHit> hit = f2Panel ? probeSquareAt(renderer, f2Panel) : std::nullopt;
  if (!hit || hit->field != "f2") {
    std::cerr << "The probe did not read the column shown in the cell\n";
    return false;
  }

  if (!renderer.setFacetColormap(2, "viridis", true) ||
      renderer.setFacetColormap(2, "missing", false) ||
      renderer.setFacetColormap(999, "viridis", false) ||
      renderer.setFacetColormap(4, "viridis", false)) {
    std::cerr << "Facet colormap selection accepted invalid input or rejected a continuous field\n";
    return false;
  }
  renderer.setFacetPanelClipRange(2, 20.0, 60.0);
  renderer.showFacetPage(0);
  renderer.showFacetPage(1);
  renderer.getFacetPanelInfo(2, f2);
  FacetPanelInfo unchanged;
  renderer.getFacetPanelInfo(3, unchanged);
  if (f2.analysis.colormap.map != findColormap("viridis") || !f2.analysis.colormap.reversed ||
      unchanged.analysis.colormap.map == findColormap("viridis")) {
    std::cerr << "Facet colormap choice was lost or changed another field\n";
    return false;
  }
  const double* lutRange = renderer.getFacetPanelLUT(2)->GetRange();
  if (f2.clipRange[0] != 20.0 || f2.clipRange[1] != 60.0 || lutRange[0] != 20.0 ||
      lutRange[1] != 60.0) {
    std::cerr << "A column's clip range did not survive a page round trip\n";
    return false;
  }

  // The last page's one column, and an empty renderer clearing the cell beside it.
  renderer.showFacetPage(2);
  auto* blank = vtkRenderer::SafeDownCast(panels->GetItemAsObject(5));
  if (shownPanels() != std::vector<size_t>{4} || panels->GetNumberOfItems() != 6 || !blank ||
      blank->GetViewProps()->GetNumberOfItems() != 0 ||
      !sameViewport(blank->GetViewport(), second.viewport)) {
    std::cerr << "The last page did not clear its empty cell\n";
    return false;
  }

  // Several files page their columns together, a row each.
  renderer.setupFacetGrid(
      {mesh, mesh}, {{"a", {0}}, {"b", {1}}}, {{0.0, 0.5, 1.0}, {0.0, 0.5, 1.0}});
  renderer.setFacetPages(2, 1, 0);
  FacetPanelInfo b2;
  renderer.getFacetPanelInfo(7, b2);
  if (shownPanels() != std::vector<size_t>{2, 3, 7, 8} || b2.viewport[0] != 0.0 ||
      b2.viewport[3] != 0.5) {
    std::cerr << "Paging several files did not show the page's columns in every row\n";
    return false;
  }
  if (!renderer.setFacetColormap(2, "magma", true)) {
    return false;
  }
  renderer.getFacetPanelInfo(7, b2);
  renderer.getFacetPanelInfo(3, unchanged);
  if (b2.analysis.colormap.map != findColormap("magma") || !b2.analysis.colormap.reversed ||
      unchanged.analysis.colormap.map == findColormap("magma")) {
    std::cerr << "Facet colormap did not follow its column across files\n";
    return false;
  }
  renderer.getFacetPanelInfo(7, b2);
  double viewport[4];
  if (!renderer.getPanelViewport(7, viewport) || renderer.getPanelViewport(0, viewport) ||
      renderer.getPanelViewport(999, viewport) || b2.viewport[0] != 0.0) {
    std::cerr << "The visible panel lookup is wrong\n";
    return false;
  }
  return true;
}

// A pinned hit reads the field shown now at the same point, carries a marker that nothing
// picks, and maps back to where it was picked.
bool checkPin(MeshRenderer& renderer) {
  const vtkSmartPointer<vtkPolyData> mesh = probeSquare();
  renderer.context.window->SetSize(400, 400);
  renderer.setup({mesh}, {{"square", {0}}}, {{0.0, 0.5, 1.0}});
  renderer.setActiveScalar("lat", FieldAssociation::Point);
  vtkRenderer* panel = renderer.getRenderer();
  const std::optional<ProbeHit> pin = probeSquareAt(renderer, panel);
  if (!readsCell6(pin, "lat", 36.0) || std::abs(pin->position[0] - 0.6) > 1e-3 ||
      std::abs(pin->position[1] - 0.3) > 1e-3) {
    std::cerr << "A probe hit did not give where it lies\n";
    return false;
  }
  renderer.setActiveScalar("region", FieldAssociation::Cell);
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

// probeSquare's fields over 9 frames, read like the XDMF source: only the active array,
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

  bool sampledScalarRange(const std::string& /*scalarName*/,
                          double /*out*/[2],
                          int /*maxSamples*/) override {
    return false;
  }

  void setActiveArray(const std::string& scalarName) override {
    active_ = scalarName;
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
  series.setActiveArray("lat");
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
  series.setActiveArray("drift");
  trace.reset(9);
  readSome({"drift", FieldAssociation::Point}, trace, std::chrono::hours(1));
  if (!sameTrace(trace, {0.0, -36.0, -72.0, -108.0, -144.0, nan, -216.0, -252.0, -288.0})) {
    std::cerr << "A trace did not follow the field to the same point\n";
    return false;
  }
  series.setActiveArray("region");
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
  renderer.setup({mesh}, {{"square", {0}}}, {{0.0, 0.5, 1.0}});
  renderer.setActiveScalar("lat", FieldAssociation::Point);
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

// The legend reads its rows from the active table. The first file lacks the field
// shown second, so its mapper still holds the first field's shorter table.
bool checkCategoricalTable(MeshRenderer& renderer) {
  renderer.setup({categoricalCells("zones", 3), categoricalCells("regions", 5)},
                 {{"a", {0}}, {"b", {1}}},
                 {{0.0, 0.5, 1.0}, {0.0, 0.5, 1.0}});
  renderer.setActiveScalar("zones", FieldAssociation::Cell);
  renderer.setActiveScalar("regions", FieldAssociation::Cell);
  vtkLookupTable* lut = renderer.getActiveLUT();
  if (!lut || lut->GetNumberOfAnnotatedValues() != 5 || lut->GetAnnotatedValueIndex(40.0) != 4) {
    std::cerr << "The active table is not the field's when the first file lacks it\n";
    return false;
  }
  return true;
}

bool categoryColored(vtkLookupTable* lut, double value, const std::array<double, 3>& rgb) {
  return lut && lut->GetAnnotatedValueIndex(value) >= 0 &&
         sameColor(lut, lut->GetAnnotatedValueIndex(value), rgb);
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
  renderer.setup({mesh}, {{"cells", {0}}}, {{0.0, 0.5, 1.0}});
  const std::array<double, 3> fileColor = {0.8, 0.1, 0.5};
  const CategoryPalette& defaults = categoryPalettes().front();
  const CategoryPalette& okabeIto = *findCategoryPalette("okabe-ito");

  renderer.setActiveScalar("tissue", FieldAssociation::Cell);
  // What a render does first: vtkMapper colors an array with the array's own table.
  renderer.context.actors.front()->GetMapper()->MapScalars(1.0);
  if (!categoryColored(renderer.getActiveLUT(), 1.0, fileColor)) {
    std::cerr << "Default does not draw the colors of the file's table\n";
    return false;
  }
  if (!renderer.setCategoryPalette("okabe-ito") ||
      !categoryColored(renderer.getActiveLUT(), 1.0, okabeIto.colors[1])) {
    std::cerr << "Picking a palette did not recolor the field\n";
    return false;
  }
  renderer.setActiveScalar("zones", FieldAssociation::Cell);
  if (!categoryColored(renderer.getActiveLUT(), 10.0, defaults.colors[1])) {
    std::cerr << "A palette picked for one field colors another\n";
    return false;
  }
  const std::array<double, 3> red = {1.0, 0.0, 0.0};
  if (!renderer.setCategoryColor(20.0, red) ||
      !renderer.getActiveScalarAnalysis().coloring.custom ||
      !categoryColored(renderer.getActiveLUT(), 20.0, red) ||
      !categoryColored(renderer.getActiveLUT(), 30.0, defaults.colors[3])) {
    std::cerr << "A recolor did not make the field's colors custom over its palette\n";
    return false;
  }
  renderer.setActiveScalar("tissue", FieldAssociation::Cell);
  if (!categoryColored(renderer.getActiveLUT(), 1.0, okabeIto.colors[1])) {
    std::cerr << "A field lost its palette after cycling away and back\n";
    return false;
  }
  renderer.setActiveScalar("zones", FieldAssociation::Cell);
  if (!renderer.getActiveScalarAnalysis().coloring.custom ||
      !categoryColored(renderer.getActiveLUT(), 20.0, red)) {
    std::cerr << "A field lost its custom colors after cycling away and back\n";
    return false;
  }
  renderer.setActiveScalar("tissue", FieldAssociation::Cell);
  if (!renderer.setCategoryPalette("default") ||
      !categoryColored(renderer.getActiveLUT(), 1.0, fileColor)) {
    std::cerr << "Default lost the file's colors once the field was drawn\n";
    return false;
  }

  // A facet panel draws the file's colors too, and the palette picked for it.
  renderer.setupFacetGrid({mesh}, {{"cells", {0}}}, {{0.0, 0.5, 1.0}});
  for (size_t panel = 0; panel < renderer.getFacetPanelCount(); ++panel) {
    FacetPanelInfo info;
    if (!renderer.getFacetPanelInfo(panel, info) || info.title != "tissue") {
      continue;
    }
    if (!categoryColored(renderer.getFacetPanelLUT(panel), 1.0, fileColor) ||
        !renderer.setFacetCategoryPalette(panel, "okabe-ito") ||
        !categoryColored(renderer.getFacetPanelLUT(panel), 1.0, okabeIto.colors[1])) {
      std::cerr << "A facet panel does not draw the file's colors or its palette\n";
      return false;
    }
    return true;
  }
  std::cerr << "The facet grid has no tissue panel\n";
  return false;
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
  renderer.setup(meshes, groups, colors);
  if (!checkGrid(renderer, 3, 3, 1)) {
    return 1;
  }
  window->SetSize(1000, 1000);
  renderer.refitCameraIfUntouched();
  if (!checkGrid(renderer, 3, 2, 2)) {
    return 1;
  }
  auto* camera = renderer.getRenderer()->GetActiveCamera();
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

  window->SetSize(2000, 1200);
  renderer.setupFacetGrid(meshes, {groups.front()}, colors);
  if (!checkGrid(renderer, 3, 3, 1)) {
    return 1;
  }
  window->SetSize(1000, 1000);
  renderer.refitCameraIfUntouched();
  if (!checkGrid(renderer, 3, 2, 2)) {
    return 1;
  }

  renderer.setupFacetGrid(meshes, groups, colors);
  window->SetSize(600, 1600);
  renderer.refitCameraIfUntouched();
  if (!checkGrid(renderer, 9, 3, 3)) {
    return 1;
  }
  renderer.setup(meshes, {groups.front()}, colors);
  if (!checkGrid(renderer, 1, 1, 1)) {
    return 1;
  }
  return checkColormaps(renderer) && checkSurfaceLook(renderer, interactor) && checkHistogram() &&
                 checkNonFiniteValues() && checkProbe(renderer) && checkGlass(renderer) &&
                 checkCategoricalTable(renderer) && checkCategoryPalettes(renderer) &&
                 checkDegeneratePick() && checkPin(renderer) && checkTrace() &&
                 checkFacetPages(renderer)
             ? 0
             : 1;
}
