#pragma once

#include <string>
#include <vector>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

struct MeshGroup {
  std::string name;
  std::vector<size_t> partIndices;
};

struct LoadedMeshes {
  std::vector<vtkSmartPointer<vtkPolyData>> polys;
  std::vector<std::string> names;
  std::vector<std::string> partNames;
  std::vector<MeshGroup> groups;
};

struct MeshLoadResult {
  bool ok = false;
  int exitCode = 0;
  std::string error;
  LoadedMeshes meshes;
};

MeshLoadResult loadMeshes(const std::vector<std::string>& meshfiles, bool explodeView);
