#pragma once

#include <string>
#include <vector>
#include <vtkDataSet.h>
#include <vtkSmartPointer.h>

struct MeshGroup {
  std::string name;
  std::vector<size_t> partIndices;
};

struct LoadedMeshes {
  std::vector<vtkSmartPointer<vtkDataSet>> meshes;
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
