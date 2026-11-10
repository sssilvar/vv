#pragma once
#include <vector>
#include <vtkImageData.h>
#include <vtkMatrix4x4.h>
#include <vtkSmartPointer.h>

enum class VolumeKind { Intensity, Labels };

// A voxel grid as read from file: image coordinates are index * spacing (origin 0),
// and toWorld maps them to the world coordinates meshes use.
struct Volume {
  VolumeKind kind = VolumeKind::Intensity;
  vtkSmartPointer<vtkImageData> image;
  vtkSmartPointer<vtkMatrix4x4> toWorld;
  // Labels only: the voxel value of each part the parser returned, in order.
  std::vector<double> labels;
};
