#include "XMLMeshParser.h"
#include <vtkXMLDataElement.h>
#include <vtkXMLUtilities.h>
#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <vtkFloatArray.h>
#include <vtkPointData.h>
#include "mesh_utils.h"

XMLMeshParser::~XMLMeshParser() = default;

// Helper: parse doubles from string (copied from main.cpp)
static std::vector<double> ParseDoubles(const std::string &s)
{
    std::istringstream iss(s);
    std::vector<double> v;
    double x;
    while (iss >> x)
        v.push_back(x);
    return v;
}
// Helper: parse ids from string
static std::vector<vtkIdType> ParseIds(const std::string &s)
{
    std::istringstream iss(s);
    std::vector<vtkIdType> v;
    long long x;
    while (iss >> x)
        v.push_back(static_cast<vtkIdType>(x));
    return v;
}

std::vector<vtkSmartPointer<vtkPolyData>> XMLMeshParser::parse(const std::string &filename)
{
    std::vector<vtkSmartPointer<vtkPolyData>> polys;
    vtkSmartPointer<vtkXMLDataElement> root =
        vtkSmartPointer<vtkXMLDataElement>::Take(
            vtkXMLUtilities::ReadElementFromFile(filename.c_str()));
    if (!root)
    {
        std::cerr << "Failed to read XML: " << filename << std::endl;
        return polys;
    }
    vtkXMLDataElement *body = root->FindNestedElementWithName("DIFBody");
    if (!body)
        body = root;
    vtkXMLDataElement *vols = body->FindNestedElementWithName("Volumes");
    if (!vols)
    {
        std::cerr << "No <Volumes> in XML: " << filename << std::endl;
        return polys;
    }
    for (int i = 0; i < vols->GetNumberOfNestedElements(); ++i)
    {
        vtkXMLDataElement *vol = vols->GetNestedElement(i);
        if (std::string(vol->GetName()) != "Volume")
            continue;
        vtkXMLDataElement *vertsElem = vol->FindNestedElementWithName("Vertices");
        if (!vertsElem)
            continue;
        auto verts = ParseDoubles(vertsElem->GetCharacterData() ? vertsElem->GetCharacterData() : "");
        if (verts.size() % 3 != 0)
            continue;
        vtkNew<vtkPoints> pts;
        pts->SetNumberOfPoints(static_cast<vtkIdType>(verts.size() / 3));
        for (vtkIdType vi = 0; vi < static_cast<vtkIdType>(verts.size() / 3); ++vi)
            pts->SetPoint(vi, verts[3 * vi + 0], verts[3 * vi + 1], verts[3 * vi + 2]);
        vtkXMLDataElement *polyElem = vol->FindNestedElementWithName("Polygons");
        if (!polyElem)
            continue;
        auto ids = ParseIds(polyElem->GetCharacterData() ? polyElem->GetCharacterData() : "");
        if (ids.size() % 3 != 0)
            continue;
        vtkNew<vtkCellArray> polysArr;
        vtkIdType tri[3];
        bool oneBased = true;
        if (std::any_of(ids.begin(), ids.end(), [](vtkIdType v){ return v == 0; }))
            oneBased = false;
        for (size_t j = 0; j < ids.size(); j += 3)
        {
            tri[0] = ids[j + 0] - (oneBased ? 1 : 0);
            tri[1] = ids[j + 1] - (oneBased ? 1 : 0);
            tri[2] = ids[j + 2] - (oneBased ? 1 : 0);
            polysArr->InsertNextCell(3, tri);
        }
        vtkNew<vtkPolyData> poly;
        poly->SetPoints(pts);
        poly->SetPolys(polysArr);
        vtkXMLDataElement *normElem = vol->FindNestedElementWithName("Normals");
        if (normElem)
        {
            auto n = ParseDoubles(normElem->GetCharacterData() ? normElem->GetCharacterData() : "");
            if (n.size() == verts.size())
            {
                vtkNew<vtkFloatArray> an;
                an->SetName("Normals");
                an->SetNumberOfComponents(3);
                an->SetNumberOfTuples(static_cast<vtkIdType>(n.size() / 3));
                for (vtkIdType ni = 0; ni < static_cast<vtkIdType>(n.size() / 3); ++ni)
                    an->SetTuple3(ni, static_cast<float>(n[3 * ni]), static_cast<float>(n[3 * ni + 1]), static_cast<float>(n[3 * ni + 2]));
                poly->GetPointData()->SetNormals(an);
                poly->GetPointData()->AddArray(an);
            }
        }
        polys.push_back(poly);
    }
    return polys;
}

bool XMLMeshParser::canParse(const std::string &filename)
{
    std::string header = readHeader(filename, 200);
    return header.find("xml") != std::string::npos && header.find("DIF") != std::string::npos;
}
