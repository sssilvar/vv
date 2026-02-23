#include "LSDynaMeshParser.h"

#include "mesh_utils.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <vtkCellType.h>
#include <vtkFieldData.h>
#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkUnstructuredGrid.h>

namespace {

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
    return {};
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

bool startsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string dirOf(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos)
    return ".";
  return path.substr(0, pos);
}

inline double parseDouble(const char* p, size_t w = 16) {
  char buf[32];
  w = (w < 31u) ? w : 31u;
  std::memcpy(buf, p, w);
  buf[w] = '\0';
  return std::strtod(buf, nullptr);
}
inline int parseInt(const char* p, size_t w = 8) {
  char buf[16];
  w = (w < 15u) ? w : 15u;
  std::memcpy(buf, p, w);
  buf[w] = '\0';
  return static_cast<int>(std::strtol(buf, nullptr, 10));
}

int parseIntField(const std::string& s, size_t pos, size_t w) {
  if (pos >= s.size())
    return 0;
  const size_t n = std::min(w, s.size() - pos);
  return parseInt(s.data() + pos, n);
}

double parseDoubleField(const std::string& s, size_t pos, size_t w) {
  if (pos >= s.size())
    return 0.0;
  const size_t n = std::min(w, s.size() - pos);
  return parseDouble(s.data() + pos, n);
}

bool parseTwoInts(const std::string& line, int& a, int& b) {
  std::istringstream iss(line);
  if (!(iss >> a >> b))
    return false;
  return true;
}

bool parseFirstNInts(const std::string& line, int n, int* out) {
  std::istringstream iss(line);
  for (int i = 0; i < n; ++i) {
    if (!(iss >> out[i]))
      return false;
  }
  return true;
}

struct RawNode {
  int nid;
  double x, y, z;
};

// Tet only: pid + 4 node ids (first 4 from solid = degenerate hex)
struct Tet {
  int pid;
  int n[4];
};

struct PartInfo {
  int pid;
  std::string name;
};

// Parse *NODE lines: fixed 8-char nid, 16-char x,y,z
void parseNodeLine(const std::string& line, std::vector<RawNode>& out) {
  const std::string t = trim(line);
  if (t.empty() || t[0] == '$')
    return;

  RawNode n;
  // LS-DYNA node lines can appear in fixed-width or whitespace-delimited format.
  // Try fixed-width first (handles compact "nid-12.3..." style), then tokenized fallback.
  n.nid = parseIntField(line, 0, 8);
  n.x = parseDoubleField(line, 8, 16);
  n.y = parseDoubleField(line, 24, 16);
  n.z = parseDoubleField(line, 40, 16);

  if (n.nid == 0 || (n.x == 0.0 && n.y == 0.0 && n.z == 0.0)) {
    std::istringstream iss(line);
    if (!(iss >> n.nid >> n.x >> n.y >> n.z))
      return;
  }
  if (n.nid == 0)
    return;
  out.push_back(n);
}

// Parse one *ELEMENT_SOLID* block: eid, pid then 4+ node ids. Use first 4 only (tet).
// Skip $# comment lines. If we stop on a * line, put it in nextLine for caller to reuse.
void parseSolidBlock(std::ifstream& f, bool /*ortho*/, std::vector<Tet>& out, std::string* nextLine) {
  std::string line1, line2;
  while (std::getline(f, line1)) {
    const std::string t1 = trim(line1);
    if (t1.empty() || t1[0] == '$')
      continue;
    if (t1[0] == '*') {
      if (nextLine)
        *nextLine = line1;
      return;
    }
    int eid = 0;
    int pid = 0;
    if (!parseTwoInts(line1, eid, pid))
      continue;
    if (eid == 0)
      continue;

    while (std::getline(f, line2)) {
      const std::string t2 = trim(line2);
      if (t2.empty() || t2[0] == '$')
        continue;
      if (t2[0] == '*') {
        if (nextLine)
          *nextLine = line2;
        return;
      }
      Tet tet;
      tet.pid = pid;
      tet.n[0] = parseIntField(line2, 0, 8);
      tet.n[1] = parseIntField(line2, 8, 8);
      tet.n[2] = parseIntField(line2, 16, 8);
      tet.n[3] = parseIntField(line2, 24, 8);
      if (!(tet.n[0] && tet.n[1] && tet.n[2] && tet.n[3])) {
        int n4[4] = {0, 0, 0, 0};
        if (parseFirstNInts(line2, 4, n4)) {
          tet.n[0] = n4[0];
          tet.n[1] = n4[1];
          tet.n[2] = n4[2];
          tet.n[3] = n4[3];
        }
      }
      if (tet.n[0] && tet.n[1] && tet.n[2] && tet.n[3])
        out.push_back(tet);
      break;
    }
  }
}

void parsePartBlock(std::ifstream& f, std::vector<PartInfo>& out, std::string* nextLine) {
  std::string line;
  std::string name;
  bool haveName = false;

  while (std::getline(f, line)) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == '$')
      continue;
    if (t[0] == '*') {
      if (nextLine)
        *nextLine = line;
      return;
    }

    if (!haveName) {
      name = t;
      haveName = true;
      continue;
    }

    int pid = 0;
    if (!parseFirstNInts(line, 1, &pid))
      pid = parseIntField(line, 0, 8);

    if (pid != 0) {
      PartInfo p;
      p.pid = pid;
      p.name = name;
      out.push_back(p);
    }
    return;
  }
}

