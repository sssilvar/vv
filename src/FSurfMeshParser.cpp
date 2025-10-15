#include "FSurfMeshParser.h"
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <iostream>
#include <cstring>

bool FSurfMeshParser::canParse(const std::string &filename) {
    auto ends_with = [](const std::string &s, const std::string &sfx) {
        return s.size() >= sfx.size() && s.compare(s.size() - sfx.size(), sfx.size(), sfx) == 0;
    };
    return ends_with(filename, ".pial") ||
           ends_with(filename, ".surf") ||
           ends_with(filename, ".white");
}

std::vector<vtkSmartPointer<vtkPolyData>> FSurfMeshParser::parse(const std::string &filename) {
    std::vector<vtkSmartPointer<vtkPolyData>> polys;
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
        std::cerr << "Could not open FreeSurfer surface file: " << filename << std::endl;
        return polys;
    }
    // Magic number: 3 bytes
    uint8_t magic[3];
    f.read(reinterpret_cast<char*>(magic), 3);
    if (!(magic[0] == 255 && magic[1] == 255 && magic[2] == 254)) {
        std::cerr << "Not a FreeSurfer surface file (bad magic)" << std::endl;
        return polys;
    }
    std::string headerline;
    std::getline(f, headerline); // header line (may include 'created by...')
    std::getline(f, headerline); // second header line (blank)
    // Counts
    uint32_t nv, nt;
    f.read(reinterpret_cast<char*>(&nv), 4); nv = __builtin_bswap32(nv);
    f.read(reinterpret_cast<char*>(&nt), 4); nt = __builtin_bswap32(nt);
    vtkNew<vtkPoints> pts;
    pts->SetNumberOfPoints(nv);
    for (uint32_t i = 0; i < nv; ++i) {
        float xyz[3];
        f.read(reinterpret_cast<char*>(xyz), 12);
        for (int j = 0; j < 3; ++j) {
            uint32_t tmp;
            memcpy(&tmp, xyz + j, 4);
            tmp = __builtin_bswap32(tmp);
            memcpy(xyz + j, &tmp, 4);
        }
        pts->SetPoint(i, xyz[0], xyz[1], xyz[2]);
    }
    vtkNew<vtkCellArray> tris;
    for (uint32_t i = 0; i < nt; ++i) {
        uint32_t tidx[3];
        f.read(reinterpret_cast<char*>(tidx), 12);
        for (int j = 0; j < 3; ++j) {
            tidx[j] = __builtin_bswap32(tidx[j]);
        }
        vtkIdType triangle[3] = { static_cast<vtkIdType>(tidx[0]), static_cast<vtkIdType>(tidx[1]), static_cast<vtkIdType>(tidx[2]) };
        tris->InsertNextCell(3, triangle);
    }
    vtkNew<vtkPolyData> poly;
    poly->SetPoints(pts);
    poly->SetPolys(tris);
    polys.push_back(poly);
    return polys;
}
