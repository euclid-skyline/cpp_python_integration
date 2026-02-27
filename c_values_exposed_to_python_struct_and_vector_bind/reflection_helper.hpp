#pragma once

#include <vector>
#include <string>
#include <cstddef>
#include "reflection_value.hpp"  // ValueType, BoundValue
#include "reflection_struct.hpp" // StructInfo, FieldInfo
#include "reflection_vector.hpp" // VectorInfo

// ==========================================================================
// REFLECTION HELPER MACROS AND TEMPLATES
//
// Purpose: Simplify metadata creation for StructInfo and VectorInfo
//          Reduce boilerplate through macros and helper templates
// ==========================================================================

// ---------------------------------------------------------
// PART 1: REFLECT_STRUCT MACRO
// ---------------------------------------------------------
// Generates StructInfo, is_reflected_struct<>, and get_struct_info<> specializations
//
// Usage Examples:
//   struct Player { int health; float speed; };
//   REFLECT_STRUCT(Player, "Player",
//       FIELD(Player, health, Int, nullptr),
//       FIELD(Player, speed, Float, nullptr)
//   )
//
//   struct Team { std::vector<int> scores; float average; };
//   REFLECT_STRUCT(Team, "Team",
//       FIELD(Team, scores, Vector, get_vector_info<int>()),
//       FIELD(Team, average, Float, nullptr)
//   )

// ---------------------------------------------------------
// HELPER: Field Entry Constructor
// ---------------------------------------------------------
// Simplifies FieldInfo initialization in REFLECT_STRUCT
//
// Usage:
//   FIELD(Player, health, Int, nullptr)
//   FIELD(Player, speed, Float, nullptr)

#define FIELD(struct_type, field_name, value_type_enum, meta_ptr) \
    {#field_name, offsetof(struct_type, field_name), ValueType::value_type_enum, meta_ptr}

#define REFLECT_STRUCT(struct_type, struct_name_str, ...)    \
    static StructInfo struct_type##Info = {                  \
        struct_name_str,                                     \
        {__VA_ARGS__}};                                      \
    template <>                                              \
    struct is_reflected_struct<struct_type> : std::true_type \
    {                                                        \
    };                                                       \
    template <>                                              \
    inline const StructInfo *get_struct_info<struct_type>()  \
    {                                                        \
        return &struct_type##Info;                           \
    }

// ---------------------------------------------------------
// PART 2: GENERIC VECTOR OPERATIONS (Template-Based)
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

// ---------------------------------------------------------
// PART 3: VECTORINFO BUILDER FUNCTION
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
// PART 4: REFLECT_VECTOR MACRO
// ---------------------------------------------------------
// One-liner to register a vector type's traits
//
// Usage Examples:
//   REFLECT_VECTOR(int, Int, nullptr)
//   REFLECT_VECTOR(Enemy, Struct, &EnemyInfo)
//   REFLECT_VECTOR(std::vector<int>, Vector, get_vector_info<std::vector<int>>())

#define REFLECT_VECTOR(element_type, value_type_enum, element_meta_ptr)   \
    template <>                                                           \
    inline const VectorInfo *get_vector_info<element_type>()              \
    {                                                                     \
        return make_vector_info<element_type>(ValueType::value_type_enum, \
                                              element_meta_ptr);          \
    }

#endif // REFLECTION_HELPER_HPP
