#include "VolumeScene.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vtkDataArray.h>
#include <vtkImageReslice.h>
#include <vtkImageSliceMapper.h>
#include <vtkPointData.h>

namespace {

// Translucent enough to read the image through a filled mask.
constexpr double kFillOpacity = 0.45;

const Volume& referenceVolume(const std::vector<LoadedVolume>& volumes) {
  const auto intensity = std::find_if(volumes.begin(), volumes.end(), [](const LoadedVolume& v) {
    return v.volume.kind == VolumeKind::Intensity;
  });
  return (intensity != volumes.end() ? *intensity : volumes.front()).volume;
}

// The permutation of voxel axes that best follows the world axes, so an oblique
// grid still gets one view per voxel axis.
std::array<int, 3> matchVoxelAxes(vtkMatrix4x4* toWorld) {
  std::array<int, 3> axes{0, 1, 2};
  std::array<int, 3> best = axes;
  double bestScore = -1.0;
  do {
    double score = 0.0;
    for (int world = 0; world < 3; ++world) {
      const int voxel = axes[static_cast<size_t>(world)];
      double norm = 0.0;
      for (int row = 0; row < 3; ++row) {
        norm += toWorld->GetElement(row, voxel) * toWorld->GetElement(row, voxel);
      }
      score += std::abs(toWorld->GetElement(world, voxel)) / std::max(std::sqrt(norm), 1e-12);
    }
    if (score > bestScore) {
      bestScore = score;
      best = axes;
    }
  } while (std::next_permutation(axes.begin(), axes.end()));
  return best;
}

// The 1st and 99th percentiles, from at most about a million voxels.
std::array<double, 2> robustRange(vtkDataArray* scalars) {
  const vtkIdType count = scalars->GetNumberOfTuples();
  const vtkIdType stride = std::max<vtkIdType>(1, count / 1'000'000);
  std::vector<double> samples;
  samples.reserve(static_cast<size_t>(count / stride + 1));
  for (vtkIdType i = 0; i < count; i += stride) {
    const double value = scalars->GetComponent(i, 0);
    if (std::isfinite(value)) {
      samples.push_back(value);
    }
  }
  if (samples.empty()) {
    return {0.0, 1.0};
  }
  const auto at = [&samples](double fraction) {
    const auto nth = samples.begin() + static_cast<std::ptrdiff_t>(
                                           fraction * static_cast<double>(samples.size() - 1));
    std::nth_element(samples.begin(), nth, samples.end());
    return *nth;
  };
  const double low = at(0.01);
  const double high = at(0.99);
  return {low, high > low ? high : low + 1.0};
}

bool sameGrid(const Volume& a, const Volume& b) {
  const auto close = [](double x, double y) { return std::abs(x - y) <= 1e-6; };
  const int* dims = a.image->GetDimensions();
  const double* spacing = a.image->GetSpacing();
  const double* toWorld = a.toWorld->GetData();
  return std::equal(dims, dims + 3, b.image->GetDimensions()) &&
         std::equal(spacing, spacing + 3, b.image->GetSpacing(), close) &&
         std::equal(toWorld, toWorld + 16, b.toWorld->GetData(), close);
}

} // namespace

VolumeScene::VolumeScene(const std::vector<LoadedVolume>& volumes,
                         const std::vector<std::array<double, 3>>& partColors)
    : partColors_(partColors), partVisible_(partColors.size(), true) {
  const Volume& reference = referenceVolume(volumes);
  toWorld_->DeepCopy(reference.toWorld);
  vtkMatrix4x4::Invert(toWorld_, fromWorld_);
  reference.image->GetDimensions(dims_.data());
  reference.image->GetSpacing(spacing_.data());
  voxelAxis_ = matchVoxelAxes(toWorld_);
  for (size_t axis = 0; axis < 3; ++axis) {
    cursor_[axis] = dims_[axis] / 2;
  }

  if (reference.kind == VolumeKind::Intensity) {
    intensity_ = reference.image;
    const auto range = robustRange(intensity_->GetPointData()->GetScalars());
    initialWindow_ = range[1] - range[0];
    grayProperty_->SetColorWindow(initialWindow_);
    grayProperty_->SetColorLevel(0.5 * (range[0] + range[1]));
    grayProperty_->SetInterpolationTypeToLinear();
    for (size_t view = 0; view < 3; ++view) {
      vtkNew<vtkImageSliceMapper> mapper;
      mapper->SetInputData(intensity_);
      mapper->SetOrientation(voxelAxis_[view]);
      mapper->SetSliceNumber(cursor_[static_cast<size_t>(voxelAxis_[view])]);
      auto plane = vtkSmartPointer<vtkImageSlice>::New();
      plane->SetMapper(mapper);
      plane->SetProperty(grayProperty_);
      plane->SetUserMatrix(toWorld_);
      planes_.push_back(plane);
    }
  }

  buildPartIds(volumes, reference);
  labelTable_->SetNumberOfTableValues(static_cast<vtkIdType>(partColors_.size() + 1));
  labelTable_->SetTableRange(0.0, static_cast<double>(partColors_.size()));
  updateLabelTable();
}

