#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

// Color data shared by the renderer's lookup tables and the Qt overlays. It
// depends on neither VTK nor Qt, so both sides read one definition.

// Category value -> RGB.
using CategoryColors = std::map<double, std::array<double, 3>>;

inline constexpr std::array<double, 3> kCategoryNanColor = {
    85.0 / 255.0, 85.0 / 255.0, 85.0 / 255.0};

// The colors a categorical field's categories take, in ascending value order.
struct CategoryPalette {
  std::string_view id;
  std::string_view label;
  std::string_view detail; // what sets it apart, for a tooltip
  std::vector<std::array<double, 3>> colors;
};

// Every categorical palette, Default first: the one a categorical field starts on.
const std::vector<CategoryPalette>& categoryPalettes();

// nullptr when no palette has this id.
const CategoryPalette* findCategoryPalette(std::string_view id);

// Picks a field's own colors again (see CategoryColoring::customColors).
inline constexpr std::string_view kCustomPaletteId = "custom";

// The rank-th color of palette. Past its last color the palette starts over, in turn
// mixed 35 % toward white, 35 % toward black, 60 % toward white and 60 % toward black,
// then from its own colors again.
std::array<double, 3> paletteColor(const CategoryPalette& palette, size_t rank);

// How a categorical field is colored: a palette, or the user's colors over one.
struct CategoryColoring {
  const CategoryPalette* palette = &categoryPalettes().front(); // drawn unless custom
  bool custom = false; // draws customColors over customBase
  const CategoryPalette* customBase = &categoryPalettes().front();
  CategoryColors customColors; // kept when a palette is picked, so Custom can come back
  std::array<double, 3> nanColor = kCategoryNanColor; // independent of the finite palette
};

// The color of the category with this value, the rank-th lowest of its field. Default
// takes fileColors' color for it where the file gives one.
std::array<double, 3> categoryColor(const CategoryColoring& coloring,
                                    const CategoryColors& fileColors,
                                    double value,
                                    size_t rank);

// Shows the palette with this id, or the custom colors for kCustomPaletteId; false when
// no palette has the id or there are no custom colors.
bool pickPalette(CategoryColoring& coloring, std::string_view id);

// Gives one category its own color, which makes the coloring custom: over the palette
// shown, as a new set, unless it is custom already. std::nullopt gives the category back
// its palette color, and the palette itself once no category has its own.
// NaN edits or resets nanColor independently, without entering customColors.
void recolorCategory(CategoryColoring& coloring,
                     double value,
                     const std::optional<std::array<double, 3>>& rgb);

enum class ColormapGroup { Sequential, Diverging, Cyclic };

// A continuous colormap: RGB stops evenly spaced from t = 0 to t = 1.
struct Colormap {
  std::string_view id;
  std::string_view label;
  ColormapGroup group;
  std::vector<std::array<double, 3>> stops;
};

// Every continuous colormap, Sequential first, then Diverging, then Cyclic. The
// first map of a group is the default for fields of that kind.
const std::vector<Colormap>& colormaps();

// nullptr when no colormap has this id.
const Colormap* findColormap(std::string_view id);

const Colormap& defaultColormap(ColormapGroup group);

// The color at t, clamped to [0, 1], interpolated between the two nearest stops.
// reversed runs the map from its last stop to its first.
std::array<double, 3> sampleColormap(const Colormap& map, double t, bool reversed);

// How values outside the clip window are drawn: in the colormap's end colors, or gray.
enum class OutOfRange { Clamp, Gray };

inline constexpr std::array<double, 3> kOutOfRangeGray = {0.5, 0.5, 0.5};

// The colormap a continuous field is drawn with. map points into colormaps().
struct ColormapChoice {
  const Colormap* map = &defaultColormap(ColormapGroup::Sequential);
  bool reversed = false;
  OutOfRange outside = OutOfRange::Clamp;
};
