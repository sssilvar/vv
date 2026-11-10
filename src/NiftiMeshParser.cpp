#include "NiftiMeshParser.h"

#include "mesh_utils.h"

#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#include <vtkDataArray.h>
#include <vtkDiscreteFlyingEdges3D.h>
#include <vtkImageData.h>
#include <vtkMatrix4x4.h>
#include <vtkNIFTIImageReader.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkStringArray.h>
#include <vtkTransform.h>
#include <vtkTransformFilter.h>

namespace {

// More distinct values than this means an intensity image (such as CT), not a label map.
constexpr std::size_t kMaxLabels = 256;

bool isIntegerType(int type) {
  switch (type) {
  case VTK_CHAR:
  case VTK_SIGNED_CHAR:
  case VTK_UNSIGNED_CHAR:
  case VTK_SHORT:
  case VTK_UNSIGNED_SHORT:
  case VTK_INT:
  case VTK_UNSIGNED_INT:
  case VTK_LONG:
  case VTK_UNSIGNED_LONG:
  case VTK_LONG_LONG:
  case VTK_UNSIGNED_LONG_LONG:
    return true;
  default:
    return false;
  }
}

// Nonzero labels in ascending order; false when there are more than kMaxLabels.
// Labels occupy long runs, so skipping repeats avoids most set lookups.
bool collectLabels(vtkDataArray* scalars, std::set<double>& labels) {
  const vtkIdType count = scalars->GetNumberOfTuples();
  double previous = 0.0;
  for (vtkIdType i = 0; i < count; ++i) {
    const double value = scalars->GetComponent(i, 0);
    if (value == previous || value == 0.0)
      continue;
    previous = value;
    if (labels.insert(value).second && labels.size() > kMaxLabels)
      return false;
  }
  return true;
}

// The voxel-to-world mapping, preferring sform over qform as NIfTI readers conventionally
// do; null without a usable one. A degenerate matrix would collapse the volume.
vtkMatrix4x4* worldMatrix(vtkNIFTIImageReader* reader) {
  for (vtkMatrix4x4* matrix : {reader->GetSFormMatrix(), reader->GetQFormMatrix()}) {
    if (matrix && std::abs(matrix->Determinant()) > 0.0)
      return matrix;
  }
  return nullptr;
}

std::vector<vtkSmartPointer<vtkDataSet>> labelSurfaces(vtkImageData* image,
                                                       const std::set<double>& labels,
                                                       vtkTransform* toWorld,
                                                       std::vector<double>& partLabels) {
  std::vector<vtkSmartPointer<vtkDataSet>> parts;
  vtkNew<vtkDiscreteFlyingEdges3D> surfaces;
  surfaces->SetInputData(image);
  surfaces->ComputeNormalsOff();
  surfaces->ComputeGradientsOff();
  surfaces->ComputeScalarsOff();
  vtkNew<vtkTransformFilter> transform;
  transform->SetInputConnection(surfaces->GetOutputPort());
  transform->SetTransform(toWorld);

  for (const double label : labels) {
    surfaces->SetValue(0, label);
    transform->Update();
    if (transform->GetPolyDataOutput()->GetNumberOfCells() == 0)
      continue;
    auto part = vtkSmartPointer<vtkPolyData>::New();
    part->ShallowCopy(transform->GetPolyDataOutput());
    vtkNew<vtkStringArray> name;
    name->SetName("vv_part_name");
    name->InsertNextValue("Label " + std::to_string(static_cast<long long>(label)));
    part->GetFieldData()->AddArray(name);
    parts.emplace_back(part);
    partLabels.push_back(label);
  }
  return parts;
}

} // namespace

bool NiftiMeshParser::canParse(const std::string& filename) {
  return endsWithIgnoreCase(filename, ".nii") || endsWithIgnoreCase(filename, ".nii.gz");
}

std::vector<vtkSmartPointer<vtkDataSet>> NiftiMeshParser::parse(const std::string& filename) {
  volume_.reset();
  vtkNew<vtkNIFTIImageReader> reader;
  if (!reader->CanReadFile(filename.c_str())) {
    std::cerr << "Not a NIfTI file: " << filename << '\n';
    return {};
  }
  reader->SetFileName(filename.c_str());
  reader->Update();
  vtkImageData* image = reader->GetOutput();
  vtkDataArray* scalars = image ? image->GetPointData()->GetScalars() : nullptr;
  if (!scalars || scalars->GetNumberOfTuples() == 0) {
    std::cerr << "Failed to read NIfTI file: " << filename << '\n';
    return {};
  }
  if (scalars->GetNumberOfComponents() != 1) {
    std::cerr << "Only single-component 3D NIfTI volumes are supported: " << filename << '\n';
    return {};
  }

  Volume grid;
  // Detached from the reader, which goes away with this call.
  grid.image = vtkSmartPointer<vtkImageData>::New();
  grid.image->ShallowCopy(image);
  grid.toWorld = vtkSmartPointer<vtkMatrix4x4>::New();
  if (vtkMatrix4x4* matrix = worldMatrix(reader))
    grid.toWorld->DeepCopy(matrix);
  vtkNew<vtkTransform> toWorld;
  toWorld->SetMatrix(grid.toWorld);

  const double slope = reader->GetRescaleSlope();
  std::set<double> labels;
  const bool labelMap = isIntegerType(scalars->GetDataType()) && (slope == 0.0 || slope == 1.0) &&
                        reader->GetRescaleIntercept() == 0.0 && collectLabels(scalars, labels);
  std::vector<vtkSmartPointer<vtkDataSet>> parts;
  if (labelMap) {
    grid.kind = VolumeKind::Labels;
    parts = labelSurfaces(image, labels, toWorld, grid.labels);
    if (parts.empty()) {
      std::cerr << "NIfTI label map has no labeled voxels: " << filename << '\n';
      return {};
    }
  }
  volume_ = std::move(grid);
  return parts;
}