void VolumeScene::buildPartIds(const std::vector<LoadedVolume>& volumes, const Volume& reference) {
  const size_t voxels =
      static_cast<size_t>(dims_[0]) * static_cast<size_t>(dims_[1]) * static_cast<size_t>(dims_[2]);
  for (const LoadedVolume& loaded : volumes) {
    const Volume& volume = loaded.volume;
    if (volume.kind != VolumeKind::Labels || volume.labels.empty() ||
        loaded.firstPart + volume.labels.size() >= 0xFFFF) {
      continue;
    }
    std::map<double, std::uint16_t> ids;
    for (size_t i = 0; i < volume.labels.size(); ++i) {
      ids[volume.labels[i]] = static_cast<std::uint16_t>(loaded.firstPart + i + 1);
    }

    // Label maps on another grid are sampled at the reference voxels' world positions.
    vtkSmartPointer<vtkImageData> onGrid = volume.image;
    if (!sameGrid(volume, reference)) {
      vtkNew<vtkMatrix4x4> fromLabelWorld;
      vtkMatrix4x4::Invert(volume.toWorld, fromLabelWorld);
      vtkNew<vtkMatrix4x4> toLabel;
      vtkMatrix4x4::Multiply4x4(fromLabelWorld, toWorld_, toLabel);
      vtkNew<vtkImageReslice> reslice;
      reslice->SetInputData(volume.image);
      reslice->SetResliceAxes(toLabel);
      reslice->SetInterpolationModeToNearestNeighbor();
      reslice->SetOutputExtent(0, dims_[0] - 1, 0, dims_[1] - 1, 0, dims_[2] - 1);
      reslice->SetOutputSpacing(spacing_.data());
      reslice->SetOutputOrigin(0.0, 0.0, 0.0);
      reslice->Update();
      onGrid = reslice->GetOutput();
    }

    vtkDataArray* scalars = onGrid->GetPointData()->GetScalars();
    partIds_.resize(voxels);
    double previous = 0.0;
    std::uint16_t previousId = 0;
    for (size_t i = 0; i < voxels; ++i) {
      const double value = scalars->GetComponent(static_cast<vtkIdType>(i), 0);
      if (value != previous) {
        previous = value;
        const auto found = ids.find(value);
        previousId = found == ids.end() ? 0 : found->second;
      }
      if (previousId != 0) {
        partIds_[i] = previousId;
      }
    }
  }
}

void VolumeScene::updateLabelTable() {
  labelTable_->SetTableValue(0, 0.0, 0.0, 0.0, 0.0);
  const double alpha = outline_ ? 1.0 : kFillOpacity;
  for (size_t part = 0; part < partColors_.size(); ++part) {
    const auto& rgb = partColors_[part];
    labelTable_->SetTableValue(
        static_cast<vtkIdType>(part + 1), rgb[0], rgb[1], rgb[2], partVisible_[part] ? alpha : 0.0);
  }
  labelTable_->Modified();
}

bool VolumeScene::setCursor(const std::array<int, 3>& voxel) {
  std::array<int, 3> clamped{};
  for (size_t axis = 0; axis < 3; ++axis) {
    clamped[axis] = std::clamp(voxel[axis], 0, dims_[axis] - 1);
  }
  if (clamped == cursor_) {
    return false;
  }
  cursor_ = clamped;
  for (size_t view = 0; view < planes_.size(); ++view) {
    auto* mapper = static_cast<vtkImageSliceMapper*>(planes_[view]->GetMapper());
    mapper->SetSliceNumber(cursor_[static_cast<size_t>(voxelAxis_[view])]);
  }
  return true;
}

