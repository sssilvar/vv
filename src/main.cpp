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
#include <cxxopts.hpp>

struct Args {
    std::vector<std::string> meshfiles;
    bool explode_view = false;
    bool version = false;
    bool help = false;
};

Args parseArgs(int argc, char *argv[]) {
    Args args;
    cxxopts::Options options("vv", "A simple mesh viewer");
    options.positional_help("<meshfile> [<meshfile2> ...]");
    options.add_options()
        ("e,explode", "Explode scalar view", cxxopts::value<bool>(args.explode_view))
        ("v,version", "Show version and exit", cxxopts::value<bool>(args.version))
        ("h,help",    "Show help and exit",    cxxopts::value<bool>(args.help))
        ("meshfiles", "Mesh files or '-'",     cxxopts::value<std::vector<std::string>>(args.meshfiles));
    options.parse_positional({"meshfiles"});

    auto result = options.parse(argc, argv);

    if (args.help) {
        std::cout << options.help() << '\n';
        std::exit(0);
    }
    if (args.version) {
        std::cout << "vv version " << VV_VERSION << " (built " << VV_BUILD_DATE << ")\n";
        std::exit(0);
    }
    if (args.meshfiles.empty()) {
        std::cerr << "Usage: vv <meshfile> [<meshfile2> ...]\n" << options.help() << '\n';
        std::exit(1);
    }
    return args;
}

int main(int argc, char *argv[])
{
    Args args = parseArgs(argc, argv);
    
    if (args.meshfiles.size() > 1 && !args.explode_view) {
        std::cerr << "Warning: Multiple mesh files provided without -e flag. Using only the first file." << std::endl;
    }
    
    std::vector<std::string> filesToProcess;
    if (args.explode_view) {
        filesToProcess = args.meshfiles;
    } else {
        filesToProcess.push_back(args.meshfiles[0]);
    }
    
    std::vector<std::unique_ptr<MeshParser>> parsers;
    parsers.emplace_back(std::make_unique<XMLMeshParser>());
    parsers.emplace_back(std::make_unique<VTKMeshParser>());
    parsers.emplace_back(std::make_unique<CartoMeshParser>());
    parsers.emplace_back(std::make_unique<FSurfMeshParser>());
    
    std::vector<vtkSmartPointer<vtkPolyData>> allPolys;
    std::vector<std::string> allNames;
    std::vector<std::string> tmpFiles;
    
    for (const std::string& filename : filesToProcess) {
        std::string realFilename = filename;
        std::string tmpFile;

        if (filename == "-") {
            tmpFile = stdinToTempFile();
            if (tmpFile.empty()) {
                std::cerr << "Failed to create temp file for stdin" << std::endl;
                return 5;
            }
            realFilename = tmpFile;
            tmpFiles.push_back(tmpFile);
        } else {
            if (access(filename.c_str(), F_OK) != 0) {
                std::cerr << "Error: File does not exist: " << filename << std::endl;
                return 1;
            }
        }
        
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
        
        std::vector<vtkSmartPointer<vtkPolyData>> polys = selected->parse(realFilename);
        if (polys.empty())
        {
            std::cerr << "Failed to parse mesh: " << filename << std::endl;
            return 3;
        }
        
        for (auto& poly : polys) {
            allPolys.push_back(poly);
            allNames.push_back(filename);
        }
    }
    
    for (const std::string& tmpFile : tmpFiles) {
        unlink(tmpFile.c_str());
    }
    
    std::vector<std::array<double, 3>> colorsHex;
    for (size_t i = 0; i < allPolys.size(); ++i)
        colorsHex.push_back(generateDistinctColor(static_cast<int>(i)));
    
    MeshRenderer renderer;
    if (args.explode_view) {
        renderer.setupFacetGrid(allPolys, allNames, colorsHex);
        renderer.startFacetGrid();
    } else {
        renderer.setup(allPolys, allNames, colorsHex);
        renderer.start();
    }
    return 0;
}