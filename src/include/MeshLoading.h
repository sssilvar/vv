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
  // Non-null and playable when a temporal VTKHDF file was loaded; meshes[0] is the
  // rendered object playback streams successive frames into.
  std::shared_ptr<TemporalSource> temporal;
};

MeshLoadResult loadMeshes(const std::vector<std::string>& meshfiles, bool explodeView);
