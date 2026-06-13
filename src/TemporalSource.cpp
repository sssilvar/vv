#include "TemporalSource.h"

#include <algorithm>
#include <vtkDataArray.h>
#include <vtkDataArraySelection.h>
#include <vtkHDFReader.h>
#include <vtkInformation.h>
#include <vtkPointData.h>
#include <vtkStreamingDemandDrivenPipeline.h>
#include <vtkVersionMacros.h>

TemporalSource::TemporalSource() = default;
TemporalSource::~TemporalSource() = default;

void TemporalSource::init(const vtkSmartPointer<vtkHDFReader>& reader,
                          std::vector<double> timeValues) {
  reader_ = reader;
  timeValues_ = std::move(timeValues);
  numSteps_ = static_cast<int>(timeValues_.size());
  if (reader_) {
    // Cache the static geometry/topology so successive frames only re-read the
    // temporal point-data arrays (incompatible with MergeParts, which is off).
    // vtkHDFReader gained UseCache in VTK 9.3; older VTK still plays back, just
    // re-reading geometry each frame.
#if VTK_VERSION_NUMBER >= VTK_VERSION_CHECK(9, 3, 0)
    reader_->UseCacheOn();
#endif
  }
}

void TemporalSource::setActiveArray(const std::string& scalarName) {
  if (!reader_ || scalarName.empty()) {
    return;
  }
  vtkDataArraySelection* sel = reader_->GetPointDataArraySelection();
  if (!sel) {
    return;
  }
  sel->DisableAllArrays();
  sel->EnableArray(scalarName.c_str());
}

double TemporalSource::timeAt(int step) const {
  if (step < 0 || step >= numSteps_) {
    return 0.0;
  }
  return timeValues_[static_cast<size_t>(step)];
}

bool TemporalSource::updateToStep(int step) {
  if (!reader_ || step < 0 || step >= numSteps_) {
    return false;
  }
  // Drive the time-series pipeline via UPDATE_TIME_STEP: vtkHDFReader::RequestData
  // recomputes its internal Step from this key on every Update.
  vtkInformation* outInfo = reader_->GetOutputInformation(0);
  if (!outInfo) {
    return false;
  }
  outInfo->Set(vtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP(),
               timeValues_[static_cast<size_t>(step)]);
  reader_->Update();
  return true;
}

bool TemporalSource::readStepInto(int step, vtkDataSet* target) {
  if (!target || !updateToStep(step)) {
    return false;
  }
  auto* out = vtkDataSet::SafeDownCast(reader_->GetOutputDataObject(0));
  if (!out) {
    return false;
  }
  target->ShallowCopy(out);
  target->Modified();
  return true;
}

bool TemporalSource::sampledScalarRange(const std::string& scalarName,
                                        double out[2],
                                        int maxSamples) {
  if (!reader_ || numSteps_ <= 0 || scalarName.empty()) {
    return false;
  }
  const int sampleCount = std::min(numSteps_, std::max(1, maxSamples));
  double lo = 0.0;
  double hi = 0.0;
  bool any = false;
  for (int s = 0; s < sampleCount; ++s) {
    // Evenly spaced steps including first and last.
    const int step =
        sampleCount == 1
            ? 0
            : static_cast<int>((static_cast<long long>(s) * (numSteps_ - 1)) / (sampleCount - 1));
    if (!updateToStep(step)) {
      continue;
    }
    auto* out2 = vtkDataSet::SafeDownCast(reader_->GetOutputDataObject(0));
    if (!out2 || !out2->GetPointData()) {
      continue;
    }
    vtkDataArray* arr = out2->GetPointData()->GetArray(scalarName.c_str());
    if (!arr) {
      continue;
    }
    double range[2];
    arr->GetRange(range);
    if (!any) {
      lo = range[0];
      hi = range[1];
      any = true;
    } else {
      lo = std::min(lo, range[0]);
      hi = std::max(hi, range[1]);
    }
  }
  if (!any) {
    return false;
  }
  out[0] = lo;
  out[1] = hi;
  return true;
}
