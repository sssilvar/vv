#include "version.h"
#include "MeshParser.h"
#include "XMLMeshParser.h"
#include "VTKMeshParser.h"
#include "CartoMeshParser.h"
#include "MeshRenderer.h"
#include "mesh_utils.h"
#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <array>
#include <vector>
#include <string>
#include <iostream>
#include <memory>
#include <cstdio>
#include <unistd.h>
#include <cstring>
#include "FSurfMeshParser.h"

int main(int argc, char *argv[])
{
    if (argc > 1 && (std::string(argv[1]) == "--version" || std::string(argv[1]) == "-v"))
    {
        std::cout << "vv version " << VV_VERSION << " (built " << VV_BUILD_DATE << ")" << std::endl;
        return 0;
    }
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <meshfile>\n";
        return 1;
    }
    std::string filename = argv[1];
    std::string realFilename = filename;
    std::string tmpFile;
    if (filename == "-") {
        tmpFile = stdinToTempFile();
        if (tmpFile.empty()) {
            std::cerr << "Failed to create temp file for stdin" << std::endl;
            return 5;
        }
        realFilename = tmpFile;
    } else {
        if (access(filename.c_str(), F_OK) != 0) {
            std::cerr << "Error: File does not exist: " << filename << std::endl;
            return 1;
        }
    }
    // List of available mesh parsers
    std::vector<std::unique_ptr<MeshParser>> parsers;
    parsers.emplace_back(std::make_unique<XMLMeshParser>());
    parsers.emplace_back(std::make_unique<VTKMeshParser>());
    parsers.emplace_back(std::make_unique<CartoMeshParser>());
    parsers.emplace_back(std::make_unique<FSurfMeshParser>());
    // Find a parser that can handle the file or stdin
    MeshParser *selected = nullptr;
    for (auto &parser : parsers)
    {
        if (parser->canParse(realFilename))
        {
            selected = parser.get();
            break;
        }
    }
    if (!selected)
    {
        std::cerr << "No suitable parser found for file: " << filename << std::endl;
        return 2;
    }
    // Parse the mesh(es)
    std::vector<vtkSmartPointer<vtkPolyData>> polys = selected->parse(realFilename);
    if (!tmpFile.empty()) unlink(tmpFile.c_str());
    if (polys.empty())
    {
        std::cerr << "Failed to parse mesh: " << filename << std::endl;
        return 3;
    }
    // Names and colors for each mesh (distinct colors)
    std::vector<std::string> names(polys.size(), filename);
    std::vector<std::array<double, 3>> colorsHex;
    for (size_t i = 0; i < polys.size(); ++i)
        colorsHex.push_back(generateDistinctColor(static_cast<int>(i)));
    // Render
    MeshRenderer renderer;
    renderer.setup(polys, names, colorsHex);
    renderer.start();
    return 0;
}