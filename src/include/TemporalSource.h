#pragma once

#include <string>
#include <vector>
#include <vtkDataSet.h>
#include <vtkSmartPointer.h>

class vtkHDFReader;

// Wraps a live vtkHDFReader for a temporal (time-series) VTKHDF file. Frames are
// streamed on demand — only the current step is held in memory — because result
// files routinely span thousands of steps and gigabytes on disk.
class TemporalSource {
public:
  TemporalSource();
  ~TemporalSource();

  // Number of time steps; >1 means the file is playable.
  int steps() const {
    return numSteps_;
  }
  bool playable() const {
    return numSteps_ > 1;
  }
  double timeAt(int step) const;

  // Read the given step and shallow-copy its dataset into `target` (the rendered
  // object the mappers point at). Returns false on out-of-range or read failure.
  bool readStepInto(int step, vtkDataSet* target);

  // Union of a point-data array's range across up to maxSamples evenly spaced
  // steps. Used to fix a stable color range for the whole animation.
  bool sampledScalarRange(const std::string& scalarName, double out[2], int maxSamples = 16);

  // Restrict per-frame reads to a single point-data array. Static geometry is
  // cached (UseCache) and the other arrays are skipped, so streaming a frame only
  // touches the array actually being colored — the dominant playback speed-up.
  void setActiveArray(const std::string& scalarName);

  // Called by the parser once the reader is constructed and information is read.
  void init(const vtkSmartPointer<vtkHDFReader>& reader, std::vector<double> timeValues);

private:
  bool updateToStep(int step);

  vtkSmartPointer<vtkHDFReader> reader_;
  std::vector<double> timeValues_;
  int numSteps_ = 0;
};
