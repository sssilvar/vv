#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

std::size_t visiblePixels(const std::vector<std::uint8_t>& pixels) {
  std::size_t count = 0;
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    count += pixels[i] != 0 || pixels[i + 1] != 0 || pixels[i + 2] != 0;
  }
  return count;
}
} // namespace

int main() {
  vv::Scene scene(vv::Backend::Software);
  const float positions[] = {-1, -1, 0, 1, -1, 0, 0, 1, 0};
  const std::uint32_t indices[] = {0, 1, 2};

  require(scene.resize(200, 160), "resize");
  require(scene.setSurface(positions, 9, indices, 3, false), "triangle");
  scene.render();

  const auto fullPixels = visiblePixels(scene.pixels());
  require(fullPixels > 1000, "software rendering produced no geometry");
  const auto center = scene.project({0, 0, 0});
  require(scene.pick(center[0], center[1]).cell == 0, "cell picking");

  const float values[] = {-80, -40, 40};
  const float colors[] = {1, 0, 0, 1, 0, 0, 1, 1};
  const double stops[] = {-80, 40};
  require(scene.setScalar(values, 3, false), "point scalars");
  require(scene.setLut(colors, 2, stops, -80, 40, true), "pinned LUT");
  scene.render();
  const auto previous = scene.pixels();
  const float frame[] = {40, 0, -80};
  require(scene.setScalar(frame, 3, false), "simulation frame update");
  scene.render();

  require(previous != scene.pixels(), "scalar-only update did not change image");
  require(scene.statistics().geometryUploads == 1, "scalar update rebuilt geometry");
  require(scene.statistics().scalarUploads == 2, "scalar updates not counted");

  const double plane[] = {1, 0, 0, 0};
  require(scene.setPlanes(plane, 1), "clip plane");
  scene.render();

  const auto clippedPixels = visiblePixels(scene.pixels());
  require(clippedPixels > fullPixels / 3 && clippedPixels < fullPixels * 2 / 3,
          "spatial clipping should retain half the triangle");
  require(scene.pick(center[0] - 20, center[1]).cell == -1, "clipped surface should not be picked");

  const float label[] = {7};
  const double category[] = {7};
  require(scene.setScalar(label, 1, true), "cell scalar");
  require(scene.setLut(colors, 1, category, 7, 7, false), "constant categorical LUT");
  scene.render();
  require(visiblePixels(scene.pixels()) > 1000, "constant scalar vanished");

  const auto beforeInvalid = scene.pixels();
  const std::uint32_t invalidIndex[] = {0, 1, 3};
  require(!scene.setSurface(positions, 9, invalidIndex, 3, false), "reject invalid index");
  require(!scene.resize(5000, 5000), "bound framebuffer memory");
  require(!scene.setScalar(values, 2, false), "reject wrong tuple count");
  const float infinity[] = {std::numeric_limits<float>::infinity()};
  require(!scene.setScalar(infinity, 1, true), "reject infinity");
  const double invalidPlane[] = {0, 0, 0, 1};
  require(!scene.setPlanes(invalidPlane, 1), "reject zero normal");
  scene.render();

  require(beforeInvalid == scene.pixels(), "invalid inputs changed scene state");

  require(scene.setPlanes(nullptr, 0), "clear clipping");
  require(scene.setScalar(nullptr, 0, false), "clear scalar");
  const double tag[] = {0, 0, -0.1, 1, 1, 1, 0.2};
  require(scene.setAnnotations(tag, 1), "annotations");
  scene.render();
  const auto tagPosition = scene.project({0, 0, -0.1});

  require(scene.pick(tagPosition[0], tagPosition[1]).annotation == -1,
          "opaque surface should occlude tag");

  require(scene.setStyle({0.7, 0.7, 0.7}, 0.4, false, 2), "transparent surface");
  scene.render();

  require(scene.pick(tagPosition[0], tagPosition[1]).annotation == 0,
          "transparent surface should expose tag");
  const auto tagPixel =
      (static_cast<std::size_t>(tagPosition[1]) * 200 + static_cast<std::size_t>(tagPosition[0])) *
      4;
  require(scene.pixels()[tagPixel] > 180,
          "software transparency should reveal the bright tag behind the surface");

  require(scene.setSurface(positions, 9, nullptr, 0, true), "unindexed points");
  require(scene.setPlanes(nullptr, 0), "clear clipping");
  scene.render();

  require(visiblePixels(scene.pixels()) > 0, "unindexed point cloud vanished");

  vv::Scene categorical(vv::Backend::Software);
  const float labels[] = {1, 3, 1};
  const double categories[] = {1, 3};
  require(categorical.resize(400, 300), "categorical viewport");
  require(categorical.setSurface(positions, 9, indices, 3, false), "categorical triangle");
  require(categorical.setScalar(labels, 3, false), "categorical points");
  require(categorical.setLut(colors, 2, categories, 1, 3, false), "categorical colors");
  categorical.render();
  const auto unclipped = categorical.pixels();

  require(categorical.setPlanes(plane, 1), "clip categorical triangle");
  categorical.render();

  for (std::size_t i = 0; i < unclipped.size(); i += 4) {
    const auto& clipped = categorical.pixels();
    if (clipped[i] == 0 && clipped[i + 1] == 0 && clipped[i + 2] == 0)
      continue;
    for (std::size_t c = 0; c < 3; ++c)
      require(std::abs(int(clipped[i + c]) - int(unclipped[i + c])) <= 1,
              "clipping must preserve categorical colors");
  }

  const float transparent[] = {1, 0, 0, 0, 0, 0, 1, 0};
  require(categorical.setPlanes(nullptr, 0), "clear categorical clipping");
  require(categorical.setLut(transparent, 2, categories, 1, 3, false), "transparent LUT");
  require(categorical.setAnnotations(tag, 1), "tag through transparent LUT");
  categorical.render();
  const auto tagScreen = categorical.project({0, 0, -0.1});

  require(categorical.pick(tagScreen[0], tagScreen[1]).annotation == 0,
          "LUT transparency must expose annotation picking");

  const auto homeCamera = categorical.cameraState();
  categorical.camera(30, 20, 0.1, -0.1, 0.2, false);
  const auto camera = categorical.cameraState();
  const auto projected = categorical.project({0, 0, 0});
  categorical.camera(0, 0, 0, 0, 0, true);
  require(categorical.cameraState() == homeCamera, "reset restores the home camera pose");
  require(categorical.setCameraState(camera.data()), "restore camera");
  categorical.render();
  const auto restored = categorical.project({0, 0, 0});

  require(std::hypot(projected[0] - restored[0], projected[1] - restored[1]) < 1e-8,
          "camera restoration must preserve projection");

  float* buffer = categorical.scalarBuffer(3, false);
  require(buffer != nullptr, "direct scalar transfer buffer");
  std::copy(labels, labels + 3, buffer);
  require(categorical.commitScalar(3, false), "commit direct transfer");
  categorical.render();
  const auto validFrame = categorical.pixels();
  buffer = categorical.scalarBuffer(3, false);
  require(buffer != nullptr, "reuse direct transfer buffer");
  buffer[0] = std::numeric_limits<float>::infinity();

  require(!categorical.commitScalar(3, false), "reject invalid back buffer");
  categorical.render();
  require(validFrame == categorical.pixels(), "invalid frame must retain the displayed scalar");
  const double movedTag[] = {0.4, 0, -0.1, 1, 1, 1, 0.2};
  require(categorical.setAnnotations(movedTag, 1), "update annotation buffers");
  categorical.render();
  const auto movedScreen = categorical.project({0.4, 0, -0.1});
  require(categorical.pick(movedScreen[0], movedScreen[1]).annotation == 0,
          "moving annotation remains pickable");
  require(categorical.setAnnotations(nullptr, 0), "clear annotation buffers");
  require(categorical.pick(movedScreen[0], movedScreen[1]).annotation == -1,
          "removed annotation must not be picked");

  vv::Scene layers(vv::Backend::Software);
  const float layeredPoints[] = {-1, -1, 0.1f, 1, -1, 0.1f, 0, 1, 0.1f,
                                  -1, -1, 0, 1, -1, 0, 0, 1, 0};
  const std::uint32_t layeredIndices[] = {0, 1, 2, 3, 4, 5};
  const float layeredLabels[] = {1, 3};
  const float layeredColors[] = {1, 0, 0, 0, 0, 0, 1, 1};
  require(layers.resize(200, 160), "layered viewport");
  require(layers.setSurface(layeredPoints, 18, layeredIndices, 6, false), "layered surface");
  require(layers.setScalar(layeredLabels, 2, true), "layered categories");
  require(layers.setLut(layeredColors, 2, categories, 1, 3, false), "layered alpha");
  require(layers.setAnnotations(tag, 1), "tag behind opaque back layer");
  layers.render();
  const auto behind = layers.project({0, 0, -0.1});

  require(layers.pick(behind[0], behind[1]).annotation == -1,
          "transparent foreground must not hide opaque-layer occlusion");
  require(layers.pick(behind[0], behind[1]).cell == 1,
          "fully transparent cells must not intercept picking");
  std::cout << "Scene checks passed\n";
}
