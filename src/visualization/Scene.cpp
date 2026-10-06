#include "Scene.h"

#include "SoftwareRasterizer.h"
#include "SceneLighting.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataSetMapper.h>
#include <vtkFloatArray.h>
#include <vtkGenericCell.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkGlyph3DMapper.h>
#include <vtkIdList.h>
#include <vtkLight.h>
#include <vtkPlane.h>
#include <vtkPlaneCollection.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyDataNormals.h>
#include <vtkProperty.h>
#include <vtkShaderProperty.h>
#include <vtkSphereSource.h>
#include <vtkUnsignedCharArray.h>
#ifdef __EMSCRIPTEN__
#include <vtkWebAssemblyOpenGLRenderWindow.h>
#endif

namespace vv {
namespace {
constexpr std::size_t maxCoordinates = 12'000'000;
constexpr std::size_t maxIndices = 24'000'000;
constexpr const char* backFaceShading =
    "//VTK::Light::Impl\n"
    "  if (!gl_FrontFacing) { gl_FragData[0].rgb *= 0.45; }\n";

bool finite(const double* values, std::size_t count) {
  return values && std::all_of(values, values + count, [](double v) {
           return std::isfinite(v) && std::abs(v) <= 1e15;
         });
}
} // namespace

Scene::Scene(Backend backend, const char* canvasSelector) : backend_(backend) {
#ifdef __EMSCRIPTEN__
  if (backend == Backend::WebGL) {
    auto window = vtkSmartPointer<vtkWebAssemblyOpenGLRenderWindow>::New();
    window->SetCanvasSelector(canvasSelector);
    window->SetMultiSamples(0);
    window_ = window;
  }
#else
  (void)canvasSelector;
#endif
  if (!window_) {
    window_ = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    backend_ = Backend::Software;
  }
  renderer_ = vtkSmartPointer<vtkRenderer>::New();
  renderer_->SetBackground(0, 0, 0);
  renderer_->UseFXAAOn();
  renderer_->AutomaticLightCreationOff();
  for (std::size_t i = 0; i < lighting::directions.size(); ++i) {
    vtkNew<vtkLight> light;
    light->SetLightTypeToCameraLight();
    light->SetPosition(lighting::directions[i].data());
    light->SetFocalPoint(0, 0, 0);
    light->SetIntensity(lighting::intensities[i]);
    light->PositionalOff();
    renderer_->AddLight(light);
  }
  window_->AddRenderer(renderer_);
  mapper_ = vtkSmartPointer<vtkPolyDataMapper>::New();
  mapper_->ScalarVisibilityOff();
  mapper_->UseLookupTableScalarRangeOn();
  actor_ = vtkSmartPointer<vtkActor>::New();
  actor_->SetMapper(mapper_);
  actor_->GetProperty()->SetAmbient(lighting::ambient);
  actor_->GetProperty()->SetDiffuse(lighting::diffuse);
  actor_->GetProperty()->SetSpecular(lighting::specular);
  actor_->GetProperty()->SetSpecularPower(lighting::power);
  actor_->GetProperty()->SetSpecularColor(1, 1, 1);
  actor_->GetProperty()->SetInterpolationToPhong();
  // Backface properties are bypassed by VTK's scalar-color paths. Shade after
  // lighting instead, retaining LUT hues and alpha for every scalar association.
  actor_->GetShaderProperty()->AddFragmentShaderReplacement(
      "//VTK::Light::Impl", true, backFaceShading, false);
  renderer_->AddActor(actor_);
  scalarBack_ = vtkSmartPointer<vtkFloatArray>::New();
  scalarBack_->SetName("vv.scalar");
  software_ = std::make_unique<SoftwareRasterizer>();
}

Scene::~Scene() {
  if (backend_ == Backend::WebGL)
    window_->Finalize();
}

bool Scene::setSurface(const float* positions,
                       std::size_t coordinates,
                       const std::uint32_t* indices,
                       std::size_t indexCount,
                       bool points) {
  if (!positions || coordinates == 0 || coordinates % 3 != 0 || coordinates > maxCoordinates ||
      indexCount > maxIndices || (!points && indexCount % 3 != 0) || (indexCount && !indices))
    return false;
  if (!std::all_of(positions, positions + coordinates, [](float v) {
        return std::isfinite(v) && std::abs(v) <= 1e15f;
      }))
    return false;
  const auto vertices = coordinates / 3;
  for (std::size_t i = 0; i < indexCount; ++i) {
    if (indices[i] >= vertices)
      return false;
  }

  auto mesh = vtkSmartPointer<vtkPolyData>::New();
  vtkNew<vtkFloatArray> array;
  array->SetNumberOfComponents(3);
  if (!array->SetNumberOfValues(static_cast<vtkIdType>(coordinates)))
    return false;
  std::memcpy(array->GetPointer(0), positions, coordinates * sizeof(float));
  vtkNew<vtkPoints> vtkPoints;
  vtkPoints->SetData(array);
  mesh->SetPoints(vtkPoints);
  vtkNew<vtkCellArray> cells;
  const std::size_t cellCount = points ? (indexCount ? indexCount : vertices) : indexCount / 3;
  if (!cells->AllocateExact(static_cast<vtkIdType>(cellCount),
                            static_cast<vtkIdType>(points ? cellCount : indexCount)))
    return false;
  if (points) {
    for (std::size_t i = 0; i < cellCount; ++i) {
      const vtkIdType id = static_cast<vtkIdType>(indexCount ? indices[i] : i);
      cells->InsertNextCell(1, &id);
    }
    mesh->SetVerts(cells);
  } else {
    for (std::size_t i = 0; i < indexCount; i += 3) {
      const vtkIdType face[3] = {static_cast<vtkIdType>(indices[i]),
                                 static_cast<vtkIdType>(indices[i + 1]),
                                 static_cast<vtkIdType>(indices[i + 2])};
      cells->InsertNextCell(3, face);
    }
    mesh->SetPolys(cells);
    vtkNew<vtkPolyDataNormals> normals;
    normals->SetInputData(mesh);
    normals->SplittingOff();
    normals->ConsistencyOff();
    normals->ComputeCellNormalsOff();
    normals->Update();
    mesh->ShallowCopy(normals->GetOutput());
  }

  mesh_ = mesh;
  points_ = points;
  mapper_->SetInputData(mesh_);
  mapper_->ScalarVisibilityOff();
  renderer_->ResetCamera(mesh_->GetBounds());
  cameraTouched_ = false;
  locator_ = nullptr;
  setStyle(color_, opacity_, wireframe_, pointDiameter_);
  applyPlanes();
  ++statistics_.geometryUploads;
  return true;
}

float* Scene::scalarBuffer(std::size_t count, bool cell) {
  if (!mesh_ || count == 0 ||
      count != static_cast<std::size_t>(cell ? mesh_->GetNumberOfCells()
                                            : mesh_->GetNumberOfPoints()))
    return nullptr;
  if (!scalarBack_->SetNumberOfValues(static_cast<vtkIdType>(count)))
    return nullptr;
  return scalarBack_->GetPointer(0);
}

bool Scene::commitScalar(std::size_t count, bool cell) {
  if (!mesh_)
    return false;
  if (count == 0) {
    mapper_->ScalarVisibilityOff();
    return true;
  }
  const auto expected = cell ? mesh_->GetNumberOfCells() : mesh_->GetNumberOfPoints();
  if (count != static_cast<std::size_t>(expected) ||
      count != static_cast<std::size_t>(scalarBack_->GetNumberOfValues()))
    return false;
  const float* values = scalarBack_->GetPointer(0);
  if (std::any_of(values, values + count, [](float v) { return std::isinf(v); }))
    return false;

  auto* attributes = cell ? static_cast<vtkDataSetAttributes*>(mesh_->GetCellData())
                          : static_cast<vtkDataSetAttributes*>(mesh_->GetPointData());
  vtkSmartPointer<vtkFloatArray> previous =
      vtkFloatArray::SafeDownCast(attributes->GetArray("vv.scalar"));
  scalarBack_->Modified();
  attributes->SetScalars(scalarBack_);
  scalarBack_ = previous;
  if (!scalarBack_) {
    scalarBack_ = vtkSmartPointer<vtkFloatArray>::New();
    scalarBack_->SetName("vv.scalar");
  }
  cellScalar_ = cell;
  if (!lut_)
    lut_ = createDefaultLookupTable(attributes->GetScalars()->GetRange());
  configureScalarMapper(mapper_,
                        "vv.scalar",
                        cell ? FieldAssociation::Cell : FieldAssociation::Point,
                        lut_);
  ++statistics_.scalarUploads;
  return true;
}

bool Scene::setScalar(const float* values, std::size_t count, bool cell) {
  if (count == 0)
    return commitScalar(0, cell);
  if (!values)
    return false;
  float* buffer = scalarBuffer(count, cell);
  if (!buffer)
    return false;
  std::memcpy(buffer, values, count * sizeof(float));
  return commitScalar(count, cell);
}

bool Scene::setLut(const float* rgba,
                   std::size_t count,
                   const double* stops,
                   double min,
                   double max,
                   bool interpolate) {
  if (!mesh_)
    return false;
  auto lut = createSegmentedLookupTable(rgba, count, stops, min, max, interpolate);
  if (!lut)
    return false;
  lut_ = lut;
  auto* mapper = mapper_.GetPointer();
  mapper->SetLookupTable(lut_);
  mapper->UseLookupTableScalarRangeOn();
  mapper->SetInterpolateScalarsBeforeMapping(!cellScalar_ && interpolate);
  return true;
}

bool Scene::setStyle(const std::array<double, 3>& color,
                     double opacity,
                     bool wireframe,
                     double diameter) {
  if (!finite(color.data(), 3) || !std::isfinite(opacity) || opacity < 0 || opacity > 1 ||
      !std::isfinite(diameter) || diameter <= 0 || diameter > 1000 ||
      std::any_of(color.begin(), color.end(), [](double v) { return v < 0 || v > 1; }))
    return false;
  color_ = color;
  opacity_ = opacity;
  if (wireframe != wireframe_) {
    auto* shader = actor_->GetShaderProperty();
    shader->ClearFragmentShaderReplacement("//VTK::Light::Impl", true);
    if (!wireframe)
      shader->AddFragmentShaderReplacement("//VTK::Light::Impl", true, backFaceShading, false);
  }
  wireframe_ = wireframe;
  pointDiameter_ = diameter;
  if (mesh_) {
    auto* property = actor_->GetProperty();
    property->SetColor(color[0], color[1], color[2]);
    property->SetSpecularColor(1, 1, 1);
    // Thin edges need their full scalar/material color, without surface lighting.
    const bool lit = !wireframe || points_;
    property->SetLighting(lit);
    property->SetAmbient(lit ? lighting::ambient : 1.0);
    property->SetDiffuse(lit ? lighting::diffuse : 0.0);
    property->SetOpacity(opacity);
    property->SetPointSize(static_cast<float>(diameter));
    property->SetRenderPointsAsSpheres(true);
    if (points_)
      property->SetRepresentationToPoints();
    else if (wireframe)
      property->SetRepresentationToWireframe();
    else
      property->SetRepresentationToSurface();
  }
  return true;
}

bool Scene::setPlanes(const double* planes, std::size_t count) {
  if (count > 6 || (count && !finite(planes, count * 4)))
    return false;
  std::vector<std::array<double, 4>> validated;
  for (std::size_t i = 0; i < count; ++i) {
    const double* p = planes + i * 4;
    const double norm = std::hypot(p[0], p[1], p[2]);
    if (norm < 1e-12)
      return false;
    validated.push_back({p[0] / norm, p[1] / norm, p[2] / norm, p[3] / norm});
  }
  planes_ = std::move(validated);
  applyPlanes();
  return true;
}

void Scene::applyPlanes() {
  vtkNew<vtkPlaneCollection> collection;
  for (const auto& p : planes_) {
    vtkNew<vtkPlane> plane;
    plane->SetNormal(p[0], p[1], p[2]);
    plane->SetOrigin(-p[3] * p[0], -p[3] * p[1], -p[3] * p[2]);
    collection->AddItem(plane);
  }
  if (mesh_)
    mapper_->SetClippingPlanes(collection);
}

bool Scene::setAnnotations(const double* data, std::size_t count) {
  if (count > 100'000 || (count && !finite(data, count * 7)))
    return false;
  for (std::size_t i = 0; i < count; ++i) {
    if (data[i * 7 + 6] <= 0 || data[i * 7 + 6] > 1000)
      return false;
    for (std::size_t j = 3; j < 6; ++j) {
      if (data[i * 7 + j] < 0 || data[i * 7 + j] > 1)
        return false;
    }
  }
  if (!annotationActor_) {
    vtkNew<vtkPolyData> cloud;
    vtkNew<vtkPoints> positions;
    cloud->SetPoints(positions);
    vtkNew<vtkFloatArray> diameters;
    diameters->SetName("diameter");
    cloud->GetPointData()->AddArray(diameters);
    vtkNew<vtkUnsignedCharArray> colors;
    colors->SetName("color");
    colors->SetNumberOfComponents(3);
    cloud->GetPointData()->SetScalars(colors);
    vtkNew<vtkSphereSource> sphere;
    sphere->SetRadius(0.5);
    sphere->SetThetaResolution(12);
    sphere->SetPhiResolution(8);
    vtkNew<vtkGlyph3DMapper> mapper;
    mapper->SetInputData(cloud);
    mapper->SetSourceConnection(sphere->GetOutputPort());
    mapper->SetScaleArray("diameter");
    mapper->SetScaleModeToScaleByMagnitude();
    mapper->SetColorModeToDirectScalars();
    annotationActor_ = vtkSmartPointer<vtkActor>::New();
    annotationActor_->SetMapper(mapper);
    renderer_->AddActor(annotationActor_);
  }
  auto* cloud = vtkPolyData::SafeDownCast(annotationActor_->GetMapper()->GetInput());
  auto* positions = cloud->GetPoints();
  auto* diameters = vtkFloatArray::SafeDownCast(cloud->GetPointData()->GetArray("diameter"));
  auto* colors = vtkUnsignedCharArray::SafeDownCast(cloud->GetPointData()->GetScalars());
  const auto size = static_cast<vtkIdType>(count);
  positions->SetNumberOfPoints(size);
  diameters->SetNumberOfValues(size);
  colors->SetNumberOfTuples(size);
  annotations_.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    const double* p = data + i * 7;
    annotations_[i] = {{p[0], p[1], p[2]}, {p[3], p[4], p[5]}, p[6]};
    const auto index = static_cast<vtkIdType>(i);
    positions->SetPoint(index, p);
    diameters->SetValue(index, static_cast<float>(p[6]));
    const unsigned char color[3] = {static_cast<unsigned char>(p[3] * 255),
                                    static_cast<unsigned char>(p[4] * 255),
                                    static_cast<unsigned char>(p[5] * 255)};
    colors->SetTypedTuple(index, color);
  }
  positions->Modified();
  diameters->Modified();
  colors->Modified();
  return true;
}

bool Scene::resize(int width, int height) {
  if (width < 1 || height < 1 || width > 4096 || height > 4096 ||
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) > 8'388'608)
    return false;
  width_ = width;
  height_ = height;
  window_->SetSize(width, height);
  if (mesh_ && !cameraTouched_)
    renderer_->ResetCamera(mesh_->GetBounds());
  return true;
}

