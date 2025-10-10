#pragma once
#include <string>
#include <cstdio>
#include <array>

// Generate a visually distinct color for mesh index i
std::array<double, 3> generateDistinctColor(int i);

// Read up to nbytes from file stream
std::string readHeader(FILE *f, size_t nbytes = 200);
// Read up to nbytes from filename
std::string readHeader(const std::string& filename, size_t nbytes = 200);

// Write all of stdin to a newly created temp file, return temp file path or empty on error
std::string stdinToTempFile();
