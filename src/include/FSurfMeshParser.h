#pragma once
#include "MeshParser.h"

#include <string>
#include <vector>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

class FSurfMeshParser : public MeshParser {
public:
  bool canParse(const std::string& filename) override;
  std::vector<vtkSmartPointer<vtkPolyData>> parse(const std::string& filename) override;
};