void Scene::render() {
  if (!mesh_)
    return;
  renderer_->ResetCameraClippingRange(mesh_->GetBounds());
  if (backend_ == Backend::WebGL)
    window_->Render();
  else
    software_->render(*this);
  ++statistics_.renders;
}

void Scene::camera(double rx, double ry, double px, double py, double zoom, bool reset) {
  const double input[] = {rx, ry, px, py, zoom};
  if (!mesh_ || !finite(input, 5))
    return;
  auto* camera = renderer_->GetActiveCamera();
  cameraTouched_ = !reset;
  if (reset) {
    camera->SetPosition(0, 0, 1);
    camera->SetFocalPoint(0, 0, 0);
    camera->SetViewUp(0, 1, 0);
    camera->SetViewAngle(30);
    renderer_->ResetCamera(mesh_->GetBounds());
  } else {
    camera->Azimuth(rx);
    camera->Elevation(ry);
    camera->OrthogonalizeViewUp();
    double right[3], up[3], direction[3];
    camera->GetViewUp(up);
    camera->GetDirectionOfProjection(direction);
    right[0] = direction[1] * up[2] - direction[2] * up[1];
    right[1] = direction[2] * up[0] - direction[0] * up[2];
    right[2] = direction[0] * up[1] - direction[1] * up[0];
    const double scale =
        2 * camera->GetDistance() * std::tan(camera->GetViewAngle() * 3.141592653589793 / 360);
    double position[3], focal[3];
    camera->GetPosition(position);
    camera->GetFocalPoint(focal);
    for (int j = 0; j < 3; ++j) {
      const double delta =
          scale * (-px * right[j] * static_cast<double>(width_) / height_ + py * up[j]);
      position[j] += delta;
      focal[j] += delta;
    }
    camera->SetPosition(position);
    camera->SetFocalPoint(focal);
    camera->Dolly(std::exp(std::clamp(zoom, -2.0, 2.0)));
  }
}