// Stream-parse one file; merge into nodes/elems/parts. Resolve *INCLUDE in place.
void parseFile(const std::string& filepath,
               const std::string& baseDir,
               std::vector<RawNode>& nodes,
               std::vector<Tet>& elems,
               std::vector<PartInfo>& parts) {
  std::ifstream f(filepath);
  if (!f) {
    std::cerr << "LSDyna: cannot open " << filepath << "\n";
    return;
  }

  std::string line;
  bool reuseLine = false;

  while (reuseLine || std::getline(f, line)) {
    if (!reuseLine && line.empty())
      continue;
    reuseLine = false;

    std::string t = trim(line);
    if (t == "*INCLUDE") {
      std::vector<std::string> incList;
      while (std::getline(f, line)) {
        t = trim(line);
        if (t.empty() || t[0] == '$')
          continue;
        if (t[0] == '*') {
          reuseLine = true;
          break;
        }
        incList.push_back((t[0] == '/') ? t : baseDir + "/" + t);
      }
      for (const auto& inc : incList)
        parseFile(inc, dirOf(inc), nodes, elems, parts);
      continue;
    }
    if (t == "*NODE") {
      while (std::getline(f, line)) {
        if (!line.empty() && line[0] == '*') {
          reuseLine = true;
          break;
        }
        parseNodeLine(line, nodes);
      }
      continue;
    }
    if (startsWith(t, "*ELEMENT_SOLID")) {
      bool ortho = t.find("ORTHO") != std::string::npos;
      std::string next;
      parseSolidBlock(f, ortho, elems, &next);
      if (!next.empty()) {
        line = std::move(next);
        reuseLine = true;
      }
      continue;
    }
    if (t == "*PART") {
      std::string next;
      parsePartBlock(f, parts, &next);
      if (!next.empty()) {
        line = std::move(next);
        reuseLine = true;
      }
      continue;
    }
  }
}

} // namespace

LSDynaMeshParser::~LSDynaMeshParser() = default;

bool LSDynaMeshParser::canParse(const std::string& filename) {
  std::string header = readHeader(filename, 256);
  return header.find("*KEYWORD") != std::string::npos;
}

std::vector<vtkSmartPointer<vtkDataSet>> LSDynaMeshParser::parse(const std::string& filename) {
  std::vector<vtkSmartPointer<vtkDataSet>> result;

  std::vector<RawNode> rawNodes;
  rawNodes.reserve(65536);
  std::vector<Tet> elems;
  elems.reserve(65536);
  std::vector<PartInfo> parts;

  parseFile(filename, dirOf(filename), rawNodes, elems, parts);

  std::cerr << "LSDyna: after parse nodes=" << rawNodes.size() << " elems=" << elems.size() << "\n";
  if (rawNodes.empty() || elems.empty())
    return result;

  std::unordered_map<int, const RawNode*> nidToNode;
  nidToNode.reserve(rawNodes.size());
  for (const auto& n : rawNodes)
    nidToNode[n.nid] = &n;

  std::unordered_map<int, std::string> partNames;
  for (const auto& p : parts)
    partNames[p.pid] = p.name;

  std::unordered_map<int, std::vector<const Tet*>> elemsByPid;
  elemsByPid.reserve(parts.size() + 8);
  for (const auto& tet : elems) {
    elemsByPid[tet.pid].push_back(&tet);
  }

  std::vector<int> pids;
  pids.reserve(elemsByPid.size());
  for (const auto& kv : elemsByPid)
    pids.push_back(kv.first);
  std::sort(pids.begin(), pids.end());

  for (int pid : pids) {
    const auto& partTets = elemsByPid[pid];
    std::unordered_map<int, vtkIdType> compactMap;
    compactMap.reserve(partTets.size() * 4);

    vtkNew<vtkPoints> pts;
    vtkNew<vtkUnstructuredGrid> grid;
    for (const Tet* tet : partTets) {
      vtkIdType tetNodeIds[4];
      bool validTet = true;
      for (int k = 0; k < 4; ++k) {
        const int nid = tet->n[k];
        auto idIt = compactMap.find(nid);
        if (idIt == compactMap.end()) {
          auto nodeIt = nidToNode.find(nid);
          if (nodeIt == nidToNode.end()) {
            validTet = false;
            break;
          }
          const RawNode* rn = nodeIt->second;
          vtkIdType vid = pts->InsertNextPoint(rn->x, rn->y, rn->z);
          compactMap[nid] = vid;
          tetNodeIds[k] = vid;
        } else {
          tetNodeIds[k] = idIt->second;
        }
      }
      if (!validTet)
        continue;
      grid->InsertNextCell(VTK_TETRA, 4, tetNodeIds);
    }

    if (grid->GetNumberOfCells() == 0)
      continue;
    grid->SetPoints(pts);

    std::string name = "Part " + std::to_string(pid) + " (PID=" + std::to_string(pid) + ")";
    auto it = partNames.find(pid);
    if (it != partNames.end() && !it->second.empty())
      name = it->second + " (PID=" + std::to_string(pid) + ")";

    vtkNew<vtkStringArray> nameArr;
    nameArr->SetName("vv_part_name");
    nameArr->InsertNextValue(name);
    grid->GetFieldData()->AddArray(nameArr);

    result.push_back(grid);
  }

  return result;
}
