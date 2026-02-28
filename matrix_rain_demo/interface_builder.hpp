#pragma once

#include "value_interface.hpp"    // is_reflected_struct, get_struct_info, get_vector_info
#include "reflection_builder.hpp" // make_vector_info, FIELD

// ==========================================================================
// VALUE INTERFACE REGISTRATION MACROS
//
// Purpose: Macros to register types with the value_interface layer
//          Creates trait specializations and metadata in one call
// ==========================================================================

// ---------------------------------------------------------
// PART 1: REGISTER_STRUCT MACRO
// ---------------------------------------------------------
// Generates StructInfo, is_reflected_struct<>, and get_struct_info<> specializations
//
// Usage Examples:
//   struct Player { int health; float speed; };
//   REGISTER_STRUCT(Player, "Player",
//       FIELD(Player, health, Int, nullptr),
//       FIELD(Player, speed, Float, nullptr)
//   )
//
//   struct Team { std::vector<int> scores; float average; };
//   REGISTER_STRUCT(Team, "Team",
//       FIELD(Team, scores, Vector, get_vector_info<int>()),
//       FIELD(Team, average, Float, nullptr)
//   )

#define REGISTER_STRUCT(struct_type, struct_name_str, ...)   \
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
// PART 2: REGISTER_VECTOR MACRO
// ---------------------------------------------------------
// One-liner to register a vector type's traits
//
// Usage Examples:
//   REGISTER_VECTOR(int, Int, nullptr)
//   REGISTER_VECTOR(Enemy, Struct, &EnemyInfo)
//   REGISTER_VECTOR(std::vector<int>, Vector, get_vector_info<std::vector<int>>())

#define REGISTER_VECTOR(element_type, value_type_enum, element_meta_ptr)  \
    template <>                                                           \
    inline const VectorInfo *get_vector_info<element_type>()              \
    {                                                                     \
        return make_vector_info<element_type>(ValueType::value_type_enum, \
                                              element_meta_ptr);          \
    }
