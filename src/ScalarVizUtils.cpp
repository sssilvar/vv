#include "ScalarVizUtils.h"

#include <algorithm>
#include <limits>
#include <vtkDataArray.h>
#include <vtkNamedColors.h>
#include <vtkPointData.h>
#include <vtkTextProperty.h>

bool computeScalarGlobalRange(const std::vector<vtkPolyData*>& polys,
                              const std::string& scalarName,
                              double outRange[2]) {
  bool foundAny = false;
  double minValue = std::numeric_limits<double>::max();
  double maxValue = std::numeric_limits<double>::lowest();

  for (vtkPolyData* poly : polys) {
    if (!poly || !poly->GetPointData()) {
      continue;
    }
    auto* arr = poly->GetPointData()->GetArray(scalarName.c_str());
    if (!arr) {
      continue;
    }

    double range[2];
    arr->GetRange(range);
    minValue = std::min(minValue, range[0]);
    maxValue = std::max(maxValue, range[1]);
    foundAny = true;
  }

  if (!foundAny) {
    return false;
  }

  outRange[0] = minValue;
  outRange[1] = maxValue;
  return true;
}

vtkSmartPointer<vtkLookupTable> createDefaultLookupTable(const double range[2]) {
  auto lut = vtkSmartPointer<vtkLookupTable>::New();
  lut->SetNumberOfTableValues(256);
  applyLookupTableRange(lut, range);
  return lut;
}

void applyLookupTableRange(vtkLookupTable* lut, const double range[2]) {
  if (!lut) {
    return;
  }

  lut->SetRange(range);
  lut->SetHueRange(0.0, 0.8);

  double meshColor[3];
  vtkNew<vtkNamedColors> colors;
  colors->GetColorRGB("Grey", meshColor);
  const double nanColor[4] = {meshColor[0], meshColor[1], meshColor[2], 1.0};
  lut->SetNanColor(nanColor);
  lut->Build();
}

void updateScalarBar(vtkScalarBarActor* bar,
                     vtkRenderWindow* window,
                     vtkLookupTable* lut,
                     const std::string& title,
                     bool show) {
  if (!bar || !window) {
    return;
  }

  if (!show || !lut) {
    bar->SetLookupTable(nullptr);
    bar->SetTitle("");
    bar->SetNumberOfLabels(0);
    bar->GetLabelTextProperty()->SetFontSize(1);
    bar->GetTitleTextProperty()->SetFontSize(1);
    bar->SetVisibility(false);
    return;
  }

  bar->SetLookupTable(lut);
  bar->SetTitle(title.c_str());
  bar->SetNumberOfLabels(5);
  bar->SetUnconstrainedFontSize(true);
  bar->SetPosition(0.86, 0.10);
  bar->SetWidth(0.10);
  bar->SetHeight(0.80);
  bar->SetVisibility(true);

  const int* size = window->GetSize();
  const int barWidth = std::max(80, size[0] / 10);
  const int barHeight = std::max(200, size[1] / 2);
  bar->SetMaximumWidthInPixels(barWidth);
  bar->SetMaximumHeightInPixels(barHeight);
  const int fontSize = std::max(10, barHeight / 15);
  bar->GetLabelTextProperty()->SetFontSize(fontSize);
  bar->GetTitleTextProperty()->SetFontSize(fontSize + 2);
}

bool setMapperScalarFromPointData(vtkPolyData* poly,
                                  vtkPolyDataMapper* mapper,
                                  const std::string& scalarName,
                                  const double range[2]) {
  if (!poly || !mapper || !poly->GetPointData()) {
    return false;
  }

  auto* arr = poly->GetPointData()->GetArray(scalarName.c_str());
  if (!arr) {
    mapper->ScalarVisibilityOff();
    return false;
  }

  poly->GetPointData()->SetActiveScalars(scalarName.c_str());
  mapper->SelectColorArray(scalarName.c_str());
  mapper->SetScalarModeToUsePointData();
  mapper->SetColorModeToMapScalars();
  mapper->ScalarVisibilityOn();

  vtkLookupTable* lut = vtkLookupTable::SafeDownCast(mapper->GetLookupTable());
  if (!lut) {
    auto newLut = createDefaultLookupTable(range);
    mapper->SetLookupTable(newLut);
  } else {
    applyLookupTableRange(lut, range);
  }

  mapper->SetScalarRange(range);
  return true;
}
