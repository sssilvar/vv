#include "MeshLoading.h"

#include "CartoMeshParser.h"
#include "FSurfMeshParser.h"
#include "LSDynaMeshParser.h"
#include "MeshParser.h"
#include "VTKMeshParser.h"
#include "XMLMeshParser.h"
#include "mesh_utils.h"

#include <memory>
#include <unistd.h>
#include <vtkFieldData.h>
#include <vtkStringArray.h>

namespace {

std::string basenameOf(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos) {
    return path;
  }
  return path.substr(slash + 1);
}

std::string partNameFromMesh(vtkDataSet* mesh) {
  if (!mesh || !mesh->GetFieldData()) {
    return "";
  }
  auto* nameArray =
      vtkStringArray::SafeDownCast(mesh->GetFieldData()->GetAbstractArray("vv_part_name"));
  if (!nameArray || nameArray->GetNumberOfValues() == 0) {
    return "";
  }
  return nameArray->GetValue(0);
}

std::vector<std::string> filesToProcessFromArgs(const std::vector<std::string>& meshfiles,
                                                bool explodeView) {
  if (explodeView) {
    return meshfiles;
  }
  return {meshfiles.front()};
}

std::vector<std::unique_ptr<MeshParser>> buildParsers() {
  std::vector<std::unique_ptr<MeshParser>> parsers;
  parsers.emplace_back(std::make_unique<XMLMeshParser>());
  parsers.emplace_back(std::make_unique<VTKMeshParser>());
  parsers.emplace_back(std::make_unique<CartoMeshParser>());
  parsers.emplace_back(std::make_unique<FSurfMeshParser>());
  parsers.emplace_back(std::make_unique<LSDynaMeshParser>());
  return parsers;
}

class TempFileCleanup {
public:
  ~TempFileCleanup() {
    for (const std::string& tmpFile : tmpFiles_) {
      unlink(tmpFile.c_str());
    }
  }

  void add(const std::string& tmpFile) {
    tmpFiles_.push_back(tmpFile);
  }

private:
  std::vector<std::string> tmpFiles_;
};

} // namespace

MeshLoadResult loadMeshes(const std::vector<std::string>& meshfiles, bool explodeView) {
  MeshLoadResult result;
  auto parsers = buildParsers();
  auto filesToProcess = filesToProcessFromArgs(meshfiles, explodeView);
  TempFileCleanup tmpCleanup;

  for (const std::string& filename : filesToProcess) {
    std::string realFilename = filename;

    if (filename == "-") {
      std::string tmpFile = stdinToTempFile();
      if (tmpFile.empty()) {
        result.ok = false;
        result.exitCode = 5;
        result.error = "Failed to create temp file for stdin";
        return result;
      }
      realFilename = tmpFile;
      tmpCleanup.add(tmpFile);
    } else {
      if (access(filename.c_str(), F_OK) != 0) {
        result.ok = false;
        result.exitCode = 1;
        result.error = "Error: File does not exist: " + filename;
        return result;
      }
    }

    MeshParser* selected = nullptr;
    for (auto& parser : parsers) {
      if (parser->canParse(realFilename)) {
        selected = parser.get();
        break;
      }
    }

    if (!selected) {
      result.ok = false;
      result.exitCode = 2;
      result.error = "No suitable parser found for file: " + filename;
      return result;
    }

    std::vector<vtkSmartPointer<vtkDataSet>> parsedMeshes = selected->parse(realFilename);
    if (parsedMeshes.empty()) {
      result.ok = false;
      result.exitCode = 3;
      result.error = "Failed to parse mesh: " + filename;
      return result;
    }

    MeshGroup group;
    group.name = basenameOf(filename);

    for (size_t partIndex = 0; partIndex < parsedMeshes.size(); ++partIndex) {
      auto& mesh = parsedMeshes[partIndex];
      const size_t globalIndex = result.meshes.meshes.size();
      result.meshes.meshes.push_back(mesh);
      result.meshes.names.push_back(filename);
      const std::string parsedPartName = partNameFromMesh(mesh);
      if (!parsedPartName.empty()) {
        result.meshes.partNames.push_back(parsedPartName);
      } else if (parsedMeshes.size() == 1) {
        result.meshes.partNames.push_back(group.name);
      } else {
        result.meshes.partNames.push_back("Part " + std::to_string(partIndex + 1));
      }
      group.partIndices.push_back(globalIndex);
    }

    result.meshes.groups.push_back(std::move(group));
  }

  result.ok = true;
  result.exitCode = 0;
  return result;
}
