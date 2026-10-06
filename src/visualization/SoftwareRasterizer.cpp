#include "SoftwareRasterizer.h"

#include "Scene.h"
#include "SceneLighting.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkMatrix4x4.h>
#include <vtkMath.h>
#include <vtkPointData.h>
#include <vtkProperty.h>

namespace vv {
namespace {
struct Vertex {
  std::array<double, 4> clip;
  std::array<double, 3> world;
  double scalar;
  double light;
  double highlight;
  std::array<double, 4> color{};
};

Vertex mix(const Vertex& a, const Vertex& b, double t) {
  Vertex result{};
  for (std::size_t j = 0; j < 4; ++j)
    result.clip[j] = a.clip[j] + (b.clip[j] - a.clip[j]) * t;
  for (std::size_t j = 0; j < 3; ++j)
    result.world[j] = a.world[j] + (b.world[j] - a.world[j]) * t;
  result.scalar = a.scalar + (b.scalar - a.scalar) * t;
  for (std::size_t j = 0; j < 4; ++j)
    result.color[j] = a.color[j] + (b.color[j] - a.color[j]) * t;
  result.light = a.light + (b.light - a.light) * t;
  result.highlight = a.highlight + (b.highlight - a.highlight) * t;
  return result;
}

// A clipped triangle has at most 15 vertices with six frustum and six world planes.
using Polygon = std::array<Vertex, 16>;
template <class Distance> std::size_t clip(Polygon& polygon, std::size_t count, Distance distance) {
  if (count == 0)
    return 0;
  Polygon output{};
  std::size_t written = 0;
  Vertex previous = polygon[count - 1];
  double previousDistance = distance(previous);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& current = polygon[i];
    const double currentDistance = distance(current);
    if ((currentDistance >= 0) != (previousDistance >= 0)) {
      output[written++] =
          mix(previous, current, previousDistance / (previousDistance - currentDistance));
    }
    if (currentDistance >= 0)
      output[written++] = current;
    previous = current;
    previousDistance = currentDistance;
  }
  polygon = output;
  return written;
}

double edge(double ax, double ay, double bx, double by, double x, double y) {
  return (x - ax) * (by - ay) - (y - ay) * (bx - ax);
}
} // namespace