std::array<double, 3> VolumeScene::voxelToWorld(const std::array<double, 3>& voxel) const {
  std::array<double, 4> point{
      voxel[0] * spacing_[0], voxel[1] * spacing_[1], voxel[2] * spacing_[2], 1.0};
  toWorld_->MultiplyPoint(point.data(), point.data());
  return {point[0], point[1], point[2]};
}

std::array<int, 3> VolumeScene::worldToVoxel(const std::array<double, 3>& world) const {
  std::array<double, 4> point{world[0], world[1], world[2], 1.0};
  fromWorld_->MultiplyPoint(point.data(), point.data());
  std::array<int, 3> voxel{};
  for (size_t axis = 0; axis < 3; ++axis) {
    voxel[axis] =
        std::clamp(static_cast<int>(std::lround(point[axis] / spacing_[axis])), 0, dims_[axis] - 1);
  }
  return voxel;
}

std::array<double, 6> VolumeScene::worldBounds() const {
  std::array<double, 6> bounds{1e300, -1e300, 1e300, -1e300, 1e300, -1e300};
  for (int corner = 0; corner < 8; ++corner) {
    const auto world = voxelToWorld({(corner & 1) ? dims_[0] - 1.0 : 0.0,
                                     (corner & 2) ? dims_[1] - 1.0 : 0.0,
                                     (corner & 4) ? dims_[2] - 1.0 : 0.0});
    for (size_t axis = 0; axis < 3; ++axis) {
      bounds[2 * axis] = std::min(bounds[2 * axis], world[axis]);
      bounds[2 * axis + 1] = std::max(bounds[2 * axis + 1], world[axis]);
    }
  }
  return bounds;
}

void VolumeScene::adjustWindowLevel(double windowDelta, double levelDelta) {
  const double window = grayProperty_->GetColorWindow() + windowDelta * initialWindow_;
  grayProperty_->SetColorWindow(std::max(window, 1e-3 * initialWindow_));
  grayProperty_->SetColorLevel(grayProperty_->GetColorLevel() + levelDelta * initialWindow_);
}

void VolumeScene::labelSlice(int axis, int index, vtkImageData* out) const {
  std::array<int, 6> extent{0, dims_[0] - 1, 0, dims_[1] - 1, 0, dims_[2] - 1};
  extent[static_cast<size_t>(2 * axis)] = index;
  extent[static_cast<size_t>(2 * axis + 1)] = index;
  out->SetExtent(extent.data());
  out->SetSpacing(spacing_.data());
  out->AllocateScalars(VTK_UNSIGNED_SHORT, 1);
  auto* pixel = static_cast<std::uint16_t*>(out->GetScalarPointer());

  const std::array<size_t, 3> stride{1,
                                     static_cast<size_t>(dims_[0]),
                                     static_cast<size_t>(dims_[0]) * static_cast<size_t>(dims_[1])};
  const auto u = static_cast<size_t>(axis == 0 ? 1 : 0);
  const auto v = static_cast<size_t>(axis == 2 ? 1 : 2);
  // Whether the part ends at this voxel along an in-plane axis; the grid's border ends every part.
  const auto onEdge = [&](const std::array<int, 3>& voxel, size_t at, size_t along) {
    return voxel[along] == 0 || voxel[along] == dims_[along] - 1 ||
           partIds_[at - stride[along]] != partIds_[at] ||
           partIds_[at + stride[along]] != partIds_[at];
  };
  std::array<int, 3> voxel{};
  for (voxel[2] = extent[4]; voxel[2] <= extent[5]; ++voxel[2]) {
    for (voxel[1] = extent[2]; voxel[1] <= extent[3]; ++voxel[1]) {
      for (voxel[0] = extent[0]; voxel[0] <= extent[1]; ++voxel[0]) {
        const size_t at = static_cast<size_t>(voxel[0]) +
                          static_cast<size_t>(voxel[1]) * stride[1] +
                          static_cast<size_t>(voxel[2]) * stride[2];
        const bool inside = outline_ && !onEdge(voxel, at, u) && !onEdge(voxel, at, v);
        *pixel++ = inside ? 0 : partIds_[at];
      }
    }
  }
  out->Modified();
}

void VolumeScene::setOutline(bool outline) {
  outline_ = outline;
  updateLabelTable();
}

void VolumeScene::setPartVisible(size_t part, bool visible) {
  if (part < partVisible_.size()) {
    partVisible_[part] = visible;
    updateLabelTable();
  }
}
