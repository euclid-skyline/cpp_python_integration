#pragma once

// ==========================================================================
// INCLUDES
// ==========================================================================
#include <vector>                // std::vector - STL container for dynamic arrays
#include <cstddef>               // std::size_t - standard size type
#include <stdexcept>             // std::invalid_argument, std::out_of_range
                                
#include "reflection_vector.hpp" // VectorInfo - reflection metadata for vectors

// ==========================================================================
// REFLECTION METADATA BUILDER
//
// Purpose: Template functions and helpers to create StructInfo and VectorInfo
//          Pure C++ - no dependencies on value_interface or Python
//
// Error Handling Strategy (Updated March 2026):
//   - Functions throw C++ exceptions instead of returning error codes
//   - Boundary layer (python_proxy.cpp) catches and converts to Python errors
//   - Maintains language-agnostic design (works with Python, Lua, Ruby, etc.)
//   - Destructors and cleanup functions marked noexcept
// ==========================================================================

// ---------------------------------------------------------
// PART 1: GENERIC VECTOR OPERATIONS (Template-Based)
// ---------------------------------------------------------
// These templates work for ANY std::vector<T>
// Function pointers are generated automatically for each type

// Query operation - never throws, marked noexcept for optimization
// Returns 0 for null pointer (safe default)
template <typename T>
std::size_t generic_vec_size(void *vec_ptr) noexcept
{
    if (!vec_ptr)
        return 0;
    return static_cast<std::vector<T> *>(vec_ptr)->size();
}

// Access element by index - throws exceptions instead of returning nullptr
// Modified: Changed from returning nullptr on error to throwing exceptions
// Rationale: Prevents segfaults from null dereference, provides clear error messages
// Throws: std::invalid_argument if vec_ptr is null
//         std::out_of_range if index >= size()
template <typename T>
void *generic_vec_element_ptr(void *vec_ptr, std::size_t index)
{
    if (!vec_ptr)
        throw std::invalid_argument("vec_ptr is null");
    auto *vec = static_cast<std::vector<T> *>(vec_ptr);
    if (index >= vec->size())
        throw std::out_of_range("Index out of bounds");
    return &(*vec)[index];
}

// Append element to vector - throws exceptions instead of returning bool
// Modified: Changed from bool return type to void, throws on error
// Rationale: Exception provides meaningful error info, boundary layer can catch/convert
// Throws: std::invalid_argument if pointers are null
//         std::bad_alloc if memory allocation fails (from push_back)
//         Copy constructor exceptions (propagate naturally)
template <typename T>
void generic_vec_append(void *vec_ptr, void *value_ptr)
{
    if (!vec_ptr || !value_ptr)
        throw std::invalid_argument("vec_ptr or value_ptr is null");
    // push_back may throw std::bad_alloc or copy constructor exceptions
    static_cast<std::vector<T> *>(vec_ptr)->push_back(*static_cast<T *>(value_ptr));
}

template <typename T>
void *generic_vec_create_empty()
{
    return new std::vector<T>();
}

// Destroy vector - cleanup operation, marked noexcept
// Modified: Added noexcept specification
// Rationale: Cleanup operations should never throw
template <typename T>
void generic_vec_destroy(void *vec_ptr) noexcept
{
    if (!vec_ptr)
        return;
    delete static_cast<std::vector<T> *>(vec_ptr);
}

// Construct struct in-place using placement new
// Modified: Added null pointer validation
// Rationale: Placement new on null pointer is undefined behavior
// Throws: std::invalid_argument if ptr is null
//         T's constructor exceptions (propagate naturally)
template <typename T>
void generic_struct_construct(void *ptr)
{
    if (!ptr)
        throw std::invalid_argument("ptr is null");
    // T's constructor may throw exceptions - they propagate naturally
    new (ptr) T();
}

// Destruct struct - must never throw (destructor contract)
// Modified: Added noexcept and try-catch wrapper
// Rationale: Throwing from destructor during stack unwinding causes std::terminate()
//            noexcept ensures compile-time enforcement
// Note: catch block should never execute with well-behaved types
template <typename T>
void generic_struct_destruct(void *ptr) noexcept
{
    try
    {
        if (ptr)
        {
            static_cast<T *>(ptr)->~T();
        }
    }
    catch (...)
    {
        // Destructors must never throw - suppress all exceptions
        // This should never happen with well-behaved types
    }
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
