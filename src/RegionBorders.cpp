#include "RegionBorders.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkDoubleArray.h>
#include <vtkGenericCell.h>
#include <vtkIdList.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPointSet.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>

namespace {

using Point = std::array<double, 3>;

bool same(double x, double y) {
  return x == y || (std::isnan(x) && std::isnan(y));
}

Point between(const Point& a, const Point& b) {
  return {(a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0, (a[2] + b[2]) / 2.0};
}

// A face's corners in order around it: a quadratic face's mid-edge points come after its
// corners, and a pixel's corners run in rows.
void corners(vtkCell* face, vtkIdList* out) {
  const vtkIdType count = face->IsLinear() ? face->GetNumberOfPoints() : face->GetNumberOfEdges();
  out->SetNumberOfIds(count);
  for (vtkIdType i = 0; i < count; ++i) {
    out->SetId(i, face->GetPointIds()->GetId(i));
  }
  if (face->GetCellType() == VTK_PIXEL) {
    const vtkIdType third = out->GetId(2);
    out->SetId(2, out->GetId(3));
    out->SetId(3, third);
  }
}

using FaceKey = std::array<vtkIdType, 3>; // a face's three lowest points

struct FaceKeyHash {
  size_t operator()(const FaceKey& key) const {
    size_t hash = 0;
    for (const vtkIdType id : key) {
      hash = hash * 1000003U ^ std::hash<vtkIdType>{}(id);
    }
    return hash;
  }
};

FaceKey faceKey(vtkIdList* points) {
  FaceKey key{};
  std::partial_sort_copy(points->begin(), points->end(), key.begin(), key.end());
  return key;
}

// What a volume mesh shows: its surface cells and the faces of its solid cells that no
// other cell shares, on the mesh's points, with the cell each lies on. Faces match by their
// three lowest points. VTK 9.1's surface filters leak on this path, so it is done here.
vtkSmartPointer<vtkPolyData> outerFaces(vtkDataSet* mesh, std::vector<vtkIdType>& cells) {
  auto* pointSet = vtkPointSet::SafeDownCast(mesh);
  if (!pointSet) {
    return nullptr;
  }
  vtkNew<vtkGenericCell> cell;
  vtkNew<vtkIdList> points;
  const auto eachFace = [&](const auto& visit) {
    for (vtkIdType id = 0; id < mesh->GetNumberOfCells(); ++id) {
      mesh->GetCell(id, cell);
      if (cell->GetCellDimension() == 2 && cell->GetCellType() != VTK_TRIANGLE_STRIP) {
        corners(cell, points);
        visit(id, false);
      }
      for (int face = 0; cell->GetCellDimension() == 3 && face < cell->GetNumberOfFaces(); ++face) {
        corners(cell->GetFace(face), points);
        visit(id, true);
      }
    }
  };
  std::unordered_map<FaceKey, int, FaceKeyHash> shared;
  eachFace([&](vtkIdType, bool solid) {
    if (solid && points->GetNumberOfIds() >= 3) {
      ++shared[faceKey(points)];
    }
  });
  auto surface = vtkSmartPointer<vtkPolyData>::New();
  surface->SetPoints(pointSet->GetPoints());
  vtkNew<vtkCellArray> polys;
  eachFace([&](vtkIdType id, bool solid) {
    if (points->GetNumberOfIds() >= 3 && (!solid || shared[faceKey(points)] == 1)) {
      polys->InsertNextCell(points);
      cells.push_back(id);
    }
  });
  surface->SetPolys(polys);
  return surface;
}

} // namespace

RegionBorders::RegionBorders(vtkDataSet* mesh)
    : lines_(vtkSmartPointer<vtkPolyData>::New()), values_(vtkSmartPointer<vtkDoubleArray>::New()) {
  if (!mesh) {
    return;
  }
  meshPoints_ = mesh->GetNumberOfPoints();
  meshCells_ = mesh->GetNumberOfCells();
  // A polygonal mesh is its own surface; anything else draws its outer faces.
  surface_ = vtkPolyData::SafeDownCast(mesh);
  std::vector<vtkIdType> outerCells;
  if (!surface_) {
    surface_ = outerFaces(mesh, outerCells);
    if (!surface_) {
      return;
    }
  }

  vtkNew<vtkIdList> points;
  faceStarts_.push_back(0);
  for (vtkIdType cell = 0; cell < surface_->GetNumberOfCells(); ++cell) {
    const int type = surface_->GetCellType(cell);
    // Faces whose points run around their border; a strip's do not.
    if (type != VTK_TRIANGLE && type != VTK_QUAD && type != VTK_POLYGON && type != VTK_PIXEL) {
      continue;
    }
    surface_->GetCellPoints(cell, points);
    const vtkIdType count = points->GetNumberOfIds();
    if (count < 3) {
      continue;
    }
    if (type == VTK_PIXEL) {
      const vtkIdType third = points->GetId(2);
      points->SetId(2, points->GetId(3));
      points->SetId(3, third);
    }
    faceCells_.push_back(outerCells.empty() ? cell : outerCells[static_cast<size_t>(cell)]);
    for (vtkIdType i = 0; i < count; ++i) {
      facePoints_.push_back(points->GetId(i));
    }
    faceStarts_.push_back(static_cast<vtkIdType>(facePoints_.size()));
  }
}

void RegionBorders::findEdges() {
  edgesFound_ = true;
  // Edges are keyed by their two points packed into 64 bits.
  if (!surface_ || surface_->GetNumberOfPoints() > std::numeric_limits<std::uint32_t>::max()) {
    return;
  }
  struct Side {
    vtkIdType a = 0;
    vtkIdType b = 0;
    vtkIdType cell = 0;
    bool open = true;
  };
  std::unordered_map<std::uint64_t, Side> sides;
  sides.reserve(facePoints_.size() / 2);
  for (size_t face = 0; face < faceCells_.size(); ++face) {
    const auto begin = static_cast<size_t>(faceStarts_[face]);
    const auto end = static_cast<size_t>(faceStarts_[face + 1]);
    for (size_t i = begin; i < end; ++i) {
      const vtkIdType a = facePoints_[i];
      const vtkIdType b = facePoints_[i + 1 < end ? i + 1 : begin];
      const auto low = static_cast<std::uint64_t>(std::min(a, b));
      const auto high = static_cast<std::uint64_t>(std::max(a, b));
      const auto [found, added] = sides.try_emplace(low << 32 | high, Side{a, b, faceCells_[face]});
      Side& side = found->second;
      // The first two faces at an edge; one cell's own faces never differ.
      if (!added && side.open) {
        side.open = false;
        if (side.cell != faceCells_[face]) {
          edges_.push_back({side.a, side.b, side.cell, faceCells_[face]});
        }
      }
    }
  }
}

void RegionBorders::update(vtkDataArray* labels, FieldAssociation association) {
  vtkNew<vtkPoints> ends;
  vtkNew<vtkCellArray> borders;
  const auto line = [&ends, &borders](const Point& from, const Point& to) {
    const vtkIdType ids[2] = {ends->InsertNextPoint(from.data()), ends->InsertNextPoint(to.data())};
    borders->InsertNextCell(2, ids);
  };
  const auto at = [this](vtkIdType point) {
    Point position;
    surface_->GetPoint(point, position.data());
    return position;
  };
  const bool cells = association == FieldAssociation::Cell;
  if (surface_ && labels && labels->GetNumberOfComponents() == 1 &&
      labels->GetNumberOfTuples() == (cells ? meshCells_ : meshPoints_)) {
    values_->DeepCopy(labels);
    const double* value = values_->GetPointer(0);
    if (cells) {
      if (!edgesFound_) {
        findEdges();
      }
      for (const Edge& edge : edges_) {
        if (!same(value[edge.left], value[edge.right])) {
          line(at(edge.a), at(edge.b));
        }
      }
    } else {
      const std::vector<vtkIdType>& ids = facePoints_;
      std::vector<Point> crossings;
      for (size_t face = 0; face + 1 < faceStarts_.size(); ++face) {
        const auto begin = static_cast<size_t>(faceStarts_[face]);
        const auto end = static_cast<size_t>(faceStarts_[face + 1]);
        const double first = value[ids[begin]];
        size_t i = begin + 1;
        while (i < end && same(value[ids[i]], first)) {
          ++i;
        }
        if (i == end) {
          continue;
        }
        crossings.clear();
        Point center{0.0, 0.0, 0.0};
        for (i = begin; i < end; ++i) {
          const size_t next = i + 1 < end ? i + 1 : begin;
          const Point here = at(facePoints_[i]);
          for (size_t axis = 0; axis < 3; ++axis) {
            center[axis] += here[axis] / static_cast<double>(end - begin);
          }
          if (!same(value[ids[i]], value[ids[next]])) {
            crossings.push_back(between(here, at(facePoints_[next])));
          }
        }
        // Two labels part along one line; more meet in the middle of the face.
        if (crossings.size() == 2) {
          line(crossings[0], crossings[1]);
        } else {
          for (const Point& crossing : crossings) {
            line(crossing, center);
          }
        }
      }
    }
  }
  lines_->SetPoints(ends);
  lines_->SetLines(borders);
}