void SoftwareRasterizer::render(Scene& scene) {
  const int width = scene.width_, height = scene.height_;
  const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  scene.pixels_.resize(pixelCount * 4);
  depth_.assign(pixelCount, std::numeric_limits<float>::infinity());
  for (std::size_t i = 0; i < pixelCount; ++i) {
    scene.pixels_[i * 4] = 0;
    scene.pixels_[i * 4 + 1] = 0;
    scene.pixels_[i * 4 + 2] = 0;
    scene.pixels_[i * 4 + 3] = 255;
  }
  auto* mesh = scene.mesh_.GetPointer();
  auto* camera = scene.renderer_.GetPointer()->GetActiveCamera();
  auto* matrix =
      camera->GetCompositeProjectionTransformMatrix(static_cast<double>(width) / height, -1, 1);
  auto* normals = mesh->GetPointData()->GetNormals();
  auto* mapper = scene.mapper_.GetPointer();
  auto* values = mapper->GetScalarVisibility()
                     ? (scene.cellScalar_ ? mesh->GetCellData()->GetArray("vv.scalar")
                                          : mesh->GetPointData()->GetArray("vv.scalar"))
                     : nullptr;
  const bool transparent = scene.opacity_ < 1 || (values && scene.lut_ && !scene.lut_->IsOpaque());
  if (transparent) {
    const Fragment empty{std::numeric_limits<float>::infinity(), {0, 0, 0, 0}};
    layers_.assign(pixelCount, {empty, empty, empty, empty});
  } else {
    layers_.clear();
  }
  auto directions = lighting::directions;
  for (auto& direction : directions)
    vtkMath::Normalize(direction.data());
  auto* viewMatrix = camera->GetViewTransformMatrix();

  auto vertex = [&](vtkIdType id, vtkIdType cell) {
    Vertex result{};
    mesh->GetPoint(id, result.world.data());
    const double world[4] = {result.world[0], result.world[1], result.world[2], 1};
    matrix->MultiplyPoint(world, result.clip.data());
    result.scalar = values ? values->GetComponent(scene.cellScalar_ ? cell : id, 0) : 0;
    if (values && scene.lut_ && scene.lut_->GetIndexedLookup()) {
      const auto* mapped = scene.lut_->MapValue(result.scalar);
      for (std::size_t j = 0; j < 4; ++j)
        result.color[j] = mapped[j] / 255.0;
    }
    result.light = 1;
    if (normals && !scene.wireframe_) {
      double normal[4] = {0, 0, 0, 0};
      normals->GetTuple(id, normal);
      double n[4], position[4];
      viewMatrix->MultiplyPoint(normal, n);
      viewMatrix->MultiplyPoint(world, position);
      double eye[3] = {-position[0], -position[1], -position[2]};
      vtkMath::Normalize(eye);
      vtkMath::Normalize(n);
      if (vtkMath::Dot(n, eye) < 0)
        for (int j = 0; j < 3; ++j)
          n[j] = -n[j];
      result.light = lighting::ambient;
      for (std::size_t i = 0; i < directions.size(); ++i) {
        const double cosine = std::max(0.0, vtkMath::Dot(n, directions[i].data()));
        result.light += lighting::diffuse * lighting::intensities[i] * cosine;
        if (cosine > 0) {
          const double reflected = 2 * cosine * vtkMath::Dot(n, eye) -
                                   vtkMath::Dot(directions[i].data(), eye);
          result.highlight += lighting::specular * lighting::intensities[i] *
                              std::pow(std::max(0.0, reflected), lighting::power);
        }
      }
    }
    return result;
  };
  auto colorAt = [&](double value, double light) {
    std::array<double, 4> color = {
        scene.color_[0], scene.color_[1], scene.color_[2], scene.opacity_};
    if (values && scene.lut_) {
      const auto* mapped = scene.lut_->MapValue(value);
      for (std::size_t j = 0; j < 3; ++j)
        color[j] = mapped[j] / 255.0;
      color[3] *= mapped[3] / 255.0;
    }
    for (std::size_t j = 0; j < 3; ++j)
      color[j] *= light;
    return color;
  };
  auto write = [&](int x, int y, double depth, const std::array<double, 4>& color) {
    if (x < 0 || y < 0 || x >= width || y >= height || !std::isfinite(depth) || depth < -1 ||
        depth > 1)
      return;
    const auto i =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    if (color[3] <= 0)
      return;
    if (transparent) {
      auto& layers = layers_[i];
      std::size_t insertion = 0;
      while (insertion < layers.size() && depth >= layers[insertion].depth)
        ++insertion;
      if (insertion == layers.size())
        return;
      for (std::size_t j = layers.size() - 1; j > insertion; --j)
        layers[j] = layers[j - 1];
      layers[insertion].depth = static_cast<float>(depth);
      for (std::size_t j = 0; j < 4; ++j)
        layers[insertion].color[j] =
            static_cast<std::uint8_t>(std::clamp(color[j], 0.0, 1.0) * 255 + 0.5);
      return;
    }
    if (depth >= depth_[i])
      return;
    depth_[i] = static_cast<float>(depth);
    for (std::size_t j = 0; j < 3; ++j) {
      const double background = scene.pixels_[i * 4 + j] / 255.0;
      scene.pixels_[i * 4 + j] = static_cast<std::uint8_t>(
          std::clamp(color[j] * color[3] + background * (1 - color[3]), 0.0, 1.0) * 255 + 0.5);
    }
  };
  auto screen = [&](const Vertex& v) {
    const double inverseW = 1 / v.clip[3];
    return std::array<double, 4>{(v.clip[0] * inverseW + 1) * width * 0.5,
                                 (1 - v.clip[1] * inverseW) * height * 0.5,
                                 v.clip[2] * inverseW,
                                 inverseW};
  };
  auto triangle = [&](const Vertex& a, const Vertex& b, const Vertex& c) {
    const auto sa = screen(a), sb = screen(b), sc = screen(c);
    const double area = edge(sa[0], sa[1], sb[0], sb[1], sc[0], sc[1]);
    if (!std::isfinite(area) || std::abs(area) < 1e-10)
      return;
    const int left = static_cast<int>(std::clamp(
        std::floor(std::min({sa[0], sb[0], sc[0]})), 0.0, static_cast<double>(width - 1)));
    const int right = static_cast<int>(std::clamp(
        std::ceil(std::max({sa[0], sb[0], sc[0]})), 0.0, static_cast<double>(width - 1)));
    const int top = static_cast<int>(std::clamp(
        std::floor(std::min({sa[1], sb[1], sc[1]})), 0.0, static_cast<double>(height - 1)));
    const int bottom = static_cast<int>(std::clamp(
        std::ceil(std::max({sa[1], sb[1], sc[1]})), 0.0, static_cast<double>(height - 1)));
    const double edgeA = std::hypot(sb[0] - sc[0], sb[1] - sc[1]);
    const double edgeB = std::hypot(sc[0] - sa[0], sc[1] - sa[1]);
    const double edgeC = std::hypot(sa[0] - sb[0], sa[1] - sb[1]);
    for (int y = top; y <= bottom; ++y) {
      for (int x = left; x <= right; ++x) {
        const double wa = edge(sb[0], sb[1], sc[0], sc[1], x + 0.5, y + 0.5) / area;
        const double wb = edge(sc[0], sc[1], sa[0], sa[1], x + 0.5, y + 0.5) / area;
        const double wc = 1 - wa - wb;
        if (wa < 0 || wb < 0 || wc < 0)
          continue;
        if (scene.wireframe_ && std::min({wa * std::abs(area) / edgeA,
                                          wb * std::abs(area) / edgeB,
                                          wc * std::abs(area) / edgeC}) > 0.9)
          continue;
        const double depth = wa * sa[2] + wb * sb[2] + wc * sc[2];
        const double denominator = wa * sa[3] + wb * sb[3] + wc * sc[3];
        const double scalar =
            (wa * sa[3] * a.scalar + wb * sb[3] * b.scalar + wc * sc[3] * c.scalar) / denominator;
        const double light =
            (wa * sa[3] * a.light + wb * sb[3] * b.light + wc * sc[3] * c.light) / denominator *
            (area < 0 && !scene.wireframe_ ? 0.45 : 1.0);
        auto color = colorAt(scalar, light);
        if (values && !scene.cellScalar_ && scene.lut_ && scene.lut_->GetIndexedLookup()) {
          const auto& ca = a.color;
          const auto& cb = b.color;
          const auto& cc = c.color;
          for (std::size_t j = 0; j < 4; ++j) {
            color[j] = (wa * sa[3] * ca[j] + wb * sb[3] * cb[j] + wc * sc[3] * cc[j]) / denominator;
            color[j] *= j < 3 ? light : scene.opacity_;
          }
        }
        const double highlight =
            (wa * sa[3] * a.highlight + wb * sb[3] * b.highlight + wc * sc[3] * c.highlight) /
            denominator * (area < 0 ? 0.45 : 1.0);
        for (std::size_t j = 0; j < 3; ++j)
          color[j] += highlight;
        write(x, y, depth, color);
      }
    }
  };
  auto dot = [&](const Vertex& v, double radius, const std::array<double, 4>& color) {
    if (v.clip[3] <= 0)
      return;
    const auto s = screen(v);
    radius = std::clamp(radius, 0.5, 2048.0);
    const int left = static_cast<int>(
        std::clamp(std::floor(s[0] - radius), 0.0, static_cast<double>(width - 1)));
    const int right =
        static_cast<int>(std::clamp(std::ceil(s[0] + radius), 0.0, static_cast<double>(width - 1)));
    const int top = static_cast<int>(
        std::clamp(std::floor(s[1] - radius), 0.0, static_cast<double>(height - 1)));
    const int bottom = static_cast<int>(
        std::clamp(std::ceil(s[1] + radius), 0.0, static_cast<double>(height - 1)));
    for (int y = top; y <= bottom; ++y) {
      for (int x = left; x <= right; ++x) {
        if (std::hypot(x + 0.5 - s[0], y + 0.5 - s[1]) <= radius)
          write(x, y, s[2], color);
      }
    }
  };
  if (scene.points_) {
    vtkIdType count;
    const vtkIdType* ids;
    auto* cells = mesh->GetVerts();
    cells->InitTraversal();
    vtkIdType cell = 0;
    while (cells->GetNextCell(count, ids)) {
      const auto v = vertex(ids[0], cell++);
      const bool kept = std::all_of(scene.planes_.begin(), scene.planes_.end(), [&](const auto& p) {
        return p[0] * v.world[0] + p[1] * v.world[1] + p[2] * v.world[2] + p[3] >= 0;
      });
      if (kept)
        dot(v, scene.pointDiameter_ * 0.5, colorAt(v.scalar, 1));
    }
  } else {
    vtkIdType count;
    const vtkIdType* ids;
    auto* cells = mesh->GetPolys();
    cells->InitTraversal();
    vtkIdType cell = 0;
    while (cells->GetNextCell(count, ids)) {
      Polygon polygon{};
      for (std::size_t j = 0; j < 3; ++j)
        polygon[j] = vertex(ids[j], cell);
      ++cell;
      std::size_t size = 3;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        size = clip(polygon, size, [axis](const auto& v) { return v.clip[3] + v.clip[axis]; });
        size = clip(polygon, size, [axis](const auto& v) { return v.clip[3] - v.clip[axis]; });
      }
      for (const auto& p : scene.planes_)
        size = clip(polygon, size, [&](const auto& v) {
          return p[0] * v.world[0] + p[1] * v.world[1] + p[2] * v.world[2] + p[3];
        });
      for (std::size_t j = 1; j + 1 < size; ++j)
        triangle(polygon[0], polygon[j], polygon[j + 1]);
    }
  }
  const double* up = camera->GetViewUp();
  for (const auto& annotation : scene.annotations_) {
    Vertex v{};
    v.world = annotation.position;
    const double position[4] = {v.world[0], v.world[1], v.world[2], 1};
    matrix->MultiplyPoint(position, v.clip.data());
    const auto center = scene.project(annotation.position);
    auto edgePosition = annotation.position;
    for (std::size_t j = 0; j < 3; ++j)
      edgePosition[j] += up[j] * annotation.diameter * 0.5;
    const auto rim = scene.project(edgePosition);
    dot(v,
        std::hypot(center[0] - rim[0], center[1] - rim[1]),
        {annotation.color[0], annotation.color[1], annotation.color[2], 1});
  }
  if (transparent) {
    for (std::size_t i = 0; i < pixelCount; ++i) {
      std::array<double, 3> color = {0, 0, 0};
      double transmission = 1;
      for (const auto& layer : layers_[i]) {
        const double alpha = layer.color[3] / 255.0;
        for (std::size_t j = 0; j < 3; ++j)
          color[j] += transmission * alpha * layer.color[j];
        transmission *= 1 - alpha;
      }
      for (std::size_t j = 0; j < 3; ++j) {
        scene.pixels_[i * 4 + j] = static_cast<std::uint8_t>(
            std::clamp(color[j] + transmission * scene.pixels_[i * 4 + j], 0.0, 255.0) + 0.5);
      }
    }
  }
}
} // namespace vv
