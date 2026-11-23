#pragma once

#include <algorithm>
#include <cstddef>

// The smallest tile a split pane keeps, in logical pixels: the mesh keeps most of it beside
// a compact legend (200 x 53), which takes at most a sixth of it.
constexpr int kMinFacetPanelWidth = 280;
constexpr int kMinFacetPanelHeight = 220;
// A page's fields at most: a 3 x 3 grid, as many legends as a glance compares.
constexpr std::size_t kMaxFacetPage = 9;

// Columns of a grid of count cells in width x height that leave each cell the most room
// in its shorter direction; on a tie, the one with fewer empty cells.
inline std::size_t adaptiveGridCols(std::size_t count, int width, int height) {
  std::size_t bestCols = 1;
  std::size_t bestEmpty = count;
  double bestSize = 0.0;
  for (std::size_t cols = 1; cols <= count; ++cols) {
    const std::size_t rows = 1 + (count - 1) / cols;
    const double size =
        std::min(static_cast<double>(std::max(1, width)) / static_cast<double>(cols),
                 static_cast<double>(std::max(1, height)) / static_cast<double>(rows));
    const std::size_t empty = (cols - count % cols) % cols;
    if (size > bestSize || (size == bestSize && empty <= bestEmpty)) {
      bestCols = cols;
      bestEmpty = empty;
      bestSize = size;
    }
  }
  return bestCols;
}

// Fields a page of a split pane shows in width x height, in logical pixels: as many as keep
// every tile of their adaptive grid at least kMinFacetPanelWidth x kMinFacetPanelHeight,
// at most kMaxFacetPage and at least one. The pages are then evened out, so 13 fields
// that fit 9 at a time page as 7 and 6.
inline std::size_t facetPageSize(std::size_t fields, int width, int height) {
  if (fields <= 1) {
    return fields;
  }
  const auto fits = [width, height](std::size_t size) {
    const std::size_t cols = adaptiveGridCols(size, width, height);
    const std::size_t rows = (size + cols - 1) / cols;
    return static_cast<std::size_t>(std::max(width, 0)) / cols >=
               static_cast<std::size_t>(kMinFacetPanelWidth) &&
           static_cast<std::size_t>(std::max(height, 0)) / rows >=
               static_cast<std::size_t>(kMinFacetPanelHeight);
  };
  std::size_t size = std::min(fields, kMaxFacetPage);
  while (size > 1 && !fits(size)) {
    --size;
  }
  const std::size_t pages = (fields + size - 1) / size;
  const std::size_t even = (fields + pages - 1) / pages;
  return fits(even) ? even : size;
}
