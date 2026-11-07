#pragma once

#include "MeshLoading.h"

#include <vtkCellArray.h>
#include <vtkFloatArray.h>
#include <vtkIntArray.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>

inline MeshLoadResult testMeshes(int files, int side = 30) {
  vtkNew<vtkPoints> points;
  vtkNew<vtkCellArray> cells;
  vtkNew<vtkFloatArray> voltage;
  voltage->SetName("voltage");
  vtkNew<vtkIntArray> regions;
  regions->SetName("regions");
  for (int y = 0; y <= side; ++y) {
    for (int x = 0; x <= side; ++x) {
      points->InsertNextPoint(static_cast<double>(x) / side, static_cast<double>(y) / side, 0.0);
      voltage->InsertNextValue(static_cast<float>(y * (side + 1) + x));
      regions->InsertNextValue((y * (side + 1) + x) % 20);
      if (x < side && y < side) {
        const vtkIdType start = y * (side + 1) + x;
        const vtkIdType a[] = {start, start + 1, start + side + 2};
        const vtkIdType b[] = {start, start + side + 2, start + side + 1};
        cells->InsertNextCell(3, a);
        cells->InsertNextCell(3, b);
      }
    }
  }
  vtkNew<vtkPolyData> mesh;
  mesh->SetPoints(points);
  mesh->SetPolys(cells);
  mesh->GetPointData()->AddArray(voltage);
  mesh->GetPointData()->AddArray(regions);
  MeshLoadResult load;
  load.ok = true;
  for (int i = 0; i < files; ++i) {
    auto copy = vtkSmartPointer<vtkPolyData>::New();
    copy->DeepCopy(mesh);
    load.meshes.meshes.push_back(copy);
    load.meshes.names.push_back("mesh.vtp");
    load.meshes.partNames.push_back("surface");
    load.meshes.groups.push_back({"mesh.vtp", {static_cast<size_t>(i)}});
  }
  return load;
}
