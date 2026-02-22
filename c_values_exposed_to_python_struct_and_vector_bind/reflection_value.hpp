#pragma once
#include <cstddef> // std::byte
#include <string>  // std::string

enum class ValueType
{
    Int,
    Float,
    Bool,
    String,
    Struct,
    Vector
};

struct BoundValue
{
    std::string name;
    ValueType type; // Or use "virtual ValueType type() const = 0;" if you prefer a virtual method for type info
    virtual ~BoundValue() = default;
};

using ByteBool = std::byte; // std::vector<bool> specialization uses bit-packing, so we use std::byte to store raw bits
#define TRUE_BYTE static_cast<std::byte>(0x01)
#define FALSE_BYTE static_cast<std::byte>(0x00)
