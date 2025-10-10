#include "mesh_utils.h"
#include <cstdio>
#include <string>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <cstring>
#include <array>
#include <cmath>

std::string readHeader(FILE *f, size_t nbytes) {
    if (!f) return {};
    std::string s(nbytes, '\0');
    size_t n = fread(&s[0], 1, nbytes, f);
    s.resize(n);
    return s;
}
std::string readHeader(const std::string& filename, size_t nbytes) {
    if (filename == "-") return readHeader(stdin, nbytes);
    FILE *f = fopen(filename.c_str(), "rb");
    if (!f) return {};
    std::string s = readHeader(f, nbytes);
    fclose(f);
    return s;
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

std::string stdinToTempFile() {
    std::string tmpPath;
#ifdef _WIN32
    char tmpName[L_tmpnam+1];
    errno_t err = tmpnam_s(tmpName, L_tmpnam);
    if (err != 0) return {};
    tmpPath = tmpName;
    FILE *out = fopen(tmpPath.c_str(), "wb");
    if (!out) return {};
#else
    char tmpName[] = "/tmp/vvstdinXXXXXX";
    int fd = mkstemp(tmpName);
    if (fd == -1) return {};
    FILE *out = fdopen(fd, "wb");
    if (!out) {
        close(fd);
        unlink(tmpName);
        return {};
    }
    tmpPath = tmpName;
#endif
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(out);
#ifdef _WIN32
            remove(tmpPath.c_str());
#else
            unlink(tmpPath.c_str());
#endif
            return {};
        }
    }
    fclose(out);
    return tmpPath;
}
