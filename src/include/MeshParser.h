#pragma once
#include "Volume.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <vtkDataSet.h>
#include <vtkSmartPointer.h>

class TemporalSource;

class MeshParser {
public:
  virtual ~MeshParser();
  // Parse the file and return a vector of vtkDataSet (empty on failure)
  virtual std::vector<vtkSmartPointer<vtkDataSet>> parse(const std::string& filename) = 0;
  // Return true if this parser can handle the file
  virtual bool canParse(const std::string& filename) = 0;
  // Valid after a successful parse(); null unless the file is a playable time series.
  virtual std::shared_ptr<TemporalSource> temporal() const {
    return nullptr;
  }
  // Valid after a successful parse(); the voxel grid behind the parts, if the file has one.
  // A volume may come with no parts at all.
  virtual std::optional<Volume> volume() const {
    return std::nullopt;
  }
};
