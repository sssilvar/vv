#include "VTKMeshParser.h"
#include <vtkPolyDataReader.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <string>
#include <iostream>
#include <vtkPointData.h>
#include "mesh_utils.h"

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
}

VTKMeshParser::~VTKMeshParser() = default;

std::vector<vtkSmartPointer<vtkPolyData>> VTKMeshParser::parse(const std::string &filename)
{
    std::vector<vtkSmartPointer<vtkPolyData>> polys;
    VTKFileType type = detectVTKFileType(filename);
    if (type == VTKFileType::None)
    {
        std::cerr << "Unrecognized VTK file magic: " << filename << std::endl;
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
        vtkNew<vtkPolyDataReader> reader;
        reader->SetFileName(filename.c_str());
        reader->Update();
        vtkPolyData* poly = reader->GetOutput();
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
    VTKFileType type = detectVTKFileType(filename);
    return type != VTKFileType::None;
}
