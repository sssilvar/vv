#include "Scene.h"

#include <array>
#include <cstring>
#include <exception>
#include <memory>
#include <unordered_map>

namespace {
std::unordered_map<int, std::unique_ptr<vv::Scene>> scenes;
int nextId = 1;
std::array<double, 10> result{};

vv::Scene* scene(int id) {
  const auto found = scenes.find(id);
  return found == scenes.end() ? nullptr : found->second.get();
}

// Never let an allocation or VTK exception escape through the JS boundary.
template <class Action> int guarded(int id, Action action) {
  auto* target = scene(id);
  if (!target)
    return 0;
  try {
    return action(*target) ? 1 : 0;
  } catch (const std::exception&) {
    return 0;
  }
}
} // namespace

extern "C" {
int vv_create(int software, const char* selector) {
  if (scenes.size() >= 16 || !selector || std::strlen(selector) > 128 || nextId == 0x7fffffff)
    return 0;
  try {
    auto viewer = std::make_unique<vv::Scene>(software ? vv::Backend::Software : vv::Backend::WebGL,
                                              selector);
    const int id = nextId++;
    scenes.emplace(id, std::move(viewer));
    return id;
  } catch (const std::exception&) {
    return 0;
  }
}

void vv_destroy(int id) {
  scenes.erase(id);
}

int vv_surface(int id,
               const float* positions,
               unsigned coordinates,
               const std::uint32_t* indices,
               unsigned indexCount,
               int points) {
  return guarded(id, [&](auto& s) {
    return s.setSurface(positions, coordinates, indices, indexCount, points != 0);
  });
}
int vv_scalar(int id, const float* values, unsigned count, int cell) {
  return guarded(id, [&](auto& s) { return s.setScalar(values, count, cell != 0); });
}
float* vv_scalar_buffer(int id, unsigned count, int cell) {
  float* buffer = nullptr;
  guarded(id, [&](auto& s) {
    buffer = s.scalarBuffer(count, cell != 0);
    return buffer != nullptr;
  });
  return buffer;
}
int vv_scalar_commit(int id, unsigned count, int cell) {
  return guarded(id, [&](auto& s) { return s.commitScalar(count, cell != 0); });
}
const double* vv_camera_state(int id) {
  guarded(id, [&](auto& s) {
    result = s.cameraState();
    return true;
  });
  return result.data();
}
int vv_set_camera_state(int id, const double* state) {
  return guarded(id, [&](auto& s) { return s.setCameraState(state); });
}
int vv_lut(int id,
           const float* colors,
           unsigned count,
           const double* stops,
           double min,
           double max,
           int interpolate) {
  return guarded(
      id, [&](auto& s) { return s.setLut(colors, count, stops, min, max, interpolate != 0); });
}
int vv_style(int id, double r, double g, double b, double opacity, int wireframe, double diameter) {
  return guarded(id,
                 [&](auto& s) { return s.setStyle({r, g, b}, opacity, wireframe != 0, diameter); });
}
int vv_planes(int id, const double* planes, unsigned count) {
  return guarded(id, [&](auto& s) { return s.setPlanes(planes, count); });
}
int vv_annotations(int id, const double* annotations, unsigned count) {
  return guarded(id, [&](auto& s) { return s.setAnnotations(annotations, count); });
}
int vv_resize(int id, int width, int height) {
  return guarded(id, [&](auto& s) { return s.resize(width, height); });
}
int vv_render(int id) {
  return guarded(id, [&](auto& s) {
    s.render();
    return true;
  });
}
const std::uint8_t* vv_pixels(int id) {
  auto* s = scene(id);
  return s ? s->pixels().data() : nullptr;
}
void vv_camera(int id, double rx, double ry, double px, double py, double zoom, int reset) {
  guarded(id, [&](auto& s) {
    s.camera(rx, ry, px, py, zoom, reset != 0);
    return true;
  });
}
void vv_reveal(int id, double x, double y, double z) {
  guarded(id, [&](auto& s) {
    s.reveal({x, y, z});
    return true;
  });
}
const double* vv_project(int id, double x, double y, double z) {
  auto* s = scene(id);
  const auto position = s ? s->project({x, y, z}) : std::array<double, 3>{0, 0, 2};
  std::copy(position.begin(), position.end(), result.begin());
  return result.data();
}
const double* vv_pick(int id, double x, double y) {
  vv::Pick pick;
  guarded(id, [&](auto& target) {
    pick = target.pick(x, y);
    return true;
  });
  result[0] = pick.cell;
  result[1] = pick.annotation;
  result[2] = pick.scalar;
  return result.data();
}
const double* vv_stats(int id) {
  auto* s = scene(id);
  const auto stats = s ? s->statistics() : vv::Statistics{};
  result[0] = stats.geometryUploads;
  result[1] = stats.scalarUploads;
  result[2] = stats.renders;
  return result.data();
}
}
