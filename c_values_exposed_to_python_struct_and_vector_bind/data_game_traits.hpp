#pragma once
#include <vector>
#include <string>

#include "value_interface.hpp"    // BoundValue, ValueType

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
const StructInfo *get_struct_info<Player>()
{
    return &PlayerInfo;
}

// 2. Vector of simple types
std::vector<int> scores;
// Meta info for vector of ints
static VectorInfo IntVectorInfo = {
    ValueType::Int,
    nullptr};
// Specialize the trait to mark std::vector<int> as a reflected vector
template <>
const VectorInfo *get_vector_info<int>()
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
const StructInfo *get_struct_info<Team>()
{
    return &TeamInfo;
}

// 4) Vector containing structs
struct Enemy
{
    int health;
    float x;
};

std::vector<Enemy> enemies;
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
    &EnemyInfo};
// Specialize the trait to mark std::vector<Enemy> as a reflected vector
template <>
struct is_reflected_struct<Enemy> : std::true_type
{
};

template <>
const StructInfo *get_struct_info<Enemy>()
{
    return &EnemyInfo;
}

template <>
const VectorInfo *get_vector_info<Enemy>()
{
    return &EnemyVectorInfo;
}

// 5) Vector containing vectors
std::vector<std::vector<int>> grid;
// Metadata for inner vector
// static VectorInfo IntVectorInfo = { // Issue: duplicate symbol IntVectorInfo
//     ValueType::Int,
//     nullptr};
// Metadata for outer vector
static VectorInfo VectorOfIntVectorInfo = {
    ValueType::Vector,
    &IntVectorInfo};
// Specialize the trait to mark std::vector<std::vector<int>> as a reflected vector
template <>
const VectorInfo *get_vector_info<std::vector<int>>()
{
    return &IntVectorInfo;
}

template <>
const VectorInfo *get_vector_info<std::vector<std::vector<int>>>()
{
    return &VectorOfIntVectorInfo;
}

//-------------------------------------------------------
