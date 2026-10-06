#pragma once

#include <array>

namespace vv::lighting {
// Camera-space directions keep the light above-left as the object rotates.
inline constexpr std::array<std::array<double, 3>, 2> directions = {
    {{-0.65, 0.8, 1.0}, {0.7, -0.3, 1.0}}};
inline constexpr std::array<double, 2> intensities = {0.8, 0.2};
inline constexpr double ambient = 0.18;
inline constexpr double diffuse = 0.75;
inline constexpr double specular = 0.12;
inline constexpr double power = 24;
} // namespace vv::lighting
