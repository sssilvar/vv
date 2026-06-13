#pragma once

#include <set>
#include <string>
#include <vector>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkLookupTable.h>
#include <vtkSmartPointer.h>

// Whether a scalar array lives on the mesh points or on its cells. VTK keeps the
// two in separate attribute containers (GetPointData/GetCellData) and a mapper
// must be told which one to color by, so the association is threaded through the
// whole scalar pipeline alongside the array name.
enum class FieldAssociation { Point, Cell };

// A selectable scalar: its array name plus where it lives. Used as the unit the
// viewer cycles through with the Space key.
struct ScalarField {
  std::string name;
  FieldAssociation association = FieldAssociation::Point;
};

// Result of scalar field analysis — computed once, passed around.
struct ScalarAnalysis {
  bool categorical = false;
  std::set<double> uniqueValues; // populated iff categorical == true
};

// Fetch a named array from the point- or cell-data container of a dataset.
// Returns nullptr if the mesh is null or the array is absent.
vtkDataArray*
arrayForAssociation(vtkDataSet* mesh, const std::string& name, FieldAssociation association);

// Inspect scalar field across all meshes: detect categorical (2–20 unique integer-domain values)
// vs continuous. Integer-typed VTK arrays are always treated as categorical candidates.
ScalarAnalysis analyzeScalar(const std::vector<vtkDataSet*>& meshes,
                             const std::string& scalarName,
                             FieldAssociation association);

// Build a shared categorical analysis from the union of unique values across ALL categorical
// scalar fields (point and cell) in the given meshes. Non-categorical scalars are skipped.
// Used with --common-cat-lut to assign consistent value→color mapping across scalars.
ScalarAnalysis buildCommonCatAnalysis(const std::vector<vtkDataSet*>& meshes);

bool computeScalarGlobalRange(const std::vector<vtkDataSet*>& meshes,
                              const std::string& scalarName,
                              FieldAssociation association,
                              double outRange[2]);

// Continuous rainbow LUT.
vtkSmartPointer<vtkLookupTable> createDefaultLookupTable(const double range[2]);

// Categorical LUT using tab10 (n≤10) or tab20 (n≤20). Uses indexed lookup.
vtkSmartPointer<vtkLookupTable> createCategoricalLookupTable(const std::set<double>& uniqueValues);

// Build LUT from a pre-computed ScalarAnalysis.
vtkSmartPointer<vtkLookupTable> buildLookupTable(const ScalarAnalysis& analysis,
                                                 const double range[2]);

void applyLookupTableRange(vtkLookupTable* lut, const double range[2]);

bool setMapperScalar(vtkDataSet* mesh,
                     vtkDataSetMapper* mapper,
                     const std::string& scalarName,
                     FieldAssociation association,
                     const double range[2],
                     const ScalarAnalysis& analysis);
