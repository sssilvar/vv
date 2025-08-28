#include "VTKMeshParser.h"
#include <vtkPolyDataReader.h>
#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <string>
#include <iostream>
#include <vtkPointData.h>

VTKMeshParser::~VTKMeshParser() = default;

std::vector<vtkSmartPointer<vtkPolyData>> VTKMeshParser::parse(const std::string &filename)
{
    std::vector<vtkSmartPointer<vtkPolyData>> polys;
    vtkNew<vtkPolyDataReader> reader;
    reader->SetFileName(filename.c_str());
    reader->Update();
    vtkPolyData *poly = reader->GetOutput();
    if (!poly)
    {
        std::cerr << "Failed to read VTK file: " << filename << std::endl;
        return polys;
    }
    polys.push_back(poly);
    return polys;
}

bool VTKMeshParser::canParse(const std::string &filename)
{
    auto ends_with = [](const std::string &str, const std::string &suffix)
    {
        return str.size() >= suffix.size() &&
               str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    return ends_with(filename, ".vtk");
}
