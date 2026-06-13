#pragma once

#include "MeshParser.h"

#include <memory>

class TemporalSource;

// Parses VTKHDF files (.vtkhdf — HDF5-backed VTK). Loads the first time step for
// rendering; if the file is temporal, exposes a TemporalSource for playback.
class VTKHDFMeshParser : public MeshParser {
public:
  VTKHDFMeshParser();
  ~VTKHDFMeshParser() override;

  std::vector<vtkSmartPointer<vtkDataSet>> parse(const std::string& filename) override;
  bool canParse(const std::string& filename) override;

  // Valid after a successful parse(); null if the file is not temporal.
  std::shared_ptr<TemporalSource> temporal() const {
    return temporal_;
  }

private:
  std::shared_ptr<TemporalSource> temporal_;
};
