#pragma once
#include "MeshParser.h"

#include <optional>
#include <string>
#include <vector>
#include <vtkSmartPointer.h>

// Reads a NIfTI label map as one surface per nonzero label; any other volume yields
// no parts, only its voxel grid for the slice views.
class NiftiMeshParser : public MeshParser {
public:
  bool canParse(const std::string& filename) override;
  std::vector<vtkSmartPointer<vtkDataSet>> parse(const std::string& filename) override;
  std::optional<Volume> volume() const override {
    return volume_;
  }

private:
  std::optional<Volume> volume_;
};
