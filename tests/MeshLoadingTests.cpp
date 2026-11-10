#include "MeshLoading.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkFieldData.h>
#include <vtkImageData.h>
#include <vtkMatrix4x4.h>
#include <vtkNIFTIImageWriter.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkStringArray.h>

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

// sformScale stretches the sform's x axis; 0 makes it degenerate.
bool writeNifti(const std::string& path, int scalarType, double sformScale) {
  vtkNew<vtkImageData> image;
  image->SetDimensions(6, 6, 6);
  image->SetSpacing(2.0, 2.0, 2.0);
  image->AllocateScalars(scalarType, 1);
  for (int z = 0; z < 6; ++z) {
    for (int y = 0; y < 6; ++y) {
      for (int x = 0; x < 6; ++x) {
        const bool inner = x > 0 && x < 5 && y > 0 && y < 5 && z > 0 && z < 5;
        image->SetScalarComponentFromDouble(x, y, z, 0, inner ? (x < 3 ? 1.0 : 3.0) : 0.0);
      }
    }
  }
  vtkNew<vtkMatrix4x4> sform;
  sform->SetElement(0, 0, sformScale);
  sform->SetElement(0, 3, 100.0);
  vtkNew<vtkNIFTIImageWriter> writer;
  writer->SetInputData(image);
  writer->SetSFormMatrix(sform);
  writer->SetFileName(path.c_str());
  writer->Write();
  return writer->GetErrorCode() == 0;
}

// Each nonzero label becomes a named surface placed by the sform.
bool loadsNiftiLabelSurfaces(const std::string& path) {
  if (!writeNifti(path, VTK_UNSIGNED_CHAR, 1.0)) {
    return false;
  }
  const MeshLoadResult result = loadMeshes({path});
  if (!result.ok || result.meshes.partNames != std::vector<std::string>{"Label 1", "Label 3"}) {
    return false;
  }
  const double* bounds = result.meshes.meshes[0]->GetBounds();
  return bounds[0] > 100.0 && bounds[1] < 110.0 && result.volumes.size() == 1 &&
         result.volumes[0].volume.kind == VolumeKind::Labels &&
         result.volumes[0].volume.labels == std::vector<double>{1.0, 3.0} &&
         result.volumes[0].firstPart == 0;
}

// Other volumes load as a voxel grid alone, with no mesh or group.
bool loadsNiftiIntensityVolume(const std::string& path) {
  if (!writeNifti(path, VTK_FLOAT, 1.0)) {
    return false;
  }
  const MeshLoadResult result = loadMeshes({path});
  return result.ok && result.meshes.meshes.empty() && result.meshes.groups.empty() &&
         result.volumes.size() == 1 && result.volumes[0].volume.kind == VolumeKind::Intensity &&
         result.volumes[0].volume.toWorld->GetElement(0, 3) == 100.0;
}

// A degenerate sform cannot place the volume, which then keeps its voxel coordinates.
bool ignoresDegenerateNiftiSform(const std::string& path) {
  if (!writeNifti(path, VTK_FLOAT, 0.0)) {
    return false;
  }
  const MeshLoadResult result = loadMeshes({path});
  return result.ok && result.volumes.size() == 1 &&
         result.volumes[0].volume.toWorld->Determinant() == 1.0;
}

// Overlaid files share one group; parts of multipart files keep their file as a prefix.
bool overlaysFilesInOneGroup(const std::string& labelPath, const std::string& meshPath) {
  if (!writeNifti(labelPath, VTK_UNSIGNED_CHAR, 1.0)) {
    return false;
  }
  MeshLoadResult result = loadMeshes({labelPath, meshPath});
  if (!result.ok) {
    return false;
  }
  overlayGroups(result.meshes);

  const std::string labelName = labelPath.substr(labelPath.find_last_of("/\\") + 1);
  const std::string meshName = meshPath.substr(meshPath.find_last_of("/\\") + 1);
  return result.meshes.groups.size() == 1 &&
         result.meshes.groups[0].partIndices == std::vector<size_t>{0, 1, 2} &&
         result.meshes.groups[0].name == labelName + " + " + meshName &&
         result.meshes.partNames ==
             std::vector<std::string>{labelName + " · Label 1", labelName + " · Label 3", meshName};
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
  const std::string niftiPath = path + ".nii.gz";
  const bool niftiLabels = loadsNiftiLabelSurfaces(niftiPath);
  const bool niftiVolume = loadsNiftiIntensityVolume(niftiPath);
  const bool degenerateSform = ignoresDegenerateNiftiSform(niftiPath);
  const bool overlaid = overlaysFilesInOneGroup(niftiPath, path);
  std::remove(niftiPath.c_str());
  if (!niftiLabels || !niftiVolume || !degenerateSform || !overlaid) {
    std::cerr << "Incorrect NIfTI or overlay handling\n";
    std::remove(path.c_str());
    return 1;
  }
  return std::remove(path.c_str()) == 0 ? 0 : 1;
}
