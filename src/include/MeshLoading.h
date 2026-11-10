#pragma once
#include "Volume.h"

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

// A file's voxel grid, shown in the slice views.
struct LoadedVolume {
  Volume volume;
  std::string name;     // the file's base name
  size_t firstPart = 0; // Labels: the mesh index of volume.labels' first part
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
  std::vector<LoadedVolume> volumes;
  size_t temporalMesh = 0;
};

MeshLoadResult loadMeshes(const std::vector<std::string>& meshfiles);

// Merges every file into one group so they share a view. Parts of a multipart file
// keep their file name as a prefix to stay distinguishable.
void overlayGroups(LoadedMeshes& meshes);
