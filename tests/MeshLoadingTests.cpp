#include "MeshLoading.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vtkCellData.h>
#include <vtkPointData.h>

namespace {
// Legacy readers keep only the first SCALARS block per association unless told
// to read them all.
bool loadsEveryScalarBlock(const std::string& path) {
  {
    std::ofstream file(path);
    file << "# vtk DataFile Version 3.0\nMultiple scalar blocks regression\nASCII\n"
            "DATASET POLYDATA\nPOINTS 3 float\n0 0 0 1 0 0 0 1 0\nPOLYGONS 1 4\n3 0 1 2\n"
            "POINT_DATA 3\n"
            "SCALARS lat float 1\nLOOKUP_TABLE default\n0 1 2\n"
            "SCALARS phase float 1\nLOOKUP_TABLE default\n3 4 5\n"
            "CELL_DATA 1\n"
            "SCALARS af_sources int 1\nLOOKUP_TABLE default\n1\n";
    file.close();
    if (!file) {
      return false;
    }
  }

  const MeshLoadResult result = loadMeshes({path});
  if (!result.ok || result.meshes.meshes.size() != 1) {
    return false;
  }
  vtkDataSet* mesh = result.meshes.meshes[0];
  return mesh->GetPointData()->GetArray("lat") && mesh->GetPointData()->GetArray("phase") &&
         mesh->GetCellData()->GetArray("af_sources");
}
} // namespace

// The standard fixes main's parameters, so argv cannot be const.
// cppcheck-suppress constParameter
int main(int argc, char* argv[]) {
  if (argc != 2) {
    return 2;
  }
  const std::string path = argv[1];
  for (const int components : {2, 3, 4, 16}) {
    {
      std::ofstream file(path);
      file << "# vtk DataFile Version 3.0\nColor metadata regression\nASCII\n"
              "DATASET POLYDATA\nFIELD FieldData 1\nvv_part_color "
           << components << " 1 double\n";
      for (int i = 0; i < components; ++i) {
        file << (i == 1 ? 1.0 : 0.0) << ' ';
      }
      file << "\nPOINTS 3 double\n0 0 0 1 0 0 0 1 0\nPOLYGONS 1 4\n3 0 1 2\n";
      file.close();
      if (!file) {
        return 2;
      }
    }

    const MeshLoadResult result = loadMeshes({path});
    if (!result.ok || result.meshes.meshes.size() != 1 ||
        result.meshes.meshes[0]->GetNumberOfPoints() != 3 ||
        result.meshes.meshes[0]->GetNumberOfCells() != 1 ||
        result.meshes.partHasColors.size() != 1 ||
        result.meshes.partHasColors[0] != (components == 3) ||
        (components == 3 && result.meshes.partColors[0] != std::array<double, 3>{0.0, 1.0, 0.0})) {
      std::cerr << "Incorrect mesh color handling for " << components << " components\n";
      std::remove(path.c_str());
      return 1;
    }
  }

  if (!loadsEveryScalarBlock(path)) {
    std::cerr << "Legacy file with several SCALARS blocks lost an array\n";
    std::remove(path.c_str());
    return 1;
  }
  return std::remove(path.c_str()) == 0 ? 0 : 1;
}
