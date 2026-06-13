#include "JsonMeshParser.h"

#include "mesh_utils.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <vtkCellArray.h>
#include <vtkFieldData.h>
#include <vtkFloatArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>

namespace {

using json = nlohmann::json;

bool isMeshPart(const json& value) {
  return value.is_object() && value.contains("vertices") && value.contains("indices") &&
         value["vertices"].is_array() && value["indices"].is_array();
}

bool looksLikeJsonMesh(const json& value) {
  if (isMeshPart(value)) {
    return true;
  }
  if (value.is_object() && value.contains("surface")) {
    const auto& surface = value["surface"];
    return surface.is_array() && !surface.empty() &&
           std::all_of(surface.begin(), surface.end(), isMeshPart);
  }
  if (!value.is_array() || value.empty()) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), isMeshPart);
}

const json* meshPartArray(const json& root) {
  if (root.is_array()) {
    return &root;
  }
  if (root.is_object() && root.contains("surface") && root["surface"].is_array()) {
    return &root["surface"];
  }
  return nullptr;
}

std::string partName(const json& part) {
  if (part.contains("name") && part["name"].is_string()) {
    return part["name"].get<std::string>();
  }
  return {};
}

bool addNormals(vtkPolyData* poly, const json& part, size_t pointCount) {
  if (!part.contains("normals") || !part["normals"].is_array()) {
    return true;
  }

  const auto& normals = part["normals"];
  if (normals.size() != pointCount * 3u) {
    return false;
  }

  vtkNew<vtkFloatArray> normalArray;
  normalArray->SetName("Normals");
  normalArray->SetNumberOfComponents(3);
  normalArray->SetNumberOfTuples(static_cast<vtkIdType>(pointCount));
  for (size_t i = 0; i < pointCount; ++i) {
    const size_t i3 = i * 3u;
    const float tuple[3] = {
        normals[i3].get<float>(), normals[i3 + 1u].get<float>(), normals[i3 + 2u].get<float>()};
    normalArray->SetTypedTuple(static_cast<vtkIdType>(i), tuple);
  }
  poly->GetPointData()->SetNormals(normalArray);
  poly->GetPointData()->AddArray(normalArray);
  return true;
}

bool addColor(vtkPolyData* poly, const json& part) {
  if (!part.contains("color") || !part["color"].is_array()) {
    return true;
  }

  const auto& color = part["color"];
  if (color.size() < 3u) {
    return false;
  }

  vtkNew<vtkFloatArray> colorArray;
  colorArray->SetName("vv_part_color");
  colorArray->SetNumberOfComponents(3);
  colorArray->InsertNextTuple3(
      color[0].get<double>(), color[1].get<double>(), color[2].get<double>());
  poly->GetFieldData()->AddArray(colorArray);
  return true;
}

vtkSmartPointer<vtkPolyData> parsePart(const json& part) {
  try {
    const auto& vertices = part["vertices"];
    const auto& indices = part["indices"];
    if (vertices.size() % 3u != 0u || indices.size() % 3u != 0u || vertices.empty() ||
        indices.empty()) {
      return nullptr;
    }

    const size_t pointCount = vertices.size() / 3u;
    if (pointCount > static_cast<size_t>(std::numeric_limits<vtkIdType>::max())) {
      return nullptr;
    }
    vtkNew<vtkPoints> points;
    points->SetNumberOfPoints(static_cast<vtkIdType>(pointCount));
    for (size_t i = 0; i < pointCount; ++i) {
      const size_t i3 = i * 3u;
      points->SetPoint(static_cast<vtkIdType>(i),
                       vertices[i3].get<double>(),
                       vertices[i3 + 1u].get<double>(),
                       vertices[i3 + 2u].get<double>());
    }

    vtkNew<vtkCellArray> triangles;
    for (size_t i = 0; i < indices.size(); i += 3u) {
      vtkIdType triangle[3] = {indices[i].get<vtkIdType>(),
                               indices[i + 1u].get<vtkIdType>(),
                               indices[i + 2u].get<vtkIdType>()};
      if (triangle[0] < 0 || triangle[1] < 0 || triangle[2] < 0 ||
          triangle[0] >= static_cast<vtkIdType>(pointCount) ||
          triangle[1] >= static_cast<vtkIdType>(pointCount) ||
          triangle[2] >= static_cast<vtkIdType>(pointCount)) {
        return nullptr;
      }
      triangles->InsertNextCell(3, triangle);
    }

    vtkNew<vtkPolyData> poly;
    poly->SetPoints(points);
    poly->SetPolys(triangles);

    const std::string name = partName(part);
    if (!name.empty()) {
      vtkNew<vtkStringArray> nameArray;
      nameArray->SetName("vv_part_name");
      nameArray->InsertNextValue(name);
      poly->GetFieldData()->AddArray(nameArray);
    }

    if (!addNormals(poly, part, pointCount)) {
      return nullptr;
    }
    if (!addColor(poly, part)) {
      return nullptr;
    }

    return poly;
  } catch (const json::exception&) {
    return nullptr;
  }
}

} // namespace

JsonMeshParser::~JsonMeshParser() = default;

std::vector<vtkSmartPointer<vtkDataSet>> JsonMeshParser::parse(const std::string& filename) {
  std::vector<vtkSmartPointer<vtkDataSet>> meshes;
  std::ifstream file(filename);
  if (!file.is_open()) {
    return meshes;
  }

  json root;
  try {
    file >> root;
  } catch (const json::exception& e) {
    std::cerr << "Failed to read JSON mesh: " << filename << ": " << e.what() << '\n';
    return meshes;
  }

  if (isMeshPart(root)) {
    auto poly = parsePart(root);
    if (poly) {
      meshes.push_back(poly);
    }
    return meshes;
  }

  const json* parts = meshPartArray(root);
  if (!parts) {
    return meshes;
  }

  for (const auto& part : *parts) {
    if (!isMeshPart(part)) {
      continue;
    }
    auto poly = parsePart(part);
    if (poly) {
      meshes.push_back(poly);
    }
  }
  return meshes;
}

bool JsonMeshParser::canParse(const std::string& filename) {
  const std::string header = readHeader(filename, 64);
  const size_t first = header.find_first_not_of(" \t\r\n");
  if (first == std::string::npos || (header[first] != '[' && header[first] != '{')) {
    return false;
  }

  std::ifstream file(filename);
  if (!file.is_open()) {
    return false;
  }

  try {
    json root;
    file >> root;
    return looksLikeJsonMesh(root);
  } catch (const json::exception&) {
    return false;
  }
}
