#include "XdmfMeshParser.h"

#include "TemporalSource.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkFloatArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkXMLDataElement.h>
#include <vtkXMLUtilities.h>
#include <vtk_hdf5.h>

namespace {

// Frames of one XDMF Attribute: an HDF5 dataset path per time step.
struct AttributeSeries {
  bool cell = false;
  std::vector<std::string> datasets;
};

// Cap for the in-memory frame cache of the array being played. 302 steps of a
// 23k-node field is ~28 MB, so typical runs cache whole and loop without I/O.
constexpr size_t kCacheBudgetBytes = 512u * 1024u * 1024u;

class H5File {
public:
  explicit H5File(const std::string& path)
      : id_(H5Fopen(path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT)) {}
  ~H5File() {
    if (id_ >= 0) {
      H5Fclose(id_);
    }
  }
  H5File(const H5File&) = delete;
  H5File& operator=(const H5File&) = delete;

  bool valid() const {
    return id_ >= 0;
  }

  // Read a whole dataset, converting to `type` on the fly. Fails unless the
  // dataset holds exactly `count` elements.
  bool read(const std::string& path, hid_t type, void* buffer, hsize_t count) const {
    if (id_ < 0) {
      return false;
    }
    const hid_t dset = H5Dopen2(id_, path.c_str(), H5P_DEFAULT);
    if (dset < 0) {
      return false;
    }
    const hid_t space = H5Dget_space(dset);
    const bool sizeOk = space >= 0 && H5Sget_simple_extent_npoints(space) == hssize_t(count);
    const bool ok = sizeOk && H5Dread(dset, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, buffer) >= 0;
    if (space >= 0) {
      H5Sclose(space);
    }
    H5Dclose(dset);
    return ok;
  }

  hssize_t datasetSize(const std::string& path) const {
    if (id_ < 0) {
      return -1;
    }
    const hid_t dset = H5Dopen2(id_, path.c_str(), H5P_DEFAULT);
    if (dset < 0) {
      return -1;
    }
    const hid_t space = H5Dget_space(dset);
    const hssize_t n = space >= 0 ? H5Sget_simple_extent_npoints(space) : -1;
    if (space >= 0) {
      H5Sclose(space);
    }
    H5Dclose(dset);
    return n;
  }

private:
  hid_t id_;
};

class XdmfTemporalSource : public TemporalSource {
public:
  XdmfTemporalSource(std::shared_ptr<H5File> file,
                     std::map<std::string, AttributeSeries> attributes,
                     std::vector<double> times,
                     vtkIdType numPoints,
                     vtkIdType numCells)
      : file_(std::move(file)), attributes_(std::move(attributes)), numPoints_(numPoints),
        numCells_(numCells) {
    timeValues_ = std::move(times);
  }

  void setActiveArray(const std::string& scalarName) override {
    if (scalarName == active_) {
      return;
    }
    active_ = scalarName;
    cache_.assign(attributes_.count(scalarName) != 0 ? timeValues_.size() : 0, {});
    cachedBytes_ = 0;
  }

  bool readStepInto(int step, vtkDataSet* target) override {
    const AttributeSeries* series = seriesFor(active_);
    if (!series || !target || step < 0 || step >= steps()) {
      return false;
    }
    vtkFloatArray* array = ensureArray(target, *series);
    if (!array) {
      return false;
    }
    const size_t index = static_cast<size_t>(step);
    const size_t count = static_cast<size_t>(array->GetNumberOfTuples());
    float* out = array->GetPointer(0);

    if (index < cache_.size() && !cache_[index].empty()) {
      std::memcpy(out, cache_[index].data(), count * sizeof(float));
    } else {
      if (!file_->read(series->datasets[index], H5T_NATIVE_FLOAT, out, hsize_t(count))) {
        return false;
      }
      const size_t bytes = count * sizeof(float);
      if (index < cache_.size() && cachedBytes_ + bytes <= kCacheBudgetBytes) {
        cache_[index].assign(out, out + count);
        cachedBytes_ += bytes;
      }
    }
    // Only the scalar values changed: touching the array (not the geometry) keeps
    // the point/cell buffers off the per-frame upload path.
    array->Modified();
    return true;
  }

