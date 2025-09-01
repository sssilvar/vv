#pragma once
#include "MeshParser.h"

class XMLMeshParser : public MeshParser
{
public:
    ~XMLMeshParser() override;
    std::vector<vtkSmartPointer<vtkPolyData>> parse(const std::string &filename) override;
    bool canParse(const std::string &filename) override;
};
