#include "CartoMeshParser.h"
#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkFloatArray.h>
#include <vtkPointData.h>
#include <vtkNew.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cctype>
#include "mesh_utils.h"


CartoMeshParser::~CartoMeshParser() = default;

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(first, (last - first + 1));
}

static bool isSectionHeader(const std::string& line) {
    return line.length() > 2 && line[0] == '[' && line[line.length()-1] == ']';
}

static bool parseVertexLine(const std::string& line, double& x, double& y, double& z,
                           double& nx, double& ny, double& nz, int& groupId) {
    std::string processedLine = line;
    size_t equalsPos = processedLine.find(" = ");
    if (equalsPos != std::string::npos) {
        processedLine.replace(equalsPos, 3, " ");
    }
    std::istringstream iss(processedLine);
    long long index;
    if (iss >> index >> x >> y >> z >> nx >> ny >> nz >> groupId) return true;
    return false;
}

static bool parseTriangleLine(const std::string& line, int& v0, int& v1, int& v2,
                             double& nx, double& ny, double& nz, int& groupId) {
    std::string processedLine = line;
    size_t equalsPos = processedLine.find(" = ");
    if (equalsPos != std::string::npos) {
        processedLine.replace(equalsPos, 3, " ");
    }
    std::istringstream iss(processedLine);
    long long index;
    if (iss >> index >> v0 >> v1 >> v2 >> nx >> ny >> nz >> groupId) return true;
    return false;
}

std::vector<vtkSmartPointer<vtkPolyData>> CartoMeshParser::parse(const std::string &filename)
{
    std::vector<vtkSmartPointer<vtkPolyData>> polys;
    std::ifstream file(filename);
    if (!file.is_open()) {
        return polys;
    }
    std::string line;
    std::vector<std::array<double, 3>> vertices, normals;
    std::vector<std::array<int, 3>> triangles;
    std::vector<int> vertexGroups;
    bool inVerticesSection = false, inTrianglesSection = false;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;
        if (isSectionHeader(line)) {
            inVerticesSection = (line == "[VerticesSection]");
            inTrianglesSection = (line == "[TrianglesSection]");
            continue;
        }
        if (inVerticesSection) {
            double x, y, z, nx, ny, nz; int groupId;
            if (parseVertexLine(line, x, y, z, nx, ny, nz, groupId)) {
                vertices.push_back({x, y, z});
                normals.push_back({nx, ny, nz});
                vertexGroups.push_back(groupId);
            }
        } else if (inTrianglesSection) {
            int v0, v1, v2; double nx, ny, nz; int groupId;
            if (parseTriangleLine(line, v0, v1, v2, nx, ny, nz, groupId)) {
                triangles.push_back({v0, v1, v2});
            }
        }
    }
    file.close();
    if (vertices.empty() || triangles.empty()) return polys;
    vtkNew<vtkPolyData> poly;
    vtkNew<vtkPoints> points;
    points->SetNumberOfPoints(static_cast<vtkIdType>(vertices.size()));
    for (size_t i = 0; i < vertices.size(); ++i) {
        points->SetPoint(static_cast<vtkIdType>(i), vertices[i][0], vertices[i][1], vertices[i][2]);
    }
    poly->SetPoints(points);
    vtkNew<vtkCellArray> trianglesArray;
    for (const auto& tri : triangles) {
        vtkIdType triangle[3] = {static_cast<vtkIdType>(tri[0]), static_cast<vtkIdType>(tri[1]), static_cast<vtkIdType>(tri[2])};
        trianglesArray->InsertNextCell(3, triangle);
    }
    poly->SetPolys(trianglesArray);
    if (!normals.empty() && normals.size() == vertices.size()) {
        vtkNew<vtkFloatArray> normalArray;
        normalArray->SetName("Normals");
        normalArray->SetNumberOfComponents(3);
        normalArray->SetNumberOfTuples(static_cast<vtkIdType>(normals.size()));
        for (size_t i = 0; i < normals.size(); ++i) {
            normalArray->SetTuple3(static_cast<vtkIdType>(i), static_cast<float>(normals[i][0]), static_cast<float>(normals[i][1]), static_cast<float>(normals[i][2]));
        }
        poly->GetPointData()->SetNormals(normalArray);
        poly->GetPointData()->AddArray(normalArray);
    }
    if (!vertexGroups.empty()) {
        vtkNew<vtkFloatArray> groupArray;
        groupArray->SetName("GroupID");
        groupArray->SetNumberOfComponents(1);
        groupArray->SetNumberOfTuples(static_cast<vtkIdType>(vertexGroups.size()));
        for (size_t i = 0; i < vertexGroups.size(); ++i) {
            groupArray->SetValue(static_cast<vtkIdType>(i), static_cast<float>(vertexGroups[i]));
        }
        poly->GetPointData()->AddArray(groupArray);
    }
    polys.push_back(poly);
    return polys;
}

bool CartoMeshParser::canParse(const std::string &filename)
{
    std::string header = readHeader(filename, 200);
    return header.find("#TriangulatedMeshVersion2.0") != std::string::npos;
}