  bool sampledScalarRange(const std::string& scalarName, double out[2], int maxSamples) override {
    const AttributeSeries* series = seriesFor(scalarName);
    if (!series || steps() <= 0) {
      return false;
    }
    const size_t count = static_cast<size_t>(series->cell ? numCells_ : numPoints_);
    std::vector<float> buffer(count);
    const int sampleCount = std::min(steps(), std::max(1, maxSamples));
    float lo = 0.0F;
    float hi = 0.0F;
    bool any = false;
    for (int s = 0; s < sampleCount; ++s) {
      const int step =
          sampleCount == 1
              ? 0
              : static_cast<int>((static_cast<long long>(s) * (steps() - 1)) / (sampleCount - 1));
      const bool cached =
          static_cast<size_t>(step) < cache_.size() && !cache_[static_cast<size_t>(step)].empty();
      const float* data = cached ? cache_[static_cast<size_t>(step)].data() : buffer.data();
      if (!cached && !file_->read(series->datasets[static_cast<size_t>(step)],
                                  H5T_NATIVE_FLOAT,
                                  buffer.data(),
                                  hsize_t(count))) {
        continue;
      }
      const auto [minIt, maxIt] = std::minmax_element(data, data + count);
      lo = any ? std::min(lo, *minIt) : *minIt;
      hi = any ? std::max(hi, *maxIt) : *maxIt;
      any = true;
    }
    if (!any) {
      return false;
    }
    out[0] = double(lo);
    out[1] = double(hi);
    return true;
  }

private:
  const AttributeSeries* seriesFor(const std::string& name) const {
    const auto it = attributes_.find(name);
    return it == attributes_.end() ? nullptr : &it->second;
  }

  vtkFloatArray* ensureArray(vtkDataSet* target, const AttributeSeries& series) const {
    vtkDataSetAttributes* data = series.cell
                                     ? static_cast<vtkDataSetAttributes*>(target->GetCellData())
                                     : static_cast<vtkDataSetAttributes*>(target->GetPointData());
    if (!data) {
      return nullptr;
    }
    auto* existing = vtkFloatArray::SafeDownCast(data->GetArray(active_.c_str()));
    if (existing) {
      return existing;
    }
    vtkNew<vtkFloatArray> array;
    array->SetName(active_.c_str());
    array->SetNumberOfComponents(1);
    array->SetNumberOfTuples(series.cell ? numCells_ : numPoints_);
    data->AddArray(array);
    return array;
  }

  std::shared_ptr<H5File> file_;
  std::map<std::string, AttributeSeries> attributes_;
  std::string active_;
  std::vector<std::vector<float>> cache_;
  size_t cachedBytes_ = 0;
  vtkIdType numPoints_ = 0;
  vtkIdType numCells_ = 0;
};

vtkXMLDataElement* childNamed(vtkXMLDataElement* parent, const char* name) {
  return parent ? parent->FindNestedElementWithName(name) : nullptr;
}

std::string attributeOr(vtkXMLDataElement* element, const char* name, const std::string& fallback) {
  const char* value = element ? element->GetAttribute(name) : nullptr;
  return value ? std::string(value) : fallback;
}

// "s1s2.h5:/Function/v/0" (heavy-data reference, relative to the XDMF file).
bool splitDataItem(vtkXMLDataElement* dataItem,
                   const std::filesystem::path& baseDir,
                   std::string& h5File,
                   std::string& h5Path) {
  const char* text = dataItem ? dataItem->GetCharacterData() : nullptr;
  if (!text) {
    return false;
  }
  std::string reference(text);
  const size_t begin = reference.find_first_not_of(" \t\r\n");
  const size_t end = reference.find_last_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return false;
  }
  reference = reference.substr(begin, end - begin + 1);
  const size_t colon = reference.rfind(':');
  if (colon == std::string::npos) {
    return false;
  }
  h5File = (baseDir / reference.substr(0, colon)).string();
  h5Path = reference.substr(colon + 1);
  return true;
}

int nodesPerElement(const std::string& topologyType) {
  if (topologyType == "Triangle") {
    return 3;
  }
  if (topologyType == "Quadrilateral") {
    return 4;
  }
  if (topologyType == "Polyline" || topologyType == "Line") {
    return 2;
  }
  return 0;
}

