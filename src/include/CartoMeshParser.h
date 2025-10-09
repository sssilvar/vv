#pragma once
#include "MeshParser.h"

class CartoMeshParser : public MeshParser
{
public:
    ~CartoMeshParser() override;
    std::vector<vtkSmartPointer<vtkPolyData>> parse(const std::string &filename) override;
    bool canParse(const std::string &filename) override;
};
