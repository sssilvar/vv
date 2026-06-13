#include "VTKHDFMeshParser.h"

#include "TemporalSource.h"
#include "mesh_utils.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <vtkHDFReader.h>
#include <vtkInformation.h>
#include <vtkSmartPointer.h>
#include <vtkStreamingDemandDrivenPipeline.h>

namespace {

bool hasHDF5Magic(const std::string& filename) {
  // HDF5 superblock signature: \x89 H D F \r \n \x1a \n
  static const char kMagic[8] = {'\x89', 'H', 'D', 'F', '\r', '\n', '\x1a', '\n'};
  std::string header = readHeader(filename, 8);
  if (header.size() < 8) {
    return false;
  }
  return std::equal(std::begin(kMagic), std::end(kMagic), header.begin());
}

bool endsWithIgnoreCase(const std::string& value, const std::string& suffix) {
  if (value.size() < suffix.size()) {
    return false;
  }
  return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin(), [](char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) ==
           std::tolower(static_cast<unsigned char>(b));
  });
}

} // namespace

VTKHDFMeshParser::VTKHDFMeshParser() = default;
VTKHDFMeshParser::~VTKHDFMeshParser() = default;

bool VTKHDFMeshParser::canParse(const std::string& filename) {
  if (!endsWithIgnoreCase(filename, ".vtkhdf")) {
    return false;
  }
  return hasHDF5Magic(filename);
}

std::vector<vtkSmartPointer<vtkDataSet>> VTKHDFMeshParser::parse(const std::string& filename) {
  temporal_.reset();
  std::vector<vtkSmartPointer<vtkDataSet>> meshes;

  vtkSmartPointer<vtkHDFReader> reader = vtkSmartPointer<vtkHDFReader>::New();
  reader->SetFileName(filename.c_str());
  if (!reader->CanReadFile(filename.c_str())) {
    std::cerr << "Not a readable VTKHDF file: " << filename << '\n';
    return meshes;
  }
  reader->UpdateInformation();

  // Collect the available time steps (if any) from the pipeline.
  std::vector<double> timeValues;
  if (vtkInformation* outInfo = reader->GetOutputInformation(0)) {
    if (outInfo->Has(vtkStreamingDemandDrivenPipeline::TIME_STEPS())) {
      const int n = outInfo->Length(vtkStreamingDemandDrivenPipeline::TIME_STEPS());
      double* values = outInfo->Get(vtkStreamingDemandDrivenPipeline::TIME_STEPS());
      timeValues.assign(values, values + n);
    }
  }

  // Read the first step for display.
  reader->Update();
  auto* output = vtkDataSet::SafeDownCast(reader->GetOutputDataObject(0));
  if (!output || output->GetNumberOfPoints() == 0) {
    std::cerr << "Failed to read VTKHDF dataset: " << filename << '\n';
    return meshes;
  }

  // Persistent dataset the mappers point at; playback shallow-copies new frames in.
  vtkSmartPointer<vtkDataSet> mesh;
  mesh.TakeReference(vtkDataSet::SafeDownCast(output->NewInstance()));
  mesh->ShallowCopy(output);
  meshes.push_back(mesh);

  if (timeValues.size() > 1) {
    temporal_ = std::make_shared<TemporalSource>();
    temporal_->init(reader, std::move(timeValues));
  }

  return meshes;
}
