#pragma once
#include <string>
#include <vtkSmartPointer.h>
#include <vtkPolyData.h>

class MeshParser
{
public:
    virtual ~MeshParser();
    // Parse the file and return a vector of vtkPolyData (empty on failure)
    virtual std::vector<vtkSmartPointer<vtkPolyData>> parse(const std::string &filename) = 0;
    // Return true if this parser can handle the file
    virtual bool canParse(const std::string &filename) = 0;
};
