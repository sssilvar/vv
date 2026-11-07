#pragma once

#include <algorithm>
#include <cstddef>

// The smallest panel a page of the -e grid keeps, in logical pixels: the mesh keeps most
// of it beside a compact legend (200 x 53), which takes at most a sixth of it.
constexpr int kMinFacetPanelWidth = 280;
constexpr int kMinFacetPanelHeight = 220;
// One file's fields on a page at most: a 3 x 3 grid, as many legends as a glance compares.
constexpr std::size_t kMaxFacetPage = 9;

// Columns of a grid of count panels in width x height that leave each panel the most room
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

// Fields a page of the -e grid shows in a view width x height, in logical pixels. One
// file's fields share an adaptive grid of at most kMaxFacetPage panels; several files take
// a row each, with as many field columns as fit across. A page holds as many fields as
// keep every panel at least kMinFacetPanelWidth x kMinFacetPanelHeight, and at least one;
// with more than one page, pagerHeight at the bottom is left for the pager. The pages are
// then evened out, so 13 fields that fit 9 at a time page as 7 and 6.
inline std::size_t
facetPageSize(std::size_t fields, std::size_t files, int width, int height, int pagerHeight) {
  if (fields <= 1) {
    return fields;
  }
  const auto fits = [files, width](std::size_t size, int area) {
    const std::size_t cols =
        std::max<std::size_t>(1, files > 1 ? size : adaptiveGridCols(size, width, area));
    const std::size_t rows = std::max<std::size_t>(1, (size + cols - 1) / cols);
    const std::size_t across = static_cast<std::size_t>(std::max(width, 0)) / cols;
    const std::size_t down = static_cast<std::size_t>(std::max(area, 0)) / rows;
    // A file's row cannot move to another page, so with several files only columns count.
    return across >= static_cast<std::size_t>(kMinFacetPanelWidth) &&
           (files > 1 || down >= static_cast<std::size_t>(kMinFacetPanelHeight));
  };
  const std::size_t most = files > 1 ? fields : std::min(fields, kMaxFacetPage);
  const auto largest = [&fits, most](int area) {
    std::size_t size = most;
    while (size > 1 && !fits(size, area)) {
      --size;
    }
    return size;
  };
  std::size_t size = largest(height);
  if (size < fields) {
    size = largest(height - pagerHeight);
  }
  const std::size_t pages = (fields + size - 1) / size;
  const std::size_t even = (fields + pages - 1) / pages;
  return fits(even, height - pagerHeight) ? even : size;
}
