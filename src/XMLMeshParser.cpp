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
        for (vtkIdType i = 0; i < static_cast<vtkIdType>(verts.size() / 3); ++i)
            pts->SetPoint(i, verts[3 * i + 0], verts[3 * i + 1], verts[3 * i + 2]);
        vtkXMLDataElement *polyElem = vol->FindNestedElementWithName("Polygons");
        if (!polyElem)
            continue;
        auto ids = ParseIds(polyElem->GetCharacterData() ? polyElem->GetCharacterData() : "");
        if (ids.size() % 3 != 0)
            continue;
        vtkNew<vtkCellArray> polysArr;
        vtkIdType tri[3];
        bool oneBased = true;
        for (size_t j = 0; j < ids.size(); ++j)
            if (ids[j] == 0)
            {
                oneBased = false;
                break;
            }
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
                for (vtkIdType i = 0; i < static_cast<vtkIdType>(n.size() / 3); ++i)
                    an->SetTuple3(i, static_cast<float>(n[3 * i]), static_cast<float>(n[3 * i + 1]), static_cast<float>(n[3 * i + 2]));
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
    // Simple extension check for now
    auto ends_with = [](const std::string &str, const std::string &suffix)
    {
        return str.size() >= suffix.size() &&
               str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    auto tolower = [](char c)
    { return std::tolower(static_cast<unsigned char>(c)); };
    std::string fname_lower(filename.size(), '\0');
    std::transform(filename.begin(), filename.end(), fname_lower.begin(), tolower);
    return fname_lower.find("model") != std::string::npos && fname_lower.find("groups") != std::string::npos;
}
