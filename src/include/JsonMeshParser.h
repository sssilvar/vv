#pragma once
#include "MeshParser.h"

class JsonMeshParser : public MeshParser {
public:
  ~JsonMeshParser() override;
  std::vector<vtkSmartPointer<vtkDataSet>> parse(const std::string& filename) override;
  bool canParse(const std::string& filename) override;
};
