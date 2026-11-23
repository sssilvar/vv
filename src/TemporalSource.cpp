#include "TemporalSource.h"

#include "ScalarField.h"

#include <algorithm>
#include <cmath>
#include <vtkDataArray.h>
#include <vtkDataArraySelection.h>
#include <vtkDataSetAttributes.h>
#include <vtkHDFReader.h>
#include <vtkNew.h>
#include <vtkVersionMacros.h>

TemporalSource::~TemporalSource() = default;

VTKHDFTemporalSource::VTKHDFTemporalSource() = default;
VTKHDFTemporalSource::~VTKHDFTemporalSource() = default;

double TemporalSource::timeAt(int step) const {
  if (step < 0 || step >= steps()) {
    return 0.0;
  }
  return timeValues_[static_cast<size_t>(step)];
}

void VTKHDFTemporalSource::init(const vtkSmartPointer<vtkHDFReader>& reader,
                                std::vector<double> timeValues) {
  reader_ = reader;
  timeValues_ = std::move(timeValues);
}

void VTKHDFTemporalSource::setActiveArrays(const std::vector<std::string>& scalarNames) {
  if (!reader_) {
    return;
  }
  // VTK can hit its array cache after a field is re-enabled and leave that field
  // out of the output. Start with an empty cache for each array selection.
  vtkNew<vtkHDFReader> reader;
  reader->SetFileName(reader_->GetFileName());
#if VTK_VERSION_NUMBER >= VTK_VERSION_CHECK(9, 3, 0) &&                                            \
    VTK_VERSION_NUMBER < VTK_VERSION_CHECK(9, 7, 0)
  reader->UseCacheOn();
#endif
  reader->UpdateInformation();
  vtkDataArraySelection* sel = reader->GetPointDataArraySelection();
  sel->DisableAllArrays();
  for (const std::string& name : scalarNames) {
    if (sel->ArrayExists(name.c_str())) {
      sel->EnableArray(name.c_str());
    }
  }
  reader_ = reader;
}

bool VTKHDFTemporalSource::updateToStep(int step) {
  if (!reader_ || step < 0 || step >= steps()) {
    return false;
  }
  return reader_->UpdateTimeStep(timeValues_[static_cast<size_t>(step)]) != 0;
}

bool VTKHDFTemporalSource::readStepInto(int step, vtkDataSet* target) {
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

bool VTKHDFTemporalSource::sampledScalarRange(const ScalarField& field,
                                              double out[2],
                                              int maxSamples) {
  if (!reader_ || steps() <= 0 || field.name.empty()) {
    return false;
  }
  const int sampleCount = std::min(steps(), std::max(1, maxSamples));
  double lo = 0.0;
  double hi = 0.0;
  bool any = false;
  for (int s = 0; s < sampleCount; ++s) {
    // Evenly spaced steps including first and last.
    const int step =
        sampleCount == 1
            ? 0
            : static_cast<int>((static_cast<long long>(s) * (steps() - 1)) / (sampleCount - 1));
    if (!updateToStep(step)) {
      continue;
    }
    auto* data = vtkDataSet::SafeDownCast(reader_->GetOutputDataObject(0));
    const int association =
        field.association == FieldAssociation::Point ? vtkDataObject::POINT : vtkDataObject::CELL;
    auto* attributes = data ? data->GetAttributes(association) : nullptr;
    auto* arr = attributes ? attributes->GetArray(field.name.c_str()) : nullptr;
    if (!arr || arr->GetNumberOfComponents() != 1) {
      continue;
    }
    double range[2];
    arr->GetFiniteRange(range);
    if (!std::isfinite(range[0]) || !std::isfinite(range[1]) || range[0] > range[1]) {
      continue;
    }
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
