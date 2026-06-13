#include "ScalarVizUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataSetAttributes.h>
#include <vtkNamedColors.h>
#include <vtkPointData.h>
#include <vtkType.h>

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

// Matplotlib tab10 palette (10 colors).
static const double kTab10[10][3] = {
    {0.122, 0.467, 0.706},
    {1.000, 0.498, 0.055},
    {0.173, 0.627, 0.173},
    {0.839, 0.153, 0.157},
    {0.580, 0.404, 0.741},
    {0.549, 0.337, 0.294},
    {0.890, 0.467, 0.761},
    {0.498, 0.498, 0.498},
    {0.737, 0.741, 0.133},
    {0.090, 0.745, 0.812},
};

// Matplotlib tab20 palette (20 colors).
static const double kTab20[20][3] = {
    {0.122, 0.467, 0.706}, {0.682, 0.780, 0.910}, {1.000, 0.498, 0.055}, {1.000, 0.733, 0.471},
    {0.173, 0.627, 0.173}, {0.596, 0.875, 0.541}, {0.839, 0.153, 0.157}, {1.000, 0.596, 0.588},
    {0.580, 0.404, 0.741}, {0.773, 0.690, 0.835}, {0.549, 0.337, 0.294}, {0.769, 0.612, 0.580},
    {0.890, 0.467, 0.761}, {0.969, 0.714, 0.824}, {0.498, 0.498, 0.498}, {0.780, 0.780, 0.780},
    {0.737, 0.741, 0.133}, {0.859, 0.859, 0.553}, {0.090, 0.745, 0.812}, {0.620, 0.855, 0.898},
};

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
  const int limit = 20;
  auto unique = collectUniqueValues(meshes, scalarName, association, isInt, limit);
  const int n = static_cast<int>(unique.size());

  if (n >= 2 && n <= limit) {
    result.categorical = true;
    result.uniqueValues = std::move(unique);
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

vtkSmartPointer<vtkLookupTable> createCategoricalLookupTable(const std::set<double>& uniqueValues) {
  const int n = static_cast<int>(uniqueValues.size());
  const bool useTab20 = (n > 10);
  const int paletteSize = useTab20 ? 20 : 10;

  auto lut = vtkSmartPointer<vtkLookupTable>::New();
  lut->SetNumberOfTableValues(n);
  lut->SetIndexedLookup(1);

  int idx = 0;
  for (double v : uniqueValues) {
    // Index the 2D palette directly: keeps the static-array element known to be
    // initialized (no pointer-to-array variable that clang-format reflows).
    const int row = idx % paletteSize;
    const double* c = useTab20 ? kTab20[row] : kTab10[row];
    lut->SetTableValue(idx, c[0], c[1], c[2], 1.0);
    // Annotation label: show integer if the value is whole, else decimal.
    char label[32];
    if (v == std::floor(v)) {
      std::snprintf(label, sizeof(label), "%g", v);
    } else {
      std::snprintf(label, sizeof(label), "%.3g", v);
    }
    lut->SetAnnotation(v, label);
    ++idx;
  }
  lut->Build();
  return lut;
}

vtkSmartPointer<vtkLookupTable> buildLookupTable(const ScalarAnalysis& analysis,
                                                 const double range[2]) {
  if (analysis.categorical)
    return createCategoricalLookupTable(analysis.uniqueValues);
  return createDefaultLookupTable(range);
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

bool setMapperScalar(vtkDataSet* mesh,
                     vtkDataSetMapper* mapper,
                     const std::string& scalarName,
                     FieldAssociation association,
                     const double range[2],
                     const ScalarAnalysis& analysis) {
  if (!mesh || !mapper) {
    return false;
  }

  const auto* arr = arrayForAssociation(mesh, scalarName, association);
  if (!arr) {
    mapper->ScalarVisibilityOff();
    return false;
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

  auto lut = buildLookupTable(analysis, range);
  mapper->SetLookupTable(lut);
  mapper->SetScalarRange(range);
  return true;
}
