#include "version.h"
#include "MeshParser.h"
#include "XMLMeshParser.h"
#include "VTKMeshParser.h"
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

// Detect mesh format from buffer (returns "xml", "vtk", or "unknown")
std::string detect_format(const char *buf, size_t n)
{
    std::string s(buf, n);
    // XML: starts with <?xml or <DIF or <
    if (s.find("<?xml") == 0 || s.find("<DIF") == 0 || s.find('<') == 0)
        return "xml";
    // VTK: contains # vtk DataFile, <VTKFile, or VTK anywhere
    if (s.find("# vtk DataFile") != std::string::npos ||
        s.find("<VTKFile") != std::string::npos ||
        s.find("VTK") != std::string::npos)
        return "vtk";
    return "unknown";
}

int main(int argc, char *argv[])
{
    if (argc > 1 && std::string(argv[1]) == "--version")
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
    std::string detectedFormat;
    if (filename == "-")
    {
        detectedFormat = detectFormatFromStream(stdin, tmpFile, detectedFormat);
        if (detectedFormat == "unknown")
        {
            std::cerr << "Could not detect mesh format from stdin" << std::endl;
            return 4;
        }
        realFilename = tmpFile;
        std::cerr << "[DEBUG] Detected format: " << detectedFormat << std::endl;
    }
    // List of available mesh parsers
    std::vector<std::unique_ptr<MeshParser>> parsers;
    parsers.emplace_back(std::make_unique<XMLMeshParser>());
    parsers.emplace_back(std::make_unique<VTKMeshParser>());
    // Find a parser that can handle the file
    MeshParser *selected = nullptr;
    if (!detectedFormat.empty())
    {
        for (auto &parser : parsers)
        {
            if ((detectedFormat == "xml" && dynamic_cast<XMLMeshParser *>(parser.get())) ||
                (detectedFormat == "vtk" && dynamic_cast<VTKMeshParser *>(parser.get())))
            {
                selected = parser.get();
                break;
            }
        }
    }
    else
    {
        for (auto &parser : parsers)
        {
            if (parser->canParse(realFilename))
            {
                selected = parser.get();
                break;
            }
        }
    }
    if (!selected)
    {
        if (!tmpFile.empty())
        {
            std::cerr << "[DEBUG] First 200 bytes of stdin (for parser selection):\n";
            FILE *f = fopen(tmpFile.c_str(), "rb");
            if (f)
            {
                char head[201] = {0};
                size_t n = fread(head, 1, 200, f);
                std::cerr << std::string(head, n) << std::endl;
                fclose(f);
            }
        }
        std::cerr << "No suitable parser found for file: " << filename << std::endl;
        return 2;
    }
    // Parse the mesh(es)
    std::vector<vtkSmartPointer<vtkPolyData>> polys = selected->parse(realFilename);
    if (!tmpFile.empty())
        unlink(tmpFile.c_str());
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