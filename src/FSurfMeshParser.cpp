#include "FSurfMeshParser.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#include <vtkCellArray.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>

namespace {
uint32_t bswap32(uint32_t value) {
#if defined(_MSC_VER)
  return _byteswap_ulong(value);
#elif defined(__clang__) || defined(__GNUC__)
  return __builtin_bswap32(value);
#else
  return ((value & 0x000000FFu) << 24) | ((value & 0x0000FF00u) << 8) |
         ((value & 0x00FF0000u) >> 8) | ((value & 0xFF000000u) >> 24);
#endif
}
} // namespace

bool FSurfMeshParser::canParse(const std::string& filename) {
  auto ends_with = [](const std::string& s, const std::string& sfx) {
    return s.size() >= sfx.size() && s.compare(s.size() - sfx.size(), sfx.size(), sfx) == 0;
  };
  return ends_with(filename, ".pial") || ends_with(filename, ".surf") ||
         ends_with(filename, ".white");
}

std::vector<vtkSmartPointer<vtkDataSet>> FSurfMeshParser::parse(const std::string& filename) {
  std::vector<vtkSmartPointer<vtkDataSet>> polys;
  std::ifstream f(filename, std::ios::binary);
  if (!f) {
    std::cerr << "Could not open FreeSurfer surface file: " << filename << '\n';
    return polys;
  }
  // Magic number: 3 bytes
  uint8_t magic[3];
  if (!f.read(reinterpret_cast<char*>(magic), 3) ||
      !(magic[0] == 255 && magic[1] == 255 && magic[2] == 254)) {
    std::cerr << "Not a FreeSurfer surface file (bad magic)" << '\n';
    return polys;
  }
  std::string headerline;
  std::getline(f, headerline); // header line (may include 'created by...')
  std::getline(f, headerline); // second header line (blank)
  // Counts. Cross-check against the remaining file size so a corrupt header
  // cannot trigger a huge allocation or reads of garbage data.
  uint32_t nv, nt;
  if (!f.read(reinterpret_cast<char*>(&nv), 4) || !f.read(reinterpret_cast<char*>(&nt), 4)) {
    std::cerr << "Truncated FreeSurfer surface file: " << filename << '\n';
    return polys;
  }
  nv = bswap32(nv);
  nt = bswap32(nt);
  const std::streamoff dataStart = f.tellg();
  f.seekg(0, std::ios::end);
  const std::streamoff fileEnd = f.tellg();
  f.seekg(dataStart, std::ios::beg);
  const uint64_t available = (fileEnd > dataStart) ? static_cast<uint64_t>(fileEnd - dataStart) : 0;
  const uint64_t needed = (static_cast<uint64_t>(nv) + static_cast<uint64_t>(nt)) * 12u;
  if (nv == 0 || nt == 0 || needed > available) {
    std::cerr << "Invalid FreeSurfer surface counts in " << filename << " (vertices=" << nv
              << ", triangles=" << nt << ")" << '\n';
    return polys;
  }
  vtkNew<vtkPoints> pts;
  pts->SetNumberOfPoints(nv);
  for (uint32_t i = 0; i < nv; ++i) {
    float xyz[3];
    if (!f.read(reinterpret_cast<char*>(xyz), 12)) {
      std::cerr << "Truncated FreeSurfer surface file: " << filename << '\n';
      return polys;
    }
    for (int j = 0; j < 3; ++j) {
      uint32_t tmp;
      memcpy(&tmp, xyz + j, 4);
      tmp = bswap32(tmp);
      memcpy(xyz + j, &tmp, 4);
    }
    pts->SetPoint(
        i, static_cast<double>(xyz[0]), static_cast<double>(xyz[1]), static_cast<double>(xyz[2]));
  }
  vtkNew<vtkCellArray> tris;
  for (uint32_t i = 0; i < nt; ++i) {
    uint32_t tidx[3];
    if (!f.read(reinterpret_cast<char*>(tidx), 12)) {
      std::cerr << "Truncated FreeSurfer surface file: " << filename << '\n';
      return polys;
    }
    for (int j = 0; j < 3; ++j) {
      tidx[j] = bswap32(tidx[j]);
    }
    if (tidx[0] >= nv || tidx[1] >= nv || tidx[2] >= nv) {
      std::cerr << "FreeSurfer surface has out-of-range triangle index in " << filename << '\n';
      return polys;
    }
    vtkIdType triangle[3] = {static_cast<vtkIdType>(tidx[0]),
                             static_cast<vtkIdType>(tidx[1]),
                             static_cast<vtkIdType>(tidx[2])};
    tris->InsertNextCell(3, triangle);
  }
  vtkNew<vtkPolyData> poly;
  poly->SetPoints(pts);
  poly->SetPolys(tris);
  polys.push_back(poly);
  return polys;
}
