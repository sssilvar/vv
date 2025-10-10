#include "VTKMeshParser.h"
#include <vtkPolyDataReader.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <string>
#include <iostream>
#include <vtkPointData.h>
#include "mesh_utils.h"

VTKMeshParser::~VTKMeshParser() = default;

std::vector<vtkSmartPointer<vtkPolyData>> VTKMeshParser::parse(const std::string &filename)
{
    std::vector<vtkSmartPointer<vtkPolyData>> polys;
    // Try VTP (XML) format first
    {
        vtkNew<vtkXMLPolyDataReader> reader;
        reader->SetFileName(filename.c_str());
        reader->Update();
        vtkPolyData *poly = reader->GetOutput();
        if (poly && poly->GetNumberOfPoints() > 0) {
            polys.push_back(poly);
            return polys;
        }
    }
    // Fall back to legacy VTK format
    {
        vtkNew<vtkPolyDataReader> reader;
        reader->SetFileName(filename.c_str());
        reader->Update();
        vtkPolyData *poly = reader->GetOutput();
        if (poly && poly->GetNumberOfPoints() > 0) {
            polys.push_back(poly);
            return polys;
        }
    }
    std::cerr << "Failed to read VTK file: " << filename << std::endl;
    return polys;
}

bool VTKMeshParser::canParse(const std::string &filename)
{
    std::string header = readHeader(filename, 200);
    return header.find("# vtk DataFile") != std::string::npos ||
           header.find("<VTKFile") != std::string::npos;
}
