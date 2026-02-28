#pragma once
#include <vector>
#include <string>

#include "interface_builder.hpp" // REGISTER_STRUCT, REGISTER_VECTOR, FIELD

// Matrix rain animation state
struct MatrixColumn
{
    float pos;
    float speed;
    int trail;
    std::string chars;
};

extern std::vector<MatrixColumn> matrix_columns;

REGISTER_STRUCT(MatrixColumn, "MatrixColumn",
                FIELD(MatrixColumn, pos, Float, nullptr),
                FIELD(MatrixColumn, speed, Float, nullptr),
                FIELD(MatrixColumn, trail, Int, nullptr),
                FIELD(MatrixColumn, chars, String, nullptr))

REGISTER_VECTOR(MatrixColumn, Struct, get_struct_info<MatrixColumn>())