// Collect every Grid element under `root`, flattening collections.
void collectGrids(vtkXMLDataElement* root, std::vector<vtkXMLDataElement*>& out) {
  for (int i = 0; i < root->GetNumberOfNestedElements(); ++i) {
    vtkXMLDataElement* child = root->GetNestedElement(i);
    if (child && child->GetName() && std::string(child->GetName()) == "Grid") {
      out.push_back(child);
      collectGrids(child, out);
    }
  }
}

} // namespace

XdmfMeshParser::XdmfMeshParser() = default;
XdmfMeshParser::~XdmfMeshParser() = default;

bool XdmfMeshParser::canParse(const std::string& filename) {
  const std::string ext = std::filesystem::path(filename).extension().string();
  return ext == ".xdmf" || ext == ".xmf";
}

std::vector<vtkSmartPointer<vtkDataSet>> XdmfMeshParser::parse(const std::string& filename) {
  temporal_.reset();
  vtkSmartPointer<vtkXMLDataElement> root = vtkSmartPointer<vtkXMLDataElement>::Take(
      vtkXMLUtilities::ReadElementFromFile(filename.c_str()));
  vtkXMLDataElement* domain = childNamed(root, "Domain");
  if (!domain) {
    std::cerr << "XDMF: no <Domain> in " << filename << "\n";
    return {};
  }

  const std::filesystem::path baseDir = std::filesystem::path(filename).parent_path();
  std::vector<vtkXMLDataElement*> grids;
  collectGrids(domain, grids);

  // The topology/geometry are written once; temporal grids reference them by
  // xpointer, which we do not resolve — take the first grid that carries them.
  vtkXMLDataElement* topologyElem = nullptr;
  vtkXMLDataElement* geometryElem = nullptr;
  for (vtkXMLDataElement* grid : grids) {
    if (!topologyElem) {
      topologyElem = childNamed(grid, "Topology");
      geometryElem = childNamed(grid, "Geometry");
      if (topologyElem && geometryElem) {
        break;
      }
      topologyElem = nullptr;
    }
  }
  if (!topologyElem || !geometryElem) {
    std::cerr << "XDMF: no <Topology>/<Geometry> in " << filename << "\n";
    return {};
  }

  std::string h5File;
  std::string topologyPath;
  std::string geometryPath;
  if (!splitDataItem(childNamed(topologyElem, "DataItem"), baseDir, h5File, topologyPath) ||
      !splitDataItem(childNamed(geometryElem, "DataItem"), baseDir, h5File, geometryPath)) {
    std::cerr << "XDMF: only HDF5 heavy data is supported\n";
    return {};
  }

  auto file = std::make_shared<H5File>(h5File);
  if (!file->valid()) {
    std::cerr << "XDMF: cannot open heavy data file " << h5File << "\n";
    return {};
  }

  const int perCell = nodesPerElement(attributeOr(topologyElem, "TopologyType", ""));
  if (perCell == 0) {
    std::cerr << "XDMF: unsupported TopologyType " << attributeOr(topologyElem, "TopologyType", "?")
              << "\n";
    return {};
  }
  const std::string geometryType = attributeOr(geometryElem, "GeometryType", "XYZ");
  const int perPoint = geometryType == "XY" ? 2 : (geometryType == "XYZ" ? 3 : 0);
  if (perPoint == 0) {
    std::cerr << "XDMF: unsupported GeometryType " << geometryType << "\n";
    return {};
  }

  const hssize_t coordCount = file->datasetSize(geometryPath);
  const hssize_t connCount = file->datasetSize(topologyPath);
  if (coordCount <= 0 || connCount <= 0 || coordCount % perPoint != 0 || connCount % perCell != 0) {
    std::cerr << "XDMF: geometry/topology dimensions do not match the declared types\n";
    return {};
  }
  const vtkIdType numPoints = vtkIdType(coordCount / perPoint);
  const vtkIdType numCells = vtkIdType(connCount / perCell);

  std::vector<float> coords(static_cast<size_t>(coordCount));
  std::vector<long long> connectivity(static_cast<size_t>(connCount));
  if (!file->read(geometryPath, H5T_NATIVE_FLOAT, coords.data(), hsize_t(coordCount)) ||
      !file->read(topologyPath, H5T_NATIVE_LLONG, connectivity.data(), hsize_t(connCount))) {
    std::cerr << "XDMF: failed to read geometry/topology from " << h5File << "\n";
    return {};
  }

  vtkNew<vtkPoints> points;
  points->SetDataTypeToFloat();
  points->SetNumberOfPoints(numPoints);
  for (vtkIdType i = 0; i < numPoints; ++i) {
    const size_t base = static_cast<size_t>(i) * static_cast<size_t>(perPoint);
    points->SetPoint(i,
                     double(coords[base]),
                     double(coords[base + 1]),
                     perPoint == 3 ? double(coords[base + 2]) : 0.0);
  }

  vtkNew<vtkCellArray> cells;
  cells->AllocateEstimate(numCells, perCell);
  std::vector<vtkIdType> ids(static_cast<size_t>(perCell));
  for (vtkIdType c = 0; c < numCells; ++c) {
    const size_t base = static_cast<size_t>(c) * static_cast<size_t>(perCell);
    for (int k = 0; k < perCell; ++k) {
      ids[static_cast<size_t>(k)] = vtkIdType(connectivity[base + static_cast<size_t>(k)]);
    }
    cells->InsertNextCell(perCell, ids.data());
  }

  vtkNew<vtkPolyData> mesh;
  mesh->SetPoints(points);
  if (perCell == 2) {
    mesh->SetLines(cells);
  } else {
    mesh->SetPolys(cells);
  }

  // Attributes: one entry per time step, in the order the grids appear.
  std::map<std::string, AttributeSeries> attributes;
  std::vector<double> times;
  for (vtkXMLDataElement* grid : grids) {
    std::vector<vtkXMLDataElement*> attributeElems;
    for (int i = 0; i < grid->GetNumberOfNestedElements(); ++i) {
      vtkXMLDataElement* child = grid->GetNestedElement(i);
      if (child && child->GetName() && std::string(child->GetName()) == "Attribute") {
        attributeElems.push_back(child);
      }
    }
    if (attributeElems.empty()) {
      continue;
    }
    double timeValue = double(times.size());
    if (vtkXMLDataElement* timeElem = childNamed(grid, "Time")) {
      timeElem->GetScalarAttribute("Value", timeValue);
    }
    times.push_back(timeValue);
    for (vtkXMLDataElement* attributeElem : attributeElems) {
      std::string ignored;
      std::string path;
      if (!splitDataItem(childNamed(attributeElem, "DataItem"), baseDir, ignored, path)) {
        continue;
      }
      const std::string name = attributeOr(attributeElem, "Name", "attribute");
      AttributeSeries& series = attributes[name];
      series.cell = attributeOr(attributeElem, "Center", "Node") == "Cell";
      series.datasets.resize(times.size() - 1);
      series.datasets.push_back(path);
    }
  }

  // Drop series that are not defined on every step; they cannot be played.
  for (auto it = attributes.begin(); it != attributes.end();) {
    it->second.datasets.resize(times.size());
    const bool complete = std::none_of(it->second.datasets.begin(),
                                       it->second.datasets.end(),
                                       [](const std::string& p) { return p.empty(); });
    it = complete ? std::next(it) : attributes.erase(it);
  }

  std::vector<vtkSmartPointer<vtkDataSet>> result{mesh};
  if (attributes.empty()) {
    return result;
  }

  auto source = std::make_shared<XdmfTemporalSource>(file, attributes, times, numPoints, numCells);
  // Materialise every attribute's first frame so the scalar picker lists them all;
  // playback then streams only whichever one is active.
  for (const auto& [name, series] : attributes) {
    source->setActiveArray(name);
    source->readStepInto(0, mesh);
  }
  const std::string& first = attributes.begin()->first;
  source->setActiveArray(first);
  if (attributes.begin()->second.cell) {
    mesh->GetCellData()->SetActiveScalars(first.c_str());
  } else {
    mesh->GetPointData()->SetActiveScalars(first.c_str());
  }
  temporal_ = source;
  return result;
}
