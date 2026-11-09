#pragma once

#include "Colormaps.h"
#include "ProbeTrace.h"

#include <array>
#include <chrono>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkLookupTable.h>
#include <vtkSmartPointer.h>

class TemporalSource;

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
  bool hasNaN = false;
  std::set<double> uniqueValues; // populated iff categorical == true
  CategoryColoring coloring;     // categorical fields
  // The colors the file gives every category, which the Default palette draws; empty
  // when it gives none or misses one.
  CategoryColors fileColors;
  // Continuous fields only. A cyclic domain (e.g. a phase field wrapping at the
  // cycle length) starts on a cyclic map, whose two ends share a color, so the
  // wrap shows no false seam.
  ColormapChoice colormap;
};

// Fetch a named array from the point- or cell-data container of a dataset.
// Returns nullptr if the mesh is null or the array is absent.
vtkDataArray*
arrayForAssociation(vtkDataSet* mesh, const std::string& name, FieldAssociation association);

// Inspect scalar field across all meshes: 2 to 20 distinct finite values, or one plus NaN,
// are categorical. NaNs are tracked separately from uniqueValues. A
// categorical field takes fileColors from the first array that carries a table (see
// fileCategoryColors), or for vv_scalar from the colors its values encode.
ScalarAnalysis analyzeScalar(const std::vector<vtkDataSet*>& meshes,
                             const std::string& scalarName,
                             FieldAssociation association);

// Build a shared categorical analysis from the union of unique values across ALL categorical
// scalar fields (point and cell) in the given meshes. Non-categorical scalars are skipped.
// Used with --common-cat-lut to assign consistent value→color mapping across scalars.
ScalarAnalysis buildCommonCatAnalysis(const std::vector<vtkDataSet*>& meshes);

// A point inside a mesh cell as the weights of the cell's points there: where a probe
// reads a field, in any frame of a time series whose topology stays the same.
struct CellPoint {
  vtkIdType cell = -1;
  std::vector<vtkIdType> points;
  std::vector<double> weights; // parallel to points
};

// Where vtkCellPicker reports a hit: parametric coordinates pcoords in sub-cell subId of
// cell cellId. std::nullopt when the mesh lacks the cell.
std::optional<CellPoint>
cellPointAtPick(vtkDataSet* mesh, vtkIdType cellId, int subId, const double pcoords[3]);

// The value of a one-component field of mesh at a point inside a cell. A point field is
// interpolated with the point's weights, except a categorical one, which takes the value
// of the vertex that weighs most because categories do not blend; a cell field gives the
// cell's value. Only the field's array is read, so mesh may be a frame without geometry.
// std::nullopt when the mesh lacks the field or its values there.
std::optional<double> fieldValueAt(vtkDataSet* mesh,
                                   const ScalarField& field,
                                   const ScalarAnalysis& analysis,
                                   const CellPoint& at);

// fieldValueAt where vtkCellPicker reports a hit (see cellPointAtPick).
std::optional<double> fieldValueAtPick(vtkDataSet* mesh,
                                       const std::string& name,
                                       FieldAssociation association,
                                       const ScalarAnalysis& analysis,
                                       vtkIdType cellId,
                                       int subId,
                                       const double pcoords[3]);

// Reads frames of source that trace still lacks, in its order, until budget has passed
// (at least one): each into frame, a dataset of the series' type apart from the one on
// screen, then the field's value at `at` there, NaN where the frame has none. The field
// must be the one source streams (TemporalSource::setActiveArray). Returns the frames read.
int readTraceFrames(TemporalSource& source,
                    vtkDataSet* frame,
                    const CellPoint& at,
                    const ScalarField& field,
                    const ScalarAnalysis& analysis,
                    ProbeTrace& trace,
                    std::chrono::steady_clock::duration budget);

// The smallest and largest finite values of the field across every mesh; infinities
// and NaN are left out. false when no mesh has the field.
bool computeScalarGlobalRange(const std::vector<vtkDataSet*>& meshes,
                              const std::string& scalarName,
                              FieldAssociation association,
                              double outRange[2]);

// Counts of the scalar's values across every mesh in bins equal-width bins over
// range; the last bin includes range[1], and values outside range and NaN are left
// out. A range that is not finite gives no counts.
std::vector<size_t> computeScalarHistogram(const std::vector<vtkDataSet*>& meshes,
                                           const std::string& scalarName,
                                           FieldAssociation association,
                                           const double range[2],
                                           size_t bins);

// Continuous LUT sampled from a colormap; values outside range take its end colors,
// or gray when colormap.outside asks for it.
vtkSmartPointer<vtkLookupTable> createColormapLookupTable(const ColormapChoice& colormap,
                                                          const double range[2]);

// The colors table gives categories with these values: the entry each value indexes when
// every value is a whole number inside the table, else the entries in order; none when the
// table has fewer entries than there are values. Alpha is left out.
CategoryColors fileCategoryColors(vtkLookupTable* table, const std::set<double>& values);

// The table a file attached to array, as VTK's legacy reader does with a LOOKUP_TABLE
// section; nullptr without one. Still found after setMapperScalar takes it off the array.
vtkLookupTable* fileLookupTable(vtkDataArray* array);

// Indexed LUT with a row per value, annotated with its label, in coloring's colors.
vtkSmartPointer<vtkLookupTable> createCategoricalLookupTable(const std::set<double>& uniqueValues,
                                                             const CategoryColoring& coloring,
                                                             const CategoryColors& fileColors);

// Build LUT from a pre-computed ScalarAnalysis: a categorical one in analysis.coloring, a
// continuous one in analysis.colormap.
vtkSmartPointer<vtkLookupTable> buildLookupTable(const std::string& scalarName,
                                                 const ScalarAnalysis& analysis,
                                                 const double range[2]);

// Draws mesh's field with the table buildLookupTable gives it. A table the file attached to
// the array moves off it (see fileLookupTable), since vtkMapper would draw with that one.
bool setMapperScalar(vtkDataSet* mesh,
                     vtkDataSetMapper* mapper,
                     const std::string& scalarName,
                     FieldAssociation association,
                     const double range[2],
                     const ScalarAnalysis& analysis);
