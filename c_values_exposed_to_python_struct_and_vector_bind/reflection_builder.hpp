#pragma once

#include <vector>
#include <cstddef>
#include "reflection_vector.hpp" // VectorInfo

// ==========================================================================
// REFLECTION METADATA BUILDER
//
// Purpose: Template functions and helpers to create StructInfo and VectorInfo
//          Pure C++ - no dependencies on value_interface or Python
// ==========================================================================

// ---------------------------------------------------------
// PART 1: GENERIC VECTOR OPERATIONS (Template-Based)
// ---------------------------------------------------------
// These templates work for ANY std::vector<T>
// Function pointers are generated automatically for each type

template <typename T>
std::size_t generic_vec_size(void *vec_ptr)
{
    if (!vec_ptr)
        return 0;
    return static_cast<std::vector<T> *>(vec_ptr)->size();
}

template <typename T>
void *generic_vec_element_ptr(void *vec_ptr, std::size_t index)
{
    if (!vec_ptr)
        return nullptr;
    auto *vec = static_cast<std::vector<T> *>(vec_ptr);
    if (index >= vec->size())
        return nullptr;
    return &(*vec)[index];
}

template <typename T>
bool generic_vec_append(void *vec_ptr, void *value_ptr)
{
    if (!vec_ptr || !value_ptr)
        return false;
    static_cast<std::vector<T> *>(vec_ptr)->push_back(*static_cast<T *>(value_ptr));
    return true;
}

template <typename T>
void *generic_vec_create_empty()
{
    return new std::vector<T>();
}

template <typename T>
void generic_vec_destroy(void *vec_ptr)
{
    if (!vec_ptr)
        return;
    delete static_cast<std::vector<T> *>(vec_ptr);
}

template <typename T>
void generic_struct_construct(void *ptr)
{
    new (ptr) T();
}

template <typename T>
void generic_struct_destruct(void *ptr)
{
    static_cast<T *>(ptr)->~T();
}

// ---------------------------------------------------------
// PART 2: VECTORINFO BUILDER FUNCTION
// ---------------------------------------------------------
// Constructs a VectorInfo with all function pointers auto-filled

template <typename ElementType>
const VectorInfo *make_vector_info(ValueType element_type, const void *element_meta = nullptr)
{
    static VectorInfo info = {
        element_type,
        element_meta,
        generic_vec_size<ElementType>,
        generic_vec_element_ptr<ElementType>,
        generic_vec_append<ElementType>,
        generic_vec_create_empty<ElementType>,
        generic_vec_destroy<ElementType>};
    return &info;
}

// ---------------------------------------------------------
// PART 3: HELPER - Field Entry Constructor
// ---------------------------------------------------------
// Simplifies FieldInfo initialization
//
// Usage:
//   FIELD(Player, health, Int, nullptr)
//   FIELD(Player, speed, Float, nullptr)

#define FIELD(struct_type, field_name, value_type_enum, meta_ptr) \
    {#field_name, offsetof(struct_type, field_name), ValueType::value_type_enum, meta_ptr}