std::array<double, 10> Scene::cameraState() const {
  auto* camera = renderer_->GetActiveCamera();
  std::array<double, 10> state{};
  camera->GetPosition(state.data());
  camera->GetFocalPoint(state.data() + 3);
  camera->GetViewUp(state.data() + 6);
  state[9] = camera->GetViewAngle();
  return state;
}

bool Scene::setCameraState(const double* state) {
  if (!finite(state, 10) || state[9] <= 0 || state[9] >= 180 ||
      std::hypot(state[0] - state[3], state[1] - state[4], state[2] - state[5]) < 1e-12 ||
      std::hypot(state[6], state[7], state[8]) < 1e-12)
    return false;
  auto* camera = renderer_->GetActiveCamera();
  camera->SetPosition(state);
  camera->SetFocalPoint(state + 3);
  camera->SetViewUp(state + 6);
  camera->SetViewAngle(state[9]);
  camera->OrthogonalizeViewUp();
  cameraTouched_ = true;
  return true;
}

std::array<double, 3> Scene::project(const std::array<double, 3>& position) {
  if (!mesh_)
    return {0, 0, 2};
  auto* renderer = renderer_.GetPointer();
  renderer->SetWorldPoint(position[0], position[1], position[2], 1);
  renderer->WorldToDisplay();
  const double* display = renderer->GetDisplayPoint();
  return {display[0], height_ - display[1], display[2]};
}

