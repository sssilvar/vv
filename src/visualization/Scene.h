#pragma once

#include "ScalarVizUtils.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>
#include <vtkActor.h>
#include <vtkFloatArray.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkStaticCellLocator.h>

namespace vv {

enum class Backend { WebGL, Software };
struct Annotation {
  std::array<double, 3> position;
  std::array<double, 3> color;
  double diameter;
};
struct Pick {
  int cell = -1;
  int annotation = -1;
  double scalar = std::numeric_limits<double>::quiet_NaN();
};
struct Statistics {
  std::uint32_t geometryUploads = 0;
  std::uint32_t scalarUploads = 0;
  std::uint32_t renders = 0;
};

class SoftwareRasterizer;

// Owns geometry and scalars. Callers may release input buffers after each update.
// Scalar-only updates retain topology, camera and GPU geometry buffers.
class Scene {
public:
  explicit Scene(Backend backend, const char* canvasSelector = nullptr);
  ~Scene();
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;

  bool setSurface(const float* positions,
                  std::size_t coordinates,
                  const std::uint32_t* indices,
                  std::size_t indexCount,
                  bool points);
  bool setScalar(const float* values, std::size_t count, bool cell);
  // Writable back buffer; commit validates and swaps ownership without a second copy.
  float* scalarBuffer(std::size_t count, bool cell);
  bool commitScalar(std::size_t count, bool cell);
  bool setLut(const float* rgba,
              std::size_t count,
              const double* stops,
              double min,
              double max,
              bool interpolate);
  bool setStyle(const std::array<double, 3>& color,
                double opacity,
                bool wireframe,
                double pointDiameter);
  bool setPlanes(const double* planes, std::size_t count);
  bool setAnnotations(const double* data, std::size_t count);
  bool resize(int width, int height);
  void render();
  // Rotate in degrees; pan in fractions of viewport; zoom is an exponential delta.
  void camera(double rotateX, double rotateY, double panX, double panY, double zoom, bool reset);
  std::array<double, 10> cameraState() const;
  bool setCameraState(const double* state);
  void reveal(const std::array<double, 3>& position);
  std::array<double, 3> project(const std::array<double, 3>& position);
  Pick pick(double x, double y);
  const std::vector<std::uint8_t>& pixels() const;
  const Statistics& statistics() const {
    return statistics_;
  }
  Backend backend() const {
    return backend_;
  }

private:
  friend class SoftwareRasterizer;
  void applyPlanes();
  Backend backend_;
  vtkSmartPointer<vtkRenderer> renderer_;
  vtkSmartPointer<vtkActor> actor_;
  vtkSmartPointer<vtkPolyDataMapper> mapper_;
  vtkSmartPointer<vtkFloatArray> scalarBack_;
  bool cameraTouched_ = false;
  vtkSmartPointer<vtkRenderWindow> window_;
  vtkSmartPointer<vtkPolyData> mesh_;
  vtkSmartPointer<vtkStaticCellLocator> locator_;
  vtkSmartPointer<vtkActor> annotationActor_;
  vtkSmartPointer<vtkLookupTable> lut_;
  std::unique_ptr<SoftwareRasterizer> software_;
  std::vector<Annotation> annotations_;
  std::vector<std::array<double, 4>> planes_;
  std::vector<std::uint8_t> pixels_;
  std::array<double, 3> color_ = {0.7, 0.7, 0.7};
  double opacity_ = 1.0;
  double pointDiameter_ = 2.0;
  bool wireframe_ = false;
  bool points_ = false;
  bool cellScalar_ = false;
  int width_ = 1;
  int height_ = 1;
  Statistics statistics_;
};

} // namespace vv
