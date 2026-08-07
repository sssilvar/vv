#pragma once

#include "MeshParser.h"

#include <memory>

// Parses XDMF (.xdmf/.xmf) light-data files whose heavy data lives in a companion
// HDF5 file — the usual output format of FEniCS/DOLFIN and similar solvers.
// Static geometry plus one temporal collection of node/cell attributes; the first
// frame is rendered and the rest are streamed for playback.
class XdmfMeshParser : public MeshParser {
public:
  XdmfMeshParser();
  ~XdmfMeshParser() override;

  std::vector<vtkSmartPointer<vtkDataSet>> parse(const std::string& filename) override;
  bool canParse(const std::string& filename) override;
  std::shared_ptr<TemporalSource> temporal() const override {
    return temporal_;
  }

private:
  std::shared_ptr<TemporalSource> temporal_;
};
