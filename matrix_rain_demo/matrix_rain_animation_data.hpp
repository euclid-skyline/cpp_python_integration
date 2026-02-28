#pragma once
#include <vector>
#include <string>

#include "interface_builder.hpp" // REGISTER_STRUCT, REGISTER_VECTOR, FIELD

// Matrix rain animation state
struct MatrixColumn
{
    float pos;         // Current head position on Y axis (row), updated every frame
    float speed;       // Falling speed in rows per frame
    int trail;         // Number of characters in this column trail
    std::string chars; // Characters currently rendered for the trail (length should match trail)
};

extern std::vector<MatrixColumn> matrix_columns;

REGISTER_STRUCT(MatrixColumn, "MatrixColumn",
                FIELD(MatrixColumn, pos, Float, nullptr),
                FIELD(MatrixColumn, speed, Float, nullptr),
                FIELD(MatrixColumn, trail, Int, nullptr),
                FIELD(MatrixColumn, chars, String, nullptr))

REGISTER_VECTOR(MatrixColumn, Struct, get_struct_info<MatrixColumn>())
