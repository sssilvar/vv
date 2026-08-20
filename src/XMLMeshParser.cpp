#include "XMLMeshParser.h"

#include "mesh_utils.h"

#include <array>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <vtkFieldData.h>
#include <vtkFloatArray.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkXMLDataElement.h>
#include <vtkXMLUtilities.h>

XMLMeshParser::~XMLMeshParser() = default;

// A DIF volume's colour, as `rrggbb` hex. EnSite writes it bare and lowercase, and
// leaves it empty when the volume has none; a leading '#' is tolerated too. Volumes
// without a usable colour keep the viewer's categorical palette.
static bool ParseHexColor(const char* attribute, std::array<double, 3>& color) {
  if (!attribute)
    return false;
  std::string hex(attribute);
  if (!hex.empty() && hex.front() == '#')
    hex.erase(0, 1);
  if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
    return false;
  for (int channel = 0; channel < 3; ++channel) {
    color[static_cast<size_t>(channel)] =
        static_cast<double>(std::stoi(hex.substr(static_cast<size_t>(channel) * 2, 2), nullptr, 16)) /
        255.0;
  }
  return true;
}

// Helper: parse doubles from string (copied from main.cpp)
static std::vector<double> ParseDoubles(const std::string& s) {
  std::istringstream iss(s);
  std::vector<double> v;
  double x;
  while (iss >> x)
    v.push_back(x);
  return v;
}
// Helper: parse ids from string
static std::vector<vtkIdType> ParseIds(const std::string& s) {
  std::istringstream iss(s);
  std::vector<vtkIdType> v;
  long long x;
  while (iss >> x)
    v.push_back(static_cast<vtkIdType>(x));
  return v;
}

std::vector<vtkSmartPointer<vtkDataSet>> XMLMeshParser::parse(const std::string& filename) {
  std::vector<vtkSmartPointer<vtkDataSet>> polys;
  vtkSmartPointer<vtkXMLDataElement> root = vtkSmartPointer<vtkXMLDataElement>::Take(
      vtkXMLUtilities::ReadElementFromFile(filename.c_str()));
  if (!root) {
    std::cerr << "Failed to read XML: " << filename << '\n';
    return polys;
  }
  vtkXMLDataElement* body = root->FindNestedElementWithName("DIFBody");
  if (!body)
    body = root;
  vtkXMLDataElement* vols = body->FindNestedElementWithName("Volumes");
  if (!vols) {
    std::cerr << "No <Volumes> in XML: " << filename << '\n';
    return polys;
  }
  for (int i = 0; i < vols->GetNumberOfNestedElements(); ++i) {
    vtkXMLDataElement* vol = vols->GetNestedElement(i);
    if (std::string(vol->GetName()) != "Volume")
      continue;
    vtkXMLDataElement* vertsElem = vol->FindNestedElementWithName("Vertices");
    if (!vertsElem)
      continue;
    auto verts = ParseDoubles(vertsElem->GetCharacterData() ? vertsElem->GetCharacterData() : "");
    if (verts.size() % 3 != 0)
      continue;
    vtkNew<vtkPoints> pts;
    pts->SetNumberOfPoints(static_cast<vtkIdType>(verts.size() / 3));
    for (vtkIdType vi = 0; vi < static_cast<vtkIdType>(verts.size() / 3); ++vi) {
      const size_t vi3 = static_cast<size_t>(vi) * 3;
      pts->SetPoint(vi, verts[vi3], verts[vi3 + 1], verts[vi3 + 2]);
    }
    vtkXMLDataElement* polyElem = vol->FindNestedElementWithName("Polygons");
    if (!polyElem)
      continue;
    auto ids = ParseIds(polyElem->GetCharacterData() ? polyElem->GetCharacterData() : "");
    if (ids.size() % 3 != 0)
      continue;
    vtkNew<vtkCellArray> polysArr;
    vtkIdType tri[3];
    bool oneBased = true;
    if (std::any_of(ids.begin(), ids.end(), [](vtkIdType v) { return v == 0; }))
      oneBased = false;
    for (size_t j = 0; j < ids.size(); j += 3) {
      tri[0] = ids[j + 0] - (oneBased ? 1 : 0);
      tri[1] = ids[j + 1] - (oneBased ? 1 : 0);
      tri[2] = ids[j + 2] - (oneBased ? 1 : 0);
      polysArr->InsertNextCell(3, tri);
    }
    vtkNew<vtkPolyData> poly;
    poly->SetPoints(pts);
    poly->SetPolys(polysArr);

    const char* volumeName = vol->GetAttribute("name");
    if (volumeName && std::string(volumeName).size() > 0) {
      vtkNew<vtkStringArray> partNameArray;
      partNameArray->SetName("vv_part_name");
      partNameArray->InsertNextValue(volumeName);
      poly->GetFieldData()->AddArray(partNameArray);
    }

    std::array<double, 3> volumeColor{};
    if (ParseHexColor(vol->GetAttribute("color"), volumeColor)) {
      vtkNew<vtkFloatArray> partColorArray;
      partColorArray->SetName("vv_part_color");
      partColorArray->SetNumberOfComponents(3);
      partColorArray->InsertNextTuple3(volumeColor[0], volumeColor[1], volumeColor[2]);
      poly->GetFieldData()->AddArray(partColorArray);
    }

    vtkXMLDataElement* normElem = vol->FindNestedElementWithName("Normals");
    if (normElem) {
      auto n = ParseDoubles(normElem->GetCharacterData() ? normElem->GetCharacterData() : "");
      if (n.size() == verts.size()) {
        vtkNew<vtkFloatArray> an;
        an->SetName("Normals");
        an->SetNumberOfComponents(3);
        an->SetNumberOfTuples(static_cast<vtkIdType>(n.size() / 3));
        for (vtkIdType ni = 0; ni < static_cast<vtkIdType>(n.size() / 3); ++ni) {
          const size_t ni3 = static_cast<size_t>(ni) * 3;
          const float tuple[3] = {static_cast<float>(n[ni3]),
                                  static_cast<float>(n[ni3 + 1]),
                                  static_cast<float>(n[ni3 + 2])};
          an->SetTypedTuple(ni, tuple);
        }
        poly->GetPointData()->SetNormals(an);
        poly->GetPointData()->AddArray(an);
      }
    }
    polys.push_back(poly);
  }
  return polys;
}

bool XMLMeshParser::canParse(const std::string& filename) {
  std::string header = readHeader(filename, 200);
  return header.find("xml") != std::string::npos && header.find("DIF") != std::string::npos;
}
