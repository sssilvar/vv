#pragma once
#include "MeshParser.h"

class VTKMeshParser : public MeshParser
{
public:
    ~VTKMeshParser() override;
    std::vector<vtkSmartPointer<vtkPolyData>> parse(const std::string &filename) override;
    bool canParse(const std::string &filename) override;
};
