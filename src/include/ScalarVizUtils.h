#pragma once

#include <string>
#include <vector>
#include <vtkLookupTable.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderWindow.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>

bool computeScalarGlobalRange(const std::vector<vtkPolyData*>& polys,
                              const std::string& scalarName,
                              double outRange[2]);

vtkSmartPointer<vtkLookupTable> createDefaultLookupTable(const double range[2]);

void applyLookupTableRange(vtkLookupTable* lut, const double range[2]);

void updateScalarBar(vtkScalarBarActor* bar,
                     vtkRenderWindow* window,
                     vtkLookupTable* lut,
                     const std::string& title,
                     bool show);

bool setMapperScalarFromPointData(vtkPolyData* poly,
                                  vtkPolyDataMapper* mapper,
                                  const std::string& scalarName,
                                  const double range[2]);
