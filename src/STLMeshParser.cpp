#include "STLMeshParser.h"

#include "mesh_utils.h"

#include <iostream>
#include <string>
#include <vector>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkSTLReader.h>

// Binary STL has no magic number (its 80-byte header may even begin with
// "solid", like ASCII STL), so the extension is the only reliable signal.
bool STLMeshParser::canParse(const std::string& filename) {
  return endsWithIgnoreCase(filename, ".stl");
}

std::vector<vtkSmartPointer<vtkDataSet>> STLMeshParser::parse(const std::string& filename) {
  std::vector<vtkSmartPointer<vtkDataSet>> polys;
  // Handles ASCII and binary; merges the per-facet duplicate vertices by default.
  vtkNew<vtkSTLReader> reader;
  reader->SetFileName(filename.c_str());
  reader->Update();
  vtkPolyData* poly = reader->GetOutput();
  if (poly == nullptr || poly->GetNumberOfPoints() == 0) {
    std::cerr << "Failed to read STL file: " << filename << '\n';
    return polys;
  }
  polys.emplace_back(poly);
  return polys;
}
