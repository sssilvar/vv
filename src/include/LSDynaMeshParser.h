#pragma once
#include "MeshParser.h"

class LSDynaMeshParser : public MeshParser {
public:
  ~LSDynaMeshParser() override;
  std::vector<vtkSmartPointer<vtkDataSet>> parse(const std::string& filename) override;
  bool canParse(const std::string& filename) override;
};
