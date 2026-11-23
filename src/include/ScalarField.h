#pragma once

#include <string>

enum class FieldAssociation { Point, Cell };

struct ScalarField {
  std::string name;
  FieldAssociation association = FieldAssociation::Point;
};
