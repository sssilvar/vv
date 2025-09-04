#include "mesh_utils.h"
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <array>
#include <cmath>

std::string detectFormatFromStream(FILE *in, std::string &tmpFileOut, std::string &detectedFormatOut, size_t maxBytes)
{
    char head[201] = {0};
    size_t nhead = fread(head, 1, maxBytes, in);
    std::string s(head, nhead);
    // XML: starts with <?xml or <DIF or <
    if (s.find("<?xml") == 0 || s.find("<DIF") == 0 || s.find('<') == 0)
        detectedFormatOut = "xml";
    // VTK: contains # vtk DataFile, <VTKFile, or VTK anywhere
    else if (s.find("# vtk DataFile") != std::string::npos ||
             s.find("<VTKFile") != std::string::npos ||
             s.find("VTK") != std::string::npos)
        detectedFormatOut = "vtk";
    else
        detectedFormatOut = "unknown";
    // Write head + rest of in to a temp file
    char tmpname[] = "/tmp/vvstdinXXXXXX";
    int fd = mkstemp(tmpname);
    if (fd == -1)
    {
        std::cerr << "Failed to create temp file for stdin" << std::endl;
        tmpFileOut.clear();
        return detectedFormatOut;
    }
    FILE *out = fdopen(fd, "wb");
    if (!out)
    {
        std::cerr << "Failed to open temp file for writing" << std::endl;
        close(fd);
        unlink(tmpname);
        tmpFileOut.clear();
        return detectedFormatOut;
    }
    if (fwrite(head, 1, nhead, out) != nhead)
    {
        std::cerr << "Failed to write to temp file" << std::endl;
        fclose(out);
        unlink(tmpname);
        tmpFileOut.clear();
        return detectedFormatOut;
    }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        if (fwrite(buf, 1, n, out) != n)
        {
            std::cerr << "Failed to write to temp file" << std::endl;
            fclose(out);
            unlink(tmpname);
            tmpFileOut.clear();
            return detectedFormatOut;
        }
    }
    fclose(out);
    tmpFileOut = tmpname;
    return detectedFormatOut;
}

static inline std::array<double, 3> hsv2rgb(double h, double s, double v)
{
    double h6 = h * 6.0;
    int k = static_cast<int>(std::floor(h6)) % 6;
    double f = h6 - std::floor(h6);
    double p = v * (1.0 - s);
    double q = v * (1.0 - f * s);
    double t = v * (1.0 - (1.0 - f) * s);
    switch (k < 0 ? k + 6 : k)
    {
    case 0:
        return {v, t, p};
    case 1:
        return {q, v, p};
    case 2:
        return {p, v, t};
    case 3:
        return {p, q, v};
    case 4:
        return {t, p, v};
    default:
        return {v, p, q};
    }
}

std::array<double, 3> generateDistinctColor(int i)
{
    if (i == 0)
        return {0.83, 0.83, 0.83};                // lightgray first
    constexpr double phi = 0.6180339887498948482; // golden-ratio conjugate
    double h = std::fmod((i - 1) * phi, 1.0);     // well-spaced hues
    double s = 0.95;
    double v = 1.0;
    return hsv2rgb(h, s, v);
}
