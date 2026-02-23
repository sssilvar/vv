#pragma once
#include "MeshParser.h"

#include <string>
#include <vector>
#include <vtkSmartPointer.h>

class FSurfMeshParser : public MeshParser {
public:
  bool canParse(const std::string& filename) override;
  std::vector<vtkSmartPointer<vtkDataSet>> parse(const std::string& filename) override;
};
