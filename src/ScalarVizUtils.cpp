#include "ScalarVizUtils.h"

#include "TemporalSource.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <set>
#include <vtkCellData.h>
#include <vtkCellPicker.h>
#include <vtkDataArray.h>
#include <vtkDataSetAttributes.h>
#include <vtkGenericCell.h>
#include <vtkInformation.h>
#include <vtkInformationObjectBaseKey.h>
#include <vtkNamedColors.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkType.h>

namespace {

// vtkCellPicker reports a hit on a triangle strip, poly-line or poly-vertex in one of
// its sub-cells, whose vertex order it picks itself; these protected helpers rebuild
// that sub-cell. Never instantiated.
struct PickedSubCell : vtkCellPicker {
  using vtkCellPicker::HasSubCells;
  using vtkCellPicker::SubCellFromCell;
};

// Where an array keeps the table its file attached once setMapperScalar takes it off.
vtkInformationObjectBaseKey* fileTableKey() {
  // VTK's key manager deletes keys at exit.
  static auto* const key =
      new vtkInformationObjectBaseKey("FILE_LOOKUP_TABLE", "vv", "vtkLookupTable");
  return key;
}

// vv_scalar holds 1 + 0xRRGGBB per painted point and 0 elsewhere (see JsonMeshParser).
constexpr char kPaintedField[] = "vv_scalar";
constexpr std::array<double, 3> kUnpaintedGray = {0.6, 0.6, 0.6};

CategoryColors paintedColors(const std::set<double>& values) {
  CategoryColors colors;
  for (const double value : values) {
    const int hex = static_cast<int>(value) - 1;
    colors[value] = hex < 0 ? kUnpaintedGray
                            : std::array<double, 3>{(hex >> 16 & 0xFF) / 255.0,
                                                    (hex >> 8 & 0xFF) / 255.0,
                                                    (hex & 0xFF) / 255.0};
  }
  return colors;
}

} // namespace

vtkDataArray*
arrayForAssociation(vtkDataSet* mesh, const std::string& name, FieldAssociation association) {
  if (!mesh) {
    return nullptr;
  }
  vtkDataSetAttributes* attrs = (association == FieldAssociation::Cell)
                                    ? static_cast<vtkDataSetAttributes*>(mesh->GetCellData())
                                    : static_cast<vtkDataSetAttributes*>(mesh->GetPointData());
  if (!attrs) {
    return nullptr;
  }
  return attrs->GetArray(name.c_str());
}

std::optional<CellPoint>
cellPointAtPick(vtkDataSet* mesh, vtkIdType cellId, int subId, const double pcoords[3]) {
  if (!mesh || cellId < 0 || cellId >= mesh->GetNumberOfCells()) {
    return std::nullopt;
  }
  vtkNew<vtkGenericCell> cell;
  mesh->GetCell(cellId, cell);
  if (PickedSubCell::HasSubCells(cell->GetCellType())) {
    PickedSubCell::SubCellFromCell(cell, subId);
  }
  CellPoint at;
  at.cell = cellId;
  at.weights.resize(static_cast<size_t>(cell->GetNumberOfPoints()));
  if (at.weights.empty()) {
    return std::nullopt;
  }
  // On a degenerate cell, such as a triangle at a mesh's pole, the picker measures
  // the hit along an edge of zero length, which gives NaN. That edge's far vertex
  // sits on the edge's origin, so no weight needs to move along it.
  double finite[3];
  std::transform(pcoords, pcoords + 3, finite, [](double coordinate) {
    return std::isfinite(coordinate) ? coordinate : 0.0;
  });
  double position[3];
  cell->EvaluateLocation(subId, finite, position, at.weights.data());
  at.points.reserve(at.weights.size());
  for (vtkIdType vertex = 0; vertex < cell->GetNumberOfPoints(); ++vertex) {
    at.points.push_back(cell->GetPointId(static_cast<int>(vertex)));
  }
  return at;
}

std::optional<double> fieldValueAt(vtkDataSet* mesh,
                                   const ScalarField& field,
                                   const ScalarAnalysis& analysis,
                                   const CellPoint& at) {
  vtkDataArray* values = arrayForAssociation(mesh, field.name, field.association);
  if (!values || values->GetNumberOfComponents() != 1) {
    return std::nullopt;
  }
  const vtkIdType tuples = values->GetNumberOfTuples();
  if (field.association == FieldAssociation::Cell) {
    if (at.cell < 0 || at.cell >= tuples) {
      return std::nullopt;
    }
    return values->GetComponent(at.cell, 0);
  }
  if (at.points.empty() || at.points.size() != at.weights.size() ||
      std::any_of(at.points.begin(), at.points.end(), [tuples](vtkIdType point) {
        return point < 0 || point >= tuples;
      })) {
    return std::nullopt;
  }
  if (analysis.categorical) {
    const auto heaviest =
        std::distance(at.weights.begin(), std::max_element(at.weights.begin(), at.weights.end()));
    return values->GetComponent(at.points[static_cast<size_t>(heaviest)], 0);
  }
  double value = 0.0;
  for (size_t vertex = 0; vertex < at.points.size(); ++vertex) {
    value += at.weights[vertex] * values->GetComponent(at.points[vertex], 0);
  }
  return value;
}

