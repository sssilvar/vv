#pragma once

#include "ScalarField.h"

#include <vector>
#include <vtkSmartPointer.h>
#include <vtkType.h>

class vtkDataArray;
class vtkDataSet;
class vtkDoubleArray;
class vtkPolyData;

// Where a label array changes value on a mesh's surface, as lines. Cell labels change
// across an edge, which the line follows. Point labels change along an edge, so the line
// crosses each face whose points differ through the middles of those edges: halfway
// between the labels, where colors interpolated from the points turn from one to the other.
//
// The surface's faces are found once; update then reads only the labels, so it can run
// while a brush paints.
class RegionBorders {
public:
  explicit RegionBorders(vtkDataSet* mesh);

  // Recompute the lines from labels, read as association. An array that does not hold a
  // value per point or cell of the mesh leaves no lines.
  void update(vtkDataArray* labels, FieldAssociation association);

  // Changed in place by update, with points of its own.
  vtkPolyData* lines() const {
    return lines_;
  }

private:
  struct Edge {
    vtkIdType a = 0; // surface points
    vtkIdType b = 0;
    vtkIdType left = 0; // mesh cells on either side
    vtkIdType right = 0;
  };

  void findEdges();

  vtkSmartPointer<vtkPolyData> surface_;
  vtkSmartPointer<vtkPolyData> lines_;
  vtkSmartPointer<vtkDoubleArray> values_; // the labels as doubles, reused between updates
  vtkIdType meshPoints_ = 0;
  vtkIdType meshCells_ = 0;
  std::vector<vtkIdType> faceStarts_; // into the faces' points, one past the end last
  std::vector<vtkIdType> facePoints_; // surface points around each face
  std::vector<vtkIdType> faceCells_;  // the mesh cell each face lies on
  // Edges between two faces of different cells, found when cell labels first need them.
  std::vector<Edge> edges_;
  bool edgesFound_ = false;
};
