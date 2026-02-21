#pragma once
#include <vector>
#include <string>
#include <cstddef> // for std::size_t

#include "value_interface.hpp" // BoundValue, ValueType

// Forward declarations for function pointers
std::size_t int_vec_size(void *ptr);
void *int_vec_element_ptr(void *ptr, std::size_t idx);
bool int_vec_append(void *ptr, void *val);

std::size_t enemy_vec_size(void *ptr);
void *enemy_vec_element_ptr(void *ptr, std::size_t idx);
bool enemy_vec_append(void *ptr, void *val);

std::size_t grid_vec_size(void *ptr);
void *grid_vec_element_ptr(void *ptr, std::size_t idx);
bool grid_vec_append(void *ptr, void *val);

// 1. Simple struct
struct Player
{
    int health;
    float speed;
};
// Meta info for Player struct
static StructInfo PlayerInfo = {
    "Player",
    {
        {"health", offsetof(Player, health), ValueType::Int, nullptr},
        {"speed", offsetof(Player, speed), ValueType::Float, nullptr},
    }};
// Specialize the trait to mark Player as a reflected struct
template <>
struct is_reflected_struct<Player> : std::true_type
{
};

template <>
inline const StructInfo *get_struct_info<Player>()
{
    return &PlayerInfo;
}

// 2. Vector of simple types
extern std::vector<int> scores;
// Meta info for vector of ints
static VectorInfo IntVectorInfo = {
    ValueType::Int,
    nullptr,
    int_vec_size,
    int_vec_element_ptr,
    int_vec_append};
// Specialize the trait to mark std::vector<int> as a reflected vector
template <>
inline const VectorInfo *get_vector_info<int>()
{
    return &IntVectorInfo;
}

// 3) Struct containing a vector
struct Team
{
    std::vector<int> scores;
    float average;
};
// Metadata for inner vector
// static VectorInfo IntVectorInfo = { // Issue: duplicate symbol IntVectorInfo
//     ValueType::Int,
//     nullptr};
// Metadata for struct fields
static StructInfo TeamInfo = {
    "Team",
    {
        {"scores", offsetof(Team, scores), ValueType::Vector, &IntVectorInfo},
        {"average", offsetof(Team, average), ValueType::Float, nullptr},
    }};
// Specialize the trait to mark Team as a reflected struct
template <>
struct is_reflected_struct<Team> : std::true_type
{
};

template <>
inline const StructInfo *get_struct_info<Team>()
{
    return &TeamInfo;
}

// 4) Vector containing structs
struct Enemy
{
    int health;
    float x;
};

extern std::vector<Enemy> enemies;
// Metadata for Enemy struct
static StructInfo EnemyInfo = {
    "Enemy",
    {
        {"health", offsetof(Enemy, health), ValueType::Int, nullptr},
        {"x", offsetof(Enemy, x), ValueType::Float, nullptr},
    }};
// Metadata for vector of Enemy structs
static VectorInfo EnemyVectorInfo = {
    ValueType::Struct,
    &EnemyInfo,
    enemy_vec_size,
    enemy_vec_element_ptr,
    enemy_vec_append};
// Specialize the trait to mark std::vector<Enemy> as a reflected vector
template <>
struct is_reflected_struct<Enemy> : std::true_type
{
};

template <>
inline const StructInfo *get_struct_info<Enemy>()
{
    return &EnemyInfo;
}

template <>
inline const VectorInfo *get_vector_info<Enemy>()
{
    return &EnemyVectorInfo;
}

// 5) Vector containing vectors
extern std::vector<std::vector<int>> grid;
// Metadata for inner vector
// static VectorInfo IntVectorInfo = { // Issue: duplicate symbol IntVectorInfo
//     ValueType::Int,
//     nullptr};
// Metadata for outer vector
static VectorInfo VectorOfIntVectorInfo = {
    ValueType::Vector,
    &IntVectorInfo,
    grid_vec_size,
    grid_vec_element_ptr,
    grid_vec_append};

// Specialize get_vector_info for the element type of grid's outer vector
// grid is std::vector<std::vector<int>>, so its element type is std::vector<int>
template <>
inline const VectorInfo *get_vector_info<std::vector<int>>()
{
    return &VectorOfIntVectorInfo;
}

//-------------------------------------------------------
