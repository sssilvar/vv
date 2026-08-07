#pragma once
#include <memory>
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
};
