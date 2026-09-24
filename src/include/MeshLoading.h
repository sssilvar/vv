#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>
#include <vtkDataSet.h>
#include <vtkSmartPointer.h>

class TemporalSource;

struct MeshGroup {
  std::string name;
  std::vector<size_t> partIndices;
};

struct LoadedMeshes {
  std::vector<vtkSmartPointer<vtkDataSet>> meshes;
  std::vector<std::string> names;
  std::vector<std::string> partNames;
  std::vector<std::array<double, 3>> partColors;
  std::vector<bool> partHasColors;
  std::vector<MeshGroup> groups;
};

struct MeshLoadResult {
  bool ok = false;
  int exitCode = 0;
  std::string error;
  LoadedMeshes meshes;
  // Non-null and playable when a temporal file was loaded; meshes[temporalMesh] is
  // the rendered object playback streams successive frames into. Only the first
  // temporal file plays.
  std::shared_ptr<TemporalSource> temporal;
  size_t temporalMesh = 0;
};

MeshLoadResult loadMeshes(const std::vector<std::string>& meshfiles);