void Scene::reveal(const std::array<double, 3>& position) {
  if (!mesh_ || !finite(position.data(), 3))
    return;
  auto* camera = renderer_->GetActiveCamera();
  cameraTouched_ = true;
  const double* target = camera->GetFocalPoint();
  const double distance =
      std::hypot(position[0] - target[0], position[1] - target[1], position[2] - target[2]);
  if (distance <= 1e-12)
    return;
  const double scale = camera->GetDistance() / distance;
  camera->SetPosition(target[0] + (position[0] - target[0]) * scale,
                      target[1] + (position[1] - target[1]) * scale,
                      target[2] + (position[2] - target[2]) * scale);
  camera->OrthogonalizeViewUp();
}

Pick Scene::pick(double x, double y) {
  Pick result;
  if (!mesh_ || !std::isfinite(x) || !std::isfinite(y))
    return result;
  auto* renderer = renderer_.GetPointer();
  std::array<double, 3> endpoints[2];
  for (int i = 0; i < 2; ++i) {
    renderer->SetDisplayPoint(x, height_ - y, i);
    renderer->DisplayToWorld();
    const double* world = renderer->GetWorldPoint();
    if (world[3] == 0)
      return result;
    for (int j = 0; j < 3; ++j)
      endpoints[i][static_cast<std::size_t>(j)] = world[j] / world[3];
  }
  double surfaceDepth = 1.0;
  if (!points_) {
    if (!locator_) {
      locator_ = vtkSmartPointer<vtkStaticCellLocator>::New();
      locator_->SetDataSet(mesh_);
      locator_->BuildLocator();
    }
    vtkNew<vtkPoints> intersections;
    vtkNew<vtkIdList> cellIds;
    vtkNew<vtkGenericCell> cell;
    locator_->IntersectWithLine(
        endpoints[0].data(), endpoints[1].data(), 1e-8, intersections, cellIds, cell);
    for (vtkIdType i = 0; i < intersections->GetNumberOfPoints(); ++i) {
      double intersection[3];
      intersections->GetPoint(i, intersection);
      const bool kept = std::all_of(planes_.begin(), planes_.end(), [&](const auto& p) {
        return p[0] * intersection[0] + p[1] * intersection[1] + p[2] * intersection[2] + p[3] >= 0;
      });
      if (kept) {
        double scalar = std::numeric_limits<double>::quiet_NaN();
        double alpha = opacity_;
        if (mapper_->GetScalarVisibility() && lut_) {
          auto* values = cellScalar_ ? mesh_->GetCellData()->GetScalars()
                                    : mesh_->GetPointData()->GetScalars();
          if (cellScalar_) {
            scalar = values->GetComponent(cellIds->GetId(i), 0);
            alpha *= lut_->MapValue(scalar)[3] / 255.0;
          } else {
            mesh_->GetCell(cellIds->GetId(i), cell);
            double closest[3], pcoords[3], distance;
            int subId;
            double weights[3];
            cell->EvaluatePosition(intersection, closest, subId, pcoords, distance, weights);
            scalar = 0;
            double mappedAlpha = 0;
            for (int j = 0; j < 3; ++j) {
              const double value = values->GetComponent(cell->GetPointId(j), 0);
              scalar += weights[j] * value;
              mappedAlpha += weights[j] * lut_->MapValue(value)[3] / 255.0;
            }
            alpha *= lut_->GetIndexedLookup() ? mappedAlpha
                                              : lut_->MapValue(scalar)[3] / 255.0;
          }
        }
        if (result.cell < 0 && alpha > 0) {
          result.cell = static_cast<int>(cellIds->GetId(i));
          result.scalar = scalar;
        }
        if (alpha >= 1 - 1e-6) {
          surfaceDepth = project({intersection[0], intersection[1], intersection[2]})[2];
          break;
        }
      }
    }
  }
  for (std::size_t i = 0; i < annotations_.size(); ++i) {
    const auto& annotation = annotations_[i];
    const auto position = project(annotation.position);
    auto edge = annotation.position;
    const double* up = renderer->GetActiveCamera()->GetViewUp();
    for (std::size_t j = 0; j < 3; ++j)
      edge[j] += up[j] * annotation.diameter * 0.5;
    const auto radius = project(edge);
    const double distance = std::hypot(position[0] - x, position[1] - y);
    const double screenRadius =
        std::max(3.0, std::hypot(position[0] - radius[0], position[1] - radius[1]));
    if (position[2] >= 0 && position[2] <= surfaceDepth && distance <= screenRadius) {
      result.annotation = static_cast<int>(i);
      surfaceDepth = position[2];
    }
  }
  return result;
}

const std::vector<std::uint8_t>& Scene::pixels() const {
  return pixels_;
}

} // namespace vv