std::optional<double> fieldValueAtPick(vtkDataSet* mesh,
                                       const std::string& name,
                                       FieldAssociation association,
                                       const ScalarAnalysis& analysis,
                                       vtkIdType cellId,
                                       int subId,
                                       const double pcoords[3]) {
  const std::optional<CellPoint> at = cellPointAtPick(mesh, cellId, subId, pcoords);
  return at ? fieldValueAt(mesh, {name, association}, analysis, *at) : std::nullopt;
}

int readTraceFrames(TemporalSource& source,
                    vtkDataSet* frame,
                    const CellPoint& at,
                    const ScalarField& field,
                    const ScalarAnalysis& analysis,
                    ProbeTrace& trace,
                    std::chrono::steady_clock::duration budget) {
  const auto start = std::chrono::steady_clock::now();
  int count = 0;
  for (std::optional<int> step = trace.next(); step; step = trace.next()) {
    const std::optional<double> value =
        source.readStepInto(*step, frame) ? fieldValueAt(frame, field, analysis, at) : std::nullopt;
    trace.fill(value.value_or(std::numeric_limits<double>::quiet_NaN()));
    ++count;
    if (std::chrono::steady_clock::now() - start >= budget) {
      break;
    }
  }
  return count;
}

bool computeScalarGlobalRange(const std::vector<vtkDataSet*>& meshes,
                              const std::string& scalarName,
                              FieldAssociation association,
                              double outRange[2]) {
  bool foundAny = false;
  double minValue = std::numeric_limits<double>::max();
  double maxValue = std::numeric_limits<double>::lowest();

  for (vtkDataSet* mesh : meshes) {
    auto* arr = arrayForAssociation(mesh, scalarName, association);
    if (!arr) {
      continue;
    }

    double range[2];
    arr->GetFiniteRange(range);
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

std::vector<size_t> computeScalarHistogram(const std::vector<vtkDataSet*>& meshes,
                                           const std::string& scalarName,
                                           FieldAssociation association,
                                           const double range[2],
                                           size_t bins) {
  std::vector<size_t> counts(bins, 0);
  const double span = range[1] - range[0];
  // Bins over an infinite span (an infinite end, or ends more than DBL_MAX apart) have
  // no finite width, and a tiny span overflows the scale: both would give NaN bin
  // positions. A tiny span then counts like an empty one, all in the first bin.
  if (bins == 0 || !std::isfinite(span)) {
    return counts;
  }
  const double inverseWidth = static_cast<double>(bins) / span;
  const double scale = span > 0.0 && std::isfinite(inverseWidth) ? inverseWidth : 0.0;
  for (vtkDataSet* mesh : meshes) {
    vtkDataArray* arr = arrayForAssociation(mesh, scalarName, association);
    if (!arr || arr->GetNumberOfComponents() != 1) {
      continue;
    }
    const vtkIdType n = arr->GetNumberOfTuples();
    for (vtkIdType i = 0; i < n; ++i) {
      const double value = arr->GetComponent(i, 0);
      // Written so NaN fails it too; infinities fail it since the range is finite.
      if (!(value >= range[0] && value <= range[1])) {
        continue;
      }
      ++counts[std::min(static_cast<size_t>((value - range[0]) * scale), bins - 1)];
    }
  }
  return counts;
}

static bool isIntegerType(vtkDataArray* arr) {
  const int t = arr->GetDataType();
  return t == VTK_CHAR || t == VTK_SIGNED_CHAR || t == VTK_UNSIGNED_CHAR || t == VTK_SHORT ||
         t == VTK_UNSIGNED_SHORT || t == VTK_INT || t == VTK_UNSIGNED_INT || t == VTK_LONG ||
         t == VTK_UNSIGNED_LONG || t == VTK_LONG_LONG || t == VTK_UNSIGNED_LONG_LONG;
}

static std::set<double> collectUniqueValues(const std::vector<vtkDataSet*>& meshes,
                                            const std::string& scalarName,
                                            FieldAssociation association,
                                            bool roundToInt,
                                            int maxUnique) {
  std::set<double> unique;
  for (vtkDataSet* mesh : meshes) {
    auto* arr = arrayForAssociation(mesh, scalarName, association);
    if (!arr || arr->GetNumberOfComponents() != 1)
      continue;
    const vtkIdType n = arr->GetNumberOfTuples();
    for (vtkIdType i = 0; i < n; ++i) {
      double v = arr->GetComponent(i, 0);
      if (roundToInt)
        v = std::round(v);
      unique.insert(v);
      if (static_cast<int>(unique.size()) > maxUnique)
        return unique;
    }
  }
  return unique;
}

ScalarAnalysis analyzeScalar(const std::vector<vtkDataSet*>& meshes,
                             const std::string& scalarName,
                             FieldAssociation association) {
  ScalarAnalysis result;
  if (scalarName.empty() || meshes.empty())
    return result;

  // Determine array type from first mesh that has the scalar.
  bool isInt = false;
  for (vtkDataSet* mesh : meshes) {
    auto* arr = arrayForAssociation(mesh, scalarName, association);
    if (arr && arr->GetNumberOfComponents() == 1) {
      isInt = isIntegerType(arr);
      break;
    }
  }

  // Integer arrays: collect up to 20 unique (rounded) values.
  // Float arrays: collect up to 20 unique values (no rounding).
  const bool painted = scalarName == kPaintedField;
  const int limit = painted ? 256 : 20;
  auto unique = collectUniqueValues(meshes, scalarName, association, isInt, limit);
  const int n = static_cast<int>(unique.size());

  if ((n >= 2 || painted) && n <= limit) {
    result.categorical = true;
    result.uniqueValues = std::move(unique);
    if (painted) {
      result.fileColors = paintedColors(result.uniqueValues);
      return result;
    }
    for (vtkDataSet* mesh : meshes) {
      if (vtkLookupTable* table =
              fileLookupTable(arrayForAssociation(mesh, scalarName, association))) {
        result.fileColors = fileCategoryColors(table, result.uniqueValues);
        break;
      }
    }
  } else if (scalarName.find("phase") != std::string::npos ||
             scalarName.find("Phase") != std::string::npos) {
    result.colormap.map = &defaultColormap(ColormapGroup::Cyclic);
  }
  return result;
}

ScalarAnalysis buildCommonCatAnalysis(const std::vector<vtkDataSet*>& meshes) {
  // Collect all (name, association) fields present across any mesh.
  std::set<std::pair<std::string, FieldAssociation>> fields;
  for (vtkDataSet* mesh : meshes) {
    if (!mesh)
      continue;
    if (auto* pd = mesh->GetPointData()) {
      for (int i = 0; i < pd->GetNumberOfArrays(); ++i) {
        if (const char* name = pd->GetArrayName(i))
          fields.insert({name, FieldAssociation::Point});
      }
    }
    if (auto* cd = mesh->GetCellData()) {
      for (int i = 0; i < cd->GetNumberOfArrays(); ++i) {
        if (const char* name = cd->GetArrayName(i))
          fields.insert({name, FieldAssociation::Cell});
      }
    }
  }

  // Union of unique values from every scalar that is itself categorical.
  std::set<double> unionValues;
  for (const auto& [name, association] : fields) {
    ScalarAnalysis a = analyzeScalar(meshes, name, association);
    if (a.categorical)
      unionValues.insert(a.uniqueValues.begin(), a.uniqueValues.end());
  }

  ScalarAnalysis result;
  const int n = static_cast<int>(unionValues.size());
  if (n >= 2 && n <= 20) {
    result.categorical = true;
    result.uniqueValues = std::move(unionValues);
  }
  return result;
}

CategoryColors fileCategoryColors(vtkLookupTable* table, const std::set<double>& values) {
  if (!table) {
    return {};
  }
  const vtkIdType entries = table->GetNumberOfTableValues();
  const bool byValue = std::all_of(values.begin(), values.end(), [entries](double value) {
    return value >= 0.0 && value < static_cast<double>(entries) && value == std::floor(value);
  });
  if (!byValue && static_cast<vtkIdType>(values.size()) > entries) {
    return {};
  }
  CategoryColors colors;
  vtkIdType rank = 0;
  for (const double value : values) {
    double rgba[4];
    table->GetTableValue(byValue ? static_cast<vtkIdType>(value) : rank++, rgba);
    colors[value] = {rgba[0], rgba[1], rgba[2]};
  }
  return colors;
}

vtkLookupTable* fileLookupTable(vtkDataArray* array) {
  if (!array) {
    return nullptr;
  }
  if (vtkLookupTable* attached = array->GetLookupTable()) {
    return attached;
  }
  return array->HasInformation()
             ? vtkLookupTable::SafeDownCast(array->GetInformation()->Get(fileTableKey()))
             : nullptr;
}

vtkSmartPointer<vtkLookupTable> createCategoricalLookupTable(const std::set<double>& uniqueValues,
                                                             const CategoryColoring& coloring,
                                                             const CategoryColors& fileColors) {
  auto lut = vtkSmartPointer<vtkLookupTable>::New();
  lut->SetNumberOfTableValues(static_cast<vtkIdType>(uniqueValues.size()));
  lut->SetIndexedLookup(1);

  size_t rank = 0;
  for (const double v : uniqueValues) {
    const std::array<double, 3> c = categoryColor(coloring, fileColors, v, rank);
    lut->SetTableValue(static_cast<vtkIdType>(rank), c[0], c[1], c[2], 1.0);
    // Annotation label: show integer if the value is whole, else decimal.
    char label[32];
    if (v == std::floor(v)) {
      std::snprintf(label, sizeof(label), "%g", v);
    } else {
      std::snprintf(label, sizeof(label), "%.3g", v);
    }
    lut->SetAnnotation(v, label);
    ++rank;
  }
  lut->Build();
  return lut;
}

vtkSmartPointer<vtkLookupTable> buildLookupTable(const std::string& scalarName,
                                                 const ScalarAnalysis& analysis,
                                                 const double range[2]) {
  if (!analysis.categorical)
    return createColormapLookupTable(analysis.colormap, range);
  auto lut =
      createCategoricalLookupTable(analysis.uniqueValues, analysis.coloring, analysis.fileColors);
  // A painted category is named by its color.
  if (scalarName == kPaintedField) {
    for (const double v : analysis.uniqueValues) {
      const int hex = static_cast<int>(v) - 1;
      if (hex >= 0) {
        char label[8];
        std::snprintf(label, sizeof(label), "#%06x", hex & 0xFFFFFF);
        lut->SetAnnotation(v, label);
      }
    }
  }
  return lut;
}

vtkSmartPointer<vtkLookupTable> createColormapLookupTable(const ColormapChoice& colormap,
                                                          const double range[2]) {
  constexpr int kEntries = 256;
  auto lut = vtkSmartPointer<vtkLookupTable>::New();
  lut->SetNumberOfTableValues(kEntries);
  for (int i = 0; i < kEntries; ++i) {
    const std::array<double, 3> rgb =
        sampleColormap(*colormap.map, i / (kEntries - 1.0), colormap.reversed);
    lut->SetTableValue(i, rgb[0], rgb[1], rgb[2], 1.0);
  }
  lut->SetRange(range);
  if (colormap.outside == OutOfRange::Gray) {
    const double gray[4] = {kOutOfRangeGray[0], kOutOfRangeGray[1], kOutOfRangeGray[2], 1.0};
    lut->SetBelowRangeColor(gray);
    lut->SetAboveRangeColor(gray);
    lut->UseBelowRangeColorOn();
    lut->UseAboveRangeColorOn();
  }

  double meshColor[3];
  vtkNew<vtkNamedColors> colors;
  colors->GetColorRGB("Grey", meshColor);
  const double nanColor[4] = {meshColor[0], meshColor[1], meshColor[2], 1.0};
  lut->SetNanColor(nanColor);
  // The NaN and gray colors came after the table values, which copied the old ones.
  lut->BuildSpecialColors();
  return lut;
}

bool setMapperScalar(vtkDataSet* mesh,
                     vtkDataSetMapper* mapper,
                     const std::string& scalarName,
                     FieldAssociation association,
                     const double range[2],
                     const ScalarAnalysis& analysis) {
  if (!mesh || !mapper) {
    return false;
  }

  vtkDataArray* arr = arrayForAssociation(mesh, scalarName, association);
  if (!arr) {
    mapper->ScalarVisibilityOff();
    return false;
  }
  if (vtkLookupTable* attached = arr->GetLookupTable()) {
    arr->GetInformation()->Set(fileTableKey(), attached);
    arr->SetLookupTable(nullptr);
  }

  const bool cell = (association == FieldAssociation::Cell);
  if (cell) {
    mesh->GetCellData()->SetActiveScalars(scalarName.c_str());
    mapper->SetScalarModeToUseCellFieldData();
  } else {
    mesh->GetPointData()->SetActiveScalars(scalarName.c_str());
    mapper->SetScalarModeToUsePointFieldData();
  }
  mapper->SelectColorArray(scalarName.c_str());
  mapper->SetColorModeToMapScalars();
  mapper->ScalarVisibilityOn();
  // Interpolate the scalar across the triangle and map afterwards, so nodal
  // fields get a smooth colormap-correct gradient. Cell fields (and indexed
  // categorical LUTs) must stay flat per cell.
  mapper->SetInterpolateScalarsBeforeMapping((!cell && !analysis.categorical) ? 1 : 0);

  mapper->SetLookupTable(buildLookupTable(scalarName, analysis, range));
  mapper->SetScalarRange(range);
  return true;
}
