#pragma once
#include "MeshLoading.h"

#include <array>
#include <cstdint>
#include <vector>
#include <vtkImageData.h>
#include <vtkImageProperty.h>
#include <vtkImageSlice.h>
#include <vtkLookupTable.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkSmartPointer.h>

// Slice views by the world axis they look along, in RAS order.
enum class SliceOrientation { Sagittal = 0, Coronal = 1, Axial = 2 };

// What the slice views and the 3-D view share: the reference voxel grid (the first
// intensity volume, else the first label map), label parts resampled onto it, the
// crosshair cursor and the grayscale window.
class VolumeScene {
public:
  // volumes must not be empty. partColors: every mesh's color, so labels match their
  // surfaces.
  VolumeScene(const std::vector<LoadedVolume>& volumes,
              const std::vector<std::array<double, 3>>& partColors);

  // Null without an intensity volume.
  vtkImageData* intensity() const {
    return intensity_;
  }
  vtkMatrix4x4* toWorld() const {
    return toWorld_;
  }
  const std::array<int, 3>& dimensions() const {
    return dims_;
  }
  const std::array<double, 3>& spacing() const {
    return spacing_;
  }
  // The voxel axis the view slices across: the one closest to its world axis.
  int voxelAxis(SliceOrientation view) const {
    return voxelAxis_[static_cast<size_t>(view)];
  }

  const std::array<int, 3>& cursor() const {
    return cursor_;
  }
  // Clamped to the grid; false when nothing changed.
  bool setCursor(const std::array<int, 3>& voxel);
  // World position of a voxel's centre, and the voxel nearest a world point.
  std::array<double, 3> voxelToWorld(const std::array<double, 3>& voxel) const;
  std::array<int, 3> worldToVoxel(const std::array<double, 3>& world) const;
  std::array<double, 6> worldBounds() const;

  // Shared by every grayscale slice, in the slice views and the 3-D view.
  vtkImageProperty* grayProperty() const {
    return grayProperty_;
  }
  // Window and level change by these fractions of the initial window.
  void adjustWindowLevel(double windowDelta, double levelDelta);

  bool hasLabels() const {
    return !partIds_.empty();
  }
  // Mesh parts (+1, 0 for none) in one slice, filled or as outlines one voxel wide.
  // out gets the slice's single-plane extent on the reference grid. Requires hasLabels().
  void labelSlice(int axis, int index, vtkImageData* out) const;
  // Part id to color; hidden parts and id 0 are transparent.
  vtkLookupTable* labelTable() const {
    return labelTable_;
  }
  void setOutline(bool outline); // filled masks are translucent
  bool outline() const {
    return outline_;
  }
  void setPartVisible(size_t part, bool visible);

  // The cursor's three planes for the 3-D view; empty without an intensity volume.
  const std::vector<vtkSmartPointer<vtkImageSlice>>& planes() const {
    return planes_;
  }

private:
  void buildPartIds(const std::vector<LoadedVolume>& volumes, const Volume& reference);
  void updateLabelTable();

  vtkSmartPointer<vtkImageData> intensity_;
  vtkNew<vtkMatrix4x4> toWorld_;
  vtkNew<vtkMatrix4x4> fromWorld_;
  std::array<int, 3> dims_{};
  std::array<double, 3> spacing_{};
  std::array<int, 3> voxelAxis_{};
  std::array<int, 3> cursor_{};
  vtkNew<vtkImageProperty> grayProperty_;
  double initialWindow_ = 1.0;

  std::vector<std::uint16_t> partIds_; // reference grid, x fastest; empty without labels
  std::vector<std::array<double, 3>> partColors_;
  std::vector<bool> partVisible_;
  bool outline_ = true;
  vtkNew<vtkLookupTable> labelTable_;
  std::vector<vtkSmartPointer<vtkImageSlice>> planes_;
};
