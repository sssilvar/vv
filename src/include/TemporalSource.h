#pragma once

#include <string>
#include <vector>
#include <vtkDataSet.h>
#include <vtkSmartPointer.h>

class vtkHDFReader;

// A playable time series backing the rendered dataset. Frames are produced on
// demand — result files routinely span thousands of steps and gigabytes on disk,
// so only what is needed for the current frame is materialised.
class TemporalSource {
public:
  virtual ~TemporalSource();

  int steps() const {
    return static_cast<int>(timeValues_.size());
  }
  bool playable() const {
    return timeValues_.size() > 1;
  }
  double timeAt(int step) const;

  // Bring `target` (the rendered object the mappers point at) to the given step.
  // Returns false on out-of-range or read failure.
  virtual bool readStepInto(int step, vtkDataSet* target) = 0;

  // Union of an array's range across up to maxSamples evenly spaced steps. Used to
  // fix a stable color range for the whole animation.
  virtual bool
  sampledScalarRange(const std::string& scalarName, double out[2], int maxSamples = 16) = 0;

  // Restrict per-frame work to a single array — the dominant playback speed-up.
  virtual void setActiveArray(const std::string& scalarName) = 0;

protected:
  std::vector<double> timeValues_;
};

// Wraps a live vtkHDFReader for a temporal VTKHDF file.
class VTKHDFTemporalSource : public TemporalSource {
public:
  VTKHDFTemporalSource();
  ~VTKHDFTemporalSource() override;

  // Called by the parser once the reader is constructed and information is read.
  void init(const vtkSmartPointer<vtkHDFReader>& reader, std::vector<double> timeValues);

  bool readStepInto(int step, vtkDataSet* target) override;
  bool sampledScalarRange(const std::string& scalarName, double out[2], int maxSamples) override;
  void setActiveArray(const std::string& scalarName) override;

private:
  bool updateToStep(int step);

  vtkSmartPointer<vtkHDFReader> reader_;
};
