#include "VTKMeshParser.h"

#include "mesh_utils.h"

#include <iostream>
#include <string>
#include <vtkDataSet.h>
#include <vtkDataSetReader.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkXMLPolyDataReader.h>

namespace {
enum class VTKFileType { None, Legacy, XML };
VTKFileType detectVTKFileType(const std::string& filename) {
  std::string header = readHeader(filename, 200);
  if (header.find("# vtk DataFile") != std::string::npos)
    return VTKFileType::Legacy;
  if (header.find("<VTKFile") != std::string::npos)
    return VTKFileType::XML;
  return VTKFileType::None;
}
} // namespace

VTKMeshParser::~VTKMeshParser() = default;

std::vector<vtkSmartPointer<vtkDataSet>> VTKMeshParser::parse(const std::string& filename) {
  std::vector<vtkSmartPointer<vtkDataSet>> polys;
  VTKFileType type = detectVTKFileType(filename);
  if (type == VTKFileType::None) {
    std::cerr << "Unrecognized VTK file magic: " << filename << '\n';
    return polys;
  }
  if (type == VTKFileType::XML) {
    vtkNew<vtkXMLPolyDataReader> reader;
    reader->SetFileName(filename.c_str());
    reader->Update();
    vtkPolyData* poly = reader->GetOutput();
    if (poly && poly->GetNumberOfPoints() > 0) {
      polys.push_back(poly);
      return polys;
    }
  }
  if (type == VTKFileType::Legacy) {
    // Generic legacy reader auto-detects POLYDATA, UNSTRUCTURED_GRID, etc.
    // Clipped meshes (e.g. from ParaView) become UNSTRUCTURED_GRID.
    vtkNew<vtkDataSetReader> reader;
    reader->SetFileName(filename.c_str());
    reader->Update();
    vtkDataSet* ds = reader->GetOutput();
    if (ds && ds->GetNumberOfPoints() > 0) {
      polys.push_back(ds);
      return polys;
    }
  }
  std::cerr << "Failed to read VTK file: " << filename << '\n';
  return polys;
}

bool VTKMeshParser::canParse(const std::string& filename) {
  VTKFileType type = detectVTKFileType(filename);
  return type != VTKFileType::None;
}
