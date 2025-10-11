#pragma once
#include "MeshParser.h"
#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <string>
#include <vector>

class FSurfMeshParser : public MeshParser {
public:
    bool canParse(const std::string &filename) override;
    std::vector<vtkSmartPointer<vtkPolyData>> parse(const std::string &filename) override;
};
