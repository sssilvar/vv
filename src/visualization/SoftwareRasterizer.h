#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace vv {
class Scene;

class SoftwareRasterizer {
public:
  void render(Scene& scene);

private:
  struct Fragment {
    float depth;
    std::array<std::uint8_t, 4> color;
  };
  std::vector<float> depth_;
  std::vector<std::array<Fragment, 4>> layers_;
};
} // namespace vv
