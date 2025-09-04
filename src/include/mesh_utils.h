#pragma once
#include <string>
#include <cstdio>
#include <array>

// Reads up to maxBytes from in, writes to tmp file, returns temp file name and detected format ("xml", "vtk", or "unknown")
std::string detectFormatFromStream(FILE *in, std::string &tmpFileOut, std::string &detectedFormatOut, size_t maxBytes = 200);

// Generate a visually distinct color for mesh index i
std::array<double, 3> generateDistinctColor(int i);
